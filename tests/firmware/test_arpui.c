/* Host harness for the arp UI (docs/SPEC.md: "Arp engine" — Controls, Display, Robustness;
 * "Seq" — generator selection, transport, record mode, Back, transposition). The sequencer
 * engine's own timing is tested in test_seq.c; here the UI's routing of buttons, keys and
 * ticks to the two generators is. `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "arpui.h"
#include "platform.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake platform ------------------------------------------------------------------ */
enum { EV_VON, EV_VOFF, EV_LOFF, EV_D3, EV_INT, EV_RESTORE, EV_LED, EV_PARAM, EV_A440_STOCK, EV_HOLD_STOCK };
typedef struct { int type, a, b, c; } ev_t;
static ev_t log_[4096];
static int nlog, fake_globals_open, fake_a440_down, fake_tone_on, fake_latch;
static void push(int t, int a, int b, int c) { if (nlog < 4096) log_[nlog++] = (ev_t){t, a, b, c}; }
void plat_voice_on(int src, int note, int vel) { push(EV_VON, src, note, vel); }
void plat_voice_off(int src, int note) { push(EV_VOFF, src, note, 0); }    /* a generated note's release */
void plat_live_off(int src, int note) { push(EV_LOFF, src, note, 0); }     /* a live note's release */
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
static int params[99];
int  plat_param_read(int p) { return params[p]; }
void plat_param_store(int p, int v) { params[p] = v; push(EV_PARAM, p, v, 0); }
/* the flash: sequence blocks that were written read back; elsewhere an 8 MB part's view —
 * bootloader bytes at offset 0, blank otherwise, a 24-bit address wrapping */
static uint8_t blk[SEQMEM_SLOTS][SEQMEM_BLOCK], blk_written[SEQMEM_SLOTS], sector_buf[SEQMEM_SECTOR];
static int writes, bad_writes;
static uint32_t write_off[8], write_len[8];
static int fake_factory, fake_bank, fake_group, fake_prog;
int  plat_flash_read(uint32_t off, void *dst, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint32_t a = off + i, p = a % 0x800000u;
        if (a >= SEQMEM_BASE && a < SEQMEM_END && blk_written[(a - SEQMEM_BASE) / SEQMEM_BLOCK])
            ((uint8_t *)dst)[i] = blk[(a - SEQMEM_BASE) / SEQMEM_BLOCK][(a - SEQMEM_BASE) % SEQMEM_BLOCK];
        else
            ((uint8_t *)dst)[i] = p < 0x1000u ? (uint8_t)(p * 7u + 1u) : 0xFF;
    }
    return 0;
}
int  plat_flash_write(uint32_t off, const void *src, uint32_t len) {   /* stock's verified writer */
    if (writes < 8) { write_off[writes] = off; write_len[writes] = len; }
    writes++;
    if (off % SEQMEM_SECTOR || len % SEQMEM_SECTOR || len == 0 || off < SEQMEM_BASE || off + len > SEQMEM_END) { bad_writes++; return 4; }
    for (uint32_t i = 0; i < len; i++) {
        uint32_t a = off + i - SEQMEM_BASE;
        blk[a / SEQMEM_BLOCK][a % SEQMEM_BLOCK] = ((const uint8_t *)src)[i];
        blk_written[a / SEQMEM_BLOCK] = 1;
    }
    return 0;
}
void *plat_sector_buffer(void) { return sector_buf; }
void plat_program_slot(int *factory, int *bank, int *group, int *prog) { *factory = fake_factory; *bank = fake_bank; *group = fake_group; *prog = fake_prog; }

static int count_type(int t) { int n = 0; for (int i = 0; i < nlog; i++) n += log_[i].type == t; return n; }
static ev_t *last_of(int t) { for (int i = nlog - 1; i >= 0; i--) if (log_[i].type == t) return &log_[i]; return 0; }
static int last_d3_is(int c0, int c1, int c2) { ev_t *e = last_of(EV_D3); return e && e->a == c0 && e->b == c1 && e->c == c2; }
static int last_int(void) { ev_t *e = last_of(EV_INT); return e ? e->a : -1; }
static int last_led(void) { ev_t *e = last_of(EV_LED); return e ? (e->a == ARPUI_LED_A440 ? e->b : -2) : -1; }
static void clear_log(void) { nlog = 0; }

/* panel character codes */
enum { CH_U = 0x1E, CH_P = 0x19, CH_D = 0x0D, CH_N = 0x17, CH_R = 0x1B, CH_I = 0x12, CH_T = 0x1D,
       CH_S = 0x1C, CH_Y = 0x22, CH_O = 0x18, CH_F = 0x0F, CH_E = 0x0E, CH_C = 0x0C, CH_H = 0x11, CH_Q = 0x1A,
       CH_LO = 0x24, BLANK = 0x25, DASH = 0x26 };
enum { A440 = ARPUI_A440, GLOBALS = ARPUI_GLOBALS, GROUP = ARPUI_GROUP, BANK = ARPUI_BANK, VELOCITY = ARPUI_VELOCITY, UNISON = 0x19,
       AFTERTOUCH = ARPUI_AFTERTOUCH, OSCB_KEYB = 36, CH_A = 0x0A, CH_B = 0x0B, CH_L = 0x15,
       TUNE = ARPUI_TUNE, HOLD = ARPUI_HOLD, KEYB = ARPUI_KEYB,
       P1 = 0, P2 = 1, P3 = 2, P4 = 3, P5 = 4, P6 = 5, P7 = 6, P8 = 7,
       PRESS = 1, RELEASE = 2, REPEAT = 3, LOCAL = ARP_SRC_LOCAL, MIDI = ARP_SRC_MIDI };

static arpui_t u;
static arp_t a;
static seq_t q;
static flash_t fl;

static void reset(void) {
    arpui_init(&u); arp_init(&a); seq_init(&q); flash_init(&fl); u.flash = &fl;
    clear_log(); fake_globals_open = 0; fake_a440_down = 0; fake_tone_on = 0; fake_latch = 0;
    memset(params, 0, sizeof params);
    memset(blk_written, 0, sizeof blk_written); writes = bad_writes = 0;
    fake_factory = fake_bank = fake_group = fake_prog = 0;
    for (int i = 0; i < ARPUI_BOOT_TICKS; i++) arpui_tick(&u, &a, &q);   /* past the kill-switch window */
    clear_log();
}
static int btn(int id, int value) { return arpui_button(&u, &a, &q, id, value); }
static void tap_a440(void) { btn(A440, PRESS); btn(A440, RELEASE); }
static void ticks(int n) { while (n-- > 0) { arpui_tick(&u, &a, &q); seq_tick(&q, &a); arp_tick(&a); } }   /* as the glue's tick */

/* ---- A440 ---------------------------------------------------------------------------- */
static void test_tap_toggles_arp_with_status_and_led(void) {
    reset();
    CHECK(sizeof(arpui_t) <= 0x80);
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
static void note(int n, int vel) { arpui_note(&u, &a, &q, LOCAL, n, vel); }
/* a local key before the octave shift, as the glue sees it: a transposition command is swallowed */
static int key(int raw, int vel) { int r = arpui_key(&u, &a, &q, raw, vel); if (!r) note(raw, vel); return r; }
static void enter_rec(void) { btn(A440, PRESS); btn(TUNE, PRESS); btn(TUNE, RELEASE); btn(A440, RELEASE); }
static void select_gen(void) { btn(A440, PRESS); btn(KEYB, PRESS); btn(KEYB, RELEASE); btn(A440, RELEASE); }
/* record C, D, E as three events and leave: SEq selected, stopped */
static void record_cde(void) {
    enter_rec();
    note(60, 100); note(60, 0); note(62, 100); note(62, 0); note(64, 100); note(64, 0);
    tap_a440();
    clear_log();
}
static int von_note(int k) { for (int i = 0; i < nlog; i++) if (log_[i].type == EV_VON && k-- == 0) return log_[i].b; return -1; }

static void test_a440_tune_enters_record_mode_and_a_tap_leaves_it_stopped(void) {
    reset();
    tap_a440();                                                        /* arp on */
    clear_log();
    btn(A440, PRESS);
    CHECK(btn(TUNE, PRESS) == 1 && u.rec && last_is_rn(0));
    CHECK(!a.enabled && u.gen == ARPUI_GEN_SEQ);                       /* entering selects SEq and stops the arp */
    CHECK(btn(TUNE, REPEAT) == 1 && btn(TUNE, RELEASE) == 1);
    CHECK(btn(A440, RELEASE) == 1 && u.rec);                           /* used the hold: no toggle */
    note(60, 100);
    CHECK(count_type(EV_VON) == 1 && last_is_rn(1));                   /* sounds, count shown */
    note(60, 0);
    arpui_note(&u, &a, &q, MIDI, 64, 90); arpui_note(&u, &a, &q, MIDI, 64, 0);
    note(67, 100); note(67, 0);
    CHECK(last_is_rn(3) && count_type(EV_LOFF) == 3);                  /* live releases */
    clear_log();
    tap_a440();                                                        /* leave */
    CHECK(!u.rec && !a.enabled && !q.playing && q.len == 3 && q.ev[1].note[0] == 64);   /* SEq selected, stopped */
    CHECK(count_type(EV_INT) == 0 && count_type(EV_D3) == 0);          /* no message */
    ticks(1);
    CHECK(count_type(EV_RESTORE) == 1);                                /* the patch display is back */
    tap_a440();
    CHECK(q.playing && von_note(0) == 60);                             /* the next tap plays it from event 1 */
    tap_a440();
    CHECK(!q.playing);
}

static void test_record_mode_without_entries_keeps_the_sequence(void) {
    reset();
    enter_rec();
    note(60, 100); note(60, 0); note(64, 100); note(64, 0);
    tap_a440();
    CHECK(!u.rec && q.len == 2 && !q.playing && u.gen == ARPUI_GEN_SEQ);
    enter_rec();
    CHECK(u.rec && last_is_rn(0) && q.len == 2);                       /* the old one stays until an entry */
    enter_rec();                                                       /* A440 + Tune again leaves */
    CHECK(!u.rec && q.len == 2 && !q.playing);
    tap_a440();
    CHECK(q.playing && von_note(0) == 60);                             /* the kept recording plays */
    enter_rec();                                                       /* entering stops playback */
    CHECK(!q.playing && u.rec);
    btn(HOLD, PRESS); btn(HOLD, RELEASE);                              /* a rest alone is a recording too */
    tap_a440();
    CHECK(!u.rec && q.len == 1 && q.ev[0].n == 0);
}

static void test_tune_without_a440_is_stock(void) {
    reset();
    CHECK(btn(TUNE, PRESS) == 0 && btn(TUNE, RELEASE) == 0 && !u.rec);
}

static void test_hold_button_is_rest_or_tie_in_record_mode(void) {
    reset();
    CHECK(btn(HOLD, PRESS) == 0 && btn(HOLD, RELEASE) == 0);           /* not recording: stock HOLD */
    enter_rec();
    clear_log();
    CHECK(btn(HOLD, PRESS) == 1 && q.len == 1 && q.ev[0].n == 0 && last_is_rst());   /* no key down: a rest, rSt flashes */
    CHECK(btn(HOLD, REPEAT) == 1 && btn(HOLD, RELEASE) == 1 && q.len == 1);
    CHECK(!a.hold);                                                    /* the latch did not change */
    ticks(DISP_FLASH_TICKS - 1);
    CHECK(last_is_rst());
    ticks(1);
    CHECK(last_is_rn(1));                                              /* ... then the count */
    note(60, 100);
    CHECK(last_is_rn(2));
    CHECK(btn(HOLD, PRESS) == 1 && q.len == 2 && q.ev[1].dur == 2 && last_is_tie());   /* key down: a tie, tiE flashes */
    btn(HOLD, RELEASE);
    ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(3));                                              /* the tie counts one */
    CHECK(btn(HOLD, PRESS) == 1 && q.ev[1].dur == 3 && last_is_tie());   /* and again */
    btn(HOLD, RELEASE);
    ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(4));
    note(60, 0);
    btn(A440, PRESS);
    CHECK(btn(HOLD, PRESS) == 1 && q.len == 3 && last_is_rst());  /* with A440 held too: not the id readout */
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
    arpui_program_loaded(&u, &a, &q);
    CHECK(a.enabled && count_type(EV_A440_STOCK) == 1 && !fake_tone_on);   /* tone off first */
    ticks(1);
    CHECK(count_type(EV_LED) == 1 && last_led() == 1);
    ticks(98);
    CHECK(count_type(EV_LED) == 1);
    ticks(1);
    CHECK(count_type(EV_LED) == 2 && last_led() == 1);                /* 100 ms after the replay: on again (stock's late LED-off) */
    ticks(1000);
    CHECK(count_type(EV_LED) == 2);
    arpui_program_loaded(&u, &a, &q);                                      /* already on, tone off: nothing more */
    CHECK(count_type(EV_A440_STOCK) == 1);
}

