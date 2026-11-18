/* Portable UI contracts. SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
#include "text.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void keymaps(void)
{
    int map;
    for(map = WTOP_KEYMAP_HTOP; map <= WTOP_KEYMAP_EMACS; ++map) {
        assert(wtop_ui_key_action(map, WTOP_KEY_F1, 0) == WTOP_UI_HELP);
        assert(wtop_ui_key_action(map, WTOP_KEY_F2, 0) == WTOP_UI_SETUP);
        assert(wtop_ui_key_action(map, WTOP_KEY_F7, 0) == WTOP_UI_PRIORITY_UP);
        assert(wtop_ui_key_action(map, WTOP_KEY_F8, 0) == WTOP_UI_PRIORITY_DOWN);
        assert(wtop_ui_key_action(map, WTOP_KEY_F9, 0) == WTOP_UI_KILL);
        assert(wtop_ui_key_action(map, WTOP_KEY_F10, 0) == WTOP_UI_QUIT);
        assert(wtop_ui_key_action(map, WTOP_KEY_UP, 0) == WTOP_UI_UP);
        assert(wtop_ui_key_action(map, '\t', WTOP_MOD_SHIFT) == WTOP_UI_PREVIOUS_TAB);
        assert(wtop_ui_key_action(map, 'B', 0) == WTOP_UI_BAR);
        assert(wtop_ui_key_action(map, 3, 0) == WTOP_UI_QUIT);
        assert(wtop_ui_key_action(map, 'c', WTOP_MOD_CTRL) == WTOP_UI_QUIT);
    }
    assert(wtop_ui_key_action(WTOP_KEYMAP_HTOP, 'k', 0) == WTOP_UI_KILL);
    assert(wtop_ui_key_action(WTOP_KEYMAP_VI, 'k', 0) == WTOP_UI_UP);
    assert(wtop_ui_key_action(WTOP_KEYMAP_VI, 'j', 0) == WTOP_UI_DOWN);
    assert(wtop_ui_key_action(WTOP_KEYMAP_VI, 'K', 0) == WTOP_UI_KILL);
    assert(wtop_ui_key_action(WTOP_KEYMAP_HTOP, 'N', 0) == WTOP_UI_SORT_PID);
    assert(wtop_ui_key_action(WTOP_KEYMAP_VI, 'N', 0) == WTOP_UI_SEARCH_PREVIOUS);
    assert(wtop_ui_key_action(WTOP_KEYMAP_EMACS, 19, WTOP_MOD_CTRL) == WTOP_UI_SEARCH);
    assert(wtop_ui_key_action(WTOP_KEYMAP_EMACS, 7, WTOP_MOD_CTRL) == WTOP_UI_CANCEL);
    assert(wtop_ui_key_action(WTOP_KEYMAP_EMACS, 'p', WTOP_MOD_CTRL) == WTOP_UI_UP);
    assert(wtop_ui_key_action(WTOP_KEYMAP_EMACS, 'v', WTOP_MOD_ALT) == WTOP_UI_PAGE_UP);
    assert(wtop_ui_key_action(WTOP_KEYMAP_EMACS, 'x', WTOP_MOD_ALT) == WTOP_UI_COMMAND);
    assert(wtop_ui_key_action(WTOP_KEYMAP_HTOP, 'k', WTOP_MOD_CTRL) == WTOP_UI_NONE);
    assert(wtop_ui_key_action(WTOP_KEYMAP_VI, 0, 0) == WTOP_UI_NONE);
}

static void action_bar(void)
{
    int widths[] = {0, 1, 9, 10, 40, 79, 80, 81, 120, INT_MAX};
    unsigned w;
    for(w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
        int previous = 0, index;
        for(index = 0; index < 10; ++index) {
            int start, length;
            wtop_ui_bar_span(widths[w], index, &start, &length);
            assert(start == previous && length >= 0);
            assert(start <= widths[w] && length <= widths[w] - start);
            if(length) {
                assert(wtop_ui_bar_hit(widths[w], start) == index);
                assert(wtop_ui_bar_hit(widths[w], start + length - 1) == index);
            }
            previous = start + length;
        }
        assert(previous == widths[w]);
        assert(wtop_ui_bar_hit(widths[w], -1) == -1);
        assert(wtop_ui_bar_hit(widths[w], widths[w]) == -1);
    }
    assert(wtop_ui_function_action(-1) == WTOP_UI_NONE);
    assert(wtop_ui_function_action(10) == WTOP_UI_NONE);
}

static void columns(void)
{
    int ids[WTOP_MAX_COLUMNS] = {0,1,2,3,4,5,6,7,8};
    int widths[] = {0, 1, 12, 13, 40, 80, 120};
    unsigned w;
    for(w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
        wtop_ui_column_span spans[WTOP_MAX_COLUMNS];
        int n = wtop_ui_columns(widths[w], ids, WTOP_MAX_COLUMNS, spans), i, end = 0;
        int command = 0;
        assert(n >= 0 && n <= WTOP_MAX_COLUMNS);
        for(i = 0; i < n; ++i) {
            assert(spans[i].start >= end && spans[i].width > 0);
            assert(spans[i].start <= widths[w] && spans[i].width <= widths[w] - spans[i].start);
            assert(spans[i].id >= 0 && spans[i].id < WTOP_MAX_COLUMNS);
            if(spans[i].id == WTOP_COLUMN_PROCESS) command = 1;
            end = spans[i].start + spans[i].width;
        }
        if(widths[w] >= 13) assert(command);
    }
    {
        int reordered[] = {8, 3, 0};
        wtop_ui_column_span spans[WTOP_MAX_COLUMNS];
        int n = wtop_ui_columns(80, reordered, 3, spans);
        assert(n == 3 && spans[0].id == 8 && spans[1].id == 3 && spans[2].id == 0);
        assert(reordered[0] == 8); /* Responsive layout never rewrites saved order. */
    }
}

