"""Exercise token-query contracts without claiming a native Windows result."""
from __future__ import annotations

import ctypes
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "packaging"))
import native_token


class TokenScalarContract(unittest.TestCase):
    def query(self, kind, payload, returned):
        calls = []

        def fixed_query(token, query_kind, output, capacity, needed):
            # Windows Server 2025 rejected the former NULL/zero-byte probe with
            # ERROR_BAD_LENGTH. Require a real bounded output on the first call.
            self.assertIsNotNone(output)
            self.assertEqual(capacity, 4)
            self.assertEqual(query_kind, kind)
            self.assertEqual(token, 123)
            calls.append(query_kind)
            ctypes.memmove(output, payload, len(payload))
            ctypes.cast(needed, ctypes.POINTER(native_token.w.DWORD))[0] = returned
            return 1

        native = object.__new__(native_token._NativeToken)
        native.security = SimpleNamespace(GetTokenInformation=fixed_query)
        # Allow the old implementation to reach the forbidden zero-sized call
        # on Linux too, instead of failing on the absent Win32 ctypes helpers.
        with patch.object(ctypes, "set_last_error", lambda value: None, create=True):
            result = native.scalar(123, kind)
        self.assertEqual(calls, [kind])
        return result

    def test_fixed_elevation_and_type_are_queried_without_a_sizing_probe(self):
        self.assertEqual(self.query(20, b"\x01\x00\x00\x00", 4), (1, 4))
        self.assertEqual(self.query(18, b"\x03\x00\x00\x00", 4), (3, 4))

    def test_filtering_preserves_one_or_four_actual_returned_bytes(self):
        self.assertEqual(self.query(21, b"\x01", 1), (1, 1))
        self.assertEqual(self.query(21, b"\x00\x00\x00\x00", 4), (0, 4))

    def test_other_scalar_sizes_are_rejected_before_reading(self):
        for kind, returned in ((20, 1), (18, 1), (21, 0), (21, 2), (21, 5)):
            with self.subTest(kind=kind, returned=returned):
                with self.assertRaisesRegex(RuntimeError, "unexpected scalar size"):
                    self.query(kind, b"\x00\x00\x00\x00", returned)

    def test_failed_query_cannot_be_reported_as_a_false_value(self):
        native = object.__new__(native_token._NativeToken)
        native.security = SimpleNamespace(GetTokenInformation=lambda *args: 0)
        with patch.object(ctypes, "get_last_error", lambda: 5, create=True), \
                patch.object(ctypes, "WinError", lambda code: OSError(code, "denied"), create=True):
            with self.assertRaises(OSError) as caught:
                native.scalar(123, 20)
        self.assertEqual(caught.exception.errno, 5)

    def test_unknown_scalar_kind_is_not_given_an_invented_width(self):
        native = object.__new__(native_token._NativeToken)
        with self.assertRaisesRegex(ValueError, "no fixed scalar contract"):
            native.scalar(123, 1)


if __name__ == "__main__":
    unittest.main()
