/* Native NT settings ownership and cooperative save transaction.
 * SPDX-License-Identifier: GPL-3.0-only */
#include "settings_store_nt.h"
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define PATH_BYTES ((size_t)WTOP_STORE_PATH_CHARS * sizeof(WCHAR))
#define REGISTERED_PROFILES_LIMIT 128

typedef DWORD (WINAPI *final_path_fn)(HANDLE, LPWSTR, DWORD, DWORD);
typedef HRESULT (WINAPI *folder_path_fn)(HWND, int, HANDLE, DWORD, LPWSTR);
static LONG temporary_sequence;
#ifdef WTOP_STORE_TESTING
static wtop_store_replace_fn test_replace = ReplaceFileW;
static wtop_store_flush_fn test_flush = FlushFileBuffers;
void wtop_store_test_operations(wtop_store_replace_fn replace, wtop_store_flush_fn flush)
{
    test_replace = replace ? replace : ReplaceFileW;
    test_flush = flush ? flush : FlushFileBuffers;
}
#define STORE_REPLACE test_replace
#define STORE_FLUSH test_flush
#else
#define STORE_REPLACE ReplaceFileW
#define STORE_FLUSH FlushFileBuffers
#endif

static int finish(wtop_store_result *result, wtop_store_outcome outcome,
                  DWORD code, const char *message)
{
    result->outcome = outcome;
    result->native_error = code;
    result->message = message;
    return outcome == WTOP_STORE_OK;
}
static wtop_store_outcome io_outcome(DWORD code)
{
    if (code == ERROR_ACCESS_DENIED || code == ERROR_WRITE_PROTECT) return WTOP_STORE_DENIED;
    if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) return WTOP_STORE_BUSY;
    return WTOP_STORE_UNCHANGED_FAILURE;
}
static int io_failure(wtop_store_result *result, DWORD code, const char *message)
{
    return finish(result, io_outcome(code), code, message);
}
const char *wtop_store_owner_name(wtop_store_owner owner)
{
    static const char *names[] = { "memory", "explicit", "portable", "installed-user", "installed-machine", "unregistered" };
    return owner >= WTOP_STORE_MEMORY && owner <= WTOP_STORE_UNREGISTERED ? names[owner] : "unknown";
}
const char *wtop_store_source_name(wtop_store_source source)
{
    static const char *names[] = { "compiled", "explicit-file", "portable-file", "adjacent-file", "legacy-file", "user-file", "machine-defaults" };
    return source >= WTOP_STORE_COMPILED && source <= WTOP_STORE_MACHINE_DEFAULTS ? names[source] : "unknown";
}
const char *wtop_store_outcome_name(wtop_store_outcome outcome)
{
    static const char *names[] = { "ok", "conflict", "busy", "denied", "invalid", "unavailable", "unchanged-failure", "committed-unverified", "recovery-required" };
    return outcome >= WTOP_STORE_OK && outcome <= WTOP_STORE_RECOVERY_REQUIRED ? names[outcome] : "unknown";
}
void wtop_store_dispose(wtop_settings_store *store)
{
    if (!store) return;
    free(store->expected_bytes);
    memset(store, 0, sizeof(*store));
}
static int copy_path(WCHAR *target, const WCHAR *source)
{
    size_t length = wcslen(source);
    if (length >= WTOP_STORE_PATH_CHARS) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return 0; }
    memmove(target, source, (length + 1) * sizeof(WCHAR));
    return 1;
}
static int append_path(WCHAR *target, const WCHAR *suffix)
{
    size_t current = wcslen(target), added = wcslen(suffix);
    if (current + added >= WTOP_STORE_PATH_CHARS) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return 0; }
    memcpy(target + current, suffix, (added + 1) * sizeof(WCHAR));
    return 1;
}
static int parent_path(WCHAR *path)
{
    WCHAR *last = wcsrchr(path, L'\\');
    if (!last) { SetLastError(ERROR_BAD_PATHNAME); return 0; }
    /* Keep the separator for a drive root, including \\?\C:\. */
    if (last > path && last[-1] == L':') last[1] = 0;
    else *last = 0;
    return 1;
}
static int child_path(WCHAR *target, const WCHAR *directory, const WCHAR *name)
{
    size_t length;
    if (!copy_path(target, directory)) return 0;
    length = wcslen(target);
    if (length && target[length - 1] != L'\\' && !append_path(target, L"\\")) return 0;
    return append_path(target, name);
}
static final_path_fn final_path_api(void)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    return kernel ? (final_path_fn)(void *)GetProcAddress(kernel, "GetFinalPathNameByHandleW") : NULL;
}
/* Resolve existing files through their handles; for absent files resolve their
 * parent directory. This handles case, short names and supported reparse aliases.
 * Hard-link aliases are refused separately, because a pathname lock cannot name
 * all links and replacing one link would detach it from the others. */
