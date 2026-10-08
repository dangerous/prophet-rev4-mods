# Stock Main OS 2.1.0 — internal APIs an arpeggiator hook needs

Source: `orig.asm` (stock), `hack.asm`/`blob.asm` (the Arp Mod), `orig_ram.bin` (RAM image, base 0x20010000) for
data tables. Addresses are RAM addresses; Thumb call targets are given even (add 1 for `bx`/pointers).
Reading literals in objdump output: two decoded halfwords `XXXX YYYY` = word `0xYYYYXXXX`
(e.g. `aa84 2007` = 0x2007AA84); the data-table addresses below were verified by byte search in `orig_ram.bin`.
Confidence: **H** = read directly from the code and cross-checked, **M** = one interpretation step, **L** = inferred.

## 0. Architecture (needed to read everything else)

- RTOS with `svc #0` yield, tick counter and **software timers**; the UI is a QP-style hierarchical state
  machine ("active object", AO) living at **`ui = 0x20057390`**. `ui+8` holds the current state handler
  `fn(ui, msg)`; handlers return **0 = super (pass to parent), 3 = handled, 8 = transition**
  (`ldr r3,=target; str r3,[r0,#8]; movs r0,#8`). Top state = `0x200314AD`, main state = **`0x20039801`**
  (`0x200397b8: ldr r3,[pc,#0x3c] @0x200397f8 (=0x20039801); str r3,[r4,#8]`). **H**
- Message/event: allocated by `0x20031970(size, 0xFFFF, sig)` (`strh r8,[r0]` → **u16 signal at +0**,
  `strb r4,[r0,#2]` pool idx, `[+3]` refcount); posted to the UI queue object `*0x2004D5EC` through its
  vtable slot 3: `ldr r0,[0x2004D5EC]; ldr r2,[r0]; ldr r4,[r2,#0xc]; bx r4` with `(obj, msg, 1)`.
  Posters bail out when `0x2003867C()` (`ldrh ui+0x15A`, "UI up") or `0x2003D7D4()`
  (`*0x200595CC == 0`) is zero. **H**
- Signals in the main state table (`tbh` at 0x20039824): 1 ENTRY, 2 EXIT, 3 INIT,
  **4 BUTTON** `{u16 id @+4, u8 value @+6}`, **6 POT** `{u16 pot @+4, u16 new @+6, u16 old @+8}`,
  **8 KEY** `{u16 note @+4, u16 vel @+6}` (ignored by the main state), **0xA ANALOG**
  `{u16 id @+4, u16 value @+6}` (1 pitch wheel, 2 mod wheel, 3 pressure, 8 sustain pedal),
  **0xB MIDI** `{u16 port @+4, u16 status @+6, u16 d1 @+8, u16 d2 @+0xA, u16 extra @+0xC}`,
  **0xE DISPLAY-TIMEOUT**, 0x12 and 0x13 (misc). **H**
- Three periodic software timers are created in the init routine `0x2003D340` with
  `0x200310B4(name, period_ticks, 1, 0, callback, &obj)` + `0x2003111C(obj, 1, …)`:
  `"Note Handler Event Timer"` period **1** → **`0x2003BE8C`** (keyboard FIFO consumer),
  `"Panel Timer"` period **6** → `0x2003C4C8` (panel link task), `"ADC Timer"` period **4** → `0x200361F0`
  (wheels/pedals). Evidence: `0x2003d546: ldr r3,[pc,#0x114] @0x2003d65c (=0x2003be8d); movs r1,#1;
  str r3,[sp] … bl 0x200310b4`; name strings at 0x2004D611 / 0x2004D62A / 0x2004D636. **H**

## 1. Voice / note API

