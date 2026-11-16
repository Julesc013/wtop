/* Portable input and layout policy. SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"
#include <stdio.h>
#include <string.h>

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
    static const char *long_labels[10] = {
        "Help", "Setup", "Search", "Filter", "Tree", "Sort",
        "Prio+", "Prio-", "Kill", "Quit"
    };
    static const char *short_labels[10] = { "?", "S", "/", "\\", "T", "O", "+", "-", "K", "Q" };
    if(index < 0 || index >= 10) return "";
    return compact ? short_labels[index] : long_labels[index];
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