static int canonical_path(const WCHAR *input, WCHAR *output)
{
    WCHAR *full = (WCHAR *)malloc(PATH_BYTES);
    WCHAR *tail = (WCHAR *)malloc(PATH_BYTES);
    DWORD length, failure;
    HANDLE handle;
    final_path_fn final = final_path_api();
    int missing = 0, ok = 0;
    if (!full || !tail) { free(full); free(tail); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    length = GetFullPathNameW(input, WTOP_STORE_PATH_CHARS, full, NULL);
    if (!length || length >= WTOP_STORE_PATH_CHARS) goto done;
    if (!final) { ok = copy_path(output, full); goto done; }
    handle = CreateFileW(full, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        failure = GetLastError();
        if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND) goto done;
        if (!copy_path(tail, wcsrchr(full, L'\\') ? wcsrchr(full, L'\\') + 1 : L"") || !tail[0] || !parent_path(full)) goto done;
        missing = 1;
        handle = CreateFileW(full, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        /* A missing selected user subdirectory is resolved from an already
         * canonical known folder by the caller; preserve the absolute spelling. */
        if (handle == INVALID_HANDLE_VALUE) {
            failure = GetLastError();
            if (failure == ERROR_FILE_NOT_FOUND || failure == ERROR_PATH_NOT_FOUND) {
                if (!child_path(output, full, tail)) goto done;
                ok = 1;
            }
            goto done;
        }
    }
    length = final(handle, output, WTOP_STORE_PATH_CHARS, 0 /* normalized DOS path */);
    failure = GetLastError();
    CloseHandle(handle);
    SetLastError(failure);
    if (!length || length >= WTOP_STORE_PATH_CHARS) goto done;
    if (missing) {
        if (wcslen(output) && output[wcslen(output) - 1] != L'\\' && !append_path(output, L"\\")) goto done;
        if (!append_path(output, tail)) goto done;
    }
    ok = 1;
done:
    failure = GetLastError();
    free(full); free(tail);
    SetLastError(failure);
    return ok;
}
static int utf8_path(const char *input, WCHAR *output)
{
    WCHAR *wide;
    int count, ok;
    DWORD failure;
    if (!input || !*input) { SetLastError(ERROR_BAD_PATHNAME); return 0; }
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, NULL, 0);
    if (count <= 0 || count > WTOP_STORE_PATH_CHARS) return 0;
    wide = (WCHAR *)malloc((size_t)count * sizeof(WCHAR));
    if (!wide) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    ok = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, wide, count) && canonical_path(wide, output);
    failure = GetLastError(); free(wide); SetLastError(failure);
    return ok;
}
/* 0 absent, 1 file, 2 directory, -1 another error (never bypass it). */
static int path_presence(const WCHAR *path)
{
    DWORD attributes = GetFileAttributesW(path), failure;
    if (attributes != INVALID_FILE_ATTRIBUTES) return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 2 : 1;
    failure = GetLastError();
    return failure == ERROR_FILE_NOT_FOUND || failure == ERROR_PATH_NOT_FOUND ? 0 : -1;
}
static int folder_path(int folder, WCHAR *output)
{
    WCHAR library[MAX_PATH + 32], value[MAX_PATH];
    UINT length;
    HMODULE shell;
    folder_path_fn resolve;
    HRESULT status;
    length = GetSystemDirectoryW(library, MAX_PATH);
    if (!length || length >= MAX_PATH) return 0;
    wcscat(library, L"\\shell32.dll");
    shell = LoadLibraryW(library);
    if (!shell) return 0;
    resolve = (folder_path_fn)(void *)GetProcAddress(shell, "SHGetFolderPathW");
    value[0] = 0;
    status = resolve ? resolve(NULL, folder /* no CREATE flag */, NULL, 0, value) : E_NOTIMPL;
    FreeLibrary(shell);
    if (FAILED(status) || !value[0]) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0; }
    return canonical_path(value, output);
}
static int registered_owner(HKEY root, const WCHAR *module_directory)
{
    HKEY installations, record;
    DWORD index, name_length, type, bytes, version;
    WCHAR name[256];
    WCHAR *value = (WCHAR *)malloc(PATH_BYTES), *canonical = (WCHAR *)malloc(PATH_BYTES);
    LONG status;
    int found = 0;
    if (!value || !canonical) { free(value); free(canonical); return 0; }
    status = RegOpenKeyExW(root, L"Software\\Wtop\\Installations", 0, KEY_READ, &installations);
    if (status != ERROR_SUCCESS) goto done;
    for (index = 0; index < REGISTERED_PROFILES_LIMIT; ++index) {
        name_length = sizeof(name) / sizeof(name[0]);
        status = RegEnumKeyExW(installations, index, name, &name_length, NULL, NULL, NULL, NULL);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS || RegOpenKeyExW(installations, name, 0, KEY_READ, &record) != ERROR_SUCCESS) continue;
        bytes = sizeof(version); type = 0; version = 0;
        status = RegQueryValueExW(record, L"SchemaVersion", NULL, &type, (LPBYTE)&version, &bytes);
        if (status == ERROR_SUCCESS && type == REG_DWORD && bytes == sizeof(version) && version == 1) {
            bytes = (DWORD)PATH_BYTES; type = 0; value[0] = 0;
            status = RegQueryValueExW(record, L"InstallPath", NULL, &type, (LPBYTE)value, &bytes);
            if (status == ERROR_SUCCESS && type == REG_SZ && bytes >= 2 * sizeof(WCHAR) &&
                bytes <= PATH_BYTES && bytes % sizeof(WCHAR) == 0 && value[bytes / sizeof(WCHAR) - 1] == 0 &&
                ((value[0] && value[1] == L':') || (value[0] == L'\\' && value[1] == L'\\')) &&
                canonical_path(value, canonical) && !_wcsicmp(canonical, module_directory)) found = 1;
        }
        RegCloseKey(record);
        if (found) break;
    }
    RegCloseKey(installations);
