#include "arpui.h"
#include "platform.h"

enum { UI_PRESS = 1, UI_RELEASE = 2 };
enum { UC_A = 0x0A, UC_B = 0x0B, UC_C = 0x0C, UC_D = 0x0D, UC_E = 0x0E, UC_F = 0x0F, UC_H = 0x11, UC_I = 0x12,
       UC_N = 0x17, UC_O = 0x18, UC_P = 0x19, UC_Q = 0x1A, UC_R = 0x1B, UC_S = 0x1C, UC_T = 0x1D, UC_U = 0x1E,
       UC_Y = 0x22, UC_LO = 0x24, UC_BLANK = 0x25, UC_DASH = 0x26 };

/* chord lengths in cycle order: beats, display (as the note values are shown) */
static const struct { uint8_t beats, d[3]; } CHORD[ARPUI_CHORDS] = {
    {  1, {UC_BLANK, UC_BLANK, 4} },                       /* Qtr */
    {  2, {UC_BLANK, UC_BLANK, 2} },                       /* Half */
    {  4, {UC_BLANK, UC_BLANK, 1} },                       /* Whole */
    {  8, {UC_BLANK, 2, UC_B} },                           /* 2 bars */
    { 16, {UC_BLANK, 4, UC_B} },                           /* 4 bars */
};

static int chord_index(const seq_t *q)                     /* the Seq's chord length as a list index */
{
    for (int i = 0; i < ARPUI_CHORDS; i++)
        if (CHORD[i].beats == q->chord_beats)
            return i;
    return 2;                                              /* Whole */
}
enum { P1 = 0, P4 = 3, P5 = 4, P6 = 5, P7 = 6, P8 = 7 };

static const uint8_t MODE_TEXT[ARP_MODES][3] = {
    { UC_U, UC_P, UC_BLANK },        /* UP  */
    { UC_D, UC_N, UC_BLANK },        /* dn  */
    { UC_U, UC_D, UC_BLANK },        /* Ud  */
    { UC_R, UC_N, UC_D },            /* rnd */
    { UC_A, UC_S, UC_S },            /* ASS */
};

static const uint8_t ORDER_TEXT[SEQ_ORDERS][3] = {
    { UC_F, UC_LO, UC_R },           /* For */
    { UC_B, UC_A, UC_C },            /* bAC */
    { UC_P, UC_N, UC_D },            /* Pnd */
};

void arpui_init(arpui_t *u)
{
    uint8_t *p = (uint8_t *)u;
    for (unsigned i = 0; i < sizeof *u; i++)
        p[i] = 0;
    rate_init(&u->rate);
    rate_init(&u->seq_rate);
    disp_init(&u->disp);
    u->cmd_key = ARP_NONE;
    u->glide_raw = ARPUI_RAW_NONE;
}

/* --- display ---------------------------------------------------------------------------- */
static void show3(arpui_t *u, int c0, int c1, int c2)
{
    plat_display3(c0, c1, c2);
    disp_touch(&u->disp);
}

static void show_int(arpui_t *u, int v)
{
    plat_display_int(v);
    disp_touch(&u->disp);
}

static void show_clock(arpui_t *u, const arp_t *a)
{
    if (a->ext)
        show3(u, UC_S, UC_Y, UC_N);
    else
        show3(u, UC_I, UC_N, UC_T);
}

static void show_running(arpui_t *u, const arp_t *a, int running)   /* a generator started or stopped */
{
    if (!running)
        show3(u, UC_O, UC_F, UC_F);
    else if (a->ext)
        show3(u, UC_S, UC_Y, UC_N);
    else
        show_int(u, a->bpm);
}

static void show_rate(arpui_t *u, const rate_t *r)
{
    uint8_t d[3];
    rate_display(r, d);
    show3(u, d[0], d[1], d[2]);
}

static void show_gen(arpui_t *u)
{
    if (u->gen == ARPUI_GEN_SEQ)
        show3(u, UC_S, UC_E, UC_Q);
    else
        show3(u, UC_A, UC_R, UC_P);
}

static void show_none(arpui_t *u)                          /* no sequence */
{
    show3(u, UC_DASH, UC_DASH, UC_DASH);
}

static void apply_rate(arpui_t *u, arp_t *a)               /* the Arp's note value to the arp */
{
    int num, den;
    rate_beats(&u->rate, &num, &den);
    arp_set_beats(a, num, den);
    arp_set_swing(a, rate_swing(&u->rate));
}

