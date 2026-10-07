# Hardware checklist

Manual verification on the instrument. Record results against the spec markers in
`docs/SPEC.md` (`[HW: verified <date>, <unit>]`).

## Before installing

- [ ] Back up patches (Globals → Pgm Dump → ALL → RECORD) if not done recently. The update
      does not touch patch storage, but it is cheap insurance.
- [ ] `make image`, copy `build/prophet10_native.syx` to `dist/`, then
      `cd dist && shasum -a 256 -c SHA256SUMS` (images are built, not shipped — `dist/README.md`).
- [ ] Globals → Program 6 (MIDI SysEx) set to `USB`; SysEx Librarian output = the Prophet;
      no other MIDI apps running, nothing else routed to the Prophet.

## Installing

1. Send `dist/prophet10_native.syx`. The display counts `000`→`100` (transfer), then
   `10`→`0` (writing). If the transfer is rejected the display stays at `100` (eight
   Program LEDs lit) or shows an error and nothing is written — the loader verifies before
   the write phase; a power cycle brings the previous OS back.
2. Do not power off during the write countdown. When it finishes, power cycle.
3. Globals + Program 1 should still show `2.1.0`.

## First things after a new build

- [ ] **Kill switch**: hold A440 from before power-on, keep it ~3 s. Everything must be pure
      stock: keys sound, A440 plays the tuning tone, Glide Rate is glide, HOLD sustains.
      Power cycle; this time tap A440 about 1 s after power-on: it must **not** kill the arp
      (tap again later: arp on as usual).
- [ ] Arp off: play, HOLD, sustain pedal, MIDI in, program change — all as stock. A440 does
      **not** play the tuning tone (it is the arp button); Globals → A440 still does.
- [ ] Tap A440: LED on, display `120`, patch number back after 1.5 s. Hold C‑E‑G: C E G C…
      at 120 BPM, eighths, one note at a time. Tap A440: `OFF`, keys sound again.

## Arp engine (spec "Arp engine")

- [ ] Arp on, HOLD off: play a chord, release, wait, play another: starts immediately.
- [ ] A440 + Bank/Group: `dn` (G E C), `Ud` (C E G E C…), `rnd`, `UP`. A440 + Program 2:
      `o 2` → C E G C' E' G'. Hold C3 + D4 at `o 2`: C3 D4 C4 D5 (per pass).
- [ ] Glide Rate while on: BPM shown while turning, 40–300, patch number back after 1.5 s;
      arp off: Glide Rate is glide again and the patch's glide value was not changed.
- [ ] A440 + Program 5: `Syn`. DAW clock: steps on the grid, Start restarts, Stop silences,
      Continue resumes; pull the cable: silence after 1 s. A440 + Program 5: `int`.
- [ ] CC 123 from the DAW: pool cleared, HOLD LED unchanged. Program change while the arp
      runs: the pattern continues (HOLD drops its latched notes, as stock).
- [ ] Globals menu open: Program buttons edit globals as stock even with A440 held;
      pressing GLOBALS while holding A440 abandons the hold (nothing toggles on release).
- [ ] Power cycle: arp off, Up, 1 octave, internal, 120 BPM, 1/8, shift 0, no sequence.

## Patch memory (spec "Patch memory")

- [ ] Load a few factory programs with the arp running: it switches **off** each time; mode,
      octaves and note value stay as they were. Listen for any change of sound with the arp
      on at `o 1`…`o 4` (parameters 93/94 reach the voice engine; they should be inert).
- [ ] Arp on, `dn`, `o 2`, `16`. Record it to a user program. Load another program (arp off),
      then load the saved one: arp on, `dn`, `o 2`, `16` restored. Power cycle on that
      program: the arp comes up on.
- [ ] Save a program with the arp **off** but `Ud`: loading it switches the arp off and sets
      `Ud`.
- [ ] MIDI program change from the DAW to the saved program: same as the panel.
- [ ] Dump the program over SysEx and load it back: settings survive.
- [ ] BPM, clock source and keyboard shift are **not** changed by loading.

## HOLD while the arp is on (spec "HOLD while the arp is on")

- [ ] Arp on, HOLD on. Hold C‑E‑G: **one note sounds at a time**. Release the keys: still
      one note at a time. Repeat with the sustain pedal in `HLd` mode.
- [ ] HOLD on, arp **off**: play and release a chord — it sustains (stock hold). Tap A440
      while it sustains: the sustained notes stop and the pattern plays from the keys still
      down / latched. Tap A440 again: the keys still down sustain again.
- [ ] Arp on, HOLD on, pattern latched. HOLD off: the released notes drop; the HOLD LED
      follows the button throughout.

## Re-latch (spec "Re-latch under HOLD")

- [ ] Arp on, HOLD on. Play C‑E‑G, release all. Arp keeps playing C‑E‑G.
- [ ] Play D‑F‑A: the arp plays **D‑F‑A only** from the next step (no hiccup in the
      groove). Release D‑F‑A: it keeps playing. Repeat with a third chord.
