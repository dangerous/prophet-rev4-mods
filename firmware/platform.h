/* Platform calls the portable logic makes into V5/stock. Implemented by firmware/wrapper.c
 * on the instrument and faked by the host harnesses. */
#ifndef PLATFORM_H
#define PLATFORM_H

int  plat_arp_enabled(void);                      /* V5 engine enabled byte != 0 */
void plat_v5_note(int src, int note, int vel);    /* V5 local (src 1) / MIDI (src 2) note entry */
void plat_v5_clear(void);                         /* V5 all-notes-off hook: stock release + arp clear */
void plat_v5_hold(int on);                        /* V5 hold-event entry: tells the arp hold is on/off */
void plat_v5_button(int id, int value);           /* V5 panel-button hook (1 = press, 2 = release) */
int  plat_v5_octaves(void);                       /* V5 octave setting byte (1..4) */
int  plat_globals_active(void);                   /* V5 "GLOBALS held/active" flag */
void plat_display_int(int value);                 /* stock 3-digit integer display */
void plat_display3(int c0, int c1, int c2);       /* stock 3-character display (panel codes) */
void plat_display_hold(void);                     /* start V5's display timeout (home view after ~1 s) */
void plat_a440_mark_used(void);                   /* V5: this A440 hold was a modifier, not a tap */
void plat_engine_reset_acc(void);                 /* V5 engine: restart the internal step accumulator */
void plat_orig_out(int ctx, int src, int on, int note, int vel);  /* V5's original note output */

/* Native engine (arp.c): the stock voice allocator, note_on(src, note, vel) / note_off(src, note) */
void plat_voice_on(int src, int note, int vel);
void plat_voice_off(int src, int note);

#endif
