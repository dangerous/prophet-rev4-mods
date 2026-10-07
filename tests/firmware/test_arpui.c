/* Host harness for the arp UI (docs/SPEC.md: "Arp engine" — Controls,
 * Display, Robustness). `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "arpui.h"
#include "platform.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake platform ------------------------------------------------------------------ */
enum { EV_VON, EV_VOFF, EV_D3, EV_INT, EV_RESTORE, EV_LED, EV_PARAM };
typedef struct { int type, a, b, c; } ev_t;
static ev_t log_[4096];
static int nlog, fake_globals_open, fake_a440_down;
static void push(int t, int a, int b, int c) { if (nlog < 4096) log_[nlog++] = (ev_t){t, a, b, c}; }
void plat_voice_on(int src, int note, int vel) { push(EV_VON, src, note, vel); }
void plat_voice_off(int src, int note) { push(EV_VOFF, src, note, 0); }
void plat_display3(int c0, int c1, int c2) { push(EV_D3, c0, c1, c2); }
void plat_display_int(int v) { push(EV_INT, v, 0, 0); }
void plat_display_restore(void) { push(EV_RESTORE, 0, 0, 0); }
void plat_led(int led, int on) { push(EV_LED, led, on, 0); }
int  plat_globals_open(void) { return fake_globals_open; }
int  plat_a440_down(void) { return fake_a440_down; }
static int params[99];
int  plat_param_read(int p) { return params[p]; }
void plat_param_store(int p, int v) { params[p] = v; push(EV_PARAM, p, v, 0); }

static int count_type(int t) { int n = 0; for (int i = 0; i < nlog; i++) n += log_[i].type == t; return n; }
static ev_t *last_of(int t) { for (int i = nlog - 1; i >= 0; i--) if (log_[i].type == t) return &log_[i]; return 0; }
static int last_d3_is(int c0, int c1, int c2) { ev_t *e = last_of(EV_D3); return e && e->a == c0 && e->b == c1 && e->c == c2; }
static int last_int(void) { ev_t *e = last_of(EV_INT); return e ? e->a : -1; }
static int last_led(void) { ev_t *e = last_of(EV_LED); return e ? (e->a == ARPUI_LED_A440 ? e->b : -2) : -1; }
static void clear_log(void) { nlog = 0; }

/* panel character codes */
enum { CH_U = 0x1E, CH_P = 0x19, CH_D = 0x0D, CH_N = 0x17, CH_R = 0x1B, CH_I = 0x12, CH_T = 0x1D,
       CH_S = 0x1C, CH_Y = 0x22, CH_O = 0x18, CH_F = 0x0F, CH_LO = 0x24, BLANK = 0x25 };
enum { A440 = ARPUI_A440, GLOBALS = ARPUI_GLOBALS, GROUP = ARPUI_GROUP, BANK = ARPUI_BANK, UNISON = 0x19,
       P1 = 0, P2 = 1, P3 = 2, P4 = 3, P5 = 4, P6 = 5, P7 = 6, P8 = 7,
       PRESS = 1, RELEASE = 2, REPEAT = 3, LOCAL = ARP_SRC_LOCAL, MIDI = ARP_SRC_MIDI };

static arpui_t u;
static arp_t a;

static void reset(void) {
    arpui_init(&u); arp_init(&a);
    clear_log(); fake_globals_open = 0; fake_a440_down = 0;
    memset(params, 0, sizeof params);
    for (int i = 0; i < ARPUI_BOOT_TICKS; i++) arpui_tick(&u, &a);     /* past the kill-switch window */
    clear_log();
}
static int btn(int id, int value) { return arpui_button(&u, &a, id, value); }
static void tap_a440(void) { btn(A440, PRESS); btn(A440, RELEASE); }
static void ticks(int n) { while (n-- > 0) arpui_tick(&u, &a); }