static void apply_seq_rate(arpui_t *u, seq_t *q)           /* the Seq's note value to the sequencer */
{
    int num, den;
    rate_beats(&u->seq_rate, &num, &den);
    seq_set_beats(q, num, den);
    seq_set_swing(q, rate_swing(&u->seq_rate));
}

static int gen_running(const arpui_t *u, const arp_t *a, const seq_t *q)   /* the selected generator runs */
{
    return u->gen == ARPUI_GEN_SEQ ? seq_running(q) : a->enabled;
}

static int seq_runs(const arpui_t *u, const seq_t *q)      /* the sequencer runs as the selected generator */
{
    return u->gen == ARPUI_GEN_SEQ && seq_running(q);
}

/* while the sequencer runs the synth's hold is suspended and the engine sustains live notes */
static void update_sustain(arpui_t *u, arp_t *a, const seq_t *q)
{
    arp_set_sustain(a, seq_runs(u, q) && !u->rec);
}

/* --- stock's tuning tone --------------------------------------------------------------------- */
/* Replay an A440 press to stock: with HOLD up it toggles its reference tone (and the A440 LED)
 * a few milliseconds later, in its own task — so the LED is asserted again after that. */
static void stock_tone_toggle(arpui_t *u)
{
    plat_stock_a440_press();
    u->led_fix = ARPUI_LED_FIX_MS;
}

/* switching the arp on (any route) silences the tone first and gives the arp its own note
 * value back (the sequencer's Arpeggiated style borrows the arp); either way the HOLD latch
 * of the mode being left is remembered and the one being entered restored (a replayed HOLD
 * press makes stock flip its latch, LED and hold handler) */
static void ui_enable(arpui_t *u, arp_t *a, int on)
{
    on = on != 0;
    if (on && !a->enabled && plat_tone_on())
        stock_tone_toggle(u);
    if (on != a->enabled) {
        int latch = plat_hold_latch() != 0, want;
        if (on) { u->hold_stock = (uint8_t)latch; want = u->hold_arp; apply_rate(u, a); }
        else    { u->hold_arp = (uint8_t)latch;   want = u->hold_stock; }
        if (want != latch)
            plat_stock_hold_press();
    }
    arp_enable(a, on);
}

/* --- the generators --------------------------------------------------------------------- */
/* select a generator: the one being left stops (its generated notes released, live notes
 * untouched), the new one is left stopped; SEq needs a recording */
static void select_gen(arpui_t *u, arp_t *a, seq_t *q, int gen)
{
    if (gen == ARPUI_GEN_SEQ && q->len == 0) {
        show_none(u);                                      /* nothing to play: ArP stays */
        return;
    }
    if (gen == u->gen) {
        show_gen(u);
        return;
    }
    if (gen == ARPUI_GEN_SEQ)
        ui_enable(u, a, 0);                                /* the arp stops; the HOLD latch is handed over */
    else
        seq_stop(q, a);
    u->gen = (uint8_t)gen;
    update_sustain(u, a, q);
    show_gen(u);
}

/* --- patch memory ------------------------------------------------------------------------ */
/* the saved settings live in two spare program parameters ("Patch memory"):
 * 94 = octaves + 4 L (L = 1: a long note value; 0 = no arp data; 1.2.0 also wrote 8 C + 40 M, read and ignored),
 * 93 = n * 10 + mode * 2 + on/off, n = the Prophet-6 position (L = 0) or the long value 0..2 (L = 1) */
static void store_patch(const arpui_t *u, const arp_t *a)
{
    int code = rate_code(&u->rate), lng = code >= 10;
    plat_param_store(ARPUI_PARAM_OCT, a->octaves + (lng ? 4 : 0));
    plat_param_store(ARPUI_PARAM_PACK, (lng ? code - 10 : code) * 10 + a->mode * 2 + (a->enabled ? 1 : 0));
}

