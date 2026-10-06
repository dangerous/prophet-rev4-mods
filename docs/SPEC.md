# prophet-rev4-mods — Spec

## Overview

Wrapper firmware for the Sequential Prophet-5/10 Rev4 that layers new behaviour on top of
the third-party "Arp Mod V5" (an unofficial patch of Main OS 2.1.0), plus the tooling that
unpacks, patches and re-packs the OS SysEx image. Consumers are the player at the
instrument (behaviour is observable as notes sounding, display text and LED state in
response to keys, panel buttons, HOLD/pedal and MIDI) and the instrument's OS loader
(which consumes the `.syx` file, making the file format an external contract).

## System type

Embedded firmware patch (ARM Cortex-A5 Thumb-2, loaded into RAM by the stock loader) plus
a Python build/packing CLI.

## Conventions

- "Stock" = Sequential Main OS 2.1.0. "V5" = the Arp Mod V5 `.syx` as received.
  "Wrapper" = the code this project adds.
- Addresses are RAM addresses as loaded by the stock OS loader unless stated otherwise.
- Each behaviour carries a hardware-verification marker: `[HW: unverified]` or
  `[HW: verified <date>, <unit>]`. Host tests enforce the behaviour; the marker records
  whether it has also been confirmed on an instrument.

## Behaviours

### OS image format and round-trip

The file format is a contract with the stock OS loader (receiver at stock RAM
`0x200326A4`–`0x200324A8`); every rule below was read from that code and confirmed against
the three fixture files.

#### SysEx container

```
F0 01 32 7C [7D] 7A <6-byte header group> <payload> <trailer: 2 bytes> F7
```

- `7C` = OS update. An optional `7D` immediately after it selects the Panel OS; otherwise
  the Main OS is targeted.
- All bytes between `F0` and `F7` are 7-bit (`< 0x80`).
- **Packed groups.** A full group is 8 bytes: one MS byte followed by 7 data bytes; data
  byte *k* (0–6) takes bit 7 from bit *k* of the MS byte. A partial group of `1 + n` bytes
  (`1 ≤ n ≤ 6`) encodes `n` data bytes the same way.
- **Header group.** One 6-byte packed group (MS + 5 data) decoding to: `groups`, a
  big-endian u32 = number of full payload groups; `tail`, a u8 = number of data bytes in
  the final partial payload group (0 = none).
- **Payload.** `groups` full groups, then one partial group of `1 + tail` bytes if
  `tail > 0`. Decoded length = `7 × groups + tail`.
- **Trailer.** Let `S` be the 32-bit sum of the decoded payload taken as little-endian
  u16 halfwords (a final odd byte is not summed). Trailer byte 0 = `S & 0x7F`, byte 1 =
  `(S >> 8) & 0x7F`. The loader compares `S & 0x7F7F` with `t0 | t1 << 8` and rejects the
  transfer **before** the write phase on mismatch.

Fixture facts: Main 2.1.0 `groups=30180, tail=4`; V5 `groups=31353, tail=1`; Panel 1.1.3
`groups=2498, tail=2`.

#### Decoded payload: record stream

The decoded payload is a sequence of 16-byte records, each optionally followed by its
payload bytes:

```
0D <type> <chk> <fam> | w1 (u32 LE) | w2 (u32 LE) | w3 (u32 LE)
```

- `chk` is chosen so that the XOR of all 16 record bytes is zero.
- `fam` is constant within an image: `AD` = main-CPU (Cortex-A5) image, `AC` = SHARC image.
  A Main OS payload is image `AD` followed immediately by image `AC`; the payload ends
  exactly where the last image's declared length ends.
- `type 50` **EXEC** — first record of an image. `w1` = entry address (bit 0 set = Thumb),
  `w2 = 0`, `w3` = total byte length of everything after this record up to the end of the
  image.
- `type 00` **COPY** — `w2` payload bytes follow the record and are loaded at address `w1`.
  `w3 = 0`.
- `type 01` **FILL** — no payload; `w2` bytes at `w1` are filled with the u32 `w3` repeated
  (truncated to `w2`).
- `type 80` **START** — no payload; `w1` = entry address at which the target core is
  released. When present it is the last record of its image. The SHARC image (`AC`) ends
  with one; the main-CPU image (`AD`) has none (the bootloader uses its EXEC entry).

#### Tool behaviours `[HW: n/a]`

- `python3 -m tools inspect FILE` prints: target (Main/Panel), `groups`, `tail`, trailer
  status (`ok`/`BAD expected xx xx`), and for each image: family, entry, declared length,
  record count, and the lowest/highest load address.
