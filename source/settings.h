/* Wtop bounded presentation settings. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_SETTINGS_H
#define WTOP_SETTINGS_H

#include <stddef.h>
#include <stdio.h>
#include "core.h"

#define WTOP_MAX_TABS 8
#define WTOP_MAX_COLUMNS 9
#define WTOP_COLUMN_COUNT WTOP_MAX_COLUMNS
#define WTOP_HEADER_ROWS 5
#define WTOP_TAB_NAME_BYTES 32
#define WTOP_FILTER_BYTES 128
#define WTOP_CONFIG_MAX_BYTES 32768UL
#define WTOP_CONFIG_LINE_BYTES 1024
#define WTOP_CONFIG_VERSION 2
#define WTOP_TAB_ID_MAX 4294967295UL

typedef enum wtop_keymap {
    WTOP_KEYMAP_HTOP, WTOP_KEYMAP_VI, WTOP_KEYMAP_EMACS
} wtop_keymap;
typedef enum wtop_theme {
    WTOP_THEME_HTOP, WTOP_THEME_NTOP, WTOP_THEME_MONO
} wtop_theme;
typedef enum wtop_footer_style { WTOP_FOOTER_FUNCTIONS, WTOP_FOOTER_COMPACT, WTOP_FOOTER_STATUS } wtop_footer_style;
typedef enum wtop_status_position { WTOP_STATUS_BANNER, WTOP_STATUS_BOTTOM, WTOP_STATUS_HIDDEN } wtop_status_position;
typedef enum wtop_meter_style { WTOP_METER_BARS, WTOP_METER_BLOCKS, WTOP_METER_TEXT } wtop_meter_style;
typedef enum wtop_character_set { WTOP_CHARSET_ASCII, WTOP_CHARSET_UNICODE } wtop_character_set;
typedef enum wtop_background_mode { WTOP_BACKGROUND_TERMINAL, WTOP_BACKGROUND_PALETTE } wtop_background_mode;

typedef enum wtop_mouse {
    WTOP_MOUSE_AUTO, WTOP_MOUSE_ON, WTOP_MOUSE_OFF
} wtop_mouse;
typedef enum wtop_color_role {
    WTOP_COLOR_FG, WTOP_COLOR_BG, WTOP_COLOR_FG_HIGHLIGHT,
    WTOP_COLOR_BG_HIGHLIGHT, WTOP_COLOR_MENU_BAR, WTOP_COLOR_HEADER,
    WTOP_COLOR_CPU, WTOP_COLOR_MEMORY, WTOP_COLOR_COMMIT, WTOP_COLOR_ERROR,
    WTOP_COLOR_TAG, WTOP_COLOR_FOLLOW, WTOP_COLOR_METER_LABEL,
    WTOP_COLOR_FUNCTION_KEY, WTOP_COLOR_FG_FOCUS, WTOP_COLOR_BG_FOCUS,
    WTOP_COLOR_FG_SELECTED, WTOP_COLOR_BG_SELECTED, WTOP_COLOR_COUNT
} wtop_color_role;
typedef enum wtop_header_row {
    WTOP_ROW_TITLE, WTOP_ROW_CPU, WTOP_ROW_MEMORY,
    WTOP_ROW_COMMIT, WTOP_ROW_SUMMARY
} wtop_header_row;
/* Column identifiers retain the inherited process-sort numbering. */
typedef enum wtop_column {
    WTOP_COLUMN_ID, WTOP_COLUMN_USER, WTOP_COLUMN_PRIORITY, WTOP_COLUMN_CPU,
    WTOP_COLUMN_MEMORY, WTOP_COLUMN_THREADS, WTOP_COLUMN_IO,
    WTOP_COLUMN_AGE, WTOP_COLUMN_PROCESS
} wtop_column;

typedef struct wtop_tab {
    unsigned long id; /* Persistent positive 32-bit identity; independent of index/name. */
    char name[WTOP_TAB_NAME_BYTES];
    char filter[WTOP_FILTER_BYTES];
    int visible;
    int sort_column;
    int descending;
    int tree;
    int columns[WTOP_MAX_COLUMNS];
    int column_count;
} wtop_tab;

