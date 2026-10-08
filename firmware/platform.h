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
int  plat_param_read(int param);                  /* stock live program parameter, layer A */
int  plat_tone_on(void);                          /* stock's A440 reference tone is sounding (ui + 0x16A) */
void plat_stock_a440_press(void);                 /* replay an A440 press to stock: toggles its tone (HOLD up) */
int  plat_hold_latch(void);                       /* stock's HOLD button latch (ui + 0x19C) */
void plat_stock_hold_press(void);                 /* replay a HOLD press to stock: toggles its latch, LED and hold */
int  plat_pedal_down(void);                       /* stock's sustain-pedal state (ui + 0x19D) */
void plat_dsp_hold(int on);                       /* the voice engine's hold message (0x080D0000 | on) */
void plat_release_unheld(void);                   /* stock: release every sounding voice whose key is up */
void plat_param_store(int param, int value);      /* stock plain parameter store, layer A */

#endif
