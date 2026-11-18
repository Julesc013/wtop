/* Bounded portable text policy. SPDX-License-Identifier: GPL-3.0-only */
#include "text.h"

int wtop_text_decode_utf8(const char *input, size_t length,
                          unsigned long *scalar, size_t *consumed)
{
    const unsigned char *bytes = (const unsigned char *)input;
    unsigned long value, minimum;
    size_t need, i;
    if (!scalar || !consumed) return 0;
    *scalar = '?';
    *consumed = 0;
    if (!input || !length) return 0;
    *consumed = 1;
    if (bytes[0] < 0x80) { *scalar = bytes[0]; return 1; }
    if (bytes[0] >= 0xc2 && bytes[0] <= 0xdf) {
        need = 2; minimum = 0x80UL; value = bytes[0] & 0x1f;
    } else if (bytes[0] >= 0xe0 && bytes[0] <= 0xef) {
        need = 3; minimum = 0x800UL; value = bytes[0] & 0x0f;
    } else if (bytes[0] >= 0xf0 && bytes[0] <= 0xf4) {
        need = 4; minimum = 0x10000UL; value = bytes[0] & 0x07;
    } else return 0;
    if (need > length) return 0;
    for (i = 1; i < need; ++i) {
        if ((bytes[i] & 0xc0) != 0x80) return 0;
        value = (value << 6) | (bytes[i] & 0x3f);
    }
    if (value < minimum || value > 0x10ffffUL ||
        (value >= 0xd800UL && value <= 0xdfffUL)) return 0;
    *scalar = value;
    *consumed = need;
    return 1;
}

int wtop_text_valid_utf8(const char *input, size_t length)
{
    size_t offset = 0, consumed;
    unsigned long scalar;
    if (!input && length) return 0;
    while (offset < length) {
        if (!wtop_text_decode_utf8(input + offset, length - offset,
                                    &scalar, &consumed)) return 0;
        offset += consumed;
    }
    return 1;
}

wtop_text_result wtop_text_ascii(const char *input, size_t length,
                                  size_t max_cells, char *output,
                                  size_t capacity)
{
    wtop_text_result result;
    unsigned long scalar;
    size_t consumed, limit = max_cells;
    int valid, measure = output == NULL && capacity == 0;
    char shown;
    result.input_used = result.output_bytes = result.cells = 0;
    result.flags = 0;
    if (output && capacity) output[0] = 0;
    if (!input && length) {
        result.flags = WTOP_TEXT_INVALID | WTOP_TEXT_LOSSY;
        return result;
    }
    if (!measure) {
        if (!output || !capacity) limit = 0;
        else if (limit >= capacity) limit = capacity - 1;
    }
    while (result.input_used < length && result.cells < limit) {
        valid = wtop_text_decode_utf8(input + result.input_used,
                  length - result.input_used, &scalar, &consumed);
        shown = '?';
        if (!valid) result.flags |= WTOP_TEXT_INVALID | WTOP_TEXT_LOSSY;
        else if (scalar >= 0x20UL && scalar <= 0x7eUL) shown = (char)scalar;
        else {
            result.flags |= WTOP_TEXT_LOSSY;
            if (scalar < 0x20UL || (scalar >= 0x7fUL && scalar <= 0x9fUL))
                result.flags |= WTOP_TEXT_CONTROL;
        }
        if (!measure) output[result.output_bytes] = shown;
        ++result.output_bytes;
        ++result.cells;
        result.input_used += consumed;
    }
    if (result.input_used < length) result.flags |= WTOP_TEXT_CLIPPED;
    if (output && capacity) output[result.output_bytes] = 0;
    return result;
}

size_t wtop_text_ascii_offset(const char *input, size_t length, size_t cell)
{
    size_t offset = 0, count = 0, consumed;
    unsigned long scalar;
    if (!input) return 0;
    while (offset < length && count < cell) {
        (void)wtop_text_decode_utf8(input + offset, length - offset,
                                   &scalar, &consumed);
        offset += consumed;
        ++count;
    }
    return offset;
}

unsigned short wtop_text_cell_glyph(unsigned long scalar, int unicode, unsigned *flags)
{
    if (scalar >= 0x20UL && scalar <= 0x7eUL) return (unsigned short)scalar;
    if (unicode && ((scalar >= 0x00a0UL && scalar <= 0x02ffUL && scalar != 0x00adUL) ||
        (scalar >= 0x0370UL && scalar <= 0x052fUL && !(scalar >= 0x0483UL && scalar <= 0x0489UL)) ||
        (scalar >= 0x2500UL && scalar <= 0x259fUL))) return (unsigned short)scalar;
    if (flags) {
        *flags |= WTOP_TEXT_LOSSY;
        if (scalar < 0x20UL || (scalar >= 0x7fUL && scalar <= 0x9fUL) ||
            (scalar >= 0x202aUL && scalar <= 0x202eUL) ||
            (scalar >= 0x2066UL && scalar <= 0x2069UL)) *flags |= WTOP_TEXT_CONTROL;
        if (scalar > 0x10ffffUL || (scalar >= 0xd800UL && scalar <= 0xdfffUL)) *flags |= WTOP_TEXT_INVALID;
    }
    return '?';
}
