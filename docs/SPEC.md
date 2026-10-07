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
- **Payload.** `groups` full groups, then the tail group: its MS byte **always present**,
  followed by `tail` data bytes — so an empty tail (`tail = 0`) is still one byte on the
  wire. Decoded length = `7 × groups + tail`. The loader reads that MS byte
  unconditionally before the trailer; a file without it stalls the transfer at `100`
  with the eight Program LEDs lit, nothing is written, and a power cycle recovers.
  `[HW: 2026-10-07 — happened with the first native image, 244,048 payload bytes = 34,864
  groups + 0]`
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
build — see "Native arp engine"), a **wrapper binary** with its symbol
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
  -o OUT.syx [--record BASE:SIZE]` — performs the above and writes `OUT.syx` (trailer and
  header recomputed). `--record` selects the appended record: default `0x2008A000:0x2000`
  (the V5-based build); the native build uses `0x20088000:0x8000`.
- `python3 -m tools fwbuild SRC OUT [--profile v5|native]` cross-compiles and links the
  wrapper for that record: `v5` (default) builds `SRC/wrapper.c` at `0x2008A000` with the
  code limit `0x2008B800` and writes `wrapper.bin/.map/.layout`; `native` builds
  `SRC/native.c` at `0x20088000` with the limit `0x2008E000` and writes `native.*`. Each
  profile checks that its hook symbols are present.
  The map is `nm`-style text: `<hex address> <symbol>` per line. Each hook entry has an
  optional `"kind": "bl" | "word"` (default `bl`).
- `python3 -m tools diff A.syx B.syx` — prints every differing byte span of the decoded
  payloads as `image <n> record @0x<offset> ram 0x<lo>..0x<hi>: <n> bytes`, so a reviewer
  can see exactly what a build changed. Record-structure differences are reported as such.

### Native arp engine (stock 2.1.0 base) `[HW: unverified]`

Replaces the dependency on Arp Mod V5: the base image becomes Sequential's stock Main OS
2.1.0 and every arp behaviour below is this project's code. The behaviours specified elsewhere in
this document (re-latch, seq, note value, keyboard octave shift, button id readout, HOLD
while the arp is on, display messages) keep their meaning;
where their "Realisation" notes name V5 addresses, this section's realisation supersedes
them for the native image (`make image-native`). V5's documented behaviour is the baseline;
deviations are marked **(change)** with the reason.

#### Settings and defaults

- Arp settings are global, not per patch, and not saved: power-up = arp off, internal
  clock, 120 BPM, Up, 1 octave, note value 1/8, keyboard shift 0, no sequence.
- Mode, octaves, clock source, BPM and note value survive the arp being switched off and on.

#### Controls

- **A440** is the arp button: a tap (press and release with no arp combo in between)
  toggles the arp; its LED is lit while the arp is on. The stock tuning-reference tone is not
  available in play mode. **(change)** It remains available from the Globals menu, where all
  buttons behave as stock (below).
- **While A440 is held**: Bank = next mode, Group = previous mode (Up → Down → Up/Down →
  Random → Up…; display `UP`, `dn`, `Ud`, `rnd`); Program 1–4 = 1–4 octaves (`o 1`…`o 4`);
  Program 5 = toggle clock source (`int` / `Syn`); Program 6 = clear sequence; Program 7/8 =
  note value shorter/longer; any other button = id readout. Any of these cancels the toggle
  on A440 release. Held-repeat events (value 3) are ignored. A combo button whose release
  arrives after A440 has been released is still consumed **(change: V5 leaked the orphan
  release to stock)**.
- **Glide Rate** sets the tempo, 40–300 BPM (`BPM = 40 + round(260 · raw / 1023)`, raw
  0–1023), only while the arp is on *and* the clock source is internal **(change: V5 captured
  the pot whenever the arp was on, so under `Syn` glide was dead and BPM changed silently)**.
  Otherwise Glide Rate is the normal glide control, and tempo changes never disturb the
  patch's glide value.
- **Display**: transient messages (mode, octaves, clock, note value, BPM while the pot
  moves, step count, shift, readout) show for 1.2 s, then the display returns to the stock
  program display **(change: V5 left `OFF` / BPM / `Syn` on the display for as long as the
  arp was on)**. Switching the arp on shows the BPM (`Syn` under external clock) for 1.2 s;
  switching it off shows `OFF` for 1.2 s.
- **Globals menu**: while the stock Globals menu is open, every button including A440
  behaves exactly as stock (so the stock tuning tone is reachable from there); the arp keeps
  running with its current settings. Pressing GLOBALS while A440 is held abandons the hold:
  nothing toggles on the A440 release, and a recording in progress is kept as recorded.

#### Note pool

- The pool is the set of notes currently "down" for the arp: local keys (after keyboard
  octave shift) and MIDI-in notes, each with its velocity. A note present from both sources
  counts once.
- Arp off: nothing changes for stock — keys and MIDI notes sound directly. Arp on: keys and
  MIDI notes do not sound directly; they enter the pool. Switching the arp on releases any
  directly sounding notes and starts the pattern from the pool; switching it off releases
  the step note and re-sounds the keys that are physically held (not latched ones).
- Releasing a note removes it from the pool unless HOLD is active. A note that leaves the
  pool while it is the sounding step is released immediately. Removing the last note
  silences the arp.
- **HOLD** (button, or sustain pedal in `HLd` mode — the stock merged state): released notes
  stay in the pool (latched). Hold off: latched notes leave the pool. **Re-latch** ("Re-latch
  under HOLD", now native): with HOLD active, the first note that arrives after every pool
  note has been released replaces the pool.
- **Start rule**: with HOLD off and the pool empty, a new note starts the pattern
  immediately and the step phase restarts from that moment. In every other case (pool not
  empty, or HOLD active — including a re-latch) the new note is picked up at the next
  scheduled step and the phase is not disturbed. Under external clock notes never reset the
  phase: steps always fall on the clock grid.

#### Pattern

- **Order**: Up = pool ascending by pitch; Down = descending; Up/Down = ascending then
  descending without repeating the top and bottom notes (C E G E C E G …); Random = each
  step picks uniformly from the pattern, independently of the previous step; Seq = the
  recorded order ("Seq").
- **Octaves N**: the pattern is played in the base octave, then transposed +12 for each
  further pass, N passes in all (2 octaves: C3 D4 | C4 D5). Down plays the highest pass
  first. Up/Down bounces over the whole N-pass sequence. Random picks over all N passes.
  **(change: V5 merged the transpositions into one pitch-sorted set, giving C3 C4 D4 D5.)**
  Transposed notes above 127 are skipped.
- A single pool note repeats every step. Changing the pool mid-pattern keeps the current
  pass and position (position clamped to the new pool size). Changing mode or octaves
  restarts the pattern at its first step on the next step boundary without disturbing the
  phase **(change: V5 forced an immediate step)**.
- Each step releases the previous step note and plays the new one with the pool note's own
  velocity. Gate = 50 % of the step (as V5).

#### Clock

- **Internal**: step period = 60 s / BPM × beats per note value ("Note value" table),
  accumulated in 1 ms ticks with the remainder carried, so a long run does not drift.
  Changing BPM or note value takes effect from the next step without resetting the phase.
- **External (`Syn`)**: 24 clocks per quarter; one step every `24 × beats` clocks (1/8 = 12,
  1/16 = 6, 1/8T = 8, 1/16d = 9, 1/4 = 24 … 4 bars = 384); gate-off at half, rounded down.
  The clock count runs from Start: Start resets the count and the pattern position; Continue
  resumes both; Stop releases the sounding note and holds the position (no steps until
  Continue or Start). No clock → silence. 1 s without a clock releases the sounding note; the
  arp waits for the next clock. Clocks are accepted from whichever port (DIN or USB) sends
  first; the other port is ignored until 1 s of silence or a clock-source toggle. Changing
  the note value under external clock re-aligns at the next multiple of the new step length
  counted from Start, so the pattern stays on the DAW grid.
- F8/FA/FB/FC have no stock meaning (stock discards them) and are acted on only while the
  clock source is external.

#### Output and MIDI

- Step notes go to the stock voice allocator exactly as a local key would (`note_on(1, note,
  vel)` / `note_off(1, note)`). The stock keyboard MIDI Out keeps carrying the raw held keys
  (shifted, per "Keyboard octave shift"), not the arp; the arp never sends MIDI itself (as
  V5 — an "arp to MIDI Out" option is a possible later feature).
- Local Control off: stock does not pass local keys to the OS note path, so the arp is fed
  by MIDI-in only (as V5).
- MIDI CC 123–127 (all notes off, omni, mono, poly): stock all-notes-off runs, then the pool
  is cleared and the arp silenced; the HOLD state is not changed **(change: V5 hooked only
  CC 123 and dropped its own hold flag)**. CC 64 reaches the arp as hold via the stock merged
  state.
- Program change / patch load: the arp continues on the new patch with its keys down, mode
  and tempo. Stock switches HOLD off when a program loads, so latched notes drop then (as
  stock and V5 behave).

#### Robustness

- Events from the other task are queued to the tick; the queue holds 64 events; if it ever
  fills, the newest events are dropped and the arp is **not** disabled **(change: V5 cleared
  and disabled the arp on overflow)**.
- **Kill switch**: A440 held at power-on, **or pressed within the first 3 s after
  power-on**, disables every arp hook for the session (all hooks fall straight through to
  stock; the A440 press itself reaches stock too), so a misbehaving engine can be bypassed
  without re-flashing. Detected two ways: the stock button-held table, and the A440 press
  event itself. `[HW: 2026-10-07 — the table alone (1 s window) did not detect a button
  held from before power-on: a panel reports changes, and its link comes up late]`
- No code runs at boot; all state is zero in the image and initialised lazily by the first
  hook that runs.

#### Realisation (facts asserted by the image tests)

- Base image `fixtures/prophet5_main_2.1.0.syx`; everything added lives in one appended
  record `0x20088000–0x20090000` (code `0x20088000–0x2008E000`, state `0x2008E000–0x20090000`,
  zero in the image); nothing else differs except the hook sites below (`python3 -m tools
  diff` lists exactly them). The MMU already maps `0x20020000–0x2008FFFF` as one RAM region.
- Hook sites (Thumb target expected in stock at each): keyboard FIFO count `0x2003BE9C`
  (`0x2003D829`) = the 1 kHz tick; local note-on `0x2003BECC` (`0x2003EC5D`, src=1, note,
  vel); local-key MIDI Out `0x2003BEFA` (`0x20033F85`) and `0x2003BF16` (`0x20033F39`), args
  (cable, channel, note, vel) — octave shift applies to the note; MIDI note-on `0x2003B07A`
  (`0x2003EC5D`, src=2), MIDI note-off `0x2003B032` (`0x2003EEDD`); CC 123 `0x2003B294` and
  CC 124–127 `0x2003B144` (`0x2003EBE5`); hold `0x200396CA` (`0x2003D325`, r4 = merged hold
  state); panel button `0x2003C244` (`0x2003BC31`, (id, value)); pot store `0x2003C292`
  (`0x20036B51`, (pot, raw)); pot change `0x2003C2A6` (`0x2003BC6D`, (pot, old, new)); MIDI
  parser table words `0x200343D8/E0/E4/E8` (F8/FA/FB/FC, stock `0x20034343`). The site
  `0x2003BED8` used by the V5-based image for octave shift is **not** a MIDI Out call (it
  posts a message no state handles) and is not hooked.
- Stock functions used: note_on `0x2003EC5D`, note_off `0x2003E95D`, all_notes_off
  `0x2003EBE5`, LED `0x20036825(led, 0 off / 1 on)` with A440 LED `0x24`, display `0x20037F25`
  / `0x20037FF7`, display restore `0x2003818D(ui = 0x20057390)`, Globals-open test: word
  `0x20057438 ≠ 0`, button-held table `0x20079B20` (kill switch), get_global `0x20037B21`.
  Glide Rate = pot id `0x16`.
- Task contexts: the tick, local notes, panel buttons/pots and realtime bytes run in the
  FreeRTOS Timer Service task; MIDI notes, CC and hold run in the Prophet5 active-object
  task. Only the latter are queued (a 64-entry single-producer ring drained by the tick);
  the former act on the engine directly. The lazy init runs under `cpsid i` / `cpsie i`
  with the previous state restored; hooks never block.
- Build: `make image-native` → `build/prophet10_native.syx` from `firmware/native.c`
  (`fwbuild --profile native`) and `firmware/hooks_native.json` (`build --record
  0x20088000:0x8000`). Sources: `arp.c` (engine), `arpui.c` (controls, display, LED, kill
  switch), `oct.c`, `rate.c`, `disp.c`, `vhold.c`. The V5-based images remain available
  (`make image`, `make image-internal`) until this section is hardware-verified.

### Note value (arp/seq step length) `[HW: verified 2026-10-07, Prophet-10 Rev4 — internal clock; MIDI sync not exercised]`

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

### Keyboard octave shift `[HW: verified 2026-10-07, Prophet-10 Rev4 — Lo Freq modifier, tap replay, shifted keys and MIDI Out]`

1. Hold the Osc B **Lo Freq** button (panel id 37) and press **Bank** to shift the
   keyboard up one octave, **Group** to shift it down; range −2…+2. (Lo Freq sits next to
   Bank/Group, so the shift is a one-handed move. Versions before 2026-10-07 used the
   filter Keyboard Amount button, id 8.) Pressing Bank and
   Group together (the second while the first is still down) resets the shift to 0. The
   display shows the shift (`-2`…`2`) for about a second after each press (also at the
   ends, unchanged). Power-up: 0. Not saved with patches.
2. The shift applies to keys played on the keyboard: what the synth plays, what the arp/seq
   receive (re-latch, recording, triggers), and what is sent to MIDI Out for those keys.
   MIDI-in notes are not shifted. A key's release always uses the shift that was in force
   when it was pressed, so changing the shift while holding keys never sticks a note.
3. A tap of Lo Freq (press and release with no Bank/Group press in between) still toggles
   Osc B low-frequency mode as in stock; the setting and its LED change on the release
   rather than on the press. While it is held with Bank/Group, the Lo Freq setting does
   not change. Repeat events (value 3) of any of the three buttons are ignored.
4. Because the shift is applied before the voice engine, a Prophet-10 split point moves
   with the keyboard. A key whose shifted note would fall outside 0–127 is silent (not
   reachable from the 61-key range at ±2).
5. In Local-Off mode (keys go to MIDI Out only) the shift still applies to what is sent.
   Lo Freq held together with A440 is undefined.
6. Realisation: the local-note hook shifts the key and records the per-key shift
   (128-entry table) so the release maps identically; `bl` hooks on the two stock calls
   that send a local key to MIDI Out — note-on `0x2003BEFA` (expecting `0x20033F85`) and
   note-off `0x2003BF16` (expecting `0x20033F39`), both `(cable_mask, channel, note, vel)`
   — apply the same per-key mapping to `note`. These sites are reached whether Local
   Control is on or off. (The site used before, `0x2003BED8` → `0x2003BCE1`, is not a MIDI
   Out call: it posts a message no state handles. `[HW: bug found 2026-10-07 by
   disassembly before that image was installed]`.) The button hook swallows id 37 (press,
   repeats, release), treats Bank/Group presses while it is held as shift commands, and on
   a release with no shift command replays press+release of id 37 through V5's button
   entry (which forwards non-arp buttons to stock when A440 is not held).

### HOLD while the arp is on `[HW: verified 2026-10-07, Prophet-10 Rev4]`

1. While the arp is on, HOLD (button, or sustain pedal in `HLd` mode) is the arp's latch and
   nothing else: the synth's own hold — the voice sustain that normally keeps released keys
   sounding — is suspended. Each arp step therefore releases the previous step's note and
   one note sounds at a time, exactly as with HOLD off. `[HW: bug seen 2026-10-07 — with
   HOLD active the steps accumulated (C, then C+E, then C+E+G) because the stock sustain
   kept every step sounding]`
2. The HOLD LED and the latch still follow the button/pedal as in "Re-latch under HOLD".
3. Switching the arp off while HOLD is active restores the synth's hold at once (keys held
   or latched at that moment sustain as stock hold would). Switching the arp on while HOLD
   is active suspends it at once (notes sustained only by hold are released; the pattern
   starts from the keys down and latched notes as before).
4. With the arp off nothing changes: stock hold behaves exactly as before.
5. Realisation: stock `note_off` asks the hold state (`bl 0x2003B694` at `0x2003EACE`) and,
   when it is on, hands the voice to the voice engine's sustain instead of releasing it; the
   voice engine is told about hold by the message `0x080D0000 | state` posted from the hold
   handler (`0x200396CA`). The wrapper hooks `0x2003EACE` (expecting `0x2003B695`) to answer
   "off" while the arp is enabled, skips the hold message in its hold hook while the arp is
   enabled (still passing the state to the arp's hold event), and on every arp enable/disable
   transition with HOLD active posts the message itself (`0x2003D325`: off on enable, on on
   disable).

### Display messages `[HW: verified 2026-10-07, Prophet-10 Rev4]`

1. Every message the arp or the wrapper puts on the display — mode (`UP dn Ud rnd`), octaves
   (`o N`), clock (`int`/`Syn`), `OFF`, BPM (while Glide Rate turns, and when the arp is
   switched on), note value, seq step count, keyboard shift, button id — shows for 1.5 s
   after the last change and then the display returns to the stock patch display (bank,
   group and program). Nothing stays on the display permanently. `(change from V5, which
   kept OFF / BPM / Syn up for as long as the arp was on)`
2. A new message restarts the 1.5 s.
3. Realisation: the wrapper keeps its own timer, fed by its own display calls and by
   watching the arp's displayed settings (enabled, mode, octaves, clock source, BPM) change
   each tick; while it runs, V5's display timer is held above zero so V5 never draws its
   status text; at expiry V5's timer is zeroed and the stock display restore
   `0x2003818D(ui = 0x20057390)` is called.

### Button id readout `[HW: unverified]`

While A440 is held, pressing a panel button that the arp does not assign — anything other
than Program 1–8, Bank, Group, GLOBALS (id 13 / `0x0D`, which must still open its menu) and
Lo Freq (id 37, consumed by the keyboard octave shift) — shows that button's id on the
display and is otherwise ignored. `[HW: 2026-10-07 — ids read out as expected (GLOBALS
shows 13); the V5-based image excludes UNISON (`0x19`) instead of GLOBALS, a leftover of
the mis-identified id, so there Unison toggles and GLOBALS is read out; correct in the
native build only]` An aid for
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

### Seq (step-recorded sequence) `[HW: verified 2026-10-06/07, Prophet-10 Rev4 — record, transposed playback, clear, octaves (o N, no toggle on A440 release, Program 6); MIDI-in recording not exercised]`

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

