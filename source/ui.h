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
    WTOP_UI_EMACS_PREFIX, WTOP_UI_SAVE,
    WTOP_UI_ACCEPT, WTOP_UI_CONFIRM, WTOP_UI_CLEAR,
    WTOP_UI_SETUP_PREVIOUS_PAGE, WTOP_UI_SETUP_NEXT_PAGE,
    WTOP_UI_ADJUST_DOWN, WTOP_UI_ADJUST_UP,
    WTOP_UI_TAB_NEW, WTOP_UI_TAB_DUPLICATE, WTOP_UI_TAB_RENAME,
    WTOP_UI_TAB_DELETE, WTOP_UI_TAB_MOVE_LEFT, WTOP_UI_TAB_MOVE_RIGHT, WTOP_UI_PAUSE,
    WTOP_UI_SELECT, WTOP_UI_COLLAPSE, WTOP_UI_EXPAND, WTOP_UI_DETAILS,
    WTOP_UI_THREADS, WTOP_UI_AFFINITY, WTOP_UI_REFRESH, WTOP_UI_AFFINITY_APPLY
} wtop_ui_action;

typedef enum wtop_ui_context {
    WTOP_CONTEXT_PROCESS, WTOP_CONTEXT_HELP, WTOP_CONTEXT_SETUP,
    WTOP_CONTEXT_EDITOR, WTOP_CONTEXT_SORT, WTOP_CONTEXT_CONFIRM,
    WTOP_CONTEXT_RESULTS, WTOP_CONTEXT_DETAILS, WTOP_CONTEXT_THREADS, WTOP_CONTEXT_AFFINITY
} wtop_ui_context;
typedef enum wtop_ui_reason {
    WTOP_UI_AVAILABLE, WTOP_UI_WRONG_CONTEXT, WTOP_UI_READONLY,
    WTOP_UI_NO_SELECTION, WTOP_UI_NO_IDENTITY, WTOP_UI_UNSUPPORTED,
    WTOP_UI_NO_SAVE_DESTINATION, WTOP_UI_CAPACITY, WTOP_UI_LAST_VIEW,
    WTOP_UI_ORDER_BOUNDARY, WTOP_UI_NO_FOCUS
} wtop_ui_reason;
typedef struct wtop_ui_context_options {
    wtop_ui_context context;
    int readonly;
    int has_selection, identity_known;
    int has_focus, focus_identity_known, affinity_supported;
    int kill_supported, priority_supported, save_available;
    int setup_tabs, can_add_tab, can_delete_tab;
    int can_move_left, can_move_right;
} wtop_ui_context_options;
typedef struct wtop_ui_binding {
    wtop_ui_action action;
    const char *label;
    const char *compact_label;
    int enabled;
    wtop_ui_reason reason;
} wtop_ui_binding;

/* Initialize a conservative context with no assumed target, native capability,
   or writable destination. Caller supplies observed capabilities, not guessed
   per-process access rights. Bindings borrow static labels. */
void wtop_ui_context_defaults(wtop_ui_context_options *options,
                              wtop_ui_context context);
void wtop_ui_resolve_action(const wtop_ui_context_options *options,
                            wtop_ui_action action, wtop_ui_binding *binding);
void wtop_ui_resolve_function(const wtop_ui_context_options *options,
                              int index, wtop_ui_binding *binding);
wtop_ui_action wtop_ui_context_key_action(const wtop_ui_context_options *options,
                                          int keymap, int key,
                                          unsigned modifiers);
const char *wtop_ui_reason_name(wtop_ui_reason reason);

/* Borrowed UTF-8 strings are read only through the first 256 bytes. This status
   projection uses the conservative ASCII text profile, reports losses through
   flags, and never refreshes/infers sampling timestamps. Age comes from model.
   Prompt > freshness/provider status > view/filter/sort > dirty > counts. */
typedef struct wtop_ui_status {
    const char *prompt;
    const char *provider_notice;
    const char *view;
    const char *filter;
    const char *sort;
    int sample_available, stale, partial, dirty, descending;
    unsigned long age_ms, observed_rows, selected_rows;
} wtop_ui_status;
size_t wtop_ui_status_format(const wtop_ui_status *status, int width,
                              char *output, size_t capacity, unsigned *flags);

wtop_ui_action wtop_ui_key_action(int keymap, int key, unsigned modifiers);
/* ETX or Ctrl-C is a global shutdown request, including text editors. Hosts
   must check this before routing a key to any mode-specific editor. */
int wtop_ui_interrupt_key(int key, unsigned modifiers);
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
typedef struct wtop_ui_chrome {
    int footer, status, prompt, content_end, banner_status;
} wtop_ui_chrome;
/* Half-open content range [0, content_end). A second footer row exists only
   when bottom status is explicitly requested. Transient prompts replace a
   footer, never an unreserved process row. */
void wtop_ui_chrome_layout(int height, int visible, int footer_style,
                          int status_position, int prompting, wtop_ui_chrome *layout);
#endif
