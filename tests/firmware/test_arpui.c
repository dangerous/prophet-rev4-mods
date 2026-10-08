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
enum { EV_VON, EV_VOFF, EV_D3, EV_INT, EV_RESTORE, EV_LED, EV_PARAM, EV_A440_STOCK, EV_HOLD_STOCK, EV_DSP_HOLD, EV_RELEASE_WALK };
typedef struct { int type, a, b, c; } ev_t;
static ev_t log_[4096];
static int nlog, fake_globals_open, fake_a440_down, fake_tone_on, fake_latch, fake_pedal;
static void push(int t, int a, int b, int c) { if (nlog < 4096) log_[nlog++] = (ev_t){t, a, b, c}; }
void plat_voice_on(int src, int note, int vel) { push(EV_VON, src, note, vel); }
void plat_voice_off(int src, int note) { push(EV_VOFF, src, note, 0); }
void plat_display3(int c0, int c1, int c2) { push(EV_D3, c0, c1, c2); }
void plat_display_int(int v) { push(EV_INT, v, 0, 0); }
void plat_display_restore(void) { push(EV_RESTORE, 0, 0, 0); }
void plat_led(int led, int on) { push(EV_LED, led, on, 0); }
int  plat_globals_open(void) { return fake_globals_open; }
int  plat_a440_down(void) { return fake_a440_down; }
int  plat_tone_on(void) { return fake_tone_on; }
void plat_stock_a440_press(void) { push(EV_A440_STOCK, 0, 0, 0); fake_tone_on = !fake_tone_on; }   /* stock toggles its tone */
int  plat_hold_latch(void) { return fake_latch; }
void plat_stock_hold_press(void) { push(EV_HOLD_STOCK, 0, 0, 0); fake_latch = !fake_latch; }      /* stock toggles its latch */
int  plat_pedal_down(void) { return fake_pedal; }
void plat_dsp_hold(int on) { push(EV_DSP_HOLD, on, 0, 0); }
void plat_release_unheld(void) { push(EV_RELEASE_WALK, 0, 0, 0); }
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
       CH_S = 0x1C, CH_Y = 0x22, CH_O = 0x18, CH_F = 0x0F, CH_E = 0x0E, CH_LO = 0x24, BLANK = 0x25 };
enum { A440 = ARPUI_A440, GLOBALS = ARPUI_GLOBALS, GROUP = ARPUI_GROUP, BANK = ARPUI_BANK, VELOCITY = ARPUI_VELOCITY, UNISON = 0x19,
       AFTERTOUCH = ARPUI_AFTERTOUCH, KEYBOARD = ARPUI_KEYBOARD, OSCB_KEYB = 36, CH_A = 0x0A, CH_B = 0x0B, CH_C = 0x0C, CH_L = 0x15,
       TUNE = ARPUI_TUNE, HOLD = ARPUI_HOLD,
       P1 = 0, P2 = 1, P3 = 2, P4 = 3, P5 = 4, P6 = 5, P7 = 6, P8 = 7,
       PRESS = 1, RELEASE = 2, REPEAT = 3, LOCAL = ARP_SRC_LOCAL, MIDI = ARP_SRC_MIDI };

static arpui_t u;
static arp_t a;

static void reset(void) {
    arpui_init(&u); arp_init(&a);
    clear_log(); fake_globals_open = 0; fake_a440_down = 0; fake_tone_on = 0; fake_latch = 0; fake_pedal = 0;
    memset(params, 0, sizeof params);
    for (int i = 0; i < ARPUI_BOOT_TICKS; i++) arpui_tick(&u, &a);     /* past the kill-switch window */
    clear_log();
}
static int btn(int id, int value) { return arpui_button(&u, &a, id, value); }
static void tap_a440(void) { btn(A440, PRESS); btn(A440, RELEASE); }
static void ticks(int n) { while (n-- > 0) { arpui_tick(&u, &a); arp_tick(&a); } }   /* as the glue's tick does */

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
    CHECK(a.mode == ARP_ASSIGN && last_d3_is(CH_A, CH_S, CH_S));      /* ASS */
    btn(BANK, PRESS); btn(BANK, RELEASE);
    CHECK(a.mode == ARP_UP && last_d3_is(CH_U, CH_P, BLANK));
    btn(GROUP, PRESS); btn(GROUP, RELEASE);
    CHECK(a.mode == ARP_ASSIGN && last_d3_is(CH_A, CH_S, CH_S));
    btn(GROUP, PRESS); btn(GROUP, RELEASE);
    CHECK(a.mode == ARP_RANDOM && last_d3_is(CH_R, CH_N, CH_D));
    btn(GROUP, PRESS); btn(GROUP, RELEASE);
    CHECK(a.mode == ARP_UPDOWN);
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
        { BLANK, BLANK, 1, 4, 1, 0 }, { BLANK, 2, CH_B, 8, 1, 0 }, { BLANK, 4, CH_B, 16, 1, 0 },
        { BLANK, 4, CH_B, 16, 1, 0 } };
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
    for (int i = 0; i < 7; i++) {                                      /* 8d 4 2 1 2b 4b, stays 4b */
        btn(P7, PRESS); btn(P7, RELEASE);
        CHECK(last_d3_is(LONGER[i][0], LONGER[i][1], LONGER[i][2]));
        CHECK(a.beats_num == LONGER[i][3] && a.beats_den == LONGER[i][4] && a.swing == LONGER[i][5]);
    }
    btn(A440, RELEASE);
    CHECK(!a.enabled);
}

/* ---- seq record mode (spec "Seq") ---------------------------------------------------- */
static int last_is_rn(int n) { return last_d3_is(CH_R, n >= 10 ? n / 10 : BLANK, n % 10); }   /* the readout r N */
static int last_is_rst(void) { return last_d3_is(CH_R, CH_S, CH_T); }
static int last_is_tie(void) { return last_d3_is(CH_T, CH_I, CH_E); }
static void note(int n, int vel) { arpui_note(&u, &a, LOCAL, n, vel); }
static void enter_rec(void) { btn(A440, PRESS); btn(TUNE, PRESS); btn(TUNE, RELEASE); btn(A440, RELEASE); }

static void test_a440_tune_enters_record_mode_and_a_tap_leaves(void) {
    reset();
    tap_a440();                                                        /* arp on */
    clear_log();
    btn(A440, PRESS);
    CHECK(btn(TUNE, PRESS) == 1 && u.rec && last_is_rn(0));
    CHECK(btn(TUNE, REPEAT) == 1 && btn(TUNE, RELEASE) == 1);
    CHECK(btn(A440, RELEASE) == 1 && a.enabled && u.rec);              /* used the hold: no toggle */
    note(60, 100);
    CHECK(count_type(EV_VON) == 1 && last_is_rn(1));                   /* sounds, count shown */
    note(60, 0);
    arpui_note(&u, &a, MIDI, 64, 90); arpui_note(&u, &a, MIDI, 64, 0);
    note(67, 100); note(67, 0);
    CHECK(last_is_rn(3) && count_type(EV_VOFF) == 3);
    clear_log();
    tap_a440();                                                        /* leave */
    CHECK(!u.rec && a.enabled && a.seq_len == 3 && a.seq_note[1][0] == 64);
    ticks(1);
    CHECK(count_type(EV_RESTORE) == 1);                                /* the patch display is back */
    tap_a440();
    CHECK(!a.enabled);                                                 /* taps toggle again */
}