void arpui_program_loaded(arpui_t *u, arp_t *a, seq_t *q)
{
    int v = plat_param_read(ARPUI_PARAM_OCT), pack = plat_param_read(ARPUI_PARAM_PACK);
    unsigned f, lng, oct;                                  /* unsigned: the target has no signed-divide helper */
    int note, mode, on;
    if (u->kill)
        return;
    u->hold_arp = u->hold_stock = 0;                       /* a program load drops both HOLD latches */
    if (!u->rec)
        seq_stop(q, a);                                    /* the sequence, its settings and transposition are kept */
    if (v < 1 || v > ARPUI_OCT_MAX || pack < 0) {
        ui_enable(u, a, 0);                                /* no arp data: off, settings untouched */
        return;
    }
    f = (unsigned)(v - 40 * (v > 40)) - 1u;                /* 0..39: octaves-1 + 4 L + 8 C (C and M: 1.2.0's, ignored) */
    lng = (f % 8u) >= 4u;
    oct = f % 4u + 1u;
    if (pack > (lng ? ARPUI_PACK_MAX_LONG : ARPUI_PACK_MAX)) {
        ui_enable(u, a, 0);
        return;
    }
    note = pack / 10;
    mode = pack % 10 / 2;
    on = pack & 1;
    arp_set_octaves(a, (int)oct);
    arp_set_mode(a, mode);
    rate_set_code(&u->rate, lng ? 10 + note : note);
    apply_rate(u, a);
    if (u->gen != ARPUI_GEN_ARP)
        on = 0;                                            /* a saved "on" starts the Arp only while ArP is selected */
    ui_enable(u, a, on);
    update_sustain(u, a, q);
}

/* --- seq record mode (A440 + Tune) ------------------------------------------------------ */
static void flash3(arpui_t *u, int c0, int c1, int c2)
{
    plat_display3(c0, c1, c2);
    disp_flash(&u->disp);
}

static void draw_rec(const seq_t *q)                       /* the readout: r N, the timing steps recorded so far */
{
    int n = seq_rec_count(q);
    if (n >= 100)
        plat_display_int(n);                               /* three digits: the plain number */
    else
        plat_display3(UC_R, n >= 10 ? n / 10 : UC_BLANK, n % 10);
}

static void show_rec(arpui_t *u, const seq_t *q)
{
    draw_rec(q);
    disp_touch(&u->disp);
}

static void rest_tie(arpui_t *u, seq_t *q)                 /* HOLD button or pedal while recording */
{
    int r = seq_rec_rest_tie(q);
    if (r == 1)
        flash3(u, UC_T, UC_I, UC_E);                       /* tiE, then the count */
    else if (r == 0)
        flash3(u, UC_R, UC_S, UC_T);                       /* rSt, then the count */
    else
        show_rec(u, q);                                    /* refused (capacity): the count stays */
}

static void rec_enter(arpui_t *u, arp_t *a, seq_t *q)
{
    u->rec = 1;
    u->rec_ms = 0;
    if (u->gen == ARPUI_GEN_ARP) {                         /* recording selects SEq: the arp stops */
        ui_enable(u, a, 0);
        u->gen = ARPUI_GEN_SEQ;
    }
    seq_rec_begin(q, a);
    update_sustain(u, a, q);
    show_rec(u, q);
}

static void rec_leave(arpui_t *u, arp_t *a, seq_t *q, int restore)   /* restore = 0 when the Globals menu takes the display */
{
    u->rec = 0;
    seq_rec_end(q);                                        /* SEq stays selected, stopped */
    update_sustain(u, a, q);
    disp_cancel(&u->disp);                                 /* the readout goes with the mode */
    if (restore)
        plat_display_restore();
}

/* --- tap tempo (A440 + Velocity) -------------------------------------------------------- */
static void tempo_tap(arpui_t *u, arp_t *a)
{
    uint32_t iv = u->ms - u->tap_last, sum = 0;
    int bpm;
    if (a->ext) {                                          /* synced: the clock sets the tempo */
        u->tap_on = 0;
        show_clock(u, a);
        return;
    }
    u->tap_last = u->ms;
    if (!u->tap_on || iv > ARPUI_TAP_MAX_MS) {             /* first tap of a series */
        u->tap_on = 1;
        u->tap_n = 0;
        show3(u, UC_T, UC_A, UC_P);
        return;
    }
    for (int i = ARPUI_TAP_IVS - 1; i > 0; i--)
        u->tap_iv[i] = u->tap_iv[i - 1];
    u->tap_iv[0] = (uint16_t)iv;
    if (u->tap_n < ARPUI_TAP_IVS)
        u->tap_n++;
    for (int i = 0; i < u->tap_n; i++)
        sum += u->tap_iv[i];
    bpm = sum ? (int)((120000u * u->tap_n + sum) / (2 * sum)) : 300;   /* round(60000 n / sum) */
    if (bpm < 40) bpm = 40;
    if (bpm > 300) bpm = 300;
    arp_set_bpm(a, bpm);
    u->tempo_caught = 0;                                   /* the knob is no longer where the tempo is */
    show_int(u, a->bpm);
}

