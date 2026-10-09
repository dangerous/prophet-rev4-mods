#include "seq.h"
#include "platform.h"

static void seq_zero(void *p, unsigned n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}

void seq_init(seq_t *q)
{
    seq_zero(q, sizeof *q);
    q->chord_beats = 4;                                    /* Whole */
    q->beats_num = 1;                                      /* 8th */
    q->beats_den = 2;
    q->dir = 1;
    q->loss = SEQ_LOSS_TICKS;
    q->gate = ARP_GATE_DEFAULT;
    q->gate_cur = ARP_GATE_DEFAULT;
}

int seq_running(const seq_t *q)
{
    return q->playing || q->armed;
}

/* --- voices ---------------------------------------------------------------------------- */
static void gen_release(seq_t *q)                          /* the Chords notes: generated releases */
{
    for (int i = 0; i < q->snd_n; i++)
        plat_voice_off(ARP_SRC_LOCAL, q->snd[i]);
    q->snd_n = 0;
    q->gate_open = 0;
    q->gate_ms = 0;
}

static void silence(seq_t *q, arp_t *a)                    /* everything the sequencer generates */
{
    gen_release(q);
    arp_chord_clear(a);
}

/* --- the walk -------------------------------------------------------------------------- */
static uint16_t first_pos(seq_t *q)
{
    q->dir = 1;
    return (uint16_t)(q->order == SEQ_BACK && q->style == SEQ_CHORDS && q->len ? q->len - 1 : 0);
}

static void advance(seq_t *q)
{
    uint16_t n = q->len;
    if (n <= 1 || q->pos >= n) {
        q->pos = 0;
        return;
    }
    if (q->style == SEQ_ARPEGGIATED || q->order == SEQ_FOR) {   /* Arpeggiated: always in order */
        q->pos = (uint16_t)(q->pos + 1 < n ? q->pos + 1 : 0);
    } else if (q->order == SEQ_BACK) {
        q->pos = (uint16_t)(q->pos ? q->pos - 1 : n - 1);
    } else if (q->dir > 0) {                               /* Pnd: turn at the ends, no repeat */
        if (q->pos + 1 < n) q->pos++;
        else { q->dir = -1; q->pos--; }
    } else {
        if (q->pos > 0) q->pos--;
        else { q->dir = 1; q->pos++; }
    }
}

static void apply_pending(seq_t *q)
{
    if (!q->pend)
        return;
    q->pend = 0;
    q->beats_num = q->pend_num;
    q->beats_den = q->pend_den;
    if (q->swing != q->pend_swing) {
        q->swing = q->pend_swing;
        q->swing_short = 0;
    }
}

/* the event at pos begins: a chord sounds (Chords) or becomes the arp's chord (Arpeggiated) */
static void sound_event(seq_t *q, arp_t *a)
{
    const seq_ev_t *e = &q->ev[q->pos];
    uint8_t notes[SEQ_CHORD], vels[SEQ_CHORD];
    int n = 0;
    q->remain = e->dur;
    for (int k = 0; k < e->n; k++) {
        int p = e->note[k] + q->transpose;
        if (p >= 0 && p <= 127) {                          /* out of range: silent */
            notes[n] = (uint8_t)p;
            vels[n++] = e->vel[k];
        }
    }
    if (q->style == SEQ_CHORDS) {
        gen_release(q);
        for (int k = 0; k < n; k++) {
            plat_voice_on(ARP_SRC_LOCAL, notes[k], vels[k]);
            q->snd[q->snd_n++] = notes[k];
        }
        q->gate_open = (uint8_t)(n > 0);
        q->gate_cur = q->gate;                             /* a change waits for the next event */
    } else {
        arp_set_beats(a, q->beats_num, q->beats_den);      /* the note rate */
        arp_set_swing(a, q->swing);
        arp_set_gate(a, q->gate);                          /* and the Seq's gate */
        arp_chord_set(a, notes, vels, n);                  /* its pattern afresh, the first note now */
    }
}

static void begin(seq_t *q, arp_t *a)                      /* from event 1: the chord clock afresh */
{
    apply_pending(q);
    q->pos = first_pos(q);
    q->playing = 1;
    q->restart = 0;
    q->chord_acc = 0;
    q->chord_clk = 0;
    sound_event(q, a);
}

