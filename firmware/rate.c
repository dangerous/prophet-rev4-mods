#include "rate.h"

enum { BLANK = 0x25, CH_T = 0x1D, CH_D = 0x0D, CH_B = 0x0B };   /* panel character codes */

/* beats per step (bn/bd) and the display text; 24*bn/bd = MIDI clocks per step */
static const struct {
    uint8_t bn, bd;
    uint8_t d[3];
} R[RATE_COUNT] = {
    {  1, 8, {BLANK, 3, 2} },        /* 1/32    3 clocks */
    {  1, 6, {1, 6, CH_T} },         /* 1/16T   4 */
    {  1, 4, {BLANK, 1, 6} },        /* 1/16    6 */
    {  1, 3, {BLANK, 8, CH_T} },     /* 1/8T    8 */
    {  3, 8, {1, 6, CH_D} },         /* 1/16d   9 */
    {  1, 2, {BLANK, BLANK, 8} },    /* 1/8    12 */
    {  3, 4, {BLANK, 8, CH_D} },     /* 1/8d   18 */
    {  1, 1, {BLANK, BLANK, 4} },    /* 1/4    24 */
    {  3, 2, {BLANK, 4, CH_D} },     /* 1/4d   36 */
    {  2, 1, {BLANK, BLANK, 2} },    /* 1/2    48 */
    {  4, 1, {BLANK, BLANK, 1} },    /* 1      96 */
    {  8, 1, {BLANK, 2, CH_B} },     /* 2 bars 192 */
    { 16, 1, {BLANK, 4, CH_B} },     /* 4 bars 384 */
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
    int i = rate_index(r) + (dir < 0 ? -1 : 1);
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