static void test_record_readout_counts_timing_steps(void) {
    reset();
    enter_rec();
    note(60, 100); note(64, 100); note(67, 100);
    CHECK(last_is_rn(1));                                              /* a chord is one */
    btn(HOLD, PRESS); btn(HOLD, RELEASE); ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(2));                                              /* tied: two */
    note(60, 0); note(64, 0); note(67, 0);
    note(62, 100); note(62, 0);
    CHECK(last_is_rn(3));
    note(62, 100);                                                     /* a long tied chord */
    for (int i = 0; i < 70; i++) { btn(HOLD, PRESS); btn(HOLD, RELEASE); }
    note(62, 0);
    ticks(DISP_FLASH_TICKS);
    CHECK(q.len == 3 && q.ev[2].dur == 71 && last_is_rn(74));          /* 2 + 1 + 71 */
    note(64, 100);                                                     /* past 99: the plain number */
    for (int i = 0; i < 40; i++) { btn(HOLD, PRESS); btn(HOLD, RELEASE); }
    note(64, 0);
    ticks(DISP_FLASH_TICKS);
    CHECK(q.ev[3].dur == 41 && last_int() == 115 && count_type(EV_INT) == 1);   /* 74 + 41 */
    tap_a440();
    enter_rec();
    note(60, 100);
    for (int i = 0; i < 600; i++) { btn(HOLD, PRESS); btn(HOLD, RELEASE); }   /* to the 512 cap */
    note(60, 0);
    ticks(DISP_FLASH_TICKS);
    CHECK(q.total == 512 && q.ev[0].dur == 512 && last_int() == 512);
    clear_log();
    note(62, 100);                                                     /* refused: sounds, the count stays */
    CHECK(count_type(EV_VON) == 1 && q.len == 1 && last_int() == 512);
    btn(HOLD, PRESS); btn(HOLD, RELEASE);
    CHECK(q.total == 512 && count_type(EV_D3) == 0);                   /* no rSt flash either */
    note(62, 0);
}

static void test_pedal_on_transition_is_rest_or_tie_in_record_mode(void) {
    reset();
    arpui_hold(&u, &a, &q, 1);
    CHECK(a.hold == 1);                                                /* not recording: the arp's hold */
    arpui_hold(&u, &a, &q, 0);
    CHECK(a.hold == 0);
    enter_rec();
    arpui_hold(&u, &a, &q, 1);
    CHECK(q.len == 1 && q.ev[0].n == 0 && last_is_rst());        /* pedal down, no key: a rest */
    arpui_hold(&u, &a, &q, 0);
    CHECK(q.len == 1);                                             /* pedal up: nothing */
    ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(1));
    note(60, 100);
    arpui_hold(&u, &a, &q, 1);
    CHECK(q.len == 2 && q.ev[1].dur == 2 && last_is_tie());      /* pedal down, key down: a tie */
    ticks(DISP_FLASH_TICKS);
    CHECK(last_is_rn(3));
    arpui_hold(&u, &a, &q, 0);
    note(60, 0);
    tap_a440();
    CHECK(!u.rec && a.hold == 0);
}

static void test_record_readout_persists_and_patch_display_returns_on_leaving(void) {
    reset();
    enter_rec();
    clear_log();
    btn(A440, PRESS); btn(UNISON, PRESS); btn(UNISON, RELEASE); btn(A440, RELEASE);
    CHECK(last_d3_is(CH_A, CH_R, CH_P) && u.rec);                     /* ArP (style) shown, still recording */
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
    reset();
    enter_rec();
    ticks(1);
    CHECK(last_led() == 1);
    ticks(499);
    CHECK(last_led() == 1);
    ticks(1);
    CHECK(last_led() == 0);                                            /* 500 ms on */
    ticks(500);
    CHECK(last_led() == 1);                                            /* 500 ms off */
    tap_a440();                                                        /* leave: SEq selected, stopped */
    ticks(1);
    CHECK(last_led() == 0);
    clear_log();
    tap_a440();                                                        /* nothing recorded: --- and no start */
    CHECK(!q.playing && last_d3_is(DASH, DASH, DASH));
    ticks(1);
    CHECK(count_type(EV_LED) == 0);
    select_gen(); tap_a440(); ticks(1);                                /* ArP on: lit */
    CHECK(a.enabled && last_led() == 1);
    enter_rec(); ticks(2000);
    tap_a440(); ticks(1);
    CHECK(last_led() == 0 && !a.enabled);                              /* SEq, stopped: dark */
}

static void test_program6_in_record_mode_clears_and_stays(void) {
    reset();
    tap_a440();
    enter_rec();
    note(60, 100); note(60, 0); note(64, 100); note(64, 0);
    btn(A440, PRESS); btn(P6, PRESS); btn(P6, RELEASE); btn(A440, RELEASE);
    CHECK(u.rec && q.len == 0 && last_is_rn(0) && u.gen == ARPUI_GEN_SEQ);
    note(67, 100); note(67, 0);
    CHECK(q.len == 1 && last_is_rn(1));
    tap_a440();
    CHECK(!u.rec && q.len == 1 && q.ev[0].note[0] == 67);
}

static void test_globals_leaves_record_mode(void) {
    reset();
    enter_rec();
    note(60, 100); note(60, 0);
    CHECK(btn(GLOBALS, PRESS) == 0 && !u.rec && q.len == 1);          /* the menu opens as stock */
    fake_globals_open = 1;
    CHECK(btn(GLOBALS, RELEASE) == 0);
}

static void test_notes_while_a440_is_held_are_ordinary_with_arp_selected(void) {
    reset();
    tap_a440();                                                        /* arp on */
    clear_log();
    btn(A440, PRESS);
    CHECK(key(60, 100) == 0);                                          /* not a command: ArP is selected */
    CHECK(count_type(EV_VON) == 1 && q.len == 0 && arp_pool_count(&a) == 1);   /* arpeggiated, not recorded */
    key(60, 0);
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled && !u.rec);            /* a tap: the note did not use the hold */
}

static void test_suspended_is_arp_on_or_record_mode(void) {
    reset();
    CHECK(!arpui_suspended(&u, &a, &q));
    tap_a440();
    CHECK(arpui_suspended(&u, &a, &q));
    tap_a440();
    CHECK(!arpui_suspended(&u, &a, &q));
    enter_rec();
    CHECK(arpui_suspended(&u, &a, &q));
    tap_a440();
    CHECK(!arpui_suspended(&u, &a, &q));
    record_cde(); tap_a440();
    CHECK(q.playing && arpui_suspended(&u, &a, &q));                   /* the sequencer runs: the synth's hold is suspended */
    tap_a440();
    CHECK(!q.playing && !arpui_suspended(&u, &a, &q));
    arp_set_ext(&a, 1); tap_a440();
    CHECK(q.armed && arpui_suspended(&u, &a, &q));                     /* armed counts as running */
    tap_a440(); arp_set_ext(&a, 0);
    select_gen();                                                      /* ArP selected: the sequencer's state is moot */
    CHECK(!arpui_suspended(&u, &a, &q));
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
    CHECK(btn(9, PRESS) == 1 && last_int() == 9);                      /* an unassigned filter button */
    CHECK(btn(9, RELEASE) == 1);
    btn(A440, RELEASE);
    CHECK(!a.enabled);
}

