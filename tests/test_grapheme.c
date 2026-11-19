/* Official boundary corpus plus independent layout/editor cases.
   SPDX-License-Identifier: GPL-3.0-only */
#include "text.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void conformance(const char *path)
{
    FILE *file = fopen(path, "rb");
    char line[8192], *token, *comment, *end;
    unsigned short text[2048];
    size_t boundaries[2048], length, count, i, position;
    unsigned long scalar, cases = 0;
    wtop_text_cluster cluster;
    assert(file);
    while (fgets(line, sizeof(line), file)) {
        comment = strchr(line, '#'); if (comment) *comment = 0;
        length = count = 0;
        for (token = strtok(line, " \t\r\n"); token; token = strtok(NULL, " \t\r\n")) {
            if (!strcmp(token, "\xc3\xb7")) { assert(count < 2048); boundaries[count++] = length; }
            else if (strcmp(token, "\xc3\x97")) {
                scalar = strtoul(token, &end, 16); assert(!*end && scalar <= 0x10ffffUL);
                assert(length + 2 < 2048);
                if (scalar > 0xffffUL) {
                    scalar -= 0x10000UL; text[length++] = (unsigned short)(0xd800UL + (scalar >> 10));
                    text[length++] = (unsigned short)(0xdc00UL + (scalar & 1023));
                } else text[length++] = (unsigned short)scalar;
            }
        }
        if (!length) continue;
        assert(count > 1 && boundaries[0] == 0 && boundaries[count - 1] == length);
        position = 0;
        for (i = 1; i < count; ++i) {
            assert(wtop_text_next_utf16(text + position, length - position, 2, &cluster));
            assert(position + cluster.input_units == boundaries[i]);
            assert(wtop_text_utf16_next(text, length, position) == boundaries[i]);
            assert(wtop_text_utf16_previous(text, length, boundaries[i]) == position);
            position = boundaries[i];
        }
        assert(position == length); ++cases;
    }
    assert(!ferror(file)); fclose(file);
    assert(cases > 700);
    printf("Unicode %s official grapheme cases: %lu\n", wtop_text_unicode_version(), cases);
}
static void layout(void)
{
    const unsigned short text[] = {'A','e',0x0301,0x4e2d,0xd83d,0xdc69,0x200d,0xd83d,0xdcbb,'Z'};
    const unsigned short flag[] = {0xd83c,0xdde6,0xd83c,0xddfa};
    const unsigned short keycap[] = {'1',0xfe0f,0x20e3};
    const unsigned short mark[] = {0x0301};
    const unsigned short nonemoji_variation[] = {'A',0xfe0f};
    const unsigned short hostile[] = {27,0x202e,0xd800,'X',0xdc00};
    unsigned short long_cluster[256];
    wtop_text_cluster c;
    size_t cells, i;
    unsigned flags = 0;
    assert(wtop_text_next_utf16(text + 1, 9, 2, &c));
    assert(c.input_units == 2 && c.output_units == 2 && c.cells == 1 && !c.flags);
    assert(wtop_text_next_utf16(text + 3, 7, 2, &c));
    assert(c.input_units == 1 && c.cells == 2);
    assert(wtop_text_next_utf16(text + 4, 6, 2, &c));
    assert(c.input_units == 5 && c.output_units == 5 && c.cells == 2);
    assert(wtop_text_utf16_span(text, 10, 3, 2, &cells, &flags) == 3);
    assert(cells == 2 && (flags & WTOP_TEXT_CLIPPED));
    flags = 0;
    assert(wtop_text_utf16_span(text, 10, 6, 2, &cells, &flags) == 9 && cells == 6);
    assert(wtop_text_utf16_span(text, 10, 7, 2, &cells, NULL) == 10 && cells == 7);
    assert(wtop_text_next_utf16(flag, 4, 2, &c) && c.input_units == 4 && c.cells == 2);
    assert(wtop_text_next_utf16(keycap, 3, 2, &c) && c.input_units == 3 && c.cells == 2);
    assert(wtop_text_next_utf16(nonemoji_variation, 2, 2, &c) && c.cells == 1 && c.output_units == 2);
    assert(wtop_text_next_utf16(mark, 1, 2, &c) && c.output_units == 2 && c.text[0] == 0x25cc);
    assert(wtop_text_next_utf16(text + 1, 2, 0, &c) && c.text[0] == '?' && c.input_units == 2);
    for (i = 0; i < 5; ++i) {
        assert(wtop_text_next_utf16(hostile + i, 5 - i, 2, &c));
        if (i != 3) assert(c.text[0] == '?' && (c.flags & WTOP_TEXT_LOSSY));
    }
    long_cluster[0] = 'e';
    for (i = 1; i < 256; ++i) long_cluster[i] = 0x301;
    assert(wtop_text_next_utf16(long_cluster, 256, 2, &c));
    assert(c.input_units == 256 && c.output_units == 1 && (c.flags & WTOP_TEXT_CAPACITY));
    assert(wtop_text_utf16_previous(text, 10, 9) == 4);
    assert(wtop_text_utf16_next(text, 10, 6) == 9);
    assert(!wtop_text_next_utf16(NULL, 0, 2, &c));
}
int main(int argc, char **argv)
{
    assert(argc == 2); layout(); conformance(argv[1]); return 0;
}
