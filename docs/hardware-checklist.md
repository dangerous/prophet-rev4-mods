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
      **not** play the tuning tone (it is the arp button) — nor with the Globals menu open,
      where stock ignores it (no tone, no LED, arp unchanged).
- [ ] Arp off: hold A440, press and release HOLD (RELEASE/HOLD): the tuning tone sounds, A440
      LED lit, HOLD LED unchanged; release A440: the arp stays off. Again: tone off, LED off.
      Tone on, then tap A440: tone stops, arp on (LED stays lit for the arp). Arp on: A440 +
      HOLD does nothing and the A440 release does not toggle.
- [ ] Tap A440: LED on, display `120`, patch number back after 1.5 s. Hold C‑E‑G: C E G C…
      at 120 BPM, eighths, one note at a time. Tap A440: `OFF`, keys sound again.

## Arp engine (spec "Arp engine")

- [ ] Arp on, HOLD off: play a chord, release, wait, play another: starts immediately.
- [ ] A440 + Bank/Group: `dn` (G E C), `Ud` (C E G E C…), `rnd`, `ASS`, `UP`; Group goes
      the other way (`UP` → `ASS` → `rnd`). A440 + Program 2:
      `o 2` → C E G C' E' G'. Hold C3 + D4 at `o 2`: C3 D4 C4 D5 (per pass).
- [ ] Hold A440 and turn Glide Rate: BPM shown while turning, 40–300, the tempo follows,
      patch number back after 1.5 s; releasing A440 does **not** toggle the arp. Also with
      the arp off (hold A440, turn, release: still off; tap on: the new BPM). Under `Syn`:
      hold A440 and turn Glide Rate → `Syn`, the tempo is unchanged, no toggle on release.
- [ ] Tap tempo (spec "A440 + Velocity is tap tempo"): hold A440, tap Velocity four times
      at a steady ~120 BPM → `tAP` on the first tap, then ~120 (the tempo follows); tap at
      ~90 → ~90. Pause 3 s, tap once → `tAP`. Release A440: the arp does **not** toggle. Also
      with the arp off (then tap A440 on: the tapped BPM). Velocity without A440 is stock
      Velocity; A440 + Unison now just shows `25`.
      Under `Syn` (A440 + Program 5): A440 + Velocity shows `Syn`, the tempo is unchanged,
      and releasing A440 does not toggle the arp; back at `int` the next tap shows `tAP`.
- [ ] Note values: from `2` (Half), A440 + Program 7 three more times → `1`, `2b`, `4b`
      (stays `4b`); at `4b`, 120 BPM, one step every 8 s (16 beats), the note released at 4 s.
      Save a program at `4b`, load another, reload: `4b` comes back; a program saved before
      this build loads with its old note value.
- [ ] Assign (spec "Pattern" — Assign): A440 + Bank to `ASS`. HOLD off, play G C E D (one
      after another, keep them down): G C E D G C E D … (not sorted). Release E: G C D.
- [ ] Assign + HOLD: hold G down and play C, E, D (releasing them): G C E D. Still holding G,
      press C again: G C E D C. Release everything: keeps playing. Play a new chord: it
      replaces the pattern (in the new order).
- [ ] Assign at `o 2`: G C E D, then G C E D an octave up.
- [ ] Assign with a recorded sequence: plays the recorded order (as `UP`).
- [ ] Arp running, Glide Rate alone (no A440): it is **glide** — the step notes glide, the
      tempo does not change; the patch's glide value was not changed by the A440 + Glide
      turns before.
- [ ] A440 + Program 5: `Syn`. DAW clock: steps on the grid, Start restarts, Stop silences,
      Continue resumes; pull the cable: silence after 1 s. A440 + Program 5: `int`.