/* ---- A440 ---------------------------------------------------------------------------- */
static void test_tap_toggles_arp_with_status_and_led(void) {
    reset();
    CHECK(sizeof(arpui_t) <= 0x40);
    CHECK(btn(A440, PRESS) == 1 && !a.enabled);
    CHECK(btn(A440, RELEASE) == 1 && a.enabled);
    CHECK(last_int() == 120);                                          /* on: BPM */
    ticks(1);
    CHECK(last_led() == 1);
    tap_a440();
    CHECK(!a.enabled && last_d3_is(CH_O, CH_F, CH_F));                 /* OFF */
    ticks(1);
    CHECK(last_led() == 0);
    arp_set_ext(&a, 1);
    tap_a440();
    CHECK(a.enabled && last_d3_is(CH_S, CH_Y, CH_N));                  /* on under MIDI clock: Syn */
}

static void test_repeats_are_ignored(void) {
    reset();
    btn(A440, PRESS);
    CHECK(btn(A440, REPEAT) == 1);
    CHECK(btn(BANK, PRESS) == 1 && a.mode == ARP_DOWN);
    CHECK(btn(BANK, REPEAT) == 1 && a.mode == ARP_DOWN);
    btn(BANK, RELEASE); btn(A440, RELEASE);
    CHECK(!a.enabled);                                                 /* used as a modifier: no toggle */
}

static void test_bank_group_cycle_modes_with_names(void) {
    reset();
    btn(A440, PRESS);
    btn(BANK, PRESS); btn(BANK, RELEASE);
    CHECK(a.mode == ARP_DOWN && last_d3_is(CH_D, CH_N, BLANK));
    btn(BANK, PRESS); btn(BANK, RELEASE);
    CHECK(a.mode == ARP_UPDOWN && last_d3_is(CH_U, CH_D, BLANK));
    btn(BANK, PRESS); btn(BANK, RELEASE);
    CHECK(a.mode == ARP_RANDOM && last_d3_is(CH_R, CH_N, CH_D));
    btn(BANK, PRESS); btn(BANK, RELEASE);
    CHECK(a.mode == ARP_UP && last_d3_is(CH_U, CH_P, BLANK));
    btn(GROUP, PRESS); btn(GROUP, RELEASE);
    CHECK(a.mode == ARP_RANDOM);
    btn(A440, RELEASE);
    CHECK(!a.enabled);
}

static void test_program_buttons_set_octaves_clock_and_note_value(void) {
    reset();
    btn(A440, PRESS);
    btn(P3, PRESS); btn(P3, RELEASE);
    CHECK(a.octaves == 3 && last_d3_is(CH_LO, BLANK, 3));
    btn(P1, PRESS); btn(P1, RELEASE);
    CHECK(a.octaves == 1 && last_d3_is(CH_LO, BLANK, 1));
    btn(P5, PRESS); btn(P5, RELEASE);
    CHECK(a.ext == 1 && last_d3_is(CH_S, CH_Y, CH_N));
    btn(P5, PRESS); btn(P5, RELEASE);
    CHECK(a.ext == 0 && last_d3_is(CH_I, CH_N, CH_T));
    btn(P8, PRESS); btn(P8, RELEASE);                                  /* shorter (+): 8th S */
    CHECK(rate_index(&u.rate) == RATE_DEFAULT_INDEX + 1 && a.beats_num == 1 && a.beats_den == 1 && a.swing == 1);
    CHECK(last_d3_is(BLANK, 8, CH_S));
    btn(P7, PRESS); btn(P7, RELEASE); btn(P7, PRESS); btn(P7, RELEASE);  /* longer (-) twice: 8th, 8th D */
    CHECK(a.beats_num == 3 && a.beats_den == 4 && a.swing == 0 && last_d3_is(BLANK, 8, CH_D));
    btn(P7, PRESS); btn(P7, RELEASE);                                  /* Qtr */
    CHECK(a.beats_num == 1 && a.beats_den == 1 && a.swing == 0 && last_d3_is(BLANK, BLANK, 4));
    btn(A440, RELEASE);
    CHECK(!a.enabled);
}

