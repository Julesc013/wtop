#!/usr/bin/env python3
"""Build/inspect bounded loader probes; never promote a target to supported."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from inspect_pe import inspect_bytes as inspect_pe

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "spec/catalog/targets.json"
MAX_ARTIFACT_BYTES = 64 * 1024 * 1024
PROBE_SCHEMA = "wtop.loader-probe/1"
STATES = {"observed", "present", "unavailable", "unsupported", "denied", "failed"}
# This maps source entrypoints to canonical target IDs; catalog data is not
# duplicated here. Missing entries are an explicit absence of a loader spike.
ENTRYPOINTS = {
    "WT-TGT-DOS1": "dos", "WT-TGT-DOS2": "dos",
    "WT-TGT-WIN16-3": "win16", "WT-TGT-OS2-16": "os2", "WT-TGT-OS2-32": "os2",
    "WT-TGT-WIN32-MODERN": "nt", "WT-TGT-WIN64-MODERN": "nt",
    "WT-TGT-WINARM64": "nt", "WT-TGT-WINPE": "nt", "WT-TGT-REACTOS": "nt",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def targets() -> dict:
    values = json.loads(CATALOG.read_text(encoding="utf-8"))["targets"]
    ids = {item["id"] for item in values}
    if not set(ENTRYPOINTS).issubset(ids):
        raise ValueError("probe source mapping refers to an unknown target catalog ID")
    return {item["profile"]: item for item in values}


def wrap_mz(payload: bytes) -> bytes:
    """One 8086 code/data segment, no relocations/CRT; separate 256-byte stack."""
    if not 1 <= len(payload) <= 0xFE00:
        raise ValueError("DOS probe payload must fit one bounded 16-bit segment")
    total = 32 + len(payload)
    header = struct.pack("<14H", 0x5A4D, total % 512, (total + 511) // 512,
                         0, 2, 16, 16, (len(payload) + 15) // 16, 256, 0, 0, 0, 28, 0)
    return header.ljust(32, b"\0") + payload


def inspect_artifact(data: bytes, target: dict) -> dict:
    if not data or len(data) > MAX_ARTIFACT_BYTES:
        raise ValueError("artifact size outside probe inspection bounds")
    allowed = target["format"].split("/")
    result = {"format": "COM", "native_qualification": False}
    if data[:2] == b"MZ":
        if len(data) < 28:
            raise ValueError("truncated MZ header")
        words = struct.unpack_from("<14H", data)
        _, last, pages, relocations, paragraphs, minimum, maximum, ss, sp, _, ip, cs, reloc_offset, overlay = words
        header_bytes = paragraphs * 16
        declared = (pages - 1) * 512 + (last or 512)
        if not pages or last >= 512 or header_bytes < 28 or header_bytes > len(data) or declared > len(data):
            raise ValueError("invalid MZ length/header")
        if relocations and (reloc_offset < 28 or reloc_offset + relocations * 4 > header_bytes):
            raise ValueError("relocation table escapes MZ header")
        result = {"format": "MZ", "header_bytes": header_bytes, "declared_file_bytes": declared,
                  "relocations": relocations, "entry_cs_ip": [cs, ip], "stack_ss_sp": [ss, sp],
                  "minimum_extra_paragraphs": minimum, "maximum_extra_paragraphs": maximum,
                  "overlay": overlay, "native_qualification": False}
        new_offset = int.from_bytes(data[60:64], "little") if len(data) >= 64 and header_bytes >= 64 else 0
        if new_offset >= 64 and new_offset + 2 <= len(data):
            signature = data[new_offset:new_offset + 4]
            if signature == b"PE\0\0":
                result.update(inspect_pe(data))
            elif signature[:2] in (b"NE", b"LX", b"LE"):
                result["format"] = signature[:2].decode("ascii")
                result["new_header_offset"] = new_offset
        if result["format"] == "MZ":
            module_bytes = declared - header_bytes
            if module_bytes <= 0 or cs * 16 + ip >= module_bytes or maximum < minimum:
                raise ValueError("invalid MZ entrypoint/allocation")
            if ss * 16 + sp > ((module_bytes + 15) // 16 + minimum) * 16:
                raise ValueError("MZ stack exceeds minimum allocated image")
    else:
        if len(data) > 0xFF00:
            raise ValueError("COM image exceeds one segment after PSP")
    actual = result["format"]
    if actual not in allowed and not ("PE" in allowed and actual in ("PE32", "PE32+")):
        raise ValueError(f"artifact {actual} does not match catalog format {target['format']}")
    architecture = result.get("architecture")
    if target["architecture"] in {"x86", "x64", "arm64"} and architecture and architecture != target["architecture"]:
        raise ValueError("artifact architecture does not match target catalog")
    return result


def command(arguments: list[str], directory: Path) -> dict:
    try:
        run = subprocess.run(arguments, cwd=directory, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=60, check=False)
        return {"arguments": arguments, "exit_code": run.returncode,
                "stdout": run.stdout[-16384:], "stderr": run.stderr[-16384:]}
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"arguments": arguments, "exit_code": None, "error": str(exc), "stage": "tool-execution-failed"}


def resolve_tool(value: str | None) -> str | None:
    if not value:
        return None
    result = shutil.which(value)
    return str(Path(result).resolve()) if result else None


def build_probe(profile: str, output: Path, *, cc: str | None = None,
                assembler: str = "as", linker: str = "ld") -> dict:
    catalog = targets()
    if profile not in catalog:
        raise ValueError("unknown profile; use a profile from spec/catalog/targets.json")
    target = catalog[profile]
    family = ENTRYPOINTS.get(target["id"])
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipt = {"schema": "wtop.loader-build/1", "profile": profile, "target_id": target["id"],
               "catalog_sha256": sha256(CATALOG.read_bytes()), "at": datetime.now(timezone.utc).isoformat(),
               "host": platform.platform(), "native_qualification": False,
               "stage": "source-unavailable", "commands": [], "sources": []}
    if not family:
        receipt["reason"] = "No source spike yet; the target remains independently unqualified."
        return receipt
    source = ROOT / "source/legacy" / {"dos": "dos_probe.s", "win16": "win16_probe.c", "os2": "os2_probe.c", "nt": "nt_probe.c"}[family]
    sources = [source] if family == "dos" else [source, ROOT / "source/legacy/probe.c", ROOT / "source/text.c"]
    identity_sources = sources if family == "dos" else sources + [ROOT / "source/legacy/probe.h", ROOT / "source/text.h"]
    receipt["sources"] = [{"path": str(path.relative_to(ROOT)), "sha256": sha256(path.read_bytes())} for path in identity_sources]
    receipt["harness_sha256"] = sha256(Path(__file__).read_bytes())
    receipt["pe_inspector_sha256"] = sha256((ROOT / "tools/inspect_pe.py").read_bytes())
    artifact = output / ("WTOPPROB.COM" if target["id"] == "WT-TGT-DOS1" else "WTOPPROB.EXE")
    if family == "dos":
        tools = [resolve_tool(assembler), resolve_tool(linker)]
        if not all(tools):
            receipt.update(stage="toolchain-unavailable", reason="GNU 8086-capable assembler/linker were not found")
            return receipt
        dos2 = target["id"] == "WT-TGT-DOS2"
        object_file, payload = output / "probe.o", output / "probe.bin"
        commands = [[tools[0], "--32", "--defsym", f"WTOP_DOS2={int(dos2)}", str(source), "-o", str(object_file)],
                    [tools[1], "-m", "elf_i386", "-Ttext", "0" if dos2 else "0x100", "--oformat", "binary", str(object_file), "-o", str(payload)]]
    else:
        default = "wcl" if family == "win16" or target["id"] == "WT-TGT-OS2-16" else "wcl386" if family == "os2" else "cl" if os.name == "nt" else None
        compiler = resolve_tool(cc or default)
        tools = [compiler]
        if not compiler:
            receipt.update(stage="toolchain-unavailable", reason="A matching native compiler/SDK is required; supply --cc without implying runtime support")
            return receipt
        if family in ("win16", "os2"):
            options = ["-bt=windows", "-ml", "-0"] if family == "win16" else ["-bt=os2"]
            if target["id"] == "WT-TGT-OS2-16":
                options.extend(["-ml", "-2"])
            commands = [[compiler, *options, "-q", f"-fe={artifact}", *map(str, sources)]]
        elif Path(compiler).name.lower() in {"cl", "cl.exe"}:
            commands = [[compiler, "/nologo", "/TC", "/W4", "/MT", f'/DWTOP_PROBE_PROFILE="{profile}"',
                         f"/Fe:{artifact}", *map(str, sources), "/link", "advapi32.lib"]]
        else:
            commands = [[compiler, "-std=c90", "-Wall", "-Wextra", "-Werror", f'-DWTOP_PROBE_PROFILE="{profile}"',
                         *map(str, sources), "-ladvapi32", "-o", str(artifact)]]
    receipt["tools"] = [{"path": path, "launcher_sha256": sha256(Path(path).read_bytes()),
                         "version_probe": command([path, "--version"], output)} for path in tools]
    for arguments in commands:
        result = command(arguments, output)
        receipt["commands"].append(result)
        if result["exit_code"] != 0:
            receipt.update(stage="build-failed", reason="See the exact compiler/linker result; no artifact qualification follows")
            return receipt
    if family == "dos":
        artifact.write_bytes(wrap_mz(payload.read_bytes()) if dos2 else payload.read_bytes())
    data = artifact.read_bytes()
    try:
        inspection = inspect_artifact(data, target)
    except ValueError as exc:
        receipt.update(stage="artifact-inspection-failed", reason=str(exc))
        return receipt
    receipt.update(stage="probe-built-not-run", artifact={"path": str(artifact), "bytes": len(data),
                   "sha256": sha256(data), "inspection": inspection})
    return receipt


def validate_observation(value: object, profile: str) -> dict:
    if not isinstance(value, dict) or value.get("schema") != PROBE_SCHEMA or value.get("profile") != profile:
        raise ValueError("observation schema/profile mismatch")
    if value.get("qualification") != "not-established":
        raise ValueError("loader output cannot assert platform qualification")
    records = value.get("observations")
    if not isinstance(records, list) or len(records) > 24:
        raise ValueError("invalid observation count")
    seen = set()
    for record in records:
        if not isinstance(record, dict) or record.get("state") not in STATES:
            raise ValueError("invalid observation state")
        key, text, code = record.get("id"), record.get("value"), record.get("native_code")
        if not isinstance(key, str) or not 1 <= len(key.encode("utf-8")) < 64 or key in seen:
            raise ValueError("invalid or duplicate observation ID")
        if not all(char.isascii() and (char.isalnum() or char in "._-+") for char in key):
            raise ValueError("invalid observation ID syntax")
        if not isinstance(text, str) or len(text.encode("utf-8")) >= 256 or "\0" in text:
            raise ValueError("invalid observation value")
        if type(code) is not int or not 0 <= code <= 0xFFFFFFFF:
            raise ValueError("invalid native status code")
        seen.add(key)
    return value


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    commands.add_parser("list", help="catalog targets and availability of probe source")
    build = commands.add_parser("build")
    build.add_argument("profile")
    build.add_argument("--output", type=Path, required=True)
    build.add_argument("--cc")
    build.add_argument("--assembler", default="as")
    build.add_argument("--linker", default="ld")
    inspect = commands.add_parser("inspect")
    inspect.add_argument("profile")
    inspect.add_argument("artifact", type=Path)
    record = commands.add_parser("record", help="bind supplied guest output to an artifact without certifying the image")
    record.add_argument("profile")
    record.add_argument("artifact", type=Path)
    record.add_argument("observation", type=Path)
    record.add_argument("--image-id", required=True, help="exact guest/image identifier, preferably image hash and build")
    record.add_argument("--host-description", required=True, help="hardware/emulator/version/CPU and relevant components")
    record.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        catalog = targets()
        if args.action == "list":
            result = [{"profile": value["profile"], "target_id": value["id"],
                       "probe_source": ENTRYPOINTS.get(value["id"]), "qualification": value["qualification"]} for value in catalog.values()]
        elif args.action == "build":
            result = build_probe(args.profile, args.output, cc=args.cc, assembler=args.assembler, linker=args.linker)
            (args.output / "loader-build.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        else:
            if args.profile not in catalog:
                raise ValueError("profile is not present in canonical target catalog")
            if args.artifact.stat().st_size > MAX_ARTIFACT_BYTES:
                raise ValueError("artifact exceeds bounded inspection size")
            data = args.artifact.read_bytes()
            result = {"schema": "wtop.loader-inspection/1", "profile": args.profile,
                      "artifact_sha256": sha256(data), "inspection": inspect_artifact(data, catalog[args.profile]),
                      "native_qualification": False}
            if args.action == "record":
                if args.observation.stat().st_size > 16384:
                    raise ValueError("observation exceeds 16 KiB")
                observed = validate_observation(json.loads(args.observation.read_text(encoding="utf-8")), args.profile)
                if observed.get("format") != result["inspection"]["format"]:
                    raise ValueError("observation does not match inspected artifact format")
                result.update(schema="wtop.loader-evidence/1", observation=observed,
                              image_id=args.image_id, host_description=args.host_description,
                              provenance="supplied-guest-output-not-independently-attested", at=datetime.now(timezone.utc).isoformat())
                args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if args.action != "build" or result["stage"] == "probe-built-not-run" else 4
    except (OSError, ValueError, KeyError) as exc:
        print(f"legacy probe: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
