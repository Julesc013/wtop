/* Settings behavioral and preservation tests. SPDX-License-Identifier: GPL-3.0-only */
#include "settings.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int read_bytes(const char *bytes, size_t length, wtop_settings *settings,
                      wtop_config_document *document, const char **error)
{
    FILE *file;
    int result;
    unsigned long line;
    file = tmpfile();
    assert(file);
    assert(fwrite(bytes, 1, length, file) == length);
    rewind(file);
    result = wtop_settings_read(file, settings, document, &line, error);
    assert(!fclose(file));
    return result;
}
static int read_text(const char *text, wtop_settings *settings,
                     wtop_config_document *document, const char **error)
{
    return read_bytes(text, strlen(text), settings, document, error);
}
static char *serialize(const wtop_settings *settings, const wtop_config_document *document)
{
    FILE *file;
    char *bytes;
    size_t length;
    const char *error;
    file = tmpfile();
    assert(file);
    assert(wtop_settings_write(file, settings, document, &error));
    assert(!fflush(file));
    rewind(file);
    bytes = (char *)malloc((size_t)WTOP_CONFIG_MAX_BYTES + 1);
    assert(bytes);
    length = fread(bytes, 1, (size_t)WTOP_CONFIG_MAX_BYTES, file);
    bytes[length] = 0;
    assert(!ferror(file));
    assert(!fclose(file));
    {
        char *memory;
        size_t memory_length;
        memory = NULL;
        memory_length = 0;
        assert(wtop_settings_serialize(settings, document, &memory, &memory_length, &error));
        assert(memory_length == length && !memcmp(memory, bytes, length + 1));
        free(memory);
    }
    return bytes;
}

static void defaults_and_overrides(void)
{
    wtop_settings settings;
    wtop_options options;
    const char *error;
    const char *args[] = { "wtop", "--keymap", "emacs", "--theme", "ntop",
        "--mouse", "on", "--no-status-bar", "--interval", "250", "-s", "MEM" };
    wtop_settings_defaults(&settings);
    assert(wtop_settings_validate(&settings, &error));
    assert(settings.keymap == WTOP_KEYMAP_HTOP && settings.theme == WTOP_THEME_HTOP);
    assert(settings.show_function_bar && settings.show_tabs && settings.mouse == WTOP_MOUSE_AUTO);
    assert(settings.colors[WTOP_COLOR_BG] == 0);
    assert(settings.colors[WTOP_COLOR_HEADER] == 0x20);
    assert(settings.colors[WTOP_COLOR_BG_HIGHLIGHT] == 0x30);
    assert(settings.tab_count == 2 && !strcmp(settings.tabs[0].name, "Main"));
    assert(settings.tabs[0].sort_column == WTOP_COLUMN_CPU);
    assert(settings.tabs[1].sort_column == WTOP_COLUMN_IO);
    assert(wtop_parse_options(12, args, &options, &error));
    assert(wtop_settings_apply_options(&settings, &options, &error));
    assert(settings.keymap == WTOP_KEYMAP_EMACS && settings.theme == WTOP_THEME_NTOP);
    assert(settings.mouse == WTOP_MOUSE_ON && !settings.show_function_bar);
    assert(settings.interval_ms == 250 && settings.tabs[0].sort_column == WTOP_COLUMN_MEMORY);
    assert(settings.colors[WTOP_COLOR_BG_HIGHLIGHT] == 0xb0);
}

