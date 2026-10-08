# Stock Main OS 2.1.0 — the ten Arp Mod patch sites, their functions and execution contexts

Sources: `orig.asm` (stock), `hack.asm` (the Arp Mod), `blob.asm`, `orig_ram.bin` (RAM image, base 0x20010000).
Confidence per fact: **H** high, **M** medium, **L** low.

## 0. Architecture facts that decide "locking or not"

**The firmware is FreeRTOS + a QP/C-style active-object state machine.** (H)
- FreeRTOS: strings `"IDLE"` @0x2004CF2A, `"Tmr Svc"` @0x2004CF34. `0x20030564` is `xTaskCreateStatic`
  (asserts `sizeof(StaticTask_t)==0x54`: `2003058e: movs r3,#0x54 … 20030594: cmp r3,#0x54`), `0x200310B4` is
  `xTimerCreateStatic` (asserts `sizeof(StaticTimer_t)==0x2c`: `200310ba: movs r3,#0x2c … cmp r3,#0x2c`),
  `0x2003111C` = `xTimerGenericCommand` (used as xTimerStart), `0x20030730` = `xTaskGetTickCount`.
- Timer Service task priority **6**: `2003106e: movs r3,#6 ; str r3,[sp]` as 5th arg of the `"Tmr Svc"` xTaskCreateStatic @0x20031082.
- QP/C: `0x20031970` = `QF_newX_(evtSize, margin, sig)` (`200319c2: strh.w r8,[r0]` sig, `strb r4,[r0,#2]` poolId,
  `strb r3,[r0,#3]` refCtr). State handlers return 3 = Q_HANDLED, 8 = Q_TRAN (after `str r3,[r4,#8]`), 0 = Q_SUPER;
  `0x200314AD` = QHsm_top (`movs r0,#4; bx lr` = Q_IGNORED). `0x2003154C` = QHsm_dispatch_ (`2003156a: ldr r6,[r4,#8] … blx r6`).
- One application AO, **"Prophet5"** (`QActive_setAttr(AO, TASK_NAME_ATTR, "Prophet5")` @0x2003D528), pointer at
  `[0x2004D5EC]`, started @0x2003D544 via vtable slot 2 with **QP prio 2, queue 0x7F events, 8 KB stack**; the FreeRTOS
  priority passed is the QP prio unchanged (`2003141c…20031456: stm.w sp,{r6,r8}` with r6 = prio) → **AO task prio 2**.
  AO task body `0x200313A4`: `loop { e = 0x200318A4(me) /*blocking get*/; (*vptr->dispatch)(me,e) @200313be; QF_gc(e) 0x200319EC }`.
- All Prophet5 states are **siblings directly under QHsm_top** (every Q_SUPER site stores 0x200314AD):
  main/playing state `0x20039801`, boot sub-state `0x200396F1`, `0x200387B9`, `0x2003883D`, `0x200389C1`,
  `0x2003904D`, `0x20039261`, `0x2003B701`.
- Software timers (all auto-reload), created in init `0x2003D340…` and lazily in `0x2003D6E8`:

| name (string) | period (ticks) | callback | role |
|---|---|---|---|
| "Note Handler Event Timer" 0x2004D611 | 1 | **0x2003BE8D** | keyboard FIFO consumer (sites 0x2003BE9C/0x2003BECC) |
| "Panel Timer" 0x2004D62A | 6 | **0x2003C4C9** | panel event-word reader → decoder 0x2003C204 (sites 0x2003C244/92/A6) |
| "ADC Timer" 0x2004D636 | 4 | 0x200361F1 | wheels/pedals → posts sig 0xA events |
| "Midi Handler Timer" 0x2004D640 | 1 | **0x200342ED** | MIDI byte parser (site 0x200343D8 table) |

  Evidence: `2003d546: ldr r3,[pc] @0x2003d65c (=0x2003BE8D); movs r1,#1 … bl 0x200310b4`; `2003d57a: ldr r3 @0x2003d664 (=0x2003C4C9); movs r1,#6`;
  `2003d5ae: …@0x2003d66c (=0x200361F1); movs r1,#4`; `2003d6f4: ldr r3 @0x2003d750 (=0x200342ED); movs r1,#1 … bl 0x200310b4`.
