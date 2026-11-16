"""Drive a real isolated Win32 console; never inject input into the user's console.

The outer process keeps CTest's redirected streams. A helper owns a new hidden
console, launches Wtop there, injects native input records and reads the active
screen buffer. This checks behavior, not a reimplementation of the renderer.
"""

from __future__ import annotations

import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import traceback


class Coord(C.Structure):
    _fields_ = [("X", W.SHORT), ("Y", W.SHORT)]


class Rect(C.Structure):
    _fields_ = [("Left", W.SHORT), ("Top", W.SHORT),
                ("Right", W.SHORT), ("Bottom", W.SHORT)]


class ScreenInfo(C.Structure):
    _fields_ = [("size", Coord), ("cursor", Coord), ("attributes", W.WORD),
                ("window", Rect), ("maximum", Coord)]


class KeyChar(C.Union):
    _fields_ = [("UnicodeChar", W.WCHAR), ("AsciiChar", C.c_char)]


class KeyEvent(C.Structure):
    _fields_ = [("down", W.BOOL), ("repeat", W.WORD), ("vk", W.WORD),
                ("scan", W.WORD), ("char", KeyChar), ("modifiers", W.DWORD)]


class MouseEvent(C.Structure):
    _fields_ = [("position", Coord), ("buttons", W.DWORD),
                ("modifiers", W.DWORD), ("flags", W.DWORD)]


class EventUnion(C.Union):
    _fields_ = [("key", KeyEvent), ("mouse", MouseEvent), ("size", Coord),
                ("menu", W.UINT), ("focus", W.BOOL)]


class InputRecord(C.Structure):
    _fields_ = [("kind", W.WORD), ("event", EventUnion)]


class BasicJobLimits(C.Structure):
    _fields_ = [("process_time", C.c_longlong), ("job_time", C.c_longlong),
                ("flags", W.DWORD), ("minimum_working_set", C.c_size_t),
                ("maximum_working_set", C.c_size_t), ("active_processes", W.DWORD),
                ("affinity", C.c_size_t), ("priority", W.DWORD),
                ("scheduling", W.DWORD)]


class JobIoCounters(C.Structure):
    _fields_ = [(name, C.c_ulonglong) for name in
                ("reads", "writes", "others", "read_bytes", "write_bytes", "other_bytes")]


class ExtendedJobLimits(C.Structure):
    _fields_ = [("basic", BasicJobLimits), ("io", JobIoCounters),
                ("process_memory", C.c_size_t), ("job_memory", C.c_size_t),
                ("peak_process_memory", C.c_size_t), ("peak_job_memory", C.c_size_t)]


class FixtureJob:
    """Keep only this test's worker and descendants inside a kill-on-close job."""

    def __init__(self):
        assert C.sizeof(ExtendedJobLimits) == (144 if C.sizeof(C.c_void_p) == 8 else 112)
        self.k = C.WinDLL("kernel32", use_last_error=True)
        declarations = {
            "CreateJobObjectW": ([C.c_void_p, W.LPCWSTR], W.HANDLE),
            "SetInformationJobObject": ([W.HANDLE, C.c_int, C.c_void_p, W.DWORD], W.BOOL),
            "AssignProcessToJobObject": ([W.HANDLE, W.HANDLE], W.BOOL),
            "CloseHandle": ([W.HANDLE], W.BOOL),
        }
        for name, (args, result) in declarations.items():
            fn = getattr(self.k, name)
            fn.argtypes, fn.restype = args, result
        # NULL security attributes make the only job handle non-inheritable.
        self.handle = self.k.CreateJobObjectW(None, None)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        limits = ExtendedJobLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.k.SetInformationJobObject(self.handle, 9, C.byref(limits), C.sizeof(limits)):
            error = C.get_last_error()
            self.close()
            raise C.WinError(error)

    def assign(self, process):
        # Popen owns this stable process handle; do not reopen a possibly reused PID.
        if not self.k.AssignProcessToJobObject(self.handle, int(process._handle)):
            raise C.WinError(C.get_last_error())

    def close(self):
        if self.handle:
            handle, self.handle = self.handle, None
            if not self.k.CloseHandle(handle):
                raise C.WinError(C.get_last_error())


