"""Validate diagnostic error and native-buffer handling without claiming MSI acceptance."""
import ctypes
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "packaging"))
from installer_diagnostics import _default_acl, _observed


class InstallerDiagnosticsContracts(unittest.TestCase):
    def test_denied_and_missing_are_distinct_observations(self):
        def failure(code):
            error = OSError(code, "native diagnostic failure")
            error.winerror = code
            raise error

        for code in (2, 5):
            observed = _observed(lambda: failure(code))
            self.assertFalse(observed["ok"])
            self.assertEqual(observed["winerror"], code)
            self.assertNotIn("value", observed)
        self.assertEqual(_observed(lambda: 0), {"ok": True, "value": 0})

    def test_default_acl_can_be_null_or_bounded(self):
        header = ctypes.sizeof(ctypes.c_void_p)
        buffer = ctypes.create_string_buffer(header + 8)
        self.assertIsNone(_default_acl(buffer, header))
        address = ctypes.addressof(buffer) + header
        ctypes.c_void_p.from_buffer(buffer).value = address
        ctypes.memmove(address, b"\x02\x00\x08\x00\x00\x00\x00\x00", 8)
        self.assertEqual(_default_acl(buffer, len(buffer)), address)

    def test_outside_pointer_and_truncated_acl_are_rejected_before_use(self):
        header = ctypes.sizeof(ctypes.c_void_p)
        buffer = ctypes.create_string_buffer(header + 8)
        start = ctypes.addressof(buffer)
        for address in (start - 1, start, start + len(buffer) - 7):
            ctypes.c_void_p.from_buffer(buffer).value = address
            with self.assertRaisesRegex(RuntimeError, "outside"):
                _default_acl(buffer, len(buffer))
        ctypes.c_void_p.from_buffer(buffer).value = start + header
        for length in (7, 9):
            ctypes.memmove(start + header, b"\x02\x00" + length.to_bytes(2, "little") + b"\0" * 4, 8)
            with self.assertRaisesRegex(RuntimeError, "truncated"):
                _default_acl(buffer, len(buffer))
        for returned in (header - 1, len(buffer) + 1):
            with self.assertRaisesRegex(RuntimeError, "length"):
                _default_acl(buffer, returned)


if __name__ == "__main__":
    unittest.main()
