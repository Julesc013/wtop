/* Wtop shared command and serialization helpers. SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
#include "settings.h"
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
        *error = "choose only one information or configuration-writing action";
        return 0;
    }
    options->action = action;
    return 1;
}

/* Aliases preserve inherited meanings independently of the selected keymap. */
static const char *canonical_option(const char *name)
{
    static const struct { const char *name; const char *canonical; } aliases[] = {
        { "-V", "--version" }, { "-M", "--no-mouse" },
        { "--pid", "-p" }, { "--name", "-n" }, { "--user", "-u" },
        { "--sort-key", "-s" }, { "--no-color", "-C" },
        { "--no-colour", "-C" }, { "--function-bar", "--status-bar" },
        { "--no-function-bar", "--no-status-bar" }
    };
    size_t i;
    for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
        if (!strcmp(name, aliases[i].name)) return aliases[i].canonical;
    return name;
}

static int option_takes_value(const char *name)
{
    static const char *const names[] = {
        "--footer", "--status-position", "--character-set", "--frame-interval",
        "--format", "--interval", "--delay", "--config", "--keymap",
        "--theme", "--mouse", "--write-config", "--cpu-normalization",
        "--portable-root", "--samples", "--duration", "--max-bytes", "--output",
        "-p", "-n", "-u", "-s"
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(name, names[i])) return 1;
    return 0;
}

