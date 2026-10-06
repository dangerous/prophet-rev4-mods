/* HOLD while the arp is on (docs/SPEC.md): the synth's own hold is suspended while the arp
 * is enabled, so arp steps release normally; HOLD stays the arp's latch. Portable logic. */
#ifndef VHOLD_H
#define VHOLD_H

#include <stdint.h>

typedef struct {
    uint8_t prev_enabled;     /* arp enabled state seen at the previous tick (0 at boot) */
    uint8_t pad[3];
} vhold_t;

void vhold_init(vhold_t *s);
/* Answer for stock note_off's "is HOLD active?": 0 while the arp is enabled, else stock's. */
int  vhold_query(int enabled, int stock_hold);
/* Hold handler: post the voice-engine hold message? Only while the arp is off. */
int  vhold_post_on_hold_change(int enabled);
/* Per tick. Returns the hold state (0/1) to post to the voice engine when the arp's enabled
 * state changed while HOLD is active (off on enable, on on disable), else -1. */
int  vhold_tick(vhold_t *s, int enabled, int stock_hold);

#endif
