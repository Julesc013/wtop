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
import re
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


class CharInfo(C.Structure):
    _fields_ = [("char", KeyChar), ("attributes", W.WORD)]


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
        assert C.sizeof(CharInfo) == 4, "incorrect CHAR_INFO ABI"
        self.k = C.WinDLL("kernel32", use_last_error=True)
        declarations = {
            "CreateFileW": ([W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p,
                             W.DWORD, W.DWORD, W.HANDLE], W.HANDLE),
            "CreateConsoleScreenBuffer": ([W.DWORD, W.DWORD, C.c_void_p,
                                            W.DWORD, C.c_void_p], W.HANDLE),
            "CloseHandle": ([W.HANDLE], W.BOOL),
            "SetHandleInformation": ([W.HANDLE, W.DWORD, W.DWORD], W.BOOL),
            "GetConsoleMode": ([W.HANDLE, C.POINTER(W.DWORD)], W.BOOL),
            "SetConsoleMode": ([W.HANDLE, W.DWORD], W.BOOL),
            "SetStdHandle": ([W.DWORD, W.HANDLE], W.BOOL),
            "GetConsoleScreenBufferInfo": ([W.HANDLE, C.POINTER(ScreenInfo)], W.BOOL),
            "SetConsoleScreenBufferSize": ([W.HANDLE, Coord], W.BOOL),
            "SetConsoleWindowInfo": ([W.HANDLE, W.BOOL, C.POINTER(Rect)], W.BOOL),
            "SetConsoleCursorPosition": ([W.HANDLE, Coord], W.BOOL),
            "ReadConsoleOutputCharacterW": ([W.HANDLE, W.LPWSTR, W.DWORD,
                                             Coord, C.POINTER(W.DWORD)], W.BOOL),
            "ReadConsoleOutputW": ([W.HANDLE, C.POINTER(CharInfo), Coord,
                                     Coord, C.POINTER(Rect)], W.BOOL),
            "ReadConsoleOutputAttribute": ([W.HANDLE, C.POINTER(W.WORD), W.DWORD,
                                            Coord, C.POINTER(W.DWORD)], W.BOOL),
            "WriteConsoleOutputCharacterW": ([W.HANDLE, W.LPCWSTR, W.DWORD,
                                              Coord, C.POINTER(W.DWORD)], W.BOOL),
            "WriteConsoleW": ([W.HANDLE, W.LPCWSTR, W.DWORD,
                                C.POINTER(W.DWORD), C.c_void_p], W.BOOL),
            "WriteConsoleInputW": ([W.HANDLE, C.POINTER(InputRecord), W.DWORD,
                                     C.POINTER(W.DWORD)], W.BOOL),
            "FlushConsoleInputBuffer": ([W.HANDLE], W.BOOL),
            "SetConsoleCtrlHandler": ([C.c_void_p, W.BOOL], W.BOOL),
            "GenerateConsoleCtrlEvent": ([W.DWORD, W.DWORD], W.BOOL),
        }
        for name, (args, result) in declarations.items():
            fn = getattr(self.k, name)
            fn.argtypes, fn.restype = args, result
        # This helper owns the private test console. Its sleeper fixtures inherit
        # ignore-Ctrl-C; Wtop must explicitly opt itself back in on startup.
        self.check(self.k.SetConsoleCtrlHandler(None, True))
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
            # Read the cell grid, not a stream of UTF-16 characters. The latter
            # expands combining clusters and can return fewer units than the
            # cell width, shifting footer coordinates or introducing NULs.
            # ReadConsoleOutputW is intentionally a legacy cell projection;
            # it does not prove the terminal's complete glyph/font appearance.
            cells = (CharInfo * (width * height))()
            region = Rect(info.window.Left, info.window.Top,
                          info.window.Right, info.window.Bottom)
            self.check(self.k.ReadConsoleOutputW(handle, cells, Coord(width, height),
                                                Coord(0, 0), C.byref(region)))
            assert (region.Left, region.Top, region.Right, region.Bottom) == (
                info.window.Left, info.window.Top, info.window.Right, info.window.Bottom)
            rows, attrs, units = [], [], []
            for y in range(height):
                line = cells[y * width:(y + 1) * width]
                units.append([ord(cell.char.UnicodeChar) for cell in line])
                attrs.append([cell.attributes for cell in line])
                # Preserve one Python character per terminal cell. A trailing
                # wide-cell marker is spacing, not a duplicated CJK glyph.
                rows.append("".join(" " if cell.attributes & 0x200 else
                    ("\ufffd" if 0xD800 <= ord(cell.char.UnicodeChar) <= 0xDFFF
                     else cell.char.UnicodeChar) for cell in line))
            self.last_frame = {"width": width, "height": height,
                               "left": info.window.Left, "top": info.window.Top,
                               "rows": rows, "attributes": attrs, "cell_units": units}
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

    def cluster_widths(self):
        """Independent host receipt, separate from Wtop's projected frame."""
        handle = self.k.CreateConsoleScreenBuffer(0xC0000000, 3, None, 1, None)
        if handle == C.c_void_p(-1).value:
            return {"qualified": False, "error": C.get_last_error(), "widths": {}}
        samples = {"combining": ("e\u0301", 1), "CJK": ("\u4e2d", 2),
                   "supplementary": ("\U0001f600", 2),
                   "ZWJ": ("\U0001f469\u200d\U0001f4bb", 2),
                   "regional": ("\U0001f1e6\U0001f1fa", 2),
                   "keycap": ("1\ufe0f\u20e3", 2),
                   "leading_mark": ("\u25cc\u0301", 1)}
        result = {"qualified": True, "widths": {}}
        try:
            mode = W.DWORD()
            if not self.k.GetConsoleMode(handle, C.byref(mode)) or not self.k.SetConsoleMode(handle, (mode.value | 5) & ~2):
                return {"qualified": False, "error": C.get_last_error(), "widths": {}}
            for name, (sample, expected) in samples.items():
                self.check(self.k.SetConsoleCursorPosition(handle, Coord(0, 0)))
                # The trailing ASCII marker makes this an independent advance
                # measurement rather than reading Wtop's capability variable.
                text = sample + "Z"
                count = len(text.encode("utf-16-le")) // 2
                written = W.DWORD()
                self.check(self.k.WriteConsoleW(handle, text, count, C.byref(written), None))
                assert written.value == count
                info = self.info(handle)
                observed = info.cursor.X - 1
                result["widths"][name] = {"observed": observed, "expected": expected}
                if observed != expected or info.cursor.Y != 0:
                    result["qualified"] = False
            return result
        finally:
            self.check(self.k.CloseHandle(handle))

    def records(self, records):
        data = (InputRecord * len(records))(*records)
        count = W.DWORD()
        self.check(self.k.WriteConsoleInputW(self.input, data, len(records), C.byref(count)))
        assert count.value == len(records), (
            f"console accepted {count.value}/{len(records)} records in input mode {self.mode():#x}")

    @staticmethod
    def key_records(vk, char="\0", modifiers=0, repeat=1):
        events = []
        for down in (True, False):
            event = InputRecord()
            event.kind = 1
            event.event.key.down = down
            event.event.key.repeat = repeat
            event.event.key.vk = vk
            event.event.key.char.UnicodeChar = char
            event.event.key.modifiers = modifiers
            events.append(event)
        return events

    def key(self, vk, char="\0", modifiers=0):
        self.records(self.key_records(vk, char, modifiers))

    def type(self, text):
        events = []
        # INPUT_RECORD contains one UTF-16 code unit. A supplementary scalar is
        # two packets; isolated surrogates are intentional validation fixtures.
        encoded = text.encode("utf-16-le", "surrogatepass")
        for index in range(0, len(encoded), 2):
            unit = int.from_bytes(encoded[index:index + 2], "little")
            char = chr(unit)
            virtual = ord(char.upper()) if char.isascii() and char.isalpha() else (unit if unit < 128 else 0xE7)
            events.extend(self.key_records(virtual, char))
        self.records(events)

    def resize(self, width, height):
        handle = self.open("CONOUT$")
        try:
            small = Rect(0, 0, 1, 1)
            self.check(self.k.SetConsoleWindowInfo(handle, True, C.byref(small)))
            self.check(self.k.SetConsoleScreenBufferSize(handle, Coord(width, height)))
            target = Rect(0, 0, width - 1, height - 1)
            self.check(self.k.SetConsoleWindowInfo(handle, True, C.byref(target)))
        finally:
            self.k.CloseHandle(handle)
        return self.wait(lambda f: f["width"] == width and f["height"] == height,
                         f"console resized to {width}x{height}")

    def click(self, x, y, *, frame=None):
        if frame is None:
            frame = self.frame()
        events = []
        for buttons in (1, 0):
            event = InputRecord()
            event.kind = 2
            event.event.mouse.position = Coord(frame["left"] + x, frame["top"] + y)
            event.event.mouse.buttons = buttons
            events.append(event)
        self.records(events)

    def start(self, exe, config, pids, *options, interval=250):
        self.check(self.k.FlushConsoleInputBuffer(self.input))
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESTDHANDLES
        startup.hStdInput = self.input
        startup.hStdOutput = startup.hStdError = self.original_output
        self.process = subprocess.Popen(
            [exe, "--tui", "--config", str(config), "--interval", str(interval),
             "-p", ",".join(map(str, pids)), *options],
            startupinfo=startup, close_fds=False,
        )
        return self.wait(lambda f: "Main" in "\n".join(f["rows"])
                         and "I/O" in "\n".join(f["rows"]), "initial tabs")

    def stop(self, send_key=True):
        if send_key:
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
                try:
                    self.k.CloseHandle(self.original_output)
                finally:
                    self.check(self.k.SetConsoleCtrlHandler(None, False))


