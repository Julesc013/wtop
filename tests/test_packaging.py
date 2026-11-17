"""Independent PE fixtures and exact-byte packaging failure cases."""
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from inspect_pe import PEError, inspect_bytes
from package_candidate import PackageError, package, promotion_blockers, verify

loader = importlib.util.spec_from_file_location("wtop_build_msi", ROOT / "packaging/build_msi.py")
msi = importlib.util.module_from_spec(loader)
loader.loader.exec_module(msi)


def fixture(arch="x64"):
    """One hand-authored section, one KERNEL32 import and no resources."""
    data = bytearray(0x600)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    pe32 = arch == "x86"
    optional_size = 224 if pe32 else 240
    struct.pack_into("<HHIIIHH", data, 0x84, {"x86": 0x14C, "x64": 0x8664, "arm64": 0xAA64}[arch], 1, 0, 0, 0, optional_size, 2)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x10B if pe32 else 0x20B)
    struct.pack_into("<I", data, optional + 16, 0x1000)
    struct.pack_into("<I" if pe32 else "<Q", data, optional + (28 if pe32 else 24), 0x400000)
    struct.pack_into("<HH", data, optional + 40, 6, 1)
    struct.pack_into("<HH", data, optional + 48, 6, 1)
    struct.pack_into("<I", data, optional + 56, 0x2000)
    struct.pack_into("<I", data, optional + 60, 0x200)
    struct.pack_into("<H", data, optional + 68, 3)
    directories = optional + (96 if pe32 else 112)
    struct.pack_into("<I", data, directories - 4, 16)
    struct.pack_into("<II", data, directories + 8, 0x1000, 40)
    section = optional + optional_size
    data[section:section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x400, 0x1000, 0x400, 0x200)
    struct.pack_into("<IIIII", data, 0x200, 0x1100, 0, 0, 0x1080, 0x1120)
    data[0x280:0x28D] = b"KERNEL32.dll\0"
    struct.pack_into("<I" if pe32 else "<Q", data, 0x300, 0x1180)
    data[0x382:0x38E] = b"ExitProcess\0"
    return bytes(data)


PROVENANCE = {"compiler": "fixture", "linker": "fixture", "sdk": "fixture",
              "runtime": "none", "flags": ["fixture"], "build_command": ["fixture"]}
SOURCE = {"revision": "a" * 40, "dirty": True, "binding": "test-fixture"}


class PEInspection(unittest.TestCase):
    def test_machine_and_imports(self):
        for arch in ("x86", "x64", "arm64"):
            with self.subTest(arch=arch):
                report = inspect_bytes(fixture(arch))
                self.assertEqual(report["architecture"], arch)
                self.assertEqual(report["subsystem_name"], "windows-console")
                self.assertEqual(report["imports"], [{"dll": "kernel32.dll", "symbols": ["ExitProcess"]}])
                self.assertFalse(report["minimum_os_qualified"])
                self.assertFalse(report["resources"]["present"])

    def test_bounds_and_malformed_imports(self):
        data = fixture()
        for length in (0, 2, 63, 128, 200, 511, 1000):
            with self.subTest(length=length), self.assertRaises(PEError):
                inspect_bytes(data[:length])
        bad = bytearray(data)
        struct.pack_into("<I", bad, 0x3C, 0xFFFFFFF0)
        with self.assertRaises(PEError):
            inspect_bytes(bytes(bad))
        bad = bytearray(data)
        struct.pack_into("<I", bad, 0x200 + 12, 0xFFFFFF00)
        with self.assertRaises(PEError):
            inspect_bytes(bytes(bad))
        bad = bytearray(data)
        bad[0x280] = 27
        with self.assertRaises(PEError):
            inspect_bytes(bytes(bad))


class CandidatePackaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.exe = self.root / "input.exe"
        self.exe.write_bytes(fixture())

    def create(self, name="candidate"):
        destination = self.root / name
        manifest = package(self.exe, destination, "win-nt-x64", PROVENANCE, source=SOURCE)
        return destination, manifest

    def test_identical_payload_and_deterministic_zip(self):
        first, manifest = self.create()
        second, _ = self.create("second")
        archive = next(item["name"] for item in manifest["artifacts"] if item["kind"] == "portable-zip")
        self.assertEqual((first / archive).read_bytes(), (second / archive).read_bytes())
        with zipfile.ZipFile(first / archive) as zipped:
            self.assertEqual(zipped.read("wtop.exe"), self.exe.read_bytes())
            self.assertNotIn("wtop.conf", zipped.namelist())
        self.assertEqual(verify(first), manifest)
        self.assertIn("candidate_promotion_disabled", promotion_blockers(manifest))
        self.assertIn("source_tree_dirty", promotion_blockers(manifest))

    def test_mismatch_and_overwrite_refusal(self):
        with self.assertRaises(PackageError):
            package(self.exe, self.root / "wrong", "win-nt-x86", PROVENANCE, source=SOURCE)
        destination, _ = self.create()
        with self.assertRaises(PackageError):
            package(self.exe, destination, "win-nt-x64", PROVENANCE, source=SOURCE)
        (destination / "wtop.exe").write_bytes(b"changed")
        with self.assertRaises(PackageError):
            verify(destination)

    def test_inventory_and_checksums_are_verified(self):
        directory, manifest = self.create()
        manifest["payload"]["pe"]["architecture"] = "arm64"
        (directory / "candidate.json").write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaises(PackageError):
            verify(directory)

    def test_unsafe_artifact_path_refused(self):
        directory, manifest = self.create()
        manifest["artifacts"][0]["name"] = "../input.exe"
        (directory / "candidate.json").write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaises(PackageError):
            verify(directory)

    def test_msi_versions_and_hash_bound_guard(self):
        directory, manifest = self.create()
        for invalid in ("0.1.0-dev.2", "1.2.3.4", "256.0.0", "1.256.0", "1.2.65536", "01.2.3"):
            with self.subTest(version=invalid), self.assertRaises(PackageError):
                msi.msi_version(invalid)
        self.assertEqual(msi.msi_version("255.255.65535"), "255.255.65535")
        self.assertEqual(msi.guid("same"), msi.guid("same"))
        self.assertNotEqual(msi.guid("same"), msi.guid("other"))
        header = msi.write_guard_config(directory, self.root / "stage", manifest)
        text = header.read_text(encoding="ascii")
        self.assertIn(manifest["payload"]["executable"]["sha256"], text)
        self.assertIn('L"wtop.exe"', text)
        manifest["installer"] = {"built": True, "qualified": False}
        self.assertIn("installer_preservation_not_qualified", promotion_blockers(manifest))


if __name__ == "__main__":
    unittest.main()