- `python3 -m tools unpack FILE OUTDIR` writes `payload.bin` (decoded record stream) and
  `header.json` (`{"target": "main"|"panel", "groups": N, "tail": N, "trailer": [a, b]}`).
  It fails, without writing anything, with a distinct message for each of: wrong
  prefix/command, missing `7A`, payload length inconsistent with `groups`/`tail`, trailer
  mismatch, record XOR failure, image length not matching the record walk.
- `python3 -m tools pack PAYLOAD.bin OUT.syx [--target main|panel]` computes the header
  group and the trailer from the payload; it never copies them from an input.
- **Round-trip invariant:** for each fixture `f`, `pack(unpack(f)) == f` byte for byte, and
  the trailer computed from the decoded payload equals the trailer present in `f`.

#### Required input files `[HW: n/a]`

- The stock, Panel and V5 `.syx` files are copyrighted and are not part of the repository.
  They live in `fixtures/`; `fixtures/SHA256SUMS` is the authoritative list of the files
  the tools and tests need, with their SHA-256 (`<hash>  <name>` per line, `shasum -c`
  format); `fixtures/README.md` says where each comes from.
- Any `python3 -m tools` command given an input file that does not exist stops before
  writing anything, with exit status 1 and a one-line message on stderr naming the missing
  path and pointing at `fixtures/README.md` - not a traceback.
- A test module that needs a fixture which is missing, or whose SHA-256 differs from
  `fixtures/SHA256SUMS`, is **skipped** (not failed), and a warning naming the file, the
  expected hash and `fixtures/README.md` is printed on stderr once per test process. Tests
  that need no fixture (the host C harnesses) still run.

### Image patching (hook chaining, wrapper record)

The build takes a **base** OS file (V5 for the wrapper build, stock 2.1.0 for the native
build — see "Arp engine (native)"), a **wrapper binary** with its symbol
map, and a **hook list**, and produces a new OS file. Nothing is changed that the hook
list and wrapper placement do not require.

#### Wrapper record

- The wrapper is loaded by **one COPY record appended to image A** after the base's last
  record: V5 base — load address `0x2008A000`, length `0x2000`; stock base — `0x20088000`,
  length `0x8000`. Image A's EXEC declared length grows by the record size + 16 and its
  check byte is recomputed. This is exactly the mechanism V5 used to add itself to stock.
- Why that address: the stock startup's MMU region table maps `0x20020000–0x2008FFFF` as
  one RAM region (same attributes as V5's own blob); stock uses nothing above
  `0x200874CC`, V5 nothing above `0x2008A000`. The region is physically contiguous L2
  SRAM. `[HW: unverified for 0x2008A000+; the first wrapper ran at 0x20089600]`
- V5 base: code occupies `[0x2008A000, 0x2008B800)`, state `[0x2008B800, 0x2008C000)`;
  stock base: code `[0x20088000, 0x2008E000)`, state `[0x2008E000, 0x20090000)`. State is
  zero after every boot. The build fails if code+rodata exceed the code area.
- V5's own RAM-window record is left byte-identical (its zero tail is no longer used).

History: versions up to `e61b2c4` placed a ≤0xA00-byte wrapper inside the zero tail of
V5's window record at `0x20089600`; that layout was verified on hardware.

#### Patch kinds

- `bl` — retarget a 4-byte Thumb-2 `BL` (below).
- `word` — replace a 32-bit word at a RAM address inside the stock code record with a
  wrapper symbol's address, after verifying the word currently holds the `expect`ed value.
  Used for the stock MIDI-parser dispatch-table entries that V5 points at its trampoline.

#### Hook retargeting

- A hook list entry names a `site` (RAM address of a 4-byte Thumb-2 `BL` inside the stock
  code record, `0x2002EF00..`), the `expect`ed current target (a V5 or stock entry point,
  Thumb bit set) and the wrapper `symbol` to call instead.
- The build locates the record containing the site, verifies the instruction there is a
  `BL` whose target equals `expect`, and rewrites it as a `BL` to the symbol's address
  from the map. It fails if the site is not such a `BL`, if the symbol is missing, if the
  site lies outside the stock code record, or if the target is out of `BL` range.

#### CLI

- `python3 -m tools build --base BASE.syx --wrapper W.bin --map W.map --hooks hooks.json
  -o OUT.syx` — performs the above and writes `OUT.syx` (trailer and header recomputed).
  The map is `nm`-style text: `<hex address> <symbol>` per line. Each hook entry has an
  optional `"kind": "bl" | "word"` (default `bl`).
- `python3 -m tools diff A.syx B.syx` — prints every differing byte span of the decoded
  payloads as `image <n> record @0x<offset> ram 0x<lo>..0x<hi>: <n> bytes`, so a reviewer
  can see exactly what a build changed. Record-structure differences are reported as such.

### Arp engine (native) `[HW: unverified]`

Our own arpeggiator, hooked directly into stock Main OS 2.1.0 in place of the third-party
V5 blob; nothing of V5 is used or required. The player-facing behaviour is V5's (the
baseline) plus this project's features, with the deliberate changes listed under
"Differences from V5". The reverse-engineering this rests on is in `docs/re/`.