- [ ] Arp on, HOLD on. Play C‑E‑G, release C and E only (G held), play D: **adds** (C D E G).
- [ ] Arp on, HOLD **off**. Play C‑E‑G, release all, play D: D only, starting immediately.
- [ ] Arp on, HOLD on, pedal mode `HLd`: pedal down, play C‑E‑G, release keys, play D with
      the pedal still down → D only.
- [ ] Arp on, HOLD on. Hold a DAW/MIDI note, play and release local keys, play a new local
      key → **adds** (the MIDI note counts as a key still down). Release the MIDI note,
      play a key → fresh start.

## Seq (spec "Seq")

- [ ] Arp on, HOLD off. Hold A440; play C, E, G, E (one at a time, any timing); display
      counts `1 2 3 4`; the notes sound as you play them. Release A440 — the arp does
      **not** toggle.
- [ ] Press and hold C: C E G E repeating at the arp tempo. Release: stops.
- [ ] Press D: D F# A F#. While holding D press F: from the next step the pattern is
      transposed to F. Release F (D still down): stays on F.
- [ ] HOLD on. Press C, release: keeps playing. Press G after releasing all: restarts from
      step 1 on G at the next step.
- [ ] A440 + Bank / Group: `dn` plays E G E C, `rnd` shuffles, `Ud` bounces.
- [ ] A440 + Program 2: `o 2` → C E G E then an octave up. Release A440: no toggle.
- [ ] A440 + Program 6: sequence gone; keys play the normal arp again.
- [ ] Hold A440, play 3 notes, release: a new sequence replaces the old one. A tap of A440
      with no notes in between keeps the sequence and toggles the arp.
- [ ] Arp off, sequence exists: keys play normally. Tap A440: the sequence plays.
- [ ] Record from MIDI-in (DAW notes) while holding A440; trigger from the keyboard.

## Note value (spec "Note value")

- [ ] Arp on, internal clock, hold a chord. Hold A440, press Program 8: `16d`, faster;
      again: `8t`, `16`, `16t`, `32`; once more: stays `32`.
- [ ] A440 + Program 7 back up through `16t … 8 … 8d 4 4d 2 1 2b 4b`; stays at `4b`.
      Each change takes effect from the next step; the patch number returns after 1.5 s.
- [ ] Release A440 after changing the value: the arp does **not** toggle.
- [ ] `Syn` with DAW clock: `16` = sixteenths locked to the grid; `4` = quarters; `8d` =
      dotted eighths with boundaries on the grid (every 18 clocks); changing the value
      mid-run re-aligns at the next multiple counted from Start.
- [ ] Seq: record a sequence, hold a key, change the value: the sequence follows.

## Keyboard octave shift (spec "Keyboard octave shift")

- [ ] Tap the Osc B **Lo Freq** button: Lo Freq still toggles, LED changing on the *release*.
- [ ] Hold Lo Freq, press Bank: display `1`; keys an octave up; the Lo Freq LED did not
      change. Release Lo Freq: nothing else happens.
- [ ] Bank again: `2`; again: stays `2`. Group ×4: `1 0 -1 -2`; again stays `-2`.
- [ ] Hold Lo Freq, hold Group, press Bank: `0`.
- [ ] Shift `1`: hold a chord, change the shift to `-1` while holding, release the chord:
      no stuck notes; the next chord sounds an octave down.
- [ ] Arp on, shift `1`: arpeggiates an octave up; seq recording and triggering follow.
- [ ] DAW recording MIDI from the Prophet: notes arrive shifted (Local Control on and off).
      MIDI-in notes played from the DAW are **not** shifted.
- [ ] Prophet-10 split mode: the split point moves with the shift (expected).

## Display messages (spec "Display messages")

- [ ] Each of these shows for about 1.5 s and then the **patch number** returns: A440 +
      Bank (`UP`…), A440 + Program 2 (`o 2`), A440 + Program 5 (`int`/`Syn`), A440 +
      Program 8 (`16d`), tap A440 on (BPM) and off (`OFF`), Lo Freq + Bank (`1`), A440 +
      Unison (`25`), a seq step count.
- [ ] A new message within the 1.5 s restarts the timing (e.g. Bank, Bank, Bank).

## Button id readout (spec "Button id readout")

- [ ] Hold A440, press a button the arp doesn't use (Osc B Keyboard = 36, Unison = 25;
      GLOBALS = 13 must still open the menu — it abandons the hold).
- [ ] Release A440: the arp does not toggle.

## If something is wrong

- The engine only runs inside the hooked stock calls. The kill switch (A440 held from before
  power-on) bypasses every hook for the session. If the synth misbehaves otherwise, the
  USB OS-update path still works: re-send stock 2.1.0 over USB exactly as above. The DIN
  bootloader (`btl`, both wheels up at power-on) is only needed if the unit will not boot.