done:
    free(value); free(canonical);
    return found;
}
static int select_file(wtop_settings_store *store, const WCHAR *path, wtop_store_source source,
                       wtop_store_result *result)
{
    if (!canonical_path(path, store->read_path)) return io_failure(result, GetLastError(), "cannot resolve selected settings file");
    store->source = source;
    return 1;
}
int wtop_store_resolve(wtop_settings_store *store, const wtop_store_request *request,
                        wtop_store_result *result)
{
    WCHAR *directory, *candidate;
    DWORD length, failure;
    int presence, portable, ok = 0;
    memset(result, 0, sizeof(*result));
    if (!store || !request || store->expected_bytes || store->module_path[0])
        return finish(result, WTOP_STORE_INVALID, ERROR_INVALID_PARAMETER, "settings owner must be zero-initialized before resolve");
    store->disabled = request->no_config;
    if (request->no_config) return finish(result, WTOP_STORE_OK, 0, "file settings disabled");
    directory = (WCHAR *)malloc(PATH_BYTES); candidate = (WCHAR *)malloc(PATH_BYTES);
    if (!directory || !candidate) { free(directory); free(candidate); return io_failure(result, ERROR_NOT_ENOUGH_MEMORY, "cannot allocate settings paths"); }
    store->canonical_aliases = final_path_api() != NULL;
    length = GetModuleFileNameW(NULL, candidate, WTOP_STORE_PATH_CHARS);
    if (!length || length >= WTOP_STORE_PATH_CHARS || !canonical_path(candidate, store->module_path) ||
        !copy_path(directory, store->module_path) || !parent_path(directory)) goto path_failure;
    store->owner = WTOP_STORE_UNREGISTERED;
    if (request->explicit_path) {
        store->owner = WTOP_STORE_EXPLICIT;
        store->explicit_required = 1;
        if (!utf8_path(request->explicit_path, store->write_path) ||
            !select_file(store, store->write_path, WTOP_STORE_SELECTED_FILE, result)) goto path_failure;
        ok = 1; goto done;
    }
    portable = request->portable || request->portable_root != NULL;
    if (request->portable_root && !utf8_path(request->portable_root, directory)) goto path_failure;
    if (!portable) {
        if (!child_path(candidate, directory, L"wtop.portable")) goto path_failure;
        presence = path_presence(candidate);
        if (presence < 0) goto path_failure;
        portable = presence == 1;
    }
    if (portable) store->owner = WTOP_STORE_PORTABLE;
    else if (registered_owner(HKEY_CURRENT_USER, directory)) store->owner = WTOP_STORE_INSTALLED_USER;
    else if (registered_owner(HKEY_LOCAL_MACHINE, directory)) store->owner = WTOP_STORE_INSTALLED_MACHINE;
    if (portable || store->owner == WTOP_STORE_UNREGISTERED) {
        if (!child_path(candidate, directory, L"wtop.conf") || !canonical_path(candidate, store->write_path)) goto path_failure;
        presence = path_presence(store->write_path);
        if (presence < 0) goto path_failure;
        if (presence) {
            ok = select_file(store, store->write_path, portable ? WTOP_STORE_PORTABLE_FILE : WTOP_STORE_ADJACENT_FILE, result); goto done;
        }
        if (!child_path(candidate, directory, L"ntop.conf")) goto path_failure;
        presence = path_presence(candidate);
        if (presence < 0) goto path_failure;
        if (presence) { ok = select_file(store, candidate, WTOP_STORE_LEGACY_FILE, result); goto done; }
        if (portable) { ok = 1; goto done; }
    }
    store->write_path[0] = 0;
    if (folder_path(0x001a /* CSIDL_APPDATA */, candidate)) {
        if (!child_path(store->user_directory, candidate, L"Wtop") ||
            !canonical_path(store->user_directory, directory) || !copy_path(store->user_directory, directory) ||
            !child_path(candidate, directory, L"wtop.conf") || !canonical_path(candidate, store->write_path)) goto path_failure;
        presence = path_presence(store->write_path);
        if (presence < 0) goto path_failure;
        if (presence) { ok = select_file(store, store->write_path, WTOP_STORE_USER_FILE, result); goto done; }
    }
    if ((store->owner == WTOP_STORE_INSTALLED_USER || store->owner == WTOP_STORE_INSTALLED_MACHINE) &&
        folder_path(0x0023 /* CSIDL_COMMON_APPDATA */, directory)) {
        if (!child_path(candidate, directory, L"Wtop\\wtop.conf")) goto path_failure;
        presence = path_presence(candidate);
        if (presence < 0) goto path_failure;
        if (presence) { ok = select_file(store, candidate, WTOP_STORE_MACHINE_DEFAULTS, result); goto done; }
    }
    ok = 1;
    goto done;
path_failure:
    failure = GetLastError();
    io_failure(result, failure ? failure : ERROR_BAD_PATHNAME, "cannot resolve settings source or destination");
done:
    free(directory); free(candidate);
    if (ok) finish(result, WTOP_STORE_OK, 0, store->write_path[0] ? "settings destination resolved; writes remain explicit" : "settings available in memory; no destination resolved");
    return ok;
}
/* Bounded exact bytes, with absence distinct from an empty file. The handle
 * excludes writing while the read is in progress; it is released before commit. */
