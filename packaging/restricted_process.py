#!/usr/bin/env python3
"""Run an owned fixture under a medium-integrity restricted primary token.

This creates no account, service, scheduled task or persistent permission change.
It is a test-only security-context boundary, not a human desktop/UAC review.
The caller owns the returned process and waits for its bounded completion.
The fixture parent needs administrator rights to create its named private window
station; the actual workload still runs as non-administrator at medium integrity.
"""
from __future__ import annotations
import ctypes
from contextlib import contextmanager
import os
from pathlib import Path
import subprocess
import sys
import uuid
from ctypes import wintypes as w


@contextmanager
def _owned_desktop(kernel, security, token, diagnostics):
    """Grant only this user/SYSTEM access to new session-local user objects.

    CreateProcessAsUser(NULL lpDesktop) inherits the caller's user objects.
    Their ACL/integrity can reject the caller's restricted child before Python
    or application code starts (STATUS_DLL_INIT_FAILED). Do not edit the real
    desktop's ACL or silently restore an administrator SID to the child token.
    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw
    https://learn.microsoft.com/windows/win32/secauthz/restricted-tokens
    """
    user = ctypes.WinDLL('user32', use_last_error=True)

    class SECURITY_ATTRIBUTES(ctypes.Structure):
        _fields_ = [('length', w.DWORD), ('descriptor', ctypes.c_void_p), ('inherit', w.BOOL)]

    security.GetTokenInformation.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD)]
    security.ConvertSidToStringSidW.argtypes = [ctypes.c_void_p, ctypes.POINTER(w.LPWSTR)]
    security.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [w.LPCWSTR, w.DWORD, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(w.DWORD)]
    user.GetProcessWindowStation.restype = w.HANDLE
    user.GetThreadDesktop.argtypes = [w.DWORD]; user.GetThreadDesktop.restype = w.HANDLE
    user.SetProcessWindowStation.argtypes = [w.HANDLE]
    user.SetThreadDesktop.argtypes = [w.HANDLE]
    user.CreateWindowStationW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, ctypes.POINTER(SECURITY_ATTRIBUTES)]
    user.CreateWindowStationW.restype = w.HANDLE
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, ctypes.c_void_p, w.DWORD, w.DWORD, ctypes.POINTER(SECURITY_ATTRIBUTES)]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.CloseWindowStation.argtypes = [w.HANDLE]

    def checked(value):
        if not value:
            raise ctypes.WinError(ctypes.get_last_error())
        return value

    needed = w.DWORD()
    security.GetTokenInformation(token, 1, None, 0, ctypes.byref(needed))
    if not needed.value or needed.value > 65536:
        raise RuntimeError('bounded token-user query failed')
    token_user = ctypes.create_string_buffer(needed.value)
    checked(security.GetTokenInformation(token, 1, token_user, len(token_user), ctypes.byref(needed)))
    sid = ctypes.cast(token_user, ctypes.POINTER(ctypes.c_void_p))[0]
    sid_string = w.LPWSTR()
    descriptor = ctypes.c_void_p()
    station = desktop = None
    original_station = checked(user.GetProcessWindowStation())
    original_desktop = checked(user.GetThreadDesktop(kernel.GetCurrentThreadId()))
    name = 'WtopFixture-' + uuid.uuid4().hex
    try:
        checked(security.ConvertSidToStringSidW(sid, ctypes.byref(sid_string)))
        # Explicit medium mandatory label is necessary too: these new objects
        # are created by an elevated parent but used by its medium child.
        sddl = 'D:P(A;;GA;;;' + sid_string.value + ')(A;;GA;;;SY)S:(ML;;NW;;;ME)'
        checked(security.ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1, ctypes.byref(descriptor), None))
        attributes = SECURITY_ATTRIBUTES(ctypes.sizeof(SECURITY_ATTRIBUTES), descriptor, False)
        station = checked(user.CreateWindowStationW(name, 1, 0x10000000, ctypes.byref(attributes)))
        try:
            checked(user.SetProcessWindowStation(station))
            desktop = checked(user.CreateDesktopW('Default', None, None, 0, 0x10000000, ctypes.byref(attributes)))
        finally:
            checked(user.SetProcessWindowStation(original_station))
            if user.GetThreadDesktop(kernel.GetCurrentThreadId()) != original_desktop:
                checked(user.SetThreadDesktop(original_desktop))
        if diagnostics is not None:
            diagnostics['desktop'] = {'isolation': 'owned_noninteractive_window_station',
                'name': name + '\\Default', 'access': 'current_user_and_SYSTEM_only',
                'integrity': 'medium', 'interactive_acl_modified': False,
                'parent_window_station_restored': True}
        yield name + '\\Default'
    finally:
        primary_error = sys.exc_info()[1]
        cleanup_errors = []
        # Each independent owned allocation must be released even if a previous
        # native close failed. Keep the original launch/workload failure too.
        for operation, handle, release in (
            ('CloseDesktop', desktop, user.CloseDesktop),
            ('CloseWindowStation', station, user.CloseWindowStation),
            ('LocalFree(descriptor)', descriptor, kernel.LocalFree),
            ('LocalFree(SID)', ctypes.cast(sid_string, ctypes.c_void_p) if sid_string else None, kernel.LocalFree),
        ):
            if not handle:
                continue
            try:
                result = release(handle)
                if (operation.startswith('LocalFree') and result) or (not operation.startswith('LocalFree') and not result):
                    raise ctypes.WinError(ctypes.get_last_error())
            except BaseException as error:
                cleanup_errors.append(operation + ': ' + str(error))
        if cleanup_errors:
            if diagnostics is not None:
                diagnostics['desktop_cleanup_errors'] = cleanup_errors
            detail = 'restricted desktop cleanup: ' + '; '.join(cleanup_errors)
            if primary_error is not None:
                if hasattr(primary_error, 'add_note'):
                    primary_error.add_note(detail)
                else:
                    primary_error.cleanup_errors = cleanup_errors
            else:
                raise RuntimeError(detail)