- "MIDI Process Task" (0x2004D653), **prio 3**, body `0x200344AC`: blocks on a stream buffer (`0x200301FC(buf,&rec,6,-1)`),
  calls `0x20033D88(port?, status, d1, &d2)` which **posts QP events (sig 0xB) to the Prophet5 AO** (`20033f10: bl 0x2003bd5c`).
  Created at `2003d73e: bl 0x20030564` with `[sp]=3`.
- Keyboard scanner `0x200364D4` is an **interrupt handler**: installed by `2003677a: bl 0x200431f4(iid=0x4C, 0x200364D5, 0)`
  (0x200431F4 stores handler/param into the dispatch table at 0x2007D048; 0x20042DF0/0x20042EC8 poke GIC regs 0x310B2xxx).
  It pushes 2-byte (note, vel) records into the keyboard FIFO at `[0x20071738]` (`20036456 / 200364c0: bl 0x2003d862(fifo,&rec,2)`; note = key+0x24).

**Consequence (H):** the hooked sites live in **two FreeRTOS tasks**: the Timer Service task (prio 6: keyboard notes, panel
buttons/pots, MIDI realtime bytes) and the Prophet5 AO task (prio 2: MIDI note-on/off, CC123, HOLD). The timer task
pre-empts the AO task at any instruction (higher priority, every tick); the four timer callbacks never pre-empt each
other (one task). Nothing hooked runs in an ISR. Any state shared between the timer-side hooks and the AO-side hooks
(or touched by both) needs a critical section (`cpsid/cpsie` as the Arp Mod does at 0x20088F9E–0x20088FC4, or
taskENTER_CRITICAL) or a single-producer/single-consumer design. Timer callbacks must not block.

## 1. Site 0x200343D8..0x200343EC — MIDI byte-parser system-status jump table

- **Function:** `0x200342EC` = "Midi Handler Timer" callback (`void cb(TimerHandle_t)`), period 1 tick, **Timer Service task**. (H)
  Loop top `0x20034342`: `mov r0,r4 (port); add r1,sp,#7; bl 0x200342a8 /*get byte*/` … `20034366: ldrb r0,[sp,#7]` → status/data
  decode. r4 = port index (0/1; 0 → ring at [0x2007172C] via 0x20032CEC, 1 → 0x20033834) (M).
- **The table** is a jump table, not a state table: `200343aa: subs r0,#0xf0 ; cmp r0,#0xc ; bhi 0x2003443a ; adr r3,#4 ; ldr.w pc,[r3,r0,lsl #2]`
  → base **0x200343B8**, index = status − 0xF0:

| idx | status | word addr | stock target | the Arp Mod (hack.asm) |
|---|---|---|---|---|
| 0 | F0 | 0x200343B8 | 0x200343ED (SysEx start) | same |
| 1–7 | F1–F7 | 0x200343BC–D4 | 0x2003443B (`mov r0,r4; bl 0x20033d08` reset parser for port; continue) | same |
| 8 | **F8** | **0x200343D8** | 0x20034343 (= loop top: byte ignored) | **0x20089095** |
| 9 | F9 | 0x200343DC | 0x2003443B | same (unchanged — only 4 of the 5 words differ) |
| 10 | **FA** | **0x200343E0** | 0x20034343 | **0x20089095** |
| 11 | **FB** | **0x200343E4** | 0x20034343 | **0x20089095** |
| 12 | **FC** | **0x200343E8** | 0x20034343 | **0x20089095** |

  Stock therefore discards clock/start/continue/stop entirely at byte level. The Arp Mod's trampoline 0x20089094:
  `push {r0-r4,lr}; add r0,r0,#0xf0 (status byte); mov r1,r4 (port); bl sniff 0x20088F58; pop; ldr pc,[pc] → 0x20034343`.
  hooks.json's "entry 1/3/4/5" numbering is relative to 0x200343D4; the real meaning is F8/FA/FB/FC. (H)

