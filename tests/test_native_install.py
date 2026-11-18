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


def msi_call(arguments, log: Path, expect_success=True, expected_reason=None):
    completed = subprocess.run(["msiexec.exe", *arguments, "/qn", "/norestart", "/L*V", str(log)],
                               timeout=120, capture_output=True, text=True)
    raw = log.read_bytes() if log.exists() else b""
    encoding = "utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else ("utf-16-le" if b"\0" in raw[:128] else "utf-8")
    text = raw.decode(encoding, errors="replace")
    if expect_success:
        require(completed.returncode == 0, f"msiexec failed ({completed.returncode}); see {log}")
    else:
        require(completed.returncode == 1603, f"expected installer refusal 1603, received {completed.returncode}; see {log}")
        if expected_reason:
            require(expected_reason in text, f"refusal did not report the expected guard: {log}")
    return completed.returncode


def upgrade_probe(original: Path, destination: Path, version: str, delta=1, fail_after_execute=False):
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
        if fail_after_execute:
            # Test-copy only. InstallExecute applies the generated transaction;
            # the following type-19 action then requests failure/real rollback.
            statements += [
                "INSERT INTO `CustomAction` (`Action`,`Type`,`Target`) VALUES ('WtopFixtureFail',19,'Wtop fixture rollback after script execution')",
                "INSERT INTO `InstallExecuteSequence` (`Action`,`Sequence`) VALUES ('InstallExecute',6500)",
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
            upgrade_probe(msi, rollback, manifest["installer"]["msi_version"], fail_after_execute=True)
            msi_call(["/i", str(rollback), *scope_arguments], logs / f"{scope}-rollback.log", False,
                     "Wtop fixture rollback after script execution")
            require((directory / "wtop.exe").read_bytes() == original, "rollback did not restore original payload")
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
                "missing_file_repair_exact": True, "running_payload_refused_without_termination": True,
                "unowned_path_integration_refused": True, "both_registry_path_values_unchanged": True,
                "same_version_different_product_refused": True, "rollback_disabled_refused": True,
                "untrusted_machine_destination_refused": True if machine else None,
                "foreign_file_and_registry_value_preserved": True, "user_data_preserved": True}
    finally:
        cleanup_preserving_failure(cleanup_fixture)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--disposable-host", action="store_true", required=True)
    parser.add_argument("--machine", action="store_true")
    parser.add_argument("--restricted-user", action="store_true", help="also repeat per-user fixtures with a medium restricted token")
    parser.add_argument("--require-ordinary-token", action="store_true")
    parser.add_argument("--label", default="", choices=("", "restricted-user"))
    args = parser.parse_args(argv)
    if os.name != "nt":
        parser.error("native installer execution requires Windows")
    candidate = args.candidate.resolve()
    suffix = "-" + args.label if args.label else ""
    logs = candidate / ("native-install-logs" + suffix)
    logs.mkdir(exist_ok=True)
    observations = {"schema": "wtop.install-observations/1", "kernel": sys.getwindowsversion().build,
                    "token_elevated": bool(ctypes.windll.shell32.IsUserAnAdmin()),
                    "scope_results": [], "qualified": False,
                    "limits": ["hostile same-user preflight/deferred-recheck-to-mutation race",
                               "standard-user UAC boundary when runner elevated", "power loss/restart recovery",
                               "host matrix", "real prior-release application/version upgrade",
                               "optional owned PATH alias transaction", "same-profile scope coexistence"]}
    try:
        if args.require_ordinary_token:
            require(not observations["token_elevated"], "restricted child unexpectedly retains administrator membership")
            observations["execution_context"] = "medium_restricted_token_same_user_not_human_UAC_review"
        manifest = verify(candidate)
        require(manifest["installer"].get("built"), "candidate has no MSI")
        observations["payload_sha256"] = manifest["payload"]["executable"]["sha256"]
        observations["source_revision"] = manifest['payload']['source']['revision']
        observations["version"] = manifest['payload']['version']
        observations["profile"] = manifest['payload']['profile']
        observations["msi_version"] = manifest['installer']['msi_version']
        observations["installer_sha256"] = next(item['sha256'] for item in manifest['artifacts'] if item['kind'] == 'msi')
        observations["scope_results"].append(run_scope(candidate, manifest, "user", logs))
        if args.machine:
            require(observations["token_elevated"], "machine fixture requires an elevated disposable runner")
            observations["scope_results"].append(run_scope(candidate, manifest, "machine", logs))
        if args.restricted_user:
            require(observations["token_elevated"], "restricted-token launcher requires the disposable elevated parent")
            sys.path.insert(0, str(ROOT / "packaging"))
            from restricted_process import run_restricted
            code = run_restricted([sys.executable, str(Path(__file__).resolve()), str(candidate),
                "--disposable-host", "--require-ordinary-token", "--label=restricted-user"], ROOT, timeout=900)
            ordinary = json.loads((candidate / "native-install-observations-restricted-user.json").read_text())
            observations["restricted_user"] = ordinary
            require(code == 0 and not ordinary.get("failure") and ordinary.get("token_elevated") is False,
                    "restricted ordinary-user MSI fixture failed; retain its independent logs")
    except (OSError, ValueError, AssertionError, subprocess.SubprocessError) as exc:
        observations["failure"] = str(exc)
        observations["failure_traceback"] = traceback.format_exc()
        if hasattr(exc, "fixture_cleanup_failure"):
            observations["cleanup_failure_traceback"] = exc.fixture_cleanup_failure
        traceback.print_exc()
        return 1
    finally:
        (candidate / ("native-install-observations" + suffix + ".json")).write_text(json.dumps(observations, indent=2) + "\n", encoding="utf-8")
    print("Native MSI fixture checks passed; preservation/race and release qualification remain pending.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
