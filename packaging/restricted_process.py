#!/usr/bin/env python3
"""Run an owned fixture under a medium-integrity restricted primary token.

This creates no account, service, scheduled task or persistent permission change.
It is a test-only security-context boundary, not a human desktop/UAC review.
The caller owns the returned process and waits for its bounded completion.
"""
from __future__ import annotations
import ctypes
import os
from pathlib import Path
import subprocess
from ctypes import wintypes as w


def run_restricted(arguments: list[str], cwd: Path, timeout: int = 600) -> int:
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
    kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
    kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
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
        startup = STARTUPINFO(); startup.cb = ctypes.sizeof(startup)
        command = ctypes.create_unicode_buffer(subprocess.list2cmdline(arguments))
        checked(security.CreateProcessAsUserW(restricted, str(Path(arguments[0]).resolve()),
            command, None, None, False, 0x08000000 | 0x00000400, None,
            str(cwd.resolve()), ctypes.byref(startup), ctypes.byref(process)))
        waited = kernel.WaitForSingleObject(process.hProcess, timeout * 1000)
        if waited == 258:
            # Only this owned test child is terminated. The caller must retain
            # failed MSI logs; no claim is made about transaction retirement.
            kernel.TerminateProcess(process.hProcess, 124)
            kernel.WaitForSingleObject(process.hProcess, 10000)
            raise TimeoutError('restricted fixture exceeded its bound; installer retirement is unverified')
        if waited != 0:
            raise ctypes.WinError(ctypes.get_last_error())
        code = w.DWORD(); checked(kernel.GetExitCodeProcess(process.hProcess, ctypes.byref(code)))
        return code.value
    finally:
        for handle in (process.hThread, process.hProcess, restricted, original):
            if handle:
                kernel.CloseHandle(handle)
        for sid in (medium, admin):
            if sid:
                kernel.LocalFree(sid)
