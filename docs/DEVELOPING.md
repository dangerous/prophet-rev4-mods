# Developing

How the project is laid out, built and tested. The behaviour itself is specified in
[`SPEC.md`](SPEC.md); this is the engineering side.

- [`SPEC.md`](SPEC.md) is the source of truth for every behaviour, with a marker
  recording whether it has been verified on an instrument;
  [`hardware-checklist.md`](hardware-checklist.md) is the manual test pass;
  [`NOTES.md`](NOTES.md) the engineering context; [`re/`](re/) the
  reverse‑engineering notes on the stock OS.
- `firmware/` — the engine in C11 (`arp.c` pattern and clock, `arpui.c` controls and display,
  `oct.c`, `rate.c`, `disp.c`, `vhold.c`; `native.c` the hooks and the stock interface
  table). It is one 32 KB record added at `0x20088000` plus 18 retargeted sites — 14 `BL`s
  and the four MIDI‑parser table words for realtime bytes (`firmware/hooks_native.json`).
  Nothing runs at boot; the engine only runs when a hooked stock call fires.
- `tools/` — Python 3.9+, standard library only: `inspect`, `unpack`, `pack`, `diff`,
  `fwbuild`, `build`, `manifest`, `apply`.
- `site/` — the patcher page (`index.html`, `patcher.js`) and the generated
  `manifest.js` / `version.json`, published by GitHub Pages from that directory alone.
- `fixtures/` — where Sequential's Main 2.1.0 and Panel 1.1.3 files go for the tests and the
  build (git‑ignored; `fixtures/README.md` says where they come from and lists their
  hashes).

```
make test       # tooling tests, host harnesses, cross-build and image invariants
make image      # build/prophet10_native.syx
make manifest   # site/manifest.js and site/version.json from that image
```

## Releasing

1. Bump `VERSION` (semantic versioning) and make sure `make test` is green.
2. `make image`, then `make manifest`; copy the image into the root checkout's `dist/` and
   update `dist/SHA256SUMS` (see `CLAUDE.md` for the dist conventions).
3. Commit `VERSION`, `site/manifest.js`, `site/version.json` and `dist/SHA256SUMS`
   together, merge to `main` and push: the Pages workflow publishes the patcher, and the
   README badge follows `site/version.json`.

The cross build needs Apple clang (Xcode command line tools). `dist/SHA256SUMS` records
the hash of the image built at HEAD; the build is deterministic for a given compiler.

Controls follow the conventions of an earlier third‑party **Arp Mod** for the Rev4 that
circulated privately, whose documented behaviour the arp was modelled on (A440 as the arp
button, Bank/Group for modes, Program buttons for octaves and clock, Glide Rate for tempo);
the deliberate differences are listed in the spec. The project began in October 2026 as
hooks chained in front of that mod and was re‑implemented as this self‑contained,
MIT‑licensed engine the next day.
