# prophet-rev4-mods

An arpeggiator and a polyphonic step sequencer for the Sequential **Prophet‑5 / Prophet‑10
Rev4**, added to Sequential's own Main OS 2.1.0 as a firmware patch. You apply the patch to
your copy of the official OS in your browser and install the result over USB like any OS
update. Everything is controlled from the front panel — no menus, no computer at the gig.

> **Not affiliated with Sequential.** This is a hobby project that modifies the
> instrument's firmware. Installing a modified OS is at your own risk and may void your
> warranty. Only the project's own code, tooling and notes are published here; Sequential's
> OS files are not redistributed, which is why you supply your own copy.

## What it adds

- **Arpeggiator** — Up, Down, Up/Down, Random and Assign (the notes in the order you played
  them), 1–4 octaves, HOLD latch with re‑latch, tempo 40–300 BPM by knob or tap, MIDI clock
  sync, ten note values from half notes to 32nds including triplets and Prophet‑6‑style
  swing.
- **Polyphonic step sequencer** — up to 64 steps of chords (up to 10 notes each), rests and
  ties, recorded from the keyboard or MIDI, played transposed from any key, in any of the
  arp's directions, octaves and note values, in sync.
- **Keyboard octave shift** — ±2 octaves from the panel, applied to the keys, the arp and
  MIDI Out.
- **Patch memory** — the arp's on/off, mode, octaves and note value are saved with each
  program.
- Nothing else changes: with the arp off the synth is stock, and the whole patch can be
  bypassed at power‑on.

Verified on a Prophet‑10 Rev4 (October 2026); the spec marks the few paths not yet tried on
hardware. Sequential's own A440 tuning tone remains available from the Globals menu.

## Installing

You need Sequential's **Main OS 2.1.0** file, a USB connection, and a program that sends
SysEx files (SysEx Librarian on macOS, MIDI‑OX on Windows, SendMIDI on anything).

