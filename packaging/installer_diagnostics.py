"""Bounded, non-mutating observations for native Installer fixture failures.

Existing MSI mutexes are opened and closed only: never acquired, created,
deleted or granted different access. Separate randomly named objects test this
process's ability to create/open a mutex in each namespace. These diagnostics
do not qualify installation or replace the actual lifecycle assertions.

https://learn.microsoft.com/windows/win32/termserv/kernel-object-namespaces
https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-openmutexw
https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createmutexw
"""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import os
import uuid


class _SecurityDescriptor(C.Structure):
    _fields_ = [("revision", C.c_ubyte), ("reserved", C.c_ubyte),
                ("control", W.WORD), ("owner", C.c_void_p), ("group", C.c_void_p),
                ("sacl", C.c_void_p), ("dacl", C.c_void_p)]


class _Luid(C.Structure):
    _fields_ = [("low", C.c_uint32), ("high", C.c_int32)]


class _TokenStatistics(C.Structure):
    _fields_ = [("token_id", _Luid), ("authentication_id", _Luid),
                ("expiration", C.c_int64), ("token_type", C.c_uint32),
                ("impersonation_level", C.c_uint32), ("dynamic_charged", C.c_uint32),
                ("dynamic_available", C.c_uint32), ("groups", C.c_uint32),
                ("privileges", C.c_uint32), ("modified_id", _Luid)]


def _observed(operation):
    """Keep a denied/unavailable observation distinct from a successful one."""
    try:
        return {"ok": True, "value": operation()}
    except (OSError, ValueError, RuntimeError) as error:
        return {"ok": False, "error": str(error),
                "winerror": getattr(error, "winerror", None),
                "errno": getattr(error, "errno", None)}


def _default_acl(buffer, returned):
    """Validate the embedded TOKEN_DEFAULT_DACL pointer before native use."""
    if returned < C.sizeof(C.c_void_p) or returned > len(buffer):
        raise RuntimeError("default DACL information has an invalid length")
    address = C.c_void_p.from_buffer(buffer).value
    if address is None:
        return None
    start = C.addressof(buffer)
    if not start + C.sizeof(C.c_void_p) <= address <= start + returned - 8:
        raise RuntimeError("default DACL points outside its token buffer")
    length = int.from_bytes(C.string_at(address + 2, 2), "little")
    if length < 8 or address + length > start + returned:
        raise RuntimeError("default DACL is truncated")
    return address


