"""Exercise the actual native binary through redirected, bounded CLI invocations."""

from __future__ import annotations

import json
import math
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest


EXE = None
if __name__ == "__main__":
    if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
        raise SystemExit("usage: test_native_cli.py PATH_TO_WTOP_EXE")
    EXE = str(Path(sys.argv.pop(1)).resolve())


@unittest.skipUnless(os.name == "nt" and EXE, "requires explicit native Windows executable")
class NativeCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Inspect the artifact, not the runner architecture or generated header.
        with open(EXE, "rb") as binary:
            dos_header = binary.read(64)
            if len(dos_header) != 64 or dos_header[:2] != b"MZ":
                raise ValueError("native test executable has no valid DOS header")
            binary.seek(struct.unpack_from("<I", dos_header, 0x3C)[0])
            pe_header = binary.read(6)
            if len(pe_header) != 6 or pe_header[:4] != b"PE\0\0":
                raise ValueError("native test executable has no valid PE header")
            machine = struct.unpack_from("<H", pe_header, 4)[0]
        profiles = {0x014C: "win-nt-x86", 0x8664: "win-nt-x64", 0xAA64: "win-nt-arm64"}
        if machine not in profiles:
            raise ValueError(f"unsupported PE machine for current profiles: {machine:#06x}")
        cls.expected_profile = profiles[machine]

    def run_cli(self, *args: str, code: int = 0, timeout: float = 8):
        result = subprocess.run(
            [EXE, *args], stdin=subprocess.DEVNULL, capture_output=True,
            encoding="utf-8", errors="strict", timeout=timeout,
        )
        self.assertEqual(result.returncode, code, result.stderr)
        return result

    def snapshot(self, *args: str):
        result = self.run_cli("--snapshot", "--format", "json", "--interval", "50", "--no-config", *args)
        self.assertEqual(result.stderr, "")
        return json.loads(result.stdout)

    def test_informational_commands_do_not_wait_for_sampling(self):
        version = json.loads(self.run_cli("--interval", "60000", "--version", "--format", "json").stdout)
        self.assertEqual(version["name"], "wtop")
        self.assertEqual(version["version"], (Path(__file__).parents[1] / "VERSION").read_text().strip())
        self.assertEqual(version["profile"], self.expected_profile)
        capabilities = json.loads(self.run_cli("--gui", "--capabilities", "--format", "json").stdout)
        self.assertIs(capabilities["runtime_probe"], False)
        self.assertEqual(capabilities["profile"], self.expected_profile)
        self.assertIs(capabilities["tui"], True)
        self.assertIs(capabilities["gui"], False)
        help_data = json.loads(self.run_cli("--format", "json", "--help").stdout)
        self.assertIn("--snapshot", help_data["help"])

    def test_reserved_gui_and_redirected_default_are_bounded_errors(self):
        gui = self.run_cli("--gui", "--interval", "60000", code=2)
        self.assertIn("not implemented", gui.stderr)
        self.assertEqual(gui.stdout, "")
        default = self.run_cli(code=2)
        self.assertIn("requires console", default.stderr)
        self.assertEqual(default.stdout, "")

    def test_snapshot_observes_real_parent_and_metric_states(self):
        data = self.snapshot("-p", str(os.getpid()))
        self.assertEqual(data["schema"], 1)
        self.assertEqual(data["profile"], self.expected_profile)
        self.assertGreaterEqual(data["sample_elapsed_ms"], 1)
        self.assertEqual([p["pid"] for p in data["processes"]], [os.getpid()])
        row = data["processes"][0]
        self.assertTrue(row["name"])
        self.assertGreater(row["threads"], 0)
        self.assertTrue(row["creation_filetime"].isdigit())
        for name in ("cpu_share_percent", "working_set_bytes", "io_read_write_bytes_per_second", "age_ms", "owner"):
            field = row[name]
            self.assertIn(field["status"], ("ok", "denied", "unavailable", "warming", "discontinuity"))
            if field["status"] == "ok":
                self.assertIsNotNone(field["value"])
                if name != "owner":
                    value = field["value"]
                    if name in ("working_set_bytes", "age_ms"):
                        self.assertIsInstance(value, str)
                        self.assertTrue(value.isdigit())
                        value = int(value)
                    self.assertGreaterEqual(value, 0)
                    self.assertTrue(math.isfinite(value))
            else:
                self.assertIsNone(field["value"])

    def test_empty_collection_is_valid_json(self):
        self.assertEqual(self.snapshot("-n", "__wtop_nonexistent_process_6fa211__")["processes"], [])

    def test_legacy_dump_and_column_aliases(self):
        result = self.run_cli("-d", "-s", "TIME", "-p", str(os.getpid()), "--interval", "50", "--no-config")
        self.assertIn("WORKING_SET_BYTES", result.stdout)
        self.assertIn(str(os.getpid()), result.stdout)
        self.assertEqual(len(self.snapshot("-s", "DISK", "-p", str(os.getpid()))["processes"]), 1)

    def test_format_without_mode_selects_snapshot(self):
        result = self.run_cli("--format", "json", "--interval", "50", "--no-config", "-p", str(os.getpid()))
        self.assertEqual(json.loads(result.stdout)["processes"][0]["pid"], os.getpid())

    def test_invalid_arguments_fail_without_output(self):
        for args in (
            ("--unknown",), ("--interval", "0"), ("--interval", "60001"),
            ("--interval", "1ms"), ("--tui", "--format", "json"),
            ("-p", "1,"), ("-p", "-1"), ("-p", "4294967296"),
            ("-n", ","), ("-s", "nonsense"), ("--config",),
        ):
            with self.subTest(args=args):
                result = self.run_cli(*args, code=2)
                self.assertEqual(result.stdout, "")
                self.assertTrue(result.stderr)

    def test_configuration_bounds_and_unicode_path(self):
        with tempfile.TemporaryDirectory(prefix="wtop-") as temp:
            config = Path(temp) / "config-\u00e9.conf"
            config.write_text("BGHighlightColor 0xff\nRedrawInterval 50\n", encoding="utf-8")
            result = self.run_cli("--snapshot", "--format", "json", "--config", str(config), "-p", str(os.getpid()))
            self.assertEqual(json.loads(result.stdout)["processes"][0]["pid"], os.getpid())
            for content in ("RedrawInterval -1\n", "FGColor 256\n", "RedrawInterval 100ms\n", "X" * 5000):
                config.write_text(content, encoding="utf-8")
                with self.subTest(content=content[:30]):
                    result = self.run_cli("--snapshot", "--config", str(config), code=2)
                    self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