static void preservation_and_layout(void)
{
    static const char text[] =
        "# Keep the user's explanation.\n"
        "Future.Extension keep this exact inert value ; future-owned comment\n"
        "CPUBarColor 0x5 # custom aggregate color\n"
        "Theme htop\n"
        "Keymap vim\n"
        "Mouse off\n"
        "FunctionBar false\n"
        "ShowTabs 1\n"
        "HeaderRows summary,cpu\n"
        "Tab2.Name \"Disk \\\"queue\\\"\"\n"
        "Tab2.Filter \"test # process\\\\name\" # keep filter note\n"
        "Tab2.Sort DISK\n"
        "Tab2.Columns PROCESS,PID,I/O\n"
        "Tab2.Descending 0\n"
        "Tab2.Tree 1\n"
        "TabCount 3\n"
        "ActiveTab 2\n"
        "Tab0.Filter # keep even this empty-filter comment\n";
    wtop_settings settings;
    wtop_settings reread;
    wtop_config_document document;
    wtop_config_document second_document;
    const char *error;
    char *first;
    char *second;
    memset(&document, 0, sizeof(document));
    memset(&second_document, 0, sizeof(second_document));
    wtop_settings_defaults(&settings);
    assert(read_text(text, &settings, &document, &error));
    assert(settings.colors[WTOP_COLOR_CPU] == 5);
    assert(settings.keymap == WTOP_KEYMAP_VI && settings.mouse == WTOP_MOUSE_OFF);
    assert(!settings.show_function_bar && settings.header_count == 2);
    assert(settings.header_rows[0] == WTOP_ROW_SUMMARY && settings.header_rows[1] == WTOP_ROW_CPU);
    assert(settings.active_tab == 2 && settings.tab_count == 3);
    assert(!strcmp(settings.tabs[2].name, "Disk \"queue\""));
    assert(!strcmp(settings.tabs[2].filter, "test # process\\name"));
    assert(settings.tabs[2].columns[0] == WTOP_COLUMN_PROCESS);
    assert(settings.tabs[2].columns[1] == WTOP_COLUMN_ID);
    assert(settings.tabs[2].column_count == 3 && settings.tabs[2].tree);
    settings.show_function_bar = 1;
    settings.tabs[2].descending = 1;
    first = serialize(&settings, &document);
    assert(strstr(first, "# Keep the user's explanation.\n"));
    assert(strstr(first, "Future.Extension keep this exact inert value ; future-owned comment\n"));
    assert(strstr(first, "CPUBarColor 0x05 # custom aggregate color\n"));
    assert(strstr(first, "Tab0.Filter \"\" # keep even this empty-filter comment\n"));
    assert(strstr(first, "ConfigVersion 2\n"));
    assert(strstr(first, "Tab2.Sort I/O\n"));
    wtop_settings_defaults(&reread);
    assert(read_text(first, &reread, &second_document, &error));
    assert(reread.show_function_bar && reread.tabs[2].descending);
    assert(!strcmp(reread.tabs[2].filter, settings.tabs[2].filter));
    second = serialize(&reread, &second_document);
    assert(!strcmp(first, second));
    free(first);
    free(second);
    wtop_config_document_free(&document);
    wtop_config_document_free(&second_document);
}

static void malformed_is_transactional(void)
{
    static const char *const rejected[] = {
        "ConfigVersion 3\n", "RedrawInterval 49\n", "FGColor 256\n",
        "RedrawInterval 100ms\n", "Keymap unknown\n", "Theme default-script\n",
        "Mouse yes\n", "FunctionBar 2\n", "HeaderRows cpu,cpu\n",
        "HeaderRows cpu,\n", "HeaderRows gpu\n", "TabCount 0\n", "TabCount 9\n",
        "ActiveTab 2\n", "Tab0.Visible 0\n", "Tab0.Columns ID,PID\n",
        "Tab0.Columns ID,\n", "Tab0.Columns unknown\n", "Tab0.Columns none\n",
        "Tab0.Name \"\"\n", "Tab1.Name Main\n", "Tab0.Name \"unterminated\n",
        "Tab0.Filter \"bad\\ncontrol\"\n", "Tab9.Name other\n", "Tab2.Sort CPU%\n",
        "Theme htop\nTHEME ntop\n", "Bad$Key no\n"
    };
    wtop_settings settings;
    wtop_config_document document;
    const char *error;
    char *original;
    size_t i;
    memset(&document, 0, sizeof(document));
    wtop_settings_defaults(&settings);
    assert(read_text("RedrawInterval 777\n# original\n", &settings, &document, &error));
    original = document.original;
    for (i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        assert(!read_text(rejected[i], &settings, &document, &error));
        assert(error && settings.interval_ms == 777);
        assert(document.original == original);
        assert(!strcmp(document.original, "RedrawInterval 777\n# original\n"));
    }
    assert(!read_bytes("# \xc0\xaf\n", 5, &settings, &document, &error));
    assert(!read_bytes("# x\0y", 5, &settings, &document, &error));
    wtop_config_document_free(&document);
    wtop_config_document_free(&document);
}