/* from 8th: Program 8 (shorter, +) and Program 7 (longer, -) walk the Prophet-6 list with its display */
static void test_program_7_8_step_through_every_value(void) {
    static const int SHORTER[][6] = {   /* d0 d1 d2 num den swing */
        { BLANK, 8, CH_S, 1, 1, 1 }, { BLANK, 8, CH_T, 1, 3, 0 }, { BLANK, 1, 6, 1, 4, 0 },
        { 1, 6, CH_S, 1, 2, 1 }, { 1, 6, CH_T, 1, 6, 0 }, { BLANK, 3, 2, 1, 8, 0 }, { BLANK, 3, 2, 1, 8, 0 } };
    static const int LONGER[][6] = {
        { BLANK, 8, CH_D, 3, 4, 0 }, { BLANK, BLANK, 4, 1, 1, 0 }, { BLANK, BLANK, 2, 2, 1, 0 },
        { BLANK, BLANK, 2, 2, 1, 0 } };
    reset();
    btn(A440, PRESS);
    for (int i = 0; i < 7; i++) {                                      /* 8S 8t 16 16S 16t 32, stays 32 */
        btn(P8, PRESS); btn(P8, RELEASE);
        CHECK(last_d3_is(SHORTER[i][0], SHORTER[i][1], SHORTER[i][2]));
        CHECK(a.beats_num == SHORTER[i][3] && a.beats_den == SHORTER[i][4] && a.swing == SHORTER[i][5]);
    }
    btn(A440, RELEASE);
    reset();
    btn(A440, PRESS);
    for (int i = 0; i < 4; i++) {                                      /* 8d 4 2, stays 2 */
        btn(P7, PRESS); btn(P7, RELEASE);
        CHECK(last_d3_is(LONGER[i][0], LONGER[i][1], LONGER[i][2]));
        CHECK(a.beats_num == LONGER[i][3] && a.beats_den == LONGER[i][4] && a.swing == LONGER[i][5]);
    }
    btn(A440, RELEASE);
    CHECK(!a.enabled);
}

static void test_recording_and_program6_clear(void) {
    reset();
    tap_a440();                                                        /* arp on */
    clear_log();
    btn(A440, PRESS);
    arpui_note(&u, &a, LOCAL, 60, 100);
    CHECK(count_type(EV_VON) == 1 && last_int() == 1);                 /* sounds, count shown */
    arpui_note(&u, &a, LOCAL, 60, 0);
    arpui_note(&u, &a, MIDI, 64, 90); arpui_note(&u, &a, MIDI, 64, 0);
    arpui_note(&u, &a, LOCAL, 67, 100); arpui_note(&u, &a, LOCAL, 67, 0);
    CHECK(last_int() == 3 && count_type(EV_VOFF) == 3);
    CHECK(btn(A440, RELEASE) == 1 && a.enabled);                       /* a recording: no toggle */
    CHECK(a.seq_len == 3 && a.seq_note[1] == 64);
    tap_a440();                                                        /* tap with no notes keeps the sequence */
    CHECK(!a.enabled && a.seq_len == 3);
    tap_a440();
    btn(A440, PRESS); btn(P6, PRESS); btn(P6, RELEASE); btn(A440, RELEASE);
    CHECK(a.seq_len == 0 && a.enabled);
}

static void test_orphan_release_is_consumed_and_other_buttons_pass(void) {
    reset();
    CHECK(btn(P3, PRESS) == 0 && btn(P3, RELEASE) == 0);               /* A440 not held: stock's */
    CHECK(btn(BANK, PRESS) == 0 && btn(BANK, REPEAT) == 0 && btn(BANK, RELEASE) == 0);
    btn(A440, PRESS);
    CHECK(btn(BANK, PRESS) == 1);
    btn(A440, RELEASE);
    CHECK(btn(BANK, RELEASE) == 1);                                    /* consumed although A440 is up */
    CHECK(btn(BANK, PRESS) == 0);                                      /* and the next press is stock's */
    CHECK(!a.enabled);
}

