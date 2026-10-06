"""Structural acceptance tests on the real built image (docs/SPEC.md: "Safety invariants",
"Re-latch under HOLD" wrapper facts). Builds the wrapper with the cross-compiler and the
image with the tools, then checks everything the spec promises about the bytes."""
import json
import struct
import unittest
from pathlib import Path

from tests import fixture_check
from tools import build, fw, records, syx, thumb

ROOT = Path(__file__).resolve().parents[2]
V5 = ROOT / "fixtures" / "V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx"
STOCK = ROOT / "fixtures" / "prophet5_main_2.1.0.syx"


def setUpModule():
    fixture_check.require([V5.name, STOCK.name])
HOOKS = ROOT / "firmware" / "hooks.json"
OUT = ROOT / "build" / "test-image"

V5_ENTRIES = {
    "local_note": 0x20088C51, "midi_note_on": 0x20088CFD, "midi_note_off": 0x20088D2D,
    "hold": 0x20089081, "all_notes_off": 0x20088E83,
    "kbd_scan": 0x20088D53, "button": 0x20088EAB, "init_guard": 0x20088C81,
    "orig_out": 0x20089061, "hold_event": 0x20088F37, "rt_sniff": 0x20088F59,
    "stock_midi_out": 0x2003BCE1,
}
ARP_ENABLED_BYTE = 0x200894D8
V5_DATA = {
    "octaves": 0x200894DF, "out_ptr": 0x200894F0, "globals_flag": 0x200895E5,
    "div": 0x200894E2, "tps": 0x200894E4, "acc": 0x200894E8, "ext_clock": 0x200894F8,
    "clock_loss": 0x200894FC, "display_timer": 0x200895E8, "a440_used": 0x20089508,
}
STOCK_DISPLAY_INT = 0x20037FF7
STOCK_DISPLAY3 = 0x20037F25
STOCK_PARSER_STATE = 0x20034343
CODE_BASE = 0x2008A000          # appended wrapper record
STATE_BASE = 0x2008B800
RECORD_HI = 0x2008C000
V5_WINDOW_LO, V5_WINDOW_HI = 0x20088000, 0x2008A000