static void test_record_mode_without_steps_keeps_the_sequence(void) {
    reset();
    enter_rec();
    note(60, 100); note(60, 0); note(64, 100); note(64, 0);
    tap_a440();
    CHECK(!u.rec && a.seq_len == 2 && a.enabled && last_int() == 120);   /* recorded something: the arp comes on, BPM shown */
    enter_rec();
    CHECK(u.rec && last_is_rn(0) && a.seq_len == 2);                  /* the old one stays until a step is recorded */
    enter_rec();                                                       /* A440 + Tune again leaves */
    CHECK(!u.rec && a.seq_len == 2 && a.enabled);                      /* nothing recorded: the arp as it was */
    tap_a440();
    CHECK(!a.enabled);
    enter_rec();
    btn(HOLD, PRESS); btn(HOLD, RELEASE);                              /* a rest alone is a recording too */
    tap_a440();
    CHECK(!u.rec && a.seq_len == 1 && a.enabled);
}

static void test_tune_without_a440_is_stock(void) {
    reset();
    CHECK(btn(TUNE, PRESS) == 0 && btn(TUNE, RELEASE) == 0 && !u.rec);
}

static void test_hold_button_is_rest_or_tie_in_record_mode(void) {
    reset();
    CHECK(btn(HOLD, PRESS) == 0 && btn(HOLD, RELEASE) == 0);           /* not recording: stock HOLD */
    tap_a440();                                                        /* arp on, so leaving shows no BPM */
    enter_rec();
    clear_log();
    CHECK(btn(HOLD, PRESS) == 1 && a.seq_len == 1 && a.seq_n[0] == 0 && last_is_rst());   /* no key down: a rest, rSt flashes */
    CHECK(btn(HOLD, REPEAT) == 1 && btn(HOLD, RELEASE) == 1 && a.seq_len == 1);
    CHECK(!a.hold);                                                    /* the latch did not change */
    ticks(DISP_FLASH_TICKS - 1);
    CHECK(last_is_rst());
    ticks(1);
    CHECK(last_is_rn(1));                                              /* ... then the count */
    note(60, 100);
    CHECK(last_is_rn(2));
    CHECK(btn(HOLD, PRESS) == 1 && a.seq_len == 2 && a.seq_dur[1] == 2 && last_is_tie());   /* key down: a tie, tiE flashes */
    btn(HOLD, RELEASE);
    ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(3));                                              /* the tie counts one */
    CHECK(btn(HOLD, PRESS) == 1 && a.seq_dur[1] == 3 && last_is_tie());   /* and again */
    btn(HOLD, RELEASE);
    ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(4));
    note(60, 0);
    btn(A440, PRESS);
    CHECK(btn(HOLD, PRESS) == 1 && a.seq_len == 3 && last_is_rst());  /* with A440 held too: not the id readout */
    btn(HOLD, RELEASE); btn(A440, RELEASE);
    CHECK(count_type(EV_INT) == 0 && count_type(EV_A440_STOCK) == 0); /* and not the tuning tone */
}

/* ---- tuning tone (A440 + HOLD) --------------------------------------------------------- */
static void test_a440_hold_toggles_the_stock_tone_with_the_arp_off(void) {
    reset();
    btn(A440, PRESS);
    CHECK(btn(HOLD, PRESS) == 1 && count_type(EV_A440_STOCK) == 0 && count_type(EV_INT) == 0);   /* nothing yet, no readout */
    CHECK(btn(HOLD, REPEAT) == 1 && count_type(EV_A440_STOCK) == 0);
    CHECK(btn(HOLD, RELEASE) == 1 && count_type(EV_A440_STOCK) == 1 && fake_tone_on);   /* HOLD up: stock toggles its tone */
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled);                     /* used the hold: no toggle */
    btn(A440, PRESS); btn(HOLD, PRESS); btn(HOLD, RELEASE); btn(A440, RELEASE);
    CHECK(count_type(EV_A440_STOCK) == 2 && !fake_tone_on && !a.enabled);   /* and off again */
    btn(A440, PRESS); btn(HOLD, PRESS); btn(A440, RELEASE);          /* A440 up first: HOLD's release still counts */
    CHECK(count_type(EV_A440_STOCK) == 2 && !a.enabled);
    CHECK(btn(HOLD, RELEASE) == 1 && count_type(EV_A440_STOCK) == 3 && fake_tone_on);
    ticks(10);
    CHECK(count_type(EV_LED) == 0);                                   /* the LED is stock's while the tone sounds */
}

static void test_a440_hold_with_the_arp_on_is_consumed_and_inert(void) {
    reset();
    tap_a440();
    clear_log();
    btn(A440, PRESS);
    CHECK(btn(HOLD, PRESS) == 1 && btn(HOLD, RELEASE) == 1);
    CHECK(count_type(EV_A440_STOCK) == 0 && count_type(EV_INT) == 0 && !fake_tone_on);
    CHECK(btn(A440, RELEASE) == 1 && a.enabled);                      /* a combo: no toggle */
}

static void test_tap_while_the_tone_sounds_only_stops_it(void) {
    reset();
    btn(A440, PRESS); btn(HOLD, PRESS); btn(HOLD, RELEASE); btn(A440, RELEASE);   /* tone on */
    CHECK(fake_tone_on && count_type(EV_A440_STOCK) == 1 && !a.enabled);
    tap_a440();
    CHECK(!fake_tone_on && count_type(EV_A440_STOCK) == 2 && !a.enabled);   /* tone off, the arp still off */
    CHECK(count_type(EV_INT) == 0 && count_type(EV_D3) == 0);              /* no OFF / BPM message */
    ticks(200);
    CHECK(count_type(EV_LED) == 0);                                          /* the LED is stock's: off with the tone */
    tap_a440();
    CHECK(a.enabled && count_type(EV_A440_STOCK) == 2);                     /* the next tap: the arp, no replay */
    ticks(1);
    CHECK(last_led() == 1);
}

static void test_program_load_with_the_tone_on_silences_it_and_reasserts_the_led(void) {
    reset();
    fake_tone_on = 1;
    params[94] = 1; params[93] = 31;                                   /* a program with the arp on (8th, Up) */
    arpui_program_loaded(&u, &a);
    CHECK(a.enabled && count_type(EV_A440_STOCK) == 1 && !fake_tone_on);   /* tone off first */
    ticks(1);
    CHECK(count_type(EV_LED) == 1 && last_led() == 1);
    ticks(98);
    CHECK(count_type(EV_LED) == 1);
    ticks(1);
    CHECK(count_type(EV_LED) == 2 && last_led() == 1);                /* 100 ms after the replay: on again (stock's late LED-off) */
    ticks(1000);
    CHECK(count_type(EV_LED) == 2);
    arpui_program_loaded(&u, &a);                                      /* already on, tone off: nothing more */
    CHECK(count_type(EV_A440_STOCK) == 1);
}

