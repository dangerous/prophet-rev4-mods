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

6. Arp on, chord held. **Hold A440 and turn Glide Rate**: the display follows the tempo,
   40–300 BPM. (Glide Rate on its own is still glide, even with the arp running.)
7. **Hold A440 and tap Velocity** in time: `tAP` on the first tap, then the tempo you tapped
   from the second on. Four taps settle it.
8. **A440 + Program 8**: `8S` — eighth-note swing. Again: `8t` (triplets), `16`, `16S`,
   `16t`, `32`. **Program 7** goes the other way: `8`, `8d` (dotted), `4`, `2`, then the long
   ones: `1` (a whole note per step), `2b` (two bars), `4b` (four bars). Set it back to `8`.

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
    LED blinks: you are recording.
14. **Play C, then E, then G**, one at a time. Each sounds as you play it; the display counts
    `r 1`, `r 2`, `r 3`. Timing doesn't matter — every note is one step.
15. **Play a chord** (C, E and G together). `r 4` — notes held together are one step.
16. **Press HOLD with no key down**: `rSt` flashes, then `r 5` — a rest.
17. **Hold a D and press HOLD**: `tiE` flashes, `r 6`. Press HOLD again while still holding D:
    `tiE`, `r 7`. Let go of D. That step is now three steps long. (The sustain pedal does the
    same as HOLD here.)
18. **Tap A440.** The recording is finished; if the arp was off it comes on now.
    (A440 + Program 6 while recording would have cleared it and let you start over; tapping
    A440 without having played anything keeps your previous sequence.)

## 5. Playing the sequence

19. **Hold C.** You hear C, E, G, the chord, a rest, then D held for three steps, round and
    round, at the arp's tempo and note value.
20. **Hold D instead.** The whole sequence moves up a tone: the lowest note of its first
    chord lands on the key you hold. While holding one key, press another: from the next
    step it re-transposes.
21. **A440 + Bank**: `dn` plays the steps backwards, `Ud` back and forth, `rnd` picks steps at
    random — each step keeps its chord and its length whatever the order.
22. **Press HOLD**, let go: it keeps playing. **Play a new key**: the sequence restarts from
    step one on that key.
23. **A440 + Program 6** (not recording): the sequence is gone; the keys arpeggiate as in
    section 1.

## 6. Arpeggiating the sequence

This is the part to spend time on. The sequence becomes a chord progression and the
arpeggiator plays each chord.

24. **A440 + Tune.** Play four chords, one after the other, each held together and released:
    C major, F major, G major, C major. `r 4`. **Tap A440.**
25. **A440 + Unison**: `ArP`. **A440 + Program 8** three times: `16`.
26. **Hold C.** Sixteenths run up C major for a bar, then F major for a bar, then G, then C,
    and round. The chords always come in order; what the arpeggiator does *inside* each
    chord is up to the arp's controls:
27. **A440 + Bank**: `dn` — each chord runs downwards (the progression still goes C F G C).
    `Ud`, `rnd`, `ASS` likewise. **A440 + Program 2**: each chord over two octaves.
28. **A440 + Aftertouch**: `2b` — two bars per chord. Again: `4b`, four bars — pad territory.
    Again: `4` — a chord change every beat, four sixteenths each. Again: `2`, then `1`, back
    to a bar.
29. **Hold a different key**: the progression transposes as in section 5; the chord changes
    stay exactly where they were.
30. Ties and rests work here too: a step you tied once is held for two chord lengths; a rest
    is a chord length of silence.
31. **A440 + Unison**: `Std` — the chords play as blocks again, one per step, as in
    section 5. Both this setting and the chord length are saved with the program.

    Two things to keep in mind in `ArP`: the note value is now the arp's *speed*, so keep it
    at 8ths or 16ths (a `4b` note value would give one note every four bars), and dotted or
    triplet rates don't divide a bar evenly, so the last note before a chord change is cut.

## 6a. Playing over the arp

32. Arp on, hold a chord, **press HOLD**, let go — it keeps playing. **A440 + Keyboard**
    (the filter Keyboard Amount button, bottom row of the filter section): `ACC`.
33. **Play.** Your notes sound as normal notes — chords, velocity, all of it — on top of the
    running arpeggio, which doesn't react to them.
34. **Press the sustain pedal** and play: your notes sustain; release it and they stop. The
    arp's latch is untouched.
35. **Tap A440.** The arp stops; keep playing. **Tap A440** again: the arp resumes with the
    same notes, and you are still playing over it.
36. **A440 + Keyboard** again: the keys are the arp's once more (re-latch and transposition
    as before). Pressing HOLD to drop the latch ends it too.

## 7. Keyboard octave shift

42. Arp off. **Hold Lo Freq** (Osc B) **and press Bank**: `001`, the keyboard is up an octave.
    **Bank** again: `002`. **Group** brings it down; both together resets to `000`. It applies
    to what you play, to the arp and sequencer, and to MIDI Out.
38. **Hold Lo Freq on its own**: after a moment the display shows the current shift, and
    letting go does not change Osc B's Lo Freq setting. A quick tap still toggles it.

## 8. Saving with a program

39. Set the arp up as you like it — on, `Ud`, `o 2`, `16S`, `ArP`, `2b` — and save the program
    as you would any other (Record, then a slot). Load a different program: the arp follows
    that program (off, if it was saved without arp data). Load yours back: everything
    returns. Tempo, clock source, keyboard shift and the sequence itself are global and
    are not saved.

## 9. MIDI sync

40. **A440 + Program 5**: `Syn`. Send MIDI clock from a DAW, press Start: the arp and the
    sequencer run on the DAW's grid — notes on the beat, chord changes on the bar lines —
    and follow Stop and Continue. The tempo gestures show `Syn` (the clock sets the tempo);
    switch back to `int` and the arp carries on at the DAW's tempo.

## 10. Odds and ends

41. **Tuning tone**: with the arp off, **A440 + HOLD** switches Sequential's A440 reference
    tone on (A440 LED lit); A440 + HOLD again, or a tap of A440, switches it off.
42. **Bypass**: hold **A440 while powering on** and the patch stays completely out of the way
    for that session.
43. **Globals**: while the Globals menu is open every button is stock's.
44. **Button ids**: hold A440 and press a button the patch doesn't use — its panel id is
    shown. Handy if you want to add a control of your own.
