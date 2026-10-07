#include "arp.h"
#include "platform.h"

/* Internal clock: every 1 ms tick adds bpm*den to the accumulator; a step is due at
 * 60000*num (= 60000/bpm ms * num/den beats) and the gate closes at half that. The
 * remainder is carried so the average tempo is exact. */
#define FULL(a)  (60000u * (a)->beats_num)
#define HALF(a)  (30000u * (a)->beats_num)

static void zero(void *p, unsigned n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}

void arp_init(arp_t *a)
{
    zero(a, sizeof *a);
    a->octaves = 1;
    a->bpm = 120;
    a->beats_num = 1;
    a->beats_den = 2;
    a->port = ARP_NONE;
    a->sounding = ARP_NONE;
    a->seq_trigger = ARP_NONE;
    a->dir = 1;
    a->at_start = 1;
    a->loss = ARP_LOSS_TICKS;
    a->rng = 0x9E3779B9u;
}

/* --- pool ------------------------------------------------------------------------------ */
static int in_pool(const arp_t *a, int n) { return a->held[n] || a->latched[n]; }

int arp_pool_count(const arp_t *a)
{
    int n = 0;
    for (int i = 0; i < 128; i++)
        n += in_pool(a, i);
    return n;
}

static int held_count(const arp_t *a)
{
    int n = 0;
    for (int i = 0; i < 128; i++)
        n += a->held[i] != 0;
    return n;
}

/* The base order: the recorded sequence transposed onto the trigger key, else the pool
 * ascending by pitch. Entries outside 0..127 are marked 0xFF and skipped when stepping. */
static int base_order(const arp_t *a, uint8_t order[ARP_SEQ_MAX > 128 ? ARP_SEQ_MAX : 128],
                      uint8_t vel[ARP_SEQ_MAX > 128 ? ARP_SEQ_MAX : 128])
{
    int n = 0;
    if (a->seq_len) {
        if (a->seq_trigger == ARP_NONE)
            return 0;
        for (int i = 0; i < a->seq_len; i++) {
            int p = a->seq_note[i] - a->seq_note[0] + a->seq_trigger;
            order[n] = (p < 0 || p > 127) ? 0xFF : (uint8_t)p;
            vel[n++] = a->seq_vel[i];
        }
        return n;
    }
    for (int i = 0; i < 128; i++)
        if (in_pool(a, i)) {
            order[n] = (uint8_t)i;
            vel[n++] = a->held[i] ? a->held[i] : a->latched[i];
        }
    return n;
}

/* --- voices ---------------------------------------------------------------------------- */
static void release(arp_t *a)
{
    if (a->sounding != ARP_NONE) {
        plat_voice_off(ARP_SRC_LOCAL, a->sounding);
        a->sounding = ARP_NONE;
    }
    a->gate_open = 0;
}

static void reset_pattern(arp_t *a)
{
    a->at_start = 1;
    a->dir = 1;
}

