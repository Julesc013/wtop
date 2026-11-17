"""Exercise the actual native binary through redirected, bounded CLI invocations."""

from __future__ import annotations

import json
import math
import os
import stat
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
        self.assertEqual(data["schema"], 2)
        self.assertEqual(data["profile"], self.expected_profile)
        self.assertGreaterEqual(data["sample_elapsed_ms"], 1)
        self.assertEqual([p["pid"] for p in data["processes"]], [os.getpid()])
        row = data["processes"][0]
        self.assertTrue(row["name"])
        self.assertGreater(row["threads"], 0)
        self.assertTrue(row["creation_filetime"].isdigit())
        for name in ("cpu_percent", "working_set_bytes", "io_read_write_bytes_per_second", "age_ms", "owner"):
            field = row[name]
            self.assertIn(field["status"], ("valid", "denied", "unsupported", "warming_up", "reset", "error", "capacity", "stale"))
            if field["status"] == "valid":
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

    def test_snapshot_system_summary_and_observation_times(self):
        data = self.snapshot("-p", str(os.getpid()))
        self.assertEqual(data["cpu_normalization"], "one-core")
        sample = data["sample"]
        self.assertIs(sample["complete"], True)
        self.assertEqual(sample["clock"], "GetTickCount64-ms")
        self.assertEqual(sample["stale_after_ms"], 3 * sample["requested_interval_ms"])
        times = [int(sample[name]) for name in ("started_ms", "observed_ms", "published_ms", "read_ms")]
        self.assertEqual(times, sorted(times))
        self.assertEqual(sample["age_ms"], times[-1] - times[1])
        self.assertGreater(int(sample["sequence"]), 0)
        system = data["system"]
        self.assertIn("primary processor group", system["cpu_scope"])
        for name in ("physical_total_bytes", "physical_available_bytes", "commit_bytes", "commit_limit_bytes"):
            field = system[name]
            self.assertEqual(field["unit"], "bytes")
            if field["status"] == "valid":
                self.assertIsInstance(field["value"], str)
                self.assertTrue(field["value"].isdigit())
            else:
                self.assertIsNone(field["value"])
        if system["physical_total_bytes"]["status"] == system["physical_available_bytes"]["status"] == "valid":
            self.assertGreaterEqual(int(system["physical_total_bytes"]["value"]), int(system["physical_available_bytes"]["value"]))
        if system["commit_limit_bytes"]["status"] == "valid":
            self.assertGreater(int(system["commit_limit_bytes"]["value"]), 0)
        row = data["processes"][0]
        self.assertLessEqual(int(row["cpu_observed_ms"]), int(sample["observed_ms"]))
        self.assertLessEqual(int(row["observed_ms"]), int(sample["observed_ms"]))
        if row["cpu_percent"]["status"] == "valid":
            self.assertGreater(row["cpu_elapsed_ms"], 0)
            self.assertGreaterEqual(row["cpu_acquisition_ms"], 0)
        machine = self.snapshot("-p", str(os.getpid()), "--cpu-normalization", "machine")
        self.assertEqual(machine["cpu_normalization"], "active-machine-capacity")
        if machine["processes"][0]["cpu_percent"]["status"] == "valid":
            self.assertGreater(machine["cpu_active_capacity"], 0)

    def test_readonly_blocks_explicit_settings_mutation(self):
        with tempfile.TemporaryDirectory(prefix="wtop-readonly-") as temp:
            target = Path(temp) / "must-not-exist.conf"
            result = self.run_cli("--readonly", "--no-config", "--write-config", str(target), code=2)
            self.assertFalse(target.exists())
            self.assertIn("readonly", result.stderr)

    def test_empty_collection_is_valid_json(self):
        self.assertEqual(self.snapshot("-n", "__wtop_nonexistent_process_6fa211__")["processes"], [])

    def test_legacy_dump_and_column_aliases(self):
        result = self.run_cli("-d", "-s", "TIME", "-p", str(os.getpid()), "--interval", "50", "--no-config")
        self.assertIn("WORKING_SET_BYTES", result.stdout)
        self.assertIn("SYSTEM\tCOMMIT_BYTES", result.stdout)
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
            ("--cpu-normalization", "nonsense"),
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

    def test_write_config_is_explicit_bounded_and_preserves_user_content(self):
        with tempfile.TemporaryDirectory(prefix="wtop-settings-") as temp:
            root = Path(temp)
            config = root / "settings-\u00e9.conf"
            self.run_cli(
                "--no-config", "--write-config", str(config),
                "--keymap", "emacs", "--theme", "htop",
                "--mouse", "off", "--no-status-bar", "--interval", "60000",
            )
            # The 8-second caller timeout also proves this metadata-like action
            # did not wait for the requested 60-second collection interval.
            first = config.read_text(encoding="utf-8-sig")
            for expected in (
                "ConfigVersion 2", "Keymap emacs", "Theme htop", "Mouse off",
                "FunctionBar 0", "ShowTabs 1", "Tab0.Name \"Main\"",
                "Tab1.Name \"I/O\"", "RedrawInterval 60000",
            ):
                self.assertIn(expected, first)
            config.write_text(
                "# operator note: preserve this\nFuture.Extension inert custom value\n" + first,
                encoding="utf-8",
            )
            self.run_cli("--config", str(config), "--write-config", str(config), "--keymap", "vim", "--status-bar")
            saved = config.read_text(encoding="utf-8-sig")
            self.assertIn("# operator note: preserve this", saved)
            self.assertIn("Future.Extension inert custom value", saved)
            self.assertIn("Keymap vi", saved)
            self.assertIn("FunctionBar 1", saved)
            self.assertEqual([p.name for p in root.iterdir()], [config.name])
            # Reload the saved file through the real process path as well.
            result = self.run_cli("--config", str(config), "--snapshot", "--format", "json", "--interval", "50", "-p", str(os.getpid()))
            self.assertEqual([p["pid"] for p in json.loads(result.stdout)["processes"]], [os.getpid()])

    def test_write_config_failures_preserve_existing_data(self):
        with tempfile.TemporaryDirectory(prefix="wtop-settings-failure-") as temp:
            root = Path(temp)
            missing = root / "missing-parent" / "settings.conf"
            result = self.run_cli("--no-config", "--write-config", str(missing), code=1)
            self.assertTrue(result.stderr)
            self.assertFalse(missing.exists())
            self.assertEqual(list(root.iterdir()), [])
            config = root / "readonly.conf"
            original = b"# original must survive a rejected write\r\nFuture.Value sentinel\r\n"
            config.write_bytes(original)
            config.chmod(stat.S_IREAD)
            try:
                result = self.run_cli("--no-config", "--write-config", str(config), code=1)
                self.assertTrue(result.stderr)
                self.assertEqual(config.read_bytes(), original)
                self.assertEqual([p.name for p in root.iterdir()], [config.name])
            finally:
                config.chmod(stat.S_IREAD | stat.S_IWRITE)

    def test_settings_options_reject_malformed_values_without_creating_files(self):
        with tempfile.TemporaryDirectory(prefix="wtop-settings-invalid-") as temp:
            root = Path(temp)
            target = root / "must-not-exist.conf"
            for options in (
                ("--keymap", "unknown"), ("--theme", "unknown"),
                ("--mouse", "yes"), ("--snapshot",), ("--format", "json"),
                ("-p", "123"), ("-n", "explorer"), ("-u", "user"),
            ):
                with self.subTest(options=options):
                    self.run_cli("--no-config", "--write-config", str(target), *options, code=2)
                    self.assertFalse(target.exists())
            self.assertEqual(list(root.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