static void test_record_readout_counts_length_in_arp_steps(void) {
    reset();
    enter_rec();
    note(60, 100); note(64, 100); note(67, 100);
    CHECK(last_is_rn(1));                                              /* a chord is one */
    btn(HOLD, PRESS); btn(HOLD, RELEASE); ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(2));                                              /* tied: two */
    note(60, 0); note(64, 0); note(67, 0);
    note(62, 100); note(62, 0);
    CHECK(last_is_rn(3));
    note(62, 100);                                                     /* a step tied up to the 64 cap */
    for (int i = 0; i < 70; i++) { btn(HOLD, PRESS); btn(HOLD, RELEASE); }
    note(62, 0);
    ticks(DISP_FLASH_TICKS);
    CHECK(a.seq_len == 3 && a.seq_dur[2] == 64 && last_is_rn(67));     /* 2 + 1 + 64 */
    note(64, 100);                                                     /* past 99: the plain number */
    for (int i = 0; i < 40; i++) { btn(HOLD, PRESS); btn(HOLD, RELEASE); }
    note(64, 0);
    ticks(DISP_FLASH_TICKS);
    CHECK(a.seq_dur[3] == 41 && last_int() == 108 && count_type(EV_INT) == 1);   /* 67 + 41 */
}

static void test_pedal_on_transition_is_rest_or_tie_in_record_mode(void) {
    reset();
    arpui_hold(&u, &a, 1);
    CHECK(a.hold == 1);                                                /* not recording: the arp's hold */
    arpui_hold(&u, &a, 0);
    CHECK(a.hold == 0);
    enter_rec();
    arpui_hold(&u, &a, 1);
    CHECK(a.seq_len == 1 && a.seq_n[0] == 0 && last_is_rst());        /* pedal down, no key: a rest */
    arpui_hold(&u, &a, 0);
    CHECK(a.seq_len == 1);                                             /* pedal up: nothing */
    ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(1));
    note(60, 100);
    arpui_hold(&u, &a, 1);
    CHECK(a.seq_len == 2 && a.seq_dur[1] == 2 && last_is_tie());      /* pedal down, key down: a tie */
    ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(3));
    arpui_hold(&u, &a, 0);
    note(60, 0);
    tap_a440();
    CHECK(!u.rec && a.hold == 0);
}

static void test_record_readout_persists_and_patch_display_returns_on_leaving(void) {
    reset();
    tap_a440();                                                        /* arp on, so leaving shows no BPM */
    enter_rec();
    clear_log();
    btn(A440, PRESS); btn(BANK, PRESS); btn(BANK, RELEASE); btn(A440, RELEASE);
    CHECK(last_d3_is(CH_D, CH_N, BLANK) && u.rec);                    /* dn shown, still recording */
    ticks(DISP_TICKS);
    CHECK(last_is_rn(0) && count_type(EV_RESTORE) == 0);              /* back to r 0, not to the patch display */
    ticks(5000);
    CHECK(count_type(EV_RESTORE) == 0);
    note(60, 100); note(60, 0);
    CHECK(last_is_rn(1));
    clear_log();
    tap_a440();
    ticks(1);
    CHECK(count_type(EV_RESTORE) == 1 && count_type(EV_D3) == 0);     /* leaving: the patch display, no readout */
    ticks(5000);
    CHECK(count_type(EV_RESTORE) == 1 && count_type(EV_D3) == 0);
}

static void test_a440_led_blinks_in_record_mode(void) {
    reset();                                                           /* arp off: LED off */
    enter_rec();
    ticks(1);
    CHECK(last_led() == 1);
    ticks(499);
    CHECK(last_led() == 1);
    ticks(1);
    CHECK(last_led() == 0);                                            /* 500 ms on */
    ticks(500);
    CHECK(last_led() == 1);                                            /* 500 ms off */
    tap_a440();
    ticks(1);
    CHECK(last_led() == 0);                                            /* back to the arp state: off */
    tap_a440(); ticks(1);
    CHECK(last_led() == 1);
    enter_rec(); ticks(2000);
    tap_a440(); ticks(1);
    CHECK(last_led() == 1);                                            /* arp on: lit again */
}

static void test_program6_in_record_mode_clears_and_stays(void) {
    reset();
    tap_a440();
    enter_rec();
    note(60, 100); note(60, 0); note(64, 100); note(64, 0);
    btn(A440, PRESS); btn(P6, PRESS); btn(P6, RELEASE); btn(A440, RELEASE);
    CHECK(u.rec && a.seq_len == 0 && last_is_rn(0) && a.enabled);
    note(67, 100); note(67, 0);
    CHECK(a.seq_len == 1 && last_is_rn(1));
    tap_a440();
    CHECK(!u.rec && a.seq_len == 1 && a.seq_note[0][0] == 67);
}

static void test_globals_leaves_record_mode(void) {
    reset();
    enter_rec();
    note(60, 100); note(60, 0);
    CHECK(btn(GLOBALS, PRESS) == 0 && !u.rec && a.seq_len == 1);      /* the menu opens as stock */
    fake_globals_open = 1;
    CHECK(btn(GLOBALS, RELEASE) == 0);
}

static void test_notes_while_a440_is_held_are_ordinary(void) {
    reset();
    tap_a440();                                                        /* arp on */
    clear_log();
    btn(A440, PRESS);
    note(60, 100);
    CHECK(count_type(EV_VON) == 1 && a.seq_len == 0 && arp_pool_count(&a) == 1);   /* arpeggiated, not recorded */
    note(60, 0);
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled && !u.rec);            /* a tap: the note did not use the hold */
}

static void test_suspended_is_arp_on_or_record_mode(void) {
    reset();
    CHECK(!arpui_suspended(&u, &a));
    tap_a440();
    CHECK(arpui_suspended(&u, &a));
    tap_a440();
    CHECK(!arpui_suspended(&u, &a));
    enter_rec();
    CHECK(arpui_suspended(&u, &a));
    tap_a440();
    CHECK(!arpui_suspended(&u, &a));
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
    CHECK(btn(OSCB_KEYB, PRESS) == 1 && last_int() == OSCB_KEYB);
    CHECK(btn(OSCB_KEYB, RELEASE) == 1);
    CHECK(btn(16, PRESS) == 1 && last_int() == 16);                    /* a mod-destination button: unassigned */
    CHECK(btn(16, RELEASE) == 1);
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
    arp_set_ext(&a, 1);                                                /* under Syn, A440 held: consumed, inert */
    btn(A440, PRESS);
    clear_log();
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 100) == 1 && a.bpm == bpm);
    CHECK(last_d3_is(CH_S, CH_Y, CH_N) && count_type(EV_INT) == 0);   /* Syn as the hint */
    CHECK(arpui_pot_change(&u, &a, ARPUI_POT_GLIDE) == 1 && a.bpm == bpm);
    btn(A440, RELEASE);
    CHECK(a.enabled);                                                  /* no toggle */
    arp_set_ext(&a, 0);
    btn(A440, PRESS);                                                  /* int again: tempo */
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 100) == 1 && a.bpm == 40 + (260 * 100 + 511) / 1023);
    btn(A440, RELEASE);
    arp_set_ext(&a, 1);
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 600) == 0 && arpui_pot_change(&u, &a, ARPUI_POT_GLIDE) == 0);
    btn(A440, PRESS);                                                  /* Globals opened mid-hold: stock */
    fake_globals_open = 1;
    btn(P2, PRESS);
    CHECK(arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 600) == 0 && arpui_pot_change(&u, &a, ARPUI_POT_GLIDE) == 0);
    fake_globals_open = 0;
}

