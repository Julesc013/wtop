/* MSI ownership preflight. SPDX-License-Identifier: GPL-3.0-only
 * Callback: UINT WINAPI(MSIHANDLE), synchronous MSI execution thread. All native
 * handles belong to this invocation and are retired before return. No process,
 * background worker, file modification or registry mutation is performed here.
 * This detects pre-existing conflicts; the gap before MSI's own file operation
 * is NOT an atomic compare-and-delete/replace. Promotion remains blocked. */
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <wincrypt.h>
#include <string.h>
#include <wchar.h>

typedef struct wtop_guard_file {
    const WCHAR *name;
    DWORD size;
    const BYTE *digest;
} wtop_guard_file;
#include "guard_config.h"

#define GUARD_PATH 1024

static UINT refuse(MSIHANDLE install, const WCHAR *reason)
{
    MSIHANDLE record = MsiCreateRecord(1);
    if (record) {
        MsiRecordSetStringW(record, 0, L"Wtop candidate ownership check refused the operation: [1]");
        MsiRecordSetStringW(record, 1, reason);
        MsiProcessMessage(install, INSTALLMESSAGE_ERROR, record);
        MsiCloseHandle(record);
    }
    return ERROR_INSTALL_FAILURE;
}

static int property(MSIHANDLE install, const WCHAR *name, WCHAR *value, DWORD capacity)
{
    DWORD length = capacity;
    if (MsiGetPropertyW(install, name, value, &length) != ERROR_SUCCESS || length >= capacity)
        return 0;
    value[length] = 0;
    return 1;
}

static int canonical(const WCHAR *path, WCHAR result[GUARD_PATH])
{
    DWORD count;
    size_t length;
    /* This candidate accepts local, fully qualified drive paths only. UNC and
       device namespaces need separate installer/filesystem qualification. */
    if (!path || !((path[0] >= L'A' && path[0] <= L'Z') ||
                  (path[0] >= L'a' && path[0] <= L'z')) || path[1] != L':' || path[2] != L'\\')
        return 0;
    count = GetFullPathNameW(path, GUARD_PATH, result, NULL);
    if (!count || count >= GUARD_PATH) return 0;
    length = wcslen(result);
    while (length > 3 && result[length - 1] == L'\\') result[--length] = 0;
    return length > 3;
}

static int safe_parents(const WCHAR *directory)
{
    WCHAR path[GUARD_PATH];
    size_t i, length = wcslen(directory);
    DWORD attributes, error;
    WCHAR saved;
    if (length >= GUARD_PATH) return 0;
    memcpy(path, directory, (length + 1) * sizeof(WCHAR));
    for (i = 3; i <= length; ++i) {
        if (i != length && path[i] != L'\\') continue;
        saved = path[i]; path[i] = 0;
        attributes = GetFileAttributesW(path);
        error = GetLastError();
        path[i] = saved;
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 1;
            return 0;
        }
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
    }
    return 1;
}

static int registry_string(HKEY key, const WCHAR *name, WCHAR *value, DWORD capacity)
{
    DWORD bytes = capacity * (DWORD)sizeof(WCHAR), type = 0;
    LONG result = RegQueryValueExW(key, name, NULL, &type, (BYTE *)value, &bytes);
    if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(WCHAR) ||
        bytes > capacity * sizeof(WCHAR) || bytes % sizeof(WCHAR)) return 0;
    if (value[bytes / sizeof(WCHAR) - 1] != 0) return 0;
    return (wcslen(value) + 1) * sizeof(WCHAR) == bytes;
}

