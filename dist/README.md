# dist — the installable image (not in the repository)

The built `.syx` is derived from Sequential's OS, so it is not distributed here any more
than the input is. Build it yourself (it lands in `build/`):

```bash
make image            # build/prophet10_native.syx — stock 2.1.0 + our engine
```

`SHA256SUMS` records the hash of the image built at HEAD on the author's machine (Apple
clang 21.0.0). The build is deterministic for a given compiler, so
`shasum -a 256 build/prophet10_native.syx` should match that line; a different clang
version may produce a different — equally valid — engine, in which case rely on
`make test` (the image invariants) rather than the hash.

Install as an official OS update: USB, SysEx Librarian (or equivalent), Globals → MIDI
SysEx set to USB. The file is a complete Main OS and installs over any prior version. See
`docs/hardware-checklist.md`.
