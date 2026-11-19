#!/usr/bin/env python3
"""Package an already built PE without rebuilding, signing, publishing or relinking."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

from inspect_pe import PEError, inspect_bytes

ROOT = Path(__file__).resolve().parents[1]
from release_metadata import SEMVER, metadata
REVISION = re.compile(r"[0-9a-f]{40}(?:[0-9a-f]{24})?\Z")


class PackageError(ValueError):
    pass


def third_party_notices_required(version: str) -> bool:
    """Keep immutable Beta 1/2 carriers readable; Beta 3 links utf8proc."""
    info = metadata(version)
    core = tuple(info['native_version'][:3])
    return core > (0, 4, 0) or (core == (0, 4, 0) and not
        (info['channel'] == 'beta' and info['ordinal'] <= 2))


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n").encode("utf-8")


def target_for(profile: str, root: Path = ROOT) -> dict:
    catalog = json.loads((root / "spec/catalog/targets.json").read_text(encoding="utf-8"))
    targets = [target for target in catalog["targets"] if target["profile"] == profile]
    if len(targets) != 1 or profile not in ("win-nt-x86", "win-nt-x64", "win-nt-arm64"):
        raise PackageError("profile is not an implemented modern PE candidate in the target catalog")
    return targets[0]


def validate_identity(data: bytes, target: dict) -> dict:
    report = inspect_bytes(data)
    if report["architecture"] != target["architecture"] or report["format"] != target["format"]:
        raise PackageError("PE architecture/format does not match the canonical profile")
    if not report["executable"] or report["dll"] or report["subsystem"] != 3:
        raise PackageError("payload must be a Windows console executable")
    if not report["imports"]:
        raise PackageError("candidate has no native import inventory")
    return report


def source_identity(root: Path) -> dict:
    def git(*args):
        return subprocess.run(["git", "-C", str(root), *args], check=True,
                              capture_output=True, text=True).stdout.strip()
    revision = git("rev-parse", "HEAD")
    if not REVISION.fullmatch(revision):
        raise PackageError("git did not return an exact source revision")
    # The working tree can change during a build. This records packaging-time
    # state and never claims that a hash alone establishes build provenance.
    return {"revision": revision, "dirty": bool(git("status", "--porcelain")),
            "tree": git("rev-parse", "HEAD^{tree}"), "binding": "reported-build-input"}


def zip_bytes(entries: dict[str, bytes], destination: Path) -> None:
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def checksum_file(directory: Path, manifest: dict) -> None:
    names = [entry["name"] for entry in manifest["artifacts"]] + ["candidate.json"]
    lines = [f"{sha256((directory / name).read_bytes())}  {name}\n" for name in sorted(set(names))]
    (directory / "SHA256SUMS").write_text("".join(lines), encoding="ascii", newline="\n")


def package(executable: Path, output: Path, profile: str, provenance: dict,
            root: Path = ROOT, source: dict | None = None) -> dict:
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not SEMVER.fullmatch(version):
        raise PackageError("VERSION must be a supported unambiguous SemVer")
    target = target_for(profile, root)
    if executable.stat().st_size > 64 * 1024 * 1024:
        raise PackageError("payload exceeds candidate size bound")
    data = executable.read_bytes()
    pe = validate_identity(data, target)
    source = source or source_identity(root)
    if not REVISION.fullmatch(str(source.get("revision", ""))) or type(source.get("dirty")) is not bool:
        raise PackageError("provenance requires an exact source revision and explicit dirty boolean")
    required = ("compiler", "linker", "sdk", "runtime", "flags", "build_command")
    if any(key not in provenance or not provenance[key] for key in required):
        raise PackageError("build provenance requires compiler, linker, SDK, runtime, flags and build command")
    if output.exists() and any(output.iterdir()):
        raise PackageError("candidate output directory must be empty; existing artifacts are never overwritten")
    output.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": "wtop.payload/1", "product": "wtop", "version": version,
        "profile": profile, "target_id": target["id"], "source": source,
        "build": provenance, "executable": {"name": "wtop.exe", "sha256": sha256(data), "size": len(data)},
        "pe": pe, "release_supported": False,
    }
    portable_notes = (
        "Wtop development candidate: not a qualified release.\r\n"
        "The executable bytes are unchanged from the recorded build.\r\n"
        "Run wtop.exe --portable for explicit adjacent portable settings.\r\n"
        "No startup registration, PATH edit, service or updater is created.\r\n"
        "wtop.conf.example is not loaded automatically. Keep configuration outside managed payloads.\r\n"
        "See payload.json for exact source, compiler, PE imports and qualification limits.\r\n"
        "Verify SHA256SUMS against a separately trusted digest/source before use.\r\n"
        "GPL-3.0-only; upstream source: https://github.com/gsass1/NTop\r\n"
        "Wtop source: https://github.com/Julesc013/wtop (exact revision in payload.json).\r\n"
        "A development archive is not a completed GPL corresponding-source delivery audit.\r\n"
    ).encode("ascii")
    entries = {"wtop.exe": data, "LICENSE": (root / "LICENSE").read_bytes(),
               "payload.json": json_bytes(payload), "PORTABLE.txt": portable_notes,
               "wtop.conf.example": (root / "source/wtop.conf").read_bytes()}
    if third_party_notices_required(version):
        notices = (root / 'THIRD-PARTY-NOTICES.md').read_bytes()
        if not notices:
            raise PackageError('third-party notices must not be empty')
        entries['THIRD-PARTY-NOTICES.md'] = notices
        payload['legal_files'] = [{'name': 'THIRD-PARTY-NOTICES.md', 'size': len(notices), 'sha256': sha256(notices)}]
        entries['payload.json'] = json_bytes(payload)
    archive_name = f"wtop-{version}-{profile}-portable.zip"
    for name, content in entries.items():
        (output / name).write_bytes(content)
    zip_bytes(entries, output / archive_name)
    blockers = ["native_exact_artifact_matrix_incomplete", "human_tui_acceptance_pending",
                "signing_and_corresponding_source_audit_pending", "delivered_channel_acquisition_pending"]
    if source["dirty"]:
        blockers.append("source_tree_dirty")
    if pe["runtime_dlls"]:
        blockers.append("dynamic_runtime_floor_unqualified")
    manifest = {
        "schema": "wtop.candidate/1", "payload": payload,
        "artifacts": [{"name": name, "sha256": sha256((output / name).read_bytes()),
                       "size": (output / name).stat().st_size, "kind": kind}
                      for name, kind in (("wtop.exe", "standalone"), (archive_name, "portable-zip"))],
        "promotion": {"eligible": False, "blockers": blockers},
        "installer": {"built": False, "qualified": False},
    }
    (output / "candidate.json").write_bytes(json_bytes(manifest))
    checksum_file(output, manifest)
    verify(output)
    return manifest


def safe_name(name: str) -> bool:
    return bool(name and Path(name).name == name and "/" not in name and "\\" not in name and name not in (".", ".."))


def verify(directory: Path) -> dict:
    manifest = json.loads((directory / "candidate.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != "wtop.candidate/1":
        raise PackageError("unknown candidate manifest schema")
    payload = manifest["payload"]
    if payload.get("schema") != "wtop.payload/1":
        raise PackageError("unknown payload manifest schema")
    target = target_for(payload["profile"])
    names = set()
    zip_count, exe_count = 0, 0
    for artifact in manifest["artifacts"]:
        name = artifact["name"]
        if not safe_name(name) or name in names:
            raise PackageError("unsafe or duplicate artifact name")
        names.add(name)
        if (directory / name).stat().st_size > 128 * 1024 * 1024:
            raise PackageError("artifact exceeds the 128 MiB verification bound")
        data = (directory / name).read_bytes()
        if artifact["sha256"] != sha256(data) or artifact["size"] != len(data):
            raise PackageError(f"artifact identity mismatch: {name}")
        if artifact["kind"] == "standalone":
            exe_count += 1
            if name != "wtop.exe" or sha256(data) != payload["executable"]["sha256"]:
                raise PackageError("standalone payload mismatch")
            if validate_identity(data, target) != payload["pe"]:
                raise PackageError("PE inventory mismatch")
        elif artifact["kind"] == "portable-zip":
            zip_count += 1
            with zipfile.ZipFile(directory / name) as archive:
                members = archive.namelist()
                expected = {"wtop.exe", "LICENSE", "payload.json", "PORTABLE.txt", "wtop.conf.example"}
                if third_party_notices_required(payload['version']):
                    expected.add('THIRD-PARTY-NOTICES.md')
                if len(members) != len(expected) or set(members) != expected:
                    raise PackageError("unexpected ZIP member set")
                if any(item.file_size > 64 * 1024 * 1024 for item in archive.infolist()):
                    raise PackageError("ZIP member exceeds the extraction bound")
                if archive.read("wtop.exe") != (directory / "wtop.exe").read_bytes():
                    raise PackageError("ZIP does not contain the identical executable")
                if json.loads(archive.read("payload.json")) != payload:
                    raise PackageError("ZIP payload provenance differs")
                if archive.read("LICENSE") != (directory / "LICENSE").read_bytes():
                    raise PackageError("ZIP license differs")
                if third_party_notices_required(payload['version']):
                    notices = (directory / 'THIRD-PARTY-NOTICES.md').read_bytes()
                    if (not notices or archive.read('THIRD-PARTY-NOTICES.md') != notices or
                            payload.get('legal_files') != [{'name': 'THIRD-PARTY-NOTICES.md', 'size': len(notices), 'sha256': sha256(notices)}]):
                        raise PackageError('third-party notice bytes/provenance differ')
    if exe_count != 1 or zip_count != 1:
        raise PackageError("candidate requires exactly one standalone EXE and portable ZIP")
    expected_checksums = "".join(
        f"{sha256((directory / name).read_bytes())}  {name}\n"
        for name in sorted(names | {"candidate.json"}))
    if (directory / "SHA256SUMS").read_text(encoding="ascii") != expected_checksums:
        raise PackageError("checksum listing differs from artifacts")
    return manifest


def promotion_blockers(manifest: dict) -> list[str]:
    blockers = list(manifest.get("promotion", {}).get("blockers", []))
    if manifest.get("promotion", {}).get("eligible") is not True:
        blockers.append("candidate_promotion_disabled")
    installer = manifest.get("installer", {})
    if installer.get("built") and installer.get("qualified") is not True:
        blockers.append("installer_preservation_not_qualified")
    # This tool has no authority to convert tests into release approval. A
    # future separately reviewed promoter must implement the release contract.
    blockers.append("release_publication_work_unit_not_implemented")
    return sorted(set(blockers))


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create")
    create.add_argument("--exe", required=True, type=Path)
    create.add_argument("--output", required=True, type=Path)
    create.add_argument("--profile", required=True)
    create.add_argument("--provenance", required=True, type=Path)
    for operation in ("verify", "promote"):
        commands.add_parser(operation).add_argument("directory", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "create":
            value = package(args.exe, args.output, args.profile,
                            json.loads(args.provenance.read_text(encoding="utf-8-sig")))
            print(json.dumps({"output": str(args.output), "payload_sha256": value["payload"]["executable"]["sha256"],
                              "promotion": value["promotion"]}, indent=2))
        else:
            value = verify(args.directory)
            if args.command == "promote":
                raise PackageError("promotion refused: " + ", ".join(promotion_blockers(value)))
            print("Candidate bytes, ZIP identity and PE inventory verified; this is not release qualification.")
    except (OSError, ValueError, KeyError, PEError, subprocess.CalledProcessError, zipfile.BadZipFile) as exc:
        print(f"Packaging failed: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
