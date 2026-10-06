#include "vhold.h"

void vhold_init(vhold_t *s)
{
    s->prev_enabled = 0;
    s->pad[0] = s->pad[1] = s->pad[2] = 0;
}

int vhold_query(int enabled, int stock_hold)
{
    return enabled ? 0 : (stock_hold != 0);
}

int vhold_post_on_hold_change(int enabled)
{
    return !enabled;
}

int vhold_tick(vhold_t *s, int enabled, int stock_hold)
{
    int was = s->prev_enabled != 0;
    enabled = enabled != 0;
    s->prev_enabled = (uint8_t)enabled;
    if (was == enabled || !stock_hold)
        return -1;
    return enabled ? 0 : 1;
}
