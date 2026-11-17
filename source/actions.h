/* Bounded action requests/results. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_ACTIONS_H
#define WTOP_ACTIONS_H
#include <stddef.h>
#include "metrics.h"
typedef struct wtop_process_key { unsigned long pid; wtop_counter creation; } wtop_process_key;
typedef enum wtop_action_kind { WTOP_ACTION_TERMINATE, WTOP_ACTION_PRIORITY_UP, WTOP_ACTION_PRIORITY_DOWN } wtop_action_kind;
typedef enum wtop_action_state {
    WTOP_ACTION_APPLIED, WTOP_ACTION_REQUESTED, WTOP_ACTION_CONFIRMED_EXITED,
    WTOP_ACTION_ALREADY_EXITED, WTOP_ACTION_IDENTITY_CHANGED,
    WTOP_ACTION_IDENTITY_UNAVAILABLE, WTOP_ACTION_DENIED,
    WTOP_ACTION_UNSUPPORTED, WTOP_ACTION_SAFE_LIMIT, WTOP_ACTION_NOT_ATTEMPTED,
    WTOP_ACTION_FAILED, WTOP_ACTION_INDETERMINATE
} wtop_action_state;
typedef struct wtop_action_request { wtop_process_key target; wtop_action_kind kind; } wtop_action_request;
typedef struct wtop_action_result {
    wtop_action_request request;
    wtop_action_state state;
    unsigned long native_error, previous_priority, requested_priority;
    int local_shutdown;
} wtop_action_result;
typedef void (*wtop_action_executor)(void *context, const wtop_action_request *request,
                                    wtop_action_result *result);
typedef int (*wtop_action_stop)(void *context);
/* Synchronous callbacks only: neither callback nor context is retained. Results
   are caller-owned; capacity is checked before executing even the first action. */
int wtop_actions_run(const wtop_action_request *requests, size_t count,
    wtop_action_result *results, size_t capacity, wtop_action_executor execute,
    wtop_action_stop should_stop, void *context);
int wtop_action_success(wtop_action_state state);
const char *wtop_action_state_name(wtop_action_state state);
#endif
