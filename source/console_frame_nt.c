/* Retained viewport presentation. SPDX-License-Identifier: GPL-3.0-only */
#include "console_frame_nt.h"
#include <stdlib.h>
#include <string.h>

static int viewport_matches(wtop_console_frame *host)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(host->output, &info)) {
        host->error = GetLastError(); return 0;
    }
    if (info.srWindow.Left != host->left || info.srWindow.Top != host->top ||
        info.srWindow.Right - info.srWindow.Left + 1 != host->cells.width ||
        info.srWindow.Bottom - info.srWindow.Top + 1 != host->cells.height) {
        host->error = ERROR_RETRY; return 0;
    }
    return 1;
}
static int write_rectangle(void *context, const wtop_frame *frame, const wtop_rectangle *r)
{
    wtop_console_frame *host = (wtop_console_frame *)context;
    COORD size, origin;
    SMALL_RECT target, expected;
    int x, y, width = r->right - r->left + 1;
    size_t index = 0;
    if (!viewport_matches(host)) return 0;
    for (y = r->top; y <= r->bottom; ++y) for (x = r->left; x <= r->right; ++x) {
        const wtop_cell *cell = &frame->back[(size_t)y * (size_t)frame->width + (size_t)x];
        host->upload[index].Char.UnicodeChar = (WCHAR)cell->glyph;
        host->upload[index].Attributes = (WORD)cell->attribute;
        ++index;
    }
    size.X = (SHORT)width; size.Y = (SHORT)(r->bottom - r->top + 1);
    origin.X = origin.Y = 0;
    target.Left = (SHORT)(host->left + r->left); target.Top = (SHORT)(host->top + r->top);
    target.Right = (SHORT)(host->left + r->right); target.Bottom = (SHORT)(host->top + r->bottom);
    expected = target;
    /* One complete character+attribute update; never clear the live screen,
       move the output cursor, or write a newline at its bottom-right cell. */
    if (!WriteConsoleOutputW(host->output, host->upload, size, origin, &target)) {
        host->error = GetLastError(); return 0;
    }
    if (target.Left != expected.Left || target.Top != expected.Top ||
        target.Right != expected.Right || target.Bottom != expected.Bottom || !viewport_matches(host)) {
        host->error = ERROR_RETRY; return 0;
    }
    host->error = ERROR_SUCCESS;
    return 1;
}
int wtop_console_frame_begin(wtop_console_frame *host, HANDLE output,
                            int width, int height, int left, int top, WORD attribute)
{
    CHAR_INFO *upload;
    if (left < 0 || top < 0 || width < 1 || height < 1 ||
        left > 32767 - width || top > 32767 - height) {
        host->error = ERROR_INVALID_PARAMETER; return 0;
    }
    if (!wtop_frame_resize(&host->cells, width, height)) {
        host->error = ERROR_NOT_ENOUGH_MEMORY; return 0;
    }
    if (host->capacity < host->cells.count) {
        upload = (CHAR_INFO *)realloc(host->upload, host->cells.count * sizeof(*upload));
        if (!upload) { host->error = ERROR_NOT_ENOUGH_MEMORY; return 0; }
        host->upload = upload; host->capacity = host->cells.count;
    }
    if (host->output != output || host->left != left || host->top != top)
        wtop_frame_invalidate(&host->cells);
    host->output = output; host->left = (SHORT)left; host->top = (SHORT)top;
    wtop_frame_begin(&host->cells, attribute);
    return 1;
}
int wtop_console_frame_present(wtop_console_frame *host)
{
    int result;
    /* A host can move/resize its viewport even when our cell model did not
       change. Validate before the core's zero-write shortcut; the sink alone
       cannot detect geometry changes on an unchanged frame. */
    if (!viewport_matches(host)) {
        wtop_frame_invalidate(&host->cells); ++host->retries; return -1;
    }
    result = wtop_frame_present(&host->cells, write_rectangle, host);
    if (result < 0) ++host->retries;
    else if (result) ++host->presents;
    else ++host->unchanged;
    return result;
}
void wtop_console_frame_dispose(wtop_console_frame *host)
{
    wtop_frame_dispose(&host->cells);
    free(host->upload);
    memset(host, 0, sizeof(*host));
}