/* knobs: any but Glide Rate and Amp Decay shows P and its pot id while A440 is held, consumed */
static int pot(int id, int raw) { int r = arpui_pot_store(&u, &a, &q, id, raw); return r + 2 * arpui_pot_change(&u, &a, &q, id); }
static void test_readout_of_knobs(void) {
    reset();
    CHECK(pot(9, 400) == 0 && pot(0, 500) == 0 && pot(1, 500) == 0);  /* A440 up: every knob is stock */
    CHECK(count_type(EV_D3) == 0 && count_type(EV_INT) == 0);
    btn(A440, PRESS);
    CHECK(pot(9, 400) == 3 && last_d3_is(CH_P, BLANK, 9));             /* cutoff: both hooks consumed, P  9 */
    CHECK(pot(9, 420) == 3 && last_d3_is(CH_P, BLANK, 9));             /* refreshed while it turns */
    CHECK(pot(16, 300) == 3 && last_d3_is(CH_P, 1, 6));            /* amp attack: P16 */
    CHECK(pot(27, 0) == 3 && last_d3_is(CH_P, 2, 7));                  /* the last knob */
    CHECK(pot(0, 800) == 3 && last_d3_is(CH_P, BLANK, 0));             /* Volume too */
    CHECK(pot(1, 512) == 3 && last_d3_is(CH_P, BLANK, 1));             /* and Master Tune */
    CHECK(count_type(EV_PARAM) == 0);                                  /* no setting changed */
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled);                      /* used the hold: no toggle */
    CHECK(pot(9, 500) == 0);                                           /* stock again */
    tap_a440();                                                        /* arp on: the readout is the same while it runs */
    btn(A440, PRESS);
    CHECK(pot(12, 100) == 3 && last_d3_is(CH_P, 1, 2) && a.enabled);
    btn(A440, RELEASE);
    CHECK(a.enabled);
    fake_globals_open = 1;                                             /* Globals menu: A440 is stock's, knobs are stock */
    btn(A440, PRESS);
    CHECK(pot(9, 300) == 0);
    btn(A440, RELEASE);
    fake_globals_open = 0;
    arpui_init(&u); arp_init(&a); seq_init(&q); clear_log();           /* the kill switch: stock */
    fake_a440_down = 1;
    ticks(ARPUI_BOOT_TICKS);
    CHECK(u.kill && pot(9, 300) == 0);
    fake_a440_down = 0;
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
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 500) == 0);         /* arp off, no A440: stock glide */
    CHECK(arpui_pot_change(&u, &a, &q, ARPUI_POT_GLIDE) == 0 && a.bpm == 120);
    btn(A440, PRESS);                                                  /* arp off + A440 held: tempo */
    clear_log();
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 0) == 1 && a.bpm == 40 && last_int() == 40);   /* from 500 (167) down through 120: caught */
    CHECK(arpui_pot_change(&u, &a, &q, ARPUI_POT_GLIDE) == 1);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 1023);
    CHECK(a.bpm == 300 && last_int() == 300);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 512);
    CHECK(a.bpm == 40 + (260 * 512 + 511) / 1023);
    int bpm0 = a.bpm;
    CHECK(arpui_pot_store(&u, &a, &q, 0x15, 900) == 1 && a.bpm == bpm0);   /* another pot: its id readout, not the tempo */
    CHECK(arpui_pot_change(&u, &a, &q, 0x15) == 1);
    CHECK(count_type(EV_PARAM) == 0);                                  /* BPM is not saved */
    btn(A440, RELEASE);
    CHECK(!a.enabled);                                                 /* the pot used the hold: no toggle */
    tap_a440();                                                        /* arp on, internal clock */
    int bpm = a.bpm;
    CHECK(a.enabled && arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 1023) == 0);   /* no A440: glide */
    CHECK(arpui_pot_change(&u, &a, &q, ARPUI_POT_GLIDE) == 0 && a.bpm == bpm);
    arp_set_ext(&a, 1);                                                /* under Syn, A440 held: consumed, inert */
    btn(A440, PRESS);
    clear_log();
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 100) == 1 && a.bpm == bpm);
    CHECK(last_d3_is(CH_S, CH_Y, CH_N) && count_type(EV_INT) == 0);   /* Syn as the hint */
    CHECK(arpui_pot_change(&u, &a, &q, ARPUI_POT_GLIDE) == 1 && a.bpm == bpm);
    btn(A440, RELEASE);
    CHECK(a.enabled);                                                  /* no toggle */
    arp_set_ext(&a, 0);
    btn(A440, PRESS);                                                  /* int again: tempo (the knob sits at 100 = 65, the tempo is 170) */
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 600) == 1 && a.bpm == 40 + (260 * 600 + 511) / 1023);   /* 65 -> 192 crosses 170: caught */
    btn(A440, RELEASE);
    arp_set_ext(&a, 1);
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 600) == 0 && arpui_pot_change(&u, &a, &q, ARPUI_POT_GLIDE) == 0);
    btn(A440, PRESS);                                                  /* Globals opened mid-hold: stock */
    fake_globals_open = 1;
    btn(P2, PRESS);
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 600) == 0 && arpui_pot_change(&u, &a, &q, ARPUI_POT_GLIDE) == 0);
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
    arpui_init(&u); arp_init(&a); seq_init(&q); clear_log();                         /* kill switch: stock */
    fake_a440_down = 1; ticks(10); fake_a440_down = 0; ticks(ARPUI_BOOT_TICKS);
    CHECK(u.kill);
    btn(A440, PRESS);
    CHECK(btn(VELOCITY, PRESS) == 0 && btn(VELOCITY, RELEASE) == 0);
    ticks(500);
    CHECK(btn(VELOCITY, PRESS) == 0 && a.bpm == 120 && count_type(EV_D3) == 0 && count_type(EV_INT) == 0);
}

/* ---- tempo pickup: the knob catches the tempo instead of jumping to it ----------------- */
static int raw_bpm(int raw) { return 40 + (260 * raw + 511) / 1023; }

static void test_glide_picks_the_tempo_up_instead_of_jumping(void) {
    reset();                                                           /* 120 BPM */
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 900);                     /* A440 up: glide — but the knob is known to sit at 900 (269) */
    btn(A440, PRESS);
    clear_log();
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 800) == 1 && a.bpm == 120 && last_int() == 120);   /* above the tempo: inert, the target shown */
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 400);                     /* 141: still above */
    CHECK(a.bpm == 120 && last_int() == 120);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 300);                     /* 116: crossed 120 on the way down */
    CHECK(a.bpm == raw_bpm(300) && last_int() == a.bpm);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 600);                     /* caught: it follows */
    CHECK(a.bpm == raw_bpm(600));
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled);                      /* used the hold */
    btn(A440, PRESS);                                                  /* a new hold with the knob where the tempo is: live at once */
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 700);
    CHECK(a.bpm == raw_bpm(700));
    tap(); ticks(1000); tap();                                         /* a tap series sets 60: the knob (700 = 218) is far away again */
    CHECK(a.bpm == 60);
    clear_log();
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 650);                     /* 205: inert, re-armed by the tap */
    CHECK(a.bpm == 60 && last_int() == 60);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 80);                      /* exactly 60: reached */
    CHECK(a.bpm == 60);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 200);                     /* follows */
    CHECK(a.bpm == raw_bpm(200));
    btn(A440, RELEASE);
    arp_set_bpm(&a, 150);                                              /* the tempo changed elsewhere between holds */
    btn(A440, PRESS);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 250);                     /* 104, from 200 (90): below, inert */
    CHECK(a.bpm == 150);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 500);                     /* 167: crossed from below */
    CHECK(a.bpm == raw_bpm(500));
    btn(A440, RELEASE);
    arpui_init(&u); arp_init(&a); seq_init(&q); clear_log();           /* power-up: the knob's position is unknown ... */
    ticks(ARPUI_BOOT_TICKS);
    btn(A440, PRESS);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 700);                     /* ... so the first report alone decides: 218 is not 120 */
    CHECK(a.bpm == 120);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 200);                     /* 90: crossed */
    CHECK(a.bpm == raw_bpm(200));
    btn(A440, RELEASE);
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
    arpui_init(&u); arp_init(&a); seq_init(&q); clear_log();
    fake_a440_down = 1;
    ticks(10);
    fake_a440_down = 0;
    ticks(ARPUI_BOOT_TICKS);
    CHECK(u.kill);
    CHECK(btn(A440, PRESS) == 0 && btn(A440, RELEASE) == 0 && !a.enabled);
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 1023) == 0);
    CHECK(count_type(EV_LED) == 0 && count_type(EV_D3) == 0 && count_type(EV_INT) == 0);
    arpui_init(&u); arp_init(&a); seq_init(&q); clear_log();
    ticks(ARPUI_BOOT_TICKS + 1);
    fake_a440_down = 1;                                                /* after the window: just a button */
    ticks(10);
    CHECK(!u.kill);
    fake_a440_down = 0;
    CHECK(ARPUI_BOOT_TICKS == 3000);                                   /* the panel link can come up late */
}

static void test_a440_pressed_after_power_on_is_just_a_press(void) {
    arpui_init(&u); arp_init(&a); seq_init(&q); clear_log();
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
/* list index 0 = 4 bars ... 12 = 32nd; patch memory: one number
 * V = on + 2 mode + 10 (octaves - 1) + 40 note + 520 gate, 93 = V mod 128, 94 = 1 + V div 128 (0 = no arp data) */
enum { N_4B, N_2B, N_1, N_HALF, N_QTR, N_8D, N_8, N_8S, N_8T, N_16, N_16S, N_16T, N_32 };
static int pv(int on, int mode, int oct, int note, int g) { return on + 2 * mode + 10 * (oct - 1) + 40 * note + 520 * g; }
static void set_patch(int v) { params[ARPUI_PARAM_PACK] = v % 128; params[ARPUI_PARAM_OCT] = 1 + v / 128; }
static int patch(void) { return params[ARPUI_PARAM_OCT] ? (params[ARPUI_PARAM_OCT] - 1) * 128 + params[ARPUI_PARAM_PACK] : -1; }
static int raw_gate(int raw) { return (19 * raw + 511) / 1023; }    /* Amp Decay's position as a gate index */
static void decay(int raw) { arpui_pot_store(&u, &a, &q, ARPUI_POT_DECAY, raw); arpui_pot_change(&u, &a, &q, ARPUI_POT_DECAY); }

static void test_settings_are_written_to_the_patch_slots(void) {
    reset();
    CHECK(stores() == 0);
    tap_a440();                                                        /* on, Up, 1 octave, 8th, 50 % */
    CHECK(patch() == pv(1, ARP_UP, 1, N_8, 9));
    btn(A440, PRESS); btn(BANK, PRESS); btn(BANK, RELEASE);            /* Down */
    CHECK(patch() == pv(1, ARP_DOWN, 1, N_8, 9));
    btn(P3, PRESS); btn(P3, RELEASE);                                  /* 3 octaves */
    CHECK(patch() == pv(1, ARP_DOWN, 3, N_8, 9));
    btn(P8, PRESS); btn(P8, RELEASE);                                  /* shorter: 8th S */
    CHECK(patch() == pv(1, ARP_DOWN, 3, N_8S, 9));
    for (int i = 0; i < 5; i++) { btn(P8, PRESS); btn(P8, RELEASE); }  /* to 32nd */
    CHECK(patch() == pv(1, ARP_DOWN, 3, N_32, 9));
    for (int i = 0; i < 12; i++) { btn(P7, PRESS); btn(P7, RELEASE); } /* longer to 4 bars */
    CHECK(patch() == pv(1, ARP_DOWN, 3, N_4B, 9));
    decay(500); decay(1023);                                           /* the gate: 100 % */
    CHECK(patch() == pv(1, ARP_DOWN, 3, N_4B, 19));
    btn(GROUP, PRESS); btn(GROUP, RELEASE); btn(GROUP, PRESS); btn(GROUP, RELEASE);   /* Up, Assign */
    CHECK(a.mode == ARP_ASSIGN && patch() == pv(1, ARP_ASSIGN, 3, N_4B, 19));
    btn(BANK, PRESS); btn(BANK, RELEASE);                              /* Assign -> Up */
    CHECK(a.mode == ARP_UP && patch() == pv(1, ARP_UP, 3, N_4B, 19));
    btn(GROUP, PRESS); btn(GROUP, RELEASE);                            /* Assign again */
    int n = stores();
    btn(P5, PRESS); btn(P5, RELEASE);                                  /* clock source: not saved */
    btn(A440, RELEASE);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 900);                     /* BPM: not saved */
    arp_set_ext(&a, 0);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_GLIDE, 900);
    CHECK(stores() == n);
    tap_a440();                                                        /* off: remembered as off */
    CHECK(patch() == pv(0, ARP_ASSIGN, 3, N_4B, 19));
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
                for (int oct = 1; oct <= 4; oct++)
                    for (int g = 0; g < ARP_GATES; g++) {
                        int num, den;
                        arp_set_mode(&a, mode); arp_set_octaves(&a, oct); arp_enable(&a, on);
                        rate_set_index(&u.rate, note); u.gate = (uint8_t)g;
                        btn(A440, PRESS); btn(P1 + oct - 1, PRESS); btn(P1 + oct - 1, RELEASE); btn(A440, RELEASE);
                        if (patch() != pv(on, mode, oct, note, g) || params[ARPUI_PARAM_PACK] > 127) bad++;
                        arp_set_mode(&a, (mode + 2) % ARP_MODES); arp_set_octaves(&a, oct % 4 + 1);   /* disturb */
                        arp_enable(&a, !on); rate_set_index(&u.rate, (note + 3) % RATE_COUNT);
                        u.gate = (uint8_t)((g + 7) % ARP_GATES); arp_set_gate(&a, u.gate);
                        arpui_program_loaded(&u, &a, &q);
                        rate_beats(&u.rate, &num, &den);
                        if (a.mode != mode || rate_index(&u.rate) != note || a.enabled != on || a.octaves != oct
                            || a.beats_num != num || a.beats_den != den || a.swing != rate_swing(&u.rate)
                            || u.gate != g || a.gate != g) bad++;
                    }
    CHECK(bad == 0);
    CHECK(pv(1, ARP_ASSIGN, 4, N_32, 19) == 10399);                    /* the largest value: 94 = 82 */
    set_patch(10399);
    CHECK(params[ARPUI_PARAM_OCT] == 82 && params[ARPUI_PARAM_PACK] == 31);
}

