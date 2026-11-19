/* Owned asynchronous TUI inspection. SPDX-License-Identifier: GPL-3.0-only */
#include "tui_inspection_nt.h"
#include <stdlib.h>
#include <string.h>

static DWORD WINAPI inspection_worker(LPVOID parameter)
{
    wtop_tui_inspection *owner = parameter;
    HANDLE waits[2] = {owner->stop, owner->wake};
    for(;;) {
        DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if(wait != WAIT_OBJECT_0 + 1) break;
        EnterCriticalSection(&owner->lock);
        if(owner->stopping) { LeaveCriticalSection(&owner->lock); break; }
        if(!owner->pending) { LeaveCriticalSection(&owner->lock); continue; }
        wtop_provider_request request = owner->request;
        ULONGLONG sequence = owner->requested_sequence, mask = owner->requested_mask;
        int action = owner->action_pending;
        owner->pending = 0; owner->active = 1;
        ResetEvent(owner->cancel);
        LeaveCriticalSection(&owner->lock);

        wtop_provider_result *result = NULL;
        wtop_affinity_outcome outcome;
        memset(&outcome, 0, sizeof(outcome));
        if(action) wtop_process_affinity_apply(request.pid, request.creation, mask, 0, &outcome);
        else {
            result = calloc(1, sizeof(*result));
            if(result) {
                if(owner->provider.admission_closed) wtop_provider_session_rearm(&owner->provider);
                wtop_provider_collect(&owner->provider, &request, 3000, owner->cancel, result);
            }
        }
        EnterCriticalSection(&owner->lock);
        owner->active = 0;
        wtop_provider_result *retired = NULL;
        if(action) {
            owner->action = outcome; owner->action_sequence = sequence;
            owner->action_pending = 0; owner->action_ready = 1;
        } else if(!owner->stopping && sequence == owner->requested_sequence) {
            retired = owner->result; owner->result = result;
            owner->result_sequence = sequence; owner->result_ready = 1; result = NULL;
        }
        LeaveCriticalSection(&owner->lock);
        free(retired); free(result);
    }
    if(!wtop_provider_session_close(&owner->provider, 1000)) owner->retirement_failed = 1;
    return 0;
}

int wtop_tui_inspection_start(wtop_tui_inspection *owner)
{
    if(owner->initialized) return !owner->stopping;
    InitializeCriticalSection(&owner->lock);
    owner->initialized = 1;
    owner->wake = CreateEvent(NULL, FALSE, FALSE, NULL);
    owner->stop = CreateEvent(NULL, TRUE, FALSE, NULL);
    owner->cancel = CreateEvent(NULL, TRUE, FALSE, NULL);
    wtop_provider_session_init(&owner->provider);
    if(owner->wake && owner->stop && owner->cancel)
        owner->worker = CreateThread(NULL, 0, inspection_worker, owner, 0, NULL);
    if(owner->worker) return 1;
    if(owner->wake) CloseHandle(owner->wake);
    if(owner->stop) CloseHandle(owner->stop);
    if(owner->cancel) CloseHandle(owner->cancel);
    DeleteCriticalSection(&owner->lock);
    memset(owner, 0, sizeof(*owner));
    return 0;
}

ULONGLONG wtop_tui_inspection_request(wtop_tui_inspection *owner,
    const wtop_provider_request *request)
{
    if(!wtop_tui_inspection_start(owner)) return 0;
    ULONGLONG sequence = 0;
    EnterCriticalSection(&owner->lock);
    if(!owner->stopping && !owner->action_pending && !owner->action_ready && owner->sequence != ~(ULONGLONG)0) {
        sequence = ++owner->sequence;
        owner->requested_sequence = sequence; owner->request = *request;
        owner->pending = 1;
        SetEvent(owner->cancel); SetEvent(owner->wake);
    }
    LeaveCriticalSection(&owner->lock);
    return sequence;
}

