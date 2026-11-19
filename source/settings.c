/* Wtop bounded presentation settings. SPDX-License-Identifier: GPL-3.0-only */
#include "settings.h"
#include <stdlib.h>
#include <string.h>

static const char *const color_names[WTOP_COLOR_COUNT] = {
    "FGColor", "BGColor", "FGHighlightColor", "BGHighlightColor",
    "MenuBarColor", "ProcessListHeaderColor", "CPUBarColor", "MemoryBarColor",
    "PageMemoryBarColor", "ErrorColor", "TaggedColor", "FollowColor",
    "MeterLabelColor", "FunctionKeyColor", "FocusForeground", "FocusBackground",
    "SelectionForeground", "SelectionBackground"
};
static const char *const column_names[WTOP_MAX_COLUMNS] = {
    "ID", "USER", "PRI", "CPU%", "MEM", "THRD", "I/O", "AGE", "PROCESS"
};
static const char *const row_names[WTOP_HEADER_ROWS] = {
    "title", "cpu", "memory", "commit", "summary"
};
static const char *const base_names[] = {
    "ConfigVersion", "Theme", "Keymap", "Mouse", "FunctionBar", "ShowTabs",
    "HeaderRows", "RedrawInterval", "TabCount", "ActiveTab", "NextTabId",
    "FooterStyle", "StatusPosition", "FrameInterval", "MeterWidth", "MeterStyle", "CharacterSet", "BackgroundMode"
};
static const char *const tab_names[] = {
    "Name", "Filter", "Visible", "Sort", "Descending", "Tree", "Columns", "Id"
};
#define BASE_KEYS 18
#define TAB_FIELDS 8
#define TAB_KEY_BASE (BASE_KEYS + WTOP_COLOR_COUNT)
#define KEY_COUNT (TAB_KEY_BASE + WTOP_MAX_TABS * TAB_FIELDS)

static int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}
static int equal(const char *left, const char *right)
{
    while (*left && *right) {
        if (ascii_lower((unsigned char)*left++) != ascii_lower((unsigned char)*right++)) return 0;
    }
    return *left == *right;
}
static int whitespace(int c) { return c == ' ' || c == '\t'; }

/* UTF-8 validation rejects overlong encodings, surrogates and values above U+10FFFF.
   A portable configuration is data: only stream whitespace may be a C0 control. */
