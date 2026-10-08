# Working notes — state, facts and lessons (keep current)

Written so a fresh session can continue without re-deriving anything. `docs/SPEC.md` is the
behavioural source of truth; this file is the engineering context around it.

## Where things are

- Repo `github.com/dangerous/prophet-rev4-mods` (local folder `~/git/prophet-arp-mods`).
  Work happens in a worktree per feature under `.claude/worktrees/` (`relatch-seq` for the
  2026-10-06/07 work, `poly-seq` for the polyphonic sequencer); `main` is merged from the
  root checkout with a plain `git merge`.
- The release version is the `VERSION` file (semantic versioning, bumped by hand); `make
  manifest` writes it into `site/manifest.js` and `site/version.json` (the README badge reads
  the latter) and names the download `prophet5_main_2.1.0_patched_<version>.syx`. Release
  steps: `docs/DEVELOPING.md`.
- `dist/` holds the installable image + `SHA256SUMS` (written from inside `dist/`, so verify
  with `cd dist && shasum -a 256 -c SHA256SUMS`). **Never delete dist files**; superseded
  builds go to `dist/old/` with a hash suffix. The `.syx` files are git-ignored and therefore
  per checkout: copy new builds from the worktree's `dist/` to the root's `dist/` (David
  flashes from the root) — a stale root `dist/` nearly got flashed on 2026-10-07.
- Original inputs in `~/git/prophet` (stock 2.1.0, Panel 1.1.3, the Arp Mod and its
  guide, an 800 MB Overview.MOV); the two files the tests need are copied to `fixtures/`
  (git-ignored — the repo ships only `fixtures/README.md` + `SHA256SUMS`).
- Scratchpad (session-only, may be gone): decoded images and full disassembly listings
  (`orig.asm` = stock); regenerate with `python3 -m tools unpack` + the ELF wrapper trick
  (write a minimal ELF around a flat binary and run Apple's
  `/usr/bin/objdump -d --triple=thumbv7a-none-eabi`). Reverse-engineering reports on stock
  are kept in `docs/re/`.

## Hardware status (David's Prophet-10 Rev4, no DIN cable → bootloader recovery unavailable)

- 2026-10-06/07, wrapper era (hooks chained in front of the third-party Arp Mod): re-latch,
  seq, note values, octave shift, HOLD suspension and display revert were all verified on
  hardware before the engine replaced the Arp Mod.
- 2026-10-07, **engine** (`prophet10_native.syx`, stock 2.1.0 base): first flash passed the
  whole checklist (boot, A440, display revert, pattern incl. per-pass octaves, HOLD, seq,
  Glide tempo, note values, Syn, Lo Freq shift incl. MIDI Out, CC 123, program change,
  Globals); kill switch verified with a 3 s table window. A press-event trigger was added
  and then dropped (a normal tap soon after power-on killed the arp); the table check now
  excludes an A440 that was reported pressed — verified both ways 2026-10-07 (so the panel
  sends no synthetic press for a pre-held button). Program 7/8 direction swap (8 =
  shorter/+) verified the same day. A440 + Glide Rate tempo (Glide Rate alone = glide with
  the arp running) and the `16S` / `8S` swing values (internal clock) verified 2026-10-07.
  The Prophet-6 ten-value note-value list and order (Program 7/8 walk, display) verified
  2026-10-07. Tap tempo (A440 + Unison), Assign mode, the arithmetic packing of parameter
  93, BPM following the MIDI clock under `Syn` (gestures inert there) and swing under
  `Syn` all verified 2026-10-07 in one pass.
- 2026-10-08, **polyphonic sequencer** (record mode A440 + Tune, chord steps, rests and
  ties via HOLD, 64 steps, `r N` readout, blinking A440 LED): image `250ec97c` flashed and
  verified the same day ("it all works"); the relaid state area (ARP ≤ 0xA00) boots fine.
  Not exercised: the pedal as rest/tie, seq recording from MIDI-in, a tied step under `Syn`.
- 2026-10-08, **follow-ups** after that pass: the record readout counts the length in arp
  steps (every gesture +1; `rSt` / `tiE` flash, then the count) and holding Lo Freq alone
  shows the current shift from the panel's first held-repeat, such a hold no longer
  toggling Lo Freq on release — both verified 2026-10-08 (image `ce3c737e`, 0.5 s flash).
  The flash was then shortened to 0.25 s (image `7cfe0b8f`, verified the same day — released
  as 1.0.0 through the browser patcher). The
  shift readout stays the stock zero-padded integer (`001`, `-01`) by David's choice: it
  matches stock's own signed readouts (pitch-bend range).