typedef struct wtop_settings {
    unsigned int colors[WTOP_COLOR_COUNT];
    unsigned long interval_ms;
    wtop_keymap keymap;
    wtop_theme theme;
    wtop_mouse mouse;
    int show_function_bar;
    int show_tabs;
    wtop_footer_style footer_style;
    wtop_status_position status_position;
    wtop_meter_style meter_style;
    wtop_character_set character_set;
    wtop_background_mode background_mode;
    unsigned long frame_interval_ms;
    int meter_width;
    int header_rows[WTOP_HEADER_ROWS];
    int header_count;
    wtop_tab tabs[WTOP_MAX_TABS];
    int tab_count;
    int active_tab;
    unsigned long next_tab_id; /* Zero means the identity space is exhausted. */
} wtop_settings;

/* Original bytes are owned by this document; initialize to {0} before reading.
   Readers replace settings/document only after complete validation succeeds. */
typedef struct wtop_config_document {
    char *original;
    size_t length;
    int schema_version;
    int tab_count;
    unsigned long tab_ids[WTOP_MAX_TABS];
} wtop_config_document;

void wtop_settings_defaults(wtop_settings *settings);
void wtop_settings_set_theme(wtop_settings *settings, wtop_theme theme);
const char *wtop_keymap_name(wtop_keymap keymap);
const char *wtop_theme_name(wtop_theme theme);
const char *wtop_mouse_name(wtop_mouse mouse);
const char *wtop_column_name(int column);
const char *wtop_header_row_name(int row);
const char *wtop_color_name(int role);
const char *wtop_footer_style_name(int value);
const char *wtop_status_position_name(int value);
const char *wtop_meter_style_name(int value);
const char *wtop_character_set_name(int value);
const char *wtop_background_mode_name(int value);
int wtop_parse_background_mode(const char *text, wtop_background_mode *value);
int wtop_parse_footer_style(const char *text, wtop_footer_style *value);
int wtop_parse_status_position(const char *text, wtop_status_position *value);
int wtop_parse_character_set(const char *text, wtop_character_set *value);
int wtop_parse_keymap(const char *text, wtop_keymap *keymap);
int wtop_parse_theme(const char *text, wtop_theme *theme);
int wtop_parse_mouse(const char *text, wtop_mouse *mouse);
int wtop_parse_column(const char *text, int *column);
int wtop_settings_validate(const wtop_settings *settings, const char **error);
int wtop_settings_apply_options(wtop_settings *settings,
                                const wtop_options *options, const char **error);
int wtop_settings_read(FILE *input, wtop_settings *settings,
                       wtop_config_document *document, unsigned long *error_line,
                       const char **error);
int wtop_settings_write(FILE *output, const wtop_settings *settings,
                        const wtop_config_document *document, const char **error);
/* Allocate a complete bounded UTF-8 candidate before any filesystem mutation.
   The caller owns *bytes on success; outputs are unchanged on failure. */
int wtop_settings_serialize(const wtop_settings *settings,
                            const wtop_config_document *document,
                            char **bytes, size_t *length, const char **error);
/* Independent owned copy for export without rebasing an active session. */
int wtop_config_document_clone(wtop_config_document *destination,
                                const wtop_config_document *source, const char **error);
void wtop_config_document_free(wtop_config_document *document);

/* Transactional view edits: failure leaves settings byte-for-byte unchanged.
   Existing IDs move with views; insert/duplicate consume an ID without reuse.
   Duplicate copies recognized settings. Unknown extension records stay associated
   with their original ID in the document and are never silently reassigned. */
int wtop_settings_tab_find(const wtop_settings *settings, unsigned long id);
int wtop_settings_tab_insert(wtop_settings *settings, int index, const char *name, const char **error);
int wtop_settings_tab_duplicate(wtop_settings *settings, int source, int index,
                                 const char *name, const char **error);
int wtop_settings_tab_rename(wtop_settings *settings, int index, const char *name, const char **error);
int wtop_settings_tab_move(wtop_settings *settings, int from, int to, const char **error);
int wtop_settings_tab_delete(wtop_settings *settings, int index, const char **error);
int wtop_settings_tab_set_visible(wtop_settings *settings, int index, int visible, const char **error);

#endif
