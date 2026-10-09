# Working notes — state, facts and lessons (keep current)

Written so a fresh session can continue without re-deriving anything. `docs/SPEC.md` is the
behavioural source of truth; this file is the engineering context around it.

## Where things are

- Repo `github.com/dangerous/prophet-rev4-mods` (local folder `~/git/prophet-arp-mods`).
  Work happens in a worktree per feature under `.claude/worktrees/` (`relatch-seq` for the
  2026-10-06/07 work, `poly-seq` for the polyphonic sequencer, `seq2` for the 2.0.0
  independent sequencer; `accompany` is a parked, unmerged experiment); `main` is merged
  from the root checkout with a plain `git merge`.
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
- 2026-10-08, **1.2.0** (image `f2872ab7`, verified the same day): arpeggiated playback of the sequence (`ArP`,
  A440 + Unison; chord length on A440 + Aftertouch id 10: Qtr/Half/Whole/2b/4b in beats
  1/2/4/8/16, the chord clock `chord_acc += bpm` per tick with a boundary at 60000 × beats ×
  dur, or 24 × beats × dur MIDI clocks; each chord restarts the pattern and step clock, a
  step in progress is cut at the boundary), both saved in 94 = oct + 4L + 8C + 40M; two HOLD
  latches (stock's latch byte `ui + 0x19C` holds the active mode's, the UI remembers the other
  and replays a HOLD press at arp on/off transitions when they differ; program load clears
  both memories); leaving record mode with steps switches the arp on.
- First native flash attempt stalled the loader at `100` with the eight Program LEDs lit:
  the payload was an exact multiple of 7 and our encoder omitted the empty tail group's MS
  byte, which the loader always reads. Nothing was written; power cycle recovered. Fixed in
  `tools/syx.py` (always emit the byte) and the spec.
- Rule we hold ourselves to: no engine code at boot; only proven entry points; everything in
  the engine's own record; the kill switch bypasses every hook.

- 2026-10-08, **2.0.0** (`seq2`, image `8d8f99ed`, published): the sequencer redesigned as an
  independent generator (Prophet-6 model). Flashed and run through the 16-step panel test:
  Program 7 from 8th goes to `8d` before `4` (correct — the run-through text was wrong), and
  **HOLD sustains the sequence's chords**: the voice engine's own hold flag decides, the
  per-note "off" answer at the hold query is inert. David: good enough to publish as is.
  The other run-through steps passed by exception.
- 2026-10-09, **2.1.0** (`tempo-pickup`, image `dc8eb3ea`): the tempo knob picks the tempo up
  instead of jumping; flashed and confirmed on the panel, published.
- 2026-10-08, **2.0.2** (`seq-review-fixes`): four review findings in `seq.c` — arming off the
  grid, Continue resetting the arpeggio, a stale pending note value, chord-clock drift —
  each with a regression test; unflashed (MIDI-sync paths and a 137 BPM drift that the panel
  run-through could not reach).
- 2026-10-08, **2.0.1** (`seq-hold-sustain`, image `5c262af7`): the HOLD fix — *suspended*
  += "SEq selected and running" (the DSP hold flag goes off as for the arp), live notes
  sustained in the engine (`arp_set_sustain`), spec 4a rewritten, 28 more harness checks.
  Flashed and confirmed on the panel the same evening (live notes sustain and retrigger,
  the sequence keeps its gates, sustain outlives a stop, pedal the same); merged and
  published. Still not individually confirmed: MIDI-clock transport of the sequencer,
  pedal rest/tie, MIDI-in recording, the 512-step capacity (spec markers say so).

## Build/test

- `make test` = tooling (`unittest`), host harnesses (`test_rate`, `test_oct`, `test_vhold`,
  `test_disp`, `test_arp`, `test_seq`, `test_arpui`), image invariants (cross-build with
  Apple clang `-Oz -fno-jump-tables`, Python ELF linker, `tests/firmware/test_image.py`). No
  third-party packages; pytest is not installed — use `unittest`. The tooling and image
  tests skip (with a warning) when `fixtures/` is empty — a fresh worktree needs the stock
  files copied in from the root checkout or the suite silently tests nothing there.