static int read_bytes(const WCHAR *path, char **bytes, size_t *length, int *exists,
                      wtop_store_result *result)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    char *buffer;
    DWORD count, code;
    size_t used = 0;
    *bytes = NULL; *length = 0; *exists = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return 1;
        return io_failure(result, code, "cannot read settings revision");
    }
    if (GetFileType(file) != FILE_TYPE_DISK) { CloseHandle(file); return finish(result, WTOP_STORE_UNAVAILABLE, ERROR_NOT_SUPPORTED, "settings require a disk file"); }
    if (!GetFileInformationByHandle(file, &info)) { code = GetLastError(); CloseHandle(file); return io_failure(result, code, "cannot inspect settings identity"); }
    if (info.nNumberOfLinks > 1 || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(file);
        return finish(result, WTOP_STORE_UNAVAILABLE, ERROR_NOT_SUPPORTED, "settings directories and hard-link destinations are not supported");
    }
    buffer = (char *)malloc((size_t)WTOP_CONFIG_MAX_BYTES + 1);
    if (!buffer) { CloseHandle(file); return io_failure(result, ERROR_NOT_ENOUGH_MEMORY, "cannot allocate settings revision"); }
    for (;;) {
        if (!ReadFile(file, buffer + used, (DWORD)((size_t)WTOP_CONFIG_MAX_BYTES + 1 - used), &count, NULL)) {
            code = GetLastError(); free(buffer); CloseHandle(file); return io_failure(result, code, "cannot read complete settings revision");
        }
        used += count;
        if (used > WTOP_CONFIG_MAX_BYTES) { free(buffer); CloseHandle(file); return finish(result, WTOP_STORE_INVALID, ERROR_FILE_TOO_LARGE, "settings revision exceeds 32768 bytes"); }
        if (!count) break;
    }
    if (!CloseHandle(file)) { code = GetLastError(); free(buffer); return io_failure(result, code, "cannot close settings revision"); }
    buffer[used] = 0; *bytes = buffer; *length = used; *exists = 1;
    return 1;
}
static int capture_baseline(wtop_settings_store *store, wtop_store_result *result)
{
    char *bytes; size_t length; int exists;
    if (!store->write_path[0]) { store->baseline_loaded = 1; return 1; }
    if (!read_bytes(store->write_path, &bytes, &length, &exists, result)) return 0;
    free(store->expected_bytes); store->expected_bytes = bytes; store->expected_length = length;
    store->expected_exists = exists; store->baseline_loaded = 1;
    return 1;
}
static int same_revision(wtop_settings_store *store, wtop_store_result *result)
{
    char *bytes; size_t length; int exists, same;
    if (!read_bytes(store->write_path, &bytes, &length, &exists, result)) return 0;
    same = exists == store->expected_exists && length == store->expected_length &&
        (!length || !memcmp(bytes, store->expected_bytes, length));
    free(bytes);
    if (!same) return finish(result, WTOP_STORE_CONFLICT, ERROR_REVISION_MISMATCH, "settings changed since load; reload or export to another path");
    return 1;
}
int wtop_store_load(wtop_settings_store *store, wtop_settings *settings,
                     wtop_config_document *document, wtop_store_result *result)
{
    HANDLE handle;
    int descriptor, ok;
    FILE *file;
    wtop_settings candidate = *settings;
    wtop_config_document parsed = {0};
    const char *error = NULL;
    DWORD failure;
    BY_HANDLE_FILE_INFORMATION info;
    memset(result, 0, sizeof(*result));
    if (store->disabled) return finish(result, WTOP_STORE_OK, 0, "file settings disabled");
    if (!store->read_path[0]) {
        if (!capture_baseline(store, result)) return 0;
        return finish(result, WTOP_STORE_OK, 0, "compiled defaults selected");
    }
    handle = CreateFileW(store->read_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) return io_failure(result, GetLastError(), "cannot open selected settings file");
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.nNumberOfLinks > 1) {
        CloseHandle(handle);
        return finish(result, WTOP_STORE_UNAVAILABLE, ERROR_NOT_SUPPORTED, "selected settings must be a disk file with one link");
    }
    descriptor = _open_osfhandle((INT_PTR)handle, _O_RDONLY | _O_BINARY);
    if (descriptor < 0) { CloseHandle(handle); return io_failure(result, ERROR_NOT_ENOUGH_MEMORY, "cannot open settings stream"); }
    file = _fdopen(descriptor, "rb");
    if (!file) { _close(descriptor); return io_failure(result, ERROR_NOT_ENOUGH_MEMORY, "cannot open settings stream"); }
    ok = wtop_settings_read(file, &candidate, &parsed, &result->error_line, &error);
    if (fclose(file) && ok) { ok = 0; error = "cannot close settings stream"; }
    if (!ok) { wtop_config_document_free(&parsed); return finish(result, WTOP_STORE_INVALID, ERROR_INVALID_DATA, error ? error : "invalid settings document"); }
    if (!capture_baseline(store, result)) { wtop_config_document_free(&parsed); return 0; }
    /* A read-to-baseline race must not silently accept a document that was not
     * applied. Different source/target is intentional for legacy or machine copy. */
    if (!_wcsicmp(store->read_path, store->write_path) &&
        (!store->expected_exists || store->expected_length != parsed.length ||
         (parsed.length && memcmp(store->expected_bytes, parsed.original, parsed.length)))) {
        wtop_config_document_free(&parsed); store->baseline_loaded = 0;
        return finish(result, WTOP_STORE_CONFLICT, ERROR_REVISION_MISMATCH, "settings changed while loading; reload before editing");
    }
    failure = ERROR_SUCCESS;
    *settings = candidate; wtop_config_document_free(document); *document = parsed;
    return finish(result, WTOP_STORE_OK, failure, "settings document loaded without writing");
}
int wtop_store_export_target(wtop_settings_store *store, const char *path,
                             wtop_store_result *result)
{
    memset(result, 0, sizeof(*result));
    if (!store || store->expected_bytes || store->write_path[0]) return finish(result, WTOP_STORE_INVALID, ERROR_INVALID_PARAMETER, "export owner must be zero-initialized");
    store->owner = WTOP_STORE_EXPLICIT; store->source = WTOP_STORE_SELECTED_FILE;
    store->canonical_aliases = final_path_api() != NULL;
    if (!utf8_path(path, store->write_path)) return io_failure(result, GetLastError(), "cannot resolve explicit export path");
    if (!capture_baseline(store, result)) return 0;
    if (store->expected_exists) {
        wtop_settings previous;
        wtop_config_document original = {0};
        int valid;
        /* Explicit export replaces supported configuration documents, not an
         * unknown future schema or malformed document. Validate privately;
         * never apply the destination's values to the caller's live settings. */
        wtop_settings_defaults(&previous);
        copy_path(store->read_path, store->write_path);
        valid = wtop_store_load(store, &previous, &original, result);
        store->read_path[0] = 0;
        wtop_config_document_free(&original);
        if (!valid) { store->baseline_loaded = 0; return 0; }
    }
    return finish(result, WTOP_STORE_OK, 0, "explicit export destination selected");
}
static HANDLE create_sibling(const WCHAR *target, const WCHAR *kind, WCHAR *path)
{
    WCHAR suffix[96];
    DWORD attempt;
    LONG sequence;
    HANDLE handle;
    for (attempt = 0; attempt < 16; ++attempt) {
        sequence = InterlockedIncrement(&temporary_sequence);
        _snwprintf(suffix, sizeof(suffix) / sizeof(suffix[0]), L".%ls-%lu-%lu-%lu", kind,
                   (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(), (unsigned long)sequence);
        suffix[(sizeof(suffix) / sizeof(suffix[0])) - 1] = 0;
        if (!copy_path(path, target) || !append_path(path, suffix)) { path[0] = 0; return INVALID_HANDLE_VALUE; }
        handle = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE) return handle;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) { path[0] = 0; return INVALID_HANDLE_VALUE; }
    }
    path[0] = 0;
    SetLastError(ERROR_FILE_EXISTS);
    return INVALID_HANDLE_VALUE;
}
int wtop_store_save(wtop_settings_store *store, const wtop_settings *settings,
                     wtop_config_document *document, unsigned long captured_revision,
                     wtop_store_result *result)
{
    char *candidate = NULL, *baseline = NULL, *verified = NULL;
    size_t length = 0, verified_length;
    int verified_exists, keep_backup, replaced = 0, ok = 0;
    int own_temporary = 0, own_backup = 0;
    const char *error = NULL;
    WCHAR *lock_path;
    HANDLE lock = INVALID_HANDLE_VALUE, temporary = INVALID_HANDLE_VALUE, backup = INVALID_HANDLE_VALUE;
    DWORD failure, written;
    int i;
    memset(result, 0, sizeof(*result));
    if (store->disabled || !store->write_path[0]) return finish(result, WTOP_STORE_UNAVAILABLE, ERROR_NOT_SUPPORTED, "no implicit save destination; use an explicit export");
    if (!store->baseline_loaded) return finish(result, WTOP_STORE_INVALID, ERROR_INVALID_DATA, "load the settings baseline before saving");
    if (!wtop_settings_serialize(settings, document, &candidate, &length, &error)) return finish(result, WTOP_STORE_INVALID, ERROR_INVALID_DATA, error);
    baseline = (char *)malloc(length + 1); lock_path = (WCHAR *)malloc(PATH_BYTES);
    if (!baseline || !lock_path) { free(candidate); free(baseline); free(lock_path); return io_failure(result, ERROR_NOT_ENOUGH_MEMORY, "cannot allocate save transaction"); }
    memcpy(baseline, candidate, length + 1);
    result->migrated = document && document->original && document->schema_version < WTOP_CONFIG_VERSION;
    keep_backup = result->migrated && store->expected_exists;
    if (store->user_directory[0] && !CreateDirectoryW(store->user_directory, NULL)) {
        failure = GetLastError();
        if (failure != ERROR_ALREADY_EXISTS || path_presence(store->user_directory) != 2) { io_failure(result, failure, "cannot create selected user settings directory"); goto done; }
    }
    if (!copy_path(lock_path, store->write_path) || !append_path(lock_path, L".wtop-lock")) {
        io_failure(result, GetLastError(), "settings lock path exceeds capacity"); goto done;
    }
    /* One attempt: a cooperating writer never waits behind another writer.
     * CREATE_NEW protects foreign sentinels; DELETE_ON_CLOSE removes our lock on
     * ordinary exit or process death. A crash of the filesystem can leave a
     * stale name, which is reported busy and never automatically deleted. */
    lock = CreateFileW(lock_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (lock == INVALID_HANDLE_VALUE) {
        failure = GetLastError();
        if (failure == ERROR_FILE_EXISTS || failure == ERROR_ALREADY_EXISTS || failure == ERROR_SHARING_VIOLATION)
            finish(result, WTOP_STORE_BUSY, failure, "settings destination is locked; retry after its writer finishes (a stale lock requires inspection)");
        else io_failure(result, failure, "cannot acquire settings destination lock");
        goto done;
    }
    if (!same_revision(store, result)) goto done;
    store->recovery_temporary[0] = store->recovery_backup[0] = 0;
    temporary = create_sibling(store->write_path, L"wtop-tmp", store->recovery_temporary);
    if (temporary == INVALID_HANDLE_VALUE) { io_failure(result, GetLastError(), "cannot create exclusive sibling settings candidate"); goto done; }
    own_temporary = 1;
    SetLastError(ERROR_SUCCESS);
    if (!WriteFile(temporary, candidate, (DWORD)length, &written, NULL) || written != length || !STORE_FLUSH(temporary)) {
        failure = GetLastError(); io_failure(result, failure ? failure : ERROR_WRITE_FAULT, "cannot write and flush complete settings candidate"); goto done;
    }
    if (!CloseHandle(temporary)) { temporary = INVALID_HANDLE_VALUE; io_failure(result, GetLastError(), "cannot close settings candidate"); goto done; }
    temporary = INVALID_HANDLE_VALUE;
    if (!same_revision(store, result)) goto done;
    if (store->expected_exists) {
        backup = create_sibling(store->write_path, keep_backup ? L"migration-original" : L"wtop-backup", store->recovery_backup);
        if (backup == INVALID_HANDLE_VALUE) { io_failure(result, GetLastError(), "cannot reserve previous settings backup"); goto done; }
        own_backup = 1;
        if (!CloseHandle(backup)) { backup = INVALID_HANDLE_VALUE; io_failure(result, GetLastError(), "cannot close reserved settings backup"); goto done; }
        backup = INVALID_HANDLE_VALUE;
        /* Do not use REPLACEFILE_IGNORE_MERGE_ERRORS or IGNORE_ACL_ERRORS.
         * WRITE_THROUGH is unsupported for ReplaceFile. The backup reservation
         * is owned by this transaction and must never overwrite an existing name. */
        replaced = STORE_REPLACE(store->write_path, store->recovery_temporary, store->recovery_backup, 0, NULL, NULL);
        if (!replaced) {
            failure = GetLastError(); store->baseline_loaded = 0;
            finish(result, WTOP_STORE_RECOVERY_REQUIRED, failure,
                   "replacement failed; inspect destination, candidate and backup before retrying (names may have moved)");
            goto done;
        }
    } else {
        replaced = MoveFileExW(store->recovery_temporary, store->write_path, MOVEFILE_WRITE_THROUGH);
        if (!replaced) {
            failure = GetLastError();
            if (failure == ERROR_FILE_EXISTS || failure == ERROR_ALREADY_EXISTS)
                finish(result, WTOP_STORE_CONFLICT, failure, "settings destination was created by another writer");
            else {
                /* A remote filesystem can return an uncertain move outcome.
                 * Preserve the candidate/name evidence instead of claiming the
                 * previously absent destination is necessarily unchanged. */
                store->baseline_loaded = 0;
                finish(result, WTOP_STORE_RECOVERY_REQUIRED, failure,
                       "new-file move failed; inspect destination and retained candidate before retrying");
            }
            goto done;
        }
    }
    result->committed = 1; result->saved_revision = captured_revision;
    if (!read_bytes(store->write_path, &verified, &verified_length, &verified_exists, result) ||
        !verified_exists || verified_length != length || (length && memcmp(verified, candidate, length))) {
        failure = result->native_error; store->baseline_loaded = 0;
        finish(result, WTOP_STORE_COMMITTED_UNVERIFIED, failure ? failure : ERROR_REVISION_MISMATCH,
               "replacement succeeded but the exact candidate could not be verified; reload or inspect retained backup");
        goto done;
    }
    free(store->expected_bytes); store->expected_bytes = baseline; baseline = NULL;
    store->expected_length = length; store->expected_exists = 1;
    if (document) {
        wtop_config_document_free(document); document->original = candidate; candidate = NULL;
        document->length = length; document->schema_version = WTOP_CONFIG_VERSION; document->tab_count = settings->tab_count;
        for (i = 0; i < settings->tab_count; ++i) document->tab_ids[i] = settings->tabs[i].id;
    }
    store->recovery_temporary[0] = 0;
    if (!keep_backup && store->recovery_backup[0]) {
        if (!DeleteFileW(store->recovery_backup)) {
            /* The candidate is verified, but further saves must wait until the
             * retained backup is reviewed/reloaded; never accumulate unchecked
             * backup files after repeated cleanup failures. */
            store->baseline_loaded = 0;
            finish(result, WTOP_STORE_OK, GetLastError(), "settings saved and verified; previous backup retained because cleanup failed");
            ok = 1; goto done;
        }
        store->recovery_backup[0] = 0;
    }
    if (result->migrated && !keep_backup && store->read_path[0] && _wcsicmp(store->read_path, store->write_path))
        copy_path(store->recovery_backup, store->read_path); /* original source remains untouched */
    copy_path(store->read_path, store->write_path);
    if (store->source == WTOP_STORE_LEGACY_FILE)
        store->source = store->owner == WTOP_STORE_PORTABLE ? WTOP_STORE_PORTABLE_FILE : WTOP_STORE_ADJACENT_FILE;
    else if (store->source == WTOP_STORE_MACHINE_DEFAULTS || store->source == WTOP_STORE_COMPILED)
        store->source = store->user_directory[0] ? WTOP_STORE_USER_FILE : WTOP_STORE_PORTABLE_FILE;
    finish(result, WTOP_STORE_OK, 0, result->migrated ? "settings migrated and verified; original retained" : "settings saved and verified");
    ok = 1;
done:
    if (temporary != INVALID_HANDLE_VALUE) CloseHandle(temporary);
    if (backup != INVALID_HANDLE_VALUE) CloseHandle(backup);
    /* Before any replacement attempt the destination is unchanged and our own
     * candidate may be removed. After a failed ReplaceFile, never guess which
     * path contains the only recoverable copy. */
    if (!replaced && result->outcome != WTOP_STORE_RECOVERY_REQUIRED) {
        if (own_temporary && store->recovery_temporary[0] && DeleteFileW(store->recovery_temporary)) store->recovery_temporary[0] = 0;
        if (own_backup && store->recovery_backup[0] && DeleteFileW(store->recovery_backup)) store->recovery_backup[0] = 0;
    }
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    free(candidate); free(baseline); free(verified); free(lock_path);
    return ok;
}
