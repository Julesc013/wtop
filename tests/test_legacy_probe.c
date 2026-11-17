/* Independent bounded probe semantics, not evidence of historical runtime support.
 * SPDX-License-Identifier: GPL-3.0-only */
#include "legacy/probe.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static wtop_probe_report report, before;
static char output[WTOP_PROBE_JSON_BYTES];
int main(void)
{
    char path[32], too_long[WTOP_PROBE_VALUE_BYTES + 1], key[32];
    unsigned int i;
    assert(wtop_probe_init(&report, "os2-32-x86", "LX"));
    assert(wtop_probe_add_number(&report, "api.return_code", WTOP_PROBE_FAILED, 5, 0));
    assert(wtop_probe_add(&report, "process.cpu", WTOP_PROBE_UNSUPPORTED, 0, "not-implemented"));
    assert(wtop_probe_add(&report, "test.escaping", WTOP_PROBE_OBSERVED, 0, "quote\"slash\\line\n\xc3\xb6"));
    assert(wtop_probe_add_number(&report, "native.maximum", WTOP_PROBE_OBSERVED, 0, ULONG_MAX));
    assert(wtop_probe_json(&report, output, sizeof(output)));
    assert(strstr(output, "\"state\":\"failed\",\"native_code\":5,\"value\":\"0\""));
    assert(strstr(output, "\"qualification\":\"not-established\""));
    assert(strstr(output, "quote\\\"slash\\\\line\\u000a\xc3\xb6"));
    assert(!wtop_probe_json(&report, output, 4) && output[0] == 0);
    before = report;
    assert(!wtop_probe_add(&report, "process.cpu", WTOP_PROBE_OBSERVED, 0, "fake"));
    assert(!memcmp(&report, &before, sizeof(report)));
    memset(too_long, 'x', sizeof(too_long)); too_long[sizeof(too_long) - 1] = 0;
    assert(!wtop_probe_add(&report, "too.long", WTOP_PROBE_OBSERVED, 0, too_long));
    assert(!wtop_probe_add(&report, "bad.encoding", WTOP_PROBE_OBSERVED, 0, "\xc0\xaf"));
    assert(!memcmp(&report, &before, sizeof(report)));
    for (i = report.count; i < WTOP_PROBE_RECORDS; ++i) {
        sprintf(key, "count.%u", i); assert(wtop_probe_add(&report, key, WTOP_PROBE_PRESENT, 0, "export-address-only"));
    }
    assert(!wtop_probe_add(&report, "overflow", WTOP_PROBE_PRESENT, 0, "no"));
    assert(wtop_probe_json(&report, output, sizeof(output)));
    report.records[0].state = (wtop_probe_state)99;
    assert(!wtop_probe_json(&report, output, sizeof(output)));
    assert(wtop_probe_output_path("--output C:\\probe.json", path, sizeof(path)) && !strcmp(path, "C:\\probe.json"));
    assert(wtop_probe_output_path("--output \"C:\\space dir\\probe.json\"", path, sizeof(path)));
    assert(!wtop_probe_output_path("--output C:\\space dir\\probe.json", path, sizeof(path)));
    assert(!wtop_probe_output_path("--output \"unclosed", path, sizeof(path)));
    assert(!wtop_probe_output_path("--output x --extra", path, sizeof(path)));
    assert(!wtop_probe_output_path("", path, sizeof(path)));
    puts("legacy probe: bounded typed observations, unknown states, UTF-8, escaping and explicit output passed");
    return 0;
}
