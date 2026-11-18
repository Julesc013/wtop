/* Deterministic native input/identity contracts against production routines.
   No process mutation, live collector, console injection or copied dispatcher.
   The renamed application entry point is linked but never entered.
   SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#error Native input fixtures require assertions in Release builds too.
#endif
#define wmain wtop_application_main
#include "../source/ntop.c"
#undef wmain

static void key(WORD code, WCHAR character, DWORD modifiers)
{
    KEY_EVENT_RECORD event = {0};
    BOOL redraw = FALSE;
    event.bKeyDown = TRUE;
    event.wRepeatCount = 1;
    event.wVirtualKeyCode = code;
    event.uChar.UnicodeChar = character;
    event.dwControlKeyState = modifiers;
    HandleKeyEvent(&event, &redraw);
}

static void compose(void)
{
    assert(wtop_frame_resize(&Screen.cells, Width, Height));
    wtop_frame_begin(&Screen.cells, (WORD)Settings.colors[WTOP_COLOR_BG]);
    RenderRow = RenderColumn = 0;
    CurrentColor = (WORD)Settings.colors[WTOP_COLOR_FG];
    ComposeFrame();
}

static void reset(void)
{
    CancelEditor(); CloseOverlay(); ClearViMessage(); ClearSelection();
    free(ProcessList);
    ProcessList = calloc(3, sizeof(*ProcessList));
    assert(ProcessList);
    wtop_settings_defaults(&Settings);
    Settings.header_count = 1;
    Settings.header_rows[0] = WTOP_ROW_TITLE;
    Settings.tabs[0].sort_column = WTOP_COLUMN_ID;
    Settings.tabs[0].descending = 0;
    ProcessCount = 3;
    for(DWORD i = 0; i < ProcessCount; ++i) {
        ProcessList[i].ID = 10 + i;
        ProcessList[i].CreationTime = 100 + i;
        _tcscpy_s(ProcessList[i].ExeName, MAX_PATH, _T("fixture.exe"));
    }
    ProcessIndex = 0; ViewCount = 0; VisibleProcessCount = 2;
    TaggedProcessListCount = 0;
    Paused = FALSE; EmacsPrefix = FALSE; SearchActive = FALSE;
    ConfigDirty = FALSE; ReadOnlyMode = FALSE;
    InterlockedExchange(&CollectionPaused, 0);
    InterlockedExchange(&ShutdownRequested, 0);
    if(ShutdownEvent) ResetEvent(ShutdownEvent);
    Width = 100; Height = 25; ConsoleOriginX = ConsoleOriginY = 0;
    MouseEnabled = TRUE; LastMouseButtons = 0;
    InteractiveMode = TRUE;
    ApplyActiveTab();
    assert(!HasSelection() && SelectedViewIndex == MAXDWORD);
    compose();
}

static void no_implicit_target(void)
{
    reset();
    assert(!DisplayedProcessID && !DisplayedCreationTime);
    wtop_ui_context_options context = CurrentUiContext();
    wtop_ui_binding binding;
    assert(!context.has_selection && !context.identity_known);
    wtop_ui_resolve_function(&context, 8, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_NO_SELECTION);
    key(VK_F9, 0, 0);
    assert(Overlay == OVERLAY_NONE && !PendingKills && !PendingKillCount);
    key(' ', ' ', 0);
    key('F', 'F', 0);
    assert(!HasSelection() && !TaggedProcessListCount && !FollowProcess);

    /* The first navigation gesture selects the first visible process rather
       than moving past a row the user had never selected. */
    key(VK_DOWN, 0, 0);
    assert(HasSelection() && SelectedIdentity.ID == 10);
    compose();
    assert(DisplayedProcessID == 10 && DisplayedCreationTime == 100);
    assert(Screen.cells.back[(size_t)ProcessWindowPosY * Width].attribute == 0x30);
    key(VK_ESCAPE, 27, 0);
    assert(!HasSelection());
    compose();
    assert(Screen.cells.back[(size_t)ProcessWindowPosY * Width].attribute == 0x07);
    key(VK_UP, 0, 0);
    assert(HasSelection() && SelectedIdentity.ID == 10);

    ClearSelection(); VisibleProcessCount = 2;
    BOOL redraw = FALSE;
    DoScroll(SCROLL_PAGE_DOWN, &redraw);
    assert(redraw && HasSelection() && SelectedIdentity.ID == 11);
    ClearSelection();
    StartSearch(_T("fixture"));
    assert(HasSelection() && SelectedIdentity.ID == 10);
}