- [ ] `Syn` with the DAW at 100 BPM, run a few beats, A440 + Program 5 to `int`: the arp
      continues at 100 (tap A440 off/on: shows `100`). Repeat at 140.
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
- [ ] Save a program in `ASS` (arp on, e.g. `8t`, `o 3`). Load another program, reload it:
      `ASS`, `8t`, `o 3`, on. Then `UP` again and re-save: reloads as `UP`.
- [ ] Programs saved with arp settings by an earlier build (only the 2026-10-07 test saves)
      need re-saving: they load with other settings.
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

- [ ] Arp on, HOLD off. Hold A440, press Tune: display `r 0`, A440 LED blinks, no auto-tune
      ran. Release A440: the arp does **not** toggle.
- [ ] Play C, then E, then G, then E (one at a time): the notes sound as you play them;
      display `r 1`…`r 4`.
- [ ] Play a C-E-G chord (fingers landing in any order), release, then a single D: `r 5`,
      `r 6` — the chord is one step.
- [ ] With no key down press HOLD: `rSt` flashes, then `r 7` (a rest); HOLD LED unchanged.
- [ ] Press a key and keep it down: `r 8`. Press HOLD: `tiE` flashes, then `r 9`. Press HOLD
      again: `tiE` flashes, then `r10`. Release the key. (That step is three arp steps long;
      the count is the length recorded so far in arp steps — every gesture adds one.)
- [ ] Sustain pedal (`HLd` mode): pedal with no key down = rest, pedal with a key down = tie.
- [ ] Arp off, record two chords, tap A440: the arp comes on (LED, BPM) and the sequence plays
      on the next key. Arp off, A440 + Tune, tap A440 with nothing recorded: the arp stays off.
- [ ] Arpeggiated playback: record C major, F major, G major, C major as four chord steps.
      A440 + Unison → `ArP`. Note value `16`, chord length `1` (A440 + Aftertouch shows the
      cycle `2b 4b 4 2 1`). Hold a key: 16 sixteenths over C, then F, G, C — a bar each; `dn`
      runs each chord downwards, chords still in order; `o 2` arpeggiates each chord over two
      octaves. Chord length `4`: a chord per beat. A tied step lasts twice as long; a rest is
      silence. A440 + Unison → `Std`: the chords as blocks again. Save a program in `ArP` with
      `2b`, load another, reload: both come back.
- [ ] Accompany: arp on, chord latched (HOLD). A440 + Keyboard Amount → `ACC`. Play: notes
      sound directly on top, the arp unchanged (chords, velocity; MIDI-in too). Pedal down:
      your notes sustain, the arp's latch stays; pedal up: they release. Tap A440: arp off,
      keys still play; tap A440: the arp resumes from the same latched notes. A440 +
      Keyboard Amount: keys back to the arp (a held key releases normally; pressed again it
      re-latches). HOLD off while accompanying: `ACC` ends. Arp off, or arp on but nothing
      latched: A440 + Keyboard Amount does nothing.
- [ ] Two HOLD latches: arp on, HOLD on (latched notes play). Tap A440 off: HOLD LED goes
      off, nothing is held. Press HOLD (stock hold on), play and release: notes sustain. Tap
      A440 on: HOLD LED on again, the arp latches, the synth's hold is suspended. Tap A440
      off: HOLD LED stays on (stock hold back). HOLD off; tap A440 on: the arp's latch is
      still on. Pedal (`HLd`): momentary in both modes.
- [ ] A440 + Bank while recording: `UP`… shows for 1.5 s, then `r N` returns.
- [ ] Tap A440: the patch number returns, LED steady (arp still on). Press and hold C: the
      sequence plays — single notes, the chord as a chord, the rest silent, the tied step
      held for three steps — at the arp tempo. Release: stops.
- [ ] Press D: transposed so the lowest note of the first step lands on D. While holding D
      press F: from the next step transposed to F. Release F (D still down): stays on F.
- [ ] HOLD on. Press C, release: keeps playing. Press G after releasing all: restarts from
      step 1 on G at the next step.
