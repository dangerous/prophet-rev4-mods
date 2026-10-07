/* Note value — the arp/seq step length (docs/SPEC.md: "Note value"). Portable logic. */
#ifndef RATE_H
#define RATE_H

#include <stdint.h>

#define RATE_COUNT 13
#define RATE_DEFAULT_INDEX 5          /* 1/8 */

typedef struct {
    int8_t  delta;     /* index - RATE_DEFAULT_INDEX, so zero RAM means 1/8 */
    int8_t  pad[3];
} rate_t;

void rate_init(rate_t *r);
int  rate_index(const rate_t *r);
/* dir -1 = shorter, +1 = longer; returns 1 if the value changed (ends do not wrap) */
int  rate_step(rate_t *r, int dir);
/* beats per step as a fraction (1/8 note -> 1/2); 24 * num / den is the MIDI-clock step */
void rate_beats(const rate_t *r, int *num, int *den);
/* three display character codes, right-aligned */
void rate_display(const rate_t *r, uint8_t out[3]);

#endif
