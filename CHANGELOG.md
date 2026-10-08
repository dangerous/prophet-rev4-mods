# Changelog

Versions of the patch as published through the patcher — the `VERSION` file, the download's
name (`prophet5_main_2.1.0_patched_<version>.syx`) and `site/version.json`. Each entry names
the behaviour; the details are in [`docs/SPEC.md`](docs/SPEC.md).

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