| What | Address | Signature / behaviour | Conf |
|---|---|---|---|
| note on | **`0x2003EC5C`** | `note_on(src, note, vel)`; `vel==0` → tail-calls note_off (`0x2003ec68: cbnz r2 …; 0x2003ec70: b.w 0x2003e95c`). Marks the key in the held table (`bl 0x2003bffc`), then per layer: `0x2003E7D4(note, layer)` accept test, unison params 0x34/0x35/0x57, `0x2003E828(layer, src, note, vel)` allocator. Velocity = the 7-bit value, passed through; **no MIDI is emitted**. | H |
| note off | **`0x2003E95C`** | `note_off(src, note)`. `0x2003EEDC` is a one-instruction thunk (`b.w 0x2003e95c`) — the target of the hooked `bl` at 0x2003B032. | H |
| all notes off | **`0x2003EBE4`** | `void(void)`: loops voice ids 0x81A..0x823 posting `(id<<16)` to the DSP (`0x2003ec06: lsls r0,r4,#0x10 … bl 0x2003d324`), clears all 128 key-table entries (`bl 0x2003c024` ×128), zeroes `0x200600A4/+4`. Callers: CC123 (0x2003B294), TUNE, MIDI-out-cable global change, unison change, program apply when unison changed. | H |
| release on hold-off | **`0x2003EEE0`** | `void(void)`; only caller is the HOLD handler when hold becomes 0 (`0x200396ce: cbnz r4 …; bl 0x2003eee0`). Walks the voice table (0x2007CFE4, 10 B/voice) per layer and releases (`0x2003E790(voice)`) every sounding voice whose key is no longer down (`bl 0x2003c050; cmp r0,#0; bne skip`). | M |

`src` values: **1 = local keyboard** (0x2003BEC2 `movs r0,#1`), **2 = MIDI in** (0x2003B074 `movs r0,#2`) and the
A440 test tone (0x2003A1E4 `movs r0,#2; movs r1,#0x3c; movs r2,#0x7f`), **0 = internal re-trigger**
(0x2003EF40 `movs r1,#0`). `src` is forwarded to the allocator 0x2003E828; its downstream effect (velocity
curve / local-vs-MIDI treatment) was not decoded. The Arp Mod's output path calls note_on/note_off directly with
`src = max(1, src)` (blob 0x20089060: `cmp r1,#1; it ls; movls r0,#1 … bx r12` to 0x2003E95D/0x2003EC5D), which
is hardware-proven. **M**

Layer routing `0x2003E7D4(note, layer)`: reads layer-0 param **0x59 = layer mode** (0/1 single, 2 stack: every
note to both layers, 3 split) and **0x5F = split point** (`cmp r5,r0`: note < split → layer 0). Unison: param 0x34
on/off, 0x35 voice count (0xA/0xB special), 0x36 detune, 0x57 key-assign mode. **M**

**Stock held-key table (directly usable by an arp):** `u32 key[128]` at **`0x2007AAD4`**; 0 = up, else a
press-order stamp (set by `0x2003BFFC(note)` = max+1; cleared and compacted by `0x2003C024(note)`). Helpers:
`0x2003BF24()` lowest held note, `0x2003BFA0()` highest, `0x2003BF40(n)` n-th from bottom, `0x2003BF70(n)` n-th from
top, `0x2003BFBC(n)` n-th in press order, `0x2003C050(note)` → bool key down; all return 0x80 when none. Fed by
note_on/note_off from every source. **H**

**MIDI out is separate from note_on.** The keyboard consumer sends local keys itself, after note_on:
```
2003bee4: bl 0x20037b20   ; get_global(6) = MIDI out cable (0 off, 1 DIN, 2 USB, 3 both)
2003beea: bl 0x20037b2c   ; MIDI channel = globals[1] ? globals[1]-1 : 0
2003beee: ldrb r2,[sp,#6] ; ldrb r3,[sp,#7]     <- note, vel re-read from the stack
2003befa: bl 0x20033f84   ; note-on       (2003bf16: bl 0x20033f38 = note-off when vel==0)
```
`0x20033F84(cable, ch, note, vel)`, `0x20033F38` (note off), `0x20033FD0(cable, ch, cc, val)`, `0x20034124`
pitch bend, `0x2003409C(cable, ch, pc)`. Each checks `cable&1` → UART (`0x20032C88`) and `cable&2` → USB
(`0x200337D0`). Arp-generated notes produce **no** MIDI unless the hook calls these itself. **H**