static void lifetime_selection(void)
{
    reset();
    SelectView(0); compose();
    ChangeProcessSortType(SORT_BY_ID);
    ActiveTab()->descending = 1;
    ApplyActiveTab();
    assert(HasSelection() && SelectedIdentity.ID == 10 && SelectedViewIndex == 2);
    assert(ProcessList[SelectedProcessIndex].CreationTime == 100);
    compose();
    ToggleTaggedProcess(&ProcessList[SelectedProcessIndex]);
    key('F', 'F', 0);
    assert(FollowProcess);
    ProcessList[SelectedProcessIndex].CreationTime = 999; /* Actual PID reuse. */
    ReadjustCursor();
    assert(!HasSelection() && !FollowProcess && !DisplayedProcessID);
    assert(!IsProcessTagged(&ProcessList[2]));
    TaggedProcessListCount = 0;
    key(VK_F9, 0, 0);
    assert(!PendingKillCount && Overlay == OVERLAY_NONE);

    SelectView(1);
    DWORD survivor = SelectedIdentity.ID;
    ProcessList[SelectedProcessIndex].ID = 40; /* Selected lifetime disappeared. */
    ReadjustCursor();
    assert(!HasSelection() && survivor == 11);
    SelectView(0);
    SetPresentationFilter(_T("no-match"));
    assert(!ViewCount && !HasSelection());
    SetPresentationFilter(_T(""));
    assert(ViewCount == 3 && !HasSelection());
    ProcessCount = 0;
    ReadjustCursor();
    assert(!ViewCount && !HasSelection() && !ProcessIndex);
}

static void mouse_and_presented_identity(void)
{
    reset();
    MOUSE_EVENT_RECORD mouse = {0};
    BOOL redraw = FALSE;
    mouse.dwMousePosition.X = 0;
    mouse.dwMousePosition.Y = (SHORT)(ProcessWindowPosY + 1);
    mouse.dwButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
    HandleMouse(&mouse, &redraw);
    assert(redraw && HasSelection() && SelectedIdentity.ID == 11);
    /* A key queued behind a click has not yet acknowledged the new highlight. */
    assert(!SelectionMatchesDisplay());
    compose();
    assert(SelectionMatchesDisplay());
    mouse.dwButtonState = 0; HandleMouse(&mouse, &redraw);
    ClearSelection();
    ProcessList[1].CreationTime = 1000; /* Click still carries the old frame ID. */
    mouse.dwButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
    HandleMouse(&mouse, &redraw);
    assert(!HasSelection());
    mouse.dwButtonState = 0; HandleMouse(&mouse, &redraw);
    compose();
    mouse.dwButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
    RenderInvalidated = TRUE; /* Geometry/layout changed; never reuse its hits. */
    HandleMouse(&mouse, &redraw);
    assert(!HasSelection());
}

static void escape_layers(void)
{
    reset();
    SelectView(0); compose();
    ToggleTaggedProcess(&ProcessList[SelectedProcessIndex]);
    key('F', 'F', 0);
    assert(FollowProcess && TaggedProcessListCount == 1);
    key(VK_F1, 0, 0);
    assert(Overlay == OVERLAY_HELP);
    key(VK_ESCAPE, 27, 0);
    assert(Overlay == OVERLAY_NONE && HasSelection() && FollowProcess);
    key(':', ':', 0);
    key('Q', 'q', 0); /* q remains inert editor text. */
    assert(InInputMode && !ShouldStop());
    key(VK_ESCAPE, 27, 0);
    assert(!InInputMode && HasSelection() && TaggedProcessListCount == 1);
    key(VK_F9, 0, 0);
    assert(Overlay == OVERLAY_KILL && PendingKillCount == 1);
    key(VK_ESCAPE, 27, 0);
    assert(Overlay == OVERLAY_NONE && !PendingKills && HasSelection());

    EmacsPrefix = TRUE;
    key(VK_ESCAPE, 27, 0);
    assert(!EmacsPrefix && HasSelection());
    SearchActive = TRUE;
    ConfigDirty = TRUE;
    wtop_settings before = Settings;
    key(VK_ESCAPE, 27, 0);
    assert(!HasSelection() && !TaggedProcessListCount && !FollowProcess && !SearchActive);
    assert(ConfigDirty && !memcmp(&Settings, &before, sizeof(Settings)));
    key(VK_ESCAPE, 27, 0); /* Idempotent recovery, never an implicit quit. */
    assert(!HasSelection() && !ShouldStop());

    key(VK_F4, 0, 0);
    key('X', 'x', 0);
    assert(InInputMode && !strcmp(ActiveTab()->filter, "x"));
    key(VK_ESCAPE, 27, 0);
    assert(!InInputMode && !ActiveTab()->filter[0] && !HasSelection());
    assert(ConfigDirty); /* The pre-existing dirty ownership is restored. */
}

