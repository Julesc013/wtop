/* Owned asynchronous TUI inspection. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_TUI_INSPECTION_NT_H
#define WTOP_TUI_INSPECTION_NT_H
#include "providers_nt.h"

typedef struct wtop_tui_inspection {
    CRITICAL_SECTION lock;
    HANDLE worker, wake, cancel, stop;
    wtop_provider_session provider;
    wtop_provider_request request;
    wtop_provider_result *result;
    wtop_affinity_outcome action;
    ULONGLONG sequence, requested_sequence, result_sequence, action_sequence;
    ULONGLONG requested_mask;
    int initialized, pending, active, stopping, action_pending, action_ready;
    int retirement_failed, result_ready;
} wtop_tui_inspection;

/* Zero-initialize once. Lazy start owns all handles. UI owns request/poll;
   only worker uses the provider session. No UI callbacks execute on worker. */
int wtop_tui_inspection_start(wtop_tui_inspection *owner);
/* Replaces pending read; cancels active read cooperatively. Zero refuses
   admission (shutdown, unconsumed mutation outcome, or active mutation). */
ULONGLONG wtop_tui_inspection_request(wtop_tui_inspection *owner,
    const wtop_provider_request *request);
/* An explicit confirmed mutation, admitted only when no work is active or
   pending. It is never coalesced, retried or overwritten. */
ULONGLONG wtop_tui_inspection_apply(wtop_tui_inspection *owner,
    DWORD pid, FILETIME creation, ULONGLONG mask, int readonly);
/* Transfers owned result to UI. Caller frees it. Generation is exact. */
wtop_provider_result *wtop_tui_inspection_poll(wtop_tui_inspection *owner,
    ULONGLONG *sequence);
/* Also callable after successful stop to receive its final queued/active
   action outcome. No native resources remain in that retained small value. */
int wtop_tui_inspection_poll_action(wtop_tui_inspection *owner,
    wtop_affinity_outcome *outcome, ULONGLONG *sequence);
void wtop_tui_inspection_cancel(wtop_tui_inspection *owner);
/* False retains all worker-reachable storage/handles; caller must not free
   owner or exit its storage lifetime while native work is still running. */
int wtop_tui_inspection_stop(wtop_tui_inspection *owner, DWORD grace_ms);
#endif
