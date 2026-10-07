#include "disp.h"

void disp_init(disp_t *d)
{
    d->ticks = 0;
    d->pad[0] = d->pad[1] = 0;
}

void disp_touch(disp_t *d)
{
    d->ticks = DISP_TICKS;
}

void disp_cancel(disp_t *d)
{
    d->ticks = 0;
}

int disp_active(const disp_t *d)
{
    return d->ticks != 0;
}

int disp_tick(disp_t *d)
{
    if (d->ticks == 0)
        return 0;
    return --d->ticks == 0;
}
