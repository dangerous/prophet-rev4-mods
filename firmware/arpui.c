#include "arpui.h"
#include "platform.h"

enum { UI_PRESS = 1, UI_RELEASE = 2 };
enum { UC_U = 0x1E, UC_P = 0x19, UC_D = 0x0D, UC_N = 0x17, UC_R = 0x1B, UC_I = 0x12, UC_T = 0x1D,
       UC_S = 0x1C, UC_Y = 0x22, UC_O = 0x18, UC_F = 0x0F, UC_LO = 0x24, UC_BLANK = 0x25 };
enum { P1 = 0, P4 = 3, P5 = 4, P6 = 5, P7 = 6, P8 = 7 };

static const uint8_t MODE_TEXT[ARP_MODES][3] = {
    { UC_U, UC_P, UC_BLANK },        /* UP  */
    { UC_D, UC_N, UC_BLANK },        /* dn  */
    { UC_U, UC_D, UC_BLANK },        /* Ud  */
    { UC_R, UC_N, UC_D },         /* rnd */
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
}

/* --- buttons ---------------------------------------------------------------------------- */
static void end_hold(arpui_t *u, arp_t *a)
{
    u->a440_held = 0;
    arp_seq_record(a, 0);
}

/* a button pressed while A440 is held */
static void combo(arpui_t *u, arp_t *a, int id)
{
    u->a440_used = 1;
    switch (id) {
    case ARPUI_BANK:
        arp_set_mode(a, (a->mode + 1) % ARP_MODES);
        show3(u, MODE_TEXT[a->mode][0], MODE_TEXT[a->mode][1], MODE_TEXT[a->mode][2]);
        break;
    case ARPUI_GROUP:
        arp_set_mode(a, (a->mode + ARP_MODES - 1) % ARP_MODES);
        show3(u, MODE_TEXT[a->mode][0], MODE_TEXT[a->mode][1], MODE_TEXT[a->mode][2]);
        break;
    case P1: case P1 + 1: case P1 + 2: case P4:
        arp_set_octaves(a, id - P1 + 1);
        show3(u, UC_LO, UC_BLANK, a->octaves);
        break;
    case P5:
        arp_set_ext(a, !a->ext);
        show_clock(u, a);
        break;
    case P6:
        arp_seq_clear(a);
        break;
    case P7:
    case P8:
        if (rate_step(&u->rate, id == P7 ? 1 : -1))          /* 7 = longer (-), 8 = shorter (+) */
            apply_rate(u, a);
        ui_show_rate(u);
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
            end_hold(u, a);
        return 0;
    }
    if (id == ARPUI_A440) {
        if (value == UI_PRESS) {
            u->a440_seen = 1;                              /* a real press: not held from power-on */
            u->a440_held = 1;
            u->a440_used = 0;
            arp_seq_record(a, 1);
        } else if (value == UI_RELEASE && u->a440_held) {
            end_hold(u, a);
            if (!u->a440_used) {
                arp_enable(a, !a->enabled);
                show_status(u, a);
            }
        }
        return 1;
    }
    if (id == ARPUI_GLOBALS) {                             /* opens the menu: the hold is abandoned */
        if (u->a440_held)
            end_hold(u, a);
        return 0;
    }
    if (u->swallow[id >> 3] & bit) {                       /* press was ours: repeats and release too */
        if (value == UI_RELEASE)
            u->swallow[id >> 3] &= (uint8_t)~bit;
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
    if (u->a440_held) {                                    /* recording a sequence */
        int n = arp_seq_record_note(a, src, note, vel);
        u->a440_used = 1;
        if (vel > 0)
            show_int(u, n);
        return;
    }
    arp_note(a, src, note, vel);
}

/* --- Glide Rate -------------------------------------------------------------------------- */
static int glide_is_tempo(const arpui_t *u, const arp_t *a, int pot)
{
    return pot == ARPUI_POT_GLIDE && !u->kill && a->enabled && !a->ext;
}

int arpui_pot_store(arpui_t *u, arp_t *a, int pot, int raw)
{
    if (!glide_is_tempo(u, a, pot))
        return 0;
    if (raw < 0) raw = 0;
    if (raw > 1023) raw = 1023;
    arp_set_bpm(a, 40 + (260 * raw + 511) / 1023);
    show_int(u, a->bpm);
    return 1;
}

int arpui_pot_change(arpui_t *u, arp_t *a, int pot)
{
    return glide_is_tempo(u, a, pot);
}

/* --- tick -------------------------------------------------------------------------------- */
void arpui_tick(arpui_t *u, arp_t *a)
{
    int on;
    if (u->boot_ticks < ARPUI_BOOT_TICKS) {                /* kill switch: A440 held from power-on */
        u->boot_ticks++;
        if (plat_a440_down() && !u->a440_seen)
            u->kill = 1;
    }
    if (u->kill)
        return;
    if (disp_tick(&u->disp))
        plat_display_restore();
    on = a->enabled != 0;
    if (on != u->led_on) {
        u->led_on = (uint8_t)on;
        plat_led(ARPUI_LED_A440, on);
    }
}
