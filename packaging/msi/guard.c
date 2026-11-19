/* MSI ownership checks. SPDX-License-Identifier: GPL-3.0-only
 * Callback: UINT WINAPI(MSIHANDLE), synchronous MSI execution thread. All native
 * handles belong to this invocation and are retired before return. No process,
 * background worker, file modification or registry mutation is performed here.
 * The immediate check schedules deferred rechecks before owned registry removal
 * and before file installation. The removal check needs the existing owner.
 * Neither is an atomic compare-and-delete/replace against a hostile same-user
 * actor. Machine scope is additionally confined to protected Program Files.
 * Promotion remains blocked until that residual boundary is qualified. */
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <wincrypt.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#ifdef _MSC_VER
/* Windows SDK restartmanager.h declares an anonymous union. Keep /W4 /WX
   enabled for our C90 code; isolate this SDK-only extension at its include. */
#pragma warning(push)
#pragma warning(disable: 4201)
#endif
#include <restartmanager.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
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

static int privileged_sid(PSID sid)
{
    PSID installer = NULL;
    int trusted;
    if (IsWellKnownSid(sid, WinLocalSystemSid) ||
        IsWellKnownSid(sid, WinBuiltinAdministratorsSid)) return 1;
    /* Windows Modules Installer service SID, not a localized account name. */
    if (!ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", &installer)) return 0;
    trusted = EqualSid(sid, installer) != 0;
    LocalFree(installer);
    return trusted;
}

static int protected_directory(const WCHAR *path)
{
    HANDLE handle;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    PSID owner = NULL;
    PACL acl = NULL;
    DWORD result;
    DWORD i;
    int secure = 0;
    const DWORD writes = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
        FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE | WRITE_DAC |
        WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
    handle = CreateFileW(path, READ_CONTROL | FILE_READ_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    result = GetSecurityInfo(handle, SE_FILE_OBJECT,
                             OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                             &owner, NULL, &acl, NULL, &descriptor);
    if (result != ERROR_SUCCESS || !owner || !privileged_sid(owner) || !acl) goto done;
    for (i = 0; i < acl->AceCount; ++i) {
        ACE_HEADER *header;
        ACCESS_ALLOWED_ACE *ace;
        if (!GetAce(acl, i, (void **)&header)) goto done;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        /* Unknown allow/object ACL semantics are outside this candidate's
           protected-root profile; never guess that an ACE is harmless. */
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) goto done;
        ace = (ACCESS_ALLOWED_ACE *)header;
        /* Program Files legitimately grants inherit-only CREATOR OWNER rights
           to objects created by the privileged installer. Other inherited
           write grants would make new payloads writable by an ordinary user. */
        if ((header->AceFlags & INHERIT_ONLY_ACE) &&
            IsWellKnownSid((PSID)&ace->SidStart, WinCreatorOwnerSid)) continue;
        if ((ace->Mask & writes) && !privileged_sid((PSID)&ace->SidStart)) goto done;
    }
    secure = 1;
done:
    if (descriptor) LocalFree(descriptor);
    CloseHandle(handle);
    return secure;
}

static int machine_destination(const WCHAR *directory)
{
    WCHAR root[GUARD_PATH], normalized[GUARD_PATH], path[GUARD_PATH];
    size_t start, length, i;
    DWORD attributes, error;
    WCHAR saved;
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, root) != S_OK ||
        !canonical(root, normalized)) return 0;
    start = wcslen(normalized); length = wcslen(directory);
    if (length <= start || _wcsnicmp(directory, normalized, start) || directory[start] != L'\\') return 0;
    memcpy(path, directory, (length + 1) * sizeof(WCHAR));
    for (i = start; i <= length; ++i) {
        if (i != length && path[i] != L'\\') continue;
        saved = path[i]; path[i] = 0;
        attributes = GetFileAttributesW(path); error = GetLastError();
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            path[i] = saved;
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !protected_directory(path)) return 0;
        path[i] = saved;
    }
    return 1;
}