def run_restricted(arguments: list[str], cwd: Path, timeout: int = 600, *, diagnostics=None) -> int:
    if os.name != 'nt':
        raise OSError('restricted-token execution requires Windows')
    if not arguments or not 1 <= timeout <= 1800:
        raise ValueError('bounded owned command and timeout required')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    security = ctypes.WinDLL('advapi32', use_last_error=True)

    class SID_AND_ATTRIBUTES(ctypes.Structure):
        _fields_ = [('Sid', ctypes.c_void_p), ('Attributes', w.DWORD)]

    class STARTUPINFO(ctypes.Structure):
        _fields_ = [('cb', w.DWORD), ('lpReserved', w.LPWSTR), ('lpDesktop', w.LPWSTR),
                    ('lpTitle', w.LPWSTR), ('dwX', w.DWORD), ('dwY', w.DWORD),
                    ('dwXSize', w.DWORD), ('dwYSize', w.DWORD), ('dwXCountChars', w.DWORD),
                    ('dwYCountChars', w.DWORD), ('dwFillAttribute', w.DWORD),
                    ('dwFlags', w.DWORD), ('wShowWindow', w.WORD), ('cbReserved2', w.WORD),
                    ('lpReserved2', ctypes.POINTER(ctypes.c_ubyte)), ('hStdInput', w.HANDLE),
                    ('hStdOutput', w.HANDLE), ('hStdError', w.HANDLE)]

    class PROCESS_INFORMATION(ctypes.Structure):
        _fields_ = [('hProcess', w.HANDLE), ('hThread', w.HANDLE),
                    ('dwProcessId', w.DWORD), ('dwThreadId', w.DWORD)]

    kernel.GetCurrentProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.LocalFree.restype = ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
    kernel.GetErrorMode.restype = w.UINT
    kernel.SetErrorMode.argtypes = [w.UINT]
    kernel.SetErrorMode.restype = w.UINT
    security.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, ctypes.POINTER(w.HANDLE)]
    security.ConvertStringSidToSidW.argtypes = [w.LPCWSTR, ctypes.POINTER(ctypes.c_void_p)]
    security.CreateRestrictedToken.argtypes = [w.HANDLE, w.DWORD, w.DWORD,
        ctypes.POINTER(SID_AND_ATTRIBUTES), w.DWORD, ctypes.c_void_p,
        w.DWORD, ctypes.c_void_p, ctypes.POINTER(w.HANDLE)]
    security.SetTokenInformation.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
    security.GetLengthSid.argtypes = [ctypes.c_void_p]
    security.GetLengthSid.restype = w.DWORD
    security.CreateProcessAsUserW.argtypes = [w.HANDLE, w.LPCWSTR, w.LPWSTR,
        ctypes.c_void_p, ctypes.c_void_p, w.BOOL, w.DWORD, ctypes.c_void_p,
        w.LPCWSTR, ctypes.POINTER(STARTUPINFO), ctypes.POINTER(PROCESS_INFORMATION)]
    original = w.HANDLE(); restricted = w.HANDLE()
    admin = ctypes.c_void_p(); medium = ctypes.c_void_p()
    process = PROCESS_INFORMATION()

    def checked(result):
        if not result:
            raise ctypes.WinError(ctypes.get_last_error())

    try:
        # TOKEN_ASSIGN_PRIMARY | DUPLICATE | QUERY | ADJUST_DEFAULT.
        checked(security.OpenProcessToken(kernel.GetCurrentProcess(), 0x8B, ctypes.byref(original)))
        checked(security.ConvertStringSidToSidW('S-1-5-32-544', ctypes.byref(admin)))
        disabled = SID_AND_ATTRIBUTES(admin, 0)
        # DISABLE_MAX_PRIVILEGE; Administrators becomes deny-only.
        checked(security.CreateRestrictedToken(original, 1, 1, ctypes.byref(disabled),
                                                0, None, 0, None, ctypes.byref(restricted)))
        checked(security.ConvertStringSidToSidW('S-1-16-8192', ctypes.byref(medium)))
        integrity = SID_AND_ATTRIBUTES(medium, 0x20)  # SE_GROUP_INTEGRITY
        checked(security.SetTokenInformation(restricted, 25, ctypes.byref(integrity),
            ctypes.sizeof(integrity) + security.GetLengthSid(medium)))
        with _owned_desktop(kernel, security, restricted, diagnostics) as desktop_name:
            startup = STARTUPINFO(); startup.cb = ctypes.sizeof(startup)
            startup.lpDesktop = desktop_name
            command = ctypes.create_unicode_buffer(subprocess.list2cmdline(arguments))
            # A loader/crash/open-file error must produce a process failure rather
            # than an invisible modal dialog in this unattended owned fixture. The
            # child inherits this error mode; the caller's mode is restored even if
            # process creation fails. No machine/user registry setting is changed.
            previous_error_mode = kernel.GetErrorMode()
            kernel.SetErrorMode(previous_error_mode | 0x0001 | 0x0002 | 0x8000)
            try:
                checked(security.CreateProcessAsUserW(restricted, str(Path(arguments[0]).resolve()),
                    command, None, None, False, 0x08000000 | 0x00000400, None,
                    str(cwd.resolve()), ctypes.byref(startup), ctypes.byref(process)))
            finally:
                kernel.SetErrorMode(previous_error_mode)
            if diagnostics is not None:
                diagnostics['child_pid'] = process.dwProcessId
            waited = kernel.WaitForSingleObject(process.hProcess, timeout * 1000)
            if waited == 258:
                # Only this owned test child is terminated. The caller must retain
                # failed MSI logs; no claim is made about transaction retirement.
                kernel.TerminateProcess(process.hProcess, 124)
                retired = kernel.WaitForSingleObject(process.hProcess, 10000)
                if diagnostics is not None:
                    diagnostics['timeout_retirement_observed'] = retired == 0
                raise TimeoutError('restricted fixture exceeded its bound; installer retirement is unverified')
            if waited != 0:
                raise ctypes.WinError(ctypes.get_last_error())
            code = w.DWORD(); checked(kernel.GetExitCodeProcess(process.hProcess, ctypes.byref(code)))
            if diagnostics is not None:
                diagnostics['exit_code'] = code.value
                diagnostics['exit_code_hex'] = f'0x{code.value:08x}'
            return code.value
    finally:
        for handle in (process.hThread, process.hProcess, restricted, original):
            if handle:
                kernel.CloseHandle(handle)
        for sid in (medium, admin):
            if sid:
                kernel.LocalFree(sid)
