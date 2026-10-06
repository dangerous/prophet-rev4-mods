# prophet-rev4-mods

Wrapper firmware for the Sequential Prophet‑5/10 Rev4, layered on the third‑party
**Arp Mod V5** (an unofficial patch of Main OS 2.1.0), plus the tooling that unpacks,
patches and re‑packs the OS SysEx image.

Spec: [`docs/SPEC.md`](docs/SPEC.md). Hardware verification: [`docs/hardware-checklist.md`](docs/hardware-checklist.md).

> **Not affiliated with Sequential.** This is a hobby project that modifies the
> instrument's firmware. Installing a modified OS is at your own risk and may void your
> warranty; a bad image can leave the instrument needing the DIN‑MIDI bootloader to
> recover. Only the project's own code, tooling and reverse‑engineering notes are
> published here — Sequential's OS files, the Arp Mod V5 image (whose author did the
> original work this builds on) and the built images are not redistributed.

## What it adds

- **Re‑latch under HOLD** — with the arp on and HOLD active, the first key you play after
  releasing all keys starts a fresh chord instead of adding to the latched one.
  `[HW: verified 2026‑10‑06]`
- **Seq** — hold A440 and play to record up to 32 steps (releases ignored, so timing is
  free). Then play a key: the sequence runs at the arp's tempo/sync, transposed from the
  first recorded note, in the arp's direction mode, over the arp's octave range. HOLD
  latches it; the first key after all‑up restarts it. A440 + Program 6 clears it.
  `[HW: verified 2026‑10‑06]`
- **Note value** — A440 + Program 7 (shorter) / Program 8 (longer) steps through 1/32,
  1/16T, 1/16, 1/8T, 1/16d, 1/8, 1/8d, 1/4, 1/4d, 1/2, 1, 2 bars, 4 bars for the arp and
  seq, on internal clock and under MIDI sync. Display shows e.g. `16t`, `8d`, `2b`.
  Default 1/8 (V5's behaviour). `[HW: unverified]`
- **Button id readout** — hold A440 and press any button the arp doesn't use: its id is
  shown for a second. For mapping panel button ids. `[HW: verified 2026‑10‑07]`
- **Keyboard octave shift** — hold the filter Keyboard Amount button, Bank = up, Group =
  down (±2; both together = 0); shown on the display. Applies to the keys you play,
  including what goes to MIDI Out; a plain tap still cycles keyboard tracking (LED on
  release). `[HW: unverified]`

## Inputs you must supply

Sequential's OS files and the V5 image are copyrighted and are **not in this repository**.
Drop them into `fixtures/` — [`fixtures/README.md`](fixtures/README.md) lists the three
files, where they come from and their SHA‑256 (`fixtures/SHA256SUMS`). Tests that need a
missing file skip with a warning; `make image` stops with the same message.

## Images (`dist/` — build them; hashes in `dist/SHA256SUMS`)

The built images are derived from those inputs, so they aren't distributed either. Both
targets below produce re‑latch + seq + note values + readout + octave shift from the same
wrapper (one appended 8 KB record at `0x2008A000`: code in the first 6 KB, state in the last
2 KB) and the `BL`/word retargets listed in `firmware/hooks*.json`:

| Target | Output in `build/` | Extra change | Note values under MIDI sync |
|---|---|---|---|
| `make image-internal` | `prophet10_v5_relatch_seq_internal.syx` | none — MIDI‑parser table as V5 | no (eighths, as V5) |
| `make image` | `prophet10_v5_relatch_seq.syx` | 4 MIDI‑parser table words → wrapper trampoline | yes |

Install exactly like V5 (USB, SysEx Librarian). The `_internal` variant keeps wrapper code
out of the MIDI‑byte path that a USB re‑flash uses; the full variant adds the clock filter
and has not been installed on hardware yet. [`dist/README.md`](dist/README.md) explains the
checksums.

History: a re‑latch‑only image was installed on a Prophet‑10 Rev4 on 2026‑10‑06 and proved
the loader path, boot, the hook mechanism and the HOLD stub; it had a bug (the arp's clear
also dropped the arp's own hold flag, so a re‑latched chord did not stay latched), fixed by
"Re‑assert hold to the arp after every clear". The `_internal` image has been installed
since, through the seq and note‑value work.

In both variants the wrapper lives in a record appended to the image at
`0x2008A000–0x2008C000`; the SHARC image, the stock startup code and every other byte are
identical to V5 (`python3 -m tools diff fixtures/V5_… build/…` lists exactly the changed
spans). The wrapper runs only when the hooked stock calls fire — never at boot — so a
misbehaving hook leaves the synth bootable and re‑flashable over USB. All memory it
touches is its own state inside that record plus V5/stock entry points listed in
`firmware/v5_iface.h`, which the tests read back from the built binary.

## Layout

- `fixtures/` — where the official Main 2.1.0 / Panel 1.1.3 and the V5 `.syx` go (git‑ignored; see above).
- `tools/` — Python 3.9+ stdlib‑only CLI: `inspect`, `unpack`, `pack`, `diff`, `fwbuild`, `build`.
- `firmware/` — wrapper C sources (`relatch.c`, `seq.c`, `rate.c`, `oct.c` portable logic,
  `wrapper.c` hook glue, `hooks.json` / `hooks_internal.json` the patched call sites).
- `tests/` — `unittest` suites (tooling, built‑image invariants) and host C harnesses.
- `docs/` — spec, working notes, hardware checklist.

## Build and test

```
make test            # tooling tests, host harnesses, cross-build + image invariants
make image-internal  # build/prophet10_v5_relatch_seq_internal.syx
make image           # build/prophet10_v5_relatch_seq.syx
```

Requires Apple clang (Xcode command line tools) for the Thumb‑2 cross build; no other
dependencies.