static void test_readout_of_unassigned_buttons(void) {
    reset();
    btn(A440, PRESS);
    CHECK(btn(UNISON, PRESS) == 1 && last_int() == UNISON);
    CHECK(btn(UNISON, RELEASE) == 1);
    btn(A440, RELEASE);
    CHECK(!a.enabled);
}

static void test_globals_button_abandons_the_hold(void) {
    reset();
    btn(A440, PRESS);
    CHECK(btn(GLOBALS, PRESS) == 0);                                   /* the menu opens as stock */
    fake_globals_open = 1;
    CHECK(btn(GLOBALS, RELEASE) == 0);
    CHECK(btn(A440, RELEASE) == 0 && !a.enabled);                      /* nothing toggles */
}

static void test_globals_menu_open_passes_everything(void) {
    reset();
    fake_globals_open = 1;
    CHECK(btn(A440, PRESS) == 0 && btn(A440, RELEASE) == 0 && !a.enabled);
    CHECK(btn(P2, PRESS) == 0 && a.octaves == 1);
    fake_globals_open = 0;
    tap_a440();
    CHECK(a.enabled);
}

/* ---- Glide Rate ---------------------------------------------------------------------- */
static void test_glide_is_tempo_only_with_a440_held(void) {
    reset();
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 500) == 0);         /* arp off, no A440: stock glide */
    CHECK(arpui_pot_change(&u, &a, ARPUI_POT_GLIDE) == 0 && a.bpm == 120);
    btn(A440, PRESS);                                                  /* arp off + A440 held: tempo */
    clear_log();
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 1023) == 1 && a.bpm == 300 && last_int() == 300);
    CHECK(arpui_pot_change(&u, &a, ARPUI_POT_GLIDE) == 1);
    arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 0);
    CHECK(a.bpm == 40 && last_int() == 40);
    arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 512);
    CHECK(a.bpm == 40 + (260 * 512 + 511) / 1023);
    CHECK(arpui_pot_store(&u, &a, 0x15, 900) == 0);                    /* another pot: stock */
    CHECK(arpui_pot_change(&u, &a, 0x15) == 0);
    CHECK(count_type(EV_PARAM) == 0);                                  /* BPM is not saved */
    btn(A440, RELEASE);
    CHECK(!a.enabled);                                                 /* the pot used the hold: no toggle */
    tap_a440();                                                        /* arp on, internal clock */
    int bpm = a.bpm;
    CHECK(a.enabled && arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 1023) == 0);   /* no A440: glide */
    CHECK(arpui_pot_change(&u, &a, ARPUI_POT_GLIDE) == 0 && a.bpm == bpm);
    arp_set_ext(&a, 1);                                                /* under Syn, A440 held: still tempo */
    btn(A440, PRESS);
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 100) == 1 && a.bpm == 40 + (260 * 100 + 511) / 1023);
    CHECK(arpui_pot_change(&u, &a, ARPUI_POT_GLIDE) == 1);
    btn(A440, RELEASE);
    CHECK(a.enabled);                                                  /* no toggle */
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 600) == 0 && arpui_pot_change(&u, &a, ARPUI_POT_GLIDE) == 0);
    btn(A440, PRESS);                                                  /* Globals opened mid-hold: stock */
    fake_globals_open = 1;
    btn(P2, PRESS);
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 600) == 0 && arpui_pot_change(&u, &a, ARPUI_POT_GLIDE) == 0);
    fake_globals_open = 0;
}

