#!/usr/bin/env python3
"""Build a WiX 4.0.6 candidate wrapper around an already verified payload."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package_candidate import PackageError, checksum_file, json_bytes, sha256, verify
from inspect_pe import inspect_file
from release_metadata import msi_version as mapped_msi_version

NAMESPACE = uuid.UUID("c58bab6f-f4af-5ffb-b37b-8b0c08915594")
WIX_VERSION = "4.0.6"


def guid(name: str) -> str:
    return "{" + str(uuid.uuid5(NAMESPACE, name)).upper() + "}"


def msi_version(version: str) -> str:
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", version):
        raise PackageError("MSI version requires exactly three numeric fields; no truncated SemVer")
    major, minor, build = map(int, version.split("."))
    if major > 255 or minor > 255 or build > 65535:
        raise PackageError("MSI version exceeds its field ranges")
    return version


def write_guard_config(candidate: Path, stage: Path, manifest: dict) -> Path:
    profile = manifest["payload"]["profile"]
    if not re.fullmatch(r"win-nt-(?:x86|x64|arm64)", profile):
        raise PackageError("unsupported MSI profile")
    lines = ["/* Generated from exact candidate bytes; do not edit. */"]
    entries = []
    for i, name in enumerate(("wtop.exe", "LICENSE")):
        data = (candidate / name).read_bytes()
        digest = sha256(data)
        if len(data) > 64 * 1024 * 1024:
            raise PackageError("guarded file exceeds bounded payload profile")
        values = ",".join("0x" + digest[j:j + 2] for j in range(0, len(digest), 2))
        lines.append(f"static const BYTE wtop_guard_hash_{i}[32] = {{{values}}};")
        entries.append(f'    {{L"{name}", {len(data)}UL, wtop_guard_hash_{i}}}')
    lines += ['static const wtop_guard_file wtop_guard_files[] = {', ",\n".join(entries), "};",
        f'static const WCHAR wtop_guard_registry_key[] = L"Software\\\\Wtop\\\\Installations\\\\{profile}";',
        f'static const WCHAR wtop_guard_payload_hex[] = L"{manifest["payload"]["executable"]["sha256"]}";']
    stage.mkdir(parents=True, exist_ok=True)
    path = stage / "guard_config.h"
    path.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    return path


def build(candidate: Path, stage: Path, wix: Path, numeric_version: str | None = None) -> dict:
    manifest = verify(candidate)
    if manifest["installer"].get("built"):
        raise PackageError("this candidate already has an MSI; use a new empty candidate output")
    mapped_version = mapped_msi_version(manifest['payload']['version'])
    version = msi_version(numeric_version) if numeric_version is not None else mapped_version
    output = subprocess.run([str(wix), "--version"], check=True, capture_output=True, text=True).stdout.strip()
    if output.split("+")[0] != WIX_VERSION:
        raise PackageError(f"requires WiX {WIX_VERSION}, received {output!r}; no tool upgrades are implicit")
    payload = manifest["payload"]
    profile, arch = payload["profile"], payload["pe"]["architecture"]
    if stage.exists() and any(stage.iterdir()):
        raise PackageError("MSI staging directory must be empty")
    stage.mkdir(parents=True, exist_ok=True)
    write_guard_config(candidate, stage, manifest)
    cmake_arch = {"x86": "Win32", "x64": "x64", "arm64": "ARM64"}[arch]
    commands = [
        ["cmake", "-S", str(ROOT / "packaging/msi"), "-B", str(stage / "guard"),
         "-A", cmake_arch, "-DWTOP_GUARD_CONFIG_DIR=" + str(stage.resolve())],
        ["cmake", "--build", str(stage / "guard"), "--config", "Release", "--parallel", "2"],
    ]
    for command in commands:
        subprocess.run(command, check=True)
    guard = stage / "guard/Release/wtop_installer_guard.dll"
    guard_pe = inspect_file(guard)
    if not guard_pe["dll"] or guard_pe["architecture"] != arch:
        raise PackageError("embedded guard architecture/format differs from MSI profile")
    product = guid("candidate-product:" + profile + ":" + payload["version"] + ":" + version + ":" + payload["executable"]["sha256"])
    upgrade = guid("upgrade:" + profile)
    values = {
        "Profile": profile, "MsiVersion": version, "ProductCode": product, "UpgradeCode": upgrade,
        "PayloadComponent": guid("payload:" + profile), "LicenseComponent": guid("license:" + profile),
        "ProgramFilesDirectory": "ProgramFilesFolder" if arch == "x86" else "ProgramFiles64Folder",
        "Bitness": "always32" if arch == "x86" else "always64",
        "PayloadExe": str((candidate / "wtop.exe").resolve()),
        "PayloadHash": payload["executable"]["sha256"],
        "LicenseFile": str((candidate / "LICENSE").resolve()), "GuardDll": str(guard.resolve()),
    }
    name = f'wtop-{payload["version"]}-{profile}-candidate.msi'
    command = [str(wix), "build", str(ROOT / "packaging/msi/wtop.wxs"), "-arch", arch,
               "-out", str((candidate / name).resolve())]
    for key, value in values.items():
        command += ["-d", key + "=" + value]
    commands.append(command)
    subprocess.run(command, check=True)
    # Recheck both the inputs and embedded guard after the build; packaging must
    # never silently change the already-qualified application bytes.
    if sha256((candidate / "wtop.exe").read_bytes()) != payload["executable"]["sha256"]:
        raise PackageError("payload changed while building the wrapper")
    installer_bytes = (candidate / name).read_bytes()
    manifest["artifacts"].append({"name": name, "kind": "msi", "sha256": sha256(installer_bytes), "size": len(installer_bytes)})
    manifest["installer"] = {
        "built": True, "qualified": False, "product_code": product, "upgrade_code": upgrade,
        "msi_version": version, "semantic_version": payload["version"], "wix": output,
        "version_mapping": "wtop.version/1" if version == mapped_version else "explicit_candidate_override",
        "scope": "perUserOrMachine", "automatic_upgrade": "blocked", "path_integration": "absent",
        "guard": {"sha256": sha256(guard.read_bytes()), "pe": guard_pe,
                  "configuration_sha256": sha256((stage / "guard_config.h").read_bytes())},
        "commands": commands,
        "preservation": "preflight refusal for detected changes; not an atomic compare-and-delete or replace",
        "remaining": ["validation_to_file_operation_race", "standard_user_and_native_matrix",
                      "rollback_and_power_loss", "upgrade_transaction"],
    }
    manifest["promotion"]["blockers"] += ["msi_preservation_unqualified", "automatic_upgrade_not_implemented"]
    if version != mapped_version:
        manifest["promotion"]["blockers"].append("candidate_msi_version_override")
    (candidate / "candidate.json").write_bytes(json_bytes(manifest))
    checksum_file(candidate, manifest)
    verify(candidate)
    return manifest


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--wix", required=True, type=Path)
    parser.add_argument("--msi-version", help="optional candidate-only override; default is the bounded shared SemVer ledger")
    args = parser.parse_args(argv)
    try:
        manifest = build(args.candidate.resolve(), args.stage.resolve(), args.wix.resolve(), args.msi_version)
        print(json.dumps({"installer": manifest["installer"], "promotion": manifest["promotion"]}, indent=2))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        print(f"MSI candidate build failed: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
