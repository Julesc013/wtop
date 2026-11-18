/* Exercise the production asynchronous adapter and child protocol.
   SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#error Native ownership fixtures require assertions.
#endif
#include "tui_inspection_nt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static wtop_provider_result *await_result(wtop_tui_inspection *owner, ULONGLONG expected)
{
    ULONGLONG until = GetTickCount64() + 10000;
    while(GetTickCount64() < until) {
        ULONGLONG sequence = 0;
        wtop_provider_result *result = wtop_tui_inspection_poll(owner, &sequence);
        if(sequence == expected) { assert(result); return result; }
        free(result);
        Sleep(10);
    }
    assert(!"asynchronous provider completion exceeded fixture deadline");
    return NULL;
}

int wmain(int argc, WCHAR **argv)
{
    int worker_exit = 0;
    if(wtop_provider_worker_dispatch(argc, argv, &worker_exit)) return worker_exit;
    wtop_tui_inspection owner = {0};
    wtop_provider_request request = {0};
    FILETIME exit_time, kernel, user;
    request.pid = GetCurrentProcessId();
    assert(GetProcessTimes(GetCurrentProcess(), &request.creation, &exit_time, &kernel, &user));
    request.kind = WTOP_PROVIDER_PROCESS_DETAILS;
    request.detail_flags = WTOP_DETAIL_IMAGE_PATH;
    ULONGLONG first = wtop_tui_inspection_request(&owner, &request);
    assert(first && owner.worker);
    wtop_provider_result *result = await_result(&owner, first);
    assert(result->pid == request.pid && result->response_received);
    assert(result->state == WTOP_PROVIDER_VALID);
    assert(result->data.details.image_status.state == WTOP_PROVIDER_VALID);
    assert(result->data.details.image_path[0]);
    free(result);

    /* Repeated reads coalesce in the pending slot. Generation binding prevents
       the replaced read's result becoming the newer thread panel. */
    first = wtop_tui_inspection_request(&owner, &request);
    request.kind = WTOP_PROVIDER_THREADS; request.detail_flags = 0;
    ULONGLONG second = wtop_tui_inspection_request(&owner, &request);
    assert(first && second > first);
    result = await_result(&owner, second);
    assert(result->kind == WTOP_PROVIDER_THREADS && result->pid == request.pid);
    assert(result->state == WTOP_PROVIDER_VALID || result->state == WTOP_PROVIDER_CAPACITY);
    assert(result->count > 0 && result->count <= WTOP_PROVIDER_MAX_THREADS);
    free(result);

    /* Read-only fails before queue admission or process access. No actual
       process affinity mutation is performed by this fixture. */
    assert(!wtop_tui_inspection_apply(&owner, request.pid, request.creation, 1, 1));
    wtop_affinity_outcome outcome;
    ULONGLONG sequence = 0;
    assert(!wtop_tui_inspection_poll_action(&owner, &outcome, &sequence));
    assert(wtop_tui_inspection_request(&owner, &request));
    wtop_tui_inspection_cancel(&owner);
    assert(wtop_tui_inspection_stop(&owner, 5000));
    assert(!owner.initialized && !owner.worker && !owner.result);
    assert(wtop_tui_inspection_stop(&owner, 1));
    /* Deterministic pre-dequeue state, under the adapter's actual publication
       lock. The worker has not received wake, so production stop owns the
       decision to cancel this confirmed request without native mutation. */
    assert(wtop_tui_inspection_start(&owner));
    EnterCriticalSection(&owner.lock);
    owner.request = request; owner.requested_mask = 1;
    owner.requested_sequence = 500; owner.pending = owner.action_pending = 1;
    LeaveCriticalSection(&owner.lock);
    assert(wtop_tui_inspection_stop(&owner, 5000));
    assert(wtop_tui_inspection_poll_action(&owner, &outcome, &sequence));
    assert(sequence == 500 && outcome.pid == request.pid && outcome.requested_mask == 1);
    assert(outcome.status.state == WTOP_PROVIDER_CANCELLED && !outcome.attempted && !outcome.applied);
    assert(!wtop_tui_inspection_poll_action(&owner, &outcome, &sequence));
    puts("TUI inspection: actual bounded reads, replacement, identity, read-only admission and retirement passed");
    return 0;
}
