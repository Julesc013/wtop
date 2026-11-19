"""Read actual Windows access-token context for native acceptance receipts.

Administrator membership, UAC elevation and token filtering are independent
observations. ``is_user_an_admin`` has IsUserAnAdmin/CheckTokenMembership
semantics for the *queried* token; it is not an alias for TokenElevation.
``has_restricting_sids`` is the narrower IsTokenRestricted result, whereas
``token_has_restrictions`` reports whether a token has ever been filtered.
TokenElevationType values are 1=default, 2=full and 3=limited.

This helper neither impersonates nor changes a token. A supplied handle stays
owned by its caller. Queries fail explicitly instead of inventing a safe-looking
false/zero value. Intended for modern Windows qualification tooling, not for
the application's historical runtime backends.

https://learn.microsoft.com/windows/win32/api/securitybaseapi/nf-securitybaseapi-checktokenmembership
https://learn.microsoft.com/windows/win32/api/securitybaseapi/nf-securitybaseapi-istokenrestricted
https://learn.microsoft.com/windows/win32/api/winnt/ne-winnt-token_elevation_type
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes as w
import os
import sys


TOKEN_QUERY = 0x0008
TOKEN_DUPLICATE = 0x0002
_MAX_TOKEN_INFORMATION = 65536
_ERROR_INSUFFICIENT_BUFFER = 122


class _SidAndAttributes(ctypes.Structure):
    _fields_ = [("sid", ctypes.c_void_p), ("attributes", w.DWORD)]


class _NativeToken:
    def __init__(self):
        if os.name != "nt":
            raise OSError("native token observation requires Windows")
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.security = ctypes.WinDLL("advapi32", use_last_error=True)

        def bind(library, name, arguments, result=w.BOOL):
            function = getattr(library, name)
            function.argtypes = arguments
            function.restype = result

        bind(self.kernel, "GetCurrentProcess", [], w.HANDLE)
        bind(self.kernel, "CloseHandle", [w.HANDLE])
        bind(self.kernel, "LocalFree", [ctypes.c_void_p], ctypes.c_void_p)
        bind(self.security, "OpenProcessToken", [w.HANDLE, w.DWORD, ctypes.POINTER(w.HANDLE)])
        bind(self.security, "GetTokenInformation", [w.HANDLE, ctypes.c_int,
             ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD)])
        bind(self.security, "DuplicateToken", [w.HANDLE, ctypes.c_int, ctypes.POINTER(w.HANDLE)])
        bind(self.security, "CheckTokenMembership", [w.HANDLE, ctypes.c_void_p, ctypes.POINTER(w.BOOL)])
        bind(self.security, "CreateWellKnownSid", [ctypes.c_int, ctypes.c_void_p,
             ctypes.c_void_p, ctypes.POINTER(w.DWORD)])
        bind(self.security, "IsTokenRestricted", [w.HANDLE])
        bind(self.security, "IsValidSid", [ctypes.c_void_p])
        bind(self.security, "GetLengthSid", [ctypes.c_void_p], w.DWORD)
        bind(self.security, "ConvertSidToStringSidW", [ctypes.c_void_p, ctypes.POINTER(w.LPWSTR)])

    @staticmethod
    def checked(result):
        if not result:
            raise ctypes.WinError(ctypes.get_last_error())
        return result

    @staticmethod
    def cleanup(operation, release, handle, *, local_free=False):
        """Preserve an observation failure if releasing owned memory also fails."""
        primary_error = sys.exc_info()[1]
        result = release(handle)
        if bool(result) == local_free:
            error = ctypes.WinError(ctypes.get_last_error())
            if primary_error is None:
                raise error
            detail = operation + ": " + str(error)
            if hasattr(primary_error, "add_note"):
                primary_error.add_note(detail)
            else:
                primary_error.token_cleanup_failure = detail

    def information(self, token, kind):
        needed = w.DWORD()
        ctypes.set_last_error(0)
        result = self.security.GetTokenInformation(token, kind, None, 0, ctypes.byref(needed))
        error = ctypes.get_last_error()
        if result or error != _ERROR_INSUFFICIENT_BUFFER:
            if error:
                raise ctypes.WinError(error)
            raise RuntimeError("token sizing query did not return a required buffer size")
        # An externally supplied live token can change between sizing and read.
        # Bound both allocation and retries instead of chasing it indefinitely.
        for _ in range(3):
            if not 1 <= needed.value <= _MAX_TOKEN_INFORMATION:
                raise RuntimeError("token information exceeded the 65536-byte bound")
            buffer = ctypes.create_string_buffer(max(needed.value, 4))
            capacity = len(buffer)
            ctypes.set_last_error(0)
            if self.security.GetTokenInformation(token, kind, buffer, capacity, ctypes.byref(needed)):
                if not 1 <= needed.value <= capacity:
                    raise RuntimeError("token query returned an invalid information length")
                return buffer, needed.value
            error = ctypes.get_last_error()
            if error != _ERROR_INSUFFICIENT_BUFFER or needed.value <= capacity:
                raise ctypes.WinError(error)
        raise RuntimeError("token information changed beyond the bounded query retries")

    def scalar(self, token, kind):
        if kind not in (18, 20, 21):
            raise ValueError("no fixed scalar contract for token information class " + str(kind))
        # These classes have a known maximum four-byte result. Some Windows
        # builds return ERROR_BAD_LENGTH for a zero-byte scalar sizing probe;
        # query the documented storage directly instead of weakening the
        # variable-length SID query's ERROR_INSUFFICIENT_BUFFER contract.
        buffer = ctypes.c_uint32()
        needed = w.DWORD()
        self.checked(self.security.GetTokenInformation(token, kind,
            ctypes.byref(buffer), ctypes.sizeof(buffer), ctypes.byref(needed)))
        returned = needed.value
        # TokenHasRestrictions is a BOOLEAN in native implementations despite
        # the DWORD wording of the public enum documentation. Read only bytes
        # actually returned and preserve the observed length in the receipt.
        # Do not extend this exception to elevation or other DWORD contracts.
        allowed = (1, 4) if kind == 21 else (4,)
        if returned not in allowed:
            raise RuntimeError("unexpected scalar size for token information class " + str(kind))
        return int.from_bytes(ctypes.string_at(ctypes.byref(buffer), returned), "little"), returned

    def sid_string(self, token, kind):
        buffer, returned = self.information(token, kind)
        if returned < ctypes.sizeof(_SidAndAttributes):
            raise RuntimeError("token SID header is truncated")
        sid = _SidAndAttributes.from_buffer(buffer).sid
        start = ctypes.addressof(buffer)
        # Validate the embedded pointer and the complete SID before passing it
        # to native SID helpers. A SID contains an 8-byte header and up to 15
        # little-endian DWORD subauthorities.
        if sid is None or not start <= sid <= start + returned - 8:
            raise RuntimeError("token SID points outside its information buffer")
        header = ctypes.string_at(sid, 8)
        sid_length = 8 + 4 * header[1]
        if header[0] != 1 or header[1] > 15 or sid + sid_length > start + returned:
            raise RuntimeError("token SID is malformed or truncated")
        if not self.security.IsValidSid(sid) or self.security.GetLengthSid(sid) != sid_length:
            raise RuntimeError("token SID failed native validation")
        value = w.LPWSTR()
        try:
            self.checked(self.security.ConvertSidToStringSidW(sid, ctypes.byref(value)))
            if not value:
                raise RuntimeError("SID conversion returned no string")
            return value.value
        finally:
            if value:
                self.cleanup("LocalFree(SID)", self.kernel.LocalFree,
                             ctypes.cast(value, ctypes.c_void_p), local_free=True)

    def administrator_member(self, token):
        # CheckTokenMembership requires an impersonation-token handle. The
        # duplicate is used for this query only; no caller impersonation occurs.
        administrator_sid = ctypes.create_string_buffer(68)  # SECURITY_MAX_SID_SIZE
        size = w.DWORD(len(administrator_sid))
        self.checked(self.security.CreateWellKnownSid(26, None, administrator_sid, ctypes.byref(size)))
        if not 8 <= size.value <= len(administrator_sid):
            raise RuntimeError("administrator SID has an invalid size")
        duplicate = w.HANDLE()
        try:
            self.checked(self.security.DuplicateToken(token, 1, ctypes.byref(duplicate)))
            member = w.BOOL()
            self.checked(self.security.CheckTokenMembership(duplicate, administrator_sid, ctypes.byref(member)))
            return bool(member.value)
        finally:
            if duplicate:
                self.cleanup("CloseHandle(query token)", self.kernel.CloseHandle, duplicate)

    def context(self, token):
        elevation, _ = self.scalar(token, 20)
        elevation_type, _ = self.scalar(token, 18)
        filtered, filtered_bytes = self.scalar(token, 21)
        if elevation_type not in (1, 2, 3):
            raise RuntimeError("unrecognized token elevation type")
        integrity_sid = self.sid_string(token, 25)
        integrity_parts = integrity_sid.split("-")
        if len(integrity_parts) != 4 or integrity_parts[:3] != ["S", "1", "16"]:
            raise RuntimeError("token integrity label is not a mandatory integrity SID")
        ctypes.set_last_error(0)
        restricting_sids = self.security.IsTokenRestricted(token)
        error = ctypes.get_last_error()
        if not restricting_sids and error:
            raise ctypes.WinError(error)
        return {
            "user_sid": self.sid_string(token, 1),
            "is_user_an_admin": self.administrator_member(token),
            "token_elevation": bool(elevation),
            "token_elevation_type": elevation_type,
            "integrity_rid": int(integrity_parts[3]),
            "token_has_restrictions": bool(filtered),
            "token_has_restrictions_returned_bytes": filtered_bytes,
            "has_restricting_sids": bool(restricting_sids),
        }


def token_context(token_handle) -> dict:
    """Inspect a borrowed TOKEN_QUERY | TOKEN_DUPLICATE handle; never close it."""
    if not token_handle:
        raise ValueError("a live token handle is required")
    return _NativeToken().context(token_handle)


def current_token_context() -> dict:
    """Inspect the current process's primary token, independently of its thread."""
    native = _NativeToken()
    token = w.HANDLE()
    try:
        native.checked(native.security.OpenProcessToken(native.kernel.GetCurrentProcess(),
            TOKEN_QUERY | TOKEN_DUPLICATE, ctypes.byref(token)))
        context = native.context(token)
        context.update(process_id=os.getpid(), python_bits=ctypes.sizeof(ctypes.c_void_p) * 8)
        return context
    finally:
        if token:
            native.cleanup("CloseHandle(process token)", native.kernel.CloseHandle, token)
