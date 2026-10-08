#include "arpui.h"
#include "platform.h"

enum { UI_PRESS = 1, UI_RELEASE = 2 };
enum { UC_A = 0x0A, UC_U = 0x1E, UC_P = 0x19, UC_D = 0x0D, UC_N = 0x17, UC_R = 0x1B, UC_I = 0x12, UC_T = 0x1D,
       UC_S = 0x1C, UC_Y = 0x22, UC_O = 0x18, UC_F = 0x0F, UC_E = 0x0E, UC_L = 0x15, UC_B = 0x0B, UC_C = 0x0C,
       UC_LO = 0x24, UC_BLANK = 0x25 };

/* ArP chord lengths in cycle order: beats, the code patch memory stores (0 = Whole, the
 * default, so older programs load with it), display (as the note values are shown) */
static const struct { uint8_t beats, code, d[3]; } CHORD[ARPUI_CHORDS] = {
    {  1, 1, {UC_BLANK, UC_BLANK, 4} },                    /* Qtr */
    {  2, 2, {UC_BLANK, UC_BLANK, 2} },                    /* Half */
    {  4, 0, {UC_BLANK, UC_BLANK, 1} },                    /* Whole */
    {  8, 3, {UC_BLANK, 2, UC_B} },                        /* 2 bars */
    { 16, 4, {UC_BLANK, 4, UC_B} },                        /* 4 bars */
};

static int chord_index(const arp_t *a)                     /* the engine's chord length as a list index */
{
    for (int i = 0; i < ARPUI_CHORDS; i++)
        if (CHORD[i].beats == a->chord_beats)
            return i;
    return 2;                                              /* Whole */
}
enum { P1 = 0, P4 = 3, P5 = 4, P6 = 5, P7 = 6, P8 = 7 };

static void acc_end(arpui_t *u, arp_t *a);                /* accompany: below */

static const uint8_t MODE_TEXT[ARP_MODES][3] = {
    { UC_U, UC_P, UC_BLANK },        /* UP  */
    { UC_D, UC_N, UC_BLANK },        /* dn  */
    { UC_U, UC_D, UC_BLANK },        /* Ud  */
    { UC_R, UC_N, UC_D },            /* rnd */
    { UC_A, UC_S, UC_S },            /* ASS */
};

