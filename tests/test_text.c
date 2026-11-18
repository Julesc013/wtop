/* Conservative text contracts. SPDX-License-Identifier: GPL-3.0-only */
#include "text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void decoding(void)
{
    static const unsigned char valid[][4] = {
        {0x00,0,0,0}, {0x7f,0,0,0}, {0xc2,0x80,0,0},
        {0xdf,0xbf,0,0}, {0xe0,0xa0,0x80,0}, {0xed,0x9f,0xbf,0},
        {0xee,0x80,0x80,0}, {0xef,0xbf,0xbf,0},
        {0xf0,0x90,0x80,0x80}, {0xf4,0x8f,0xbf,0xbf}
    };
    static const unsigned long values[] = {
        0,0x7fUL,0x80UL,0x7ffUL,0x800UL,0xd7ffUL,0xe000UL,
        0xffffUL,0x10000UL,0x10ffffUL
    };
    static const size_t lengths[] = {1,1,2,2,3,3,3,3,4,4};
    static const unsigned char invalid[][4] = {
        {0x80,0,0,0}, {0xc0,0x80,0,0}, {0xc1,0xbf,0,0},
        {0xe0,0x80,0xaf,0}, {0xed,0xa0,0x80,0}, {0xed,0xbf,0xbf,0},
        {0xf0,0x80,0x80,0x80}, {0xf4,0x90,0x80,0x80},
        {0xf5,0x80,0x80,0x80}, {0xff,0,0,0}, {0xc2,0x41,0,0}
    };
    unsigned i;
    unsigned long scalar;
    size_t count;
    for (i=0; i<sizeof(valid)/sizeof(valid[0]); ++i) {
        assert(wtop_text_decode_utf8((const char *)valid[i], lengths[i], &scalar, &count));
        assert(scalar == values[i] && count == lengths[i]);
        if (lengths[i] > 1) {
            assert(!wtop_text_decode_utf8((const char *)valid[i], lengths[i]-1, &scalar, &count));
            assert(count == 1);
        }
    }
    for (i=0; i<sizeof(invalid)/sizeof(invalid[0]); ++i) {
        assert(!wtop_text_decode_utf8((const char *)invalid[i], 4, &scalar, &count));
        assert(count == 1 && scalar == '?');
    }
    assert(!wtop_text_decode_utf8("", 0, &scalar, &count) && count == 0);
    assert(!wtop_text_decode_utf8(NULL, 4, &scalar, &count) && count == 0);
    assert(wtop_text_valid_utf8(NULL, 0));
    assert(!wtop_text_valid_utf8(NULL, 1));
}

static void display_policy(void)
{
    const char mixed[] = "A\xe4\xb8\xad" "e\xcc\x81" "\xf0\x9f\x98\x80";
    const char controls[] = {'x', 0, '\n', 27, 127, 'y'};
    char output[64], original[64];
    wtop_text_result r;
    strcpy(original, mixed);
    r = wtop_text_ascii(mixed, sizeof(mixed)-1, 64, output, sizeof(output));
    assert(!strcmp(output, "A?e??"));
    assert(r.cells == 5 && r.output_bytes == 5 && r.input_used == sizeof(mixed)-1);
    assert(r.flags == WTOP_TEXT_LOSSY);
    assert(!strcmp(original, mixed));
    assert(wtop_text_valid_utf8(mixed, sizeof(mixed)-1));
    r = wtop_text_ascii(controls, sizeof(controls), 64, output, sizeof(output));
    assert(!strcmp(output, "x????y"));
    assert(r.flags == (WTOP_TEXT_LOSSY | WTOP_TEXT_CONTROL));
    r = wtop_text_ascii("\xc0\xaf", 2, 64, output, sizeof(output));
    assert(!strcmp(output, "??"));
    assert(r.flags == (WTOP_TEXT_LOSSY | WTOP_TEXT_INVALID));
    r = wtop_text_ascii(mixed, sizeof(mixed)-1, 2, output, sizeof(output));
    assert(!strcmp(output, "A?") && r.input_used == 4);
    assert(r.flags == (WTOP_TEXT_LOSSY | WTOP_TEXT_CLIPPED));
    assert(wtop_text_ascii_offset(mixed, sizeof(mixed)-1, 0) == 0);
    assert(wtop_text_ascii_offset(mixed, sizeof(mixed)-1, 1) == 1);
    assert(wtop_text_ascii_offset(mixed, sizeof(mixed)-1, 2) == 4);
    assert(wtop_text_ascii_offset(mixed, sizeof(mixed)-1, 3) == 5);
    assert(wtop_text_ascii_offset(mixed, sizeof(mixed)-1, 4) == 7);
    assert(wtop_text_ascii_offset(mixed, sizeof(mixed)-1, 5) == sizeof(mixed)-1);
    assert(wtop_text_ascii_offset(mixed, sizeof(mixed)-1, 999) == sizeof(mixed)-1);
}

