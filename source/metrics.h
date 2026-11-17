/* Exact measurement policy; no native handles. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_METRICS_H
#define WTOP_METRICS_H
#include <limits.h>
/* Each limb contains exactly 32 significant bits, including on LP64 hosts.
   This representation is also usable when the target has no 64-bit integer. */
typedef struct wtop_counter { unsigned long high, low; } wtop_counter;
typedef char wtop_requires_32_bit_unsigned_long[(ULONG_MAX >= 4294967295UL) ? 1 : -1];
typedef enum wtop_metric_state {
    WTOP_METRIC_VALID, WTOP_METRIC_DENIED, WTOP_METRIC_UNSUPPORTED,
    WTOP_METRIC_WARMING_UP, WTOP_METRIC_RESET, WTOP_METRIC_ERROR,
    WTOP_METRIC_CAPACITY, WTOP_METRIC_STALE
} wtop_metric_state;
const char *wtop_metric_state_name(wtop_metric_state state);
int wtop_counter_compare(wtop_counter a, wtop_counter b);
int wtop_counter_add(wtop_counter a, wtop_counter b, wtop_counter *out);
int wtop_counter_subtract(wtop_counter a, wtop_counter b, wtop_counter *out);
int wtop_counter_multiply(wtop_counter a, unsigned long factor, wtop_counter *out);
double wtop_counter_double(wtop_counter value);

typedef struct wtop_cpu_observation {
    wtop_counter kernel, user, before, after, lifetime;
    unsigned long epoch;
    wtop_metric_state state;
} wtop_cpu_observation;
typedef struct wtop_cpu_value {
    wtop_metric_state state;
    double core_percent, machine_percent, elapsed_ms, acquisition_ms;
} wtop_cpu_value;
/* Counters are 100 ns execution units; timestamps use ticks_per_second.
   Observations must share epoch, clock frequency and lifetime. Capacity zero
   leaves machine_percent unavailable to the caller; core_percent is independent. */
wtop_metric_state wtop_process_cpu(const wtop_cpu_observation *previous,
    const wtop_cpu_observation *current, double ticks_per_second,
    unsigned long capacity, wtop_cpu_value *out);
wtop_metric_state wtop_host_cpu(wtop_counter previous_kernel, wtop_counter kernel,
    wtop_counter previous_user, wtop_counter user, wtop_counter previous_idle,
    wtop_counter idle, double *busy_fraction);
wtop_metric_state wtop_commit_bytes(wtop_counter pages, wtop_counter limit_pages,
    unsigned long page_size, wtop_counter *bytes, wtop_counter *limit_bytes,
    double *fraction);
wtop_metric_state wtop_sample_freshness(wtop_counter now_ms,
    wtop_counter observed_ms, unsigned long valid_for_ms, double *age_ms);
#endif