static int owner_matches(HKEY key, const WCHAR *directory, const WCHAR *product)
{
    WCHAR value[GUARD_PATH], normalized[GUARD_PATH];
    DWORD schema, type, count = sizeof(schema);
    if (RegQueryValueExW(key, L"SchemaVersion", NULL, &type, (BYTE *)&schema, &count) != ERROR_SUCCESS ||
        type != REG_DWORD || count != sizeof(schema) || schema != 1) return 0;
    if (!registry_string(key, L"InstallPath", value, GUARD_PATH) ||
        !canonical(value, normalized) || lstrcmpiW(normalized, directory)) return 0;
    if (!registry_string(key, L"ProductCode", value, GUARD_PATH) || lstrcmpiW(value, product)) return 0;
    if (!registry_string(key, L"Owner", value, GUARD_PATH) || wcscmp(value, L"msi")) return 0;
    if (!registry_string(key, L"PayloadSha256", value, GUARD_PATH) || wcscmp(value, wtop_guard_payload_hex)) return 0;
    return 1;
}

static int file_matches(const WCHAR *path, const wtop_guard_file *expected, int installed)
{
    HANDLE file;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE buffer[16384], digest[32];
    DWORD read_count, digest_size = sizeof(digest), error;
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER size;
    int result = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    /* Fresh installation never adopts an existing payload, even identical bytes. */
    if (!installed) goto done;
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        info.nNumberOfLinks != 1 || !GetFileSizeEx(file, &size) || size.QuadPart != (LONGLONG)expected->size)
        goto done;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
        !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) goto done;
    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &read_count, NULL)) goto done;
        if (!read_count) break;
        if (!CryptHashData(hash, buffer, read_count, 0)) goto done;
    }
    if (CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0) && digest_size == sizeof(digest) &&
        !memcmp(digest, expected->digest, sizeof(digest))) result = 1;
done:
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return result;
}

__declspec(dllexport) UINT WINAPI WtopGuard(MSIHANDLE install)
{
    WCHAR raw[GUARD_PATH], directory[GUARD_PATH], path[GUARD_PATH];
    WCHAR installed_value[32], all_users[32], product[64];
    DWORD count = GUARD_PATH;
    HKEY key;
    LONG opened;
    int installed;
    size_t i, length;
    if (!property(install, L"Installed", installed_value, 32) ||
        !property(install, L"ALLUSERS", all_users, 32) ||
        !property(install, L"ProductCode", product, 64) ||
        MsiGetTargetPathW(install, L"INSTALLFOLDER", raw, &count) != ERROR_SUCCESS ||
        !canonical(raw, directory) || !safe_parents(directory))
        return refuse(install, L"The destination is unavailable, redirected or outside the qualified local path profile.");
    installed = installed_value[0] != 0;
    opened = RegOpenKeyExW(!wcscmp(all_users, L"1") ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                           wtop_guard_registry_key, 0, KEY_QUERY_VALUE, &key);
    if (opened == ERROR_SUCCESS) {
        int matches = installed && owner_matches(key, directory, product);
        RegCloseKey(key);
        if (!matches) return refuse(install, L"The installation record is foreign, modified or inconsistent.");
    } else if (installed || (opened != ERROR_FILE_NOT_FOUND && opened != ERROR_PATH_NOT_FOUND))
        return refuse(install, L"The expected installation owner cannot be verified.");
    length = wcslen(directory);
    for (i = 0; i < sizeof(wtop_guard_files) / sizeof(wtop_guard_files[0]); ++i) {
        if (length + 1 + wcslen(wtop_guard_files[i].name) >= GUARD_PATH)
            return refuse(install, L"The destination path exceeds the candidate bound.");
        memcpy(path, directory, length * sizeof(WCHAR)); path[length] = L'\\';
        memcpy(path + length + 1, wtop_guard_files[i].name,
               (wcslen(wtop_guard_files[i].name) + 1) * sizeof(WCHAR));
        if (!file_matches(path, &wtop_guard_files[i], installed))
            return refuse(install, L"An owned file changed, a foreign file occupies its name, or the file cannot be safely inspected. Preserve it and resolve the conflict before retrying.");
    }
    return ERROR_SUCCESS;
}
