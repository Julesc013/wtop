/* Wtop portable behavioral tests. SPDX-License-Identifier: GPL-3.0-only */
#include "core.h"
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
    assert(wtop_parse_options(1, defaults, &options, &error));
    assert(options.mode == WTOP_TUI && options.interval_ms == 1000);
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
    numeric_test();
    json_test();
    puts("core: arguments, numeric bounds, elapsed rates and JSON escaping passed");
    return 0;
}
