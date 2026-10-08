#include "arp.h"
#include "platform.h"

/* Internal clock: every 1 ms tick adds 3*bpm*den to the accumulator; a step is due at
 * 180000*num (= 60000/bpm ms * num/den beats) and the gate closes at half that. With swing
 * num/den is a pair of steps: the long step is due at 120000*num, the short one at
 * 60000*num (2/3 and 1/3, exact in integers). The remainder is carried so the average
 * tempo is exact. */
static uint32_t step_units(const arp_t *a)
{
    uint32_t u = 60000u * a->beats_num;
    if (!a->swing)
        return 3 * u;
    return a->swing_short ? u : 2 * u;
}

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

/* --- Assign entry list ------------------------------------------------------------------ */
static void asg_add(arp_t *a, int note, int vel)
{
    if (a->asg_len < ARP_ASG_MAX) {
        a->asg_note[a->asg_len] = (uint8_t)note;
        a->asg_vel[a->asg_len] = (uint8_t)vel;
        a->asg_len++;
    }
}

/* drop the entries whose pitch has left the pool, keeping the pattern position on the
 * entry that follows the one just played */
static void asg_prune(arp_t *a)
{
    int n = 0, track = a->mode == ARP_ASSIGN && !a->chord_on && !a->at_start;
    for (int i = 0; i < a->asg_len; i++) {
        if (in_pool(a, a->asg_note[i])) {
            a->asg_note[n] = a->asg_note[i];
            a->asg_vel[n++] = a->asg_vel[i];
        } else if (track && n < a->idx) {
            a->idx--;                                      /* removed before the position */
        } else if (track && n == a->idx) {
            a->asg_stay = 1;                               /* the current entry: its successor is next */
        }
    }
    a->asg_len = (uint8_t)n;
}

/* The base order: the chord source's notes while one is set (in the given order for Assign,
 * else ascending by pitch); otherwise the Assign entries in Assign mode, else the pool
 * ascending by pitch. */