static void bounds_and_unicode(void)
{
    wtop_settings settings;
    wtop_config_document document;
    const char *error;
    char *text;
    char *result;
    size_t i;
    FILE *output;
    memset(&document, 0, sizeof(document));
    wtop_settings_defaults(&settings);
    assert(read_text("\xef\xbb\xbfTab0.Name \"Pr\xc3\xb6" "cesses\"\nHeaderRows none\n", &settings, &document, &error));
    assert(settings.header_count == 0);
    result = serialize(&settings, &document);
    assert(strstr(result, "Tab0.Name \"Pr\xc3\xb6" "cesses\""));
    free(result);
    text = (char *)malloc((size_t)WTOP_CONFIG_MAX_BYTES + 2);
    assert(text);
    memset(text, '#', WTOP_CONFIG_LINE_BYTES);
    text[WTOP_CONFIG_LINE_BYTES] = 0;
    assert(!read_text(text, &settings, &document, &error));
    for (i = 0; i < (size_t)WTOP_CONFIG_MAX_BYTES; ++i) text[i] = i % 128 == 127 ? '\n' : '#';
    text[WTOP_CONFIG_MAX_BYTES] = 0;
    assert(read_text(text, &settings, &document, &error));
    output = tmpfile();
    assert(output);
    assert(!wtop_settings_write(output, &settings, &document, &error));
    assert(strstr(error, "exceeds"));
    assert(!fclose(output));
    text[WTOP_CONFIG_MAX_BYTES] = '#';
    text[WTOP_CONFIG_MAX_BYTES + 1] = 0;
    assert(!read_text(text, &settings, &document, &error));
    free(text);
    memset(settings.tabs[0].name, 'x', sizeof(settings.tabs[0].name));
    assert(!wtop_settings_validate(&settings, &error));
    wtop_config_document_free(&document);
}

static void stable_view_identity(void)
{
    wtop_settings settings;
    wtop_settings reread;
    wtop_config_document document;
    wtop_config_document next_document;
    const char *error;
    char *bytes;
    char *again;
    unsigned long left;
    unsigned long right;
    memset(&document, 0, sizeof(document));
    memset(&next_document, 0, sizeof(next_document));
    wtop_settings_defaults(&settings);
    assert(read_text("ConfigVersion 1\nTab0.Future owner-left # left\n"
                     "  Tab1.Future owner-right # right\nTab1.Name Work # work name\n",
                     &settings, &document, &error));
    left = settings.tabs[0].id;
    right = settings.tabs[1].id;
    settings.active_tab = 1;
    assert(wtop_settings_tab_move(&settings, 0, 1, &error));
    assert(settings.tabs[0].id == right && settings.tabs[1].id == left);
    assert(settings.active_tab == 0);
    bytes = serialize(&settings, &document);
    assert(strstr(bytes, "Tab1.Future owner-left # left\n"));
    assert(strstr(bytes, "  Tab0.Future owner-right # right\n"));
    assert(strstr(bytes, "Tab0.Name \"Work\" # work name\n"));
    wtop_settings_defaults(&reread);
    assert(read_text(bytes, &reread, &next_document, &error));
    assert(reread.tabs[0].id == right && reread.tabs[1].id == left);
    again = serialize(&reread, &next_document);
    assert(!strcmp(bytes, again));
    free(bytes);
    free(again);
    wtop_config_document_free(&next_document);
    assert(wtop_settings_tab_delete(&settings, 1, &error));
    assert(wtop_settings_tab_insert(&settings, 1, "New", &error));
    assert(settings.tabs[1].id != left);
    bytes = serialize(&settings, &document);
    assert(strstr(bytes, "# Removed view 1; original extension follows:\n# Tab0.Future owner-left # left\n"));
    assert(!strstr(bytes, "\nTab1.Future owner-left"));
    wtop_settings_defaults(&reread);
    assert(read_text(bytes, &reread, &next_document, &error));
    assert(reread.tabs[1].id == settings.tabs[1].id);
    free(bytes);
    wtop_config_document_free(&document);
    wtop_config_document_free(&next_document);
}

