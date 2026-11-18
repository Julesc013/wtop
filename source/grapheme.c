/* Unicode projection and editor boundaries. SPDX-License-Identifier: GPL-3.0-only */
#include "text.h"
#include "vendor/utf8proc/utf8proc.h"
#include <string.h>

static unsigned long decode(const unsigned short *s, size_t n, size_t *used, int *valid)
{
    unsigned long c = s[0];
    *used = 1; *valid = 1;
    if (c >= 0xd800UL && c <= 0xdbffUL) {
        if (n > 1 && s[1] >= 0xdc00 && s[1] <= 0xdfff) {
            *used = 2;
            return 0x10000UL + ((c - 0xd800UL) << 10) + (s[1] - 0xdc00UL);
        }
        *valid = 0; return 0xfffdUL;
    }
    if (c >= 0xdc00UL && c <= 0xdfffUL) { *valid = 0; return 0xfffdUL; }
    return c;
}

static int unsafe_scalar(unsigned long c)
{
    /* Escape/control injection and bidirectional overrides are visible, never
       executed. ZWJ/ZWNJ, variation selectors and emoji tags remain intact. */
    return c < 0x20UL || (c >= 0x7fUL && c <= 0x9fUL) || c == 0x061cUL ||
        c == 0x200eUL || c == 0x200fUL || (c >= 0x2028UL && c <= 0x202eUL) ||
        (c >= 0x2066UL && c <= 0x2069UL);
}

static size_t boundary(const unsigned short *s, size_t n)
{
    size_t offset, used;
    unsigned long previous, next;
    int valid;
    utf8proc_int32_t state = 0;
    if (!s || !n) return 0;
    previous = decode(s, n, &offset, &valid);
    while (offset < n) {
        next = decode(s + offset, n - offset, &used, &valid);
        if (utf8proc_grapheme_break_stateful((utf8proc_int32_t)previous,
                                             (utf8proc_int32_t)next, &state)) break;
        previous = next; offset += used;
    }
    return offset;
}

int wtop_text_next_utf16(const unsigned short *input, size_t length,
                         int profile, wtop_text_cluster *cluster)
{
    size_t end, offset, used, width = 0;
    unsigned long scalar;
    int valid, scalar_width, emoji = 0, keycap = 0, variation = 0, text_variation = 0;
    int regional = 0, joiner = 0;
    const utf8proc_property_t *property;
    if (!cluster) return 0;
    memset(cluster, 0, sizeof(*cluster));
    if (!input || !length) return 0;
    end = boundary(input, length);
    cluster->input_units = end;
    if (profile != 2) {
        /* Even the conservative renderer consumes a full cluster, so its
           fallback cannot expose half an emoji or detach a combining mark. */
        scalar = decode(input, end, &used, &valid);
        cluster->text[0] = wtop_text_cell_glyph(scalar, profile == 1, &cluster->flags);
        if (!valid) cluster->flags |= WTOP_TEXT_INVALID | WTOP_TEXT_LOSSY;
        if (used != end) { cluster->text[0] = '?'; cluster->flags |= WTOP_TEXT_LOSSY; }
        cluster->output_units = cluster->cells = 1;
        return 1;
    }
    for (offset = 0; offset < end; offset += used) {
        scalar = decode(input + offset, end - offset, &used, &valid);
        if (!valid) cluster->flags |= WTOP_TEXT_INVALID | WTOP_TEXT_LOSSY;
        if (unsafe_scalar(scalar)) cluster->flags |= WTOP_TEXT_CONTROL | WTOP_TEXT_LOSSY;
        property = utf8proc_get_property((utf8proc_int32_t)scalar);
        scalar_width = utf8proc_charwidth((utf8proc_int32_t)scalar);
        if (scalar_width > 0 && (size_t)scalar_width > width) width = (size_t)scalar_width;
        if (property->boundclass == UTF8PROC_BOUNDCLASS_EXTENDED_PICTOGRAPHIC) emoji = 1;
        if (scalar == 0xfe0fUL) variation = 1;
        if (scalar == 0xfe0eUL) text_variation = 1;
        if (scalar == 0x20e3UL) keycap = 1;
        if (scalar == 0x200dUL) joiner = 1;
        if (scalar >= 0x1f1e6UL && scalar <= 0x1f1ffUL) ++regional;
    }
    if (end >= WTOP_TEXT_CLUSTER_UNITS) cluster->flags |= WTOP_TEXT_CAPACITY | WTOP_TEXT_LOSSY;
    if (cluster->flags & WTOP_TEXT_LOSSY) {
        cluster->text[0] = '?'; cluster->output_units = cluster->cells = 1;
        return 1;
    }
    /* Emoji presentation and ZWJ sequences use two cells. Text selectors keep
       the underlying width; terminals/font choices can still differ. */
    if (!text_variation && ((variation && emoji) || keycap || regional || (emoji && joiner))) width = 2;
    if (!width) {
        cluster->text[0] = 0x25cc; /* Visible base for a leading combining cluster. */
        memcpy(cluster->text + 1, input, end * sizeof(*input));
        cluster->output_units = end + 1; width = 1;
    } else {
        memcpy(cluster->text, input, end * sizeof(*input));
        cluster->output_units = end;
    }
    cluster->cells = width > 2 ? 2 : width;
    return 1;
}

size_t wtop_text_utf16_span(const unsigned short *input, size_t length,
                            size_t max_cells, int profile,
                            size_t *cells, unsigned *flags)
{
    wtop_text_cluster cluster;
    size_t offset = 0, count = 0;
    unsigned found = 0;
    if (!input && length) { if (cells) *cells = 0; if (flags) *flags |= WTOP_TEXT_INVALID; return 0; }
    while (offset < length && wtop_text_next_utf16(input + offset, length - offset, profile, &cluster)) {
        if (cluster.cells > max_cells - count) { found |= WTOP_TEXT_CLIPPED; break; }
        offset += cluster.input_units; count += cluster.cells; found |= cluster.flags;
    }
    if (cells) *cells = count;
    if (flags) *flags |= found;
    return offset;
}

size_t wtop_text_utf16_previous(const unsigned short *input, size_t length, size_t offset)
{
    size_t position = 0, next;
    if (!input) return 0;
    if (offset > length) offset = length;
    while (position < offset) {
        next = position + boundary(input + position, length - position);
        if (next >= offset) return position;
        position = next;
    }
    return position;
}

size_t wtop_text_utf16_next(const unsigned short *input, size_t length, size_t offset)
{
    size_t position = 0, next;
    if (!input || offset >= length) return length;
    while (position < length) {
        next = position + boundary(input + position, length - position);
        if (next > offset) return next;
        position = next;
    }
    return length;
}

const char *wtop_text_unicode_version(void) { return utf8proc_unicode_version(); }