/* ---- tap tempo (A440 + Velocity) ----------------------------------------------------- */
static int tap(void) { int r = btn(VELOCITY, PRESS); r &= btn(VELOCITY, RELEASE); return r; }
static int last_is_tap(void) { return nlog && log_[nlog - 1].type == EV_D3 && last_d3_is(CH_T, CH_A, CH_P); }
static int last_is_int(int v) { return nlog && log_[nlog - 1].type == EV_INT && log_[nlog - 1].a == v; }
/* hold A440 and tap at the given intervals (ms); the first tap starts the series */
static void tap_series(const int *iv, int n) {
    tap();
    for (int i = 0; i < n; i++) { ticks(iv[i]); tap(); }
}

static void test_tap_tempo_steady_series(void) {
    static const int IV120[] = { 500, 500, 500 };
    reset();
    arp_set_bpm(&a, 77);
    btn(A440, PRESS);
    clear_log();
    CHECK(tap() == 1);                                                 /* first tap: tAP, BPM unchanged */
    CHECK(last_is_tap() && a.bpm == 77);
    ticks(500);
    CHECK(tap() == 1 && a.bpm == 120 && last_is_int(120));             /* each later tap: BPM */
    ticks(500); tap(); ticks(500); tap();
    CHECK(a.bpm == 120 && last_is_int(120));
    btn(A440, RELEASE);
    CHECK(!a.enabled);                                                 /* the taps used the hold */
    reset();
    btn(A440, PRESS);
    tap_series(IV120, 3);
    CHECK(a.bpm == 120);
    ticks(2001);                                                       /* > 2 s: a new series */
    tap();
    CHECK(last_is_tap() && a.bpm == 120);
    for (int i = 0; i < 3; i++) { ticks(667); tap(); }
    CHECK(a.bpm == 90 && last_is_int(90));                             /* round(60000 / 667) */
    btn(A440, RELEASE);
    CHECK(!a.enabled);
}

static void test_tap_tempo_averages_the_last_four_intervals(void) {
    static const int IV[] = { 500, 500, 500, 400 };
    reset();
    btn(A440, PRESS);
    tap_series(IV, 4);
    CHECK(a.bpm == 126);                                               /* 60000 / 475 = 126.3 */
    ticks(400); tap();                                                 /* last four: 500 500 400 400 */
    CHECK(a.bpm == 133 && last_is_int(133));                           /* 60000 / 450 = 133.3 */
    ticks(400); tap(); ticks(400); tap();                              /* 400 400 400 400 */
    CHECK(a.bpm == 150);
    reset();
    btn(A440, PRESS);
    tap(); ticks(1000); tap();                                         /* one interval: 60 */
    CHECK(a.bpm == 60);
    ticks(500); tap();                                                 /* mean of 1000, 500 = 750: 80 */
    CHECK(a.bpm == 80);
    btn(A440, RELEASE);
}

static void test_tap_tempo_clamps_and_the_two_second_limit(void) {
    reset();
    btn(A440, PRESS);
    tap(); ticks(150); tap();
    CHECK(a.bpm == 300 && last_is_int(300));                           /* 400 clamped */
    ticks(150); tap();
    CHECK(a.bpm == 300);
    reset();
    btn(A440, PRESS);
    tap(); ticks(2000); tap();                                         /* exactly 2 s counts: 30 -> 40 */
    CHECK(a.bpm == 40 && last_is_int(40));
    ticks(2000); tap();
    CHECK(a.bpm == 40);
    ticks(500); tap();                                                 /* mean of 2000 2000 500 */
    CHECK(a.bpm == 40);
    reset();
    btn(A440, PRESS);
    tap(); ticks(2001); clear_log(); tap();                            /* 2001: a new series instead */
    CHECK(last_is_tap() && a.bpm == 120);
    ticks(1000); tap();                                                /* the old interval is gone */
    CHECK(a.bpm == 60);
    btn(A440, RELEASE);
}