/* ---- two HOLD latches ----------------------------------------------------------------- */
static void hold_press_to_stock(void) {                                /* a HOLD press the UI passes to stock */
    CHECK(btn(HOLD, PRESS) == 0 && btn(HOLD, RELEASE) == 0);
    fake_latch = !fake_latch;
    arpui_hold(&u, &a, &q, fake_latch);                                    /* stock's handler reports the merged state */
}

static void test_arp_latch_and_stock_latch_are_remembered_separately(void) {
    reset();
    tap_a440();                                                        /* arp on, latch off */
    hold_press_to_stock();                                             /* the arp's latch on */
    CHECK(fake_latch && a.hold);
    clear_log();
    tap_a440();                                                        /* arp off: stock's latch was off -> replayed off */
    CHECK(!a.enabled && count_type(EV_HOLD_STOCK) == 1 && !fake_latch);
    arpui_hold(&u, &a, &q, 0);
    CHECK(!a.hold);
    tap_a440();                                                        /* arp on: its latch was on -> replayed on */
    CHECK(a.enabled && count_type(EV_HOLD_STOCK) == 2 && fake_latch);
    arpui_hold(&u, &a, &q, 1);
    CHECK(a.hold);
    hold_press_to_stock();                                             /* arp's latch off again */
    CHECK(!fake_latch);
    tap_a440();                                                        /* arp off: both off, nothing to replay */
    CHECK(count_type(EV_HOLD_STOCK) == 2 && !fake_latch);
    hold_press_to_stock();                                             /* stock's latch on, arp off */
    CHECK(fake_latch);
    tap_a440();                                                        /* arp on: its latch off -> replayed off; stock's remembered on */
    CHECK(count_type(EV_HOLD_STOCK) == 3 && !fake_latch);
    arpui_hold(&u, &a, &q, 0);
    tap_a440();                                                        /* arp off: stock's latch back on */
    CHECK(count_type(EV_HOLD_STOCK) == 4 && fake_latch);
}

static void test_pedal_is_momentary_and_program_load_clears_both_latches(void) {
    reset();
    tap_a440();
    arpui_hold(&u, &a, &q, 1);                                             /* pedal down (latch byte stays 0) */
    CHECK(a.hold && !fake_latch);
    tap_a440();                                                        /* arp off: no latch to remember or restore */
    CHECK(count_type(EV_HOLD_STOCK) == 0);
    arpui_hold(&u, &a, &q, 0);
    tap_a440();
    hold_press_to_stock();                                             /* arp's latch on */
    params[94] = 1; params[93] = 31;                                   /* a program load: stock drops the latch ... */
    fake_latch = 0; arpui_hold(&u, &a, &q, 0);
    arpui_program_loaded(&u, &a, &q);
    tap_a440();                                                        /* ... and the arp's memory of it is gone too */
    CHECK(!a.enabled && count_type(EV_HOLD_STOCK) == 0);
    tap_a440();
    CHECK(a.enabled && count_type(EV_HOLD_STOCK) == 0 && !fake_latch);
}

static void test_long_note_values_load_and_bad_values_are_no_arp_data(void) {
    reset();
    set_patch(pv(1, ARP_UP, 2, N_4B, 9));                              /* 4 bars, 2 octaves */
    arpui_program_loaded(&u, &a, &q);
    CHECK(a.enabled && a.octaves == 2 && rate_index(&u.rate) == N_4B && a.beats_num == 16 && a.beats_den == 1);
    set_patch(pv(0, ARP_DOWN, 1, N_1, 0));                             /* Whole, 5 % */
    arpui_program_loaded(&u, &a, &q);
    CHECK(!a.enabled && a.octaves == 1 && rate_index(&u.rate) == N_1 && a.beats_num == 4 && a.mode == ARP_DOWN && u.gate == 0);
    set_patch(pv(1, ARP_ASSIGN, 3, N_16S, 19));                        /* 16th S, 100 % */
    arpui_program_loaded(&u, &a, &q);
    CHECK(a.enabled && a.octaves == 3 && rate_index(&u.rate) == N_16S && a.swing && u.gate == 19 && a.gate == 19);
    static const int BAD[][2] = { { 83, 0 }, { 82, 32 }, { 127, 1 }, { 0, 5 }, { 1, 128 } };   /* (94, 93): 94 too large, a gate of 20, no data, 93 out of range */
    for (int i = 0; i < 5; i++) {
        params[ARPUI_PARAM_OCT] = BAD[i][0]; params[ARPUI_PARAM_PACK] = BAD[i][1];
        arpui_program_loaded(&u, &a, &q);
        CHECK(!a.enabled && a.octaves == 3 && rate_index(&u.rate) == N_16S && u.gate == 19);   /* no arp data: off, untouched */
        tap_a440();
    }
}

static void test_program_load_applies_saved_state(void) {
    reset();
    set_patch(pv(1, ARP_UPDOWN, 2, N_QTR, 9));
    int n = stores();
    arpui_program_loaded(&u, &a, &q);
    CHECK(a.enabled && a.mode == ARP_UPDOWN && a.octaves == 2 && rate_index(&u.rate) == N_QTR);
    CHECK(a.beats_num == 1 && a.beats_den == 1 && a.swing == 0);
    CHECK(stores() == n);                                              /* loading does not write back */
    ticks(1);
    CHECK(last_led() == 1);
    set_patch(pv(0, ARP_ASSIGN, 4, N_8T, 4));                          /* saved off, Assign, 8th T, 25 % */
    arpui_program_loaded(&u, &a, &q);
    CHECK(!a.enabled && a.mode == ARP_ASSIGN && a.octaves == 4 && rate_index(&u.rate) == N_8T && u.gate == 4);
    set_patch(pv(1, ARP_UP, 4, N_8S, 9));
    arpui_program_loaded(&u, &a, &q);
    CHECK(a.enabled && a.mode == ARP_UP && rate_index(&u.rate) == N_8S && a.beats_num == 1 && a.beats_den == 1 && a.swing == 1);
    set_patch(pv(1, ARP_UP, 4, N_16S, 9));
    arpui_program_loaded(&u, &a, &q);
    CHECK(rate_index(&u.rate) == N_16S && a.beats_num == 1 && a.beats_den == 2 && a.swing == 1);
    set_patch(pv(1, ARP_UP, 4, N_8, 9));                               /* swing off again */
    arpui_program_loaded(&u, &a, &q);
    CHECK(rate_index(&u.rate) == RATE_DEFAULT_INDEX && a.beats_num == 1 && a.beats_den == 2 && a.swing == 0 && u.gate == 9);
}

static void test_program_without_arp_data_switches_off_and_leaves_settings(void) {
    reset();
    tap_a440();
    btn(A440, PRESS); btn(GROUP, PRESS); btn(GROUP, RELEASE); btn(P2, PRESS); btn(P2, RELEASE);
    btn(P8, PRESS); btn(P8, RELEASE); decay(500); decay(0); btn(A440, RELEASE);
    CHECK(a.enabled && a.mode == ARP_ASSIGN && a.octaves == 2 && rate_index(&u.rate) == N_8S && u.gate == 0);
    params[ARPUI_PARAM_OCT] = 0; params[ARPUI_PARAM_PACK] = 0;         /* a factory program */
    arpui_program_loaded(&u, &a, &q);
    CHECK(!a.enabled && a.mode == ARP_ASSIGN && a.octaves == 2 && rate_index(&u.rate) == N_8S && u.gate == 0);   /* off, untouched */
    static const int BAD[][2] = { { 0, 99 }, { 83, 1 }, { -1, 1 }, { 82, 32 }, { 1, 128 }, { 1, -1 } };   /* (94, 93) */
    for (int i = 0; i < 6; i++) {
        tap_a440();
        CHECK(a.enabled);
        params[ARPUI_PARAM_OCT] = BAD[i][0]; params[ARPUI_PARAM_PACK] = BAD[i][1];
        arpui_program_loaded(&u, &a, &q);
        CHECK(!a.enabled && a.mode == ARP_ASSIGN && a.octaves == 2 && rate_index(&u.rate) == N_8S && u.gate == 0);
    }
}

/* ---- the two generators (spec "Seq") -------------------------------------------------- */
static void test_keyboard_selects_the_generator(void) {
    reset();
    CHECK(u.gen == ARPUI_GEN_ARP);
    btn(A440, PRESS);
    CHECK(btn(KEYB, PRESS) == 1 && u.gen == ARPUI_GEN_ARP && last_d3_is(DASH, DASH, DASH));   /* nothing recorded: ---, ArP stays */
    CHECK(btn(KEYB, REPEAT) == 1 && btn(KEYB, RELEASE) == 1);
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled);                      /* used the hold */
    record_cde();
    CHECK(u.gen == ARPUI_GEN_SEQ && !q.playing && !a.enabled);         /* recording selected SEq, left it stopped */
    select_gen();
    CHECK(u.gen == ARPUI_GEN_ARP && last_d3_is(CH_A, CH_R, CH_P));
    select_gen();
    CHECK(u.gen == ARPUI_GEN_SEQ && last_d3_is(CH_S, CH_E, CH_Q));
    CHECK(btn(KEYB, PRESS) == 0 && btn(KEYB, RELEASE) == 0);           /* without A440: stock */
}