static void bounds(void)
{
    char output[6];
    wtop_text_result r, measured;
    size_t capacity, cells;
    const char text[] = "ab\xc2\xa3" "cd";
    for (capacity=0; capacity<=sizeof(output); ++capacity) {
        for (cells=0; cells<=8; ++cells) {
            memset(output, '#', sizeof(output));
            r = wtop_text_ascii(text, sizeof(text)-1, cells, output, capacity);
            assert(r.cells <= cells);
            if (capacity) {
                assert(r.output_bytes < capacity && output[r.output_bytes] == 0);
                assert(strlen(output) == r.cells);
                if (capacity < sizeof(output)) assert(output[capacity] == '#');
            } else assert(output[0] == '#');
            assert(r.input_used <= sizeof(text)-1);
            assert(wtop_text_valid_utf8(text, r.input_used));
        }
    }
    measured = wtop_text_ascii(text, sizeof(text)-1, 999, NULL, 0);
    assert(measured.cells == 5 && measured.output_bytes == 5);
    assert(measured.flags == WTOP_TEXT_LOSSY);
    r = wtop_text_ascii(NULL, 1, 99, output, sizeof(output));
    assert(r.flags & WTOP_TEXT_INVALID);
    assert(output[0] == 0);
}

/* Exercise every possible byte in every position of short hostile input. This
   checks progress, output-cell safety and bounds without duplicating the decoder. */
static void byte_sequences(void)
{
    unsigned value, position;
    char input[4], output[5];
    wtop_text_result r;
    size_t i;
    for (position=0; position<4; ++position) {
        for (value=0; value<256; ++value) {
            memset(input, 'x', sizeof(input)); input[position] = (char)value;
            r = wtop_text_ascii(input, sizeof(input), 4, output, sizeof(output));
            assert(r.input_used == sizeof(input) && r.cells <= 4);
            assert(output[r.cells] == 0);
            for (i=0; i<r.cells; ++i) assert(output[i] >= 0x20 && output[i] <= 0x7e);
        }
    }
}

static void one_cell_repertoire(void)
{
    unsigned flags=0;
    assert(wtop_text_cell_glyph(0x00e9,1,&flags)==0x00e9 && !flags);
    assert(wtop_text_cell_glyph(0x0416,1,&flags)==0x0416 && !flags);
    assert(wtop_text_cell_glyph(0x2588,1,&flags)==0x2588 && !flags);
    assert(wtop_text_cell_glyph(0x00e9,0,&flags)=='?' && (flags&WTOP_TEXT_LOSSY));
    flags=0;
    assert(wtop_text_cell_glyph(0x0301,1,&flags)=='?' && (flags&WTOP_TEXT_LOSSY));
    assert(wtop_text_cell_glyph(0x4e2d,1,&flags)=='?');
    assert(wtop_text_cell_glyph(0x1f600,1,&flags)=='?');
    flags=0; assert(wtop_text_cell_glyph(0x202e,1,&flags)=='?' && (flags&WTOP_TEXT_CONTROL));
    flags=0; assert(wtop_text_cell_glyph(0xd800,1,&flags)=='?' && (flags&WTOP_TEXT_INVALID));
}

int main(void)
{
    decoding(); display_policy(); bounds(); byte_sequences(); one_cell_repertoire();
    puts("portable text contracts passed");
    return 0;
}