static void test_tap_tempo_consumption_and_scope(void) {
    reset();
    CHECK(btn(VELOCITY, PRESS) == 0 && btn(VELOCITY, REPEAT) == 0 && btn(VELOCITY, RELEASE) == 0);   /* stock */
    btn(A440, PRESS);
    CHECK(btn(VELOCITY, PRESS) == 1 && btn(VELOCITY, REPEAT) == 1);    /* repeats ignored */
    btn(A440, RELEASE);
    CHECK(btn(VELOCITY, RELEASE) == 1 && !a.enabled);                  /* orphan release consumed */
    CHECK(btn(VELOCITY, PRESS) == 0 && btn(VELOCITY, RELEASE) == 0);
    btn(A440, PRESS);
    CHECK(btn(UNISON, PRESS) == 1 && count_type(EV_INT) == 0 && a.bpm == 120);   /* Unison: not tap tempo any more */
    btn(UNISON, RELEASE); btn(A440, RELEASE);
    reset();                                                           /* no store: BPM is not saved */
    tap_a440();
    btn(A440, PRESS);
    int n = count_type(EV_PARAM);
    tap(); ticks(500); tap(); ticks(500); tap();
    CHECK(a.bpm == 120 && count_type(EV_PARAM) == n);
    btn(A440, RELEASE);
    CHECK(a.enabled);                                                  /* arp on: still on */
    arp_set_ext(&a, 1);                                                /* under Syn: taps do nothing */
    btn(A440, PRESS);
    ticks(3000);
    clear_log();
    CHECK(tap() == 1 && last_d3_is(CH_S, CH_Y, CH_N) && a.bpm == 120); /* Syn shown as the hint */
    ticks(1000);
    CHECK(btn(VELOCITY, PRESS) == 1 && btn(VELOCITY, REPEAT) == 1 && btn(VELOCITY, RELEASE) == 1);
    CHECK(a.bpm == 120 && a.ext && last_d3_is(CH_S, CH_Y, CH_N) && count_type(EV_INT) == 0);
    CHECK(count_type(EV_PARAM) == 0);                                  /* nothing stored either */
    btn(A440, RELEASE);
    CHECK(a.enabled);                                                  /* the tap used the hold: no toggle */
    arp_set_ext(&a, 0);                                                /* a series under int ... */
    btn(A440, PRESS);
    tap(); ticks(500); tap();
    CHECK(a.bpm == 120);
    arp_set_ext(&a, 1);
    ticks(500); tap();                                                 /* ... is ended by a tap under Syn */
    CHECK(a.bpm == 120 && last_d3_is(CH_S, CH_Y, CH_N));
    arp_set_ext(&a, 0);
    ticks(250); clear_log(); tap();                                    /* back under int: a new series */
    CHECK(last_is_tap() && a.bpm == 120);
    ticks(1000); tap();
    CHECK(a.bpm == 60);                                                /* from this series only */
    arp_set_bpm(&a, 120);
    btn(A440, RELEASE);
    CHECK(a.enabled);
    fake_globals_open = 1;                                             /* Globals menu: stock */
    btn(A440, PRESS);
    CHECK(btn(VELOCITY, PRESS) == 0 && btn(VELOCITY, RELEASE) == 0);
    btn(A440, RELEASE);
    fake_globals_open = 0;
    arpui_init(&u); arp_init(&a); clear_log();                         /* kill switch: stock */
    fake_a440_down = 1; ticks(10); fake_a440_down = 0; ticks(ARPUI_BOOT_TICKS);
    CHECK(u.kill);
    btn(A440, PRESS);
    CHECK(btn(VELOCITY, PRESS) == 0 && btn(VELOCITY, RELEASE) == 0);
    ticks(500);
    CHECK(btn(VELOCITY, PRESS) == 0 && a.bpm == 120 && count_type(EV_D3) == 0 && count_type(EV_INT) == 0);
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
/* list index 0 = 4 bars ... 12 = 32nd; patch memory: 94 = octaves + 4 * L, 93 = n * 10 + mode * 2 + on,
 * n = Prophet-6 position (L = 0) or the long value 0 = Whole, 1 = 2 bars, 2 = 4 bars (L = 1) */
enum { N_4B, N_2B, N_1, N_HALF, N_QTR, N_8D, N_8, N_8S, N_8T, N_16, N_16S, N_16T, N_32 };
static int is_long(int note) { return note < N_HALF; }
static int pack(int note, int mode, int on) { return (is_long(note) ? N_HALF - 1 - note : note - N_HALF) * 10 + mode * 2 + on; }
static int pack94(int note, int oct) { return oct + (is_long(note) ? 4 : 0); }
/* ... + 8 * C (0 = Whole, 1 = Qtr, 2 = Half, 3 = 2 bars, 4 = 4 bars) + 40 * M (ArP) */
static int pack94c(int note, int oct, int c, int m) { return pack94(note, oct) + 8 * c + 40 * m; }

static void test_settings_are_written_to_the_patch_slots(void) {
    reset();
    CHECK(stores() == 0);
    tap_a440();                                                        /* on, Up, 1 octave, 8th */
    CHECK(params[ARPUI_PARAM_OCT] == 1 && params[ARPUI_PARAM_PACK] == pack(N_8, ARP_UP, 1));
    btn(A440, PRESS); btn(BANK, PRESS); btn(BANK, RELEASE);            /* Down */
    CHECK(params[ARPUI_PARAM_PACK] == pack(N_8, ARP_DOWN, 1));
    btn(P3, PRESS); btn(P3, RELEASE);                                  /* 3 octaves */
    CHECK(params[ARPUI_PARAM_OCT] == 3);
    btn(P8, PRESS); btn(P8, RELEASE);                                  /* shorter: 8th S */
    CHECK(params[ARPUI_PARAM_PACK] == pack(N_8S, ARP_DOWN, 1));
    for (int i = 0; i < 5; i++) { btn(P8, PRESS); btn(P8, RELEASE); }  /* to 32nd */
    CHECK(params[ARPUI_PARAM_PACK] == pack(N_32, ARP_DOWN, 1) && params[ARPUI_PARAM_PACK] == 93);
    for (int i = 0; i < 9; i++) { btn(P7, PRESS); btn(P7, RELEASE); }  /* longer to Half */
    CHECK(params[ARPUI_PARAM_PACK] == pack(N_HALF, ARP_DOWN, 1) && params[ARPUI_PARAM_OCT] == 3);
    btn(P7, PRESS); btn(P7, RELEASE);                                  /* Whole: the long flag in 94 */
    CHECK(params[ARPUI_PARAM_PACK] == pack(N_1, ARP_DOWN, 1) && params[ARPUI_PARAM_PACK] == 3 && params[ARPUI_PARAM_OCT] == 7);
    btn(P7, PRESS); btn(P7, RELEASE); btn(P7, PRESS); btn(P7, RELEASE);   /* 2 bars, 4 bars */
    CHECK(params[ARPUI_PARAM_PACK] == pack(N_4B, ARP_DOWN, 1) && params[ARPUI_PARAM_PACK] == 23 && params[ARPUI_PARAM_OCT] == 7);
    for (int i = 0; i < 4; i++) { btn(P8, PRESS); btn(P8, RELEASE); }  /* back to Qtr: flag off */
    CHECK(params[ARPUI_PARAM_PACK] == pack(N_QTR, ARP_DOWN, 1) && params[ARPUI_PARAM_OCT] == 3);
    btn(GROUP, PRESS); btn(GROUP, RELEASE); btn(GROUP, PRESS); btn(GROUP, RELEASE);   /* Up, Assign */
    CHECK(a.mode == ARP_ASSIGN && params[ARPUI_PARAM_PACK] == pack(N_QTR, ARP_ASSIGN, 1));
    btn(BANK, PRESS); btn(BANK, RELEASE);                              /* Assign -> Up */
    CHECK(a.mode == ARP_UP && params[ARPUI_PARAM_PACK] == pack(N_QTR, ARP_UP, 1));
    btn(GROUP, PRESS); btn(GROUP, RELEASE);                            /* Assign again */
    int n = stores();
    btn(P5, PRESS); btn(P5, RELEASE);                                  /* clock source: not saved */
    btn(A440, RELEASE);
    arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 900);                     /* BPM: not saved */
    arp_set_ext(&a, 0);
    arpui_pot_store(&u, &a, ARPUI_POT_GLIDE, 900);
    CHECK(stores() == n);
    tap_a440();                                                        /* off: remembered as off */
    CHECK(params[ARPUI_PARAM_PACK] == pack(N_QTR, ARP_ASSIGN, 0) && params[ARPUI_PARAM_OCT] == 3);
    for (int p = 0; p < 99; p++)                                       /* only the two slots are written */
        CHECK(p == ARPUI_PARAM_PACK || p == ARPUI_PARAM_OCT || params[p] == 0);
}

/* every mode x note value x on/off x octaves: what a store writes, a load restores */
static void test_patch_round_trip_for_every_setting(void) {
    int bad = 0;
    reset();
    for (int mode = 0; mode < ARP_MODES; mode++)
        for (int note = 0; note < RATE_COUNT; note++)
            for (int on = 0; on < 2; on++)
                for (int oct = 1; oct <= 4; oct++) {
                    int num, den;
                    arp_set_mode(&a, mode); arp_set_octaves(&a, oct); arp_enable(&a, on);
                    rate_set_index(&u.rate, note);
                    btn(A440, PRESS); btn(P1 + oct - 1, PRESS); btn(P1 + oct - 1, RELEASE); btn(A440, RELEASE);
                    if (params[ARPUI_PARAM_PACK] != pack(note, mode, on) || params[ARPUI_PARAM_OCT] != pack94(note, oct)) bad++;
                    arp_set_mode(&a, (mode + 2) % ARP_MODES); arp_set_octaves(&a, oct % 4 + 1);   /* disturb */
                    arp_enable(&a, !on); rate_set_index(&u.rate, (note + 3) % RATE_COUNT);
                    arpui_program_loaded(&u, &a);
                    rate_beats(&u.rate, &num, &den);
                    if (a.mode != mode || rate_index(&u.rate) != note || a.enabled != on || a.octaves != oct
                        || a.beats_num != num || a.beats_den != den || a.swing != rate_swing(&u.rate)) bad++;
                }
    CHECK(bad == 0);
    CHECK(pack(N_32, ARP_ASSIGN, 1) == 99 && pack(N_4B, ARP_ASSIGN, 1) == 29 && pack94(N_4B, 4) == 8);   /* the largest values written */
    CHECK(pack94c(N_4B, 4, 4, 1) == 80);
}

