/* Native OS/2 VIO loader/source spike. SDK headers select the 16/32-bit ABI.
 * SPDX-License-Identifier: GPL-3.0-only */
#define INCL_DOSMISC
#define INCL_DOSMODULEMGR
#define INCL_VIO
#include <os2.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "probe.h"

static wtop_probe_report report;
int main(void)
{
    VIOMODEINFO mode;
    unsigned long status;
    int ok;
#if defined(__386__)
    ULONG version[3];
    ok = wtop_probe_init(&report, "os2-32-x86", "LX");
    memset(version, 0, sizeof(version));
    status = DosQuerySysInfo(QSV_VERSION_MAJOR, QSV_VERSION_REVISION, version, sizeof(version));
    if (!status) {
        ok = ok && wtop_probe_add_number(&report, "os2.version.major_raw", WTOP_PROBE_OBSERVED, 0, version[0]);
        ok = ok && wtop_probe_add_number(&report, "os2.version.minor_raw", WTOP_PROBE_OBSERVED, 0, version[1]);
        ok = ok && wtop_probe_add_number(&report, "os2.version.revision_raw", WTOP_PROBE_OBSERVED, 0, version[2]);
    } else ok = ok && wtop_probe_add(&report, "os2.version", WTOP_PROBE_FAILED, status, "DosQuerySysInfo-failed");
#else
    USHORT version;
    ok = wtop_probe_init(&report, "os2-16-x86", "NE");
    version = 0;
    status = DosGetVersion(&version);
    if (!status) ok = ok && wtop_probe_add_number(&report, "os2.version.raw", WTOP_PROBE_OBSERVED, 0, version);
    else ok = ok && wtop_probe_add(&report, "os2.version", WTOP_PROBE_FAILED, status, "DosGetVersion-failed");
#endif
    ok = ok && wtop_probe_add_number(&report, "c.int_bits", WTOP_PROBE_OBSERVED, 0, sizeof(int) * CHAR_BIT);
    ok = ok && wtop_probe_add_number(&report, "c.pointer_bits", WTOP_PROBE_OBSERVED, 0, sizeof(void *) * CHAR_BIT);
    memset(&mode, 0, sizeof(mode)); mode.cb = sizeof(mode);
    status = VioGetMode(&mode, 0);
    if (!status) {
        ok = ok && wtop_probe_add_number(&report, "vio.rows", WTOP_PROBE_OBSERVED, 0, mode.row);
        ok = ok && wtop_probe_add_number(&report, "vio.columns", WTOP_PROBE_OBSERVED, 0, mode.col);
    } else ok = ok && wtop_probe_add(&report, "vio.mode", WTOP_PROBE_FAILED, status, "VioGetMode-failed");
    ok = ok && wtop_probe_add(&report, "process.cpu", WTOP_PROBE_UNSUPPORTED, 0, "no-provider");
    return ok && wtop_probe_write(&report, stdout) && !fflush(stdout) ? 0 : 2;
}