/* --- pattern --------------------------------------------------------------------------- */
static uint32_t xorshift(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* Next entry above/below the last one within the current pass. The pool is anchored by
 * pitch (so a changed pool continues from the pitch just played); a sequence by index. */
static int up_next(const arp_t *a, const uint8_t *order, int n, int seq, int *idx)
{
    if (seq) {
        if (*idx + 1 < n) { (*idx)++; return 1; }
        return 0;
    }
    for (int i = 0; i < n; i++)
        if (order[i] > a->last_base) { *idx = i; return 1; }
    return 0;
}

static int down_next(const arp_t *a, const uint8_t *order, int n, int seq, int *idx)
{
    if (seq) {
        if (*idx > 0) { (*idx)--; return 1; }
        return 0;
    }
    for (int i = n - 1; i >= 0; i--)
        if (order[i] < a->last_base) { *idx = i; return 1; }
    return 0;
}

static void choose(arp_t *a, const uint8_t *order, int n, int seq)
{
    int oct = a->octaves, idx = a->idx, pass = a->pass;
    if (idx >= n) idx = n - 1;
    if (pass >= oct) pass = oct - 1;
    switch (a->mode) {
    default:
    case ARP_UP:
        if (a->at_start) { pass = 0; idx = 0; }
        else if (!up_next(a, order, n, seq, &idx)) { pass = pass + 1 < oct ? pass + 1 : 0; idx = 0; }
        break;
    case ARP_DOWN:
        if (a->at_start) { pass = oct - 1; idx = n - 1; }
        else if (!down_next(a, order, n, seq, &idx)) { pass = pass > 0 ? pass - 1 : oct - 1; idx = n - 1; }
        break;
    case ARP_UPDOWN:
        if (a->at_start) { pass = 0; idx = 0; a->dir = 1; }
        else if (a->dir > 0) {
            if (up_next(a, order, n, seq, &idx)) break;
            if (pass + 1 < oct) { pass++; idx = 0; break; }
            a->dir = -1;                                   /* top: turn, no repeat */
            if (!down_next(a, order, n, seq, &idx) && pass > 0) { pass--; idx = n - 1; }
        } else {
            if (down_next(a, order, n, seq, &idx)) break;
            if (pass > 0) { pass--; idx = n - 1; break; }
            a->dir = 1;                                    /* bottom: turn, no repeat */
            if (!up_next(a, order, n, seq, &idx) && oct > 1) { pass = 1; idx = 0; }
        }
        break;
    case ARP_RANDOM: {
        uint32_t k = xorshift(&a->rng) % (uint32_t)(n * oct);
        pass = (int)(k / (uint32_t)n);
        idx = (int)(k % (uint32_t)n);
        break;
    }
    }
    a->at_start = 0;
    a->idx = (uint8_t)idx;
    a->pass = (uint8_t)pass;
    a->last_base = order[idx];
}

static void step(arp_t *a)
{
    uint8_t order[128], vel[128];
    int n = base_order(a, order, vel);
    int seq = a->seq_len != 0;
    release(a);
    if (n == 0) {
        reset_pattern(a);
        return;
    }
    for (int tries = 0; tries <= n * a->octaves; tries++) {
        int note;
        choose(a, order, n, seq);
        if (order[a->idx] == 0xFF)
            continue;
        note = order[a->idx] + 12 * a->pass;
        if (note > 127)
            continue;                                      /* skipped, position advanced */
        plat_voice_on(ARP_SRC_LOCAL, note, vel[a->idx]);
        a->sounding = (uint8_t)note;
        a->sounding_base = order[a->idx];
        a->gate_open = 1;
        return;
    }
}

/* the pool emptied or shrank: silence or cut the step note that left it */
static void pool_changed(arp_t *a)
{
    if (arp_pool_count(a) == 0) {
        release(a);
        reset_pattern(a);
        a->seq_trigger = ARP_NONE;
    } else if (!a->seq_len && a->sounding != ARP_NONE && !in_pool(a, a->sounding_base)) {
        release(a);
    }
}

/* --- events ---------------------------------------------------------------------------- */
static void record_note(arp_t *a, int src, int note, int vel)
{
    if (vel > 0) {
        plat_voice_on(src, note, vel);
        if (a->seq_fresh) {
            a->seq_len = 0;
            a->seq_fresh = 0;
        }
        if (a->seq_len < ARP_SEQ_MAX) {
            a->seq_note[a->seq_len] = (uint8_t)note;
            a->seq_vel[a->seq_len] = (uint8_t)vel;
            a->seq_len++;
        }
    } else {
        plat_voice_off(src, note);
    }
}

void arp_note(arp_t *a, int src, int note, int vel)
{
    if (note < 0 || note > 127)
        return;
    if (a->seq_rec) {
        record_note(a, src, note, vel);
        return;
    }
    if (!a->enabled) {                                     /* pass straight through, tracked */
        if (vel > 0) {
            a->held[note] = (uint8_t)vel;
            a->direct[note] = (uint8_t)vel;
            plat_voice_on(src, note, vel);
        } else {
            a->held[note] = 0;
            if (a->direct[note]) {
                a->direct[note] = 0;
                plat_voice_off(src, note);
            }
        }
        return;
    }
    if (vel > 0) {
        int was_empty = arp_pool_count(a) == 0;
        if (a->hold && held_count(a) == 0 && !was_empty) {   /* re-latch: replaces the pool */
            zero(a->latched, sizeof a->latched);
            release(a);
            reset_pattern(a);
        }
        a->held[note] = (uint8_t)vel;
        a->latched[note] = 0;
        a->seq_trigger = (uint8_t)note;
        if (was_empty) {
            reset_pattern(a);
            if (!a->hold && !a->ext) {                     /* start rule: now, phase from here */
                step(a);
                a->acc = 0;
            }
        }
        return;
    }
    if (!a->held[note])
        return;
    if (a->hold)
        a->latched[note] = a->held[note];
    a->held[note] = 0;
    if (!a->hold)
        pool_changed(a);
}

void arp_hold(arp_t *a, int on)
{
    a->hold = (uint8_t)(on != 0);
    if (!a->hold) {
        zero(a->latched, sizeof a->latched);
        if (a->enabled)
            pool_changed(a);
    }
}

void arp_all_notes_off(arp_t *a)
{
    release(a);
    for (int i = 0; i < 128; i++)
        if (a->direct[i])
            plat_voice_off(ARP_SRC_LOCAL, i);
    zero(a->held, sizeof a->held);
    zero(a->latched, sizeof a->latched);
    zero(a->direct, sizeof a->direct);
    reset_pattern(a);
    a->seq_trigger = ARP_NONE;
}

void arp_enable(arp_t *a, int on)
{
    on = on != 0;
    if (on == a->enabled)
        return;
    a->enabled = (uint8_t)on;
    if (on) {
        for (int i = 0; i < 128; i++)
            if (a->direct[i]) {
                a->direct[i] = 0;
                plat_voice_off(ARP_SRC_LOCAL, i);
            }
        reset_pattern(a);
        if (!a->ext) {
            a->acc = 0;
            if (arp_pool_count(a))
                step(a);
        }
    } else {
        release(a);
        for (int i = 0; i < 128; i++)
            if (a->held[i]) {
                a->direct[i] = a->held[i];
                plat_voice_on(ARP_SRC_LOCAL, i, a->held[i]);
            }
    }
}

/* --- settings -------------------------------------------------------------------------- */
void arp_set_mode(arp_t *a, int mode)
{
    if (mode < 0 || mode >= ARP_MODES)
        return;
    a->mode = (uint8_t)mode;
    reset_pattern(a);
}

void arp_set_octaves(arp_t *a, int n)
{
    if (n < 1) n = 1;
    if (n > 4) n = 4;
    a->octaves = (uint8_t)n;
    reset_pattern(a);
}

void arp_set_bpm(arp_t *a, int bpm)
{
    if (bpm < 20) bpm = 20;
    if (bpm > 600) bpm = 600;
    a->bpm = (uint16_t)bpm;
}

void arp_set_beats(arp_t *a, int num, int den)
{
    if (num < 1 || den < 1 || num > 255 || den > 255)
        return;
    a->beats_num = (uint8_t)num;
    a->beats_den = (uint8_t)den;
}

void arp_set_ext(arp_t *a, int ext)
{
    a->ext = (uint8_t)(ext != 0);
    release(a);
    reset_pattern(a);
    a->acc = 0;
    a->clocks = 0;
    a->running = 1;                                        /* clocks alone drive it until a Stop */
    a->port = ARP_NONE;
    a->loss = ARP_LOSS_TICKS;                              /* no loss event before a clock arrives */
}

/* --- seq recording --------------------------------------------------------------------- */
void arp_seq_record(arp_t *a, int on)
{
    a->seq_rec = (uint8_t)(on != 0);
    if (on)
        a->seq_fresh = 1;                                  /* the first note starts a new sequence */
}

int arp_seq_record_note(arp_t *a, int src, int note, int vel)
{
    if (note >= 0 && note <= 127)
        record_note(a, src, note, vel);
    return a->seq_len;
}

void arp_seq_clear(arp_t *a)
{
    a->seq_len = 0;
    reset_pattern(a);
}

/* --- clocks ---------------------------------------------------------------------------- */
static unsigned step_clocks(const arp_t *a)
{
    unsigned c = (unsigned)ARP_PPQN * a->beats_num / a->beats_den;
    return c ? c : 1;
}

void arp_realtime(arp_t *a, int byte, int port)
{
    byte &= 0xFF;
    if (!a->ext || port < 0 || port > 1)
        return;
    if (byte != 0xF8 && byte != 0xFA && byte != 0xFB && byte != 0xFC)
        return;
    if (a->port == ARP_NONE)
        a->port = (uint8_t)port;
    else if (a->port != port)
        return;
    switch (byte) {
    case 0xF8: {
        unsigned sc = step_clocks(a), half = sc / 2;
        a->loss = 0;
        if (!a->running)
            return;
        if (a->enabled) {
            if (a->clocks % sc == 0)
                step(a);
            else if (half && a->clocks % sc == half && a->gate_open)
                release(a);
        }
        a->clocks++;
        break;
    }
    case 0xFA:
        a->clocks = 0;
        a->running = 1;
        release(a);
        reset_pattern(a);
        break;
    case 0xFB:
        a->running = 1;
        break;
    case 0xFC:
        a->running = 0;
        release(a);
        break;
    }
}

void arp_tick(arp_t *a)
{
    if (!a->enabled)
        return;
    if (a->ext) {
        if (a->loss < ARP_LOSS_TICKS && ++a->loss == ARP_LOSS_TICKS) {
            release(a);                                    /* clock lost: silence, wait for clocks */
            a->port = ARP_NONE;
            a->clocks = 0;
        }
        return;
    }
    a->acc += (uint32_t)a->bpm * a->beats_den;
    if (a->gate_open && a->acc >= HALF(a))
        release(a);
    if (a->acc >= FULL(a)) {
        a->acc -= FULL(a);
        step(a);
    }
}