/* ---- arpeggiated playback: the combos, the display, the slots ----------------------- */
static void test_unison_toggles_playback_mode_and_aftertouch_cycles_chord_length(void) {
    reset();
    CHECK(a.seq_arp == 0 && a.chord_beats == 4);                       /* defaults: POL, Whole */
    btn(A440, PRESS);
    CHECK(btn(UNISON, PRESS) == 1 && a.seq_arp == 1 && last_d3_is(CH_A, CH_R, CH_P));   /* ArP */
    CHECK(btn(UNISON, RELEASE) == 1);
    CHECK(params[ARPUI_PARAM_OCT] == pack94c(N_8, 1, 0, 1));           /* saved: M */
    btn(UNISON, PRESS); btn(UNISON, RELEASE);
    CHECK(a.seq_arp == 0 && last_d3_is(CH_S, CH_T, CH_D));             /* Std */
    CHECK(params[ARPUI_PARAM_OCT] == pack94c(N_8, 1, 0, 0));
    static const int CYCLE[][4] = {   /* beats, code, display */
        { 8, 3, 2, CH_B }, { 16, 4, 4, CH_B }, { 1, 1, BLANK, 4 }, { 2, 2, BLANK, 2 }, { 4, 0, BLANK, 1 } };
    for (int i = 0; i < 5; i++) {                                      /* from Whole: 2b 4b 4 2 1 */
        CHECK(btn(AFTERTOUCH, PRESS) == 1 && btn(AFTERTOUCH, RELEASE) == 1);
        CHECK(a.chord_beats == CYCLE[i][0] && last_d3_is(BLANK, CYCLE[i][2], CYCLE[i][3]));
        CHECK(params[ARPUI_PARAM_OCT] == pack94c(N_8, 1, CYCLE[i][1], 0));
    }
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled);                      /* combos: no toggle */
    CHECK(btn(UNISON, PRESS) == 0 && btn(AFTERTOUCH, PRESS) == 0);     /* without A440: stock */
}

static void test_playback_mode_and_chord_length_round_trip_and_old_programs_load(void) {
    int bad = 0;
    reset();
    for (int c = 0; c < 5; c++)
        for (int m = 0; m < 2; m++) {
            static const int BEATS[5] = { 4, 1, 2, 8, 16 };          /* by code */
            arp_set_seq_arp(&a, m); arp_set_chord_beats(&a, BEATS[c]);
            btn(A440, PRESS); btn(P2, PRESS); btn(P2, RELEASE); btn(A440, RELEASE);   /* a store */
            if (params[ARPUI_PARAM_OCT] != pack94c(N_8, 2, c, m)) bad++;
            arp_set_seq_arp(&a, !m); arp_set_chord_beats(&a, BEATS[(c + 1) % 5]); arp_set_octaves(&a, 4);   /* disturb */
            arpui_program_loaded(&u, &a);
            if (a.seq_arp != m || a.chord_beats != BEATS[c] || a.octaves != 2) bad++;
        }
    CHECK(bad == 0);
    arp_set_seq_arp(&a, 1); arp_set_chord_beats(&a, 16);
    params[ARPUI_PARAM_OCT] = 3; params[ARPUI_PARAM_PACK] = pack(N_8S, ARP_UP, 1);   /* saved before: POL, Whole */
    arpui_program_loaded(&u, &a);
    CHECK(a.seq_arp == 0 && a.chord_beats == 4 && a.octaves == 3 && rate_index(&u.rate) == N_8S);
    params[ARPUI_PARAM_OCT] = 7; params[ARPUI_PARAM_PACK] = pack(N_4B, ARP_UP, 1);   /* 1.1.0 long value: still POL, Whole */
    arpui_program_loaded(&u, &a);
    CHECK(a.seq_arp == 0 && a.chord_beats == 4 && a.octaves == 3 && rate_index(&u.rate) == N_4B);
    params[ARPUI_PARAM_OCT] = 81; params[ARPUI_PARAM_PACK] = 1;        /* out of range: no arp data */
    if (!a.enabled) tap_a440();
    CHECK(a.enabled);
    arpui_program_loaded(&u, &a);
    CHECK(!a.enabled && a.seq_arp == 0 && a.chord_beats == 4);
}

/* ---- accompany: playing over a latched arp (spec "Accompany") ------------------------- */
static void latched_arp(void) {                                        /* arp on, HOLD latch on, C E G latched */
    reset();
    tap_a440();
    note(60, 100); note(64, 100); note(67, 100);
    fake_latch = 1; arpui_hold(&u, &a, 1);
    note(60, 0); note(64, 0); note(67, 0);
    CHECK(a.enabled && a.hold && arp_pool_count(&a) == 3);
    clear_log();
}
static void acc_combo(void) { btn(A440, PRESS); btn(KEYBOARD, PRESS); btn(KEYBOARD, RELEASE); btn(A440, RELEASE); }

static void test_accompany_enters_only_over_a_latched_running_arp(void) {
    reset();                                                           /* arp off: nothing */
    btn(A440, PRESS);
    CHECK(btn(KEYBOARD, PRESS) == 1 && !u.acc && count_type(EV_D3) == 0 && count_type(EV_INT) == 0);
    CHECK(btn(KEYBOARD, RELEASE) == 1 && btn(A440, RELEASE) == 1 && !a.enabled);   /* consumed, no toggle */
    tap_a440(); note(60, 100);                                         /* arp on, held but not latched: nothing */
    acc_combo();
    CHECK(!u.acc && a.enabled);
    note(60, 0);
    latched_arp();
    acc_combo();
    CHECK(u.acc && last_d3_is(CH_A, CH_C, CH_C) && a.enabled);        /* ACC */
    CHECK(btn(KEYBOARD, PRESS) == 0 && btn(KEYBOARD, RELEASE) == 0);  /* without A440: stock */
}

static void test_accompany_keys_play_directly_and_leave_the_arp_alone(void) {
    latched_arp(); acc_combo(); clear_log();
    note(72, 90);
    CHECK(count_type(EV_VON) == 1 && last_of(EV_VON)->b == 72 && last_of(EV_VON)->c == 90);   /* straight to a voice */
    CHECK(arp_pool_count(&a) == 3);                                    /* not the arp's */
    arpui_note(&u, &a, MIDI, 74, 80);
    CHECK(count_type(EV_VON) == 2 && last_of(EV_VON)->b == 74 && arp_pool_count(&a) == 3);   /* MIDI-in too */
    ticks(250);
    CHECK(count_type(EV_VON) == 3 && last_of(EV_VON)->b != 72 && last_of(EV_VON)->b != 74);   /* the arp carries on */
    note(72, 0); arpui_note(&u, &a, MIDI, 74, 0);
    CHECK(count_type(EV_VOFF) >= 2);
    /* with a sequence: a key does not re-transpose it */
    reset(); tap_a440();
    enter_rec(); note(60, 100); note(60, 0); note(64, 100); note(64, 0); tap_a440();
    note(60, 100); fake_latch = 1; arpui_hold(&u, &a, 1); note(60, 0);
    CHECK(a.seq_trigger == 60);
    acc_combo(); clear_log();
    note(67, 100);
    CHECK(a.seq_trigger == 60 && count_type(EV_VON) == 1);
    note(67, 0);
}

