# Stock Main OS 2.1.0 — serial flash: driver, layout, free space

Source: disassembly of the stock Main OS (`fixtures/prophet5_main_2.1.0.syx`, decoded with
`python3 -m tools unpack`, records flattened into a RAM image at base `0x20010000`, then
`arm-none-eabi-objdump -D -b binary -marm -Mforce-thumb --adjust-vma=0x20010000`). Addresses are
RAM addresses; flash *offsets* are offsets into the chip. Confidence marks as in
`stock-apis.md` (**H** read from the code and cross-checked, **M** one interpretation step,
**L** inferred). **Nothing in this file has been checked on hardware yet.**

Why this exists: a sequence saved per program needs ~1.4 KB per program; the program record
has 29 spare bytes. This maps where the stock OS keeps things in flash, how it writes them,
and what it never touches.

## 1. The chip and the driver

- Serial NOR flash on **SPI2** (ADSP-SC57x SPI2 MMRs at `0x31044000`: STAT `+0x40`, RFIFO
  `+0x50`, TFIFO `+0x58`), **memory-mapped for reads at `0x60000000`** with a 16 MB window.
  Commands: 3-byte addresses, **4 KB sector erase `0x20`**, **256-byte page program `0x02`**,
  WREN `0x06`, RDSR `0x05` (bit 0 busy, bit 1 write-enable latch). **H**
- The OS never reads the JEDEC id (`0x9F` appears nowhere) and holds no size constant other
  than the 16 MB window. The chip's size is therefore not in the code. It is **≥ 8 MB**: offsets
  up to `0x715000`+ are in use (§2). **H**
- Driver API (Thumb; call with bit 0 set):
  - **`0x2003E2F8 flash_read(off, dst, len)`** → 0 ok, 3 range error. `memcpy` from
    `0x60000000 | off` under the mutex `*0x2007CFE0` (take `0x2002FCE4(sem, -1)`, give
    `0x2002F99C(sem, 0, 0, 0)`). Any task may call it. **H**
  - **`0x2003E3E4 flash_write(off, src, len)`** → 0 ok, 3 range, 4 misaligned (`off` and
    `len` must be multiples of 4096; `len` 0 returns 0). Takes the mutex, switches SPI2 to
    command mode (`0x2003DD44(0, 1)`), **erases every 4 KB sector** of the range (WREN, wait
    for WEL, `0x20 a2 a1 a0`, wait not-busy), then **programs every 256-byte page**
    (`0x02 a2 a1 a0` + 256 bytes), restores memory-mapped mode (`0x2003DD44(1, 0)`), gives
    the mutex. TX buffer `0x20010518` (16 + 1024 bytes), RX `0x2001092C`. **H**
  - **`0x20036E28 flash_write_verified(off, src, len)`** → 0 ok, 5 failed: per 4 KB sector,
    `flash_write`, then read back into `0x20055352` and compare the byte sums of source and
    read-back; up to 10 attempts per sector. **Every stock writer goes through this.** **H**
  - `0x2003E340 flash_wait(u8 *wel_out)`: RDSR poll, 2 ms task delay (`0x200360F0(2)`)
    between polls. `0x2003DBE4(dev, tx, txlen, rx, rxlen, cb)`: SPI DMA transfer (dev 2 =
    SPI2); the caller busy-waits on the completion flag. **M**
- Timing: a sector erase is tens to hundreds of ms, a page ~1 ms; the waits are task delays,
  so other tasks run, but the *calling* task is held for the whole write. Stock writes from
  the Prophet5 AO task (program store, Record, Globals, TUNE) and from the SysEx receiver —
  never from the Timer Service task that runs our hooks' tick. **M**
- RAM buffers: sector buffer **`0x20054352`** (4 KB), verify buffer `0x20055352`, second
  sector buffer `0x20056353` (bulk copies), flag byte `0x20056352`.

## 2. Flash layout — everything the Main OS references

