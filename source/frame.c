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
    frame->front_used = frame->back_used = 0;
    return 1;
}
void wtop_frame_dispose(wtop_frame *frame)
{
    if (!frame) return;
    free(frame->front); free(frame->back);
    free(frame->front_text); free(frame->back_text);
    memset(frame, 0, sizeof(*frame));
}
void wtop_frame_invalidate(wtop_frame *frame) { if (frame) frame->valid = 0; }
void wtop_frame_begin(wtop_frame *frame, unsigned short attribute)
{
    size_t i;
    frame->back_used = 0;
    for (i = 0; i < frame->count; ++i) {
        memset(&frame->back[i], 0, sizeof(frame->back[i]));
        frame->back[i].glyph = ' '; frame->back[i].attribute = attribute;
        frame->back[i].width = 1;
    }
}
static void erase_cluster(wtop_frame *frame, int x, int y)
{
    size_t offset = (size_t)y * (size_t)frame->width + (size_t)x;
    wtop_cell *cell = frame->back + offset;
    if (!cell->width && x > 0) {
        cell[-1].glyph = ' '; cell[-1].cluster_units = 0; cell[-1].width = 1;
    } else if (cell->width == 2 && x + 1 < frame->width) {
        cell[1].glyph = ' '; cell[1].cluster_units = 0; cell[1].width = 1;
    }
    cell->glyph = ' '; cell->cluster_units = 0; cell->width = 1;
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
        erase_cluster(frame, x + i, y);
        frame->back[offset + (size_t)i].glyph = glyph;
        frame->back[offset + (size_t)i].attribute = attribute;
        frame->back[offset + (size_t)i].cluster_units = 0;
        frame->back[offset + (size_t)i].width = 1;
    }
}
void wtop_frame_put(wtop_frame *frame, int x, int y,
                    unsigned short glyph, unsigned short attribute)
{
    wtop_frame_fill(frame, x, y, 1, glyph, attribute);
}
static int reserve_text(unsigned short **text, size_t *capacity, size_t need)
{
    size_t next, limit = (size_t)-1 / sizeof(**text);
    unsigned short *allocation;
    if (limit > 1048576UL) limit = 1048576UL;
    if (need > limit) return 0;
    if (need <= *capacity) return 1;
    next = *capacity ? *capacity : 1024;
    while (next < need) { if (next > limit / 2) { next = limit; break; } next *= 2; }
    allocation = (unsigned short *)realloc(*text, next * sizeof(**text));
    if (!allocation) return 0;
    *text = allocation; *capacity = next; return 1;
}
int wtop_frame_put_cluster(wtop_frame *frame, int x, int y,
                            const unsigned short *text, size_t units,
                            int cells, unsigned short attribute)
{
    wtop_cell *cell;
    if (!frame || !frame->back || !text || !units || units > 128 ||
        cells < 1 || cells > 2 || x < 0 || y < 0 || y >= frame->height ||
        x > frame->width - cells) return 0;
    if (units == 1 && cells == 1) { wtop_frame_put(frame, x, y, text[0], attribute); return 1; }
    if (units > (size_t)-1 - frame->back_used ||
        !reserve_text(&frame->back_text, &frame->back_capacity, frame->back_used + units)) return 0;
    erase_cluster(frame, x, y);
    if (cells == 2) erase_cluster(frame, x + 1, y);
    cell = frame->back + (size_t)y * (size_t)frame->width + (size_t)x;
    cell->glyph = text[0]; cell->attribute = attribute;
    cell->cluster_offset = frame->back_used; cell->cluster_units = (unsigned short)units;
    cell->width = (unsigned char)cells;
    memcpy(frame->back_text + frame->back_used, text, units * sizeof(*text));
    frame->back_used += units;
    if (cells == 2) {
        cell[1].glyph = ' '; cell[1].attribute = attribute;
        cell[1].cluster_units = 0; cell[1].width = 0;
    }
    return 1;
}
void wtop_frame_attribute(wtop_frame *frame, int x, int y, unsigned short attribute)
{
    wtop_cell *cell;
    if (x < 0 || y < 0 || x >= frame->width || y >= frame->height) return;
    cell = frame->back + (size_t)y * (size_t)frame->width + (size_t)x;
    if (!cell->width && x > 0) --cell;
    cell->attribute = attribute;
    if (cell->width == 2) cell[1].attribute = attribute;
}
static int same_cell(const wtop_frame *frame, size_t i)
{
    const wtop_cell *a = frame->front + i, *b = frame->back + i;
    return a->glyph == b->glyph && a->attribute == b->attribute &&
        a->width == b->width && a->cluster_units == b->cluster_units &&
        (!a->cluster_units || !memcmp(frame->front_text + a->cluster_offset,
            frame->back_text + b->cluster_offset, (size_t)a->cluster_units * sizeof(unsigned short)));
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
        if (same_cell(frame, i)) continue;
        changed = 1;
        if (x < r->left) r->left = x;
        if (x > r->right) r->right = x;
        if (y < r->top) r->top = y;
        if (y > r->bottom) r->bottom = y;
    }
    /* Closing a shared rectangle over multiple rows can expose another row's
       continuation at its new edge. Repeat to a fixed point; otherwise a VT
       row can skip that continuation and write its following glyph one cell
       early. The total scan remains bounded by the frame's cell count. */
    if (changed) {
        int expanded = 1;
        while (expanded && r->left > 0) {
            expanded = 0;
            for (y = r->top; y <= r->bottom; ++y) {
                i = (size_t)y * (size_t)frame->width + (size_t)r->left;
                if (!frame->front[i].width || !frame->back[i].width) {
                    --r->left; expanded = 1; break;
                }
            }
        }
        expanded = 1;
        while (expanded && r->right + 1 < frame->width) {
            expanded = 0;
            for (y = r->top; y <= r->bottom; ++y) {
                i = (size_t)y * (size_t)frame->width + (size_t)r->right;
                if (frame->front[i].width == 2 || frame->back[i].width == 2) {
                    ++r->right; expanded = 1; break;
                }
            }
        }
    }
    return changed;
}
int wtop_frame_present(wtop_frame *frame, wtop_frame_sink sink, void *context)
{
    wtop_rectangle rectangle;
    if (!frame || !frame->back || !sink) return -1;
    if (!wtop_frame_difference(frame, &rectangle)) return 0;
    if (!reserve_text(&frame->front_text, &frame->front_capacity, frame->back_used)) { frame->valid = 0; return -1; }
    if (!sink(context, frame, &rectangle)) { frame->valid = 0; return -1; }
    memcpy(frame->front, frame->back, frame->count * sizeof(*frame->front));
    if (frame->back_used) memcpy(frame->front_text, frame->back_text, frame->back_used * sizeof(*frame->back_text));
    frame->front_used = frame->back_used;
    frame->valid = 1;
    return 1;
}