- `make image` → `build/prophet10_native.syx`. Record `0x20088000–0x20090000`, 16 KB code +
  16 KB state since 2.0.0: code limit `0x2008C000` (10.8 KB used), state above it (SEQ
  `0x2008C000` ≤ 0x3400 — 512 events of 24 bytes —, ARP `0x2008F400` ≤ 0x400, UI
  `0x2008F800` ≤ 0x80, OCT `0x2008F880`, VHOLD `0x2008F920`, queue `0x2008F940`, init flag
  `0x2008FB40`, generated-note-off flag `0x2008FB44`), all zero at boot.
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

## Display font (stock, catalogued 2026-10-08)

Per-digit routine `0x20036A40`, glyph table at `0x2004E218` (8 segments per code); stock's
Globals value-name strings live from `0x2004E290` and use the same codes. Codes: digits
`0`–`9` = 0x00–0x09; letters A–Z = 0x0A–0x23 in order (A 0x0A, B 0x0B, C 0x0C, D 0x0D,
E 0x0E, F 0x0F, G 0x10, H 0x11, I 0x12, J 0x13, K 0x14, L 0x15, M 0x16, N 0x17, O 0x18,
P 0x19, Q 0x1A, R 0x1B, S 0x1C, T 0x1D, U 0x1E, V 0x1F, W 0x20, X 0x21, Y 0x22, Z 0x23 —
M, V, W and X are approximations on seven segments, Z is blank); `o` (lower) 0x24, blank 0x25,
`-` 0x26, `_` 0x27, `]` 0x28. Several letters render lower-case by shape (b d n r t q),
which is why the names read `dn`, `rnd`, `CHd`, `SEq`, `bAC`, `Pnd`. `plat_display_int`
goes through stock's three-digit integer display (zero-padded, `-01` for negatives).

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
- Tempo knob pickup (2.1.0): `arpui_pot_store` records the glide pot's raw value on every
  report (`glide_raw`, 0xFFFF until the first), and while A440 is held sets the BPM only
  once `tempo_caught` — the mapped tempo of the previous and the new raw straddle (or hit)
  the BPM. `tempo_caught` clears at each A440 press and on a tempo tap. The first report
  after power-on is judged on its own (no "from").
- Tap tempo (A440 + Velocity, id 11; Unison until 2026-10-08): `arpui_t.ms` is a
  free-running 1 ms counter bumped in `arpui_tick`; the UI keeps the last tap time and up to
  4 intervals (uint16, ≤ 2000 ms); BPM = round(60000·n / Σ). A gap > 2000 ms starts a new
  series. Readout examples use Osc B Keyboard 36 (Keyboard Amount 8 is the generator switch
  since 2.0.0). `arpui_t` is 56 bytes (area 0x80).
- Display: one timer (`disp.c`) restarted by every message — 1.5 s, or 0.25 s for the
  record-mode flashes (`disp_flash`); stock restore at expiry, `r N` while recording.
