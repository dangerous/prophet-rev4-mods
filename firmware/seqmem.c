/* Sequence memory — see seqmem.h for the block format. Saving streams the block through a
 * 4 KB sector buffer, one verified write per sector the stream needs. Loading validates the
 * whole block first (nothing of the live sequence changes for an invalid, foreign or
 * sequence-less block) and decodes it on a second pass, both through a 1 KB piece buffer.
 * No libc, no divide by a variable. */
#include "seqmem.h"
#include "platform.h"

enum { FLAG_SEQUENCE = 1 };

/* --- slots and checksum ---------------------------------------------------------------- */
int seqmem_slot(int factory, int bank, int group, int prog)
{
    if (factory || bank < 0 || bank > 4 || group < 0 || group > 4 || prog < 0 || prog > 7)
        return -1;
    return (5 * bank + group) * 8 + prog;
}

uint16_t seqmem_checksum(void)
{
    uint32_t sum = 0;
    for (int p = 0; p < SEQMEM_PARAMS; p++)
        sum += (uint32_t)plat_param_read(p) & 0xFFFFu;
    return (uint16_t)sum;
}

/* --- encoding ---------------------------------------------------------------------------- */
typedef struct { uint8_t *out; uint32_t off, len, pos; } win_t;   /* a window of the stream */

static void put(win_t *w, uint8_t b)
{
    if (w->pos >= w->off && w->pos - w->off < w->len)
        w->out[w->pos - w->off] = b;
    w->pos++;
}

static void put16(win_t *w, uint16_t v)
{
    put(w, (uint8_t)(v & 0xFF));
    put(w, (uint8_t)(v >> 8));
}

static uint32_t stream_len(const seq_t *q)
{
    uint32_t n = SEQMEM_HEADER;
    for (int i = 0; i < q->len; i++)
        n += 3u + 2u * q->ev[i].n;
    return n;
}

uint32_t seqmem_encode(const seq_t *q, const seqmem_settings_t *s, int slot, uint32_t off,
                       uint8_t *out, uint32_t out_len)
{
    win_t w = { out, off, out_len, 0 };
    uint32_t total = stream_len(q);
    for (uint32_t i = 0; i < out_len; i++)
        out[i] = 0;
    put(&w, 'P'); put(&w, 'S'); put(&w, 'Q'); put(&w, '2');
    put16(&w, (uint16_t)total);
    put(&w, q->len ? FLAG_SEQUENCE : 0);
    put(&w, (uint8_t)slot);
    put16(&w, seqmem_checksum());
    put(&w, s->rate_code);
    put(&w, s->style);
    put(&w, s->order);
    put(&w, s->chord_beats);
    put(&w, (uint8_t)s->transpose);
    put(&w, s->gen);
    put(&w, s->gate);
    for (int i = 0; i < q->len; i++) {
        const seq_ev_t *e = &q->ev[i];
        put16(&w, e->dur);
        put(&w, e->n);
        for (int k = 0; k < e->n; k++) {
            put(&w, e->note[k]);
            put(&w, e->vel[k]);
        }
    }
    return w.pos;
}

int seqmem_save(int slot, const seq_t *q, const seqmem_settings_t *s, uint8_t *sector_buf)
{
    uint32_t base, total, sectors;
    if (slot < 0 || (uint32_t)slot >= SEQMEM_SLOTS)
        return 1;
    base = SEQMEM_BASE + (uint32_t)slot * SEQMEM_BLOCK;
    total = stream_len(q);
    if (total > SEQMEM_BLOCK)
        return 2;
    sectors = (total + SEQMEM_SECTOR - 1) / SEQMEM_SECTOR;   /* a power of two: a shift */
    for (uint32_t k = 0; k < sectors; k++) {
        int r;
        seqmem_encode(q, s, slot, k * SEQMEM_SECTOR, sector_buf, SEQMEM_SECTOR);
        r = plat_flash_write(base + k * SEQMEM_SECTOR, sector_buf, SEQMEM_SECTOR);
        if (r != 0)
            return r;
    }
    return 0;
}

