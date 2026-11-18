#!/usr/bin/env python3
"""Bounded, ordinary-token native resource observations for exact Wtop bytes.

The outer runner can be elevated. Actual queries and private-console helpers
run under the same user's restricted medium-integrity primary token, with the
Administrators SID deny-only and maximum privileges disabled. This establishes
automated token-context behavior; it is not a human desktop or UAC review.
Only owned Wtop/helper processes receive input, cancellation or forced cleanup.
"""
from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import time
import traceback

from test_native_tui import Console, FixtureJob, is_main


class ProcessMemory(C.Structure):
    _fields_ = [("cb", W.DWORD), ("page_fault_count", W.DWORD),
                ("peak_working_set", C.c_size_t), ("working_set", C.c_size_t),
                ("quota_peak_paged", C.c_size_t), ("quota_paged", C.c_size_t),
                ("quota_peak_nonpaged", C.c_size_t), ("quota_nonpaged", C.c_size_t),
                ("pagefile_usage", C.c_size_t), ("peak_pagefile", C.c_size_t),
                ("private_bytes", C.c_size_t)]


def checked(value):
    if not value:
        raise C.WinError(C.get_last_error())
    return value


def write_receipt(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def context():
    kernel = C.WinDLL("kernel32", use_last_error=True)
    security = C.WinDLL("advapi32", use_last_error=True)
    shell = C.WinDLL("shell32", use_last_error=True)
    kernel.GetCurrentProcess.restype = W.HANDLE
    kernel.CloseHandle.argtypes = [W.HANDLE]
    security.OpenProcessToken.argtypes = [W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)]
    security.GetTokenInformation.argtypes = [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]
    security.GetSidSubAuthorityCount.argtypes = [C.c_void_p]
    security.GetSidSubAuthorityCount.restype = C.POINTER(C.c_ubyte)
    security.GetSidSubAuthority.argtypes = [C.c_void_p, W.DWORD]
    security.GetSidSubAuthority.restype = C.POINTER(W.DWORD)
    shell.IsUserAnAdmin.restype = W.BOOL
    token = W.HANDLE()
    checked(security.OpenProcessToken(kernel.GetCurrentProcess(), 0x8, C.byref(token)))
    try:
        needed = W.DWORD()
        security.GetTokenInformation(token, 25, None, 0, C.byref(needed))
        if not needed.value or needed.value > 65536:
            raise AssertionError("unbounded/missing token integrity record")
        buffer = C.create_string_buffer(needed.value)
        checked(security.GetTokenInformation(token, 25, buffer, len(buffer), C.byref(needed)))
        sid = C.cast(buffer, C.POINTER(C.c_void_p))[0]
        count = security.GetSidSubAuthorityCount(sid)[0]
        if not count:
            raise AssertionError("integrity SID has no RID")
        integrity = security.GetSidSubAuthority(sid, count - 1)[0]
        elevated = W.DWORD()
        checked(security.GetTokenInformation(token, 20, C.byref(elevated), C.sizeof(elevated), C.byref(needed)))
        return {"pid": os.getpid(), "is_user_an_admin": bool(shell.IsUserAnAdmin()),
                "integrity_rid": int(integrity), "token_elevation": bool(elevated.value),
                "python_pointer_bits": C.sizeof(C.c_void_p) * 8,
                "python_machine": platform.machine(), "windows": list(sys.getwindowsversion())}
    finally:
        kernel.CloseHandle(token)


def assert_restricted(value):
    assert not value["is_user_an_admin"], value
    assert value["integrity_rid"] == 8192, value  # SECURITY_MANDATORY_MEDIUM_RID


