/* Bounded portable text policy. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_TEXT_H
#define WTOP_TEXT_H
#include <stddef.h>

enum wtop_text_flag {
    WTOP_TEXT_LOSSY = 1,
    WTOP_TEXT_INVALID = 2,
    WTOP_TEXT_CONTROL = 4,
    WTOP_TEXT_CLIPPED = 8
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
#endif
