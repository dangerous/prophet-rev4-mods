# Working notes — state, facts and lessons (keep current)

Written so a fresh session can continue without re-deriving anything. `docs/SPEC.md` is the
behavioural source of truth; this file is the engineering context around it.

## Where things are

- Repo `github.com/dangerous/prophet-rev4-mods` (local folder `~/git/prophet-arp-mods`), branch `main` fast-forwarded from the worktree branch
  `relatch-seq` at `.claude/worktrees/relatch-seq` (all work happens there; `main` is merged
  with plain `git merge` from the root). An agent worktree
  `.claude/worktrees/agent-a8014a76fdbbe9479` holds the already-integrated octave fix; safe
  to remove.
- `dist/` holds the installable images + `SHA256SUMS` (written from inside `dist/`, so
  verify with `cd dist && shasum -a 256 -c SHA256SUMS`). **Never delete dist files.** The
  `.syx` files are git-ignored and therefore per checkout: copy new builds from the
  worktree's `dist/` to the root's `dist/` (David flashes from the root) — 2026-10-07 a
  stale root `dist/` nearly got flashed.
- Original inputs in `~/git/prophet` (stock 2.1.0, Panel 1.1.3, V5 arp mod, its PDF guide,
  an 800 MB Overview.MOV); copies of the `.syx`/guide in `fixtures/` (git-ignored — the
  repo ships only `fixtures/README.md` + `SHA256SUMS`; history was rewritten to drop them).
- Scratchpad (session-only, may be gone): decoded images and full disassembly listings
  (`orig.asm`, `hack.asm`, `blob.asm`); regenerate with `python3 -m tools unpack` + the ELF
  wrapper trick (write a minimal ELF around a flat binary and run Apple's
  `/usr/bin/objdump -d --triple=thumbv7a-none-eabi`).

## Hardware status (David's Prophet-10 Rev4, no DIN cable → bootloader recovery unavailable)

- Flashed so far: re-latch-only (bug: hold flag), fixed re-latch (OK), relatch+seq
  (bugs: button repeat, seq octave). Built but **not installed**: the 2026-10-07 image
  (seq octave fix, note values, readout, octave shift on Lo Freq, MIDI Out hook fix, HOLD
  suspension while the arp is on, display revert) — **installed and verified 2026-10-07**;
  `dist/` holds it, the superseded builds are kept in `dist/old/`. Next flash: the native
  image.
- Verified (2026-10-06/07): loader path, boot, hooks, HOLD stub, re-latch, seq
  record/play/clear/octaves, note values (internal clock), Lo Freq octave shift incl. MIDI
  Out, HOLD suspended while the arp is on, display revert, readout (Keyboard 36, GLOBALS 13).
  V5's pitch-sorted octave union (C4 C5 G5 G6) observed as expected; per-pass is native-only.
- Rule we hold ourselves to: no wrapper code at boot; only proven entry points; nothing in
  the USB re-flash path unless unavoidable (the full image's parser-table trampoline is the
  one exception, deliberately NOT installed yet — the `_internal` variant omits it).

## Build/test

- `make test` = tooling (`unittest`), host harnesses (`test_relatch`, `test_seq`,
  `test_rate`), image invariants (cross-build with Apple clang `-Oz`, Python ELF linker).
  No third-party packages; pytest is not installed — use `unittest`.
- `make image` (full) / `make image-internal` (parser table untouched). Code limit
  `0x2008A000..0x2008B800`, state `0x2008B800..0x2008C000`.
- Sandbox quirk: compound Bash with heredocs in the worktree gets refused as "too complex";
  write files with the Write tool and keep Bash lines simple. `cd` to the scratchpad resets
  the session cwd; the worktree/root flip changes whether git ops are allowed — merge
  `main` from the root checkout (`git merge relatch-seq` there).
- Don't mask `make test` with `| grep` in a chain; check its exit status.

## Platform facts (ADSP-SC5xx: Cortex-A5 + SHARC+)

- Main OS image A: Thumb-2, loaded at `0x2002E000`, entry `0x2002E001`; stock code record
  `0x2002EF00` (0x1D118). Image B: SHARC (`0xAC` family), untouched.
- MMU table in stock `.data` `0x2004DD2C…`: `0x20020000–0x2008FFFF` one RAM region (attrs
  0x5C04), `0x20090000–0x200FFFFF` (0xDC04). Stock uses ≤ `0x200874CC`; V5 blob
  `0x20088000–0x2008A000`; wrapper record `0x2008A000–0x2008C000`.
- Button events `(id, value)`: 1 press, 2 release, **3 held-repeat** (gate `0x20036114`
  passes 1–3). Known ids: Program 1–8 = 0–7, A440 = 0x0F, TUNE = 0x0C, GLOBALS = 0x0D,
  HOLD = 0x0E, UNISON = 0x19 (V5 tracks this one as its "globals" flag), Group = 0x20,
  Bank = 0x28, **Osc B Keyboard (key follow; has stock Group/Bank combos) = 36 (0x24)**,
  **Osc B Lo Freq = 37 (0x25) — the octave-shift modifier since 2026-10-07**, filter
  Keyboard Amount = 8 (the modifier before that). LED ids ≠ button ids (table
  `0x2004E496`): A440 LED 0x24, HOLD 0x23; LED setter `0x20036824(led, 0/1/2)`. The P10's
  panel is the same as the P5's — no extra buttons.
- Display: `0x20037F25(c0,c1,c2)` 3 chars; `0x20037FF7(int)` integer (negatives shown).
  Codes: digits 0–9 = 0–9, A=0x0A b=0x0B d=0x0D E=0x0E F=0x0F i=0x12 L=0x15 n=0x17 O=0x18
  P=0x19 r=0x1B S=0x1C t=0x1D U=0x1E y=0x22 o=0x24 blank=0x25 '-'=0x26.
