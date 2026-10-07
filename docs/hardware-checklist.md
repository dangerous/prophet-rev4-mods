# Hardware checklist

Manual verification on the instrument. Record results against the spec markers in
`docs/SPEC.md` (`[HW: verified <date>, <unit>]`).

## Before installing

- [ ] The instrument currently runs **V5** (install was from
      `fixtures/V5_prophet5_main_2.1.0_arp_MIDI_SYNC.syx`). This image is built on top of
      V5; it is not meant to be installed over stock 2.1.0 directly (it would work — the
      file is complete — but it has only been reasoned about as a V5 successor).
- [ ] Back up patches (Globals → Pgm Dump → ALL → RECORD) if not done recently. The update
      does not touch patch storage, but it is cheap insurance.
- [ ] `make image-internal`, then `shasum -a 256 build/prophet10_v5_relatch_seq_internal.syx`
      matches the line in `dist/SHA256SUMS` (images are built, not shipped — `dist/README.md`).
- [ ] Globals → Program 6 (MIDI SysEx) set to `USB`; SysEx Librarian output = the Prophet;
      no other MIDI apps running, nothing else routed to the Prophet.

## Installing

1. Send `build/prophet10_v5_relatch_seq_internal.syx`. The display counts `000`→`100` (transfer), then
   `10`→`0` (writing). If the transfer is rejected the display shows an error and nothing
   is written — the loader verifies the checksum before the write phase.
2. Do not power off during the write countdown. When it finishes, power cycle.
3. Globals + Program 1 should still show `2.1.0`.

## Smoke tests (everything V5 did should still work)

- [ ] Play normally with the arp off. Notes sound and release as before.
- [ ] HOLD on, arp off: play a chord, release — it sustains. HOLD off — it stops.
- [ ] Sustain pedal in `HLd` mode behaves like HOLD; in `rEL` mode it does not latch.
- [ ] Tap A440: arp on (LED). Hold a chord: it arpeggiates. Tap A440: arp off.
- [ ] A440 + Bank/Group cycles `UP dn Ud rnd`; A440 + Program 1–4 sets `o 1`–`o 4`;
      A440 + Program 5 toggles `int`/`Syn`; Glide Rate changes BPM while the arp is on.
- [ ] MIDI-in notes arpeggiate; MIDI clock sync still follows the DAW.
- [ ] Patch select, save, Globals menus unchanged.

## Re-latch (spec "Re-latch under HOLD")

- [ ] Arp on, HOLD on. Play C‑E‑G, release all. Arp keeps playing C‑E‑G.
- [ ] Play D‑F‑A. Arp plays **D‑F‑A only** (not C‑D‑E‑F‑G‑A).
- [ ] Release D‑F‑A: the arp **keeps playing** D‑F‑A (hold still latches after a re‑latch).
      Repeat with a third chord.
- [ ] Arp on, HOLD on. Play C‑E‑G, release C and E only (G held), play D: arp plays
      D‑E‑G… i.e. **adds** (C and E were released under hold, so C‑D‑E‑G).
- [ ] Arp on, HOLD **off**. Play C‑E‑G, release all, play D: arp plays D only (as before).
- [ ] Arp **off**, HOLD on. Play C‑E‑G, release all, play D: all four sustain (stock hold
      behaviour unchanged — no clear is issued when the arp is off).
- [ ] Arp on, HOLD on, pedal mode `HLd`: pedal down, play C‑E‑G, release keys, play D
      with pedal still down → D only.
- [ ] Arp on, HOLD on. Hold a DAW/MIDI note, play and release local keys, play a new local
      key → **adds** (the MIDI note counts as a key still down). Release the MIDI note,
      play a key → fresh start.
- [ ] HOLD on → off → on with nothing held, then play: no glitch, chord starts normally.

## Seq (spec "Seq") — combined image only

- [ ] Arp on, HOLD off. Hold A440; play C, E, G, E (one at a time, any timing); display
      counts `1 2 3 4`; the notes sound as you play them. Release A440 — the arp does
      **not** toggle.
