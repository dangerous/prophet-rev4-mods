"""Acceptance tests for image patching and the safety invariants (docs/SPEC.md: "Image
patching" and "Safety invariants"). Uses a stub wrapper binary so the mechanics are tested
independently of the real firmware."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools import build, records, syx, thumb

ROOT = Path(__file__).resolve().parents[2]
V5 = ROOT / "fixtures" / "V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx"

REC_BASE, REC_SIZE = 0x2008A000, 0x2000
STUB_SYMBOLS = {
    "hook_local_note": 0x2008A001,
    "hook_midi_note_on": 0x2008A011,
    "hook_midi_note_off": 0x2008A021,
    "hook_hold": 0x2008A031,
    "stub_tramp": 0x2008A041,
}
# five 16-byte "functions": recognisable filler ending in bx lr (70 47)
STUB_BIN = b"".join(bytes([0x00, 0xBF]) * 7 + bytes([0x70, 0x47]) for _ in range(5))
HOOKS = [
    {"site": "0x2003BECC", "expect": "0x20088C51", "symbol": "hook_local_note"},
    {"site": "0x2003B07A", "expect": "0x20088CFD", "symbol": "hook_midi_note_on"},
    {"site": "0x2003B032", "expect": "0x20088D2D", "symbol": "hook_midi_note_off"},
    {"site": "0x200396CA", "expect": "0x20089081", "symbol": "hook_hold"},
    # V5's MIDI-parser table entry 1 (of 0,2 stock / 1,3,4,5 V5): a word, not a BL
    {"kind": "word", "site": "0x200343D8", "expect": "0x20089095", "symbol": "stub_tramp"},
]
BL_SITES = [int(h["site"], 16) for h in HOOKS if h.get("kind", "bl") == "bl"]
WORD_SITES = [int(h["site"], 16) for h in HOOKS if h.get("kind") == "word"]


def _write_inputs(d: Path, binary=STUB_BIN, symbols=STUB_SYMBOLS, hooks=HOOKS, base=None):
    (d / "w.bin").write_bytes(binary)
    (d / "w.map").write_text("".join("%08x %s\n" % (a, s) for s, a in symbols.items()))
    (d / "hooks.json").write_text(json.dumps(hooks))
    base_path = d / "base.syx"
    base_path.write_bytes(base if base is not None else V5.read_bytes())
    return base_path, d / "w.bin", d / "w.map", d / "hooks.json"


def _cli(*args):
    return subprocess.run([sys.executable, "-m", "tools", *map(str, args)],
                          cwd=ROOT, capture_output=True, text=True)


class ThumbBlTests(unittest.TestCase):
    def test_encode_matches_stock_instruction(self):
        # stock 0x200396CA: f0 03 fe 2b = bl 0x2003D324
        self.assertEqual(thumb.encode_bl(0x200396CA, 0x2003D324), bytes.fromhex("03f02bfe"))
        self.assertEqual(thumb.decode_bl(0x200396CA, bytes.fromhex("03f02bfe")), 0x2003D324)

    def test_encode_matches_v5_retarget(self):
        # V5 0x200396CA: 4f f0 d9 fc = bl 0x20089080
        self.assertEqual(thumb.encode_bl(0x200396CA, 0x20089080), bytes.fromhex("4ff0d9fc"))
        self.assertEqual(thumb.decode_bl(0x200396CA, bytes.fromhex("4ff0d9fc")), 0x20089080)

    def test_backward_branch_and_range(self):
        self.assertEqual(thumb.decode_bl(0x2003C244, bytes.fromhex("fff7f4fc")), 0x2003BC30)
        self.assertEqual(thumb.encode_bl(0x2003C244, 0x2003BC30), bytes.fromhex("fff7f4fc"))
        thumb.encode_bl(0x20000000, 0x20FFFFFC)  # just inside +16 MB
        with self.assertRaises(thumb.RangeError):
            thumb.encode_bl(0x20000000, 0x22000000)
        # the Thumb bit on a target is accepted and ignored
        self.assertEqual(thumb.encode_bl(0x20000000, 0x20000009), thumb.encode_bl(0x20000000, 0x20000008))
        self.assertIsNone(thumb.decode_bl(0x20000000, bytes.fromhex("00bf00bf")))  # not a BL


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        self.base, self.wbin, self.wmap, self.hooks = _write_inputs(self.d)
        self.out = self.d / "out.syx"

    def tearDown(self):
        self.tmp.cleanup()

    def _build(self):
        build.build_image(self.base, self.wbin, self.wmap, self.hooks, self.out)
        return syx.decode(self.base.read_bytes()).payload, syx.decode(self.out.read_bytes()).payload

    def test_output_is_base_structure_plus_one_appended_record(self):
        base, out = self._build()
        c = syx.decode(self.out.read_bytes())  # raises on bad trailer
        self.assertEqual(c.target, "main")
        bi, oi = records.parse_images(base), records.parse_images(out)
        self.assertEqual(len(bi), len(oi))
        a_b, a_o = bi[0], oi[0]
        self.assertEqual(len(a_o.records), len(a_b.records) + 1)
        self.assertEqual([(r.type, r.w1, r.w2, r.w3) for r in a_b.records[1:]],
                         [(r.type, r.w1, r.w2, r.w3) for r in a_o.records[1:-1]])
        extra = a_o.records[-1]
        self.assertEqual((extra.type, extra.w1, extra.w2, extra.w3), (records.COPY, REC_BASE, REC_SIZE, 0))
        self.assertEqual((a_o.entry, a_o.declared_len), (a_b.entry, a_b.declared_len + 16 + REC_SIZE))
        self.assertEqual(records.serialize_images([bi[1]]), records.serialize_images([oi[1]]))

    def test_startup_record_and_v5_window_are_identical(self):
        base, out = self._build()
        self.assertEqual(build.read_ram(base, 0x2002E000, 0xEDC), build.read_ram(out, 0x2002E000, 0xEDC))
        self.assertEqual(build.read_ram(base, 0x20088000, 0x2000), build.read_ram(out, 0x20088000, 0x2000))

    def test_bl_sites_branch_to_wrapper_symbols(self):
        base, out = self._build()
        for h in HOOKS:
            if h.get("kind", "bl") != "bl":
                continue
            site = int(h["site"], 16)
            self.assertEqual(thumb.decode_bl(site, build.read_ram(base, site, 4)), int(h["expect"], 16) & ~1)
            self.assertEqual(thumb.decode_bl(site, build.read_ram(out, site, 4)), STUB_SYMBOLS[h["symbol"]] & ~1)

    def test_word_sites_hold_wrapper_symbol_addresses(self):
        base, out = self._build()
        import struct
        for h in HOOKS:
            if h.get("kind") != "word":
                continue
            site = int(h["site"], 16)
            self.assertEqual(struct.unpack("<I", build.read_ram(base, site, 4))[0], int(h["expect"], 16))
            self.assertEqual(struct.unpack("<I", build.read_ram(out, site, 4))[0], STUB_SYMBOLS[h["symbol"]])

    def test_wrapper_lands_in_the_new_record_and_nothing_else_changes(self):
        base, out = self._build()
        self.assertEqual(build.read_ram(out, REC_BASE, len(STUB_BIN)), STUB_BIN)
        self.assertEqual(build.read_ram(out, REC_BASE + len(STUB_BIN), REC_SIZE - len(STUB_BIN)),
                         bytes(REC_SIZE - len(STUB_BIN)))
        spans = build.image_diff(base, out)
        self.assertTrue(all(s.image == 0 for s in spans))
        appended = [s for s in spans if s.appended]
        self.assertEqual([(s.ram_lo, s.ram_hi) for s in appended], [(REC_BASE, REC_BASE + REC_SIZE)])
        for s in spans:
            if s.appended:
                continue
            self.assertTrue(any(site <= s.ram_lo and s.ram_hi <= site + 4 for site in BL_SITES + WORD_SITES), s)
        for site in BL_SITES + WORD_SITES:
            self.assertTrue(any(not s.appended and site <= s.ram_lo and s.ram_hi <= site + 4 for s in spans), hex(site))

    def test_build_fails_when_site_is_not_what_the_hook_list_says(self):
        hooks = json.loads(self.hooks.read_text())
        hooks[0]["expect"] = "0x20088C01"
        self.hooks.write_text(json.dumps(hooks))
        with self.assertRaisesRegex(build.BuildError, "expected"):
            self._build()
        self.assertFalse(self.out.exists())
        hooks = json.loads(self.hooks.read_text())
        hooks[0]["expect"] = "0x20088C51"
        hooks[4]["expect"] = "0x20089094"
        self.hooks.write_text(json.dumps(hooks))
        with self.assertRaisesRegex(build.BuildError, "expected"):
            self._build()

    def test_build_fails_when_site_outside_stock_code_record(self):
        hooks = json.loads(self.hooks.read_text())
        hooks[0]["site"] = "0x20088C50"  # inside V5's blob, not the stock code record
        self.hooks.write_text(json.dumps(hooks))
        with self.assertRaisesRegex(build.BuildError, "stock code record"):
            self._build()

    def test_build_fails_when_symbol_missing(self):
        self.wmap.write_text("2008a001 something_else\n")
        with self.assertRaisesRegex(build.BuildError, "symbol"):
            self._build()

    def test_build_fails_when_wrapper_too_big_or_record_already_present(self):
        self.wbin.write_bytes(b"\x00" * (REC_SIZE + 1))
        with self.assertRaisesRegex(build.BuildError, "exceeds"):
            self._build()
        self.wbin.write_bytes(STUB_BIN)
        self._build()                      # a built image as base: record already present
        self.base.write_bytes(self.out.read_bytes())
        self.out.unlink()
        with self.assertRaisesRegex(build.BuildError, "already"):
            self._build()


class CliTests(unittest.TestCase):
    def test_build_and_diff_cli(self):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            base, wbin, wmap, hooks = _write_inputs(d)
            out = d / "out.syx"
            r = _cli("build", "--base", base, "--wrapper", wbin, "--map", wmap,
                     "--hooks", hooks, "-o", out)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertTrue(out.exists())
            r = _cli("diff", base, out)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("ram 0x2003BECC..0x2003BED0: 4 bytes", r.stdout)
            self.assertIn("ram 0x200343D8..0x200343DA: 2 bytes", r.stdout)
            self.assertIn("appended record ram 0x2008A000..0x2008C000: 8192 bytes", r.stdout)
            self.assertEqual(r.stdout.count("\n"), 6)  # 4 BL sites + 1 word + appended record

    def test_build_cli_reports_error_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            base, wbin, wmap, hooks = _write_inputs(d)
            wmap.write_text("")
            out = d / "out.syx"
            r = _cli("build", "--base", base, "--wrapper", wbin, "--map", wmap,
                     "--hooks", hooks, "-o", out)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("symbol", r.stderr)
            self.assertFalse(out.exists())
