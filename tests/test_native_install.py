#!/usr/bin/env python3
"""Destructive-to-fixtures MSI tests; run only on an explicitly disposable host.

Unittest discovery does not install anything. The explicit command requires
--disposable-host, and --machine additionally authorizes the machine-scope case.
The harness never overwrites an existing installation or user configuration.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import traceback
import unittest
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package_candidate import sha256, verify, third_party_notices_required


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def cleanup_preserving_failure(cleanup):
    """Do not hide the actual failed step behind a second cleanup failure."""
    primary = sys.exc_info()[1]
    try:
        cleanup()
    except Exception:
        if primary is None:
            raise
        # Keep both failures in the receipt, including on supported Python 3.10
        # which does not have BaseException.add_note yet.
        primary.fixture_cleanup_failure = traceback.format_exc()


def install_scope_arguments(scope):
    # ALLUSERS=1 explicitly selects machine scope and ignores the package's
    # MSIINSTALLPERUSER default. Avoid an empty property whose required literal
    # quotes would be consumed or escaped by generic argv serialization.
    if scope == "machine":
        return ["ALLUSERS=1"]
    if scope == "user":
        return ["ALLUSERS=2", "MSIINSTALLPERUSER=1"]
    raise ValueError("unknown installation scope")


class InstallScopeArguments(unittest.TestCase):
    def test_explicit_machine_scope_has_no_inherited_user_selector(self):
        self.assertEqual(subprocess.list2cmdline(install_scope_arguments("machine")), "ALLUSERS=1")
        self.assertEqual(install_scope_arguments("user"), ["ALLUSERS=2", "MSIINSTALLPERUSER=1"])
        with self.assertRaises(ValueError):
            install_scope_arguments("unknown")

    def test_failed_cleanup_retains_the_primary_failure_and_both_traces(self):
        def failing_cleanup():
            raise RuntimeError("cleanup sentinel")

        with self.assertRaisesRegex(ValueError, "primary sentinel") as caught:
            try:
                raise ValueError("primary sentinel")
            finally:
                cleanup_preserving_failure(failing_cleanup)
        self.assertIn("RuntimeError: cleanup sentinel", caught.exception.fixture_cleanup_failure)

    def test_cleanup_failure_is_fatal_without_an_earlier_failure(self):
        def failing_cleanup():
            raise RuntimeError("cleanup sentinel")

        with self.assertRaisesRegex(RuntimeError, "cleanup sentinel"):
            cleanup_preserving_failure(failing_cleanup)

    def test_policy_refusal_requires_effective_policy_and_exact_product(self):
        product = "{01234567-89AB-CDEF-0123-456789ABCDEF}"
        text = ("Machine policy value 'DisableMsi' is 1\n"
                f"Rejecting product '{product}': Non-assigned apps are disabled for non-admin users.\n")
        verify_policy_refusal(text, product)
        for bad in (text.replace("is 1", "is 0"), text.replace(product, "{OTHER}"),
                    "This installation is forbidden by system policy.",
                    text + "Doing action: InstallInitialize\n"):
            with self.assertRaises(AssertionError):
                verify_policy_refusal(bad, product)

    def test_deferred_failure_oracle_requires_writes_and_rejects_rollback_errors(self):
        product = "{01234567-89AB-CDEF-0123-456789ABCDEF}"
        text = ("Executing op: FileCopy(SourceName=wtop.exe,DestName=wtop.exe,)\n"
                f"Executing op: RegAddValue(Name=ProductCode,Value={product},)\n"
                "Executing op: ProductRegister(UpgradeCode={fixture},VersionString=0.4.404,)\n"
                "Executing op: CustomActionSchedule(Action=WtopFixtureFail,ActionType=1025,)\n"
                "Missing deferred ownership context.\n"
                "CustomAction WtopFixtureFail returned actual error code 1603\n"
                "Executing op: Header(ScriptType=2,)\n"
                "Executing op: End(Checksum=0,)\n"
                "Error in rollback skipped. Return: 5\n")
        verify_deferred_rollback_log(text, product)
        for bad in (text.replace("SourceName=wtop.exe", "SourceName=other.exe"),
                    text.replace(product, "{OTHER}"), text.replace("ScriptType=2", "ScriptType=1"),
                    text.replace("Return: 5", "Return: 3"),
                    text.replace("Executing op: End(Checksum=0,)", "Executing op: RegAddValue()")):
            with self.assertRaises(AssertionError):
                verify_deferred_rollback_log(bad, product)


def msi_log_text(log):
    raw = log.read_bytes() if log.exists() else b""
    encoding = "utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else ("utf-16-le" if b"\0" in raw[:128] else "utf-8")
    return raw.decode(encoding, errors="replace")


def verify_policy_refusal(text, product):
    require(re.search(r"Machine policy value 'DisableMsi' is 1\s*$", text, re.MULTILINE),
            "refusal does not establish effective DisableMSI=1")
    require(f"Rejecting product '{product}': Non-assigned apps are disabled for non-admin users." in text,
            "refusal does not establish this product's unmanaged/non-admin policy denial")
    require("Doing action: InstallInitialize" not in text and ": InstallInitialize." not in text,
            "policy refusal unexpectedly entered the installation transaction")


def verify_deferred_rollback_log(text, failed_product):
    point = text.find("Executing op: CustomActionSchedule(Action=WtopFixtureFail,")
    require(point >= 0, "injected failure was not executed inside the installer script")
    before = text[:point]
    require("Executing op: FileCopy(SourceName=wtop.exe," in before and
            f"Executing op: RegAddValue(Name=ProductCode,Value={failed_product}," in before and
            "Executing op: ProductRegister(" in before,
            "injected deferred failure did not follow actual payload and registration writes")
    require("Missing deferred ownership context." in text[point:] and
            "CustomAction WtopFixtureFail returned actual error code 1603" in text[point:] and
            "ScriptType=2" in text[point:], "expected deferred guard failure and rollback were not observed")
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if "Error in rollback skipped." in line:
            result = re.search(r"Return:\s*(\d+)\s*$", line)
            # MSI logs use 5 for success. Accept it only at a completed script
            # End; failed actions (including access-denied Return3) are fatal.
            # https://learn.microsoft.com/windows/win32/msi/logging-of-action-return-values
            require(result and result.group(1) == "5" and index > 0 and
                    "Executing op: End(" in lines[index - 1],
                    "installer skipped a failed rollback action: " + line)


def msi_product_state(product):
    api = ctypes.WinDLL("msi", use_last_error=True)
    api.MsiQueryProductStateW.argtypes = [ctypes.c_wchar_p]
    api.MsiQueryProductStateW.restype = ctypes.c_int
    return api.MsiQueryProductStateW(product)


def msi_execute(arguments, log):
    completed = subprocess.run(["msiexec.exe", *arguments, "/qn", "/norestart", "/L*V", str(log)],
                               timeout=120, capture_output=True, text=True)
    return completed.returncode


def msi_call(arguments, log: Path, expect_success=True, expected_reason=None, expected_refusal_code=1603):
    code = msi_execute(arguments, log)
    text = msi_log_text(log)
    if expect_success:
        require(code == 0, f"msiexec failed ({code}); see {log}")
    else:
        require(code == expected_refusal_code,
                f"expected installer refusal {expected_refusal_code}, received {code}; see {log}")
        if expected_reason:
            require(expected_reason in text, f"refusal did not report the expected guard: {log}")
    return code


def upgrade_probe(original: Path, destination: Path, version: str, delta=1, fail_during_execute=False):
    """Make a local test-only related MSI by changing its product/package IDs.

    Cabinet/application bytes are untouched. This is never uploaded as a
    deliverable. Identical application bytes deliberately isolate MSI ownership,
    product-version ordering and rollback from runtime/file-version differences.
    """
    shutil.copyfile(original, destination)
    api = ctypes.WinDLL("msi", use_last_error=True)
    handle = ctypes.c_uint
    api.MsiOpenDatabaseW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.POINTER(handle)]
    api.MsiDatabaseOpenViewW.argtypes = [handle, ctypes.c_wchar_p, ctypes.POINTER(handle)]
    api.MsiViewExecute.argtypes = [handle, handle]
    api.MsiViewFetch.argtypes = [handle, ctypes.POINTER(handle)]
    api.MsiRecordGetStringW.argtypes = [handle, ctypes.c_uint, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint)]
    api.MsiRecordSetStringW.argtypes = [handle, ctypes.c_uint, ctypes.c_wchar_p]
    api.MsiGetLastErrorRecord.argtypes = []
    api.MsiGetLastErrorRecord.restype = handle
    api.MsiFormatRecordW.argtypes = [handle, handle, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint)]
    api.MsiViewClose.argtypes = [handle]
    api.MsiCloseHandle.argtypes = [handle]
    api.MsiDatabaseCommit.argtypes = [handle]
    api.MsiGetSummaryInformationW.argtypes = [handle, ctypes.c_wchar_p, ctypes.c_uint, ctypes.POINTER(handle)]
    api.MsiSummaryInfoSetPropertyW.argtypes = [handle, ctypes.c_uint, ctypes.c_uint, ctypes.c_int, ctypes.c_void_p, ctypes.c_wchar_p]
    api.MsiSummaryInfoPersist.argtypes = [handle]

    def checked(result, operation):
        if result != 0:
            # Read the extended error before another MSI call overwrites it.
            error = api.MsiGetLastErrorRecord()
            detail = ""
            if error:
                try:
                    text = ctypes.create_unicode_buffer(4096)
                    capacity = ctypes.c_uint(len(text))
                    if api.MsiFormatRecordW(0, error, text, ctypes.byref(capacity)) == 0:
                        detail = ": " + text.value
                finally:
                    api.MsiCloseHandle(error)
            raise AssertionError(f"{operation} failed ({result}){detail}")

    def execute(database, statement, record=0):
        view = handle()
        checked(api.MsiDatabaseOpenViewW(database, statement, ctypes.byref(view)),
                "MsiDatabaseOpenViewW " + statement)
        try:
            checked(api.MsiViewExecute(view, record), "MsiViewExecute " + statement)
        finally:
            api.MsiViewClose(view)
            api.MsiCloseHandle(view)

    database = handle()
    require(api.MsiOpenDatabaseW(str(destination), ctypes.c_void_p(1), ctypes.byref(database)) == 0, "cannot open test MSI transaction")
    try:
        product = "{" + str(uuid.uuid4()).upper() + "}"
        major, minor, build = map(int, version.split("."))
        require(0 <= build + delta <= 65535, "upgrade probe requires a spare candidate MSI build number")
        new_version = f"{major}.{minor}.{build + delta}"
        statements = [f"UPDATE `Property` SET `Value`='{value}' WHERE `Property`='{key}'"
                      for key, value in (("ProductCode", product), ("ProductVersion", new_version))]
        # VersionMin/Max are part of Upgrade's primary key. Rebuild this copied
        # table with parameterized full-row inserts; SQL UPDATE cannot change
        # primary keys. Retain native records to preserve NULLs and field types.
        if new_version != version:
            columns = "`UpgradeCode`,`VersionMin`,`VersionMax`,`Language`,`Attributes`,`Remove`,`ActionProperty`"
            query = "SELECT " + columns + " FROM `Upgrade`"
            upgrade_view = handle()
            checked(api.MsiDatabaseOpenViewW(database, query, ctypes.byref(upgrade_view)),
                    "open Upgrade fixture view")
            records = []
            try:
                checked(api.MsiViewExecute(upgrade_view, 0), "read Upgrade fixture rows")
                while True:
                    record = handle()
                    result = api.MsiViewFetch(upgrade_view, ctypes.byref(record))
                    if result == 259:  # ERROR_NO_MORE_ITEMS
                        break
                    checked(result, "fetch Upgrade fixture row")
                    records.append(record)
                    require(len(records) <= 16, "unexpected number of Upgrade fixture rows")
                # No result cursor remains live during replacement. Records are
                # independently owned until MsiCloseHandle below.
                api.MsiViewClose(upgrade_view)
                api.MsiCloseHandle(upgrade_view)
                upgrade_view = handle()
                changed = 0
                for record in records:
                    replace = False
                    for field in (2, 3):
                        text = ctypes.create_unicode_buffer(32)
                        capacity = ctypes.c_uint(len(text))
                        checked(api.MsiRecordGetStringW(record, field, text, ctypes.byref(capacity)),
                                "read Upgrade version bound")
                        if text.value == version:
                            checked(api.MsiRecordSetStringW(record, field, new_version),
                                    "set Upgrade fixture version bound")
                            replace = True
                    if replace:
                        changed += 1
                require(changed > 0, "Upgrade fixture has no matching version bounds")
                execute(database, "DELETE FROM `Upgrade`")
                for record in records:
                    execute(database, "INSERT INTO `Upgrade` (" + columns + ") VALUES (?,?,?,?,?,?,?)", record)
            finally:
                for record in records:
                    api.MsiCloseHandle(record)
                if upgrade_view.value:
                    api.MsiViewClose(upgrade_view)
                    api.MsiCloseHandle(upgrade_view)
        if fail_during_execute:
            # Test-copy only: fail within the normal InstallFinalize script,
            # after PublishProduct. No context is bound to this action, so the
            # existing read-only guard must take its fail-closed path. Do not
            # introduce an extra InstallExecute/immediate-action boundary which
            # is absent from the production package.
            statements += [
                "INSERT INTO `CustomAction` (`Action`,`Type`,`Source`,`Target`) VALUES ('WtopFixtureFail',1025,'Guard','WtopGuardDeferred')",
                "INSERT INTO `InstallExecuteSequence` (`Action`,`Sequence`) VALUES ('WtopFixtureFail',6550)",
            ]
        for statement in statements:
            execute(database, statement)
        summary = handle()
        require(api.MsiGetSummaryInformationW(database, None, 1, ctypes.byref(summary)) == 0, "cannot open test MSI summary")
        try:
            require(api.MsiSummaryInfoSetPropertyW(summary, 9, 30, 0, None, "{" + str(uuid.uuid4()).upper() + "}") == 0, "cannot set test package code")
            require(api.MsiSummaryInfoPersist(summary) == 0, "cannot persist test package code")
        finally:
            api.MsiCloseHandle(summary)
        require(api.MsiDatabaseCommit(database) == 0, "cannot commit test MSI")
    finally:
        api.MsiCloseHandle(database)
    return {"product_code": product, "msi_version": new_version}


def registry_path_values():
    """Read both PATH owners verbatim; this candidate must leave them unchanged."""
    import winreg
    values = []
    for hive, key in ((winreg.HKEY_CURRENT_USER, "Environment"),
                      (winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment")):
        try:
            with winreg.OpenKey(hive, key) as handle:
                values.append(winreg.QueryValueEx(handle, "Path"))
        except FileNotFoundError:
            values.append(None)
    return values


def run_server_policy_probe(candidate, manifest, logs):
    """Verify an unmodified Server policy blocks a clean ordinary-user install.

    This is evidence of refusal, not successful installation or ordinary-user
    lifecycle acceptance. No installer policy or token is changed here.
    """
    import winreg
    require(sys.getwindowsversion().product_type in (2, 3),
            "expected policy-refusal probe is restricted to Server hosts")
    require(not ctypes.windll.shell32.IsUserAnAdmin(),
            "policy-refusal probe must run without administrator membership")
    profile = manifest["payload"]["profile"]
    arch = manifest["payload"]["pe"]["architecture"]
    directory = Path(os.environ["LOCALAPPDATA"]) / "Programs" / "Wtop" / profile
    registry = "Software\\Wtop\\Installations\\" + profile
    view = winreg.KEY_WOW64_32KEY if arch == "x86" else winreg.KEY_WOW64_64KEY

    def raw_policy():
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"Software\Policies\Microsoft\Windows\Installer",
                                0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
                value, kind = winreg.QueryValueEx(key, "DisableMSI")
        except FileNotFoundError:
            return {"present": False}
        require(kind == winreg.REG_DWORD, "unexpected DisableMSI registry data type")
        return {"present": True, "registry_type": kind, "value": value}

    def require_absent_payload_and_owner():
        try:
            directory.lstat()
        except FileNotFoundError:
            pass
        else:
            raise AssertionError("policy probe requires an absent payload directory before and after refusal")
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry, 0, winreg.KEY_READ | view):
                raise AssertionError("policy probe requires an absent Wtop owner before and after refusal")
        except FileNotFoundError:
            pass

    product = manifest["installer"]["product_code"]
    product_state = msi_product_state(product)
    require(product_state == -1, "policy probe requires an unregistered MSI product")
    require_absent_payload_and_owner()
    path_before = registry_path_values()
    policy_before = raw_policy()
    msi = candidate / next(item["name"] for item in manifest["artifacts"] if item["kind"] == "msi")
    log = logs / "user-clean-install-policy-refusal.log"
    # Deliberately request a normal clean install. Do not set DISABLEROLLBACK or
    # otherwise force an application-authored failure before the policy check.
    code = msi_execute(["/i", str(msi), *install_scope_arguments("user")], log)

    def cleanup_unexpected_install():
        # A clean success is a failed refusal test. Retire only the exact new
        # owned test product; never recursively delete its destination.
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registry, 0, winreg.KEY_READ | view) as key:
            require(winreg.QueryValueEx(key, "ProductCode")[0] == product,
                    "unexpected install has a different owner; automatic cleanup refused")
            require(Path(winreg.QueryValueEx(key, "InstallPath")[0]).resolve() == directory.resolve(),
                    "unexpected install has a different directory; automatic cleanup refused")
            require(winreg.QueryValueEx(key, "Owner")[0] == "msi",
                    "unexpected install is not MSI-owned; automatic cleanup refused")
        # The embedded guard independently checks the registration and exact
        # payload hashes before the actual owner's uninstall mutates anything.
        msi_call(["/x", product], logs / "unexpected-policy-install-cleanup.log")
        require_absent_payload_and_owner()

    try:
        require(code == 1625, f"expected Server policy refusal 1625, received {code}; see {log}")
        verify_policy_refusal(msi_log_text(log), product)
        require_absent_payload_and_owner()
        require(msi_product_state(product) == product_state,
                "policy refusal changed MSI product registration")
        require(registry_path_values() == path_before, "policy refusal changed PATH")
        require(raw_policy() == policy_before, "policy refusal changed the raw installer policy")
    finally:
        if code in (0, 3010, 1641):  # MSI success, including unexpected reboot variants
            cleanup_preserving_failure(cleanup_unexpected_install)
    return {"result": "clean_install_blocked_by_host_policy", "verified": True,
            "exit_code": code, "effective_disable_msi": 1,
            "raw_disable_msi_registry_before_and_after": policy_before,
            "product_type": sys.getwindowsversion().product_type,
            "unmanaged_non_admin_denial": True, "installation_transaction_entered": False,
            "payload_directory_absent_before_and_after": True,
            "wtop_owner_absent_before_and_after": True,
            "msi_product_registration_unchanged": True,
            "both_registry_path_values_unchanged": True,
            "log": log.name, "log_sha256": sha256(log.read_bytes())}


def protected_fixture_directory(directory):
    """Give this newly created disposable machine fixture a trusted owner.

    Production pre-existing untrusted directories are refused; the installer
    never rewrites their owner or DACL to make the preflight pass.
    """
    for path in (directory.parent, directory):
        subprocess.run(["icacls.exe", str(path), "/inheritance:r", "/grant:r",
                        "*S-1-5-18:(OI)(CI)F", "*S-1-5-32-544:(OI)(CI)F", "*S-1-5-32-545:(OI)(CI)RX"],
                       check=True, capture_output=True, timeout=30)
        subprocess.run(["icacls.exe", str(path), "/setowner", "*S-1-5-32-544"],
                       check=True, capture_output=True, timeout=30)


def run_scope(candidate: Path, manifest: dict, scope: str, logs: Path) -> dict:
    import winreg
    profile = manifest["payload"]["profile"]
    arch = manifest["payload"]["pe"]["architecture"]
    machine = scope == "machine"
    hive = winreg.HKEY_LOCAL_MACHINE if machine else winreg.HKEY_CURRENT_USER
    registry = "Software\\Wtop\\Installations\\" + profile
    view = winreg.KEY_WOW64_32KEY if arch == "x86" else winreg.KEY_WOW64_64KEY
    try:
        with winreg.OpenKey(hive, registry, 0, winreg.KEY_READ | view):
            raise AssertionError("refusing to touch an existing Wtop installation record")
    except FileNotFoundError:
        pass
    base = Path(os.environ["ProgramFiles(x86)" if arch == "x86" and machine else "ProgramFiles"]) if machine else Path(os.environ["LOCALAPPDATA"]) / "Programs"
    directory = base / "Wtop" / profile
    created_parent = not directory.parent.exists()
    created_directory = not directory.exists()
    if machine:
        require(created_parent, "machine ACL fixture refuses to change an existing Wtop directory")
    payload_files = ['wtop.exe', 'LICENSE']
    if third_party_notices_required(manifest['payload']['version']):
        payload_files.append('THIRD-PARTY-NOTICES.md')
    require(not any((directory / name).exists() for name in payload_files), "existing owned-name file blocks disposable test")
    directory.mkdir(parents=True, exist_ok=True)
    if machine:
        protected_fixture_directory(directory)
    sentinel = directory / ("foreign-" + uuid.uuid4().hex + ".txt")
    sentinel.write_text("foreign payload sentinel", encoding="utf-8")
    config_directory = Path(os.environ["APPDATA"]) / "Wtop"
    config_directory.mkdir(parents=True, exist_ok=True)
    config = config_directory / ("install-test-" + uuid.uuid4().hex + ".sentinel")
    config.write_text("user data outside payload", encoding="utf-8")
    msi = candidate / next(item["name"] for item in manifest["artifacts"] if item["kind"] == "msi")
    product = manifest["installer"]["product_code"]
    original = (candidate / "wtop.exe").read_bytes()
    modified = original + b"Wtop installer regression fixture\r\n"
    scope_arguments = install_scope_arguments(scope)
    foreign_value = "FixtureForeign-" + uuid.uuid4().hex
    installed = False
    path_before = registry_path_values()

    def cleanup_fixture():
        if installed:
            # Restore only this harness's just-installed fixture, then ask its
            # actual owner to remove it. Never recursively delete a payload root.
            (directory / "wtop.exe").write_bytes(original)
            msi_call(["/x", product], logs / f"{scope}-cleanup.log")
        for path in (sentinel, config):
            if path.exists():
                path.unlink()
        # Remove only empty directories which this fixture created. A foreign
        # addition causes rmdir to fail and is preserved; never recurse.
        for path, created in ((directory, created_directory), (directory.parent, created_parent)):
            if created and path.exists():
                try:
                    path.rmdir()
                except OSError:
                    pass

    try:
        msi_call(["/i", str(msi), *scope_arguments, "DISABLEROLLBACK=1"],
                 logs / f"{scope}-rollback-disabled-refusal.log", False, "requires Windows Installer rollback")
        if machine:
            with tempfile.TemporaryDirectory(prefix="wtop-untrusted-machine-") as unsafe:
                msi_call(["/i", str(msi), *scope_arguments, "INSTALLFOLDER=" + unsafe],
                         logs / "machine-untrusted-root-refusal.log", False, "qualified local path profile")
                require(not (Path(unsafe) / "wtop.exe").exists(), "machine MSI wrote an untrusted destination")
        msi_call(["/i", str(msi), *scope_arguments], logs / f"{scope}-install.log")
        installed = True
        require(msi_product_state(product) == 5, "fresh product is not fully installed")
        require(sha256((directory / "wtop.exe").read_bytes()) == manifest["payload"]["executable"]["sha256"], "installed EXE is not the exact portable payload")
        for name in payload_files:
            require((directory / name).read_bytes() == (candidate / name).read_bytes(), "installed legal/payload file differs: " + name)
        with winreg.OpenKey(hive, registry, 0, winreg.KEY_READ | winreg.KEY_SET_VALUE | view) as key:
            location, kind = winreg.QueryValueEx(key, "InstallPath")
            require(kind == winreg.REG_SZ and Path(location).resolve() == directory.resolve(), "ownership path does not identify actual payload directory")
            schema, kind = winreg.QueryValueEx(key, "SchemaVersion")
            require(schema == 1 and kind == winreg.REG_DWORD, "ownership schema is wrong")
            require(winreg.QueryValueEx(key, "ProductCode")[0] == product, "MSI product ownership differs")
            winreg.SetValueEx(key, foreign_value, 0, winreg.REG_SZ, "preserve foreign value")
        with tempfile.TemporaryDirectory(prefix="wtop-upgrade-probe-") as temp:
            probe = Path(temp) / "upgrade-probe.msi"
            rollback = Path(temp) / "rollback-probe.msi"
            same_version = Path(temp) / "same-version-probe.msi"
            upgrade_probe(msi, same_version, manifest["installer"]["msi_version"], delta=0)
            msi_call(["/i", str(same_version), *scope_arguments], logs / f"{scope}-same-version-refusal.log", False,
                     "ownership check refused")
            rollback_product = upgrade_probe(msi, rollback, manifest["installer"]["msi_version"], fail_during_execute=True)
            msi_call(["/i", str(rollback), *scope_arguments], logs / f"{scope}-rollback.log", False,
                     "Missing deferred ownership context.")
            verify_deferred_rollback_log(msi_log_text(logs / f"{scope}-rollback.log"), rollback_product["product_code"])
            require(msi_product_state(rollback_product["product_code"]) == -1,
                    "rollback left the failed newer MSI product registered")
            require(msi_product_state(product) == 5, "rollback did not restore the original MSI registration")
            for name in payload_files:
                require((directory / name).read_bytes() == (candidate / name).read_bytes(),
                        "rollback did not restore original payload/legal file: " + name)
            with winreg.OpenKey(hive, registry, 0, winreg.KEY_READ | view) as key:
                require(winreg.QueryValueEx(key, "ProductCode")[0] == product, "rollback did not restore original owner")
            # Missing owned bytes may be repaired; modified bytes may not.
            (directory / "wtop.exe").unlink()
            msi_call(["/fa", product], logs / f"{scope}-missing-repair.log")
            require((directory / "wtop.exe").read_bytes() == original, "repair changed exact payload")
            next_product = upgrade_probe(msi, probe, manifest["installer"]["msi_version"])
            msi_call(["/i", str(probe), *scope_arguments], logs / f"{scope}-upgrade.log")
            product = next_product["product_code"]
            require((directory / "wtop.exe").read_bytes() == original, "upgrade changed exact fixture payload")
            with winreg.OpenKey(hive, registry, 0, winreg.KEY_READ | view) as key:
                require(winreg.QueryValueEx(key, "ProductCode")[0] == product, "upgrade did not transfer ownership")
            msi_call(["/i", str(msi), *scope_arguments], logs / f"{scope}-downgrade-refusal.log", False,
                     "Downgrades are refused")
        # Keeping a native payload process alive must refuse maintenance, with
        # no forced process closure and no deferred reboot replacement.
        running = subprocess.Popen([str(directory / "wtop.exe"), "log", "--samples=100", "--output=-", "--interval=1000", "--no-config"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            import time
            time.sleep(0.2)
            require(running.poll() is None, "running-image fixture ended before maintenance")
            msi_call(["/fa", product], logs / f"{scope}-running-refusal.log", False, "payload file is in use")
            require(running.poll() is None, "installer terminated the running Wtop instance")
        finally:
            if running.poll() is None:
                running.terminate()
            running.communicate(timeout=10)
        (directory / "wtop.exe").write_bytes(modified)
        msi_call(["/fa", product], logs / f"{scope}-repair-refusal.log", False, "ownership check refused")
        require((directory / "wtop.exe").read_bytes() == modified, "repair replaced changed payload")
        msi_call(["/x", product], logs / f"{scope}-remove-refusal.log", False, "ownership check refused")
        require((directory / "wtop.exe").read_bytes() == modified, "uninstall removed changed payload")
        (directory / "wtop.exe").write_bytes(original)
        msi_call(["/x", product], logs / f"{scope}-remove.log")
        installed = False
        require(not any((directory / name).exists() for name in payload_files), "unchanged owned payload was not removed")
        require(sentinel.read_text(encoding="utf-8") == "foreign payload sentinel", "foreign file was removed")
        require(config.read_text(encoding="utf-8") == "user data outside payload", "user configuration sentinel was removed")
        with winreg.OpenKey(hive, registry, 0, winreg.KEY_READ | winreg.KEY_SET_VALUE | view) as key:
            require(winreg.QueryValueEx(key, foreign_value)[0] == "preserve foreign value", "foreign registry value was removed")
            try:
                winreg.QueryValueEx(key, "InstallPath")
                raise AssertionError("owned registration survived uninstall")
            except FileNotFoundError:
                pass
            winreg.DeleteValue(key, foreign_value)
        # A fresh install must not adopt even a byte-identical foreign EXE.
        winreg.DeleteKeyEx(hive, registry, access=view)
        (directory / "wtop.exe").write_bytes(original)
        msi_call(["/i", str(msi), *scope_arguments], logs / f"{scope}-collision-refusal.log", False, "ownership check refused")
        require((directory / "wtop.exe").read_bytes() == original, "fresh-install collision changed foreign bytes")
        (directory / "wtop.exe").unlink()
        msi_call(["/i", str(msi), *scope_arguments, "ADDTOPATH=1"], logs / f"{scope}-path-refusal.log", False,
                 "does not own PATH integration")
        require(registry_path_values() == path_before, "installer changed a PATH it does not own")
        return {"scope": scope, "passed": True, "exact_payload_sha256": sha256(original),
                "install_directory": str(directory), "modified_repair_refused": True,
                "modified_uninstall_refused": True, "major_upgrade_same_payload_fixture": True,
                "downgrade_refused": True, "rollback_after_script_execution_restored_owner": True,
                "rollback_restored_msi_registration": True, "rollback_no_failed_actions_skipped": True,
                "rollback_injection": "checked_deferred_guard_after_publish",
                "missing_file_repair_exact": True, "running_payload_refused_without_termination": True,
                "unowned_path_integration_refused": True, "both_registry_path_values_unchanged": True,
                "same_version_different_product_refused": True, "rollback_disabled_refused": True,
                "untrusted_machine_destination_refused": True if machine else None,
                "foreign_file_and_registry_value_preserved": True, "user_data_preserved": True}
    finally:
        cleanup_preserving_failure(cleanup_fixture)


def run_standard_account(candidate, manifest, observations):
    """Stage exact inputs for an independent, temporary standard-user profile."""
    sys.path.insert(0, str(ROOT / "packaging"))
    from standard_user_process import standard_user_fixture
    stage = Path(tempfile.mkdtemp(prefix="wtop-msi-standard-"))
    diagnostics = {}
    observations["standard_user_fixture"] = diagnostics
    source_files = ("tests/test_native_install.py", "tools/package_candidate.py",
                    "tools/inspect_pe.py", "tools/release_metadata.py",
                    "spec/catalog/targets.json", "packaging/native_token.py")

    def copy_exact(source, destination, bound):
        require(source.is_file() and not source.is_symlink(), "staged input is not a regular file: " + str(source))
        info = source.lstat()
        require(not (getattr(info, "st_file_attributes", 0) & 0x400), "staged input is a reparse point: " + str(source))
        require(info.st_size <= bound, "staged input exceeds its size bound: " + str(source))
        with source.open("rb") as stream:
            data = stream.read(bound + 1)
        require(len(data) <= bound and len(data) == info.st_size,
                "staged input changed size or exceeded its size bound: " + str(source))
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)  # inherit the owned staging ACL; never copy a caller's ACL
        require(destination.read_bytes() == data, "staged input bytes changed: " + str(source))
        display_path = destination.relative_to(stage) if destination.is_relative_to(stage) else destination
        return {"path": str(display_path), "size": len(data), "sha256": sha256(data)}

    with standard_user_fixture(stage, diagnostics, disposable_host=True) as account:
        require(account.user_sid != observations["token_context"]["user_sid"],
                "standard-user fixture reused the elevated caller's account")
        staged_root = stage / "repo"
        staged_candidate = stage / "candidate"
        diagnostics["staged_sources"] = [copy_exact(ROOT / name, staged_root / name, 1024 * 1024)
                                         for name in source_files]
        names = {"candidate.json", "payload.json", "SHA256SUMS", "LICENSE", "PORTABLE.txt", "wtop.conf.example"}
        names.update(item["name"] for item in manifest["artifacts"])
        if third_party_notices_required(manifest["payload"]["version"]):
            names.add("THIRD-PARTY-NOTICES.md")
        require(len(names) <= 32, "unexpected candidate staging inventory")
        diagnostics["staged_candidate"] = [copy_exact(candidate / name, staged_candidate / name, 128 * 1024 * 1024)
                                           for name in sorted(names)]
        require(verify(staged_candidate) == manifest, "standard-user staging changed candidate provenance")
        receipt_name = "native-install-observations-standard-user.json"
        log_name = "native-install-logs-standard-user"

        def collect_evidence():
            captured = False
            try:
                receipt = staged_candidate / receipt_name
                if receipt.exists():
                    copy_exact(receipt, candidate / receipt_name, 4 * 1024 * 1024)
                    ordinary = json.loads((candidate / receipt_name).read_text(encoding="utf-8"))
                    require(isinstance(ordinary, dict) and ordinary.get("schema") == "wtop.install-observations/2",
                            "standard-user child did not leave the expected completion receipt")
                    observations["standard_user"] = ordinary
                    captured = True
                    if ordinary.get("cleanup_failure_traceback"):
                        account.retain_for_diagnosis("child MSI cleanup did not complete; retain its profile and registration")
            finally:
                if not captured:
                    account.retain_for_diagnosis("child completion receipt is absent or invalid; external MSI retirement is unproven")
            log_directory = staged_candidate / log_name
            if log_directory.exists():
                require(log_directory.is_dir() and not log_directory.is_symlink() and
                        not (getattr(log_directory.lstat(), "st_file_attributes", 0) & 0x400),
                        "standard-user log directory is not an ordinary owned directory")
                logs = sorted(log_directory.glob("*.log"))
                require(len(logs) <= 32, "standard-user log inventory exceeds its bound")
                for log in logs:
                    copy_exact(log, candidate / log_name / log.name, 16 * 1024 * 1024)

        command = [sys.executable, str(staged_root / "tests/test_native_install.py"), str(staged_candidate),
                   "--disposable-host", "--require-standard-token", "--expected-user-sid=" + account.user_sid,
                   "--label=standard-user"]
        try:
            code = account.run(command, cwd=staged_root, timeout=900)
            diagnostics["child_exit_code"] = code
        finally:
            cleanup_preserving_failure(collect_evidence)
        ordinary = observations.get("standard_user", {})
        token = ordinary.get("token_context", {})
        require(code == 0 and not ordinary.get("failure") and
                token.get("is_user_an_admin") is False and token.get("token_elevation") is False and
                token.get("integrity_rid") == 8192 and token.get("user_sid") == account.user_sid and
                token.get("token_has_restrictions") is False,
                "fresh standard-user MSI fixture failed; retain its independent logs")
        require(ordinary.get("execution_context") == "fresh_standard_local_account" and
                any(row.get("scope") == "user" and row.get("passed") is True
                    for row in ordinary.get("scope_results", [])),
                "fresh standard user did not complete the full installation lifecycle")
        require(ordinary.get("payload_sha256") == manifest["payload"]["executable"]["sha256"] and
                ordinary.get("installer_sha256") == observations["installer_sha256"] and
                ordinary.get("source_revision") == observations["source_revision"],
                "fresh standard-user receipt is not bound to the exact candidate")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--disposable-host", action="store_true", required=True)
    parser.add_argument("--machine", action="store_true")
    context = parser.add_mutually_exclusive_group()
    context.add_argument("--restricted-user", action="store_true", help="verify Server policy in a same-user restricted stress token")
    context.add_argument("--standard-user", action="store_true", help="repeat full per-user lifecycle in a fresh temporary standard account")
    parser.add_argument("--expect-restricted-user-policy-refusal", action="store_true",
                        help="Server-only: verify clean-install DisableMSI=1 refusal instead of claiming ordinary-user lifecycle acceptance")
    parser.add_argument("--require-ordinary-token", action="store_true")
    parser.add_argument("--require-standard-token", action="store_true")
    parser.add_argument("--expected-user-sid", default="")
    parser.add_argument("--label", default="", choices=("", "restricted-user", "standard-user"))
    args = parser.parse_args(argv)
    if args.expect_restricted_user_policy_refusal and not (args.restricted_user or args.require_ordinary_token):
        parser.error("expected restricted-user policy refusal requires --restricted-user")
    if args.restricted_user and not args.expect_restricted_user_policy_refusal:
        parser.error("restricted-token MSI context is a Server policy stress probe; use --standard-user for client acceptance")
    if args.require_standard_token and (not args.expected_user_sid or args.label != "standard-user"):
        parser.error("standard-account child requires its expected SID and label")
    if os.name != "nt":
        parser.error("native installer execution requires Windows")
    candidate = args.candidate.resolve()
    suffix = "-" + args.label if args.label else ""
    logs = candidate / ("native-install-logs" + suffix)
    logs.mkdir(exist_ok=True)
    observations = {"schema": "wtop.install-observations/2", "kernel": sys.getwindowsversion().build,
                    "product_type": sys.getwindowsversion().product_type,
                    "token_context": None,
                    "scope_results": [], "scope_attempt_order": [], "qualified": False,
                    "limits": ["hostile same-user preflight/deferred-recheck-to-mutation race",
                               "standard-user UAC boundary when runner elevated", "power loss/restart recovery",
                               "host matrix", "real prior-release application/version upgrade",
                               "optional owned PATH alias transaction", "same-profile scope coexistence"]}
    try:
        sys.path.insert(0, str(ROOT / "packaging"))
        from native_token import current_token_context
        observations["token_context"] = token = current_token_context()
        if args.require_ordinary_token:
            require(not token["is_user_an_admin"] and token["integrity_rid"] == 8192,
                    "restricted stress child is not medium integrity without administrator membership")
            observations["execution_context"] = "same_user_restricted_medium_token_stress"
        if args.require_standard_token:
            require(not token["is_user_an_admin"] and not token["token_elevation"] and
                    token["integrity_rid"] == 8192 and token["user_sid"] == args.expected_user_sid and
                    token["token_has_restrictions"] is False,
                    "fresh-account child does not have the required genuine standard-user token")
            user_profile = Path(os.environ["USERPROFILE"]).resolve()
            require(Path(os.environ["APPDATA"]).resolve().is_relative_to(user_profile) and
                    Path(os.environ["LOCALAPPDATA"]).resolve().is_relative_to(user_profile),
                    "fresh-account child inherited another user's configuration environment")
            observations["execution_context"] = "fresh_standard_local_account"
            observations["user_profile"] = str(user_profile)
        manifest = verify(candidate)
        require(manifest["installer"].get("built"), "candidate has no MSI")
        observations["payload_sha256"] = manifest["payload"]["executable"]["sha256"]
        observations["source_revision"] = manifest['payload']['source']['revision']
        observations["version"] = manifest['payload']['version']
        observations["profile"] = manifest['payload']['profile']
        observations["msi_version"] = manifest['installer']['msi_version']
        observations["installer_sha256"] = next(item['sha256'] for item in manifest['artifacts'] if item['kind'] == 'msi')
        if args.standard_user:
            require(token["is_user_an_admin"] and token["token_elevation"],
                    "fresh-account fixture requires the disposable elevated parent")
            # Keep fresh-account acceptance ahead of all caller MSI APIs. The
            # ctypes-loaded MSI library can outlive its immediate query. A
            # later different-user probe can inherit that session's client
            # preconditioning, which is a separate coexistence qualification.
            observations["scope_attempt_order"].append("fresh_standard_account_per_user")
            run_standard_account(candidate, manifest, observations)
        if args.require_ordinary_token and args.expect_restricted_user_policy_refusal:
            observations["scope_attempt_order"].append("restricted_server_policy_refusal")
            observations["policy_refusal"] = run_server_policy_probe(candidate, manifest, logs)
            observations["policy_refusal_verified"] = True
        else:
            observations["scope_attempt_order"].append("current_account_per_user")
            observations["scope_results"].append(run_scope(candidate, manifest, "user", logs))
        if args.machine:
            require(token["is_user_an_admin"] and token["token_elevation"], "machine fixture requires an elevated disposable runner")
            observations["scope_attempt_order"].append("current_account_per_machine")
            observations["scope_results"].append(run_scope(candidate, manifest, "machine", logs))
        if args.restricted_user:
            require(token["is_user_an_admin"] and token["token_elevation"], "restricted-token launcher requires the disposable elevated parent")
            observations["scope_attempt_order"].append("same_user_restricted_server_policy")
            sys.path.insert(0, str(ROOT / "packaging"))
            from restricted_process import run_restricted
            command = [sys.executable, str(Path(__file__).resolve()), str(candidate),
                "--disposable-host", "--require-ordinary-token", "--label=restricted-user"]
            if args.expect_restricted_user_policy_refusal:
                command.append("--expect-restricted-user-policy-refusal")
            code = run_restricted(command, ROOT, timeout=900)
            ordinary = json.loads((candidate / "native-install-observations-restricted-user.json").read_text())
            observations["restricted_user"] = ordinary
            require(code == 0 and not ordinary.get("failure") and
                    ordinary.get("token_context", {}).get("is_user_an_admin") is False,
                    "restricted-token Server policy fixture failed; retain its independent logs")
            if args.expect_restricted_user_policy_refusal:
                require(ordinary.get("policy_refusal_verified") is True and not ordinary.get("scope_results"),
                        "Server child did not verify policy refusal separately from installation")
    except (OSError, ValueError, AssertionError, RuntimeError, subprocess.SubprocessError) as exc:
        observations["failure"] = str(exc)
        observations["failure_traceback"] = traceback.format_exc()
        if hasattr(exc, "fixture_cleanup_failure"):
            observations["cleanup_failure_traceback"] = exc.fixture_cleanup_failure
        traceback.print_exc()
        return 1
    finally:
        (candidate / ("native-install-observations" + suffix + ".json")).write_text(json.dumps(observations, indent=2) + "\n", encoding="utf-8")
    if observations.get("policy_refusal_verified"):
        print("Native Server policy refusal verified; no ordinary-user installation was accepted.")
    else:
        print("Native MSI fixture checks passed; preservation/race and release qualification remain pending.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
