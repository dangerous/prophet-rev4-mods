# fixtures — required input files (not in the repository)

The tools and tests work on Sequential's Prophet‑5/10 Rev4 OS files and on the third‑party
**Arp Mod V5** image. Those files are copyrighted by their respective authors and are **not
distributed here**. Put them in this directory under exactly these names:

| File | Needed by | Where it comes from |
|---|---|---|
| `V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx` | everything (`make image*`, all image/tooling tests) | The Arp Mod V5 author — distributed by them together with `Prophet5_Arp_V5_Guide.pdf` |
| `prophet5_main_2.1.0.syx` | tooling tests, built‑image tests | Sequential — Prophet‑5/10 Rev4 product page, Support → OS updates, "Main OS 2.1.0" (ships alongside `P5 Main 2.1.0 Installation Instructions.txt`) |
| `prophet5Panel_v1.1.3.syx` | tooling tests | Sequential — same page, "Panel OS 1.1.3" |

`SHA256SUMS` in this directory is the authoritative list of the files the tools and tests
need, with the SHA‑256 of the exact versions this project was built and tested against.
Verify with:

```bash
cd fixtures && shasum -a 256 -c SHA256SUMS
```

A test that needs a file which is missing, or whose hash differs, is skipped with a
warning on stderr naming the file; `make image` stops with the same message. The host C
harnesses (`make test-firmware`) need none of these files.

Reference hashes of the companion documents (not needed by anything here):

- `Prophet5_Arp_V5_Guide.pdf` — `38f29816e5682f9013b36640e55ee7b0e88ccc11391bf8023c155c311bc13bd3`
- `P5 Main 2.1.0 Installation Instructions.txt` — `10e6a3d0b84a71d12c34cea04be739ded450467e4615929b9fdaf29cf28f2075`

Everything in this directory except `README.md` and `SHA256SUMS` is git‑ignored. Never edit
the input files.