- Seq (2.0.0, `seq.c`): a generator of its own. `seq_t` = 512 `seq_ev_t` events (n, 10
  notes, 10 velocities, u16 duration) + recorder + transport. The UI holds the selection
  (`arpui_t.gen`) and routes: record mode → `seq_rec_note` (which sounds through
  `arp_note` with the arp off, so every live note lives in the arp's direct table and
  releases work whoever saw the press); `SEq` → `arp_note` (arp off = live path); `ArP` →
  `arp_note`. Record mode is a UI state (A440 + Tune; a tap, A440 + Tune or GLOBALS leaves,
  leaving `SEq` selected and stopped); HOLD/pedal on-transitions → `seq_rec_rest_tie`; Group
  alone → `seq_rec_back`; `r N` = `seq_rec_count` (0 while `fresh`, i.e. until the first
  entry replaces the old sequence). Chords style: the seq's own step clock (same
  accumulator scheme as the arp's, pending note-value changes applied at the next event);
  an event's `remain` counts its timing steps, the gate fires only when `remain == 1`.
  Arpeggiated style: the seq's chord clock (`60000 × beats × dur` per tick-bpm, or `24 ×
  beats × dur` clocks) hands each chord to the arp's **chord source** (`arp_chord_set`:
  pattern afresh, first note now, steps counted from there; the arp runs its clock whenever
  a chord source is set, arp off or on, and `base_order` takes the chord instead of the
  pool). **Ordering matters**: the sequencer runs before the arp on every tick and every
  accepted clock (`seq_tick` → `arp_tick`, `seq_realtime` → `arp_rt_apply`), and a chord
  set *on* a tick/clock is the arp's step for it (`seq_tick` returns 1 → the glue skips
  `arp_tick`; `arp_rt_apply` skips the step when `chord_clk == 0`), otherwise the old chord
  stepped once more at every boundary (one extra note per chord) or the first step came a
  tick early. Under `Syn`: `armed` (A440 start, waiting for `a->clocks % sc == 0` — the arp's clock
  count is the grid, counted whether or not the sequencer is armed; 2.0.2), `paused` (FC;
  FB resumes, FA → event 1), own `loss` counter; the arp owns the port lock
  (`arp_rt_accept`). `seq_stop` resolves a pending note value; `begin` zeroes the chord
  clock, `next_event` only its clock count so the internal remainder carries (2.0.2). The UI re-applies the arp's own note value in `ui_enable(on)` because
  the Arpeggiated style sets the arp's beats to the seq's.
- Live sustain (spec 4a): *suspended* now includes "SEq selected and running", so the DSP
  hold flag is off while the sequence plays (as for the arp) and generated notes release at
  their gates. Live notes are sustained by the engine: `arp_set_sustain(a, on)` (the UI sets
  it from `seq_runs && !rec` at every transition and in the tick); in the live path a release
  with `sustain_on && hold` only marks `sustained[]`, `arp_hold(0)` releases the set, a
  re-pressed key releases its old note first, `arp_set_sustain(0)` keeps the set (a flush at
  stop would race the hold-on message to the DSP and release the notes instead).
  `plat_voice_off` / `plat_live_off` both map to stock `note_off`; the per-note flag tried
  first (2026-10-08) did nothing on hardware — the DSP sustains any released voice while its
  flag is on.
- Keyboard octave shift (`oct.c`): Lo Freq's held-repeat (panel value 3) is the hold
  detection — it shows the shift and marks the hold used, so only a press released before
  the panel's repeat delay is a tap (replayed to stock). Bank/Group repeats are ignored.
- Assign (`ASS`, mode 4): `arp_t` keeps an entry list (`asg_note`/`asg_vel`, up to 32) in
  note-on order beside the pitch-indexed pool, since the pool arrays cannot hold order or
  duplicates. Note-on appends (also while the arp is off); note-off without HOLD and HOLD
  off remove entries of pitches that left the pool; re-latch and all-notes-off clear it.
  Stepped by index like the sequence; removing an entry before the position moves the
  position back with it, removing the current one sets `asg_stay` so the next step does not
  advance past its successor. A chord source, when set, wins as the base order.
- Timing: internal `acc += bpm*den` per tick, step at `60000*num`, gate at half; MIDI clock
  `24*num/den` per step counted from Start; 1 s loss releases and resets the count.
- Under `Syn` the BPM follows the clock: each accepted F8 samples `loss` (ticks since the
  previous clock) into a 24-entry ring; with a full ring BPM = round(60000 / sum). Start /
  Continue / Stop / loss / clock-source toggle empty the ring. Both tempo gestures (A440 +
  Glide Rate, A440 + Velocity) are consumed but inert under `Syn` (show `Syn`), decided
  2026-10-07 with the clock-follow so the DAW tempo carries over to `int`.
- Note value (`rate.c`): the Prophet-6's ten values in its panel order, index 0 = Half …
  9 = 32nd, so `rate_step(+1)` = shorter = Program 8. The order is not monotonic in average
  length (8S between 8 and 8t, 16S between 16 and 16t) — by design, as on the P6.
- Patch memory: 93 = note code × 10 + mode × 2 + on (0–99; code = Prophet-6 position, or
  0–2 for Whole / 2 bars / 4 bars with the long flag), 94 = octaves + 4 × long (0 = no
  data; 1.2.0 also wrote 8 × chord-length code + 40 × ArP style — read and ignored since
  2.0.0, never written again). Parameter 93's earlier bitfield (2-bit mode, fixed note codes with a legacy table) had no
  room for a fifth mode; David chose (2026-10-07) to re-pack without compatibility, as only
  test programs had been saved. Using program-name character 84 as a flag was considered
  and dropped.

## Lessons

- Panel button ids come from the id readout on the instrument, not from the main state's
  dispatch table (2026-10-09: the table suggested Sync = 0x18; the readout showed 26).

- Hand-encoded instruction bytes in tests were wrong several times: decode with the helpers
  or copy from the disassembly (objdump prints halfwords big-endian; bytes are swapped).
- Subagents given "report only" still wrote into the repo in one case, and a forked session
  edited the same worktree concurrently — both overwrote the spec. Two sessions must not
  share a worktree; check `git diff` before `git add -A`.
- A payload length that is an exact multiple of 7 (tail 0) is a real case; the fixtures never
  had one.
- The panel reports a pre-held button in the held table (no synthetic press event), but its
  link comes up later than our first second.
- The Cortex-A5 build has only the unsigned EABI divide helpers (`__aeabi_uidiv/uidivmod` in
  `native.c`): any signed `/` or `%` by a non-constant (or a divide before a range check that
  lets a negative through) fails to link with `__aeabi_idivmod`. Use unsigned arithmetic
  and compares (`pos + 1 < len ? pos + 1 : 0`).
- `-Oz` emitted a `tbb` jump table whose byte offsets the image test's address sweep read as
  a literal; `-fno-jump-tables` keeps `.text` free of data.
- Chained shell commands with `| grep` / `| tail` after `make test` ran the dist hand-over
  on a failed build: test the exit status (`if make test > log; then …; else exit 1; fi`).
- Harness timing: `on_tick()` values are absolute — reset `now = 0` at every start you time
  (a `play()` helper), or compute relative to the start; three rounds of 2.0.0 failures were
  this.
- Record-mode arithmetic in tests: a chord tied N times is N + 1 timing steps; count from the
  recording, not from memory.

## Open items

- Patch memory (arp settings in program parameters 93/94) verified 2026-10-07; the voice
  engine ignores those slots. The 29 spare record bytes are too small for a sequence; the
  flash map, driver API and two unreferenced areas (`0x511000–0x5FFFFF`, `0x755000–end`)
  are in `docs/re/flash.md` (2026-10-07, from disassembly only).
- The pedal as rest/tie, seq recording from MIDI-in and a tied step under `Syn` have not
  been exercised on hardware.
- **2.0.0 is entirely unverified on hardware** (independent sequencer: generator select,
  transport incl. MIDI arm/Stop/Continue, orders, transposition, Back, 512 steps, two note
  values, per-note hold answer for generated notes). Checklist: `docs/hardware-checklist.md` "Seq". The per-note hold question is answered (the DSP decides; see "Live sustain" above) and the
  engine-side sustain of live notes was confirmed with 2.0.1; what remains unverified is the
  sequencer under MIDI clock (arm / Stop / Continue / loss — fixed blind in 2.0.2), pedal
  rest/tie, MIDI-in recording and the 512-step cap.
- Decisions made while implementing 2.0.0, folded into the spec: selecting `SEq` with
  nothing recorded is refused (`---`, `ArP` stays) rather than selecting an empty generator;
  A440 + Program 6 with the arp running leaves the arp running; a clock-source change
  restarts a running sequence. (A first version re-set the chord on MIDI Continue; that
  contradicted rule 6 and went in 2.0.2 — the arp keeps its chord source and position across
  Stop/Continue by itself.)
- Flash diagnostic (2.2.0) run 2026-10-09: **16 MB, both free areas blank** (`docs/re/flash.md`).
  The top 64 KB hangs a memory-mapped read (excluded).
- **Sequence memory (2.2.0) verified on hardware 2026-10-09** through checklist step 14: the
  first engine flash write, save/reload/power cycle, loads while playing, the switch at the
  next step, explicit "no sequence". Still to do: factory-slot Record, a load during record
  mode, the SysEx staleness case, PRESET off, and the full-dump comparison against the
  pre-flash backup (the "nothing else touched" proof). A SysEx sequence dump is the
  natural next feature (the only backup today is the flash itself).
- Per-program sequence storage (as the Prophet-6): needs its own flash area — `docs/re/flash.md`
  §5 (write through stock's verified writer from the program-store hook, read in the
  program-loaded hook). Gated on the read-only flash diagnostic (spec "Flash diagnostic":
  chip size, blankness of the two free areas); the per-program block size follows from it.
- Possible later features: arp to MIDI Out (for an external synth), gate length, arp
  repeats, chord/trigger mode, probability, variable swing.