static void next_event(seq_t *q, arp_t *a)                 /* a boundary: the chord clock's remainder is carried */
{
    advance(q);
    apply_pending(q);
    q->chord_clk = 0;
    sound_event(q, a);
}

/* --- transport ------------------------------------------------------------------------- */
int seq_start(seq_t *q, arp_t *a)
{
    if (q->len == 0 || q->rec)
        return 0;
    silence(q, a);
    q->paused = 0;
    q->restart = 0;
    q->acc = 0;
    q->swing_short = 0;
    if (a->ext) {                                          /* armed: the first event on the grid's next step */
        q->armed = 1;
        q->playing = 0;
        q->loss = SEQ_LOSS_TICKS;                          /* no loss event before a clock arrives */
    } else {
        q->armed = 0;
        begin(q, a);
    }
    return 1;
}

void seq_stop(seq_t *q, arp_t *a)
{
    silence(q, a);
    q->playing = 0;
    q->armed = 0;
    q->paused = 0;
    q->restart = 0;
    apply_pending(q);                                      /* a change queued while playing is current now */
}

void seq_all_notes_off(seq_t *q, arp_t *a)
{
    seq_stop(q, a);
    seq_zero(q->rec_down, sizeof q->rec_down);                 /* the open chord is closed */
}

void seq_replaced(seq_t *q, arp_t *a)
{
    if (q->playing) {                                      /* as a style change: event 1 at the next step boundary */
        silence(q, a);
        q->restart = 1;
    } else {
        q->pos = 0;
        q->remain = 0;
    }
    q->fresh = 0;
}

void seq_clear(seq_t *q, arp_t *a)
{
    seq_stop(q, a);
    q->len = 0;
    q->total = 0;
    q->transpose = 0;
    q->fresh = 0;                                          /* recording: the empty sequence is the new one */
    seq_zero(q->rec_down, sizeof q->rec_down);
}

/* Internal clock: every tick adds 3*bpm*den; a step at 180000*num, the gate at (g + 1) / 20
 * of the event's last step; with swing the pair's long step is due at 120000*num and the
 * short one at 60000*num. Under MIDI clock the gate is a ms countdown from the last step's
 * clock, as the arp's. */
static uint32_t seq_step_units(const seq_t *q)
{
    uint32_t u = 60000u * q->beats_num;
    if (!q->swing)
        return 3 * u;
    return q->swing_short ? u : 2 * u;
}

static unsigned seq_step_clocks(const seq_t *q)
{
    unsigned c = (unsigned)ARP_PPQN * q->beats_num / q->beats_den;
    return c ? c : 1;
}

static uint32_t chord_target_ticks(const seq_t *q)         /* bpm per tick: 60000 x beats x dur */
{
    return 60000u * q->chord_beats * q->ev[q->pos].dur;
}

static uint32_t chord_target_clocks(const seq_t *q)
{
    return 24u * q->chord_beats * q->ev[q->pos].dur;
}

int seq_tick(seq_t *q, arp_t *a)
{
    uint32_t units;
    int set = 0;
    if (a->ext) {
        if (q->gate_ms && --q->gate_ms == 0 && q->gate_open)
            gen_release(q);                                /* the gate, in ms from the last step's clock */
        if (q->armed && q->loss < SEQ_LOSS_TICKS && ++q->loss == SEQ_LOSS_TICKS) {
            silence(q, a);                                 /* clock lost: silence; clocks returning start at event 1 */
            q->playing = 0;
            q->paused = 0;
            q->restart = 0;
        }
        return 0;
    }
    if (!q->playing)
        return 0;
    units = seq_step_units(q);
    q->acc += 3u * a->bpm * q->beats_den;
    if (q->style == SEQ_CHORDS && q->gate_open && q->remain == 1 && q->gate_cur < ARP_GATES - 1
        && q->acc * 20u >= units * (q->gate_cur + 1u))
        gen_release(q);                                    /* the gate of the event's last step; 100 %: the boundary */
    if (q->acc >= units) {
        q->acc -= units;
        if (q->swing)
            q->swing_short ^= 1;
        if (q->restart) {                                  /* a style change: event 1 */
            silence(q, a);
            begin(q, a);
            set = q->style == SEQ_ARPEGGIATED;
        } else if (q->style == SEQ_CHORDS && --q->remain == 0) {
            next_event(q, a);                              /* else an inner boundary: no retrigger */
        }
    }
    if (q->style == SEQ_ARPEGGIATED && !q->restart && !set) {   /* the chord clock: beats x tempo, remainder carried */
        q->chord_acc += a->bpm;
        if (q->chord_acc >= chord_target_ticks(q)) {
            q->chord_acc -= chord_target_ticks(q);
            next_event(q, a);                              /* cuts an arp step in progress */
            set = 1;
        }
    }
    return set;
}

