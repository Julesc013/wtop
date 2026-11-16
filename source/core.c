/* Wtop shared command and serialization helpers. SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
#include <errno.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

int wtop_parse_unsigned(const char *text, unsigned long minimum, unsigned long maximum, int base, unsigned long *value)
{
    char *end;
    unsigned long number;
    if (!text || text[0] < '0' || text[0] > '9') return 0;
    errno = 0;
    number = strtoul(text, &end, base);
    if (errno == ERANGE || end == text || *end || number < minimum || number > maximum) return 0;
    *value = number;
    return 1;
}

static int set_action(wtop_options *options, wtop_action action, const char **error)
{
    if (options->action != WTOP_RUN && options->action != action) {
        *error = "choose only one of --help, --version and --capabilities";
        return 0;
    }
    options->action = action;
    return 1;
}

int wtop_parse_options(int argc, const char *const *argv, wtop_options *options, const char **error)
{
    int i;
    int mode_set;
    int format_set;
    const char *arg;
    const char *value;
    memset(options, 0, sizeof(*options));
    options->mode = WTOP_TUI;
    options->interval_ms = 1000;
    *error = NULL;
    mode_set = 0;
    format_set = 0;
    for (i = 1; i < argc; ++i) {
        arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            if (!set_action(options, WTOP_HELP, error)) return 0;
        } else if (!strcmp(arg, "--version") || !strcmp(arg, "-v")) {
            if (!set_action(options, WTOP_VERSION, error)) return 0;
        } else if (!strcmp(arg, "--capabilities")) {
            if (!set_action(options, WTOP_CAPABILITIES, error)) return 0;
        } else if (!strcmp(arg, "--tui") || !strcmp(arg, "--gui") ||
                   !strcmp(arg, "--snapshot") || !strcmp(arg, "--dump") || !strcmp(arg, "-d")) {
            wtop_mode mode;
            mode = !strcmp(arg, "--tui") ? WTOP_TUI : (!strcmp(arg, "--gui") ? WTOP_GUI : WTOP_SNAPSHOT);
            if (mode_set && mode != options->mode) {
                *error = "choose only one presentation mode";
                return 0;
            }
            options->mode = mode;
            mode_set = 1;
        } else if (!strcmp(arg, "-C")) options->monochrome = 1;
        else if (!strcmp(arg, "--no-config")) options->no_config = 1;
        else if (!strcmp(arg, "--format") || !strcmp(arg, "--interval") || !strcmp(arg, "--config") ||
                 !strcmp(arg, "-p") || !strcmp(arg, "-n") || !strcmp(arg, "-u") || !strcmp(arg, "-s")) {
            if (i + 1 >= argc || !argv[i + 1][0] || argv[i + 1][0] == '-') {
                *error = "option requires a nonempty value";
                return 0;
            }
            value = argv[++i];
            if (!strcmp(arg, "--format")) {
                if (strcmp(value, "text") && strcmp(value, "json")) {
                    *error = "--format must be text or json";
                    return 0;
                }
                options->json = !strcmp(value, "json");
                format_set = 1;
            } else if (!strcmp(arg, "--interval")) {
                if (!wtop_parse_unsigned(value, 50, 60000, 10, &options->interval_ms)) {
                    *error = "--interval must be 50..60000 milliseconds";
                    return 0;
                }
                options->interval_set = 1;
            } else if (!strcmp(arg, "--config")) options->config_path = value;
            else if (!strcmp(arg, "-p")) options->pid_filter = value;
            else if (!strcmp(arg, "-n")) options->name_filter = value;
            else if (!strcmp(arg, "-u")) options->user_filter = value;
            else if (!strcmp(arg, "-s")) options->sort_column = value;
        } else {
            *error = "unknown option or unexpected positional argument";
            return 0;
        }
    }
    if (options->config_path && options->no_config) {
        *error = "--config and --no-config are mutually exclusive";
        return 0;
    }
    if (options->action == WTOP_RUN && format_set) {
        if (mode_set && options->mode != WTOP_SNAPSHOT) {
            *error = "--format is for snapshots or informational commands";
            return 0;
        }
        options->mode = WTOP_SNAPSHOT;
    }
    return 1;
}

int wtop_json_string(FILE *output, const char *text)
{
    const unsigned char *cursor;
    if (fputc('"', output) == EOF) return 0;
    cursor = (const unsigned char *)text;
    while (*cursor) {
        if (*cursor == '"' || *cursor == '\\') {
            if (fputc('\\', output) == EOF || fputc(*cursor, output) == EOF) return 0;
        } else if (*cursor < 0x20) {
            if (fprintf(output, "\\u%04x", (unsigned int)*cursor) < 0) return 0;
        } else if (fputc(*cursor, output) == EOF) return 0;
        ++cursor;
    }
    return fputc('"', output) != EOF;
}

int wtop_counter_rate(double previous, double current, double elapsed_ms, double *per_second)
{
    double result;
    if (!(previous >= 0) || !(current >= previous) || !(current <= DBL_MAX) ||
        !(elapsed_ms > 0) || !(elapsed_ms <= DBL_MAX)) return 0;
    result = (current - previous) * 1000.0 / elapsed_ms;
    if (!(result >= 0) || !(result <= DBL_MAX)) return 0;
    *per_second = result;
    return 1;
}