- Stock keyboard FIFO consumer `0x2003BE8C…` (1 ms timer callback): `note_on(1,note,vel)`
  at `0x2003BECC` (hooked) only with local control on; `bl 0x2003BCE0(note, vel)` at
  `0x2003BED8` posts a UI event (sig 8), NOT MIDI; MIDI Out of the key happens for local
  on and off at `0x2003BEFA` (`bl 0x20033F84` note-on) / `0x2003BF16` (`bl 0x20033F38`
  note-off), args `(cable, channel, note, vel)`. Full stock API notes: `docs/re/`.
- Stock hold: `hold_set 0x20039688(state, source)` merges button (`ui+0x19c`) and pedal
  (`ui+0x19d`, `ui = 0x20057390`), posts `0x080D0000|state` to the voice engine via
  `0x2003D324` (hooked at `0x200396CA`, r4 = merged state), calls `0x2003EEE0` when going
  off, LED 0x23. `0x2003B694()` returns the merged state; **stock `note_off` asks it at
  `0x2003EACE` and, when on, hands the voice to the voice engine's sustain instead of
  releasing** — hence arp steps piled up under HOLD in V5. The wrapper hooks that query
  (answers 0 while the arp is on) and withholds/re-posts the hold message.
- Stock display restore (patch number) `0x2003818C(ui)`; Globals menu open ⇔ word
  `0x20057438 != 0`. Stock ignores MIDI realtime bytes (parser table F8/FA/FB/FC → loop).
- OS = FreeRTOS + QP/C active object. Hooked sites run in the Timer Service task (keys/tick
  1 ms, panel buttons/pots 6 ms, MIDI byte parser 1 ms) or the Prophet5 AO task (MIDI
  notes, CC, hold); nothing hooked runs in an ISR. Full detail: `docs/re/stock-*.md`.

## V5 facts (see `firmware/v5_iface.h` for the address table)

- Engine struct at `0x200891D8`; hook state base `0x200891D0`; ctx `0x20089500`
  ([+7] A440 held, [+8] "used"); queue `0x2008950C` (32 × 6 bytes, overflow ⇒ V5 disables
  the arp — feed ≤ 16 events/tick). Event types: 1 note-on, 2 note-off, 3 button, 4 tempo,
  5 hold, 7 clear, 8 realtime.
- Engine fields: +0x300 enabled, +0x302 hold, +0x303 sounding note, +0x305 mode,
  +0x306 octave index, +0x307 octaves, +0x308 BPM, +0x30A div (2), +0x30C tps (1000),
  +0x310 accumulator, +0x318 output fn ptr (we redirect to `wrapper_output`), +0x320 ext
  clock, +0x324 clock-loss ticks. Internal period = 60·tps/(div·bpm) ticks, gate 50 %.
  MIDI sync: 12 clocks/step hard-coded (mod-12 by 0x1556 multiply).
- Pitfalls met: the arp's **clear** zeroes its own hold flag (re-assert hold via
  `0x20088F37`); **A440 release toggles the arp unless "used"** was set by a press V5
  received (consumed presses must poke `0x20089508`); button repeats (3) must be ignored.

## Design patterns in use

- Hooks chain: stock → wrapper → V5 entry (order preserved). Only `bl`/`word` patches at
  listed sites; the wrapper's external addresses live in one `const volatile` table that
  tests read back.
- Seq uses dummy notes `k + N·m` fed through V5's local-note entry and substitutes real
  pitches in the output callback; V5 kept at one octave while a sequence exists.
- Re-latch/seq hold logic: hold re-assert after every clear while HOLD active.

## Native arp engine (spec "Native arp engine (stock 2.1.0 base)") — built, HW-unverified

Replace V5 with our own engine hooked straight into stock 2.1.0; RE reports in `docs/re/`
(`v5-*.md` are git-ignored). David's rulings: octaves are per-pass transposition (C3 D4 |
C4 D5); a key into an empty pool starts a step immediately only with HOLD off and internal
clock; under external clock the grid is never reset; all display messages revert to the
patch display after 1.5 s; stock hold is suspended while the arp is on. Open decisions in
the spec: Up/Down end repeat, random repeat avoidance, arp to MIDI Out, Globals handling,
kill-switch button (ruled: A440 at power-on). Status: `firmware/arp.c` (engine), `arpui.c`
(UI), `native.c` (glue, queue, stock table) built and host-tested (`test_arp` 97 checks,
`test_arpui` 69, `test_native_image` 10); `make image-native` → `build/prophet10_native.syx`
(5.6 KB code, record `0x20088000–0x20090000`, 17 stock sites). **Flashed 2026-10-07 and
working** (A440, display, pattern, per-pass octaves, HOLD). First attempt stalled the loader
(empty tail group byte — fixed in tools/syx.py); the 1 s table-only kill switch did not see a
pre-held A440 — now 3 s and also an A440 press event; both verified 2026-10-07. Decisions
taken: Up/Down no end repeat; Random may repeat; no MIDI-Out toggle; Globals menu = pure
stock (A440 tuning tone reachable there); mode/octave change keeps the phase.

## Open items

- Hardware-unverified: note values under MIDI sync, seq recording from MIDI-in, the full
  image (clock filter — never installed), and everything in the native image.
- Cosmetic: entering seq with the arp at o 2–4 flashes `o 1` over the step count.
- Local-off mode shifting; rests in seq; display glyph for `o` vs `O` confirmed from V5.