## 2. Site 0x200396CA — inside stock `hold_set`

- **Function `0x20039688` = hold_set(r0 = new state 0/1, r1 = source: 0 button, ≠0 pedal/CC64).** (H)
  `20039688: ldr r3,=0x20037390 ; push {r4,lr}` ; button flag `[r3+0x19c]`, pedal flag `[r3+0x19d]`;
  old = pedal?1:button (`2003968c…2003969a`), store new (`200396a0/200396a6`), new merged → r4 (`200396ba: and r4,r0,#1`);
  if changed: `200396c2: orr r0,r0,#0x8000000 ; orr r0,r0,#0xd0000 ; 200396ca: bl 0x2003d324` (DSP message 0x080D0000|state),
  `200396ce: cbnz r4 ; bl 0x2003eee0` (state 0 → release held voices), `200396d4: movs r0,#0x23; movs r1,#0; bl 0x20036824` (HOLD LED off) then on if r4.
- **Original / replacement:** `200396ca: f003 fe2b bl 0x2003d324` → the Arp Mod `f04f fcd9 bl 0x20089080`. Arp Mod hook: `push {r4,lr}; ldr r3,=0x2003D325; blx r3`
  (performs the stock DSP post), `mov r0,r4; bl 0x20088f36` (enqueue hold event, state = r4), `pop {r4,pc}`. **r4 = merged new hold state** is the only input. (H)
- `0x2003D324` = post 32-bit word to the voice DSP: `ldrb [0x20057570]; cbnz → return 0` (DSP comms disabled flag) else `[0x2005B7D0]→vtable[0xC](word)`. (M)
- **Callers / contexts** (all are Prophet5-AO state code unless noted) (H for listed ones, M for "no others"):
  - HOLD/Release button, main state: `20039dac: eor r0,r0,#1 (toggle button flag); 20039db0: bl 0x20039688` when global 9 == 1 (`20039d9c: movs r0,#9; bl 0x20037b20; cmp r0,#1`); release path 0x20039E00. Global 9 ≠ 1 → `0x2003CF58` (Release function instead of HOLD).
  - Boot sub-state 0x200396F0: `2003975c`. Sustain footswitch (sig 0xA, type 8): `2003afe8: bl 0x20039688` with r1=1. MIDI CC64 (0x2003B218): `2003b22c` with r1=1.
  - `0x2003B6B0` (clear/re-assert when global 9 == 1) ← `2003d15c` in program-load `0x2003D00C` ← AO states / SysEx handler / init.
  - Set-global `0x20037B74(…, idx 9, val)` → `20037c02/20037c0c: hold_set(0,0)` ← AO states, SysEx (AO), startup `0x200375B0`.
  → **hold_set runs in the Prophet5 AO task** (plus boot-time init before the scheduler loop). (H)

## 3. Sites 0x2003B032, 0x2003B07A, 0x2003B294 — MIDI channel messages in the main state

- **Function `0x20039800` = Prophet5 main state handler `QState main(me, e)`** (`push.w {r4-r9,lr}; sub sp,#0x2c`; `20039814: ldrh r3,[r1]` sig; `tbh` @0x20039820).
  Sig map (k = sig−1): 4 → buttons 0x2003984A (`ldrh r6,[r1,#4]` id, tbh over 0..0x28), 6 → pot change 0x2003AC1A,
  **0xB → MIDI channel message 0x2003B018**, 0xA → local controllers 0x2003AE22, 8 → `0x2003B65E: movs r0,#3` (ignored, see §7).
  Event layout for sig 0xB (built by `0x2003BD5C`, posted from the MIDI Process Task): `[4]` port/ch, **`[6]` status (channel stripped), `[8]` data1, `[0xA]` data2**, `[0xC]` extra.
  Context: **Prophet5 AO task (prio 2)**; data path: UART/USB → parser timer (Tmr Svc) → stream buffer `0x2003011C` → MIDI Process Task (prio 3) `0x20033D88` → `20033f10: bl 0x2003bd5c` → AO queue. (H)