- 2026-10-08, **tuning tone** (1.1.0): A440 pressed with the
  Globals menu open turned out to do nothing in stock (the notes had assumed the main-state
  handler would be reached), so the tone has its own combo, A440 + HOLD with the arp off,
  realised by replaying a stock A440 press on HOLD's release (HOLD up → stock's DSP-tone
  path, not its HOLD-held voice note); enabling the arp clears stock's tone flag
  (`ui + 0x16A`) the same way first. New stock-interface entry for the flag. First build
  verified for the tone itself; a tap with the tone on then enabled the arp with the LED
  dark — the replayed press is handled by stock a few ms later and its LED-off landed after
  ours. Now a tap while the tone sounds only stops the tone, and the arp LED is asserted
  again 100 ms after any replay if the arp is on.
- 2026-10-08, **long note values and tap tempo on Velocity** (1.1.0, image `7468c036`, verified
  2026-10-08): 4 bars / 2 bars / Whole (`4b` `2b` `1`) above the Prophet-6 list — 64 steps of 4
  bars = 256 bars for pad sequences; patch memory flags them with `94 = octaves + 4` (93's
  note digit then 0–2) so programs saved before load unchanged. Tap tempo moved from Unison
  (id 25, two hands) to Velocity (id 11, beside A440), freeing Unison for a future
  chord/arpeggiate switch; Unison under A440 is a readout again. The bigger `combo()` switch
  made clang emit a `tbb` jump table in .text, which the image test's address sweep misread
  as a literal load; the engine is now built with `-fno-jump-tables`.
- First native flash attempt stalled the loader at `100` with the eight Program LEDs lit:
  the payload was an exact multiple of 7 and our encoder omitted the empty tail group's MS
  byte, which the loader always reads. Nothing was written; power cycle recovered. Fixed in
  `tools/syx.py` (always emit the byte) and the spec.
- Rule we hold ourselves to: no engine code at boot; only proven entry points; everything in
  the engine's own record; the kill switch bypasses every hook.

## Build/test

- `make test` = tooling (`unittest`), host harnesses (`test_rate`, `test_oct`, `test_vhold`,
  `test_disp`, `test_arp`, `test_arpui`), image invariants (cross-build with Apple clang
  `-Oz`, Python ELF linker, `tests/firmware/test_image.py`). No third-party packages; pytest
  is not installed — use `unittest`.
- `make image` → `build/prophet10_native.syx`. Record `0x20088000–0x20090000`: code limit
  `0x2008E000`, state above it (ARP `0x2008E000` ≤ 0xA00 — the 64-step chord storage —, UI
  `0x2008EA00`, OCT `0x2008EA40`, VHOLD `0x2008EAE0`, queue `0x2008EB00`, init flag
  `0x2008ED00`), all zero at boot.
- Sandbox quirk: compound Bash with heredocs in the worktree is sometimes refused as "too
  complex"; write files with the Write tool (or via the scratchpad + `cp`) and keep Bash
  lines simple. zsh aborts a whole command line on an unmatched glob (`--include=*.py`).
- Don't mask `make test` with `| grep` in a chain; check its exit status.

## Platform facts (ADSP-SC5xx: Cortex-A5 + SHARC+)

- Main OS image A: Thumb-2, loaded at `0x2002E000`, entry `0x2002E001`; stock code record
  `0x2002EF00` (0x1D118). Image B: SHARC (`0xAC` family), untouched. No hardware divide:
  `native.c` provides `__aeabi_uidiv`/`__aeabi_uidivmod`; avoid 64-bit shifts (`__aeabi_llsl`).
- MMU table in stock `.data` `0x2004DD2C…`: `0x20020000–0x2008FFFF` one RAM region (attrs
  0x5C04), `0x20090000–0x200FFFFF` (0xDC04). Stock uses ≤ `0x200874CC`.
- OS = FreeRTOS + QP/C active object. Hooked sites run in the Timer Service task (keys/tick
  1 ms, panel buttons/pots 6 ms, MIDI byte parser 1 ms) or the Prophet5 AO task (MIDI
  notes, CC, hold); nothing hooked runs in an ISR. The engine mutates state only from the
  tick; AO-side hooks hand events over through a 64-entry single-producer ring.
- Button events `(id, value)`: 1 press, 2 release, **3 held-repeat** (gate `0x20036114`
  passes 1–3). Ids: Program 1–8 = 0–7, TUNE = 0x0C, GLOBALS = 0x0D (13), HOLD = 0x0E,
  A440 = 0x0F, UNISON = 0x19 (25), Group = 0x20, Bank = 0x28, Osc B Keyboard (key follow;
  has stock Group/Bank combos) = 36, **Osc B Lo Freq = 37 — the octave-shift modifier**,
  filter Keyboard Amount = 8. LED ids ≠ button ids (table `0x2004E496`): A440 LED 0x24,
  HOLD 0x23; LED setter `0x20036824(led, 0/1/2)`. Button-held table `0x20079B20[id]` (4
  bytes each, u16) — A440 at `0x20079B5C`; the panel link comes up >1 s after our first tick.
