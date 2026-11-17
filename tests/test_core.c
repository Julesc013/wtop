/* Wtop portable behavioral tests. SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
#include "settings.h"
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

static void options_test(void)
{
    wtop_options options;
    const char *error;
    const char *defaults[] = { "wtop" };
    const char *snapshot[] = { "wtop", "--format", "json", "--interval", "250" };
    const char *metadata[] = { "wtop", "--gui", "--format", "json", "--capabilities" };
    const char *bad_interval[] = { "wtop", "--interval", "-1" };
    const char *bad_suffix[] = { "wtop", "--interval", "100ms" };
    const char *bad_mode[] = { "wtop", "--tui", "--snapshot" };
    const char *missing[] = { "wtop", "-p" };
    const char *bad_format[] = { "wtop", "--format", "xml" };
    const char *bad_tui[] = { "wtop", "--tui", "--format", "json" };
    const char *bad_path[] = { "wtop", "--config", "x", "--no-config" };
    const char *unknown[] = { "wtop", "--count", "10" };
    const char *presentation[] = { "wtop", "--theme", "ntop", "--keymap", "vim", "--mouse", "off", "--no-status-bar" };
    const char *write_config[] = { "wtop", "--no-config", "--write-config", "settings.conf", "--theme", "monochrome" };
    const char *write_mode[] = { "wtop", "--write-config", "settings.conf", "--snapshot" };
    const char *write_format[] = { "wtop", "--write-config", "settings.conf", "--format", "json" };
    const char *write_metadata[] = { "wtop", "--write-config", "settings.conf", "--help" };
    const char *bad_keymap[] = { "wtop", "--keymap", "wordstar" };
    const char *bad_theme[] = { "wtop", "--theme", "custom-script" };
    const char *bad_mouse[] = { "wtop", "--mouse", "yes" };
    const char *write_selector[] = { "wtop", "--write-config", "settings.conf", "-p", "123" };
    const char *write_two_paths[] = { "wtop", "--write-config", "first.conf", "--write-config", "second.conf" };
    assert(wtop_parse_options(1, defaults, &options, &error));
    assert(options.mode == WTOP_TUI && options.interval_ms == 1000);
    assert(options.mouse_mode == -1 && options.show_function_bar == -1);
    assert(wtop_parse_options(5, snapshot, &options, &error));
    assert(options.mode == WTOP_SNAPSHOT && options.json && options.interval_ms == 250);
    assert(wtop_parse_options(5, metadata, &options, &error));
    assert(options.action == WTOP_CAPABILITIES && options.json);
    assert(!wtop_parse_options(3, bad_interval, &options, &error));
    assert(!wtop_parse_options(3, bad_suffix, &options, &error));
    assert(!wtop_parse_options(3, bad_mode, &options, &error));
    assert(!wtop_parse_options(2, missing, &options, &error));
    assert(!wtop_parse_options(3, bad_format, &options, &error));
    assert(!wtop_parse_options(4, bad_tui, &options, &error));
    assert(!wtop_parse_options(4, bad_path, &options, &error));
    assert(!wtop_parse_options(3, unknown, &options, &error));
    assert(wtop_parse_options(8, presentation, &options, &error));
    assert(!strcmp(options.theme_name, "ntop") && !strcmp(options.keymap_name, "vim"));
    assert(options.mouse_mode == WTOP_MOUSE_OFF && options.show_function_bar == 0);
    assert(wtop_parse_options(6, write_config, &options, &error));
    assert(options.action == WTOP_WRITE_CONFIG && options.no_config);
    assert(!strcmp(options.write_config_path, "settings.conf"));
    assert(!wtop_parse_options(4, write_mode, &options, &error));
    assert(!wtop_parse_options(5, write_format, &options, &error));
    assert(!wtop_parse_options(4, write_metadata, &options, &error));
    assert(!wtop_parse_options(3, bad_keymap, &options, &error));
    assert(!wtop_parse_options(3, bad_theme, &options, &error));
    assert(!wtop_parse_options(3, bad_mouse, &options, &error));
    assert(!wtop_parse_options(5, write_selector, &options, &error));
    assert(!wtop_parse_options(5, write_two_paths, &options, &error));
}

static void compatibility_test(void)
{
    wtop_options options;
    const char *error;
    const char *aliases[] = { "wtop", "-d", "--pid=42", "--name=worker",
        "--user=alice", "--sort-key=CPU%", "--no-colour", "-M",
        "--no-function-bar", "--cpu-normalization=machine", "--readonly" };
    const char *delay[] = { "wtop", "--delay=3", "--" };
    const char *core[] = { "wtop", "--cpu-normalization", "core", "-n", "worker" };
    const char *metadata[] = { "wtop", "-V" };
    const char *bare_value[] = { "wtop", "--readonly=yes" };
    const char *empty_value[] = { "wtop", "--name=" };
    const char *bad_cpu[] = { "wtop", "--cpu-normalization=host" };
    const char *after_end[] = { "wtop", "--", "--help" };
    const char *bad_delay[] = { "wtop", "--delay=601" };
    const char *read_write[] = { "wtop", "--readonly", "--write-config=out.conf" };
    const char *precedence[] = { "wtop", "--delay=3", "--interval=250" };
    assert(wtop_parse_options(11, aliases, &options, &error));
    assert(options.mode == WTOP_SNAPSHOT && options.cpu_machine && options.readonly);
    assert(!strcmp(options.pid_filter, "42") && !strcmp(options.name_filter, "worker"));
    assert(!strcmp(options.user_filter, "alice") && !strcmp(options.sort_column, "CPU%"));
    assert(options.monochrome && options.mouse_mode == WTOP_MOUSE_OFF && options.show_function_bar == 0);
    assert(wtop_parse_options(3, delay, &options, &error) && options.interval_ms == 300);
    assert(wtop_parse_options(5, core, &options, &error) && !options.cpu_machine);
    assert(!strcmp(options.name_filter, "worker"));
    assert(wtop_parse_options(2, metadata, &options, &error) && options.action == WTOP_VERSION);
    assert(!wtop_parse_options(2, bare_value, &options, &error));
    assert(!wtop_parse_options(2, empty_value, &options, &error));
    assert(!wtop_parse_options(2, bad_cpu, &options, &error));
    assert(!wtop_parse_options(3, after_end, &options, &error));
    assert(!wtop_parse_options(2, bad_delay, &options, &error));
    assert(!wtop_parse_options(3, read_write, &options, &error));
    assert(wtop_parse_options(3, precedence, &options, &error) && options.interval_ms == 250);
}

static void numeric_test(void)
{
    unsigned long value;
    double rate;
    assert(wtop_parse_unsigned("0xff", 0, 255, 0, &value) && value == 255);
    assert(!wtop_parse_unsigned("256", 0, 255, 0, &value));
    assert(!wtop_parse_unsigned("-1", 0, 255, 0, &value));
    assert(!wtop_parse_unsigned("+1", 0, 255, 0, &value));
    assert(!wtop_parse_unsigned(" 1", 0, 255, 0, &value));
    assert(!wtop_parse_unsigned("\v1", 0, 255, 0, &value));
    assert(!wtop_parse_unsigned("1x", 0, 255, 0, &value));
    assert(!wtop_parse_unsigned("999999999999999999999999999999", 0, 255, 0, &value));
    assert(wtop_counter_rate(1000.0, 2500.0, 1500.0, &rate) && rate == 1000.0);
    assert(wtop_counter_rate(0.0, 8589934592.0, 1000.0, &rate) && rate == 8589934592.0);
    assert(!wtop_counter_rate(2500.0, 1000.0, 1000.0, &rate));
    assert(!wtop_counter_rate(0.0, 1.0, 0.0, &rate));
    assert(!wtop_counter_rate(0.0, DBL_MAX, 1.0, &rate));
}

static void command_test(void)
{
    wtop_options options;
    const char *error;
    const char *query[] = { "wtop", "--format=json", "query", "system", "--per-core" };
    const char *inspect[] = { "wtop", "inspect", "4294967295", "--command-line", "--services" };
    const char *log[] = { "wtop", "log", "--output", "-", "--samples=2", "--interval=50", "--max-bytes=8192" };
    const char *duration[] = { "wtop", "log", "--output=capture.jsonl", "--duration=500", "--samples=10" };
    const char *paths[] = { "wtop", "--paths", "--portable-root=C:\\tools\\wtop", "--format=json" };
    const char *invalid[][7] = {
        { "wtop", "query", NULL },
        { "wtop", "query", "garbage", NULL },
        { "wtop", "--snapshot", "query", "system", NULL },
        { "wtop", "inspect", "0", NULL },
        { "wtop", "inspect", "4294967296", NULL },
        { "wtop", "inspect", "42", "--pid=42", NULL },
        { "wtop", "--command-line", NULL },
        { "wtop", "query", "processes", "--services", NULL },
        { "wtop", "log", "--output=-", NULL },
        { "wtop", "log", "--samples=1", NULL },
        { "wtop", "log", "--samples=1", "--output=-", "--format=text", NULL },
        { "wtop", "log", "--samples=0", "--output=-", NULL },
        { "wtop", "log", "--samples=1000001", "--output=-", NULL },
        { "wtop", "log", "--duration=86400001", "--output=-", NULL },
        { "wtop", "log", "--samples=1", "--output=-", "--max-bytes=1023", NULL },
        { "wtop", "--max-bytes=16777216", NULL },
        { "wtop", "--portable", "--config=file.conf", NULL }
    };
    size_t i;
    int argc;
    assert(wtop_parse_options(5, query, &options, &error));
    assert(options.mode == WTOP_QUERY && options.query_target == WTOP_QUERY_SYSTEM && options.json && options.per_core);
    assert(wtop_parse_options(5, inspect, &options, &error));
    assert(options.mode == WTOP_INSPECT && options.inspect_pid == 4294967295UL && options.command_line && options.services);
    assert(wtop_parse_options(7, log, &options, &error));
    assert(options.mode == WTOP_LOG && options.json && options.sample_limit == 2 && options.interval_ms == 50 && options.max_bytes == 8192);
    assert(!strcmp(options.output_path, "-"));
    assert(wtop_parse_options(5, duration, &options, &error));
    assert(options.duration_ms == 500 && options.sample_limit == 10);
    assert(wtop_parse_options(4, paths, &options, &error));
    assert(options.action == WTOP_PATHS && options.portable && options.json);
    assert(!strcmp(options.portable_root, "C:\\tools\\wtop"));
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        for (argc = 0; invalid[i][argc]; ++argc) { }
        assert(!wtop_parse_options(argc, invalid[i], &options, &error));
    }
}

static void json_test(void)
{
    FILE *output;
    char result[256];
    size_t length;
    output = tmpfile();
    assert(output);
    assert(wtop_json_string(output, "quote\" slash\\ line\n tab\t \xc3\xa9"));
    rewind(output);
    length = fread(result, 1, sizeof(result) - 1, output);
    result[length] = '\0';
    assert(!strcmp(result, "\"quote\\\" slash\\\\ line\\u000a tab\\u0009 \xc3\xa9\""));
    assert(!fclose(output));
}

int main(void)
{
    options_test();
    compatibility_test();
    command_test();
    numeric_test();
    json_test();
    puts("core: arguments, numeric bounds, elapsed rates and JSON escaping passed");
    return 0;
}
