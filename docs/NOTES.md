# Working notes — state, facts and lessons (keep current)

Written so a fresh session can continue without re-deriving anything. `docs/SPEC.md` is the
behavioural source of truth; this file is the engineering context around it.

## Where things are

- Repo `github.com/dangerous/prophet-rev4-mods` (local folder `~/git/prophet-arp-mods`), branch `main` fast-forwarded from the worktree branch
  `relatch-seq` at `.claude/worktrees/relatch-seq` (all work happens there; `main` is merged
  with plain `git merge` from the root). An agent worktree
  `.claude/worktrees/agent-a8014a76fdbbe9479` holds the already-integrated octave fix; safe
  to remove.
- `dist/` holds the two installable images + `SHA256SUMS` (written from inside `dist/`, so
  verify with `cd dist && shasum -a 256 -c SHA256SUMS`). **Never delete dist files.**
- Original inputs in `~/git/prophet` (stock 2.1.0, Panel 1.1.3, V5 arp mod, its PDF guide,
  an 800 MB Overview.MOV); copies of the `.syx`/guide in `fixtures/` (git-ignored — the
  repo ships only `fixtures/README.md` + `SHA256SUMS`; history was rewritten to drop them).
- Scratchpad (session-only, may be gone): decoded images and full disassembly listings
  (`orig.asm`, `hack.asm`, `blob.asm`); regenerate with `python3 -m tools unpack` + the ELF
  wrapper trick (write a minimal ELF around a flat binary and run Apple's
  `/usr/bin/objdump -d --triple=thumbv7a-none-eabi`).

## Hardware status (David's Prophet-10 Rev4, no DIN cable → bootloader recovery unavailable)

- Flashed so far: re-latch-only (bug: hold flag), fixed re-latch (OK), relatch+seq
  (bugs: button repeat, seq octave), `prophet10_v5_relatch_seq_internal.syx` with the
  octave fix pending install at the time of writing.
- Verified: loader path, boot, hooks, HOLD stub, re-latch, seq record/play/clear, note
  value UI not yet reported, readout works (Keyboard id 36 read out).
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
  passes 1–3). Known ids: Program 1–8 = 0–7, A440 = 0x0F, GLOBALS(?) = 0x19, Group = 0x20,
  Bank = 0x28, **Osc B Keyboard (key follow; has stock Group/Bank combos) = 36 (0x24)**,
  **filter Keyboard Amount = 8** (cycles off/half/full on press; LED follows). The P10's
  panel is the same as the P5's — no extra buttons.
- Display: `0x20037F25(c0,c1,c2)` 3 chars; `0x20037FF7(int)` integer (negatives shown).
  Codes: digits 0–9 = 0–9, A=0x0A b=0x0B d=0x0D E=0x0E F=0x0F i=0x12 L=0x15 n=0x17 O=0x18
  P=0x19 r=0x1B S=0x1C t=0x1D U=0x1E y=0x22 o=0x24 blank=0x25 '-'=0x26.
- Stock keyboard FIFO consumer `0x2003BE8A…`: `note_on(1,note,vel)` at `0x2003BECC`
  (hooked), then MIDI-out of the raw key `bl 0x2003BCE0(note, vel)` at `0x2003BED8`
  (candidate hook for octave shift). Local-off path uses `0x20033F84/0x20033F38`.
- Stock HOLD handler `0x200396xx` merges button + HLd pedal; LED 0x23; posts DSP msg.

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

## Next: keyboard octave shift (approved design, spec slice 7)

Modifier = Keyboard Amount (id 8): swallow its press; Bank = +1 octave, Group = −1,
range ±2, integer display; release without Bank/Group ⇒ replay press+release to the stock
(tap still cycles tracking; LED changes on release). Shift applies to local keys at the
note hook (per-key shift memory for safe releases) and to the MIDI-out call at
`0x2003BED8` (new `bl` hook, expect stock `0x2003BCE1`). Not applied in local-off mode.
Split point moves with the keyboard (documented).

## Open items

- Note value UI unverified on hardware; full image (clock filter) never installed.
- Cosmetic: entering seq with the arp at o 2–4 flashes `o 1` over the step count.
- Local-off mode shifting; rests in seq; display glyph for `o` vs `O` confirmed from V5.