- **0x2003B032 (note-off):** `2003b028: cmp r3,#0x80 ; 2003b02e: movs r0,#2 ; ldrh r1,[r1,#8] ; 2003b032: f003 ff53 bl 0x2003eedc` → the Arp Mod `f04d fe7b bl 0x20088d2c`. Args r0 = src 2, r1 = note. (H)
- **0x2003B07A (note-on):** `2003b074: movs r0,#2 ; ldrh r1,[r1,#8] ; ldrh r2,[r5,#0xa] ; 2003b07a: f003 fdef bl 0x2003ec5c` → the Arp Mod `f04d fe3f bl 0x20088cfc`. r0 = 2, r1 = note, r2 = velocity. (H)
  (The MIDI task already turns 0x90 vel 0 into status 0x80: `20033dd8: cmp r3,r1 ; moveq r1,#0x80`.)
- **0x2003B294 (CC 123):** CC dispatch `2003b082: ldrh r0,[r1,#8]` … `2003b110: cmp r0,#0x7b ; beq.w 0x2003b294` ; `2003b294: f003 fca6 bl 0x2003ebe4` → the Arp Mod `f04d fdf5 bl 0x20088e82`. No arguments. (H)
  Note: CC 124–127 also call all-notes-off (`2003b13e: cmp r0,#0x7f ; bhi … ; 2003b144: bl 0x2003ebe4`) and the Arp Mod does **not** hook that site.

## 4. Sites 0x2003BE9C, 0x2003BECC — keyboard FIFO consumer