| offset | size | content | accessors |
|---|---|---|---|
| `0x000000–0x03FFFF` | 256 KB | not referenced by the Main OS. The SC57x boot ROM loads its boot stream from offset 0, so this is Sequential's bootloader (the one that reads the OS header below and loads a slot) **L** | — |
| `0x040000–0x1FFFFF` | 1.75 MB | destination of a staged update image (copied 4 KB at a time from `0x200000` by `0x20037660`); never read by the Main OS **M** | `0x20037660` |
| `0x200000–0x3BFFFF` | 1.75 MB | staging area for that image (`0x20037904` = write at `(page + 0x200) << 12`) **H** | `0x20037904` |
| `0x3C0000` | 4 KB | **Panel OS header**: byte 0 slot flag, bytes 1–4 BE u32 `groups`, byte 5 `tail`, bytes 6–8 version digits, bytes 9–10 trailer **H** | `0x200377A0`, `0x20037940` (region 0) |
| `0x3C1000–0x440FFF` | 512 KB | Panel OS slot A | `0x20037858`, `0x20037898` |
| `0x441000–0x4C0FFF` | 512 KB | Panel OS slot B | |
| `0x4C1000–0x500FFF` | ≤ 256 KB | image "X" copy A, used when `*(u8 *)(0x2007AA38 + 13) == 0`; its length is kept in the `0x70E000` record **M** | `0x200378D8`, `0x20037914` |
| `0x501000–0x502FFF` | 8 KB | 16 × 512 B calibration tables (defaults from `0x2004D5AC`) **M** | `0x20037498` |
| `0x503000–0x506FFF` | 16 KB | not referenced | |
| `0x507000` | 4 KB | **Globals record**: 4 bytes + 29 × u16 globals (`0x20037550` saves `get_global(0..28)`, `0x200375B0` loads; an erased record gets defaults) **H** | |
| `0x508000` | 4 KB | not referenced | |
| `0x509000–0x510FFF` | 32 KB | alternative tuning tables: 64 × 512 B slots, each a 16-byte header + 128 × 3-byte entries (MIDI Tuning Standard) **M** | `0x200376BC`, `0x20037738` |
| **`0x511000–0x5FFFFF`** | **956 KB** | **not referenced** | |
| `0x600000–0x6063FF` | 25 KB | **user programs** 0–199, 128 B each | `0x20036E08` |
| `0x606400–0x60C7FF` | 25 KB | **factory programs** 0–199 | |
| `0x60C800–0x60CFFF` | 2 KB | rest of the last program sector | |
| `0x60D000` | 4 KB | **Main OS header** (same format as the Panel one) **H** | `0x200377A0`, `0x20037940` (region 1) |
| `0x60E000–0x68DFFF` | 512 KB | Main OS slot A | `0x20037858`, `0x20037898` |
| `0x68E000–0x70DFFF` | 512 KB | Main OS slot B | |
| `0x70E000` | 4 KB | settings record: byte 0 flag, bytes 1–4 and 5–8 BE u32 lengths of the image-X copies, bytes 9–10 flags **H** | `0x20036EFC`, `0x20036E9C` |
| `0x70F000–0x714FFF` | 24 KB | 48 × 512 B tables indexed by `0x2003C94C() − 20` (value range 20–66, almost certainly °C): oscillator tuning calibration, rewritten by TUNE (`0x2003728C`, from the TUNE press handler `0x2003A162`) **M** | `0x20037498`, `0x2003728C` |
| `0x715000–0x754FFF` | ≤ 256 KB | image "X" copy B (config byte 13 ≠ 0) **M** | |
| **`0x755000–end`** | **≥ 684 KB** on an 8 MB part, 8.7 MB on 16 MB | **not referenced** | |

Program address: `0x20036E08(factory, bank, group, prog)` =
`((factory ? 200 : 0) + (5·bank + group)·8 + prog + 0xC000) << 7`, i.e. 128-byte records
from `0x600000`. **H**

Program record (128 B): bytes 0–98 = parameters 0–98 as u8 (load pushes them one by one with
`0x2003CEF4(layer, param, value)` at `0x2003706A`); bytes 99–127 are never touched — the
"29 spare bytes". A record whose first two bytes read `0xFF` (erased) is initialised on first
load by writing the live program into it (`0x20036F7C`). **H**

## 3. OS update = A/B slots

- The receiver (`0x20032744…`) writes the incoming payload page by page into the **inactive**
  slot (`0x20037898` picks it by the header flag), reads every page back (`0x20037858(region,
  0, page)`) to verify, then rewrites the header with the flag toggled and the new
  `groups`/`tail`/version/trailer (`0x20037940`). The bootloader must read the header and
  load the flagged slot; a failed or interrupted update leaves the old slot active. **H** for
  the receiver, **M** for the bootloader's side.
- A slot holds 512 KB; stock 2.1.0 is 211 KB decoded, our native image 244 KB. **H**
- Two further update kinds exist (`0x20032098…`): command byte `0x7A` → staged at `0x200000`
  and copied to `0x040000`; byte `0x7D` or `0x74` (by config byte 13) → image-X copy. Neither
  is reached by the ordinary `7C`/`7C 7D` files. **M**

## 4. Program store/load paths (hook candidates)

- **Load**: `0x20036FE0(layer_from, layer_to, factory, bank, group, prog)` reads the 4 KB
  sector holding the program with `flash_read` and pushes the 99 params; the engine already
  hooks the end of program-apply (`0x2003D15C`, Prophet5 AO task). **H**
- **Store from the panel** (Record): `0x20036F7C(layer, factory, bank, group, prog)` copies
  the live params (`0x2003CB68(layer, param)`) into the sector buffer and writes it with
  `flash_write_verified`. Callers: see §4a. **H**
