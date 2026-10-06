# dist — installable images (not in the repository)

The built `.syx` images are derived from Sequential's OS and the V5 image, so they are not
distributed here any more than the inputs are. Build them yourself (they land in `build/`):

```bash
make image-internal   # build/prophet10_v5_relatch_seq_internal.syx — note values on the internal clock only
```

```bash
make image            # build/prophet10_v5_relatch_seq.syx — also patches the MIDI-parser table for note values under MIDI sync
```

`SHA256SUMS` records the hashes of the images built at HEAD on the author's machine (Apple
clang 21.0.0). The build is deterministic for a given compiler, so
`shasum -a 256 build/*.syx` should match those lines; a different clang version may produce
a different — equally valid — wrapper, in which case rely on `make test` (the image
invariants) rather than the hash.

Install exactly as for V5: USB, SysEx Librarian (or equivalent), Globals → MIDI SysEx set
to USB. See `docs/hardware-checklist.md`.
