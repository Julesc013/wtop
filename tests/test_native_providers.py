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
import hashlib
import math
import os
from pathlib import Path
import subprocess
import sys
import unittest

EXE = None
OBSERVATIONS = None
if __name__ == "__main__":
    if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
        raise SystemExit("usage: test_native_providers.py PATH_TO_WTOP_EXE")
    EXE = str(Path(sys.argv.pop(1)).resolve())
    if len(sys.argv) > 2 and sys.argv[1] == "--observations":
        sys.argv.pop(1)
        OBSERVATIONS = Path(sys.argv.pop(1))

STATES = {
    "valid", "warming_up", "unsupported", "denied", "unavailable", "error",
    "timeout", "cancelled", "capacity", "protocol_error", "identity_changed",
    "identity_unavailable", "retirement_pending", "admission_closed",
}


@unittest.skipUnless(os.name == "nt" and EXE, "requires an explicit native Windows executable")
class NativeProviderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.observations = []
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        kernel.GetCurrentThread.restype = wintypes.HANDLE
        kernel.GetCurrentThreadId.restype = wintypes.DWORD
        kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        kernel.GetThreadTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        kernel.GetThreadPriority.argtypes = [wintypes.HANDLE]
        kernel.GetThreadPriority.restype = ctypes.c_int
        kernel.GetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
        times = [wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(kernel.GetCurrentProcess(), *(ctypes.byref(value) for value in times)):
            raise ctypes.WinError(ctypes.get_last_error())
        cls.creation = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        cls.kernel = kernel

    @classmethod
    def tearDownClass(cls):
        if OBSERVATIONS:
            receipt = {
                "schema": 1, "kind": "native-provider-observations",
                "exe_sha256": hashlib.sha256(Path(EXE).read_bytes()).hexdigest(),
                "windows": list(sys.getwindowsversion()),
                "observations": cls.observations,
                "limitation": "Typed degradation checks can pass without a provider supplying data; inspect statuses. No target or release qualification is implied.",
            }
            OBSERVATIONS.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

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
        self.observations.append({"test": self._testMethodName, "profile": data["profile"], "provider": name,
            **{key: provider.get(key) for key in ("status", "error_domain", "native_error", "count", "total_count",
                "started_ms", "observed_ms", "elapsed_ms", "acquisition_ms", "retirement_confirmed", "response_received", "scope")}})
        self.assertIn(provider["status"], STATES)
        self.assertIsInstance(provider["retirement_confirmed"], bool)
        self.assertIsInstance(provider["response_received"], bool)
        self.assertIsInstance(provider["error_domain"], int)
        self.assertIsInstance(provider["native_error"], int)
        self.assertGreaterEqual(provider["total_count"], provider["count"])
        self.assertTrue(str(provider["started_ms"]).isdigit())
        self.assertTrue(str(provider["observed_ms"]).isdigit())
        if provider["status"] in ("valid", "capacity"):
            self.assertTrue(provider["retirement_confirmed"])
            self.assertTrue(provider["response_received"])
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

    def test_thread_rows_use_real_lifetime_counters_and_individual_states(self):
        ordinary, _ = self.run_json("inspect", str(os.getpid()))
        self.assertNotIn("threads", ordinary.get("providers", {}))

        def direct_times():
            values = [wintypes.FILETIME() for _ in range(4)]
            if not self.kernel.GetThreadTimes(self.kernel.GetCurrentThread(), *(ctypes.byref(v) for v in values)):
                raise ctypes.WinError(ctypes.get_last_error())
            return [(v.dwHighDateTime << 32) | v.dwLowDateTime for v in values]

        tid = self.kernel.GetCurrentThreadId()
        before = direct_times()
        data, code = self.run_json("inspect", str(os.getpid()), "--threads")
        after = direct_times()
        provider = self.provider(data, "threads")
        # This is an owned, live same-user process. Its primary thread is an
        # independent oracle, not a synthetic expected row copied from Wtop.
        self.assertEqual(provider["status"], "valid", provider)
        self.assertEqual(code, 0)
        self.assertEqual(provider["pid"], os.getpid())
        self.assertEqual(int(provider["creation_filetime"]), self.creation)
        self.assertEqual(len(provider["value"]), provider["count"])
        self.assertLessEqual(provider["count"], 1024)
        main = None
        identities = set()
        for row in provider["value"]:
            self.assertEqual(row["pid"], os.getpid())
            self.assertGreater(row["tid"], 0)
            for field in ("identity_status", "times_status", "priority_status"):
                self.assertIn(row[field], STATES)
            self.assertNotIn("cpu_percent", row)
            self.assertIsInstance(row["observed_ms"], str)
            if row["identity_status"] == "valid":
                self.assertIsInstance(row["creation_filetime"], str)
                self.assertGreater(int(row["creation_filetime"]), 0)
                key = (row["tid"], row["creation_filetime"])
                self.assertNotIn(key, identities)
                identities.add(key)
            else:
                self.assertIsNone(row["creation_filetime"])
            if row["times_status"] == "valid":
                self.assertEqual(row["identity_status"], "valid")
                for field in ("kernel_100ns", "user_100ns"):
                    self.assertIsInstance(row[field], str)
                    self.assertGreaterEqual(int(row[field]), 0)
            else:
                self.assertIsNone(row["kernel_100ns"])
                self.assertIsNone(row["user_100ns"])
            if row["priority_status"] == "valid":
                self.assertIsInstance(row["priority"], int)
            else:
                self.assertIsNone(row["priority"])
            if row["tid"] == tid:
                main = row
        self.assertIsNotNone(main)
        self.assertEqual(int(main["creation_filetime"]), before[0])
        self.assertEqual(main["times_status"], "valid")
        self.assertLessEqual(before[2], int(main["kernel_100ns"]))
        self.assertLessEqual(int(main["kernel_100ns"]), after[2])
        self.assertLessEqual(before[3], int(main["user_100ns"]))
        self.assertLessEqual(int(main["user_100ns"]), after[3])
        self.assertEqual(main["priority"], self.kernel.GetThreadPriority(self.kernel.GetCurrentThread()))

    def test_affinity_read_discloses_scope_and_preserves_mask_width(self):
        ordinary, _ = self.run_json("inspect", str(os.getpid()))
        self.assertNotIn("affinity", ordinary.get("providers", {}))
        data, code = self.run_json("inspect", str(os.getpid()), "--affinity", "--readonly")
        provider = self.provider(data, "affinity")
        self.assertEqual(provider["pid"], os.getpid())
        self.assertEqual(int(provider["creation_filetime"]), self.creation)
        value = provider["value"]
        if provider["status"] == "valid":
            process_mask, system_mask = ctypes.c_size_t(), ctypes.c_size_t()
            if not self.kernel.GetProcessAffinityMask(self.kernel.GetCurrentProcess(), ctypes.byref(process_mask), ctypes.byref(system_mask)):
                raise ctypes.WinError(ctypes.get_last_error())
            self.assertIsInstance(value, dict)
            self.assertEqual(value["status"], "valid")
            self.assertEqual(value["limitation"], "none")
            self.assertEqual(value["system_group_count"], 1)
            self.assertEqual(value["process_group_count"], 1)
            self.assertIn(value["mask_bits"], (32, 64))
            for field in ("process_mask", "system_mask"):
                self.assertRegex(value[field], r"^0x[0-9a-fA-F]{16}$")
            self.assertEqual(int(value["process_mask"], 16), process_mask.value)
            self.assertEqual(int(value["system_mask"], 16), system_mask.value)
            self.assertEqual(code, 0)
        else:
            self.assertEqual(code, 3)
            if value is not None:
                self.assertIsNone(value["process_mask"])
                self.assertIsNone(value["system_mask"])
                if provider["status"] == "unsupported":
                    self.assertIn(value["limitation"], ("multiple_processor_groups", "native_mask_width", "api_unavailable"))

    def test_full_image_path_does_not_implicitly_collect_command_lines(self):
        ordinary, _ = self.run_json("inspect", str(os.getpid()))
        self.assertNotIn("image_path", ordinary.get("providers", {}))
        data, code = self.run_json("inspect", str(os.getpid()), "--image-path")
        provider = self.provider(data, "image_path")
        self.assertNotIn("command_line", data.get("providers", {}))
        self.assertEqual(provider["status"], "valid", provider)
        self.assertEqual(code, 0)
        self.assertEqual(provider["pid"], os.getpid())
        self.assertEqual(int(provider["creation_filetime"]), self.creation)
        self.assertEqual(os.path.normcase(os.path.realpath(provider["value"])), os.path.normcase(os.path.realpath(sys.executable)))

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

    def test_optional_per_core_work_does_not_age_the_process_snapshot(self):
        result = subprocess.run(
            [EXE, "--snapshot", "--per-core", "--interval", "50", "--pid", str(os.getpid()),
             "--format", "json", "--no-config"],
            stdin=subprocess.DEVNULL, capture_output=True,
            encoding="utf-8", errors="strict", timeout=20,
        )
        self.assertIn(result.returncode, (0, 3), result.stderr)
        data = json.loads(result.stdout)
        self.assertEqual(data["schema"], 2)
        self.assertEqual([row["pid"] for row in data["processes"]], [os.getpid()])
        row = data["processes"][0]
        # Native measurement of this owned, live process is independently
        # available. A slow/unsupported optional PDH query must not stale it.
        for name in ("cpu_percent", "working_set_bytes"):
            self.assertEqual(row[name]["status"], "valid", (name, row[name], data["sample"]))
            self.assertIsNotNone(row[name]["value"])
        core = self.provider(data, "per_core")
        if core["status"] in ("valid", "capacity"):
            self.assertLessEqual(int(core["observed_ms"]), int(row["cpu_observed_ms"]))

    def test_private_worker_rejects_missing_or_invalid_inherited_handles(self):
        for args in (("--wtop-provider-v2",), ("--wtop-provider-v2", "0", "0"),
                     ("--wtop-provider-v2", "-1", "1"),
                     ("--wtop-provider-v2", "184467440737095516160", "1")):
            with self.subTest(args=args):
                result = subprocess.run([EXE, *args], stdin=subprocess.DEVNULL,
                    capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")


if __name__ == "__main__":
    unittest.main()