void arpui_init(arpui_t *u)
{
    uint8_t *p = (uint8_t *)u;
    for (unsigned i = 0; i < sizeof *u; i++)
        p[i] = 0;
    rate_init(&u->rate);
    disp_init(&u->disp);
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

static void show_status(arpui_t *u, const arp_t *a)
{
    if (!a->enabled)
        show3(u, UC_O, UC_F, UC_F);
    else if (a->ext)
        show3(u, UC_S, UC_Y, UC_N);
    else
        show_int(u, a->bpm);
}

static void ui_show_rate(arpui_t *u)
{
    uint8_t d[3];
    rate_display(&u->rate, d);
    show3(u, d[0], d[1], d[2]);
}

static void apply_rate(arpui_t *u, arp_t *a)
{
    int num, den;
    rate_beats(&u->rate, &num, &den);
    arp_set_beats(a, num, den);
    arp_set_swing(a, rate_swing(&u->rate));
}

/* --- stock's tuning tone --------------------------------------------------------------------- */
/* Replay an A440 press to stock: with HOLD up it toggles its reference tone (and the A440 LED)
 * a few milliseconds later, in its own task — so the arp LED is asserted again after that. */
static void stock_tone_toggle(arpui_t *u)
{
    plat_stock_a440_press();
    u->led_fix = ARPUI_LED_FIX_MS;
}

/* switching the arp on (any route) silences the tone first; either way the HOLD latch of
 * the mode being left is remembered and the one being entered restored (a replayed HOLD
 * press makes stock flip its latch, LED and hold handler) */
static void ui_enable(arpui_t *u, arp_t *a, int on)
{
    on = on != 0;
    if (on && !a->enabled && plat_tone_on())
        stock_tone_toggle(u);
    if (on != a->enabled) {
        int latch = plat_hold_latch() != 0, want;
        if (on) { u->hold_stock = (uint8_t)latch; want = u->hold_arp; }
        else    { u->hold_arp = (uint8_t)latch;   want = u->hold_stock; }
        if (want != latch)
            plat_stock_hold_press();
    }
    arp_enable(a, on);
}

/* --- patch memory ------------------------------------------------------------------------ */
/* the saved settings live in two spare program parameters ("Patch memory"):
 * 94 = octaves + 4 L + 8 C + 40 M (L = 1: a long note value; C = chord length code; M = ArP; 0 = no arp data),
 * 93 = n * 10 + mode * 2 + on/off, n = the Prophet-6 position (L = 0) or the long value 0..2 (L = 1) */
static void store_patch(const arpui_t *u, const arp_t *a)
{
    int code = rate_code(&u->rate), lng = code >= 10;
    plat_param_store(ARPUI_PARAM_OCT, a->octaves + (lng ? 4 : 0) + 8 * CHORD[chord_index(a)].code + 40 * (a->seq_arp ? 1 : 0));
    plat_param_store(ARPUI_PARAM_PACK, (lng ? code - 10 : code) * 10 + a->mode * 2 + (a->enabled ? 1 : 0));
}

void arpui_program_loaded(arpui_t *u, arp_t *a)
{
    int v = plat_param_read(ARPUI_PARAM_OCT), pack = plat_param_read(ARPUI_PARAM_PACK);
    unsigned f, c, lng, oct;                               /* unsigned: the target has no signed-divide helper */
    int m, note, mode, on, ci = -1;
    if (u->kill)
        return;
    acc_end(u, a);                                         /* a program load drops the latch: nothing to play over */
    u->hold_arp = u->hold_stock = 0;                       /* a program load drops both HOLD latches */
    if (v < 1 || v > ARPUI_OCT_MAX || pack < 0) {
        ui_enable(u, a, 0);                                /* no arp data: off, settings untouched */
        return;
    }
    m = v > 40;
    f = (unsigned)(v - 40 * m) - 1u;                       /* 0..39: octaves-1 + 4 L + 8 C */
    c = f / 8u;
    lng = (f % 8u) >= 4u;
    oct = f % 4u + 1u;
    for (int i = 0; i < ARPUI_CHORDS; i++)
        if (CHORD[i].code == c)
            ci = i;
    if (ci < 0 || pack > (lng ? ARPUI_PACK_MAX_LONG : ARPUI_PACK_MAX)) {
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
    arp_set_seq_arp(a, m);
    arp_set_chord_beats(a, CHORD[ci].beats);
    ui_enable(u, a, on);
}

/* --- seq record mode (A440 + Tune) ------------------------------------------------------ */
static void flash3(arpui_t *u, int c0, int c1, int c2)
{
    plat_display3(c0, c1, c2);
    disp_flash(&u->disp);
}

static void draw_rec(const arp_t *a)                       /* the readout: r N, the length recorded so far */
{
    int n = 0;
    if (!a->seq_fresh)                                     /* the old sequence stands until the first step */
        for (int i = 0; i < a->seq_len; i++)
            n += a->seq_dur[i];                            /* a chord or rest counts one, each tie one more */
    if (n >= 100)
        plat_display_int(n);                               /* three digits: the plain number */
    else
        plat_display3(UC_R, n >= 10 ? n / 10 : UC_BLANK, n % 10);
}

static void show_rec(arpui_t *u, const arp_t *a)
{
    draw_rec(a);
    disp_touch(&u->disp);
}

static void rest_tie(arpui_t *u, arp_t *a)                 /* HOLD button or pedal while recording */
{
    if (arp_seq_rest_tie(a) == 1)
        flash3(u, UC_T, UC_I, UC_E);                       /* tiE, then the count */
    else
        flash3(u, UC_R, UC_S, UC_T);                       /* rSt, then the count */
}

static void rec_enter(arpui_t *u, arp_t *a)
{
    acc_end(u, a);
    u->rec = 1;
    u->rec_ms = 0;
    arp_seq_record(a, 1);
    show_rec(u, a);
}

static void rec_leave(arpui_t *u, arp_t *a, int restore)   /* restore = 0 when the Globals menu takes the display */
{
    int recorded = !a->seq_fresh;                          /* a step was recorded: the old sequence was discarded */
    u->rec = 0;
    arp_seq_record(a, 0);
    disp_cancel(&u->disp);                                 /* the readout goes with the mode */
    if (restore)
        plat_display_restore();
    if (recorded && !a->enabled) {                         /* a recording is made to be heard */
        ui_enable(u, a, 1);
        if (restore)
            show_status(u, a);
        store_patch(u, a);
    }
}

/* --- accompany: the keys play over the latched arp ("Accompany") -------------------------- */
static void acc_end(arpui_t *u, arp_t *a)
{
    if (!u->acc)
        return;
    u->acc = 0;
    arp_set_acc(a, 0);
    if (u->pedal) {                                        /* nothing left sustained by our pedal handling */
        u->pedal = 0;
        plat_dsp_hold(0);
        plat_release_unheld();
    }
}

static void acc_toggle(arpui_t *u, arp_t *a)
{
    if (u->acc) {
        acc_end(u, a);
    } else if (a->enabled && plat_hold_latch() && arp_pool_count(a) > 0) {   /* only over a latched, running arp */
        u->acc = 1;
        u->pedal = 0;                                      /* a pedal already down is seen at the next tick */
        arp_set_acc(a, 1);
        show3(u, UC_A, UC_C, UC_C);
    }
}

int arpui_sustain(const arpui_t *u, const arp_t *a)
{
    if (u->kill || !u->acc || !a->enabled)
        return -1;
    return plat_pedal_down() != 0;
}

void arpui_all_notes_off(arpui_t *u, arp_t *a)
{
    acc_end(u, a);
    arp_all_notes_off(a);
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
    show_int(u, a->bpm);
}

/* --- buttons ---------------------------------------------------------------------------- */
static void end_hold(arpui_t *u)
{
    u->a440_held = 0;
}

/* a button pressed while A440 is held */
static void combo(arpui_t *u, arp_t *a, int id)
{
    u->a440_used = 1;
    switch (id) {
    case ARPUI_BANK:
        arp_set_mode(a, (a->mode + 1) % ARP_MODES);
        show3(u, MODE_TEXT[a->mode][0], MODE_TEXT[a->mode][1], MODE_TEXT[a->mode][2]);
        store_patch(u, a);
        break;
    case ARPUI_GROUP:
        arp_set_mode(a, (a->mode + ARP_MODES - 1) % ARP_MODES);
        show3(u, MODE_TEXT[a->mode][0], MODE_TEXT[a->mode][1], MODE_TEXT[a->mode][2]);
        store_patch(u, a);
        break;
    case P1: case P1 + 1: case P1 + 2: case P4:
        arp_set_octaves(a, id - P1 + 1);
        show3(u, UC_LO, UC_BLANK, a->octaves);
        store_patch(u, a);
        break;
    case P5:
        arp_set_ext(a, !a->ext);
        show_clock(u, a);
        break;
    case P6:
        arp_seq_clear(a);
        if (u->rec)
            show_rec(u, a);                                /* start over, still recording */
        break;
    case ARPUI_VELOCITY:
        tempo_tap(u, a);
        break;
    case ARPUI_HOLD:                                       /* stock's tuning tone, with the arp off */
        if (!a->enabled)
            u->tone_pending = 1;                           /* toggled on the release, when HOLD is up */
        break;
    case ARPUI_UNISON:                                     /* sequence playback: Std / ArP */
        arp_set_seq_arp(a, !a->seq_arp);
        if (a->seq_arp)
            show3(u, UC_A, UC_R, UC_P);
        else
            show3(u, UC_S, UC_T, UC_D);
        store_patch(u, a);
        break;
    case ARPUI_KEYBOARD:                                   /* accompany on / off */
        acc_toggle(u, a);
        break;
    case ARPUI_AFTERTOUCH: {                               /* ArP chord length: the next in the cycle */
        int i = (int)((unsigned)(chord_index(a) + 1) % ARPUI_CHORDS);
        arp_set_chord_beats(a, CHORD[i].beats);
        show3(u, CHORD[i].d[0], CHORD[i].d[1], CHORD[i].d[2]);
        store_patch(u, a);
        break;
    }
    case ARPUI_TUNE:
        if (u->rec)
            rec_leave(u, a, 1);
        else
            rec_enter(u, a);
        break;
    case P7:
    case P8:
        if (rate_step(&u->rate, id == P7 ? -1 : 1))          /* 7 = longer (-), 8 = shorter (+) */
            apply_rate(u, a);
        ui_show_rate(u);
        store_patch(u, a);
        break;
    default:
        show_int(u, id);                                   /* button id readout */
        break;
    }
}

int arpui_button(arpui_t *u, arp_t *a, int id, int value)
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
        } else if (value == UI_RELEASE && u->a440_held) {
            end_hold(u);
            if (!u->a440_used) {                           /* a tap */
                if (u->rec) {
                    rec_leave(u, a, 1);                    /* leaves record mode, nothing else */
                } else if (!a->enabled && plat_tone_on()) {
                    stock_tone_toggle(u);                  /* the tone sounds: this tap only stops it */
                } else {
                    ui_enable(u, a, !a->enabled);
                    show_status(u, a);
                    store_patch(u, a);
                }
            }
        }
        return 1;
    }
    if (id == ARPUI_GLOBALS) {                             /* opens the menu: the hold is abandoned, record mode ends */
        if (u->a440_held)
            end_hold(u);
        if (u->rec)
            rec_leave(u, a, 0);
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
            rest_tie(u, a);
        }
        return 1;
    }
    if (!u->a440_held || value != UI_PRESS)
        return 0;
    u->swallow[id >> 3] |= bit;
    combo(u, a, id);
    return 1;
}

