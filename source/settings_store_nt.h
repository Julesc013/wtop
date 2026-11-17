/* Native NT settings owner and explicit save transaction.
 * SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_SETTINGS_STORE_NT_H
#define WTOP_SETTINGS_STORE_NT_H

#include <windows.h>
#include "settings.h"

#define WTOP_STORE_PATH_CHARS 32768

typedef enum wtop_store_owner {
    WTOP_STORE_MEMORY, WTOP_STORE_EXPLICIT, WTOP_STORE_PORTABLE,
    WTOP_STORE_INSTALLED_USER, WTOP_STORE_INSTALLED_MACHINE,
    WTOP_STORE_UNREGISTERED
} wtop_store_owner;
typedef enum wtop_store_source {
    WTOP_STORE_COMPILED, WTOP_STORE_SELECTED_FILE, WTOP_STORE_PORTABLE_FILE,
    WTOP_STORE_ADJACENT_FILE, WTOP_STORE_LEGACY_FILE,
    WTOP_STORE_USER_FILE, WTOP_STORE_MACHINE_DEFAULTS
} wtop_store_source;
typedef enum wtop_store_outcome {
    WTOP_STORE_OK, WTOP_STORE_CONFLICT, WTOP_STORE_BUSY,
    WTOP_STORE_DENIED, WTOP_STORE_INVALID, WTOP_STORE_UNAVAILABLE,
    WTOP_STORE_UNCHANGED_FAILURE, WTOP_STORE_COMMITTED_UNVERIFIED,
    WTOP_STORE_RECOVERY_REQUIRED
} wtop_store_outcome;

typedef struct wtop_store_request {
    const char *explicit_path; /* UTF-8; an explicitly selected read must exist. */
    const char *portable_root; /* UTF-8 explicit directory, independent of CWD afterward. */
    int portable;             /* With no root, use the actual module directory. */
    int no_config;            /* No config reads and no implicit save destination. */
} wtop_store_request;

typedef struct wtop_store_result {
    wtop_store_outcome outcome;
    DWORD native_error;
    const char *message; /* Static text; never freed by the caller. */
    unsigned long error_line;
    unsigned long saved_revision; /* Captured revision, not the caller's current revision. */
    int committed;       /* Replacement primitive returned success; verification may fail. */
    int migrated;        /* Explicitly saved schema 1/legacy as current schema. */
} wtop_store_result;

/* Allocate this large native owner outside a small thread stack. Initialize to
 * zero before first resolve/export and dispose before reuse. Paths are UTF-16.
 * read_path is the selected document; write_path can be a disclosed user copy.
 * recovery_* identify surviving candidates after uncertain replacement; dispose
 * NEVER deletes them. A successful migration retains a named original backup.
 * No writability claim is inferred from directory permissions: a save determines
 * the actual result, without elevation or fallback to a different destination. */
typedef struct wtop_settings_store {
    wtop_store_owner owner;
    wtop_store_source source;
    WCHAR module_path[WTOP_STORE_PATH_CHARS];
    WCHAR read_path[WTOP_STORE_PATH_CHARS];
    WCHAR write_path[WTOP_STORE_PATH_CHARS];
    WCHAR user_directory[WTOP_STORE_PATH_CHARS];
    WCHAR recovery_temporary[WTOP_STORE_PATH_CHARS];
    WCHAR recovery_backup[WTOP_STORE_PATH_CHARS];
    char *expected_bytes;
    size_t expected_length;
    int expected_exists;
    int baseline_loaded;
    int disabled;
    int explicit_required;
    int canonical_aliases; /* Final-path normalization available; hard links refused. */
} wtop_settings_store;

const char *wtop_store_owner_name(wtop_store_owner owner);
const char *wtop_store_source_name(wtop_store_source source);
const char *wtop_store_outcome_name(wtop_store_outcome outcome);
int wtop_store_resolve(wtop_settings_store *store, const wtop_store_request *request,
                        wtop_store_result *result);
int wtop_store_load(wtop_settings_store *store, wtop_settings *settings,
                     wtop_config_document *document, wtop_store_result *result);
int wtop_store_save(wtop_settings_store *store, const wtop_settings *settings,
                     wtop_config_document *document, unsigned long captured_revision,
                     wtop_store_result *result);
/* Prepare a separately allocated owner for explicit --write-config. The target
 * is captured as a baseline, so exports also serialize with cooperative writers.
 * Existing destinations must parse as a supported configuration; future schemas
 * and malformed files are refused. Their values are never applied to
 * the caller's settings. */
int wtop_store_export_target(wtop_settings_store *store, const char *path,
                             wtop_store_result *result);
void wtop_store_dispose(wtop_settings_store *store);

#ifdef WTOP_STORE_TESTING
/* Test-only synchronous native operation seam. Install/reset only while no store
 * operation is running; callbacks execute on the caller thread and own no state
 * beyond that call. Production builds contain no hook or callback registration. */
typedef BOOL (WINAPI *wtop_store_replace_fn)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPVOID, LPVOID);
typedef BOOL (WINAPI *wtop_store_flush_fn)(HANDLE);
void wtop_store_test_operations(wtop_store_replace_fn replace, wtop_store_flush_fn flush);
#endif

/* Registration contract: HKCU or HKLM Software\Wtop\Installations\<profile>,
 * SchemaVersion REG_DWORD = 1, InstallPath REG_SZ = absolute payload directory.
 * Canonical module-directory equality establishes scope. HKCU wins an identical
 * user/machine match. Registry records are read only; unknown schemas are ignored.
 * Save uses one non-waiting cooperative sidecar lock plus exact byte comparisons.
 * This is NOT compare-and-swap against arbitrary external editors, a filesystem
 * operation timeout, nor a power-loss durability guarantee. */
#endif
