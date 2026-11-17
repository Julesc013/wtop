/* SPDX-License-Identifier: GPL-3.0-only */
#include "probe.h"
#include "../text.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* An old compiler must report the widths actually used; this requires an
 * unsigned long of at least 32 bits, not a particular sizeof(int) or pointer. */
typedef char probe_long_at_least_32[(ULONG_MAX >= 4294967295UL) ? 1 : -1];
static int bounded_text(const char *value, size_t capacity, int token)
{
    size_t length = 0;
    unsigned char byte;
    if (!value) return 0;
    while (length < capacity && value[length]) {
        byte = (unsigned char)value[length];
        if (token && !((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '-' || byte == '+')) return 0;
        ++length;
    }
    return length < capacity && (!token || length) && wtop_text_valid_utf8(value, length);
}
const char *wtop_probe_state_name(wtop_probe_state state)
{
    static const char *names[] = { "observed", "present", "unavailable", "unsupported", "denied", "failed" };
    return state >= WTOP_PROBE_OBSERVED && state <= WTOP_PROBE_FAILED ? names[state] : "invalid";
}
int wtop_probe_init(wtop_probe_report *report, const char *profile, const char *format)
{
    if (!report || !bounded_text(profile, WTOP_PROBE_KEY_BYTES, 1) || !bounded_text(format, 16, 1)) return 0;
    memset(report, 0, sizeof(*report));
    strcpy(report->profile, profile); strcpy(report->format, format);
    return 1;
}
int wtop_probe_add(wtop_probe_report *report, const char *id,
                    wtop_probe_state state, unsigned long native_code, const char *value)
{
    unsigned int i;
    wtop_probe_record *record;
    if (!report || report->count >= WTOP_PROBE_RECORDS || state < WTOP_PROBE_OBSERVED || state > WTOP_PROBE_FAILED ||
        !bounded_text(id, WTOP_PROBE_KEY_BYTES, 1) || !bounded_text(value, WTOP_PROBE_VALUE_BYTES, 0)) return 0;
    for (i = 0; i < report->count; ++i) if (!strcmp(report->records[i].id, id)) return 0;
    record = &report->records[report->count++];
    strcpy(record->id, id); record->state = state; record->native_code = native_code; strcpy(record->value, value);
    return 1;
}
int wtop_probe_add_number(wtop_probe_report *report, const char *id,
                           wtop_probe_state state, unsigned long native_code, unsigned long value)
{
    char text[32];
    sprintf(text, "%lu", value);
    return wtop_probe_add(report, id, state, native_code, text);
}
typedef struct probe_writer {
    char *output;
    size_t capacity;
    size_t length;
    int failed;
} probe_writer;
static void bytes(probe_writer *writer, const char *text, size_t length)
{
    if (writer->failed) return;
    if (length >= writer->capacity - writer->length) { writer->failed = 1; return; }
    memcpy(writer->output + writer->length, text, length);
    writer->length += length;
}
static void text(probe_writer *writer, const char *value) { bytes(writer, value, strlen(value)); }
static void quoted(probe_writer *writer, const char *value)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char byte;
    char escape[6];
    text(writer, "\"");
    while (*value) {
        byte = (unsigned char)*value++;
        if (byte == '"' || byte == '\\') { text(writer, "\\"); bytes(writer, (const char *)&byte, 1); }
        else if (byte < 0x20) {
            memcpy(escape, "\\u00", 4); escape[4] = hex[byte >> 4]; escape[5] = hex[byte & 15]; bytes(writer, escape, 6);
        } else bytes(writer, (const char *)&byte, 1);
    }
    text(writer, "\"");
}
static int valid_report(const wtop_probe_report *report)
{
    unsigned int i, j;
    const wtop_probe_record *record;
    if (!report || report->count > WTOP_PROBE_RECORDS || !bounded_text(report->profile, sizeof(report->profile), 1) ||
        !bounded_text(report->format, sizeof(report->format), 1)) return 0;
    for (i = 0; i < report->count; ++i) {
        record = &report->records[i];
        if (!bounded_text(record->id, sizeof(record->id), 1) || !bounded_text(record->value, sizeof(record->value), 0) ||
            record->state < WTOP_PROBE_OBSERVED || record->state > WTOP_PROBE_FAILED) return 0;
        for (j = 0; j < i; ++j) if (!strcmp(record->id, report->records[j].id)) return 0;
    }
    return 1;
}
int wtop_probe_json(const wtop_probe_report *report, char *output, size_t capacity)
{
    probe_writer writer;
    unsigned int i;
    char number[32];
    const wtop_probe_record *record;
    if (output && capacity) output[0] = 0;
    if (!output || !capacity || !valid_report(report)) return 0;
    writer.output = output; writer.capacity = capacity; writer.length = 0; writer.failed = 0;
    text(&writer, "{\"schema\":\"wtop.loader-probe/1\",\"profile\":"); quoted(&writer, report->profile);
    text(&writer, ",\"format\":"); quoted(&writer, report->format);
    text(&writer, ",\"observations\":[");
    for (i = 0; i < report->count; ++i) {
        record = &report->records[i];
        if (i) text(&writer, ",");
        text(&writer, "{\"id\":"); quoted(&writer, record->id);
        text(&writer, ",\"state\":"); quoted(&writer, wtop_probe_state_name(record->state));
        text(&writer, ",\"native_code\":"); sprintf(number, "%lu", record->native_code); text(&writer, number);
        text(&writer, ",\"value\":"); quoted(&writer, record->value); text(&writer, "}");
    }
    text(&writer, "],\"qualification\":\"not-established\"}\n");
    if (writer.failed) { output[0] = 0; return 0; }
    output[writer.length] = 0;
    return 1;
}
int wtop_probe_write(const wtop_probe_report *report, FILE *output)
{
    char *buffer;
    size_t length;
    int ok;
    if (!output) return 0;
    buffer = (char *)malloc(WTOP_PROBE_JSON_BYTES);
    if (!buffer) return 0;
    ok = wtop_probe_json(report, buffer, WTOP_PROBE_JSON_BYTES);
    if (ok) { length = strlen(buffer); ok = fwrite(buffer, 1, length, output) == length && !ferror(output); }
    free(buffer);
    return ok;
}
int wtop_probe_output_path(const char *command_line, char *path, size_t capacity)
{
    const char *value;
    size_t used = 0;
    int quoted_path;
    if (!command_line || !path || !capacity || strncmp(command_line, "--output ", 9)) return 0;
    value = command_line + 9;
    while (*value == ' ') ++value;
    quoted_path = *value == '"';
    if (quoted_path) ++value;
    while (*value && (!quoted_path || *value != '"')) {
        if (used + 1 >= capacity || (unsigned char)*value < 0x20 || (!quoted_path && *value == ' ') || *value == '"') return 0;
        ++used; ++value;
    }
    if (!used || (quoted_path && *value++ != '"')) return 0;
    while (*value == ' ') ++value;
    if (*value) return 0;
    value = command_line + 9;
    while (*value == ' ') ++value;
    if (quoted_path) ++value;
    memcpy(path, value, used); path[used] = 0;
    return 1;
}