/* ---- display revert and LED ---------------------------------------------------------- */
static void test_messages_revert_to_patch_display_after_1500_ticks(void) {
    reset();
    tap_a440();                                                        /* BPM shown */
    ticks(DISP_TICKS - 1);
    CHECK(count_type(EV_RESTORE) == 0);
    ticks(1);
    CHECK(count_type(EV_RESTORE) == 1);
    ticks(5000);
    CHECK(count_type(EV_RESTORE) == 1);
    btn(A440, PRESS); btn(BANK, PRESS); btn(BANK, RELEASE);
    ticks(1000);
    btn(BANK, PRESS); btn(BANK, RELEASE);                              /* restarts */
    ticks(DISP_TICKS - 1);
    CHECK(count_type(EV_RESTORE) == 1);
    ticks(1);
    CHECK(count_type(EV_RESTORE) == 2);
    btn(A440, RELEASE);
}

static void test_led_follows_enabled_only_when_it_changes(void) {
    reset();
    ticks(100);
    CHECK(count_type(EV_LED) == 0);
    tap_a440(); ticks(100);
    CHECK(count_type(EV_LED) == 1 && last_led() == 1);
    arp_enable(&a, 0); ticks(1);                                       /* changed by other means */
    CHECK(count_type(EV_LED) == 2 && last_led() == 0);
}

/* ---- kill switch --------------------------------------------------------------------- */
static void test_a440_held_at_power_on_disables_everything(void) {
    arpui_init(&u); arp_init(&a); clear_log();
    fake_a440_down = 1;
    ticks(10);
    fake_a440_down = 0;
    ticks(ARPUI_BOOT_TICKS);
    CHECK(u.kill);
    CHECK(btn(A440, PRESS) == 0 && btn(A440, RELEASE) == 0 && !a.enabled);
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 1023) == 0);
    CHECK(count_type(EV_LED) == 0 && count_type(EV_D3) == 0 && count_type(EV_INT) == 0);
    arpui_init(&u); arp_init(&a); clear_log();
    ticks(ARPUI_BOOT_TICKS + 1);
    fake_a440_down = 1;                                                /* after the window: just a button */
    ticks(10);
    CHECK(!u.kill);
    fake_a440_down = 0;
    CHECK(ARPUI_BOOT_TICKS == 3000);                                   /* the panel link can come up late */
}

static void test_a440_pressed_after_power_on_is_just_a_press(void) {
    arpui_init(&u); arp_init(&a); clear_log();
    ticks(500);
    CHECK(btn(A440, PRESS) == 1 && !u.kill);                           /* an ordinary press */
    fake_a440_down = 1;                                                /* the table reflects it */
    ticks(200);
    CHECK(!u.kill);
    fake_a440_down = 0;
    CHECK(btn(A440, RELEASE) == 1 && a.enabled && !u.kill);           /* the tap toggled the arp */
    ticks(ARPUI_BOOT_TICKS);
    CHECK(!u.kill);
}

/* ---- patch memory ------------------------------------------------------------------- */
static int stores(void) { return count_type(EV_PARAM); }

