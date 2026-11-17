"""Exercise the actual native binary through redirected, bounded CLI invocations."""

from __future__ import annotations

import json
import math
import os
import stat
import ntpath
import re
import shutil
import signal
import time
import traceback
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

    def run_cli(self, *args: str, code: int = 0, timeout: float = 8, cwd=None, executable=None):
        result = subprocess.run(
            [executable or EXE, *args], stdin=subprocess.DEVNULL, capture_output=True,
            encoding="utf-8", errors="strict", timeout=timeout, cwd=cwd,
        )
        self.assertEqual(result.returncode, code, result.stderr)
        return result

    @staticmethod
    def path_key(value: str):
        if value.startswith("\\\\?\\UNC\\"):
            value = "\\\\" + value[8:]
        elif value.startswith("\\\\?\\"):
            value = value[4:]
        return ntpath.normcase(ntpath.normpath(value))

    def assert_u64(self, value):
        self.assertIsInstance(value, str)
        self.assertRegex(value, r"^(0|[1-9][0-9]*)$")
        self.assertLessEqual(int(value), 18446744073709551615)

    def assert_typed_system(self, data):
        self.assertEqual(data["schema"], 2)
        self.assertEqual(data["profile"], self.expected_profile)
        self.assertIsInstance(data["text_encoding_loss"], bool)
        system = data["system"]
        for timestamp in ("cpu_observed_ms", "memory_observed_ms"):
            self.assert_u64(system[timestamp])
        units = {"cpu_busy_percent": "percent", "active_logical_processors": "count",
                 "physical_total_bytes": "bytes", "physical_available_bytes": "bytes",
                 "commit_bytes": "bytes", "commit_limit_bytes": "bytes", "uptime_ms": "milliseconds"}
        states = {"valid", "denied", "unsupported", "warming_up", "reset", "error", "capacity", "stale"}
        for name, unit in units.items():
            with self.subTest(system_metric=name):
                field = system[name]
                self.assertEqual(field["unit"], unit)
                self.assertIn(field["status"], states)
                if field["status"] != "valid":
                    self.assertIsNone(field["value"])
                elif name == "cpu_busy_percent":
                    self.assertIn(type(field["value"]), (int, float))
                    self.assertTrue(math.isfinite(field["value"]))
                    self.assertGreaterEqual(field["value"], 0)
                    self.assertLessEqual(field["value"], 100)
                else:
                    self.assert_u64(field["value"])
        if system["active_logical_processors"]["status"] == "valid":
            self.assertGreater(int(system["active_logical_processors"]["value"]), 0)
        sample = data["sample"]
        self.assertIsInstance(sample["complete"], bool)
        self.assert_u64(sample["sequence"])
        for name in ("started_ms", "observed_ms", "published_ms", "read_ms"):
            self.assert_u64(sample[name])
        for name in ("observed_process_count", "selected_process_count", "unresolved_owner_filter_count"):
            self.assertIs(type(sample[name]), int)
            self.assertGreaterEqual(sample[name], 0)
        self.assertGreaterEqual(sample["observed_process_count"], sample["selected_process_count"])

    def json_lines(self, payload: bytes):
        self.assertNotIn(b"\x1b", payload, "machine output contains terminal escape bytes")
        if payload:
            self.assertTrue(payload.endswith(b"\n"), "incomplete final JSON Lines record")
        records = []
        for line in payload.splitlines():
            self.assertTrue(line, "blank/progress line in JSON Lines output")
            value = json.loads(line.decode("utf-8"), parse_constant=lambda token: self.fail(f"non-JSON numeric token {token}"))
            self.assert_typed_system(value)
            records.append(value)
        sequences = [int(record["sample"]["sequence"]) for record in records]
        self.assertEqual(sequences, sorted(set(sequences)), "samples must have distinct advancing sequence identities")
        return records

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
        self.assert_typed_system(data)
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
            ("--pid=",), ("--pid=-1",), ("--name=",), ("--sort-key=unknown",),
            ("--readonly=true",), ("--version=value",), ("--delay=0",), ("--delay=601",),
            ("--delay=1ms",), ("--portable", "--config=ignored.conf"),
            ("query",), ("query", "unknown"), ("inspect",), ("inspect", "0"),
            ("inspect", "4294967296"), ("inspect", "1", "--pid=1"),
            ("query", "system", "--snapshot"), ("--samples=1",),
            ("log", "--output=-"), ("log", "--samples=1"),
            ("log", "--samples=1", "--output=-", "--format=text"),
            ("log", "--samples=0", "--output=-"), ("log", "--samples=1000001", "--output=-"),
            ("log", "--duration=49", "--output=-"), ("log", "--duration=86400001", "--output=-"),
            ("log", "--samples=1", "--output=-", "--max-bytes=1023"),
            ("log", "--samples=1", "--output=-", "--max-bytes=1073741825"),
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
                remaining = [path for path in root.iterdir() if path != config]
                for path in remaining:
                    self.assertTrue(path.name.startswith(config.name + ".wtop-tmp-") or
                                    path.name.startswith(config.name + ".wtop-backup-") or
                                    path.name.startswith(config.name + ".migration-original-"), path)
                    self.assertIn(path.name, result.stderr, "retained recovery file must be disclosed")
                    recovery = path.read_bytes()
                    if ".wtop-tmp-" in path.name:
                        self.assertTrue(recovery.endswith(b"\n"))
                        self.assertLessEqual(len(recovery), 32768)
                        self.assertIn(b"ConfigVersion 2\n", recovery)
                        # A retained replacement must still be a loadable complete
                        # document; verification is read-only and cannot consume it.
                        self.run_cli("--config", str(path), "--snapshot", "--format=json",
                                     "--interval=50", f"--pid={os.getpid()}")
                        self.assertEqual(path.read_bytes(), recovery)
                    else:
                        # A reservation can still be empty if replacement failed
                        # before moving the old file; otherwise it is the exact old bytes.
                        self.assertIn(recovery, (b"", original))
                if remaining:
                    self.assertIn("recovery-required", result.stderr)
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


    def test_long_aliases_equals_and_legacy_delay_units(self):
        version = json.loads(self.run_cli("-V", "--format=json", "--interval=60000").stdout)
        self.assertEqual(version["version"], (Path(__file__).parents[1] / "VERSION").read_text().strip())
        parent = self.snapshot(f"--pid={os.getpid()}")["processes"][0]
        chosen = self.snapshot(f"--pid={os.getpid()}", f"--name={parent['name']}", "--sort-key=TIME")
        self.assertEqual([row["pid"] for row in chosen["processes"]], [os.getpid()])
        self.assertEqual(self.snapshot("--name=__wtop_name_alias_absent_6811__")["processes"], [])
        owner = parent["owner"]
        self.assertIn("account_name_status", owner)
        self.assert_u64(owner["account_name_observed_ms"])
        if owner["status"] == "valid":
            self.assertIsInstance(owner["sid"], str)
            self.assertTrue(owner["sid"].startswith("S-1-"))
            same_owner = self.snapshot(f"--pid={os.getpid()}", f"--user={owner['sid']}")
            self.assertEqual([row["pid"] for row in same_owner["processes"]], [os.getpid()])
        with tempfile.TemporaryDirectory(prefix="wtop-aliases-") as folder:
            path = Path(folder) / "aliases.conf"
            self.run_cli("--no-config", f"--write-config={path}", "-M", "--no-colour",
                         "--no-function-bar", "--delay=3", "--sort-key=DISK")
            saved = path.read_text(encoding="utf-8")
            for line in ("Mouse off", "Theme monochrome", "FunctionBar 0", "RedrawInterval 300", "Tab0.Sort I/O"):
                self.assertIn(line, saved)
            self.run_cli("--no-config", f"--write-config={path}", "--no-color", "--function-bar", "--delay=2")
            saved = path.read_text(encoding="utf-8")
            self.assertIn("FunctionBar 1", saved)
            self.assertIn("RedrawInterval 200", saved)

    def test_paths_resolves_without_parsing_collection_or_writes(self):
        with tempfile.TemporaryDirectory(prefix="wtop-paths-") as folder:
            root = Path(folder)
            selected = root / "unread-configuration.conf"
            sentinel = b"ConfigVersion 999\nthis file must not be parsed or changed\n"
            selected.write_bytes(sentinel)
            paths = json.loads(self.run_cli("--paths", "--format=json", "--interval=60000",
                                           f"--config={selected}").stdout)
            self.assertEqual(paths["schema"], 1)
            self.assertEqual(paths["kind"], "settings-paths")
            self.assertEqual(paths["owner"], "explicit")
            self.assertEqual(paths["source"], "explicit-file")
            self.assertEqual(self.path_key(paths["module"]), self.path_key(EXE))
            self.assertEqual(self.path_key(paths["read"]), self.path_key(str(selected)))
            self.assertEqual(self.path_key(paths["write"]), self.path_key(str(selected)))
            self.assertIs(paths["saving_disabled"], False)
            self.assertIsInstance(paths["canonical_aliases"], bool)
            self.assertEqual(selected.read_bytes(), sentinel)
            missing = root / "absent-parent" / "new.conf"
            absent = json.loads(self.run_cli("--paths", "--format=json", f"--config={missing}").stdout)
            self.assertEqual(self.path_key(absent["write"]), self.path_key(str(missing)))
            self.assertFalse(missing.parent.exists())
            disabled = json.loads(self.run_cli("--paths", "--format=json", "--no-config", "--readonly").stdout)
            self.assertEqual(disabled["owner"], "memory")
            self.assertEqual(disabled["source"], "compiled")
            self.assertEqual(disabled["read"], "")
            self.assertEqual(disabled["write"], "")
            self.assertIs(disabled["saving_disabled"], True)
            self.assertEqual(list(root.iterdir()), [selected])

    def test_portable_root_marker_and_cwd_ownership(self):
        with tempfile.TemporaryDirectory(prefix="wtop-portable-") as folder:
            root = Path(folder)
            payload, cwd, explicit = root / "payload space", root / "cwd", root / "portable-é"
            for directory in (payload, cwd, explicit):
                directory.mkdir()
            copied_exe = payload / "wtop.exe"
            shutil.copyfile(EXE, copied_exe)
            hostile = cwd / "wtop.conf"
            hostile.write_bytes(b"ConfigVersion 999\n")
            marker = payload / "wtop.portable"
            marker.write_bytes(b"")
            args = ("--paths", "--format=json", "--interval=60000")
            marked = json.loads(self.run_cli(*args, cwd=cwd, executable=str(copied_exe)).stdout)
            self.assertEqual(marked["owner"], "portable")
            self.assertEqual(marked["source"], "compiled")
            self.assertEqual(marked["read"], "")
            self.assertEqual(self.path_key(marked["write"]), self.path_key(str(payload / "wtop.conf")))
            self.assertEqual(self.path_key(marked["module"]), self.path_key(str(copied_exe)))
            legacy = payload / "ntop.conf"
            legacy.write_bytes(b"Keymap vi\n")
            legacy_paths = json.loads(self.run_cli(*args, cwd=cwd, executable=str(copied_exe)).stdout)
            self.assertEqual(legacy_paths["source"], "legacy-file")
            self.assertEqual(self.path_key(legacy_paths["read"]), self.path_key(str(legacy)))
            self.assertEqual(self.path_key(legacy_paths["write"]), self.path_key(str(payload / "wtop.conf")))
            marker.unlink()
            bare = json.loads(self.run_cli(*args, cwd=cwd, executable=str(copied_exe)).stdout)
            self.assertEqual(bare["owner"], "unregistered", "writability alone must not establish portable ownership")
            forced = json.loads(self.run_cli(*args, "--portable", cwd=cwd, executable=str(copied_exe)).stdout)
            self.assertEqual(forced["owner"], "portable")
            root_config = explicit / "wtop.conf"
            root_config.write_bytes(b"ConfigVersion 999\n")
            selected = json.loads(self.run_cli(*args, f"--portable-root={explicit}", "--readonly",
                                               cwd=cwd, executable=str(copied_exe)).stdout)
            self.assertEqual(selected["owner"], "portable")
            self.assertEqual(selected["source"], "portable-file")
            self.assertIs(selected["saving_disabled"], True)
            self.assertEqual(self.path_key(selected["read"]), self.path_key(str(root_config)))
            self.assertEqual(self.path_key(selected["write"]), self.path_key(str(root_config)))
            self.assertEqual(root_config.read_bytes(), b"ConfigVersion 999\n")
            self.assertEqual(hostile.read_bytes(), b"ConfigVersion 999\n")
            self.assertEqual(legacy.read_bytes(), b"Keymap vi\n")
            self.assertEqual({path.name for path in payload.iterdir()}, {"wtop.exe", "ntop.conf"})
            self.assertEqual(list(explicit.iterdir()), [root_config])

    def test_query_and_inspect_reuse_typed_snapshot_contract(self):
        system = json.loads(self.run_cli("query", "system", "--format=json", "--no-config",
                                        "--interval=50").stdout)
        self.run_cli("query", "system", f"--pid={os.getpid()}", code=2)
        self.assert_typed_system(system)
        self.assertEqual(system["processes"], [])
        self.assertEqual(system["command"], "query")
        self.assertEqual(system["providers"], {})
        processes = json.loads(self.run_cli("query", "processes", "--format=json", "--no-config",
                                           "--readonly", "--interval=50", f"--pid={os.getpid()}").stdout)
        self.assert_typed_system(processes)
        self.assertEqual([row["pid"] for row in processes["processes"]], [os.getpid()])
        inspected = json.loads(self.run_cli("inspect", str(os.getpid()), "--format=json", "--readonly",
                                           "--no-config", "--interval=50").stdout)
        self.assert_typed_system(inspected)
        self.assertEqual([row["pid"] for row in inspected["processes"]], [os.getpid()])
        self.assertEqual(inspected["command"], "inspect")
        self.assertEqual(inspected["inspect_pid"], os.getpid())
        self.assertEqual(inspected["processes"][0]["creation_filetime"], processes["processes"][0]["creation_filetime"])
        # DWORD_MAX is outside Windows' allocatable aligned PID values.
        missing = self.run_cli("inspect", "4294967295", "--format=json", "--no-config", "--interval=50", code=3)
        absent = json.loads(missing.stdout)
        self.assert_typed_system(absent)
        self.assertEqual(absent["processes"], [])
        self.assertEqual(absent["command"], "inspect")
        self.assertEqual(absent["inspect_pid"], 4294967295)
        self.assertEqual(absent["providers"], {})

    def test_log_count_stdout_file_and_readonly_observation(self):
        result = self.run_cli("log", "--samples=3", "--output=-", "--interval=50", "--no-config",
                              "--readonly", f"--pid={os.getpid()}")
        self.assertEqual(result.stderr, "")
        records = self.json_lines(result.stdout.encode("utf-8"))
        self.assertEqual(len(records), 3)
        for record in records:
            self.assertEqual(record["command"], "log")
            self.assertEqual([row["pid"] for row in record["processes"]], [os.getpid()])
            self.assertEqual(record["sample"]["requested_interval_ms"], 50)
        with tempfile.TemporaryDirectory(prefix="wtop-log-") as folder:
            target = Path(folder) / "observations-é.jsonl"
            file_run = self.run_cli("log", "--samples=2", f"--output={target}", "--interval=50",
                                    "--no-config", "--readonly", f"--pid={os.getpid()}")
            self.assertEqual(file_run.stdout, "")
            self.assertEqual(file_run.stderr, "")
            self.assertEqual(len(self.json_lines(target.read_bytes())), 2)
            self.assertEqual(list(Path(folder).iterdir()), [target])

    def test_log_duration_stops_before_large_count_bound(self):
        result = self.run_cli("log", "--samples=10000", "--duration=100", "--output=-", "--interval=50",
                              "--no-config", f"--pid={os.getpid()}")
        records = self.json_lines(result.stdout.encode("utf-8"))
        self.assertGreaterEqual(len(records), 1)
        self.assertLess(len(records), 10000)
        self.assertEqual(result.stderr, "")

    def test_log_duration_caps_an_intentional_long_sample_wait(self):
        # The requested 60-second cadence cannot override the 50-ms run budget.
        # The outer timeout checks intentional wait clipping, not a promise of
        # preemption for a blocked native API or remote filesystem.
        result = self.run_cli("log", "--duration=50", "--interval=60000", "--output=-",
                              "--no-config", f"--pid={os.getpid()}")
        records = self.json_lines(result.stdout.encode("utf-8"))
        self.assertGreaterEqual(len(records), 1)
        for record in records:
            self.assertEqual(record["sample"]["requested_interval_ms"], 60000)
            self.assertLess(record["sample_elapsed_ms"], 60000)

    def test_log_preserves_existing_file_and_missing_parent(self):
        with tempfile.TemporaryDirectory(prefix="wtop-log-sentinel-") as folder:
            root = Path(folder)
            target = root / "existing.jsonl"
            original = b"foreign sentinel\r\n"
            target.write_bytes(original)
            result = self.run_cli("log", "--samples=1", f"--output={target}", "--no-config", "--interval=50", code=1)
            self.assertEqual(result.stdout, "")
            self.assertIn("existing destinations are preserved", result.stderr)
            self.assertEqual(target.read_bytes(), original)
            missing = root / "missing" / "capture.jsonl"
            self.run_cli("log", "--samples=1", f"--output={missing}", "--no-config", "--interval=50", code=1)
            self.assertFalse(missing.parent.exists())
            self.assertEqual(list(root.iterdir()), [target])

    def test_log_byte_cap_retains_only_complete_records(self):
        probe = self.run_cli("log", "--samples=1", "--output=-", "--no-config", "--interval=50", f"--pid={os.getpid()}")
        self.assertEqual(len(self.json_lines(probe.stdout.encode("utf-8"))), 1)
        # Use an observed record size plus slack, not a baked-in structure size.
        limit = max(1024, len(probe.stdout.encode("utf-8")) * 2 + 256)
        with tempfile.TemporaryDirectory(prefix="wtop-log-cap-") as folder:
            target = Path(folder) / "bounded.jsonl"
            result = self.run_cli("log", "--samples=100", f"--output={target}", f"--max-bytes={limit}",
                                  "--no-config", "--interval=50", f"--pid={os.getpid()}", code=3)
            payload = target.read_bytes()
            records = self.json_lines(payload)
            self.assertGreaterEqual(len(records), 1)
            self.assertLess(len(records), 100)
            self.assertLessEqual(len(payload), limit)
            self.assertEqual(result.stdout, "")
            report = re.search(r"after (\d+) complete records \((\d+) bytes\): byte limit reached", result.stderr)
            self.assertIsNotNone(report, result.stderr)
            self.assertEqual(int(report[1]), len(records))
            self.assertEqual(int(report[2]), len(payload))
            self.assertEqual(list(Path(folder).iterdir()), [target])

    def test_broken_log_stdout_is_runtime_failure(self):
        # Only our pipe is closed. No console event is sent to the test runner.
        process = subprocess.Popen([EXE, "log", "--samples=2", "--output=-", "--interval=50", "--no-config",
                                    f"--pid={os.getpid()}"], stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            process.stdout.close()
            process.stdout = None
            _, errors = process.communicate(timeout=8)
            self.assertEqual(process.returncode, 1, errors.decode("utf-8", "replace"))
            self.assertIn(b"output failure", errors)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=5)

    def test_owned_console_log_cancellation(self):
        # Reuse the existing kill-on-close fixture and gated helper protocol.
        # Neither CTRL_BREAK nor injected input reaches the caller's console.
        from test_native_tui import FixtureJob
        with tempfile.TemporaryDirectory(prefix="wtop-cli-cancel-") as folder:
            receipt = Path(folder) / "cancellation.json"
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            job, helper = FixtureJob(), None
            try:
                helper = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), EXE,
                                           "--cancel-worker", str(receipt)], stdin=subprocess.PIPE,
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                          creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
                job.assign(helper)
                _, errors = helper.communicate(b"start\n", timeout=18)
                self.assertEqual(helper.returncode, 0, errors.decode("utf-8", "replace"))
                data = json.loads(receipt.read_text(encoding="utf-8"))
                self.assertEqual(data["exit_code"], 130)
                self.assertLess(data["shutdown_seconds"], 6)
                records = self.json_lines((Path(folder) / "cancelled.jsonl").read_bytes())
                self.assertGreaterEqual(len(records), 1)
            finally:
                job.close()
                if helper is not None and helper.poll() is None:
                    helper.kill()
                    helper.communicate(timeout=5)

    def test_export_preserves_migration_original_and_refuses_future_schema(self):
        with tempfile.TemporaryDirectory(prefix="wtop-migration-") as folder:
            root = Path(folder)
            config = root / "migrate.conf"
            original = b"ConfigVersion 1\r\nKeymap vi\r\n# keep an exact original\r\n"
            config.write_bytes(original)
            exported = self.run_cli("--no-config", f"--write-config={config}", "--keymap=emacs")
            self.assertIn("ConfigVersion 2", config.read_text(encoding="utf-8"))
            backups = list(root.glob(config.name + ".migration-original-*"))
            self.assertEqual(len(backups), 1, "explicit export must retain the prior schema even with --no-config")
            self.assertEqual(backups[0].read_bytes(), original)
            self.assertIn(backups[0].name, exported.stderr, "successful migration must disclose its retained original")
            future = root / "future.conf"
            future_original = b"ConfigVersion 999\nFuture.Semantics must-survive\n"
            future.write_bytes(future_original)
            result = self.run_cli("--no-config", f"--write-config={future}", code=1)
            self.assertIn("invalid", result.stderr)
            self.assertEqual(future.read_bytes(), future_original)
            self.assertEqual({path.name for path in root.iterdir()}, {config.name, backups[0].name, future.name})