- **Store from SysEx** (program dump to a slot): `0x200370E4(factory, bank, group, prog, src,
  len)` from the SysEx dispatcher `0x2003498C`. Bulk factory ↔ user copy: `0x2003713C`. **H**

### 4a. Callers of the panel store

`bl 0x20036F7C` at **`0x2003B946`** — the Record + Program-button flow in the main UI state
(it reads layer `ui+0x19E` and the destination bank/group/program from `ui+0xC2/0xC4/0xC6`
and lights the Program LED `0x20036824(prog + 26, 1)` right after) — and at **`0x2003B518`**
(a second UI-side store, destination from the event, layer `ui+0x19E`). Both run in the
Prophet5 AO task, and both are 4-byte Thumb-2 `BL`s inside the stock code record, i.e. the
kind of site `hooks_native.json` already retargets: a wrapper that calls stock's store and then
writes our own sector for the same (bank, group, program) is the natural "program saved" hook.
The third caller, `0x2003704E`, is the load path initialising an erased record. **H**

## 5. What this means for a sequence saved per program

- The program record cannot hold it (29 spare bytes) and the OS has no extension mechanism,
  so a per-program sequence needs **its own flash area**.
- Two areas are never referenced by the Main OS: **`0x511000–0x5FFFFF` (956 KB)** between the
  tuning tables and the programs, and **`0x755000` to the end of the chip** (≥ 684 KB on an
  8 MB part). 200 user programs × one 4 KB sector each = 800 KB fits the first; two programs
  per sector halves that. The first is known-free regardless of chip size; the second is the
  conventional place for third-party data but needs the size.
- Write path: fill a 4 KB RAM buffer and call stock's **`0x20036E28`** from the **Prophet5 AO
  task**, right after stock's own program store (hook its caller), never from the Timer
  Service tick. Read with **`0x2003E2F8`** from the program-loaded hook (same task, so our
  reads never contend with our writes). Both are plain stock entry points of the kind
  `native.c` already lists in `stock_iface[]`.
- Before any write, a **read-only check on the instrument**: the "Flash diagnostic" (spec;
  A440 + Sync, in 2.1.0) compares two reference blocks with the blocks 8 MB higher — an 8 MB
  part aliases (identical bytes), a 16 MB part reads `0xFF` there — and scans both candidate
  areas for anything other than `0xFF`; results on the display. Nothing is written.
  **Readings taken 2026-10-09: `F16` / `1 E` / `2 E`.**
- **Readings (2026-10-09, Prophet-10 Rev4): `F16` / `1 E` / `2 E`** — a **16 MB** part;
  `0x511000–0x5FFFFF` and `0x755000–0xFEFFFF` are blank. The scan ran in seconds (the
  memory-mapped read is fast). The top 64 KB (`0xFF0000–0xFFFFFF`) is **unusable**: a
  memory-mapped read there blocked the calling task (display frozen, keys dead, power cycle
  needed) — the diagnostic stops at `0xFEFFFF`.
- **Consequence for sequence storage:** the upper half of the chip is free — 8.6 MB above
  `0x755000`. 200 user programs × 16 KB blocks = 3.2 MB covers every sequence the 512-step
  sequencer can hold (worst case ~11 KB compact); `0x800000 + slot × 0x4000`, ending at
  `0xB1FFFF`, far from everything stock touches (implemented as "Sequence memory").
- Unknowns: what the bootloader touches beyond the headers and slots; whether a future
  Sequential OS claims `0x511000+` (it looks like growth room for programs/tunings); the chip
  part number (readable on the board).

## 6. Quick address table

| what | RAM address | signature |
|---|---|---|
| flash read | `0x2003E2F8` | `(off, dst, len)` → 0/3 |
| flash write (erase + program) | `0x2003E3E4` | `(off, src, len)` 4 KB-aligned → 0/3/4 |
| flash write, verified | `0x20036E28` | `(off, src, len)` → 0/5 |
| RDSR wait | `0x2003E340` | `(u8 *wel)` |
| SPI2 mode switch | `0x2003DD44` | `(mmap, cmd)` |
| flash mutex | `*0x2007CFE0` | take `0x2002FCE4`, give `0x2002F99C` |
| program offset | `0x20036E08` | `(factory, bank, group, prog)` → offset |
| load program | `0x20036FE0` | `(layer_from, layer_to, factory, bank, group, prog)` |
| store live program | `0x20036F7C` | `(layer, factory, bank, group, prog)` |
| store record (SysEx) | `0x200370E4` | `(factory, bank, group, prog, src, len)` |
| OS header read | `0x200377A0` | `(region, u32 *groups, u8 *tail)` → slot flag |
| OS page read / write | `0x20037858` / `0x20037898` | `(region, slot, page, buf)` / `(region, page, buf)` |
| OS header write (flip) | `0x20037940` | `(region, groups, tail, version…)` |
| sector buffer | `0x20054352` | 4 KB |