class ResourceReader:
    def __init__(self):
        self.k = C.WinDLL("kernel32", use_last_error=True)
        self.p = C.WinDLL("psapi", use_last_error=True)
        self.k.GetProcessHandleCount.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
        self.k.GetProcessTimes.argtypes = [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4
        self.k.GetCurrentProcess.restype = W.HANDLE
        self.p.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(ProcessMemory), W.DWORD]

    def read(self, handle):
        memory = ProcessMemory(); memory.cb = C.sizeof(memory)
        count = W.DWORD()
        checked(self.k.GetProcessHandleCount(handle, C.byref(count)))
        checked(self.p.GetProcessMemoryInfo(handle, C.byref(memory), C.sizeof(memory)))
        times = [W.FILETIME() for _ in range(4)]
        checked(self.k.GetProcessTimes(handle, *(C.byref(v) for v in times)))
        units = [(v.dwHighDateTime << 32) | v.dwLowDateTime for v in times]
        return {"monotonic_s": time.monotonic(), "handle_count": count.value,
                "private_bytes": memory.private_bytes, "working_set_bytes": memory.working_set,
                "creation_filetime": str(units[0]), "kernel_100ns": str(units[2]),
                "user_100ns": str(units[3])}


def run_json(exe, *args):
    job = FixtureJob()
    process = None
    try:
        process = subprocess.Popen([exe, *args, "--format", "json", "--interval", "100", "--no-config"],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   encoding="utf-8", errors="strict")
        job.assign(process)
        out, error = process.communicate(timeout=20)
        assert process.returncode in (0, 3), (process.returncode, error)
        data = json.loads(out)
        assert data["schema"] == 2, data
        return data, process.returncode
    finally:
        # The restricted harness also owns one-shot children. Its own forced
        # exit closes this non-inherited job, including a blocked monitor.
        job.close()
        if process is not None and process.poll() is None:
            process.kill()  # Handles the unassigned-child failure case only.
            process.communicate(timeout=5)