Terms: *keys down*, *HOLD active* and *latched* as in "Re-latch under HOLD". *Pool* = the
notes the arp plays from: keys down (local keyboard with local control on, and MIDI-in
notes) plus latched notes. A pool note is a pitch; the same pitch from two sources is one
pool note with the higher velocity.

#### Controls

| Action | Effect | Display |
|---|---|---|
| Tap A440 (press and release, nothing consumed in between) | arp on / off; the A440 LED follows | `OFF`, or BPM / `Syn` |
| A440 + Bank / Group | mode next / previous: Up, Down, Up/Down, Random | `UP `, `dn `, `Ud `, `rnd` |
| A440 + Program 1–4 | octave range 1–4 | `o 1`…`o 4` |
| A440 + Program 5 | clock source internal / MIDI | `int` / `Syn` |
| A440 + Program 6 | clear the sequence ("Seq") | — |
| A440 + Program 7 / 8 | note value shorter / longer ("Note value") | `32`…`4b` |
| A440 + Record (id TBD by readout) | pattern to MIDI Out off / on ("Output") | `LoC` / `out` |
| A440 + any other button | id readout ("Button id readout") | id |
| Glide Rate, arp on with internal clock | tempo 40–300 BPM = 40 + round(260·raw/1023), raw 0–1023; the patch's glide is not changed | BPM |
| Glide Rate otherwise (arp off, or `Syn`) | stock glide | stock |
| HOLD button; sustain pedal with Release/Sustain `HLd` | latch ("Re-latch under HOLD") | — |
| Keyboard Amount + Bank / Group | keyboard octave shift ("Keyboard octave shift") | shift |

1. A440 never reaches stock in any state (the tuning tone is not available). A440 press
   marks the hold "unused"; every consumed combo marks it "used"; the release toggles the
   arp only if still unused. Repeat events (value 3) of any button change nothing.
2. A button consumed while A440 is held has its release consumed as well; stock never sees
   an orphan press or release. Buttons not listed pass to stock untouched (press, repeats,
   release).
3. The Globals menu (a stock mode entered with GLOBALS, id 0x0D) is unaffected: it never
   involves A440, and Program/Bank/Group are only ever consumed while A440 is held. (V5
   special-cased button 0x19 held — which is UNISON, not GLOBALS — as a pass-through
   modifier; that is dropped.)
4. Transient texts (mode, octaves, `int`/`Syn`, note value, shift, readout, `OFF`) show for
   stock's display timeout (≈1 s) and then revert: while the arp is on, to its status text
   (`Syn` under MIDI clock, otherwise the BPM); while it is off, to stock's normal display
   (the program number). A BPM change while on and internal shows immediately and stays.
5. Nothing is saved with patches or across power cycles. Power-up: off, Up, 1 octave,
   internal, 120 BPM, 1/8, MIDI-out option off, no sequence, shift 0. Mode, octaves, clock
   source, BPM and note value survive the arp being switched off and on.

#### Pool

6. Note-on adds the pitch (a latched pitch pressed again becomes held again). Note-off
   while HOLD is active latches the pitch — the pool does not shrink; otherwise it removes
   the pitch, and if that pitch is the one sounding it is released at once (step cut short).
7. HOLD going inactive drops every latched pitch (keys still down stay). Re-latch applies
   as specified in "Re-latch under HOLD": the first key after all keys were released
   replaces the latched set.
8. MIDI CC 123–127 (all notes off, omni/mono/poly — every stock all-notes-off path)
   empties the pool, releases the sounding note and resets the pattern. HOLD state is
   **kept** (these do not switch HOLD off). A program change switches HOLD off in stock
   (its LED goes out), so latched pitches drop then, as in V5; keys still down keep
   playing and mode, tempo and the other settings are untouched.
9. Arp off: keys sound directly through stock as if the arp did not exist; latched pitches
   are not re-sounded and stay latched until HOLD goes inactive. Arp on with keys down:
   the directly sounding notes are released and the pattern starts at once (internal
   clock) or on the next clock (MIDI).

#### Pattern

