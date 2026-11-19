"""Run acceptance work as a fresh local standard account on a disposable host.

This is test infrastructure, never product installation code. An explicit
disposable-host gate and an empty owned staging directory are required. The
random password is supplied only through native credential API arguments; it
is never emitted to command lines, environment variables, files or diagnostics.
Python can retain temporary string copies; clearing the native buffer is not
a claim of complete in-process secret erasure. The
caller copies its receipts out before leaving the context.

The account has only direct local Users membership. Its real logon token must
be non-admin, non-elevated and medium integrity, with its own profile/environment.
The ordinary secondary-logon path inherits the caller's desktop; CREATE_NO_WINDOW
prevents a console window. This is not private-desktop or human-logon acceptance.
A suspended child joins an owned kill-on-close job before it can execute. A timeout or unproven job
retirement retains the account/profile/staging for the disposable VM's cleanup:
retiring an MSI client does not prove the external Installer service stopped.

Process waits and post-exit retirement polls have explicit bounds. Windows
account/profile/process-creation APIs have no universal hard-call deadline;
the disposable CI job's outer timeout remains the final containment boundary.

https://learn.microsoft.com/windows/win32/api/lmaccess/nf-lmaccess-netuseradd
https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-createprocesswithlogonw
https://learn.microsoft.com/windows/win32/api/userenv/nf-userenv-loaduserprofilew
https://learn.microsoft.com/windows/win32/api/userenv/nf-userenv-createenvironmentblock
"""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from contextlib import contextmanager
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import time
import uuid

from native_token import current_token_context, token_context


class _UserInfo1(C.Structure):
    _fields_ = [("name", W.LPWSTR), ("password", W.LPWSTR), ("password_age", W.DWORD),
                ("privilege", W.DWORD), ("home_directory", W.LPWSTR), ("comment", W.LPWSTR),
                ("flags", W.DWORD), ("script_path", W.LPWSTR)]


class _ProfileInfo(C.Structure):
    _fields_ = [("size", W.DWORD), ("flags", W.DWORD), ("username", W.LPWSTR),
                ("profile_path", W.LPWSTR), ("default_path", W.LPWSTR),
                ("server_name", W.LPWSTR), ("policy_path", W.LPWSTR), ("profile", W.HANDLE)]


class _StartupInfo(C.Structure):
    _fields_ = [("cb", W.DWORD), ("reserved", W.LPWSTR), ("desktop", W.LPWSTR),
                ("title", W.LPWSTR), ("x", W.DWORD), ("y", W.DWORD),
                ("x_size", W.DWORD), ("y_size", W.DWORD), ("x_chars", W.DWORD),
                ("y_chars", W.DWORD), ("fill", W.DWORD), ("flags", W.DWORD),
                ("show", W.WORD), ("reserved_size", W.WORD), ("reserved_data", C.c_void_p),
                ("stdin", W.HANDLE), ("stdout", W.HANDLE), ("stderr", W.HANDLE)]


class _ProcessInfo(C.Structure):
    _fields_ = [("process", W.HANDLE), ("thread", W.HANDLE), ("pid", W.DWORD), ("tid", W.DWORD)]


class _BasicJobLimits(C.Structure):
    _fields_ = [("process_time", C.c_longlong), ("job_time", C.c_longlong),
                ("flags", W.DWORD), ("minimum_working_set", C.c_size_t),
                ("maximum_working_set", C.c_size_t), ("active_processes", W.DWORD),
                ("affinity", C.c_size_t), ("priority", W.DWORD), ("scheduling", W.DWORD)]


class _IoCounters(C.Structure):
    _fields_ = [(name, C.c_ulonglong) for name in
                ("reads", "writes", "others", "read_bytes", "write_bytes", "other_bytes")]


class _ExtendedJobLimits(C.Structure):
    _fields_ = [("basic", _BasicJobLimits), ("io", _IoCounters),
                ("process_memory", C.c_size_t), ("job_memory", C.c_size_t),
                ("peak_process_memory", C.c_size_t), ("peak_job_memory", C.c_size_t)]


class _JobAccounting(C.Structure):
    _fields_ = [(name, C.c_longlong) for name in ("user", "kernel", "period_user", "period_kernel")] + [
                (name, W.DWORD) for name in ("faults", "total_processes", "active_processes", "terminated")]


class _Luid(C.Structure):
    _fields_ = [("low", W.DWORD), ("high", W.LONG)]


