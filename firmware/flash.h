/* Read-only flash diagnostic (docs/SPEC.md: "Flash diagnostic"): the serial flash's size by
 * aliasing, and a blank check of the two areas the stock OS never references
 * (docs/re/flash.md). A portable state machine: flash_start, then one flash_tick per 1 ms
 * tick, each reading one FLASH_PIECE through plat_flash_read (stock's read routine). It
 * never writes — there is no write call to make. The UI shows FLA while it runs and the
 * three results when it is done (arpui.c). */
#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

#define FLASH_PIECE 512u                 /* bytes read per tick while comparing reference blocks */
#define FLASH_AREA_PIECE 1024u           /* bytes read per tick while scanning an area */
#define FLASH_BLOCK 4096u                /* a reference block */
#define FLASH_R1 0x000000u               /* reference 1: the bootloader */
#define FLASH_R2 0x606000u               /* reference 2: factory programs */
#define FLASH_UPPER 0x800000u            /* 8 MB higher: aliases on an 8 MB part */
#define FLASH_AREA1_LO 0x511000u         /* between the tuning tables and the programs */
#define FLASH_AREA1_HI 0x5FFFFFu
#define FLASH_AREA2_LO 0x755000u         /* above everything stock references */
#define FLASH_END_8M 0x7FFFFFu
#define FLASH_END_16M 0xFFFFFEu          /* stock's read routine cannot return the window's last byte */

enum { FLASH_UNKNOWN = 0, FLASH_8M = 1, FLASH_16M = 2 };   /* size verdict */
enum { FLASH_EMPTY = 0, FLASH_USED = 1 };                   /* area verdict */

typedef struct {
    uint8_t  running;                 /* a run is in progress */
    uint8_t  done;                    /* a run has completed; results below (cleared by flash_take) */
    uint8_t  have;                    /* a run has completed this session: the readings are final */
    uint8_t  size, area1, area2;      /* results */
    uint8_t  phase;                   /* internal: which test is running */
    uint8_t  pair;                    /* internal: reference pair 0 (R1) / 1 (R2) */
    uint8_t  half;                    /* internal: 0 = the reference piece is next, 1 = the upper one */
    uint8_t  ref_blank, upper_blank, identical;   /* internal: the current pair so far */
    uint8_t  any_8m, all_16m, usable; /* internal: verdict accumulation over the pairs */
    uint8_t  blank;                   /* internal: the area scanned so far */
    uint16_t piece;                   /* internal: piece index within the reference block */
    uint32_t off, end;                /* internal: the area scan's next offset and last byte */
    uint8_t  buf[FLASH_AREA_PIECE];   /* a reference piece in the first half, the piece 8 MB higher in the second; a whole area piece */
} flash_t;

void flash_init(flash_t *f);
int  flash_start(flash_t *f);        /* 1 = a run started; 0 = one is in progress, or has completed this session */
void flash_tick(flash_t *f);         /* one piece per tick while running; nothing otherwise */
int  flash_take(flash_t *f);         /* 1 once per completed run: size / area1 / area2 are valid */
uint32_t flash_pos(const flash_t *f); /* the offset the next piece is read from (the progress readout) */

#endif
