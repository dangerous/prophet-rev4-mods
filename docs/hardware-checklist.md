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
- [ ] `sha256sum dist/prophet10_v5_relatch.syx` matches `dist/SHA256SUMS`.
- [ ] Globals → Program 6 (MIDI SysEx) set to `USB`; SysEx Librarian output = the Prophet;
      no other MIDI apps running, nothing else routed to the Prophet.

## Installing

1. Send `dist/prophet10_v5_relatch.syx`. The display counts `000`→`100` (transfer), then
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
- [ ] A440 + Program 2: display `2`; the pattern plays C E G E then an octave up.
      A440 + Program 1 back to one octave.
- [ ] Glide Rate still sets tempo; `Syn` follows DAW clock; start/stop as with the arp.
- [ ] A440 + Program 6: sequence gone; keys play the normal arp again; octave setting back
      to what it was before recording (A440 + Program 1–4 shows `o N` as in V5).
- [ ] Hold A440, play 3 notes, release: a new sequence replaces the old one.
- [ ] Arp off, sequence exists: keys play normally (no sequence). Tap A440: sequence plays.
- [ ] Record from MIDI-in (DAW notes) while holding A440; trigger from the keyboard.

## If something is wrong

- The wrapper only runs inside the arp's hook paths. If a hook misbehaves, the synth still
  boots and the USB OS-update path still works: re-send V5 (or stock 2.1.0) over USB
  exactly as above. The DIN bootloader (`btl`) is only needed if the unit will not boot.
