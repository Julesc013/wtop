/* Small native-loader observations shared across historical feasibility probes.
 * SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_LEGACY_PROBE_H
#define WTOP_LEGACY_PROBE_H
#include <stddef.h>
#include <stdio.h>
#define WTOP_PROBE_RECORDS 24
#define WTOP_PROBE_KEY_BYTES 64
#define WTOP_PROBE_VALUE_BYTES 256
#define WTOP_PROBE_JSON_BYTES 16384

typedef enum wtop_probe_state {
    WTOP_PROBE_OBSERVED, WTOP_PROBE_PRESENT, WTOP_PROBE_UNAVAILABLE,
    WTOP_PROBE_UNSUPPORTED, WTOP_PROBE_DENIED, WTOP_PROBE_FAILED
} wtop_probe_state;
typedef struct wtop_probe_record {
    char id[WTOP_PROBE_KEY_BYTES];
    wtop_probe_state state;
    unsigned long native_code;
    char value[WTOP_PROBE_VALUE_BYTES];
} wtop_probe_record;
typedef struct wtop_probe_report {
    char profile[WTOP_PROBE_KEY_BYTES];
    char format[16];
    unsigned int count;
    wtop_probe_record records[WTOP_PROBE_RECORDS];
} wtop_probe_report;

/* These probes establish observations only. Neither a version string, an export
 * address, a successful loader run nor a host test establishes monitor support.
 * Strings are bounded UTF-8; no implicit truncation, duplicates or zero-as-error. */
int wtop_probe_init(wtop_probe_report *report, const char *profile, const char *format);
int wtop_probe_add(wtop_probe_report *report, const char *id,
                    wtop_probe_state state, unsigned long native_code, const char *value);
int wtop_probe_add_number(wtop_probe_report *report, const char *id,
                           wtop_probe_state state, unsigned long native_code, unsigned long value);
int wtop_probe_json(const wtop_probe_report *report, char *output, size_t capacity);
int wtop_probe_write(const wtop_probe_report *report, FILE *output);
const char *wtop_probe_state_name(wtop_probe_state state);
/* Win16 has no inherited Win32 console. Its loader probe writes only to a named
 * explicit --output PATH. Paths may be fully quoted; no shell interpretation. */
int wtop_probe_output_path(const char *command_line, char *path, size_t capacity);
#endif
