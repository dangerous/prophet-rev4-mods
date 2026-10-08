# prophet-rev4-mods — Spec

## Overview

An arpeggiator and step sequencer for the Sequential Prophet-5/10 Rev4: this project's own
code hooked into Sequential's stock Main OS 2.1.0, plus the tooling that unpacks, patches
and re-packs the OS SysEx image. Consumers are the player at the
instrument (behaviour is observable as notes sounding, display text and LED state in
response to keys, panel buttons, HOLD/pedal and MIDI) and the instrument's OS loader
(which consumes the `.syx` file, making the file format an external contract).

## System type

Embedded firmware patch (ARM Cortex-A5 Thumb-2, loaded into RAM by the stock loader) plus
a Python build/packing CLI.

## Conventions

- "Stock" = Sequential Main OS 2.1.0. "The engine" = the code this project adds, one
  record appended to stock. "The Arp Mod" = the third-party arpeggiator mod for the Rev4
  that circulated privately before this project, whose documented behaviour the arp was
  modelled on; named only where behaviour deliberately differs from it.
- "HOLD" = the panel button labelled **RELEASE / HOLD** (id 14), which the Release/Hold
  global makes a hold latch; "the pedal" = the sustain pedal, in `HLd` mode as "HOLD".
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

Fixture facts: Main 2.1.0 `groups=30180, tail=4`; Panel 1.1.3 `groups=2498, tail=2`.

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

- Sequential's Main and Panel OS `.syx` files are copyrighted and not part of the repository.
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

### Image patching (hook chaining, the engine record)

The build takes the **stock** OS file, the **engine binary** with its symbol map, and a
**hook list**, and produces a new OS file. Nothing is changed that the hook list and the
record placement do not require.

#### Engine record

- The engine is loaded by **one COPY record appended to image A** after stock's last
  record: load address `0x20088000`, length `0x8000`. Image A's EXEC declared length grows
  by the record size + 16 and its check byte is recomputed.
- Why that address: the stock startup's MMU region table maps `0x20020000–0x2008FFFF` as
  one RAM region; stock uses nothing above `0x200874CC`. The region is physically
  contiguous L2 SRAM. `[HW: verified 2026-10-07]`
- Code occupies `[0x20088000, 0x2008E000)`, state `[0x2008E000, 0x20090000)`. State is zero
  after every boot. The build fails if code+rodata exceed the code area.

#### Patch kinds

- `bl` — retarget a 4-byte Thumb-2 `BL` (below).
- `word` — replace a 32-bit word at a RAM address inside the stock code record with an
  engine symbol's address, after verifying the word currently holds the `expect`ed value.
  Used for the stock MIDI-parser status-table entries for F8/FA/FB/FC.

#### Hook retargeting

- A hook list entry names a `site` (RAM address of a 4-byte Thumb-2 `BL` inside the stock
  code record, `0x2002EF00..`), the `expect`ed current target (a stock entry point, Thumb
  bit set) and the engine `symbol` to call instead.
- The build locates the record containing the site, verifies the instruction there is a
  `BL` whose target equals `expect`, and rewrites it as a `BL` to the symbol's address
  from the map. It fails if the site is not such a `BL`, if the symbol is missing, if the
  site lies outside the stock code record, or if the target is out of `BL` range.

#### CLI

- `python3 -m tools build --base BASE.syx --wrapper E.bin --map E.map --hooks hooks.json
  -o OUT.syx [--record BASE:SIZE]` — performs the above and writes `OUT.syx` (trailer and
  header recomputed). `--record` defaults to `0x20088000:0x8000`. The map is `nm`-style
  text: `<hex address> <symbol>` per line. Each hook entry has an optional
  `"kind": "bl" | "word"` (default `bl`).
- `python3 -m tools fwbuild SRC OUT` cross-compiles `SRC/native.c` for thumbv7a, links it
  at `0x20088000` with the code limit `0x2008E000`, checks the hook symbols are present and
  writes `native.bin`, `native.map` and `native.layout` into `OUT`. The compiler is told
  not to emit jump tables, so the code consists of instructions and literal pools only and
  the image test's sweep for materialised addresses stays reliable (a `tbb` table once read
  as a literal load).
- `python3 -m tools diff A.syx B.syx` — prints every differing byte span of the decoded
  payloads as `image <n> record @0x<offset> ram 0x<lo>..0x<hi>: <n> bytes`, so a reviewer
  can see exactly what a build changed. Record-structure differences are reported as such.

### Patcher (distribution) `[HW: n/a]`

Users install the engine without the toolchain: a static web page applies a published
**patch manifest** to their own copy of Sequential's Main OS 2.1.0 file, in the browser,
and hands back the image to install. Nothing is uploaded, the page loads no external
resources, and it works equally from a local copy of its directory.

#### Patch manifest

- The project has a **release version** — semantic versioning, in the `VERSION` file at the
  repository root, bumped by hand for each release (`1.0.0` first).
