/* Note value — the arp/seq step length (docs/SPEC.md: "Note value"). Portable logic. */
#ifndef RATE_H
#define RATE_H

#include <stdint.h>

#define RATE_COUNT 10
#define RATE_DEFAULT_INDEX 3          /* 8th (index 0 = Half, the longest) */

typedef struct {
    int8_t  delta;     /* index - RATE_DEFAULT_INDEX, so zero RAM means 8th */
    int8_t  pad[3];
} rate_t;

void rate_init(rate_t *r);
int  rate_index(const rate_t *r);
/* dir -1 = longer (Program 7, -), +1 = shorter (Program 8, +); returns 1 if the value
 * changed (ends do not wrap) */
int  rate_step(rate_t *r, int dir);
/* select by index 0..RATE_COUNT-1; returns 1 if valid */
int  rate_set_index(rate_t *r, int i);
/* beats per step as a fraction (8th -> 1/2; for a swing value, per pair of steps);
 * 24 * num / den is the MIDI-clock step (pair) */
void rate_beats(const rate_t *r, int *num, int *den);
/* three display character codes, right-aligned */
void rate_display(const rate_t *r, uint8_t out[3]);
/* 1 for a swing value (8th S, 16th S): rate_beats is then the length of a pair of steps */
int  rate_swing(const rate_t *r);

#endif
