/* Exact measurement policy. SPDX-License-Identifier: GPL-3.0-only */
#include "metrics.h"
#include <float.h>
#include <string.h>

int wtop_metric_sort_availability(wtop_metric_state left, wtop_metric_state right)
{
    return (right == WTOP_METRIC_VALID) - (left == WTOP_METRIC_VALID);
}
#define LIMB 4294967295UL
static int counter_valid(wtop_counter c) { return c.high <= LIMB && c.low <= LIMB; }
const char *wtop_metric_state_name(wtop_metric_state state)
{
    static const char *names[] = { "valid", "denied", "unsupported", "warming_up",
        "reset", "error", "capacity", "stale" };
    return state >= WTOP_METRIC_VALID && state <= WTOP_METRIC_STALE ? names[state] : "error";
}
int wtop_counter_compare(wtop_counter a, wtop_counter b)
{
    if (a.high != b.high) return a.high < b.high ? -1 : 1;
    return (a.low > b.low) - (a.low < b.low);
}
int wtop_counter_add(wtop_counter a, wtop_counter b, wtop_counter *out)
{
    wtop_counter result;
    unsigned long carry;
    if (!out || !counter_valid(a) || !counter_valid(b)) return 0;
    result.low = (a.low + b.low) & LIMB;
    carry = result.low < a.low;
    if (b.high > LIMB - a.high || carry > LIMB - a.high - b.high) return 0;
    result.high = a.high + b.high + carry;
    *out = result;
    return 1;
}
int wtop_counter_subtract(wtop_counter a, wtop_counter b, wtop_counter *out)
{
    wtop_counter result;
    if (!out || !counter_valid(a) || !counter_valid(b) || wtop_counter_compare(a,b) < 0) return 0;
    result.low = (a.low - b.low) & LIMB;
    result.high = a.high - b.high - (a.low < b.low);
    *out = result;
    return 1;
}
int wtop_counter_multiply(wtop_counter a, unsigned long factor, wtop_counter *out)
{
    wtop_counter result = {0,0};
    if (!out || !counter_valid(a) || factor > LIMB) return 0;
    while (factor) {
        if ((factor & 1UL) && !wtop_counter_add(result,a,&result)) return 0;
        factor >>= 1;
        if (factor && !wtop_counter_add(a,a,&a)) return 0;
    }
    *out = result;
    return 1;
}
double wtop_counter_double(wtop_counter c)
{
    return (double)c.high * 4294967296.0 + (double)c.low;
}
static wtop_counter half(wtop_counter c)
{
    wtop_counter result;
    result.high = c.high >> 1;
    result.low = (c.low >> 1) | ((c.high & 1UL) << 31);
    return result;
}
static int midpoint(wtop_counter before, wtop_counter after, wtop_counter *middle,
                    wtop_counter *span)
{
    return wtop_counter_subtract(after,before,span) &&
        wtop_counter_add(before,half(*span),middle);
}
wtop_metric_state wtop_process_cpu(const wtop_cpu_observation *previous,
    const wtop_cpu_observation *current, double ticks_per_second,
    unsigned long capacity, wtop_cpu_value *out)
{
    wtop_counter kernel, user, cpu, begin, end, duration, first_span, last_span;
    double elapsed;
    if (!out) return WTOP_METRIC_ERROR;
    memset(out,0,sizeof(*out));
    out->state = WTOP_METRIC_ERROR;
    if (!previous || !current || !(ticks_per_second > 0.0) || ticks_per_second > DBL_MAX) return out->state;
    if (current->state != WTOP_METRIC_VALID) return out->state = current->state;
    if (previous->state != WTOP_METRIC_VALID) return out->state = WTOP_METRIC_WARMING_UP;
    if (previous->epoch != current->epoch ||
        wtop_counter_compare(previous->lifetime,current->lifetime) != 0 ||
        !midpoint(previous->before,previous->after,&begin,&first_span) ||
        !midpoint(current->before,current->after,&end,&last_span) ||
        !wtop_counter_subtract(end,begin,&duration) ||
        (!duration.high && !duration.low) ||
        !wtop_counter_subtract(current->kernel,previous->kernel,&kernel) ||
        !wtop_counter_subtract(current->user,previous->user,&user))
        return out->state = WTOP_METRIC_RESET;
    if (!wtop_counter_add(kernel,user,&cpu)) return out->state = WTOP_METRIC_CAPACITY;
    elapsed = wtop_counter_double(duration) / ticks_per_second;
    if (!(elapsed > 0.0) || elapsed > DBL_MAX) return out->state = WTOP_METRIC_RESET;
    out->core_percent = wtop_counter_double(cpu) / (elapsed * 100000.0);
    out->machine_percent = capacity ? out->core_percent / (double)capacity : 0.0;
    out->elapsed_ms = elapsed * 1000.0;
    out->acquisition_ms = (wtop_counter_double(first_span) + wtop_counter_double(last_span)) / ticks_per_second * 1000.0;
    if (!(out->core_percent >= 0.0) || out->core_percent > DBL_MAX ||
        out->elapsed_ms > DBL_MAX || out->acquisition_ms > DBL_MAX)
        return out->state = WTOP_METRIC_CAPACITY;
    return out->state = WTOP_METRIC_VALID;
}
wtop_metric_state wtop_host_cpu(wtop_counter previous_kernel, wtop_counter kernel,
    wtop_counter previous_user, wtop_counter user, wtop_counter previous_idle,
    wtop_counter idle, double *busy_fraction)
{
    wtop_counter dk,du,di,total,busy;
    if (!busy_fraction) return WTOP_METRIC_ERROR;
    *busy_fraction = 0;
    if (!wtop_counter_subtract(kernel,previous_kernel,&dk) ||
        !wtop_counter_subtract(user,previous_user,&du) ||
        !wtop_counter_subtract(idle,previous_idle,&di)) return WTOP_METRIC_RESET;
    if (!wtop_counter_add(dk,du,&total)) return WTOP_METRIC_CAPACITY;
    if ((!total.high && !total.low) || !wtop_counter_subtract(total,di,&busy)) return WTOP_METRIC_RESET;
    *busy_fraction = wtop_counter_double(busy)/wtop_counter_double(total);
    return WTOP_METRIC_VALID;
}
wtop_metric_state wtop_commit_bytes(wtop_counter pages, wtop_counter limit_pages,
    unsigned long page_size, wtop_counter *bytes, wtop_counter *limit_bytes,
    double *fraction)
{
    wtop_counter measured_bytes, measured_limit;
    double measured_fraction;
    if (!bytes || !limit_bytes || !fraction || bytes == limit_bytes) return WTOP_METRIC_ERROR;
    if (!page_size || (!limit_pages.high && !limit_pages.low)) return WTOP_METRIC_ERROR;
    /* Publish all outputs together. An overflowing limit must not leave a new
       byte count paired with an old limit/fraction in caller-owned storage. */
    if (!wtop_counter_multiply(pages,page_size,&measured_bytes) ||
        !wtop_counter_multiply(limit_pages,page_size,&measured_limit)) return WTOP_METRIC_CAPACITY;
    measured_fraction = wtop_counter_double(pages)/wtop_counter_double(limit_pages);
    *bytes = measured_bytes;
    *limit_bytes = measured_limit;
    *fraction = measured_fraction;
    return WTOP_METRIC_VALID;
}
wtop_metric_state wtop_sample_freshness(wtop_counter now_ms,
    wtop_counter observed_ms, unsigned long valid_for_ms, double *age_ms)
{
    wtop_counter age;
    if (!age_ms) return WTOP_METRIC_ERROR;
    if (!wtop_counter_subtract(now_ms,observed_ms,&age)) return WTOP_METRIC_RESET;
    *age_ms = wtop_counter_double(age);
    return *age_ms > (double)valid_for_ms ? WTOP_METRIC_STALE : WTOP_METRIC_VALID;
}
