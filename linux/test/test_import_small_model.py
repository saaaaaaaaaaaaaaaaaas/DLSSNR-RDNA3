#!/usr/bin/env python3
"""CPU-only importer checks. All fixtures are synthetic, without model weights."""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parents[1] / "package/model-tools"
sys.path.insert(0, str(TOOLS))
import import_small_model as importer


def pack(entries):
    entries = list(entries)
    header_size = 16 + sum(20 + len(name.encode()) for name, _ in entries)
    pos = (header_size + 255) // 256 * 256
    header = bytearray(b"NRMODEL1" + struct.pack("<II", len(entries), 0))
    payload = bytearray(pos - header_size)
    for i, (name, data) in enumerate(entries):
        header += struct.pack("<I", len(name.encode())) + name.encode() + struct.pack("<QQ", pos, len(data))
        payload += data
        pos += len(data)
        if i + 1 < len(entries):
            padding = (-pos) % 256
            payload += b"\0" * padding
            pos += padding
    return bytes(header + payload)


def resource(name, data):
    body = (struct.pack("<QQI", len(data) + 40, len(data), 1) + data
            + struct.pack("<IIQI", 0, 0, 1, len(data) // 2))
    record = struct.pack("<Q", len(name)) + name.encode() + struct.pack("<Q", len(body)) + body
    return struct.pack("<Q", len(record) + 8) + record


def delta(entries):
    result = bytearray(b"NRXDIFF1" + struct.pack("<I", len(entries)))
    for name, data in entries:
        result += struct.pack("<I", len(name)) + name.encode() + struct.pack("<Q", len(data)) + data
    return bytes(result)


class ImporterTests(unittest.TestCase):
    def test_real_descriptor_sizes(self):
        raw, packed, matrices, floats = importer.schema()
        self.assertEqual(len(raw), 77)
        self.assertEqual(len(packed), 255)
        self.assertEqual(sum(packed.values()), 113887280)
        self.assertTrue(matrices)
        self.assertTrue(floats)

    def test_raw_resource_envelope_and_fp16_rejection(self):
        name = "block0.layer0.layer"
        valid = resource(name, b"\x01\x02\x03\x04")
        self.assertEqual(importer.validate_resource(valid, {name: 4})[0]["payload_size"], 4)
        with self.assertRaisesRegex(importer.FormatError, "FP16 and INT8"):
            importer.validate_resource(resource(name, b"\0" * 8), {name: 4})
        with self.assertRaises(importer.FormatError):
            importer.validate_resource(valid[:-1], {name: 4})
        with self.assertRaisesRegex(importer.FormatError, "missing"):
            importer.validate_resource(valid, {"other": 4})

    def test_pack_rejects_duplicates_ranges_and_unknown_entries(self):
        expected = {"a": 4, "b": 2}
        good = pack([("a", b"1234"), ("b", b"56")])
        self.assertEqual(set(importer.parse_pack(good, expected)), set(expected))
        with self.assertRaisesRegex(importer.FormatError, "duplicate"):
            importer.parse_pack(pack([("a", b"1234"), ("a", b"5678")]), expected)
        with self.assertRaisesRegex(importer.FormatError, "unexpected"):
            importer.parse_pack(pack([("a", b"1234"), ("../b", b"56")]), expected)
        with self.assertRaises(importer.FormatError):
            importer.parse_pack(good[:-1], expected)
        overlap = bytearray(good)
        struct.pack_into("<Q", overlap, 16 + 21 + 4 + 1, 256)
        with self.assertRaisesRegex(importer.FormatError, "overlapping"):
            importer.parse_pack(overlap, expected)

    def test_pack_rejects_truncated_index_and_trailing_bytes(self):
        good = pack([("a", b"12")])
        for invalid in (good[:18], good + b"\0"):
            with self.assertRaises(importer.FormatError):
                importer.parse_pack(invalid, {"a": 2})

    def test_numeric_validation_checks_fp8_and_fp16(self):
        for bad, matrices, floats in [(b"\x7f\0", {"a": [(0, 2)]}, {}),
                                      (struct.pack("<e", float("nan")), {}, {"a": "<e"})]:
            data = pack([("a", bad)])
            with self.assertRaisesRegex(importer.FormatError, "non-finite"):
                importer.validate_values(data, importer.parse_pack(data, {"a": 2}), matrices, floats)

    def test_raw_matrix_validation_skips_metadata_and_checks_weights(self):
        data = pack([("raw", b"\x7f\xff\x01\x02")])
        index = importer.parse_pack(data, {"raw": 4})
        importer.validate_values(data, index, {"raw": [(2, 2)]}, {})
        with self.assertRaisesRegex(importer.FormatError, "non-finite"):
            importer.validate_values(data, index, {"raw": [(0, 2)]}, {})

    def test_folder_selection_uses_contents_and_detects_ambiguity(self):
        name = "block0.layer0.layer"
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            fp8 = folder / "renamed.bin"
            fp8.write_bytes(resource(name, b"1234"))
            (folder / "WEIGHTS_FP8.but-actually-fp16.bin").write_bytes(resource(name, b"0" * 8))
            chosen, _, kind, ignored = importer.select_input(folder, {name: 4}, {})
            self.assertEqual(chosen, fp8)
            self.assertEqual(kind, "WEIGHTS_CC_SMALL_FP8_FP8")
            self.assertEqual(len(ignored), 1)
            (folder / "another.bin").write_bytes(resource(name, b"5678"))
            with self.assertRaisesRegex(importer.FormatError, "multiple different"):
                importer.select_input(folder, {name: 4}, {})

    def test_delta_rejects_truncation_duplicates_and_unknown_names(self):
        for invalid in (delta([("a", b"12")])[:-1], delta([("a", b"12"), ("a", b"34")]),
                        delta([("other", b"12")]), delta([("a", b"1234")])):
            with self.assertRaises(importer.FormatError):
                importer.delta_entries(invalid, {"a": 2, "b": 2})

    def test_hybrid_recovers_fitted_entries_and_preserves_custom_entries(self):
        expected = {"fitted": 2, "preserved": 2}
        reference = pack([("fitted", b"\x01\x02"), ("preserved", b"\x03\x04")])
        fitted = pack([("fitted", b"\x05\x06"), ("preserved", b"\x03\x04")])
        custom = pack([("fitted", b"\x10\x20"), ("preserved", b"\x30\x40")])
        change = delta([("fitted", b"\x04\x04")])
        with patch.object(importer, "STOCK_SHA256", hashlib.sha256(reference).hexdigest()):
            hybrid, replaced = importer.make_hybrid(custom, reference, expected, change,
                                                    hashlib.sha256(fitted).hexdigest())
            self.assertEqual(hybrid, pack([("fitted", b"\x05\x06"), ("preserved", b"\x30\x40")]))
            self.assertEqual(replaced, ["fitted"])
            stock, _ = importer.make_hybrid(reference, reference, expected, change,
                                           hashlib.sha256(fitted).hexdigest())
            self.assertEqual(stock, fitted)
            with self.assertRaisesRegex(importer.FormatError, "checksum"):
                importer.make_hybrid(custom, reference, expected, change, "0" * 64)
            with self.assertRaisesRegex(importer.FormatError, "unchanged stock"):
                importer.make_hybrid(custom, custom, expected, change, hashlib.sha256(fitted).hexdigest())

    def test_cli_preserves_existing_destination_and_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            source = folder / "bad.bin"
            source.write_bytes(b"not a model")
            destination = folder / "output"
            destination.mkdir()
            (destination / "keep").write_bytes(b"keep")
            result = subprocess.run([sys.executable, str(TOOLS / "import_small_model.py"), str(source),
                                     "--small-only", "--output", str(destination)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("output already exists", result.stderr)
            self.assertEqual(source.read_bytes(), b"not a model")
            self.assertEqual((destination / "keep").read_bytes(), b"keep")
            result = subprocess.run([sys.executable, str(TOOLS / "import_small_model.py"), str(source),
                                     "--small-only", "--output", str(folder / "new")], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((folder / "new").exists())
            self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
