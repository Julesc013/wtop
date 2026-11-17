/* Win3.x NE loader/source spike. Requires its real SDK/CRT and native execution.
 * No message window, GUI feature, console emulation or task monitor is supplied.
 * SPDX-License-Identifier: GPL-3.0-only */
#include <windows.h>
#include <limits.h>
#include <string.h>
#include "probe.h"

static wtop_probe_report report;
static char output[WTOP_PROBE_JSON_BYTES];
static char path[256];
int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    HFILE file;
    HMODULE toolhelp;
    UINT length, written;
    int ok;
    (void)instance; (void)previous; (void)show;
    /* Calling convention belongs to this entrypoint; there are no callbacks or
     * persistent workers. Output is explicit because Win16 has no Win32 console. */
    if (!wtop_probe_output_path(command_line, path, sizeof(path))) return 2;
    ok = wtop_probe_init(&report, "win16-3-x86", "NE");
    ok = ok && wtop_probe_add_number(&report, "c.int_bits", WTOP_PROBE_OBSERVED, 0, sizeof(int) * CHAR_BIT);
    ok = ok && wtop_probe_add_number(&report, "c.pointer_bits", WTOP_PROBE_OBSERVED, 0, sizeof(void *) * CHAR_BIT);
    ok = ok && wtop_probe_add_number(&report, "windows.GetVersion.raw", WTOP_PROBE_OBSERVED, 0, GetVersion());
    toolhelp = GetModuleHandle("TOOLHELP");
    ok = ok && wtop_probe_add(&report, "toolhelp.loaded_module", toolhelp ? WTOP_PROBE_PRESENT : WTOP_PROBE_UNAVAILABLE,
                              0, toolhelp ? "loaded-module-only" : "not-loaded");
    ok = ok && wtop_probe_add(&report, "toolhelp.TaskFirst.export",
        toolhelp && GetProcAddress(toolhelp, "TaskFirst") ? WTOP_PROBE_PRESENT : WTOP_PROBE_UNAVAILABLE,
        0, "export-address-only-not-invoked");
    ok = ok && wtop_probe_add(&report, "process.cpu", WTOP_PROBE_UNSUPPORTED, 0, "no-provider");
    ok = ok && wtop_probe_add(&report, "text_grid.host", WTOP_PROBE_UNSUPPORTED, 0, "not-implemented");
    if (!ok || !wtop_probe_json(&report, output, sizeof(output))) return 3;
    /* This dedicated probe explicitly creates/replaces exactly --output PATH;
     * ordinary Wtop settings semantics are not delegated to this primitive. */
    file = _lcreat(path, 0);
    if (file == HFILE_ERROR) return 4;
    length = (UINT)strlen(output);
    written = _lwrite(file, output, length);
    ok = _lclose(file) != HFILE_ERROR;
    return written == length && ok ? 0 : 5;
}