1. Download Main OS 2.1.0 from
   [Sequential's download page](https://sequential.com/support/download/prophet-5-10-operating-system/)
   and unzip it. The Main OS file is named **`prophet5_main_2.1.0.syx`** — that is the one
   the patcher takes (not the Panel OS).
2. Open the patcher: **<https://dangerous.github.io/prophet-rev4-mods/>**. Drop the file on
   the page. It checks that the file is exactly the stock 2.1.0 OS, applies the patch in
   your browser (nothing is uploaded), checks that the result is exactly the released image,
   and offers `prophet10_native.syx` for download.
3. On the synth: Globals → **MIDI SysEx** = **USB**. Send `prophet10_native.syx` with your
   SysEx program, exactly as you would an official update. The display counts to 100 while
   the OS is written and the synth restarts.
4. Tap **A440**: its LED lights and the arpeggiator is on. You're done.

To go back to stock, send Sequential's original 2.1.0 file the same way. If an update ever
fails to boot, Sequential's
[troubleshooting guide](https://support.sequential.com/hc/en-gb/articles/5314996425362-Prophet-5-10-Keyboard-Troubleshooting)
describes bootloader mode — it needs a **5‑pin DIN MIDI** connection, so have one available
before you start.

The same patch can be applied from a terminal with Python 3.9+ and nothing else installed:

```bash
python3 -m tools apply site/manifest.js prophet5_main_2.1.0.syx prophet10_native.syx
```

Both routes refuse a wrong input file and refuse to hand over a result whose hash does not
match the release (`dist/SHA256SUMS`).

## Manual

The arp lives on the **A440** button. A **tap** toggles it; **holding** it turns the
buttons around it into the arp's controls. Every message the patch puts on the display
goes away after 1.5 s and the program number comes back.

### Arpeggiator

| Control | Action | Display |
| --- | --- | --- |
| Tap **A440** | Arp on / off (LED) | BPM / `OFF` |
| A440 + **Bank** / **Group** | Next / previous mode | `UP` `dn` `Ud` `rnd` `ASS` |
| A440 + **Program 1–4** | 1–4 octaves | `o 1` … `o 4` |
| A440 + **Program 5** | Clock: internal / MIDI sync | `int` / `Syn` |
| A440 + **Program 7** / **8** | Note value longer (−) / shorter (+) | `2 4 8d 8 8S 8t 16 16S 16t 32` |
| A440 + **Glide Rate** knob | Tempo 40–300 BPM | the BPM |
| A440 + **Unison** (tap repeatedly) | Tap tempo | `tAP`, then the BPM |
| **HOLD** (or pedal in `HLd` mode) | Latch | — |

- **Modes.** Up and Down by pitch; Up/Down bounces without repeating the top and bottom
  notes; Random picks a note from the held set at every step; **Assign** plays the notes in
  the order you pressed them (press a note again to repeat it in the pattern).
- **Octaves** play the whole pattern, then the pattern an octave up, and so on — not one
  big sorted set.
- **Note values** are the Prophet‑6's ten, in its order: half, quarter, dotted 8th, 8th,
  8th swing, 8th triplet, 16th, 16th swing, 16th triplet, 32nd. The swing values play pairs
  of steps long–short (2 : 1).
- **Tempo.** The knob and the taps set the internal clock. Under MIDI sync the arp follows
  the incoming clock and transport (Start/Stop/Continue, 24 ppqn, always on the DAW's grid);
  the tempo it measures from the clock is what the internal clock resumes at when you switch
  back to `int`, and the two tempo gestures just show `Syn`.
- **HOLD** latches whatever you hold. With HOLD on, the first key you press after releasing
  all keys starts a fresh chord instead of adding to the old one (re‑latch). One note sounds
  per step, HOLD or not — the synth's own sustain is suspended while the arp is on.
- Glide Rate on its own is always the normal glide, even with the arp running. Keys played
  via MIDI In arpeggiate like local keys; MIDI Out carries the keys you play, not the arp.

### Step sequencer

Recording happens in **record mode**:

| Control | Action | Display |
| --- | --- | --- |
| A440 + **Tune** | Enter record mode | `r 0`, A440 LED blinks |
| Play a note or a chord | Record a step (notes held together = one chord) | `r 1`, `r 2` … |
| **HOLD** with no key down | Insert a rest | `rSt`, then the count |
| **HOLD** while holding the step's keys | Tie: the step lasts one more step (repeatable) | `tiE`, then the count |
| A440 + **Program 6** | Start over (stays in record mode) | `r 0` |
| Tap **A440** (or A440 + Tune) | Finish | program number |

The count is the length recorded so far in arp steps — every gesture adds one. The
sustain pedal (in `HLd` mode) does the same as the HOLD button here; the HOLD latch itself
is not changed. What you play sounds as you play it. Finishing with nothing recorded keeps
your previous sequence; finishing never changes whether the arp is on.

Playing it back:

- With the arp on and a sequence recorded, pressing a key plays the sequence **transposed**
  so that the lowest note of its first chord lands on that key. A new key re‑transposes from
  the next step. Release everything and it stops; with **HOLD** on it keeps running, and the
  first key after releasing all keys restarts it from step 1.
- The arp's **direction** modes, **octaves**, **note value** and **clock** apply: `UP` plays
  the steps in order, `dn` backwards, `Ud` back and forth, `rnd` picks steps at random; each
  step keeps its chord and its length whatever the order. Ties are held for their length
  (released half‑way through the last step, like any step's gate) and stay on the grid under
  MIDI sync.
- A440 + **Program 6** outside record mode clears the sequence; you're back to the plain
  arp.
- Limits: 64 steps, 10 notes per step, a step can be tied up to 64 steps long. The sequence
  is global and not saved — it is gone at power‑off.

### Keyboard octave shift

| Control | Action | Display |
| --- | --- | --- |
| **Lo Freq** (Osc B) + **Bank** / **Group** | Keyboard up / down an octave (±2) | `001`, `-01` … |
| Lo Freq + Bank **and** Group together | Back to 0 | `000` |
| **Hold** Lo Freq on its own | Show the current shift | the shift |
| **Tap** Lo Freq | Osc B low‑frequency mode, as stock (on release) | — |

The shift applies to the keys you play, to what the arp and sequencer see, and to the
notes sent to MIDI Out; notes arriving from MIDI In are not shifted. A key always releases
at the pitch it was pressed at, so changing the shift mid‑chord never sticks a note. Not
saved with programs.

### Patch memory

Arp on/off, mode, octaves and note value are stored with the program when you save it
(they travel in SysEx program dumps too) and come back when the program is loaded; a
program saved without arp data loads with the arp off. Tempo, clock source, keyboard shift
and the sequence are global and not saved.

### Safety net

- **Kill switch.** Hold **A440** while powering on and the patch stays completely inactive
  for that session — every hook passes straight through to the stock OS.
- The **Globals** menu is pure stock while it is open; Sequential's A440 tuning tone is in
  there.
- **Button id readout.** Hold A440 and press a button the patch doesn't use: its panel id
  is shown. Handy if you want to add controls of your own.

## For developers

- [`docs/SPEC.md`](docs/SPEC.md) is the source of truth for every behaviour, with a marker
  recording whether it has been verified on an instrument;
  [`docs/hardware-checklist.md`](docs/hardware-checklist.md) is the manual test pass;
  [`docs/NOTES.md`](docs/NOTES.md) the engineering context; [`docs/re/`](docs/re/) the
  reverse‑engineering notes on the stock OS.
- `firmware/` — the engine in C11 (`arp.c` pattern and clock, `arpui.c` controls and display,
  `oct.c`, `rate.c`, `disp.c`, `vhold.c`; `native.c` the hooks and the stock interface
  table). It is one 32 KB record added at `0x20088000` plus 18 retargeted sites — 14 `BL`s
  and the four MIDI‑parser table words for realtime bytes (`firmware/hooks_native.json`).
  Nothing runs at boot; the engine only runs when a hooked stock call fires.
- `tools/` — Python 3.9+, standard library only: `inspect`, `unpack`, `pack`, `diff`,
  `fwbuild`, `build`, `manifest`, `apply`.
- `site/` — the patcher page (`index.html`, `patcher.js`) and the generated
  `manifest.js`, published by GitHub Pages from that directory alone.
- `fixtures/` — where Sequential's Main 2.1.0 and Panel 1.1.3 files go for the tests and the
  build (git‑ignored; `fixtures/README.md` says where they come from and lists their
  hashes).

```
make test       # tooling tests, host harnesses, cross-build and image invariants
make image      # build/prophet10_native.syx
make manifest   # site/manifest.js from that image
```

The cross build needs Apple clang (Xcode command line tools). `dist/SHA256SUMS` records
the hash of the image built at HEAD; the build is deterministic for a given compiler.

Controls follow the conventions of Nicolas Maldonado's **Arp Mod V5**, whose documented
behaviour the arp was modelled on (A440 as the arp button, Bank/Group for modes, Program
buttons for octaves and clock, Glide Rate for tempo); the deliberate differences are listed
in the spec. The project began in October 2026 as hooks chained in front of V5 and was
re‑implemented as this self‑contained, MIT‑licensed engine the next day.
