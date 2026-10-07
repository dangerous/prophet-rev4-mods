"""Acceptance tests for the OS image format and round-trip (docs/SPEC.md: "OS image format
and round-trip")."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests import fixture_check
from tools import records, syx

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "fixtures"
MAIN = FIXTURES / "prophet5_main_2.1.0.syx"
PANEL = FIXTURES / "prophet5Panel_v1.1.3.syx"


def setUpModule():
    fixture_check.require([MAIN.name, PANEL.name])

# (path, target, groups, tail) — spec "Fixture facts"
FIXTURE_FACTS = [
    (MAIN, "main", 30180, 4),
    (PANEL, "panel", 2498, 2),
]


def _corrupt(raw: bytes, index: int, value: int) -> bytes:
    b = bytearray(raw)
    b[index] = value
    return bytes(b)


def _cli(*args):
    return subprocess.run(
        [sys.executable, "-m", "tools", *map(str, args)],
        cwd=ROOT, capture_output=True, text=True,
    )


class ContainerTests(unittest.TestCase):
    def test_decode_reports_header_facts(self):
        for path, target, groups, tail in FIXTURE_FACTS:
            with self.subTest(path.name):
                c = syx.decode(path.read_bytes())
                self.assertEqual((c.target, c.groups, c.tail), (target, groups, tail))
                self.assertEqual(len(c.payload), 7 * groups + tail)

    def test_trailer_computed_from_payload_matches_fixture(self):
        for path, *_ in FIXTURE_FACTS:
            with self.subTest(path.name):
                c = syx.decode(path.read_bytes())
                self.assertEqual(syx.trailer_for(c.payload), c.trailer)

    def test_round_trip_is_byte_exact(self):
        for path, *_ in FIXTURE_FACTS:
            with self.subTest(path.name):
                raw = path.read_bytes()
                c = syx.decode(raw)
                self.assertEqual(syx.encode(c.payload, c.target), raw)

    def test_trailer_rule_examples(self):
        # S = sum of LE u16 halfwords; bytes = S & 0x7F, (S >> 8) & 0x7F; odd final byte ignored
        self.assertEqual(syx.trailer_for(bytes([0x01, 0x02])), bytes([0x01, 0x02]))
        self.assertEqual(syx.trailer_for(bytes([0xFF, 0xFF])), bytes([0x7F, 0x7F]))
        self.assertEqual(syx.trailer_for(bytes([0x01, 0x00, 0x01, 0x00])), bytes([0x02, 0x00]))
        self.assertEqual(syx.trailer_for(bytes([0x01, 0x02, 0x55])), bytes([0x01, 0x02]))

    def test_pack7_unpack7_cover_full_and_partial_groups(self):
        data = bytes(range(256)) * 3 + b"\x80\x7f\x00"  # 771 bytes = 110 groups + 1 tail byte
        packed = syx.pack7(data)
        self.assertEqual(len(packed), 110 * 8 + 2)
        self.assertTrue(all(b < 0x80 for b in packed))
        self.assertEqual(syx.unpack7(packed), data)

    def test_empty_tail_group_still_has_its_header_byte(self):
        # spec "Payload": the loader always reads the tail group's MS byte, even for tail = 0
        payload = bytes(range(14))                      # exactly two full groups
        raw = syx.encode(payload, "main")
        body = raw[4 + 1 + 6:-1]                        # after F0 01 32 7C 7A + header group
        packed, trailer = body[:-2], body[-2:]
        self.assertEqual(len(packed), 2 * 8 + 1)
        self.assertEqual(packed[-1], 0)                 # the empty tail group's MS byte
        self.assertEqual(trailer, syx.trailer_for(payload))
        c = syx.decode(raw)
        self.assertEqual((c.groups, c.tail, c.payload), (2, 0, payload))
        # without that byte the file is what stalled the loader: reject it
        without = raw[:4 + 1 + 6 + 16] + raw[4 + 1 + 6 + 17:]
        with self.assertRaisesRegex(syx.SyxError, "inconsistent"):
            syx.decode(without)
        self.assertEqual(syx.pack7(b""), b"")           # pack7 itself stays pure

    def test_decode_rejects_wrong_prefix_or_command(self):
        raw = MAIN.read_bytes()
        with self.assertRaisesRegex(syx.SyxError, "prefix"):
            syx.decode(_corrupt(raw, 1, 0x02))
        with self.assertRaisesRegex(syx.SyxError, "command"):
            syx.decode(_corrupt(raw, 3, 0x02))

    def test_decode_rejects_missing_7a(self):
        raw = MAIN.read_bytes()
        with self.assertRaisesRegex(syx.SyxError, "7A"):
            syx.decode(_corrupt(raw, 4, 0x7B))

    def test_decode_rejects_length_inconsistent_with_counts(self):
        raw = MAIN.read_bytes()
        truncated = raw[:-10] + raw[-3:]  # drop 7 payload bytes, keep trailer + F7
        with self.assertRaisesRegex(syx.SyxError, "length"):
            syx.decode(truncated)

    def test_decode_rejects_bad_trailer(self):
        raw = MAIN.read_bytes()
        with self.assertRaisesRegex(syx.SyxError, "trailer"):
            syx.decode(_corrupt(raw, len(raw) - 2, (raw[-2] + 1) & 0x7F))

    def test_encode_rejects_unknown_target(self):
        with self.assertRaises(ValueError):
            syx.encode(b"\x00" * 16, "nope")


class RecordStreamTests(unittest.TestCase):
    def test_main_fixture_has_two_images_with_known_facts(self):
        payload = syx.decode(MAIN.read_bytes()).payload
        images = records.parse_images(payload)
        self.assertEqual([im.family for im in images], [0xAD, 0xAC])
        self.assertEqual([im.entry for im in images], [0x2002E001, 0x001C0AFC])
        self.assertEqual([im.declared_len for im in images], [0x20FA4, 0x1297C])
        self.assertEqual([len(im.records) for im in images], [35, 33])  # EXEC record included
        self.assertEqual(images[-1].end, len(payload))

    def test_record_types_and_payload_rules(self):
        payload = syx.decode(MAIN.read_bytes()).payload
        image_a = records.parse_images(payload)[0]
        first = image_a.records[0]
        self.assertEqual(first.type, records.EXEC)
        self.assertEqual(first.w2, 0)
        copies = [r for r in image_a.records if r.type == records.COPY]
        fills = [r for r in image_a.records if r.type == records.FILL]
        self.assertTrue(copies and fills)
        self.assertTrue(all(len(r.payload) == r.w2 and r.w3 == 0 for r in copies))
        self.assertTrue(all(r.payload == b"" for r in fills))
        # the stock image's first COPY loads the startup code at the entry address
        self.assertEqual((copies[0].w1, copies[0].w2), (0x2002E000, 0xEDC))
        # the SHARC image ends with a START record at its entry; the ARM image has none
        image_b = records.parse_images(payload)[1]
        last = image_b.records[-1]
        self.assertEqual((last.type, last.w1, last.payload), (records.START, image_b.entry, b""))
        self.assertFalse(any(r.type == records.START for r in image_a.records))

    def test_record_bytes_xor_to_zero_and_serialize_round_trips(self):
        rec = records.record_bytes(records.COPY, 0xAD, 0x20089800, 0x800, 0)
        self.assertEqual(len(rec), 16)
        x = 0
        for b in rec:
            x ^= b
        self.assertEqual(x, 0)
        self.assertEqual((rec[0], rec[1], rec[3]), (0x0D, records.COPY, 0xAD))
        payload = syx.decode(MAIN.read_bytes()).payload
        self.assertEqual(records.serialize_images(records.parse_images(payload)), payload)

    def test_parse_detects_record_xor_failure(self):
        payload = bytearray(syx.decode(MAIN.read_bytes()).payload)
        payload[0x10 + 2] ^= 0x01  # check byte of the second record
        with self.assertRaisesRegex(records.RecordError, "XOR"):
            records.parse_images(bytes(payload))

    def test_parse_detects_declared_length_mismatch(self):
        payload = bytearray(syx.decode(MAIN.read_bytes()).payload)
        payload[0x0C] ^= 0x10  # low byte of image A's declared length (w3)
        payload[0x02] ^= 0x10  # keep the XOR check valid so only the length is wrong
        with self.assertRaisesRegex(records.RecordError, "length"):
            records.parse_images(bytes(payload))


class CliTests(unittest.TestCase):
    def test_inspect_reports_target_counts_trailer_and_images(self):
        r = _cli("inspect", MAIN)
        self.assertEqual(r.returncode, 0, r.stderr)
        out = r.stdout
        for needle in ("target: main", "groups: 30180", "tail: 4", "trailer: ok",
                       "family: AD", "family: AC", "entry: 0x2002E001", "entry: 0x001C0AFC",
                       "records: 35", "records: 33", "0x20010000"):
            self.assertIn(needle, out)

    def test_inspect_reports_bad_trailer(self):
        raw = MAIN.read_bytes()
        bad = _corrupt(raw, len(raw) - 2, (raw[-2] + 1) & 0x7F)
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "bad.syx"
            p.write_bytes(bad)
            r = _cli("inspect", p)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("trailer: BAD expected 1d 7d", r.stdout)

    def test_unpack_then_pack_reproduces_fixture(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "out"
            r = _cli("unpack", PANEL, out)
            self.assertEqual(r.returncode, 0, r.stderr)
            header = json.loads((out / "header.json").read_text())
            self.assertEqual(header, {"target": "panel", "groups": 2498, "tail": 2,
                                      "trailer": [0x67, 0x24]})
            payload = (out / "payload.bin").read_bytes()
            self.assertEqual(len(payload), 7 * 2498 + 2)
            r = _cli("pack", out / "payload.bin", Path(d) / "re.syx", "--target", "panel")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual((Path(d) / "re.syx").read_bytes(), PANEL.read_bytes())

    def test_unpack_rejects_corrupt_input_without_writing(self):
        raw = MAIN.read_bytes()
        with tempfile.TemporaryDirectory() as d:
            bad = Path(d) / "bad.syx"
            bad.write_bytes(_corrupt(raw, len(raw) - 2, (raw[-2] + 1) & 0x7F))
            r = _cli("unpack", bad, Path(d) / "out")
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("trailer", r.stderr)
            self.assertFalse((Path(d) / "out").exists())


if __name__ == "__main__":
    unittest.main()