/* --- notes ------------------------------------------------------------------------------ */
void arpui_note(arpui_t *u, arp_t *a, int src, int note, int vel)
{
    if (u->kill) {
        if (vel > 0)
            plat_voice_on(src, note, vel);
        else
            plat_voice_off(src, note);
        return;
    }
    if (u->rec) {                                          /* record mode: sounds directly, recorded */
        arp_seq_record_note(a, src, note, vel);
        if (vel > 0)
            show_rec(u, a);
        return;
    }
    if (u->acc) {                                          /* accompanying: on top, never the arp's */
        arp_play_direct(a, src, note, vel);
        return;
    }
    arp_note(a, src, note, vel);
}

/* --- HOLD -------------------------------------------------------------------------------- */
void arpui_hold(arpui_t *u, arp_t *a, int on)
{
    if (u->kill)
        return;
    if (u->rec && on && !a->hold)                          /* the pedal went down while recording */
        rest_tie(u, a);
    if (u->acc && !a->enabled && !on)
        return;                                            /* accompaniment suspended: the arp keeps its latched notes */
    arp_hold(a, on);
    if (u->acc && arp_pool_count(a) == 0)
        acc_end(u, a);                                     /* the latch went: nothing left to play over */
}

int arpui_suspended(const arpui_t *u, const arp_t *a)
{
    return !u->kill && (a->enabled || u->rec);
}

