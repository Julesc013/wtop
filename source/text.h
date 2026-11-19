/* Bounded portable text policy. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_TEXT_H
#define WTOP_TEXT_H
#include <stddef.h>

enum wtop_text_flag {
    WTOP_TEXT_LOSSY = 1,
    WTOP_TEXT_INVALID = 2,
    WTOP_TEXT_CONTROL = 4,
    WTOP_TEXT_CLIPPED = 8,
    WTOP_TEXT_CAPACITY = 16
};
typedef struct wtop_text_result {
    size_t input_used;
    size_t output_bytes;
    size_t cells;
    unsigned flags;
} wtop_text_result;

/* Reads only length bytes. Valid scalars return 1 and their complete byte count.
   Invalid nonempty input returns 0, scalar '?' and consumed=1 for progress.
   Empty input returns 0 and consumed=0. Output pointers must be nonnull. */
int wtop_text_decode_utf8(const char *input, size_t length,
                          unsigned long *scalar, size_t *consumed);
int wtop_text_valid_utf8(const char *input, size_t length);

/* Conservative ASCII display profile: printable ASCII survives; each other
   scalar/control or invalid input byte becomes one visible '?' cell. Loss is
   explicit, raw input remains caller-owned and unchanged. This is not a claim
   of Unicode terminal-width support. Output is NUL-terminated when capacity>0.
   The output range must not overlap the input range.
   A NULL output with capacity=0 measures the profile up to max_cells without
   allocating. Every output byte occupies exactly one cell on an ASCII host. */
wtop_text_result wtop_text_ascii(const char *input, size_t length,
                                  size_t max_cells, char *output,
                                  size_t capacity);
/* Map a cell boundary under that same profile to an input-byte boundary.
   Out-of-range cell positions map to length. Never splits valid UTF-8. */
size_t wtop_text_ascii_offset(const char *input, size_t length, size_t cell);
/* A deliberately narrow one-cell repertoire, not general Unicode width.
   Supports Latin/Greek/Cyrillic letters and console box/block glyphs. Complex,
   combining, bidi/control and wide scalars are replaced; raw text is untouched. */
unsigned short wtop_text_cell_glyph(unsigned long scalar, int unicode, unsigned *flags);

/* UTF-16 projection shared by layout, the editor and native presenters. Profile
   0 is ASCII, 1 the legacy one-cell repertoire, 2 Unicode 17 extended grapheme
   clusters. Raw strings are never modified. An oversized cluster is consumed
   whole and replaced, with CAPACITY|LOSSY; it is never split across rows.
   Width policy: ambiguous=1, wide/fullwidth and emoji presentation=2, a
   standalone zero-width cluster receives a dotted circle. Terminal shaping is
   host-dependent; these are layout cells, not pixel/font measurements. */
#define WTOP_TEXT_CLUSTER_UNITS 128
typedef struct wtop_text_cluster {
    size_t input_units, output_units, cells;
    unsigned flags;
    unsigned short text[WTOP_TEXT_CLUSTER_UNITS];
} wtop_text_cluster;
int wtop_text_next_utf16(const unsigned short *input, size_t length,
                         int profile, wtop_text_cluster *cluster);
size_t wtop_text_utf16_span(const unsigned short *input, size_t length,
                            size_t max_cells, int profile,
                            size_t *cells, unsigned *flags);
/* Editor offsets are UTF-16 units. Boundaries always come from the complete
   string prefix, preserving regional-indicator and Indic-conjunct context. */
size_t wtop_text_utf16_previous(const unsigned short *input, size_t length, size_t offset);
size_t wtop_text_utf16_next(const unsigned short *input, size_t length, size_t offset);
const char *wtop_text_unicode_version(void);
#endif
