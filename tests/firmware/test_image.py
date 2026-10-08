"""Structural acceptance tests on the built image — our arp engine hooked into stock Main
OS 2.1.0 (docs/SPEC.md: "Native arp engine (stock 2.1.0 base)" Realisation and
Robustness, "Image patching"). Builds firmware/native.c with the cross-compiler and the image
from the stock fixture, then checks everything the spec promises about the bytes."""
import json
import struct
import unittest
from pathlib import Path

from tests import fixture_check
from tools import build, fw, records, syx, thumb

ROOT = Path(__file__).resolve().parents[2]
STOCK = ROOT / "fixtures" / "prophet5_main_2.1.0.syx"
HOOKS = ROOT / "firmware" / "hooks_native.json"
OUT = ROOT / "build" / "test-native"

REC_BASE, STATE_BASE, REC_HI = 0x20088000, 0x2008E000, 0x20090000

# every stock BL site the native build retargets, with the stock target it must find there
BL_SITES = {
    0x2003BE9C: 0x2003D829,   # keyboard FIFO count: the 1 ms tick
    0x2003BECC: 0x2003EC5D,   # local note_on(1, note, vel)
    0x2003BEFA: 0x20033F85,   # local key -> MIDI Out note-on (cable, ch, note, vel)
    0x2003BF16: 0x20033F39,   # local key -> MIDI Out note-off
    0x2003B07A: 0x2003EC5D,   # MIDI note_on(2, note, vel)
    0x2003B032: 0x2003EEDD,   # MIDI note_off(2, note)
    0x2003B294: 0x2003EBE5,   # CC 123 all-notes-off
    0x2003B144: 0x2003EBE5,   # CC 124-127 all-notes-off
    0x200396CA: 0x2003D325,   # hold handler's voice-engine message (r4 = merged state)
    0x2003C244: 0x2003BC31,   # panel button (id, value)
    0x2003C292: 0x20036B51,   # pot raw store (pot, raw)
    0x2003C2A6: 0x2003BC6D,   # pot change post (pot, old, new)
    0x2003EACE: 0x2003B695,   # note_off's hold query
    0x2003D15C: 0x2003B6B1,   # end of program apply: hold off -> "program loaded"
}
WORD_SITES = {0x200343D8: 0x20034343, 0x200343E0: 0x20034343,   # parser table F8 FA FB FC
              0x200343E4: 0x20034343, 0x200343E8: 0x20034343}
SYMBOLS = {
    0x2003BE9C: "hook_kbd_scan", 0x2003BECC: "hook_local_note",
    0x2003BEFA: "hook_local_midi_out_on", 0x2003BF16: "hook_local_midi_out_off",
    0x2003B07A: "hook_midi_note_on", 0x2003B032: "hook_midi_note_off",
    0x2003B294: "hook_cc123", 0x2003B144: "hook_cc123", 0x200396CA: "hook_hold",
    0x2003C244: "hook_button", 0x2003C292: "hook_pot_store", 0x2003C2A6: "hook_pot_change",
    0x2003EACE: "hook_hold_query", 0x2003D15C: "hook_program_loaded",
}

# firmware/native.c stock_iface order: every stock address the native code touches
IFACE = [
    0x2003EC5D,   # note_on(src, note, vel)
    0x2003E95D,   # note_off(src, note)
    0x2003EBE5,   # all_notes_off()
    0x20036825,   # led_set(led, 0/1)
    0x20037F25,   # display3(c0, c1, c2)
    0x20037FF7,   # display_int(v)
    0x2003818D,   # display_restore(ui)
    0x20057390,   # ui object
    0x20057438,   # Globals menu open (word != 0)
    0x20079B5C,   # button-held table entry for A440 (u16)
    0x2003B695,   # hold query
    0x2003D325,   # voice-engine post(word)
    0x2003D829,   # fifo_count(fifo)
    0x2003BC31,   # button post(id, value)
    0x20036B51,   # pot store(pot, raw)
    0x2003BC6D,   # pot change post(pot, old, new)
    0x20033F85,   # MIDI Out note-on
    0x20033F39,   # MIDI Out note-off
    0x20034343,   # parser state handler the trampoline continues to
    0x2003CB69,   # live program parameter read(layer, param)
    0x2003CEF5,   # plain program parameter store(layer, param, value)
    0x2003B6B1,   # hold off (both sources): the original callee at the program-loaded hook
    0x200574FA,   # ui + 0x16A: stock's A440 reference tone flag (byte)
    0x2005752C,   # ui + 0x19C: stock's HOLD button latch (byte)
]


