/* Display messages (docs/SPEC.md): every message — the wrapper's own, and the arp's, which
 * are detected as a change of a displayed setting — reverts to the stock patch display
 * DISP_TICKS ticks (1 ms each) after the last change. Portable logic. */
#ifndef DISP_H
#define DISP_H

#include <stdint.h>

enum { DISP_TICKS = 1500 };

typedef struct {
    uint16_t ticks;           /* remaining until the stock display is restored; 0 = idle */
    uint16_t bpm;             /* shadows of the arp's displayed settings */
    uint8_t  enabled, mode, octaves, ext;
    uint8_t  primed;          /* shadows hold real values (first observe only records) */
    uint8_t  pad[3];
} disp_t;

void disp_init(disp_t *d);
void disp_touch(disp_t *d);                       /* a message was shown: (re)start the timer */
void disp_observe(disp_t *d, int enabled, int mode, int octaves, int ext, int bpm);
int  disp_active(const disp_t *d);                /* timer running */
int  disp_tick(disp_t *d);                        /* 1 exactly when the timer expires */

#endif