- `python3 -m tools manifest --base BASE.syx --image IMAGE.syx -o site/manifest.js` writes a
  JavaScript file that assigns `window.PATCH` one JSON object: `format` (1), `name`
  (`prophet10_native`), `version` (from `VERSION`), `built` (ISO date), `commit` (the
  repository's short HEAD hash, or `unknown`), `base` and `result` — each `{name, size,
  sha256}` of the stock file and of the image, the result being named
  `prophet5_main_2.1.0_patched_<version>.syx` — and `spans`: the edits that turn the base's
  **decoded payload** into the image's,
  each `{offset, data}` with `data` base64 and `offset` a position in the base payload, in
  ascending order and non-overlapping. A span **replaces** the bytes at its offset
  (differences less than 16 bytes apart share one span); the engine record is the last span
  and an **insertion** (`insert: true`) at the end of image A, so the SHARC image that
  follows it is shifted, not copied. The manifest therefore carries the engine's own bytes
  and the retargeted words — a few hundred bytes of edits plus the 32 KB record — and
  nothing else of the OS.
- **Applying** a manifest: decode the base file under every container rule ("SysEx
  container"); require SHA-256(base file) = `base.sha256`; apply the spans in order
  (replacing, or inserting before the base byte at `offset`); encode as a Main OS file;
  require SHA-256(result) = `result.sha256`. The result is byte-identical to `python3 -m
  tools build` for the same inputs.
- Beside the manifest it writes `version.json` — `{version, built, commit, result: {name,
  sha256}}` — so the README's version badge, and anything else, can read what the page
  installs without parsing JavaScript.
- `python3 -m tools apply MANIFEST.js BASE.syx OUT.syx` is the reference applier. It writes
  nothing and prints one line on stderr (exit status 1) when the base is not the file the
  manifest was made for (naming the file's hash and the expected one) or when the result's
  hash does not match the manifest's.

#### The page (`site/index.html`, `site/patcher.js`, `site/manifest.js`)

- Published by GitHub Pages at `https://dangerous.github.io/prophet-rev4-mods/` from the
  `site/` directory **alone** (a GitHub Actions deployment of that directory; nothing else
  in the repository is served).
