/* Wtop shared command and serialization helpers. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_CORE_H
#define WTOP_CORE_H
#include <stddef.h>
#include <stdio.h>
typedef enum wtop_action { WTOP_RUN, WTOP_HELP, WTOP_VERSION, WTOP_CAPABILITIES } wtop_action;
typedef enum wtop_mode { WTOP_TUI, WTOP_SNAPSHOT, WTOP_GUI } wtop_mode;
typedef struct wtop_options {
    wtop_action action;
    wtop_mode mode;
    int json;
    int monochrome;
    int no_config;
    int interval_set;
    unsigned long interval_ms;
    const char *config_path;
    const char *pid_filter;
    const char *name_filter;
    const char *user_filter;
    const char *sort_column;
} wtop_options;
/* argv remains owned by the caller; output strings reference argv. */
int wtop_parse_options(int argc, const char *const *argv, wtop_options *options, const char **error);
int wtop_parse_unsigned(const char *text, unsigned long minimum, unsigned long maximum, int base, unsigned long *value);
/* Serializes UTF-8 bytes; escaping preserves non-ASCII UTF-8 bytes. */
int wtop_json_string(FILE *output, const char *text);
/* False means unavailable/reset counter or time did not advance. */
int wtop_counter_rate(double previous, double current, double elapsed_ms, double *per_second);
#endif
