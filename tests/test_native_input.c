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
    CancelEditor(); CloseOverlay(); ClearViMessage(); ClearSelection(); ClearFocus();
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
    assert(!HasSelection() && !HasFocus() && SelectedViewIndex == MAXDWORD);
    compose();
}

static void no_implicit_target(void)
{
    reset();
    assert(!DisplayedProcessID && !DisplayedCreationTime && !HasFocus());
    wtop_ui_context_options context = CurrentUiContext();
    wtop_ui_binding binding;
    assert(!context.has_selection && !context.identity_known && !context.has_focus);
    wtop_ui_resolve_function(&context, 8, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_NO_SELECTION);
    key(VK_F9, 0, 0);
    assert(Overlay == OVERLAY_NONE && !PendingKills && !PendingKillCount);
    key(' ', ' ', 0); key('F', 'F', 0);
    assert(!HasSelection() && !TaggedProcessListCount && !FollowProcess);

    key(VK_DOWN, 0, 0);
    assert(HasFocus() && FocusedIdentity.ID == 10 && !HasSelection());
    compose();
    assert(!DisplayedProcessID);
    assert((Screen.cells.back[(size_t)ProcessWindowPosY * Width].attribute & 255) == 0x8f);
    key(VK_F9, 0, 0);
    assert(Overlay == OVERLAY_NONE && !PendingKills);
    key(VK_RETURN, '\r', 0);
    assert(HasSelection() && SelectedIdentity.ID == 10);
    compose();
    assert(DisplayedProcessID == 10 && DisplayedCreationTime == 100);
    assert((Screen.cells.back[(size_t)ProcessWindowPosY * Width].attribute & 255) == 0x30);
    key(VK_DOWN, 0, 0);
    assert(HasFocus() && FocusedIdentity.ID == 11 && HasSelection() && SelectedIdentity.ID == 10);
    compose();
    key(VK_ESCAPE, 27, 0);
    assert(!HasSelection() && HasFocus() && FocusedIdentity.ID == 10);
    compose();
    assert((Screen.cells.back[(size_t)ProcessWindowPosY * Width].attribute & 255) == 0x8f);
    key(VK_ESCAPE, 27, 0);
    assert(!HasSelection() && !HasFocus());
    compose();
    assert(Screen.cells.back[(size_t)ProcessWindowPosY * Width].attribute & WTOP_ATTR_DEFAULT_BACKGROUND);
    assert((Screen.cells.back[(size_t)ProcessWindowPosY * Width].attribute & 255) == 0x07);

    VisibleProcessCount = 2;
    BOOL redraw = FALSE;
    DoScroll(SCROLL_PAGE_DOWN, &redraw);
    assert(redraw && HasFocus() && FocusedIdentity.ID == 11 && !HasSelection());
    ClearFocus();
    StartSearch(_T("fixture"));
    assert(HasFocus() && FocusedIdentity.ID == 10 && !HasSelection());
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

static void hover_never_retargets_selection(void)
{
    reset();
    MOUSE_EVENT_RECORD mouse = {0};
    BOOL redraw = FALSE;
    mouse.dwMousePosition.X = 1;
    mouse.dwMousePosition.Y = (SHORT)(ProcessWindowPosY + 1);
    mouse.dwEventFlags = MOUSE_MOVED;
    HandleMouse(&mouse, &redraw);
    assert(redraw && HasFocus() && FocusedIdentity.ID == 11 && !HasSelection());
    compose();
    key(VK_RETURN, '\r', 0); compose();
    assert(SelectedIdentity.ID == 11 && SelectionMatchesDisplay());
    mouse.dwMousePosition.Y = (SHORT)ProcessWindowPosY;
    HandleMouse(&mouse, &redraw); compose();
    assert(FocusedIdentity.ID == 10 && SelectedIdentity.ID == 11);
    key(VK_F9, 0, 0);
    assert(Overlay == OVERLAY_KILL && PendingKillCount == 1 && PendingKills[0].ID == 11);
    key(VK_ESCAPE, 27, 0);
    assert(HasSelection() && SelectedIdentity.ID == 11 && FocusedIdentity.ID == 10);
    Settings.colors[WTOP_COLOR_FG_FOCUS] = 2;
    Settings.colors[WTOP_COLOR_BG_FOCUS] = 5;
    Settings.colors[WTOP_COLOR_FG_SELECTED] = 14;
    Settings.colors[WTOP_COLOR_BG_SELECTED] = 0;
    compose();
    assert((Screen.cells.back[(size_t)ProcessWindowPosY * Width].attribute & 255) == 0x52);
    WORD selected = Screen.cells.back[(size_t)(ProcessWindowPosY + 1) * Width].attribute;
    assert((selected & 255) == 0x0e && (selected & WTOP_ATTR_EXPLICIT_BACKGROUND));
    assert(!(selected & WTOP_ATTR_DEFAULT_BACKGROUND));
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
    assert(!HasSelection() && !TaggedProcessListCount && HasFocus() && SearchActive);
    assert(ConfigDirty && !memcmp(&Settings, &before, sizeof(Settings)));
    key(VK_ESCAPE, 27, 0); /* Second main Escape retires highlight/search/follow. */
    assert(!HasSelection() && !HasFocus() && !FollowProcess && !SearchActive && !ShouldStop());
    key(VK_ESCAPE, 27, 0); /* Idempotent, never implicit quit. */
    assert(!ShouldStop());

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

static void frame_text(int row, char *text, size_t capacity)
{
    assert(row >= 0 && row < Height && capacity > (size_t)Width);
    for(int column = 0; column < Width; ++column)
        text[column] = (char)Screen.cells.back[(size_t)row * Width + column].glyph;
    text[Width] = 0;
}

static void diagnostic_keeps_controls(void)
{
    reset();
    Width = 120;
    ReadOnlyMode = TRUE;
    assert(!SavePresentationSettings());
    key(VK_F2, 0, 0);
    compose();
    char line[121];
    assert(Overlay == OVERLAY_SETUP && FunctionBarRow == Height - 1 && StatusRow == -1);
    assert(Chrome.content_end == Height - 1); /* No second default footer. */
    frame_text(2, line, sizeof(line));
    assert(strstr(line, "Configuration writes disabled by --readonly"));
    frame_text(Height - 1, line, sizeof(line));
    assert(strstr(line, "F9Save") && strstr(line, "F10Quit"));
    for(int x = FooterStarts[8]; x < FooterStarts[8] + FooterWidths[8]; ++x)
        assert((Screen.cells.back[(size_t)(Height - 1) * Width + x].attribute & 255) == 0x08);

    MOUSE_EVENT_RECORD mouse = {0};
    BOOL redraw = FALSE;
    mouse.dwMousePosition.X = (SHORT)(FooterStarts[8] + FooterWidths[8] / 2);
    mouse.dwMousePosition.Y = (SHORT)FunctionBarRow;
    mouse.dwButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
    HandleMouse(&mouse, &redraw);
    assert(redraw && Overlay == OVERLAY_SETUP);
    compose();
    frame_text(2, line, sizeof(line));
    assert(strstr(line, "Save: read-only"));
    frame_text(Height - 1, line, sizeof(line));
    assert(strstr(line, "F9Save") && strstr(line, "F10Quit"));

    /* A hidden title/status preference still has a bounded, reserved banner
       for the error; neither diagnostics nor controls overwrite process rows. */
    for(int style = WTOP_FOOTER_FUNCTIONS; style <= WTOP_FOOTER_COMPACT; ++style) {
        reset(); Width = 120; Settings.footer_style = (wtop_footer_style)style;
        Settings.status_position = WTOP_STATUS_HIDDEN;
        Settings.header_count = 0;
        SetViMessage(VI_ERROR, _T("Fixture persistent diagnostic"));
        compose();
        assert(Chrome.banner_status && Chrome.content_end == Height - 1);
        assert(FunctionBarRow == Height - 1 && StatusRow == -1);
        assert(ProcessWindowPosY > 0);
        frame_text(0, line, sizeof(line));
        assert(strstr(line, "Fixture persistent diagnostic"));
        frame_text(Height - 1, line, sizeof(line));
        assert(strstr(line, "F1Help") && strstr(line, "F9Kill") && strstr(line, "F10Quit"));
        for(int i = 0; i < 10; ++i) if(FooterWidths[i]) {
            assert(FooterStarts[i] >= 0 && FooterStarts[i] + FooterWidths[i] <= Width);
            assert(Screen.cells.back[(size_t)FunctionBarRow * Width + FooterStarts[i]].glyph == 'F');
        }
    }

    reset();
    Settings.status_position = WTOP_STATUS_BOTTOM;
    SetViMessage(VI_ERROR, _T("Bottom diagnostic"));
    compose();
    assert(StatusRow == Height - 2 && FunctionBarRow == Height - 1);
    frame_text(StatusRow, line, sizeof(line));
    assert(strstr(line, "Bottom diagnostic"));

    for(int status_only = 0; status_only < 2; ++status_only) {
        reset();
        Settings.show_function_bar = status_only;
        Settings.footer_style = status_only ? WTOP_FOOTER_STATUS : WTOP_FOOTER_FUNCTIONS;
        SetViMessage(VI_ERROR, _T("Visible diagnostic without buttons"));
        compose();
        assert(FunctionBarRow == -1 && StatusRow == Height - 1);
        frame_text(StatusRow, line, sizeof(line));
        assert(strstr(line, "Visible diagnostic without buttons"));
        for(int i = 0; i < 10; ++i) assert(!FooterWidths[i]);
    }

    reset(); Width = 40; Height = 10;
    SetViMessage(VI_ERROR, _T("Narrow error"));
    compose();
    assert(Chrome.content_end == 9 && FunctionBarRow == 9 && StatusRow == -1);
    frame_text(0, line, sizeof(line));
    assert(strstr(line, "Narrow error"));
    for(int i = 0; i < 10; ++i) assert(FooterWidths[i] == 4 && FooterStarts[i] == i * 4);
    Width = 39; Height = 9;
    compose();
    assert(RenderInvalidated && !RenderedRowCount);
    frame_text(0, line, sizeof(line));
    assert(strstr(line, "Resize to at least"));
    frame_text(1, line, sizeof(line));
    assert(strstr(line, "Narrow error"));
    for(int i = 0; i < 10; ++i) assert(!FooterWidths[i]);
}

static void overlays_keep_live_status(void)
{
    char line[121];
    reset(); Width = 120;
    ObservedProcessCount = 7;
    SampleSequence = 1;
    LastSampleObservedTick = GetTickCount64();
    LastSampleInterval = 1000;
    for(int kind = OVERLAY_HELP; kind <= OVERLAY_RESULTS; ++kind) {
        Overlay = (overlay_type)kind;
        compose();
        assert(Chrome.banner_status && StatusRow == -1 && FunctionBarRow == Height - 1);
        assert(Chrome.content_end == Height - 1);
        frame_text(2, line, sizeof(line));
        assert(strstr(line, "age ") && strstr(line, "view Main") && strstr(line, "rows 3/7"));
    }

    CloseOverlay(); SelectView(0); compose(); BeginKill();
    assert(Overlay == OVERLAY_KILL && PendingKillCount == 1 && PendingKills[0].ID == 10);
    PendingSample = calloc(1, sizeof(*PendingSample));
    assert(PendingSample);
    PendingSample->rows = calloc(2, sizeof(*PendingSample->rows));
    assert(PendingSample->rows);
    memcpy(PendingSample->rows, ProcessList + 1, 2 * sizeof(*ProcessList));
    PendingSample->count = 2;
    PendingSample->observed_count = 6;
    PendingSample->complete = TRUE;
    PendingSample->sequence = 2;
    PendingSample->observed = GetTickCount64();
    PendingSample->interval = 1000;
    assert(AdoptFrameSamples(TRUE));
    compose();
    assert(Overlay == OVERLAY_KILL && !HasSelection());
    assert(PendingKillCount == 1 && PendingKills[0].ID == 10 && PendingKills[0].CreationTime == 100);
    frame_text(2, line, sizeof(line));
    assert(strstr(line, "rows 2/6"));
    frame_text(3, line, sizeof(line));
    assert(strstr(line, "PID 10  creation 100"));
    assert(!FooterWidths[8] && FooterWidths[9]); /* Repeated F9 cannot confirm. */

    /* Banner telemetry follows the saved placement. Explicit diagnostics may
       still occupy the reserved row, but do not leak hidden ordinary status. */
    Settings.status_position = WTOP_STATUS_HIDDEN;
    compose();
    frame_text(2, line, sizeof(line));
    assert(strspn(line, " ") == (size_t)Width);
    SetViMessage(VI_ERROR, _T("Captured action needs review"));
    compose();
    frame_text(2, line, sizeof(line));
    assert(strstr(line, "Captured action needs review") && !strstr(line, "rows "));
    ClearViMessage();
    Settings.status_position = WTOP_STATUS_BOTTOM;
    compose();
    assert(StatusRow == Height - 2 && FunctionBarRow == Height - 1);
    frame_text(2, line, sizeof(line));
    assert(strspn(line, " ") == (size_t)Width);
    frame_text(StatusRow, line, sizeof(line));
    assert(strstr(line, "rows 2/6"));
}

static int frame_contains(const char *needle)
{
    char line[121];
    assert(Width <= 120);
    for(int row = 0; row < Height; ++row) {
        frame_text(row, line, sizeof(line));
        if(strstr(line, needle)) return 1;
    }
    return 0;
}

static void editor_grapheme_navigation(void)
{
    reset();
    static const WCHAR combining[] = { L'e', 0x0301, L'Z', 0 };
    static const WCHAR emoji[] = { 0xd83d, 0xdc69, 0x200d, 0xd83d, 0xdcbb, L'Z', 0 };
    assert(ViSetInputText(L':', combining));
    assert(ViInputCursor() == 4);
    key(VK_LEFT, 0, 0); assert(ViInputCursor() == 3);
    key(VK_LEFT, 0, 0); assert(ViInputCursor() == 1);
    key(VK_RIGHT, 0, 0); assert(ViInputCursor() == 3);
    key(VK_BACK, 0, 0); assert(ViInputCursor() == 1 && !wcscmp(CurrentInputStr, L":Z"));
    key(VK_DELETE, 0, 0); assert(!wcscmp(CurrentInputStr, L":"));
    assert(ViSetInputText(L':', emoji));
    assert(ViInputCursor() == 7);
    key(VK_LEFT, 0, 0); assert(ViInputCursor() == 6);
    key(VK_LEFT, 0, 0); assert(ViInputCursor() == 1);
    key(VK_RIGHT, 0, 0); assert(ViInputCursor() == 6);
    key(VK_BACK, 0, 0); assert(ViInputCursor() == 1 && !wcscmp(CurrentInputStr, L":Z"));
    key(VK_ESCAPE, 27, 0);
    assert(!InInputMode && !HasSelection() && !HasFocus());
    StartEditor(L':');
    key('E', L'e', 0); key(VK_PACKET, 0x0301, 0);
    assert(ViInputCursor() == 3);
    key(VK_BACK, 0, 0);
    assert(ViInputCursor() == 1 && !wcscmp(CurrentInputStr, L":"));
    key(VK_ESCAPE, 27, 0);
    {
        static const WCHAR leading_mark[] = {0x0301, L'Z', 0};
        static const WCHAR adjacent_emoji[] = {0xd83d,0xdc69,0xd83d,0xdcbb,0};
        assert(ViSetInputText(L':', leading_mark)); key(VK_HOME, 0, 0);
        /* Prompt and body are separate grapheme streams. These assertions use
           the actual composed frame and Vi caret, not a duplicate renderer. */
        Screen.vt_enabled = 1;
        Screen.grapheme_enabled = 1;
        compose();
        size_t row = (size_t)StatusRow * (size_t)Width;
        assert(Screen.cells.back[row].glyph == ':' && !Screen.cells.back[row].cluster_units);
        assert(Screen.cells.back[row + 1].glyph == 0x25cc && Screen.cells.back[row + 1].cluster_units == 2);
        assert(Screen.cells.back[row + 2].glyph == 'Z');
        assert((Screen.cells.back[row + 1].attribute & 255) == 0x30);
        key(VK_RIGHT, 0, 0); compose();
        assert(ViInputCursor() == 2 && (Screen.cells.back[row + 2].attribute & 255) == 0x30);
        key(VK_HOME, 0, 0);
        /* VT colours can remain available on a host that failed the native
           cluster-width probe; rendering must still use the fallback. */
        Screen.grapheme_enabled = 0;
        compose();
        assert(Screen.cells.back[row].glyph == ':' && Screen.cells.back[row + 1].glyph == '?');
        assert(Screen.cells.back[row + 2].glyph == 'Z' && (Screen.cells.back[row + 1].attribute & 255) == 0x30);
        Screen.vt_enabled = 0;
        key('E', L'e', 0);
        assert(ViInputCursor() == 3); /* Newly joined e+accent, not an interior caret. */
        key(VK_BACK, 0, 0); assert(!wcscmp(CurrentInputStr, L":Z"));
        assert(ViSetInputText(L':', adjacent_emoji)); key(VK_HOME, 0, 0); key(VK_RIGHT, 0, 0);
        assert(ViInputCursor() == 3);
        key(VK_PACKET, 0x200d, 0); /* Insertion joins both neighboring pictographs. */
        assert(ViInputCursor() == 6);
        key(VK_BACK, 0, 0); assert(!wcscmp(CurrentInputStr, L":"));
        key(VK_ESCAPE, 27, 0);
    }
}

static void captured_inspection_panels(void)
{
    reset(); SelectView(0); compose();
    DetailIdentity = SelectedIdentity; DetailSnapshot = ProcessList[SelectedProcessIndex];
    Overlay = OVERLAY_DETAILS;
    DetailObservation = calloc(1, sizeof(*DetailObservation));
    assert(DetailObservation);
    DetailObservation->kind = WTOP_PROVIDER_PROCESS_DETAILS;
    DetailObservation->state = WTOP_PROVIDER_VALID; DetailObservation->response_received = 1;
    DetailObservation->pid = 10; DetailObservation->creation = IdentityFileTime(DetailIdentity);
    wtop_provider_details *details = &DetailObservation->data.details;
    details->image_status.state = WTOP_PROVIDER_VALID;
    wcscpy_s(details->image_path, _countof(details->image_path), L"C:\\owned\\fixture.exe");
    details->command_status.state = WTOP_PROVIDER_VALID;
    for(int i = 0; i < 6000; ++i) details->command_line[i] = L'x';
    wcscpy_s(details->command_line + 6000, _countof(details->command_line) - 6000, L"TAIL-COMMAND-MARKER");
    details->services_status.state = WTOP_PROVIDER_DENIED;
    details->services_status.error_domain = WTOP_PROVIDER_ERROR_WIN32;
    details->services_status.native_error = ERROR_ACCESS_DENIED;
    details->affinity.status.state = WTOP_PROVIDER_UNSUPPORTED;
    compose();
    assert(frame_contains("Process details") && DetailLineCount > 60);
    assert(!frame_contains("TAIL-COMMAND-MARKER"));
    int reached_tail = 0, reached_denied = 0;
    for(int i = 0; i < 8; ++i) {
        key(VK_NEXT, 0, 0); compose();
        reached_tail |= frame_contains("TAIL-COMMAND-MARKER");
        reached_denied |= frame_contains("Services: denied");
    }
    assert(reached_tail && reached_denied); /* Full content, not a clipped formatter prefix. */
    key(VK_ESCAPE, 27, 0);
    assert(Overlay == OVERLAY_NONE && !DetailObservation && HasSelection());

    DetailIdentity = SelectedIdentity; DetailSnapshot = ProcessList[SelectedProcessIndex];
    Overlay = OVERLAY_THREADS;
    DetailObservation = calloc(1, sizeof(*DetailObservation));
    assert(DetailObservation);
    DetailObservation->kind = WTOP_PROVIDER_THREADS;
    DetailObservation->state = WTOP_PROVIDER_CAPACITY; DetailObservation->response_received = 1;
    DetailObservation->count = 3; DetailObservation->total_count = 4;
    for(DWORD i = 0; i < 3; ++i) {
        wtop_provider_thread *thread = &DetailObservation->data.threads[i];
        thread->tid = 800 + i; thread->pid = 10; thread->creation.dwLowDateTime = 4000 + i;
        thread->identity_status.state = WTOP_PROVIDER_VALID;
        thread->times_status.state = i == 1 ? WTOP_PROVIDER_DENIED : WTOP_PROVIDER_VALID;
        thread->priority_status.state = i == 1 ? WTOP_PROVIDER_DENIED : WTOP_PROVIDER_VALID;
        thread->kernel_100ns = 123; thread->user_100ns = 456; thread->priority = 1;
    }
    compose();
    assert(frame_contains("Thread rows") && frame_contains("3 returned / 4 observed"));
    assert(frame_contains("TID 800") && frame_contains("TID 802") && frame_contains("execution time: denied"));
    assert(frame_contains("relative priority ? [denied]"));
    key(VK_ESCAPE, 27, 0);

    DetailIdentity = SelectedIdentity; DetailSnapshot = ProcessList[SelectedProcessIndex];
    Overlay = OVERLAY_AFFINITY;
    DetailObservation = calloc(1, sizeof(*DetailObservation));
    assert(DetailObservation);
    DetailObservation->kind = WTOP_PROVIDER_AFFINITY;
    DetailObservation->observed_ms = GetTickCount64();
    DetailObservation->state = WTOP_PROVIDER_VALID;
    DetailObservation->data.affinity.status.state = WTOP_PROVIDER_VALID;
    DetailObservation->data.affinity.mask_bits = 4;
    DetailObservation->data.affinity.system_mask = 15;
    DetailObservation->data.affinity.process_mask = AffinityMask = 3;
    AffinityChoice = 0;
    compose();
    key(' ', ' ', 0); assert(AffinityMask == 2);
    key(VK_RETURN, '\r', 0); assert(Overlay == OVERLAY_AFFINITY_CONFIRM);
    key(VK_F9, 0, 0); key(VK_RETURN, '\r', 0);
    assert(Overlay == OVERLAY_AFFINITY_CONFIRM && !AffinityActionRunning && !DetailWorker.initialized);
    key(VK_ESCAPE, 27, 0); assert(Overlay == OVERLAY_AFFINITY);
    AffinityMask = 0; ReviewAffinity(); assert(Overlay == OVERLAY_AFFINITY && frame_contains("Affinity"));
    ReadOnlyMode = TRUE; AffinityMask = 2; ToggleAffinityChoice();
    assert(AffinityMask == 2 && !AffinityActionRunning);
    key(VK_ESCAPE, 27, 0); assert(!DetailObservation);
}

static void interrupt_every_context(void)
{
    for(int map = WTOP_KEYMAP_HTOP; map <= WTOP_KEYMAP_EMACS; ++map) {
        /* Main, Help, Setup, Sort, Kill, Results, command/search/filter/rename
           editors, pause and the Emacs prefix all share one raw-key contract. */
        for(int state = 0; state < 16; ++state) {
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
            if(state == 12) Overlay = OVERLAY_DETAILS;
            if(state == 13) Overlay = OVERLAY_THREADS;
            if(state == 14) Overlay = OVERLAY_AFFINITY;
            if(state == 15) Overlay = OVERLAY_AFFINITY_CONFIRM;
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
    hover_never_retargets_selection();
    escape_layers();
    pending_samples_do_not_drop_hits();
    pending_sample_selection();
    diagnostic_keeps_controls();
    overlays_keep_live_status();
    editor_grapheme_navigation();
    captured_inspection_panels();
    interrupt_every_context();
    CancelEditor(); CloseOverlay();
    ViDestroy();
    free(ViMessage); free(TaggedProcessList); free(ProcessList);
    free(ViewIndices); free(RenderedRows); free(CollapsedProcesses);
    wtop_console_frame_dispose(&Screen);
    CloseHandle(ShutdownEvent);
    DeleteCriticalSection(&SyncLock);
    puts("native input: selection lifetime, layered Escape, queued-click adoption and global interrupt contracts passed");
    return 0;
}
