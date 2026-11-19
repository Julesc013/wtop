/* Retained viewport presentation. SPDX-License-Identifier: GPL-3.0-only */
#include "console_frame_nt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

static int probe_grapheme_widths(void)
{
    /* VT parsing and Unicode shaping are independent. In particular, classic
       conhost accepts SGR49 but can advance two cells for e + combining acute.
       Probe a separate, never-activated buffer: no caller content or cursor is
       changed, and no terminal replies/input callbacks are required. Failure
       retains VT colours while selecting the conservative glyph projection. */
    static const WCHAR samples[7][6] = {
        {'e',0x0301}, {0x4e2d}, {0xd83d,0xde00},
        {0xd83d,0xdc69,0x200d,0xd83d,0xdcbb},
        {0xd83c,0xdde6,0xd83c,0xddfa}, {'1',0xfe0f,0x20e3}, {0x25cc,0x0301}
    };
    static const DWORD units[7] = {2,1,2,5,4,3,2};
    static const SHORT widths[7] = {1,2,2,2,2,2,1};
    HANDLE probe;
    CONSOLE_SCREEN_BUFFER_INFO info;
    COORD origin = {0,0};
    DWORD mode, written;
    int i, qualified = 0;
    probe = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    if (probe == INVALID_HANDLE_VALUE) return 0;
    if (!GetConsoleMode(probe, &mode) ||
        !SetConsoleMode(probe, (mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING) &
            ~ENABLE_WRAP_AT_EOL_OUTPUT) || !GetConsoleScreenBufferInfo(probe, &info) || info.dwSize.X < 8)
        goto done;
    for (i = 0; i < 7; ++i) {
        if (!SetConsoleCursorPosition(probe, origin) ||
            !WriteConsoleW(probe, samples[i], units[i], &written, NULL) || written != units[i] ||
            !GetConsoleScreenBufferInfo(probe, &info) || info.dwCursorPosition.Y != 0 ||
            info.dwCursorPosition.X != widths[i]) goto done;
    }
    qualified = 1;
done:
    CloseHandle(probe);
    return qualified;
}

int wtop_console_frame_configure(wtop_console_frame *host, HANDLE output,
                                  WORD inherited_attributes, int prefer_vt)
{
    DWORD mode;
    if (!host) return 0;
    /* Reconfiguration of the same handle must recapture its restored mode,
       never promote our temporary VT/no-wrap mode into the caller baseline. */
    wtop_console_frame_restore(host);
    if (!GetConsoleMode(output, &mode)) return 0;
    host->configured_output = output; host->original_output_mode = mode;
    host->inherited_attributes = inherited_attributes;
    /* No wrap: writing the bottom-right cell must never scroll the viewport.
       The private screen buffer means this cannot alter the invoking shell. */
    if (prefer_vt && SetConsoleMode(output,
        (mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING) & ~ENABLE_WRAP_AT_EOL_OUTPUT)) {
        host->vt_enabled = 1; host->mode_changed = 1;
        host->grapheme_enabled = probe_grapheme_widths();
    }
    wtop_frame_invalidate(&host->cells);
    return 1;
}
void wtop_console_frame_restore(wtop_console_frame *host)
{
    if (!host) return;
    if (host->mode_changed && host->configured_output)
        SetConsoleMode(host->configured_output, host->original_output_mode);
    host->mode_changed = host->vt_enabled = host->grapheme_enabled = 0;
    host->configured_output = NULL;
}

static WORD legacy_attribute(const wtop_console_frame *host, unsigned short attribute)
{
    WORD result = attribute & WTOP_ATTR_PALETTE_MASK;
    if (attribute & WTOP_ATTR_DEFAULT_BACKGROUND)
        result = (result & 0x0f) | (host->inherited_attributes & 0xf0);
    return result;
}

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
        host->upload[index].Char.UnicodeChar = (WCHAR)(cell->cluster_units ? '?' : cell->glyph);
        host->upload[index].Attributes = legacy_attribute(host, cell->attribute);
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

