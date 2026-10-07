# fixtures — required input files (not in the repository)

The tools and tests work on Sequential's Prophet‑5/10 Rev4 OS files. They are copyrighted
by Sequential and are **not distributed here**. Put them in this directory under exactly
these names:

| File | Needed by | Where it comes from |
|---|---|---|
| `prophet5_main_2.1.0.syx` | everything (`make image`, all image and tooling tests) | Sequential — Prophet‑5/10 Rev4 product page, Support → OS updates, "Main OS 2.1.0" (ships alongside `P5 Main 2.1.0 Installation Instructions.txt`) |
| `prophet5Panel_v1.1.3.syx` | tooling tests (the Panel file is the second container format) | Sequential — same page, "Panel OS 1.1.3" |

`SHA256SUMS` in this directory is the authoritative list of the files the tools and tests
need, with the SHA‑256 of the exact versions this project was built and tested against.
Verify with:

```bash
cd fixtures && shasum -a 256 -c SHA256SUMS
```

A test that needs a file which is missing, or whose hash differs, is skipped with a
warning on stderr naming the file; `make image` stops with the same message. The host C
harnesses (`make test-firmware`) need none of these files.

Everything in this directory except `README.md` and `SHA256SUMS` is git‑ignored. Never edit
the input files.