def text_contains(frame, text):
    return text.lower() in "\n".join(frame["rows"]).lower()


def overlay_contains(frame, title):
    return title.lower() in frame["rows"][0].lower()


def has_bar(frame):
    return "Help" in frame["rows"][-1] and "Quit" in frame["rows"][-1]


def is_main(frame):
    return frame["rows"][0].startswith("Wtop ")


def editor_contains(frame, text):
    return any(text in row for row in frame["rows"][-3:])


def tab_position(frame, name):
    for y, row in enumerate(frame["rows"]):
        if " Main " in row and " I/O " in row:
            return row.index(" " + name + " ") + 1, y
    return None


def tab_selected(frame, name):
    position = tab_position(frame, name)
    return position is not None and frame["attributes"][position[1]][position[0]] == 0x30


def visible_pids(frame, pids, selected_only=False, highlighted_only=False):
    found = set()
    for y, row in enumerate(frame["rows"]):
        trimmed = row.lstrip()
        token = trimmed.split(None, 1)[0] if trimmed else ""
        if token.isdigit() and int(token) in pids:
            x = len(row) - len(trimmed)
            if (not selected_only or frame["attributes"][y][x] == 0x30) and (not highlighted_only or frame["attributes"][y][x] == 0x8f):
                found.add(int(token))
    return found


