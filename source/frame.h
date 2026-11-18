/* Retained cell compositor. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_FRAME_H
#define WTOP_FRAME_H
#include <stddef.h>

/* One qualified single-cell BMP glyph plus native-compatible color roles.
   No host handles, cursor operations, control sequences or output in this core. */
typedef struct wtop_cell { unsigned short glyph, attribute; } wtop_cell;
typedef struct wtop_rectangle { int left, top, right, bottom; } wtop_rectangle;
typedef struct wtop_frame {
    wtop_cell *front, *back;
    size_t count;
    int width, height, valid;
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
