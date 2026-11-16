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
    assert(strstr(first, "ConfigVersion 1\n"));
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
        "ConfigVersion 2\n", "RedrawInterval 49\n", "FGColor 256\n",
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

int main(void)
{
    defaults_and_overrides();
    preservation_and_layout();
    malformed_is_transactional();
    bounds_and_unicode();
    puts("settings: defaults, overrides, layout, preservation, UTF-8 and transactional bounds passed");
    return 0;
}
