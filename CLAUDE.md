# CLAUDE.md

## Spec
The product spec is `docs/SPEC.md`. Behaviours carry a hardware-verification marker; host
tests enforce them, the marker records whether they were also confirmed on an instrument.

## System type
Embedded firmware patch for the Sequential Prophet-5/10 Rev4 Main OS (ARM Cortex-A5,
Thumb-2, RAM-loaded) layered on the third-party Arp Mod V5, plus a Python CLI that
unpacks, patches and re-packs the OS SysEx image.

## Layout
- `fixtures/` — the official Main OS 2.1.0, Panel OS 1.1.3 and V5 arp mod `.syx` files go here;
  they are copyrighted and not committed (see `fixtures/README.md`, `fixtures/SHA256SUMS`).
  Tests read them and skip, with a warning, when they are missing.
- `tools/` — Python packing/patching CLI (`python3 -m tools ...`).
- `firmware/` — wrapper C sources, built freestanding for `thumbv7a` at a fixed address.
- `tests/` — `unittest` suites for the tooling and the built image; host-side C harness for
  the wrapper behaviour.
- `docs/` — spec, reverse-engineering notes, hardware test checklist.

## Build and test
- `make test` runs everything (Python `unittest` suites + host C harness). Single suite:
  `python3 -m unittest discover -s tests/tooling -t .`, `make test-firmware`.
- `make image` builds the patched `.syx` into `build/`.
- Tests use the stdlib `unittest` runner (no third-party packages are installed on this
  machine; do not add dependencies).

## Acceptance tests
- Tooling: `unittest` against `fixtures/` — byte-exact round-trips and structural
  validation of built images (records, XOR check bytes, lengths, hook-site disassembly).
- Wrapper behaviour: the re-latch/seq logic is portable C compiled natively and driven by
  a harness that fakes the arp's event queue and the stock note/display calls. Tests feed
  key/button/hold/MIDI events and assert the exact note-on/off sequence emitted.
- Hardware: manual checklist in `docs/hardware-checklist.md`; results recorded as spec
  markers.

## Conventions
- Python: stdlib only, 3.9+, `unittest`. C: C11, no libc dependence in firmware sources.
- Never commit anything under `build/`. Never edit `fixtures/`.
- `dist/*.syx` is git-ignored, so it is **per checkout**: after building in a worktree, copy
  the images into the root checkout's `dist/` (that is where they are flashed from) and
  check `cd dist && shasum -a 256 -c SHA256SUMS` there. Never delete dist files; superseded
  builds go to `dist/old/` with a hash suffix.
- Flashing is a human step; the tools only produce files.
