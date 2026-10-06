#include "disp.h"

void disp_init(disp_t *d)
{
    uint8_t *p = (uint8_t *)d;
    for (unsigned i = 0; i < sizeof *d; i++)
        p[i] = 0;
}

void disp_touch(disp_t *d)
{
    d->ticks = DISP_TICKS;
}

static void record(disp_t *d, int enabled, int mode, int octaves, int ext, int bpm)
{
    d->enabled = (uint8_t)(enabled != 0);
    d->mode = (uint8_t)mode;
    d->octaves = (uint8_t)octaves;
    d->ext = (uint8_t)(ext != 0);
    d->bpm = (uint16_t)bpm;
}

void disp_observe(disp_t *d, int enabled, int mode, int octaves, int ext, int bpm)
{
    if (!d->primed) {
        d->primed = 1;
        record(d, enabled, mode, octaves, ext, bpm);
        return;
    }
    if (d->enabled != (enabled != 0) || d->mode != (uint8_t)mode || d->octaves != (uint8_t)octaves
        || d->ext != (ext != 0) || d->bpm != (uint16_t)bpm) {
        record(d, enabled, mode, octaves, ext, bpm);
        disp_touch(d);
    }
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
