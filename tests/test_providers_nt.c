/* Native ownership/protocol fixtures; test-only child variants are not shipped.
   SPDX-License-Identifier: GPL-3.0-only */
#include "providers_nt.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

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
int wmain(int argc, WCHAR **argv)
{
    wtop_provider_session session;
    wtop_provider_request request;
    wtop_provider_result *result;
    HANDLE cancel, thread;
    FILETIME created, exited, kernel, user;
    DWORD before, after, sequence;
    int code, i;
    ULONGLONG started;
    if(wtop_provider_worker_dispatch(argc, argv, &code)) return code;
    result = calloc(1, sizeof(*result));
    assert(result);
    wtop_provider_session_init(&session);
    assert(GetProcessHandleCount(GetCurrentProcess(), &before));
    request_kind(&request, WTOP_PROVIDER_TEST_VALID);
    for(i = 0; i < 5; ++i) {
        assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_VALID);
        closed(&session, result);
        assert(result->started_ms <= result->observed_ms);
    }
    assert(GetProcessHandleCount(GetCurrentProcess(), &after));
    assert(before == after);

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
    closed(&session, result);
    request_kind(&request, WTOP_PROVIDER_TEST_PARTIAL);
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_PROTOCOL);
    closed(&session, result);
    request_kind(&request, WTOP_PROVIDER_TEST_EXTRA);
    assert(wtop_provider_collect(&session, &request, 3000, NULL, result) == WTOP_PROVIDER_PROTOCOL);
    closed(&session, result);
    request_kind(&request, WTOP_PROVIDER_TEST_RESPONSE_THEN_BLOCK);
    assert(wtop_provider_collect(&session, &request, 150, NULL, result) == WTOP_PROVIDER_TIMEOUT);
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
