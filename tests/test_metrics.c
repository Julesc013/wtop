/* Independent arithmetic fixtures. SPDX-License-Identifier: GPL-3.0-only */
#include "metrics.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static wtop_counter c(unsigned long high,unsigned long low) { wtop_counter v; v.high=high; v.low=low; return v; }
static void near(double actual,double expected) { assert(fabs(actual-expected)<0.000001); }
static wtop_cpu_observation observation(unsigned long before,unsigned long after,unsigned long low)
{
    wtop_cpu_observation o;
    memset(&o,0,sizeof(o));
    o.kernel=c(2097152UL,low); /* above 2^53: do not round before subtraction */
    o.before=c(0,before); o.after=c(0,after); o.lifetime=c(1,42);
    o.state=WTOP_METRIC_VALID; o.epoch=3;
    return o;
}
int main(void)
{
    wtop_counter result=c(7,7),bytes,limit;
    wtop_cpu_observation a,b;
    wtop_cpu_value value;
    double ratio,age;
    assert(wtop_metric_sort_availability(WTOP_METRIC_VALID, WTOP_METRIC_DENIED) == -1);
    assert(wtop_metric_sort_availability(WTOP_METRIC_STALE, WTOP_METRIC_VALID) == 1);
    assert(wtop_metric_sort_availability(WTOP_METRIC_ERROR, WTOP_METRIC_WARMING_UP) == 0);
    assert(wtop_metric_sort_availability(WTOP_METRIC_VALID, WTOP_METRIC_VALID) == 0);
    assert(wtop_counter_subtract(c(2097152UL,16),c(2097152UL,1),&result));
    assert(result.high==0 && result.low==15);
    assert(wtop_counter_subtract(c(2,0),c(1,4294967295UL),&result));
    assert(result.high==0 && result.low==1);
    assert(wtop_counter_add(c(0,4294967295UL),c(0,1),&result));
    assert(result.high==1 && result.low==0);
    result=c(7,7);
    assert(!wtop_counter_add(c(4294967295UL,4294967295UL),c(0,1),&result));
    assert(result.high==7 && result.low==7);
    assert(!wtop_counter_subtract(c(0,0),c(0,1),&result));
    assert(wtop_counter_multiply(c(0,786432),4096,&bytes));
    assert(bytes.high==0 && bytes.low==3221225472UL);
    assert(wtop_commit_bytes(c(0,786432),c(0,2097152),4096,&bytes,&limit,&ratio)==WTOP_METRIC_VALID);
    assert(limit.high==2 && limit.low==0); near(ratio,0.375);
    assert(wtop_commit_bytes(c(4294967295UL,0),c(1,0),4096,&bytes,&limit,&ratio)==WTOP_METRIC_CAPACITY);
    assert(wtop_commit_bytes(c(0,1),c(0,0),4096,&bytes,&limit,&ratio)==WTOP_METRIC_ERROR);
    /* Midpoints 100 and 270, exact delta 1700000 * 100 ns => 100% of one core. */
    a=observation(80,120,1); b=observation(250,290,1700001);
    assert(wtop_process_cpu(&a,&b,1000,4,&value)==WTOP_METRIC_VALID);
    near(value.core_percent,100); near(value.machine_percent,25);
    near(value.elapsed_ms,170); near(value.acquisition_ms,80);
    b.kernel.low=3400001;
    assert(wtop_process_cpu(&a,&b,1000,4,&value)==WTOP_METRIC_VALID);
    near(value.core_percent,200); near(value.machine_percent,50);
    b.kernel.low=1700001; b.epoch=4;
    assert(wtop_process_cpu(&a,&b,1000,4,&value)==WTOP_METRIC_RESET);
    b.epoch=3; b.lifetime.low=43;
    assert(wtop_process_cpu(&a,&b,1000,4,&value)==WTOP_METRIC_RESET);
    b.lifetime.low=42; b.kernel.low=0;
    assert(wtop_process_cpu(&a,&b,1000,4,&value)==WTOP_METRIC_RESET);
    b.state=WTOP_METRIC_DENIED;
    assert(wtop_process_cpu(&a,&b,1000,4,&value)==WTOP_METRIC_DENIED);
    b.state=WTOP_METRIC_VALID; a.state=WTOP_METRIC_ERROR;
    assert(wtop_process_cpu(&a,&b,1000,4,&value)==WTOP_METRIC_WARMING_UP);
    a=observation(100,100,1); b=observation(100,100,1);
    assert(wtop_process_cpu(&a,&b,1000,4,&value)==WTOP_METRIC_RESET);
    b=observation(250,290,1700001);
    assert(wtop_process_cpu(&a,&b,1000,0,&value)==WTOP_METRIC_VALID);
    near(value.core_percent,100); /* Missing machine capacity does not spoil core rate. */
    assert(wtop_host_cpu(c(1,0),c(1,800),c(0,0),c(0,200),c(0,0),c(0,300),&ratio)==WTOP_METRIC_VALID);
    near(ratio,0.7);
    assert(wtop_host_cpu(c(0,0),c(0,1),c(0,0),c(0,0),c(0,0),c(0,2),&ratio)==WTOP_METRIC_RESET);
    assert(wtop_sample_freshness(c(0,150),c(0,100),50,&age)==WTOP_METRIC_VALID); near(age,50);
    assert(wtop_sample_freshness(c(0,151),c(0,100),50,&age)==WTOP_METRIC_STALE);
    assert(wtop_sample_freshness(c(0,99),c(0,100),50,&age)==WTOP_METRIC_RESET);
    assert(strcmp(wtop_metric_state_name(WTOP_METRIC_WARMING_UP),"warming_up")==0);
    return 0;
}
