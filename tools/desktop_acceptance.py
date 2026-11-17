#!/usr/bin/env python3
"""Collect exact-payload desktop evidence; human answers are never synthesized.

Development/test tooling only. The Wtop executable needs no Python runtime.
All process mutations are confined to child processes owned by this script.
"""
from __future__ import annotations
import argparse
import ctypes
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import sys
import tempfile

CHECKS = (
    "40x25, 80x25 and 80x50 readable; tiny-window fallback and resize recovery",
    "htop help/search/filter/tree/sort, mouse and keyboard-only fallback",
    "vi and Emacs navigation/search/cancel do not trigger unintended actions",
    "readonly blocks kill, priority, exec and settings save",
    "explicit settings save/reload works only in the temporary test directory",
    "F9 requires confirmation and affects only the disposable selected process",
    "normal quit and Ctrl+C restore the original terminal without a leftover worker",
)

def supported_host(host: dict) -> bool:
    return (host.get("system") == "Windows" and host.get("product_type") == 1
            and host.get("major") == 10 and type(host.get("build")) is int
            and host["build"] >= 22000 and host.get("native_architecture") == "x64"
            and host.get("elevated") is False)

def host_identity() -> dict:
    if os.name != "nt":
        return {"system": platform.system(), "elevated": None}
    version = sys.getwindowsversion()
    machine = ctypes.c_ushort()
    process_machine = ctypes.c_ushort()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    query = kernel.IsWow64Process2
    query.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ushort), ctypes.POINTER(ctypes.c_ushort)]
    query.restype = ctypes.c_int
    if not query(kernel.GetCurrentProcess(), ctypes.byref(process_machine), ctypes.byref(machine)):
        raise ctypes.WinError(ctypes.get_last_error())
    return {"system": "Windows", "major": version.major, "minor": version.minor,
            "build": version.build, "product_type": version.product_type,
            "native_architecture": {0x8664: "x64", 0xAA64: "arm64", 0x14C: "x86"}.get(machine.value, "unknown"),
            "elevated": bool(ctypes.windll.shell32.IsUserAnAdmin())}

def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()

def digest(path: Path) -> str:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError("invalid or oversized executable")
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run_smoke(exe: Path, version: str) -> None:
    result = subprocess.run([str(exe), "--no-config", "--version"], capture_output=True, timeout=10)
    if result.returncode or version not in result.stdout.decode("utf-8", "strict"):
        raise ValueError("embedded version smoke check failed")
    for args in (["--capabilities", "--format", "json"], ["query", "system", "--format", "json", "--interval", "100"]):
        result = subprocess.run([str(exe), "--no-config", "--readonly", *args], capture_output=True, timeout=20)
        if result.returncode != 0:
            raise ValueError("native read-only smoke check failed; inspect locally, not by uploading raw data")
        data = json.loads(result.stdout.decode("utf-8", "strict"))
        if not isinstance(data, dict):
            raise ValueError("invalid smoke output")
        if args[0] == "query" and (data.get("schema") != 2 or data.get("version") != version):
            raise ValueError("snapshot contract/version mismatch")

def interactive_run(arguments):
    # A custom parent handler (not SIG_IGN) lets the child exercise Ctrl+C without
    # aborting this evidence collector or passing an inherited ignore flag.
    original = signal.signal(signal.SIGINT, lambda _signal, _frame: None)
    try:
        result = subprocess.run(arguments, timeout=600)
        if result.returncode not in (0, 130):
            raise ValueError("interactive candidate did not exit normally or by cooperative cancellation")
    finally:
        signal.signal(signal.SIGINT, original)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path, help="directory containing wtop.exe and payload.json")
    parser.add_argument("--output", type=Path, required=True, help="new JSON receipt; never overwritten")
    parser.add_argument("--terminal", required=True, help="terminal name and version, not username/hostname")
    args = parser.parse_args(argv)
    receipt = {"schema": "wtop.desktop-acceptance/1", "started_at": utc_now(), "result": "blocked"}
    child = None
    try:
        host = host_identity()
        receipt["host"] = host
        if not supported_host(host):
            raise ValueError("requires Windows 11 x64 workstation under an ordinary (not elevated) user token")
        if not sys.stdin.isatty() or not sys.stdout.isatty():
            raise ValueError("human review requires an interactive terminal; no assume-yes mode exists")
        if not args.terminal.strip() or len(args.terminal) > 128 or any(ord(c) < 32 for c in args.terminal):
            raise ValueError("invalid terminal identity")
        candidate = args.candidate.resolve()
        exe = candidate / "wtop.exe"
        payload = json.loads((candidate / "payload.json").read_text(encoding="utf-8"))
        version = payload["version"]
        if not re.fullmatch(r"0\.4\.0-beta\.[1-9][0-9]*", version) or payload["profile"] != "win-nt-x64":
            raise ValueError("not the declared beta profile/version")
        sha = digest(exe)
        if sha != payload["executable"]["sha256"] or payload["source"]["dirty"] is not False:
            raise ValueError("payload hash or source cleanliness mismatch")
        receipt.update(version=version, profile=payload["profile"], source_revision=payload["source"]["revision"],
                       executable_sha256=sha, terminal=args.terminal)
        run_smoke(exe, version)
        receipt["automated_smoke"] = "passed"
        print("Review the exact candidate. Only disposable processes created here are selected.")
        print("Use the listed checks; do not type a different PID or launch external commands.")
        with tempfile.TemporaryDirectory(prefix="wtop-beta-review-") as temporary:
            config = Path(temporary) / "review.conf"
            subprocess.run([str(exe), "--no-config", "--write-config", str(config)], check=True, timeout=10,
                           stdout=subprocess.DEVNULL)
            for keymap in ("htop", "vi", "emacs"):
                child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(900)"],
                                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                print("Review", keymap, "with disposable PID", child.pid, "; F10 quits. Try required resize sizes.")
                try:
                    interactive_run([str(exe), "--config", str(config), "--keymap", keymap,
                                     "--pid", str(child.pid)])
                finally:
                    if child.poll() is None:
                        child.terminate()
                    child.wait(timeout=10)
                    child = None
            interactive_run([str(exe), "--config", str(config), "--readonly", "--keymap", "htop",
                             "--pid", str(os.getpid())])
            answers = []
            for check in CHECKS:
                answer = input(check + " [type PASS only after actually checking; anything else blocks]: ")
                answers.append({"check": check, "passed": answer == "PASS"})
            receipt["human_review"] = {"method": "interactive-attestation", "checks": answers}
            if not all(a["passed"] for a in answers):
                raise ValueError("one or more human checks were not confirmed")
        if digest(exe) != sha:
            raise ValueError("executable changed during review")
        receipt["result"] = "passed"
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        receipt["reason"] = str(error)
        print("Desktop acceptance blocked:", error, file=sys.stderr)
    finally:
        if child is not None and child.poll() is None:
            child.terminate(); child.wait(timeout=10)
        receipt["ended_at"] = utc_now()
    # Explicit path, exclusive creation. No automatic upload or user-path capture.
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(receipt, stream, indent=2); stream.write("\n")
    return 0 if receipt["result"] == "passed" else 2

if __name__ == "__main__":
    raise SystemExit(main())