/* --- buttons ---------------------------------------------------------------------------- */
static void end_hold(arpui_t *u)
{
    u->a440_held = 0;
}

/* a button pressed while A440 is held: the combos act on the selected generator */
static void combo(arpui_t *u, arp_t *a, seq_t *q, int id)
{
    int seq_sel = u->gen == ARPUI_GEN_SEQ;
    u->a440_used = 1;
    switch (id) {
    case ARPUI_BANK:
    case ARPUI_GROUP:
        if (seq_sel && q->style == SEQ_CHORDS) {           /* the Seq's order */
            int o = id == ARPUI_BANK ? q->order + 1 : q->order + SEQ_ORDERS - 1;
            seq_set_order(q, (int)((unsigned)o % SEQ_ORDERS));
            show3(u, ORDER_TEXT[q->order][0], ORDER_TEXT[q->order][1], ORDER_TEXT[q->order][2]);
        } else {                                           /* the Arp's direction (also inside Arpeggiated chords) */
            int m = id == ARPUI_BANK ? a->mode + 1 : a->mode + ARP_MODES - 1;
            arp_set_mode(a, (int)((unsigned)m % ARP_MODES));
            show3(u, MODE_TEXT[a->mode][0], MODE_TEXT[a->mode][1], MODE_TEXT[a->mode][2]);
            store_patch(u, a);
        }
        break;
    case P1: case P1 + 1: case P1 + 2: case P4:
        arp_set_octaves(a, id - P1 + 1);
        show3(u, UC_LO, UC_BLANK, a->octaves);
        store_patch(u, a);
        break;
    case P5:
        arp_set_ext(a, !a->ext);
        show_clock(u, a);
        if (seq_running(q)) {                              /* a running sequence restarts under the new clock */
            seq_stop(q, a);
            seq_start(q, a);
        }
        break;
    case P6:
        seq_clear(q, a);
        if (u->rec) {
            show_rec(u, q);                                /* start over, still recording */
        } else {
            u->gen = ARPUI_GEN_ARP;                        /* nothing left to select */
            show_none(u);
        }
        update_sustain(u, a, q);
        break;
    case ARPUI_VELOCITY:
        tempo_tap(u, a);
        break;
    case ARPUI_HOLD:                                       /* stock's tuning tone, with the generator stopped */
        if (!gen_running(u, a, q))
            u->tone_pending = 1;                           /* toggled on the release, when HOLD is up */
        break;
    case ARPUI_UNISON:                                     /* the Seq's style */
        seq_set_style(q, a, !q->style);
        if (q->style == SEQ_ARPEGGIATED)
            show3(u, UC_A, UC_R, UC_P);
        else
            show3(u, UC_C, UC_H, UC_D);
        break;
    case ARPUI_AFTERTOUCH: {                               /* the Seq's chord length: the next in the cycle */
        int i = (int)((unsigned)(chord_index(q) + 1) % ARPUI_CHORDS);
        seq_set_chord_beats(q, CHORD[i].beats);
        show3(u, CHORD[i].d[0], CHORD[i].d[1], CHORD[i].d[2]);
        break;
    }
    case ARPUI_TUNE:
        if (u->rec)
            rec_leave(u, a, q, 1);
        else
            rec_enter(u, a, q);
        break;
    case P7:
    case P8:
        if (seq_sel) {                                     /* 7 = longer (-), 8 = shorter (+) */
            if (rate_step(&u->seq_rate, id == P7 ? -1 : 1))
                apply_seq_rate(u, q);
            show_rate(u, &u->seq_rate);
        } else {
            if (rate_step(&u->rate, id == P7 ? -1 : 1))
                apply_rate(u, a);
            show_rate(u, &u->rate);
            store_patch(u, a);
        }
        break;
    case ARPUI_KEYB:
        select_gen(u, a, q, !u->gen);
        break;
    default:
        show_int(u, id);                                   /* button id readout */
        break;
    }
}