static int vt_append(wtop_console_frame *host, size_t *used, const WCHAR *text, size_t count)
{
    WCHAR *buffer;
    size_t need, next;
    if (count > 8388608UL || *used > 8388608UL - count) { host->error = ERROR_NOT_ENOUGH_MEMORY; return 0; }
    need = *used + count;
    if (need > host->vt_capacity) {
        next = host->vt_capacity ? host->vt_capacity : 4096;
        while (next < need) next *= 2;
        buffer = (WCHAR *)realloc(host->vt_buffer, next * sizeof(*buffer));
        if (!buffer) { host->error = ERROR_NOT_ENOUGH_MEMORY; return 0; }
        host->vt_buffer = buffer; host->vt_capacity = next;
    }
    memcpy(host->vt_buffer + *used, text, count * sizeof(*text)); *used = need;
    return 1;
}
static int vt_color(wtop_console_frame *host, size_t *used, unsigned short attribute)
{
    /* Win32 BGR bits -> ANSI RGB indices. Do not translate an inherited
       background into a palette colour: SGR49 is semantically different. */
    static const unsigned indices[8] = {0,4,2,6,1,5,3,7};
    WCHAR sequence[32];
    unsigned fg = attribute & 15, bg = (attribute >> 4) & 15;
    int count = swprintf(sequence, 32, L"\x1b[0;%u;%um",
        (fg & 8 ? 90 : 30) + indices[fg & 7],
        attribute & WTOP_ATTR_DEFAULT_BACKGROUND ? 49 : (bg & 8 ? 100 : 40) + indices[bg & 7]);
    return count > 0 && vt_append(host, used, sequence, (size_t)count);
}
static int write_vt_rectangle(void *context, const wtop_frame *frame, const wtop_rectangle *r)
{
    wtop_console_frame *host = (wtop_console_frame *)context;
    size_t used = 0, sent = 0;
    int x, y, count;
    unsigned short previous = 0xffff;
    WCHAR sequence[48], glyph;
    DWORD written, request;
    if (!viewport_matches(host)) return 0;
    for (y = r->top; y <= r->bottom; ++y) {
        /* The Windows VT adapter anchors absolute rows to the active page,
           but columns to the buffer, not the horizontally scrolled viewport.
           See Microsoft Terminal AdaptDispatch::_CursorMovePosition. */
        count = swprintf(sequence, 48, L"\x1b[%d;%dH", y + 1, host->left + r->left + 1);
        if (count < 1 || !vt_append(host, &used, sequence, (size_t)count)) return 0;
        for (x = r->left; x <= r->right; ++x) {
            const wtop_cell *cell = frame->back + (size_t)y * (size_t)frame->width + (size_t)x;
            if (!cell->width) continue;
            if (previous != cell->attribute) {
                if (!vt_color(host, &used, cell->attribute)) return 0;
                previous = cell->attribute;
            }
            if (cell->cluster_units) {
                if (!vt_append(host, &used, (const WCHAR *)(frame->back_text + cell->cluster_offset), cell->cluster_units)) return 0;
            } else {
                glyph = (WCHAR)cell->glyph;
                if (!vt_append(host, &used, &glyph, 1)) return 0;
            }
        }
    }
    /* Keep complete sequences/clusters in one write when possible. Short
       writes resume exactly; any error invalidates the retained frame. */
    host->vt_used = used;
    while (sent < used) {
        request = (DWORD)(used - sent);
        if (!WriteConsoleW(host->output, host->vt_buffer + sent, request, &written, NULL) || !written) {
            host->error = GetLastError(); if (!host->error) host->error = ERROR_WRITE_FAULT; return 0;
        }
        sent += written;
    }
    if (!viewport_matches(host)) return 0;
    host->error = ERROR_SUCCESS; return 1;
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
    host->error = ERROR_SUCCESS;
    result = wtop_frame_present(&host->cells, host->vt_enabled ? write_vt_rectangle : write_rectangle, host);
    if (result < 0 && !host->error) host->error = ERROR_NOT_ENOUGH_MEMORY;
    if (result < 0) ++host->retries;
    else if (result) ++host->presents;
    else ++host->unchanged;
    return result;
}
void wtop_console_frame_dispose(wtop_console_frame *host)
{
    wtop_console_frame_restore(host);
    wtop_frame_dispose(&host->cells);
    free(host->upload);
    free(host->vt_buffer);
    memset(host, 0, sizeof(*host));
}
