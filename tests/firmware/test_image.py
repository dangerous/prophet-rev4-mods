"""Structural acceptance tests on the real built image (docs/SPEC.md: "Safety invariants",
"Re-latch under HOLD" wrapper facts). Builds the wrapper with the cross-compiler and the
image with the tools, then checks everything the spec promises about the bytes."""
import json
import struct
import unittest
from pathlib import Path

from tools import build, fw, records, syx, thumb

ROOT = Path(__file__).resolve().parents[2]
V5 = ROOT / "fixtures" / "V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx"
HOOKS = ROOT / "firmware" / "hooks.json"
OUT = ROOT / "build" / "test-image"

V5_ENTRIES = {
    "local_note": 0x20088C51, "midi_note_on": 0x20088CFD, "midi_note_off": 0x20088D2D,
    "hold": 0x20089081, "all_notes_off": 0x20088E83,
    "kbd_scan": 0x20088D53, "button": 0x20088EAB, "init_guard": 0x20088C81,
    "orig_out": 0x20089061, "hold_event": 0x20088F37,
}
ARP_ENABLED_BYTE = 0x200894D8
V5_DATA = {
    "octaves": 0x200894DF, "out_ptr": 0x200894F0, "globals_flag": 0x200895E5,
}
STOCK_DISPLAY_INT = 0x20037FF7
STATE_BASE = 0x20089E80
WINDOW_LO, WINDOW_HI = 0x20089600, 0x2008A000


class V5FactTests(unittest.TestCase):
    """The addresses the wrapper hard-codes must be what the V5 fixture actually contains."""

    def setUp(self):
        self.payload = syx.decode(V5.read_bytes()).payload

    def test_hook_sites_branch_to_the_v5_entry_points(self):
        for h in json.loads(HOOKS.read_text()):
            site, expect = int(h["site"], 16), int(h["expect"], 16)
            self.assertEqual(thumb.decode_bl(site, build.read_ram(self.payload, site, 4)), expect & ~1)

    def test_v5_entry_points_start_with_a_push(self):
        for name, addr in V5_ENTRIES.items():
            hw = struct.unpack("<H", build.read_ram(self.payload, addr & ~1, 2))[0]
            if name == "orig_out":   # a bare trampoline: movw r12, #0xe95d
                self.assertTrue(thumb.is_movw(hw), "%s @0x%08X: %04x" % (name, addr, hw))
                continue
            self.assertTrue((hw & 0xFE00) == 0xB400 or hw == 0xE92D,   # push / push.w
                            "%s @0x%08X: %04x" % (name, addr, hw))

    def test_arp_hold_flag_is_reset_by_its_clear(self):
        # hold handler: strb.w r1, [r0, #0x302]; clear handler: strh.w r9(=0), [r4, #0x301]
        self.assertEqual(build.read_ram(self.payload, 0x2008863C, 4), bytes.fromhex("80f80213"))
        self.assertEqual(build.read_ram(self.payload, 0x200886BC, 4), bytes.fromhex("a4f80193"))
        # the hold-event entry: push {r4, lr}; mov r4, r0; bl guard; ... enqueues event 5
        self.assertEqual(build.read_ram(self.payload, 0x20088F36, 4), bytes.fromhex("10b50446"))

    def test_stock_button_gate_passes_values_1_to_3(self):
        # 0x20036114: subs r3, r1, #1 ; push {r4, lr} ; cmp r3, #2 ; bhi -> reject
        self.assertEqual(build.read_ram(self.payload, 0x20036114, 8), bytes.fromhex("4b1e10b5022b19d8"))
        # V5's button hook only acts on values 1 and 2: subs r1, r5, #1 ... cmp r1, #1 ; bhi
        self.assertEqual(build.read_ram(self.payload, 0x20088EC2, 4), bytes.fromhex("012907d8"))

    def test_hold_hook_forwards_r4_and_calls_stock_post(self):
        code = build.read_ram(self.payload, 0x20089080, 0x14)
        # push {r4, lr}; ldr r3,[pc,#0xc]; blx r3; mov r0, r4; bl ...; pop {r4, pc}; ...; literal
        self.assertEqual(code[:4], bytes.fromhex("10b5034b"))
        self.assertEqual(code[4:8], bytes.fromhex("98472046"))
        self.assertEqual(struct.unpack_from("<I", code, 0x10)[0], 0x2003D325)

    def test_arp_enabled_byte_is_engine_offset_0x300(self):
        # init: movw r6,#0x91d0 ; movt r6,#0x2008 ; engine = r6 + 8 ; enabled = engine + 0x300
        self.assertEqual(ARP_ENABLED_BYTE, 0x200891D0 + 8 + 0x300)
        code = build.read_ram(self.payload, 0x20088C84, 8)
        hw = struct.unpack("<4H", code)
        self.assertTrue(thumb.is_movw(hw[0]) and thumb.is_movt(hw[2]))
        self.assertEqual(((hw[1] >> 8) & 0xF, (hw[3] >> 8) & 0xF), (6, 6))          # r6
        self.assertEqual((thumb.movw_imm16(hw[0], hw[1]), thumb.movw_imm16(hw[2], hw[3])),
                         (0x91D0, 0x2008))

    def test_window_tail_is_zero_in_v5(self):
        self.assertEqual(build.read_ram(self.payload, WINDOW_LO, WINDOW_HI - WINDOW_LO),
                         bytes(WINDOW_HI - WINDOW_LO))


class BuiltImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)
        cls.wbin, cls.wmap = fw.build_wrapper(ROOT / "firmware", OUT)
        cls.out = OUT / "image.syx"
        build.build_image(V5, cls.wbin, cls.wmap, HOOKS, cls.out)
        cls.base = syx.decode(V5.read_bytes()).payload
        cls.img = syx.decode(cls.out.read_bytes()).payload
        cls.symbols = build.parse_map(cls.wmap.read_text())
        cls.wrapper = cls.wbin.read_bytes()

    def test_output_is_a_valid_main_os_file(self):
        c = syx.decode(self.out.read_bytes())
        self.assertEqual(c.target, "main")
        self.assertEqual(syx.trailer_for(c.payload), c.trailer)

    def test_invariants_sharc_identical_structure_identical_startup_identical(self):
        bi, oi = records.parse_images(self.base), records.parse_images(self.img)
        self.assertEqual(records.serialize_images([bi[1]]), records.serialize_images([oi[1]]))
        self.assertEqual([(r.type, r.w1, r.w2, r.w3) for r in bi[0].records],
                         [(r.type, r.w1, r.w2, r.w3) for r in oi[0].records])
        self.assertEqual(bi[0].entry, oi[0].entry)
        self.assertEqual(build.read_ram(self.base, 0x2002E000, 0xEDC),
                         build.read_ram(self.img, 0x2002E000, 0xEDC))

    def test_only_hook_sites_and_window_differ(self):
        sites = [int(h["site"], 16) for h in json.loads(HOOKS.read_text())]
        for s in build.image_diff(self.base, self.img):
            in_site = any(site <= s.ram_lo and s.ram_hi <= site + 4 for site in sites)
            in_window = WINDOW_LO <= s.ram_lo and s.ram_hi <= WINDOW_HI
            self.assertTrue(in_site or in_window, s)

    def test_hooks_land_on_wrapper_symbols_inside_the_window(self):
        for h in json.loads(HOOKS.read_text()):
            site = int(h["site"], 16)
            target = thumb.decode_bl(site, build.read_ram(self.img, site, 4))
            self.assertEqual(target, self.symbols[h["symbol"]] & ~1)
            self.assertTrue(WINDOW_LO <= target < STATE_BASE)

    def test_wrapper_fits_below_its_state_and_is_placed_verbatim(self):
        self.assertLessEqual(WINDOW_LO + len(self.wrapper), STATE_BASE)
        self.assertEqual(build.read_ram(self.img, WINDOW_LO, len(self.wrapper)), self.wrapper)
        self.assertEqual(build.read_ram(self.img, STATE_BASE, WINDOW_HI - STATE_BASE),
                         bytes(WINDOW_HI - STATE_BASE))

    # firmware/v5_iface.h order
    IFACE = [V5_ENTRIES["local_note"], V5_ENTRIES["midi_note_on"], V5_ENTRIES["midi_note_off"],
             V5_ENTRIES["all_notes_off"], V5_ENTRIES["kbd_scan"], V5_ENTRIES["button"],
             V5_ENTRIES["init_guard"], V5_ENTRIES["orig_out"], 0x20089081,
             ARP_ENABLED_BYTE, V5_DATA["octaves"], V5_DATA["out_ptr"], V5_DATA["globals_flag"],
             STOCK_DISPLAY_INT, V5_ENTRIES["hold_event"]]

    def test_interface_table_is_exactly_the_known_addresses(self):
        table = self.symbols["v5_iface"] & ~1
        self.assertTrue(WINDOW_LO <= table < STATE_BASE)
        words = struct.unpack_from("<%dI" % len(self.IFACE), self.wrapper, table - WINDOW_LO)
        self.assertEqual([hex(w) for w in words], [hex(a) for a in self.IFACE])
        # and the word after the table is not another address into V5/stock (table is complete)
        after = struct.unpack_from("<I", self.wrapper + b"\0" * 4, table - WINDOW_LO + 4 * len(self.IFACE))[0]
        self.assertNotIn(after, set(self.IFACE))

    def test_wrapper_materialises_no_unknown_addresses(self):
        refs = thumb.find_absolute_addresses(self.wrapper, WINDOW_LO)
        allowed = set(self.IFACE)
        allowed |= set(range(STATE_BASE, WINDOW_HI))      # its own state
        allowed |= set(range(WINDOW_LO, STATE_BASE))      # its own code and table
        unknown = sorted(a for a in refs if a not in allowed)
        self.assertEqual(unknown, [], [hex(a) for a in unknown])
        self.assertIn(self.symbols["v5_iface"] & ~1, refs)   # the table itself is referenced

    def test_v5_output_pointer_facts(self):
        # init stores the output fn (movw/movt #0x9061/#0x2008 into r1) and the engine is
        # state+8, so the pointer lives at engine+0x318 and its ctx at engine+0x31c.
        code = build.read_ram(self.base, 0x20088CA4, 8)
        hw = struct.unpack("<4H", code)
        self.assertEqual((thumb.movw_imm16(hw[0], hw[1]), thumb.movw_imm16(hw[2], hw[3])), (0x9061, 0x2008))
        self.assertEqual(V5_DATA["out_ptr"], 0x200891D0 + 8 + 0x318)
        self.assertEqual(V5_DATA["octaves"], 0x200891D0 + 8 + 0x307)
        # octave handler stores its argument at engine+0x307: strb.w r1, [r4, #0x307]
        self.assertEqual(build.read_ram(self.base, 0x2008826E, 4), bytes.fromhex("84f80713"))

    def test_wrapper_has_no_data_sections_and_contains_no_privileged_instructions(self):
        info = fw.inspect_object(OUT / "wrapper.o")
        self.assertEqual(info["data_size"] + info["bss_size"], 0)
        for hw_pattern in ("cpsid", "cpsie", "mcr", "mrc", "svc", "msr"):
            self.assertNotIn(hw_pattern, info["mnemonics"], hw_pattern)
