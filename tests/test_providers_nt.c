/* Native ownership/protocol fixtures; test-only child variants are not shipped.
   SPDX-License-Identifier: GPL-3.0-only */
#include "providers_nt.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stddef.h>

/* Diagnostic only: this native ABI is queried dynamically and is never a
   product dependency or a reason to waive the exact handle-count assertion.
   ProcessHandleInformation (51) and its layout are recorded by System Informer:
   https://github.com/winsiderss/phnt/blob/master/ntpsapi.h
   Only object TYPE is queried; object-name/file queries can block in drivers. */
typedef struct handle_entry {
    HANDLE value;
    SIZE_T handle_count, pointer_count;
    ULONG access, type_index, attributes, reserved;
} handle_entry;
typedef struct handle_snapshot {
    ULONG_PTR count, reserved;
    handle_entry entries[1];
} handle_snapshot;
typedef struct diagnostic_unicode {
    USHORT length, maximum_length;
    WCHAR *buffer;
} diagnostic_unicode;
typedef LONG (NTAPI *query_native_fn)(HANDLE, ULONG, void *, ULONG, ULONG *);

static query_native_fn native_query(const char *name)
{
    query_native_fn query = NULL;
    FARPROC address = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), name);
    if(address) memcpy(&query, &address, sizeof(query));
    return query;
}

static handle_snapshot *snapshot_handles(void)
{
    const ULONG capacity = 65536;
    query_native_fn query = native_query("NtQueryInformationProcess");
    handle_snapshot *snapshot;
    ULONG bytes = 0;
    LONG status;
    if(!query) return NULL;
    snapshot = calloc(1, capacity);
    if(!snapshot) return NULL;
    status = query(GetCurrentProcess(), 51, snapshot, capacity, &bytes);
    if(status < 0 || snapshot->count > (capacity - offsetof(handle_snapshot, entries)) / sizeof(handle_entry)) {
        free(snapshot); return NULL;
    }
    return snapshot;
}

static void print_new_handles(const char *stage, const handle_snapshot *before, const handle_snapshot *after)
{
    query_native_fn query = native_query("NtQueryObject");
    ULONG_PTR i, j;
    if(!before || !after || !query) {
        fprintf(stderr, "handle difference at %s: native type diagnostics unavailable\n", stage);
        fflush(stderr);
        return;
    }
    for(i = 0; i < after->count; ++i) {
        const handle_entry *entry = &after->entries[i];
        void *information;
        ULONG bytes = 0;
        LONG status;
        for(j = 0; j < before->count; ++j)
            if(before->entries[j].value == entry->value && before->entries[j].type_index == entry->type_index) break;
        if(j < before->count) continue;
        information = calloc(1, 4096);
        status = information ? query(entry->value, 2, information, 4096, &bytes) : (LONG)0xc0000017UL;
        fprintf(stderr, "new handle at %s: value=%p, type_index=%lu, access=0x%08lx, type=", stage,
            (void *)entry->value, entry->type_index, entry->access);
        if(status >= 0) {
            const diagnostic_unicode *name = information;
            ULONG_PTR start = (ULONG_PTR)information, text = (ULONG_PTR)name->buffer;
            if(name->buffer && !(name->length % sizeof(WCHAR)) && name->length <= 4096 &&
               text >= start && text - start <= 4096 - name->length)
                fprintf(stderr, "%.*ls", (int)(name->length / sizeof(WCHAR)), name->buffer);
        } else fprintf(stderr, "query failed 0x%08lx", (unsigned long)status);
        fputc('\n', stderr);
        free(information);
    }
    fflush(stderr);
}

static DWORD WINAPI signal_cancel(void *context)
{
    Sleep(60);
    SetEvent((HANDLE)context);
    return 0;
}
static void request_kind(wtop_provider_request *request, wtop_provider_kind kind)
{
    memset(request, 0, sizeof(*request));
    request->kind = kind;
}
static void closed(const wtop_provider_session *session, const wtop_provider_result *result)
{
    assert(result->retirement_confirmed);
    assert(!session->process && !session->job && !session->pipe);
}
/* First use of Windows process creation can initialize process-wide facilities
   outside the provider's ownership. Exercise that API independently, without
   protocol pipes or a resumed child, before measuring per-collection handle
   balance. This does not permit a single handle of drift in the provider loop. */
