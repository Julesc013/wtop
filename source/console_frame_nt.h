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
} wtop_console_frame;
/* No physical writes during composition. UI thread only; no callback retention.
   begin returns 0 on allocation/geometry error. present returns -1 to retry a
   changed/clipped viewport; it never acknowledges a partially written frame. */
int wtop_console_frame_begin(wtop_console_frame *host, HANDLE output,
                            int width, int height, int left, int top, WORD attribute);
int wtop_console_frame_present(wtop_console_frame *host);
void wtop_console_frame_dispose(wtop_console_frame *host);
#endif