10. Base pattern = the pool sorted by pitch. With octave range *o* the base pattern is
    played *o* times, each pass one octave above the previous (per-pass octaves); an
    entry above pitch 127 is silent. Up plays the expanded sequence ascending; Down plays
    it in reverse (top octave first); Up/Down plays it forward then backward **without
    repeating the turning notes** (C E G at `o 1` → C E G E C E G E …; one pitch at `o 1`
    simply repeats). Random plays one entry of the expanded sequence per step, uniformly
    at random, never the same entry twice in a row unless the sequence has one entry.
11. Pool changes mid-pattern: Up/Down/Up-Down continue from the pitch just played (the
    next entry above or below it in the new sequence, honouring the current direction);
    an emptied pool silences and resets so the next note starts at the beginning.
12. Changing mode, octave range or clock source, and a re-latch, restart the pattern. With
    the internal clock a restarted pattern with a non-empty pool steps immediately.
13. A step's velocity is its pool note's velocity. Steps are played through stock's voice
    allocation exactly as keys are (unison, split/stack, glide and the DSP path are
    stock's; the arp sends no note-off velocity).
14. Seq mode ("Seq") substitutes the recorded steps, transposed by the trigger key, for
    the base pattern; octaves per pass, modes, timing and output apply unchanged.

#### Timing

15. Internal clock: one step every 60 s ÷ (BPM × steps per beat), steps per beat from the
    note value (1/8 → 2). The sounding note is released half-way through the step (gate
    50 %). The time base is the 1 ms keyboard-scan tick; the remainder is carried so the
    average tempo is exact.
16. A note-on into an idle arp (nothing sounding, pattern at rest) steps immediately;
    otherwise the note waits for the running clock. A tempo or note-value change does not
    reset the phase. Under MIDI clock a note never moves the phase: steps always fall on
    the clock grid.
17. MIDI clock (`Syn`): 24 clocks per beat; clocks per step from the note value (1/8 → 12),
    release at half the step (rounded down). Start → the pattern restarts on the next
    clock; Continue → resumes; Stop → silence, position kept. Without clocks nothing
    sounds (keys still pool). 1 s without a clock → silence, and the next clock restarts
    the pattern. Realtime is followed from whichever port (USB or DIN) sends it first,
    until clock loss or a switch to `int`. The internal clock is not used under `Syn`.
    A note-value change under `Syn` re-aligns to the next multiple of the new step length
    counted from Start, so the pattern stays on the DAW grid.

#### Output

18. Pattern to MIDI Out **off** (default): stock behaviour — the keys you play go to MIDI
    Out, arp steps do not. **On**: arp/seq steps go to MIDI Out as note-on/note-off (the
    keyboard octave shift already applied, on stock's MIDI channel and output cable) and
    the raw keys do not; with the arp off the option changes nothing. For an external
    synth on the Prophet's MIDI Out to play the pattern.
19. Local control off: keys bypass the arp entirely and go straight to MIDI Out (stock);
    MIDI-in notes still arpeggiate (as in V5).

#### Robustness

20. Up to 128 events arriving between two ticks are applied in order; beyond that the
    newest are dropped and `Err` is shown for the display timeout — the arp is **never
    disabled** by input volume. (V5 dropped events and switched the arp off above 32.)
21. HOLD state belongs to stock; the engine reads it and never resets it (no hold
    re-assert anywhere).
22. No wrapper code runs at boot. While the arp is off, only the hooks' bookkeeping runs
    (pool tracking, seq recording, octave shift, button handling). Power-up state is "arp
    off", so a fault inside the engine proper is cleared by a power cycle and the USB
    OS-update path stays available. Hooks in timer context never block.
23. Kill switch: holding A440 while powering on (checked against stock's button-state
    table during the first second of ticks; button TBD if stock assigns A440 a power-on
    meaning) makes every hook pass straight through to stock for the whole session, with
    no engine state touched — the instrument behaves as stock 2.1.0, so a misbehaving
    build can always be re-flashed over USB.

#### Differences from V5 (intentional)

- Octaves are per pass, not a pitch-sorted union of transposed notes (C3 + D4 at `o 2` →
  C3 D4 C4 D5; V5: C3 C4 D4 D5).
- Random never repeats an entry back-to-back.
- Re-latch under HOLD, seq, note values (also under MIDI clock), keyboard octave shift,
  button id readout, pattern-to-MIDI-Out option, kill switch.
- Glide Rate is only captured with the arp on *and* internal clock (V5 captured it whenever
  the arp was on, so under `Syn` glide was dead and BPM changed silently).
- CC 123–127 all clear the pool and keep HOLD (V5: CC 123 only, and it dropped its hold);
  a consumed button's release is consumed; no overflow shutdown.
- Display timeouts are stock's (≈1 s, not 1.2 s) and the program number comes back after
  the arp is switched off (V5 left `OFF` on the display).
- V5's pass-through while button 0x19 (UNISON) is held is dropped.
- Everything else matches V5: modes and the Up/Down turning rule, timing math, 50 % gate,
  Glide → BPM mapping, display texts, LED, Start/Stop/Continue, clock loss, port follow,
  pass-through when off, power-up defaults.

#### Realisation (stock facts asserted by tests; details in `docs/re/`)

- Base image: stock Main OS 2.1.0 (`fixtures/prophet5_main_2.1.0.syx`). Hook sites, all
  inside the stock code record and each verified to hold the expected stock target before
  patching: `0x2003BE9C` (`bl 0x2003D828`, keyboard FIFO count — the 1 ms tick),
  `0x2003BECC` (`bl 0x2003EC5C`, local `note_on(1, note, vel)`), `0x2003BEFA` and
  `0x2003BF16` (`bl 0x20033F84` / `bl 0x20033F38`, local key → MIDI Out as
  `midi_note_on/off(cable, channel, note, vel)`; run for local control on and off),
  `0x2003B07A` (`bl 0x2003EC5C`, MIDI `note_on(2, note, vel)`), `0x2003B032`
  (`bl 0x2003EEDC`, MIDI `note_off(2, note)`), `0x2003B294` and `0x2003B144`
  (`bl 0x2003EBE4`, CC 123 and CC 124–127 all-notes-off), `0x200396CA` (`bl 0x2003D324`
  inside `hold_set`, r4 = merged button/pedal hold state), `0x2003C244` (`bl 0x2003BC30`,
  panel button `(id, value)`), `0x2003C292` (`bl 0x20036B50`, pot raw store `(id, raw)`),
  `0x2003C2A6` (`bl 0x2003BC6C`, pot-change post `(id, old, new)`), and the four
  MIDI-parser table words `0x200343D8/E0/E4/E8` (F8/FA/FB/FC, stock `0x20034343`). The
  stock call at `0x2003BED8` (`0x2003BCE0`) posts a UI event, not MIDI — it is not hooked.
- Contexts (FreeRTOS): the tick, button, pot and realtime hooks run in the timer-service
  task; the MIDI note, CC and HOLD hooks run in the lower-priority Prophet5 active-object
  task and can be pre-empted by the former. No hook runs in an interrupt. Engine state is
  mutated only from the tick; the other hooks hand events over through a 128-entry queue
  under a short interrupt-disabled section (previous state restored).
- Stock primitives used: `note_on 0x2003EC5C(src, note, vel)` / `note_off 0x2003E95C(src,
  note)` (src 1 local, 2 MIDI; bookkeeping only in stock), `all_notes_off 0x2003EBE4`,
  `led_set 0x20036824(id, state)` with A440 LED id `0x24`, display `0x20037F25(c0, c1,
  c2)` / `0x20037FF7(int)`, display timeout `0x20036B14(1)` + `0x2003C940()` (stock then
  restores the program number itself via `0x2003818C`), MIDI transmit `0x20033F84` /
  `0x20033F38(cable, channel, note, vel)` with cable = global 6 and channel from
  `0x20037B2C()` (how the pattern reaches MIDI Out), pot store `0x20036B50` and pot-change
  post `0x2003BC6C` (Glide Rate = pot 22, raw 0–1023; the store is always performed so
  stock's pot tracking stays current, only the post is swallowed while captured),
  `global_get 0x20037B20(idx)` (7 == 2 → local control on), button-state table
  `0x20079B20` (kill switch), `fifo_count 0x2003D828`.
- Wrapper record: one COPY record appended to image A at `0x20088000`, length `0x8000`
  (code `[0x20088000, 0x2008E000)`, state `[0x2008E000, 0x20090000)`, zero at boot) — the
  RAM V5 and the V5-based wrapper occupied, inside the stock MMU region that ends at
  `0x2008FFFF`.
- Build: `make image-native` → `build/prophet10_native.syx` from `firmware/hooks_native.json`.
  The V5-based images stay the shipped build, and the "Realisation" subsections of the
  other behaviour sections describe that build, until this section is hardware-verified.

### Note value (arp/seq step length) `[HW: unverified]`

1. The step length is one of 13 values, shortest to longest: 1/32, 1/16T, 1/16, 1/8T,
   1/16d, 1/8, 1/8d, 1/4, 1/4d, 1/2, 1 (whole), 2 bars, 4 bars (a bar is four beats).
   Power-up default is 1/8, which is exactly V5's behaviour. Not saved with patches.
2. A440 + **Program 7** selects the next shorter value, A440 + **Program 8** the next
   longer; the ends do not wrap. The display shows the new value for about a second:
   `32`, `16t`, `16`, `8t`, `16d`, `8`, `8d`, `4`, `4d`, `2`, `1`, `2b`, `4b`
   (right-aligned; glyphs as the panel font allows).
3. Internal clock: the step period is the note value at the current BPM (Glide pot, as in
   V5); the note is released half-way through the step, as V5 does.
4. MIDI sync: steps follow the incoming clock at the selected value — 3, 4, 6, 8, 9, 12,
   18, 24, 36, 48, 96, 192 or 384 clocks per step for the list above. Step boundaries fall
   exactly on clocks. Start/Stop/Continue behave as in V5; the clock-loss timeout (≈1 s)
   is unchanged for every value.
5. A change takes effect from the next step. Applies to the arp and to seq alike.
   **Build variants:** `make image` includes the MIDI-sync behaviour (4); `make
   image-internal` (`firmware/hooks_internal.json`) leaves the stock MIDI-parser table
   untouched, so under `Syn` the arp keeps V5's eighths whatever the setting — chosen as
   the first install because the parser table is on the USB re-flash path.
6. Realisation: internal — the engine's divisions-per-beat (engine + 0x30a) and
   ticks-per-second (engine + 0x30c) fields are set to a (div, tps) pair per value
   (1/32 → 8,1000; 1/16T → 12,1000; 1/16 → 4,1000; 1/8T → 6,1000; 1/16d → 8,3000;
   1/8 → 2,1000; 1/8d → 4,3000; 1/4 → 1,1000; 1/4d → 2,3000; 1/2 → 1,2000; 1 → 1,4000;
   2b → 1,8000; 4b → 1,16000) and the step accumulator (engine + 0x310) is zeroed on
   change. Sync — the engine steps every 12 clocks it sees (hard-coded), so the wrapper's
   trampoline, installed at the four MIDI-parser table entries V5 uses, hands V5's sniff
   a filtered stream: per real clock it forwards 4, 3, 2, 3/2, 4/3, 1, 2/3, 1/2, 1/3, 1/4,
   1/8, 1/16 or 1/32 clocks (fractions as exact repeating patterns), forwards FA/FB/FC
   unchanged, and zeroes the engine's clock-loss counter (engine + 0x324) on every real
   clock so withheld clocks never trigger the timeout. While sync is active the (div, tps)
   fields hold V5's (2, 1000).

### Keyboard octave shift `[HW: unverified]`

1. Hold the filter **Keyboard Amount** button (panel id 8) and press **Bank** to shift the
   keyboard up one octave, **Group** to shift it down; range −2…+2. Pressing Bank and
   Group together (the second while the first is still down) resets the shift to 0. The
   display shows the shift (`-2`…`2`) for about a second after each press (also at the
   ends, unchanged). Power-up: 0. Not saved with patches.
2. The shift applies to keys played on the keyboard: what the synth plays, what the arp/seq
   receive (re-latch, recording, triggers), and what is sent to MIDI Out for those keys.
   MIDI-in notes are not shifted. A key's release always uses the shift that was in force
   when it was pressed, so changing the shift while holding keys never sticks a note.
3. A tap of Keyboard Amount (press and release with no Bank/Group press in between) still
   cycles keyboard tracking as in stock; the setting and its LED change on the release
   rather than on the press. While it is held with Bank/Group, the tracking setting does
   not change. Repeat events (value 3) of any of the three buttons are ignored.
4. Because the shift is applied before the voice engine, a Prophet-10 split point moves
   with the keyboard. A key whose shifted note would fall outside 0–127 is silent (not
   reachable from the 61-key range at ±2).
5. Limits of this version: keys are not shifted in Local-Off mode (keys to MIDI Out only);
   Keyboard Amount held together with A440 is undefined.
6. Realisation: the local-note hook shifts the key and records the per-key shift
   (128-entry table) so the release maps identically; `bl` hooks on the two stock calls
   that send local keys to MIDI Out (`0x2003BEFA` note-on and `0x2003BF16` note-off,
   expecting stock `0x20033F85` / `0x20033F39`, `(cable, channel, note, vel)`) apply the
   same per-key mapping — the call at `0x2003BED8` hooked by earlier builds posts a UI
   event, not MIDI, so those builds did not shift MIDI Out; the button hook swallows id 8 (press,
   repeats, release), treats Bank/Group presses while it is held as shift commands, and on
   a release with no shift command replays press+release of id 8 through V5's button entry
   (which forwards non-arp buttons to stock when A440 is not held).

### Button id readout `[HW: unverified]`

While A440 is held, pressing a panel button that neither V5 nor the wrapper assigns
(anything other than Program 1–8, Bank, Group, Keyboard Amount (id 8) and UNISON, id
`0x19`, which V5 treats as a pass-through modifier) shows
that button's id on the display for about a second and is otherwise ignored. An aid for
mapping panel button ids when designing new combinations; V5 passed such presses to
stock, where they had no defined meaning while A440 was held.

### Safety invariants

Enforced by tests on every built image against its base:

1. The output decodes with `trailer: ok`, target `main`.
2. The SHARC image (`AC`) is byte-identical to the base.
3. The main-CPU image has the base's record sequence (same count, types, load addresses,
   lengths, EXEC entry) **plus exactly one appended COPY record** (V5 base `0x2008A000`,
   `0x2000`; stock base `0x20088000`, `0x8000`); the EXEC declared length is the base's
   plus the record size + 16.
4. Payload bytes of the base's records differ only within the 4-byte `BL` at each `bl`
   hook site (a retarget may leave the first halfword unchanged) and within the 4-byte
   word at each `word` site.
5. The stock startup record (COPY `0x2002E000`, `0xEDC` bytes) is byte-identical to the
   base; no wrapper code runs at boot — the wrapper is entered only through the hooks.
6. Every hook site lies inside the stock code record and, before patching, holds exactly
   what the hook list says it holds (a `BL` to the named V5 or stock entry, or the named
   word).

### Re-latch under HOLD `[HW: verified 2026-10-06, Prophet-10 Rev4 — incl. the hold re-assert fix]`

Terms: *HOLD active* = the HOLD button is lit, or the sustain pedal is down with Global
Release/Sustain set to `HLd` (both reach the same stock hold handler). *Keys down* = the
set of physical keys currently pressed plus MIDI-in notes currently on, counted together
(the same key number from both sources counts twice). *Latched* = the arp's note set may
be non-empty.

1. With the arp enabled and HOLD active, the first key pressed after **all keys were
   released** clears the arp's latched notes before that key is added. The arp restarts
   with the new key; keys pressed while at least one key is still down are added as before.
   Example: hold C‑E‑G, release all, play D‑F‑A → the arp plays D‑F‑A, not C‑D‑E‑F‑G‑A.
2. The clear is never issued while any key is down, never when HOLD is inactive, never
   when the arp is disabled (stock HOLD sustain is unaffected), and never when nothing is
   latched (e.g. the first chord after power-up, or after HOLD was switched off).
3. A MIDI Note On with velocity 0 is a release. Local key releases are seen even while
   HOLD is active (the stock keyboard scanner reports them regardless of HOLD).
4. Switching HOLD off makes the arp drop released notes (existing V5 behaviour); from
   then on only keys still down count as latched.
5. All note events are forwarded to the arp exactly as before, after any clear.
6. Realisation: the clear uses the arp's own All‑Notes‑Off path (stock all‑notes‑off
   followed by the arp's clear event), i.e. the same thing MIDI CC 123 does in V5. That
   clear also resets the arp's **own hold flag** (engine + 0x302; CC 123 semantics), so
   immediately after it the wrapper re‑asserts "hold on" through V5's hold‑event entry
   (`0x20088F37`, what its HOLD hook calls). Queue order: clear, hold on, new notes. The
   re‑latched chord therefore stays latched. `[HW: bug seen 2026-10-06 before this fix]`

Wrapper facts this depends on (V5 as shipped; each is asserted by a test on the fixture):
arp‑enabled byte at `0x200894D8`; V5 entry points local‑note `0x20088C51`, MIDI note‑on
`0x20088CFD`, MIDI note‑off `0x20088D2D`, hold `0x20089081` (reads the new hold state from
`r4` as left by the stock caller), all‑notes‑off `0x20088E83`. Wrapper state lives in
`0x20089E80–0x2008A000` inside the wrapper window and is zero after every boot.

### Seq (step-recorded sequence) `[HW: verified 2026-10-06, Prophet-10 Rev4 — record, transposed playback, clear; octaves and MIDI-in recording not yet exercised]`

A sequence is up to 32 steps of (pitch, velocity), recorded by holding A440 and playing.
*Seq mode* is active exactly when a sequence exists. *Keys*, *HOLD active* and
*keys down* are as in "Re-latch under HOLD" (local keys and MIDI-in notes count alike).

#### Recording

1. The first note-on while A440 is held starts a new recording, discarding any existing
   sequence (and stopping its playback). Each further note-on while A440 stays held
   appends a step; after 32 steps further note-ons are ignored. Key releases are ignored
   while recording. The display shows the step count after each recorded step.
2. Notes played while recording sound as they would have without this wrapper (they reach
   the arp normally) and their later releases are forwarded, so nothing sticks.
3. Releasing A440 ends the recording. If at least one step was recorded, seq mode becomes
   active and **that A440 release does not toggle the arp**. If nothing was recorded,
   A440 behaves exactly as in V5 (a tap toggles the arp; held + buttons = settings).
4. Buttons keep working while A440 is held during recording (mode, clock source).

#### Playback (seq mode active, arp enabled)

5. The sequence plays while at least one key is down and, with HOLD active, after all
   keys are released until the next key — the re-latch rule: the first key after all keys
   were released restarts the sequence from step 1. Each step sounds the recorded pitch
   transposed by *(trigger key − first recorded pitch)*, at the recorded velocity. The
   trigger key is the most recently pressed key; a new trigger takes effect from the next
   step and stays in effect even if that key is released while others remain down.
6. A step whose transposed pitch falls outside 0–127 is silent.
7. Direction, tempo, clock source and MIDI sync are the arp's: `UP` plays the steps in
   recorded order, `dn` reversed, `Ud` forward then back, `rnd` shuffled; one step per arp
   step.
8. With the arp disabled, keys play normally (no sequence runs). With HOLD inactive,
   releasing all keys stops the sequence; switching HOLD off with no keys down stops it.
9. MIDI CC 123 (All Notes Off) stops the sequence; the next key starts it again.

#### Octaves

10. There is one octave setting, shared by the arp and seq, changed with A440 + Program
    1–4 at any time and shown as `o N` for about a second, as in V5. Entering seq mode
    takes over the setting in force. While a sequence exists (recording or active, arp on
    or off) the setting spans the whole sequence: with `o 2` the sequence plays once as
    recorded, then once an octave up; direction modes apply across the expanded pattern;
    changes take effect immediately. The arp's own expansion is held at one octave while a
    sequence exists — the wrapper expands instead — and a Program 1–4 press in seq mode
    counts as using the A440 hold, so releasing A440 afterwards does not toggle the arp.
    `[HW: bug seen 2026-10-06 — consumed presses left V5's "used" flag clear, so the A440
    release toggled the arp; fixed]`
11. Leaving seq mode (Program 6) hands the current setting back to the arp: whatever `o N`
    was last selected — before, during or after recording — is what the arp plays with
    afterwards, and `o N` is shown if the arp's value changes.

#### Clear

12. A440 + Program 6 clears the sequence and leaves seq mode: normal arp behaviour
    (including re-latch) resumes; the A440 release afterwards does not toggle the arp.
13. Power-up: no sequence.

#### Realisation (V5 facts asserted by tests)

- The engine emits every step and every pass-through note through a function pointer at
  `0x200894F0` (context word `0x200894F4`) with `(ctx, src, on, note, velocity)`; V5
  installs `0x20089061` there during its lazy init (`0x20088C81`). The wrapper installs
  its own output function after that init, from the keyboard-scan tick, and substitutes
  **dummy notes** `k + N·m` (step `k`, octave `m`, `N` = step count) for recorded pitches;
  real notes pass through unchanged.
- Dummies are fed through the V5 local-note entry and drained via the V5 event queue, at
  most 16 per tick (32-entry ring; an overflow makes V5 disable the arp).
- Every clear the wrapper issues (restart, end of recording, Program 6) is followed by a
  hold re‑assert when HOLD is active, for the same reason as in "Re-latch under HOLD".
- V5 draws `o N` from its keyboard-scan hook whenever the octave byte changed during the
  engine tick: stock 3-character display `0x20037F25` with codes `0x24` ('o'), `0x25`
  (blank), N, then display timeout `0x4B0` ticks at `0x200895E8`; the wrapper draws the
  seq-mode value the same way. V5's engine sets its "A440 used" byte (`0x20089508`, button
  ctx + 8) only for Program/Bank/Group presses it receives and otherwise toggles the arp on
  the A440 release (engine `0x200889BA`), so a Program 1–4 press the wrapper consumes sets
  that byte itself.
- Octave setting byte `0x200894DF` (engine + 0x307); Program ids 0–3 = octaves 1–4;
  id 5 = Program 6. Any Program press while A440 is held marks the A440 hold as "used",
  which suppresses the toggle on release; ids 5–7 are otherwise no-ops in V5.
- Panel button events arrive as `(id, value)` with value 1 = press, 2 = release and
  **3 = still held** (the stock gate `0x20036114` passes 1–3; repeats arrive while a
  button is down). Only 1 and 2 change wrapper state, as in V5.
  `[HW: bug seen 2026-10-06 — a repeat reset the A440-held flag; fixed]`
- Hooks: keyboard-scan tick `0x2003BE9C → 0x20088D53`, panel buttons `0x2003C244 →
  0x20088EAB` `(id, value)`, CC 123 `0x2003B294 → 0x20088E83`, in addition to the
  re-latch hooks. Wrapper state occupies `0x20089E80–0x2008A000`.