static void prime_process_creation(void)
{
    WCHAR *path = calloc(32768, sizeof(*path));
    WCHAR *command = calloc(32768, sizeof(*command));
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    HANDLE job;
    BOOL assigned;
    DWORD length;
    assert(path && command);
    length = GetModuleFileNameW(NULL, path, 32768);
    assert(length && length < 32600);
    assert(_snwprintf_s(command, 32768, _TRUNCATE, L"\"%s\"", path) > 0);
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    memset(&process, 0, sizeof(process));
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    job = CreateJobObjectW(NULL, NULL);
    assert(job);
    assert(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    assert(CreateProcessW(path, command, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
        NULL, NULL, &startup, &process));
    /* Never resume the preflight child. It owns no application resources. */
    assigned = AssignProcessToJobObject(job, process.hProcess);
    /* Even a fixture containment failure must retire our suspended child. */
    if(assigned) assert(TerminateJobObject(job, 0));
    else assert(TerminateProcess(process.hProcess, 1));
    assert(WaitForSingleObject(process.hProcess, 3000) == WAIT_OBJECT_0);
    assert(CloseHandle(process.hThread));
    assert(CloseHandle(process.hProcess));
    assert(CloseHandle(job));
    free(command); free(path);
    assert(assigned);
}

/* Independent primitive probe: no provider, attributes, worker or request
   protocol. Record every returned handle and close it before taking the final
   count, so first-use allocations made inside Windows can be distinguished
   from an endpoint that the application forgot to close. */
static void probe_pipe_io(int named)
{
    HANDLE input, output;
    DWORD before, opened, written_count, read_count, after, count, flags;
    char sent = 'p', received = 0;
    static unsigned sequence;
    assert(GetProcessHandleCount(GetCurrentProcess(), &before));
    if(named) {
        WCHAR name[160];
        BOOL connected;
        DWORD error;
        _snwprintf_s(name, sizeof(name) / sizeof(name[0]), _TRUNCATE,
            L"\\\\.\\pipe\\wtop-fixture-%lu-%llu-%u", GetCurrentProcessId(), GetTickCount64(), ++sequence);
        input = CreateNamedPipeW(name, PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 128, 128, 0, NULL);
        assert(input != INVALID_HANDLE_VALUE);
        connected = ConnectNamedPipe(input, NULL); error = GetLastError();
        assert(connected || error == ERROR_PIPE_LISTENING);
        output = CreateFileW(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        assert(output != INVALID_HANDLE_VALUE);
        connected = ConnectNamedPipe(input, NULL); error = GetLastError();
        assert(connected || error == ERROR_PIPE_CONNECTED);
    } else assert(CreatePipe(&input, &output, NULL, 128));
    assert(GetProcessHandleCount(GetCurrentProcess(), &opened));
    assert(WriteFile(output, &sent, 1, &count, NULL) && count == 1);
    assert(GetProcessHandleCount(GetCurrentProcess(), &written_count));
    assert(ReadFile(input, &received, 1, &count, NULL) && count == 1 && received == sent);
    assert(GetProcessHandleCount(GetCurrentProcess(), &read_count));
    assert(CloseHandle(output));
    assert(CloseHandle(input));
    assert(!GetHandleInformation(input, &flags) && GetLastError() == ERROR_INVALID_HANDLE);
    assert(!GetHandleInformation(output, &flags) && GetLastError() == ERROR_INVALID_HANDLE);
    assert(GetProcessHandleCount(GetCurrentProcess(), &after));
    fprintf(stderr, "native %s pipe primitive: input=%p, output=%p; handles before=%lu, opened=%lu, written=%lu, read=%lu, closed=%lu\n",
        named ? "named" : "anonymous", (void *)input, (void *)output, before, opened, written_count, read_count, after);
    fflush(stderr);
}

int wmain(int argc, WCHAR **argv)
{
    wtop_provider_session session;
    wtop_provider_request request;
    wtop_provider_result *result;
    HANDLE cancel, thread;
    FILETIME created, exited, kernel, user;
    DWORD cold, diagnostics_ready, before, after, sequence, primitives_ready;
    handle_snapshot *cold_handles, *diagnostic_handles, *baseline_handles;
    int code, i;
    ULONGLONG started;
    if(wtop_provider_worker_dispatch(argc, argv, &code)) return code;
    result = calloc(1, sizeof(*result));
    assert(result);
    wtop_provider_session_init(&session);
    cold_handles = snapshot_handles();
    assert(GetProcessHandleCount(GetCurrentProcess(), &cold));
    /* Initialize diagnostic I/O before the measured API path. A static CRT may
       initialize stdio lazily; the diagnostic itself must not look like a leak. */
    fprintf(stderr, "native provider diagnostics starting: cold handles=%lu\n", cold);
    fflush(stderr);
    assert(GetProcessHandleCount(GetCurrentProcess(), &diagnostics_ready));
    diagnostic_handles = snapshot_handles();
    if(cold != diagnostics_ready) print_new_handles("diagnostic stream initialization", cold_handles, diagnostic_handles);
    free(cold_handles); free(diagnostic_handles);
    prime_process_creation();
    assert(GetProcessHandleCount(GetCurrentProcess(), &before));
    fprintf(stderr, "native process-creation preflight handles: cold=%lu, diagnostics=%lu, warm=%lu\n", cold, diagnostics_ready, before);
    fflush(stderr);
    baseline_handles = snapshot_handles();
    probe_pipe_io(0);
    assert(GetProcessHandleCount(GetCurrentProcess(), &after));
    diagnostic_handles = snapshot_handles();
    if(before != after) print_new_handles("anonymous pipe primitive", baseline_handles, diagnostic_handles);
    free(baseline_handles); baseline_handles = diagnostic_handles; before = after;
    probe_pipe_io(1);
    assert(GetProcessHandleCount(GetCurrentProcess(), &primitives_ready));
    diagnostic_handles = snapshot_handles();
    if(before != primitives_ready) print_new_handles("named pipe primitive", baseline_handles, diagnostic_handles);
    free(baseline_handles); baseline_handles = diagnostic_handles;
    /* The primitive's own repeated lifecycle must also have exact balance. */
    for(i = 0; i < 5; ++i) {
        probe_pipe_io(0); probe_pipe_io(1);
        assert(GetProcessHandleCount(GetCurrentProcess(), &after));
        if(after != primitives_ready) {
            diagnostic_handles = snapshot_handles();
            print_new_handles("repeated pipe primitive", baseline_handles, diagnostic_handles);
            free(diagnostic_handles);
        }
        assert(after == primitives_ready);
    }
    before = primitives_ready;
    request_kind(&request, WTOP_PROVIDER_TEST_VALID);
    for(i = 0; i < 5; ++i) {
        assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_VALID);
        closed(&session, result);
        assert(result->response_received);
        assert(result->started_ms <= result->observed_ms);
        assert(GetProcessHandleCount(GetCurrentProcess(), &after));
        fprintf(stderr, "native provider handles after iteration %d: expected=%lu, actual=%lu\n", i, before, after);
        fflush(stderr);
        if(before != after) {
            handle_snapshot *current = snapshot_handles();
            print_new_handles("provider collection", baseline_handles, current);
            free(current);
        }
        assert(before == after);
    }
    free(baseline_handles);

    request_kind(&request, WTOP_PROVIDER_TEST_DELAY);
    started = GetTickCount64();
    assert(wtop_provider_collect(&session, &request, 100, NULL, result) == WTOP_PROVIDER_TIMEOUT);
    assert(GetTickCount64() - started < 5000);
    closed(&session, result);

    cancel = CreateEventW(NULL, TRUE, TRUE, NULL);
    assert(cancel);
    sequence = session.sequence;
    assert(wtop_provider_collect(&session, &request, 3000, cancel, result) == WTOP_PROVIDER_CANCELLED);
    assert(sequence == session.sequence); /* No worker admission after cancellation. */
    ResetEvent(cancel);
    thread = CreateThread(NULL, 0, signal_cancel, cancel, 0, NULL);
    assert(thread);
    assert(wtop_provider_collect(&session, &request, 3000, cancel, result) == WTOP_PROVIDER_CANCELLED);
    assert(WaitForSingleObject(thread, 3000) == WAIT_OBJECT_0);
    CloseHandle(thread); CloseHandle(cancel);
    closed(&session, result);

    request_kind(&request, WTOP_PROVIDER_TEST_CRASH);
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_ERROR);
    assert(result->error_domain == WTOP_PROVIDER_ERROR_EXIT_CODE && result->native_error == 23);
    closed(&session, result);
    request_kind(&request, WTOP_PROVIDER_TEST_BAD_PROTOCOL);
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_PROTOCOL);
    assert(!result->response_received);
    closed(&session, result);
    request_kind(&request, WTOP_PROVIDER_TEST_PARTIAL);
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_PROTOCOL);
    assert(!result->response_received);
    closed(&session, result);
    request_kind(&request, WTOP_PROVIDER_TEST_EXTRA);
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_PROTOCOL);
    closed(&session, result);
    request_kind(&request, WTOP_PROVIDER_TEST_RESPONSE_THEN_BLOCK);
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_TIMEOUT);
    assert(result->response_received); /* Prove the fixture reached the post-response block. */
    closed(&session, result); /* A complete response was not claimed as child exit. */

    request_kind(&request, WTOP_PROVIDER_TEST_RETIRE_PENDING);
    assert(wtop_provider_collect(&session, &request, 100, NULL, result) == WTOP_PROVIDER_TIMEOUT);
    assert(!result->retirement_confirmed && session.admission_closed && session.process);
    assert(!wtop_provider_session_rearm(&session));
    sequence = session.sequence;
    request_kind(&request, WTOP_PROVIDER_TEST_VALID);
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_RETIREMENT_PENDING);
    assert(sequence == session.sequence);
    session.test_defer_retirement = 0; /* Explicit release of injected retirement failure. */
    assert(wtop_provider_session_close(&session, 3000));
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_ADMISSION_CLOSED);
    assert(wtop_provider_session_rearm(&session));
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_VALID);
    closed(&session, result);

    request_kind(&request, WTOP_PROVIDER_PER_CORE);
    sequence = session.sequence;
    request.interval_ms = 0;
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_CAPACITY);
    assert(sequence == session.sequence);
    request_kind(&request, WTOP_PROVIDER_ACCOUNT);
    for(i = 0; i < WTOP_PROVIDER_SID_CHARS; ++i) request.sid[i] = L'x';
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_CAPACITY);
    assert(sequence == session.sequence);

    assert(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user));
    request_kind(&request, WTOP_PROVIDER_COMMAND_LINE);
    request.pid = GetCurrentProcessId(); request.creation = created;
    request.creation.dwLowDateTime ^= 1; /* Same PID, wrong lifetime: no WMI read accepted. */
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_IDENTITY_CHANGED);
    assert(!result->data.command_line[0]);
    closed(&session, result);
    request.creation.dwLowDateTime = request.creation.dwHighDateTime = 0;
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_IDENTITY_UNAVAILABLE);
    closed(&session, result);

    request_kind(&request, WTOP_PROVIDER_ACCOUNT);
    wcscpy(request.sid, L"S-1-5-18");
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_VALID);
    assert(result->data.account.name[0] && !wcscmp(result->data.account.sid, L"S-1-5-18"));
    closed(&session, result);

    request_kind(&request, WTOP_PROVIDER_SERVICES);
    request.pid = GetCurrentProcessId(); request.creation = created;
    assert(wtop_provider_collect(&session, &request, 5000, NULL, result) == WTOP_PROVIDER_VALID);
    assert(result->count == 0); /* This fixture is not a registered service process. */
    closed(&session, result);

    request_kind(&request, WTOP_PROVIDER_PER_CORE); request.interval_ms = 100;
    wtop_provider_collect(&session, &request, 5000, NULL, result);
    printf("native PDH state: %s; instances: %lu; elapsed ms: %.3f\n",
        wtop_provider_state_name(result->state), result->count, result->elapsed_ms);
    assert(result->state != WTOP_PROVIDER_PROTOCOL && result->state != WTOP_PROVIDER_IDENTITY_CHANGED);
    if(result->state == WTOP_PROVIDER_VALID || result->state == WTOP_PROVIDER_CAPACITY) {
        assert(result->count > 0 && result->count <= WTOP_PROVIDER_MAX_CORES && result->elapsed_ms > 0);
        for(i = 0; i < (int)result->count; ++i) {
            assert(result->data.cores[i].instance[0]);
            assert(!wcsstr(result->data.cores[i].instance, L"_Total"));
            if(result->data.cores[i].state == WTOP_PROVIDER_VALID) assert(result->data.cores[i].busy_percent >= 0);
        }
    }
    closed(&session, result);
    assert(wtop_provider_session_close(&session, 3000));
    free(result);
    puts("native provider ownership/protocol/identity fixtures passed");
    return 0;
}
