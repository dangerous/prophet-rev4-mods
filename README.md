# prophet-rev4-mods

An arpeggiator and step sequencer for the Sequential Prophet‑5 / Prophet‑10 Rev4, written
as our own code hooked into Sequential's stock Main OS 2.1.0, plus the tooling that
unpacks, patches and re‑packs the OS SysEx image.

Spec: [`docs/SPEC.md`](docs/SPEC.md). Hardware verification: [`docs/hardware-checklist.md`](docs/hardware-checklist.md).

> **Not affiliated with Sequential.** This is a hobby project that modifies the
> instrument's firmware. Installing a modified OS is at your own risk and may void your
> warranty; a bad image can leave the instrument needing the DIN‑MIDI bootloader to
> recover. Only the project's own code, tooling and reverse‑engineering notes are
> published here — Sequential's OS files and the built image are not redistributed.

## What it does

All of it `[HW: verified 2026‑10‑07, Prophet‑10 Rev4]` unless marked.

- **Arp** — tap **A440** to switch it on (LED). Modes Up, Down, Up/Down (ends not
  repeated), Random; 1–4 octaves played **per pass** (C3 D4 | C4 D5); 40–300 BPM from the
  Glide Rate pot while the arp is on with the internal clock; MIDI clock sync with
  Start/Stop/Continue, 1 s clock‑loss, the pattern always on the clock grid. One note
  sounds per step, also under HOLD.
- **Re‑latch under HOLD** — with HOLD active, the first key after releasing all keys starts
  a fresh chord instead of adding to the latched one.
- **Seq** — hold A440 and play up to 32 notes (releases ignored, so timing is free).
  Then play a key: the sequence runs transposed from the first recorded note, at the arp's
  tempo/sync, in the arp's direction mode, over the arp's octaves; HOLD latches it.
  A440 + Program 6 clears it.
- **Note value** — A440 + Program 8 (shorter, +) / Program 7 (longer, −): 1/32, 1/16T,
  1/16, 1/8T, 1/16d, 1/8, 1/8d, 1/4, 1/4d, 1/2, 1, 2 bars, 4 bars, on the internal clock
  and under MIDI sync. Default 1/8.
- **Keyboard octave shift** — hold the Osc B **Lo Freq** button, Bank = up, Group = down
  (±2; both together = 0). Applies to the keys you play and to MIDI Out; a plain tap still
  toggles Lo Freq (LED on release).
- **Display** — every message (mode, `o N`, `int`/`Syn`, `OFF`, BPM, note value, step
  count, shift, button id) returns to the patch display after 1.5 s.
- **Button id readout** — hold A440 and press an unused button to see its id.
- **Patch memory** — the arp's on/off, mode, octaves and note value are saved with the
  program (two spare parameter slots, carried in SysEx dumps too) and restored when it is
  loaded; programs without arp data load with the arp off. `[HW: unverified]`
- **Kill switch** — hold A440 while powering on and every hook passes straight through to
  stock for that session (a tap after power‑on is just a tap).
- **Globals menu** is pure stock while it is open, so Sequential's A440 tuning tone is still
  available from there.

Controls follow the conventions established by Nicolas Maldonado's **Arp Mod V5**, the
third‑party patch whose documented behaviour this engine was modelled on (A440 as the arp
button, Bank/Group for modes, Program 1–5 for octaves and clock source, Glide Rate for
tempo). Deliberate differences are listed in the spec.

## Inputs you must supply

Sequential's OS files are copyrighted and **not in this repository**. Drop them into
`fixtures/` — [`fixtures/README.md`](fixtures/README.md) lists the two files, where they
come from and their SHA‑256 (`fixtures/SHA256SUMS`). Tests that need a missing file skip
with a warning; `make image` stops with the same message.

## The image (`dist/` — build it; hash in `dist/SHA256SUMS`)

`make image` → `build/prophet10_native.syx`: stock 2.1.0 plus one 32 KB record at
`0x20088000` (≈5.6 KB of code, state zero at boot) and 18 retargeted sites — 14 `BL`s and
the 4 MIDI‑parser table words for F8/FA/FB/FC (`firmware/hooks_native.json`). Nothing else
in the OS changes; `python3 -m tools diff fixtures/prophet5_main_2.1.0.syx build/…` lists
exactly those spans. The engine runs only when the hooked stock calls fire — never at boot.

Install over USB with SysEx Librarian (Globals → MIDI SysEx = USB), exactly as an official
OS update; the file is a complete Main OS and installs over any prior version.
[`dist/README.md`](dist/README.md) explains the checksum.

## Layout

- `fixtures/` — where the official Main 2.1.0 / Panel 1.1.3 `.syx` go (git‑ignored; see above).
- `tools/` — Python 3.9+ stdlib‑only CLI: `inspect`, `unpack`, `pack`, `diff`, `fwbuild`, `build`.
- `firmware/` — `arp.c` (engine), `arpui.c` (controls, display, LED, kill switch),
  `oct.c`, `rate.c`, `disp.c`, `vhold.c` (portable logic), `native.c` (hooks and the stock
  interface table), `hooks_native.json` (the patched sites).
- `tests/` — `unittest` suites (tooling, built‑image invariants) and host C harnesses.
- `docs/` — spec, working notes, hardware checklist, reverse‑engineering notes on stock
  (`docs/re/`).

## Build and test

```
make test     # tooling tests, host harnesses, cross-build + image invariants
make image    # build/prophet10_native.syx
```

Requires Apple clang (Xcode command line tools) for the Thumb‑2 cross build; no other
dependencies.

## History

The project began (2026‑10‑06) as a wrapper around Arp Mod V5 — re‑latch, seq, note values
and the octave shift were first built as hooks chained in front of V5's entry points and
verified on hardware that way. On 2026‑10‑07 the arp was re‑implemented as this engine so
the project is self‑contained and MIT‑licensed; the wrapper was then removed.