**Correction:** `0x2003BCE0(note, vel)` (hooked at 0x2003BED8 as "local MIDI out") is **not** MIDI out — it posts a
signal-8 KEY event to the UI AO (`movs r0,#8; mov r2,r0 … bl 0x20031970; strh r5,[r0,#4]; strh r4,[r0,#6]`), which
the main state ignores. Shifting its arguments cannot change the MIDI-out note; to octave-shift MIDI out, hook the
two `bl`s at **0x2003BEFA** (expect 0x20033F85) and **0x2003BF16** (expect 0x20033F39) and alter `r2`. **H**

## 2. LEDs

- **`0x20036824(led_id, mode)`**: `mode 0` off, `1` on, `2` toggle (`0x20036844: cmp r1,#2 … ldrh r3,[r2,r3];
  cmp r3,#0; bne off-path`). Writes a `u16` on/off at `LED_TBL[id]`, table **`0x20060830`, 22 bytes per LED,
  70 entries (0..0x45)**. **H**
- Blink: **`0x200369C0(led_id, on)`** sets `[+4]=1, [+0xC]=1, [+0xE]=10` (period in panel ticks); `0x2003690C()` is
  the blink tick run by the panel task. **M**
- No flush call is needed: the Panel Timer task calls **`0x2003C3B8`** each run (6 ms), which packs all 0x46 LED
  entries into the outgoing panel-link buffer (`bfi r1,r5,#4,#1` on-bit, `bfi r1,r3,#5,#8` id,
  `bfi r5,r7,#2,#2` blink) and the buffer is exchanged with the panel processor; LED latency ≤ ~12 ms. **M**
- **Button → LED table `0x2004E496`** (`u8 button, u8 led`; 0x46 = none): Program 1-8 (ids 0-7) → 0x1A-0x21,
  TUNE 0x0C → 0x22, **HOLD 0x0E → 0x23, A440 0x0F → 0x24**, UNISON 0x19 → 0x27, mod-destination buttons
  0x10-0x17 → 0x12-0x19, 0x18→3, 0x1A→2, 0x1B→0, 0x1C→1, 0x1D-0x1F → 0x28-0x2A, 0x21→0x2B, 0x22→0x2C, 0x23→0x2D,
  0x24→7, 0x25→6, 0x26→4, 0x27→5. The three display digits are LEDs 0x2E-0x45 (8 segments each, font at
  0x2004E218). **H**
- Stock A440 LED handling (press, main state 0x2003A1D6): toggle `ui+0x16A`, `0x20036824(0x24, 0)`,
  DSP `0x2003D324(flag | 0x80B0000)`, then `0x20036824(0x24, 1)` if the flag is set. **H**
- **The Arp Mod never calls the LED API**: the blob's only stock references are 0x20037F24, 0x20037FF6, 0x2003EBE4,
  0x2003D325, 0x20034343 (plus hook continuations 0x20036B51, 0x2003BC6D, 0x2003E95D, 0x2003EC5D). Any A440 LED
  effect under the Arp Mod comes from stock handling the (replayed) A440 press. **H**

## 3. Pots / parameters (incl. Glide Rate)

Path (**H** unless noted):
1. Panel-link words (16-bit, type in bits 13-15) are decoded in `0x2003C204` (called from the Panel Timer task):
   type 4 = button (`ubfx r5,r0,#4,#2` value 1 press / 2 release / 3 held, `ubfx r6,r0,#6,#7` id) → gate
   `0x20036114(id, val)` → post **`0x2003BC30(id, val)`** (hooked site 0x2003C244); type 7 = pot high bits →
   `0x20036B5C(pot, hi)`; type 6 = pot low 7 bits → `raw = lo | (hi<<7)` (**10-bit, 0..1023**; pot 1 gets a centre
   dead-zone 0x1DD..0x223 via `0x2003C1D0`).
2. **`0x20036B50(pot, raw)`** stores raw in the pot table **`0x20079D84`** (16 B/pot, 28 pots: `[+0]` raw, `[+4]` hi<<7,
   `[+8],[+9]` pass-through flags, `[+0xC]` countdown; **The Arp Mod hooks this `bl` at 0x2003C292**).