- Display: `0x20037F25(c0,c1,c2)` 3 chars; `0x20037FF7(int)` integer (negatives shown);
  patch display restore `0x2003818C(ui)`, `ui = 0x20057390`. Codes: digits 0–9 = 0–9,
  A=0x0A b=0x0B d=0x0D E=0x0E F=0x0F i=0x12 L=0x15 n=0x17 O=0x18 P=0x19 r=0x1B S=0x1C
  t=0x1D U=0x1E y=0x22 o=0x24 blank=0x25 '-'=0x26. Globals menu open ⇔ word
  `0x20057438 != 0`.
- Keyboard FIFO consumer `0x2003BE8C…` (1 ms timer callback): `fifo_count` at `0x2003BE9C`
  (our tick), `note_on(1,note,vel)` at `0x2003BECC` only with Local Control (global 7) == 2;
  `bl 0x2003BCE0(note, vel)` at `0x2003BED8` posts a UI event nobody handles, NOT MIDI; MIDI
  Out of the key happens for local on and off at `0x2003BEFA` (`0x20033F84` note-on) /
  `0x2003BF16` (`0x20033F38` note-off), args `(cable, channel, note, vel)`.
- Voice API: `note_on 0x2003EC5C(src, note, vel)` (vel 0 → note_off), `note_off
  0x2003E95C(src, note)` (`0x2003EEDC` is a thunk to it), `all_notes_off 0x2003EBE4`; src 1
  local / 2 MIDI is bookkeeping only.
- Hold: `hold_set 0x20039688(state, source)` merges button (`ui+0x19c`) and pedal
  (`ui+0x19d`), posts `0x080D0000|state` to the voice engine via `0x2003D324` (hooked at
  `0x200396CA`, r4 = merged state), calls `0x2003EEE0` when going off, LED 0x23.
  `0x2003B694()` returns the merged state; **stock `note_off` asks it at `0x2003EACE` and,
  when on, hands the voice to the voice engine's sustain instead of releasing** — hence
  arp steps pile up under HOLD unless that query is hooked (we answer 0 while the arp is on
  and withhold/re-post the hold message).
- Pots: panel decoder `0x2003C204` (6 ms Panel Timer): raw 10-bit store `0x20036B50(pot,
  raw)` at `0x2003C292`, change post `0x2003BC6C(pot, old, new)` at `0x2003C2A6`; pot 0x16
  = Glide Rate (→ program parameter 13). Button post `0x2003BC30(id, value)` at `0x2003C244`.
- MIDI: parser status table at `0x200343B8` (index = status − 0xF0); F8/FA/FB/FC entries
  (`0x200343D8/E0/E4/E8`) point at the loop head `0x20034343` — stock discards realtime, so
  our trampoline is additive. Note-on/off hooks `0x2003B07A`/`0x2003B032` (AO task); CC 123
  at `0x2003B294`, CC 124–127 at `0x2003B144` (both → `0x2003EBE4`). Program load
  `0x200384C4` switches HOLD off (via `0x2003B6B0`). Full detail: `docs/re/stock-*.md`.
- OS-update loader (`0x20032744…`): reads the header group, then the full groups in chunks
  (progress 0→100), then **always** one tail-group MS byte, `tail` data bytes and two
  trailer bytes; verifies the trailer before writing.

## Engine design notes

- Hooks: 14 `BL` sites + 4 parser-table words (`firmware/hooks_native.json`); every stock
  address the code touches is in `stock_iface[]` (`native.c`), compared word for word by
  `test_image.py`; the code may reference nothing else outside its record.
- Lazy init on the first hook (under `cpsid i`); kill switch checked in the tick before any
  engine code (button-held table during the first 3 s, unless an A440 press event was seen);
  `UI->kill` makes every hook fall through to stock.
- HOLD while the arp is on: hook the hold query in `note_off`; withhold the voice-engine
  hold message while *suspended* (arp enabled or seq record mode, `arpui_suspended`);
  re-post it on transitions of that state (`vhold.c`).
- Tap tempo (A440 + Unison, id 25): `arpui_t.ms` is a free-running 1 ms counter bumped in
  `arpui_tick`; the UI keeps the last tap time and up to 4 intervals (uint16, ≤ 2000 ms);
  BPM = round(60000·n / Σ). A gap > 2000 ms starts a new series. Unison is no longer a
  readout button (readout examples now use Osc B Keyboard 36). `arpui_t` stays ≤ 0x40.
- Display: one timer (`disp.c`) restarted by every message — 1.5 s, or 0.25 s for the
  record-mode flashes (`disp_flash`); stock restore at expiry, `r N` while recording.
