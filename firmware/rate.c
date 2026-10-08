#include "rate.h"

enum { RC_BLANK = 0x25, RC_T = 0x1D, RC_D = 0x0D, RC_S = 0x1C, RC_B = 0x0B };   /* panel character codes */

/* Longest first: three long values for pad sequences (4 bars, 2 bars, Whole — `1`, as `1b`
 * would read like `16`), then the Prophet-6's list in its panel order; rate_step +1 (Program 8)
 * moves down the list. bn/bd = beats per step, or per pair of steps for a swing value (sw);
 * 24*bn/bd = MIDI clocks per step (pair). Patch memory stores a code, not the index
 * (rate_code: the Prophet-6 position, or 10-12 for the long values). */
static const struct {
    uint8_t bn, bd, sw;
    uint8_t d[3];
} R[RATE_COUNT] = {
    { 16, 1, 0, {RC_BLANK, 4, RC_B} },     /* 4 bars 384 clocks */
    {  8, 1, 0, {RC_BLANK, 2, RC_B} },     /* 2 bars 192 */
    {  4, 1, 0, {RC_BLANK, RC_BLANK, 1} }, /* Whole   96 */
    {  2, 1, 0, {RC_BLANK, RC_BLANK, 2} }, /* Half   48 clocks */
    {  1, 1, 0, {RC_BLANK, RC_BLANK, 4} }, /* Qtr    24 */
    {  3, 4, 0, {RC_BLANK, 8, RC_D} },     /* 8th D  18 */
    {  1, 2, 0, {RC_BLANK, RC_BLANK, 8} }, /* 8th    12 */
    {  1, 1, 1, {RC_BLANK, 8, RC_S} },     /* 8th S  16 + 8 */
    {  1, 3, 0, {RC_BLANK, 8, RC_T} },     /* 8th T   8 */
    {  1, 4, 0, {RC_BLANK, 1, 6} },        /* 16th    6 */
    {  1, 2, 1, {1, 6, RC_S} },            /* 16th S  8 + 4 */
    {  1, 6, 0, {1, 6, RC_T} },            /* 16th T  4 */
    {  1, 8, 0, {RC_BLANK, 3, 2} },        /* 32nd    3 */
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

#define LONG_VALUES 3                 /* list indices 0..2 */

int rate_code(const rate_t *r)
{
    int i = rate_index(r);
    return i < LONG_VALUES ? 12 - i : i - LONG_VALUES;    /* 4 bars 12, 2 bars 11, Whole 10; Half 0 ... */
}

int rate_set_code(rate_t *r, int code)
{
    if (code < 0 || code > 12)
        return 0;
    return rate_set_index(r, code < 10 ? code + LONG_VALUES : 12 - code);
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
