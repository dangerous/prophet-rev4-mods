# prophet-arp-mods

Wrapper firmware for the Sequential Prophet‑5/10 Rev4, layered on the third‑party
**Arp Mod V5** (an unofficial patch of Main OS 2.1.0), plus the tooling that unpacks,
patches and re‑packs the OS SysEx image.

Spec: [`docs/SPEC.md`](docs/SPEC.md). Hardware verification: [`docs/hardware-checklist.md`](docs/hardware-checklist.md).

## What it adds

- **Re‑latch under HOLD** — with the arp on and HOLD active, the first key you play after
  releasing all keys starts a fresh chord instead of adding to the latched one.
  `[HW: unverified]`
- **Seq** — hold A440 and play to record up to 32 steps (releases ignored, so timing is
  free). Then play a key: the sequence runs at the arp's tempo/sync, transposed from the
  first recorded note, in the arp's direction mode, over the arp's octave range. HOLD
  latches it; the first key after all‑up restarts it. A440 + Program 6 clears it.
  `[HW: verified 2025‑10‑06]`
- **Note value** — A440 + Program 7 (shorter) / Program 8 (longer) steps through 1/32,
  1/16T, 1/16, 1/8T, 1/16d, 1/8, 1/8d, 1/4, 1/4d, 1/2, 1, 2 bars, 4 bars for the arp and
  seq, on internal clock and under MIDI sync. Display shows e.g. `16t`, `8d`, `2b`.
  Default 1/8 (V5's behaviour). `[HW: unverified]`
- **Button id readout** — hold A440 and press any button the arp doesn't use: its id is
  shown for a second. For mapping the P10's extra buttons. `[HW: unverified]`

## Deliverables (`dist/`, checksums in `dist/SHA256SUMS`)

Both are re‑latch + seq + note values + readout, built at HEAD with the same wrapper
(one appended 8 KB record at `0x2008A000`, ~2.9 KB code) and 7 `BL` retargets:

| File | Extra change | Note values under MIDI sync |
|---|---|---|
| `prophet10_v5_relatch_seq_internal.syx` (`make image-internal`) | none — MIDI‑parser table as V5 | no (eighths, as V5) |
| `prophet10_v5_relatch_seq.syx` (`make image`) | 4 MIDI‑parser table words → wrapper trampoline | yes |

Install exactly like V5 (USB, SysEx Librarian). The `_internal` variant keeps wrapper code
out of the MIDI‑byte path that a USB re‑flash uses; the full variant adds the clock filter.

History: a re‑latch‑only image (commit `5cf3f41`) was installed on a Prophet‑10 on
2025‑10‑06 and proved the loader path, boot, the hook mechanism and the HOLD stub; it had
a bug (the arp's clear also dropped the arp's own hold flag, so a re‑latched chord did not
stay latched) which is fixed from commit `ed2cebc`'s successor onward. That file is no
longer shipped.

In both cases the wrapper code lives in the zero tail of V5's own RAM‑window record at
`0x20089600–0x2008A000`; the SHARC image, the stock startup code and every other byte are
identical to V5 (`python3 -m tools diff fixtures/V5_… dist/…` lists exactly the changed
spans). The wrapper runs only when the hooked stock calls fire — never at boot — so a
misbehaving hook leaves the synth bootable and re‑flashable over USB. All memory it
touches is its own state inside that window plus V5/stock entry points listed in
`firmware/v5_iface.h`, which the tests read back from the built binary.

## Layout

- `fixtures/` — official Main 2.1.0 / Panel 1.1.3 and the V5 `.syx` as received.
- `tools/` — Python 3.9+ stdlib‑only CLI: `inspect`, `unpack`, `pack`, `diff`, `fwbuild`, `build`.
- `firmware/` — wrapper C sources (`relatch.c` portable logic, `wrapper.c` hook glue,
  `hooks.json` the patched call sites).
- `tests/` — `unittest` suites (tooling, built‑image invariants) and a host C harness.
- `docs/` — spec, hardware checklist.

## Build and test

```
make test     # tooling tests, host harnesses, cross-build + image invariants
make image    # build/prophet10_v5_relatch_seq.syx
```

Requires Apple clang (Xcode command line tools) for the Thumb‑2 cross build; no other
dependencies.
