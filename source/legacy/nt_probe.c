/* NT environment observation for named WinPE/ReactOS/NT images. Export presence
 * is NOT a working-provider claim. This is a separate probe, never the monitor.
 * SPDX-License-Identifier: GPL-3.0-only */
#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "probe.h"
#ifndef WTOP_PROBE_PROFILE
#error Supply WTOP_PROBE_PROFILE from spec/catalog/targets.json via legacy_probe.py
#endif

typedef LONG (WINAPI *rtl_version_fn)(OSVERSIONINFOW *);
static wtop_probe_report report;
static int export_probe(HMODULE module, const char *id, const char *symbol)
{
    return wtop_probe_add(&report, id,
        module && GetProcAddress(module, symbol) ? WTOP_PROBE_PRESENT : WTOP_PROBE_UNAVAILABLE,
        0, "export-address-only-not-invoked");
}
static int registry_string(const WCHAR *key, const WCHAR *name, const char *id)
{
    HKEY handle;
    WCHAR wide[WTOP_PROBE_VALUE_BYTES];
    char utf8[WTOP_PROBE_VALUE_BYTES];
    DWORD type = 0, bytes = sizeof(wide);
    LONG code = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE, &handle);
    if (code != ERROR_SUCCESS) return wtop_probe_add(&report, id,
        code == ERROR_ACCESS_DENIED ? WTOP_PROBE_DENIED : WTOP_PROBE_UNAVAILABLE, code, "registry-key-unavailable");
    code = RegQueryValueExW(handle, name, NULL, &type, (LPBYTE)wide, &bytes);
    RegCloseKey(handle);
    if (code != ERROR_SUCCESS) return wtop_probe_add(&report, id,
        code == ERROR_ACCESS_DENIED ? WTOP_PROBE_DENIED : WTOP_PROBE_UNAVAILABLE, code, "registry-value-unavailable");
    if (type != REG_SZ || bytes < sizeof(WCHAR) || bytes > sizeof(wide) || bytes % sizeof(WCHAR) || wide[bytes / sizeof(WCHAR) - 1])
        return wtop_probe_add(&report, id, WTOP_PROBE_FAILED, ERROR_INVALID_DATA, "invalid-registry-string");
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), NULL, NULL))
        return wtop_probe_add(&report, id, WTOP_PROBE_FAILED, GetLastError(), "UTF8-conversion-or-capacity-failed");
    return wtop_probe_add(&report, id, WTOP_PROBE_OBSERVED, 0, utf8);
}
int main(void)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll"), ntdll = GetModuleHandleW(L"ntdll.dll");
    rtl_version_fn version_api = ntdll ? (rtl_version_fn)(void *)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    OSVERSIONINFOW version;
    WCHAR module[WTOP_PROBE_VALUE_BYTES];
    char utf8[WTOP_PROBE_VALUE_BYTES];
    DWORD length, mode, code;
    HKEY marker;
    LONG status;
    int ok;
#if defined(_WIN64)
    ok = wtop_probe_init(&report, WTOP_PROBE_PROFILE, "PE32+");
#else
    ok = wtop_probe_init(&report, WTOP_PROBE_PROFILE, "PE32");
#endif
    ok = ok && wtop_probe_add_number(&report, "c.int_bits", WTOP_PROBE_OBSERVED, 0, sizeof(int) * CHAR_BIT);
    ok = ok && wtop_probe_add_number(&report, "c.pointer_bits", WTOP_PROBE_OBSERVED, 0, sizeof(void *) * CHAR_BIT);
    length = GetModuleFileNameW(NULL, module, sizeof(module) / sizeof(module[0]));
    if (length && length < sizeof(module) / sizeof(module[0]) &&
        WideCharToMultiByte(CP_UTF8, 0, module, -1, utf8, sizeof(utf8), NULL, NULL))
        ok = ok && wtop_probe_add(&report, "module.actual_path", WTOP_PROBE_OBSERVED, 0, utf8);
    else ok = ok && wtop_probe_add(&report, "module.actual_path", WTOP_PROBE_FAILED, GetLastError(), "path-query-or-capacity-failed");
    memset(&version, 0, sizeof(version)); version.dwOSVersionInfoSize = sizeof(version);
    status = version_api ? version_api(&version) : (LONG)0xc0000002UL;
    if (!status) {
        ok = ok && wtop_probe_add_number(&report, "nt.reported_major", WTOP_PROBE_OBSERVED, 0, version.dwMajorVersion);
        ok = ok && wtop_probe_add_number(&report, "nt.reported_minor", WTOP_PROBE_OBSERVED, 0, version.dwMinorVersion);
        ok = ok && wtop_probe_add_number(&report, "nt.reported_build", WTOP_PROBE_OBSERVED, 0, version.dwBuildNumber);
    } else ok = ok && wtop_probe_add(&report, "nt.reported_version", WTOP_PROBE_UNAVAILABLE, (unsigned long)status, "RtlGetVersion-unavailable-or-failed");
    status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MiniNT", 0, KEY_READ, &marker);
    if (!status) RegCloseKey(marker);
    ok = ok && wtop_probe_add(&report, "winpe.MiniNT_registry_marker",
        !status ? WTOP_PROBE_PRESENT : status == ERROR_ACCESS_DENIED ? WTOP_PROBE_DENIED : WTOP_PROBE_UNAVAILABLE,
        status, "marker-observation-not-platform-qualification");
    ok = ok && registry_string(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName", "nt.registry.ProductName");
    ok = ok && registry_string(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"BuildLabEx", "nt.registry.BuildLabEx");
    ok = ok && export_probe(kernel, "kernel.GetSystemTimes.export", "GetSystemTimes");
    ok = ok && export_probe(kernel, "kernel.GetTickCount64.export", "GetTickCount64");
    ok = ok && export_probe(kernel, "kernel.GetFinalPathNameByHandleW.export", "GetFinalPathNameByHandleW");
    ok = ok && export_probe(kernel, "kernel.QueryFullProcessImageNameW.export", "QueryFullProcessImageNameW");
    ok = ok && export_probe(ntdll, "nt.NtQuerySystemInformation.export", "NtQuerySystemInformation");
    if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode))
        ok = ok && wtop_probe_add_number(&report, "stdout.console_mode", WTOP_PROBE_OBSERVED, 0, mode);
    else { code = GetLastError(); ok = ok && wtop_probe_add(&report, "stdout.console_mode", WTOP_PROBE_UNAVAILABLE, code, "redirected-or-unavailable"); }
    ok = ok && wtop_probe_add(&report, "process.cpu", WTOP_PROBE_UNSUPPORTED, 0, "no-provider-in-loader-probe");
    return ok && wtop_probe_write(&report, stdout) && !fflush(stdout) ? 0 : 2;
}