3. Mapped value `0x20036C70(pot, raw) = (raw * (max+1)) >> 10`; `max = 0x20036C44(pot)` → param max from the
   **param table `0x2004C478`** (12 B/param: `[+0]` max, `[+2]` default; readers `0x2002F16C(param)`,
   `0x2002F17C(param)`).
4. If the mapped value changed (or force byte `0x2005754A` is set): **`0x2003BC6C(pot, old_mapped, new_mapped)`**
   posts POT (sig 6) to the UI AO (**The Arp Mod hooks this `bl` at 0x2003C2A6**).
5. UI main-state POT handler `0x2003AC1A`: pot 0 = volume (`0x20038778(0, val)`; layer volumes 0x58/0x60 while
   button 0x23 is held), pot 1 = master tune → DSP `0x8010000 | val`; otherwise `param = POT2PARAM[pot]`,
   pot mode = `get_global(8)` (0 relative → `0x2003CF7C` with delta, 1 pass-through → `0x20036BB8` crossing check,
   2 jump) → **`0x2003CCB8(src=0, layer=ui+0x19E, param, value, link=ui+0x1A0)`** (stores into the patch array
   `u16 patch[layer*99 + param]` at 0x2007B9C4, sends `(layer<<24)|(param<<16)|value` to the DSP via
   `0x2003C994` → `0x2003D324`, NRPN out via `0x2003401C` when enabled; a param-0x34 change triggers
   all-notes-off). Finally **only if `ui+0x19F` is set, `0x20037FF6(new_mapped)` shows the value** (that flag is
   toggled by a Program-button combo at 0x2003997E and initialised in the UI constructor 0x20037EA2) — stock does
   *not* always display pot values. The display timeout is armed on the pot 0/1 paths (`0x2003ad9a: bl 0x20036b14;
   bl 0x2003c940`).

**POT → PARAM table `0x2004D190`** (28 × u16): `[0]=0x802 volume, [1]=0x801 master tune, [2]=0 OscA freq,
[3]=1 OscB freq, [4]=2 OscB fine, [5]=8 OscA PW, [6]=9 OscB PW, [7]=0xE mix A, [8]=0xF mix B, [9]=0x11 cutoff,
[10]=0x12 resonance, [11]=0x28 filter env amount, [12..15]=0x2B,0x2D,0x2F,0x31 filter ADSR,
[16..19]=0x2C,0x2E,0x30,0x32 amp ADSR, [20]=0x25, [21]=0x10 noise, **[22]=0x0D Glide Rate**, [23]=0x20, [24]=0x21,
[25]=0x16, [26]=0x15, [27]=0x1A`.

**Glide Rate = pot id 0x16 (22) → param 0x0D (13), value range 0..127 (max-table entry 13 = 127, default 0);
NRPN 26 maps to it (NRPN→param table `0x2004C91C`).** Confirmed by the Arp Mod itself: its hook at 0x2003C292
(blob 0x20088F90) does `sub.w r3,r5,#0x16; clz r3; lsrs r3,#5` (pot == 0x16) `&& engine.enabled` → enqueues a
tempo event carrying the raw value and skips the stock store; its hook at 0x2003C2A6 (blob 0x20089010) swallows
the POT post for pot 0x16 when enabled, else `bx 0x2003bc6d`. **H**

How a hook consumes Glide while the arp is on and lets it through otherwise:
- Hook **0x2003C2A6** (`bl 0x2003bc6c`; r0 = pot, r1 = old mapped, r2 = new mapped 0..127). If `arp_on && r0 == 0x16`:
  use `r2` (or re-read the 10-bit raw at `0x20079D84 + 0x16*16`) as the tempo control and **return without
  calling stock** — patch param, DSP, NRPN and stock display stay untouched. Otherwise tail-call 0x2003BC6D. This
  runs in the Panel Timer thread (not the UI thread). Because the stock raw/old value keeps tracking the knob,
  when the arp is switched off the next knob move behaves per the pot-mode global as if the knob had always
  been live. (Recommended.)