def cancellation_worker(exe: str, receipt: Path):
    if sys.stdin.buffer.readline(16) != b"start\n":
        raise RuntimeError("cancellation worker did not receive its job gate")
    child = None
    result = {}
    target = receipt.parent / "cancelled.jsonl"
    try:
        child = subprocess.Popen([exe, "log", "--samples=1000000", "--duration=60000", "--interval=50",
                                  "--no-config", "--readonly", f"--pid={os.getpid()}", f"--output={target}"],
                                 stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if child.poll() is not None:
                raise AssertionError(f"log exited before cancellation: {child.communicate()!r}")
            if target.exists() and target.read_bytes().endswith(b"\n"):
                break
            time.sleep(0.01)
        else:
            raise AssertionError("no complete record was observed before the cancellation deadline")
        started = time.monotonic()
        child.send_signal(signal.CTRL_BREAK_EVENT)
        output, errors = child.communicate(timeout=6)
        result.update(exit_code=child.returncode, shutdown_seconds=time.monotonic() - started,
                      stdout=output.decode("utf-8"), stderr=errors.decode("utf-8"))
        if child.returncode != 130 or output:
            raise AssertionError(result)
    except Exception:
        result["error"] = traceback.format_exc()
        raise
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.communicate(timeout=5)
        receipt.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--cancel-worker":
        cancellation_worker(EXE, Path(sys.argv[2]))
    else:
        unittest.main()