class Console:
    def __init__(self):
        assert C.sizeof(InputRecord) == 20, "incorrect INPUT_RECORD ABI"
        self.k = C.WinDLL("kernel32", use_last_error=True)
        declarations = {
            "CreateFileW": ([W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p,
                             W.DWORD, W.DWORD, W.HANDLE], W.HANDLE),
            "CloseHandle": ([W.HANDLE], W.BOOL),
            "SetHandleInformation": ([W.HANDLE, W.DWORD, W.DWORD], W.BOOL),
            "GetConsoleMode": ([W.HANDLE, C.POINTER(W.DWORD)], W.BOOL),
            "SetStdHandle": ([W.DWORD, W.HANDLE], W.BOOL),
            "GetConsoleScreenBufferInfo": ([W.HANDLE, C.POINTER(ScreenInfo)], W.BOOL),
            "SetConsoleScreenBufferSize": ([W.HANDLE, Coord], W.BOOL),
            "SetConsoleWindowInfo": ([W.HANDLE, W.BOOL, C.POINTER(Rect)], W.BOOL),
            "ReadConsoleOutputCharacterW": ([W.HANDLE, W.LPWSTR, W.DWORD,
                                             Coord, C.POINTER(W.DWORD)], W.BOOL),
            "ReadConsoleOutputAttribute": ([W.HANDLE, C.POINTER(W.WORD), W.DWORD,
                                            Coord, C.POINTER(W.DWORD)], W.BOOL),
            "WriteConsoleOutputCharacterW": ([W.HANDLE, W.LPCWSTR, W.DWORD,
                                              Coord, C.POINTER(W.DWORD)], W.BOOL),
            "WriteConsoleInputW": ([W.HANDLE, C.POINTER(InputRecord), W.DWORD,
                                     C.POINTER(W.DWORD)], W.BOOL),
            "FlushConsoleInputBuffer": ([W.HANDLE], W.BOOL),
        }
        for name, (args, result) in declarations.items():
            fn = getattr(self.k, name)
            fn.argtypes, fn.restype = args, result
        self.input = self.open("CONIN$")
        self.original_output = self.open("CONOUT$")
        self.original_mode = self.mode()
        for handle in (self.input, self.original_output):
            self.check(self.k.SetHandleInformation(handle, 1, 1))
        for identifier, handle in ((-10, self.input), (-11, self.original_output),
                                   (-12, self.original_output)):
            self.check(self.k.SetStdHandle(identifier & 0xFFFFFFFF, handle))
        info = self.info(self.original_output)
        self.width = min(120, info.maximum.X)
        self.height = min(32, info.maximum.Y)
        assert self.width >= 80 and self.height >= 25, "console too small for native fixture"
        small = Rect(0, 0, 1, 1)
        self.check(self.k.SetConsoleWindowInfo(self.original_output, True, C.byref(small)))
        self.check(self.k.SetConsoleScreenBufferSize(self.original_output, Coord(self.width, self.height)))
        target = Rect(0, 0, self.width - 1, self.height - 1)
        self.check(self.k.SetConsoleWindowInfo(self.original_output, True, C.byref(target)))
        self.marker = "WTOP_TEST_ORIGINAL_BUFFER"
        written = W.DWORD()
        self.check(self.k.WriteConsoleOutputCharacterW(self.original_output, self.marker,
                   len(self.marker), Coord(0, 0), C.byref(written)))
        self.process = None
        self.last_frame = None

    @staticmethod
    def check(result):
        if not result:
            raise C.WinError(C.get_last_error())
        return result

    def open(self, name):
        handle = self.k.CreateFileW(name, 0xC0000000, 3, None, 3, 0, None)
        if handle == C.c_void_p(-1).value:
            raise C.WinError(C.get_last_error())
        return handle

    def mode(self):
        value = W.DWORD()
        self.check(self.k.GetConsoleMode(self.input, C.byref(value)))
        return value.value

    def info(self, handle):
        info = ScreenInfo()
        self.check(self.k.GetConsoleScreenBufferInfo(handle, C.byref(info)))
        return info

    def frame(self):
        handle = self.open("CONOUT$")
        try:
            info = self.info(handle)
            width = info.window.Right - info.window.Left + 1
            height = info.window.Bottom - info.window.Top + 1
            rows, attrs = [], []
            for y in range(height):
                coord = Coord(info.window.Left, info.window.Top + y)
                chars = C.create_unicode_buffer(width + 1)
                colors = (W.WORD * width)()
                count = W.DWORD()
                self.check(self.k.ReadConsoleOutputCharacterW(handle, chars, width, coord, C.byref(count)))
                rows.append(chars[:width])
                self.check(self.k.ReadConsoleOutputAttribute(handle, colors, width, coord, C.byref(count)))
                attrs.append(list(colors))
            self.last_frame = {"width": width, "height": height,
                               "left": info.window.Left, "top": info.window.Top,
                               "rows": rows, "attributes": attrs}
            return self.last_frame
        finally:
            self.k.CloseHandle(handle)

    def wait(self, predicate, description, timeout=6):
        end = time.monotonic() + timeout
        matches = 0
        while time.monotonic() < end:
            if self.process is not None and self.process.poll() is not None:
                raise AssertionError(f"Wtop exited {self.process.returncode} while waiting for {description}")
            frame = self.frame()
            if predicate(frame):
                matches += 1
                # Rows are read separately while Wtop draws; avoid accepting a
                # single transient cleared row as evidence of a state change.
                if matches == 2:
                    return frame
            else:
                matches = 0
            time.sleep(0.03)
        raise AssertionError(f"timeout waiting for {description}:\n" + "\n".join(self.last_frame["rows"]))

    def records(self, records):
        data = (InputRecord * len(records))(*records)
        count = W.DWORD()
        self.check(self.k.WriteConsoleInputW(self.input, data, len(records), C.byref(count)))
        assert count.value == len(records)

    def key(self, vk, char="\0", modifiers=0):
        events = []
        for down in (True, False):
            event = InputRecord()
            event.kind = 1
            event.event.key.down = down
            event.event.key.repeat = 1
            event.event.key.vk = vk
            event.event.key.char.UnicodeChar = char
            event.event.key.modifiers = modifiers
            events.append(event)
        self.records(events)

    def type(self, text):
        for char in text:
            self.key(ord(char.upper()) if char.isascii() and char.isalpha() else ord(char), char)

    def click(self, x, y):
        frame = self.frame()
        events = []
        for buttons in (1, 0):
            event = InputRecord()
            event.kind = 2
            event.event.mouse.position = Coord(frame["left"] + x, frame["top"] + y)
            event.event.mouse.buttons = buttons
            events.append(event)
        self.records(events)

    def start(self, exe, config, pids, *options):
        self.check(self.k.FlushConsoleInputBuffer(self.input))
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESTDHANDLES
        startup.hStdInput = self.input
        startup.hStdOutput = startup.hStdError = self.original_output
        self.process = subprocess.Popen(
            [exe, "--tui", "--config", str(config), "--interval", "250",
             "-p", ",".join(map(str, pids)), *options],
            startupinfo=startup, close_fds=False,
        )
        return self.wait(lambda f: "Main" in "\n".join(f["rows"])
                         and "I/O" in "\n".join(f["rows"]), "initial tabs")

    def stop(self):
        self.key(0x79)  # F10 is a common quit binding in all profiles.
        assert self.process.wait(timeout=5) == 0
        self.process = None
        assert self.mode() == self.original_mode, "caller console input mode was not restored"
        assert self.frame()["rows"][0].startswith(self.marker), "original screen buffer was not restored"

    def close(self):
        try:
            if self.process is not None and self.process.poll() is None:
                self.process.kill()  # Only this test's own monitor process.
                self.process.wait(timeout=5)
        finally:
            try:
                self.k.CloseHandle(self.input)
            finally:
                self.k.CloseHandle(self.original_output)