- Optionally also hook **0x2003C292** (`bl 0x20036b50`) as the Arp Mod did, to see every raw step (finer than 0..127).
  Then the stock "previous" value is frozen while the arp is on, and the first move after arp-off yields one POT
  event with a possibly large delta (relative mode) or an immediate jump (jump mode).
- Show the tempo with `0x20037FF6(bpm)` + `0x20036B14(1)` + `0x2003C940()` (see §7).

Other param-side helpers: `0x2003CB68(layer, param)` read patch value; `0x2003CF58(src, layer, param, link)`
toggle a 0/1 button param; `0x20037ECC(button_id)` → param id (table 0x2004E332, `u8 btn, pad, u16 param`).
Globals: `0x20037B20(idx)` read, `0x20037B74(src, idx, value)` set (clamps to `0x2002F068(idx)`; side effects
idx 6 → all notes off, idx 9 → hold off), `0x20037B2C()` MIDI channel. Globals table `u16[29]` at
**`0x2007AA84`**: idx 1 MIDI channel (0 = omni), 4 MIDI receive enable, 6 MIDI out cable, 7 local control
(== 2 → keys reach the voices; also `!= 0` gates patch→voices on load), 8 pot mode, 9 sustain-pedal/HOLD mode
(1 = HLd), 0x1C used inside note_on. **M**

## 4. GLOBALS mode and the A440 button

- **GLOBALS = button id 0x0D** (not 0x19 — 0x19 is UNISON, param 0x34; held-UNISON combos set detune / chord).
  Press in the main state (0x2003A258): if BANK (0x28) is held → transition to state **`0x2003904D`** (service
  page), else to **`0x200389C1`**: `0x2003a28c: ldr r3,[pc,#0x48] @0x2003a2d8 (=0x200389c1); str r3,[r0,#8];
  movs r0,#8`. **H**
- Globals state ENTRY: `ui+0xA8 = 1` (page), LEDs 0x0C/0x25 on; pressing GLOBALS again → page 2 (LEDs 0x0D/0x26);
  Program buttons select item `ui+0xAC` (0..7, mirrored on the Program LEDs); GROUP = value−1, BANK = value+1 via
  `0x20037B74(0, idx, v)` followed by `0x20037F90(ui)` (display); EXIT sets `ui+0xA8 = 0`, restores LEDs and the
  display. Global index = `GLOBAL_PAGE_TBL[(page-1)*8 + item]`, table **`0x2004E4F4`** (page 1: 0..7,
  page 2: 8..14 then 0x1D = none). **H**
- **Flag for a hook: `*(u32*)(0x20057390 + 0xA8) != 0` ⇔ Globals menu active** (1/2 = page, 3 = service page);
  alternatively compare the state pointer `*(u32*)(0x20057390 + 8)` with 0x200389C1 / 0x2003904D
  (main = 0x20039801). **H**
- The Globals state's super-state is the main state (`0x20038c62: ldr r3,[pc,#0xb4] (=0x20039801); str r3,[r4,#8];
  … movs r0,#0`), so buttons it does not handle (A440, HOLD, TUNE, …) behave exactly as in the main state while
  Globals is open. **H**
- **Stock A440 (id 0x0F) handling**, main state 0x2003A1D6, **press only** (`ldrb r5,[r1,#6]; cmp r5,#1; bne →
  super`):
  - if HOLD is physically held (`ldrh r1,[0x20079B20 + 0x0E*4]` ≠ 0): `note_on(2, 60, 127)` and `ui+0x16C = 1`
    (test tone through a voice; the HOLD release then re-toggles hold to stop it);
  - else toggle tone flag `ui+0x16A`, LED 0x24 off, `0x2003D324(flag | 0x80B0000)` (DSP reference tone on/off),
    LED 0x24 on if the flag is set. Release/held events → super → ignored. TUNE press (0x2003A170) switches the
    tone off. **H**
  - To replace it: hook the button poster site 0x2003C244 (as the Arp Mod does), swallow id 0x0F presses, and to get stock
    behaviour replay `(0x0F, 1)`; releases are no-ops in stock.