/* Chords under MIDI clock: a step of len clocks has begun; when it is the event's last, the
 * gate's countdown starts (none at 100 %: the next event releases it) */
static void arm_gate(seq_t *q, const arp_t *a, unsigned len)
{
    unsigned ms;
    if (!q->gate_open || q->remain != 1 || q->gate_cur >= ARP_GATES - 1)
        return;
    ms = (q->gate_cur + 1u) * len * 125u / a->bpm;
    q->gate_ms = (uint16_t)(ms ? ms : 1);
}

/* an accepted clock (the arp counts it after us, so a->clocks is this clock's index on the
 * grid counted from Start — kept while the sequencer is disarmed): a step (or swing pair)
 * every sc clocks, the gate in the event's last step */
static void on_clock(seq_t *q, arp_t *a)
{
    unsigned sc = seq_step_clocks(q), lng = q->swing ? sc / 3 * 2 : sc, m;
    q->loss = 0;
    if (!q->armed || q->paused)
        return;
    m = a->clocks % sc;
    if (!q->playing || q->restart) {                       /* waiting for the grid */
        if (m == 0) {
            silence(q, a);
            begin(q, a);
            if (q->style == SEQ_CHORDS)
                arm_gate(q, a, lng);
        }
        return;
    }
    if (q->style == SEQ_CHORDS) {
        if (m == 0 || m == lng) {
            if (--q->remain == 0)
                next_event(q, a);                          /* else an inner boundary: no retrigger */
            arm_gate(q, a, m == 0 ? lng : sc - lng);
        }
    } else if (++q->chord_clk >= chord_target_clocks(q)) {
        next_event(q, a);                                  /* on the beat grid, whatever the note value */
    }
}

void seq_realtime(seq_t *q, arp_t *a, int byte)
{
    switch (byte & 0xFF) {
    case 0xF8:
        on_clock(q, a);
        break;
    case 0xFA:                                             /* Start: event 1 on the next clock */
        silence(q, a);
        q->paused = 0;
        q->playing = 0;
        q->restart = 0;
        break;
    case 0xFB:                                             /* Continue: position, phase and remaining duration kept
                                                              (the arp keeps its chord source and pattern position too) */
        q->paused = 0;
        break;
    case 0xFC:                                             /* Stop: pause — the generated notes go, the arp keeps its
                                                              chord source (its own Stop releases its note) */
        if (q->armed) {
            q->paused = 1;
            gen_release(q);
        }
        break;
    default:
        break;
    }
}

/* --- settings -------------------------------------------------------------------------- */
void seq_set_style(seq_t *q, arp_t *a, int style)
{
    style = style != 0;
    if (style == q->style)
        return;
    q->style = (uint8_t)style;
    if (q->playing) {                                      /* release, event 1 at the next step boundary */
        silence(q, a);
        q->restart = 1;
    }
}

void seq_set_order(seq_t *q, int order)
{
    if (order >= 0 && order < SEQ_ORDERS)
        q->order = (uint8_t)order;
}

static void pend_rate(seq_t *q)                            /* a change while playing waits for the next event */
{
    if (!q->pend) {
        q->pend = 1;
        q->pend_num = q->beats_num;
        q->pend_den = q->beats_den;
        q->pend_swing = q->swing;
    }
}

void seq_set_beats(seq_t *q, int num, int den)
{
    if (num < 1 || den < 1 || num > 255 || den > 255)
        return;
    if (q->playing) {
        pend_rate(q);
        q->pend_num = (uint8_t)num;
        q->pend_den = (uint8_t)den;
        return;
    }
    q->pend = 0;
    q->beats_num = (uint8_t)num;
    q->beats_den = (uint8_t)den;
}

void seq_set_swing(seq_t *q, int on)
{
    on = on != 0;
    if (q->playing) {
        pend_rate(q);
        q->pend_swing = (uint8_t)on;
        return;
    }
    q->pend = 0;
    if (on != q->swing) {
        q->swing = (uint8_t)on;
        q->swing_short = 0;
    }
}

