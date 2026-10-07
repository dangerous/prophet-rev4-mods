/* Platform calls the portable logic makes into stock. Implemented by firmware/native.c on
 * the instrument and faked by the host harnesses. */
#ifndef PLATFORM_H
#define PLATFORM_H

void plat_display_int(int value);                 /* stock 3-digit integer display */
void plat_display3(int c0, int c1, int c2);       /* stock 3-character display (panel codes) */
void plat_display_hold(void);                     /* a message was shown: (re)start the 1.5 s revert */
void plat_voice_on(int src, int note, int vel);   /* stock voice allocator: note_on(src, note, vel) */
void plat_voice_off(int src, int note);           /* stock note_off(src, note) */
void plat_led(int led, int on);                   /* stock panel LED setter */
void plat_display_restore(void);                  /* stock: redraw the patch display */
int  plat_globals_open(void);                     /* stock Globals menu is open */
int  plat_a440_down(void);                        /* stock button-held table: A440 is down */

#endif