- Shows what it builds (version, date, commit) and the two hashes it will check. The user picks
  or drops their stock `.syx`; the page applies the manifest under the rules above, in the
  browser, and then: a base-hash mismatch names the problem (not Sequential's Main OS
  2.1.0 — the file's hash and the expected one), nothing is offered; a result-hash mismatch
  says so and offers no download; success shows the result's SHA-256 marked as matching
  the release and offers `prophet5_main_2.1.0_patched_<version>.syx` for download, with the install steps
  (SysEx Librarian or equivalent, Globals → MIDI SysEx = USB) and the warranty/risk notice.
- The page's applier (`patcher.js`, plain JavaScript, no framework) produces the same bytes
  as the reference applier; the acceptance test runs it under Node.js against the fixture
  when Node is available and skips otherwise.
- The stock file never leaves the browser: no requests other than the page's own files, no
  analytics.

### Arp engine `[HW: verified 2026-10-07, Prophet-10 Rev4 — the whole checklist]`

The arp proper. The behaviours specified in their own sections (re-latch, seq, note
value, keyboard octave shift, HOLD while the arp is on, display messages, button id
readout) are realised by this engine. The Arp Mod's documented behaviour was the baseline; where
this engine deliberately differs it is marked **(change)** with the reason.

#### Settings and defaults

- On/off, mode, octaves and note value are stored with each program ("Patch memory"); BPM,
  clock source, keyboard shift and the sequence are global and not saved. Power-up = the
  recalled program's arp settings (off for a program without arp data), internal clock,
  120 BPM, keyboard shift 0, no sequence.
- Mode, octaves, clock source, BPM and note value survive the arp being switched off and on.

#### Controls

- **A440** is the arp button: a tap (press and release with no arp combo in between)
  toggles the arp; its LED is lit while the arp is on. **(change)** The stock
  tuning-reference tone moves to **A440 + HOLD**: with the arp **off**, pressing and
  releasing the HOLD button while A440 is held toggles stock's reference tone on HOLD's
  release — the tone and its LED (the A440 LED, as in stock) are then stock's. **While the
  tone sounds, a tap of A440 only switches it off**: the arp stays off, and the next tap
  toggles the arp as usual `[HW: verified 2026-10-08, Prophet-10 Rev4]` (the first build enabled the arp
  on that tap, with the LED dark). Should the arp come on by another route while the tone sounds (a
  program load with the arp saved on), the tone is silenced first, so a lit LED means the
  arp from then on. With the arp on the combo is consumed and does nothing; in record mode
  HOLD is rest/tie ("Seq"). It counts as using the A440 hold (no toggle on A440's release)
  `[HW: verified 2026-10-08, Prophet-10 Rev4 — tone on and off, LED, arp untouched; inert
  with the arp on]`. The tone is
  not reachable any other way: not by a tap of A440, and not from the Globals menu either —
  A440 is passed to stock there, but stock ignores it while its menu is open `[HW: 2026-10-08,
  Prophet-10 Rev4 — menu open, A440 pressed: no tone, no LED, arp unchanged. Earlier text
  claimed the tone was reachable from Globals; that came from reading the state machine, not
  from the instrument]`.
- **While A440 is held**: Bank = next mode, Group = previous mode (Up → Down → Up/Down →
  Random → Assign → Up…, Group the other way; display `UP`, `dn`, `Ud`, `rnd`, `ASS` —
  Assign `[HW: verified 2026-10-07, Prophet-10 Rev4]`); Program 1–4 = 1–4 octaves (`o 1`…`o 4`);
  Program 5 = toggle clock source (`int` / `Syn`); Program 6 = clear sequence; Program 7/8 =
  note value longer/shorter (− / +, + is faster); Velocity = tempo tap (below) `[HW: verified 2026-10-08, Prophet-10 Rev4 — moved from Unison]`; Tune = seq record mode on/off ("Seq") `[HW: verified 2026-10-08, Prophet-10 Rev4]`; HOLD = stock tuning tone on/off (arp off; above) `[HW: unverified]`; any other button = id readout. Any of these cancels the toggle
  on A440 release. Held-repeat events (value 3) are ignored. A combo button whose release
  arrives after A440 has been released is still consumed **(change: the Arp Mod leaked the orphan
  release to stock)**.
- **A440 + Glide Rate** sets the tempo `[HW: verified 2026-10-07, Prophet-10 Rev4]`: while
  A440 is held, turning Glide Rate sets the BPM of the internal clock, 40–300 (`BPM = 40 + round(260 · raw / 1023)`, raw
  0–1023), whether the arp is on or off. **Under external clock (`Syn`) it does nothing to
  the BPM** `[HW: verified 2026-10-07, Prophet-10 Rev4]` (the tempo follows the clock, "Clock"): the display shows
  `Syn` as the hint instead. The display shows the BPM while
  the pot turns (a display message like any other). Turning the pot counts as using the A440
  hold, so the A440 release does not toggle the arp, also under `Syn`. Both pot hooks (raw store and change
  post) are consumed exactly while A440 is held (and the kill switch is not engaged); the
  patch's glide value is not touched. **Glide Rate alone is always the normal glide control**,
  also while the arp is running. **(change: the Arp Mod, and this engine before 2026-10-07, captured
  the pot as tempo whenever the arp was on, which made glide unusable with the arp running.)**
- **A440 + Velocity is tap tempo** `[HW: verified 2026-10-07, Prophet-10 Rev4 on Unison; moved to Velocity 2026-10-08 (one hand: it sits beside A440) and verified there the same day]`: while A440 is held, each press of
  the Velocity button (id 11 / `0x0B`) is a tempo tap. The first tap of a series shows `tAP` and
  records the time; the BPM is unchanged. The **second tap sets the tempo immediately** from
  that single interval; each later tap uses a rolling mean of the most recent four
  intervals of the series (so it steadies without resisting a deliberate change), `BPM =
  round(60000 / mean ms)`, clamped to 40–300, and the display shows the new integer BPM (a
  display message like any other). An interval of more than 2000 ms ends the series: that tap starts
  a new one (shows `tAP` again, BPM unchanged); an interval of exactly 2000 ms still counts
  (30 BPM → 40). Intervals are measured on the UI's 1 ms tick. The series only ends by such a
  gap (releasing A440 in between does not end it). A tap counts as using the A440 hold, so the
  A440 release does not toggle the arp; Velocity's press, held repeats and release are
  consumed like any combo button, and Velocity without A440 held is always stock Velocity. Taps
  work whether the arp is on or off. **Under external clock (`Syn`) a tap does nothing** but
  show `Syn` as a hint (a display message like any other): the BPM is unchanged and no
  series is started or continued (a running series ends, so the next tap under `int` shows
  `tAP`); the press still counts as using the A440 hold and is consumed with its repeats
  and release as usual (A440 + Glide Rate is inert under `Syn` the same way). The
  BPM is not saved with the program (see above).
- **Display**: transient messages (mode, octaves, clock, note value, BPM while the pot
  moves or on a tempo tap, `tAP`, `Syn` for a tempo gesture under external clock, step count, shift, readout) show for 1.5 s, then the display returns to the stock
  program display **(change: the Arp Mod left `OFF` / BPM / `Syn` on the display for as long as the
  arp was on)**. Switching the arp on shows the BPM (`Syn` under external clock) for 1.5 s;
  switching it off shows `OFF` for 1.5 s.
- **Globals menu**: while the stock Globals menu is open, every button including A440 is
  passed to stock untouched (stock ignores A440 there); the arp keeps running with its
  current settings. Pressing GLOBALS while A440 is held abandons the hold:
  nothing toggles on the A440 release, and a recording in progress is kept as recorded.

#### Note pool

- The pool is the set of notes currently "down" for the arp: local keys (after keyboard
  octave shift) and MIDI-in notes, each with its velocity. A note present from both sources
  counts once.
- Arp off: nothing changes for stock — keys and MIDI notes sound directly. Arp on: keys and
  MIDI notes do not sound directly; they enter the pool. (In seq record mode, arp on or
  off, they are recorded instead — "Seq".) Switching the arp on releases any directly
  sounding notes and starts the pattern from the pool; switching it off releases the step
  note and re-sounds the keys that are physically held (not latched ones).
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
- **Assign** (as the Prophet-6's Assign mode) `[HW: verified 2026-10-07, Prophet-10 Rev4]`: the pool notes in the
  **order they were entered** (note-on order), each with the velocity of its own entry:
  play G C E D → G C E D G C E D … Duplicates are allowed: with HOLD active a latched
  pitch pressed again (while another key is down, so no re-latch) becomes a further entry
  (G C E D, then C again → G C E D C). Releasing a key with HOLD off removes every entry
  of that pitch (G C E D, release E → G C D); switching HOLD off removes the entries of the
  latched pitches (keys no longer down); a re-latch replaces the entries with the new
  notes, as it replaces the pool; MIDI CC 123–127 clears them. Direction is forward only
  (Assign is itself the order). Octaves apply per pass over the whole ordered pattern
  (G C E D at `o 2` → G C E D, then each +12). With a recorded sequence active, Assign
  plays the recorded order (as Up does). Up to 32 entries: a note entered while 32 exist
  still joins the pool (it counts for the start rule, re-latch and the other modes) but is
  not added to the Assign order.
- **Octaves N**: the pattern is played in the base octave, then transposed +12 for each
  further pass, N passes in all (2 octaves: C3 D4 | C4 D5). Down plays the highest pass
  first. Up/Down bounces over the whole N-pass sequence. Random picks over all N passes.
  **(change: the Arp Mod merged the transpositions into one pitch-sorted set, giving C3 C4 D4 D5.)**
  Transposed notes above 127 are skipped.
- A single pool note repeats every step. Changing the pool mid-pattern keeps the current
  pass and position (position clamped to the new pool size; in Assign removing entries
  never skips one: the entry that followed the note just played is still the next step). Changing mode or octaves
  restarts the pattern at its first step on the next step boundary without disturbing the
  phase **(change: the Arp Mod forced an immediate step)**.
- Each step releases the previous step note and plays the new one with the pool note's own
  velocity. Gate = 50 % of the step (as the Arp Mod).

#### Clock

- **Internal**: step period = 60 s / BPM × beats per note value ("Note value" table),
  accumulated in 1 ms ticks with the remainder carried, so a long run does not drift.
  Changing BPM or note value takes effect from the next step without resetting the phase.
  Swing values (16th S, 8th S, "Note value") alternate a long and a short step, 2 : 1
  within each pair; a start-rule step or switching the arp on begins a pair (long step
  first); pool, mode and octave changes do not change which half comes next.
  `[HW: verified 2026-10-07, Prophet-10 Rev4]`
- **External (`Syn`)**: 24 clocks per quarter; one step every `24 × beats` clocks (8th = 12,
  16th = 6, 8th T = 8, 8th D = 18, Qtr = 24, Half = 48); gate-off at half, rounded down.
  Swing values split each pair of steps 2 : 1 with the pair boundary at multiples of the pair
  length counted from Start (16th S: 12-clock pairs, steps at 0, 8, 12, 20, 24 …; 8th S:
  24-clock pairs, steps at 0, 16, 24, 40, 48 …), gate-off at half of each step, rounded
  down. `[HW: verified 2026-10-07, Prophet-10 Rev4]`
  **The BPM follows the external clock** `[HW: verified 2026-10-07, Prophet-10 Rev4]`, measured over each beat on the
  1 ms tick: the intervals between accepted clocks are summed over the most recent 24 (one
  beat), and once a full beat has been measured BPM = round(60000 / beat ms), clamped to
  40–300, updated with every further clock (a rolling window). It has no effect while
  synced (steps follow the clocks); it is what the internal clock resumes at when the clock
  source returns to `int` (DAW at 100 BPM, switch to `int`: the arp continues at 100).
  Start, Continue, Stop, clock loss (1 s) and a clock-source toggle restart the
  measurement (a full beat is measured again before the BPM changes); the clocks while
  stopped still count towards it.
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
  the Arp Mod — an "arp to MIDI Out" option is a possible later feature).
- Local Control off: stock does not pass local keys to the OS note path, so the arp is fed
  by MIDI-in only (as the Arp Mod).
- MIDI CC 123–127 (all notes off, omni, mono, poly): stock all-notes-off runs, then the pool
  is cleared and the arp silenced; the HOLD state is not changed **(change: the Arp Mod hooked only
  CC 123 and dropped its own hold flag)**. CC 64 reaches the arp as hold via the stock merged
  state.
- Program change / patch load: the arp continues on the new patch with its keys down, mode
  and tempo. Stock switches HOLD off when a program loads, so latched notes drop then (as
  stock and the Arp Mod behave).

#### Robustness

- Events from the other task are queued to the tick; the queue holds 64 events; if it ever
  fills, the newest events are dropped and the arp is **not** disabled **(change: the Arp Mod cleared
  and disabled the arp on overflow)**.
- **Kill switch**: A440 **held from before power-on** disables every arp hook for the
  session (all hooks fall straight through to stock), so a misbehaving engine can be
  bypassed without re-flashing. A press after power-on — even within the first seconds —
  is an ordinary A440 press. Realisation: during the first 3 s of ticks (the panel link
  comes up later than 1 s) the stock button-held table is polled; A440 down there without
  a press event having been reported since power-on means it was held from the start.
  `[HW: verified 2026-10-07, Prophet-10 Rev4 — held from power-on kills; a tap 1 s after
  power-on does not. A 1 s table-only window had missed the pre-held button (the panel
  link comes up later); the panel reports no synthetic press for a pre-held button]`
- No code runs at boot; all state is zero in the image and initialised lazily by the first
  hook that runs.

- **Tuning tone, realisation**: stock's main state toggles the DSP reference tone on an A440
  press when HOLD is *not* physically held (held, it plays a voice note instead), keeping
  the state in a flag at `ui + 0x16A` and the LED. On HOLD's release of the combo the UI has
  the glue replay a stock A440 press through the button post (`0x2003BC31(0x0F, 1)`) — HOLD
  is up by then, so stock takes the tone path. A tap of A440 with the flag set replays the
  press too (tone off) instead of toggling the arp; an enable from a program load with the
  flag set replays it before enabling. Stock handles a replayed press a few milliseconds
  later in its own task and switches the LED off then, so 100 ms after a replay the UI
  asserts the arp LED again if the arp is on by then (otherwise the LED stays stock's,
  showing the tone).

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
  (`0x20036B51`, (pot, raw)); pot change `0x2003C2A6` (`0x2003BC6D`, (pot, old, new)); end of program
  apply `0x2003D15C` (`0x2003B6B1`, "Patch memory"); MIDI parser table words `0x200343D8/E0/E4/E8` (F8/FA/FB/FC, stock `0x20034343`). The site
  `0x2003BED8` is **not** a MIDI Out call (it posts a message no state handles) and is not
  hooked.
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
- Build: `make image` (alias `make image-native`) → `build/prophet10_native.syx` from
  `firmware/native.c` (`fwbuild`) and `firmware/hooks_native.json`. Engine state stays
  within 0x400 bytes. Assign keeps an entry list of up to 32 (pitch, velocity) pairs next
  to the pool, maintained on every note-on/off (also while the arp is off, so switching it
  on with keys held plays them in entered order); in Assign mode with no sequence it is the
  base order, stepped by index like the sequence. Sources: `arp.c`
  (engine), `arpui.c` (controls, display, LED, kill switch), `oct.c`, `rate.c`, `disp.c`,
  `vhold.c`.

### Patch memory `[HW: verified 2026-10-07, Prophet-10 Rev4]`

1. Saved with a program: the arp **on/off**, **mode**, **octaves** and **note value**. Not
   saved: BPM, clock source, keyboard octave shift, the sequence.
2. Loading a program — from the panel, a MIDI program change, a SysEx program or edit-buffer
   receive, the PRESET toggle or the power-on recall — applies the program's arp settings and
   switches the arp on or off accordingly (on with keys held starts the pattern as "Arp
   engine" says). A program **without arp data** (every factory program, and user programs
   saved before this feature) loads with the arp **off** and mode, octaves and note value
   **untouched**.
3. Changing any saved setting edits the live program silently, as turning a knob does; the
   stock Record flow stores it with the program, SysEx program dumps carry it, and a received
   dump restores it. There is no "edited" indication (stock has none beyond the pot
   pass-through).
4. Realisation: program parameters **93** (0–127) and **94** (0–8) of layer A, which the stock
   OS stores, dumps and loads verbatim but never reads: **94 = octaves (1–4) + 4 × L**, where
   L = 0 for one of the Prophet-6's ten note values and L = 1 for a long one (so 1–8; **0 =
   no arp data**); **93 = n × 10 + mode × 2 + on/off**, where n is the Prophet-6 position
   (0 = Half … 9 = 32nd) when L = 0 or the long value (0 = Whole, 1 = 2 bars, 2 = 4 bars)
   when L = 1, mode 0–4 = Up, Down, Up/Down, Random, Assign, and on/off 0/1 — so 93 is 0–99,
   or 0–29 with L = 1. Anything else counts as no arp data. Programs saved before the long
   values existed (94 in 1–4) load unchanged. `[HW: verified 2026-10-07, Prophet-10 Rev4 for
   L = 0; 2026-10-08 for L = 1 and for an older program loading unchanged]`
   Programs saved under the earlier bitfield layout of 93 (on/off | mode << 1 | note code
   << 3 — only test saves made on 2026-10-07) are not converted: they load with the wrong
   settings (or as no arp data) and must be re-saved. Written
   through stock's plain parameter store `0x2003CEF5(layer, param, value)` (no clamp, no NRPN echo) on every change; read with
   `0x2003CB69(layer, param)` from a hook on the unconditional `bl 0x2003B6B0` at
   `0x2003D15C` at the end of stock's program-apply routine, which every load path reaches
   with the live table final (the hook runs in the Prophet5 task and queues "program loaded"
   to the tick). Stock's own load clamps the slots to their maxima. With PRESET off, stock
   restores the live table from its panel shadow on every load, so arp settings behave like
   every other parameter in that mode (the stored ones are ignored). `[HW: 2026-10-07 — the
   voice engine receives 93/94 like every parameter and showed no audible reaction to them]`

### Note value (arp/seq step length) `[HW: verified 2026-10-07, Prophet-10 Rev4 — internal clock and MIDI sync, with the earlier 15-value list; the Prophet-6 list, order and display below: verified 2026-10-07]`

1. The step length is one of thirteen values, from longest to shortest. Three long ones for
   pad sequences — **4 bars** (16 beats), **2 bars** (8 beats), **Whole** (1 bar, 4 beats)
   `[HW: verified 2026-10-08, Prophet-10 Rev4]` **(change: added above the Prophet-6's list, so 64 steps of 4 bars make a
   256-bar sequence)** — then the Prophet-6's ten values, in the Prophet-6's order:
   **Half** (1/2 note, 2 beats), **Qtr** (1/4, 1 beat), **8th D**
   (dotted eighth, 3/4 beat), **8th** (1/2 beat), **8th S** (eighth swing), **8th T**
   (eighth triplet, 1/3 beat), **16th** (1/4 beat), **16th S** (sixteenth swing), **16th T**
   (1/6 beat), **32nd** (1/8 beat) `[HW: verified 2026-10-07, Prophet-10 Rev4]`. Power-up default is 8th. Saved with
   the program ("Patch memory").
   **Swing** `[HW: verified 2026-10-07, Prophet-10 Rev4 — internal clock]` (as the
   Prophet-6's 2 : 1 swing): 8th S and 16th S play steps in pairs, the first step of a pair
   lasting 2/3 of the pair and the second 1/3. A 16th S pair is two sixteenths (1/2 beat:
   1/3 + 1/6 beat), an 8th S pair two eighths (one beat: 2/3 + 1/3 beat), so the average
   step equals the plain value. The pattern order is unaffected.
2. A440 + **Program 8** selects the next value in the list (shorter, +), A440 + **Program 7**
   the previous one (longer, −) — + is faster; the ends do not wrap (4 bars and 32nd stay).
   The Prophet-6 part of the list keeps its order, so a step does not always change the
   length monotonically: 8th S (average 1/2 beat) sits between 8th and 8th T, 16th S between
   16th and 16th T. The display shows the new value, right-aligned in three characters:
   `4b`, `2b`, `1` (the long values; `1b` would read like `16`) `[HW: verified 2026-10-08, Prophet-10 Rev4]`, `2`, `4`,
   `8d`, `8`, `8S`, `8t`, `16`, `16S`, `16t`, `32` `[HW: verified 2026-10-07, Prophet-10 Rev4]`.
3. Internal clock: the step period is the note value at the current BPM; the note is
   released half-way through the step (each swing step at half of its own length). At
   120 BPM an 8th S pair is 500 ms (steps 334 ms and 166 ms with the remainder carried), a
   16th S pair 250 ms.
4. MIDI sync: steps follow the incoming clock at the selected value — 4 bars 384, 2 bars
   192, Whole 96, Half 48, Qtr 24,
   8th D 18, 8th 12, 8th S 24-clock pairs split 16 + 8, 8th T 8, 16th 6, 16th S 12-clock
   pairs split 8 + 4, 16th T 4, 32nd 3 clocks per step — counted from Start (swing pairs
   start at multiples of the pair length), so step boundaries fall exactly on clocks and stay
   on the DAW grid across a change. `[HW: verified 2026-10-07, Prophet-10 Rev4 — incl. the swing values]`
5. A change takes effect from the next step. Applies to the arp and to seq alike.
6. Realisation: `rate.c` holds the list in the order above (index 0 = 4 bars … 12 = 32nd;
   `rate_step(dir)` moves −1 = longer / Program 7, +1 = shorter / Program 8) and maps each
   value to beats per step as a fraction (4 bars → 16 … 32nd → 1/8 beat) plus a swing flag —
   for a swing value the fraction is the pair length (16th S → 1/2, 8th S → 1). Patch memory
   stores a *code*: the Prophet-6 position (0 = Half … 9 = 32nd) for those ten, and a long
   flag with 0–2 (Whole, 2 bars, 4 bars) for the others ("Patch memory"). The engine's internal period is `60 s / BPM × beats` with the
   remainder carried (swing: 2/3 and 1/3 of the pair, exact in integers), and the MIDI-clock
   step (pair) is `24 × beats` clocks (always integral, and a multiple of 3 for swing pairs).

### Keyboard octave shift `[HW: verified 2026-10-07, Prophet-10 Rev4 — Lo Freq modifier, tap replay, shifted keys and MIDI Out]`

1. Hold the Osc B **Lo Freq** button (panel id 37) and press **Bank** to shift the
   keyboard up one octave, **Group** to shift it down; range −2…+2. (Lo Freq sits next to
   Bank/Group, so the shift is a one-handed move. Versions before 2026-10-07 used the
   filter Keyboard Amount button, id 8.) Pressing Bank and
   Group together (the second while the first is still down) resets the shift to 0. The
   display shows the shift (`-2`…`2`) for 1.5 s after each press (also at the ends,
   unchanged), through the stock integer readout — `001`, `-01` — deliberately, as that is
   how stock shows its own signed values such as the pitch-bend range. **Holding Lo
   Freq on its own shows the current shift** `[HW: verified 2026-10-08, Prophet-10 Rev4]`: from the moment the
   panel reports the button held (its first held-repeat event, after the stock repeat
   delay) the display shows the shift, unchanged or not, and keeps showing it for as long
   as the button is held (each repeat renews the message); it reverts 1.5 s after the last
   repeat as any message does. Power-up: 0. Not saved with patches.
2. The shift applies to keys played on the keyboard: what the synth plays, what the arp/seq
   receive (re-latch, recording, triggers), and what is sent to MIDI Out for those keys.
   MIDI-in notes are not shifted. A key's release always uses the shift that was in force
   when it was pressed, so changing the shift while holding keys never sticks a note.
3. A tap of Lo Freq (press and release with no Bank/Group press in between, and released
   before the panel reports it held) still toggles Osc B low-frequency mode as in stock;
   the setting and its LED change on the release rather than on the press. While it is
   held with Bank/Group, the Lo Freq setting does not change; nor does it change when a
   hold ends without a shift command — a hold is not a tap `[HW: verified 2026-10-08, Prophet-10 Rev4]` (the
   stock repeat delay sets how long a tap may last). Repeat events (value 3) of Bank and
   Group are ignored; Lo Freq's are the hold detection.
4. Because the shift is applied before the voice engine, a Prophet-10 split point moves
   with the keyboard. A key whose shifted note would fall outside 0–127 is silent (not
   reachable from the 61-key range at ±2).
5. In Local-Off mode (keys go to MIDI Out only) the shift still applies to what is sent.
   Lo Freq held together with A440 is undefined.
6. Realisation: the local-note hook (`0x2003BECC`) shifts the key and records the per-key
   shift (128-entry table) so the release maps identically; `bl` hooks on the two stock
   calls that send a local key to MIDI Out — note-on `0x2003BEFA` (expecting `0x20033F85`)
   and note-off `0x2003BF16` (expecting `0x20033F39`), both `(cable_mask, channel, note,
   vel)` — apply the same per-key mapping to `note`. These sites are reached whether Local
   Control is on or off. (`0x2003BED8` is not a MIDI Out call: it posts a message no state
   handles. `[HW: bug found 2026-10-07 by disassembly before that image was installed]`.)
   The button hook swallows id 37 (press, repeats, release), treats Bank/Group presses
   while it is held as shift commands, treats a repeat of id 37 as the hold (shows the
   shift, and marks the hold as used so its release is no tap), and on a release with
   neither replays press+release of id 37 into stock's button post (`0x2003BC31`).

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
4. With the arp off nothing changes: stock hold behaves exactly as before — except in seq
   record mode, which suspends the synth's hold in the same way while it lasts, arp on or
   off ("Seq") `[HW: verified 2026-10-08, Prophet-10 Rev4]`.
5. Realisation: stock `note_off` asks the hold state (`bl 0x2003B694` at `0x2003EACE`) and,
   when it is on, hands the voice to the voice engine's sustain instead of releasing it; the
   voice engine is told about hold by the message `0x080D0000 | state` posted from the hold
   handler (`0x200396CA`). Let *suspended* = the arp is enabled or seq record mode is
   active. The engine hooks `0x2003EACE` (expecting `0x2003B695`) to answer "off" while
   suspended, skips the hold message in its hold hook while suspended (still passing the
   state to the arp's hold event), and on every transition of *suspended* with HOLD active
   posts the message itself (`0x2003D325`: off when it begins, on when it ends).

### Display messages `[HW: verified 2026-10-07, Prophet-10 Rev4]`

1. Every message the engine puts on the display — mode (`UP dn Ud rnd`), octaves
   (`o N`), clock (`int`/`Syn`), `OFF`, BPM (while Glide Rate turns with A440 held, on a
   tempo tap, and when the arp is switched on), `tAP`, note value, keyboard shift, button
   id — shows for 1.5 s after the last change and then the display returns to the stock
   patch display (bank, group and program). The record-mode flashes `rSt` and `tiE` are the
   one shorter message: 0.25 s ("Seq"). Nothing stays on the display permanently, with one
   exception: while seq record mode lasts the display returns to its readout `r N`
   instead, and the patch display comes back when record mode ends ("Seq")
   `[HW: verified 2026-10-08, Prophet-10 Rev4]`. `(change from the Arp Mod, which kept OFF / BPM / Syn up for as long as the
   arp was on)`
2. A new message restarts the 1.5 s.
3. Realisation: the UI keeps one timer, restarted by every message it shows (1.5 s, or
   0.25 s for a flash); at expiry the stock patch display is redrawn with
   `0x2003818D(ui = 0x20057390)` — or, while record mode lasts, `r N` is shown again.

### Button id readout `[HW: verified 2026-10-07, Prophet-10 Rev4 — Keyboard 36, GLOBALS 13; Unison read 25 before it became tap tempo, Tune read 12 before it became record mode]`

While A440 is held, pressing a panel button that the arp does not assign — anything other
than Program 1–8, Bank, Group, Velocity (id 11 / `0x0B`, tap tempo), Tune (id 12 / `0x0C`,
seq record mode), HOLD (id 14 / `0x0E`: the tuning tone, or rest/tie in record mode),
GLOBALS (id 13 / `0x0D`, which must still open its menu) and Lo Freq (id 37, consumed by
the keyboard octave shift) — shows that button's id on the display and is otherwise ignored
(its release is consumed too). An aid for mapping panel button ids when designing new
combinations.

### Safety invariants

Enforced by tests on every built image against stock:

1. The output decodes with `trailer: ok`, target `main`.
2. The SHARC image (`AC`) is byte-identical to stock.
3. The main-CPU image has stock's record sequence (same count, types, load addresses,
   lengths, EXEC entry) **plus exactly one appended COPY record** (`0x20088000`, `0x8000`);
   the EXEC declared length is stock's plus the record size + 16.
4. Payload bytes of stock's records differ only within the 4-byte `BL` at each `bl` hook
   site (a retarget may leave the first halfword unchanged) and within the 4-byte word at
   each `word` site.
5. The stock startup record (COPY `0x2002E000`, `0xEDC` bytes) is byte-identical; no engine
   code runs at boot — the engine is entered only through the hooks.
6. Every hook site lies inside the stock code record and, before patching, holds exactly
   what the hook list says it holds (a `BL` to the named stock entry, or the named word).
7. The engine's code references no RAM or MMR address outside its own record and the stock
   addresses listed in its interface table, which the tests compare word for word.

### Re-latch under HOLD `[HW: verified 2026-10-06/07, Prophet-10 Rev4]`

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
4. Switching HOLD off makes the arp drop released notes; from then on only keys still
   down count as latched.
5. The new key is added after the clear, so it is the first note of the new pattern.
6. Realisation: the engine's note pool (`arp.c`): when HOLD is active, no key is down and
   latched notes exist, a note-on empties the latched set and releases the sounding
   step before adding the key; the pattern restarts at the next step boundary ("Arp
   engine" — Start rule). `[HW: verified 2026-10-06 (Arp-Mod-based build) and 2026-10-07 (engine)]`

### Seq (step-recorded sequence) `[HW: verified 2026-10-08, Prophet-10 Rev4 — record mode (A440 + Tune, tap to leave), chord steps, rests and ties via the HOLD button, the `r N` readout and the blinking LED, playback; the pedal as rest/tie, recording from MIDI-in and a tied step under Syn not yet exercised. The hold-A440 recording this replaced was verified 2026-10-06/07]`

A sequence is up to **64 steps**. A step is a **chord of up to 10 notes** (each with its own
velocity) or a **rest**, and has a **length** in arp steps (1 unless tied). It is recorded in
*record mode*, entered and left from the panel; *seq mode* is active exactly when a
sequence exists. *Keys*, *HOLD active* and *keys down* are as in "Re-latch under HOLD"
(local keys after the keyboard octave shift and MIDI-in notes count alike).

#### Record mode

1. **Enter**: A440 held + **Tune** (button id 12 / `0x0C`). This counts as using the A440
   hold (the A440 release afterwards does not toggle the arp); Tune's press, held repeats
   and release are consumed, so stock auto-tune does not run. Tune without A440 held is
   stock Tune.
2. On entry everything the engine has sounding is released — the arp's step notes, or keys
   sounding directly with the arp off — and the pool is emptied; keys down at entry are
   ignored until released and pressed again. The existing sequence stays until the first
   step is recorded, so entering and leaving without recording changes nothing.
3. **Readout**: the display shows `r N` for as long as record mode lasts, where N is the
   **length recorded so far in arp steps**: a chord or a rest counts one, each tie one
   more — so every recording gesture advances it by exactly one (`r 0` on entry; from 100,
   the plain number) `[HW: verified 2026-10-08, Prophet-10 Rev4]`. Any other message shown meanwhile (a mode, octaves, BPM, note value…)
   lasts its 1.5 s and then the display returns to `r N`, not to the patch display
   ("Display messages"). The A440 LED blinks (500 ms on, 500 ms off) while record mode
   lasts and shows the arp state again afterwards.
4. **Notes** (keys, or MIDI-in) sound directly through the stock voice allocator — their
   releases are honoured, so nothing sticks — and are recorded. **Chord rule**: a note-on
   while any key already recorded into the step being recorded (the *open step*) is still
   down joins that step; a note-on after every key of the open step has been released
   starts a new step. A pitch already in the open step is not added twice. Each note keeps
   its own velocity. Beyond 10 notes in a step, or beyond 64 steps, notes still sound but
   are not recorded.
5. **Rest**: a press of the HOLD button, or of the sustain pedal in `HLd` mode, while no key
   of the open step is down appends a rest (a step with no notes, length 1). **Tie**: the
   same with at least one key of the open step down makes the open step one arp step
   longer (up to 64). Either way the display **flashes** `rSt` or `tiE` for 0.25 s and then
   shows the advanced count `[HW: verified 2026-10-08, Prophet-10 Rev4 — first with a 0.5 s
   flash, then at 0.25 s]`. The HOLD button is consumed,
   whether or not A440 is held:
   the hold latch and its LED do not change. The pedal keeps driving the stock hold state
   and LED as in stock but is otherwise only a rest/tie control. Held repeats are ignored;
   releases do nothing.
6. While record mode lasts the synth's own hold is suspended as it is while the arp is on
   ("HOLD while the arp is on"), arp on or off, so recorded notes release on key-up even
   with HOLD lit or the pedal down.
7. **A440 combos** keep working (mode, octaves, clock source, note value, tap tempo, Glide
   Rate tempo). A440 + **Program 6** clears the sequence — what has been recorded so far,
   or the previous sequence if nothing has been yet — and stays in record mode (`r 0`).
8. MIDI CC 123–127 silence the sounding notes as stock does and leave the recording as it
   is. Loading a program does not end record mode (its arp settings apply as usual; no
   pattern starts, the pool being empty).
9. **Leave**: a **tap** of A440 (press and release with no combo in between), A440 + Tune
   again, or GLOBALS (which opens its menu as stock). At least one step recorded → it is
   the sequence and seq mode is active; none → the previous sequence, or none, stands.
   Leaving never toggles the arp: it stays on or off as it was. Directly sounding notes
   are released; keys down at exit are ignored until pressed again.
10. The hold-A440-and-play recording this replaces is gone: notes played while A440 is
    held behave as if it were not held and do not count as using it.
11. Power-up: no sequence, not in record mode.

#### Playback (seq mode active, arp enabled)

12. The sequence plays while at least one key is down and, with HOLD active, after all
    keys are released until the next key — the re-latch rule: the first key after all keys
    were released restarts the sequence from step 1. Each step releases the previous step's
    notes and plays all of its own, each transposed by *(trigger key − reference)* at its
    recorded velocity; the **reference** is the lowest pitch of the first step that has
    notes. The trigger key is the most recently pressed key; a new trigger takes effect
    from the next step and stays in effect even if that key is released while others
    remain down. A rest plays nothing (the previous step's notes are released as at any
    step).
13. A note whose transposed pitch falls outside 0–127 is silent; the other notes of its
    step still play.
14. **Length**: a step of length L occupies L arp steps: its notes are played at the first,
    not re-triggered at the inner boundaries, and released at the gate point of the L-th
    (half-way through it, as any step's gate — under MIDI clock, that step's gate-off
    clock), so tied steps stay on the DAW grid.
15. Direction, tempo, clock source and MIDI sync are the arp's, and **steps are the
    units**: `UP` plays the steps in recorded order, `dn` reversed, `Ud` forward then back,
    `rnd` picks a step at random for each, `ASS` in recorded order (as `UP`); a step keeps
    its chord and its length in every order.
16. With the arp disabled, keys play normally (no sequence runs). With HOLD inactive,
    releasing all keys stops the sequence; switching HOLD off with no keys down stops it.
17. MIDI CC 123 (All Notes Off) stops the sequence; the next key starts it again.

#### Octaves

18. The octave setting is shared by the arp and seq (A440 + Program 1–4, `o N`). With
    `o 2` the sequence plays once as recorded, then once an octave up — per pass, as the
    arp's own octaves; direction modes apply across the expanded pattern; a change takes
    effect from the next step. A Program 1–4 press while A440 is held counts as using the
    hold, so releasing A440 afterwards does not toggle the arp.
19. Leaving seq mode (Program 6) leaves the octave setting as it is.

#### Clear

20. A440 + Program 6 clears the sequence and leaves seq mode: normal arp behaviour
    (including re-latch) resumes; the A440 release afterwards does not toggle the arp. In
    record mode it clears and stays (rule 7).

#### Realisation

- Record mode lives in the UI (`arpui.c`): A440 + Tune toggles it; while it lasts the UI
  routes every note to the recorder, consumes the HOLD button (id 14 / `0x0E`) and turns
  its presses — and the pedal's on-transitions, seen through the hold event — into rest or
  tie, keeps the A440 LED blinking from its tick, and restores `r N` instead of the patch
  display when the message timer expires. The engine (`arp.c`) stores a step as (note
  count, up to 10 pitches and velocities, length), groups notes into the open step by the
  recorded keys still down, and in playback uses the step list in place of the pitch-sorted
  pool as the pattern's base order, holding the walker for the step's length before it
  advances; the pool still decides whether the pattern runs and the trigger is the most
  recently pressed pool note. The hold suspension is the "HOLD while the arp is on"
  mechanism, driven by *arp enabled or record mode*.
- The readout sums the steps' lengths; `rSt` / `tiE` go through the UI's display path with
  the short timer and revert to the readout like every other message ("Display messages").