/* a tap of A440: record mode ends; the tone, if sounding, stops; else the selected generator toggles */
static void tap(arpui_t *u, arp_t *a, seq_t *q)
{
    if (u->rec) {
        rec_leave(u, a, q, 1);
    } else if (!gen_running(u, a, q) && plat_tone_on()) {
        stock_tone_toggle(u);                              /* this tap only stops the tone */
    } else if (u->gen == ARPUI_GEN_SEQ) {
        if (seq_running(q)) {
            seq_stop(q, a);
            show_running(u, a, 0);
        } else if (seq_start(q, a)) {
            show_running(u, a, 1);
        } else {
            show_none(u);                                  /* nothing recorded */
        }
        update_sustain(u, a, q);
    } else {
        ui_enable(u, a, !a->enabled);
        show_running(u, a, a->enabled);
        store_patch(u, a);
    }
}

int arpui_button(arpui_t *u, arp_t *a, seq_t *q, int id, int value)
{
    uint8_t bit;
    if (u->kill || id < 0 || id > 63)
        return 0;
    bit = (uint8_t)(1u << (id & 7));
    if (plat_globals_open()) {                             /* the Globals menu is pure stock */
        if (u->a440_held)
            end_hold(u);
        return 0;
    }
    if (id == ARPUI_A440) {
        if (value == UI_PRESS) {
            u->a440_seen = 1;                              /* a real press: not held from power-on */
            u->a440_held = 1;
            u->a440_used = 0;
            u->tempo_caught = 0;                           /* the glide knob has to pick the tempo up again */
        } else if (value == UI_RELEASE && u->a440_held) {
            end_hold(u);
            if (!u->a440_used)
                tap(u, a, q);
        }
        return 1;
    }
    if (id == ARPUI_GLOBALS) {                             /* opens the menu: the hold is abandoned, record mode ends */
        if (u->a440_held)
            end_hold(u);
        if (u->rec)
            rec_leave(u, a, q, 0);
        return 0;
    }
    if (u->swallow[id >> 3] & bit) {                       /* press was ours: repeats and release too */
        if (value == UI_RELEASE) {
            u->swallow[id >> 3] &= (uint8_t)~bit;
            if (id == ARPUI_HOLD && u->tone_pending) {
                u->tone_pending = 0;
                stock_tone_toggle(u);                      /* HOLD is up now: stock toggles its reference tone */
            }
        }
        return 1;
    }
    if (u->rec && id == ARPUI_HOLD) {                      /* record mode: HOLD is rest / tie, A440 held or not */
        if (value == UI_PRESS) {
            u->swallow[id >> 3] |= bit;
            rest_tie(u, q);
        }
        return 1;
    }
    if (u->rec && !u->a440_held && id == ARPUI_GROUP) {    /* record mode: Group alone is Back */
        if (value == UI_PRESS) {
            u->swallow[id >> 3] |= bit;
            seq_rec_back(q);
            show_rec(u, q);
        }
        return 1;
    }
    if (!u->a440_held || value != UI_PRESS)
        return 0;
    u->swallow[id >> 3] |= bit;
    combo(u, a, q, id);
    return 1;
}

/* --- keys and notes ---------------------------------------------------------------------- */
int arpui_key(arpui_t *u, arp_t *a, seq_t *q, int note, int vel)
{
    (void)a;
    if (u->kill || note < 0 || note > 127)
        return 0;
    if (vel > 0) {
        if (u->a440_held && !u->a440_used && u->gen == ARPUI_GEN_SEQ && !u->rec && !plat_globals_open()) {
            u->a440_used = 1;                              /* the first key of the hold: the command */
            u->cmd_key = (uint8_t)note;
            seq_set_transpose(q, note - ARPUI_MIDDLE_C);
            show_int(u, q->transpose);
            return 1;
        }
        return 0;
    }
    if (note == u->cmd_key) {                              /* its release, A440 up or not */
        u->cmd_key = ARP_NONE;
        return 1;
    }
    return 0;
}

void arpui_note(arpui_t *u, arp_t *a, seq_t *q, int src, int note, int vel)
{
    if (u->kill) {
        if (vel > 0)
            plat_voice_on(src, note, vel);
        else
            plat_live_off(src, note);
        return;
    }
    if (u->rec) {                                          /* record mode: sounds (live), recorded */
        seq_rec_note(q, a, src, note, vel);
        if (vel > 0)
            show_rec(u, q);
        return;
    }
    arp_note(a, src, note, vel);                           /* ArP: the arp's; SEq: the arp is off, so live */
}

