/* Portable input and layout policy. SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

static void action_labels(wtop_ui_binding *binding);

wtop_ui_action wtop_ui_function_action(int index)
{
    static const wtop_ui_action actions[10] = {
        WTOP_UI_HELP, WTOP_UI_SETUP, WTOP_UI_SEARCH, WTOP_UI_FILTER,
        WTOP_UI_TREE, WTOP_UI_SORT, WTOP_UI_PRIORITY_UP,
        WTOP_UI_PRIORITY_DOWN, WTOP_UI_KILL, WTOP_UI_QUIT
    };
    return index >= 0 && index < 10 ? actions[index] : WTOP_UI_NONE;
}

const char *wtop_ui_function_label(int index, int compact)
{
    wtop_ui_binding binding;
    binding.action = wtop_ui_function_action(index);
    action_labels(&binding);
    return compact ? binding.compact_label : binding.label;
}

void wtop_ui_bar_span(int width, int index, int *start, int *length)
{
    int end;
    *start = *length = 0;
    if(width <= 0 || index < 0 || index >= 10) return;
    *start = (width / 10) * index + ((width % 10) * index) / 10;
    end = (width / 10) * (index + 1) + ((width % 10) * (index + 1)) / 10;
    *length = end - *start;
}

int wtop_ui_bar_hit(int width, int x)
{
    int index, start, length;
    if(x < 0 || x >= width) return -1;
    for(index = 0; index < 10; ++index) {
        wtop_ui_bar_span(width, index, &start, &length);
        if(x >= start && x - start < length) return index;
    }
    return -1;
}

wtop_ui_action wtop_ui_key_action(int keymap, int key, unsigned modifiers)
{
    int ctrl = (modifiers & WTOP_MOD_CTRL) != 0;
    int alt = (modifiers & WTOP_MOD_ALT) != 0;
    if(key >= WTOP_KEY_F1 && key <= WTOP_KEY_F10)
        return wtop_ui_function_action(key - WTOP_KEY_F1);
    if(key == 27) return WTOP_UI_CANCEL;
    if(key == '\t') return modifiers & WTOP_MOD_SHIFT ? WTOP_UI_PREVIOUS_TAB : WTOP_UI_NEXT_TAB;
    if(key == WTOP_KEY_UP) return WTOP_UI_UP;
    if(key == WTOP_KEY_DOWN) return WTOP_UI_DOWN;
    if(key == WTOP_KEY_PAGE_UP) return WTOP_UI_PAGE_UP;
    if(key == WTOP_KEY_PAGE_DOWN) return WTOP_UI_PAGE_DOWN;
    if(key == WTOP_KEY_HOME) return WTOP_UI_TOP;
    if(key == WTOP_KEY_END) return WTOP_UI_BOTTOM;
    if(key == WTOP_KEY_LEFT && ctrl) return WTOP_UI_SORT_LEFT;
    if(key == WTOP_KEY_RIGHT && ctrl) return WTOP_UI_SORT_RIGHT;
    if(keymap == WTOP_KEYMAP_EMACS) {
        if(ctrl) {
            switch(key) {
            case 'n': case 'N': case 14: return WTOP_UI_DOWN;
            case 'p': case 'P': case 16: return WTOP_UI_UP;
            case 'v': case 'V': case 22: return WTOP_UI_PAGE_DOWN;
            case 's': case 'S': case 19: return WTOP_UI_SEARCH;
            case 'r': case 'R': case 18: return WTOP_UI_SEARCH_PREVIOUS;
            case 'g': case 'G': case 7: return WTOP_UI_CANCEL;
            case 'x': case 'X': case 24: return WTOP_UI_EMACS_PREFIX;
            default: return WTOP_UI_NONE;
            }
        }
        if(alt) {
            if(key == 'v' || key == 'V') return WTOP_UI_PAGE_UP;
            if(key == '<') return WTOP_UI_TOP;
            if(key == '>') return WTOP_UI_BOTTOM;
            if(key == 'x' || key == 'X') return WTOP_UI_COMMAND;
            return WTOP_UI_NONE;
        }
    } else if(ctrl || alt) return WTOP_UI_NONE;
    if(key == ' ') return WTOP_UI_TAG;
    if(key == 'U') return WTOP_UI_UNTAG;
    if(key == 'I') return WTOP_UI_REVERSE;
    if(key == 'F') return WTOP_UI_FOLLOW;
    if(key == 'P') return WTOP_UI_SORT_CPU;
    if(key == 'M') return WTOP_UI_SORT_MEMORY;
    if(key == '/' ) return WTOP_UI_SEARCH;
    if(key == '\\') return WTOP_UI_FILTER;
    if(key == ':' ) return WTOP_UI_COMMAND;
    if(key == 'q') return WTOP_UI_QUIT;
    if(key == '?' || key == 'h') return WTOP_UI_HELP;
    if(key == 'B') return WTOP_UI_BAR;
    if(key == 't') return WTOP_UI_TREE;
    if(keymap == WTOP_KEYMAP_VI) {
        if(key == 'j') return WTOP_UI_DOWN;
        if(key == 'k') return WTOP_UI_UP;
        if(key == 'g') return WTOP_UI_TOP;
        if(key == 'G') return WTOP_UI_BOTTOM;
        if(key == 'n') return WTOP_UI_SEARCH_NEXT;
        if(key == 'N') return WTOP_UI_SEARCH_PREVIOUS;
        if(key == 'K') return WTOP_UI_KILL;
    } else {
        if(key == 'k') return WTOP_UI_KILL;
        if(key == 'S' || key == 'C') return WTOP_UI_SETUP;
        if(key == 'N') return WTOP_UI_SORT_PID;
    }
    return WTOP_UI_NONE;
}

int wtop_ui_columns(int width, const int *columns, int count,
                    wtop_ui_column_span spans[WTOP_MAX_COLUMNS])
{
    static const int preferred[WTOP_MAX_COLUMNS] = { 10, 8, 3, 5, 9, 4, 8, 8, 12 };
    int i, n = 0, x = 0, has_command = 0;
    if(width <= 0 || !columns || count < 0 || count > WTOP_MAX_COLUMNS) return 0;
    for(i = 0; i < count; ++i) if(columns[i] == 8) has_command = 1;
    for(i = 0; i < count; ++i) {
        int id = columns[i], amount, reserve;
        if(id < 0 || id >= WTOP_MAX_COLUMNS) continue;
        reserve = has_command && id != 8 ? 13 : 0;
        amount = id == 8 && i == count - 1 ? width - x : preferred[id];
        if(amount <= 0 || amount > width - x - reserve) continue;
        spans[n].id = id; spans[n].start = x; spans[n].width = amount;
        ++n;
        if(amount == width - x) break; /* A final full-width field needs no separator. */
        x += amount + 1;
        if(id == 8) has_command = 0;
    }
    return n;
}