static void test_accompany_pedal_sustains_and_does_not_touch_the_latch(void) {
    latched_arp();
    CHECK(arpui_sustain(&u, &a) == -1);                                /* not accompanying: not ours to answer */
    acc_combo(); clear_log();
    CHECK(arpui_sustain(&u, &a) == 0);                                 /* pedal up: no sustain, latch or not */
    fake_pedal = 1; ticks(1);
    CHECK(count_type(EV_DSP_HOLD) == 1 && last_of(EV_DSP_HOLD)->a == 1 && arpui_sustain(&u, &a) == 1);
    CHECK(a.hold && arp_pool_count(&a) == 3);                          /* the arp's latch untouched */
    ticks(100);
    CHECK(count_type(EV_DSP_HOLD) == 1);                               /* transitions only */
    fake_pedal = 0; ticks(1);
    CHECK(count_type(EV_DSP_HOLD) == 2 && last_of(EV_DSP_HOLD)->a == 0 && count_type(EV_RELEASE_WALK) == 1);
    CHECK(arpui_sustain(&u, &a) == 0 && a.hold && arp_pool_count(&a) == 3);
    fake_pedal = 1; ticks(1);
    acc_combo();                                                       /* leaving with the pedal down: nothing left stuck */
    CHECK(!u.acc && last_of(EV_DSP_HOLD)->a == 0 && count_type(EV_RELEASE_WALK) == 2);
    CHECK(arpui_sustain(&u, &a) == -1);
}

static void test_accompany_survives_the_arp_toggling_off_and_on(void) {
    latched_arp(); acc_combo(); clear_log();
    tap_a440();                                                        /* arp off: latched notes and accompaniment kept */
    CHECK(!a.enabled && u.acc && arp_pool_count(&a) == 3);
    CHECK(count_type(EV_HOLD_STOCK) == 1 && !fake_latch);             /* stock's latch (off) restored ... */
    arpui_hold(&u, &a, 0);                                             /* ... and the hold-off that follows */
    CHECK(arp_pool_count(&a) == 3 && u.acc);                           /* does not drop the arp's latched notes */
    CHECK(arpui_sustain(&u, &a) == -1);                                /* arp off: the synth's hold is stock's */
    note(72, 100); CHECK(count_type(EV_VON) == 1); note(72, 0);
    clear_log();
    tap_a440();                                                        /* arp on: resumes from the latched notes */
    CHECK(a.enabled && u.acc && count_type(EV_VON) == 1 && last_of(EV_VON)->b == 60);
    CHECK(count_type(EV_HOLD_STOCK) == 1 && fake_latch);              /* the arp's latch replayed on */
    arpui_hold(&u, &a, 1);
    note(72, 100);
    CHECK(arp_pool_count(&a) == 3 && count_type(EV_VON) == 2);        /* keys still on top */
    note(72, 0);
}

static void test_accompany_ends_when_there_is_nothing_to_play_over(void) {
    latched_arp(); acc_combo();
    CHECK(btn(HOLD, PRESS) == 0 && btn(HOLD, RELEASE) == 0);           /* the HOLD button is stock's: the latch goes off */
    fake_latch = 0; arpui_hold(&u, &a, 0);
    CHECK(!u.acc && arp_pool_count(&a) == 0);
    acc_combo();
    CHECK(!u.acc);                                                     /* no re-entry without a latched arp */
    latched_arp(); acc_combo(); arpui_all_notes_off(&u, &a);
    CHECK(!u.acc);                                                     /* CC 123 */
    latched_arp(); acc_combo(); enter_rec();
    CHECK(!u.acc && u.rec);                                            /* record mode */
    tap_a440();
    latched_arp(); acc_combo(); params[94] = 1; params[93] = 31; arpui_program_loaded(&u, &a);
    CHECK(!u.acc);                                                     /* a program load */
}

static void test_leaving_accompany_hands_the_keys_back(void) {
    latched_arp(); acc_combo();
    note(72, 100);                                                     /* a key down, playing directly */
    acc_combo();
    CHECK(!u.acc && a.enabled && arp_pool_count(&a) == 3);            /* the key down at leaving is not the arp's */
    clear_log();
    note(72, 0);
    CHECK(count_type(EV_VOFF) == 1 && last_of(EV_VOFF)->b == 72);     /* ... but it still releases */
    note(72, 100);
    CHECK(arp_pool_count(&a) == 1);                                    /* pressed again: the arp's (a re-latch, HOLD being on) */
}

/* ---- two HOLD latches ----------------------------------------------------------------- */
static void hold_press_to_stock(void) {                                /* a HOLD press the UI passes to stock */
    CHECK(btn(HOLD, PRESS) == 0 && btn(HOLD, RELEASE) == 0);
    fake_latch = !fake_latch;
    arpui_hold(&u, &a, fake_latch);                                    /* stock's handler reports the merged state */
}

static void test_arp_latch_and_stock_latch_are_remembered_separately(void) {
    reset();
    tap_a440();                                                        /* arp on, latch off */
    hold_press_to_stock();                                             /* the arp's latch on */
    CHECK(fake_latch && a.hold);
    clear_log();
    tap_a440();                                                        /* arp off: stock's latch was off -> replayed off */
    CHECK(!a.enabled && count_type(EV_HOLD_STOCK) == 1 && !fake_latch);
    arpui_hold(&u, &a, 0);
    CHECK(!a.hold);
    tap_a440();                                                        /* arp on: its latch was on -> replayed on */
    CHECK(a.enabled && count_type(EV_HOLD_STOCK) == 2 && fake_latch);
    arpui_hold(&u, &a, 1);
    CHECK(a.hold);
    hold_press_to_stock();                                             /* arp's latch off again */
    CHECK(!fake_latch);
    tap_a440();                                                        /* arp off: both off, nothing to replay */
    CHECK(count_type(EV_HOLD_STOCK) == 2 && !fake_latch);
    hold_press_to_stock();                                             /* stock's latch on, arp off */
    CHECK(fake_latch);
    tap_a440();                                                        /* arp on: its latch off -> replayed off; stock's remembered on */
    CHECK(count_type(EV_HOLD_STOCK) == 3 && !fake_latch);
    arpui_hold(&u, &a, 0);
    tap_a440();                                                        /* arp off: stock's latch back on */
    CHECK(count_type(EV_HOLD_STOCK) == 4 && fake_latch);
}

static void test_pedal_is_momentary_and_program_load_clears_both_latches(void) {
    reset();
    tap_a440();
    arpui_hold(&u, &a, 1);                                             /* pedal down (latch byte stays 0) */
    CHECK(a.hold && !fake_latch);
    tap_a440();                                                        /* arp off: no latch to remember or restore */
    CHECK(count_type(EV_HOLD_STOCK) == 0);
    arpui_hold(&u, &a, 0);
    tap_a440();
    hold_press_to_stock();                                             /* arp's latch on */
    params[94] = 1; params[93] = 31;                                   /* a program load: stock drops the latch ... */
    fake_latch = 0; arpui_hold(&u, &a, 0);
    arpui_program_loaded(&u, &a);
    tap_a440();                                                        /* ... and the arp's memory of it is gone too */
    CHECK(!a.enabled && count_type(EV_HOLD_STOCK) == 0);
    tap_a440();
    CHECK(a.enabled && count_type(EV_HOLD_STOCK) == 0 && !fake_latch);
}

