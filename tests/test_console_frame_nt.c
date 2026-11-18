/* Actual Win32 block-output contract; no user-console input or mutation.
   SPDX-License-Identifier: GPL-3.0-only */
#include "console_frame_nt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
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
static int contains_sequence(const wtop_console_frame *host, const WCHAR *text)
{
    size_t i, n = wcslen(text);
    for(i=0;i+n<=host->vt_used;++i)
        if(!wmemcmp(host->vt_buffer+i,text,n)) return 1;
    return 0;
}
int main(void)
{
    BOOL allocated = FALSE;
    HANDLE output;
    CONSOLE_SCREEN_BUFFER_INFO before, after;
    SMALL_RECT smaller, moved;
    CHAR_INFO cell;
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
    cell = read_cell(output,before.srWindow.Right,before.srWindow.Bottom);
    assert(cell.Char.UnicodeChar=='A' && cell.Attributes==0x30);
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
    cell = read_cell(output,before.srWindow.Left,before.srWindow.Top);
    assert(cell.Char.UnicodeChar=='Z' && cell.Attributes==0x20);
    /* An attribute-only update must preserve the glyph and both foreground
       and background bits, including combined selection/tag colors. */
    wtop_frame_attribute(&host.cells,0,0,0x3e);
    assert(wtop_console_frame_present(&host)==1);
    cell = read_cell(output,before.srWindow.Left,before.srWindow.Top);
    assert(cell.Char.UnicodeChar=='Z' && cell.Attributes==0x3e);

    /* Change the actual native viewport, not a private cached coordinate.
       No cells have changed: a sink-only geometry check would miss this. */
    smaller = before.srWindow;
    --smaller.Right; --smaller.Bottom;
    assert(SetConsoleWindowInfo(output,TRUE,&smaller));
    assert(wtop_console_frame_present(&host)==-1 && !host.cells.valid);
    assert(host.error==ERROR_RETRY && host.presents==3 && host.retries==1);
    cell = read_cell(output,before.srWindow.Right,before.srWindow.Bottom);
    assert(cell.Char.UnicodeChar=='A' && cell.Attributes==0x30);
    assert(wtop_console_frame_begin(&host,output,width-1,height-1,smaller.Left,smaller.Top,7));
    wtop_frame_fill(&host.cells,0,height-2,width-1,'R',0x4f);
    assert(wtop_console_frame_present(&host)==1);
    cell = read_cell(output,smaller.Right,smaller.Bottom);
    assert(cell.Char.UnicodeChar=='R' && cell.Attributes==0x4f);

    /* The shrunken window has room to move without resizing the buffer. */
    moved = smaller;
    ++moved.Left; ++moved.Right; ++moved.Top; ++moved.Bottom;
    assert(SetConsoleWindowInfo(output,TRUE,&moved));
    assert(wtop_console_frame_present(&host)==-1 && !host.cells.valid);
    assert(host.error==ERROR_RETRY && host.presents==4 && host.retries==2);
    cell = read_cell(output,smaller.Right,smaller.Bottom);
    assert(cell.Char.UnicodeChar=='R' && cell.Attributes==0x4f);
    assert(wtop_console_frame_begin(&host,output,width-1,height-1,moved.Left,moved.Top,7));
    wtop_frame_put(&host.cells,0,0,'M',0x0e);
    assert(wtop_console_frame_present(&host)==1);
    cell = read_cell(output,moved.Left,moved.Top);
    assert(cell.Char.UnicodeChar=='M' && cell.Attributes==0x0e);

    assert(SetConsoleWindowInfo(output,TRUE,&before.srWindow));
    assert(wtop_console_frame_begin(&host,output,width,height,before.srWindow.Left,before.srWindow.Top,7));
    wtop_frame_fill(&host.cells,0,height-1,width,'A',0x30);
    assert(wtop_console_frame_present(&host)==1);
    assert(host.presents==6 && host.unchanged==1000 && host.retries==2);
    assert(GetConsoleScreenBufferInfo(output,&after));
    assert(after.dwCursorPosition.X==before.dwCursorPosition.X && after.dwCursorPosition.Y==before.dwCursorPosition.Y);
    cell = read_cell(output,before.srWindow.Right,before.srWindow.Bottom);
    assert(cell.Char.UnicodeChar=='A' && cell.Attributes==0x30);
    printf("presenter: 1000 unchanged frames, no live clear, attribute-only update, actual resize/origin retry, cursor retained\n");
    /* Legacy fallback inherits the invoking palette background and strips the
       semantic flags rather than leaking them into COMMON_LVB attributes. */
    assert(wtop_console_frame_configure(&host,output,0x57,0));
    assert(wtop_console_frame_begin(&host,output,width,height,before.srWindow.Left,before.srWindow.Top,
        7|WTOP_ATTR_DEFAULT_BACKGROUND));
    wtop_frame_put(&host.cells,1,0,'B',3|WTOP_ATTR_DEFAULT_BACKGROUND);
    assert(wtop_console_frame_present(&host)==1);
    cell=read_cell(output,before.srWindow.Left+1,before.srWindow.Top);
    assert(cell.Char.UnicodeChar=='B' && cell.Attributes==0x53);
    {
        DWORD original, restored;
        assert(GetConsoleMode(output,&original));
        assert(wtop_console_frame_configure(&host,output,before.wAttributes,1));
        if(host.vt_enabled) {
            assert(wtop_console_frame_begin(&host,output,width,height,before.srWindow.Left,before.srWindow.Top,
                7|WTOP_ATTR_DEFAULT_BACKGROUND));
            wtop_frame_put(&host.cells,1,0,'V',0x30|WTOP_ATTR_EXPLICIT_BACKGROUND);
            wtop_frame_put(&host.cells,width-1,height-1,'Q',7|WTOP_ATTR_DEFAULT_BACKGROUND);
            assert(wtop_console_frame_present(&host)==1);
            assert(contains_sequence(&host,L"\x1b[0;37;49m"));
            assert(contains_sequence(&host,L"\x1b[0;30;46m"));
            cell=read_cell(output,before.srWindow.Left+1,before.srWindow.Top);
            assert(cell.Char.UnicodeChar=='V' && (cell.Attributes&0xff)==0x30);
            assert(GetConsoleScreenBufferInfo(output,&after));
            assert(after.srWindow.Top==before.srWindow.Top);
            cell=read_cell(output,before.srWindow.Right,before.srWindow.Bottom);
            assert(cell.Char.UnicodeChar=='Q');
            assert(wtop_console_frame_present(&host)==0);
            /* Reconfiguring an already configured handle must not capture the
               temporary VT/no-wrap mode as the mode to restore later. */
            assert(wtop_console_frame_configure(&host,output,before.wAttributes,1));
            assert(host.vt_enabled);
            assert(SetConsoleWindowInfo(output,TRUE,&moved));
            assert(wtop_console_frame_begin(&host,output,width-1,height-1,moved.Left,moved.Top,
                7|WTOP_ATTR_DEFAULT_BACKGROUND));
            wtop_frame_put(&host.cells,0,0,'O',7|WTOP_ATTR_DEFAULT_BACKGROUND);
            wtop_frame_put(&host.cells,width-2,height-2,'E',7|WTOP_ATTR_DEFAULT_BACKGROUND);
            assert(wtop_console_frame_present(&host)==1);
            cell=read_cell(output,moved.Left,moved.Top); assert(cell.Char.UnicodeChar=='O');
            cell=read_cell(output,moved.Right,moved.Bottom); assert(cell.Char.UnicodeChar=='E');
            assert(GetConsoleScreenBufferInfo(output,&after));
            assert(after.srWindow.Left==moved.Left && after.srWindow.Top==moved.Top);
            assert(SetConsoleWindowInfo(output,TRUE,&before.srWindow));
            puts("VT presenter: SGR49 background, explicit selection, bottom-right/no scroll, moved origin, exact mode restoration");
        } else puts("VT presenter unavailable: inherited-palette fallback verified");
        wtop_console_frame_restore(&host);
        assert(GetConsoleMode(output,&restored) && restored==original);
    }
    wtop_console_frame_dispose(&host);
    assert(CloseHandle(output));
    if(allocated) assert(FreeConsole());
    return 0;
}