static void metric_units(void)
{
    char value[32];
    int process_only = WTOP_COLUMN_PROCESS;
    wtop_ui_column_span span[WTOP_MAX_COLUMNS];
    assert(wtop_ui_columns(INT_MAX, &process_only, 1, span) == 1);
    assert(span[0].start == 0 && span[0].width == INT_MAX && span[0].id == WTOP_COLUMN_PROCESS);
    assert(wtop_ui_format_bytes(1048576.0, 0, value, sizeof(value)) && !strcmp(value, "1.0MB"));
    assert(wtop_ui_format_bytes(1234000000.0, 0, value, sizeof(value)) && !strcmp(value, "1.2GB"));
    assert(wtop_ui_format_bytes(123400000.0, 1, value, sizeof(value)) && !strcmp(value, "123MB/s"));
    assert(strlen(value) <= 8);
    assert(wtop_ui_format_bytes(999900000.0, 1, value, sizeof(value)) && !strcmp(value, "1.0GB/s"));
    assert(wtop_ui_format_bytes(0, 1, value, sizeof(value)) && !strcmp(value, "0B/s"));
    assert(wtop_ui_format_bytes(1.0e300, 1, value, sizeof(value)) && !strcmp(value, ">999EB/s"));
    assert(!wtop_ui_format_bytes(1000000, 0, value, 3) && !value[0]);
    assert(wtop_ui_format_age(45000, value, sizeof(value)) && !strcmp(value, "45s"));
    assert(wtop_ui_format_age(125000, value, sizeof(value)) && !strcmp(value, "2m05s"));
    assert(wtop_ui_format_age(7380000, value, sizeof(value)) && !strcmp(value, "2h03m"));
    assert(wtop_ui_format_age(183600000, value, sizeof(value)) && !strcmp(value, "2d03h"));
    assert(wtop_ui_format_age(999.0 * 86400000, value, sizeof(value)) && !strcmp(value, "999d00h"));
    assert(strlen(value) <= 8);
    assert(wtop_ui_format_age(31557600000.0 * 10000, value, sizeof(value)) && !strcmp(value, ">9999y"));
}