static void test_tap_starts_and_stops_the_sequencer(void) {
    reset(); record_cde();
    tap_a440();
    CHECK(q.playing && last_int() == 120 && count_type(EV_VON) == 1 && von_note(0) == 60);   /* event 1 at once, BPM shown */
    ticks(1);
    CHECK(last_led() == 1);
    ticks(249);
    CHECK(count_type(EV_VON) == 2 && von_note(1) == 62);               /* 8th steps */
    tap_a440();
    CHECK(!q.playing && last_d3_is(CH_O, CH_F, CH_F) && !a.enabled);   /* OFF; the arp was never involved */
    ticks(1);
    CHECK(last_led() == 0);
    ticks(1000);
    CHECK(count_type(EV_VON) == 2);
    arp_set_ext(&a, 1);
    clear_log();
    tap_a440();
    CHECK(q.armed && !q.playing && last_d3_is(CH_S, CH_Y, CH_N));     /* armed under Syn */
    ticks(1);
    CHECK(last_led() == 1 && count_type(EV_VON) == 0);
    arpui_realtime(&u, &a, &q, 0xFA, 0);
    arpui_realtime(&u, &a, &q, 0xF8, 0);
    CHECK(q.playing && count_type(EV_VON) == 1 && von_note(0) == 60);  /* the first clock after Start */
    arpui_realtime(&u, &a, &q, 0xF8, 1);                               /* the other port: locked out */
    for (int i = 0; i < 11; i++) arpui_realtime(&u, &a, &q, 0xF8, 0);
    CHECK(count_type(EV_VON) == 1);
    arpui_realtime(&u, &a, &q, 0xF8, 0);
    CHECK(count_type(EV_VON) == 2 && von_note(1) == 62);               /* 12 clocks later */
    tap_a440();
    CHECK(!q.armed && !q.playing && last_d3_is(CH_O, CH_F, CH_F));
}

static void test_switching_generators_stops_the_running_one(void) {
    reset(); record_cde();
    select_gen();                                                      /* ArP */
    tap_a440();                                                        /* arp on */
    note(48, 100);
    CHECK(a.enabled && count_type(EV_VON) == 1);                       /* the arp sounds the key */
    clear_log();
    select_gen();                                                      /* SEq: the arp is stopped, the key goes live */
    CHECK(!a.enabled && u.gen == ARPUI_GEN_SEQ && !q.playing);
    CHECK(count_type(EV_VOFF) == 1 && count_type(EV_VON) == 1 && von_note(0) == 48);   /* arp off: the held key sounds directly */
    note(48, 0);
    CHECK(count_type(EV_LOFF) == 1);                                   /* a live release */
    tap_a440(); ticks(1);
    CHECK(q.playing && last_led() == 1);
    clear_log();
    select_gen();                                                      /* ArP: the sequencer stops, the arp stays off */
    CHECK(u.gen == ARPUI_GEN_ARP && !q.playing && !a.enabled && count_type(EV_VOFF) == 1);
    ticks(1);
    CHECK(last_led() == 0);
    tap_a440();
    CHECK(a.enabled);
}

static void test_tone_combo_follows_the_selected_generators_state(void) {
    reset(); record_cde();
    btn(A440, PRESS); btn(HOLD, PRESS); btn(HOLD, RELEASE); btn(A440, RELEASE);   /* SEq stopped: the tone */
    CHECK(fake_tone_on && count_type(EV_A440_STOCK) == 1 && !q.playing);
    tap_a440();
    CHECK(!fake_tone_on && !q.playing && count_type(EV_A440_STOCK) == 2);   /* a tap only stops the tone */
    tap_a440();
    CHECK(q.playing);
    btn(A440, PRESS); btn(HOLD, PRESS); btn(HOLD, RELEASE); btn(A440, RELEASE);   /* running: consumed, inert */
    CHECK(!fake_tone_on && count_type(EV_A440_STOCK) == 2 && q.playing);
}

static void test_live_notes_sustain_under_hold_while_the_sequence_runs_generated_ones_do_not(void) {
    reset(); record_cde();
    hold_press_to_stock();                                             /* HOLD on (stock's latch, SEq stopped) */
    CHECK(a.hold && fake_latch);
    note(50, 100); note(50, 0);
    CHECK(count_type(EV_LOFF) == 1);                                   /* stopped: the release goes to stock, whose hold sustains it */
    tap_a440();                                                        /* SEq playing, HOLD on */
    CHECK(q.playing && arpui_suspended(&u, &a, &q) && count_type(EV_VON) == 2);
    note(52, 100);
    note(52, 0);
    CHECK(count_type(EV_LOFF) == 1);                                   /* deferred: the engine sustains it */
    ticks(125);
    CHECK(count_type(EV_VOFF) == 1 && count_type(EV_LOFF) == 1);       /* the chord's gate: a generated release, HOLD or not */
    ticks(125);
    CHECK(count_type(EV_VON) == 4);                                    /* the next step sounds */
    note(52, 100);                                                     /* pressed again: released first, then sounds */
    CHECK(count_type(EV_LOFF) == 2 && count_type(EV_VON) == 5);
    note(52, 0);
    tap_a440();                                                        /* stopped with HOLD on: still sustained by us */
    CHECK(!q.playing && count_type(EV_LOFF) == 2);
    note(54, 100); note(54, 0);
    CHECK(count_type(EV_LOFF) == 3);                                   /* a new release is stock's (its hold is back) */
    hold_press_to_stock();                                             /* HOLD off */
    CHECK(!a.hold && count_type(EV_LOFF) == 4);                        /* ours released now */
    hold_press_to_stock();                                             /* HOLD on again, SEq stopped */
    tap_a440();                                                        /* playing */
    note(55, 100); note(55, 0);
    CHECK(count_type(EV_LOFF) == 4);                                   /* sustained */
    select_gen();                                                      /* ArP: the sequencer stops; the note stays until HOLD off */
    CHECK(count_type(EV_LOFF) == 4);
    tap_a440();                                                        /* the arp starts: live notes are cut as always */
    CHECK(a.enabled && count_type(EV_LOFF) == 5);
    tap_a440(); fake_latch = 0; arpui_hold(&u, &a, &q, 0);
    enter_rec();                                                       /* recording: pedal / HOLD are rest and tie, notes release on key-up */
    arpui_hold(&u, &a, &q, 1);
    note(57, 100); note(57, 0);
    CHECK(count_type(EV_LOFF) == 6);
    arpui_hold(&u, &a, &q, 0);
}

static void test_group_alone_is_back_in_record_mode(void) {
    reset();
    CHECK(btn(GROUP, PRESS) == 0 && btn(GROUP, RELEASE) == 0);         /* not recording: stock */
    enter_rec();
    note(60, 100); note(64, 100);
    btn(HOLD, PRESS); btn(HOLD, RELEASE); btn(HOLD, PRESS); btn(HOLD, RELEASE);   /* chord, two ties */
    note(60, 0); note(64, 0);
    btn(HOLD, PRESS); btn(HOLD, RELEASE);                              /* a rest */
    ticks(DISP_FLASH_TICKS);
    CHECK(q.len == 2 && q.total == 4 && last_is_rn(4));
    CHECK(btn(GROUP, PRESS) == 1 && q.len == 1 && q.total == 3 && last_is_rn(3));   /* the rest goes, the count at once */
    CHECK(btn(GROUP, REPEAT) == 1 && q.total == 3);                    /* repeats ignored */
    CHECK(btn(GROUP, RELEASE) == 1);
    btn(GROUP, PRESS); btn(GROUP, RELEASE);
    CHECK(q.ev[0].dur == 2 && last_is_rn(2));                          /* a tie comes off */
    btn(GROUP, PRESS); btn(GROUP, RELEASE);
    CHECK(q.ev[0].dur == 1 && last_is_rn(1));
    btn(GROUP, PRESS); btn(GROUP, RELEASE);
    CHECK(q.len == 0 && last_is_rn(0));                                /* the chord goes */
    CHECK(btn(GROUP, PRESS) == 1 && btn(GROUP, RELEASE) == 1 && q.len == 0 && last_is_rn(0));   /* nothing left: nothing */
    CHECK(btn(BANK, PRESS) == 0 && btn(BANK, RELEASE) == 0);           /* Bank alone stays stock */
    note(67, 100);                                                     /* an open chord ... */
    CHECK(q.len == 1 && last_is_rn(1));
    btn(GROUP, PRESS); btn(GROUP, RELEASE);                            /* ... removed and closed */
    CHECK(q.len == 0 && last_is_rn(0));
    note(69, 100);
    CHECK(q.len == 1 && q.ev[0].n == 1 && q.ev[0].note[0] == 69);     /* G does not join */
    note(67, 0); note(69, 0);
    CHECK(count_type(EV_LOFF) == 4);                                   /* 60 64 67 69: every key still frees its voice */
    btn(A440, PRESS); btn(GROUP, PRESS); btn(GROUP, RELEASE); btn(A440, RELEASE);   /* under A440: the order combo */
    CHECK(q.order == SEQ_PEND && last_d3_is(CH_P, CH_N, CH_D) && q.len == 1 && u.rec);
    tap_a440();
    CHECK(!u.rec && q.len == 1);
}

static void test_a440_key_sets_the_transposition_with_seq_selected(void) {
    reset();
    btn(A440, PRESS);
    CHECK(key(62, 100) == 0 && count_type(EV_VON) == 1);               /* ArP selected: an ordinary key */
    key(62, 0);
    btn(A440, RELEASE);
    CHECK(a.enabled);                                                  /* a tap still: the key did not use the hold */
    record_cde();                                                      /* SEq selected, stopped, the arp off */
    btn(A440, PRESS);
    CHECK(key(62, 100) == 1 && q.transpose == 2 && last_int() == 2 && count_type(EV_VON) == 0);   /* the command: silent */
    CHECK(key(64, 100) == 0 && count_type(EV_VON) == 1 && q.transpose == 2);   /* a second key: live */
    key(64, 0);
    CHECK(btn(A440, RELEASE) == 1 && !q.playing);                      /* used the hold: no start */
    CHECK(key(62, 0) == 1 && count_type(EV_LOFF) == 1);                /* its release is consumed, A440 up or not */
    CHECK(key(62, 100) == 0 && count_type(EV_VON) == 2);               /* A440 up: an ordinary key again */
    key(62, 0);
    btn(A440, PRESS);
    CHECK(key(60, 100) == 1 && q.transpose == 0 && last_int() == 0);   /* middle C: zero */
    key(60, 0); btn(A440, RELEASE);
    btn(A440, PRESS);
    CHECK(key(48, 100) == 1 && q.transpose == -12 && last_int() == -12);
    key(48, 0); btn(A440, RELEASE);
    btn(A440, PRESS); btn(AFTERTOUCH, PRESS); btn(AFTERTOUCH, RELEASE);   /* after another combo: keys are live */
    CHECK(key(67, 100) == 0 && q.transpose == -12);
    key(67, 0); btn(A440, RELEASE);
    btn(A440, PRESS);
    arpui_note(&u, &a, &q, MIDI, 67, 100);                             /* MIDI-in: never a command, not a use of the hold */
    arpui_note(&u, &a, &q, MIDI, 67, 0);
    CHECK(q.transpose == -12);
    clear_log();
    btn(A440, RELEASE);                                                /* a tap: starts the sequence, transposed */
    CHECK(q.playing && von_note(0) == 48);
    btn(A440, PRESS);
    CHECK(key(62, 100) == 1 && q.transpose == 2 && q.playing);         /* while playing too; from the next event */
    key(62, 0); btn(A440, RELEASE);
    tap_a440();
    enter_rec();
    btn(A440, PRESS);
    CHECK(key(62, 100) == 0 && q.len == 1 && q.transpose == 0 && u.rec);   /* recording: a note, not a command; a new recording resets the offset */
    key(62, 0); btn(A440, RELEASE);
    CHECK(!u.rec && !q.playing);                                       /* the note did not use the hold: a tap, which leaves */
}

