/* Independent dispatch/outcome fixtures. SPDX-License-Identifier: GPL-3.0-only */
#include "actions.h"
#include <assert.h>
#include <string.h>
typedef struct fixture { unsigned count,stop_after; } fixture;
static int stop(void *context) { fixture *f=context; return f->count>=f->stop_after; }
static void execute(void *context,const wtop_action_request *request,wtop_action_result *result)
{
    fixture *f=context; ++f->count;
    if(request->target.pid==11) { result->state=WTOP_ACTION_DENIED; result->native_error=5; }
    else if(request->target.pid==12) result->state=WTOP_ACTION_REQUESTED;
    else if(request->target.pid==13) result->state=WTOP_ACTION_IDENTITY_CHANGED;
    else result->state=WTOP_ACTION_APPLIED;
}
int main(void)
{
    wtop_action_request request[4]; wtop_action_result result[4];
    fixture f; unsigned i;
    memset(request,0,sizeof(request));
    for(i=0;i<4;++i) { request[i].target.pid=10+i; request[i].target.creation.low=100+i; request[i].kind=WTOP_ACTION_TERMINATE; }
    f.count=0; f.stop_after=5;
    assert(!wtop_actions_run(request,4,result,3,execute,stop,&f)); assert(f.count==0);
    assert(wtop_actions_run(request,4,result,4,execute,stop,&f)); assert(f.count==4);
    assert(result[0].state==WTOP_ACTION_APPLIED);
    assert(result[1].state==WTOP_ACTION_DENIED && result[1].native_error==5);
    assert(result[2].state==WTOP_ACTION_REQUESTED);
    assert(result[3].state==WTOP_ACTION_IDENTITY_CHANGED);
    for(i=0;i<4;++i) { assert(result[i].request.target.pid==10+i); assert(result[i].request.target.creation.low==100+i); }
    assert(wtop_action_success(WTOP_ACTION_REQUESTED));
    assert(!wtop_action_success(WTOP_ACTION_INDETERMINATE));
    assert(strcmp(wtop_action_state_name(WTOP_ACTION_REQUESTED),"requested")==0);
    f.count=0; f.stop_after=2;
    assert(wtop_actions_run(request,4,result,4,execute,stop,&f)); assert(f.count==2);
    assert(result[1].native_error==5 && result[2].state==WTOP_ACTION_NOT_ATTEMPTED && result[3].state==WTOP_ACTION_NOT_ATTEMPTED);
    assert(result[2].native_error==0);
    return 0;
}
