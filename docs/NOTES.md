# Working notes — state, facts and lessons (keep current)

Written so a fresh session can continue without re-deriving anything. `docs/SPEC.md` is the
behavioural source of truth; this file is the engineering context around it.

## Where things are

- Repo `github.com/dangerous/prophet-rev4-mods` (local folder `~/git/prophet-arp-mods`).
  All work happens in the worktree `.claude/worktrees/relatch-seq` (branch `relatch-seq`);
  `main` is merged from the root checkout with a plain `git merge`.
- `dist/` holds the installable image + `SHA256SUMS` (written from inside `dist/`, so verify
  with `cd dist && shasum -a 256 -c SHA256SUMS`). **Never delete dist files**; superseded
  builds go to `dist/old/` with a hash suffix. The `.syx` files are git-ignored and therefore
  per checkout: copy new builds from the worktree's `dist/` to the root's `dist/` (David
  flashes from the root) — a stale root `dist/` nearly got flashed on 2026-10-07.
- Original inputs in `~/git/prophet` (stock 2.1.0, Panel 1.1.3, the V5 arp mod and its
  guide, an 800 MB Overview.MOV); the two files the tests need are copied to `fixtures/`
  (git-ignored — the repo ships only `fixtures/README.md` + `SHA256SUMS`).
- Scratchpad (session-only, may be gone): decoded images and full disassembly listings
  (`orig.asm` = stock); regenerate with `python3 -m tools unpack` + the ELF wrapper trick
  (write a minimal ELF around a flat binary and run Apple's
  `/usr/bin/objdump -d --triple=thumbv7a-none-eabi`). Reverse-engineering reports on stock
  are kept in `docs/re/`.

## Hardware status (David's Prophet-10 Rev4, no DIN cable → bootloader recovery unavailable)

- 2026-10-06/07, wrapper era (hooks chained in front of the third-party V5 arp): re-latch,
  seq, note values, octave shift, HOLD suspension and display revert were all verified on
  hardware before the engine replaced V5.
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
  2026-10-07. Pending: A440 + Unison tap tempo (added 2026-10-07, unflashed), the
  legacy-code mapping on load (programs saved at 16d / 4d / 1 / 2b / 4b), and swing
  under `Syn`.
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
  `0x2008E000`, state above it (ARP `0x2008E000`, UI `0x2008E400`, OCT `0x2008E440`, VHOLD
  `0x2008E4E0`, queue `0x2008E500`, init flag `0x2008E700`), all zero at boot.
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
  hold message while enabled; re-post it on enable/disable transitions (`vhold.c`).
- Tap tempo (A440 + Unison, id 25): `arpui_t.ms` is a free-running 1 ms counter bumped in
  `arpui_tick`; the UI keeps the last tap time and up to 4 intervals (uint16, ≤ 2000 ms);
  BPM = round(60000·n / Σ). A gap > 2000 ms starts a new series. Unison is no longer a
  readout button (readout examples now use Osc B Keyboard 36). `arpui_t` stays ≤ 0x40.
- Display: one 1.5 s timer (`disp.c`) restarted by every message; stock restore at expiry.
- Seq: recording routes notes directly to voices and appends; playback substitutes the
  recorded steps (transposed onto the trigger key) for the pitch-sorted pool as the pattern's
  base order; pitch-anchored stepping for the pool, index-anchored for the sequence.
- Timing: internal `acc += bpm*den` per tick, step at `60000*num`, gate at half; MIDI clock
  `24*num/den` per step counted from Start; 1 s loss releases and resets the count.
- Note value (`rate.c`): the Prophet-6's ten values in its panel order, index 0 = Half …
  9 = 32nd, so `rate_step(+1)` = shorter = Program 8. The order is not monotonic in average
  length (8S between 8 and 8t, 16S between 16 and 16t) — by design, as on the P6. Patch codes
  are fixed per value (never the index); codes 4, 8, 10–12 of removed values are read-only
  and map to the nearest remaining value.

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
- Seq recording from MIDI-in not yet exercised on hardware.
- Possible later features: arp to MIDI Out (for an external synth), rests in seq.