static void contexts(void)
{
    static const wtop_ui_action setup_actions[10] = {
        WTOP_UI_HELP, WTOP_UI_SETUP_NEXT_PAGE, WTOP_UI_TAB_NEW,
        WTOP_UI_TAB_DUPLICATE, WTOP_UI_TAB_RENAME, WTOP_UI_TAB_DELETE,
        WTOP_UI_TAB_MOVE_LEFT, WTOP_UI_TAB_MOVE_RIGHT, WTOP_UI_SAVE, WTOP_UI_QUIT
    };
    wtop_ui_context_options options;
    wtop_ui_binding binding;
    int map, slot, context;
    wtop_ui_context_defaults(&options, WTOP_CONTEXT_PROCESS);
    options.has_selection = options.identity_known = options.kill_supported = 1;
    options.priority_supported = options.save_available = 1;
    options.can_add_tab = options.can_delete_tab = 1;
    options.can_move_left = options.can_move_right = options.setup_tabs = 1;
    for (map=WTOP_KEYMAP_HTOP; map<=WTOP_KEYMAP_EMACS; ++map) {
        for (context=WTOP_CONTEXT_PROCESS; context<=WTOP_CONTEXT_RESULTS; ++context) {
            options.context = (wtop_ui_context)context;
            for (slot=0; slot<10; ++slot) {
                wtop_ui_action key;
                wtop_ui_resolve_function(&options, slot, &binding);
                key = wtop_ui_context_key_action(&options, map, WTOP_KEY_F1+slot, 0);
                assert(key == (binding.enabled ? binding.action : WTOP_UI_NONE));
                if (binding.enabled) assert(binding.label[0] && binding.compact_label[0]);
            }
            assert(wtop_ui_context_key_action(&options, map, WTOP_KEY_F1, 0) == WTOP_UI_HELP);
            assert(wtop_ui_context_key_action(&options, map, WTOP_KEY_F10, 0) == WTOP_UI_QUIT);
            assert(wtop_ui_context_key_action(&options, map, 27, 0) == WTOP_UI_CANCEL);
            assert(wtop_ui_context_key_action(&options, map, 3, WTOP_MOD_CTRL) == WTOP_UI_QUIT);
            assert(wtop_ui_context_key_action(&options, map, 'C', WTOP_MOD_CTRL) == WTOP_UI_QUIT);
            assert(wtop_ui_context_key_action(&options, map, 3, 0) == WTOP_UI_QUIT);
        }
    }
    options.context = WTOP_CONTEXT_SETUP;
    for (slot=0; slot<10; ++slot) {
        wtop_ui_resolve_function(&options, slot, &binding);
        assert(binding.enabled && binding.action == setup_actions[slot]);
    }
    options.setup_tabs = 0;
    wtop_ui_resolve_function(&options, 2, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_WRONG_CONTEXT);
    wtop_ui_resolve_function(&options, 6, &binding);
    assert(binding.enabled && binding.action == WTOP_UI_ADJUST_DOWN);
    options.context = WTOP_CONTEXT_CONFIRM;
    for (slot=0; slot<40; ++slot)
        assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_HTOP, WTOP_KEY_F9, 0) == WTOP_UI_NONE);
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_HTOP, '\r', 0) == WTOP_UI_NONE);
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_HTOP, 'y', 0) == WTOP_UI_CONFIRM);
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_VI, 'Y', WTOP_MOD_SHIFT) == WTOP_UI_CONFIRM);
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_HTOP, 'y', WTOP_MOD_CTRL) == WTOP_UI_NONE);
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_HTOP, 'n', 0) == WTOP_UI_CANCEL);
    wtop_ui_resolve_action(&options, WTOP_UI_SAVE, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_WRONG_CONTEXT);
    options.context = WTOP_CONTEXT_EDITOR;
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_HTOP, 'q', 0) == WTOP_UI_NONE);
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_HTOP, '\r', 0) == WTOP_UI_ACCEPT);
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_EMACS, 7, WTOP_MOD_CTRL) == WTOP_UI_CANCEL);
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_HTOP, 'c', 0) == WTOP_UI_NONE);
    assert(!wtop_ui_interrupt_key('c', 0));
    assert(!wtop_ui_interrupt_key('C', WTOP_MOD_SHIFT));
    assert(!wtop_ui_interrupt_key('x', WTOP_MOD_CTRL));
}

static void availability(void)
{
    wtop_ui_context_options options;
    wtop_ui_binding binding;
    wtop_ui_context_defaults(&options, WTOP_CONTEXT_PROCESS);
    options.readonly = 1;
    wtop_ui_resolve_function(&options, 8, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_READONLY);
    assert(wtop_ui_context_key_action(&options, WTOP_KEYMAP_HTOP, 'k', 0) == WTOP_UI_NONE);
    options.readonly = 0;
    wtop_ui_resolve_function(&options, 8, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_NO_SELECTION);
    options.has_selection = 1;
    wtop_ui_resolve_function(&options, 8, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_NO_IDENTITY);
    options.identity_known = 1;
    wtop_ui_resolve_function(&options, 8, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_UNSUPPORTED);
    options.kill_supported = 1;
    wtop_ui_resolve_function(&options, 8, &binding);
    assert(binding.enabled && !strcmp(binding.label, "Kill"));
    options.context = WTOP_CONTEXT_SETUP;
    options.setup_tabs = 1;
    wtop_ui_resolve_function(&options, 2, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_CAPACITY);
    wtop_ui_resolve_function(&options, 5, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_LAST_VIEW);
    wtop_ui_resolve_function(&options, 6, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_ORDER_BOUNDARY);
    wtop_ui_resolve_function(&options, 8, &binding);
    assert(!binding.enabled && binding.reason == WTOP_UI_NO_SAVE_DESTINATION);
    wtop_ui_resolve_function(&options, -1, &binding);
    assert(!binding.enabled && binding.action == WTOP_UI_NONE);
    wtop_ui_resolve_action(NULL, WTOP_UI_KILL, &binding);
    assert(!binding.enabled);
}

