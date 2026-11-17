"""Loader artifact bounds and optional instruction execution with scripted BIOS/DOS.

Unicorn is an optional development dependency. These tests do NOT boot DOS,
qualify OEM images, establish Win16/OS2 support, or attest native output.
"""
from __future__ import annotations

import copy
import importlib.util
import json
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import legacy_probe

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR
    from unicorn.x86_const import UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_DX, UC_X86_REG_IP, UC_X86_REG_SP, UC_X86_REG_SS
except ImportError:
    Uc = None


class ArtifactBounds(unittest.TestCase):
    def test_mz_header_stack_and_record(self):
        target = legacy_probe.targets()["dos2plus-x86"]
        image = legacy_probe.wrap_mz(b"\x90" * 33)
        result = legacy_probe.inspect_artifact(image, target)
        self.assertEqual(result["header_bytes"], 32)
        self.assertEqual(result["stack_ss_sp"], [3, 256])
        self.assertEqual(result["declared_file_bytes"], 65)
        self.assertEqual(result["minimum_extra_paragraphs"], 16)
        self.assertFalse(result["native_qualification"])
        malformed = bytearray(image)
        struct.pack_into("<H", malformed, 14, 0xFFFF)
        with self.assertRaisesRegex(ValueError, "stack"):
            legacy_probe.inspect_artifact(bytes(malformed), target)
        for size in (0, 0xFE01):
            with self.assertRaises(ValueError):
                legacy_probe.wrap_mz(bytes(size))

    def test_target_format_is_not_inferred_from_name(self):
        data = b"\xcd\x20"
        with self.assertRaisesRegex(ValueError, "catalog format"):
            legacy_probe.inspect_artifact(data, legacy_probe.targets()["os2-32-x86"])
        with self.assertRaises(ValueError):
            legacy_probe.inspect_artifact(b"MZ", legacy_probe.targets()["dos1-x86"])

    def test_observations_do_not_promote_support(self):
        value = {"schema": "wtop.loader-probe/1", "profile": "dos1-x86", "format": "COM",
                 "observations": [{"id": "process.cpu", "state": "unsupported", "native_code": 0, "value": "no-provider"}],
                 "qualification": "not-established"}
        self.assertEqual(legacy_probe.validate_observation(value, "dos1-x86"), value)
        for change in (lambda obj: obj.update(qualification="supported"),
                       lambda obj: obj["observations"].append(obj["observations"][0]),
                       lambda obj: obj["observations"][0].update(native_code=True),
                       lambda obj: obj["observations"][0].update(state="successful")):
            invalid = copy.deepcopy(value)
            change(invalid)
            with self.assertRaises(ValueError):
                legacy_probe.validate_observation(invalid, "dos1-x86")

    def test_unavailable_toolchain_is_a_recorded_stage(self):
        with tempfile.TemporaryDirectory() as folder:
            receipt = legacy_probe.build_probe("win16-3-x86", Path(folder), cc="definitely-absent-wtop-compiler")
        self.assertEqual(receipt["stage"], "toolchain-unavailable")
        self.assertFalse(receipt["native_qualification"])
        self.assertNotIn("artifact", receipt)


@unittest.skipUnless(Uc and shutil.which("as") and shutil.which("ld"), "optional Unicorn and GNU binutils required for scripted interrupts")
class DOSInstructionExecution(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.images = {}
        for profile in ("dos1-x86", "dos2plus-x86"):
            receipt = legacy_probe.build_probe(profile, Path(cls.temporary.name) / profile)
            if receipt["stage"] != "probe-built-not-run":
                raise AssertionError(receipt)
            cls.images[profile] = Path(receipt["artifact"]["path"]).read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def execute(self, profile, conventional_kib, major=0, minor=0, oem=0):
        image = self.images[profile]
        machine = Uc(UC_ARCH_X86, UC_MODE_16)
        machine.mem_map(0, 1024 * 1024)
        psp = 0x1000
        if profile == "dos1-x86":
            segment, ip, ss, sp = psp, 0x100, psp, 0xFFFE
            payload = image
        else:
            words = struct.unpack_from("<14H", image)
            segment, ip, ss, sp = 0x2000, words[10], 0x2000 + words[7], words[8]
            payload = image[words[4] * 16:]
        machine.mem_write(segment * 16 + ip, payload)
        for register, value in ((UC_X86_REG_CS, segment), (UC_X86_REG_IP, ip), (UC_X86_REG_DS, psp - 0x10),
                                (UC_X86_REG_SS, ss), (UC_X86_REG_SP, sp)):
            machine.reg_write(register, value)
        output, calls = bytearray(), []
        ended = False

        def interrupt(uc, number, _context):
            nonlocal ended
            ax = uc.reg_read(UC_X86_REG_AX)
            ah = ax >> 8
            calls.append((number, ah))
            if number == 0x12:
                uc.reg_write(UC_X86_REG_AX, conventional_kib)
            elif number == 0x21 and ah == 0x09:
                address = uc.reg_read(UC_X86_REG_DS) * 16 + uc.reg_read(UC_X86_REG_DX)
                for offset in range(2048):
                    byte = uc.mem_read(address + offset, 1)[0]
                    if byte == ord("$"):
                        break
                    output.append(byte)
                else:
                    raise AssertionError("unbounded DOS output string")
                # A documented caller must not depend on AX surviving output.
                uc.reg_write(UC_X86_REG_AX, 0x0924)
            elif number == 0x21 and ah == 0x30 and profile == "dos2plus-x86":
                uc.reg_write(UC_X86_REG_AX, (minor << 8) | major)
                uc.reg_write(UC_X86_REG_BX, (oem << 8) | 0x12)
            elif number == 0x20 and profile == "dos1-x86":
                self.assertEqual(uc.reg_read(UC_X86_REG_CS), psp)
                ended = True
                uc.emu_stop()
            elif number == 0x21 and ah == 0x4C and profile == "dos2plus-x86":
                self.assertEqual(ax & 255, 0)
                ended = True
                uc.emu_stop()
            else:
                raise AssertionError(f"unqualified interrupt/API {number:x}/{ah:x}")

        machine.hook_add(UC_HOOK_INTR, interrupt)
        machine.emu_start(segment * 16 + ip, 1024 * 1024, count=10000)
        self.assertTrue(ended, "instruction budget ended without declared termination")
        record = legacy_probe.validate_observation(json.loads(output.decode("ascii")), profile)
        values = {item["id"]: item for item in record["observations"]}
        self.assertEqual(values["bios.int12.conventional_kib"]["value"], str(conventional_kib))
        self.assertEqual(values["process.cpu"]["state"], "unsupported")
        return values, calls

    def test_dos1_never_calls_dos2_functions(self):
        for conventional in (0, 640, 65535):
            values, calls = self.execute("dos1-x86", conventional)
            self.assertEqual(values["dos.version"]["state"], "unsupported")
            self.assertNotIn((0x21, 0x30), calls)
            self.assertNotIn((0x21, 0x4C), calls)

    def test_dos2_retains_raw_reported_version_oem(self):
        for major, minor, oem in ((2, 0, 0), (7, 10, 0xFD), (255, 255, 255)):
            values, calls = self.execute("dos2plus-x86", 640, major, minor, oem)
            self.assertEqual(values["dos.int21_30.major"]["value"], str(major))
            self.assertEqual(values["dos.int21_30.minor"]["value"], str(minor))
            self.assertEqual(values["dos.int21_30.oem_raw"]["value"], str(oem))
            self.assertIn((0x21, 0x30), calls)


if __name__ == "__main__":
    unittest.main()