/* --- Glide Rate -------------------------------------------------------------------------- */
/* A440 + Glide Rate = tempo; Glide Rate alone is always stock glide */
static int glide_is_tempo(const arpui_t *u, int pot)
{
    return pot == ARPUI_POT_GLIDE && !u->kill && u->a440_held;
}

int arpui_pot_store(arpui_t *u, arp_t *a, int pot, int raw)
{
    if (!glide_is_tempo(u, pot))
        return 0;
    u->a440_used = 1;                                      /* the pot used the hold: no toggle */
    if (a->ext) {                                          /* synced: the clock sets the tempo */
        show_clock(u, a);
        return 1;
    }
    if (raw < 0) raw = 0;
    if (raw > 1023) raw = 1023;
    arp_set_bpm(a, 40 + (260 * raw + 511) / 1023);
    show_int(u, a->bpm);
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
void arpui_tick(arpui_t *u, arp_t *a)
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
    if (disp_tick(&u->disp)) {
        if (u->rec)
            draw_rec(a);                                   /* a message over the readout: back to r N */
        else
            plat_display_restore();
    }
    if (u->led_fix && --u->led_fix == 0 && a->enabled && !u->rec)
        u->led_on = 0;                                     /* stock's late LED-off has landed: assert ours again */
    if (u->acc && a->enabled) {                            /* the pedal sustains what is played, by our hand */
        int p = plat_pedal_down() != 0;
        if (p != u->pedal) {
            u->pedal = (uint8_t)p;
            plat_dsp_hold(p);
            if (!p)
                plat_release_unheld();
        }
    }
    if (u->rec) {                                          /* the LED blinks while recording */
        on = u->rec_ms < ARPUI_BLINK_MS;
        if (++u->rec_ms >= 2 * ARPUI_BLINK_MS)
            u->rec_ms = 0;
    } else {
        on = a->enabled != 0;
    }
    if (on != u->led_on) {
        u->led_on = (uint8_t)on;
        plat_led(ARPUI_LED_A440, on);
    }
}