def function_slot(frame, number):
    row = frame["rows"][-1]
    matches = list(re.finditer(r"F(10|[1-9])(?=[A-Za-z?+\\-])", row))
    for index, match in enumerate(matches):
        if int(match.group(1)) == number:
            start = match.start()
            end = matches[index + 1].start() if index + 1 < len(matches) else frame["width"]
            # Standard bars reserve ten fixed spans even when a contextual
            # binding is absent. F7 must not absorb blank F8/F9 cells merely
            # because the next visible label is F10. Shared-editor/compact
            # labels retain their observed adjacent bounds.
            if start == (frame["width"] * (number - 1)) // 10:
                end = min(end, (frame["width"] * number) // 10)
            return start, end, row[start:end], frame["attributes"][-1][start:end]
    return 0, 0, "", []


def assert_function(frame, number, label, enabled=True):
    _start, _end, shown, colors = function_slot(frame, number)
    prefix = "F" + str(number)
    assert shown.startswith(prefix), (number, shown)
    assert shown[len(prefix):].strip().startswith(label), (number, shown, label)
    if enabled:
        assert any(color != 8 for color in colors), (number, shown, colors)
    else:
        assert all(color == 8 for color in colors), (number, shown, colors)


def function_ready(frame, number, label, enabled=True):
    _start, _end, shown, colors = function_slot(frame, number)
    prefix = "F" + str(number)
    if not shown.startswith(prefix) or not shown[len(prefix):].strip().startswith(label):
        return False
    return any(color != 8 for color in colors) if enabled else bool(colors) and all(color == 8 for color in colors)


def footer_slot_contract():
    row = list(" " * 120)
    for start, label in ((0, "F1Help"), (72, "F7Affinity"), (108, "F10Quit")):
        row[start:start + len(label)] = label
    colors = [0] * 120
    colors[72:84] = [8] * 12
    frame = {"width": 120, "rows": ["".join(row)], "attributes": [colors]}
    assert function_slot(frame, 7)[:2] == (72, 84)
    assert function_ready(frame, 7, "Affinity", False)
    colors[80] = 7
    assert not function_ready(frame, 7, "Affinity", False), "disabled oracle ignored a wrongly enabled cell"


def click_function(console, number, label, enabled=True):
    # The editor/overlay can become readable before its footer is painted.
    # Observe the requested label/state and the last button before clicking;
    # do not replace that acknowledged frame with an unchecked repaint read.
    # Waiting only samples output. The action itself is injected exactly once.
    frame = console.wait(lambda f: function_ready(f, number, label, enabled)
                         and function_ready(f, 10, "Quit"),
                         f"F{number} {label} button ready")
    assert_function(frame, number, label, enabled)
    start, end, _shown, _colors = function_slot(frame, number)
    console.click(start + (end - start) // 2, frame["height"] - 1, frame=frame)


def setup_rows(frame):
    values = []
    for row in frame["rows"][3:-2]:
        match = re.match(r"\s*\[[x ]\] (.*?)(?: \(active\))?\s*$", row)
        if match:
            values.append(match.group(1).rstrip())
    return values


def config_values(path):
    values = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"([^\s#]+)\s+(.+)$", line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def config_tabs(path):
    values = config_values(path)
    assert values["ConfigVersion"] == "2"
    return [{"id": int(values[f"Tab{i}.Id"]), "name": json.loads(values[f"Tab{i}.Name"]),
             "visible": int(values[f"Tab{i}.Visible"]), "index": i}
            for i in range(int(values["TabCount"]))]


def bounded_ascii_frame(frame):
    assert len(frame["rows"]) == len(frame["attributes"]) == frame["height"]
    for row, attributes in zip(frame["rows"], frame["attributes"]):
        assert len(row) == len(attributes) == frame["width"]
        assert all(32 <= ord(char) <= 126 for char in row), repr(row)


def new_config(exe, path):
    result = subprocess.run([exe, "--no-config", "--write-config", str(path)],
                            capture_output=True, timeout=5)
    assert result.returncode == 0, result.stderr.decode("utf-8", "replace")


def sampling_click_contract(console, exe, directory, pids, observations):
    config = directory / "sampling-clicks.conf"
    new_config(exe, config)
    # A slower frame cadence than sampling makes the previously unpainted
    # adoption window deterministic; clicks are still injected exactly once.
    console.start(exe, config, pids, "--status-bar", "--frame-interval", "200", interval=50)
    console.wait(lambda f: is_main(f) and has_bar(f), "50 ms sampling function bar")
    # A pending collector publication used to invalidate the visible frame
    # before input, silently dropping valid mouse presses. Every iteration
    # must acknowledge its one click; retries would conceal that regression.
    for iteration in range(8):
        command = ":unfinished-sample-" + str(iteration)
        console.type(command)
        console.wait(lambda f: editor_contains(f, command),
                     "sampling click command " + str(iteration))
        click_function(console, 1, "Help")
        console.wait(lambda f: overlay_contains(f, "Help"),
                     "single Help click during 50 ms sampling " + str(iteration))
        console.key(27, "\x1b")
        console.wait(lambda f: is_main(f) and has_bar(f)
                     and not editor_contains(f, ":"),
                     "sampling click leaves editor cancelled " + str(iteration))
    console.stop()
    observations["checks"].append(
        "eight single Help clicks at 50 ms sampling each acknowledge and cancel the editor, without click retries")


def setup_contract(console, exe, directory, pids, observations):
    config = directory / "setup-and-unicode.conf"
    new_config(exe, config)
    initial = config_tabs(config)
    before_ids = {tab["name"]: tab["id"] for tab in initial}
    next_id = int(config_values(config)["NextTabId"])
    with config.open("a", encoding="utf-8") as stream:
        stream.write("\n# Setup fixture extension must follow I/O's stable identity\nTab1.FutureScoped io-owned\n")
    console.start(exe, config, pids, "--status-bar", "--character-set", "ascii")
    console.key(0x71)
    frame = console.wait(lambda f: overlay_contains(f, "Setup") and f["rows"][1].startswith("General")
                         and function_ready(f, 2, "Next") and function_ready(f, 10, "Quit"), "Setup General page")
    assert_function(frame, 2, "Next")
    console.key(0x71)  # Contextual F2 advances a page; it does not reopen General.
    console.wait(lambda f: f["rows"][1].startswith("Meters"), "keyboard next Setup page")
    click_function(console, 2, "Next")
    console.wait(lambda f: f["rows"][1].startswith("Columns"), "mouse same next-page binding")
    console.key(0x71)
    tab_functions = ((3, "New"), (4, "Copy"), (5, "Rename"), (6, "Delete"), (8, "Move+"), (9, "Save"))
    frame = console.wait(lambda f: f["rows"][1].startswith("Tabs")
                         and all(function_ready(f, number, label) for number, label in tab_functions)
                         and function_ready(f, 7, "Move-", False)
                         and function_ready(f, 10, "Quit"), "Setup Tabs page and contextual controls")
    for number, label in tab_functions:
        assert_function(frame, number, label)
    assert_function(frame, 7, "Move-", False)  # Already at the first view.
    console.key(0x72)
    expected_new = f"View {next_id}"
    console.wait(lambda f: setup_rows(f) == ["Main", expected_new, "I/O"], "F3 inserts a view")
    click_function(console, 4, "Copy")
    expected_copy = f"Copy {next_id + 1}"
    console.wait(lambda f: setup_rows(f) == ["Main", expected_new, expected_copy, "I/O"], "mouse F4 duplicates selected view")
    console.key(0x74)
    console.wait(lambda f: editor_contains(f, "=" + expected_copy), "F5 opens view-name editor")
    click_function(console, 4, "Clear")
    console.wait(lambda f: editor_contains(f, "=") and not editor_contains(f, "=" + expected_copy), "editor F4 clears text")
    name = "Audit \u4e2d\u6587 e\u0301 \U0001f600"
    # Beta 3 substitutes a complete unsupported grapheme with one cell, so
    # the base plus combining accent is one '?' rather than two projections.
    shown_name = "Audit ?? ? ?"
    console.type(name)
    frame = console.wait(lambda f: editor_contains(f, "=" + shown_name), "CJK, combining and surrogate-pair input uses explicit ASCII cells")
    bounded_ascii_frame(frame)
    click_function(console, 9, "Apply")
    frame = console.wait(lambda f: shown_name in setup_rows(f) and function_ready(f, 9, "Save")
                         and text_contains(f, "glyph substitution"), "renamed view remains in Setup")
    assert text_contains(frame, "glyph substitution"), "lossy display was not disclosed"
    console.key(0x76)
    console.wait(lambda f: setup_rows(f) == ["Main", shown_name, expected_new, "I/O"], "F7 reorders selected stable view")
    click_function(console, 8, "Move+")
    console.wait(lambda f: setup_rows(f) == ["Main", expected_new, shown_name, "I/O"], "mouse F8 reverses reorder")
    console.key(0x78)
    console.wait(lambda f: text_contains(f, "Saved configuration:"), "Setup F9 explicit save")
    saved = config_tabs(config)
    assert [tab["id"] for tab in saved] == [before_ids["Main"], next_id, next_id + 1, before_ids["I/O"]]
    assert saved[2]["name"] == name, "ASCII projection overwrote raw Unicode configuration"
    assert config_values(config)["Tab3.FutureScoped"] == "io-owned", "unknown extension did not follow its view identity"

    # Invalid native input must stay editable and must not change the view name.
    console.key(0x74)
    console.wait(lambda f: editor_contains(f, "=" + shown_name), "rename editor after persisted Unicode")
    console.key(ord("U"), "\x15", 8)
    console.type("Bad\ud800")
    frame = console.wait(lambda f: editor_contains(f, "=Bad?"), "isolated surrogate has a safe fallback")
    bounded_ascii_frame(frame)
    console.key(0x78)
    console.wait(lambda f: editor_contains(f, "=Bad?") and shown_name in setup_rows(f)
                 and text_contains(f, "View not renamed"), "invalid name remains uncommitted, editable and visibly explained")
    console.key(27, "\x1b")
    console.wait(lambda f: shown_name in setup_rows(f) and not editor_contains(f, "="), "invalid rename cancellation")
    console.key(0x74)
    console.wait(lambda f: editor_contains(f, "=" + shown_name), "control-display editor")
    console.key(ord("U"), "\x15", 8)
    console.type("Control\u0085text")
    frame = console.wait(lambda f: editor_contains(f, "=Control?text"), "C1 control is not emitted into the console")
    bounded_ascii_frame(frame)
    console.key(27, "\x1b")
    console.wait(lambda f: shown_name in setup_rows(f) and not editor_contains(f, "="), "control fixture cancelled")

    original_size = (console.width, console.height)
    for width, height in ((60, 12), (20, 6), original_size):
        console.resize(width, height)
        title = "Resize to at least" if width < 40 or height < 10 else "Setup"
        frame = console.wait(lambda f: f["width"] == width and f["height"] == height
                             and f["rows"][0].startswith(title), "bounded redraw or explicit minimum-size fallback")
        bounded_ascii_frame(frame)
    console.key(0x75)
    frame = console.wait(lambda f: overlay_contains(f, "Delete view")
                         and str(next_id + 1) in f["rows"][1]
                         and function_ready(f, 10, "Quit"), "F6 captures view deletion")
    assert str(next_id + 1) in frame["rows"][1]
    assert not function_slot(frame, 9)[2].strip(), "delete confirmation accidentally exposes a repeated F9 action"
    console.key(27, "\x1b")
    console.wait(lambda f: f["rows"][1].startswith("Tabs") and shown_name in setup_rows(f), "delete cancellation preserves the view")
    click_function(console, 6, "Delete")
    console.wait(lambda f: overlay_contains(f, "Delete view"), "mouse same delete binding")
    console.type("y")
    console.wait(lambda f: setup_rows(f) == ["Main", expected_new, "I/O"], "explicit y deletes captured view")
    click_function(console, 9, "Save")
    console.wait(lambda f: text_contains(f, "Saved configuration:"), "mouse same save binding")
    final = config_tabs(config)
    assert [tab["id"] for tab in final] == [before_ids["Main"], next_id, before_ids["I/O"]]
    assert int(config_values(config)["NextTabId"]) == next_id + 2
    assert config_values(config)["Tab2.FutureScoped"] == "io-owned"
    console.stop()
    observations["checks"].append("contextual Setup CRUD/reorder/save with stable IDs, keyboard/mouse labels, Unicode raw preservation and bounded ASCII display")


def unicode_layout_contract(console, exe, directory, pids, observations):
    config = directory / "unicode-layout.conf"
    new_config(exe, config)
    assert config_values(config)["CharacterSet"] == "unicode"
    probe = console.cluster_widths()
    observations["unicode_host_probe"] = probe
    full = probe["qualified"]
    console.start(exe, config, pids, "--status-position", "bottom")
    console.wait(has_bar, "Unicode layout function bar")
    console.type(":e\u0301\u4e2d\U0001f600Z")

    def editor(frame):
        # The active editor shares the final function row even when idle
        # status is configured as a separate bottom row.
        return frame["rows"][-1], frame["attributes"][-1]

    def anchored(frame, column, caret):
        row, colors = editor(frame)
        return row.startswith(":e" if full else ":?") and row[column] == "Z" and (colors[caret] & 0xFF) == 0x30

    def plain(frame):
        row, colors = editor(frame)
        return row.startswith(":Z") and (colors[1] & 0xFF) == 0x30

    # These are independent terminal-cell positions: prompt 1, e+accent 1,
    # CJK 2, emoji 2, Z 1. ReadConsoleOutputW cannot expose the full combining
    # text; raw preservation and cluster segmentation have separate fixtures.
    # On a host that fails the independent native probe, every unsupported
    # cluster has one visible fallback cell. Do not pretend legacy conhost
    # proved full shaping merely because it accepted ENABLE_VT_PROCESSING.
    last, end, emoji = (6, 7, 4) if full else (4, 5, 3)
    frame = console.wait(lambda f: anchored(f, last, end), "host-qualified Unicode cell widths and end caret")
    if full:
        assert frame["cell_units"][-1][2] == 0x4E2D, "CJK glyph lost in qualified VT projection"
    else:
        assert frame["rows"][-1].startswith(":???Z"), "unsupported clusters were not conservatively projected"
    assert has_bar(frame), "Unicode editor displaced the independent function bar"
    assert function_slot(frame, 1)[0] == frame["width"] // 2, "Unicode width drift moved the function buttons"
    console.key(0x25)
    console.wait(lambda f: anchored(f, last, last), "caret before final ASCII glyph")
    console.key(0x25)
    console.wait(lambda f: anchored(f, last, emoji), "caret skips the complete supplementary grapheme")
    console.key(8, "\b")
    console.wait(lambda f: anchored(f, 4 if full else 3, 2), "backspace removes the preceding whole CJK grapheme")
    console.key(0x2E)
    console.wait(lambda f: anchored(f, 2, 2), "delete removes the complete supplementary grapheme")
    console.key(0x24)
    console.key(0x2E)
    console.wait(plain,
                 "delete removes base plus combining accent as one grapheme")
    console.key(ord("U"), "\x15", 8)
    console.type("\U0001f469\u200d\U0001f4bbZ")
    console.wait(lambda f: editor(f)[0][3 if full else 2] == "Z"
                 and (editor(f)[1][4 if full else 3] & 0xFF) == 0x30,
                 "ZWJ sequence occupies its qualified layout allocation")
    console.key(0x24)
    console.key(0x2E)
    console.wait(plain, "delete removes the complete ZWJ sequence")
    console.key(27, "\x1b")
    console.wait(lambda f: is_main(f) and has_bar(f) and not editor_contains(f, ":Z"),
                 "Unicode editor cancellation returns to main")
    console.stop()
    observations["checks"].append(
        "independently probed native Unicode projection, exact combining/CJK/supplementary/ZWJ cell anchors and caret positions, whole-grapheme deletion, fixed function-bar coordinates")


def readonly_contract(console, exe, directory, pids, fixtures, observations):
    config = directory / "readonly.conf"
    new_config(exe, config)
    original = config.read_bytes()
    sentinel = directory / "must-not-launch.txt"
    console.start(exe, config, pids, "--readonly", "--keymap", "vi", "--status-bar")
    disabled_functions = ((7, "Prio+"), (8, "Prio-"), (9, "Kill"))
    frame = console.wait(lambda f: is_main(f) and visible_pids(f, pids) == set(pids)
                         and all(function_ready(f, number, label, False) for number, label in disabled_functions)
                         and function_ready(f, 10, "Quit"), "read-only process view and disabled controls")
    for number, label in disabled_functions:
        assert_function(frame, number, label, False)
    console.key(0x78)
    console.wait(lambda f: is_main(f) and text_contains(f, "read-only"), "read-only keyboard action refusal")
    click_function(console, 9, "Kill", False)
    console.wait(lambda f: is_main(f) and text_contains(f, "Kill: read-only"), "same read-only mouse refusal")
    launch = subprocess.list2cmdline([sys.executable, "-c", "import pathlib,sys; pathlib.Path(sys.argv[1]).write_text('launched')", str(sentinel)])
    console.type(":exec " + launch)
    console.key(13, "\r")
    console.wait(lambda f: text_contains(f, "forbids launching commands"), "read-only :exec rejected before launch")
    assert not sentinel.exists(), "read-only :exec launched its owned sentinel command"
    console.type(":kill " + " ".join(map(str, pids)))
    console.key(13, "\r")
    frame = console.wait(lambda f: overlay_contains(f, "Actions") and all(text_contains(f, f"PID {pid} denied") for pid in pids), "each read-only :kill result remains visible")
    assert all(child.poll() is None for child in fixtures)
    console.key(13, "\r")
    console.wait(lambda f: is_main(f) and has_bar(f), "advertised Enter closes retained action results")
    console.type(":save")
    console.key(13, "\r")
    console.wait(lambda f: text_contains(f, "writes disabled by --readonly"), "read-only :save refusal")
    assert config.read_bytes() == original
    console.key(0x71)
    frame = console.wait(lambda f: overlay_contains(f, "Setup")
                         and function_ready(f, 9, "Save", False)
                         and function_ready(f, 10, "Quit"), "read-only Setup and disabled save")
    assert_function(frame, 9, "Save", False)
    click_function(console, 9, "Save", False)
    console.wait(lambda f: overlay_contains(f, "Setup") and text_contains(f, "Save: read-only"), "read-only save button is disabled with its reason")
    console.stop()
    assert config.read_bytes() == original
    observations["checks"].append("readonly :exec/:kill/:save and function buttons retain per-target denial and leave all owned fixtures/settings unchanged")


def churn_contract(console, exe, directory, pids, fixtures, observations):
    config = directory / "churn.conf"
    new_config(exe, config)
    victim = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(240)"],
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fixtures.append(victim)
    targets = pids + [victim.pid]
    console.start(exe, config, targets, "--status-bar", "-s", "ID")
    frame = console.wait(lambda f: visible_pids(f, targets) == set(targets), "all churn fixtures observed")
    row = next(y for y, text in enumerate(frame["rows"]) if text.lstrip().split(None, 1)[0:1] == [str(victim.pid)])
    console.click(0, row)
    console.wait(lambda f: visible_pids(f, targets, True) == {victim.pid}, "lifetime-bound victim selection")
    console.key(0x78)
    console.wait(lambda f: overlay_contains(f, "Terminate") and text_contains(f, f"PID {victim.pid}  creation"), "captured victim identity")
    victim.terminate()
    victim.wait(timeout=5)
    replacement = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(240)"],
                                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fixtures.append(replacement)
    console.wait(lambda f: overlay_contains(f, "Terminate") and text_contains(f, f"rows {len(pids)}/"), "published process churn while confirmation remains open")
    console.type("y")
    frame = console.wait(lambda f: overlay_contains(f, "Actions") and text_contains(f, f"PID {victim.pid} "), "retained result belongs to the captured exited identity")
    results = re.findall(r"\bPID ([0-9]+) ([a-z_]+)", "\n".join(frame["rows"]))
    assert len(results) == 1 and results[0][0] == str(victim.pid), results
    assert results[0][1] in ("already_exited", "identity_changed", "failed", "denied"), results
    assert all(child.poll() is None for child in fixtures if child is not victim), "churn redirected termination to another live process"
    console.key(13, "\r")
    console.wait(lambda f: is_main(f) and has_bar(f)
                 and not visible_pids(f, targets, True)
                 and function_ready(f, 9, "Kill", False),
                 "exited selection retires without selecting a neighbour")
    console.stop()
    observations["checks"].append("process churn retains captured identity and action result without terminating a replacement/neighbour; deterministic PID reuse remains a core-test case")


def inspection_contract(console, exe, directory, pids, observations):
    config = directory / "inspection.conf"
    new_config(exe, config)
    original = config.read_bytes()
    console.start(exe, config, pids, "--readonly", "-s", "ID")
    console.wait(lambda f: is_main(f) and visible_pids(f, pids) == set(pids), "inspection owned targets")
    console.key(0x24)
    console.wait(lambda f: visible_pids(f, pids, highlighted_only=True) == {max(pids)} and not visible_pids(f, pids, True), "read-only inspection from focus")
    console.type("d")
    console.wait(lambda f: overlay_contains(f, "Process details"), "asynchronous detail panel opens")
    frame = console.wait(lambda f: any(row.startswith("Provider: ") for row in f["rows"])
                         and function_ready(f, 7, "Affinity", False),
                         "detail provider result and read-only function state are displayed")
    assert_function(frame, 7, "Affinity", False)
    console.type("H")
    # The cached basic process section also says 'sampled threads: N'. It is
    # visible while the optional provider is still collecting, so it cannot
    # acknowledge thread observation completion. Match the returned/observed
    # summary row, independently of the exact unit assertion that follows.
    frame = console.wait(lambda f: overlay_contains(f, "Thread rows")
                         and any(re.match(r"Threads: [0-9]+ returned / [0-9]+ observed;", row)
                                 for row in f["rows"]),
                         "bounded thread observation returned and rendered")
    assert text_contains(frame, "lifetime 100ns"), "thread counters were not labelled as lifetime values"
    console.key(0x23)  # End exposes later rows; it never changes the captured target.
    console.wait(lambda f: overlay_contains(f, "Thread rows") and text_contains(f, "TID "), "thread rows remain navigable")
    console.key(27, "\x1b")
    console.wait(lambda f: is_main(f) and not visible_pids(f, pids, True), "inspection closes without creating selection")
    console.stop()
    assert config.read_bytes() == original
    observations["checks"].append("read-only focused details and thread panels use bounded providers, scroll and return without selecting or writing config")


def recovery_contract(console, exe, directory, pids, observations):
    config = directory / "recovery.conf"
    new_config(exe, config)
    original = config.read_bytes()
    console.start(exe, config, pids, "--keymap", "htop", "--status-bar", "-s", "ID")
    frame = console.wait(lambda f: is_main(f) and visible_pids(f, pids) == set(pids)
                         and not visible_pids(f, pids, True)
                         and function_ready(f, 9, "Kill", False), "startup has no implicit target")
    assert all(function_ready(frame, number, label, False)
               for number, label in ((7, "Prio+"), (8, "Prio-"), (9, "Kill")))
    console.key(0x28)  # Down only highlights the first visible row.
    console.wait(lambda f: visible_pids(f, pids, highlighted_only=True) == {max(pids)}
                         and not visible_pids(f, pids, True) and function_ready(f, 9, "Kill", False), "Down highlights without enabling mutation")
    console.key(13, "\r")
    console.wait(lambda f: visible_pids(f, pids, True) == {max(pids)}, "Enter confirms highlighted target")
    console.type(" ")
    console.wait(lambda f: visible_pids(f, pids, highlighted_only=True) == {sorted(pids, reverse=True)[1]}
                         and visible_pids(f, pids, True) == {max(pids)}, "tag moves focus while confirmed target stays fixed")
    console.type("F")
    console.key(0x70)
    console.wait(lambda f: overlay_contains(f, "Help"), "Help above a selection and tag")
    console.key(27, "\x1b")
    console.wait(lambda f: is_main(f) and function_ready(f, 9, "Kill"), "Escape closes Help without mutating captured selection")
    console.key(27, "\x1b")
    frame = console.wait(lambda f: is_main(f) and function_ready(f, 9, "Kill", False)
                         and not visible_pids(f, pids, True) and visible_pids(f, pids, highlighted_only=True) == {max(pids)}, "first main Escape clears selection/tags and highlights former target")
    console.key(27, "\x1b")
    frame = console.wait(lambda f: is_main(f) and not visible_pids(f, pids, True)
                         and not visible_pids(f, pids, highlighted_only=True), "second main Escape clears highlight/search/follow")
    for y, row in enumerate(frame["rows"]):
        token = row.lstrip().split(None, 1)[0:1]
        if token and token[0].isdigit() and int(token[0]) in pids:
            assert frame["attributes"][y][0] == 0x07, "Escape retained a tag/follow/selected row attribute"
    console.stop()
    assert config.read_bytes() == original

    # All 36 context/keymap combinations are checked in native_input using the
    # actual dispatcher. Here representative raw-input paths qualify real
    # screen/mode restoration; the signal case below uses the actual handler.
    for keymap, context in (("htop", "command"), ("vi", "help"), ("emacs", "paused")):
        console.start(exe, config, pids, "--keymap", keymap, "--status-bar")
        if context == "command":
            console.type(":unfinished")
            console.wait(lambda f: editor_contains(f, ":unfinished"), "command before Ctrl-C")
        elif context == "help":
            console.key(0x70)
            console.wait(lambda f: overlay_contains(f, "Help"), "Help before Ctrl-C")
        else:
            console.type("Z")
            console.wait(lambda f: text_contains(f, "PAUSED"), "pause before Ctrl-C")
        console.key(ord("C"), "\x03", 8)
        console.stop(send_key=False)

    # The helper owns a private console. Ignore Ctrl-C here and in owned fixture
    # processes, then verify Wtop explicitly clears its inherited ignore flag.
    console.start(exe, config, pids, "--status-bar")
    console.key(0x71)
    console.wait(lambda f: overlay_contains(f, "Setup"), "Setup before real inherited-ignore Ctrl-C")
    console.check(console.k.GenerateConsoleCtrlEvent(0, 0))
    console.stop(send_key=False)
    assert config.read_bytes() == original
    observations["checks"].append("no startup target; first navigation; layered Escape clears transient state; global raw Ctrl-C and real inherited-ignore Ctrl-C restore caller modes/buffer without saving")


def viewport_matrix_contract(console, exe, directory, pids, observations):
    """Exercise the actual screen buffer; saved layout must not be rewritten."""
    config = directory / "viewport-matrix.conf"
    new_config(exe, config)
    original = config.read_bytes()
    console.start(exe, config, pids, "--keymap", "htop", "--status-bar", interval=50)
    sizes = [(40, 25), (80, 25), (40, 10), (39, 9), (80, 25)]
    observed = []
    for width, height in sizes:
        console.resize(width, height)
        if width < 40 or height < 10:
            console.wait(lambda f: text_contains(f, "Resize to at least"), "safe tiny viewport")
        else:
            console.wait(is_main, "main view after resize")
            console.key(0x70)
            console.wait(lambda f: overlay_contains(f, "Help"), "help after resize")
            console.key(27, "\x1b")
            console.wait(is_main, "keyboard recovery after resize")
        frame = console.frame()
        assert len(frame["rows"]) == height
        assert all(len(row) == width for row in frame["rows"])
        observed.append([width, height])
    # Test 80x50 only when the real host permits that viewport. A host limitation
    # is disclosed, not a fabricated pass; the desktop review still requires it.
    handle = console.open("CONOUT$")
    try:
        maximum = console.info(handle).maximum
    finally:
        console.k.CloseHandle(handle)
    if maximum.X >= 80 and maximum.Y >= 50:
        console.resize(80, 50)
        console.wait(is_main, "80x50 native view")
        observed.append([80, 50])
    else:
        observations["viewport_80x50"] = "host-limited; desktop review required"
    console.resize(console.width, console.height)
    console.wait(is_main, "original viewport recovery")
    console.stop()
    assert config.read_bytes() == original, "resizing implicitly saved settings"
    observations["viewport_sizes"] = observed
    observations["checks"].append("native viewport matrix, tiny fallback, keyboard recovery and unchanged settings")


def retained_frame_contract(console, exe, directory, pids, observations):
    config = directory / "retained-frame.conf"
    new_config(exe, config)
    for style in ("functions", "compact", "status"):
        console.start(exe, config, pids, "--footer", style, "--status-position", "banner", interval=50)
        frame = console.wait(lambda f: is_main(f) and visible_pids(f, pids) == set(pids), "buffered " + style)
        assert not frame["rows"][-2].startswith("age "), "default leaked a second footer"
        if style == "status":
            assert "view " in frame["rows"][-1]
        else:
            assert has_bar(frame)
        samples = 0
        deadline = time.monotonic() + 0.8
        while time.monotonic() < deadline:
            actual = console.frame()
            assert actual["rows"][0].startswith("Wtop "), "observed intermediate clear during steady refresh"
            assert visible_pids(actual, pids) == set(pids), "process rows disappeared during steady refresh"
            samples += 1
        console.type("Z")
        console.wait(lambda f: text_contains(f, "PAUSED"), "pause remains visible")
        console.type("Z")
        console.wait(lambda f: not text_contains(f, "PAUSED"), "resume")
        console.stop()
        observations.setdefault("buffered_frames", []).append({"style": style, "sampled_frames": samples})
    console.start(exe, config, pids, "--footer", "functions", "--status-position", "bottom")
    console.wait(lambda f: has_bar(f) and "view " in f["rows"][-2], "explicit two-row opt-in")
    console.stop()
    observations["checks"].append("one footer default, three styles, explicit dual-row opt-in, pause/resume and no observed intermediate blank refresh")


def worker(exe, output):
    console = None
    fixtures = []
    observations = {"checks": [], "human_visual_acceptance": False}
    try:
        footer_slot_contract()
        console = Console()
        with tempfile.TemporaryDirectory(prefix="wtop-native-ui-") as temp:
            config = Path(temp) / "settings.conf"
            created = subprocess.run([exe, "--no-config", "--write-config", str(config)],
                                     capture_output=True, timeout=5)
            assert created.returncode == 0, created.stderr.decode("utf-8", "replace")
            with config.open("a", encoding="utf-8") as stream:
                stream.write("\n# retained native fixture comment\nFutureOption keep-me\n")
            fixtures = [subprocess.Popen([sys.executable, "-c", "import time; time.sleep(240)"],
                        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL) for _ in range(3)]
            pids = [p.pid for p in fixtures]
            console.start(exe, config, pids)
            frame = console.wait(has_bar, "default last-row function bar")
            observations["default_frame"] = frame
            assert console.mode() & 0x10, "mouse input was not enabled on this console"
            assert not console.mode() & 0x40, "QuickEdit may freeze interactive input"
            assert not console.mode() & 0x06, "line/echo modes can intercept TUI control keys"
            observations["checks"].append("default htop function bar and native mouse mode")

            click_function(console, 2, "Setup")
            console.wait(lambda f: overlay_contains(f, "Setup"), "mouse-opened setup")
            console.key(27, "\x1b")
            console.wait(lambda f: has_bar(f) and is_main(f), "setup close")
            console.key(0x70)  # F1
            console.wait(lambda f: overlay_contains(f, "Help"), "keyboard help")
            console.key(27, "\x1b")
            console.wait(lambda f: has_bar(f) and is_main(f), "help close")
            observations["checks"].append("mouse function action and keyboard overlays")

            console.wait(lambda f: visible_pids(f, pids) == set(pids), "identity-bearing targets before confirmation")
            assert not visible_pids(console.frame(), pids, True), "startup unexpectedly selected a process"
            console.key(0x24)  # Home highlights; Enter confirms an explicit action target.
            console.wait(lambda f: bool(visible_pids(f, pids, highlighted_only=True)) and not visible_pids(f, pids, True), "Home highlights without selecting")
            console.key(13, "\r")
            console.wait(lambda f: bool(visible_pids(f, pids, True))
                         and function_ready(f, 9, "Kill"), "explicit target before confirmation")
            console.records(console.key_records(0x78, repeat=32))  # Held initiating F9.
            frame = console.wait(lambda f: overlay_contains(f, "Terminate")
                                 and function_ready(f, 10, "Quit"), "termination confirmation")
            assert all(p.poll() is None for p in fixtures), "opening confirmation mutated a process"
            assert not function_slot(frame, 9)[2].strip(), "F9 was actionable inside confirmation"
            repeated = []
            for _ in range(10):
                repeated.extend(console.key_records(0x78, repeat=32))
            repeated.extend(console.key_records(13, "\r"))
            repeated.extend(console.key_records(0x70))  # Positive acknowledgement after every queued repeat.
            console.records(repeated)
            console.wait(lambda f: overlay_contains(f, "Help"), "held F9 and Enter cannot confirm; later help is processed")
            assert all(p.poll() is None for p in fixtures), "repeated F9 or Enter confirmed termination"
            console.key(27, "\x1b")
            console.wait(lambda f: has_bar(f) and is_main(f), "termination cancellation")
            assert all(p.poll() is None for p in fixtures)
            observations["checks"].append("held/repeated F9 and Enter never confirm; contextual help/cancel remain non-mutating")

            console.type(":unfinished-fixture-command")
            frame = console.wait(lambda f: editor_contains(f, ":unfinished-fixture-command"),
                                 "unfinished command editor")
            click_function(console, 1, "Help")
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
            console.wait(lambda f: is_main(f) and not has_bar(f) and "age " in f["rows"][0]
                         and "view " in f["rows"][0], "hidden function bar retains banner telemetry without another footer")
            console.type("B")
            console.wait(lambda f: is_main(f) and has_bar(f), "keyboard recovery of hidden function bar")
            console.type("B")
            console.wait(lambda f: is_main(f) and not has_bar(f) and "age " in f["rows"][0], "banner telemetry retained after bar is hidden again")
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
            restored = console.frame()
            assert not has_bar(restored), "hidden bar was not restored from saved settings"
            assert not visible_pids(restored, pids, True), "restoring settings restored a process action target"
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
            console.wait(lambda f: visible_pids(f, pids, highlighted_only=True) == {ordered_pids[0]} and not visible_pids(f, pids, True), "vi Home highlights first process")
            console.type("j")
            console.wait(lambda f: visible_pids(f, pids, highlighted_only=True) == {ordered_pids[1]} and not visible_pids(f, pids, True), "vi j highlights next process")
            console.type("k")
            console.wait(lambda f: is_main(f) and visible_pids(f, pids, highlighted_only=True) == {ordered_pids[0]} and not visible_pids(f, pids, True),
                         "vi k highlights previous process without opening termination")
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

            console.start(exe, config, pids, "--keymap", "htop", "--status-bar")
            console.wait(lambda f: is_main(f) and visible_pids(f, pids) == set(pids),
                         "owned process targets before queued quit")
            # One input-buffer write: q must retire the rest of this same batch.
            # A regression can target only the three processes owned by this test.
            console.type("qky")
            console.stop(send_key=False)
            assert all(p.poll() is None for p in fixtures), "input after quit mutated an owned process"
            observations["checks"].append("queued actions after quit are discarded before mutation")
            viewport_matrix_contract(console, exe, Path(temp), pids, observations)
            retained_frame_contract(console, exe, Path(temp), pids, observations)
            sampling_click_contract(console, exe, Path(temp), pids, observations)
            setup_contract(console, exe, Path(temp), pids, observations)
            unicode_layout_contract(console, exe, Path(temp), pids, observations)
            readonly_contract(console, exe, Path(temp), pids, fixtures, observations)
            churn_contract(console, exe, Path(temp), pids, fixtures, observations)
            recovery_contract(console, exe, Path(temp), pids, observations)
            inspection_contract(console, exe, Path(temp), pids, observations)
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
            _, errors = process.communicate(b"start\n", timeout=160)
        except subprocess.TimeoutExpired:
            job.close()  # Kill the worker AND its monitor/fixture descendants.
            _, errors = process.communicate(timeout=5)
            print(errors.decode("utf-8", "replace"), file=sys.stderr)
            raise AssertionError("native TUI worker exceeded 160 seconds; owned process tree terminated")
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