class _LuidAttributes(C.Structure):
    _fields_ = [("luid", _Luid), ("attributes", W.DWORD)]


class _TwoPrivileges(C.Structure):
    _fields_ = [("count", W.DWORD), ("values", _LuidAttributes * 2)]


def _checked(value):
    if not value:
        raise C.WinError(C.get_last_error())
    return value


def _net_checked(code):
    if code:
        raise C.WinError(code)


class _StandardAccount:
    def __init__(self, staging_root, diagnostics):
        self.staging_root = Path(staging_root)
        self.staging_owned = False
        self.diagnostics = diagnostics
        self.username = "wtp_" + uuid.uuid4().hex[:12]
        # Keep the native buffer alive through all calls; overwrite it on close.
        self.password = C.create_unicode_buffer("Wt1!" + secrets.token_urlsafe(36))
        self.user_sid = None
        self.computer_name = None
        self.created = False
        self.token = W.HANDLE()
        self.profile = _ProfileInfo()
        self.environment = C.c_void_p()
        self.retirement_proven = True
        self.closed = False
        self.k = C.WinDLL("kernel32", use_last_error=True)
        self.a = C.WinDLL("advapi32", use_last_error=True)
        self.n = C.WinDLL("netapi32", use_last_error=True)
        self.u = C.WinDLL("userenv", use_last_error=True)
        self._bind()

    def _bind(self):
        def bind(lib, name, args, result=W.BOOL):
            fn = getattr(lib, name); fn.argtypes, fn.restype = args, result
        bind(self.k, "GetCurrentProcess", [], W.HANDLE)
        bind(self.k, "CloseHandle", [W.HANDLE])
        bind(self.k, "LocalFree", [C.c_void_p], C.c_void_p)
        bind(self.k, "GetFileAttributesW", [W.LPCWSTR], W.DWORD)
        bind(self.k, "GetComputerNameW", [W.LPWSTR, C.POINTER(W.DWORD)])
        bind(self.k, "CreateJobObjectW", [C.c_void_p, W.LPCWSTR], W.HANDLE)
        bind(self.k, "SetInformationJobObject", [W.HANDLE, C.c_int, C.c_void_p, W.DWORD])
        bind(self.k, "QueryInformationJobObject", [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)])
        bind(self.k, "AssignProcessToJobObject", [W.HANDLE, W.HANDLE])
        bind(self.k, "TerminateJobObject", [W.HANDLE, W.UINT])
        bind(self.k, "TerminateProcess", [W.HANDLE, W.UINT])
        bind(self.k, "WaitForSingleObject", [W.HANDLE, W.DWORD], W.DWORD)
        bind(self.k, "ResumeThread", [W.HANDLE], W.DWORD)
        bind(self.k, "GetExitCodeProcess", [W.HANDLE, C.POINTER(W.DWORD)])
        bind(self.k, "GetErrorMode", [], W.UINT)
        bind(self.k, "SetErrorMode", [W.UINT], W.UINT)
        bind(self.a, "OpenProcessToken", [W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)])
        bind(self.a, "LookupPrivilegeValueW", [W.LPCWSTR, W.LPCWSTR, C.POINTER(_Luid)])
        bind(self.a, "AdjustTokenPrivileges", [W.HANDLE, W.BOOL, C.c_void_p, W.DWORD, C.c_void_p, C.POINTER(W.DWORD)])
        bind(self.a, "CreateWellKnownSid", [C.c_int, C.c_void_p, C.c_void_p, C.POINTER(W.DWORD)])
        bind(self.a, "LookupAccountSidW", [W.LPCWSTR, C.c_void_p, W.LPWSTR, C.POINTER(W.DWORD), W.LPWSTR, C.POINTER(W.DWORD), C.POINTER(W.DWORD)])
        bind(self.a, "LookupAccountNameW", [W.LPCWSTR, W.LPCWSTR, C.c_void_p, C.POINTER(W.DWORD), W.LPWSTR, C.POINTER(W.DWORD), C.POINTER(W.DWORD)])
        bind(self.a, "ConvertSidToStringSidW", [C.c_void_p, C.POINTER(W.LPWSTR)])
        bind(self.a, "LogonUserW", [W.LPCWSTR, W.LPCWSTR, W.LPCWSTR, W.DWORD, W.DWORD, C.POINTER(W.HANDLE)])
        bind(self.a, "CreateProcessWithLogonW", [W.LPCWSTR, W.LPCWSTR, W.LPCWSTR, W.DWORD,
             W.LPCWSTR, W.LPWSTR, W.DWORD, C.c_void_p, W.LPCWSTR, C.POINTER(_StartupInfo), C.POINTER(_ProcessInfo)])
        bind(self.a, "ConvertStringSecurityDescriptorToSecurityDescriptorW", [W.LPCWSTR, W.DWORD, C.POINTER(C.c_void_p), C.POINTER(W.DWORD)])
        bind(self.a, "GetSecurityDescriptorDacl", [C.c_void_p, C.POINTER(W.BOOL), C.POINTER(C.c_void_p), C.POINTER(W.BOOL)])
        bind(self.a, "GetSecurityDescriptorSacl", [C.c_void_p, C.POINTER(W.BOOL), C.POINTER(C.c_void_p), C.POINTER(W.BOOL)])
        bind(self.a, "SetNamedSecurityInfoW", [W.LPWSTR, C.c_int, W.DWORD, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p], W.DWORD)
        bind(self.n, "NetUserAdd", [W.LPCWSTR, W.DWORD, C.c_void_p, C.POINTER(W.DWORD)], W.DWORD)
        bind(self.n, "NetUserDel", [W.LPCWSTR, W.LPCWSTR], W.DWORD)
        bind(self.n, "NetLocalGroupAddMembers", [W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_void_p, W.DWORD], W.DWORD)
        bind(self.n, "NetUserGetLocalGroups", [W.LPCWSTR, W.LPCWSTR, W.DWORD, W.DWORD, C.POINTER(C.c_void_p), W.DWORD, C.POINTER(W.DWORD), C.POINTER(W.DWORD)], W.DWORD)
        bind(self.n, "NetApiBufferFree", [C.c_void_p], W.DWORD)
        bind(self.u, "LoadUserProfileW", [W.HANDLE, C.POINTER(_ProfileInfo)])
        bind(self.u, "UnloadUserProfile", [W.HANDLE, W.HANDLE])
        bind(self.u, "GetUserProfileDirectoryW", [W.HANDLE, W.LPWSTR, C.POINTER(W.DWORD)])
        bind(self.u, "CreateEnvironmentBlock", [C.POINTER(C.c_void_p), W.HANDLE, W.BOOL])
        bind(self.u, "DestroyEnvironmentBlock", [C.c_void_p])
        bind(self.u, "DeleteProfileW", [W.LPCWSTR, W.LPCWSTR, W.LPCWSTR])

    @contextmanager
    def _profile_privileges(self):
        """Temporarily enable only the two documented profile-service rights."""
        token = W.HANDLE(); previous = _TwoPrivileges(); received = W.DWORD()
        changed = False
        try:
            _checked(self.a.OpenProcessToken(self.k.GetCurrentProcess(), 0x28, C.byref(token)))
            wanted = _TwoPrivileges(); wanted.count = 2
            for item, name in zip(wanted.values, ("SeBackupPrivilege", "SeRestorePrivilege")):
                _checked(self.a.LookupPrivilegeValueW(None, name, C.byref(item.luid)))
                item.attributes = 2  # SE_PRIVILEGE_ENABLED
            C.set_last_error(0)
            _checked(self.a.AdjustTokenPrivileges(token, False, C.byref(wanted), C.sizeof(previous), C.byref(previous), C.byref(received)))
            changed = True
            if C.get_last_error() == 1300:
                raise PermissionError("fixture parent lacks required profile privileges")
            yield
        finally:
            primary = sys.exc_info()[1]
            errors = []
            if token and changed:
                C.set_last_error(0)
                restored = self.a.AdjustTokenPrivileges(token, False, C.byref(previous), 0, None, None)
                error = C.get_last_error()
                if not restored or error == 1300:
                    errors.append("profile privilege restoration: " + str(C.WinError(error)))
            if token and not self.k.CloseHandle(token):
                errors.append("CloseHandle(profile privilege token): " + str(C.WinError(C.get_last_error())))
            if errors:
                self._cleanup_problem(primary, "; ".join(errors))

    def _cleanup_problem(self, primary, detail):
        self.diagnostics.setdefault("native_cleanup_errors", []).append(detail)
        if primary is None:
            raise RuntimeError(detail)
        if hasattr(primary, "add_note"):
            primary.add_note(detail)
        else:
            errors = getattr(primary, "fixture_cleanup_errors", [])
            primary.fixture_cleanup_errors = [*errors, detail]

    def _account_sid(self):
        sid = C.create_string_buffer(68); size = W.DWORD(len(sid))
        domain = C.create_unicode_buffer(257); domains = W.DWORD(len(domain)); kind = W.DWORD()
        name = self.computer_name + "\\" + self.username
        if not self.a.LookupAccountNameW(None, name, sid, C.byref(size), domain, C.byref(domains), C.byref(kind)):
            error = C.get_last_error()
            if error == 1332:  # ERROR_NONE_MAPPED: account already absent.
                return None
            raise C.WinError(error)
        if kind.value != 1 or not 8 <= size.value <= len(sid):  # SidTypeUser
            raise RuntimeError("fixture name no longer identifies a bounded user SID")
        text = W.LPWSTR()
        try:
            _checked(self.a.ConvertSidToStringSidW(sid, C.byref(text)))
            return text.value
        finally:
            primary = sys.exc_info()[1]
            if text and self.k.LocalFree(C.cast(text, C.c_void_p)):
                self._cleanup_problem(primary, "LocalFree(account SID): " + str(C.WinError(C.get_last_error())))

    def _users_group(self):
        sid = C.create_string_buffer(68); size = W.DWORD(len(sid))
        _checked(self.a.CreateWellKnownSid(27, None, sid, C.byref(size)))  # WinBuiltinUsersSid
        name = C.create_unicode_buffer(257); domain = C.create_unicode_buffer(257)
        names = W.DWORD(len(name)); domains = W.DWORD(len(domain)); kind = W.DWORD()
        _checked(self.a.LookupAccountSidW(None, sid, name, C.byref(names), domain, C.byref(domains), C.byref(kind)))
        return name.value

    def _group_membership(self, expected):
        data = C.c_void_p(); count = W.DWORD(); total = W.DWORD()
        try:
            _net_checked(self.n.NetUserGetLocalGroups(None, self.username, 0, 0, C.byref(data), 65536, C.byref(count), C.byref(total)))
            if count.value != total.value or not 1 <= count.value <= 256 or not data:
                raise RuntimeError("bounded direct local-group enumeration was incomplete")
            names = list(C.cast(data, C.POINTER(W.LPWSTR * count.value)).contents)
            if set(names) != {expected}:
                raise RuntimeError("fresh account has unexpected direct local groups")
            self.diagnostics["direct_local_groups"] = names
        finally:
            primary = sys.exc_info()[1]
            if data:
                code = self.n.NetApiBufferFree(data)
                if code:
                    self._cleanup_problem(primary, "NetApiBufferFree(groups): " + str(C.WinError(code)))

    def _stage_access(self, parent_sid):
        descriptor = C.c_void_p()
        try:
            sddl = "D:P" + "".join("(A;OICI;FA;;;" + sid + ")" for sid in (self.user_sid, parent_sid, "SY"))
            sddl += "S:(ML;OICI;NW;;;ME)"
            _checked(self.a.ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1, C.byref(descriptor), None))
            present = W.BOOL(); defaulted = W.BOOL(); dacl = C.c_void_p(); sacl = C.c_void_p()
            _checked(self.a.GetSecurityDescriptorDacl(descriptor, C.byref(present), C.byref(dacl), C.byref(defaulted)))
            if not present.value or not dacl:
                raise RuntimeError("owned staging requires an explicit DACL")
            _checked(self.a.GetSecurityDescriptorSacl(descriptor, C.byref(present), C.byref(sacl), C.byref(defaulted)))
            if not present.value or not sacl:
                raise RuntimeError("owned staging requires an explicit medium label")
            _net_checked(self.a.SetNamedSecurityInfoW(str(self.staging_root), 1, 0x80000014, None, None, dacl, sacl))
            self.diagnostics["staging_acl"] = {"path": str(self.staging_root),
                "access": "target_user_parent_and_SYSTEM_only", "integrity": "medium",
                "ancestors_modified": False}
        finally:
            primary = sys.exc_info()[1]
            if descriptor and self.k.LocalFree(descriptor):
                self._cleanup_problem(primary, "LocalFree(staging descriptor): " + str(C.WinError(C.get_last_error())))

    def open(self):
        attributes = self.k.GetFileAttributesW(str(self.staging_root))
        if attributes == 0xffffffff:
            raise C.WinError(C.get_last_error())
        if attributes & 0x400 or not attributes & 0x10 or any(self.staging_root.iterdir()):
            raise ValueError("fixture staging must be an existing empty non-reparse directory")
        self.staging_root = self.staging_root.resolve(strict=True)
        if self.staging_root == self.staging_root.parent:
            raise ValueError("a volume root is not disposable staging")
        self.staging_owned = True
        parent = current_token_context()
        if not parent["is_user_an_admin"] or not parent["token_elevation"]:
            raise PermissionError("disposable standard-account fixture requires an elevated parent")
        self.diagnostics.update(schema=1, kind="fresh-standard-account-fixture", parent_context=parent,
            account_name=self.username, staging_root=str(self.staging_root), runs=[], human_review=False,
            policy_modified=False, password_persisted=False)
        info = _UserInfo1(self.username, C.cast(self.password, W.LPWSTR), 0, 1, None,
                          "Disposable Wtop acceptance fixture", 0x201, None)
        computer = C.create_unicode_buffer(256); count = W.DWORD(len(computer))
        _checked(self.k.GetComputerNameW(computer, C.byref(count)))
        self.computer_name = computer.value
        parameter = W.DWORD()
        _net_checked(self.n.NetUserAdd(None, 1, C.byref(info), C.byref(parameter)))
        self.created = True; self.diagnostics["account_created"] = True
        created_sid = self._account_sid()
        if not created_sid or created_sid == parent["user_sid"]:
            raise RuntimeError("new account identity was not independently established")
        self.user_sid = created_sid
        self.diagnostics["account_user_sid"] = self.user_sid
        users = self._users_group()
        member = W.LPWSTR(self.computer_name + "\\" + self.username)
        status = self.n.NetLocalGroupAddMembers(None, users, 3, C.byref(member), 1)
        if status != 1378:  # ERROR_MEMBER_IN_ALIAS: NetUserAdd may already add Users.
            _net_checked(status)
        self._group_membership(users)
        _checked(self.a.LogonUserW(self.username, ".", self.password, 2, 0, C.byref(self.token)))
        context = token_context(self.token)
        if context["is_user_an_admin"] or context["token_elevation"] or context["integrity_rid"] != 8192:
            raise RuntimeError("new account did not produce an ordinary medium non-elevated logon token")
        if context["token_has_restrictions"] or context["has_restricting_sids"]:
            raise RuntimeError("fresh account token unexpectedly has synthetic restrictions")
        if context["user_sid"] != self.user_sid:
            raise RuntimeError("logon token does not belong to the newly created account")
        self.diagnostics["logon_context"] = context
        self.profile.size = C.sizeof(self.profile); self.profile.flags = 1  # PI_NOUI
        self.profile.username = self.username
        with self._profile_privileges():
            _checked(self.u.LoadUserProfileW(self.token, C.byref(self.profile)))
        self.diagnostics["profile_loaded"] = True
        path = C.create_unicode_buffer(32768); count = W.DWORD(len(path))
        _checked(self.u.GetUserProfileDirectoryW(self.token, path, C.byref(count)))
        self.diagnostics["profile_path"] = path.value
        _checked(self.u.CreateEnvironmentBlock(C.byref(self.environment), self.token, False))
        self.diagnostics["environment"] = "CreateEnvironmentBlock fresh user; parent environment not inherited"
        self._stage_access(parent["user_sid"])
        self.diagnostics["desktop"] = {"selection": "caller_inherited",
            "startup_desktop": None, "private_window_station_created": False,
            "shared_object_acl_modified": False, "create_no_window": True}
        return self

    def _job_empty(self, job, seconds):
        deadline = time.monotonic() + seconds
        while True:
            accounting = _JobAccounting()
            _checked(self.k.QueryInformationJobObject(job, 1, C.byref(accounting), C.sizeof(accounting), None))
            if not accounting.active_processes:
                return True
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.05)

    def retain_for_diagnosis(self, reason):
        """Fail cleanup conservatively when the caller lacks a retirement receipt."""
        self.retirement_proven = False
        self.diagnostics.setdefault("retention_reasons", []).append(str(reason))

    def run(self, arguments, cwd, timeout=600):
        if (self.closed or not self.created or not self.environment or
                not self.profile.profile or not self.retirement_proven):
            raise RuntimeError("standard-account fixture is not admitting work")
        if not arguments or not 1 <= timeout <= 1800:
            raise ValueError("explicit command and a bounded 1..1800-second wait required")
        directory = Path(cwd).resolve(strict=True)
        if not directory.is_relative_to(self.staging_root):
            raise ValueError("standard fixture cwd must stay inside its exact staging directory")
        executable = Path(arguments[0]).resolve(strict=True)
        command = C.create_unicode_buffer(subprocess.list2cmdline([str(executable), *map(str, arguments[1:])]))
        if len(command) > 1024:
            raise ValueError("CreateProcessWithLogonW command exceeds its 1024-character bound")
        run = {"timeout_s": timeout, "cwd": str(directory), "executable": str(executable),
               "creation": "suspended_then_owned_job_then_resume", "job_retired": False}
        self.diagnostics["runs"].append(run)
        job = None; process = _ProcessInfo(); assigned = False; resumed = False
        try:
            job = _checked(self.k.CreateJobObjectW(None, None))
            limits = _ExtendedJobLimits(); limits.basic.flags = 0x2000  # KILL_ON_JOB_CLOSE
            _checked(self.k.SetInformationJobObject(job, 9, C.byref(limits), C.sizeof(limits)))
            startup = _StartupInfo(); startup.cb = C.sizeof(startup)
            # Use the documented normal secondary-logon desktop selection.
            # The former private station let the child start but its named
            # object operations were denied, even for a new random mutex.
            # Do not work around that by modifying shared ACLs or token groups.
            startup.desktop = None
            previous = self.k.GetErrorMode()
            self.k.SetErrorMode(previous | 0x8003)
            try:
                _checked(self.a.CreateProcessWithLogonW(self.username, ".", self.password, 1,
                    str(executable), command, 0x08000404, self.environment, str(directory),
                    C.byref(startup), C.byref(process)))
            finally:
                self.k.SetErrorMode(previous)
            run["pid"] = process.pid
            _checked(self.k.AssignProcessToJobObject(job, process.process)); assigned = True
            if self.k.ResumeThread(process.thread) == 0xffffffff:
                raise C.WinError(C.get_last_error())
            resumed = True
            _checked(self.k.CloseHandle(process.thread)); process.thread = None
            waited = self.k.WaitForSingleObject(process.process, timeout * 1000)
            if waited == 258:
                self.retirement_proven = False
                run["external_service_retirement"] = "unproven_after_timeout"
                raise TimeoutError("standard fixture client exceeded its bound; external MSI service retirement is unproven")
            if waited != 0:
                raise C.WinError(C.get_last_error())
            code = W.DWORD(); _checked(self.k.GetExitCodeProcess(process.process, C.byref(code)))
            run["exit_code"] = code.value; run["exit_code_hex"] = f"0x{code.value:08x}"
            if code.value >= 0x80000000:
                self.retain_for_diagnosis("native abnormal client exit; external service retirement unproven")
            run["job_retired"] = self._job_empty(job, 5)
            if not run["job_retired"]:
                self.retirement_proven = False
                raise RuntimeError("standard fixture client exited with live owned descendants")
            return code.value
        finally:
            primary = sys.exc_info()[1]
            cleanup_errors = []
            try:
                if process.process and not run["job_retired"]:
                    if resumed:
                        # A failed wait/query also leaves possible service-side
                        # work, even when our kill request empties the job.
                        self.retirement_proven = False
                        run["external_service_retirement"] = "unproven_after_forced_client_retirement"
                    if assigned:
                        _checked(self.k.TerminateJobObject(job, 124))
                        run["job_retired"] = self._job_empty(job, 5)
                    else:
                        _checked(self.k.TerminateProcess(process.process, 124))
                        run["job_retired"] = self.k.WaitForSingleObject(process.process, 5000) == 0
                    if not run["job_retired"]:
                        self.retirement_proven = False
                        cleanup_errors.append("owned fixture process/job retirement not confirmed")
            except BaseException as error:
                self.retirement_proven = False; cleanup_errors.append(str(error))
            for label, handle in (("thread", process.thread), ("process", process.process), ("job", job)):
                if handle and not self.k.CloseHandle(handle):
                    cleanup_errors.append("CloseHandle(" + label + "): " + str(C.WinError(C.get_last_error())))
            if cleanup_errors:
                run["cleanup_errors"] = cleanup_errors
                detail = "standard fixture run cleanup: " + "; ".join(cleanup_errors)
                self._cleanup_problem(primary, detail)

    def close(self):
        if self.closed:
            return
        self.closed = True
        errors = []
        cleanup = self.diagnostics.setdefault("cleanup", {})

        def attempt(name, operation):
            try:
                operation(); cleanup[name] = True
                return True
            except BaseException as error:
                cleanup[name] = False; errors.append(name + ": " + str(error))
                return False

        if self.environment:
            attempt("environment_released", lambda: _checked(self.u.DestroyEnvironmentBlock(self.environment)))
            self.environment = C.c_void_p()
        cleanup["owned_process_retirement_proven"] = self.retirement_proven
        safe = self.retirement_proven and (not self.created or bool(self.user_sid))
        if safe and self.created:
            def verify_account():
                actual = self._account_sid()
                if actual is not None and actual != self.user_sid:
                    raise RuntimeError("fixture account name now belongs to a different SID")
            safe = attempt("account_identity_checked", verify_account)
        if safe and self.profile.profile:
            def unload():
                deadline = time.monotonic() + 10
                with self._profile_privileges():
                    while not self.u.UnloadUserProfile(self.token, self.profile.profile):
                        error = C.get_last_error()
                        if time.monotonic() >= deadline:
                            raise C.WinError(error)
                        time.sleep(0.1)
                self.profile.profile = None
            safe = attempt("profile_unloaded", unload)
        if safe and self.created and self.user_sid:
            def delete_profile():
                if not self.u.DeleteProfileW(self.user_sid, None, None):
                    error = C.get_last_error()
                    if error not in (2, 3):  # No profile existed after failed setup.
                        raise C.WinError(error)
            safe = attempt("profile_deleted", delete_profile)
        if safe and self.created:
            def delete_account():
                actual = self._account_sid()
                if actual is None:
                    self.created = False
                    return
                if actual != self.user_sid:
                    raise RuntimeError("refusing to delete replacement account identity")
                result = self.n.NetUserDel(None, self.username)
                if result != 2221:  # NERR_UserNotFound is already absent.
                    _net_checked(result)
                self.created = False
            safe = attempt("account_deleted", delete_account)
        if self.token:
            attempt("token_closed", lambda: _checked(self.k.CloseHandle(self.token)))
            self.token = W.HANDLE()
        C.memset(C.addressof(self.password), 0, C.sizeof(self.password))
        cleanup["password_buffer_cleared"] = True
        if safe and self.staging_owned:
            def delete_staging():
                attributes = self.k.GetFileAttributesW(str(self.staging_root))
                if attributes == 0xffffffff:
                    error = C.get_last_error()
                    if error in (2, 3):
                        return
                    raise C.WinError(error)
                if attributes & 0x400 or not attributes & 0x10:
                    raise RuntimeError("refusing recursive removal of replaced/reparse fixture staging")
                shutil.rmtree(self.staging_root)
            attempt("staging_deleted", delete_staging)
        elif not safe:
            cleanup["retained_account"] = self.username if self.created else None
            cleanup["retained_profile"] = self.diagnostics.get("profile_path")
            cleanup["retained_staging"] = str(self.staging_root)
            errors.append("account/profile/staging retained because retirement or profile cleanup is unproven")
        else:
            cleanup["staging_untouched"] = True
        cleanup["result"] = "failed" if errors else "passed"
        if errors:
            cleanup["errors"] = errors
            raise RuntimeError("standard fixture cleanup failed; see retained diagnostics: " + "; ".join(errors))


@contextmanager
def standard_user_fixture(staging_root, diagnostics, *, disposable_host=False):
    """Yield a fresh account; copy named receipts out before exiting this scope.

    ``staging_root`` must already exist and be empty. Populate it only after
    entry so copied files inherit its exact temporary account ACL. The context
    deletes it on proven cleanup and retains it when retirement is uncertain.
    """
    if os.name != "nt":
        raise OSError("standard-account acceptance requires Windows")
    if not disposable_host:
        raise PermissionError("fresh account creation requires the explicit disposable-host gate")
    account = _StandardAccount(staging_root, diagnostics)
    try:
        yield account.open()
    finally:
        primary = sys.exc_info()[1]
        try:
            account.close()
        except BaseException as error:
            account._cleanup_problem(primary, str(error))