- Seq: record mode is a UI state (`arpui_t.rec`, A440 + Tune; a tap of A440, A440 + Tune
  or GLOBALS leaves). While it lasts the UI routes notes to `arp_seq_record_note`, consumes
  the HOLD button (id 0x0E) and turns its presses — and the pedal's on-transitions, which
  reach the UI through `arpui_hold` — into `arp_seq_rest_tie`; the display timer restores
  `r N` instead of the patch display; the A440 LED blinks (500/500 ms). Engine storage: a
  step is `seq_n` notes (`seq_note`/`seq_vel`, up to 10) and a length `seq_dur`; the open
  step is "a recorded key is still down" (`rec_down` bitmap). Playback substitutes the step
  list for the pitch-sorted pool as the base order (the walker runs over step indices;
  `seq_hold` counts the arp steps a step still has to run, the gate fires only in its last
  one); the reference pitch is the lowest note of the first sounding step. Entering/leaving
  releases everything the engine has sounding and empties the pool, so keys down at the
  transition are ignored until pressed again; `arp_enable` while recording only sets the
  flag. Pitch-anchored stepping for the pool, index-anchored for the sequence.
- Keyboard octave shift (`oct.c`): Lo Freq's held-repeat (panel value 3) is the hold
  detection — it shows the shift and marks the hold used, so only a press released before
  the panel's repeat delay is a tap (replayed to stock). Bank/Group repeats are ignored.
- Assign (`ASS`, mode 4): `arp_t` keeps an entry list (`asg_note`/`asg_vel`, up to 32) in
  note-on order beside the pitch-indexed pool, since the pool arrays cannot hold order or
  duplicates. Note-on appends (also while the arp is off); note-off without HOLD and HOLD
  off remove entries of pitches that left the pool; re-latch and all-notes-off clear it.
  Stepped by index like the sequence; removing an entry before the position moves the
  position back with it, removing the current one sets `asg_stay` so the next step does not
  advance past its successor. A sequence, when present, still wins as the base order.
- Timing: internal `acc += bpm*den` per tick, step at `60000*num`, gate at half; MIDI clock
  `24*num/den` per step counted from Start; 1 s loss releases and resets the count.
- Under `Syn` the BPM follows the clock: each accepted F8 samples `loss` (ticks since the
  previous clock) into a 24-entry ring; with a full ring BPM = round(60000 / sum). Start /
  Continue / Stop / loss / clock-source toggle empty the ring. Both tempo gestures (A440 +
  Glide Rate, A440 + Unison) are consumed but inert under `Syn` (show `Syn`), decided
  2026-10-07 with the clock-follow so the DAW tempo carries over to `int`.
- Note value (`rate.c`): the Prophet-6's ten values in its panel order, index 0 = Half …
  9 = 32nd, so `rate_step(+1)` = shorter = Program 8. The order is not monotonic in average
  length (8S between 8 and 8t, 16S between 16 and 16t) — by design, as on the P6.
- Patch memory: 93 = note index × 10 + mode × 2 + on (0–99), 94 = octaves (0 = no data).
  Parameter 93's earlier bitfield (2-bit mode, fixed note codes with a legacy table) had no
  room for a fifth mode; David chose (2026-10-07) to re-pack without compatibility, as only
  test programs had been saved. Using program-name character 84 as a flag was considered
  and dropped.

## Lessons

- Hand-encoded instruction bytes in tests were wrong several times: decode with the helpers
  or copy from the disassembly (objdump prints halfwords big-endian; bytes are swapped).
- Subagents given "report only" still wrote into the repo in one case, and a forked session
  edited the same worktree concurrently — both overwrote the spec. Two sessions must not
  share a worktree; check `git diff` before `git add -A`.
- A payload length that is an exact multiple of 7 (tail 0) is a real case; the fixtures never
  had one.
- The panel reports a pre-held button in the held table (no synthetic press event), but its
  link comes up later than our first second.

## Open items

- Patch memory (arp settings in program parameters 93/94) verified 2026-10-07; the voice
  engine ignores those slots. Possible follow-up: the sequence in the 29 spare
  bytes of the flash record (pitches only, ~28 steps, not in SysEx dumps; hooks on the flash
  serialiser/deserialiser — a 4 KB sector holds 32 programs, so backup first).
- The pedal as rest/tie, seq recording from MIDI-in and a tied step under `Syn` have not
  been exercised on hardware.
- Per-program sequence storage (as the Prophet-6): the 29 spare flash bytes above would
  hold a pitch-only mono sequence, not 64 chord steps with velocity; a SysEx dump/restore of
  the sequence is the realistic alternative.
- Possible later features: arp to MIDI Out (for an external synth), gate length, arp
  repeats, chord/trigger mode, probability, variable swing.
