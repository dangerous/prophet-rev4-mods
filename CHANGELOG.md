# Changelog

Versions of the patch as published through the patcher — the `VERSION` file, the download's
name (`prophet5_main_2.1.0_patched_<version>.syx`) and `site/version.json`. Each entry names
the behaviour; the details are in [`docs/SPEC.md`](docs/SPEC.md).

## 2.0.0 — 2026-10-08

The step sequencer becomes a generator of its own, the Prophet‑6 way: it plays on its own
transport while the keyboard stays yours. A major version because the sequencer's controls
and behaviour change; the arpeggiator is unchanged and programs saved by 1.x load.

### Added
- **Two generators**, `ArP` and `SEq`, selected with A440 + **Keyboard** (the filter's
  Keyboard Amount button). A440 starts and stops the selected one and its LED shows it
  running; entering record mode selects `SEq`.
- **Independent transport**: a tap plays the recording from event 1 — at once under the
  internal clock, armed to the next grid step under MIDI sync, where MIDI Stop pauses and
  Continue resumes — and a tap stops it. **Play over it**: keys and MIDI‑in sound as normal,
  polyphonic, under the synth's own hold, and never disturb playback.
- **Orders** for the Chords style: `For`, `bAC`, `Pnd` (A440 + Bank / Group with `SEq`
  selected).
- **Transposition** by key: A440 + a key, middle C = 0, applied from the next event or chord;
  survives stop/start, style and generator changes and program loads; a new recording resets
  it.
- **Back** in record mode: Group alone undoes the last tie, else the last chord or rest.
- **Capacity 512 timing steps** — chords, rests and ties counted together (was 64 events).
- **Two note values**: the arp's (saved with the program) and the sequencer's (kept with the
  recording); A440 + Program 7 / 8 edit the selected generator's.

### Changed
- The sequence's **style** (A440 + Unison, now `CHd` / `ArP` — `POL` is `CHd`) and **chord
  length** (A440 + Aftertouch) are session settings and no longer saved with programs;
  programs saved by 1.2.0 load with those bits ignored.
- **Leaving record mode** leaves the sequencer selected and stopped; the next tap plays it
  (1.2.0 switched the arp on).
- A440 + Program 6 outside record mode clears the sequence and selects `ArP`.
- The tuning tone (A440 + HOLD) is available whenever the selected generator is stopped.
- The engine's state area grows to 16 KB (and the code limit shrinks to 16 KB) for the 512
  events.

### Removed
- **Trigger‑key playback**: the sequence no longer plays from a held key transposed to it,
  nor in the arp's direction modes, octaves or random order, and HOLD no longer latches it.
- Finishing a recording no longer switches the arp on.

## 1.2.0 — 2026-10-08

### Added
- **Arpeggiated playback** of the step sequencer: A440 + **Unison** switches the sequence
  between `POL` (each step sounds as the chord it is, as before) and `ArP`, where the
  sequence is a chord progression for the arpeggiator — each chord is held for the **chord
  length** (A440 + **Aftertouch**: quarter, half, whole, 2 bars, 4 bars, shown as `4 2 1 2b
  4b`) while the arp runs over its notes at the note value, in the arp's direction mode and
  octaves. Chords always advance in order; ties make a chord last longer; rests are silence.
  Both settings are saved with the program.
- Finishing a recording switches the arp on if it was off — a recording is made to be heard.

### Changed
- **Two HOLD latches**: the HOLD button latches the arp while the arp is on and the synth's
  own hold while it is off, each remembering its state while the other is in use; the HOLD
  LED shows the active one. Switching the arp off no longer leaves the synth in hold.

## 1.1.0 — 2026-10-08

### Added
- Three long note values above the Prophet‑6's ten: **4 bars**, **2 bars** and a **whole
  note** (`4b`, `2b`, `1`), for pad sequences — 64 sequencer steps of 4 bars make a 256‑bar
  sequence. Saved with the program like the other values; programs saved before this
  version load unchanged.
- Sequential's **A440 tuning tone** is available again, on **A440 + HOLD** with the arp off
  (stock's tone and LED). A tap of A440 while it sounds only switches it off.

### Changed
- **Tap tempo** moved from A440 + Unison to **A440 + Velocity** — the button beside A440,
  so tapping is one‑handed. A440 + Unison now just shows the button id (25); it is reserved
  for a coming chord / arpeggiate switch for the sequencer.

### Fixed
- The documentation claimed the tuning tone was still reachable from the Globals menu; stock
  ignores A440 while its menu is open, which is why the tone has its own combo now.

## 1.0.0 — 2026-10-08

First release through the browser patcher.

- **Arpeggiator**: Up, Down, Up/Down, Random and Assign (the notes in the order played);
  1–4 octaves played per pass; HOLD latch with re‑latch; 40–300 BPM from the Glide Rate knob
  while A440 is held, or tap tempo; MIDI clock sync with Start/Stop/Continue and the BPM
  following the clock; the Prophet‑6's ten note values in its order, including the swing
  values; one note per step even under HOLD.
- **Polyphonic step sequencer**: record mode on A440 + Tune; up to 64 steps of chords (up
  to 10 notes, velocities kept), rests and ties from the HOLD button or the sustain pedal;
  the `r N` length readout with `rSt` / `tiE` flashes and a blinking A440 LED while
  recording; playback transposed from any key, in the arp's direction modes, octaves, note
  values and clock; HOLD latches it.
- **Keyboard octave shift** ±2 on Lo Freq + Bank / Group, applied to the keys, the arp and
  MIDI Out; holding Lo Freq shows the current shift.
- **Patch memory**: the arp's on/off, mode, octaves and note value are saved with the
  program.
- **Safety**: kill switch (hold A440 while powering on), the Globals menu left to stock, a
  button id readout for mapping new combinations.
- **Distribution**: the browser patcher on GitHub Pages and the `python3 -m tools apply`
  reference applier, with the release version in the download's name.
