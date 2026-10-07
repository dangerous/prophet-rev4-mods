#include "arpui.h"
#include "platform.h"

enum { UI_PRESS = 1, UI_RELEASE = 2 };
enum { UC_A = 0x0A, UC_U = 0x1E, UC_P = 0x19, UC_D = 0x0D, UC_N = 0x17, UC_R = 0x1B, UC_I = 0x12, UC_T = 0x1D,
       UC_S = 0x1C, UC_Y = 0x22, UC_O = 0x18, UC_F = 0x0F, UC_E = 0x0E, UC_LO = 0x24, UC_BLANK = 0x25 };
enum { P1 = 0, P4 = 3, P5 = 4, P6 = 5, P7 = 6, P8 = 7 };

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

/* --- patch memory ------------------------------------------------------------------------ */
/* the saved settings live in two spare program parameters ("Patch memory"):
 * 93 = note-value index * 10 + mode * 2 + on/off (0..99), 94 = octaves (0 = no arp data) */
static void store_patch(const arpui_t *u, const arp_t *a)
{
    plat_param_store(ARPUI_PARAM_OCT, a->octaves);
    plat_param_store(ARPUI_PARAM_PACK, rate_index(&u->rate) * 10 + a->mode * 2 + (a->enabled ? 1 : 0));
}

void arpui_program_loaded(arpui_t *u, arp_t *a)
{
    int oct = plat_param_read(ARPUI_PARAM_OCT), pack = plat_param_read(ARPUI_PARAM_PACK);
    int note, mode, on;
    if (u->kill)
        return;
    if (oct < 1 || oct > 4 || pack < 0 || pack > ARPUI_PACK_MAX) {
        arp_enable(a, 0);                                  /* no arp data: off, settings untouched */
        return;
    }
    note = pack / 10;
    mode = pack % 10 / 2;
    on = pack & 1;
    arp_set_octaves(a, oct);
    arp_set_mode(a, mode);
    rate_set_index(&u->rate, note);
    apply_rate(u, a);
    arp_enable(a, on);
}

/* --- seq record mode (A440 + Tune) ------------------------------------------------------ */
static void draw_rec(const arp_t *a)                       /* the readout: r N, steps of this recording */
{
    int n = a->seq_fresh ? 0 : a->seq_len;                 /* the old sequence stands until the first step */
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
        show3(u, UC_T, UC_I, UC_E);
    else
        show_rec(u, a);
}

static void rec_enter(arpui_t *u, arp_t *a)
{
    u->rec = 1;
    u->rec_ms = 0;
    arp_seq_record(a, 1);
    show_rec(u, a);
}

static void rec_leave(arpui_t *u, arp_t *a, int restore)   /* restore = 0 when the Globals menu takes the display */
{
    u->rec = 0;
    arp_seq_record(a, 0);
    disp_cancel(&u->disp);                                 /* the readout goes with the mode */
    if (restore)
        plat_display_restore();
}

/* --- tap tempo (A440 + Unison) ---------------------------------------------------------- */
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
    case ARPUI_UNISON:
        tempo_tap(u, a);
        break;
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
                } else {
                    arp_enable(a, !a->enabled);
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
        if (value == UI_RELEASE)
            u->swallow[id >> 3] &= (uint8_t)~bit;
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
    arp_note(a, src, note, vel);
}

/* --- HOLD -------------------------------------------------------------------------------- */
void arpui_hold(arpui_t *u, arp_t *a, int on)
{
    if (u->kill)
        return;
    if (u->rec && on && !a->hold)                          /* the pedal went down while recording */
        rest_tie(u, a);
    arp_hold(a, on);
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
