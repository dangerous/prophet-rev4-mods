#include "rate.h"
#include "platform.h"

enum { RB_A440 = 0x0F, RB_GLOBALS = 0x19, RB_GROUP = 0x20, RB_BANK = 0x28,
       RB_PROGRAM7 = 6, RB_PROGRAM8 = 7, RB_PRESS = 1 };
enum { BLANK = 0x25, CH_T = 0x1D, CH_D = 0x0D, CH_B = 0x0B };   /* panel character codes */

/* div, tps: engine fields giving period = 60*tps/(div*bpm) ticks.
 * num/den: arp clocks forwarded per real MIDI clock (12 forwarded = one step). */
static const struct {
    uint8_t div;
    uint16_t tps;
    uint8_t num, den;
    uint8_t bn, bd;        /* beats per step (native engine) */
    uint8_t d[3];
} R[RATE_COUNT] = {
    { 8,  1000, 4,  1,  1, 8, {BLANK, 3, 2} },        /* 1/32   3 clocks  */
    { 12, 1000, 3,  1,  1, 6, {1, 6, CH_T} },         /* 1/16T  4 */
    { 4,  1000, 2,  1,  1, 4, {BLANK, 1, 6} },        /* 1/16   6 */
    { 6,  1000, 3,  2,  1, 3, {BLANK, 8, CH_T} },     /* 1/8T   8 */
    { 8,  3000, 4,  3,  3, 8, {1, 6, CH_D} },         /* 1/16d  9 */
    { 2,  1000, 1,  1,  1, 2, {BLANK, BLANK, 8} },    /* 1/8   12 */
    { 4,  3000, 2,  3,  3, 4, {BLANK, 8, CH_D} },     /* 1/8d  18 */
    { 1,  1000, 1,  2,  1, 1, {BLANK, BLANK, 4} },    /* 1/4   24 */
    { 2,  3000, 1,  3,  3, 2, {BLANK, 4, CH_D} },     /* 1/4d  36 */
    { 1,  2000, 1,  4,  2, 1, {BLANK, BLANK, 2} },    /* 1/2   48 */
    { 1,  4000, 1,  8,  4, 1, {BLANK, BLANK, 1} },    /* 1     96 */
    { 1,  8000, 1, 16,  8, 1, {BLANK, 2, CH_B} },     /* 2 bars 192 */
    { 1, 16000, 1, 32, 16, 1, {BLANK, 4, CH_B} },     /* 4 bars 384 */
};

void rate_init(rate_t *r)
{
    r->delta = 0;
    r->credit = 0;
    r->pad[0] = r->pad[1] = 0;
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
    r->credit = 0;
    return 1;
}

void rate_params(const rate_t *r, int *div, int *tps)
{
    *div = R[rate_index(r)].div;
    *tps = R[rate_index(r)].tps;
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

int rate_clock(rate_t *r, int byte)
{
    int n = 0;
    if (byte == 0xFA) {
        r->credit = 0;
        return 1;
    }
    if (byte != 0xF8)
        return 1;
    /* forwarded(j) = ceil(j * num / den): each real clock adds num, each forward costs den */
    r->credit = (int8_t)(r->credit + R[rate_index(r)].num);
    while (r->credit > 0) {
        r->credit = (int8_t)(r->credit - R[rate_index(r)].den);
        n++;
    }
    return n;
}

static void show_rate(const rate_t *r)
{
    uint8_t d[3];
    rate_display(r, d);
    plat_display3(d[0], d[1], d[2]);
    plat_display_hold();
}

int rate_button(rate_t *r, int a440_held, int id, int value)
{
    if (!a440_held)
        return 0;
    if (id == RB_PROGRAM7 || id == RB_PROGRAM8) {
        if (value == RB_PRESS) {
            if (rate_step(r, id == RB_PROGRAM7 ? 1 : -1))      /* 7 = longer (-), 8 = shorter (+) */
                plat_engine_reset_acc();
            show_rate(r);
        }
        return 0;                     /* V5 sees the press and marks A440 as used */
    }
    if (id < 8 || id == RB_A440 || id == RB_GLOBALS || id == RB_GROUP || id == RB_BANK)
        return 0;
    /* an unassigned button while A440 is held: show its id (readout) */
    if (value == RB_PRESS) {
        plat_display_int(id);
        plat_display_hold();
        plat_a440_mark_used();
    }
    return 1;
}
