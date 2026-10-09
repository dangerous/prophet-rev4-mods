# Walkthrough

A guided tour of the patch, from a fresh install to the arpeggiated sequencer. Each step
says what to press and what you will hear or see; the [README](../README.md) has the
reference tables if you want to look something up afterwards.

**Before you start.** Install the patched OS ([README — Installing](../README.md#installing))
and power up. Pick a bright, short patch so you can hear single notes clearly. The button
labelled **RELEASE / HOLD** is called HOLD below; set the Release/Hold global to hold if it
isn't already. "A440 + X" always means: hold A440 down, press X, let go of both.

## 1. The arpeggiator in one minute

1. **Tap A440.** The display shows `120` for a moment (the tempo) and the A440 LED lights:
   the arpeggiator is on.
2. **Hold a C major chord.** You hear C, E, G, C, E, G… one note at a time, in eighth notes.
   Let go and it stops.
3. **A440 + Bank**, holding the chord: `dn` — now it runs downwards. Bank again: `Ud`
   bounces. Again: `rnd`, random. Again: `ASS` — the notes in the order you pressed them
   (play them as E, C, G and that is the order you hear). Bank once more: back to `UP`.
   **Group** goes the other way round the list.
4. **A440 + Program 2**: `o 2`. The chord plays through, then again an octave up. Program 3
   and 4 add octaves; Program 1 is back to one.
5. **Tap A440.** `OFF`, LED dark, the keys sound as normal again.

## 2. Tempo and note values

6. Arp on, chord held. **Hold A440 and turn Glide Rate**: at first nothing happens — the
   display shows `120`, the tempo to reach — then as the knob passes 120 the tempo follows
   it, 40–300 BPM. The knob picks the tempo up like that at every A440 hold, so it never
   lurches. (Glide Rate on its own is still glide, even with the arp running.)
7. **Hold A440 and tap Velocity** in time: `tAP` on the first tap, then the tempo you tapped
   from the second on. Four taps settle it.
8. **A440 + Program 8**: `8S` — eighth-note swing. Again: `8t` (triplets), `16`, `16S`,
   `16t`, `32`. **Program 7** goes the other way: `8`, `8d` (dotted), `4`, `2`, then the long
   ones: `1` (a whole note per step), `2b` (two bars), `4b` (four bars). Set it back to `8`.
   **Hold A440 and turn Amp Decay**: the gate jumps to the knob and follows it — `5` is a
   click, `100` legato-length notes that end as the next begins. Set it back to `50`.

## 3. HOLD

9. Arp on, chord held. **Press HOLD.** Let go of the keys: it keeps playing.
10. **Play a different chord.** The old one is dropped the moment you play the first new key
    — you don't have to release HOLD between chords.
11. **Press HOLD** again: the latch is off, the next release stops the arp.
12. Latch a chord again, then **tap A440** (arp off). The HOLD LED goes out and nothing
    sustains — the arp's latch is the arp's. **Press HOLD** now and the synth's own hold is
    on, as stock. **Tap A440** (arp on): the LED shows the arp's latch again, which is still
    on from before. Each mode keeps its own HOLD; the LED always shows the one in use.

## 4. Recording a sequence

13. Arp on or off, it doesn't matter. **A440 + Tune.** The display shows `r 0` and the A440
    LED blinks: you are recording. (If the arp was running it has stopped: the sequencer is
    the selected generator now.)
14. **Play C, then E, then G**, one at a time. Each sounds as you play it; the display counts
    `r 1`, `r 2`, `r 3`. Timing doesn't matter — every note is one step.
15. **Play a chord** (C, E and G together). `r 4` — notes held together are one step.
16. **Press HOLD with no key down**: `rSt` flashes, then `r 5` — a rest.
17. **Hold a D**: `r 6`. **Press HOLD** while still holding it: `tiE` flashes, `r 7`. Press
    HOLD again: `tiE`, `r 8`. Let go of D. That step is now three steps long — the note and
    two ties. (The sustain pedal does the same as HOLD here.)
18. Made a mistake? **Press Group** (A440 not held): the last thing you entered is undone — a
    tie first (`r 7`), then the other (`r 6`), then the D itself (`r 5`). Put it back the
    same way as before (`r 8`).
19. **Tap A440.** The recording is finished; the sequencer is selected and waiting.
    (A440 + Program 6 while recording would have cleared it and let you start over; tapping
    A440 without having played anything keeps your previous sequence.)

## 5. Playing the sequence

20. **Tap A440.** You hear C, E, G, the chord, a rest, then D held for three steps, round and
    round, at the tempo and the sequencer's note value (8ths to start with); the A440 LED is
    lit while it runs. **Play along** — the keyboard is yours, HOLD and the pedal sustain what you play (and
    only that), and nothing you play disturbs the sequence.
21. **Hold A440 and press a D**: from the next step the whole sequence is up a tone — middle C
    is "as recorded", any other key is that many semitones away. The key you press is a
    command and doesn't sound. **A440 + middle C** puts it back.
22. **A440 + Bank**: `bAC` plays the steps backwards, `Pnd` back and forth without repeating
    the ends, `For` forward again — each step keeps its chord and its length whatever the
    order.
23. **A440 + Program 7**: `4` — quarters, a steadier pace for chords. This is the sequencer's
    own note value; the arp keeps its own. **Tap A440**: it stops.
24. **A440 + Keyboard** (the filter's Keyboard Amount button): `ArP` — the arpeggiator is
    selected again; tap A440 and the keys arpeggiate as in section 1, the sequence waiting
    untouched. **A440 + Keyboard**: `SEq`, back to the sequencer. (A440 + **Program 6**, not
    recording, clears the sequence instead and selects the arp.)

## 6. Arpeggiating the sequence

This is the part to spend time on. The sequence becomes a chord progression and the
arpeggiator plays each chord.

25. **A440 + Tune.** Play four chords, one after the other, each held together and released:
    C major, F major, G major, C major. `r 4`. **Tap A440.**
26. **A440 + Unison**: `ArP`. **A440 + Program 8** until the display says `16` — the
    sequencer's note value is the arpeggio's rate here.
27. **Tap A440.** Sixteenths run up C major for a bar, then F major for a bar, then G, then C,
    and round. The chords always come in order; what the arpeggiator does *inside* each
    chord is up to the arp's controls:
28. **A440 + Bank**: `dn` — each chord runs downwards (the progression still goes C F G C).
    `Ud`, `rnd`, `ASS` likewise. **A440 + Program 2**: each chord over two octaves.
29. **A440 + Aftertouch**: `2b` — two bars per chord. Again: `4b`, four bars — pad territory.
    Again: `4` — a chord change every beat, four sixteenths each. Again: `2`, then `1`, back
    to a bar.
30. **A440 + a key**: the progression transposes as in section 5, from the next chord; the
    chord changes stay exactly where they were.
31. Ties and rests work here too: a step you tied once is held for two chord lengths; a rest
    is a chord length of silence.
32. **A440 + Unison**: `CHd` — the chords play as blocks again, one per step, as in
    section 5. The style, chord length, order, note value and transposition live with the
    recording — and are saved with it when you Record a user program (section 8).

## 7. Keyboard octave shift

33. Arp off. **Hold Lo Freq** (Osc B) **and press Bank**: `001`, the keyboard is up an octave.
    **Bank** again: `002`. **Group** brings it down; both together resets to `000`. It applies
    to what you play, to the arp and the recording, and to MIDI Out.
34. **Hold Lo Freq on its own**: after a moment the display shows the current shift, and
    letting go does not change Osc B's Lo Freq setting. A quick tap still toggles it.

## 8. Saving with a program

35. Set the arp up as you like it — on, `Ud`, `o 2`, `16S` — and save the program as you
    would any other (Record, then a slot). Load a different program: the arp follows that
    program (off, if it was saved without arp data). Load yours back: everything returns.
36. **The sequence goes with it.** With a recording in place, Record a *user* program: the
    sequence, its style, order, note value, chord length, transposition and the generator
    selection are stored in the synth's flash with it. Load a factory program: your sequence
    keeps playing, untouched. Load your program back: it is there again — power the synth
    off and on, still there. Start it, then load another program that has its own saved
    sequence: it switches over at the next step without missing a beat. A program saved
    with nothing recorded brings no sequence and changes nothing. Tempo, clock source and
    keyboard shift remain global and are not saved.

## 9. MIDI sync

36. **A440 + Program 5**: `Syn`. Send MIDI clock from a DAW, press Start: the arp runs on
    the DAW's grid — notes on the beat — and follows Stop and Continue. With the sequencer
    selected, a tap of A440 *arms* it: it starts on the next step of the grid, chord changes
    on the bar lines; the DAW's Stop pauses it where it is, Continue carries on, Start takes
    it back to the first step, and a tap of A440 stops it for good. The tempo gestures show
    `Syn` (the clock sets the tempo); switch back to `int` and the arp carries on at the
    DAW's tempo.

## 10. Odds and ends

37. **Tuning tone**: with the selected generator stopped, **A440 + HOLD** switches
    Sequential's A440 reference tone on (A440 LED lit); A440 + HOLD again, or a tap of A440,
    switches it off.
38. **Bypass**: hold **A440 while powering on** and the patch stays completely out of the way
    for that session.
39. **Globals**: while the Globals menu is open every button is stock's.
40. **Button ids**: hold A440 and press a button the patch doesn't use — its panel id is
    shown. Handy if you want to add a control of your own.
41. **Flash diagnostic**: hold A440 and press **Sync** — a number climbs while the flash is
    read, then `F16`, `1 E`, `2 U` (chip size; the two spare areas, the second holding your
    saved sequences). A440 + Sync again recalls them one at a time. Nothing is written.