ULONGLONG wtop_tui_inspection_apply(wtop_tui_inspection *owner,
    DWORD pid, FILETIME creation, ULONGLONG mask, int readonly)
{
    if(readonly || !wtop_tui_inspection_start(owner)) return 0;
    ULONGLONG sequence = 0;
    EnterCriticalSection(&owner->lock);
    if(!owner->stopping && !owner->active && !owner->pending && !owner->action_ready &&
       !owner->action_pending && owner->sequence != ~(ULONGLONG)0) {
        sequence = ++owner->sequence;
        memset(&owner->request, 0, sizeof(owner->request));
        owner->request.pid = pid; owner->request.creation = creation;
        owner->requested_sequence = sequence; owner->requested_mask = mask;
        owner->pending = owner->action_pending = 1;
        SetEvent(owner->wake);
    }
    LeaveCriticalSection(&owner->lock);
    return sequence;
}

wtop_provider_result *wtop_tui_inspection_poll(wtop_tui_inspection *owner,
    ULONGLONG *sequence)
{
    if(!owner->initialized) return NULL;
    EnterCriticalSection(&owner->lock);
    wtop_provider_result *result = owner->result;
    owner->result = NULL;
    if(owner->result_ready) *sequence = owner->result_sequence;
    owner->result_ready = 0;
    LeaveCriticalSection(&owner->lock);
    return result;
}

int wtop_tui_inspection_poll_action(wtop_tui_inspection *owner,
    wtop_affinity_outcome *outcome, ULONGLONG *sequence)
{
    if(!owner->initialized) {
        int ready = owner->action_ready;
        if(ready) { *outcome = owner->action; *sequence = owner->action_sequence; owner->action_ready = 0; }
        return ready;
    }
    EnterCriticalSection(&owner->lock);
    int ready = owner->action_ready;
    if(ready) { *outcome = owner->action; *sequence = owner->action_sequence; owner->action_ready = 0; }
    LeaveCriticalSection(&owner->lock);
    return ready;
}

void wtop_tui_inspection_cancel(wtop_tui_inspection *owner)
{
    if(!owner->initialized) return;
    EnterCriticalSection(&owner->lock);
    /* Leaving the panel never cancels an already confirmed mutation. Its own
       outcome remains retained even when no panel is visible. */
    if(!owner->action_pending) {
        owner->pending = 0;
        if(owner->sequence != ~(ULONGLONG)0) owner->requested_sequence = ++owner->sequence;
        SetEvent(owner->cancel);
    }
    LeaveCriticalSection(&owner->lock);
}

int wtop_tui_inspection_stop(wtop_tui_inspection *owner, DWORD grace_ms)
{
    if(!owner->initialized) return 1;
    EnterCriticalSection(&owner->lock);
    owner->stopping = 1;
    if(owner->pending && owner->action_pending && !owner->active) {
        memset(&owner->action, 0, sizeof(owner->action));
        owner->action.pid = owner->request.pid; owner->action.creation = owner->request.creation;
        owner->action.requested_mask = owner->requested_mask;
        owner->action.status.state = WTOP_PROVIDER_CANCELLED;
        owner->action_sequence = owner->requested_sequence;
        owner->action_ready = 1; owner->action_pending = 0;
    }
    owner->pending = 0;
    SetEvent(owner->stop); SetEvent(owner->cancel);
    LeaveCriticalSection(&owner->lock);
    if(WaitForSingleObject(owner->worker, grace_ms) != WAIT_OBJECT_0) return 0;
    /* Exit has been observed, so the UI may now attempt the bounded final
       session retirement. On failure all retryable ownership remains intact. */
    if(owner->retirement_failed && !wtop_provider_session_close(&owner->provider, 1000)) return 0;
    CloseHandle(owner->worker); owner->worker = NULL;
    CloseHandle(owner->wake); CloseHandle(owner->stop); CloseHandle(owner->cancel);
    free(owner->result); owner->result = NULL;
    DeleteCriticalSection(&owner->lock);
    wtop_affinity_outcome outcome = owner->action;
    ULONGLONG action_sequence = owner->action_sequence;
    int action_ready = owner->action_ready;
    memset(owner, 0, sizeof(*owner));
    /* Small, non-owning action evidence survives resource disposal until the
       caller polls it; an exit request never turns attempted into not-applied. */
    owner->action = outcome; owner->action_sequence = action_sequence; owner->action_ready = action_ready;
    return 1;
}