/* A truncated number without its unit is misleading. Format bounded strings
   first, then refuse insufficient output space rather than clipping the unit. */
static int copy_metric(char *output, size_t capacity, const char *text)
{
    size_t length = strlen(text);
    if(!output || !capacity) return 0;
    if(length >= capacity) { output[0] = 0; return 0; }
    memcpy(output, text, length + 1);
    return 1;
}

int wtop_ui_format_bytes(double bytes, int per_second, char *output, size_t capacity)
{
    static const char *units[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB" };
    char text[32];
    int unit = 0;
    if(!(bytes >= 0)) return copy_metric(output, capacity, "?");
    while(bytes >= 1000 && unit < 6) { bytes /= 1000; ++unit; }
    if(bytes >= 999.5 && unit < 6) { bytes /= 1000; ++unit; }
    if(bytes >= 999.5) return copy_metric(output, capacity, per_second ? ">999EB/s" : ">999EB");
    if(!unit || (per_second && bytes >= 99.95))
        sprintf(text, "%.0f%s%s", bytes, units[unit], per_second ? "/s" : "");
    else sprintf(text, "%.1f%s%s", bytes, units[unit], per_second ? "/s" : "");
    return copy_metric(output, capacity, text);
}

int wtop_ui_format_age(double milliseconds, char *output, size_t capacity)
{
    char text[32];
    unsigned long first, second;
    if(!(milliseconds >= 0)) return copy_metric(output, capacity, "?");
    if(milliseconds < 60000) sprintf(text, "%lus", (unsigned long)(milliseconds / 1000));
    else if(milliseconds < 3600000) {
        first = (unsigned long)(milliseconds / 60000);
        second = (unsigned long)(milliseconds / 1000) % 60;
        sprintf(text, "%lum%02lus", first, second);
    } else if(milliseconds < 86400000) {
        first = (unsigned long)(milliseconds / 3600000);
        second = (unsigned long)(milliseconds / 60000) % 60;
        sprintf(text, "%luh%02lum", first, second);
    } else if(milliseconds < 86400000000.0) {
        first = (unsigned long)(milliseconds / 86400000);
        second = (unsigned long)(milliseconds / 3600000) % 24;
        sprintf(text, "%lud%02luh", first, second);
    } else if(milliseconds < 315576000000000.0) {
        sprintf(text, "%.1fy", milliseconds / 31557600000.0);
    } else return copy_metric(output, capacity, ">9999y");
    return copy_metric(output, capacity, text);
}


void wtop_ui_context_defaults(wtop_ui_context_options *options,
                              wtop_ui_context context)
{
    if (!options) return;
    memset(options, 0, sizeof(*options));
    options->context = context;
}

const char *wtop_ui_reason_name(wtop_ui_reason reason)
{
    static const char *names[] = {
        "available", "not available in this context", "read-only mode",
        "no selected process", "process identity is unavailable",
        "operation is unsupported", "no configuration save destination",
        "view capacity reached", "at least one view must remain",
        "already at the ordering boundary"
    };
    if (reason < WTOP_UI_AVAILABLE || reason > WTOP_UI_ORDER_BOUNDARY)
        return "invalid availability state";
    return names[reason];
}

static void action_labels(wtop_ui_binding *binding)
{
    const char *label = "", *compact = "";
    switch (binding->action) {
    case WTOP_UI_UP: label = "Up"; compact = "^"; break;
    case WTOP_UI_DOWN: label = "Down"; compact = "v"; break;
    case WTOP_UI_PAGE_UP: label = "Page up"; compact = "Pg-"; break;
    case WTOP_UI_PAGE_DOWN: label = "Page down"; compact = "Pg+"; break;
    case WTOP_UI_TOP: label = "Top"; compact = "Top"; break;
    case WTOP_UI_BOTTOM: label = "Bottom"; compact = "End"; break;
    case WTOP_UI_HELP: label = "Help"; compact = "?"; break;
    case WTOP_UI_SETUP: label = "Setup"; compact = "S"; break;
    case WTOP_UI_SEARCH: label = "Search"; compact = "/"; break;
    case WTOP_UI_FILTER: label = "Filter"; compact = "\\"; break;
    case WTOP_UI_TREE: label = "Tree"; compact = "T"; break;
    case WTOP_UI_SORT: label = "Sort"; compact = "O"; break;
    case WTOP_UI_PRIORITY_UP: label = "Prio+"; compact = "+"; break;
    case WTOP_UI_PRIORITY_DOWN: label = "Prio-"; compact = "-"; break;
    case WTOP_UI_KILL: label = "Kill"; compact = "K"; break;
    case WTOP_UI_QUIT: label = "Quit"; compact = "Q"; break;
    case WTOP_UI_TAG: label = "Tag"; compact = "+"; break;
    case WTOP_UI_UNTAG: label = "Untag"; compact = "U"; break;
    case WTOP_UI_REVERSE: label = "Reverse"; compact = "I"; break;
    case WTOP_UI_FOLLOW: label = "Follow"; compact = "F"; break;
    case WTOP_UI_SEARCH_NEXT: label = "Next match"; compact = "n"; break;
    case WTOP_UI_SEARCH_PREVIOUS: label = "Previous match"; compact = "N"; break;
    case WTOP_UI_NEXT_TAB: label = "Next view"; compact = ">"; break;
    case WTOP_UI_PREVIOUS_TAB: label = "Previous view"; compact = "<"; break;
    case WTOP_UI_BAR: label = "Controls"; compact = "B"; break;
    case WTOP_UI_COMMAND: label = "Command"; compact = ":"; break;
    case WTOP_UI_CANCEL: label = "Back"; compact = "Esc"; break;
    case WTOP_UI_SORT_CPU: label = "Sort CPU"; compact = "P"; break;
    case WTOP_UI_SORT_MEMORY: label = "Sort memory"; compact = "M"; break;
    case WTOP_UI_SORT_PID: label = "Sort PID"; compact = "N"; break;
    case WTOP_UI_SORT_LEFT: label = "Previous sort"; compact = "<"; break;
    case WTOP_UI_SORT_RIGHT: label = "Next sort"; compact = ">"; break;
    case WTOP_UI_EMACS_PREFIX: label = "Prefix"; compact = "C-x"; break;
    case WTOP_UI_SAVE: label = "Save"; compact = "S"; break;
    case WTOP_UI_ACCEPT: label = "Apply"; compact = "OK"; break;
    case WTOP_UI_CONFIRM: label = "Confirm"; compact = "y"; break;
    case WTOP_UI_CLEAR: label = "Clear"; compact = "X"; break;
    case WTOP_UI_SETUP_PREVIOUS_PAGE: label = "Prev page"; compact = "<"; break;
    case WTOP_UI_SETUP_NEXT_PAGE: label = "Next page"; compact = ">"; break;
    case WTOP_UI_ADJUST_DOWN: label = "Less"; compact = "-"; break;
    case WTOP_UI_ADJUST_UP: label = "More"; compact = "+"; break;
    case WTOP_UI_TAB_NEW: label = "New"; compact = "N"; break;
    case WTOP_UI_TAB_DUPLICATE: label = "Copy"; compact = "C"; break;
    case WTOP_UI_TAB_RENAME: label = "Rename"; compact = "R"; break;
    case WTOP_UI_TAB_DELETE: label = "Delete"; compact = "D"; break;
    case WTOP_UI_TAB_MOVE_LEFT: label = "Move-"; compact = "<"; break;
    case WTOP_UI_TAB_MOVE_RIGHT: label = "Move+"; compact = ">"; break;
    default: binding->action = WTOP_UI_NONE; break;
    }
    binding->label = label;
    binding->compact_label = compact;
}

static int process_only_action(wtop_ui_action action)
{
    switch (action) {
    case WTOP_UI_SEARCH: case WTOP_UI_FILTER: case WTOP_UI_TREE:
    case WTOP_UI_SORT: case WTOP_UI_PRIORITY_UP: case WTOP_UI_PRIORITY_DOWN:
    case WTOP_UI_KILL: case WTOP_UI_TAG: case WTOP_UI_UNTAG:
    case WTOP_UI_REVERSE: case WTOP_UI_FOLLOW: case WTOP_UI_SEARCH_NEXT:
    case WTOP_UI_SEARCH_PREVIOUS: case WTOP_UI_NEXT_TAB:
    case WTOP_UI_PREVIOUS_TAB: case WTOP_UI_COMMAND: case WTOP_UI_SORT_CPU:
    case WTOP_UI_SORT_MEMORY: case WTOP_UI_SORT_PID: case WTOP_UI_SORT_LEFT:
    case WTOP_UI_SORT_RIGHT:
        return 1;
    default: return 0;
    }
}

void wtop_ui_resolve_action(const wtop_ui_context_options *options,
                            wtop_ui_action action, wtop_ui_binding *binding)
{
    wtop_ui_reason reason = WTOP_UI_AVAILABLE;
    wtop_ui_context context;
    int target_action;
    if (!binding) return;
    memset(binding, 0, sizeof(*binding));
    binding->action = action;
    action_labels(binding);
    binding->reason = WTOP_UI_WRONG_CONTEXT;
    if (!options || binding->action == WTOP_UI_NONE) return;
    context = options->context;
    if (context < WTOP_CONTEXT_PROCESS || context > WTOP_CONTEXT_RESULTS) return;
    if (context == WTOP_CONTEXT_CONFIRM && action != WTOP_UI_CONFIRM &&
        action != WTOP_UI_HELP && action != WTOP_UI_CANCEL && action != WTOP_UI_QUIT)
        reason = WTOP_UI_WRONG_CONTEXT;
    else if (context == WTOP_CONTEXT_EDITOR && action != WTOP_UI_ACCEPT &&
        action != WTOP_UI_CLEAR && action != WTOP_UI_HELP &&
        action != WTOP_UI_CANCEL && action != WTOP_UI_QUIT &&
        action != WTOP_UI_SAVE && action != WTOP_UI_EMACS_PREFIX)
        reason = WTOP_UI_WRONG_CONTEXT;
    else if (process_only_action(action) && context != WTOP_CONTEXT_PROCESS)
        reason = WTOP_UI_WRONG_CONTEXT;
    else if (action == WTOP_UI_CONFIRM && context != WTOP_CONTEXT_CONFIRM)
        reason = WTOP_UI_WRONG_CONTEXT;
    else if (action == WTOP_UI_CLEAR && context != WTOP_CONTEXT_EDITOR)
        reason = WTOP_UI_WRONG_CONTEXT;
    else if (action == WTOP_UI_ACCEPT && context != WTOP_CONTEXT_EDITOR &&
             context != WTOP_CONTEXT_SORT && context != WTOP_CONTEXT_SETUP)
        reason = WTOP_UI_WRONG_CONTEXT;
    else if ((action == WTOP_UI_SETUP_PREVIOUS_PAGE ||
              action == WTOP_UI_SETUP_NEXT_PAGE || action == WTOP_UI_ADJUST_UP ||
              action == WTOP_UI_ADJUST_DOWN) && context != WTOP_CONTEXT_SETUP)
        reason = WTOP_UI_WRONG_CONTEXT;
    else if (action >= WTOP_UI_TAB_NEW && action <= WTOP_UI_TAB_MOVE_RIGHT) {
        if (context != WTOP_CONTEXT_SETUP || !options->setup_tabs)
            reason = WTOP_UI_WRONG_CONTEXT;
        else if ((action == WTOP_UI_TAB_NEW || action == WTOP_UI_TAB_DUPLICATE) &&
                 !options->can_add_tab) reason = WTOP_UI_CAPACITY;
        else if (action == WTOP_UI_TAB_DELETE && !options->can_delete_tab)
            reason = WTOP_UI_LAST_VIEW;
        else if ((action == WTOP_UI_TAB_MOVE_LEFT && !options->can_move_left) ||
                 (action == WTOP_UI_TAB_MOVE_RIGHT && !options->can_move_right))
            reason = WTOP_UI_ORDER_BOUNDARY;
    }
    target_action = action == WTOP_UI_KILL || action == WTOP_UI_CONFIRM ||
                    action == WTOP_UI_PRIORITY_UP || action == WTOP_UI_PRIORITY_DOWN;
    if (reason == WTOP_UI_AVAILABLE && target_action) {
        if (options->readonly) reason = WTOP_UI_READONLY;
        else if (!options->has_selection) reason = WTOP_UI_NO_SELECTION;
        else if (!options->identity_known) reason = WTOP_UI_NO_IDENTITY;
        else if ((action == WTOP_UI_KILL || action == WTOP_UI_CONFIRM) &&
                 !options->kill_supported) reason = WTOP_UI_UNSUPPORTED;
        else if ((action == WTOP_UI_PRIORITY_UP || action == WTOP_UI_PRIORITY_DOWN) &&
                 !options->priority_supported) reason = WTOP_UI_UNSUPPORTED;
    }
    if (reason == WTOP_UI_AVAILABLE &&
        (action == WTOP_UI_TAG || action == WTOP_UI_FOLLOW)) {
        if (!options->has_selection) reason = WTOP_UI_NO_SELECTION;
        else if (!options->identity_known) reason = WTOP_UI_NO_IDENTITY;
    }
    if (reason == WTOP_UI_AVAILABLE && action == WTOP_UI_SAVE) {
        if(options->readonly) reason = WTOP_UI_READONLY;
        else if(!options->save_available) reason = WTOP_UI_NO_SAVE_DESTINATION;
    }
    binding->reason = reason;
    binding->enabled = reason == WTOP_UI_AVAILABLE;
}

void wtop_ui_resolve_function(const wtop_ui_context_options *options,
                              int index, wtop_ui_binding *binding)
{
    wtop_ui_action action = WTOP_UI_NONE;
    if (!binding) return;
    if (options && index >= 0 && index < 10) {
        if (index == 0) action = WTOP_UI_HELP;
        else if (index == 9) action = WTOP_UI_QUIT;
        else switch (options->context) {
        case WTOP_CONTEXT_PROCESS:
            action = wtop_ui_function_action(index); break;
        case WTOP_CONTEXT_HELP: case WTOP_CONTEXT_RESULTS:
            if (index == 1) action = WTOP_UI_SETUP;
            else if (index == 2) action = WTOP_UI_PAGE_UP;
            else if (index == 3) action = WTOP_UI_PAGE_DOWN;
            else if (index == 8) action = WTOP_UI_CANCEL;
            break;
        case WTOP_CONTEXT_SETUP:
            if (index == 1) action = WTOP_UI_SETUP_NEXT_PAGE;
            else if (index >= 2 && index <= 5) {
                static const wtop_ui_action edits[] = {
                    WTOP_UI_TAB_NEW, WTOP_UI_TAB_DUPLICATE,
                    WTOP_UI_TAB_RENAME, WTOP_UI_TAB_DELETE
                };
                action = edits[index - 2];
            } else if (index == 6) action = options->setup_tabs ?
                WTOP_UI_TAB_MOVE_LEFT : WTOP_UI_ADJUST_DOWN;
            else if (index == 7) action = options->setup_tabs ?
                WTOP_UI_TAB_MOVE_RIGHT : WTOP_UI_ADJUST_UP;
            else if (index == 8) action = WTOP_UI_SAVE;
            break;
        case WTOP_CONTEXT_EDITOR:
            if (index == 1) action = WTOP_UI_CANCEL;
            else if (index == 3) action = WTOP_UI_CLEAR;
            else if (index == 8) action = WTOP_UI_ACCEPT;
            break;
        case WTOP_CONTEXT_SORT:
            if (index == 1) action = WTOP_UI_CANCEL;
            else if (index == 6) action = WTOP_UI_UP;
            else if (index == 7) action = WTOP_UI_DOWN;
            else if (index == 8) action = WTOP_UI_ACCEPT;
            break;
        case WTOP_CONTEXT_CONFIRM:
            if (index == 1) action = WTOP_UI_CANCEL;
            /* F9 deliberately has no action: repeating the initiating F9 must
               never confirm a captured destructive request. Only explicit y
               (or a separately labelled affirmative button) can confirm. */
            break;
        default: break;
        }
    }
    wtop_ui_resolve_action(options, action, binding);
}

static int navigation_action(wtop_ui_action action)
{
    return action == WTOP_UI_UP || action == WTOP_UI_DOWN ||
        action == WTOP_UI_PAGE_UP || action == WTOP_UI_PAGE_DOWN ||
        action == WTOP_UI_TOP || action == WTOP_UI_BOTTOM;
}

wtop_ui_action wtop_ui_context_key_action(const wtop_ui_context_options *options,
                                          int keymap, int key,
                                          unsigned modifiers)
{
    wtop_ui_action action = WTOP_UI_NONE;
    wtop_ui_binding binding;
    int command_modifier = (modifiers & (WTOP_MOD_CTRL | WTOP_MOD_ALT)) != 0;
    if (!options) return WTOP_UI_NONE;
    if (key >= WTOP_KEY_F1 && key <= WTOP_KEY_F10) {
        wtop_ui_resolve_function(options, key - WTOP_KEY_F1, &binding);
        return binding.enabled ? binding.action : WTOP_UI_NONE;
    }
    if (key == 27 || (keymap == WTOP_KEYMAP_EMACS &&
        (modifiers & WTOP_MOD_CTRL) && (key == 'g' || key == 'G' || key == 7)))
        action = WTOP_UI_CANCEL;
    else if ((modifiers & WTOP_MOD_CTRL) && (key == 'c' || key == 'C' || key == 3))
        action = WTOP_UI_QUIT;
    else if (options->context == WTOP_CONTEXT_CONFIRM) {
        if (!command_modifier && (key == 'y' || key == 'Y')) action = WTOP_UI_CONFIRM;
        else if (!command_modifier && (key == 'n' || key == 'N')) action = WTOP_UI_CANCEL;
    } else if (options->context == WTOP_CONTEXT_EDITOR) {
        if (!command_modifier && (key == '\r' || key == '\n')) action = WTOP_UI_ACCEPT;
        /* Printable keys, including q, remain editor text. */
    } else if (options->context == WTOP_CONTEXT_RESULTS && !command_modifier &&
               (key == '\r' || key == '\n')) action = WTOP_UI_CANCEL;
    else if (!command_modifier && key == 'q') action = WTOP_UI_QUIT;
    else if (options->context == WTOP_CONTEXT_PROCESS)
        action = wtop_ui_key_action(keymap, key, modifiers);
    else if (options->context == WTOP_CONTEXT_SETUP) {
        if (!command_modifier && key == WTOP_KEY_LEFT) action = WTOP_UI_SETUP_PREVIOUS_PAGE;
        else if (!command_modifier && key == WTOP_KEY_RIGHT) action = WTOP_UI_SETUP_NEXT_PAGE;
        else if (!command_modifier && (key == 's' || key == 'S')) action = WTOP_UI_SAVE;
        else if (!command_modifier && (key == '+' || key == '=')) action = options->setup_tabs ? WTOP_UI_TAB_MOVE_RIGHT : WTOP_UI_ADJUST_UP;
        else if (!command_modifier && key == '-') action = options->setup_tabs ? WTOP_UI_TAB_MOVE_LEFT : WTOP_UI_ADJUST_DOWN;
        else if (!command_modifier && (key == ' ' || key == '\r')) action = WTOP_UI_ACCEPT;
        else {
            action = wtop_ui_key_action(keymap, key, modifiers);
            if (!navigation_action(action)) action = WTOP_UI_NONE;
        }
    } else {
        if (options->context == WTOP_CONTEXT_SORT && !command_modifier &&
            (key == '\r' || key == ' ')) action = WTOP_UI_ACCEPT;
        else {
            action = wtop_ui_key_action(keymap, key, modifiers);
            if (!navigation_action(action) && action != WTOP_UI_HELP)
                action = WTOP_UI_NONE;
        }
    }
    wtop_ui_resolve_action(options, action, &binding);
    return binding.enabled ? binding.action : WTOP_UI_NONE;
}

static size_t bounded_status_length(const char *text, unsigned *flags)
{
    size_t length = 0;
    if (!text) return 0;
    while (length < 256 && text[length]) ++length;
    if (length == 256) *flags |= WTOP_TEXT_CLIPPED;
    return length;
}

/* Whole components are admitted in priority order. Skipping one does not stop
   later short components (notably the dirty marker) from fitting. Numeric units
   are never clipped to make a component fit. */
static void status_component(char *output, size_t *used, size_t limit,
                              const char *text, unsigned *flags)
{
    char clean[257];
    size_t length, separator = *used ? 1 : 0;
    wtop_text_result rendered;
    length = bounded_status_length(text, flags);
    if (!length) return;
    rendered = wtop_text_ascii(text, length, 256, clean, sizeof(clean));
    *flags |= rendered.flags;
    if (separator > limit - *used || rendered.cells > limit - *used - separator) {
        *flags |= WTOP_TEXT_CLIPPED;
        return;
    }
    if (separator) output[(*used)++] = ' ';
    memcpy(output + *used, clean, rendered.output_bytes);
    *used += rendered.output_bytes;
    output[*used] = 0;
}

static void status_named_component(char *output, size_t *used, size_t limit,
                                    const char *name, const char *value,
                                    const char *suffix, unsigned *flags)
{
    char clean[257], component[288];
    wtop_text_result rendered;
    size_t length = bounded_status_length(value, flags);
    if (!length) return;
    rendered = wtop_text_ascii(value, length, 256, clean, sizeof(clean));
    *flags |= rendered.flags;
    /* name/suffix are private short literals; value is bounded above. */
    sprintf(component, "%s%s%s", name, clean, suffix);
    status_component(output, used, limit, component, flags);
}

size_t wtop_ui_status_format(const wtop_ui_status *status, int width,
                              char *output, size_t capacity, unsigned *flags)
{
    size_t limit, used = 0, length;
    unsigned result_flags = 0;
    char age[32], component[80];
    wtop_text_result rendered;
    if (flags) *flags = 0;
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (!status || width <= 0) return 0;
    limit = (size_t)width;
    if (limit >= capacity) limit = capacity - 1;
    length = bounded_status_length(status->prompt, &result_flags);
    if (length) {
        rendered = wtop_text_ascii(status->prompt, length, limit, output, capacity);
        result_flags |= rendered.flags;
        used = rendered.output_bytes;
        if ((result_flags & WTOP_TEXT_CLIPPED) && used) output[used - 1] = '~';
    } else {
        if (!status->sample_available) status_component(output, &used, limit, "WARMING", &result_flags);
        else if (status->stale) status_component(output, &used, limit, "STALE", &result_flags);
        if (status->partial) status_component(output, &used, limit, "PARTIAL", &result_flags);
        /* Even an extremely narrow frame retains a visible availability signal. */
        if (!used && limit && (!status->sample_available || status->stale || status->partial)) {
            output[used++] = status->sample_available ? '!' : '?'; output[used] = 0;
        }
        status_component(output, &used, limit, status->provider_notice, &result_flags);
        if (status->sample_available && wtop_ui_format_age((double)status->age_ms, age, sizeof(age))) {
            sprintf(component, "age %s", age);
            status_component(output, &used, limit, component, &result_flags);
        }
        status_named_component(output, &used, limit, "view ", status->view, "", &result_flags);
        status_named_component(output, &used, limit, "filter ", status->filter, "", &result_flags);
        status_named_component(output, &used, limit, "sort ", status->sort,
                                 status->descending ? " v" : " ^", &result_flags);
        if (status->dirty) status_component(output, &used, limit, "*unsaved*", &result_flags);
        sprintf(component, "rows %lu/%lu", status->selected_rows, status->observed_rows);
        status_component(output, &used, limit, component, &result_flags);
    }
    if (flags) *flags = result_flags;
    return used;
}