class _Context:
    def __init__(self):
        self.k = C.WinDLL("kernel32", use_last_error=True)
        self.a = C.WinDLL("advapi32", use_last_error=True)
        self.u = C.WinDLL("user32", use_last_error=True)
        self.cleanup_errors = []

        def bind(lib, name, arguments, result=W.BOOL):
            fn = getattr(lib, name); fn.argtypes, fn.restype = arguments, result

        bind(self.k, "GetCurrentProcess", [], W.HANDLE)
        bind(self.k, "GetCurrentThreadId", [], W.DWORD)
        bind(self.k, "ProcessIdToSessionId", [W.DWORD, C.POINTER(W.DWORD)])
        bind(self.k, "OpenMutexW", [W.DWORD, W.BOOL, W.LPCWSTR], W.HANDLE)
        bind(self.k, "CreateMutexW", [C.c_void_p, W.BOOL, W.LPCWSTR], W.HANDLE)
        bind(self.k, "CloseHandle", [W.HANDLE])
        bind(self.k, "LocalFree", [C.c_void_p], C.c_void_p)
        bind(self.a, "OpenProcessToken", [W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)])
        bind(self.a, "GetTokenInformation", [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)])
        bind(self.a, "GetKernelObjectSecurity", [W.HANDLE, W.DWORD, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)])
        bind(self.a, "InitializeSecurityDescriptor", [C.c_void_p, W.DWORD])
        bind(self.a, "SetSecurityDescriptorDacl", [C.c_void_p, W.BOOL, C.c_void_p, W.BOOL])
        bind(self.a, "ConvertSecurityDescriptorToStringSecurityDescriptorW",
             [C.c_void_p, W.DWORD, W.DWORD, C.POINTER(W.LPWSTR), C.POINTER(W.DWORD)])
        bind(self.u, "GetProcessWindowStation", [], W.HANDLE)
        bind(self.u, "GetThreadDesktop", [W.DWORD], W.HANDLE)
        bind(self.u, "GetUserObjectInformationW", [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)])

    @staticmethod
    def checked(value):
        if not value:
            raise C.WinError(C.get_last_error())
        return value

    def close(self, handle):
        if handle and not self.k.CloseHandle(handle):
            self.cleanup_errors.append({"operation": "CloseHandle", "winerror": C.get_last_error()})

    def query(self, operation):
        # Two calls, no retry loop; races and an oversized response stay visible.
        needed = W.DWORD()
        C.set_last_error(0)
        if operation(None, 0, C.byref(needed)) or C.get_last_error() != 122:
            raise C.WinError(C.get_last_error())
        if not 1 <= needed.value <= 65536:
            raise RuntimeError("diagnostic query exceeds its 65536-byte bound")
        buffer = C.create_string_buffer(needed.value)
        self.checked(operation(buffer, len(buffer), C.byref(needed)))
        if not 1 <= needed.value <= len(buffer):
            raise RuntimeError("diagnostic query returned an invalid length")
        return buffer, needed.value

    def sddl(self, descriptor, information):
        value = W.LPWSTR()
        try:
            self.checked(self.a.ConvertSecurityDescriptorToStringSecurityDescriptorW(
                descriptor, 1, information, C.byref(value), None))
            return value.value
        finally:
            if value and self.k.LocalFree(C.cast(value, C.c_void_p)):
                self.cleanup_errors.append({"operation": "LocalFree(SDDL)", "winerror": C.get_last_error()})

    def object_security(self, handle):
        # Owner/DACL/mandatory label, not SACL auditing information.
        information = 0x15
        buffer, _ = self.query(lambda data, size, needed:
            self.a.GetKernelObjectSecurity(handle, information, data, size, needed))
        return self.sddl(buffer, information)

    def session(self):
        session = W.DWORD()
        self.checked(self.k.ProcessIdToSessionId(os.getpid(), C.byref(session)))
        return session.value

    def user_object_name(self, handle):
        self.checked(handle)  # Borrowed process/thread user objects; do not close.
        buffer, returned = self.query(lambda data, size, needed:
            self.u.GetUserObjectInformationW(handle, 2, data, size, needed))
        if returned % C.sizeof(C.c_wchar):
            raise RuntimeError("user-object name has an invalid wide-string size")
        return C.wstring_at(C.addressof(buffer), returned // C.sizeof(C.c_wchar)).rstrip("\0")

    def token(self):
        token = W.HANDLE()
        try:
            self.checked(self.a.OpenProcessToken(self.k.GetCurrentProcess(), 8, C.byref(token)))
            session = C.c_uint32(); needed = W.DWORD()
            self.checked(self.a.GetTokenInformation(token, 12, C.byref(session), 4, C.byref(needed)))
            if needed.value != 4:
                raise RuntimeError("token session ID has an invalid size")
            statistics = _TokenStatistics()
            self.checked(self.a.GetTokenInformation(token, 10, C.byref(statistics), C.sizeof(statistics), C.byref(needed)))
            if needed.value != C.sizeof(statistics):
                raise RuntimeError("token statistics have an invalid size")
            buffer, returned = self.query(lambda data, size, length:
                self.a.GetTokenInformation(token, 6, data, size, length))
            acl = _default_acl(buffer, returned)
            dacl_text = None
            if acl is not None:
                descriptor = _SecurityDescriptor()
                self.checked(self.a.InitializeSecurityDescriptor(C.byref(descriptor), 1))
                self.checked(self.a.SetSecurityDescriptorDacl(C.byref(descriptor), True, acl, False))
                dacl_text = self.sddl(C.byref(descriptor), 4)
            luid = statistics.authentication_id
            return {"session_id": session.value,
                    "authentication_id": f"{luid.high & 0xffffffff:08x}:{luid.low:08x}",
                    "default_dacl": dacl_text,
                    "null_default_dacl": acl is None}
        finally:
            self.close(token)

    def mutex(self, name, access):
        handle = self.checked(self.k.OpenMutexW(access, False, name))
        try:
            return {"opened": True, "security": _observed(lambda: self.object_security(handle))} if access & 0x20000 else {"opened": True}
        finally:
            self.close(handle)

    def owned_mutex(self, prefix):
        name = prefix + "WtopDiagnostic-" + uuid.uuid4().hex
        C.set_last_error(0)
        handle = self.checked(self.k.CreateMutexW(None, False, name))
        existed = C.get_last_error() == 183
        try:
            if existed:
                raise RuntimeError("random diagnostic mutex unexpectedly already exists")
            return {"name": name, "created": True,
                    "security": _observed(lambda: self.object_security(handle)),
                    "reopened": _observed(lambda: self.mutex(name, 0x1f0001))}
        finally:
            self.close(handle)


def installer_context():
    """Capture failure evidence without changing installer policy or behavior."""
    if os.name != "nt":
        raise OSError("Installer diagnostics require Windows")
    native = _Context()
    result = {"schema": "wtop.installer-context/1", "process_id": os.getpid(),
              "session": _observed(native.session), "token": _observed(native.token),
              "window_station": _observed(lambda: native.user_object_name(native.u.GetProcessWindowStation())),
              "desktop": _observed(lambda: native.user_object_name(native.u.GetThreadDesktop(native.k.GetCurrentThreadId()))),
              "existing_mutexes": [], "owned_mutex_probes": {},
              "existing_object_security_modified": False, "existing_mutexes_acquired": False}
    for prefix in ("Global\\", "Local\\", ""):
        name = prefix + "_MSIExecute"
        for access in (0x100000, 0x20000, 0x1f0001):  # SYNCHRONIZE, READ_CONTROL, MUTEX_ALL_ACCESS
            result["existing_mutexes"].append({"name": name, "access": f"0x{access:08x}",
                                               "observation": _observed(lambda: native.mutex(name, access))})
    for prefix in ("Global\\", "Local\\"):
        result["owned_mutex_probes"][prefix] = _observed(lambda: native.owned_mutex(prefix))
    result["cleanup_errors"] = native.cleanup_errors
    return result
