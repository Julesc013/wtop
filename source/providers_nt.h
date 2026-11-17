/* Optional native providers. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_PROVIDERS_NT_H
#define WTOP_PROVIDERS_NT_H
#include <windows.h>

#define WTOP_PROVIDER_COMMAND_CHARS 16384
#define WTOP_PROVIDER_MAX_SERVICES 512
#define WTOP_PROVIDER_MAX_CORES 512
#define WTOP_PROVIDER_NAME_CHARS 257
#define WTOP_PROVIDER_SID_CHARS 192
#define WTOP_PROVIDER_MAX_TIMEOUT_MS 60000UL

typedef enum wtop_provider_kind {
    WTOP_PROVIDER_COMMAND_LINE = 1,
    WTOP_PROVIDER_SERVICES,
    WTOP_PROVIDER_PER_CORE,
    WTOP_PROVIDER_ACCOUNT
#ifdef WTOP_PROVIDER_TESTING
    , WTOP_PROVIDER_TEST_VALID = 101, WTOP_PROVIDER_TEST_DELAY,
    WTOP_PROVIDER_TEST_CRASH, WTOP_PROVIDER_TEST_BAD_PROTOCOL,
    WTOP_PROVIDER_TEST_PARTIAL, WTOP_PROVIDER_TEST_RETIRE_PENDING,
    WTOP_PROVIDER_TEST_RESPONSE_THEN_BLOCK, WTOP_PROVIDER_TEST_EXTRA
#endif
} wtop_provider_kind;
typedef enum wtop_provider_state {
    WTOP_PROVIDER_VALID, WTOP_PROVIDER_WARMING_UP, WTOP_PROVIDER_UNSUPPORTED,
    WTOP_PROVIDER_DENIED, WTOP_PROVIDER_UNAVAILABLE, WTOP_PROVIDER_ERROR,
    WTOP_PROVIDER_TIMEOUT, WTOP_PROVIDER_CANCELLED, WTOP_PROVIDER_CAPACITY,
    WTOP_PROVIDER_PROTOCOL, WTOP_PROVIDER_IDENTITY_CHANGED,
    WTOP_PROVIDER_IDENTITY_UNAVAILABLE, WTOP_PROVIDER_RETIREMENT_PENDING, WTOP_PROVIDER_ADMISSION_CLOSED
} wtop_provider_state;
typedef enum wtop_provider_error_domain {
    WTOP_PROVIDER_ERROR_NONE, WTOP_PROVIDER_ERROR_WIN32,
    WTOP_PROVIDER_ERROR_HRESULT, WTOP_PROVIDER_ERROR_PDH, WTOP_PROVIDER_ERROR_EXIT_CODE
} wtop_provider_error_domain;
typedef struct wtop_provider_request {
    wtop_provider_kind kind;
    DWORD pid;
    FILETIME creation; /* Required for command line and a nonzero services PID. */
    DWORD interval_ms; /* Requested 50..60000; documented PDH rate minimum is 1000 ms. */
    WCHAR sid[WTOP_PROVIDER_SID_CHARS]; /* Account name resolution input. */
} wtop_provider_request;
typedef struct wtop_provider_service {
    WCHAR name[WTOP_PROVIDER_NAME_CHARS];
    WCHAR display_name[WTOP_PROVIDER_NAME_CHARS];
    DWORD pid, service_state;
    FILETIME creation;
    wtop_provider_state identity_state; /* A PID alone is never an identity. */
} wtop_provider_service;
typedef struct wtop_provider_core {
    WCHAR instance[64]; /* Preserve PDH's original group/instance label. */
    DWORD group, index; /* MAXDWORD when this source does not identify one. */
    wtop_provider_state state;
    DWORD native_error; /* PDH status for this instance. */
    double busy_percent;
} wtop_provider_core;
typedef struct wtop_provider_result {
    wtop_provider_kind kind;
    wtop_provider_state state;
    wtop_provider_error_domain error_domain;
    DWORD native_error;
    DWORD pid, count, total_count; /* total_count is observed rows; capacity can mean incomplete enumeration. */
    FILETIME creation;
    ULONGLONG started_ms, observed_ms; /* GetTickCount64; actual worker observations, not delivery time. */
    double elapsed_ms, acquisition_ms;
    int retirement_confirmed; /* Parent-owned: a response is not worker exit. */
    int legacy_processor_scope; /* Processor(*) fallback cannot claim group parity. */
    union {
        WCHAR command_line[WTOP_PROVIDER_COMMAND_CHARS];
        wtop_provider_service services[WTOP_PROVIDER_MAX_SERVICES];
        wtop_provider_core cores[WTOP_PROVIDER_MAX_CORES];
        struct { WCHAR name[257], domain[257], sid[WTOP_PROVIDER_SID_CHARS]; } account;
    } data;
} wtop_provider_result;
/* Initialize once. Only the calling thread may use a session. Handles remain
   owned here until exit is observed. Do not copy a live session or overwrite it. */
typedef struct wtop_provider_session {
    HANDLE process, job, pipe;
    DWORD sequence;
    int admission_closed;
    int job_assigned;
#ifdef WTOP_PROVIDER_TESTING
    int test_defer_retirement;
#endif
} wtop_provider_session;
void wtop_provider_session_init(wtop_provider_session *session);
/* Synchronous admission/launch plus a bounded worker wait. Win32 process launch
   itself has no documented cancellation deadline. No implicit retry occurs. */
wtop_provider_state wtop_provider_collect(wtop_provider_session *session,
    const wtop_provider_request *request, DWORD timeout_ms, HANDLE cancel_event,
    wtop_provider_result *result);
/* Request job termination, observe exit up to grace_ms, then release ownership.
   False retains handles and closes admission. A later explicit rearm is needed. */
int wtop_provider_session_close(wtop_provider_session *session, DWORD grace_ms);
int wtop_provider_session_rearm(wtop_provider_session *session);
const char *wtop_provider_state_name(wtop_provider_state state);
/* Call before public CLI parsing. Returns 1 for a handled private invocation,
   otherwise 0. exit_code is set only for handled invocations. */
int wtop_provider_worker_dispatch(int argc, WCHAR *const *argv, int *exit_code);
#endif