int wtop_parse_options(int argc, const char *const *argv, wtop_options *options, const char **error)
{
    int i;
    int mode_set;
    int format_set;
    int max_bytes_set;
    const char *arg;
    const char *value;
    const char *equals;
    const char *inline_value;
    char option_name[64];
    size_t option_length;
    memset(options, 0, sizeof(*options));
    options->mode = WTOP_TUI;
    options->interval_ms = 1000;
    options->max_bytes = 16777216UL;
    options->mouse_mode = -1;
    options->show_function_bar = -1;
    *error = NULL;
    mode_set = 0;
    format_set = 0;
    max_bytes_set = 0;
    for (i = 1; i < argc; ++i) {
        arg = argv[i];
        if (!strcmp(arg, "--")) {
            if (i + 1 != argc) {
                *error = "positional arguments are not accepted after --";
                return 0;
            }
            break;
        }
        inline_value = NULL;
        equals = arg[0] == '-' && arg[1] == '-' ? strchr(arg, '=') : NULL;
        if (equals) {
            option_length = (size_t)(equals - arg);
            if (option_length >= sizeof(option_name)) {
                *error = "option name is too long"; return 0;
            }
            memcpy(option_name, arg, option_length);
            option_name[option_length] = '\0';
            arg = option_name;
            inline_value = equals + 1;
        }
        arg = canonical_option(arg);
        if (inline_value && !option_takes_value(arg)) {
            *error = "this option does not accept an =value"; return 0;
        }
        if (!strcmp(arg, "query") || !strcmp(arg, "inspect") || !strcmp(arg, "log")) {
            wtop_mode verb_mode;
            verb_mode = !strcmp(arg, "query") ? WTOP_QUERY :
                (!strcmp(arg, "inspect") ? WTOP_INSPECT : WTOP_LOG);
            if (mode_set) { *error = "choose one command or presentation mode"; return 0; }
            mode_set = 1;
            options->mode = verb_mode;
            if (verb_mode == WTOP_QUERY) {
                if (i + 1 == argc) { *error = "query requires processes, system or services"; return 0; }
                value = argv[++i];
                if (!strcmp(value, "processes")) options->query_target = WTOP_QUERY_PROCESSES;
                else if (!strcmp(value, "system")) options->query_target = WTOP_QUERY_SYSTEM;
                else if (!strcmp(value, "services")) options->query_target = WTOP_QUERY_SERVICES;
                else { *error = "query requires processes, system or services"; return 0; }
            } else if (verb_mode == WTOP_INSPECT) {
                if (i + 1 == argc || !wtop_parse_unsigned(argv[++i], 1, 4294967295UL, 10, &options->inspect_pid)) {
                    *error = "inspect requires a decimal PID in 1..4294967295"; return 0;
                }
            }
        } else if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            if (!set_action(options, WTOP_HELP, error)) return 0;
        } else if (!strcmp(arg, "--version") || !strcmp(arg, "-v")) {
            if (!set_action(options, WTOP_VERSION, error)) return 0;
        } else if (!strcmp(arg, "--capabilities")) {
            if (!set_action(options, WTOP_CAPABILITIES, error)) return 0;
        } else if (!strcmp(arg, "--paths")) {
            if (!set_action(options, WTOP_PATHS, error)) return 0;
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
        else if (!strcmp(arg, "--readonly")) options->readonly = 1;
        else if (!strcmp(arg, "--portable")) options->portable = 1;
        else if (!strcmp(arg, "--command-line")) options->command_line = 1;
        else if (!strcmp(arg, "--services")) options->services = 1;
        else if (!strcmp(arg, "--per-core")) options->per_core = 1;
        else if (!strcmp(arg, "--image-path")) options->image_path = 1;
        else if (!strcmp(arg, "--threads")) options->threads = 1;
        else if (!strcmp(arg, "--affinity")) options->affinity = 1;
        else if (!strcmp(arg, "--status-bar")) options->show_function_bar = 1;
        else if (!strcmp(arg, "--no-status-bar")) options->show_function_bar = 0;
        else if (!strcmp(arg, "--no-mouse")) options->mouse_mode = WTOP_MOUSE_OFF;
        else if (option_takes_value(arg)) {
            if (inline_value) value = inline_value;
            else if (i + 1 < argc) value = argv[++i];
            else value = NULL;
            if (!value || !value[0] || (value[0] == '-' &&
                (strcmp(arg, "--output") || strcmp(value, "-")))) {
                *error = "option requires a nonempty value";
                return 0;
            }
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
            } else if (!strcmp(arg, "--delay")) {
                unsigned long deciseconds;
                if (!wtop_parse_unsigned(value, 1, 600, 10, &deciseconds)) {
                    *error = "--delay must be 1..600 deciseconds"; return 0;
                }
                options->interval_ms = deciseconds * 100UL;
                options->interval_set = 1;
            } else if (!strcmp(arg, "--cpu-normalization")) {
                if (strcmp(value, "core") && strcmp(value, "machine")) {
                    *error = "--cpu-normalization must be core or machine"; return 0;
                }
                options->cpu_machine = !strcmp(value, "machine");
            } else if (!strcmp(arg, "--portable-root")) {
                options->portable_root = value;
                options->portable = 1;
            } else if (!strcmp(arg, "--samples")) {
                if (!wtop_parse_unsigned(value, 1, 1000000UL, 10, &options->sample_limit)) {
                    *error = "--samples must be 1..1000000"; return 0;
                }
            } else if (!strcmp(arg, "--duration")) {
                if (!wtop_parse_unsigned(value, 50, 86400000UL, 10, &options->duration_ms)) {
                    *error = "--duration must be 50..86400000 milliseconds"; return 0;
                }
            } else if (!strcmp(arg, "--max-bytes")) {
                if (!wtop_parse_unsigned(value, 1024, 1073741824UL, 10, &options->max_bytes)) {
                    *error = "--max-bytes must be 1024..1073741824"; return 0;
                }
                max_bytes_set = 1;
            } else if (!strcmp(arg, "--output")) options->output_path = value;
            else if (!strcmp(arg, "--config")) options->config_path = value;
            else if (!strcmp(arg, "--keymap")) {
                wtop_keymap keymap;
                if (!wtop_parse_keymap(value, &keymap)) {
                    *error = "--keymap must be htop, vi/vim or emacs";
                    return 0;
                }
                options->keymap_name = value;
            } else if (!strcmp(arg, "--footer")) {
                wtop_footer_style style;
                if (strcmp(value, "hidden") && !wtop_parse_footer_style(value, &style)) {
                    *error = "--footer must be functions, compact, status or hidden"; return 0;
                }
                options->footer_style = value;
            } else if (!strcmp(arg, "--status-position")) {
                wtop_status_position position;
                if (!wtop_parse_status_position(value, &position)) {
                    *error = "--status-position must be banner, bottom or hidden"; return 0;
                }
                options->status_position = value;
            } else if (!strcmp(arg, "--character-set")) {
                wtop_character_set characters;
                if (!wtop_parse_character_set(value, &characters)) {
                    *error = "--character-set must be ascii or unicode"; return 0;
                }
                options->character_set = value;
            } else if (!strcmp(arg, "--frame-interval")) {
                if (!wtop_parse_unsigned(value, 16, 1000, 10, &options->frame_interval_ms)) {
                    *error = "--frame-interval must be 16..1000 milliseconds"; return 0;
                }
            } else if (!strcmp(arg, "--theme")) {
                wtop_theme theme;
                if (!wtop_parse_theme(value, &theme)) {
                    *error = "--theme must be htop, ntop or monochrome";
                    return 0;
                }
                options->theme_name = value;
            } else if (!strcmp(arg, "--mouse")) {
                wtop_mouse mouse;
                if (!wtop_parse_mouse(value, &mouse)) {
                    *error = "--mouse must be auto, on or off";
                    return 0;
                }
                options->mouse_mode = (int)mouse;
            } else if (!strcmp(arg, "--write-config")) {
                if (options->write_config_path && strcmp(options->write_config_path, value)) {
                    *error = "--write-config cannot name more than one destination";
                    return 0;
                }
                if (!set_action(options, WTOP_WRITE_CONFIG, error)) return 0;
                options->write_config_path = value;
            }
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
    if (options->portable && options->config_path) {
        *error = "--portable/--portable-root and --config select different stores"; return 0;
    }
    if (options->mode == WTOP_INSPECT && (options->pid_filter || options->name_filter || options->user_filter)) {
        *error = "inspect selects exactly one PID; additional process selectors conflict"; return 0;
    }
    if (options->mode == WTOP_QUERY && options->query_target != WTOP_QUERY_PROCESSES &&
        (options->pid_filter || options->name_filter || options->user_filter)) {
        *error = "process selectors apply to query processes; use inspect PID for service association"; return 0;
    }
    if ((options->command_line || options->services || options->image_path ||
         options->threads || options->affinity) && options->mode != WTOP_INSPECT) {
        *error = "--command-line, --services, --image-path, --threads and --affinity require inspect PID"; return 0;
    }
    if (options->mode == WTOP_LOG) {
        if (!options->sample_limit && !options->duration_ms) {
            *error = "log requires --samples or --duration"; return 0;
        }
        if (!options->output_path) { *error = "log requires --output PATH or --output -"; return 0; }
        if (format_set && !options->json) { *error = "log writes JSON Lines; --format text is unsupported"; return 0; }
        options->json = 1;
    } else if (options->sample_limit || options->duration_ms || options->output_path || max_bytes_set) {
        *error = "--samples/--duration/--max-bytes/--output apply only to log"; return 0;
    }
    if (options->action == WTOP_WRITE_CONFIG && (mode_set || format_set)) {
        *error = "--write-config exits after saving; presentation and format options do not apply";
        return 0;
    }
    if (options->readonly && options->action == WTOP_WRITE_CONFIG) {
        *error = "--readonly forbids configuration writes";
        return 0;
    }
    if (options->action == WTOP_WRITE_CONFIG &&
        (options->pid_filter || options->name_filter || options->user_filter)) {
        *error = "process selectors are not saved by --write-config";
        return 0;
    }
    if (options->action == WTOP_RUN && format_set) {
        if (mode_set && (options->mode == WTOP_TUI || options->mode == WTOP_GUI)) {
            *error = "--format is for snapshots or informational commands";
            return 0;
        }
        if (!mode_set) options->mode = WTOP_SNAPSHOT;
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
