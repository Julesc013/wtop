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
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package_candidate import sha256, verify


def require(condition, message):
    if not condition:
        raise AssertionError(message)


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


def upgrade_probe(original: Path, destination: Path, version: str):
    """Make a local test-only related MSI by changing its product/package IDs.

    Cabinet/application bytes are untouched. This is never uploaded as a
    deliverable; it exercises the package's explicit automatic-upgrade refusal.
    """
    shutil.copyfile(original, destination)
    api = ctypes.WinDLL("msi", use_last_error=True)
    handle = ctypes.c_uint
    api.MsiOpenDatabaseW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.POINTER(handle)]
    api.MsiDatabaseOpenViewW.argtypes = [handle, ctypes.c_wchar_p, ctypes.POINTER(handle)]
    api.MsiViewExecute.argtypes = [handle, handle]
    api.MsiViewClose.argtypes = [handle]
    api.MsiCloseHandle.argtypes = [handle]
    api.MsiDatabaseCommit.argtypes = [handle]
    api.MsiGetSummaryInformationW.argtypes = [handle, ctypes.c_wchar_p, ctypes.c_uint, ctypes.POINTER(handle)]
    api.MsiSummaryInfoSetPropertyW.argtypes = [handle, ctypes.c_uint, ctypes.c_uint, ctypes.c_int, ctypes.c_void_p, ctypes.c_wchar_p]
    api.MsiSummaryInfoPersist.argtypes = [handle]
    database = handle()
    require(api.MsiOpenDatabaseW(str(destination), ctypes.c_void_p(1), ctypes.byref(database)) == 0, "cannot open test MSI transaction")
    try:
        product = "{" + str(uuid.uuid4()).upper() + "}"
        major, minor, build = map(int, version.split("."))
        require(build < 65535, "upgrade probe requires a spare candidate MSI build number")
        new_version = f"{major}.{minor}.{build + 1}"
        for key, value in (("ProductCode", product), ("ProductVersion", new_version)):
            view = handle()
            require(api.MsiDatabaseOpenViewW(database, f"UPDATE `Property` SET `Value`='{value}' WHERE `Property`='{key}'", ctypes.byref(view)) == 0, "cannot author test MSI property")
            try:
                require(api.MsiViewExecute(view, 0) == 0, "cannot update test MSI property")
            finally:
                api.MsiViewClose(view)
                api.MsiCloseHandle(view)
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
    require(not (directory / "wtop.exe").exists() and not (directory / "LICENSE").exists(), "existing owned-name file blocks disposable test")
    directory.mkdir(parents=True, exist_ok=True)
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
    scope_arguments = ["ALLUSERS=2", "MSIINSTALLPERUSER=" + ("" if machine else "1")]
    foreign_value = "FixtureForeign-" + uuid.uuid4().hex
    installed = False
    try:
        msi_call(["/i", str(msi), *scope_arguments], logs / f"{scope}-install.log")
        installed = True
        require(sha256((directory / "wtop.exe").read_bytes()) == manifest["payload"]["executable"]["sha256"], "installed EXE is not the exact portable payload")
        with winreg.OpenKey(hive, registry, 0, winreg.KEY_READ | winreg.KEY_SET_VALUE | view) as key:
            location, kind = winreg.QueryValueEx(key, "InstallPath")
            require(kind == winreg.REG_SZ and Path(location).resolve() == directory.resolve(), "ownership path does not identify actual payload directory")
            schema, kind = winreg.QueryValueEx(key, "SchemaVersion")
            require(schema == 1 and kind == winreg.REG_DWORD, "ownership schema is wrong")
            require(winreg.QueryValueEx(key, "ProductCode")[0] == product, "MSI product ownership differs")
            winreg.SetValueEx(key, foreign_value, 0, winreg.REG_SZ, "preserve foreign value")
        with tempfile.TemporaryDirectory(prefix="wtop-upgrade-probe-") as temp:
            probe = Path(temp) / "upgrade-probe.msi"
            upgrade_probe(msi, probe, manifest["installer"]["msi_version"])
            msi_call(["/i", str(probe), *scope_arguments], logs / f"{scope}-upgrade-refusal.log", False,
                     "Candidate automatic upgrades are blocked")
        (directory / "wtop.exe").write_bytes(modified)
        msi_call(["/fa", product], logs / f"{scope}-repair-refusal.log", False, "ownership check refused")
        require((directory / "wtop.exe").read_bytes() == modified, "repair replaced changed payload")
        msi_call(["/x", product], logs / f"{scope}-remove-refusal.log", False, "ownership check refused")
        require((directory / "wtop.exe").read_bytes() == modified, "uninstall removed changed payload")
        (directory / "wtop.exe").write_bytes(original)
        msi_call(["/x", product], logs / f"{scope}-remove.log")
        installed = False
        require(not (directory / "wtop.exe").exists() and not (directory / "LICENSE").exists(), "unchanged owned payload was not removed")
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
        return {"scope": scope, "passed": True, "exact_payload_sha256": sha256(original),
                "install_directory": str(directory), "modified_repair_refused": True,
                "modified_uninstall_refused": True, "automatic_upgrade_refused": True,
                "foreign_file_and_registry_value_preserved": True, "user_data_preserved": True}
    finally:
        if installed:
            # Restore only this harness's just-installed fixture, then ask its
            # actual owner to remove it. Never recursively delete a payload root.
            (directory / "wtop.exe").write_bytes(original)
            msi_call(["/x", product], logs / f"{scope}-cleanup.log")
        for path in (sentinel, config):
            if path.exists():
                path.unlink()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--disposable-host", action="store_true", required=True)
    parser.add_argument("--machine", action="store_true")
    args = parser.parse_args(argv)
    if os.name != "nt":
        parser.error("native installer execution requires Windows")
    candidate = args.candidate.resolve()
    logs = candidate / "native-install-logs"
    logs.mkdir(exist_ok=True)
    observations = {"schema": "wtop.install-observations/1", "kernel": sys.getwindowsversion().build,
                    "token_elevated": bool(ctypes.windll.shell32.IsUserAnAdmin()),
                    "scope_results": [], "qualified": False,
                    "limits": ["preflight-to-mutation race", "standard-user UAC boundary when runner elevated",
                               "interrupted transaction/rollback", "host matrix", "automatic upgrade implementation"]}
    try:
        manifest = verify(candidate)
        require(manifest["installer"].get("built"), "candidate has no MSI")
        observations["payload_sha256"] = manifest["payload"]["executable"]["sha256"]
        observations["scope_results"].append(run_scope(candidate, manifest, "user", logs))
        if args.machine:
            require(observations["token_elevated"], "machine fixture requires an elevated disposable runner")
            observations["scope_results"].append(run_scope(candidate, manifest, "machine", logs))
    except (OSError, ValueError, AssertionError, subprocess.SubprocessError) as exc:
        observations["failure"] = str(exc)
        print(str(exc), file=sys.stderr)
        return 1
    finally:
        (candidate / "native-install-observations.json").write_text(json.dumps(observations, indent=2) + "\n", encoding="utf-8")
    print("Native MSI fixture checks passed; preservation/race and release qualification remain pending.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
