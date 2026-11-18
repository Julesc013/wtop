/* Actual Win32 block-output contract; no user-console input or mutation.
   SPDX-License-Identifier: GPL-3.0-only */
#include "console_frame_nt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static CHAR_INFO read_cell(HANDLE output, SHORT x, SHORT y)
{
    CHAR_INFO cell;
    COORD size = {1,1}, origin = {0,0};
    SMALL_RECT region = {x,y,x,y};
    memset(&cell,0,sizeof(cell));
    assert(ReadConsoleOutputW(output,&cell,size,origin,&region));
    assert(region.Left == x && region.Top == y);
    return cell;
}
int main(void)
{
    BOOL allocated = FALSE;
    HANDLE output;
    CONSOLE_SCREEN_BUFFER_INFO before, after;
    wtop_console_frame host;
    int width, height, i;
    if(GetConsoleWindow() == NULL) allocated = AllocConsole();
    output = CreateConsoleScreenBuffer(GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,CONSOLE_TEXTMODE_BUFFER,NULL);
    assert(output != INVALID_HANDLE_VALUE);
    assert(GetConsoleScreenBufferInfo(output,&before));
    width=before.srWindow.Right-before.srWindow.Left+1;
    height=before.srWindow.Bottom-before.srWindow.Top+1;
    assert(width > 1 && height > 1);
    memset(&host,0,sizeof(host));
    assert(wtop_console_frame_begin(&host,output,width,height,before.srWindow.Left,before.srWindow.Top,7));
    wtop_frame_fill(&host.cells,0,height-1,width,'A',0x30);
    assert(wtop_console_frame_present(&host)==1);
    assert(read_cell(output,before.srWindow.Right,before.srWindow.Bottom).Char.UnicodeChar=='A');
    assert(GetConsoleScreenBufferInfo(output,&after));
    assert(after.dwCursorPosition.X==before.dwCursorPosition.X && after.dwCursorPosition.Y==before.dwCursorPosition.Y);
    assert(after.srWindow.Top==before.srWindow.Top); /* bottom-right never scrolls */
    for(i=0;i<1000;++i) {
        assert(wtop_console_frame_begin(&host,output,width,height,before.srWindow.Left,before.srWindow.Top,7));
        /* Composition's blank backing frame never clears the live footer. */
        assert(read_cell(output,before.srWindow.Right,before.srWindow.Bottom).Char.UnicodeChar=='A');
        wtop_frame_fill(&host.cells,0,height-1,width,'A',0x30);
        assert(wtop_console_frame_present(&host)==0);
    }
    assert(host.presents==1 && host.unchanged==1000);
    wtop_frame_put(&host.cells,0,0,'Z',0x20);
    assert(wtop_console_frame_present(&host)==1);
    assert(read_cell(output,before.srWindow.Left,before.srWindow.Top).Char.UnicodeChar=='Z');
    ++host.left; /* changed viewport must not acknowledge or write stale geometry */
    assert(wtop_console_frame_present(&host)==-1 && !host.cells.valid);
    assert(host.error==ERROR_RETRY && host.presents==2);
    --host.left;
    assert(wtop_console_frame_present(&host)==1);
    printf("presenter: 1000 unchanged frames, no live clear, bottom-right cursor retained, clipped geometry refused\n");
    wtop_console_frame_dispose(&host);
    assert(CloseHandle(output));
    if(allocated) assert(FreeConsole());
    return 0;
}