def console_worker(exe, output):
    receipt = {"result": "failed", "context": context(), "samples": [], "human_review": False}
    console = None
    try:
        assert_restricted(receipt["context"])
        reader = ResourceReader()
        console = Console()
        with tempfile.TemporaryDirectory(prefix="wtop-resource-console-") as temporary:
            config = Path(temporary) / "settings.conf"
            result = subprocess.run([exe, "--no-config", "--write-config", str(config)],
                                    capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr.decode("utf-8", "replace")
            original = config.read_bytes()
            console.start(exe, config, [os.getpid()], "--readonly", "--no-mouse", interval=250)
            console.wait(is_main, "ordinary-token main view")
            monitor = console.process  # Hold its native lifetime handle through verification.
            receipt["monitor_pid"] = monitor.pid
            receipt["monitor_started_unix_s"] = time.time()
            time.sleep(2)  # Explicit warm-up; first-use allocations are outside growth window.
            for index in range(6):
                if index:
                    time.sleep(2)
                assert monitor.poll() is None, monitor.returncode
                receipt["samples"].append(reader.read(int(monitor._handle)))
            samples = receipt["samples"]
            baseline, final = samples[0], samples[-1]
            elapsed = final["monotonic_s"] - baseline["monotonic_s"]
            cpu = ((int(final["kernel_100ns"]) + int(final["user_100ns"])) -
                   (int(baseline["kernel_100ns"]) + int(baseline["user_100ns"]))) / 10_000_000
            handle_growth = max(s["handle_count"] for s in samples) - baseline["handle_count"]
            private_growth = max(s["private_bytes"] for s in samples) - baseline["private_bytes"]
            receipt["measurements"] = {"sample_window_s": elapsed, "cpu_time_s": cpu,
                "cpu_percent_one_logical_processor": 100 * cpu / elapsed,
                "peak_handle_growth": handle_growth, "peak_private_growth_bytes": private_growth}
            # These are deliberately coarse bounded-regression limits, not a
            # zero-overhead claim. System process churn changes retained rows.
            receipt["growth_limits"] = {"handles": 512, "private_bytes": 64 * 1024 * 1024,
                                         "cpu_percent": None}
            assert elapsed >= 9.5 and cpu >= 0, receipt["measurements"]
            assert handle_growth <= 512, receipt["measurements"]
            assert private_growth <= 64 * 1024 * 1024, receipt["measurements"]
            assert config.read_bytes() == original, "idle readonly TUI changed settings"
            receipt["cancel_started_unix_s"] = time.time()
            started = time.monotonic()
            console.check(console.k.GenerateConsoleCtrlEvent(0, 0))
            console.stop(send_key=False)
            receipt["cancel_to_retirement_s"] = time.monotonic() - started
            receipt["monitor_exit_code"] = monitor.returncode
            assert monitor.returncode == 0
            assert receipt["cancel_to_retirement_s"] <= 5
            assert config.read_bytes() == original
            receipt["result"] = "passed"
    except BaseException:
        receipt["failure"] = traceback.format_exc()
        raise
    finally:
        try:
            if console is not None:
                console.close()
        except BaseException:
            receipt["result"] = "failed"
            receipt["cleanup_failure"] = traceback.format_exc()
            raise
        finally:
            write_receipt(output, receipt)


def restricted_worker(exe, output):
    receipt = {"schema": 1, "kind": "native-resource-observations", "result": "failed",
               "at_utc": datetime.now(timezone.utc).isoformat(), "exe_sha256": hashlib.sha256(Path(exe).read_bytes()).hexdigest(),
               "source_revision": os.environ.get("GITHUB_SHA"), "context": context(),
               "queries": [], "console_workers": [], "human_review": False,
               "limits": ["restricted same-user medium token; no human desktop/UAC qualification",
                          "short resource window and coarse growth gates; not proof of absence of every leak",
                          "CPU is measured and reported; no timing-dependent CPU pass threshold",
                          "only owned monitor/helper retirement is asserted; no service-side WMI cancellation claim"]}
    jobs = []
    workers = []
    try:
        assert_restricted(receipt["context"])
        reader = ResourceReader()
        # Prime ctypes and subprocess before measuring the test harness itself.
        warm, code = run_json(exe, "inspect", str(os.getpid()), "--image-path")
        assert code == 0 and warm["providers"]["image_path"]["status"] == "valid", warm
        receipt["profile"] = warm["profile"]; receipt["version"] = warm["version"]
        baseline = reader.read(reader.k.GetCurrentProcess())
        for index in range(6):
            for args in (("query", "processes", "--pid", str(os.getpid())),
                         ("inspect", str(os.getpid()), "--image-path", "--threads", "--affinity", "--readonly")):
                data, code = run_json(exe, *args)
                assert [row["pid"] for row in data["processes"]] == [os.getpid()], data
                states = {name: value["status"] for name, value in data.get("providers", {}).items()}
                if args[0] == "inspect":
                    assert states["image_path"] == states["threads"] == "valid", states
                    assert states["affinity"] in ("valid", "unsupported"), states
                    assert "command_line" not in states
                receipt["queries"].append({"iteration": index, "command": list(args), "exit_code": code,
                                            "provider_states": states})
        final = reader.read(reader.k.GetCurrentProcess())
        receipt["harness_resources"] = {"before": baseline, "after": final,
            "handle_growth": final["handle_count"] - baseline["handle_count"],
            "private_growth_bytes": final["private_bytes"] - baseline["private_bytes"]}
        assert receipt["harness_resources"]["handle_growth"] <= 8, receipt["harness_resources"]
        with tempfile.TemporaryDirectory(prefix="wtop-resource-workers-") as temporary:
            directory = Path(temporary)
            # Every helper has its own hidden console and a parent-held job.
            # A start barrier prevents it creating descendants before assignment.
            for index in range(3):
                destination = directory / f"console-{index}.json"
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 0
                job = FixtureJob(); jobs.append(job)
                process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), exe,
                    "--console-worker", str(destination)], creationflags=subprocess.CREATE_NEW_CONSOLE,
                    startupinfo=startup, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                workers.append((process, destination))
                job.assign(process)
                process.stdin.write(b"start\n"); process.stdin.flush()
            for process, destination in workers:
                out, error = process.communicate(timeout=60)
                if destination.exists():
                    receipt["console_workers"].append(json.loads(destination.read_text(encoding="utf-8")))
                assert process.returncode == 0, (process.returncode, out.decode("utf-8", "replace"), error.decode("utf-8", "replace"))
                assert destination.exists(), "console helper produced no resource receipt"
            observations = receipt["console_workers"]
            assert len(observations) == 3 and all(item["result"] == "passed" for item in observations)
            assert len({item["monitor_pid"] for item in observations}) == 3
            overlap = min(item["cancel_started_unix_s"] for item in observations) - max(item["monitor_started_unix_s"] for item in observations)
            receipt["three_monitor_overlap_s"] = overlap
            assert overlap > 0, "three monitors were not actually concurrent"
        receipt["result"] = "passed"
    except BaseException:
        receipt["failure"] = traceback.format_exc()
        raise
    finally:
        cleanup_errors = []
        for job in jobs:
            try:
                job.close()  # Includes each helper's monitor/provider descendants.
            except BaseException:
                cleanup_errors.append(traceback.format_exc())
        for process, _ in workers:
            try:
                if process.poll() is None:
                    process.kill()  # Only a still-gated helper if assignment failed.
                process.communicate(timeout=5)
            except BaseException:
                cleanup_errors.append(traceback.format_exc())
        if cleanup_errors:
            receipt["cleanup_errors"] = cleanup_errors
            receipt["result"] = "failed"
        write_receipt(output, receipt)
        if cleanup_errors and "failure" not in receipt:
            raise AssertionError("resource fixture cleanup failed; see observations")