static void pending_samples_do_not_drop_hits(void)
{
    reset();
    StartEditor(':'); compose();
    PendingSample = calloc(1, sizeof(*PendingSample));
    assert(PendingSample);
    PendingSample->rows = calloc(1, sizeof(*PendingSample->rows));
    assert(PendingSample->rows);
    PendingSample->rows[0].ID = 80;
    PendingSample->rows[0].CreationTime = 800;
    _tcscpy_s(PendingSample->rows[0].ExeName, MAX_PATH, _T("new.exe"));
    PendingSample->count = 1;
    assert(!AdoptFrameSamples(FALSE));
    assert(PendingSample && ProcessCount == 3 && !RenderInvalidated);
    MOUSE_EVENT_RECORD mouse = {0};
    BOOL redraw = FALSE;
    assert(FooterWidths[0] > 0 && FooterStarts[0] >= Width / 2);
    mouse.dwMousePosition.X = (SHORT)(FooterStarts[0] + FooterWidths[0] / 2);
    mouse.dwMousePosition.Y = (SHORT)FunctionBarRow;
    mouse.dwButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
    HandleMouse(&mouse, &redraw);
    assert(redraw && Overlay == OVERLAY_HELP && !InInputMode);
    assert(AdoptFrameSamples(TRUE));
    assert(!PendingSample && ProcessCount == 1 && RenderInvalidated);
    compose();
    assert(!RenderInvalidated && Overlay == OVERLAY_HELP);
}

static void pending_sample_selection(void)
{
    for(int known = 0; known < 2; ++known) {
        reset();
        ProcessList[1].CreationTime = known ? 101 : 0;
        SelectView(1); compose();
        assert(HasSelection());
        PendingSample = calloc(1, sizeof(*PendingSample));
        assert(PendingSample);
        PendingSample->rows = calloc(1, sizeof(*PendingSample->rows));
        assert(PendingSample->rows);
        PendingSample->count = 1;
        PendingSample->rows[0].ID = 11;
        PendingSample->rows[0].CreationTime = 101;
        _tcscpy_s(PendingSample->rows[0].ExeName, MAX_PATH, _T("fixture.exe"));
        assert(AdoptFrameSamples(TRUE));
        assert(!PendingSample && ProcessCount == 1);
        assert(HasSelection() == known);
        if(known) assert(SelectedIdentity.ID == 11 && SelectedIdentity.CreationTime == 101 && SelectedViewIndex == 0);
        else assert(!DisplayedProcessID && SelectedViewIndex == MAXDWORD);
    }
}

static void interrupt_every_context(void)
{
    for(int map = WTOP_KEYMAP_HTOP; map <= WTOP_KEYMAP_EMACS; ++map) {
        /* Main, Help, Setup, Sort, Kill, Results, command/search/filter/rename
           editors, pause and the Emacs prefix all share one raw-key contract. */
        for(int state = 0; state < 12; ++state) {
            reset(); Settings.keymap = (wtop_keymap)map;
            SelectView(0); compose();
            if(state == 1) Overlay = OVERLAY_HELP;
            if(state == 2) Overlay = OVERLAY_SETUP;
            if(state == 3) Overlay = OVERLAY_SORT;
            if(state == 4) BeginKill();
            if(state == 5) Overlay = OVERLAY_RESULTS;
            if(state >= 6 && state <= 8) StartEditor(state == 6 ? ':' : state == 7 ? '/' : '\\');
            if(state == 9) { Overlay = OVERLAY_SETUP; SetupPage = 3; SetupIndex = 0; EditSetupTab(WTOP_UI_TAB_RENAME); }
            if(state == 10) { Paused = TRUE; InterlockedExchange(&CollectionPaused, 1); }
            if(state == 11) EmacsPrefix = TRUE;
            key('C', 3, LEFT_CTRL_PRESSED);
            assert(ShouldStop());
            assert(WaitForSingleObject(ShutdownEvent, 0) == WAIT_OBJECT_0);
            overlay_type overlay = Overlay;
            key('Y', 'y', 0); /* Same-batch input after shutdown must be discarded. */
            assert(Overlay == overlay);
        }
    }
}

int main(void)
{
    InitializeCriticalSection(&SyncLock);
    ShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    TaggedProcessList = calloc(TaggedProcessListSize, sizeof(*TaggedProcessList));
    ViMessage = calloc(DEFAULT_STR_SIZE, sizeof(*ViMessage));
    assert(ShutdownEvent && TaggedProcessList && ViMessage && ViInit());
    no_implicit_target();
    lifetime_selection();
    mouse_and_presented_identity();
    escape_layers();
    pending_samples_do_not_drop_hits();
    pending_sample_selection();
    interrupt_every_context();
    CancelEditor(); CloseOverlay();
    ViDestroy();
    free(ViMessage); free(TaggedProcessList); free(ProcessList);
    free(ViewIndices); free(RenderedRows);
    wtop_console_frame_dispose(&Screen);
    CloseHandle(ShutdownEvent);
    DeleteCriticalSection(&SyncLock);
    puts("native input: selection lifetime, layered Escape, queued-click adoption and global interrupt contracts passed");
    return 0;
}