static void test_settings_are_written_to_the_patch_slots(void) {
    reset();
    CHECK(stores() == 0);
    tap_a440();                                                        /* on, Up, 1 octave, 1/8 */
    CHECK(params[ARPUI_PARAM_OCT] == 1 && params[ARPUI_PARAM_PACK] == (1 | (0 << 1) | (5 << 3)));
    btn(A440, PRESS); btn(BANK, PRESS); btn(BANK, RELEASE);            /* Down */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (5 << 3)));
    btn(P3, PRESS); btn(P3, RELEASE);                                  /* 3 octaves */
    CHECK(params[ARPUI_PARAM_OCT] == 3);
    btn(P8, PRESS); btn(P8, RELEASE);                                  /* shorter: 8th S, code 14 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (14 << 3)));
    btn(P8, PRESS); btn(P8, RELEASE); btn(P8, PRESS); btn(P8, RELEASE);  /* 8th T, 16th: code 2 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (2 << 3)));
    btn(P8, PRESS); btn(P8, RELEASE);                                  /* 16th S: code 13 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (13 << 3)));
    btn(P8, PRESS); btn(P8, RELEASE); btn(P8, PRESS); btn(P8, RELEASE);  /* 16th T, 32nd: code 0 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (0 << 3)));
    for (int i = 0; i < 9; i++) { btn(P7, PRESS); btn(P7, RELEASE); }  /* longer to Half: code 9 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (9 << 3)));
    btn(P8, PRESS); btn(P8, RELEASE);                                  /* Qtr: code 7 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (7 << 3)));
    btn(P8, PRESS); btn(P8, RELEASE); btn(P8, PRESS); btn(P8, RELEASE);  /* 8th D, 8th: code 5 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (5 << 3)));
    btn(P7, PRESS); btn(P7, RELEASE);                                  /* 8th D: code 6 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (6 << 3)));
    btn(P8, PRESS); btn(P8, RELEASE); btn(P8, PRESS); btn(P8, RELEASE);  /* 8th, 8th S: code 14 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (14 << 3)));
    btn(P8, PRESS); btn(P8, RELEASE);                                  /* 8th T: code 3 */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (3 << 3)));
    int n = stores();
    btn(P5, PRESS); btn(P5, RELEASE);                                  /* clock source: not saved */
    btn(A440, RELEASE);
    arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 900);                     /* BPM: not saved (ext on now) */
    arp_set_ext(&a, 0);
    arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 900);
    CHECK(stores() == n);
    tap_a440();                                                        /* off: remembered as off */
    CHECK(params[ARPUI_PARAM_PACK] == (0 | (1 << 1) | (3 << 3)) && params[ARPUI_PARAM_OCT] == 3);
}

static void test_program_load_applies_saved_state(void) {
    reset();
    params[ARPUI_PARAM_OCT] = 2;
    params[ARPUI_PARAM_PACK] = 1 | (2 << 1) | (7 << 3);                /* on, Up/Down, Qtr */
    int n = stores();
    arpui_program_loaded(&u, &a);
    CHECK(a.enabled && a.mode == ARP_UPDOWN && a.octaves == 2 && rate_index(&u.rate) == 1);   /* code 7 */
    CHECK(a.beats_num == 1 && a.beats_den == 1 && a.swing == 0);
    CHECK(stores() == n);                                              /* loading does not write back */
    ticks(1);
    CHECK(last_led() == 1);
    params[ARPUI_PARAM_PACK] = 0 | (1 << 1) | (3 << 3);                /* saved with the arp off, Down, 8th T */
    params[ARPUI_PARAM_OCT] = 4;
    arpui_program_loaded(&u, &a);
    CHECK(!a.enabled && a.mode == ARP_DOWN && a.octaves == 4 && rate_index(&u.rate) == 5);   /* code 3 */
    params[ARPUI_PARAM_PACK] = 1 | (14 << 3);                         /* 8th S */
    arpui_program_loaded(&u, &a);
    CHECK(a.enabled && rate_index(&u.rate) == 4 && a.beats_num == 1 && a.beats_den == 1 && a.swing == 1);
    params[ARPUI_PARAM_PACK] = 1 | (13 << 3);                         /* 16th S */
    arpui_program_loaded(&u, &a);
    CHECK(rate_index(&u.rate) == 7 && a.beats_num == 1 && a.beats_den == 2 && a.swing == 1);
    params[ARPUI_PARAM_PACK] = 1 | (5 << 3);                          /* 8th: swing off again */
    arpui_program_loaded(&u, &a);
    CHECK(rate_index(&u.rate) == RATE_DEFAULT_INDEX && a.beats_num == 1 && a.beats_den == 2 && a.swing == 0);
}

