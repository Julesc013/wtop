/* Retained cell compositor. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_FRAME_H
#define WTOP_FRAME_H
#include <stddef.h>

/* Internal semantic flag, not a Win32 COMMON_LVB attribute. Native adapters
   must strip it. A VT presenter uses SGR49; legacy hosts use inherited colour. */
#define WTOP_ATTR_DEFAULT_BACKGROUND 0x0100
#define WTOP_ATTR_EXPLICIT_BACKGROUND 0x0200
#define WTOP_ATTR_PALETTE_MASK 0x00ff
/* width=0 is the trailing cell of a two-cell cluster; width=1/2 is a lead.
   Single BMP glyphs need no pool allocation. Pool offsets are frame-owned. */
typedef struct wtop_cell {
    unsigned short glyph, attribute;
    size_t cluster_offset;
    unsigned short cluster_units;
    unsigned char width;
} wtop_cell;
typedef struct wtop_rectangle { int left, top, right, bottom; } wtop_rectangle;
typedef struct wtop_frame {
    wtop_cell *front, *back;
    size_t count;
    int width, height, valid;
    unsigned short *front_text, *back_text;
    size_t front_capacity, back_capacity, front_used, back_used;
} wtop_frame;

/* UI-thread owned. Zero-initialize before first use; dispose once after use.
   Resize is transactional; failure leaves the previous allocation intact. */
int wtop_frame_resize(wtop_frame *frame, int width, int height);
void wtop_frame_dispose(wtop_frame *frame);
void wtop_frame_invalidate(wtop_frame *frame);
void wtop_frame_begin(wtop_frame *frame, unsigned short attribute);
void wtop_frame_fill(wtop_frame *frame, int x, int y, int length,
                     unsigned short glyph, unsigned short attribute);
void wtop_frame_put(wtop_frame *frame, int x, int y,
                    unsigned short glyph, unsigned short attribute);
/* Atomic cluster placement. Returns 1, or 0 for clipping/invalid input/resource
   exhaustion without placing half a cluster. Input is borrowed only here. */
int wtop_frame_put_cluster(wtop_frame *frame, int x, int y,
                            const unsigned short *text, size_t units,
                            int cells, unsigned short attribute);
void wtop_frame_attribute(wtop_frame *frame, int x, int y, unsigned short attribute);
int wtop_frame_difference(const wtop_frame *frame, wtop_rectangle *rectangle);
/* Sink borrows a complete, immutable frame only for this call. It returns 1
   only when the entire rectangle reached the expected viewport. No retention,
   reentrancy, worker access or asynchronous completion is permitted.
   Result: 0 unchanged (no sink call), 1 committed, -1 failed (cache invalid). */
typedef int (*wtop_frame_sink)(void *context, const wtop_frame *frame,
                               const wtop_rectangle *rectangle);
int wtop_frame_present(wtop_frame *frame, wtop_frame_sink sink, void *context);
#endif