static void test_long_note_values_load_and_old_programs_still_do(void) {
    reset();
    params[ARPUI_PARAM_OCT] = 2 + 4; params[ARPUI_PARAM_PACK] = pack(N_4B, ARP_UP, 1);   /* 4 bars, 2 octaves */
    arpui_program_loaded(&u, &a);
    CHECK(a.enabled && a.octaves == 2 && rate_index(&u.rate) == N_4B && a.beats_num == 16 && a.beats_den == 1);
    params[ARPUI_PARAM_OCT] = 1 + 4; params[ARPUI_PARAM_PACK] = pack(N_1, ARP_DOWN, 0);   /* Whole */
    arpui_program_loaded(&u, &a);
    CHECK(!a.enabled && a.octaves == 1 && rate_index(&u.rate) == N_1 && a.beats_num == 4 && a.mode == ARP_DOWN);
    params[ARPUI_PARAM_OCT] = 3; params[ARPUI_PARAM_PACK] = 7 * 10 + ARP_ASSIGN * 2 + 1;   /* saved before the long values: 16th S */
    arpui_program_loaded(&u, &a);
    CHECK(a.enabled && a.octaves == 3 && rate_index(&u.rate) == N_16S && a.swing);
    static const int BAD[][2] = { { 5, 30 }, { 8, 99 }, { 81, 1 }, { 127, 1 } };   /* (94, 93): n > 2 with the flag, 94 out of range */
    for (int i = 0; i < 4; i++) {
        params[ARPUI_PARAM_OCT] = BAD[i][0]; params[ARPUI_PARAM_PACK] = BAD[i][1];
        arpui_program_loaded(&u, &a);
        CHECK(!a.enabled && a.octaves == 3 && rate_index(&u.rate) == N_16S);   /* no arp data: off, untouched */
        tap_a440();
    }
}

static void test_program_load_applies_saved_state(void) {
    reset();
    params[ARPUI_PARAM_OCT] = 2;
    params[ARPUI_PARAM_PACK] = pack(N_QTR, ARP_UPDOWN, 1);
    int n = stores();
    arpui_program_loaded(&u, &a);
    CHECK(a.enabled && a.mode == ARP_UPDOWN && a.octaves == 2 && rate_index(&u.rate) == N_QTR);
    CHECK(a.beats_num == 1 && a.beats_den == 1 && a.swing == 0);
    CHECK(stores() == n);                                              /* loading does not write back */
    ticks(1);
    CHECK(last_led() == 1);
    params[ARPUI_PARAM_PACK] = pack(N_8T, ARP_ASSIGN, 0);              /* saved off, Assign, 8th T */
    params[ARPUI_PARAM_OCT] = 4;
    arpui_program_loaded(&u, &a);
    CHECK(!a.enabled && a.mode == ARP_ASSIGN && a.octaves == 4 && rate_index(&u.rate) == N_8T);
    params[ARPUI_PARAM_PACK] = pack(N_8S, ARP_UP, 1);
    arpui_program_loaded(&u, &a);
    CHECK(a.enabled && a.mode == ARP_UP && rate_index(&u.rate) == N_8S && a.beats_num == 1 && a.beats_den == 1 && a.swing == 1);
    params[ARPUI_PARAM_PACK] = pack(N_16S, ARP_UP, 1);
    arpui_program_loaded(&u, &a);
    CHECK(rate_index(&u.rate) == N_16S && a.beats_num == 1 && a.beats_den == 2 && a.swing == 1);
    params[ARPUI_PARAM_PACK] = pack(N_8, ARP_UP, 1);                   /* swing off again */
    arpui_program_loaded(&u, &a);
    CHECK(rate_index(&u.rate) == RATE_DEFAULT_INDEX && a.beats_num == 1 && a.beats_den == 2 && a.swing == 0);
}

static void test_program_without_arp_data_switches_off_and_leaves_settings(void) {
    reset();
    tap_a440();
    btn(A440, PRESS); btn(GROUP, PRESS); btn(GROUP, RELEASE); btn(P2, PRESS); btn(P2, RELEASE);
    btn(P8, PRESS); btn(P8, RELEASE); btn(A440, RELEASE);
    CHECK(a.enabled && a.mode == ARP_ASSIGN && a.octaves == 2 && rate_index(&u.rate) == N_8S);
    params[ARPUI_PARAM_OCT] = 0; params[ARPUI_PARAM_PACK] = 0;         /* a factory program */
    arpui_program_loaded(&u, &a);
    CHECK(!a.enabled && a.mode == ARP_ASSIGN && a.octaves == 2 && rate_index(&u.rate) == N_8S);   /* off, untouched */
    static const int BAD[][2] = { { 0, 99 }, { 81, 1 }, { -1, 1 }, { 1, 100 }, { 1, 127 }, { 1, -1 } };   /* (94, 93) */
    for (int i = 0; i < 6; i++) {
        tap_a440();
        CHECK(a.enabled);
        params[ARPUI_PARAM_OCT] = BAD[i][0]; params[ARPUI_PARAM_PACK] = BAD[i][1];
        arpui_program_loaded(&u, &a);
        CHECK(!a.enabled && a.mode == ARP_ASSIGN && a.octaves == 2 && rate_index(&u.rate) == N_8S);
    }
}

int main(void) {
    test_tap_toggles_arp_with_status_and_led();
    test_repeats_are_ignored();
    test_bank_group_cycle_modes_with_names();
    test_program_buttons_set_octaves_clock_and_note_value();
    test_program_7_8_step_through_every_value();
    test_a440_tune_enters_record_mode_and_a_tap_leaves();
    test_record_mode_without_steps_keeps_the_sequence();
    test_tune_without_a440_is_stock();
    test_hold_button_is_rest_or_tie_in_record_mode();
    test_a440_hold_toggles_the_stock_tone_with_the_arp_off();
    test_a440_hold_with_the_arp_on_is_consumed_and_inert();
    test_tap_while_the_tone_sounds_only_stops_it();
    test_program_load_with_the_tone_on_silences_it_and_reasserts_the_led();
    test_record_readout_counts_length_in_arp_steps();
    test_pedal_on_transition_is_rest_or_tie_in_record_mode();
    test_record_readout_persists_and_patch_display_returns_on_leaving();
    test_a440_led_blinks_in_record_mode();
    test_program6_in_record_mode_clears_and_stays();
    test_globals_leaves_record_mode();
    test_notes_while_a440_is_held_are_ordinary();
    test_suspended_is_arp_on_or_record_mode();
    test_orphan_release_is_consumed_and_other_buttons_pass();
    test_readout_of_unassigned_buttons();
    test_globals_button_abandons_the_hold();
    test_tap_tempo_steady_series();
    test_tap_tempo_averages_the_last_four_intervals();
    test_tap_tempo_clamps_and_the_two_second_limit();
    test_tap_tempo_consumption_and_scope();
    test_globals_menu_open_passes_everything();
    test_glide_is_tempo_only_with_a440_held();
    test_messages_revert_to_patch_display_after_1500_ticks();
    test_led_follows_enabled_only_when_it_changes();
    test_a440_held_at_power_on_disables_everything();
    test_a440_pressed_after_power_on_is_just_a_press();
    test_settings_are_written_to_the_patch_slots();
    test_program_load_applies_saved_state();
    test_patch_round_trip_for_every_setting();
    test_long_note_values_load_and_old_programs_still_do();
    test_unison_toggles_playback_mode_and_aftertouch_cycles_chord_length();
    test_playback_mode_and_chord_length_round_trip_and_old_programs_load();
    test_accompany_enters_only_over_a_latched_running_arp();
    test_accompany_keys_play_directly_and_leave_the_arp_alone();
    test_accompany_pedal_sustains_and_does_not_touch_the_latch();
    test_accompany_survives_the_arp_toggling_off_and_on();
    test_accompany_ends_when_there_is_nothing_to_play_over();
    test_leaving_accompany_hands_the_keys_back();
    test_arp_latch_and_stock_latch_are_remembered_separately();
    test_pedal_is_momentary_and_program_load_clears_both_latches();
    test_program_without_arp_data_switches_off_and_leaves_settings();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