/* programs saved by earlier builds at a value no longer in the list load at the nearest one */
static void test_legacy_note_value_codes_load_at_the_nearest_value(void) {
    static const int LEGACY[][4] = {    /* code, index, num, den */
        { 4, 6, 1, 4 },                 /* 1/16d -> 16th */
        { 8, 1, 1, 1 },                 /* 1/4d -> Qtr */
        { 10, 0, 2, 1 }, { 11, 0, 2, 1 }, { 12, 0, 2, 1 } };   /* whole, 2 bars, 4 bars -> Half */
    reset();
    for (int i = 0; i < 5; i++) {
        params[ARPUI_PARAM_OCT] = 1;
        params[ARPUI_PARAM_PACK] = 1 | (LEGACY[i][0] << 3);
        rate_set_index(&u.rate, RATE_DEFAULT_INDEX);
        int n = stores();
        arpui_program_loaded(&u, &a);
        CHECK(a.enabled && rate_index(&u.rate) == LEGACY[i][1]);
        CHECK(a.beats_num == LEGACY[i][2] && a.beats_den == LEGACY[i][3] && a.swing == 0);
        CHECK(stores() == n && params[ARPUI_PARAM_PACK] == (1 | (LEGACY[i][0] << 3)));   /* not rewritten */
    }
    btn(A440, PRESS); btn(BANK, PRESS); btn(BANK, RELEASE); btn(A440, RELEASE);   /* an edit writes */
    CHECK(params[ARPUI_PARAM_PACK] == (1 | (1 << 1) | (9 << 3)));                  /* Half's own code */
    params[ARPUI_PARAM_PACK] = 1 | (4 << 3);
    arpui_program_loaded(&u, &a);
    btn(A440, PRESS); btn(P7, PRESS); btn(P7, RELEASE); btn(A440, RELEASE);       /* 16th -> 8th T */
    CHECK(rate_index(&u.rate) == 5 && params[ARPUI_PARAM_PACK] == (1 | (0 << 1) | (3 << 3)));
}

static void test_program_without_arp_data_switches_off_and_leaves_settings(void) {
    reset();
    tap_a440();
    btn(A440, PRESS); btn(GROUP, PRESS); btn(GROUP, RELEASE); btn(P2, PRESS); btn(P2, RELEASE); btn(A440, RELEASE);
    CHECK(a.enabled && a.mode == ARP_RANDOM && a.octaves == 2);
    params[ARPUI_PARAM_OCT] = 0; params[ARPUI_PARAM_PACK] = 0;         /* a factory program */
    arpui_program_loaded(&u, &a);
    CHECK(!a.enabled && a.mode == ARP_RANDOM && a.octaves == 2);       /* off, untouched */
    tap_a440();
    params[ARPUI_PARAM_OCT] = 5; params[ARPUI_PARAM_PACK] = 1;         /* out of range: no data */
    arpui_program_loaded(&u, &a);
    CHECK(!a.enabled && a.mode == ARP_RANDOM);
    tap_a440();
    params[ARPUI_PARAM_OCT] = 1; params[ARPUI_PARAM_PACK] = 1 | (15 << 3);   /* note-value code 15: no data */
    arpui_program_loaded(&u, &a);
    CHECK(!a.enabled && a.mode == ARP_RANDOM);
}

int main(void) {
    test_tap_toggles_arp_with_status_and_led();
    test_repeats_are_ignored();
    test_bank_group_cycle_modes_with_names();
    test_program_buttons_set_octaves_clock_and_note_value();
    test_program_7_8_step_through_every_value();
    test_recording_and_program6_clear();
    test_orphan_release_is_consumed_and_other_buttons_pass();
    test_readout_of_unassigned_buttons();
    test_globals_button_abandons_the_hold();
    test_globals_menu_open_passes_everything();
    test_glide_is_tempo_only_with_a440_held();
    test_messages_revert_to_patch_display_after_1500_ticks();
    test_led_follows_enabled_only_when_it_changes();
    test_a440_held_at_power_on_disables_everything();
    test_a440_pressed_after_power_on_is_just_a_press();
    test_settings_are_written_to_the_patch_slots();
    test_program_load_applies_saved_state();
    test_legacy_note_value_codes_load_at_the_nearest_value();
    test_program_without_arp_data_switches_off_and_leaves_settings();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