static void view_crud_and_capacity(void)
{
    wtop_settings settings;
    wtop_settings before;
    const char *error;
    char name[32];
    unsigned long original;
    int i;
    wtop_settings_defaults(&settings);
    original = settings.tabs[1].id;
    assert(wtop_settings_tab_duplicate(&settings, 1, 0, "Clone", &error));
    assert(settings.tabs[0].id != original && settings.tabs[0].sort_column == WTOP_COLUMN_IO);
    assert(settings.active_tab == 1);
    assert(wtop_settings_tab_rename(&settings, 0, "Copy", &error));
    before = settings;
    assert(!wtop_settings_tab_rename(&settings, 0, "MAIN", &error));
    assert(!memcmp(&before, &settings, sizeof(settings)));
    assert(wtop_settings_tab_delete(&settings, 0, &error));
    assert(settings.active_tab == 0 && settings.tabs[1].id == original);
    assert(wtop_settings_tab_set_visible(&settings, 0, 0, &error));
    assert(settings.active_tab == 1);
    before = settings;
    assert(!wtop_settings_tab_set_visible(&settings, 1, 0, &error));
    assert(!memcmp(&before, &settings, sizeof(settings)));
    assert(!wtop_settings_tab_delete(&settings, 1, &error));
    assert(!memcmp(&before, &settings, sizeof(settings)));
    assert(wtop_settings_tab_set_visible(&settings, 0, 1, &error));
    for (i = settings.tab_count; i < WTOP_MAX_TABS; ++i) {
        sprintf(name, "Extra %d", i);
        assert(wtop_settings_tab_insert(&settings, settings.tab_count, name, &error));
    }
    before = settings;
    assert(!wtop_settings_tab_insert(&settings, settings.tab_count, "Overflow", &error));
    assert(!memcmp(&before, &settings, sizeof(settings)));
    wtop_settings_defaults(&settings);
    settings.next_tab_id = WTOP_TAB_ID_MAX;
    assert(wtop_settings_tab_insert(&settings, 2, "Last identity", &error));
    assert(settings.tabs[2].id == WTOP_TAB_ID_MAX && settings.next_tab_id == 0);
    assert(wtop_settings_tab_delete(&settings, 2, &error));
    before = settings;
    assert(!wtop_settings_tab_insert(&settings, 2, "No reuse", &error));
    assert(!memcmp(&before, &settings, sizeof(settings)));
}

