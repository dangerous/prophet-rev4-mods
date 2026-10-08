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
      Tone on, then tap A440: tone stops, the arp stays off; the next tap: arp on. Arp on:
      A440 + HOLD does nothing and the A440 release does not toggle. The same with the
      sequencer selected: stopped → tone, playing → nothing.
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
- [ ] Two HOLD latches: arp on, HOLD on (latched notes play). Tap A440 off: HOLD LED goes
      off, nothing is held. Press HOLD (stock hold on), play and release: notes sustain. Tap
      A440 on: HOLD LED on again, the arp latches, the synth's hold is suspended. Tap A440
      off: HOLD LED stays on (stock hold back). HOLD off; tap A440 on: the arp's latch is
      still on. Pedal (`HLd`): momentary in both modes.
- [x] **Rule 4a:** sequencer selected and playing chords, HOLD on. Play and release a key:
      it sustains (the engine's own sustain). The sequence's chords still release at their
      gates. Press the sustained key again: it retriggers cleanly. Stop the sequence (tap):
      the sustained note keeps sounding; HOLD off: it stops. HOLD on again, play and release
      with the sequence stopped: stock's hold sustains as usual. Pedal (`HLd`) the same.
      `[2026-10-08: the first 2.0.0 build let the chords sustain too — the voice engine's own
      hold flag decides, so the hold is now suspended while the sequencer runs]`. Then: arp
      on, HOLD latched, A440 + Keyboard → `SEq`: HOLD LED goes off (stock's latch back), the
      arp's latch returns when the arp is next started.

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

## Seq (spec "Seq") — the independent sequencer (2.0.x; panel run-through 2026-10-08, the MIDI-sync items still open)

Record mode (verified in 1.x; re-check what changed):

- [ ] Arp on, HOLD off. Hold A440, press Tune: display `r 0`, A440 LED blinks, no auto-tune
      ran, **the arp has stopped** (keys you were holding keep sounding as plain notes until
      released). Release A440: nothing toggles.
- [ ] Play C, then E, then G, then E (one at a time): the notes sound as you play them;
      display `r 1`…`r 4`.
- [ ] Play a C-E-G chord (fingers landing in any order), release, then a single D: `r 5`,
      `r 6` — the chord is one step.
- [ ] With no key down press HOLD: `rSt` flashes, then `r 7` (a rest); HOLD LED unchanged.
- [ ] Press a key and keep it down: `r 8`. Press HOLD: `tiE` flashes, then `r 9`. Press HOLD
      again: `tiE`, `r10`. Release the key. (Three timing steps; the count is timing steps.)
- [ ] **Back**: press Group (A440 up): `r 9`, again `r 8` (the ties), again `r 7` (the key),
      again `r 6` (the rest). Held Group: no repeat. Bank alone: stock (bank changes).
      Group with a key still down: the step goes and the key, kept down, does not rejoin;
      its release still silences it. Back at `r 0` right after entering: the old sequence is
      kept; after one entry, backing to `r 0` leaves nothing.
- [ ] Sustain pedal (`HLd` mode): pedal with no key down = rest, pedal with a key down = tie.
- [ ] A440 + Unison / Aftertouch / Program 7-8 / Bank while recording: the message shows for
      1.5 s, then `r N` returns. A440 + Program 6: `r 0`, still recording.
- [ ] Tap A440: the patch number returns, LED dark, **nothing plays** — the sequencer is
      selected and stopped. A440 + Tune, then tap A440 with nothing played: the old sequence
      is kept.
- [ ] Record from MIDI-in (DAW notes) in record mode, including a chord.

Transport and generators:

- [ ] With a recording, tap A440: the sequence plays from step 1 at the tempo (BPM shown,
      LED lit): single notes, the chord as a chord, the rest silent, the tied step held for
      three steps and released half-way through its last one. Tap A440: `OFF`, silence.
- [ ] While it plays: play the keyboard — every key sounds polyphonically with its velocity
      and nothing changes in the sequence; HOLD / pedal sustain *your* notes only (rule 4a
      above); starting or stopping the sequence does not cut your notes.
- [ ] A440 + Keyboard (filter Keyboard Amount, id 8): `ArP`; tap A440: the keys arpeggiate as
      before, the sequence untouched. A440 + Keyboard: `SEq`; tap: it plays again from step 1.
      With no recording (after power-up): A440 + Keyboard shows `---` and `ArP` stays.
- [ ] Switching generators while one plays stops it (its notes released) and leaves the other
      stopped; keys you hold keep sounding.
- [ ] Transposition: sequencer selected, hold A440, press D above middle C: display `002`,
      the key is silent and does not go to MIDI Out; from the next step everything is up a
      tone. Hold A440, press middle C: `000`, as recorded. A440 + a low key: `-12`. A second
      key during the same A440 hold plays normally. Releasing A440 after the command does
      not start/stop. The offset survives stop/start, `CHd`/`ArP`, `ArP`/`SEq` and program
      loads; a new recording's first entry and A440 + Program 6 reset it.
- [ ] Orders (chords style): A440 + Bank → `bAC` (backwards), `Pnd` (back and forth, ends not
      repeated), `For`. A440 + Group goes the other way. The arp's `dn`/`o 2` do nothing to
      the sequence.
- [ ] Note values: A440 + Program 7 with `SEq` selected changes the sequencer's value (display
      as the arp's); the arp's own value is unchanged (check after A440 + Keyboard → `ArP`).
      The change takes effect from the next step without a stumble.
- [ ] Arpeggiated: record C major, F major, G major, C major as four chord steps. A440 +
      Unison → `ArP`. Sequencer note value `16`, chord length `1` (A440 + Aftertouch shows
      the cycle `2b 4b 4 2 1`). Tap A440: 16 sixteenths over C, then F, G, C — a bar each;
      `dn` runs each chord downwards, chords still in order; `o 2` arpeggiates each chord over
      two octaves. Chord length `4`: a chord per beat. A tied step lasts twice as long; a rest
      is silence. A440 + Unison while playing → `CHd`: the chords as blocks again from step 1
      at the next step. Dotted 8th (`8d`) into a bar: the last arp note is cut at the chord
      change, the change on time; `8t` fits.
- [ ] A440 + Program 6 (not recording): `---`, the sequence is gone, `ArP` selected; a tap
      arpeggiates the keys.
- [ ] Program loads: play the sequence, load another program: it stops, the recording and
      the transposition are kept (tap: plays again). With `SEq` selected, load a program saved
      with the arp on: the arp stays off; A440 + Keyboard → `ArP` (stopped), load it again:
      the arp comes on. A program saved by 1.2.0 in `ArP`/`2b`: loads with its octaves and
      note value, the sequencer's style and chord length untouched.
- [ ] `Syn` from a DAW, sequencer selected: tap A440 → `Syn`, LED lit, silence until the DAW
      runs; press Start: step 1 on the first clock, steps on the grid, the tied step spanning
      three grid steps; Arpeggiated chord changes on the bar lines. DAW Stop: silence; DAW
      Continue: carries on where it was; DAW Start: step 1. Tap A440: `OFF`; DAW Start/
      Continue do not restart it. Stop the clock for a second while it plays: silence; clocks
      again: from step 1.
- [ ] CC 123 (all notes off) from the DAW while the sequence plays: silence and stopped; the
      recording is kept.
- [ ] HOLD lit before entering record mode: recorded notes still release on key-up; HOLD
      still lit on exit.
- [ ] Hold A440 (no Tune) with `ArP` selected and play keys: nothing is recorded; the keys
      arpeggiate (arp on) or play (arp off); releasing A440 toggles the arp as a tap does.

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

## Flash diagnostic (spec "Flash diagnostic") — read-only

- [ ] Back up patches first anyway (Globals → Pgm Dump → ALL → RECORD): the run only reads,
      but this is the first time the engine touches the flash driver at all.
- [ ] Hold A440, press Sync (Osc A Sync): `FLA` appears and stays up for about 2 s (10 s on a 16 MB
      part), then three results of 1.5 s each — `F 8`/`F16`/`F -`, `1 E`/`1 U`, `2 E`/`2 U` —
      then the patch number. Note all three in `docs/re/flash.md` and the spec marker. If a
      **number** shows instead of `FLA`, Sync's id is not 24: note the number, nothing else
      happens. Releasing A440 does **not** toggle the generator.
- [ ] While `FLA` is up: play keys (with the arp on and off), move a pot, press Bank with
      A440 held — everything behaves as usual; the display message interrupts the sequence.
- [ ] Sync **without** A440: stock Osc A sync toggles as before (LED).
- [ ] Press Sync again with A440 held while `FLA` is up: nothing changes; the run completes.
- [ ] Afterwards: load a few programs, save one, power cycle — everything intact (nothing was
      written, this just confirms it).

## If something is wrong

- The engine only runs inside the hooked stock calls. The kill switch (A440 held from before
  power-on) bypasses every hook for the session. If the synth misbehaves otherwise, the
  USB OS-update path still works: re-send stock 2.1.0 over USB exactly as above. The DIN
  bootloader (`btl`, both wheels up at power-on) is only needed if the unit will not boot.
