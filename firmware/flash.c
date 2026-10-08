/* Read-only flash diagnostic (docs/SPEC.md: "Flash diagnostic") — see flash.h.
 *
 * Phases: SIZE — two reference blocks (R1 the bootloader, R2 factory programs) compared piece
 * by piece with the blocks 8 MB higher: identical ⇒ the 24-bit address wrapped ⇒ an 8 MB
 * part; all 0xFF above ⇒ compatible with 16 MB (nothing of stock's lives above 8 MB);
 * anything else ⇒ unknown. Then AREA1 and AREA2: every byte 0xFF ⇒ empty. One read of at
 * most FLASH_PIECE bytes per tick; an unreadable piece counts as not blank and not identical
 * (conservative), so a run always completes. No divide: the piece counts are constants. */
#include "flash.h"
#include "platform.h"

enum { PH_IDLE = 0, PH_SIZE = 1, PH_AREA1 = 2, PH_AREA2 = 3 };
enum { PIECES_PER_BLOCK = FLASH_BLOCK / FLASH_PIECE, PAIRS = 2 };

static int all_ff(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i] != 0xFF)
            return 0;
    return 1;
}

static int same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

void flash_init(flash_t *f)
{
    uint8_t *p = (uint8_t *)f;
    for (unsigned i = 0; i < sizeof *f; i++)
        p[i] = 0;
}

static void begin_pair(flash_t *f)
{
    f->piece = 0;
    f->half = 0;
    f->ref_blank = f->upper_blank = f->identical = 1;
}

int flash_start(flash_t *f)
{
    if (f->running)
        return 0;
    f->running = 1;
    f->done = 0;
    f->phase = PH_SIZE;
    f->pair = 0;
    f->any_8m = 0;
    f->all_16m = 1;
    f->usable = 0;
    begin_pair(f);
    return 1;
}

static void begin_area(flash_t *f, int phase)
{
    f->phase = (uint8_t)phase;
    f->blank = 1;
    if (phase == PH_AREA1) {
        f->off = FLASH_AREA1_LO;
        f->end = FLASH_AREA1_HI;
    } else {
        f->off = FLASH_AREA2_LO;
        f->end = f->size == FLASH_16M ? FLASH_END_16M : FLASH_END_8M;   /* unknown: as 8 MB */
    }
}

static void size_tick(flash_t *f)
{
    uint32_t off = (f->pair ? FLASH_R2 : FLASH_R1) + (uint32_t)f->piece * FLASH_PIECE;
    if (f->half == 0) {                                    /* the reference piece */
        if (plat_flash_read(off, f->ref, FLASH_PIECE) != 0)
            f->ref_blank = f->identical = f->upper_blank = 0;
        else if (!all_ff(f->ref, FLASH_PIECE))
            f->ref_blank = 0;
        f->half = 1;
        return;
    }
    if (plat_flash_read(off + FLASH_UPPER, f->upper, FLASH_PIECE) != 0) {   /* the piece 8 MB higher */
        f->ref_blank = f->identical = f->upper_blank = 0;
    } else {
        if (!all_ff(f->upper, FLASH_PIECE))
            f->upper_blank = 0;
        if (!same(f->ref, f->upper, FLASH_PIECE))
            f->identical = 0;
    }
    f->half = 0;
    if (++f->piece < PIECES_PER_BLOCK)
        return;
    if (!f->ref_blank) {                                   /* a usable pair: it has a say */
        f->usable = 1;
        if (f->identical)
            f->any_8m = 1;
        else if (!f->upper_blank)
            f->all_16m = 0;                                /* neither identical nor blank */
    }
    if (++f->pair < PAIRS) {
        begin_pair(f);
        return;
    }
    if (f->any_8m)
        f->size = FLASH_8M;
    else if (f->usable && f->all_16m)
        f->size = FLASH_16M;
    else
        f->size = FLASH_UNKNOWN;
    begin_area(f, PH_AREA1);
}

static void area_tick(flash_t *f)
{
    uint32_t len = f->end - f->off + 1;
    if (len > FLASH_PIECE)
        len = FLASH_PIECE;
    if (plat_flash_read(f->off, f->upper, len) != 0 || !all_ff(f->upper, len))
        f->blank = 0;
    f->off += len;
    if (f->off <= f->end)
        return;
    if (f->phase == PH_AREA1) {
        f->area1 = f->blank ? FLASH_EMPTY : FLASH_USED;
        begin_area(f, PH_AREA2);
    } else {
        f->area2 = f->blank ? FLASH_EMPTY : FLASH_USED;
        f->phase = PH_IDLE;
        f->running = 0;
        f->done = 1;
    }
}

void flash_tick(flash_t *f)
{
    if (!f->running)
        return;
    if (f->phase == PH_SIZE)
        size_tick(f);
    else
        area_tick(f);
}

int flash_take(flash_t *f)
{
    if (!f->done)
        return 0;
    f->done = 0;
    return 1;
}
