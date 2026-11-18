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
#define WTOP_PROVIDER_IMAGE_CHARS 32768
#define WTOP_PROVIDER_MAX_THREADS 1024
#define WTOP_PROVIDER_THREAD_SCAN_LIMIT 65536UL
#define WTOP_DETAIL_IMAGE_PATH 0x01UL
#define WTOP_DETAIL_COMMAND_LINE 0x02UL
#define WTOP_DETAIL_SERVICES 0x04UL
#define WTOP_DETAIL_AFFINITY 0x08UL
#define WTOP_DETAIL_ALL 0x0fUL

typedef enum wtop_provider_kind {
    WTOP_PROVIDER_COMMAND_LINE = 1,
    WTOP_PROVIDER_SERVICES,
    WTOP_PROVIDER_PER_CORE,
    WTOP_PROVIDER_ACCOUNT,
    WTOP_PROVIDER_PROCESS_DETAILS,
    WTOP_PROVIDER_THREADS,
    WTOP_PROVIDER_AFFINITY
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
    DWORD detail_flags; /* Explicit opt-in WTOP_DETAIL_*; PROCESS_DETAILS only. */
} wtop_provider_request;
typedef struct wtop_provider_status {
    wtop_provider_state state;
    wtop_provider_error_domain error_domain;
    DWORD native_error;
} wtop_provider_status;
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
typedef enum wtop_affinity_limit {
    WTOP_AFFINITY_LIMIT_NONE, WTOP_AFFINITY_LIMIT_MULTIPLE_GROUPS,
    WTOP_AFFINITY_LIMIT_NATIVE_WIDTH, WTOP_AFFINITY_LIMIT_API
} wtop_affinity_limit;
typedef struct wtop_provider_affinity {
    wtop_provider_status status;
    ULONGLONG process_mask, system_mask; /* Exact masks only when status valid. */
    DWORD mask_bits, group, process_group_count, system_group_count;
    wtop_affinity_limit limitation;
} wtop_provider_affinity;
typedef struct wtop_provider_thread {
    DWORD tid, pid;
    FILETIME creation; /* Identity is (owning PID/creation, TID/creation). */
    wtop_provider_status identity_status, times_status, priority_status;
    ULONGLONG kernel_100ns, user_100ns; /* Lifetime counters, never a sampled rate. */
    ULONGLONG observed_ms;
    LONG priority; /* GetThreadPriority relative priority, not scheduler/base state. */
} wtop_provider_thread;
typedef struct wtop_provider_details {
    DWORD requested;
    wtop_provider_status image_status, command_status, services_status;
    WCHAR image_path[WTOP_PROVIDER_IMAGE_CHARS];
    WCHAR command_line[WTOP_PROVIDER_COMMAND_CHARS];
    DWORD service_count, service_total_count;
    wtop_provider_service services[WTOP_PROVIDER_MAX_SERVICES];
    wtop_provider_affinity affinity;
} wtop_provider_details;
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
    int response_received; /* Parent-owned: complete validated response, even if the child has not exited. */
    int legacy_processor_scope; /* Processor(*) fallback cannot claim group parity. */
    union {
        WCHAR command_line[WTOP_PROVIDER_COMMAND_CHARS];
        wtop_provider_service services[WTOP_PROVIDER_MAX_SERVICES];
        wtop_provider_core cores[WTOP_PROVIDER_MAX_CORES];
        struct { WCHAR name[257], domain[257], sid[WTOP_PROVIDER_SID_CHARS]; } account;
        wtop_provider_details details;
        wtop_provider_thread threads[WTOP_PROVIDER_MAX_THREADS];
        wtop_provider_affinity affinity;
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
typedef struct wtop_affinity_outcome {
    wtop_provider_status status;
    DWORD pid;
    FILETIME creation;
    ULONGLONG requested_mask;
    wtop_provider_affinity before, after;
    int readonly_blocked, attempted, applied, verified;
} wtop_affinity_outcome;
/* Explicit mutation only. One process handle authenticates identity and receives
   the action; no PID lookup occurs between revalidation and SetProcessAffinityMask.
   readonly is enforced before opening the process. A successful mutation remains
   applied even if subsequent verification fails. Do not automatically retry it.
   This native action has no documented hard deadline; run it on the owned action
   worker, never a render/input callback. Multi-group hosts and folded WOW64 masks
   are deliberately unsupported. The caller retains each outcome independently. */
wtop_provider_state wtop_process_affinity_apply(DWORD pid, FILETIME creation,
    ULONGLONG requested_mask, int readonly, wtop_affinity_outcome *outcome);
const char *wtop_affinity_limit_name(wtop_affinity_limit limitation);
#endif