- Button state table **`0x20079B20`**, 4 bytes/button: `u16 held, u16 used`. Gate `0x20036114(id, value)`:
  press → held=1, used=0, pass; held(3) → pass while `used==0`; release → pass if `used==0`, then `used=1`
  (`0x2003614a: movs r2,#1; strh r2,[r3,#2]`). Writing `used=1` for a button swallows its release — useful for
  modifier combos. Value-3 events arrive from the panel for held buttons and are also synthesised for GROUP/BANK
  by the panel task every 9 panel ticks while 0x0A/0x0B are held (0x2003C84E..). **H/M**

## 5. Program change / patch load

- **`0x200384C4(ui, layer_sel, factory, bank, group, prog, from_midi)`** (stack args at `[sp+0x30..0x38]`):
  stores the location in `ui+0x88/0x8A/0x8C/0x8E`, calls **`0x20036FE0(layer, 1, factory, bank, group, prog)`**
  (flash read `0x2003E2F8`, per-param `0x2003CEF4(layer, param, value)` straight into the patch array), then
  **`0x2003D00C(layer_from, layer_to)`** "apply": pushes all params to the DSP (`0x2003C9D4`), **all-notes-off only if
  the unison param changed** (`0x2003d142: ldrb r3,[r5,r4]; cbz r3; bl 0x2003ebe4` with r5 = 0x2007B0DA), clears
  pot pass-through state (`0x20036B88`, `0x20036BF0`), and **cancels HOLD (`0x2003B6B0()` → `hold(0,0); hold(0,1)`
  when global 9 == HLd)**. Back in the caller: MIDI Bank Select / PC out when from the panel, then display restore
  `0x2003818C`. **H**
- Callers: MIDI PC (0x2003B320), Program buttons (0x20039C2E), BANK/GROUP and factory-toggle paths, SysEx. **H**
- Consequences for an arp: every program change clears the stock hold latch and pedal state (HOLD LED off) through
  the normal hold handler — the already-hooked `bl` at 0x200396CA fires with hold = 0; voices are **not** silenced
  unless unison changed; Glide (param 0x0D) is re-sent from the new patch to the DSP; the held-key table is
  untouched. **H**

## 6. Timing and execution context

- **1 kHz keyboard poll confirmed structurally:** `0x2003BE8C` is the callback of the period-1 timer
  "Note Handler Event Timer". Timers expire in the **timer thread** `0x200311F8` → `0x20031178`
  (`ldr r3,[r4,#0x24]; mov r0,r4; blx r3`, re-armed with `[r4+0x18]` = period when `[r4+0x1C] == 1`), which blocks on
  a queue receive with timeout (`0x2002FBC8`). The hardware tick period was not read from peripheral registers;
  1 tick = 1 ms is inferred from the timer names/periods (1, 4, 6) and from the Arp Mod running its engine at 1000 ticks/s
  with hardware-correct BPM. **M-H**
- Stock millisecond counter: `*(u32*)0x20054050` (`0x20030720`: `ldr r2,[r3]; adds r2,#1; str r2,[r3]`, once per
  tick from the timer thread; read by `0x20030730()`). `0x20030938(ms)` = sleep (0 → `svc #0` yield). **H**
- Contexts: **keyboard consumer → timer thread** (not an ISR). **Buttons, pots, MIDI channel messages → UI AO
  thread**: the panel task posts BUTTON/POT, the ADC task posts ANALOG, the MIDI byte parser (state table
  0x200343D8 → message assembler `0x20033D88(port, …, status, data)`, one implementation for DIN and USB with the
  port passed as an argument) posts MIDI (`0x2003BD5C`, sig 0xB); the handlers at 0x2003B018.. (note on/off
  0x2003B032 / 0x2003B07A, CC123 0x2003B294, PC 0x2003B2C6, CC64 0x2003B218) therefore run in the UI thread.
  **Local-key note_on (timer thread) and MIDI note_on (UI thread) are concurrent** → a hook needs a critical
  section around shared state (the Arp Mod wraps its queue with `cpsid i`/`cpsie i`, blob 0x20088FA2). **H**
