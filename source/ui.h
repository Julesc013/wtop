/* Portable input and layout policy. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_UI_H
#define WTOP_UI_H
#include "settings.h"

enum wtop_ui_key {
    WTOP_KEY_UP = 256, WTOP_KEY_DOWN, WTOP_KEY_LEFT, WTOP_KEY_RIGHT,
    WTOP_KEY_PAGE_UP, WTOP_KEY_PAGE_DOWN, WTOP_KEY_HOME, WTOP_KEY_END,
    WTOP_KEY_F1, WTOP_KEY_F2, WTOP_KEY_F3, WTOP_KEY_F4, WTOP_KEY_F5,
    WTOP_KEY_F6, WTOP_KEY_F7, WTOP_KEY_F8, WTOP_KEY_F9, WTOP_KEY_F10
};
enum wtop_ui_modifier { WTOP_MOD_SHIFT = 1, WTOP_MOD_CTRL = 2, WTOP_MOD_ALT = 4 };
typedef enum wtop_ui_action {
    WTOP_UI_NONE, WTOP_UI_UP, WTOP_UI_DOWN, WTOP_UI_PAGE_UP, WTOP_UI_PAGE_DOWN,
    WTOP_UI_TOP, WTOP_UI_BOTTOM, WTOP_UI_HELP, WTOP_UI_SETUP, WTOP_UI_SEARCH,
    WTOP_UI_FILTER, WTOP_UI_TREE, WTOP_UI_SORT, WTOP_UI_PRIORITY_UP,
    WTOP_UI_PRIORITY_DOWN, WTOP_UI_KILL, WTOP_UI_QUIT, WTOP_UI_TAG,
    WTOP_UI_UNTAG, WTOP_UI_REVERSE, WTOP_UI_FOLLOW, WTOP_UI_SEARCH_NEXT,
    WTOP_UI_SEARCH_PREVIOUS, WTOP_UI_NEXT_TAB, WTOP_UI_PREVIOUS_TAB,
    WTOP_UI_BAR, WTOP_UI_COMMAND, WTOP_UI_CANCEL, WTOP_UI_SORT_CPU,
    WTOP_UI_SORT_MEMORY, WTOP_UI_SORT_PID, WTOP_UI_SORT_LEFT, WTOP_UI_SORT_RIGHT,
    WTOP_UI_EMACS_PREFIX, WTOP_UI_SAVE
} wtop_ui_action;

wtop_ui_action wtop_ui_key_action(int keymap, int key, unsigned modifiers);
wtop_ui_action wtop_ui_function_action(int index);
const char *wtop_ui_function_label(int index, int compact);
int wtop_ui_bar_hit(int width, int x);
void wtop_ui_bar_span(int width, int index, int *start, int *length);

typedef struct wtop_ui_column_span { int id, start, width; } wtop_ui_column_span;
/* Omits overflowing optional columns without mutating the saved column list. */
int wtop_ui_columns(int width, const int *columns, int count,
                    wtop_ui_column_span spans[WTOP_MAX_COLUMNS]);
/* Decimal byte units; compact elapsed units. Refuses unit-truncating output. */
int wtop_ui_format_bytes(double bytes, int per_second, char *output, size_t capacity);
int wtop_ui_format_age(double milliseconds, char *output, size_t capacity);
#endif