void seq_set_chord_beats(seq_t *q, int beats)
{
    if (beats < 1) beats = 1;
    if (beats > 16) beats = 16;
    q->chord_beats = (uint8_t)beats;                       /* the running chord's boundary moves with it */
}

void seq_set_transpose(seq_t *q, int semis)
{
    if (semis < -127) semis = -127;
    if (semis > 127) semis = 127;
    q->transpose = (int8_t)semis;                          /* read at the next event / chord boundary */
}

/* --- record mode ------------------------------------------------------------------------ */
static int rec_open(const seq_t *q)                        /* a key of the last event is still down */
{
    for (int i = 0; i < 16; i++)
        if (q->rec_down[i])
            return 1;
    return 0;
}

static void rec_commit(seq_t *q)                           /* the first entry replaces the old sequence */
{
    if (q->fresh) {
        q->fresh = 0;
        q->len = 0;
        q->total = 0;
        q->transpose = 0;
    }
}

void seq_rec_begin(seq_t *q, arp_t *a)
{
    if (q->rec)
        return;
    seq_stop(q, a);                                        /* generated notes released; live notes play on */
    q->rec = 1;
    q->fresh = 1;
    seq_zero(q->rec_down, sizeof q->rec_down);
}

void seq_rec_end(seq_t *q)
{
    q->rec = 0;
    q->fresh = 0;                                          /* nothing entered: the old sequence stands */
    seq_zero(q->rec_down, sizeof q->rec_down);
}

int seq_rec_count(const seq_t *q)
{
    return q->fresh ? 0 : q->total;
}

int seq_rec_note(seq_t *q, arp_t *a, int src, int note, int vel)
{
    if (note < 0 || note > 127)
        return seq_rec_count(q);
    arp_note(a, src, note, vel);                           /* sounds as played: the live path (the arp is off) */
    if (!q->rec)
        return seq_rec_count(q);
    if (vel > 0) {
        if (rec_open(q)) {                                 /* joins the open chord */
            seq_ev_t *e = &q->ev[q->len - 1];
            int k;
            for (k = 0; k < e->n; k++)
                if (e->note[k] == note)
                    return q->total;                       /* already in it */
            if (k >= SEQ_CHORD)
                return q->total;                           /* full: sounds, not recorded */
            e->note[k] = (uint8_t)note;
            e->vel[k] = (uint8_t)vel;
            e->n++;
        } else {
            seq_ev_t *e;
            rec_commit(q);
            if (q->total >= SEQ_STEPS)
                return q->total;                           /* capacity: sounds, not recorded */
            e = &q->ev[q->len++];
            e->n = 1;
            e->dur = 1;
            e->note[0] = (uint8_t)note;
            e->vel[0] = (uint8_t)vel;
            q->total++;
        }
        q->rec_down[note >> 3] |= (uint8_t)(1u << (note & 7));
    } else {
        q->rec_down[note >> 3] &= (uint8_t)~(1u << (note & 7));
    }
    return seq_rec_count(q);
}

int seq_rec_rest_tie(seq_t *q)
{
    if (!q->rec)
        return -1;
    if (rec_open(q)) {                                     /* tie: the open event one step longer */
        if (q->total >= SEQ_STEPS)
            return -1;
        q->ev[q->len - 1].dur++;
        q->total++;
        return 1;
    }
    rec_commit(q);
    if (q->total >= SEQ_STEPS)
        return -1;
    q->ev[q->len].n = 0;                                   /* rest */
    q->ev[q->len].dur = 1;
    q->len++;
    q->total++;
    return 0;
}

int seq_rec_back(seq_t *q)
{
    seq_ev_t *e;
    if (!q->rec || q->fresh || q->len == 0)                /* the preserved old sequence is left alone */
        return 0;
    e = &q->ev[q->len - 1];
    if (e->dur > 1) {
        e->dur--;                                          /* a tie comes off */
    } else {
        q->len--;                                          /* the event goes */
    }
    q->total--;
    seq_zero(q->rec_down, sizeof q->rec_down);                 /* the open chord is closed */
    return 1;
}

void seq_set_gate(seq_t *q, int g)
{
    if (g >= 0 && g < ARP_GATES)
        q->gate = (uint8_t)g;
}
