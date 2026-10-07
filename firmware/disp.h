/* Display messages (docs/SPEC.md): every message the UI shows reverts to the stock patch
 * display DISP_TICKS ticks (1 ms each) after the last one. Portable logic. */
#ifndef DISP_H
#define DISP_H

#include <stdint.h>

enum { DISP_TICKS = 1500 };

typedef struct {
    uint16_t ticks;           /* remaining until the stock display is restored; 0 = idle */
    uint8_t  pad[2];
} disp_t;

void disp_init(disp_t *d);
void disp_touch(disp_t *d);                       /* a message was shown: (re)start the timer */
void disp_cancel(disp_t *d);                      /* drop a pending revert (the display was taken over) */
int  disp_active(const disp_t *d);                /* timer running */
int  disp_tick(disp_t *d);                        /* 1 exactly when the timer expires */

#endif