- Panel Timer (6 ms) duties: panel SPI exchange, button/pot decode and posting, LED/display flush, display-timeout
  countdown, GROUP/BANK repeat synthesis. **H**

## 7. Display

- **Font**: `display3` → per-digit `0x20036A40(digit 0-2, code, show, dp)`; code 0–0x28 indexes
  the glyph byte table at **`0x2004E218`** (bits 0–6 = segments a–g, each a panel LED: digit
  0 at LED 0x2E.., digit 1 at 0x36.., digit 2 at 0x3E..; the 8th LED is the decimal point).
  Glyphs: 0–9, then A–Z at 0x0A–0x23 (K, W, Z blank; M/X/V approximate), `o` 0x24, blank
  0x25, `-` 0x26, `_` 0x27, `]` 0x28. The Globals value names live as 3-byte code strings
  in tables pointed to from `0x2004E290` (see `0x20037F90`). **H**

- `0x20037F24(c0, c1, c2)` writes three chars via `0x20036A40(pos, code, 1, dp)` into the LED table (digit-1 DP =
  editing layer B `ui+0x19E`, digit-2 DP = param 0x59 == 2); `0x20037FF6(int)` signed decimal ('−' + 2 digits when
  negative); `0x2003804E(int)` variant. Any display write clears the "temporary display" flag `0x20054351`. **H**
- **Restore the normal display: `0x2003818C(ui = 0x20057390)`** — shows bank/group/program (+1 each), sets the
  Program LEDs and LED 0x2B (factory/user); every handler calls it after a temporary message. **H**
- **Stock timeout mechanism:** `0x2003C940()` loads the countdown `*(u32*)0x20057560 = 0xA7` panel ticks
  (≈ 1.0 s at 6 ms); the Panel Timer decrements it and at zero posts sig 0xE (`0x2003BE00(0)`); the main state
  (0x2003B63C) then does `if (0x20036B20()) { 0x2003818C(ui); 0x20036B14(0); }`. A hook showing a value should:
  write the display, `0x20036B14(1)`, `0x2003C940()`; or restore explicitly with `0x2003818C(ui)`. Other states
  may handle 0xE differently (not checked). **H**

## 8. Sustain / HOLD

- **`0x20039688(state, src)`**: `src 0` = HOLD button latch `ui+0x19C`, `src 1` = sustain pedal `ui+0x19D`;
  effective hold = pedal || button (`0x2003968c: ldrb r2,[r3,#0x19d]; cbnz; ldrb r2,[r3,#0x19c]`). On change:
  **DSP `0x2003D324(new | 0x80D0000)`** (the `bl` at 0x200396CA that the Arp Mod hooks; `r4` = new state), if new == 0 →
  `0x2003EEE0()`, then LED 0x23 off / on. `0x2003B694()` returns the effective hold state; `0x2003B6B0()` clears
  both sources when global 9 == HLd. **H**
- Sources: HOLD button id 0x0E (main state 0x20039D90: `get_global(9) == 1` → `hold(!ui+0x19C, 0)`, else toggles
  param 0x33 "Release"); MIDI CC64 (0x2003B22C when global 9 == 1, else a DSP release message); sustain pedal via
  ADC event id 8 (0x2003AFE8: needs global 7 == 2 and global 9 == 1; also echoes CC64 out); program apply
  (0x2003B6B0); set_global idx 9 (0x20037C02 / 0x20037C0C). **H**
- Variables: `0x20057390+0x19C` (button latch), `+0x19D` (pedal), `+0x16C` (HOLD+A440 test-tone pending). **H**

## 9. Other useful facts / corrections to existing notes

- DSP command post **`0x2003D324(u32 msg)`**: returns 0 if byte `0x20057570` is set, else calls the RTOS service
  `(*0x2005B7D0)[+0xC](msg)` (SHARC mailbox). Formats: controllers `0x8000000 | (type<<16) | value` (1 master
  tune, 3 pitch bend, 4 mod wheel, 7 pressure, 0xB A440 tone, 0xD hold); params `(layer<<24)|(param<<16)|value`;
  voice-off `(0x81A+voice)<<16`. **M**
