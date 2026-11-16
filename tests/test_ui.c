/* Portable UI contracts. SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
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

int main(void)
{
    keymaps(); action_bar(); columns(); metric_units();
    puts("portable UI contracts passed");
    return 0;
}