/* --- decoding ---------------------------------------------------------------------------- */
typedef struct {                                           /* a byte reader over the block, piece by piece */
    uint32_t base, len, pos;                               /* block base, stream length, next byte */
    uint32_t pbase, plen;                                  /* the piece in buf: [pbase, pbase + plen) */
    uint8_t *buf;
    int err;
} rd_t;

static int rd_byte(rd_t *r)
{
    if (r->err || r->pos >= r->len) {
        r->err = 1;
        return 0;
    }
    if (r->pos < r->pbase || r->pos >= r->pbase + r->plen) {
        uint32_t n = r->len - r->pos;
        if (n > SEQMEM_PIECE)
            n = SEQMEM_PIECE;
        if (plat_flash_read(r->base + r->pos, r->buf, n) != 0) {
            r->err = 1;
            return 0;
        }
        r->pbase = r->pos;
        r->plen = n;
    }
    return r->buf[r->pos++ - r->pbase];
}

static int rd_16(rd_t *r)
{
    int lo = rd_byte(r), hi = rd_byte(r);
    return lo | hi << 8;
}

/* one pass over the events: validate (q == 0) or decode into q. Returns the event count, -1 bad. */
static int walk_events(rd_t *r, seq_t *q)
{
    int events = 0, total = 0;
    r->pos = SEQMEM_HEADER;
    r->pbase = r->plen = 0;
    r->err = 0;
    while (r->pos < r->len) {
        int dur = rd_16(r), n = rd_byte(r);
        if (r->err || dur < 1 || n > SEQ_CHORD || events >= SEQ_STEPS)
            return -1;
        total += dur;
        if (total > SEQ_STEPS)
            return -1;
        if (q) {
            q->ev[events].dur = (uint16_t)dur;
            q->ev[events].n = (uint8_t)n;
            q->ev[events].pad = 0;
        }
        for (int k = 0; k < n; k++) {
            int note = rd_byte(r), vel = rd_byte(r);
            if (r->err || note > 127 || vel > 127)
                return -1;
            if (q) {
                q->ev[events].note[k] = (uint8_t)note;
                q->ev[events].vel[k] = (uint8_t)vel;
            }
        }
        events++;
    }
    if (r->err || events == 0)
        return -1;
    if (q) {
        q->len = (uint16_t)events;
        q->total = (uint16_t)total;
    }
    return events;
}

int seqmem_load(int slot, seq_t *q, seqmem_settings_t *s, uint8_t *piece_buf)
{
    rd_t r;
    uint8_t *h = piece_buf;
    uint32_t len;
    if (slot < 0 || (uint32_t)slot >= SEQMEM_SLOTS)
        return SEQMEM_NONE;
    r.base = SEQMEM_BASE + (uint32_t)slot * SEQMEM_BLOCK;
    r.buf = piece_buf;
    if (plat_flash_read(r.base, h, SEQMEM_HEADER) != 0)
        return SEQMEM_NONE;
    if (h[0] != 'P' || h[1] != 'S' || h[2] != 'Q' || h[3] != '2')
        return SEQMEM_NONE;
    len = (uint32_t)h[4] | (uint32_t)h[5] << 8;
    if (len <= SEQMEM_HEADER || len > SEQMEM_BLOCK || h[7] != (uint8_t)slot)
        return SEQMEM_NONE;
    if (((uint32_t)h[8] | (uint32_t)h[9] << 8) != seqmem_checksum())
        return SEQMEM_NONE;                                /* saved with another program's sound */
    if (!(h[6] & FLAG_SEQUENCE))
        return SEQMEM_NONE;                                /* "no sequence": the live one stays */
    if (h[16] >= ARP_GATES)
        return SEQMEM_NONE;
    s->rate_code = h[10];
    s->style = h[11];
    s->order = h[12];
    s->chord_beats = h[13];
    s->transpose = (int8_t)h[14];
    s->gen = h[15];
    s->gate = h[16];
    r.len = len;
    if (walk_events(&r, 0) < 0)                            /* validate everything first */
        return SEQMEM_NONE;
    if (walk_events(&r, q) < 0)                            /* then decode (the same bytes) */
        return SEQMEM_NONE;
    return SEQMEM_LOADED;
}