static void view_schema_validation(void)
{
    static const char *const rejected[] = {
        "ConfigVersion 2\n",
        "ConfigVersion 2\nNextTabId 3\nTab0.Id 1\nTab1.Id 1\n",
        "ConfigVersion 2\nNextTabId 2\nTab0.Id 1\nTab1.Id 2\n",
        "ConfigVersion 2\nNextTabId 4294967296\nTab0.Id 1\nTab1.Id 2\n",
        "ConfigVersion 2\nNextTabId 3\nTab0.Id 0\nTab1.Id 2\n",
        "ConfigVersion 1\nTab0.Id 1\n",
        "NextTabId 3\n"
    };
    wtop_settings settings;
    wtop_settings before;
    wtop_config_document document;
    const char *error;
    size_t i;
    memset(&document, 0, sizeof(document));
    wtop_settings_defaults(&settings);
    before = settings;
    for (i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        assert(!read_text(rejected[i], &settings, &document, &error));
        assert(!memcmp(&before, &settings, sizeof(settings)) && !document.original);
    }
    assert(read_text("ConfigVersion 02\nNextTabId 43\nTab0.Id 42\nTab1.Id 8\n",
                     &settings, &document, &error));
    assert(document.schema_version == 2 && settings.tabs[0].id == 42);
    wtop_config_document_free(&document);
}

static void orphan_extension_is_not_rebound(void)
{
    wtop_settings settings;
    wtop_config_document document = {0};
    const char *error;
    char *bytes;
    size_t length;
    wtop_settings_defaults(&settings);
    assert(read_text("Tab7.Future unknown original owner\n", &settings, &document, &error));
    bytes = NULL; length = 71;
    assert(!wtop_settings_serialize(&settings, &document, &bytes, &length, &error));
    assert(!bytes && length == 71 && strstr(error, "outside the original view set"));
    assert(!strcmp(document.original, "Tab7.Future unknown original owner\n"));
    wtop_config_document_free(&document);
}

static void document_clone_is_independent(void)
{
    wtop_settings settings;
    wtop_config_document source = {0};
    wtop_config_document copy = {0};
    wtop_config_document invalid;
    const char *error;
    char *original;
    wtop_settings_defaults(&settings);
    assert(read_text("# preserved\nTab0.Future original owner\n", &settings, &source, &error));
    assert(wtop_config_document_clone(&copy, &source, &error));
    assert(copy.original != source.original && !strcmp(copy.original, source.original));
    assert(copy.schema_version == source.schema_version && copy.tab_ids[0] == source.tab_ids[0]);
    invalid = source; invalid.schema_version = WTOP_CONFIG_VERSION + 1;
    original = copy.original;
    assert(!wtop_config_document_clone(&copy, &invalid, &error));
    assert(copy.original == original && !strcmp(copy.original, source.original));
    wtop_config_document_free(&source);
    assert(!strcmp(copy.original, "# preserved\nTab0.Future original owner\n"));
    assert(wtop_config_document_clone(&copy, &copy, &error));
    assert(wtop_config_document_clone(&copy, &source, &error));
    assert(!copy.original && !copy.length && !copy.schema_version);
    wtop_config_document_free(&copy);
}

static void beta2_presentation(void)
{
    wtop_settings settings, copy;
    wtop_config_document document = {0}, other = {0};
    const char *error;
    char *serialized;
    wtop_settings_defaults(&settings);
    assert(settings.footer_style==WTOP_FOOTER_FUNCTIONS && settings.status_position==WTOP_STATUS_BANNER);
    assert(settings.frame_interval_ms==50 && settings.meter_width==25);
    assert(read_text("ConfigVersion 2\nNextTabId 9\nTab0.Id 1\nTab1.Id 2\nFooterStyle compact\nStatusPosition bottom\nFrameInterval 80\nMeterWidth 36\nMeterStyle blocks\nCharacterSet ascii\n", &settings, &document, &error));
    assert(settings.footer_style==WTOP_FOOTER_COMPACT && settings.status_position==WTOP_STATUS_BOTTOM);
    assert(settings.frame_interval_ms==80 && settings.meter_width==36);
    assert(settings.meter_style==WTOP_METER_BLOCKS && settings.character_set==WTOP_CHARSET_ASCII);
    serialized = NULL;
    {
        size_t length=0;
        assert(wtop_settings_serialize(&settings,&document,&serialized,&length,&error));
        assert(length && strstr(serialized,"FooterStyle compact"));
    }
    wtop_settings_defaults(&copy);
    assert(read_text(serialized,&copy,&other,&error));
    assert(copy.footer_style==settings.footer_style && copy.meter_width==settings.meter_width);
    assert(!read_text("FrameInterval 0\n",&copy,&other,&error));
    assert(copy.frame_interval_ms==80);
    assert(!read_text("MeterWidth 32767\n",&copy,&other,&error));
    assert(!read_text("StatusPosition automatic\n",&copy,&other,&error));
    free(serialized); wtop_config_document_free(&document); wtop_config_document_free(&other);
}