static int valid_text(const char *text, size_t length, int stream)
{
    size_t i;
    unsigned long scalar;
    unsigned long minimum;
    unsigned int c;
    int tail;
    i = 0;
    while (i < length) {
        c = (unsigned char)text[i++];
        if (c < 0x80) {
            if (c < 0x20 && !(stream && (c == '\t' || c == '\r' || c == '\n'))) return 0;
            if (c == 0x7f) return 0;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf) { tail = 1; scalar = c & 0x1f; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { tail = 2; scalar = c & 0x0f; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { tail = 3; scalar = c & 7; minimum = 0x10000; }
        else return 0;
        while (tail--) {
            if (i == length) return 0;
            c = (unsigned char)text[i++];
            if (c < 0x80 || c > 0xbf) return 0;
            scalar = (scalar << 6) | (c & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return 0;
    }
    return 1;
}

const char *wtop_keymap_name(wtop_keymap value)
{
    static const char *const names[] = { "htop", "vi", "emacs" };
    return value >= WTOP_KEYMAP_HTOP && value <= WTOP_KEYMAP_EMACS ? names[(int)value] : "invalid";
}
const char *wtop_theme_name(wtop_theme value)
{
    static const char *const names[] = { "htop", "ntop", "monochrome" };
    return value >= WTOP_THEME_HTOP && value <= WTOP_THEME_MONO ? names[(int)value] : "invalid";
}
const char *wtop_mouse_name(wtop_mouse value)
{
    static const char *const names[] = { "auto", "on", "off" };
    return value >= WTOP_MOUSE_AUTO && value <= WTOP_MOUSE_OFF ? names[(int)value] : "invalid";
}
const char *wtop_column_name(int value)
{
    return value >= 0 && value < WTOP_MAX_COLUMNS ? column_names[value] : "invalid";
}
const char *wtop_header_row_name(int value)
{
    return value >= 0 && value < WTOP_HEADER_ROWS ? row_names[value] : "invalid";
}
const char *wtop_color_name(int value)
{
    return value >= 0 && value < WTOP_COLOR_COUNT ? color_names[value] : "invalid";
}
static const char *const footer_names[] = { "functions", "compact", "status" };
static const char *const status_names[] = { "banner", "bottom", "hidden" };
static const char *const meter_names[] = { "bars", "blocks", "text" };
static const char *const charset_names[] = { "ascii", "unicode" };
static const char *const background_names[] = { "terminal", "palette" };
const char *wtop_background_mode_name(int v) { return v >= 0 && v < 2 ? background_names[v] : "invalid"; }
const char *wtop_footer_style_name(int v) { return v >= 0 && v < 3 ? footer_names[v] : "invalid"; }
const char *wtop_status_position_name(int v) { return v >= 0 && v < 3 ? status_names[v] : "invalid"; }
const char *wtop_meter_style_name(int v) { return v >= 0 && v < 3 ? meter_names[v] : "invalid"; }
const char *wtop_character_set_name(int v) { return v >= 0 && v < 2 ? charset_names[v] : "invalid"; }
static int named_value(const char *text, const char *const *names, int count, int *value)
{
    int i;
    for (i = 0; i < count; ++i) if (equal(text, names[i])) { *value = i; return 1; }
    return 0;
}
int wtop_parse_footer_style(const char *text, wtop_footer_style *value)
{
    int v; if (!named_value(text, footer_names, 3, &v)) return 0;
    *value = (wtop_footer_style)v; return 1;
}
int wtop_parse_status_position(const char *text, wtop_status_position *value)
{
    int v; if (!named_value(text, status_names, 3, &v)) return 0;
    *value = (wtop_status_position)v; return 1;
}
int wtop_parse_character_set(const char *text, wtop_character_set *value)
{
    int v; if (!named_value(text, charset_names, 2, &v)) return 0;
    *value = (wtop_character_set)v; return 1;
}
int wtop_parse_background_mode(const char *text, wtop_background_mode *value)
{
    int v; if (!named_value(text, background_names, 2, &v)) return 0;
    *value = (wtop_background_mode)v; return 1;
}
int wtop_parse_keymap(const char *text, wtop_keymap *value)
{
    if (equal(text, "htop")) *value = WTOP_KEYMAP_HTOP;
    else if (equal(text, "vi") || equal(text, "vim")) *value = WTOP_KEYMAP_VI;
    else if (equal(text, "emacs")) *value = WTOP_KEYMAP_EMACS;
    else return 0;
    return 1;
}
int wtop_parse_theme(const char *text, wtop_theme *value)
{
    if (equal(text, "htop")) *value = WTOP_THEME_HTOP;
    else if (equal(text, "ntop")) *value = WTOP_THEME_NTOP;
    else if (equal(text, "monochrome") || equal(text, "mono")) *value = WTOP_THEME_MONO;
    else return 0;
    return 1;
}
int wtop_parse_mouse(const char *text, wtop_mouse *value)
{
    if (equal(text, "auto")) *value = WTOP_MOUSE_AUTO;
    else if (equal(text, "on")) *value = WTOP_MOUSE_ON;
    else if (equal(text, "off")) *value = WTOP_MOUSE_OFF;
    else return 0;
    return 1;
}
int wtop_parse_column(const char *text, int *value)
{
    int i;
    for (i = 0; i < WTOP_MAX_COLUMNS; ++i) {
        if (equal(text, column_names[i])) { *value = i; return 1; }
    }
    if (equal(text, "PID")) *value = WTOP_COLUMN_ID;
    else if (equal(text, "CPU")) *value = WTOP_COLUMN_CPU;
    else if (equal(text, "DISK") || equal(text, "IO")) *value = WTOP_COLUMN_IO;
    else if (equal(text, "TIME")) *value = WTOP_COLUMN_AGE;
    else return 0;
    return 1;
}

void wtop_settings_set_theme(wtop_settings *settings, wtop_theme theme)
{
    static const unsigned int schemes[3][WTOP_COLOR_COUNT] = {
        { 0x07, 0x00, 0x0b, 0x30, 0x30, 0x20, 0x02, 0x02, 0x03, 0x4f, 0x0e, 0x60, 0x03, 0x07, 15, 8, 0, 3 },
        { 0x07, 0x08, 0x0b, 0xb0, 0x10, 0x20, 0x04, 0x02, 0x02, 0x47, 0x08, 0xb0, 0x0b, 0x07, 15, 8, 0, 11 },
        { 0x07, 0x00, 0x0f, 0x70, 0x70, 0x70, 0x07, 0x07, 0x07, 0x70, 0x0f, 0x70, 0x07, 0x07, 15, 8, 0, 7 }
    };
    if (theme < WTOP_THEME_HTOP || theme > WTOP_THEME_MONO) return;
    settings->theme = theme;
    memcpy(settings->colors, schemes[(int)theme], sizeof(settings->colors));
}

void wtop_settings_defaults(wtop_settings *settings)
{
    int i;
    int j;
    static const int io_columns[] = { WTOP_COLUMN_ID, WTOP_COLUMN_USER, WTOP_COLUMN_IO,
        WTOP_COLUMN_MEMORY, WTOP_COLUMN_CPU, WTOP_COLUMN_THREADS, WTOP_COLUMN_PROCESS };
    memset(settings, 0, sizeof(*settings));
    wtop_settings_set_theme(settings, WTOP_THEME_HTOP);
    settings->interval_ms = 1000;
    settings->frame_interval_ms = 50;
    settings->footer_style = WTOP_FOOTER_FUNCTIONS;
    settings->status_position = WTOP_STATUS_BANNER;
    settings->meter_width = 25;
    settings->meter_style = WTOP_METER_BARS;
    settings->character_set = WTOP_CHARSET_UNICODE;
    settings->background_mode = WTOP_BACKGROUND_TERMINAL;
    settings->keymap = WTOP_KEYMAP_HTOP;
    settings->mouse = WTOP_MOUSE_AUTO;
    settings->show_function_bar = 1;
    settings->show_tabs = 1;
    settings->header_count = WTOP_HEADER_ROWS;
    for (i = 0; i < WTOP_HEADER_ROWS; ++i) settings->header_rows[i] = i;
    settings->tab_count = 2;
    settings->next_tab_id = WTOP_MAX_TABS + 1UL;
    for (i = 0; i < WTOP_MAX_TABS; ++i) {
        settings->tabs[i].id = (unsigned long)i + 1;
        sprintf(settings->tabs[i].name, "Tab %d", i + 1);
        settings->tabs[i].visible = 1;
        settings->tabs[i].sort_column = WTOP_COLUMN_CPU;
        settings->tabs[i].descending = 1;
        settings->tabs[i].column_count = WTOP_MAX_COLUMNS;
        for (j = 0; j < WTOP_MAX_COLUMNS; ++j) settings->tabs[i].columns[j] = j;
    }
    strcpy(settings->tabs[0].name, "Main");
    strcpy(settings->tabs[1].name, "I/O");
    settings->tabs[1].sort_column = WTOP_COLUMN_IO;
    settings->tabs[1].column_count = (int)(sizeof(io_columns) / sizeof(io_columns[0]));
    memcpy(settings->tabs[1].columns, io_columns, sizeof(io_columns));
}

static int boolean(const char *value, int *result)
{
    if (equal(value, "1") || equal(value, "true") || equal(value, "on")) *result = 1;
    else if (equal(value, "0") || equal(value, "false") || equal(value, "off")) *result = 0;
    else return 0;
    return 1;
}
static int bounded_string(const char *text, size_t capacity)
{
    const char *end;
    end = (const char *)memchr(text, 0, capacity);
    return end && valid_text(text, (size_t)(end - text), 0);
}

int wtop_settings_validate(const wtop_settings *settings, const char **error)
{
    int i;
    int j;
    int seen[WTOP_MAX_COLUMNS];
    const wtop_tab *tab;
    *error = NULL;
    if (settings->keymap < WTOP_KEYMAP_HTOP || settings->keymap > WTOP_KEYMAP_EMACS ||
        settings->theme < WTOP_THEME_HTOP || settings->theme > WTOP_THEME_MONO ||
        settings->mouse < WTOP_MOUSE_AUTO || settings->mouse > WTOP_MOUSE_OFF ||
        settings->interval_ms < 50 || settings->interval_ms > 60000 ||
        settings->show_function_bar < 0 || settings->show_function_bar > 1 ||
        settings->show_tabs < 0 || settings->show_tabs > 1 ||
        settings->footer_style < WTOP_FOOTER_FUNCTIONS || settings->footer_style > WTOP_FOOTER_STATUS ||
        settings->status_position < WTOP_STATUS_BANNER || settings->status_position > WTOP_STATUS_HIDDEN ||
        settings->meter_style < WTOP_METER_BARS || settings->meter_style > WTOP_METER_TEXT ||
        settings->character_set < WTOP_CHARSET_ASCII || settings->character_set > WTOP_CHARSET_UNICODE ||
        settings->background_mode < WTOP_BACKGROUND_TERMINAL || settings->background_mode > WTOP_BACKGROUND_PALETTE ||
        settings->frame_interval_ms < 16 || settings->frame_interval_ms > 1000 ||
        settings->meter_width < 8 || settings->meter_width > 80) {
        *error = "invalid general setting"; return 0;
    }
    for (i = 0; i < WTOP_COLOR_COUNT; ++i) if (settings->colors[i] > (i >= WTOP_COLOR_FG_FOCUS ? 15U : 255U)) {
        *error = "legacy color attributes must be 0..255; semantic palette indices must be 0..15"; return 0;
    }
    if (settings->header_count < 0 || settings->header_count > WTOP_HEADER_ROWS) {
        *error = "invalid header-row count"; return 0;
    }
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < settings->header_count; ++i) {
        j = settings->header_rows[i];
        if (j < 0 || j >= WTOP_HEADER_ROWS || seen[j]) {
            *error = "header rows must be known and unique"; return 0;
        }
        seen[j] = 1;
    }
    if (settings->tab_count < 1 || settings->tab_count > WTOP_MAX_TABS ||
        settings->active_tab < 0 || settings->active_tab >= settings->tab_count) {
        *error = "invalid tab count or active tab"; return 0;
    }
    for (i = 0; i < settings->tab_count; ++i) {
        tab = &settings->tabs[i];
        if (!tab->id || tab->id > WTOP_TAB_ID_MAX ||
            settings->next_tab_id > WTOP_TAB_ID_MAX ||
            (settings->next_tab_id && tab->id >= settings->next_tab_id)) {
            *error = "invalid or exhausted view identity sequence"; return 0;
        }
        for (j = 0; j < i; ++j) if (tab->id == settings->tabs[j].id) {
            *error = "view identities must be unique"; return 0;
        }
        if (!tab->name[0] || !bounded_string(tab->name, sizeof(tab->name)) ||
            !bounded_string(tab->filter, sizeof(tab->filter)) ||
            tab->visible < 0 || tab->visible > 1 || tab->tree < 0 || tab->tree > 1 ||
            tab->descending < 0 || tab->descending > 1 ||
            tab->sort_column < 0 || tab->sort_column >= WTOP_MAX_COLUMNS ||
            tab->column_count < 1 || tab->column_count > WTOP_MAX_COLUMNS) {
            *error = "invalid tab name, filter, flags, sort or columns"; return 0;
        }
        for (j = 0; j < i; ++j) if (equal(tab->name, settings->tabs[j].name)) {
            *error = "tab names must be unique"; return 0;
        }
        memset(seen, 0, sizeof(seen));
        for (j = 0; j < tab->column_count; ++j) {
            int column;
            column = tab->columns[j];
            if (column < 0 || column >= WTOP_MAX_COLUMNS || seen[column]) {
                *error = "tab columns must be known and unique"; return 0;
            }
            seen[column] = 1;
        }
    }
    if (!settings->tabs[settings->active_tab].visible) {
        *error = "the active tab must be visible"; return 0;
    }
    return 1;
}

int wtop_settings_apply_options(wtop_settings *settings, const wtop_options *options, const char **error)
{
    wtop_settings candidate;
    wtop_theme theme;
    if (!wtop_settings_validate(settings, error)) return 0;
    candidate = *settings;
    *error = NULL;
    if (options->theme_name) {
        if (!wtop_parse_theme(options->theme_name, &theme)) { *error = "invalid theme"; return 0; }
        wtop_settings_set_theme(&candidate, theme);
    }
    if (options->monochrome) wtop_settings_set_theme(&candidate, WTOP_THEME_MONO);
    if (options->keymap_name && !wtop_parse_keymap(options->keymap_name, &candidate.keymap)) {
        *error = "invalid keymap"; return 0;
    }
    if (options->mouse_mode != -1) candidate.mouse = (wtop_mouse)options->mouse_mode;
    if (options->show_function_bar != -1) candidate.show_function_bar = options->show_function_bar;
    if (options->footer_style) {
        if (equal(options->footer_style, "hidden")) candidate.show_function_bar = 0;
        else if (!wtop_parse_footer_style(options->footer_style, &candidate.footer_style)) {
            *error = "invalid footer style"; return 0;
        } else candidate.show_function_bar = 1;
    }
    if (options->status_position && !wtop_parse_status_position(options->status_position, &candidate.status_position)) {
        *error = "invalid status position"; return 0;
    }
    if (options->character_set && !wtop_parse_character_set(options->character_set, &candidate.character_set)) {
        *error = "invalid character set"; return 0;
    }
    if (options->frame_interval_ms) candidate.frame_interval_ms = options->frame_interval_ms;
    if (options->interval_set) candidate.interval_ms = options->interval_ms;
    if (options->sort_column && !wtop_parse_column(options->sort_column, &candidate.tabs[candidate.active_tab].sort_column)) {
        *error = "invalid sort column"; return 0;
    }
    if (!wtop_settings_validate(&candidate, error)) return 0;
    *settings = candidate;
    return 1;
}

int wtop_settings_tab_find(const wtop_settings *settings, unsigned long id)
{
    int i;
    for (i = 0; i < settings->tab_count; ++i) if (settings->tabs[i].id == id) return i;
    return -1;
}

static int tab_name_copy(wtop_tab *tab, const char *name, const char **error)
{
    size_t length;
    if (!name) { *error = "view name is required"; return 0; }
    length = 0;
    while (length < sizeof(tab->name) && name[length]) ++length;
    if (!length || length == sizeof(tab->name) || !valid_text(name, length, 0)) {
        *error = "view name must be valid UTF-8 within 31 bytes"; return 0;
    }
    memcpy(tab->name, name, length + 1);
    return 1;
}

static int tab_insert(wtop_settings *settings, const wtop_tab *value,
                      int index, const char *name, const char **error)
{
    wtop_settings candidate;
    wtop_tab tab;
    unsigned long active;
    int i;
    if (!wtop_settings_validate(settings, error)) return 0;
    if (index < 0 || index > settings->tab_count || settings->tab_count == WTOP_MAX_TABS) {
        *error = "view insertion exceeds the eight-view capacity"; return 0;
    }
    if (!settings->next_tab_id) { *error = "view identity space is exhausted"; return 0; }
    tab = *value;
    if (!tab_name_copy(&tab, name, error)) return 0;
    candidate = *settings;
    active = candidate.tabs[candidate.active_tab].id;
    tab.id = candidate.next_tab_id;
    candidate.next_tab_id = tab.id == WTOP_TAB_ID_MAX ? 0 : tab.id + 1;
    for (i = candidate.tab_count; i > index; --i) candidate.tabs[i] = candidate.tabs[i - 1];
    candidate.tabs[index] = tab;
    ++candidate.tab_count;
    candidate.active_tab = wtop_settings_tab_find(&candidate, active);
    if (!wtop_settings_validate(&candidate, error)) return 0;
    *settings = candidate;
    return 1;
}

int wtop_settings_tab_insert(wtop_settings *settings, int index, const char *name, const char **error)
{
    wtop_tab tab;
    int i;
    memset(&tab, 0, sizeof(tab));
    tab.visible = 1;
    tab.sort_column = WTOP_COLUMN_CPU;
    tab.descending = 1;
    tab.column_count = WTOP_MAX_COLUMNS;
    for (i = 0; i < WTOP_MAX_COLUMNS; ++i) tab.columns[i] = i;
    return tab_insert(settings, &tab, index, name, error);
}

int wtop_settings_tab_duplicate(wtop_settings *settings, int source, int index,
                                 const char *name, const char **error)
{
    if (!wtop_settings_validate(settings, error)) return 0;
    if (source < 0 || source >= settings->tab_count) { *error = "unknown source view"; return 0; }
    return tab_insert(settings, &settings->tabs[source], index, name, error);
}

int wtop_settings_tab_rename(wtop_settings *settings, int index, const char *name, const char **error)
{
    wtop_settings candidate;
    if (!wtop_settings_validate(settings, error)) return 0;
    if (index < 0 || index >= settings->tab_count) { *error = "unknown view"; return 0; }
    candidate = *settings;
    if (!tab_name_copy(&candidate.tabs[index], name, error) ||
        !wtop_settings_validate(&candidate, error)) return 0;
    *settings = candidate;
    return 1;
}

int wtop_settings_tab_move(wtop_settings *settings, int from, int to, const char **error)
{
    wtop_settings candidate;
    wtop_tab tab;
    unsigned long active;
    int i;
    if (!wtop_settings_validate(settings, error)) return 0;
    if (from < 0 || to < 0 || from >= settings->tab_count || to >= settings->tab_count) {
        *error = "unknown view position"; return 0;
    }
    candidate = *settings;
    active = candidate.tabs[candidate.active_tab].id;
    tab = candidate.tabs[from];
    if (from < to) for (i = from; i < to; ++i) candidate.tabs[i] = candidate.tabs[i + 1];
    else for (i = from; i > to; --i) candidate.tabs[i] = candidate.tabs[i - 1];
    candidate.tabs[to] = tab;
    candidate.active_tab = wtop_settings_tab_find(&candidate, active);
    if (!wtop_settings_validate(&candidate, error)) return 0;
    *settings = candidate;
    return 1;
}

static int select_visible(wtop_settings *settings, const char **error)
{
    int i;
    if (settings->active_tab >= 0 && settings->active_tab < settings->tab_count &&
        settings->tabs[settings->active_tab].visible) return 1;
    for (i = 0; i < settings->tab_count; ++i) if (settings->tabs[i].visible) {
        settings->active_tab = i; return 1;
    }
    *error = "at least one view must remain visible";
    return 0;
}

int wtop_settings_tab_delete(wtop_settings *settings, int index, const char **error)
{
    wtop_settings candidate;
    unsigned long active;
    int i;
    if (!wtop_settings_validate(settings, error)) return 0;
    if (index < 0 || index >= settings->tab_count || settings->tab_count == 1) {
        *error = "at least one view must remain"; return 0;
    }
    candidate = *settings;
    active = candidate.tabs[candidate.active_tab].id;
    for (i = index; i + 1 < candidate.tab_count; ++i) candidate.tabs[i] = candidate.tabs[i + 1];
    --candidate.tab_count;
    memset(&candidate.tabs[candidate.tab_count], 0, sizeof(candidate.tabs[0]));
    candidate.active_tab = wtop_settings_tab_find(&candidate, active);
    if (!select_visible(&candidate, error) || !wtop_settings_validate(&candidate, error)) return 0;
    *settings = candidate;
    return 1;
}

int wtop_settings_tab_set_visible(wtop_settings *settings, int index, int visible, const char **error)
{
    wtop_settings candidate;
    if (!wtop_settings_validate(settings, error)) return 0;
    if (index < 0 || index >= settings->tab_count || (visible != 0 && visible != 1)) {
        *error = "invalid view visibility request"; return 0;
    }
    candidate = *settings;
    candidate.tabs[index].visible = visible;
    if (!select_visible(&candidate, error) || !wtop_settings_validate(&candidate, error)) return 0;
    *settings = candidate;
    return 1;
}

/* Any indexed record has a view scope, including an unknown extension field.
   Returns one for a scope, zero for an ordinary key, minus one for malformed. */
static int tab_scope(const char *key, int *index, size_t *suffix)
{
    const char *p;
    unsigned long tab;
    if (strlen(key) < 4 || ascii_lower(key[0]) != 't' || ascii_lower(key[1]) != 'a' ||
        ascii_lower(key[2]) != 'b' || key[3] < '0' || key[3] > '9') return 0;
    tab = 0;
    p = key + 3;
    while (*p >= '0' && *p <= '9') {
        tab = tab * 10 + (unsigned long)(*p++ - '0');
        if (tab >= WTOP_MAX_TABS) return -1;
    }
    if (*p != '.' || !p[1]) return -1;
    *index = (int)tab;
    *suffix = (size_t)(p - key);
    return 1;
}

/* Return -1 for an inert unknown key, -2 for a malformed reserved TabN key. */
static int key_id(const char *key)
{
    int i;
    int tab;
    int scope;
    size_t suffix;
    for (i = 0; i < BASE_KEYS; ++i) if (equal(key, base_names[i])) return i;
    for (i = 0; i < WTOP_COLOR_COUNT; ++i) if (equal(key, color_names[i])) return BASE_KEYS + i;
    scope = tab_scope(key, &tab, &suffix);
    if (scope < 0) return -2;
    if (!scope) return -1;
    for (i = 0; i < TAB_FIELDS; ++i) if (equal(key + suffix + 1, tab_names[i]))
        return TAB_KEY_BASE + tab * TAB_FIELDS + i;
    return -1;
}

/* The caller keeps original bytes; only this line copy is tokenized. */
static int split_line(char *line, char **key, char **value, char **comment, char *comment_buffer)
{
    char *p;
    char *end;
    int quoted;
    int equals;
    p = line;
    *comment = NULL;
    while (whitespace((unsigned char)*p)) ++p;
    if (!*p || *p == '#' || *p == ';' || *p == '\r' || *p == '\n') return 1;
    *key = p;
    while (*p && !whitespace((unsigned char)*p) && *p != '=' && *p != '\r' && *p != '\n') {
        int c;
        c = (unsigned char)*p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return 0;
        ++p;
    }
    if (p - *key > 79) return 0;
    equals = *p == '=';
    if (*p) *p++ = 0;
    while (whitespace((unsigned char)*p)) ++p;
    if (!equals && *p == '=') { ++p; while (whitespace((unsigned char)*p)) ++p; }
    *value = p;
    quoted = 0;
    while (*p && *p != '\r' && *p != '\n') {
        if (*p == '\\' && quoted && p[1]) { p += 2; continue; }
        if (*p == '"') quoted = !quoted;
        if (!quoted && (*p == '#' || *p == ';') && (p == *value || whitespace((unsigned char)p[-1]))) {
            *comment = p;
            break;
        }
        ++p;
    }
    end = p;
    while (end > *value && whitespace((unsigned char)end[-1])) --end;
    if (*comment) {
        char *comment_end;
        strcpy(comment_buffer, *comment);
        *comment = comment_buffer;
        comment_end = comment_buffer + strlen(comment_buffer);
        while (comment_end > comment_buffer && (comment_end[-1] == '\r' || comment_end[-1] == '\n')) --comment_end;
        *comment_end = 0;
    }
    *end = 0;
    return 2;
}

static int parse_string(const char *value, char *output, size_t capacity)
{
    size_t used;
    int quoted;
    char c;
    used = 0;
    quoted = *value == '"';
    if (quoted) ++value;
    while (*value) {
        c = *value++;
        if (quoted && c == '"') {
            while (whitespace((unsigned char)*value)) ++value;
            if (*value) return 0;
            output[used] = 0;
            return valid_text(output, used, 0);
        }
        if (quoted && c == '\\') {
            c = *value++;
            if (c != '\\' && c != '"') return 0;
        }
        if (used + 1 >= capacity) return 0;
        output[used++] = c;
    }
    if (quoted) return 0;
    output[used] = 0;
    return valid_text(output, used, 0);
}

static int parse_list(char *value, int *items, int *count, int rows)
{
    char *p;
    char *end;
    char *comma;
    int item;
    int i;
    int length;
    int max_items;
    max_items = rows ? WTOP_HEADER_ROWS : WTOP_MAX_COLUMNS;
    *count = 0;
    if (rows && equal(value, "none")) return 1;
    p = value;
    for (;;) {
        while (whitespace((unsigned char)*p)) ++p;
        comma = strchr(p, ',');
        if (comma) *comma = 0;
        end = p + strlen(p);
        while (end > p && whitespace((unsigned char)end[-1])) *--end = 0;
        if (!*p || *count >= max_items) return 0;
        item = -1;
        if (rows) {
            for (i = 0; i < WTOP_HEADER_ROWS; ++i) if (equal(p, row_names[i])) item = i;
        } else if (!wtop_parse_column(p, &item)) return 0;
        if (item < 0) return 0;
        length = *count;
        for (i = 0; i < length; ++i) if (items[i] == item) return 0;
        items[(*count)++] = item;
        if (!comma) break;
        p = comma + 1;
    }
    return 1;
}

static int apply_key(wtop_settings *settings, int id, char *value)
{
    unsigned long number;
    wtop_theme theme;
    wtop_tab *tab;
    int field;
    if (id >= TAB_KEY_BASE) {
        tab = &settings->tabs[(id - TAB_KEY_BASE) / TAB_FIELDS];
        field = (id - TAB_KEY_BASE) % TAB_FIELDS;
        switch (field) {
        case 0: return parse_string(value, tab->name, sizeof(tab->name)) && tab->name[0];
        case 1: return parse_string(value, tab->filter, sizeof(tab->filter));
        case 2: return boolean(value, &tab->visible);
        case 3: return wtop_parse_column(value, &tab->sort_column);
        case 4: return boolean(value, &tab->descending);
        case 5: return boolean(value, &tab->tree);
        case 6: return parse_list(value, tab->columns, &tab->column_count, 0);
        case 7:
            if (!wtop_parse_unsigned(value, 1, WTOP_TAB_ID_MAX, 10, &number)) return 0;
            tab->id = number;
            return 1;
        }
        return 0;
    }
    if (id >= BASE_KEYS) {
        if (!wtop_parse_unsigned(value, 0, 255, 0, &number)) return 0;
        settings->colors[id - BASE_KEYS] = (unsigned int)number;
        return 1;
    }
    switch (id) {
    case 0: return wtop_parse_unsigned(value, 1, WTOP_CONFIG_VERSION, 10, &number);
    case 1:
        if (!wtop_parse_theme(value, &theme)) return 0;
        wtop_settings_set_theme(settings, theme);
        return 1;
    case 2: return wtop_parse_keymap(value, &settings->keymap);
    case 3: return wtop_parse_mouse(value, &settings->mouse);
    case 4: return boolean(value, &settings->show_function_bar);
    case 5: return boolean(value, &settings->show_tabs);
    case 6: return parse_list(value, settings->header_rows, &settings->header_count, 1);
    case 7:
        if (!wtop_parse_unsigned(value, 50, 60000, 10, &number)) return 0;
        settings->interval_ms = number;
        return 1;
    case 8:
        if (!wtop_parse_unsigned(value, 1, WTOP_MAX_TABS, 10, &number)) return 0;
        settings->tab_count = (int)number;
        return 1;
    case 9:
        if (!wtop_parse_unsigned(value, 0, WTOP_MAX_TABS - 1, 10, &number)) return 0;
        settings->active_tab = (int)number;
        return 1;
    case 10:
        if (!wtop_parse_unsigned(value, 0, WTOP_TAB_ID_MAX, 10, &number)) return 0;
        settings->next_tab_id = number;
        return 1;
    case 11: return wtop_parse_footer_style(value, &settings->footer_style);
    case 12: return wtop_parse_status_position(value, &settings->status_position);
    case 13:
        if (!wtop_parse_unsigned(value, 16, 1000, 10, &number)) return 0;
        settings->frame_interval_ms = number; return 1;
    case 14:
        if (!wtop_parse_unsigned(value, 8, 80, 10, &number)) return 0;
        settings->meter_width = (int)number; return 1;
    case 15: {
        int v; if (!named_value(value, meter_names, 3, &v)) return 0;
        settings->meter_style = (wtop_meter_style)v; return 1;
    }
    case 16: return wtop_parse_character_set(value, &settings->character_set);
    case 17: return wtop_parse_background_mode(value, &settings->background_mode);
    }
    return 0;
}

void wtop_config_document_free(wtop_config_document *document)
{
    if (!document) return;
    free(document->original);
    memset(document, 0, sizeof(*document));
}

int wtop_config_document_clone(wtop_config_document *destination,
                                const wtop_config_document *source, const char **error)
{
    wtop_config_document candidate;
    int i;
    int j;
    if (!destination || !source) { *error = "configuration document is missing"; return 0; }
    if (destination == source) { *error = NULL; return 1; }
    candidate = *source;
    if (source->original) {
        if (source->length > WTOP_CONFIG_MAX_BYTES ||
            source->schema_version < 1 || source->schema_version > WTOP_CONFIG_VERSION ||
            source->tab_count < 1 || source->tab_count > WTOP_MAX_TABS ||
            !valid_text(source->original, source->length, 1)) {
            *error = "invalid original configuration document"; return 0;
        }
        for (i = 0; i < source->tab_count; ++i) {
            if (!source->tab_ids[i] || source->tab_ids[i] > WTOP_TAB_ID_MAX) {
                *error = "invalid original view identity"; return 0;
            }
            for (j = 0; j < i; ++j) if (source->tab_ids[j] == source->tab_ids[i]) {
                *error = "duplicate original view identity"; return 0;
            }
        }
        candidate.original = (char *)malloc(source->length + 1);
        if (!candidate.original) { *error = "out of memory copying configuration"; return 0; }
        memcpy(candidate.original, source->original, source->length);
        candidate.original[source->length] = 0;
    } else if (source->length || source->schema_version || source->tab_count) {
        *error = "configuration metadata has no original document"; return 0;
    }
    wtop_config_document_free(destination);
    *destination = candidate;
    *error = NULL;
    return 1;
}

int wtop_settings_read(FILE *input, wtop_settings *settings, wtop_config_document *document,
                       unsigned long *error_line, const char **error)
{
    char *bytes;
    char *larger;
    char line[WTOP_CONFIG_LINE_BYTES];
    char comment_buffer[WTOP_CONFIG_LINE_BYTES];
    char *key;
    char *value;
    char *comment;
    size_t length;
    size_t capacity;
    size_t start;
    size_t end;
    size_t n;
    unsigned long line_number;
    unsigned int explicit_colors[WTOP_COLOR_COUNT];
    int seen[KEY_COUNT];
    int c;
    int kind;
    int id;
    int i;
    int schema_version;
    wtop_settings candidate;
    if (!wtop_settings_validate(settings, error)) return 0;
    *error = NULL;
    if (error_line) *error_line = 0;
    length = 0;
    capacity = 1024;
    bytes = (char *)malloc(capacity);
    if (!bytes) { *error = "out of memory reading configuration"; return 0; }
    while ((c = fgetc(input)) != EOF) {
        if (length >= WTOP_CONFIG_MAX_BYTES) { *error = "configuration exceeds 32768 bytes"; goto fail; }
        if (length + 1 >= capacity) {
            size_t next;
            next = capacity > WTOP_CONFIG_MAX_BYTES / 2 ?
                (size_t)(WTOP_CONFIG_MAX_BYTES + 1) : capacity * 2;
            larger = (char *)realloc(bytes, next);
            if (!larger) { *error = "out of memory reading configuration"; goto fail; }
            bytes = larger;
            capacity = next;
        }
        bytes[length++] = (char)c;
    }
    if (ferror(input)) { *error = "cannot read configuration"; goto fail; }
    bytes[length] = 0;
    if (!valid_text(bytes, length, 1)) { *error = "configuration is not valid UTF-8 text"; goto fail; }
    candidate = *settings;
    schema_version = 1;
    candidate.next_tab_id = WTOP_MAX_TABS + 1UL;
    for (i = 0; i < WTOP_MAX_TABS; ++i) candidate.tabs[i].id = (unsigned long)i + 1;
    memset(seen, 0, sizeof(seen));
    memset(explicit_colors, 0, sizeof(explicit_colors));
    start = 0;
    if (length >= 3 && (unsigned char)bytes[0] == 0xef && (unsigned char)bytes[1] == 0xbb &&
        (unsigned char)bytes[2] == 0xbf) start = 3;
    line_number = 0;
    while (start < length) {
        ++line_number;
        if (error_line) *error_line = line_number;
        end = start;
        while (end < length && bytes[end] != '\n') ++end;
        n = end - start;
        if (n >= sizeof(line)) { *error = "configuration line exceeds 1023 bytes"; goto fail; }
        memcpy(line, bytes + start, n);
        line[n] = 0;
        kind = split_line(line, &key, &value, &comment, comment_buffer);
        if (!kind) { *error = "invalid configuration key syntax"; goto fail; }
        if (kind == 2) {
            id = key_id(key);
            if (id == -2) { *error = "invalid or out-of-range tab key"; goto fail; }
            if (id >= 0) {
                if (seen[id]) { *error = "duplicate configuration key"; goto fail; }
                if (!apply_key(&candidate, id, value)) { *error = "invalid configuration value"; goto fail; }
                seen[id] = 1;
                if (id == 0) {
                    unsigned long version;
                    if (!wtop_parse_unsigned(value, 1, WTOP_CONFIG_VERSION, 10, &version)) goto fail;
                    schema_version = (int)version;
                }
                if (id >= BASE_KEYS && id < TAB_KEY_BASE) explicit_colors[id - BASE_KEYS] = candidate.colors[id - BASE_KEYS];
            }
        }
        start = end < length ? end + 1 : end;
    }
    for (i = 0; i < WTOP_COLOR_COUNT; ++i) if (seen[BASE_KEYS + i]) candidate.colors[i] = explicit_colors[i];
    /* An explicit old background remains intentional. New BackgroundMode wins
       independently of record order. Old highlight was a complete attribute. */
    if (!seen[17] && seen[BASE_KEYS + WTOP_COLOR_BG]) candidate.background_mode = WTOP_BACKGROUND_PALETTE;
    if (seen[BASE_KEYS + WTOP_COLOR_BG_HIGHLIGHT]) {
        if (!seen[BASE_KEYS + WTOP_COLOR_FG_SELECTED])
            candidate.colors[WTOP_COLOR_FG_SELECTED] = candidate.colors[WTOP_COLOR_BG_HIGHLIGHT] & 15U;
        if (!seen[BASE_KEYS + WTOP_COLOR_BG_SELECTED])
            candidate.colors[WTOP_COLOR_BG_SELECTED] = (candidate.colors[WTOP_COLOR_BG_HIGHLIGHT] >> 4) & 15U;
    }
    for (i = candidate.tab_count; i < WTOP_MAX_TABS; ++i) {
        int field;
        for (field = 0; field < TAB_FIELDS; ++field) if (seen[TAB_KEY_BASE + i * TAB_FIELDS + field]) {
            *error = "TabCount excludes a configured tab"; goto fail;
        }
    }
    if (schema_version == 2) {
        if (!seen[10]) { *error = "version 2 requires NextTabId"; goto fail; }
        for (i = 0; i < candidate.tab_count; ++i) {
            if (!seen[TAB_KEY_BASE + i * TAB_FIELDS + 7]) {
                *error = "version 2 requires an Id for each view"; goto fail;
            }
        }
    } else {
        if (seen[10]) { *error = "NextTabId requires ConfigVersion 2"; goto fail; }
        for (i = 0; i < WTOP_MAX_TABS; ++i) if (seen[TAB_KEY_BASE + i * TAB_FIELDS + 7]) {
            *error = "view Id requires ConfigVersion 2"; goto fail;
        }
    }
    if (!wtop_settings_validate(&candidate, error)) goto fail;
    *settings = candidate;
    if (document) {
        wtop_config_document_free(document);
        document->original = bytes;
        document->length = length;
        document->schema_version = schema_version;
        document->tab_count = candidate.tab_count;
        for (i = 0; i < WTOP_MAX_TABS; ++i)
            document->tab_ids[i] = i < candidate.tab_count ? candidate.tabs[i].id : 0;
    } else free(bytes);
    if (error_line) *error_line = 0;
    return 1;
fail:
    free(bytes);
    return 0;
}

typedef struct writer {
    FILE *file;
    char *memory;
    size_t count;
    int last;
    int failed;
} writer;
static int write_bytes(writer *out, const char *text, size_t length)
{
    if (out->failed) return 0;
    if (length > WTOP_CONFIG_MAX_BYTES - out->count ||
        (out->file && fwrite(text, 1, length, out->file) != length)) {
        out->failed = 1; return 0;
    }
    if (out->memory) memcpy(out->memory + out->count, text, length);
    out->count += length;
    if (length) out->last = (unsigned char)text[length - 1];
    return 1;
}
static int write_text(writer *out, const char *text) { return write_bytes(out, text, strlen(text)); }
static int write_number(writer *out, unsigned long number, int hexadecimal)
{
    char buffer[32];
    if (hexadecimal) sprintf(buffer, "0x%02lx", number);
    else sprintf(buffer, "%lu", number);
    return write_text(out, buffer);
}
static int write_string(writer *out, const char *text)
{
    if (!write_text(out, "\"")) return 0;
    while (*text) {
        if ((*text == '\\' || *text == '"') && !write_text(out, "\\")) return 0;
        if (!write_bytes(out, text++, 1)) return 0;
    }
    return write_text(out, "\"");
}
static int write_list(writer *out, const int *items, int count, int rows)
{
    int i;
    if (!count) return write_text(out, "none");
    for (i = 0; i < count; ++i) {
        if (i && !write_text(out, ",")) return 0;
        if (!write_text(out, rows ? row_names[items[i]] : column_names[items[i]])) return 0;
    }
    return 1;
}
static int write_value(writer *out, const wtop_settings *settings, int id)
{
    const wtop_tab *tab;
    if (id >= TAB_KEY_BASE) {
        tab = &settings->tabs[(id - TAB_KEY_BASE) / TAB_FIELDS];
        switch ((id - TAB_KEY_BASE) % TAB_FIELDS) {
        case 0: return write_string(out, tab->name);
        case 1: return write_string(out, tab->filter);
        case 2: return write_number(out, (unsigned long)tab->visible, 0);
        case 3: return write_text(out, column_names[tab->sort_column]);
        case 4: return write_number(out, (unsigned long)tab->descending, 0);
        case 5: return write_number(out, (unsigned long)tab->tree, 0);
        case 6: return write_list(out, tab->columns, tab->column_count, 0);
        case 7: return write_number(out, tab->id, 0);
        }
    } else if (id >= BASE_KEYS) return write_number(out, settings->colors[id - BASE_KEYS], 1);
    else switch (id) {
    case 0: return write_text(out, "2");
    case 1: return write_text(out, wtop_theme_name(settings->theme));
    case 2: return write_text(out, wtop_keymap_name(settings->keymap));
    case 3: return write_text(out, wtop_mouse_name(settings->mouse));
    case 4: return write_number(out, (unsigned long)settings->show_function_bar, 0);
    case 5: return write_number(out, (unsigned long)settings->show_tabs, 0);
    case 6: return write_list(out, settings->header_rows, settings->header_count, 1);
    case 7: return write_number(out, settings->interval_ms, 0);
    case 8: return write_number(out, (unsigned long)settings->tab_count, 0);
    case 9: return write_number(out, (unsigned long)settings->active_tab, 0);
    case 10: return write_number(out, settings->next_tab_id, 0);
    case 11: return write_text(out, wtop_footer_style_name(settings->footer_style));
    case 12: return write_text(out, wtop_status_position_name(settings->status_position));
    case 13: return write_number(out, settings->frame_interval_ms, 0);
    case 14: return write_number(out, (unsigned long)settings->meter_width, 0);
    case 15: return write_text(out, wtop_meter_style_name(settings->meter_style));
    case 16: return write_text(out, wtop_character_set_name(settings->character_set));
    case 17: return write_text(out, wtop_background_mode_name(settings->background_mode));
    }
    return 0;
}
static int write_key(writer *out, const wtop_settings *settings, int id, const char *comment)
{
    char key[80];
    if (id >= TAB_KEY_BASE) {
        int tab;
        tab = (id - TAB_KEY_BASE) / TAB_FIELDS;
        if (tab >= settings->tab_count) {
            if (comment) return write_text(out, comment) && write_text(out, "\n");
            return 1;
        }
        sprintf(key, "Tab%d.%s", tab, tab_names[(id - TAB_KEY_BASE) % TAB_FIELDS]);
    } else strcpy(key, id >= BASE_KEYS ? color_names[id - BASE_KEYS] : base_names[id]);
    if (!write_text(out, key) || !write_text(out, " ") || !write_value(out, settings, id)) return 0;
    if (comment && (!write_text(out, " ") || !write_text(out, comment))) return 0;
    return write_text(out, "\n");
}

static int write_document(FILE *output, char *memory, size_t *length,
                          const wtop_settings *settings,
                          const wtop_config_document *document, const char **error)
{
    writer out;
    size_t start;
    size_t end;
    size_t n;
    size_t suffix;
    size_t key_offset;
    int seen[KEY_COUNT];
    int original_seen[KEY_COUNT];
    int i;
    int kind;
    int id;
    int output_id;
    int old_tab;
    int new_tab;
    int scope;
    char line[WTOP_CONFIG_LINE_BYTES];
    char comment_buffer[WTOP_CONFIG_LINE_BYTES];
    char prefix[32];
    char *key;
    char *value;
    char *comment;
    if (!wtop_settings_validate(settings, error)) return 0;
    memset(&out, 0, sizeof(out));
    out.file = output;
    out.memory = memory;
    memset(seen, 0, sizeof(seen));
    memset(original_seen, 0, sizeof(original_seen));
    if (document && document->original) {
        if (document->length > WTOP_CONFIG_MAX_BYTES || !valid_text(document->original, document->length, 1) ||
            document->schema_version < 1 || document->schema_version > WTOP_CONFIG_VERSION ||
            document->tab_count < 1 || document->tab_count > WTOP_MAX_TABS) {
            *error = "invalid original configuration document"; return 0;
        }
        if (document->schema_version < WTOP_CONFIG_VERSION &&
            !write_text(&out, "# Explicit migration: configuration version 1 to 2; stable view identities.\n")) goto failure;
        start = 0;
        if (document->length >= 3 && (unsigned char)document->original[0] == 0xef &&
            (unsigned char)document->original[1] == 0xbb && (unsigned char)document->original[2] == 0xbf) start = 3;
        while (start < document->length) {
            end = start;
            while (end < document->length && document->original[end] != '\n') ++end;
            n = end - start;
            if (n >= sizeof(line)) { *error = "invalid original configuration line"; return 0; }
            memcpy(line, document->original + start, n);
            line[n] = 0;
            kind = split_line(line, &key, &value, &comment, comment_buffer);
            if (!kind) { *error = "invalid original configuration syntax"; return 0; }
            id = kind == 2 ? key_id(key) : -1;
            if (id == -2 || (id >= 0 && original_seen[id])) { *error = "invalid original configuration key"; return 0; }
            if (id >= 0) original_seen[id] = 1;
            scope = kind == 2 ? tab_scope(key, &old_tab, &suffix) : 0;
            if (scope < 0) { *error = "invalid original view scope"; return 0; }
            if (scope && !document->tab_ids[old_tab]) {
                *error = "cannot rebind an unknown field outside the original view set"; return 0;
            }
            new_tab = scope ? wtop_settings_tab_find(settings, document->tab_ids[old_tab]) : -1;
            if (scope && new_tab < 0) {
                /* Explicit deletion retires the scope. Preserve unknown data as
                   inert comments instead of attaching it to a reused index. */
                if (id < 0) {
                    if (n + 2 >= sizeof(line)) { *error = "removed view extension exceeds comment line capacity"; return 0; }
                    if (!write_text(&out, "# Removed view ") ||
                        !write_number(&out, document->tab_ids[old_tab], 0) ||
                        !write_text(&out, "; original extension follows:\n# ") ||
                        !write_bytes(&out, document->original + start, n) ||
                        !write_text(&out, "\n")) goto failure;
                } else if (comment && (!write_text(&out, comment) || !write_text(&out, "\n"))) goto failure;
            } else if (id >= 0) {
                output_id = scope ? TAB_KEY_BASE + new_tab * TAB_FIELDS + (id - TAB_KEY_BASE) % TAB_FIELDS : id;
                if (seen[output_id]) { *error = "duplicate original view identity"; return 0; }
                if (!write_key(&out, settings, output_id, comment)) goto failure;
                seen[output_id] = 1;
            } else if (scope) {
                /* Keep the extension's exact suffix/value/comment while changing
                   only its positional prefix to follow its stable view ID. */
                key_offset = (size_t)(key - line);
                sprintf(prefix, "Tab%d", new_tab);
                if (!write_bytes(&out, document->original + start, key_offset) ||
                    !write_text(&out, prefix) ||
                    !write_bytes(&out, document->original + start + key_offset + suffix,
                                 n - key_offset - suffix + (end < document->length ? 1 : 0))) goto failure;
            } else if (!write_bytes(&out, document->original + start, n + (end < document->length ? 1 : 0))) goto failure;
            start = end < document->length ? end + 1 : end;
        }
    }
    if (out.count && out.last != '\n' && !write_text(&out, "\n")) goto failure;
    for (i = 0; i < TAB_KEY_BASE + settings->tab_count * TAB_FIELDS; ++i) {
        if (!seen[i] && !write_key(&out, settings, i, NULL)) goto failure;
    }
    if (output && ferror(output)) goto failure;
    if (length) *length = out.count;
    if (memory) memory[out.count] = 0;
    return 1;
failure:
    *error = output && ferror(output) ? "cannot write configuration" : "serialized configuration exceeds 32768 bytes";
    return 0;
}

int wtop_settings_write(FILE *output, const wtop_settings *settings,
                        const wtop_config_document *document, const char **error)
{
    if (!output) { *error = "configuration output is missing"; return 0; }
    return write_document(output, NULL, NULL, settings, document, error);
}

int wtop_settings_serialize(const wtop_settings *settings,
                            const wtop_config_document *document,
                            char **bytes, size_t *length, const char **error)
{
    char *candidate;
    size_t candidate_length;
    if (!bytes || !length) { *error = "configuration output is missing"; return 0; }
    candidate = (char *)malloc((size_t)WTOP_CONFIG_MAX_BYTES + 1);
    if (!candidate) { *error = "out of memory serializing configuration"; return 0; }
    if (!write_document(NULL, candidate, &candidate_length, settings, document, error)) {
        free(candidate); return 0;
    }
    *bytes = candidate;
    *length = candidate_length;
    return 1;
}