- [ ] Press and hold C: hear C E G E repeating at the arp tempo. Release: stops.
- [ ] Press D: D F# A F#. While holding D press F: from the next step the pattern is
      transposed to F. Release F (D still down): stays on F.
- [ ] HOLD on. Press C, release: keeps playing. Press G: restarts from step 1 on G.
- [ ] A440 + Bank / Group: `dn` plays E G E C, `rnd` shuffles, `Ud` bounces.
- [ ] A440 + Program 2: display `o 2`; the pattern plays C E G E then an octave up.
      Release A440: the arp does **not** toggle (LED unchanged, sequence keeps playing).
      A440 + Program 1 back to one octave.
- [ ] Glide Rate still sets tempo; `Syn` follows DAW clock; start/stop as with the arp.
- [ ] A440 + Program 6: sequence gone; keys play the normal arp again at the octave last
      selected (`o N` shown if the arp's value changes); A440 + Program 1–4 then behave as
      in V5.
- [ ] Hold A440, play 3 notes, release: a new sequence replaces the old one.
- [ ] Arp off, sequence exists: keys play normally (no sequence). Tap A440: sequence plays.
- [ ] Record from MIDI-in (DAW notes) while holding A440; trigger from the keyboard.

## Note value (spec "Note value")

- [ ] Arp on, internal clock, hold a chord. Hold A440, press Program 7: display `16d`,
      pattern noticeably faster; again: `8t`, `16`, `16t`, `32`; once more: stays `32`.
- [ ] A440 + Program 8 back up through `16t … 8 … 8d 4 4d 2 1 2b 4b`; stays at `4b`.
      Each change takes effect from the next step; the display returns to BPM after ~1 s.
- [ ] Release A440 after changing the value: the arp does **not** toggle.
- [ ] Glide Rate still changes tempo at every value; `o 2` and modes still work.
- [ ] Seq: record a sequence, hold a key, change the value: the sequence follows.
- [ ] (`_internal` image) `Syn` with DAW clock: the arp plays eighths whatever the value
      shows — expected; the clock filter is only in the full image.
- [ ] (full image) MIDI sync (`Syn`, DAW clock running): at `8` the arp plays eighths as in V5. Select
      `16`: sixteenths locked to the DAW grid; `4`: quarters; `8d`: dotted eighths, with
      step boundaries staying on the DAW grid (every 18 clocks). Stop/Start/Continue as
      before. Pull the USB/MIDI cable with clock running: notes stop after ~1 s at every
      value.
- [ ] Power cycle: back to `8`.

## Keyboard octave shift (spec "Keyboard octave shift")

- [ ] Tap the Osc B **Lo Freq** button: Lo Freq still toggles, LED changing on the
      *release*.
- [ ] Hold Lo Freq, press Bank: display `1`; keys sound an octave up; the Lo Freq LED did
      not change. Release Lo Freq: nothing else happens.
- [ ] Bank again: `2`; again: stays `2`. Group ×4: `1 0 -1 -2`; again stays `-2`.
- [ ] Hold Lo Freq, hold Group, press Bank: `0`.
- [ ] Globals → Local Control off, shift `1`: the DAW receives shifted notes; back on.
- [ ] Shift `1`: hold a chord, change the shift to `-1` while holding, release the chord:
      no stuck notes; the next chord sounds an octave down.
- [ ] Arp on, shift `1`: arpeggiates an octave up. HOLD + re-latch still work. Seq
      recording and triggering follow the shifted keys.
- [ ] DAW recording MIDI from the Prophet: notes arrive shifted. MIDI-in notes played
      from the DAW are **not** shifted.
- [ ] Prophet-10 split mode: the split point moves with the shift (expected).
- [ ] Power cycle: shift back to 0.

## HOLD while the arp is on (spec "HOLD while the arp is on")

- [ ] Arp on, HOLD on. Hold C‑E‑G: **one note sounds at a time** (no C, C+E, C+E+G
      pile‑up). Release the keys: still one note at a time. Repeat with the sustain pedal in
      `HLd` mode.
- [ ] HOLD on, arp **off**: play and release a chord — it sustains (stock hold unchanged).
      Tap A440 while it sustains: the sustained notes stop and the arp plays the pattern from
      the keys still down / latched. Tap A440 again: the keys still down sustain again.
- [ ] Arp on, HOLD on, pattern latched. HOLD off: the arp drops the released notes as
      before; the HOLD LED follows the button throughout.
- [ ] Seq and re‑latch under HOLD behave as in their sections (one note per step).

## Display messages (spec "Display messages")

- [ ] Each of these shows for about 1.5 s and then the **patch number** returns: A440 +
      Bank (`UP`…), A440 + Program 2 (`o 2`), A440 + Program 5 (`int`/`Syn`), A440 + Program
      7 (`16d`), tap A440 on (`BPM`) and off (`OFF`), Lo Freq + Bank (`1`), A440 + Unison
      (id), a seq step count.
- [ ] Arp on, internal: turn Glide Rate — BPM shows while turning, then 1.5 s after you
      stop the patch number returns. Nothing ever stays on the display permanently.
- [ ] A new message within the 1.5 s restarts the timing (e.g. Bank, Bank, Bank).

## Button id readout (spec "Button id readout")

- [ ] Hold A440, press a button the arp doesn't use (e.g. Osc B Keyboard = 36, Hold,
      Preset, Record, Unison = 25): display shows its id — note it down.