static void semantic_row_colors_and_migration(void)
{
    wtop_settings settings, copy;
    wtop_config_document document = {0}, reread = {0};
    const char *error;
    char *text;
    wtop_settings_defaults(&settings);
    assert(settings.background_mode == WTOP_BACKGROUND_TERMINAL);
    assert(settings.colors[WTOP_COLOR_FG_FOCUS] == 15 && settings.colors[WTOP_COLOR_BG_FOCUS] == 8);
    assert(settings.colors[WTOP_COLOR_FG_SELECTED] == 0 && settings.colors[WTOP_COLOR_BG_SELECTED] == 3);
    assert(read_text("BGColor 0x40\nBGHighlightColor 0x6e\n", &settings, &document, &error));
    assert(settings.background_mode == WTOP_BACKGROUND_PALETTE);
    assert(settings.colors[WTOP_COLOR_FG_SELECTED] == 14 && settings.colors[WTOP_COLOR_BG_SELECTED] == 6);
    assert(read_text("SelectionForeground 2\nBackgroundMode terminal\nBGColor 0x20\nSelectionBackground 0\nBGHighlightColor 0x7f\n", &settings, &document, &error));
    assert(settings.background_mode == WTOP_BACKGROUND_TERMINAL);
    assert(settings.colors[WTOP_COLOR_FG_SELECTED] == 2 && settings.colors[WTOP_COLOR_BG_SELECTED] == 0);
    settings.colors[WTOP_COLOR_FG_FOCUS] = 4; settings.colors[WTOP_COLOR_BG_FOCUS] = 7;
    text = serialize(&settings, &document);
    wtop_settings_defaults(&copy);
    assert(read_text(text, &copy, &reread, &error));
    assert(copy.background_mode == WTOP_BACKGROUND_TERMINAL);
    assert(copy.colors[WTOP_COLOR_FG_FOCUS] == 4 && copy.colors[WTOP_COLOR_BG_FOCUS] == 7);
    assert(copy.colors[WTOP_COLOR_FG_SELECTED] == 2 && copy.colors[WTOP_COLOR_BG_SELECTED] == 0);
    assert(!read_text("FocusBackground 16\n", &copy, &reread, &error));
    assert(!read_text("SelectionForeground 0x30\n", &copy, &reread, &error));
    assert(!read_text("BackgroundMode transparent\n", &copy, &reread, &error));
    assert(copy.colors[WTOP_COLOR_FG_FOCUS] == 4 && copy.background_mode == WTOP_BACKGROUND_TERMINAL);
    assert(strstr(text, "BackgroundMode terminal") && !strstr(text, "SelectedPid") && !strstr(text, "FocusedPid"));
    free(text); wtop_config_document_free(&document); wtop_config_document_free(&reread);
}

int main(void)
{
    semantic_row_colors_and_migration();
    beta2_presentation();
    defaults_and_overrides();
    preservation_and_layout();
    malformed_is_transactional();
    bounds_and_unicode();
    stable_view_identity();
    view_crud_and_capacity();
    view_schema_validation();
    orphan_extension_is_not_rebound();
    document_clone_is_independent();
    puts("settings: defaults, overrides, layout, preservation, UTF-8 and transactional bounds passed");
    return 0;
}
