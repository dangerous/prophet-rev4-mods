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

### Re-latch under HOLD
_Not yet specced._

### Seq (step-recorded sequence)
_Not yet specced._

### Safety invariants
_Not yet specced._
