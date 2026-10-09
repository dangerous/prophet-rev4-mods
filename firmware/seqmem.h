/* Sequence memory (docs/SPEC.md: "Sequence memory"): the sequence and its settings saved
 * with a user program in a 16 KB block of the serial flash's upper half, written with
 * stock's verified writer after stock has stored the program, read back on a program load.
 * Portable: flash access through platform.h (plat_flash_read / plat_flash_write). The block:
 *
 *   0  'P' 'S' 'Q' '1'            magic
 *   4  length (u16 LE)            bytes in the block, header included
 *   6  flags                      bit 0 = a sequence follows ("no sequence" otherwise)
 *   7  slot                       0..199: the user program the block belongs to
 *   8  checksum (u16 LE)          sum of the program's 99 stored parameters
 *  10  rate code, style, order, chord beats, transpose (int8), generator selection
 *  16  events: duration (u16 LE), count, count x (note, velocity)
 *
 * An erased block (0xFF) or anything that does not match the program just loaded is "no
 * valid block": the live sequence is left alone. */
#ifndef SEQMEM_H
#define SEQMEM_H

#include <stdint.h>

#include "seq.h"

#define SEQMEM_BASE 0x800000u            /* block of user program slot s at BASE + s * BLOCK */
#define SEQMEM_BLOCK 0x4000u             /* 16 KB: 512 steps of ten-note chords need 11.8 KB */
#define SEQMEM_SLOTS 200u
#define SEQMEM_END (SEQMEM_BASE + SEQMEM_SLOTS * SEQMEM_BLOCK)   /* 0xC80000, exclusive */
#define SEQMEM_SECTOR 4096u              /* the verified writer's unit */
#define SEQMEM_PIECE 1024u               /* read scratch */
#define SEQMEM_HEADER 16u
#define SEQMEM_PARAMS 99                 /* stored program parameters summed into the checksum */

enum { SEQMEM_NONE = 0, SEQMEM_LOADED = 1 };

typedef struct {
    uint8_t rate_code;                /* the Seq's note value (rate.h code) */
    uint8_t style, order, chord_beats;
    int8_t  transpose;
    uint8_t gen;                      /* generator selection: 0 ArP, 1 SEq */
} seqmem_settings_t;

/* The slot of a program location: 0..199 for a user program, -1 for factory or out of range. */
int seqmem_slot(int factory, int bank, int group, int prog);

/* The checksum of the live program's stored parameters (plat_param_read 0..98). */
uint16_t seqmem_checksum(void);

/* The block's byte stream: the window [off, off + out_len) of it into out (zero-filled past the
 * end); returns the stream's total length. */
uint32_t seqmem_encode(const seq_t *q, const seqmem_settings_t *s, int slot, uint32_t off,
                       uint8_t *out, uint32_t out_len);

/* Save: the block for slot, assembled sector by sector in sector_buf (SEQMEM_SECTOR bytes)
 * and written with plat_flash_write — only the sectors the stream needs. "No sequence" when
 * q->len == 0. Returns 0, or the first write's error. */
int seqmem_save(int slot, const seq_t *q, const seqmem_settings_t *s, uint8_t *sector_buf);

/* Load: the block for slot, read in SEQMEM_PIECE pieces through piece_buf. When it is valid
 * for this program (slot and checksum) and holds a sequence, q's events, len and total are
 * replaced and *s filled: SEQMEM_LOADED. Otherwise nothing changes: SEQMEM_NONE. The caller
 * tells the sequencer afterwards (seq_replaced) and applies the settings. */
int seqmem_load(int slot, seq_t *q, seqmem_settings_t *s, uint8_t *piece_buf);

#endif