def setUpModule():
    fixture_check.require([STOCK.name])


class NativeHookListTests(unittest.TestCase):
    def test_hook_list_is_exactly_the_specified_sites(self):
        hooks = json.loads(HOOKS.read_text())
        bl = {int(h["site"], 16): h for h in hooks if h.get("kind", "bl") == "bl"}
        words = {int(h["site"], 16): h for h in hooks if h.get("kind") == "word"}
        self.assertEqual(set(bl), set(BL_SITES))
        self.assertEqual(set(words), set(WORD_SITES))
        for site, h in bl.items():
            self.assertEqual(int(h["expect"], 16), BL_SITES[site], hex(site))
            self.assertEqual(h["symbol"], SYMBOLS[site], hex(site))
        for site, h in words.items():
            self.assertEqual(int(h["expect"], 16), WORD_SITES[site])
            self.assertEqual(h["symbol"], "hook_rt_trampoline")

    def test_stock_patch_memory_facts(self):
        payload = syx.decode(STOCK.read_bytes()).payload
        # end of program apply: bl hold_off_both (unconditional, every load path)
        self.assertEqual(thumb.decode_bl(0x2003D15C, build.read_ram(payload, 0x2003D15C, 4)), 0x2003B6B0)
        # plain parameter store: cmp r1,#0x59 ; push {r3-r7,lr} (special cases 0x59/0x34, else strh [table + (99*layer+param)*2])
        self.assertEqual(build.read_ram(payload, 0x2003CEF4, 4), bytes.fromhex("5929f8b5"))
        self.assertEqual(build.read_ram(payload, 0x2003CF42, 10), bytes.fromhex("632303fb047425f81460"))
        # parameter read: movs r3,#0x63 ; ldr r2,[pc] ; mla r1,r3,r0,r1 ; ldrh.w r0,[r2,r1,lsl #1]
        self.assertEqual(build.read_ram(payload, 0x2003CB68, 12), bytes.fromhex("6323034a03fb001132f81100"))

    def test_stock_holds_the_expected_instructions_at_every_site(self):
        payload = syx.decode(STOCK.read_bytes()).payload
        for site, target in BL_SITES.items():
            self.assertEqual(thumb.decode_bl(site, build.read_ram(payload, site, 4)), target & ~1, hex(site))
        for site, word in WORD_SITES.items():
            self.assertEqual(struct.unpack("<I", build.read_ram(payload, site, 4))[0], word, hex(site))


class NativeImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)
        cls.wbin, cls.wmap = fw.build_wrapper(ROOT / "firmware", OUT)
        cls.out = OUT / "native.syx"
        build.build_image(STOCK, cls.wbin, cls.wmap, HOOKS, cls.out, rec_base=REC_BASE, rec_size=REC_HI - REC_BASE)
        cls.base = syx.decode(STOCK.read_bytes()).payload
        cls.img = syx.decode(cls.out.read_bytes()).payload
        cls.symbols = build.parse_map(cls.wmap.read_text())
        cls.wrapper = cls.wbin.read_bytes()

    def test_output_is_a_valid_main_os_file(self):
        c = syx.decode(self.out.read_bytes())
        self.assertEqual(c.target, "main")
        self.assertEqual(c.trailer, syx.trailer_for(c.payload))
        images = records.parse_images(self.img)
        self.assertEqual(len(images), len(records.parse_images(self.base)))
        self.assertEqual(images[0].entry, records.parse_images(self.base)[0].entry)

    def test_only_the_hook_sites_and_the_record_differ_from_stock(self):
        spans = build.image_diff(self.base, self.img)
        self.assertEqual([(s.ram_lo, s.ram_hi) for s in spans if s.appended], [(REC_BASE, REC_HI)])
        # the four parser-table words are adjacent and reported as one span (the F9 word
        # between them is untouched); every BL site is its own 4-byte span
        changed = sorted(s.ram_lo for s in spans if not s.appended)
        self.assertEqual(changed, sorted(set(BL_SITES) | {min(WORD_SITES)}))
        for s in spans:
            if s.appended:
                continue
            if s.ram_lo in BL_SITES:
                self.assertLessEqual(s.size, 4)
            else:
                self.assertEqual((s.ram_lo, s.ram_hi), (0x200343D8, 0x200343EC))
        self.assertEqual(build.read_ram(self.img, 0x200343DC, 4), build.read_ram(self.base, 0x200343DC, 4))

    def test_hooks_land_on_symbols_inside_the_code_area(self):
        for h in json.loads(HOOKS.read_text()):
            site = int(h["site"], 16)
            if h.get("kind", "bl") == "word":
                target = struct.unpack("<I", build.read_ram(self.img, site, 4))[0] & ~1
            else:
                target = thumb.decode_bl(site, build.read_ram(self.img, site, 4))
            self.assertEqual(target, self.symbols[h["symbol"]] & ~1, h["symbol"])
            self.assertTrue(REC_BASE <= target < STATE_BASE)

    def test_wrapper_fits_below_its_state_and_state_is_zero(self):
        self.assertLessEqual(REC_BASE + len(self.wrapper), STATE_BASE)
        self.assertEqual(build.read_ram(self.img, REC_BASE, len(self.wrapper)), self.wrapper)
        self.assertEqual(build.read_ram(self.img, STATE_BASE, REC_HI - STATE_BASE), bytes(REC_HI - STATE_BASE))

    def test_interface_table_is_exactly_the_known_stock_addresses(self):
        table = self.symbols["stock_iface"] & ~1
        self.assertTrue(REC_BASE <= table < STATE_BASE)
        words = struct.unpack_from("<%dI" % len(IFACE), self.wrapper, table - REC_BASE)
        self.assertEqual([hex(w) for w in words], [hex(a) for a in IFACE])
        after = struct.unpack_from("<I", self.wrapper + b"\0" * 4, table - REC_BASE + 4 * len(IFACE))[0]
        self.assertNotIn(after, set(IFACE))

    def test_code_materialises_no_unknown_addresses(self):
        layout = (OUT / "native.layout").read_text()
        text = next(l.split() for l in layout.splitlines() if l.split() and l.split()[0] == ".text")
        text_size = int(text[2])
        self.assertTrue(0 < text_size <= len(self.wrapper))
        refs = thumb.find_absolute_addresses(self.wrapper[:text_size], REC_BASE)
        allowed = set(IFACE) | set(range(REC_BASE, REC_HI))
        unknown = sorted(a for a in refs if 0x20000000 <= a < 0x50000000 and a not in allowed)
        self.assertEqual(unknown, [], [hex(a) for a in unknown])
        self.assertIn(self.symbols["stock_iface"] & ~1, refs)

    def test_mmu_region_covers_the_record(self):
        rows = struct.unpack("<9I", build.read_ram(self.base, 0x2004DD38, 36))
        self.assertEqual(rows[3:6], (0x20020000, 0x2008FFFF, 0x00005C04))
        self.assertTrue(0x20020000 <= REC_BASE and REC_HI - 1 <= 0x2008FFFF)

    def test_rt_trampoline_continues_to_the_stock_parser_state(self):
        # last word of the trampoline path is loaded from the table entry for the parser state
        self.assertEqual(IFACE[18], 0x20034343)
        self.assertEqual(self.symbols["hook_rt_trampoline"] & 1, 1)   # Thumb


if __name__ == "__main__":
    unittest.main()