static void live_status(void)
{
    wtop_ui_status status;
    char out[256], small[5];
    unsigned flags;
    size_t n;
    memset(&status, 0, sizeof(status));
    status.sample_available = 1;
    status.age_ms = 2500;
    status.stale = 1;
    status.view = "Main";
    status.filter = "worker";
    status.sort = "CPU%";
    status.descending = 1;
    status.dirty = 1;
    status.selected_rows = 3; status.observed_rows = 400;
    n = wtop_ui_status_format(&status, 120, out, sizeof(out), &flags);
    assert(!strcmp(out, "STALE age 2s view Main filter worker sort CPU% v *unsaved* rows 3/400"));
    assert(n == strlen(out) && !flags);
    status.prompt = "Denied: cannot signal";
    wtop_ui_status_format(&status, 80, out, sizeof(out), &flags);
    assert(!strcmp(out, status.prompt)); /* Prompt displaces lower-priority state. */
    wtop_ui_status_format(&status, 8, out, sizeof(out), &flags);
    assert(!strcmp(out, "Denied:~") && (flags & WTOP_TEXT_CLIPPED));
    status.prompt = "bad\033[31m";
    wtop_ui_status_format(&status, 80, out, sizeof(out), &flags);
    assert(!strcmp(out, "bad?[31m"));
    assert(flags & WTOP_TEXT_CONTROL);
    status.prompt = NULL;
    n = wtop_ui_status_format(&status, 5, out, sizeof(out), &flags);
    assert(n == 5 && !strcmp(out, "STALE"));
    assert(flags & WTOP_TEXT_CLIPPED);
    wtop_ui_status_format(&status, 4, out, sizeof(out), &flags);
    assert(out[0] == '!');
    status.sample_available = 0; status.stale = 0;
    wtop_ui_status_format(&status, 2, out, sizeof(out), &flags);
    assert(!strcmp(out, "?"));
    memset(small, '#', sizeof(small));
    n = wtop_ui_status_format(&status, INT_MAX, small, 4, &flags);
    assert(n < 4 && small[n] == 0 && small[4] == '#');
    assert(wtop_ui_status_format(&status, -1, out, sizeof(out), &flags) == 0 && !out[0]);
    assert(wtop_ui_status_format(&status, 80, NULL, 0, &flags) == 0);
}

static void chrome_layout(void)
{
    wtop_ui_chrome c;
    wtop_ui_context_options context;
    int h, visible, style, position, prompt;
    wtop_ui_chrome_layout(25, 1, WTOP_FOOTER_FUNCTIONS, WTOP_STATUS_BANNER, 0, &c);
    assert(c.footer == 24 && c.status == -1 && c.content_end == 24 && c.banner_status);
    wtop_ui_chrome_layout(25, 1, WTOP_FOOTER_FUNCTIONS, WTOP_STATUS_BOTTOM, 0, &c);
    assert(c.footer == 24 && c.status == 23 && c.content_end == 23 && !c.banner_status);
    wtop_ui_chrome_layout(25, 1, WTOP_FOOTER_STATUS, WTOP_STATUS_BOTTOM, 0, &c);
    assert(c.footer == -1 && c.status == 24 && c.content_end == 24);
    wtop_ui_chrome_layout(25, 0, WTOP_FOOTER_FUNCTIONS, WTOP_STATUS_BANNER, 0, &c);
    assert(c.footer == -1 && c.status == -1 && c.content_end == 25);
    for(h=0; h<100; ++h) for(visible=0; visible<2; ++visible)
    for(style=0; style<3; ++style) for(position=0; position<3; ++position)
    for(prompt=0; prompt<2; ++prompt) {
        wtop_ui_chrome_layout(h,visible,style,position,prompt,&c);
        assert(c.content_end >= 0 && c.content_end <= h);
        assert(c.footer < h && c.status < h && c.prompt < h);
        if(c.footer>=0) assert(c.footer>=c.content_end);
        if(c.status>=0) assert(c.status>=c.content_end);
        if(c.prompt>=0) assert(c.prompt>=c.content_end);
        if(c.footer>=0 && c.status>=0) assert(c.footer!=c.status && position==WTOP_STATUS_BOTTOM);
        if(position!=WTOP_STATUS_BOTTOM) assert(h-c.content_end<=1);
    }
    wtop_ui_context_defaults(&context,WTOP_CONTEXT_PROCESS);
    assert(wtop_ui_context_key_action(&context,WTOP_KEYMAP_HTOP,'Z',0)==WTOP_UI_PAUSE);
    context.context=WTOP_CONTEXT_CONFIRM;
    assert(wtop_ui_context_key_action(&context,WTOP_KEYMAP_HTOP,'Z',0)==WTOP_UI_NONE);
}

int main(void)
{
    keymaps(); action_bar(); columns(); metric_units();
    contexts(); availability(); live_status(); chrome_layout();
    puts("portable UI contracts passed");
    return 0;
}
