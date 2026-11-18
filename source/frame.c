/* Retained cell compositor. SPDX-License-Identifier: GPL-3.0-only */
#include "frame.h"
#include <stdlib.h>
#include <string.h>

int wtop_frame_resize(wtop_frame *frame, int width, int height)
{
    size_t count, bound;
    wtop_cell *front, *back;
    if (!frame || width <= 0 || height <= 0 || width > 32767 || height > 32767) return 0;
    bound = (size_t)-1 / sizeof(wtop_cell);
    /* An explicit resource ceiling, also respecting a 16-bit size_t. */
    if (bound > 1048576UL) bound = 1048576UL;
    if ((unsigned long)width > (unsigned long)bound / (unsigned long)height) return 0;
    count = (size_t)width * (size_t)height;
    if (frame->back && frame->width == width && frame->height == height) return 1;
    front = (wtop_cell *)calloc(count, sizeof(*front));
    back = (wtop_cell *)calloc(count, sizeof(*back));
    if (!front || !back) { free(front); free(back); return 0; }
    free(frame->front); free(frame->back);
    frame->front = front; frame->back = back; frame->count = count;
    frame->width = width; frame->height = height; frame->valid = 0;
    return 1;
}
void wtop_frame_dispose(wtop_frame *frame)
{
    if (!frame) return;
    free(frame->front); free(frame->back);
    memset(frame, 0, sizeof(*frame));
}
void wtop_frame_invalidate(wtop_frame *frame) { if (frame) frame->valid = 0; }
void wtop_frame_begin(wtop_frame *frame, unsigned short attribute)
{
    size_t i;
    for (i = 0; i < frame->count; ++i) {
        frame->back[i].glyph = ' '; frame->back[i].attribute = attribute;
    }
}
void wtop_frame_fill(wtop_frame *frame, int x, int y, int length,
                     unsigned short glyph, unsigned short attribute)
{
    size_t offset;
    int i;
    if (y < 0 || y >= frame->height || x >= frame->width || length <= 0) return;
    if (x < 0) {
        /* Avoid negating INT_MIN. Valid in-range overlap has |x| < length. */
        if (x <= -length) return;
        length += x; x = 0;
    }
    if (length > frame->width - x) length = frame->width - x;
    offset = (size_t)y * (size_t)frame->width + (size_t)x;
    for (i = 0; i < length; ++i) {
        frame->back[offset + (size_t)i].glyph = glyph;
        frame->back[offset + (size_t)i].attribute = attribute;
    }
}
void wtop_frame_put(wtop_frame *frame, int x, int y,
                    unsigned short glyph, unsigned short attribute)
{
    wtop_frame_fill(frame, x, y, 1, glyph, attribute);
}
void wtop_frame_attribute(wtop_frame *frame, int x, int y, unsigned short attribute)
{
    if (x >= 0 && y >= 0 && x < frame->width && y < frame->height)
        frame->back[(size_t)y * (size_t)frame->width + (size_t)x].attribute = attribute;
}
int wtop_frame_difference(const wtop_frame *frame, wtop_rectangle *r)
{
    int x, y, changed;
    size_t i;
    if (!frame || !frame->back || !r) return 0;
    if (!frame->valid) {
        r->left = r->top = 0; r->right = frame->width - 1; r->bottom = frame->height - 1;
        return 1;
    }
    changed = 0; r->left = frame->width; r->top = frame->height; r->right = r->bottom = -1;
    i = 0;
    for (y = 0; y < frame->height; ++y) for (x = 0; x < frame->width; ++x, ++i) {
        if (frame->front[i].glyph == frame->back[i].glyph &&
            frame->front[i].attribute == frame->back[i].attribute) continue;
        changed = 1;
        if (x < r->left) r->left = x;
        if (x > r->right) r->right = x;
        if (y < r->top) r->top = y;
        if (y > r->bottom) r->bottom = y;
    }
    return changed;
}
int wtop_frame_present(wtop_frame *frame, wtop_frame_sink sink, void *context)
{
    wtop_rectangle rectangle;
    if (!frame || !frame->back || !sink) return -1;
    if (!wtop_frame_difference(frame, &rectangle)) return 0;
    if (!sink(context, frame, &rectangle)) { frame->valid = 0; return -1; }
    memcpy(frame->front, frame->back, frame->count * sizeof(*frame->front));
    frame->valid = 1;
    return 1;
}