static int base_order(const arp_t *a, uint8_t order[128], uint8_t vel[128])
{
    int n = 0;
    if (a->chord_on) {
        for (int i = 0; i < a->chord_n; i++) {
            order[n] = a->chord_note[i];
            vel[n++] = a->chord_vel[i];
        }
        if (a->mode != ARP_ASSIGN)                         /* by pitch (insertion sort, n <= 10) */
            for (int i = 1; i < n; i++) {
                uint8_t o = order[i], v = vel[i];
                int j = i;
                while (j > 0 && order[j - 1] > o) { order[j] = order[j - 1]; vel[j] = vel[j - 1]; j--; }
                order[j] = o; vel[j] = v;
            }
        return n;
    }
    if (a->mode == ARP_ASSIGN) {
        for (int i = 0; i < a->asg_len; i++) {
            order[n] = a->asg_note[i];
            vel[n++] = a->asg_vel[i];
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
static void release(arp_t *a)                             /* the step note: a generated release */
{
    if (a->sounding != ARP_NONE)
        plat_voice_off(ARP_SRC_LOCAL, a->sounding);
    a->sounding = ARP_NONE;
    a->gate_open = 0;
}

static void sound(arp_t *a, int note, int vel)
{
    plat_voice_on(ARP_SRC_LOCAL, note, vel);
    a->sounding = (uint8_t)note;
    a->gate_open = 1;
}

static void reset_pattern(arp_t *a)
{
    a->at_start = 1;
    a->dir = 1;
    a->asg_stay = 0;
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
 * pitch (so a changed pool continues from the pitch just played); Assign walks by index. */
static int up_next(const arp_t *a, const uint8_t *order, int n, int by_index, int *idx)
{
    if (by_index) {
        if (*idx + 1 < n) { (*idx)++; return 1; }
        return 0;
    }
    for (int i = 0; i < n; i++)
        if (order[i] > a->last_base) { *idx = i; return 1; }
    return 0;
}

static int down_next(const arp_t *a, const uint8_t *order, int n, int by_index, int *idx)
{
    if (by_index) {
        if (*idx > 0) { (*idx)--; return 1; }
        return 0;
    }
    for (int i = n - 1; i >= 0; i--)
        if (order[i] < a->last_base) { *idx = i; return 1; }
    return 0;
}

static void choose(arp_t *a, const uint8_t *order, int n, int by_index)
{
    int oct = a->octaves, idx = a->idx, pass = a->pass;
    if (idx >= n) idx = n - 1;
    if (pass >= oct) pass = oct - 1;
    switch (a->mode) {
    default:
    case ARP_UP:
        if (a->at_start) { pass = 0; idx = 0; }
        else if (!up_next(a, order, n, by_index, &idx)) { pass = pass + 1 < oct ? pass + 1 : 0; idx = 0; }
        break;
    case ARP_DOWN:
        if (a->at_start) { pass = oct - 1; idx = n - 1; }
        else if (!down_next(a, order, n, by_index, &idx)) { pass = pass > 0 ? pass - 1 : oct - 1; idx = n - 1; }
        break;
    case ARP_UPDOWN:
        if (a->at_start) { pass = 0; idx = 0; a->dir = 1; }
        else if (a->dir > 0) {
            if (up_next(a, order, n, by_index, &idx)) break;
            if (pass + 1 < oct) { pass++; idx = 0; break; }
            a->dir = -1;                                   /* top: turn, no repeat */
            if (!down_next(a, order, n, by_index, &idx) && pass > 0) { pass--; idx = n - 1; }
        } else {
            if (down_next(a, order, n, by_index, &idx)) break;
            if (pass > 0) { pass--; idx = n - 1; break; }
            a->dir = 1;                                    /* bottom: turn, no repeat */
            if (!up_next(a, order, n, by_index, &idx) && oct > 1) { pass = 1; idx = 0; }
        }
        break;
    case ARP_ASSIGN:                                       /* forward only, by index */
        if (a->at_start) { pass = 0; idx = 0; }
        else if (a->asg_stay && !a->chord_on) {
            idx = a->idx;                                  /* already on the successor */
            if (idx >= n) { pass = pass + 1 < oct ? pass + 1 : 0; idx = 0; }
        }
        else if (!up_next(a, order, n, 1, &idx)) { pass = pass + 1 < oct ? pass + 1 : 0; idx = 0; }
        a->asg_stay = 0;
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
    uint8_t *order = a->order, *vel = a->ovel;
    int n, by_index = a->mode == ARP_ASSIGN;
    release(a);
    n = base_order(a, order, vel);
    if (n == 0) {
        reset_pattern(a);
        return;
    }
    for (int tries = 0; tries <= n * a->octaves; tries++) {
        int note;
        choose(a, order, n, by_index);
        note = order[a->idx] + 12 * a->pass;
        if (note > 127)
            continue;                                      /* skipped, position advanced */
        sound(a, note, vel[a->idx]);
        a->sounding_base = order[a->idx];
        return;
    }
}

/* the pool emptied or shrank: silence or cut the step note that left it */
static void pool_changed(arp_t *a)
{
    if (a->chord_on)
        return;                                            /* the pattern is the chord source's */
    if (arp_pool_count(a) == 0) {
        release(a);
        reset_pattern(a);
    } else if (a->sounding != ARP_NONE && !in_pool(a, a->sounding_base)) {
        release(a);
    }
}

/* the clock runs the pattern: arp on over the pool, or a chord source set */
static int clocked(const arp_t *a)
{
    return a->enabled || a->chord_on;
}

/* --- events ---------------------------------------------------------------------------- */
static void release_direct(arp_t *a)                      /* live notes, sustained ones included */
{
    for (int i = 0; i < 128; i++)
        if (a->direct[i]) {
            a->direct[i] = 0;
            plat_live_off(ARP_SRC_LOCAL, i);
        }
    zero(a->sustained, sizeof a->sustained);
}

static int is_sustained(const arp_t *a, int note) { return (a->sustained[note >> 3] >> (note & 7)) & 1; }

static void live_off(arp_t *a, int src, int note)          /* a live release: now, or deferred under HOLD */
{
    if (!a->direct[note])
        return;
    if (a->sustain_on && a->hold) {
        a->sustained[note >> 3] |= (uint8_t)(1u << (note & 7));
        return;
    }
    a->direct[note] = 0;
    a->sustained[note >> 3] &= (uint8_t)~(1u << (note & 7));
    plat_live_off(src, note);
}

static void release_sustained(arp_t *a)                    /* HOLD went off */
{
    for (int i = 0; i < 128; i++)
        if (is_sustained(a, i)) {
            a->sustained[i >> 3] &= (uint8_t)~(1u << (i & 7));
            if (a->direct[i]) {
                a->direct[i] = 0;
                plat_live_off(ARP_SRC_LOCAL, i);
            }
        }
}

void arp_note(arp_t *a, int src, int note, int vel)
{
    if (note < 0 || note > 127)
        return;
    if (!a->enabled) {                                     /* pass straight through (live), tracked */
        if (vel > 0) {
            if (is_sustained(a, note)) {                   /* pressed again: its sustained note goes first */
                a->sustained[note >> 3] &= (uint8_t)~(1u << (note & 7));
                a->direct[note] = 0;
                plat_live_off(src, note);
            }
            a->held[note] = (uint8_t)vel;
            a->direct[note] = (uint8_t)vel;
            asg_add(a, note, vel);
            plat_voice_on(src, note, vel);
        } else {
            a->held[note] = 0;
            asg_prune(a);
            live_off(a, src, note);
        }
        return;
    }
    if (vel > 0) {
        int was_empty = arp_pool_count(a) == 0;
        if (a->hold && held_count(a) == 0 && !was_empty && !a->chord_on) {   /* re-latch: replaces the pool */
            zero(a->latched, sizeof a->latched);
            a->asg_len = 0;
            release(a);
            reset_pattern(a);
        }
        a->held[note] = (uint8_t)vel;
        a->latched[note] = 0;
        asg_add(a, note, vel);
        if (was_empty && !a->chord_on) {
            reset_pattern(a);
            if (!a->hold && !a->ext) {                     /* start rule: now, phase from here */
                step(a);
                a->acc = 0;
                a->swing_short = 0;                        /* a swing pair begins: long step */
            }
        }
        return;
    }
    if (!a->held[note])
        return;
    if (a->hold)
        a->latched[note] = a->held[note];
    a->held[note] = 0;
    if (!a->hold) {
        asg_prune(a);
        pool_changed(a);
    }
}

void arp_hold(arp_t *a, int on)
{
    a->hold = (uint8_t)(on != 0);
    if (!a->hold) {
        zero(a->latched, sizeof a->latched);
        asg_prune(a);
        if (a->enabled)
            pool_changed(a);
        release_sustained(a);
    }
}

void arp_set_sustain(arp_t *a, int on)
{
    a->sustain_on = (uint8_t)(on != 0);                    /* what is deferred stays so until HOLD off */
}

void arp_all_notes_off(arp_t *a)
{
    release(a);
    zero(a->direct, sizeof a->direct);                     /* stock's all_notes_off silenced them already */
    zero(a->sustained, sizeof a->sustained);
    zero(a->held, sizeof a->held);
    zero(a->latched, sizeof a->latched);
    a->asg_len = 0;
    reset_pattern(a);
}

void arp_enable(arp_t *a, int on)
{
    on = on != 0;
    if (on == a->enabled)
        return;
    a->enabled = (uint8_t)on;
    if (a->chord_on)
        return;                                            /* the chord source keeps the pattern either way */
    if (on) {
        release_direct(a);
        reset_pattern(a);
        if (!a->ext) {
            a->acc = 0;
            a->swing_short = 0;
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
static void beat_reset(arp_t *a);

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

void arp_set_swing(arp_t *a, int on)
{
    on = on != 0;
    if (on == a->swing)
        return;
    a->swing = (uint8_t)on;
    a->swing_short = 0;                                    /* the next swing step is a long one */
}

void arp_set_ext(arp_t *a, int ext)
{
    a->ext = (uint8_t)(ext != 0);
    release(a);
    reset_pattern(a);
    a->acc = 0;
    a->swing_short = 0;
    a->clocks = 0;
    a->running = 1;                                        /* clocks alone drive it until a Stop */
    a->port = ARP_NONE;
    a->loss = ARP_LOSS_TICKS;                              /* no loss event before a clock arrives */
    beat_reset(a);
}

/* --- chord source ------------------------------------------------------------------------ */
void arp_chord_set(arp_t *a, const uint8_t *notes, const uint8_t *vels, int n)
{
    if (n < 0) n = 0;
    if (n > ARP_CHORD_MAX) n = ARP_CHORD_MAX;
    release(a);
    a->chord_on = 1;
    a->chord_n = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        a->chord_note[i] = notes[i];
        a->chord_vel[i] = vels[i];
    }
    reset_pattern(a);
    a->acc = 0;                                            /* the step phase starts here ... */
    a->swing_short = 0;
    a->chord_clk = 0;                                      /* ... and the clocks are counted from here */
    if (n)
        step(a);                                           /* the first note at once */
}

void arp_chord_clear(arp_t *a)
{
    if (!a->chord_on)
        return;
    release(a);
    a->chord_on = 0;
    a->chord_n = 0;
    reset_pattern(a);
    a->acc = 0;
    a->swing_short = 0;
}

/* --- clocks ---------------------------------------------------------------------------- */
static void beat_reset(arp_t *a)
{
    a->clk_n = 0;
    a->clk_pos = 0;
    a->clk_prev = 0;
}

/* an accepted clock: `loss` is the ticks since the previous one; with a full beat of
 * intervals the BPM becomes round(60000 / beat ms), 40..300 */
static void beat_measure(arp_t *a)
{
    uint32_t sum = 0;
    int bpm;
    if (a->clk_prev) {
        a->clk_iv[a->clk_pos] = a->loss;
        a->clk_pos = (uint8_t)((a->clk_pos + 1) % ARP_BEAT_IVS);
        if (a->clk_n < ARP_BEAT_IVS)
            a->clk_n++;
    }
    a->clk_prev = 1;
    if (a->clk_n < ARP_BEAT_IVS)
        return;
    for (int i = 0; i < ARP_BEAT_IVS; i++)
        sum += a->clk_iv[i];
    bpm = sum ? (int)((120000u + sum) / (2 * sum)) : 300;
    if (bpm < 40) bpm = 40;
    if (bpm > 300) bpm = 300;
    if (bpm != a->bpm)
        arp_set_bpm(a, bpm);
}

static unsigned step_clocks(const arp_t *a)
{
    unsigned c = (unsigned)ARP_PPQN * a->beats_num / a->beats_den;
    return c ? c : 1;
}

int arp_rt_accept(arp_t *a, int byte, int port)
{
    byte &= 0xFF;
    if (!a->ext || port < 0 || port > 1)
        return 0;
    if (byte != 0xF8 && byte != 0xFA && byte != 0xFB && byte != 0xFC)
        return 0;
    if (a->port == ARP_NONE)
        a->port = (uint8_t)port;
    else if (a->port != port)
        return 0;
    return 1;
}

void arp_rt_apply(arp_t *a, int byte)
{
    switch (byte & 0xFF) {
    case 0xF8: {
        /* sc = clocks per step, or per pair with swing: long = 2/3 of it, short = 1/3, the
         * pair boundary at multiples of sc from Start — or from the chord source's start */
        unsigned sc = step_clocks(a), m, lng = a->swing ? sc / 3 * 2 : sc;
        int fresh = 0;
        beat_measure(a);
        a->loss = 0;
        if (!a->running)
            return;
        if (a->chord_on) {                                 /* steps counted from the chord's clock 0, ... */
            m = a->chord_clk % sc;
            fresh = a->chord_clk == 0;                     /* ... whose note sounded when it was set */
            a->chord_clk++;
        } else {
            m = a->clocks % sc;
        }
        if (clocked(a)) {
            if ((m == 0 && !fresh) || m == lng)
                step(a);
            else if (a->gate_open && ((m < lng && m == lng / 2) || (m > lng && m - lng == (sc - lng) / 2)))
                release(a);                                /* the gate */
        }
        a->clocks++;
        break;
    }
    case 0xFA:
        beat_reset(a);
        a->clocks = 0;
        a->running = 1;
        release(a);
        reset_pattern(a);
        break;
    case 0xFB:
        beat_reset(a);
        a->running = 1;
        break;
    case 0xFC:
        beat_reset(a);
        a->running = 0;
        release(a);
        break;
    default:
        break;
    }
}

int arp_realtime(arp_t *a, int byte, int port)
{
    if (!arp_rt_accept(a, byte, port))
        return 0;
    arp_rt_apply(a, byte);
    return 1;
}

void arp_tick(arp_t *a)
{
    if (a->ext) {                                          /* also with the arp off: the BPM follows */
        if (a->loss < ARP_LOSS_TICKS && ++a->loss == ARP_LOSS_TICKS) {
            release(a);                                    /* clock lost: silence, wait for clocks */
            a->port = ARP_NONE;
            a->clocks = 0;
            beat_reset(a);
        }
        return;
    }
    if (!clocked(a))
        return;
    a->acc += 3u * a->bpm * a->beats_den;
    if (a->gate_open && a->acc >= step_units(a) / 2)
        release(a);                                        /* the gate */
    if (a->acc >= step_units(a)) {
        a->acc -= step_units(a);
        if (a->swing)
            a->swing_short ^= 1;
        step(a);
    }
}

