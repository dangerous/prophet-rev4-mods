/* Keyboard octave shift (docs/SPEC.md: "Keyboard octave shift"). Portable logic; the
 * glue's hooks call these and act on the return values. */
#ifndef OCT_H
#define OCT_H

#include <stdint.h>

enum { OCT_FORWARD = 0, OCT_CONSUMED = 1, OCT_REPLAY_TAP = 2 };
#define OCT_MOD_ID 37             /* modifier button: Osc B Lo Freq (was filter Keyboard Amount, 8) */

typedef struct {
    int8_t  shift;            /* -2..2 octaves */
    uint8_t mod_held;         /* the modifier (Lo Freq, id 37) is down */
    uint8_t used;             /* Bank/Group pressed, or the panel reported it held: not a tap */
    uint8_t bank_down;
    uint8_t group_down;
    uint8_t pad[3];
    uint8_t key_shift[128];   /* per key: 0 = never pressed, 1..5 = shift+3 at press, 0x7F = dropped */
} oct_t;

void oct_init(oct_t *o);
int  oct_shift(const oct_t *o);
/* Panel button event. OCT_REPLAY_TAP means: consumed, and the caller must replay a
 * press+release of the modifier button to the stock so a plain tap keeps its function. A
 * held-repeat of the modifier (the panel's hold detection) shows the current shift and
 * makes the hold no tap. */
int  oct_button(oct_t *o, int id, int value);
/* Map a local key to the note to play/send. on != 0 records the shift for that key;
 * on == 0 uses the shift recorded at the key's press. Returns -1 if the note is silent. */
int  oct_map_key(oct_t *o, int note, int on);

#endif