class V5FactTests(unittest.TestCase):
    """The addresses the wrapper hard-codes must be what the V5 fixture actually contains."""

    def setUp(self):
        self.payload = syx.decode(V5.read_bytes()).payload

    def test_hook_sites_hold_the_v5_entry_points(self):
        for h in json.loads(HOOKS.read_text()):
            site, expect = int(h["site"], 16), int(h["expect"], 16)
            raw = build.read_ram(self.payload, site, 4)
            if h.get("kind", "bl") == "word":
                self.assertEqual(struct.unpack("<I", raw)[0], expect)
            else:
                self.assertEqual(thumb.decode_bl(site, raw), expect & ~1)

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

    def test_mmu_region_table_maps_the_wrapper_record_range(self):
        # stock .data: (start, end, attrs) rows; 0x20020000-0x2008FFFF shares V5's attributes
        stock = syx.decode(STOCK.read_bytes()).payload
        rows = struct.unpack("<9I", build.read_ram(stock, 0x2004DD38, 36))
        self.assertEqual(rows[0:3], (0x20010000, 0x2001FFFF, 0x00001C00))
        self.assertEqual(rows[3:6], (0x20020000, 0x2008FFFF, 0x00005C04))
        self.assertEqual(rows[6:9], (0x20090000, 0x200FFFFF, 0x0000DC04))
        self.assertTrue(0x20020000 <= CODE_BASE and RECORD_HI - 1 <= 0x2008FFFF)


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

    def test_invariants_sharc_identical_structure_plus_one_record_startup_identical(self):
        bi, oi = records.parse_images(self.base), records.parse_images(self.img)
        self.assertEqual(records.serialize_images([bi[1]]), records.serialize_images([oi[1]]))
        self.assertEqual([(r.type, r.w1, r.w2, r.w3) for r in bi[0].records[1:]],
                         [(r.type, r.w1, r.w2, r.w3) for r in oi[0].records[1:-1]])
        extra = oi[0].records[-1]
        self.assertEqual((extra.type, extra.w1, extra.w2), (records.COPY, CODE_BASE, RECORD_HI - CODE_BASE))
        self.assertEqual((bi[0].entry, bi[0].declared_len + 16 + extra.w2), (oi[0].entry, oi[0].declared_len))
        self.assertEqual(build.read_ram(self.base, 0x2002E000, 0xEDC),
                         build.read_ram(self.img, 0x2002E000, 0xEDC))
        self.assertEqual(build.read_ram(self.base, V5_WINDOW_LO, V5_WINDOW_HI - V5_WINDOW_LO),
                         build.read_ram(self.img, V5_WINDOW_LO, V5_WINDOW_HI - V5_WINDOW_LO))

    def test_only_hook_sites_and_the_appended_record_differ(self):
        sites = [int(h["site"], 16) for h in json.loads(HOOKS.read_text())]
        allowed = {site + k for site in sites for k in range(4)}
        bi, oi = records.parse_images(self.base), records.parse_images(self.img)
        for rb, ro in zip(bi[0].records, oi[0].records[:-1]):   # the last output record is the wrapper
            if rb.type != records.COPY:
                continue
            changed = {rb.w1 + i for i in range(len(rb.payload)) if rb.payload[i] != ro.payload[i]}
            self.assertTrue(changed <= allowed, sorted(hex(a) for a in changed - allowed)[:8])
        spans = build.image_diff(self.base, self.img)
        self.assertEqual([(s.ram_lo, s.ram_hi) for s in spans if s.appended], [(CODE_BASE, RECORD_HI)])

    def test_hooks_land_on_wrapper_symbols_inside_the_record(self):
        for h in json.loads(HOOKS.read_text()):
            site = int(h["site"], 16)
            if h.get("kind", "bl") == "word":
                target = struct.unpack("<I", build.read_ram(self.img, site, 4))[0] & ~1
            else:
                target = thumb.decode_bl(site, build.read_ram(self.img, site, 4))
            self.assertEqual(target, self.symbols[h["symbol"]] & ~1)
            self.assertTrue(CODE_BASE <= target < STATE_BASE)

    def test_wrapper_fits_below_its_state_and_is_placed_verbatim(self):
        self.assertLessEqual(CODE_BASE + len(self.wrapper), STATE_BASE)
        self.assertEqual(build.read_ram(self.img, CODE_BASE, len(self.wrapper)), self.wrapper)
        self.assertEqual(build.read_ram(self.img, STATE_BASE, RECORD_HI - STATE_BASE),
                         bytes(RECORD_HI - STATE_BASE))

    # firmware/v5_iface.h order
    IFACE = [V5_ENTRIES["local_note"], V5_ENTRIES["midi_note_on"], V5_ENTRIES["midi_note_off"],
             V5_ENTRIES["all_notes_off"], V5_ENTRIES["kbd_scan"], V5_ENTRIES["button"],
             V5_ENTRIES["init_guard"], V5_ENTRIES["orig_out"], 0x20089081,
             ARP_ENABLED_BYTE, V5_DATA["octaves"], V5_DATA["out_ptr"], V5_DATA["globals_flag"],
             STOCK_DISPLAY_INT, V5_ENTRIES["hold_event"],
             V5_ENTRIES["rt_sniff"], STOCK_PARSER_STATE, STOCK_DISPLAY3,
             V5_DATA["div"], V5_DATA["tps"], V5_DATA["acc"], V5_DATA["ext_clock"],
             V5_DATA["clock_loss"], V5_DATA["display_timer"], V5_DATA["a440_used"],
             V5_ENTRIES["stock_midi_out"]]

    def test_v5_realtime_trampoline_and_engine_timing_facts(self):
        # V5 trampoline 0x20089094: push {r0-r4,lr}; add r0,r0,#0xf0; mov r1,r4; bl sniff; pop; ldr pc,[pc]; lit
        tramp = build.read_ram(self.base, 0x20089094, 0x18)
        self.assertEqual(tramp[:4], bytes.fromhex("1fb500f1"))
        self.assertEqual(tramp[4:8], bytes.fromhex("f0002146"))
        self.assertEqual(struct.unpack_from("<I", tramp, 0x14)[0], STOCK_PARSER_STATE)
        # engine tick: ldrh r0,[r4,#0x308]; ldrh r1,[r4,#0x30a]; ldrd r2,r3,[r4,#0x30c]
        self.assertEqual(build.read_ram(self.base, 0x20088750, 12), bytes.fromhex("b4f80803b4f80a13d4e9c323"))
        # realtime handler: modulo-12 by multiply 0x1556 -> 12 clocks per step hard-coded
        self.assertEqual(build.read_ram(self.base, 0x20088922, 4), bytes.fromhex("41f25651"))
        # clock-loss counter compared with tps: ldr r0,[r4,#0x30c]; ldr r1,[r4,#0x324]
        self.assertEqual(build.read_ram(self.base, 0x20088700, 8), bytes.fromhex("d4f80c03d4f82413"))

    def test_interface_table_is_exactly_the_known_addresses(self):
        table = self.symbols["v5_iface"] & ~1
        self.assertTrue(CODE_BASE <= table < STATE_BASE)
        words = struct.unpack_from("<%dI" % len(self.IFACE), self.wrapper, table - CODE_BASE)
        self.assertEqual([hex(w) for w in words], [hex(a) for a in self.IFACE])
        # and the word after the table is not another address into V5/stock (table is complete)
        after = struct.unpack_from("<I", self.wrapper + b"\0" * 4, table - CODE_BASE + 4 * len(self.IFACE))[0]
        self.assertNotIn(after, set(self.IFACE))

    def test_wrapper_materialises_no_unknown_addresses(self):
        # scan the code only: rodata (interface table, note-value table) is data, not instructions
        layout = (OUT / "wrapper.layout").read_text()
        text = next(l.split() for l in layout.splitlines() if l.split() and l.split()[0] == ".text")
        text_size = int(text[2])
        self.assertTrue(0 < text_size <= len(self.wrapper))
        refs = thumb.find_absolute_addresses(self.wrapper[:text_size], CODE_BASE)
        allowed = set(self.IFACE)
        allowed |= set(range(STATE_BASE, RECORD_HI))      # its own state
        allowed |= set(range(CODE_BASE, STATE_BASE))      # its own code and table
        # only values that could be pointers on this SoC matter (L2 RAM and MMRs per the stock
        # MMU table); smaller literals are plain data constants the compiler pooled
        unknown = sorted(a for a in refs if 0x20000000 <= a < 0x50000000 and a not in allowed)
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

    def test_internal_clock_variant_leaves_the_parser_table_as_v5(self):
        hooks_internal = ROOT / "firmware" / "hooks_internal.json"
        full = json.loads(HOOKS.read_text())
        internal = json.loads(hooks_internal.read_text())
        self.assertEqual(internal, [h for h in full if h.get("kind", "bl") == "bl"])
        out = OUT / "image-internal.syx"
        build.build_image(V5, self.wbin, self.wmap, hooks_internal, out)
        img = syx.decode(out.read_bytes()).payload
        for h in full:
            site = int(h["site"], 16)
            if h.get("kind", "bl") == "word":
                self.assertEqual(build.read_ram(img, site, 4), build.read_ram(self.base, site, 4))
            else:
                self.assertEqual(thumb.decode_bl(site, build.read_ram(img, site, 4)), self.symbols[h["symbol"]] & ~1)
        self.assertEqual(build.read_ram(img, CODE_BASE, len(self.wrapper)), self.wrapper)
        self.assertEqual(len(records.parse_images(img)[0].records), len(records.parse_images(self.base)[0].records) + 1)

    def test_wrapper_has_no_data_sections_and_contains_no_privileged_instructions(self):
        info = fw.inspect_object(OUT / "wrapper.o")
        self.assertEqual(info["data_size"] + info["bss_size"], 0)
        for hw_pattern in ("cpsid", "cpsie", "mcr", "mrc", "svc", "msr"):
            self.assertNotIn(hw_pattern, info["mnemonics"], hw_pattern)