- [ ] Release A440: the arp does not toggle; the buttons do nothing else while A440 is held.

## Native image (spec "Native arp engine (stock 2.1.0 base)")

`build/prophet10_native.syx` (also in `dist/`). The file is a complete Main OS, so it
installs over V5 or stock alike. First flash of a new engine — test in this order and stop
at the first surprise:

- [ ] **Kill switch first**: power on and **press A440 within 3 s** (holding it from before
      power-on should work too). Everything must be pure stock: keys sound, A440 plays the
      tuning tone, Glide Rate is glide, HOLD sustains. Power cycle without touching anything.
- [ ] Arp off: play, HOLD, sustain pedal, MIDI in, program change — all as stock. A440 does
      **not** play the tuning tone (it is the arp button); Globals → A440 still does.
- [ ] Tap A440: LED on, display shows `120` then the patch number after 1.5 s. Hold C‑E‑G:
      C E G C… at 120 BPM, eighths, one note at a time. Tap A440: `OFF`, keys sound again.
- [ ] Arp on, HOLD off: play a chord, release, wait, play another: starts immediately.
      HOLD on: the chord latches, one note at a time; play a new chord after releasing all:
      replaces at the next step (no hiccup). Release keys: still latched. HOLD off: drops.
- [ ] A440 + Bank/Group: `dn` (G E C), `Ud` (C E G E C…), `rnd`, `UP`. A440 + Program 2:
      `o 2` → C E G C' E' G'. Hold C3 + D4 at `o 2`: C3 D4 C4 D5.
- [ ] Glide Rate while on: BPM shown while turning, 40–300, patch number back after 1.5 s;
      arp off: Glide Rate is glide again and the patch's glide value was not changed.
- [ ] A440 + Program 7/8 note values (`16d`, `16`, …, `4b`); A440 + Program 5: `Syn`;
      DAW clock: steps on the grid, Start/Stop/Continue; pull the cable: silence after 1 s.
- [ ] Seq: hold A440, play C E G E (display 1 2 3 4), release; press D: D F# A F#; HOLD
      latches it; A440 + Program 6 clears. Lo Freq + Bank/Group: keyboard shift ±2, MIDI
      Out shifted. A440 + Unison: `25` (readout).
- [ ] CC 123 from the DAW: pool cleared, HOLD LED unchanged. Program change while the arp
      runs: the pattern continues (HOLD drops its latched notes, as stock).
- [ ] Globals menu open: Program buttons edit globals as stock even with A440 held.

## If something is wrong

- The wrapper only runs inside the arp's hook paths. If a hook misbehaves, the synth still
  boots and the USB OS-update path still works: re-send V5 (or stock 2.1.0) over USB
  exactly as above. The DIN bootloader (`btl`) is only needed if the unit will not boot.
