/* Note value (arp/seq step length) and the button-id readout (docs/SPEC.md: "Note value",
 * "Button id readout"). Portable logic; platform.h for everything external. */
#ifndef RATE_H
#define RATE_H

#include <stdint.h>

#define RATE_COUNT 13
#define RATE_DEFAULT_INDEX 5          /* 1/8 — V5's fixed behaviour */

typedef struct {
    int8_t  delta;     /* index - RATE_DEFAULT_INDEX, so zero RAM means 1/8 */
    int8_t  credit;    /* clock-filter accumulator: real clocks owed minus forwarded */
    uint8_t pad[2];
} rate_t;

void rate_init(rate_t *r);
int  rate_index(const rate_t *r);
/* dir -1 = shorter, +1 = longer; returns 1 if the value changed (ends do not wrap) */
int  rate_step(rate_t *r, int dir);
/* engine fields for the internal clock: divisions per beat and ticks per second */
void rate_params(const rate_t *r, int *div, int *tps);
/* three display character codes, right-aligned */
void rate_display(const rate_t *r, uint8_t out[3]);
/* MIDI realtime byte seen: how many copies to hand the arp (0..4 for F8, 1 otherwise).
 * FA (Start) resets the filter phase. */
int  rate_clock(rate_t *r, int byte);
/* Panel button while A440 is held. Returns 1 if consumed (not forwarded to V5). */
int  rate_button(rate_t *r, int a440_held, int id, int value);

#endif
