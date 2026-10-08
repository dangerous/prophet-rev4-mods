#include "oct.h"
#include "platform.h"

enum { OB_MOD = OCT_MOD_ID, OB_BANK = 0x28, OB_GROUP = 0x20, OB_PRESS = 1, OB_RELEASE = 2, OB_REPEAT = 3 };
#define OCT_MAX 2
#define KEY_DROPPED 0x7F

void oct_init(oct_t *o)
{
    uint8_t *p = (uint8_t *)o;
    for (unsigned i = 0; i < sizeof *o; i++)
        p[i] = 0;
}

int oct_shift(const oct_t *o)
{
    return o->shift;
}

static void show(const oct_t *o)
{
    plat_display_int(o->shift);
    plat_display_hold();
}

int oct_button(oct_t *o, int id, int value)
{
    if (id == OB_MOD) {
        if (value == OB_PRESS) {
            o->mod_held = 1;
            o->used = 0;
            o->bank_down = o->group_down = 0;
        } else if (value == OB_REPEAT && o->mod_held) {
            o->used = 1;                    /* held, not tapped: no replay on release ... */
            show(o);                        /* ... and the current shift, changed or not */
        } else if (value == OB_RELEASE) {
            int tap = o->mod_held && !o->used;
            o->mod_held = 0;
            if (tap)
                return OCT_REPLAY_TAP;      /* plain tap: stock gets its press+release now */
        }
        return OCT_CONSUMED;                /* presses, repeats and modifier releases: ours */
    }
    if (!o->mod_held || (id != OB_BANK && id != OB_GROUP))
        return OCT_FORWARD;
    if (value == OB_PRESS) {
        int other_down = id == OB_BANK ? o->group_down : o->bank_down;
        o->used = 1;
        if (id == OB_BANK)
            o->bank_down = 1;
        else
            o->group_down = 1;
        if (other_down)
            o->shift = 0;                   /* both together: reset */
        else if (id == OB_BANK && o->shift < OCT_MAX)
            o->shift++;
        else if (id == OB_GROUP && o->shift > -OCT_MAX)
            o->shift--;
        show(o);
    } else if (value == OB_RELEASE) {
        if (id == OB_BANK)
            o->bank_down = 0;
        else
            o->group_down = 0;
    }
    return OCT_CONSUMED;
}

int oct_map_key(oct_t *o, int note, int on)
{
    int shift;
    if (note < 0 || note > 127)
        return -1;
    if (on) {
        int n = note + 12 * o->shift;
        if (n < 0 || n > 127) {
            o->key_shift[note] = KEY_DROPPED;
            return -1;
        }
        o->key_shift[note] = (uint8_t)(o->shift + 3);
        return n;
    }
    if (o->key_shift[note] == KEY_DROPPED)
        return -1;
    shift = o->key_shift[note] ? o->key_shift[note] - 3 : o->shift;
    return note + 12 * shift;
}