- [ ] A440 + Bank / Group: `dn` plays the steps reversed (chord and rest in their places),
      `rnd` shuffles steps, `Ud` bounces.
- [ ] A440 + Program 2: `o 2` → the sequence, then an octave up. Release A440: no toggle.
- [ ] `Syn` from a DAW: steps on the grid, the tied step spanning three grid steps.
- [ ] A440 + Tune, play 3 notes, tap A440: the new sequence replaces the old one. A440 +
      Tune then tap A440 with nothing played: the old sequence is kept, arp unchanged.
- [ ] In record mode, A440 + Program 6: `r 0`, still recording. Tap A440: no sequence.
- [ ] A440 + Program 6 outside record mode: sequence gone; keys play the normal arp again.
- [ ] Arp off, A440 + Tune, record, tap A440: still off, keys play normally. Tap A440: the
      sequence plays.
- [ ] HOLD lit before entering record mode: recorded notes still release on key-up; HOLD
      still lit on exit.
- [ ] Hold A440 (no Tune) and play keys: nothing is recorded; the keys arpeggiate (arp on)
      or play (arp off); releasing A440 toggles the arp as a tap does.
- [ ] Record from MIDI-in (DAW notes) in record mode, including a chord; trigger from the
      keyboard.

## Note value (spec "Note value")

- [ ] Arp on, internal clock, at the default `8`, hold a chord. Hold A440, press Program 8:
      `8S`; again: `8t`, `16`, `16S`, `16t`, `32`; once more: stays `32`.
- [ ] From `8` (reload the program or step back), A440 + Program 7: `8d`, `4`, `2`; once
      more: stays `2`.
- [ ] Swing: `8S` = long-short pairs, the pair one beat (triplet feel, long = 2 × short);
      `16S` = the same at sixteenths (pair = half a beat). The pattern order does not change.
      Under `Syn`: `8S` steps on clocks 0, 16, 24, 40 … and `16S` on 0, 8, 12, 20 … counted
      from Start (the long step falls on the beat).
- [ ] Patch memory: save a program at `8S`, another at `16S`; both reload with their swing.
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
- [ ] Hold Lo Freq alone: after the panel's hold delay the display shows the current shift
      (`000`, `001`, `-01`…) and keeps showing it while held; release: Osc B Lo Freq did
      **not** toggle.
- [ ] A quick tap of Lo Freq still toggles Osc B Lo Freq (LED changes on release).
- [ ] DAW recording MIDI from the Prophet: notes arrive shifted (Local Control on and off).
      MIDI-in notes played from the DAW are **not** shifted.
- [ ] Prophet-10 split mode: the split point moves with the shift (expected).

## Display messages (spec "Display messages")

- [ ] Each of these shows for about 1.5 s and then the **patch number** returns: A440 +
      Bank (`UP`…), A440 + Program 2 (`o 2`), A440 + Program 5 (`int`/`Syn`), A440 +
      Program 8 (`8S`), tap A440 on (BPM) and off (`OFF`), Lo Freq + Bank (`1`), A440 +
      Osc B Keyboard (`36`), A440 + Velocity (`tAP`). In record mode `rSt` / `tiE` flash for
      about a quarter of a second and return to `r N` instead of the patch number.
- [ ] A new message within the 1.5 s restarts the timing (e.g. Bank, Bank, Bank).

## Button id readout (spec "Button id readout")

- [ ] Hold A440, press a button the arp doesn't use (Osc B Keyboard = 36;
      GLOBALS = 13 must still open the menu — it abandons the hold).
- [ ] Release A440: the arp does not toggle.

## If something is wrong

- The engine only runs inside the hooked stock calls. The kill switch (A440 held from before
  power-on) bypasses every hook for the session. If the synth misbehaves otherwise, the
  USB OS-update path still works: re-send stock 2.1.0 over USB exactly as above. The DIN
  bootloader (`btl`, both wheels up at power-on) is only needed if the unit will not boot.
