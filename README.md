# prophet-arp-mods

Wrapper firmware for the Sequential Prophet‑5/10 Rev4, layered on the third‑party
**Arp Mod V5** (an unofficial patch of Main OS 2.1.0), plus the tooling that unpacks,
patches and re‑packs the OS SysEx image.

Spec: [`docs/SPEC.md`](docs/SPEC.md). Hardware verification: [`docs/hardware-checklist.md`](docs/hardware-checklist.md).

## What it adds

- **Re‑latch under HOLD** — with the arp on and HOLD active, the first key you play after
  releasing all keys starts a fresh chord instead of adding to the latched one.
  `[HW: unverified]`

## Deliverable

`dist/prophet10_v5_relatch.syx` — V5 with the wrapper. Install it exactly like V5 (USB,
SysEx Librarian). Checksums in `dist/SHA256SUMS`.

What the build changes relative to V5 (`python3 -m tools diff fixtures/V5_… dist/…`):
four 4‑byte `BL` retargets in the stock code (local note, MIDI note‑on, MIDI note‑off,
HOLD) and 726 bytes of wrapper code written into the zero tail of V5's own RAM‑window
record at `0x20089600`. The SHARC image, the stock startup code and every other byte are
identical to V5. The wrapper runs only when those four hooks fire — never at boot — so a
misbehaving hook leaves the synth bootable and re‑flashable over USB.

## Layout

- `fixtures/` — official Main 2.1.0 / Panel 1.1.3 and the V5 `.syx` as received.
- `tools/` — Python 3.9+ stdlib‑only CLI: `inspect`, `unpack`, `pack`, `diff`, `fwbuild`, `build`.
- `firmware/` — wrapper C sources (`relatch.c` portable logic, `wrapper.c` hook glue,
  `hooks.json` the patched call sites).
- `tests/` — `unittest` suites (tooling, built‑image invariants) and a host C harness.
- `docs/` — spec, hardware checklist.

## Build and test

```
make test     # tooling tests, host harness, cross-build + image invariants
make image    # build/prophet10_v5_relatch.syx
```

Requires Apple clang (Xcode command line tools) for the Thumb‑2 cross build; no other
dependencies.