static void test_program_7_8_edit_the_selected_generators_note_value(void) {
    reset(); record_cde();                                             /* SEq selected */
    int n = stores();
    btn(A440, PRESS);
    btn(P7, PRESS); btn(P7, RELEASE);                                  /* longer: 8th D */
    CHECK(rate_index(&u.seq_rate) == RATE_DEFAULT_INDEX - 1 && q.beats_num == 3 && q.beats_den == 4 && !q.swing);
    CHECK(last_d3_is(BLANK, 8, CH_D) && rate_index(&u.rate) == RATE_DEFAULT_INDEX && a.beats_num == 1 && a.beats_den == 2);
    CHECK(stores() == n);                                              /* the Seq's value is not saved */
    btn(P8, PRESS); btn(P8, RELEASE); btn(P8, PRESS); btn(P8, RELEASE);   /* 8th, 8th S */
    CHECK(q.beats_num == 1 && q.beats_den == 1 && q.swing && last_d3_is(BLANK, 8, CH_S));
    btn(KEYB, PRESS); btn(KEYB, RELEASE);                              /* ArP */
    btn(P8, PRESS); btn(P8, RELEASE);                                  /* the Arp's: 8th S */
    CHECK(rate_index(&u.rate) == RATE_DEFAULT_INDEX + 1 && a.swing && stores() > n);
    CHECK(q.beats_num == 1 && q.beats_den == 1 && rate_index(&u.seq_rate) == RATE_DEFAULT_INDEX + 1);   /* the Seq's untouched */
    btn(A440, RELEASE);
    set_patch(pv(0, ARP_UP, 1, N_QTR, 9));                              /* a program load: the Arp's only */
    arpui_program_loaded(&u, &a, &q);
    CHECK(rate_index(&u.rate) == N_QTR && rate_index(&u.seq_rate) == RATE_DEFAULT_INDEX + 1 && q.swing);
}

static void test_bank_group_are_the_order_in_chords_style_and_the_direction_otherwise(void) {
    reset(); record_cde();
    int n = stores();
    btn(A440, PRESS);
    btn(BANK, PRESS); btn(BANK, RELEASE);
    CHECK(q.order == SEQ_BACK && last_d3_is(CH_B, CH_A, CH_C) && a.mode == ARP_UP);
    btn(BANK, PRESS); btn(BANK, RELEASE);
    CHECK(q.order == SEQ_PEND && last_d3_is(CH_P, CH_N, CH_D));
    btn(BANK, PRESS); btn(BANK, RELEASE);
    CHECK(q.order == SEQ_FOR && last_d3_is(CH_F, CH_LO, CH_R));
    btn(GROUP, PRESS); btn(GROUP, RELEASE);
    CHECK(q.order == SEQ_PEND && stores() == n);                       /* not saved */
    btn(UNISON, PRESS); btn(UNISON, RELEASE);                          /* Arpeggiated: the direction inside the chord */
    CHECK(q.style == SEQ_ARPEGGIATED);
    btn(BANK, PRESS); btn(BANK, RELEASE);
    CHECK(a.mode == ARP_DOWN && last_d3_is(CH_D, CH_N, BLANK) && q.order == SEQ_PEND && stores() > n);
    btn(KEYB, PRESS); btn(KEYB, RELEASE);                              /* ArP selected: the direction */
    btn(UNISON, PRESS); btn(UNISON, RELEASE);                          /* back to Chords: still the direction */
    btn(GROUP, PRESS); btn(GROUP, RELEASE);
    CHECK(a.mode == ARP_UP && q.order == SEQ_PEND);
    btn(A440, RELEASE);
    CHECK(!a.enabled && !q.playing);
}

static void test_unison_and_aftertouch_set_style_and_chord_length_without_saving(void) {
    static const int CYCLE[][3] = { { 8, 2, CH_B }, { 16, 4, CH_B }, { 1, BLANK, 4 }, { 2, BLANK, 2 }, { 4, BLANK, 1 } };
    reset();
    CHECK(q.style == SEQ_CHORDS && q.chord_beats == 4);                /* defaults: CHd, Whole */
    btn(A440, PRESS);
    CHECK(btn(UNISON, PRESS) == 1 && q.style == SEQ_ARPEGGIATED && last_d3_is(CH_A, CH_R, CH_P));   /* ArP */
    CHECK(btn(UNISON, RELEASE) == 1 && stores() == 0);
    btn(UNISON, PRESS); btn(UNISON, RELEASE);
    CHECK(q.style == SEQ_CHORDS && last_d3_is(CH_C, CH_H, CH_D));      /* CHd */
    for (int i = 0; i < 5; i++) {                                      /* from Whole: 2b 4b 4 2 1 */
        CHECK(btn(AFTERTOUCH, PRESS) == 1 && btn(AFTERTOUCH, RELEASE) == 1);
        CHECK(q.chord_beats == CYCLE[i][0] && last_d3_is(BLANK, CYCLE[i][1], CYCLE[i][2]));
    }
    CHECK(stores() == 0);                                              /* session settings: nothing written */
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled);                      /* combos: no toggle */
    CHECK(btn(UNISON, PRESS) == 0 && btn(AFTERTOUCH, PRESS) == 0);     /* without A440: stock */
}

static void test_program_load_keeps_the_sequence_playing_and_starts_the_arp_only_if_selected(void) {
    reset(); record_cde();
    btn(A440, PRESS); key(62, 100); key(62, 0); btn(A440, RELEASE);  /* transposed +2 */
    tap_a440(); ticks(1);
    CHECK(q.playing && q.transpose == 2 && last_led() == 1);
    set_patch(pv(1, ARP_UP, 1, N_8, 9));                                /* saved: arp on */
    arpui_program_loaded(&u, &a, &q);
    CHECK(q.playing && q.len == 3 && q.transpose == 2 && !a.enabled && u.gen == ARPUI_GEN_SEQ);   /* keeps playing; the arp stays off */
    ticks(1);
    CHECK(last_led() == 1);
    select_gen();                                                      /* ArP: the switch stops it, as any switch does */
    CHECK(!a.enabled);
    arpui_program_loaded(&u, &a, &q);
    CHECK(a.enabled && q.len == 3);                                    /* now the saved "on" acts */
    enter_rec();
    note(60, 100); note(60, 0);
    arpui_program_loaded(&u, &a, &q);
    CHECK(u.rec && q.len == 1 && !a.enabled);                          /* recording kept, the arp off while recording */
    note(62, 100); note(62, 0);
    CHECK(q.len == 2);
}

static void test_program6_outside_record_mode_clears_and_selects_arp(void) {
    reset(); record_cde();
    btn(A440, PRESS); key(62, 100); key(62, 0); btn(A440, RELEASE);
    tap_a440();
    CHECK(q.playing);
    clear_log();
    btn(A440, PRESS); btn(P6, PRESS); btn(P6, RELEASE);
    CHECK(q.len == 0 && q.total == 0 && q.transpose == 0 && !q.playing && u.gen == ARPUI_GEN_ARP && !a.enabled);
    CHECK(count_type(EV_VOFF) == 1 && last_d3_is(DASH, DASH, DASH));  /* the generated note released */
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled);                      /* a combo: no toggle */
    tap_a440();
    CHECK(a.enabled);                                                  /* ArP: the tap is the arp's */
    btn(A440, PRESS); btn(P6, PRESS); btn(P6, RELEASE); btn(A440, RELEASE);   /* ArP running: the arp is left alone */
    CHECK(a.enabled && u.gen == ARPUI_GEN_ARP);
}

static void test_all_notes_off_stops_the_sequencer_and_empties_the_pool(void) {
    reset(); record_cde();
    tap_a440();
    arpui_all_notes_off(&u, &a, &q);
    CHECK(!q.playing && !q.armed && q.len == 3);
    ticks(1000);
    CHECK(count_type(EV_VON) == 1);                                    /* nothing more */
    select_gen(); tap_a440();                                          /* the arp with a key */
    note(48, 100);
    arpui_all_notes_off(&u, &a, &q);
    CHECK(arp_pool_count(&a) == 0 && a.enabled);
    enter_rec(); note(60, 100);
    arpui_all_notes_off(&u, &a, &q);
    CHECK(u.rec && q.len == 1);                                        /* recording kept */
}

static void test_generator_switch_hands_the_latch_over_with_the_arp(void) {
    reset(); record_cde();
    select_gen();                                                      /* ArP */
    tap_a440();                                                        /* arp on */
    hold_press_to_stock();                                             /* the arp's latch on */
    CHECK(fake_latch && a.hold);
    clear_log();
    select_gen();                                                      /* SEq: the arp stops; stock's latch (off) is put back */
    CHECK(!a.enabled && count_type(EV_HOLD_STOCK) == 1 && !fake_latch);
    arpui_hold(&u, &a, &q, 0);
    hold_press_to_stock();                                             /* stock's latch on for the live notes */
    CHECK(fake_latch && !arpui_suspended(&u, &a, &q));
    select_gen();                                                      /* ArP, stopped: stock's hold stays in use */
    CHECK(count_type(EV_HOLD_STOCK) == 1 && fake_latch);
    tap_a440();                                                        /* arp on: its latch (on) wanted, stock's remembered */
    CHECK(a.enabled && count_type(EV_HOLD_STOCK) == 1 && fake_latch); /* both on: nothing to replay */
    arpui_hold(&u, &a, &q, 1);
    hold_press_to_stock();                                             /* the arp's latch off */
    CHECK(!fake_latch);
    tap_a440();                                                        /* arp off: stock's latch (on) back */
    CHECK(count_type(EV_HOLD_STOCK) == 2 && fake_latch);
}

/* ---- flash diagnostic (A440 + Sync; spec "Flash diagnostic") ---------------------------- */
enum { SYNC = 26, FLASH_RUN_TICKS = 1672 };   /* Sync = 26, read on the instrument 2026-10-09; the 8 MB fake: 32 + 956 + 684 pieces */

