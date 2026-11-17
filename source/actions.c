/* Bounded batch policy. SPDX-License-Identifier: GPL-3.0-only */
#include "actions.h"
#include <string.h>
int wtop_action_success(wtop_action_state state)
{
    return state == WTOP_ACTION_APPLIED || state == WTOP_ACTION_REQUESTED ||
        state == WTOP_ACTION_CONFIRMED_EXITED || state == WTOP_ACTION_ALREADY_EXITED;
}
const char *wtop_action_state_name(wtop_action_state state)
{
    static const char *names[] = { "applied", "requested", "confirmed_exited", "already_exited",
        "identity_changed", "identity_unavailable", "denied", "unsupported", "safe_limit",
        "not_attempted", "failed", "indeterminate" };
    return state >= WTOP_ACTION_APPLIED && state <= WTOP_ACTION_INDETERMINATE ? names[state] : "failed";
}
int wtop_actions_run(const wtop_action_request *requests, size_t count,
    wtop_action_result *results, size_t capacity, wtop_action_executor execute,
    wtop_action_stop should_stop, void *context)
{
    size_t i;
    int stopped = 0;
    if (count > capacity || (count && (!requests || !results)) || !execute) return 0;
    /* Reject an invalid operation anywhere before dispatching any target. This
       prevents a malformed batch from becoming a partially applied mutation. */
    for (i=0;i<count;++i)
        if (requests[i].kind < WTOP_ACTION_TERMINATE ||
            requests[i].kind > WTOP_ACTION_PRIORITY_DOWN) return 0;
    for (i=0;i<count;++i) {
        memset(&results[i],0,sizeof(results[i]));
        results[i].request = requests[i];
        results[i].state = WTOP_ACTION_NOT_ATTEMPTED;
    }
    for (i=0;i<count;++i) {
        if (!stopped && should_stop && should_stop(context)) stopped = 1;
        if (stopped) continue;
        results[i].state = WTOP_ACTION_FAILED;
        execute(context,&requests[i],&results[i]);
    }
    return 1;
}