static int no_running_users(const WCHAR *path)
{
    DWORD session = 0, result, reasons = 0;
    UINT count = 0, needed = 0;
    WCHAR key[CCH_RM_SESSION_KEY + 1];
    LPCWSTR files[1];
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        result = GetLastError();
        return result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND;
    }
    if (RmStartSession(&session, 0, key) != ERROR_SUCCESS) return 0;
    files[0] = path;
    result = RmRegisterResources(session, 1, files, 0, NULL, 0, NULL);
    if (result == ERROR_SUCCESS) result = RmGetList(session, &needed, &count, NULL, &reasons);
    RmEndSession(session);
    /* No enumeration retry, shutdown request, restart or process termination. */
    return result == ERROR_SUCCESS && needed == 0 && count == 0;
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

static int owner_matches(HKEY key, const WCHAR *directory, const WCHAR *product, int upgrading)
{
    WCHAR value[GUARD_PATH], normalized[GUARD_PATH];
    DWORD schema, type, count = sizeof(schema);
    if (RegQueryValueExW(key, L"SchemaVersion", NULL, &type, (BYTE *)&schema, &count) != ERROR_SUCCESS ||
        type != REG_DWORD || count != sizeof(schema) || schema != 1) return 0;
    if (!registry_string(key, L"InstallPath", value, GUARD_PATH) ||
        !canonical(value, normalized) || lstrcmpiW(normalized, directory)) return 0;
    if (!registry_string(key, L"ProductCode", value, GUARD_PATH) || lstrcmpiW(value, product)) return 0;
    if (!registry_string(key, L"Owner", value, GUARD_PATH) || wcscmp(value, L"msi")) return 0;
    if (!registry_string(key, L"PayloadSha256", value, GUARD_PATH) ||
        (!upgrading && wcscmp(value, wtop_guard_payload_hex))) return 0;
    if (upgrading) {
        DWORD size = GUARD_PATH;
        /* Require the old product's cached MSI. Its own guard, not an
           untrusted registry hash, will authorize removal of its old bytes. */
        if (MsiGetProductInfoW(product, INSTALLPROPERTY_LOCALPACKAGE, value, &size) != ERROR_SUCCESS ||
            !value[0] || GetFileAttributesW(value) == INVALID_FILE_ATTRIBUTES) return 0;
    }
    return 1;
}

static int owner_names_absent(HKEY key)
{
    static const WCHAR *names[] = { L"SchemaVersion", L"InstallPath", L"Owner", L"ProductCode", L"PayloadSha256" };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (RegQueryValueExW(key, names[i], NULL, NULL, NULL, NULL) != ERROR_FILE_NOT_FOUND) return 0;
    }
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

static UINT check_payload(MSIHANDLE install, const WCHAR *directory,
                          const WCHAR *product, int installed, int machine,
                          const WCHAR *older_product)
{
    WCHAR path[GUARD_PATH];
    HKEY key;
    LONG opened;
    int upgrading = older_product && older_product[0];
    size_t i, length;
    if (!safe_parents(directory) || (machine && !machine_destination(directory)))
        return refuse(install, L"The destination is unavailable, redirected or outside the qualified local path profile.");
    opened = RegOpenKeyExW(machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                           wtop_guard_registry_key, 0, KEY_QUERY_VALUE, &key);
    if (opened == ERROR_SUCCESS) {
        int matches = (installed || upgrading) ?
            owner_matches(key, directory, upgrading ? older_product : product, upgrading) : owner_names_absent(key);
        RegCloseKey(key);
        if (!matches) return refuse(install, L"The installation record is foreign, modified or inconsistent.");
    } else if (installed || upgrading || (opened != ERROR_FILE_NOT_FOUND && opened != ERROR_PATH_NOT_FOUND))
        return refuse(install, L"The expected installation owner cannot be verified.");
    length = wcslen(directory);
    for (i = 0; i < sizeof(wtop_guard_files) / sizeof(wtop_guard_files[0]); ++i) {
        if (length + 1 + wcslen(wtop_guard_files[i].name) >= GUARD_PATH)
            return refuse(install, L"The destination path exceeds the candidate bound.");
        memcpy(path, directory, length * sizeof(WCHAR)); path[length] = L'\\';
        memcpy(path + length + 1, wtop_guard_files[i].name,
               (wcslen(wtop_guard_files[i].name) + 1) * sizeof(WCHAR));
        if (!no_running_users(path))
            return refuse(install, L"A payload file is in use or its users cannot be checked. Close Wtop before maintenance; this installer never closes it or schedules a reboot.");
        if (!upgrading && !file_matches(path, &wtop_guard_files[i], installed))
            return refuse(install, L"An owned file changed, a foreign file occupies its name, or the file cannot be safely inspected. Preserve it and resolve the conflict before retrying.");
    }
    return ERROR_SUCCESS;
}