static void test_sync_combo_runs_the_flash_diagnostic_and_shows_the_results(void) {
    reset();
    btn(A440, PRESS);
    CHECK(btn(SYNC, PRESS) == 1 && fl.running);
    CHECK(last_int() == 0 && count_type(EV_D3) == 0);                  /* progress: reading at 0 */
    CHECK(btn(SYNC, RELEASE) == 1);
    btn(A440, RELEASE);
    CHECK(!a.enabled);                                                 /* used the hold: no toggle */
    ticks(1);
    CHECK(last_int() == 128);                                          /* the piece 8 MB higher */
    ticks(40);
    CHECK(last_int() == 81 && fl.running);                             /* area 1: 0x510000 / 64 KB */
    ticks(1559);
    CHECK(fl.running && last_int() > 81 && count_type(EV_RESTORE) == 0);   /* climbing, kept up past 1.5 s */
    ticks(FLASH_RUN_TICKS - 1600);
    CHECK(!fl.running && last_d3_is(CH_F, BLANK, 8));                 /* F 8 as the run completes */
    ticks(1499);
    CHECK(last_d3_is(CH_F, BLANK, 8));
    ticks(1);
    CHECK(last_d3_is(1, BLANK, CH_E));                                 /* 1 E */
    ticks(1500);
    CHECK(last_d3_is(2, BLANK, CH_E) && count_type(EV_RESTORE) == 0);  /* 2 E */
    ticks(1500);
    CHECK(count_type(EV_RESTORE) == 1);                                /* then the patch display */
    ticks(3000);
    CHECK(count_type(EV_RESTORE) == 1 && last_d3_is(2, BLANK, CH_E));  /* and nothing more */
    clear_log();
    btn(A440, PRESS); btn(SYNC, PRESS); btn(SYNC, RELEASE); btn(A440, RELEASE);   /* recall: no new scan */
    CHECK(!fl.running && last_d3_is(CH_F, BLANK, 8) && !a.enabled);
    btn(A440, PRESS); btn(SYNC, PRESS); btn(SYNC, RELEASE); btn(A440, RELEASE);
    CHECK(last_d3_is(1, BLANK, CH_E));
    btn(A440, PRESS); btn(SYNC, PRESS); btn(SYNC, RELEASE); btn(A440, RELEASE);
    CHECK(last_d3_is(2, BLANK, CH_E));
    btn(A440, PRESS); btn(SYNC, PRESS); btn(SYNC, RELEASE); btn(A440, RELEASE);
    CHECK(last_d3_is(CH_F, BLANK, 8) && !fl.running && count_type(EV_INT) == 0);   /* round again; no progress readout */
    ticks(1500);
    CHECK(count_type(EV_RESTORE) == 1);                                /* a recalled reading reverts like any message */
}

static void test_sync_during_a_run_is_ignored_and_sync_alone_is_stock(void) {
    reset();
    CHECK(btn(SYNC, PRESS) == 0 && !fl.running);                      /* no A440: stock Osc A sync */
    CHECK(btn(SYNC, RELEASE) == 0);
    btn(A440, PRESS); btn(SYNC, PRESS); btn(SYNC, RELEASE); btn(A440, RELEASE);
    ticks(100);
    btn(A440, PRESS);
    CHECK(btn(SYNC, PRESS) == 1 && btn(SYNC, RELEASE) == 1);           /* consumed, ignored */
    btn(A440, RELEASE);
    CHECK(!a.enabled && fl.running);
    ticks(FLASH_RUN_TICKS - 100);
    CHECK(!fl.running && last_d3_is(CH_F, BLANK, 8));                 /* on time: the run was not restarted */
}

static void test_another_message_cancels_the_remaining_results(void) {
    reset();
    btn(A440, PRESS); btn(SYNC, PRESS); btn(SYNC, RELEASE); btn(A440, RELEASE);
    ticks(FLASH_RUN_TICKS);
    CHECK(last_d3_is(CH_F, BLANK, 8));
    btn(A440, PRESS); btn(BANK, PRESS); btn(BANK, RELEASE); btn(A440, RELEASE);   /* dn */
    CHECK(last_d3_is(CH_D, CH_N, BLANK));
    ticks(1500);
    CHECK(count_type(EV_RESTORE) == 1 && last_d3_is(CH_D, CH_N, BLANK));   /* 1 E and 2 E dropped */
    ticks(3000);
    CHECK(count_type(EV_RESTORE) == 1);
}

static void test_kill_switch_disables_the_diagnostic(void) {
    reset();
    u.kill = 1;
    btn(A440, PRESS); btn(SYNC, PRESS); btn(SYNC, RELEASE); btn(A440, RELEASE);
    ticks(10);
    CHECK(!fl.running && count_type(EV_D3) == 0 && count_type(EV_INT) == 0);
}

/* ---- sequence memory (spec "Sequence memory") -------------------------------------------- */
static void fill_seq(int note, int events, int dur) {      /* events one-note chords of `note` */
    seq_init(&q);
    for (int i = 0; i < events; i++) { q.ev[i].n = 1; q.ev[i].note[0] = (uint8_t)note; q.ev[i].vel[0] = 100; q.ev[i].dur = (uint16_t)dur; }
    q.len = (uint16_t)events; q.total = (uint16_t)(events * dur);
}
static void stored(int factory, int bank, int group, int prog) { arpui_program_stored(&u, &a, &q, factory, bank, group, prog); }
static void loaded(int factory, int bank, int group, int prog) {
    fake_factory = factory; fake_bank = bank; fake_group = group; fake_prog = prog;
    arpui_program_loaded(&u, &a, &q);
}

static void test_record_writes_the_sequence_block_and_a_load_brings_it_back(void) {
    int idx;
    reset();
    fill_seq(67, 2, 1);
    seq_set_order(&q, SEQ_BACK); seq_set_chord_beats(&q, 8); seq_set_transpose(&q, 3);
    rate_step(&u.seq_rate, 1); idx = rate_index(&u.seq_rate);
    seq_set_gate(&q, 16);
    u.gen = ARPUI_GEN_SEQ;
    stored(0, 1, 2, 3);                                                /* U 2-3-4: slot 59 */
    CHECK(writes == 1 && bad_writes == 0 && write_off[0] == SEQMEM_BASE + 59 * SEQMEM_BLOCK && write_len[0] == SEQMEM_SECTOR);
    seq_init(&q); u.gen = ARPUI_GEN_ARP; rate_init(&u.seq_rate);
    loaded(0, 1, 2, 4);                                                /* another program, no block: untouched */
    CHECK(q.len == 0 && u.gen == ARPUI_GEN_ARP);
    loaded(0, 1, 2, 3);
    CHECK(q.len == 2 && q.ev[0].note[0] == 67 && q.order == SEQ_BACK && q.chord_beats == 8 && q.transpose == 3 && q.gate == 16);
    CHECK(u.gen == ARPUI_GEN_SEQ && rate_index(&u.seq_rate) == idx && !a.enabled && !q.playing);
    clear_log();
    tap_a440();
    CHECK(q.playing && last_of(EV_VON) && last_of(EV_VON)->b == 70);  /* the next Start plays it: 67 transposed +3 */
}

static void test_factory_programs_stale_blocks_and_no_sequence_leave_things_alone(void) {
    reset();
    fill_seq(60, 1, 1);
    stored(1, 0, 0, 0);
    CHECK(writes == 0);                                                /* factory: nothing of ours */
    stored(0, 0, 0, 1);
    CHECK(writes == 1);
    params[10] = 5;                                                    /* the program was overwritten */
    seq_init(&q);
    loaded(0, 0, 0, 1);
    CHECK(q.len == 0);                                                 /* stale: untouched */
    params[10] = 0;
    loaded(0, 0, 0, 1);
    CHECK(q.len == 1 && q.ev[0].note[0] == 60);
    seq_clear(&q, &a);
    stored(0, 0, 0, 1);                                                /* nothing recorded: "no sequence" */
    CHECK(writes == 2 && bad_writes == 0);
    fill_seq(72, 2, 1);
    loaded(0, 0, 0, 1);
    CHECK(q.len == 2 && q.ev[0].note[0] == 72);                       /* the live one stays */
}

static void test_a_load_while_playing_switches_at_the_next_step_boundary(void) {
    reset();
    fill_seq(67, 1, 1); u.gen = ARPUI_GEN_ARP;
    stored(0, 0, 0, 1);                                                /* G4, saved with ArP selected */
    fill_seq(60, 2, 1); u.gen = ARPUI_GEN_SEQ;
    tap_a440();
    CHECK(q.playing && last_of(EV_VON)->b == 60);
    ticks(100); clear_log();
    loaded(0, 0, 0, 1);
    CHECK(q.playing && q.len == 1 && q.ev[0].note[0] == 67 && count_type(EV_VON) == 0);   /* not yet */
    CHECK(u.gen == ARPUI_GEN_SEQ);                                     /* the saved selection waits: it plays */
    ticks(149);
    CHECK(count_type(EV_VON) == 0);
    ticks(1);                                                          /* the step boundary: 250 ticks at 120 BPM, 8th */
    CHECK(q.playing && last_of(EV_VON) && last_of(EV_VON)->b == 67);
}

static void test_a_load_applies_the_saved_generator_only_when_stopped_and_not_in_record_mode(void) {
    reset();
    fill_seq(67, 1, 1); u.gen = ARPUI_GEN_ARP;
    stored(0, 0, 0, 2);
    fill_seq(60, 1, 1); u.gen = ARPUI_GEN_SEQ;
    loaded(0, 0, 0, 2);
    CHECK(u.gen == ARPUI_GEN_ARP && q.ev[0].note[0] == 67);           /* stopped: the selection follows */
    enter_rec();
    note(62, 100); note(62, 0);
    loaded(0, 0, 0, 2);
    CHECK(u.rec && q.len == 1 && q.ev[0].note[0] == 62);              /* recording: the block is ignored */
}

static void test_a_block_load_waits_for_a_running_diagnostic(void) {
    reset();
    fill_seq(67, 1, 1);
    stored(0, 0, 0, 3);
    seq_init(&q);
    btn(A440, PRESS); btn(SYNC, PRESS); btn(SYNC, RELEASE); btn(A440, RELEASE);
    loaded(0, 0, 0, 3);
    CHECK(fl.running && q.len == 0);                                   /* deferred: the buffer is in use */
    ticks(FLASH_RUN_TICKS + 1);
    CHECK(!fl.running && q.len == 1 && q.ev[0].note[0] == 67);
}

