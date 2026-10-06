# prophet-arp-mods — Spec

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

### Image patching (hook chaining, wrapper record)

The build takes a **base** OS file (the V5 arp mod), a **wrapper binary** with its symbol
map, and a **hook list**, and produces a new OS file. Nothing is changed that the hook
list and wrapper placement do not require.

#### Wrapper placement

- V5 loads its arp blob with a single COPY record of `0x2000` bytes at `0x20088000`. The
  arp's code and state end below `0x20089600`; the rest of that record is zero in V5.
- The wrapper occupies the **wrapper window** `[0x20089600, 0x2008A000)` (`0xA00` bytes)
  and is written **into that record's payload** at offset `0x1600`. No record is added or
  resized, so the loader sees exactly the structure it already accepted for V5.
- The build fails if the wrapper binary exceeds the window, or if the window bytes in the
  base record are not all zero.

#### Hook retargeting

- A hook list entry names a `site` (RAM address of a 4-byte Thumb-2 `BL` inside the stock
  code record, `0x2002EF00..`), the `expect`ed current target (a V5 entry point, Thumb
  bit set) and the wrapper `symbol` to call instead.
- The build locates the record containing the site, verifies the instruction there is a
  `BL` whose target equals `expect`, and rewrites it as a `BL` to the symbol's address
  from the map. It fails if the site is not such a `BL`, if the symbol is missing, if the
  site lies outside the stock code record, or if the target is out of `BL` range.

#### CLI

- `python3 -m tools build --base BASE.syx --wrapper W.bin --map W.map --hooks hooks.json
  -o OUT.syx` — performs the above and writes `OUT.syx` (trailer and header recomputed).
  The map is `nm`-style text: `<hex address> <symbol>` per line.
- `python3 -m tools diff A.syx B.syx` — prints every differing byte span of the decoded
  payloads as `image <n> record @0x<offset> ram 0x<lo>..0x<hi>: <n> bytes`, so a reviewer
  can see exactly what a build changed. Record-structure differences are reported as such.

### Safety invariants

Enforced by tests on every built image against its base:

1. The output decodes with `trailer: ok`, target `main`.
2. The SHARC image (`AC`) is byte-identical to the base.
3. The main-CPU image has the same record sequence as the base: same count, types, load
   addresses, lengths and EXEC entry/declared length.
4. Payload bytes differ from the base only within the 4-byte `BL` at each hook site (a
   retarget may leave the first halfword unchanged) and inside the wrapper window of the
   RAM-window record.
5. The stock startup record (COPY `0x2002E000`, `0xEDC` bytes) is byte-identical to the
   base; no wrapper code runs at boot — the wrapper is entered only through the hooks.
6. Every hook site lies inside the stock code record and, before patching, is a `BL` to
   the V5 entry point named in the hook list.

### Re-latch under HOLD `[HW: unverified]`

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
   re‑latched chord therefore stays latched. `[HW: bug seen 2025-10-06 before this fix]`

Wrapper facts this depends on (V5 as shipped; each is asserted by a test on the fixture):
arp‑enabled byte at `0x200894D8`; V5 entry points local‑note `0x20088C51`, MIDI note‑on
`0x20088CFD`, MIDI note‑off `0x20088D2D`, hold `0x20089081` (reads the new hold state from
`r4` as left by the stock caller), all‑notes‑off `0x20088E83`. Wrapper state lives in
`0x20089E80–0x2008A000` inside the wrapper window and is zero after every boot.

### Seq (step-recorded sequence) `[HW: unverified]`

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

10. In seq mode the octave setting (A440 + Program 1–4; the setting in force when seq mode
    was entered applies initially) spans the whole sequence: with `o 2` the sequence plays
    once as recorded, then once an octave up; direction modes apply across the expanded
    pattern. Changing it while playing takes effect immediately. The display shows the
    new count as a number.
11. Leaving seq mode restores the arp's own octave setting to the value in force when seq
    mode was entered.

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
- Octave setting byte `0x200894DF` (engine + 0x307); Program ids 0–3 = octaves 1–4;
  id 5 = Program 6. Any Program press while A440 is held marks the A440 hold as "used",
  which suppresses the toggle on release; ids 5–7 are otherwise no-ops in V5.
- Hooks: keyboard-scan tick `0x2003BE9C → 0x20088D53`, panel buttons `0x2003C244 →
  0x20088EAB` `(id, value)`, CC 123 `0x2003B294 → 0x20088E83`, in addition to the
  re-latch hooks. Wrapper state occupies `0x20089E80–0x2008A000`.

### Safety invariants
_Not yet specced._
