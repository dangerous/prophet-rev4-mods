/* Note value — the arp/seq step length (docs/SPEC.md: "Note value"). Portable logic. */
#ifndef RATE_H
#define RATE_H

#include <stdint.h>

#define RATE_COUNT 15
#define RATE_DEFAULT_INDEX 6          /* 1/8 */

typedef struct {
    int8_t  delta;     /* index - RATE_DEFAULT_INDEX, so zero RAM means 1/8 */
    int8_t  pad[3];
} rate_t;

void rate_init(rate_t *r);
int  rate_index(const rate_t *r);
/* dir -1 = shorter, +1 = longer; returns 1 if the value changed (ends do not wrap) */
int  rate_step(rate_t *r, int dir);
/* select by index 0..RATE_COUNT-1; returns 1 if valid */
int  rate_set_index(rate_t *r, int i);
/* beats per step as a fraction (1/8 note -> 1/2; for a swing value, per pair of steps);
 * 24 * num / den is the MIDI-clock step (pair) */
void rate_beats(const rate_t *r, int *num, int *den);
/* three display character codes, right-aligned */
void rate_display(const rate_t *r, uint8_t out[3]);
/* 1 for a swing value (16S, 8S): rate_beats is then the length of a pair of steps */
int  rate_swing(const rate_t *r);
/* fixed patch-memory code of the value at index i ("Patch memory"), -1 if out of range */
int  rate_code(int i);
/* index of the value with patch-memory code c, -1 if none */
int  rate_index_from_code(int c);

#endif
