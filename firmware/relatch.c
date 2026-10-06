#include "relatch.h"

static int keys_down(const relatch_t *s)
{
    int n = 0;
    for (int i = 0; i < 2; i++) {
        for (int w = 0; w < 4; w++) {
            uint32_t v = s->keys[i][w];
            while (v) {
                v &= v - 1;
                n++;
            }
        }
    }
    return n;
}

void relatch_init(relatch_t *s)
{
    uint8_t *p = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof *s; i++)
        p[i] = 0;
}

void relatch_hold(relatch_t *s, int on)
{
    s->hold = on ? 1 : 0;
    /* HOLD off: the arp drops every released note; only keys still down remain. */
    if (!s->hold && keys_down(s) == 0)
        s->latched = 0;
}

int relatch_note(relatch_t *s, int arp_enabled, int src, int note, int vel)
{
    int action = RELATCH_FORWARD;
    uint32_t *word;
    uint32_t bit;

    if (note < 0 || note > 127)
        return RELATCH_FORWARD;
    word = &s->keys[src == RELATCH_SRC_MIDI ? 1 : 0][note >> 5];
    bit = 1u << (note & 31);

    if (!arp_enabled)
        s->latched = 0;             /* a disabled arp holds nothing */

    if (vel > 0) {
        if (arp_enabled && s->hold && s->latched && keys_down(s) == 0)
            action = RELATCH_CLEAR_THEN_FORWARD;
        *word |= bit;
        if (arp_enabled)
            s->latched = 1;
    } else {
        *word &= ~bit;
        if (!s->hold && keys_down(s) == 0)
            s->latched = 0;
    }
    return action;
}
