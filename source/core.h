/* Wtop shared command and serialization helpers. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_CORE_H
#define WTOP_CORE_H
#include <stddef.h>
#include <stdio.h>
typedef enum wtop_action {
    WTOP_RUN, WTOP_HELP, WTOP_VERSION, WTOP_CAPABILITIES, WTOP_WRITE_CONFIG,
    WTOP_PATHS
} wtop_action;
typedef enum wtop_mode {
    WTOP_TUI, WTOP_SNAPSHOT, WTOP_GUI, WTOP_QUERY, WTOP_INSPECT, WTOP_LOG
} wtop_mode;
typedef enum wtop_query_target {
    WTOP_QUERY_PROCESSES, WTOP_QUERY_SYSTEM, WTOP_QUERY_SERVICES
} wtop_query_target;
typedef struct wtop_options {
    wtop_action action;
    wtop_mode mode;
    int json;
    int monochrome;
    int no_config;
    int cpu_machine; /* 0: one logical CPU is 100%; 1: declared machine capacity. */
    int readonly; /* Common mutation/launch policy; not merely a hidden key. */
    int portable;
    const char *portable_root;
    wtop_query_target query_target;
    unsigned long inspect_pid;
    int command_line; /* Sensitive command arguments are strictly opt-in. */
    int services;
    int per_core;
    int image_path;
    int threads;
    int affinity; /* Read-only inspection; mutations require explicit TUI confirmation. */
    unsigned long sample_limit; /* Zero: no count bound; log requires a time/count bound. */
    unsigned long duration_ms;
    unsigned long max_bytes;
    const char *output_path; /* Log destination; '-' is explicit stdout. */
    int interval_set;
    unsigned long interval_ms;
    const char *config_path;
    const char *pid_filter;
    const char *name_filter;
    const char *user_filter;
    const char *sort_column;
    const char *keymap_name;
    const char *theme_name;
    const char *write_config_path;
    const char *footer_style;
    const char *status_position;
    const char *character_set;
    unsigned long frame_interval_ms;
    int mouse_mode; /* -1 means no command-line override. */
    int show_function_bar; /* -1 means no command-line override. */
} wtop_options;
/* argv remains owned by the caller; output strings reference argv. */
int wtop_parse_options(int argc, const char *const *argv, wtop_options *options, const char **error);
int wtop_parse_unsigned(const char *text, unsigned long minimum, unsigned long maximum, int base, unsigned long *value);
/* Serializes UTF-8 bytes; escaping preserves non-ASCII UTF-8 bytes. */
int wtop_json_string(FILE *output, const char *text);
/* False means unavailable/reset counter or time did not advance. */
int wtop_counter_rate(double previous, double current, double elapsed_ms, double *per_second);
#endif
