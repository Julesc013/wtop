"""Native, public-CLI contracts for optional providers and lifetime qualification.

These tests query their own Python process or read-only system metadata. They
never signal, reprioritize, install a service, or inspect command lines of an
unrelated process. A denied/unsupported/timed-out provider is an explicit result,
not a passing claim that the platform supplied that capability.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import unittest

EXE = None
if __name__ == "__main__":
    if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
        raise SystemExit("usage: test_native_providers.py PATH_TO_WTOP_EXE")
    EXE = str(Path(sys.argv.pop(1)).resolve())

STATES = {
    "valid", "warming_up", "unsupported", "denied", "unavailable", "error",
    "timeout", "cancelled", "capacity", "protocol_error", "identity_changed",
    "identity_unavailable", "retirement_pending", "admission_closed",
}


@unittest.skipUnless(os.name == "nt" and EXE, "requires an explicit native Windows executable")
class NativeProviderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        times = [wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(kernel.GetCurrentProcess(), *(ctypes.byref(value) for value in times)):
            raise ctypes.WinError(ctypes.get_last_error())
        cls.creation = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime

    def run_json(self, *args):
        result = subprocess.run(
            [EXE, *args, "--format", "json", "--interval", "100", "--no-config"],
            stdin=subprocess.DEVNULL, capture_output=True,
            encoding="utf-8", errors="strict", timeout=20,
        )
        self.assertIn(result.returncode, (0, 3), result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual(data["schema"], 2)
        return data, result.returncode

    def provider(self, data, name):
        provider = data["providers"][name]
        self.assertIn(provider["status"], STATES)
        self.assertIsInstance(provider["retirement_confirmed"], bool)
        self.assertIsInstance(provider["error_domain"], int)
        self.assertIsInstance(provider["native_error"], int)
        self.assertGreaterEqual(provider["total_count"], provider["count"])
        self.assertTrue(str(provider["started_ms"]).isdigit())
        self.assertTrue(str(provider["observed_ms"]).isdigit())
        if provider["status"] in ("valid", "capacity"):
            self.assertTrue(provider["retirement_confirmed"])
            self.assertGreater(int(provider["started_ms"]), 0)
            self.assertGreaterEqual(int(provider["observed_ms"]), int(provider["started_ms"]))
        for name in ("elapsed_ms", "acquisition_ms"):
            self.assertTrue(math.isfinite(provider[name]))
            self.assertGreaterEqual(provider[name], 0)
        return provider

    def test_command_line_is_explicit_and_lifetime_bound(self):
        ordinary, _ = self.run_json("inspect", str(os.getpid()))
        self.assertNotIn("command_line", ordinary.get("providers", {}))
        data, code = self.run_json("inspect", str(os.getpid()), "--command-line")
        provider = self.provider(data, "command_line")
        self.assertEqual(provider["pid"], os.getpid())
        self.assertEqual(int(provider["creation_filetime"]), self.creation)
        if provider["status"] == "valid":
            self.assertIsInstance(provider["value"], str)
            self.assertIn(Path(__file__).name, provider["value"])
            self.assertEqual(code, 0)
        else:
            self.assertIsNone(provider["value"])
            self.assertEqual(code, 3)

    def test_service_inspection_keeps_target_identity(self):
        data, _ = self.run_json("inspect", str(os.getpid()), "--services")
        provider = self.provider(data, "services")
        self.assertEqual(provider["pid"], os.getpid())
        self.assertEqual(int(provider["creation_filetime"]), self.creation)
        if provider["status"] in ("valid", "capacity"):
            self.assertEqual(len(provider["value"]), provider["count"])
            # This Python fixture is not installed in the service database.
            self.assertEqual(provider["value"], [])

    def test_service_inventory_qualifies_each_pid_association(self):
        data, _ = self.run_json("query", "services")
        provider = self.provider(data, "services")
        if provider["status"] in ("valid", "capacity"):
            self.assertEqual(len(provider["value"]), provider["count"])
            for row in provider["value"]:
                self.assertIsInstance(row["name"], str)
                self.assertIsInstance(row["display_name"], str)
                self.assertIn(row["identity_status"], STATES)
                self.assertGreaterEqual(row["pid"], 0)
                if row["identity_status"] == "valid":
                    self.assertIn(row["service_state"], (4, 5, 6, 7))
                    self.assertGreater(row["pid"], 0)
                    self.assertGreater(int(row["creation_filetime"]), 0)
                if row["pid"] == 0:
                    self.assertNotEqual(row["identity_status"], "valid")

    def test_per_core_labels_are_observed_instances(self):
        data, _ = self.run_json("query", "system", "--per-core")
        provider = self.provider(data, "per_core")
        if provider["status"] in ("valid", "capacity"):
            self.assertIn(provider["scope"], ("legacy-processor", "processor-information"))
            self.assertGreater(provider["elapsed_ms"], 0)
            self.assertEqual(len(provider["value"]), provider["count"])
            self.assertGreater(provider["count"], 0)
            labels = set()
            for row in provider["value"]:
                self.assertNotIn("_Total", row["instance"])
                self.assertNotIn(row["instance"], labels)
                labels.add(row["instance"])
                self.assertIn(row["status"], STATES)
                if row["status"] == "valid":
                    self.assertTrue(math.isfinite(row["busy_percent"]))
                    self.assertGreaterEqual(row["busy_percent"], 0)
                else:
                    self.assertIsNone(row["busy_percent"])
                if provider["scope"] == "legacy-processor":
                    self.assertIsNone(row["group"])

    def test_private_worker_rejects_missing_or_invalid_inherited_handles(self):
        for args in (("--wtop-provider-v1",), ("--wtop-provider-v1", "0", "0"),
                     ("--wtop-provider-v1", "-1", "1"),
                     ("--wtop-provider-v1", "184467440737095516160", "1")):
            with self.subTest(args=args):
                result = subprocess.run([EXE, *args], stdin=subprocess.DEVNULL,
                    capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")


if __name__ == "__main__":
    unittest.main()
