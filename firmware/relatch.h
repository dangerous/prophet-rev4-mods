/* Re-latch under HOLD — portable logic (docs/SPEC.md: "Re-latch under HOLD").
 *
 * The wrapper's hooks feed every local-key and MIDI note event through relatch_note() and
 * every HOLD state change through relatch_hold(). relatch_note() answers whether the arp
 * must be cleared before the event is forwarded. No I/O, no libc, no allocation: the state
 * lives in a fixed RAM block that is zero after every boot. */
#ifndef RELATCH_H
#define RELATCH_H

#include <stdint.h>

enum { RELATCH_FORWARD = 0, RELATCH_CLEAR_THEN_FORWARD = 1 };
enum { RELATCH_SRC_LOCAL = 1, RELATCH_SRC_MIDI = 2 };

typedef struct {
    uint32_t keys[2][4];   /* keys currently down, one 128-bit map per source */
    uint8_t  hold;         /* HOLD active (button or HLd pedal) */
    uint8_t  latched;      /* the arp's note set may be non-empty */
    uint8_t  pad[2];
} relatch_t;

void relatch_init(relatch_t *s);
void relatch_hold(relatch_t *s, int on);
/* src: RELATCH_SRC_*; vel 0 = release. Returns RELATCH_FORWARD or RELATCH_CLEAR_THEN_FORWARD. */
int  relatch_note(relatch_t *s, int arp_enabled, int src, int note, int vel);

#endif
