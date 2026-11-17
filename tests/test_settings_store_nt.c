/* Native settings transaction behavioral fixtures. No install ownership claim.
 * SPDX-License-Identifier: GPL-3.0-only */
#include "settings_store_nt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static WCHAR directory[MAX_PATH];
static WCHAR target[MAX_PATH];
static char utf8_target[MAX_PATH * 4];
static const char legacy[] = "ConfigVersion 1\nKeymap vi\nTab0.Future marker\n";
static DWORD replace_error;
static int change_after_replace;

static void write_file(const WCHAR *path, const char *text)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written;
    assert(file != INVALID_HANDLE_VALUE);
    assert(WriteFile(file, text, (DWORD)strlen(text), &written, NULL));
    assert(written == strlen(text));
    assert(CloseHandle(file));
}
static char *file_bytes(const WCHAR *path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD count;
    char *bytes = (char *)malloc((size_t)WTOP_CONFIG_MAX_BYTES + 1);
    assert(file != INVALID_HANDLE_VALUE && bytes);
    assert(ReadFile(file, bytes, (DWORD)WTOP_CONFIG_MAX_BYTES, &count, NULL));
    bytes[count] = 0;
    assert(CloseHandle(file));
    return bytes;
}
static void remove_files(void)
{
    WIN32_FIND_DATAW data;
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    HANDLE search;
    swprintf(pattern, MAX_PATH, L"%ls\\*", directory);
    search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            swprintf(path, MAX_PATH, L"%ls\\%ls", directory, data.cFileName);
            assert(DeleteFileW(path));
        }
    } while (FindNextFileW(search, &data));
    assert(FindClose(search));
}
static wtop_settings_store *load_explicit(wtop_settings *settings, wtop_config_document *document)
{
    wtop_settings_store *store = (wtop_settings_store *)calloc(1, sizeof(*store));
    wtop_store_request request = {0};
    wtop_store_result result;
    assert(store);
    request.explicit_path = utf8_target;
    assert(wtop_store_resolve(store, &request, &result));
    assert(store->owner == WTOP_STORE_EXPLICIT);
    assert(wtop_store_load(store, settings, document, &result));
    return store;
}
static void dispose(wtop_settings_store *store, wtop_config_document *document)
{
    wtop_store_dispose(store); free(store); wtop_config_document_free(document);
}
static void migration_and_no_hidden_save(void)
{
    wtop_settings settings;
    wtop_config_document document = {0};
    wtop_settings_store *store;
    wtop_store_result result;
    char *bytes;
    WCHAR retained[MAX_PATH];
    write_file(target, legacy); wtop_settings_defaults(&settings);
    store = load_explicit(&settings, &document);
    assert(settings.keymap == WTOP_KEYMAP_VI && document.schema_version == 1);
    bytes = file_bytes(target); assert(!strcmp(bytes, legacy)); free(bytes);
    settings.keymap = WTOP_KEYMAP_EMACS;
    assert(wtop_store_save(store, &settings, &document, 17, &result));
    assert(result.outcome == WTOP_STORE_OK && result.committed && result.migrated && result.saved_revision == 17);
    assert(document.schema_version == 2 && store->recovery_backup[0]);
    bytes = file_bytes(store->recovery_backup); assert(!strcmp(bytes, legacy)); free(bytes);
    assert(wcslen(store->recovery_backup) < MAX_PATH); wcscpy(retained, store->recovery_backup);
    bytes = file_bytes(target); assert(strstr(bytes, "ConfigVersion 2\n") && strstr(bytes, "Keymap emacs\n")); free(bytes);
    settings.keymap = WTOP_KEYMAP_HTOP;
    assert(wtop_store_save(store, &settings, &document, 18, &result));
    assert(!result.migrated && result.saved_revision == 18);
    bytes = file_bytes(retained); assert(!strcmp(bytes, legacy)); free(bytes);
    dispose(store, &document); remove_files();
}
static void conflict_lock_and_flush(void)
{
    wtop_settings settings;
    wtop_config_document document = {0};
    wtop_settings_store *store;
    wtop_store_result result;
    WCHAR lock_path[MAX_PATH];
    HANDLE lock;
    char *bytes;
    write_file(target, legacy); wtop_settings_defaults(&settings); store = load_explicit(&settings, &document);
    write_file(target, "# external writer\n");
    settings.keymap = WTOP_KEYMAP_HTOP;
    assert(!wtop_store_save(store, &settings, &document, 2, &result));
    assert(result.outcome == WTOP_STORE_CONFLICT && !result.committed && document.schema_version == 1);
    bytes = file_bytes(target); assert(!strcmp(bytes, "# external writer\n")); free(bytes);
    write_file(target, legacy);
    swprintf(lock_path, MAX_PATH, L"%ls.wtop-lock", target);
    write_file(lock_path, "foreign sentinel");
    assert(!wtop_store_save(store, &settings, &document, 3, &result));
    assert(result.outcome == WTOP_STORE_BUSY);
    bytes = file_bytes(lock_path); assert(!strcmp(bytes, "foreign sentinel")); free(bytes);
    assert(DeleteFileW(lock_path));
    lock = CreateFileW(lock_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    assert(lock != INVALID_HANDLE_VALUE);
    assert(!wtop_store_save(store, &settings, &document, 4, &result));
    assert(result.outcome == WTOP_STORE_BUSY);
    assert(CloseHandle(lock));
    assert(wtop_store_save(store, &settings, &document, 5, &result));
    dispose(store, &document); remove_files();
}
static BOOL WINAPI failed_flush(HANDLE file)
{
    (void)file; SetLastError(ERROR_DISK_FULL); return FALSE;
}
static BOOL WINAPI replacement_fixture(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup,
                                       DWORD flags, LPVOID exclude, LPVOID reserved)
{
    BOOL result;
    if (replace_error) {
        if (replace_error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            assert(MoveFileExW(replaced, backup, MOVEFILE_REPLACE_EXISTING));
        }
        SetLastError(replace_error); return FALSE;
    }
    result = ReplaceFileW(replaced, replacement, backup, flags, exclude, reserved);
    if (result && change_after_replace) write_file(replaced, "# outside writer after commit\n");
    return result;
}
static void replacement_failures(void)
{
    static const DWORD errors[] = { ERROR_UNABLE_TO_REMOVE_REPLACED, ERROR_UNABLE_TO_MOVE_REPLACEMENT,
        ERROR_UNABLE_TO_MOVE_REPLACEMENT_2, ERROR_ACCESS_DENIED };
    unsigned int i;
    for (i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        wtop_settings settings;
        wtop_config_document document = {0};
        wtop_settings_store *store;
        wtop_store_result result;
        char *bytes;
        write_file(target, legacy); wtop_settings_defaults(&settings); store = load_explicit(&settings, &document);
        settings.keymap = WTOP_KEYMAP_HTOP;
        replace_error = errors[i];
        wtop_store_test_operations(replacement_fixture, NULL);
        assert(!wtop_store_save(store, &settings, &document, 20, &result));
        assert(result.outcome == WTOP_STORE_RECOVERY_REQUIRED && result.native_error == errors[i] && !result.committed);
        assert(document.schema_version == 1 && !store->baseline_loaded);
        assert(GetFileAttributesW(store->recovery_temporary) != INVALID_FILE_ATTRIBUTES);
        if (errors[i] == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            assert(GetFileAttributesW(target) == INVALID_FILE_ATTRIBUTES);
            bytes = file_bytes(store->recovery_backup);
        } else bytes = file_bytes(target);
        assert(!strcmp(bytes, legacy)); free(bytes);
        wtop_store_test_operations(NULL, NULL); replace_error = 0;
        dispose(store, &document); remove_files();
    }
}
static void flush_and_verification_failures(void)
{
    wtop_settings settings;
    wtop_config_document document = {0};
    wtop_settings_store *store;
    wtop_store_result result;
    char *bytes;
    write_file(target, legacy); wtop_settings_defaults(&settings); store = load_explicit(&settings, &document);
    settings.keymap = WTOP_KEYMAP_EMACS;
    wtop_store_test_operations(NULL, failed_flush);
    assert(!wtop_store_save(store, &settings, &document, 31, &result));
    assert(result.outcome == WTOP_STORE_UNCHANGED_FAILURE && result.native_error == ERROR_DISK_FULL);
    assert(!result.committed && !store->recovery_temporary[0]);
    bytes = file_bytes(target); assert(!strcmp(bytes, legacy)); free(bytes);
    wtop_store_test_operations(replacement_fixture, NULL); change_after_replace = 1;
    assert(!wtop_store_save(store, &settings, &document, 32, &result));
    assert(result.outcome == WTOP_STORE_COMMITTED_UNVERIFIED && result.committed && result.saved_revision == 32);
    assert(document.schema_version == 1 && !store->baseline_loaded);
    bytes = file_bytes(store->recovery_backup); assert(!strcmp(bytes, legacy)); free(bytes);
    bytes = file_bytes(target); assert(!strcmp(bytes, "# outside writer after commit\n")); free(bytes);
    wtop_store_test_operations(NULL, NULL); change_after_replace = 0;
    dispose(store, &document); remove_files();
}
static void new_destination_and_future_refusal(void)
{
    wtop_settings settings, before;
    wtop_config_document document = {0};
    wtop_settings_store *store = (wtop_settings_store *)calloc(1, sizeof(*store));
    wtop_store_request request = {0};
    wtop_store_result result;
    char *bytes;
    assert(store); wtop_settings_defaults(&settings);
    assert(wtop_store_export_target(store, utf8_target, &result));
    assert(!store->expected_exists && store->baseline_loaded);
    write_file(target, "foreign sentinel");
    assert(!wtop_store_save(store, &settings, &document, 1, &result));
    assert(result.outcome == WTOP_STORE_CONFLICT);
    bytes = file_bytes(target); assert(!strcmp(bytes, "foreign sentinel")); free(bytes);
    wtop_store_dispose(store);
    write_file(target, "ConfigVersion 3\n"); before = settings;
    assert(!wtop_store_export_target(store, utf8_target, &result));
    assert(result.outcome == WTOP_STORE_INVALID && !store->baseline_loaded);
    wtop_store_dispose(store);
    request.explicit_path = utf8_target;
    assert(wtop_store_resolve(store, &request, &result));
    assert(!wtop_store_load(store, &settings, &document, &result));
    assert(result.outcome == WTOP_STORE_INVALID && !memcmp(&settings, &before, sizeof(settings)));
    bytes = file_bytes(target); assert(!strcmp(bytes, "ConfigVersion 3\n")); free(bytes);
    wtop_store_dispose(store); assert(DeleteFileW(target));
    assert(wtop_store_export_target(store, utf8_target, &result));
    assert(wtop_store_save(store, &settings, &document, 2, &result));
    assert(result.committed && result.saved_revision == 2 && !result.migrated);
    dispose(store, &document); remove_files();
}
int main(void)
{
    WCHAR root[MAX_PATH], reserved[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, root);
    assert(length && length < MAX_PATH);
    assert(GetTempFileNameW(root, L"wts", 0, reserved));
    assert(DeleteFileW(reserved) && CreateDirectoryW(reserved, NULL));
    wcscpy(directory, reserved); swprintf(target, MAX_PATH, L"%ls\\wtop.conf", directory);
    assert(WideCharToMultiByte(CP_UTF8, 0, target, -1, utf8_target, sizeof(utf8_target), NULL, NULL));
    migration_and_no_hidden_save();
    conflict_lock_and_flush();
    replacement_failures();
    flush_and_verification_failures();
    new_destination_and_future_refusal();
    assert(RemoveDirectoryW(directory));
    puts("native settings: explicit migration, conflicts, lock ownership, flush and replacement recovery passed");
    return 0;
}