- **Function `0x2003BE8C` = "Note Handler Event Timer" callback, period 1 tick (1 ms per NOTES' 1 kHz tick), Timer Service task (prio 6).** (H)
  `2003be8e: ldr r4,=0x20071738 ; ldr r0,[r4] (fifo) ; 2003be9c: bl 0x2003d828 ; cmp r0,#1 ; bls return` → needs ≥2 bytes;
  `2003beaa/2003beb4: bl 0x2003d8a6(fifo,&byte)` pops note → `[sp+6]`, vel → `[sp+7]`;
  `2003beb8: movs r0,#7 ; bl 0x20037b20 ; cmp r0,#2` → global 7 (Local Control; CC122 toggles it 0↔2 at 0x2003B124) == 2 → local path:
  `2003bec2: movs r0,#1 ; ldrb r1,[sp,#6] ; ldrb r2,[sp,#7] ; 2003becc: bl 0x2003ec5c` then `2003bed0…2003bed8: bl 0x2003bce0(note,vel)`;
  else (local off) → direct MIDI send `0x20033F84(ch=global 6, port=0x20037B2C(), note, vel)` / `0x20033F38` for vel 0.
- **0x2003BE9C:** original `f001 fcc4 bl 0x2003d828` = fifo_count(fifo) — `{[0] buf,[4] wr,[8] rd,[0xC] size}` → `wr>=rd ? wr-rd : size+wr-rd`; the Arp Mod `f04c ff59 bl 0x20088d52` (kbd-scan tick hook, same (fifo) → count contract). (H)
- **0x2003BECC:** original `f002 fec6 bl 0x2003ec5c` = note_on(src=1, note, vel); the Arp Mod `f04c fec0 bl 0x20088c50`. (H)
- FIFO producer is the keyboard-scan **ISR** (IRQ 0x4C, §0); velocity from contact timing (`2003649a: cmp r3,#0x89 … rsbhi r4,r3,#0x8a ; movls r4,#0x7f`). (H)

## 5. Sites 0x2003C244, 0x2003C292, 0x2003C2A6 — panel event-word decoder (buttons and pots)

- **Function `0x2003C204` = decode one 16-bit panel event word (r0) → returns 1 (continue) / 0 (stop).** Called only from
  `2003c6be: bl 0x2003c204` inside the **"Panel Timer" callback `0x2003C4C8`** (period 6 ticks, Timer Service task), which walks up to 0x45 words
  from the panel SPI buffer `[base+0x18c+4*i]` (`2003c6b6…2003c6c8`). (H)
  Word type = bits 15..13 (`2003c214: ubfx r3,r0,#13,#3 ; subs r3,#1 ; tbb`): **4 = button, 6 = pot low byte, 7 = pot high byte**; others return.
  - Button (0x2003C22A): `ubfx r5,r0,#4,#2` value (1 press/2 release/3 held), `ubfx r6,r0,#6,#7` id; `bl 0x20036114(id,value)` gate (accepts 1..3) →
    **0x2003C244: `f7ff fcf4 bl 0x2003bc30`** = post button event: `QF_newX_(8, NO_MARGIN, sig 4)`; `e[4]=id (u16), e[6]=value (u8)`; `QACTIVE_POST([0x2004D5EC], e, 1)`
    (`2003bc50: strb r4,[r0,#6] ; strh r5,[r0,#4] ; … ldr r4,[r2,#0xc] ; bx r3`). The Arp Mod: `f04c fe31 bl 0x20088eaa` (button hook (id,value)). (H)
  - Pot type 7 (0x2003C2AC): `ubfx r0,r0,#7,#6` id, `and r1,r4,#0x7f` → `0x20036B5C`: `pot_tbl[id].hi = (v<<7)&0xF80` (table 0x20079D84, 16-byte rows). (H)
  - Pot type 6 (0x2003C24A): `ubfx r5,r0,#7,#6` **id**, raw = `(word&0x7f) | pot_tbl[id].hi` (`2003c256…2003c25e`); id 1 gets a centre-detent remap `0x2003C1D0(0x1DD,0x223,raw)`;
    `r6 = 0x20036C70(id, raw)` new scaled value (= raw·(max+1)>>10, max from pot→param table), `r1 = 0x20036B6C(id)` old raw, `r7 = 0x20036C70(id, old)`;
    **0x2003C292: `f7fa fc5d bl 0x20036b50`** = `pot_tbl[id].raw = r1` (`20036b50: lsls r0,r0,#4 ; ldr r3,=0x20079D84 ; str r1,[r3,r0]`), **args r0 = pot id, r1 = 12-bit raw**. The Arp Mod: `f04c fe7d bl 0x20088f90`.
    Then `2003c296: cmp r7,r6 ; bne post ; ldrb [0x2005754A] ; cbz return` → **0x2003C2A6: `f7ff fce1 bl 0x2003bc6c`** = post pot-change event:
    `QF_newX_(10, NO_MARGIN, sig 6)`; `e[4]=id, e[6]=new scaled, e[8]=old scaled` (`2003bc8e: strh r6,[r0,#4] ; strh r5,[r0,#8] ; strh r4,[r0,#6]`); **args r0 = id, r1 = old, r2 = new**. The Arp Mod: `f04c feb3 bl 0x20089010`. (H)
    The AO consumes sig 6 at 0x2003AC1A (`ldrh r6,[r1,#4]` id → `0x20036C30(id)` program-parameter index → apply/display).
- **What these two hooks are (answer to the hypotheses): the Glide Rate pot → arp tempo.** (H)
  - Blob `0x20088F90` (replaces the raw store): `mov r4,r1 (raw); mov r5,r0 (id); bl 0x20088c80 (init guard); … 20088fa4: sub.w r3,r5,#0x16 ; clz r3,r3 ; lsrs r3,r3,#5` (= id == **22**),
    `20088fb0: ldrb.w r1,[r0,#0x308]` (0x200891D0+0x308 = **0x200894D8 arp-enabled byte**), `ands r1,r3`; if both: `add.w r0,r0,#0x33c (queue 0x2008950C); uxth r2,r4; movs r1,#4 ; b.w 0x20088aae`
    → **enqueue Arp Mod event type 4 (tempo) with the raw pot value, and skip the stock store**; else `20088fd8: movw r2,#0x6b51; movt r2,#0x2003 … bx r2` → stock `0x20036B51(id, raw)`.
  - Blob `0x20089010` (replaces the event post): same `id == 0x16 && arp enabled` test (`20089026…20089040`); if true `2008904a: pop {r4,r5,r6,pc}` → **swallow the pot-change event** (stock never sees the Glide change); else tail-call `0x2003BC6D(id, old, new)` with registers restored (`2008904c…2008905e`).
  - Pot id 22 → program parameter **13** via the stock pot→parameter table at 0x2004D190 (`pot 22 -> 13`); parameter 13's descriptor (0x2004C478+13·12) = max 127, default 0.
    the Arp Mod's guide (p. 1): "With the arp enabled and internal clock selected, turn **Glide Rate** for 40-300 BPM… Glide Rate returns to normal when the arp is off" — exactly the swallow-when-enabled behaviour above. Identity "pot 22 = Glide Rate" rests on the Arp Mod guide + this behaviour (H); the mapping 13 = Glide Rate in the Rev4 parameter numbering is from memory of the manual (M).
  - Not Globals entry/exit, not program change: no code in either entry touches state or program data. (H)

## 6. note_on / note_off / all-notes-off — signatures and callers

- **`0x2003EC5C` = note_on(r0 = src, r1 = note, r2 = vel).** `2003ec60: mov r10,r0 ; mov r4,r1 ; mov r8,r2 ; cbnz r2 … ; 2003ec70: b.w 0x2003e95c` → **vel 0 tail-calls note_off(src, note)**.
  src flows only to the voice assigner `0x2003E828(layer, src, note, vel)` (`2003ecf4: mov r0,r5 ; mov r1,r10 ; mov r2,r4 ; mov r3,r8`) → voice trigger `0x2003E718(src, voice, note, vel)` which stores it in the
  10-byte voice record at 0x2007CFE4: `2003e762: strh r0,[r4,#6]` (and `2003e766` note, `2003e76a` vel, `[4]` age), then posts `(0x810+voice)<<16 | note<<8 | vel` to the DSP (`2003e77a…2003e788`).
  Release clears it (`2003e7c6: strh r2,[r3,#6]`). **No reader of voice[+6] exists** anywhere the voice table (literal 0x2007CFE4, refs 0x2003E78C…0x2003F0DC) is used → src is bookkeeping only; local (1) and MIDI (2) notes are treated identically by the voice path. (M-H)
  Callers with r0: `2003becc` **1** (local keyboard, Tmr Svc); `2003b07a` **2** (MIDI in, AO); `2003a1ea` **2** with note 0x3C vel 0x7F (A440 button press in "A440 plays a note" mode, AO: `2003a1dc: ldrh r1,[r3,#0x38]; cbz … movs r0,#2 ; movs r1,#0x3c ; movs r2,#0x7f`).
- **`0x2003EEDC` = `b.w 0x2003e95c`, a thunk; real note_off = `0x2003E95C(r0 = src, r1 = note)`** (`2003e960: mov r10,r0 ; mov r0,r1 ; bl 0x2003c024` clears the per-key state, then per layer finds/releases voices; src only re-passed on re-trigger `2003ea8c: mov r1,r10 ; bl 0x2003e828`).
  Callers: `2003b032` src 2 (MIDI, AO); `2003a248` src 2 (A440 release, AO); implicit via note_on vel 0. Hold-off release `0x2003EEE0` re-triggers with src **0** (`2003ef40: movs r1,#0`). (H)
- **`0x2003EBE4` = all_notes_off()**: posts voice-off for voices 0x81A..0x823 (`movw r4,#0x81a … movw r7,#0x824 ; lsls r0,r4,#16 ; bl 0x2003d324`), clears per-note state for notes 0..127 (`2003ec18: bl 0x2003c024`), zeroes `[0x200600A4]`,`[+4]`.
  Other callers (none hooked by the Arp Mod): 0x20034664, 0x20037BE0 (set-global idx 6), 0x20039786 (boot state, button 0x22), 0x2003A1A2, 0x2003A58C, 0x2003A7DA, 0x2003A9D6, 0x2003AB58, **0x2003B144 (CC 124–127)**, 0x2003CD12, 0x2003D146 (program load), 0x2003EC50 (voice-count change wrapper 0x2003EC38). (H)

## 7. Side finding: `0x2003BCE0` (the planned octave-shift "MIDI out" hook at 0x2003BED8) is not a MIDI send

`0x2003BCE0(note, vel)` builds `QF_newX_(8, NO_MARGIN, sig 8)` with `e[4]=note, e[6]=vel` (`2003bcf2: movs r0,#8 ; movw r1,#0xffff ; mov r2,r0 ; … strh r5,[r0,#4] ; strh r4,[r0,#6]`) and posts it to the Prophet5 AO.
Every Prophet5 state maps sig 8 to a no-op: main `0x2003B65E: movs r0,#3`; 0x200389C0 → Q_SUPER(top); 0x200387B8 → 0x20038828 (default); 0x2003883C → 0x200389AA; 0x2003904C → 0x2003923C; 0x20039260 → 0x20039674; 0x2003B700 → 0x2003BBF8; 0x200396F0 → default. The only raw MIDI note sends (`0x20033F84` on, `0x20033F38` off) are called solely from the local-off branch (`2003befa`, `2003bf16`). So in Local-On mode the stock code does not transmit keyboard notes through any path I can find (M-H; worth confirming on hardware before building the octave shift on 0x2003BED8).

## 8. Per-site summary

| site | function (start) | stock instr → the Arp Mod | regs at call | task context |
|---|---|---|---|---|
| 0x200343D8/E0/E4/E8 | MIDI byte parser 0x200342EC (timer cb) | words 0x20034343 → 0x20089095 (F8/FA/FB/FC slots) | r0 = status−0xF0, r4 = port | **Tmr Svc (prio 6)** |
| 0x200396CA | hold_set 0x20039688 | `bl 0x2003d324` → `bl 0x20089080` | r0 = DSP word 0x080D0000|state, **r4 = new hold** | **Prophet5 AO (prio 2)** |
| 0x2003B032 | main state 0x20039800 | `bl 0x2003eedc` → `bl 0x20088d2c` | r0=2, r1=note | AO |
| 0x2003B07A | main state 0x20039800 | `bl 0x2003ec5c` → `bl 0x20088cfc` | r0=2, r1=note, r2=vel | AO |
| 0x2003B294 | main state 0x20039800 | `bl 0x2003ebe4` → `bl 0x20088e82` | none | AO |
| 0x2003BE9C | kbd FIFO consumer 0x2003BE8C (timer cb) | `bl 0x2003d828` → `bl 0x20088d52` | r0 = fifo ptr → returns count | **Tmr Svc** |
| 0x2003BECC | kbd FIFO consumer 0x2003BE8C | `bl 0x2003ec5c` → `bl 0x20088c50` | r0=1, r1=note, r2=vel | Tmr Svc |
| 0x2003C244 | panel decoder 0x2003C204 ← Panel Timer 0x2003C4C8 | `bl 0x2003bc30` → `bl 0x20088eaa` | r0=button id, r1=value 1/2/3 | **Tmr Svc** |
| 0x2003C292 | panel decoder 0x2003C204 | `bl 0x20036b50` → `bl 0x20088f90` | r0=pot id, r1=raw value | Tmr Svc |
| 0x2003C2A6 | panel decoder 0x2003C204 | `bl 0x2003bc6c` → `bl 0x20089010` | r0=pot id, r1=old, r2=new | Tmr Svc |

Open/unverified: FreeRTOS `configUSE_PREEMPTION` not read from the tick ISR (assumed 1 — the default, and the Arp Mod itself uses `cpsid/cpsie` around its queue); exact tick rate taken from NOTES (1 kHz); port index meaning (0 DIN / 1 USB) inferred from the two reader functions, not from hardware code.