def text_contains(frame, text):
    return text.lower() in "\n".join(frame["rows"]).lower()


def overlay_contains(frame, title):
    return title.lower() in frame["rows"][0].lower()


def has_bar(frame):
    return "Help" in frame["rows"][-1] and "Quit" in frame["rows"][-1]


def is_main(frame):
    return frame["rows"][0].startswith("Wtop ")


def editor_contains(frame, text):
    return any(row.lstrip().startswith(text) for row in frame["rows"][-3:])


def tab_position(frame, name):
    for y, row in enumerate(frame["rows"]):
        if " Main " in row and " I/O " in row:
            return row.index(" " + name + " ") + 1, y
    return None


def tab_selected(frame, name):
    position = tab_position(frame, name)
    return position is not None and frame["attributes"][position[1]][position[0]] == 0x30


def visible_pids(frame, pids, selected_only=False):
    found = set()
    for y, row in enumerate(frame["rows"]):
        trimmed = row.lstrip()
        token = trimmed.split(None, 1)[0] if trimmed else ""
        if token.isdigit() and int(token) in pids:
            x = len(row) - len(trimmed)
            if not selected_only or frame["attributes"][y][x] == 0x30:
                found.add(int(token))
    return found


def worker(exe, output):
    console = None
    fixtures = []
    observations = {"checks": [], "human_visual_acceptance": False}
    try:
        console = Console()
        with tempfile.TemporaryDirectory(prefix="wtop-native-ui-") as temp:
            config = Path(temp) / "settings.conf"
            created = subprocess.run([exe, "--no-config", "--write-config", str(config)],
                                     capture_output=True, timeout=5)
            assert created.returncode == 0, created.stderr.decode("utf-8", "replace")
            with config.open("a", encoding="utf-8") as stream:
                stream.write("\n# retained native fixture comment\nFutureOption keep-me\n")
            fixtures = [subprocess.Popen([sys.executable, "-c", "import time; time.sleep(90)"],
                        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL) for _ in range(3)]
            pids = [p.pid for p in fixtures]
            console.start(exe, config, pids)
            frame = console.wait(has_bar, "default last-row function bar")
            observations["default_frame"] = frame
            assert console.mode() & 0x10, "mouse input was not enabled on this console"
            assert not console.mode() & 0x40, "QuickEdit may freeze interactive input"
            observations["checks"].append("default htop function bar and native mouse mode")

            console.click(frame["width"] * 15 // 100, frame["height"] - 1)
            console.wait(lambda f: overlay_contains(f, "Setup"), "mouse-opened setup")
            console.key(27, "\x1b")
            console.wait(lambda f: has_bar(f) and is_main(f), "setup close")
            console.key(0x70)  # F1
            console.wait(lambda f: overlay_contains(f, "Help"), "keyboard help")
            console.key(27, "\x1b")
            console.wait(lambda f: has_bar(f) and is_main(f), "help close")
            observations["checks"].append("mouse function action and keyboard overlays")

            console.key(0x78)  # F9
            console.wait(lambda f: overlay_contains(f, "Terminate"), "termination confirmation")
            assert all(p.poll() is None for p in fixtures), "opening confirmation mutated a process"
            console.key(27, "\x1b")
            console.wait(lambda f: has_bar(f) and is_main(f), "termination cancellation")
            assert all(p.poll() is None for p in fixtures)
            observations["checks"].append("F9 confirmation and cancellation are non-mutating")

            console.type(":unfinished-fixture-command")
            frame = console.wait(lambda f: editor_contains(f, ":unfinished-fixture-command"),
                                 "unfinished command editor")
            console.click(frame["width"] * 5 // 100, frame["height"] - 1)
            console.wait(lambda f: overlay_contains(f, "Help"), "mouse help cancels command editor")
            console.key(27, "\x1b")
            console.wait(lambda f: is_main(f) and has_bar(f) and not editor_contains(f, ":"),
                         "help closes without returning to unfinished command")

            console.key(0x73)  # F4
            console.type("wtop_fixture_unmatched_filter")
            frame = console.wait(lambda f: editor_contains(f, "\\wtop_fixture_unmatched_filter")
                                 and tab_selected(f, "Main"), "live filter on Main tab")
            position = tab_position(frame, "I/O")
            assert position is not None
            console.click(*position)  # Tab navigation is ignored until the editor closes.
            console.type("_continued")  # Positive acknowledgement after the queued click.
            console.wait(lambda f: editor_contains(f, "\\wtop_fixture_unmatched_filter_continued")
                         and tab_selected(f, "Main"), "tab click keeps the filter in its original tab")
            console.key(27, "\x1b")
            frame = console.wait(lambda f: is_main(f) and tab_selected(f, "Main")
                                 and not editor_contains(f, "\\")
                                 and visible_pids(f, pids) == set(pids), "cancel restores Main's filter")
            console.click(*tab_position(frame, "I/O"))
            frame = console.wait(lambda f: tab_selected(f, "I/O") and visible_pids(f, pids) == set(pids),
                                 "I/O tab remains unfiltered after editor cancellation")
            console.click(*tab_position(frame, "Main"))
            console.wait(lambda f: tab_selected(f, "Main") and visible_pids(f, pids) == set(pids),
                         "return to unfiltered Main tab")
            observations["checks"].append("mouse actions cancel commands and preserve filter ownership across tabs")

            console.type("B")
            console.wait(lambda f: is_main(f) and not has_bar(f), "hidden function bar")
            console.key(0x71)  # F2
            console.wait(lambda f: overlay_contains(f, "Setup"), "setup before explicit save")
            before = config.read_bytes()
            console.type("s")
            # Windows CRT readers do not share file deletion. Polling read_bytes
            # during the save can block the atomic replacement being tested.
            console.wait(lambda f: text_contains(f, "Saved configuration:"), "explicit save acknowledgement")
            saved = config.read_text(encoding="utf-8-sig")
            assert config.read_bytes() != before, "explicit save did not update settings"
            assert "# retained native fixture comment" in saved and "FutureOption keep-me" in saved
            assert 'Tab0.Filter ""' in saved and 'Tab1.Filter ""' in saved
            console.key(27, "\x1b")
            console.wait(is_main, "setup close after save")
            console.stop()
            console.start(exe, config, pids)
            assert not has_bar(console.frame()), "hidden bar was not restored from saved settings"
            console.stop()
            observations["checks"].append("explicit save, unknown/comment preservation, reload and console restoration")

            console.start(exe, config, pids, "--status-bar")
            externally_edited = config.read_bytes() + b"\n# concurrent external edit\n"
            config.write_bytes(externally_edited)
            console.type("B")
            console.wait(lambda f: is_main(f) and not has_bar(f), "unsaved preference after external edit")
            console.key(0x71)
            console.wait(lambda f: overlay_contains(f, "Setup"), "setup before stale save")
            console.type("s")
            console.wait(lambda f: text_contains(f, "changed since load"), "stale-file refusal")
            assert config.read_bytes() == externally_edited, "stale save overwrote external changes"
            console.key(27, "\x1b")
            console.wait(is_main, "setup close after refused save")
            console.stop()
            assert config.read_bytes() == externally_edited, "normal exit wrote configuration"
            observations["checks"].append("external edits survive refused save and normal exit")

            console.start(exe, config, pids, "--keymap", "vi", "--status-bar", "-s", "ID")
            console.wait(has_bar, "vi common bar")
            ordered_pids = sorted(pids, reverse=True)
            console.key(0x24)  # Home: first row in stable descending PID order.
            console.wait(lambda f: visible_pids(f, pids, True) == {ordered_pids[0]}, "vi first process selection")
            console.type("j")
            console.wait(lambda f: visible_pids(f, pids, True) == {ordered_pids[1]}, "vi j selects next process")
            console.type("k")
            console.wait(lambda f: is_main(f) and visible_pids(f, pids, True) == {ordered_pids[0]},
                         "vi k selects previous process without opening termination")
            console.key(0x70)
            console.wait(lambda f: overlay_contains(f, "Help"), "vi help input acknowledgement")
            console.key(27, "\x1b")
            console.wait(lambda f: is_main(f) and has_bar(f), "vi help closes normally")
            assert all(p.poll() is None for p in fixtures)
            console.stop()
            observations["checks"].append("vi navigation does not invoke htop k action")

            console.start(exe, config, pids, "--keymap", "emacs", "--status-bar", "--no-mouse")
            console.wait(has_bar, "Emacs common bar")
            assert not console.mode() & 0x10, "--no-mouse did not disable native mouse input"
            console.key(ord("S"), "\x13", 8)
            console.wait(lambda f: editor_contains(f, "/"),
                         "Emacs C-s search")
            console.key(ord("G"), "\x07", 8)
            console.wait(lambda f: is_main(f) and has_bar(f) and not editor_contains(f, "/"),
                         "Emacs C-g cancel")
            console.stop()
            observations["checks"].append("Emacs search/cancel and keyboard-only fallback")
        observations["result"] = "passed"
    except BaseException:
        observations["result"] = "failed"
        observations["error"] = traceback.format_exc()
        if console is not None:
            observations["last_frame"] = console.last_frame
        raise
    finally:
        already_failed = observations.get("result") == "failed"
        cleanup_errors = []
        try:
            for child in fixtures:
                try:
                    if child.poll() is None:
                        child.terminate()
                    child.wait(timeout=5)
                except Exception:
                    cleanup_errors.append(traceback.format_exc())
        finally:
            try:
                if console is not None:
                    console.close()
            except Exception:
                cleanup_errors.append(traceback.format_exc())
            finally:
                if cleanup_errors:
                    observations["result"] = "failed"
                    observations["cleanup_errors"] = cleanup_errors
                output.write_text(json.dumps(observations, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if cleanup_errors and not already_failed:
            raise AssertionError("native fixture cleanup failed; see observations")


def main():
    if os.name != "nt":
        raise SystemExit("native TUI interaction requires Windows")
    if len(sys.argv) < 2 or not Path(sys.argv[1]).is_file():
        raise SystemExit("usage: test_native_tui.py PATH_TO_WTOP_EXE")
    exe = str(Path(sys.argv[1]).resolve())
    if len(sys.argv) >= 4 and sys.argv[2] == "--console-worker":
        # Do not create a console fixture or any descendants until the parent
        # has assigned us to its non-inherited, kill-on-close job.
        if sys.stdin.buffer.readline(16) != b"start\n":
            raise SystemExit("native console worker did not receive its job gate")
        worker(exe, Path(sys.argv[3]))
        return
    output = Path.cwd() / "native-tui-observations.json"
    output.unlink(missing_ok=True)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    job = FixtureJob()
    process = None
    try:
        process = subprocess.Popen(
            [sys.executable, str(Path(__file__).resolve()), exe, "--console-worker", str(output)],
            creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        job.assign(process)
        try:
            _, errors = process.communicate(b"start\n", timeout=80)
        except subprocess.TimeoutExpired:
            job.close()  # Kill the worker AND its monitor/fixture descendants.
            _, errors = process.communicate(timeout=5)
            print(errors.decode("utf-8", "replace"), file=sys.stderr)
            raise AssertionError("native TUI worker exceeded 80 seconds; owned process tree terminated")
    finally:
        try:
            job.close()
        finally:
            if process is not None and process.poll() is None:
                # Assignment failure leaves only the still-gated worker.
                process.kill()
                process.communicate(timeout=5)
    if process.returncode:
        print(errors.decode("utf-8", "replace"), file=sys.stderr)
        if output.exists():
            print(output.read_text(encoding="utf-8"), file=sys.stderr)
        raise SystemExit(process.returncode)
    data = json.loads(output.read_text(encoding="utf-8"))
    print("native console: " + "; ".join(data["checks"]))
    print("No human visual or historical OS acceptance is inferred.")


if __name__ == "__main__":
    main()
