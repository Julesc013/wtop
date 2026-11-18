/* Single-writer native presentation. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_CONSOLE_FRAME_NT_H
#define WTOP_CONSOLE_FRAME_NT_H
#include <windows.h>
#include "frame.h"
typedef struct wtop_console_frame {
    wtop_frame cells;
    CHAR_INFO *upload;
    size_t capacity;
    HANDLE output; /* Borrowed; owned by the application session. */
    SHORT left, top;
    DWORD error;
    unsigned long presents, unchanged, retries;
    HANDLE configured_output;
    DWORD original_output_mode;
    WORD inherited_attributes;
    int vt_enabled, mode_changed;
    WCHAR *vt_buffer;
    size_t vt_capacity, vt_used;
} wtop_console_frame;

/* Select VT only after the host accepts it. Never change palette/opacity.
   A legacy host retains the caller's background palette index. Call restore
   before closing a borrowed output handle; dispose is otherwise sufficient. */
int wtop_console_frame_configure(wtop_console_frame *host, HANDLE output,
                                  WORD inherited_attributes, int prefer_vt);
void wtop_console_frame_restore(wtop_console_frame *host);
/* No physical writes during composition. UI thread only; no callback retention.
   begin returns 0 on allocation/geometry error. present returns -1 to retry a
   changed/clipped viewport; it never acknowledges a partially written frame. */
int wtop_console_frame_begin(wtop_console_frame *host, HANDLE output,
                            int width, int height, int left, int top, WORD attribute);
int wtop_console_frame_present(wtop_console_frame *host);
void wtop_console_frame_dispose(wtop_console_frame *host);
#endif