/* --- HOLD, all notes off, realtime -------------------------------------------------------- */
void arpui_hold(arpui_t *u, arp_t *a, seq_t *q, int on)
{
    if (u->kill)
        return;
    if (u->rec && on && !a->hold)                          /* the pedal went down while recording */
        rest_tie(u, q);
    arp_hold(a, on);
}

void arpui_all_notes_off(arpui_t *u, arp_t *a, seq_t *q)
{
    if (u->kill)
        return;
    seq_all_notes_off(q, a);
    arp_all_notes_off(a);
    update_sustain(u, a, q);
}

void arpui_realtime(arpui_t *u, arp_t *a, seq_t *q, int byte, int port)
{
    if (u->kill)
        return;
    if (arp_rt_accept(a, byte, port)) {
        seq_realtime(q, a, byte);                          /* a chord boundary sets the arp's chord first */
        arp_rt_apply(a, byte);
    }
}

int arpui_suspended(const arpui_t *u, const arp_t *a, const seq_t *q)
{
    return !u->kill && (a->enabled || u->rec || seq_runs(u, q));
}

/* --- Glide Rate -------------------------------------------------------------------------- */
/* A440 + Glide Rate = tempo; Glide Rate alone is always stock glide */
static int glide_is_tempo(const arpui_t *u, int pot)
{
    return pot == ARPUI_POT_GLIDE && !u->kill && u->a440_held;
}

static int raw_bpm(int raw)                                /* the tempo a knob position maps to */
{
    return 40 + (260 * raw + 511) / 1023;
}

int arpui_pot_store(arpui_t *u, arp_t *a, int pot, int raw)
{
    int prev = ARPUI_RAW_NONE, cur;
    if (raw < 0) raw = 0;
    if (raw > 1023) raw = 1023;
    if (pot == ARPUI_POT_GLIDE && !u->kill) {              /* remember where the knob is, tempo or glide */
        prev = u->glide_raw;
        u->glide_raw = (uint16_t)raw;
    }
    if (!glide_is_tempo(u, pot))
        return 0;
    u->a440_used = 1;                                      /* the pot used the hold: no toggle */
    if (a->ext) {                                          /* synced: the clock sets the tempo */
        show_clock(u, a);
        return 1;
    }
    cur = raw_bpm(raw);
    if (!u->tempo_caught) {                                /* pickup: inert until the knob reaches or crosses the tempo */
        int from = prev == ARPUI_RAW_NONE ? cur : raw_bpm(prev);
        if ((from <= a->bpm && a->bpm <= cur) || (cur <= a->bpm && a->bpm <= from))
            u->tempo_caught = 1;
    }
    if (u->tempo_caught)
        arp_set_bpm(a, cur);
    show_int(u, a->bpm);                                   /* the tempo — the target while the knob is inert */
    return 1;
}

int arpui_pot_change(arpui_t *u, arp_t *a, int pot)
{
    (void)a;
    if (!glide_is_tempo(u, pot))
        return 0;
    u->a440_used = 1;
    return 1;
}

/* --- tick -------------------------------------------------------------------------------- */
void arpui_tick(arpui_t *u, arp_t *a, seq_t *q)
{
    int on;
    u->ms++;
    if (u->boot_ticks < ARPUI_BOOT_TICKS) {                /* kill switch: A440 held from power-on */
        u->boot_ticks++;
        if (plat_a440_down() && !u->a440_seen)
            u->kill = 1;
    }
    if (u->kill)
        return;
    update_sustain(u, a, q);                               /* catches changes the clock made (loss, CC) */
    if (disp_tick(&u->disp)) {
        if (u->rec)
            draw_rec(q);                                   /* a message over the readout: back to r N */
        else
            plat_display_restore();
    }
    if (u->led_fix && --u->led_fix == 0 && gen_running(u, a, q) && !u->rec)
        u->led_on = 0;                                     /* stock's late LED-off has landed: assert ours again */
    if (u->rec) {                                          /* the LED blinks while recording */
        on = u->rec_ms < ARPUI_BLINK_MS;
        if (++u->rec_ms >= 2 * ARPUI_BLINK_MS)
            u->rec_ms = 0;
    } else {
        on = gen_running(u, a, q);                         /* lit while the selected generator runs */
    }
    if (on != u->led_on) {
        u->led_on = (uint8_t)on;
        plat_led(ARPUI_LED_A440, on);
    }
}