/* ---- gate: A440 + Amp Decay (spec "Gate") --------------------------------------------- */
static void test_amp_decay_sets_the_gate_only_with_a440_held_and_jumps_to_the_knob(void) {
    reset();
    CHECK(ARPUI_POT_DECAY == 0x11 && u.gate == ARP_GATE_DEFAULT && a.gate == ARP_GATE_DEFAULT);
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_DECAY, 1000) == 0 && arpui_pot_change(&u, &a, &q, ARPUI_POT_DECAY) == 0);   /* A440 up: stock amp decay */
    CHECK(u.gate == 9 && stores() == 0);
    btn(A440, PRESS);
    clear_log();
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_DECAY, 900) == 1 && arpui_pot_change(&u, &a, &q, ARPUI_POT_DECAY) == 1);   /* far from 50 %: jumps */
    CHECK(raw_gate(900) == 17 && u.gate == 17 && last_int() == 90);
    decay(300);
    CHECK(raw_gate(300) == 6 && u.gate == 6 && last_int() == 35);
    decay(1023);
    CHECK(u.gate == 19 && last_int() == 100);
    decay(0);
    CHECK(u.gate == 0 && last_int() == 5);
    CHECK(btn(A440, RELEASE) == 1 && !a.enabled);                      /* used the hold: no toggle */
    btn(A440, PRESS);                                                  /* a new hold: the first movement sets it */
    decay(108);
    CHECK(u.gate == 2);                                                /* 15 % */
    btn(A440, RELEASE);
    arpui_pot_store(&u, &a, &q, ARPUI_POT_DECAY, 900);                 /* moved as amp decay in between */
    btn(A440, PRESS);
    decay(950);                                                        /* far from 15 %: still jumps */
    CHECK(u.gate == raw_gate(950) && u.gate == 18);
    decay(108);
    CHECK(u.gate == 2);
    btn(A440, RELEASE);
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_DECAY, 700) == 0 && u.gate == 2);   /* alone: stock again */
    arp_set_bpm(&a, 120);
    tap_a440();                                                        /* the arp plays at 15 %: 37.5 ms of 250 */
    note(48, 100); note(52, 100);
    clear_log();
    ticks(37);
    CHECK(count_type(EV_VOFF) == 0);
    ticks(1);
    CHECK(count_type(EV_VOFF) == 1);
    btn(A440, PRESS); decay(108); decay(1023); btn(A440, RELEASE);     /* while it runs: from the next step */
    CHECK(u.gate == 19 && a.gate == 19);
}

static void test_amp_decay_edits_the_selected_generators_gate(void) {
    reset(); record_cde();                                             /* SEq selected, stopped */
    int n = stores();
    btn(A440, PRESS);
    decay(500);                                                        /* 9 = the Seq's 50 %: caught */
    decay(100);
    CHECK(q.gate == raw_gate(100) && q.gate == 2 && u.gate == 9 && last_int() == 15);
    CHECK(stores() == n);                                              /* the Seq's gate is not in the program record */
    btn(KEYB, PRESS); btn(KEYB, RELEASE);                              /* ArP: the knob now sets the Arp's gate */
    CHECK(u.gen == ARPUI_GEN_ARP);
    decay(120);
    CHECK(u.gate == raw_gate(120) && u.gate == 2 && last_int() == 15 && q.gate == 2);
    decay(600);
    CHECK(u.gate == 11 && q.gate == 2);
    btn(A440, RELEASE);
}

static void test_the_seq_lends_its_gate_to_the_arp_and_the_arp_gets_its_own_back(void) {
    reset(); record_cde();
    btn(A440, PRESS); btn(UNISON, PRESS); btn(UNISON, RELEASE);        /* Arpeggiated */
    decay(500); decay(100);                                            /* the Seq's gate: 15 % */
    btn(A440, RELEASE);
    CHECK(q.style == SEQ_ARPEGGIATED && q.gate == 2);
    tap_a440();                                                        /* plays: each arp step at 15 % */
    clear_log();
    ticks(37);
    CHECK(count_type(EV_VOFF) == 0);
    ticks(1);
    CHECK(count_type(EV_VOFF) == 1);
    tap_a440();                                                        /* stop */
    select_gen();                                                      /* ArP, with its own 50 % */
    CHECK(u.gen == ARPUI_GEN_ARP && u.gate == 9);
    tap_a440();
    note(48, 100); note(52, 100);
    clear_log();
    ticks(124);
    CHECK(count_type(EV_VOFF) == 0);
    ticks(1);
    CHECK(count_type(EV_VOFF) == 1);
}

static void test_a_seq_gate_change_reaches_the_arpeggiated_style_at_the_next_step(void) {
    reset(); record_cde();
    btn(A440, PRESS); btn(UNISON, PRESS); btn(UNISON, RELEASE); btn(A440, RELEASE);   /* Arpeggiated, the Seq's 50 % */
    tap_a440();                                                        /* plays: a Whole chord of repeated steps */
    ticks(10);
    btn(A440, PRESS); decay(500); decay(100); btn(A440, RELEASE);      /* 15 %, during the first step */
    CHECK(q.gate == 2 && q.playing);
    clear_log();
    ticks(115);                                                        /* the first step keeps its 50 %: 125 ms */
    CHECK(count_type(EV_VOFF) == 1);
    ticks(125);                                                        /* 250: the next step, inside the same chord ... */
    clear_log();
    ticks(37);
    CHECK(count_type(EV_VOFF) == 0);
    ticks(1);                                                          /* ... at 15 % */
    CHECK(count_type(EV_VOFF) == 1);
}

static void test_a_program_load_mid_chord_leaves_the_seqs_arpeggio_alone(void) {
    reset(); record_cde();
    btn(A440, PRESS); btn(UNISON, PRESS); btn(UNISON, RELEASE); btn(AFTERTOUCH, PRESS); btn(AFTERTOUCH, RELEASE); btn(A440, RELEASE);
    CHECK(q.style == SEQ_ARPEGGIATED && q.chord_beats == 8);           /* 2-bar chords, 8ths at 50 % */
    tap_a440();
    ticks(10);
    set_patch(pv(1, ARP_UP, 1, N_QTR, 19));                            /* the program: Qtr, 100 % */
    arpui_program_loaded(&u, &a, &q);
    CHECK(q.playing && !a.enabled && rate_index(&u.rate) == N_QTR && u.gate == 19);   /* remembered ... */
    CHECK(a.beats_num == 1 && a.beats_den == 2 && a.gate == 9);        /* ... the chord keeps the Seq's */
    clear_log();
    ticks(240);                                                        /* 250: the next 8th, still the Seq's */
    CHECK(count_type(EV_VOFF) == 1 && count_type(EV_VON) == 1);
    tap_a440();                                                        /* stop; the Arp as the Arp gets its own */
    select_gen();
    tap_a440();
    CHECK(a.enabled && a.beats_num == 1 && a.beats_den == 1 && a.gate == 19);
}

static void test_amp_decay_is_stock_under_the_kill_switch(void) {
    arpui_init(&u); arp_init(&a); seq_init(&q); clear_log();
    fake_a440_down = 1;
    ticks(ARPUI_BOOT_TICKS);
    CHECK(u.kill);
    CHECK(arpui_pot_store(&u, &a, &q, ARPUI_POT_DECAY, 300) == 0 && arpui_pot_change(&u, &a, &q, ARPUI_POT_DECAY) == 0);
    fake_a440_down = 0;
}

int main(void) {
    test_tap_toggles_arp_with_status_and_led();
    test_repeats_are_ignored();
    test_bank_group_cycle_modes_with_names();
    test_program_buttons_set_octaves_clock_and_note_value();
    test_program_7_8_step_through_every_value();
    test_a440_tune_enters_record_mode_and_a_tap_leaves_it_stopped();
    test_record_mode_without_entries_keeps_the_sequence();
    test_tune_without_a440_is_stock();
    test_hold_button_is_rest_or_tie_in_record_mode();
    test_a440_hold_toggles_the_stock_tone_with_the_arp_off();
    test_a440_hold_with_the_arp_on_is_consumed_and_inert();
    test_tap_while_the_tone_sounds_only_stops_it();
    test_program_load_with_the_tone_on_silences_it_and_reasserts_the_led();
    test_record_readout_counts_timing_steps();
    test_pedal_on_transition_is_rest_or_tie_in_record_mode();
    test_record_readout_persists_and_patch_display_returns_on_leaving();
    test_a440_led_blinks_in_record_mode();
    test_program6_in_record_mode_clears_and_stays();
    test_globals_leaves_record_mode();
    test_notes_while_a440_is_held_are_ordinary_with_arp_selected();
    test_suspended_is_arp_on_or_record_mode();
    test_orphan_release_is_consumed_and_other_buttons_pass();
    test_readout_of_unassigned_buttons();
    test_readout_of_knobs();
    test_globals_button_abandons_the_hold();
    test_tap_tempo_steady_series();
    test_tap_tempo_averages_the_last_four_intervals();
    test_tap_tempo_clamps_and_the_two_second_limit();
    test_tap_tempo_consumption_and_scope();
    test_globals_menu_open_passes_everything();
    test_glide_is_tempo_only_with_a440_held();
    test_glide_picks_the_tempo_up_instead_of_jumping();
    test_messages_revert_to_patch_display_after_1500_ticks();
    test_led_follows_enabled_only_when_it_changes();
    test_a440_held_at_power_on_disables_everything();
    test_a440_pressed_after_power_on_is_just_a_press();
    test_settings_are_written_to_the_patch_slots();
    test_program_load_applies_saved_state();
    test_patch_round_trip_for_every_setting();
    test_long_note_values_load_and_bad_values_are_no_arp_data();
    test_arp_latch_and_stock_latch_are_remembered_separately();
    test_pedal_is_momentary_and_program_load_clears_both_latches();
    test_program_without_arp_data_switches_off_and_leaves_settings();
    test_keyboard_selects_the_generator();
    test_tap_starts_and_stops_the_sequencer();
    test_switching_generators_stops_the_running_one();
    test_tone_combo_follows_the_selected_generators_state();
    test_live_notes_sustain_under_hold_while_the_sequence_runs_generated_ones_do_not();
    test_group_alone_is_back_in_record_mode();
    test_a440_key_sets_the_transposition_with_seq_selected();
    test_program_7_8_edit_the_selected_generators_note_value();
    test_bank_group_are_the_order_in_chords_style_and_the_direction_otherwise();
    test_unison_and_aftertouch_set_style_and_chord_length_without_saving();
    test_program_load_keeps_the_sequence_playing_and_starts_the_arp_only_if_selected();
    test_program6_outside_record_mode_clears_and_selects_arp();
    test_all_notes_off_stops_the_sequencer_and_empties_the_pool();
    test_generator_switch_hands_the_latch_over_with_the_arp();
    test_sync_combo_runs_the_flash_diagnostic_and_shows_the_results();
    test_sync_during_a_run_is_ignored_and_sync_alone_is_stock();
    test_another_message_cancels_the_remaining_results();
    test_kill_switch_disables_the_diagnostic();
    test_record_writes_the_sequence_block_and_a_load_brings_it_back();
    test_factory_programs_stale_blocks_and_no_sequence_leave_things_alone();
    test_a_load_while_playing_switches_at_the_next_step_boundary();
    test_a_load_applies_the_saved_generator_only_when_stopped_and_not_in_record_mode();
    test_a_block_load_waits_for_a_running_diagnostic();
    test_amp_decay_sets_the_gate_only_with_a440_held_and_jumps_to_the_knob();
    test_amp_decay_edits_the_selected_generators_gate();
    test_the_seq_lends_its_gate_to_the_arp_and_the_arp_gets_its_own_back();
    test_a_seq_gate_change_reaches_the_arpeggiated_style_at_the_next_step();
    test_a_program_load_mid_chord_leaves_the_seqs_arpeggio_alone();
    test_amp_decay_is_stock_under_the_kill_switch();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
