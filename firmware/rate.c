#include "rate.h"

enum { RC_BLANK = 0x25, RC_T = 0x1D, RC_D = 0x0D, RC_S = 0x1C };   /* panel character codes */

/* The Prophet-6's list in its panel order, longest first; rate_step +1 (Program 8) moves down
 * the list. bn/bd = beats per step, or per pair of steps for a swing value (sw); 24*bn/bd =
 * MIDI clocks per step (pair). code = the fixed patch-memory code ("Patch memory"). */
static const struct {
    uint8_t bn, bd, sw, code;
    uint8_t d[3];
} R[RATE_COUNT] = {
    {  2, 1, 0,  9, {RC_BLANK, RC_BLANK, 2} }, /* Half   48 clocks */
    {  1, 1, 0,  7, {RC_BLANK, RC_BLANK, 4} }, /* Qtr    24 */
    {  3, 4, 0,  6, {RC_BLANK, 8, RC_D} },     /* 8th D  18 */
    {  1, 2, 0,  5, {RC_BLANK, RC_BLANK, 8} }, /* 8th    12 */
    {  1, 1, 1, 14, {RC_BLANK, 8, RC_S} },     /* 8th S  16 + 8 */
    {  1, 3, 0,  3, {RC_BLANK, 8, RC_T} },     /* 8th T   8 */
    {  1, 4, 0,  2, {RC_BLANK, 1, 6} },        /* 16th    6 */
    {  1, 2, 1, 13, {1, 6, RC_S} },            /* 16th S  8 + 4 */
    {  1, 6, 0,  1, {1, 6, RC_T} },            /* 16th T  4 */
    {  1, 8, 0,  0, {RC_BLANK, 3, 2} },        /* 32nd    3 */
};

/* codes of values removed from the list, read only: 1/16d, 1/4d, whole, 2 bars, 4 bars */
static const struct { uint8_t code, index; } LEGACY[] = {
    {  4, 6 },                                 /* 1/16d  -> 16th */
    {  8, 1 },                                 /* 1/4d   -> Qtr */
    { 10, 0 }, { 11, 0 }, { 12, 0 },           /* whole, 2 bars, 4 bars -> Half */
};

void rate_init(rate_t *r)
{
    r->delta = 0;
    r->pad[0] = r->pad[1] = r->pad[2] = 0;
}

int rate_index(const rate_t *r)
{
    return RATE_DEFAULT_INDEX + r->delta;
}

int rate_step(rate_t *r, int dir)
{
    int i = rate_index(r) + (dir < 0 ? -1 : 1);            /* -1 = longer, +1 = shorter */
    if (i < 0 || i >= RATE_COUNT)
        return 0;
    r->delta = (int8_t)(i - RATE_DEFAULT_INDEX);
    return 1;
}

int rate_set_index(rate_t *r, int i)
{
    if (i < 0 || i >= RATE_COUNT)
        return 0;
    r->delta = (int8_t)(i - RATE_DEFAULT_INDEX);
    return 1;
}

void rate_beats(const rate_t *r, int *num, int *den)
{
    *num = R[rate_index(r)].bn;
    *den = R[rate_index(r)].bd;
}

void rate_display(const rate_t *r, uint8_t out[3])
{
    const uint8_t *d = R[rate_index(r)].d;
    out[0] = d[0];
    out[1] = d[1];
    out[2] = d[2];
}

int rate_swing(const rate_t *r)
{
    return R[rate_index(r)].sw;
}

int rate_code(int i)
{
    return (i < 0 || i >= RATE_COUNT) ? -1 : R[i].code;
}

int rate_index_from_code(int c)
{
    for (int i = 0; i < RATE_COUNT; i++)
        if (R[i].code == c)
            return i;
    for (unsigned k = 0; k < sizeof LEGACY / sizeof *LEGACY; k++)
        if (LEGACY[k].code == c)
            return LEGACY[k].index;
    return -1;
}