def main():
    if os.name != "nt":
        raise SystemExit("native resource qualification requires Windows")
    if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
        raise SystemExit("usage: test_native_resources.py WTOP_EXE [--observations PATH]")
    exe = str(Path(sys.argv[1]).resolve())
    if len(sys.argv) == 4 and sys.argv[2] == "--console-worker":
        if sys.stdin.buffer.readline(16) != b"start\n":
            raise SystemExit("resource console worker did not receive its job gate")
        console_worker(exe, Path(sys.argv[3])); return
    if len(sys.argv) == 4 and sys.argv[2] == "--restricted-worker":
        restricted_worker(exe, Path(sys.argv[3])); return
    if len(sys.argv) == 4 and sys.argv[2] == "--observations":
        output = Path(sys.argv[3]).resolve()
    elif len(sys.argv) == 2:
        output = Path.cwd() / "native-resource-observations.json"
    else:
        raise SystemExit("unexpected native resource fixture arguments")
    output.unlink(missing_ok=True)
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "packaging"))
    from restricted_process import run_restricted
    outer_context = context()
    try:
        code = run_restricted([sys.executable, str(Path(__file__).resolve()), exe,
                              "--restricted-worker", str(output)], Path.cwd(), timeout=180)
    except BaseException:
        if not output.exists():
            write_receipt(output, {"schema": 1, "kind": "native-resource-observations", "result": "failed",
                "exe_sha256": hashlib.sha256(Path(exe).read_bytes()).hexdigest(),
                "outer_context": outer_context, "failure": traceback.format_exc(), "human_review": False})
        raise
    if not output.exists():
        write_receipt(output, {"schema": 1, "kind": "native-resource-observations", "result": "failed",
            "exe_sha256": hashlib.sha256(Path(exe).read_bytes()).hexdigest(),
            "outer_context": outer_context, "restricted_exit_code": code,
            "failure": "restricted child exited without a receipt", "human_review": False})
        raise AssertionError(f"restricted fixture exit {code} produced no receipt")
    receipt = json.loads(output.read_text(encoding="utf-8"))
    receipt["outer_context"] = outer_context
    receipt["restricted_exit_code"] = code
    write_receipt(output, receipt)
    if code or receipt["result"] != "passed":
        print(json.dumps(receipt, indent=2), file=sys.stderr)
        raise SystemExit(code or 1)
    print("Restricted medium-token queries and three concurrent private-console monitors passed; exact measurements are in " + str(output))
    print("No human desktop review or exhaustive leak/resource guarantee is inferred.")


if __name__ == "__main__":
    main()