__declspec(dllexport) UINT WINAPI WtopGuard(MSIHANDLE install)
{
    WCHAR raw[GUARD_PATH], directory[GUARD_PATH], data[GUARD_PATH + 100];
    WCHAR installed_value[32], all_users[32], product[64], older[64];
    DWORD count = GUARD_PATH;
    UINT result;
    int installed, machine;
    size_t product_length, path_length;
    if (!property(install, L"Installed", installed_value, 32) ||
        !property(install, L"ALLUSERS", all_users, 32) ||
        !property(install, L"ProductCode", product, 64) ||
        !property(install, L"WIX_UPGRADE_DETECTED", older, 64) ||
        wcschr(older, L';') || wcschr(product, L'|') || wcslen(product) != 38 ||
        MsiGetTargetPathW(install, L"INSTALLFOLDER", raw, &count) != ERROR_SUCCESS ||
        !canonical(raw, directory) || wcschr(directory, L'|'))
        return refuse(install, L"Invalid destination, scope or ambiguous upgrade owner.");
    installed = installed_value[0] != 0; machine = !wcscmp(all_users, L"1");
    result = check_payload(install, directory, product, installed, machine, installed ? NULL : older);
    if (result != ERROR_SUCCESS) return result;
    /* ProductCode is bounded and the path cannot contain the delimiter. No
       untrusted CustomActionData is interpreted as an MSI command or code. */
    data[0] = installed ? L'1' : L'0'; data[1] = L'|';
    data[2] = machine ? L'1' : L'0'; data[3] = L'|';
    product_length = wcslen(product); path_length = wcslen(directory);
    memcpy(data + 4, product, product_length * sizeof(WCHAR));
    data[4 + product_length] = L'|';
    memcpy(data + 5 + product_length, directory, (path_length + 1) * sizeof(WCHAR));
    if (MsiSetPropertyW(install, L"RecheckBeforeRemove", data) != ERROR_SUCCESS ||
        MsiSetPropertyW(install, L"RecheckBeforeInstall", data) != ERROR_SUCCESS)
        return refuse(install, L"Could not bind the deferred ownership recheck.");
    return ERROR_SUCCESS;
}

__declspec(dllexport) UINT WINAPI WtopGuardDeferred(MSIHANDLE install)
{
    WCHAR data[GUARD_PATH + 100], directory[GUARD_PATH], *product, *path;
    if (!property(install, L"CustomActionData", data, GUARD_PATH + 100) ||
        (data[0] != L'0' && data[0] != L'1') || data[1] != L'|' ||
        (data[2] != L'0' && data[2] != L'1') || data[3] != L'|')
        return refuse(install, L"Missing deferred ownership context.");
    product = data + 4; path = wcschr(product, L'|');
    if (!path) return refuse(install, L"Invalid deferred ownership context.");
    *path++ = 0;
    if (wcslen(product) != 38 || wcschr(path, L'|') || !canonical(path, directory))
        return refuse(install, L"Invalid deferred path or product identity.");
    /* For an upgrade the old MSI has now removed its payload within the
       rollback transaction, so this must pass the fresh-install rules. */
    return check_payload(install, directory, product, data[0] == L'1', data[2] == L'1', NULL);
}
