#!/usr/bin/env python3
"""Bounded, dependency-free PE inventory. Inspection is not OS qualification."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path


class PEError(ValueError):
    pass


MACHINES = {0x014C: "x86", 0x8664: "x64", 0xAA64: "arm64"}
MAX_IMAGE_BYTES = 64 * 1024 * 1024


class PE:
    def __init__(self, data: bytes):
        self.data = data
        self.symbol_budget = 32768
        if len(data) > MAX_IMAGE_BYTES:
            raise PEError("image exceeds the 64 MiB candidate inspection limit")
        if self.bytes(0, 2) != b"MZ":
            raise PEError("missing DOS MZ signature")
        pe = self.uint(0x3C, 4)
        if pe < 64 or self.bytes(pe, 4) != b"PE\0\0":
            raise PEError("missing PE signature")
        self.machine = self.uint(pe + 4, 2)
        self.arch = MACHINES.get(self.machine, "unknown")
        count = self.uint(pe + 6, 2)
        optional_size = self.uint(pe + 20, 2)
        self.characteristics = self.uint(pe + 22, 2)
        if not 1 <= count <= 96:
            raise PEError("invalid or excessive section count")
        optional = pe + 24
        self.bytes(optional, optional_size)
        magic = self.uint(optional, 2)
        if magic == 0x10B:
            self.format, self.pointer_size, directory_offset = "PE32", 4, 96
            self.image_base = self.uint(optional + 28, 4)
        elif magic == 0x20B:
            self.format, self.pointer_size, directory_offset = "PE32+", 8, 112
            self.image_base = self.uint(optional + 24, 8)
        else:
            raise PEError("unsupported optional-header format")
        if optional_size < directory_offset:
            raise PEError("truncated optional header")
        if self.arch == "x86" and self.pointer_size != 4:
            raise PEError("x86 machine does not match PE32 header")
        if self.arch in ("x64", "arm64") and self.pointer_size != 8:
            raise PEError("64-bit machine does not match PE32+ header")
        self.subsystem = self.uint(optional + 68, 2)
        self.os_version = [self.uint(optional + 40, 2), self.uint(optional + 42, 2)]
        self.subsystem_version = [self.uint(optional + 48, 2), self.uint(optional + 50, 2)]
        self.entrypoint = self.uint(optional + 16, 4)
        self.header_size = self.uint(optional + 60, 4)
        if self.header_size > len(data):
            raise PEError("headers exceed the file")
        directories = self.uint(optional + directory_offset - 4, 4)
        if directories > 16 or directories * 8 > optional_size - directory_offset:
            raise PEError("invalid data-directory count")
        self.directories = [
            (self.uint(optional + directory_offset + i * 8, 4),
             self.uint(optional + directory_offset + i * 8 + 4, 4))
            for i in range(directories)
        ]
        self.directories.extend([(0, 0)] * (16 - directories))
        self.sections = []
        for i in range(count):
            offset = optional + optional_size + i * 40
            self.bytes(offset, 40)
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
            if raw_size:
                self.bytes(raw_offset, raw_size)
            self.sections.append((rva, virtual_size, raw_offset, raw_size))

    def bytes(self, offset: int, count: int) -> bytes:
        if offset < 0 or count < 0 or offset > len(self.data) or count > len(self.data) - offset:
            raise PEError("structure points outside file")
        return self.data[offset:offset + count]

    def uint(self, offset: int, size: int) -> int:
        return int.from_bytes(self.bytes(offset, size), "little")

    def offset(self, rva: int, size: int = 1) -> int:
        candidates = []
        if 0 <= rva < self.header_size and size <= self.header_size - rva:
            candidates.append(rva)
        for start, _virtual, raw, count in self.sections:
            if start <= rva and rva - start < count and size <= count - (rva - start):
                candidates.append(raw + rva - start)
        if len(candidates) != 1:
            raise PEError("unmapped or ambiguous RVA")
        self.bytes(candidates[0], size)
        return candidates[0]

    def string(self, rva: int, limit: int = 1024) -> str:
        value = bytearray()
        for i in range(limit):
            char = self.uint(self.offset(rva + i), 1)
            if not char:
                try:
                    return bytes(value).decode("ascii")
                except UnicodeDecodeError as exc:
                    raise PEError("non-ASCII import name") from exc
            if char < 0x20 or char > 0x7E:
                raise PEError("control or non-ASCII byte in import name")
            value.append(char)
        raise PEError("unterminated or excessive import name")

    def symbols(self, rva: int, values_are_rva: bool = True) -> list[str]:
        symbols = []
        ordinal_bit = 1 << (self.pointer_size * 8 - 1)
        for i in range(8192):
            item = self.uint(self.offset(rva + i * self.pointer_size, self.pointer_size), self.pointer_size)
            if not item:
                return symbols
            if self.symbol_budget <= 0:
                raise PEError("total import symbol budget exceeded")
            self.symbol_budget -= 1
            if item & ordinal_bit:
                symbols.append("ordinal:" + str(item & 0xFFFF))
            else:
                address = item if values_are_rva else item - self.image_base
                self.offset(address, 2)  # IMAGE_IMPORT_BY_NAME hint
                symbols.append(self.string(address + 2))
        raise PEError("unterminated or excessive import thunk table")

    def imports(self, delayed: bool = False) -> list[dict]:
        rva, size = self.directories[13 if delayed else 1]
        if not rva and not size:
            return []
        stride = 32 if delayed else 20
        if not rva or size < stride or size > stride * 1024:
            raise PEError("invalid import directory size")
        imports = []
        for position in range(0, size - stride + 1, stride):
            offset = self.offset(rva + position, stride)
            record = struct.unpack("<" + "I" * (stride // 4), self.bytes(offset, stride))
            if not any(record):
                return imports
            if delayed:
                attributes, name, _handle, iat, names, _bound, _unload, _time = record
                if attributes & ~1:
                    raise PEError("unknown delayed-import addressing flags")
                values_are_rva = bool(attributes & 1)
                if not values_are_rva:
                    name -= self.image_base
                    names = names - self.image_base if names else 0
                    iat -= self.image_base
            else:
                names, _time, _forward, name, iat = record
                values_are_rva = True
            imports.append({"dll": self.string(name, 260).lower(),
                            "symbols": self.symbols(names or iat, values_are_rva)})
        raise PEError("import directory has no terminating descriptor")

    def resources(self) -> dict:
        rva, size = self.directories[2]
        if not rva and not size:
            return {"present": False, "manifests": []}
        if not rva or not size:
            raise PEError("incomplete resource directory")
        root = self.offset(rva, size)
        manifests = []
        visited = set()
        entry_budget = 4096

        def within(relative, length):
            if relative < 0 or length < 0 or relative > size or length > size - relative:
                raise PEError("resource points outside resource directory")
            return root + relative

        def visit(relative, path):
            nonlocal entry_budget
            if len(path) > 3 or relative in visited or len(visited) >= 4096:
                raise PEError("cyclic or excessive resource tree")
            visited.add(relative)
            table = within(relative, 16)
            count = self.uint(table + 12, 2) + self.uint(table + 14, 2)
            if count > 4096:
                raise PEError("excessive resource entries")
            if count > entry_budget:
                raise PEError("total resource entry budget exceeded")
            entry_budget -= count
            within(relative + 16, count * 8)
            for i in range(count):
                entry = table + 16 + 8 * i
                name, target = self.uint(entry, 4), self.uint(entry + 4, 4)
                if name & 0x80000000:
                    name_offset = name & 0x7FFFFFFF
                    name_size = self.uint(within(name_offset, 2), 2) * 2
                    raw = self.bytes(within(name_offset + 2, name_size), name_size)
                    name = raw.decode("utf-16-le", errors="strict")
                current = path + [name]
                if target & 0x80000000:
                    visit(target & 0x7FFFFFFF, current)
                else:
                    leaf = within(target, 16)
                    data_rva, data_size = self.uint(leaf, 4), self.uint(leaf + 4, 4)
                    content = self.bytes(self.offset(data_rva, data_size), data_size)
                    if current[0] == 24:
                        if data_size > 65536 or len(manifests) >= 32:
                            raise PEError("manifest content/count exceeds bounded profile")
                        encoding = "utf-16" if content.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
                        manifests.append({"resource": current,
                            "sha256": hashlib.sha256(content).hexdigest(),
                            "text": content.decode(encoding, errors="strict").rstrip("\0")})

        try:
            visit(0, [])
        except UnicodeError as exc:
            raise PEError("invalid text resource encoding") from exc
        return {"present": True, "size": size,
                "sha256": hashlib.sha256(self.bytes(root, size)).hexdigest(), "manifests": manifests}

    def report(self) -> dict:
        imports, delayed = self.imports(), self.imports(True)
        dlls = sorted({entry["dll"] for entry in imports + delayed})
        runtime = [name for name in dlls if name.startswith(("vcruntime", "msvcp", "msvcr", "api-ms-win-crt-")) or name == "ucrtbase.dll"]
        certificate_offset, certificate_size = self.directories[4]
        if certificate_offset or certificate_size:
            certificate = self.bytes(certificate_offset, certificate_size)
            if len(certificate) < 8 or not 8 <= int.from_bytes(certificate[:4], "little") <= certificate_size:
                raise PEError("malformed certificate table")
        return {
            "format": self.format, "machine": self.machine, "architecture": self.arch,
            "subsystem": self.subsystem, "subsystem_name": {2: "windows-gui", 3: "windows-console"}.get(self.subsystem, "other"),
            "os_header_version": self.os_version, "subsystem_header_version": self.subsystem_version,
            "entrypoint_rva": self.entrypoint, "executable": bool(self.characteristics & 2),
            "dll": bool(self.characteristics & 0x2000), "imports": imports, "delay_imports": delayed,
            "runtime_dlls": runtime, "resources": self.resources(),
            "certificate_table_present": bool(certificate_size), "signature_verified": False,
            "minimum_os_qualified": False,
        }


def inspect_bytes(data: bytes) -> dict:
    return PE(data).report()


def inspect_file(path: Path) -> dict:
    if path.stat().st_size > MAX_IMAGE_BYTES:
        raise PEError("image exceeds 64 MiB")
    return inspect_bytes(path.read_bytes())


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        report = inspect_file(args.image)
        encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8")
        else:
            print(encoded, end="")
    except (OSError, PEError) as exc:
        print(f"PE inspection failed: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
