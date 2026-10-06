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

WINDOW_LO, WINDOW_HI = 0x20089600, 0x2008A000
STUB_SYMBOLS = {
    "hook_local_note": 0x20089601,
    "hook_midi_note_on": 0x20089611,
    "hook_midi_note_off": 0x20089621,
    "hook_hold": 0x20089631,
}
# four 16-byte "functions": recognisable filler ending in bx lr (70 47)
STUB_BIN = b"".join(bytes([0x00, 0xBF]) * 7 + bytes([0x70, 0x47]) for _ in range(4))
HOOKS = [
    {"site": "0x2003BECC", "expect": "0x20088C51", "symbol": "hook_local_note"},
    {"site": "0x2003B07A", "expect": "0x20088CFD", "symbol": "hook_midi_note_on"},
    {"site": "0x2003B032", "expect": "0x20088D2D", "symbol": "hook_midi_note_off"},
    {"site": "0x200396CA", "expect": "0x20089081", "symbol": "hook_hold"},
]


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

    def test_output_decodes_with_valid_trailer_and_same_structure(self):
        base, out = self._build()
        c = syx.decode(self.out.read_bytes())  # raises on bad trailer
        self.assertEqual(c.target, "main")
        bi, oi = records.parse_images(base), records.parse_images(out)
        self.assertEqual(len(bi), len(oi))
        for b, o in zip(bi, oi):
            self.assertEqual((b.family, b.entry, b.declared_len), (o.family, o.entry, o.declared_len))
            self.assertEqual([(r.type, r.w1, r.w2, r.w3) for r in b.records],
                             [(r.type, r.w1, r.w2, r.w3) for r in o.records])

    def test_sharc_image_and_startup_record_are_identical(self):
        base, out = self._build()
        bi, oi = records.parse_images(base), records.parse_images(out)
        self.assertEqual(records.serialize_images([bi[1]]), records.serialize_images([oi[1]]))
        startup_b = next(r for r in bi[0].records if r.type == records.COPY and r.w1 == 0x2002E000)
        startup_o = next(r for r in oi[0].records if r.type == records.COPY and r.w1 == 0x2002E000)
        self.assertEqual((startup_b.w2, startup_b.payload), (0xEDC, startup_o.payload))

    def test_hook_sites_now_branch_to_wrapper_symbols(self):
        base, out = self._build()
        for h in HOOKS:
            site = int(h["site"], 16)
            before = build.read_ram(base, site, 4)
            after = build.read_ram(out, site, 4)
            self.assertEqual(thumb.decode_bl(site, before), int(h["expect"], 16) & ~1)
            self.assertEqual(thumb.decode_bl(site, after), STUB_SYMBOLS[h["symbol"]] & ~1)

    def test_wrapper_bytes_land_in_window_and_nothing_else_changes(self):
        base, out = self._build()
        self.assertEqual(build.read_ram(out, WINDOW_LO, len(STUB_BIN)), STUB_BIN)
        spans = build.image_diff(base, out)
        self.assertTrue(all(s.image == 0 for s in spans))
        sites = [int(h["site"], 16) for h in HOOKS]
        window_spans = [s for s in spans if WINDOW_LO <= s.ram_lo and s.ram_hi <= WINDOW_HI]
        site_spans = [s for s in spans if s not in window_spans]
        # every non-window change lies within the 4-byte BL of some hook site ...
        for s in site_spans:
            self.assertTrue(any(site <= s.ram_lo and s.ram_hi <= site + 4 for site in sites), s)
        # ... every site changed (our stub targets differ from the V5 targets) ...
        for site in sites:
            self.assertTrue(any(site <= s.ram_lo and s.ram_hi <= site + 4 for s in site_spans), hex(site))
        # ... and the window change is exactly the stub
        self.assertEqual([(s.ram_lo, s.ram_hi) for s in window_spans],
                         [(WINDOW_LO, WINDOW_LO + len(STUB_BIN))])

    def test_build_fails_when_site_is_not_a_bl_to_expected_target(self):
        hooks = json.loads(self.hooks.read_text())
        hooks[0]["expect"] = "0x20088C01"
        self.hooks.write_text(json.dumps(hooks))
        with self.assertRaisesRegex(build.BuildError, "expected"):
            self._build()
        self.assertFalse(self.out.exists())

    def test_build_fails_when_site_outside_stock_code_record(self):
        hooks = json.loads(self.hooks.read_text())
        hooks[0]["site"] = "0x20088C50"  # inside V5's blob, not the stock code record
        self.hooks.write_text(json.dumps(hooks))
        with self.assertRaisesRegex(build.BuildError, "stock code record"):
            self._build()

    def test_build_fails_when_symbol_missing(self):
        self.wmap.write_text("20089601 something_else\n")
        with self.assertRaisesRegex(build.BuildError, "symbol"):
            self._build()

    def test_build_fails_when_wrapper_too_big_or_window_not_free(self):
        self.wbin.write_bytes(b"\x00" * (WINDOW_HI - WINDOW_LO + 1))
        with self.assertRaisesRegex(build.BuildError, "window"):
            self._build()
        self.wbin.write_bytes(STUB_BIN)
        # dirty the window in the base
        payload = bytearray(syx.decode(V5.read_bytes()).payload)
        off = build.ram_to_payload_offset(bytes(payload), WINDOW_LO + 0x100)
        payload[off] = 0x5A
        self.base.write_bytes(syx.encode(bytes(payload), "main"))
        with self.assertRaisesRegex(build.BuildError, "not free"):
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
            # the HOLD site keeps its first halfword (same imm10), so only 2 bytes change
            self.assertIn("ram 0x200396CC..0x200396CE: 2 bytes", r.stdout)
            self.assertIn("ram 0x20089600..0x20089640: 64 bytes", r.stdout)
            self.assertEqual(r.stdout.count("\n"), 5)  # 4 sites + window, nothing else

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