- Button ids (main-state `tbh` at 0x20039856): 0-7 Program, 8 filter keyboard amount (param 0x13), 9 filter rev
  switch (param 0x14), 0x0A/0x0B velocity/aftertouch on-off (params 0x26/0x27, 0x29/0x2A; amounts 0x62/0x61) (M),
  **0x0C TUNE, 0x0D GLOBALS, 0x0E HOLD, 0x0F A440**, 0x10-0x17 mod destinations, 0x18 OscA sync (param 5),
  **0x19 UNISON** (0x34), 0x1A-0x1C OscA/B shape, 0x1D-0x1F LFO shape, **0x20 GROUP**, 0x21 factory/user toggle +
  reload, 0x22 / 0x23 enter sub-states (0x23 toggles edit layer `ui+0x1A0`), 0x24 OscB keyboard (0x0C),
  0x25 OscB lo-freq (0x0B), 0x26/0x27 OscB saw/tri, **0x28 BANK**. **H** (names of 0x21-0x23 **L**)
- `NOTES.md` / `hooks.json`: "0x2003BCE0 = MIDI-out of the raw key" is wrong (it is the sig-8 UI event); the
  octave-shift hook at 0x2003BED8 does not affect MIDI out (see §1). "GLOBALS(?) = 0x19" is wrong (0x19 = UNISON,
  GLOBALS = 0x0D). The globals table is at 0x2007AA84 (not 0x200784AA).
- Current edit layer `ui+0x19E` (0 = A, 1 = B), link flag `ui+0x1A0`; "UI up" `ui+0x15A`.

## 10. Quick address table

| Function / variable | Address | Notes |
|---|---|---|
| note_on(src, note, vel) | 0x2003EC5C | vel 0 → off |
| note_off(src, note) | 0x2003E95C (thunk 0x2003EEDC) | |
| all_notes_off() | 0x2003EBE4 | |
| hold_off_release() | 0x2003EEE0 | |
| key table u32[128] | 0x2007AAD4 | helpers 0x2003BF24 / BFA0 / BF40 / BF70 / BFBC / C050 |
| MIDI tx note on / off / CC | 0x20033F84 / 0x20033F38 / 0x20033FD0 | (cable, ch, a, b) |
| LED set / blink | 0x20036824(id, 0/1/2) / 0x200369C0(id, on) | table 0x20060830, 22 B/LED |
| A440 LED / HOLD LED | 0x24 / 0x23 | button→LED table 0x2004E496 |
| display 3 chars / int | 0x20037F24 / 0x20037FF6 | |
| display restore | 0x2003818C(0x20057390) | |
| display timeout start / flag | 0x2003C940() / 0x20036B14(1) | countdown 0x20057560, flag 0x20054351 |
| set_param | 0x2003CCB8(src, layer, param, val, link) | get 0x2003CB68(layer, param) |
| pot→param table / param max table | 0x2004D190 / 0x2004C478 | Glide: pot 0x16 → param 0x0D, 0..127 |
| pot raw store / pot post | 0x20036B50 (site 0x2003C292) / 0x2003BC6C (site 0x2003C2A6) | raw table 0x20079D84 |
| get / set global | 0x20037B20(idx) / 0x20037B74(src, idx, val) | table 0x2007AA84 |
| Globals page flag | *(u32*)0x20057438 (ui+0xA8) | item ui+0xAC; states 0x200389C1 / 0x2003904D |
| UI state pointer | *(u32*)0x20057398 (ui+8) | main 0x20039801 |
| button state table | 0x20079B20 (u16 held, u16 used)[id] | gate 0x20036114 |
| hold(state, src) | 0x20039688 | vars ui+0x19C / 0x19D; get 0x2003B694 |
| program load / apply | 0x200384C4 / 0x20036FE0 / 0x2003D00C | apply cancels hold |
| DSP post | 0x2003D324(msg) | |
| ms tick counter | *(u32*)0x20054050 | timer-thread tick |
| keyboard FIFO consumer | 0x2003BE8C | 1 ms timer callback; FIFO ptr *0x20071738 |
