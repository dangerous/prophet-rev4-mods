/* Host harness for the arp engine core (docs/SPEC.md: "Arp engine" — Note pool, Pattern,
 * Clock, Output). A fake voice allocator records every note-on /
 * note-off with the tick it happened at, so tests assert exact sequences and timing.
 * `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "arp.h"
#include "platform.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake voices --------------------------------------------------------------------- */
typedef struct { int on, src, note, vel, t; } ev_t;
static ev_t log_[8192];
static int nlog, now;
static int sounding[128];

static void push(int on, int src, int note, int vel) {
    if (nlog < (int)(sizeof log_ / sizeof *log_)) log_[nlog++] = (ev_t){on, src, note, vel, now};
    if (note >= 0 && note < 128) sounding[note] = on;
}
void plat_voice_on(int src, int note, int vel) { push(1, src, note, vel); }
void plat_voice_off(int src, int note) { push(0, src, note, 0); }
void plat_live_off(int src, int note) { push(0, src, note, 0); }   /* a direct (live) note's release: the same here */

static int n_on(void) { int n = 0; for (int i = 0; i < nlog; i++) n += log_[i].on; return n; }
static int n_off(void) { return nlog - n_on(); }
static ev_t *on_at(int k) { for (int i = 0; i < nlog; i++) if (log_[i].on && k-- == 0) return &log_[i]; return 0; }
static int on_note(int k) { ev_t *e = on_at(k); return e ? e->note : -1; }
static int on_tick(int k) { ev_t *e = on_at(k); return e ? e->t : -1; }
static int n_sounding(void) { int n = 0; for (int i = 0; i < 128; i++) n += sounding[i] != 0; return n; }
static int has_off(int note) { for (int i = 0; i < nlog; i++) if (!log_[i].on && log_[i].note == note) return 1; return 0; }
static void clear_log(void) { nlog = 0; }

/* ---- helpers ------------------------------------------------------------------------- */
enum { LOCAL = ARP_SRC_LOCAL, MIDI = ARP_SRC_MIDI, C3 = 48, D3 = 50, E3 = 52, G3 = 55, C4 = 60, D4 = 62, E4 = 64, F4 = 65, A4 = 69 };
static arp_t a;

static void reset(void) {
    arp_init(&a);
    clear_log();
    memset(sounding, 0, sizeof sounding);
    now = 0;
}
static void ticks(int n) { while (n-- > 0) { now++; arp_tick(&a); } }   /* tick k is logged as t = k */
static void on(int note) { arp_note(&a, LOCAL, note, 100); }
static void off(int note) { arp_note(&a, LOCAL, note, 0); }
static void chord_ceg(void) { on(C3); on(E3); on(G3); }
static void enabled_with_ceg(void) { reset(); arp_enable(&a, 1); chord_ceg(); clear_log(); }
/* the sequence of note-ons as a string of pitches, e.g. "48 52 55 " */
static const char *ons(void) {
    static char buf[2048]; int p = 0; buf[0] = 0;
    for (int i = 0; i < nlog && p < 2000; i++) if (log_[i].on) p += sprintf(buf + p, "%d ", log_[i].note);
    return buf;
}

/* ---- defaults and pass-through ------------------------------------------------------- */
static void test_defaults(void) {
    reset();
    CHECK(!a.enabled && a.mode == ARP_UP && a.octaves == 1 && a.bpm == 120 && !a.ext && !a.hold);
    CHECK(a.beats_num == 1 && a.beats_den == 2);                      /* 1/8 */
    CHECK(a.sounding == ARP_NONE);
    CHECK(sizeof(arp_t) <= 0x400);                                    /* the sequence storage lives in seq.c now */
    ticks(5000);
    CHECK(nlog == 0);
}

static void test_disabled_passes_notes_straight_through(void) {
    reset();
    on(C3);
    arp_note(&a, MIDI, E3, 90);
    CHECK(nlog == 2 && log_[0].on && log_[0].src == LOCAL && log_[0].note == C3 && log_[0].vel == 100);
    CHECK(log_[1].on && log_[1].src == MIDI && log_[1].note == E3 && log_[1].vel == 90);
    ticks(1000);
    CHECK(nlog == 2);
    off(C3);
    arp_note(&a, MIDI, E3, 0);
    CHECK(nlog == 4 && !log_[2].on && log_[2].note == C3 && !log_[3].on && log_[3].src == MIDI && log_[3].note == E3);
    arp_realtime(&a, 0xFA, 0); arp_realtime(&a, 0xF8, 0);
    CHECK(nlog == 4);
}

/* ---- enable / disable ---------------------------------------------------------------- */
static void test_enable_releases_direct_notes_and_starts_at_once(void) {
    reset();
    chord_ceg();
    clear_log();
    arp_enable(&a, 1);
    CHECK(has_off(C3) && has_off(E3) && has_off(G3));                /* directly sounding notes released */
    CHECK(n_on() == 1 && on_note(0) == C3 && on_tick(0) == 0);        /* and the pattern starts now (Up) */
    CHECK(n_sounding() == 1);
}

static void test_disable_releases_step_and_resounds_held_keys_only(void) {
    enabled_with_ceg();
    arp_hold(&a, 1);
    off(G3);                                                           /* G latched, C E held */
    ticks(10);
    clear_log();
    arp_enable(&a, 0);
    CHECK(n_sounding() == 2 && sounding[C3] && sounding[E3] && !sounding[G3]);
    ticks(2000);
    CHECK(n_on() == 2);                                                /* nothing else happens */
    off(C3);
    CHECK(!sounding[C3]);                                              /* and releases pass through */
}

/* ---- order and timing ---------------------------------------------------------------- */
static void test_up_at_120_bpm_eighths_with_half_gate(void) {
    reset();
    arp_enable(&a, 1);
    chord_ceg();                                                       /* C steps immediately */
    ticks(1000);
    CHECK(strcmp(ons(), "48 52 55 48 52 ") == 0);
    CHECK(on_tick(0) == 0 && on_tick(1) == 250 && on_tick(2) == 500 && on_tick(3) == 750);
    /* gate: C released at 125, before E */
    CHECK(!log_[1].on && log_[1].note == C3 && log_[1].t == 125);
    CHECK(log_[2].on && log_[2].note == E3 && log_[2].t == 250);
    CHECK(log_[0].vel == 100);
}

static void test_velocity_is_the_keys_own(void) {
    reset();
    arp_enable(&a, 1);
    arp_note(&a, LOCAL, C3, 30); arp_note(&a, LOCAL, E3, 60); arp_note(&a, MIDI, G3, 90);
    ticks(600);
    CHECK(on_at(0)->vel == 30 && on_at(1)->vel == 60 && on_at(2)->vel == 90);
    CHECK(on_at(0)->src == LOCAL && on_at(2)->src == LOCAL);           /* steps are played as local keys */
}

static void test_down_updown_and_single_note(void) {
    enabled_with_ceg();
    arp_set_mode(&a, ARP_DOWN);
    ticks(1500);
    CHECK(strcmp(ons(), "55 52 48 55 52 48 ") == 0);                   /* restarts at the next step, from the top */
    clear_log();
    arp_set_mode(&a, ARP_UPDOWN);
    ticks(2000);
    CHECK(strcmp(ons(), "48 52 55 52 48 52 55 52 ") == 0);              /* ends not repeated */
    reset(); arp_enable(&a, 1); on(C3);
    ticks(600);
    CHECK(strcmp(ons(), "48 48 48 ") == 0);                            /* single note repeats */
}

static void test_random_is_uniform_over_the_pool_and_may_repeat(void) {
    enabled_with_ceg();
    arp_set_mode(&a, ARP_RANDOM);
    ticks(250 * 300);
    int seen[128] = {0}, repeats = 0, ok = 1;
    for (int k = 0; on_at(k); k++) {
        int n = on_note(k);
        ok &= (n == C3 || n == E3 || n == G3);
        seen[n]++;
        if (k && n == on_note(k - 1)) repeats++;
    }
    CHECK(ok && seen[C3] > 50 && seen[E3] > 50 && seen[G3] > 50);
    CHECK(repeats > 10);                                               /* no repeat avoidance (the Arp Mod behaviour) */
}

/* ---- octaves per pass ---------------------------------------------------------------- */
static void test_octaves_are_per_pass(void) {
    reset(); arp_enable(&a, 1); arp_set_octaves(&a, 2);
    on(C3); on(D4);
    ticks(1000);
    CHECK(strcmp(ons(), "48 62 60 74 48 ") == 0);                      /* C3 D4 | C4 D5 */
    clear_log();
    arp_set_mode(&a, ARP_DOWN);
    ticks(1000);
    CHECK(strcmp(ons(), "74 60 62 48 ") == 0);                         /* highest pass first */
    clear_log();
    arp_set_mode(&a, ARP_UPDOWN);
    ticks(1750);
    CHECK(strcmp(ons(), "48 62 60 74 60 62 48 ") == 0);                /* bounce over all passes */
    reset(); arp_enable(&a, 1); arp_set_octaves(&a, 4);
    on(120);
    ticks(1000);
    CHECK(strcmp(ons(), "120 120 120 120 120 ") == 0);                 /* 132, 144, 156 skipped */
}

/* ---- start rule ---------------------------------------------------------------------- */
static void test_first_key_into_empty_pool_starts_now_and_resets_phase(void) {
    reset(); arp_enable(&a, 1);
    ticks(100);
    on(C3);
    CHECK(n_on() == 1 && on_tick(0) == 100);
    ticks(300);
    CHECK(n_on() == 2 && on_tick(1) == 350);                           /* period restarts from the key */
}

static void test_key_added_to_running_pattern_waits_for_the_step(void) {
    enabled_with_ceg();                                                /* C at 0; next steps 250, 500 */
    ticks(100);
    on(D3);
    CHECK(n_on() == 0);                                                /* nothing immediate */
    ticks(400);                                                        /* 250: D, 500: E */
    CHECK(strcmp(ons(), "50 52 ") == 0 && on_tick(0) == 250);
}

static void test_relatch_replaces_pool_without_touching_the_phase(void) {
    enabled_with_ceg();
    arp_hold(&a, 1);
    off(C3); off(E3); off(G3);
    ticks(260);                                                        /* latched: E at 250 */
    CHECK(strcmp(ons(), "52 ") == 0);
    clear_log();
    on(D3);                                                            /* all keys were up: replaces the pool */
    CHECK(n_on() == 0);                                                /* no immediate step under HOLD */
    ticks(500);                                                        /* 500: D, 750: D */
    CHECK(strcmp(ons(), "50 50 ") == 0 && on_tick(0) == 500 - 260 + 260 - 0 - 0 && on_tick(0) == 500);
    on(F4); on(A4);                                                    /* D still down: adds */
    clear_log();
    ticks(750);
    CHECK(strcmp(ons(), "65 69 50 ") == 0);
}

/* ---- releases and HOLD --------------------------------------------------------------- */
static void test_release_of_sounding_note_is_immediate_and_last_note_silences(void) {
    enabled_with_ceg();                                                /* C sounding since 0 */
    ticks(50);
    off(C3);
    CHECK(n_off() == 1 && !log_[0].on && log_[0].note == C3 && log_[0].t == 50);
    ticks(300);                                                        /* 250: E (position clamped, continues) */
    CHECK(strcmp(ons(), "52 ") == 0);
    off(E3); off(G3);
    CHECK(n_sounding() == 0);
    ticks(1000);
    CHECK(n_on() == 1);                                                /* silence with an empty pool */
}

static void test_hold_latches_and_hold_off_drops_latched_notes(void) {
    enabled_with_ceg();
    arp_hold(&a, 1);
    off(C3); off(E3); off(G3);
    ticks(1000);
    CHECK(n_on() == 4);                                                /* keeps playing */
    clear_log();
    arp_hold(&a, 0);
    CHECK(n_sounding() == 0);                                          /* sounding latched note released */
    ticks(1000);
    CHECK(n_on() == 0);
    on(E3); ticks(1); off(E3);
    CHECK(n_sounding() == 0);                                          /* no longer latching */
}

static void test_hold_off_keeps_keys_still_down(void) {
    enabled_with_ceg();
    arp_hold(&a, 1);
    off(C3);
    arp_hold(&a, 0);
    clear_log();
    ticks(1000);
    CHECK(strstr(ons(), "48") == 0 && n_on() == 4);                    /* E G E G */
}

static void test_all_notes_off_clears_pool_but_keeps_hold(void) {
    enabled_with_ceg();
    arp_hold(&a, 1);
    off(C3);
    arp_all_notes_off(&a);
    CHECK(n_sounding() == 0);
    ticks(1000);
    CHECK(n_on() == 0);
    on(D3); off(D3);
    ticks(300);
    CHECK(n_on() >= 1 && a.hold);                                      /* D latched: hold still active */
}

/* ---- settings while running ---------------------------------------------------------- */
static void test_bpm_and_beats_changes_keep_the_phase(void) {
    enabled_with_ceg();                                                /* steps at 0, 250, 500 ... */
    ticks(100);
    arp_set_bpm(&a, 240);                                              /* period 125 from here on */
    ticks(400);
    CHECK(on_tick(0) > 100 && on_tick(0) <= 250);                      /* no reset, next step comes sooner */
    int t0 = on_tick(0), t1 = on_tick(1);
    CHECK(t1 - t0 == 125);
    clear_log();
    arp_set_beats(&a, 1, 4);                                           /* 1/16 at 240: 62/63 ticks */
    ticks(1000);
    CHECK(n_on() >= 15 && n_on() <= 17);
}

static void test_mode_and_octave_change_restart_at_next_step_keeping_phase(void) {
    enabled_with_ceg();                                                /* C at 0 */
    ticks(100);
    arp_set_octaves(&a, 2);
    CHECK(n_on() == 0);                                                /* no immediate step */
    ticks(400);                                                        /* 250: C (restart), 500: E */
    CHECK(strcmp(ons(), "48 52 ") == 0 && on_tick(0) == 250);
}

/* ---- MIDI clock ---------------------------------------------------------------------- */
static void clocks(int n, int port) { while (n-- > 0) arp_realtime(&a, 0xF8, port); }

static void test_ext_silent_without_clocks_and_steps_every_12_clocks(void) {
    enabled_with_ceg();
    arp_set_ext(&a, 1);
    CHECK(n_sounding() == 0);
    on(D3);
    ticks(3000);
    CHECK(n_on() == 0);                                                /* keys pool, nothing sounds */
    arp_realtime(&a, 0xFA, 0);
    clocks(1, 0);                                                      /* first clock after Start steps */
    CHECK(n_on() == 1 && on_note(0) == C3);
    clocks(5, 0);
    CHECK(n_sounding() == 1);
    clocks(1, 0);                                                      /* clock 6: gate off */
    CHECK(n_sounding() == 0);
    clocks(6, 0);                                                      /* clock 12: next step */
    CHECK(n_on() == 2 && on_note(1) == D3);
    clocks(24, 0);
    CHECK(strcmp(ons(), "48 50 52 55 ") == 0);
}

static void test_stop_continue_start(void) {
    enabled_with_ceg(); arp_set_ext(&a, 1);
    arp_realtime(&a, 0xFA, 0);
    clocks(13, 0);                                                     /* C at 0, E at 12 */
    arp_realtime(&a, 0xFC, 0);
    CHECK(n_sounding() == 0);
    clocks(50, 0);
    CHECK(n_on() == 2);                                                /* stopped: no steps */
    arp_realtime(&a, 0xFB, 0);
    clocks(12, 0);                                                     /* continue: counting resumes at 13 -> step at 24 */
    CHECK(n_on() == 3 && on_note(2) == G3);
    arp_realtime(&a, 0xFA, 0);
    clocks(1, 0);
    CHECK(n_on() == 4 && on_note(3) == C3);                            /* start: pattern and count restart */
}

static void test_notes_never_reset_the_grid_under_ext(void) {
    reset(); arp_enable(&a, 1); arp_set_ext(&a, 1);
    arp_realtime(&a, 0xFA, 0);
    clocks(7, 0);
    on(C3);                                                            /* HOLD off, pool empty: still waits */
    CHECK(n_on() == 0);
    clocks(4, 0);                                                      /* clocks 7..10 */
    CHECK(n_on() == 0);
    clocks(2, 0);                                                      /* 11, then 12: on the grid */
    CHECK(n_on() == 1);
}

static void test_clock_loss_and_port_lock(void) {
    enabled_with_ceg(); arp_set_ext(&a, 1);
    arp_realtime(&a, 0xFA, 0);
    clocks(1, 0);
    clocks(5, 1);                                                      /* other port: ignored */
    CHECK(n_sounding() == 1);
    ticks(999);
    CHECK(n_sounding() == 1);
    ticks(1);                                                          /* 1 s without a clock */
    CHECK(n_sounding() == 0);
    clocks(11, 1);                                                     /* after loss the other port is accepted; the first clock steps */
    CHECK(n_on() == 2);
    arp_set_ext(&a, 0);                                                /* back to internal: runs again */
    ticks(300);
    CHECK(n_on() >= 3);
}

static void test_beats_change_under_ext_realigns_from_start(void) {
    enabled_with_ceg(); arp_set_ext(&a, 1);
    arp_realtime(&a, 0xFA, 0);
    clocks(7, 0);                                                      /* step at clock 0 */
    arp_set_beats(&a, 1, 4);                                           /* 1/16 = 6 clocks: next multiple is 12 */
    clocks(4, 0);                                                      /* clocks 7..10 */
    CHECK(n_on() == 1);
    clocks(2, 0);                                                      /* 11, 12 */
    CHECK(n_on() == 2);
    clocks(6, 0);                                                      /* 18 */
    CHECK(n_on() == 3);
    arp_set_beats(&a, 16, 1);                                          /* 4 bars = 384 clocks */
    clocks(100, 0);
    CHECK(n_on() == 3);
}

/* ---- swing ("Note value": 16S / 8S) ------------------------------------------------- */
static int off_tick(int k) { for (int i = 0; i < nlog; i++) if (!log_[i].on && k-- == 0) return log_[i].t; return -1; }

static void test_swing_8s_internal_at_120_bpm(void) {
    reset();
    arp_set_beats(&a, 1, 1); arp_set_swing(&a, 1);                     /* 8S: pair = 1 beat = 500 ms */
    CHECK(a.swing == 1);
    arp_enable(&a, 1);
    chord_ceg();                                                       /* C steps now: a pair begins */
    ticks(2000);
    CHECK(strncmp(ons(), "48 52 55 48 52 55 48 52 55 ", 27) == 0);       /* order unaffected */
    CHECK(on_tick(0) == 0 && on_tick(1) == 334 && on_tick(2) == 500 && on_tick(3) == 834 && on_tick(4) == 1000);
    CHECK(on_tick(5) == 1334 && on_tick(6) == 1500 && on_tick(7) == 1834 && on_tick(8) == 2000);
    /* gate at half of each step: long 0..334 -> 167, short 334..500 -> 417 */
    CHECK(off_tick(0) == 167 && off_tick(1) == 417 && off_tick(2) == 667 && off_tick(3) == 917);
}

static void test_swing_16s_internal_at_120_bpm(void) {
    reset();
    arp_set_beats(&a, 1, 2); arp_set_swing(&a, 1);                     /* 16S: pair = 1/2 beat = 250 ms */
    arp_enable(&a, 1);
    chord_ceg();
    ticks(1000);
    CHECK(n_on() == 9);
    CHECK(on_tick(0) == 0 && on_tick(1) == 167 && on_tick(2) == 250 && on_tick(3) == 417 && on_tick(4) == 500);
    CHECK(on_tick(8) == 1000);
    CHECK(off_tick(0) == 84 && off_tick(1) == 209);
}

static void test_swing_start_rule_begins_a_pair_and_settings_keep_the_half(void) {
    reset();
    arp_set_beats(&a, 1, 1); arp_set_swing(&a, 1);
    arp_enable(&a, 1);
    on(C3);                                                            /* 0 long, 334 short */
    ticks(400);
    off(C3);                                                           /* emptied during the short step */
    ticks(50);
    clear_log();
    on(C3);                                                            /* start rule at 450: long first */
    ticks(600);
    CHECK(on_tick(0) == 450 && on_tick(1) == 784 && on_tick(2) == 950);
    reset();
    arp_set_beats(&a, 1, 1); arp_set_swing(&a, 1);
    enabled_with_ceg();                                                /* resets: plain 1/8 again */
    CHECK(a.swing == 0);
    arp_set_beats(&a, 1, 1); arp_set_swing(&a, 1);
    arp_enable(&a, 0); arp_enable(&a, 1);                              /* switching on begins a pair */
    clear_log();
    ticks(100);
    arp_set_mode(&a, ARP_DOWN);                                        /* in the long step: keeps it */
    ticks(300);
    arp_set_octaves(&a, 2);                                            /* in the short step: keeps it */
    ticks(500);
    CHECK(on_tick(0) == 334 && on_tick(1) == 500 && on_tick(2) == 834);
    arp_set_swing(&a, 0);                                              /* plain again: 1 beat steps */
    clear_log();
    ticks(1100);
    CHECK(n_on() == 2 && on_tick(1) - on_tick(0) == 500);
}

/* clocks numbered from Start: each clock is logged at t = its index */
static void numbered_clocks(int from, int n) { for (int i = 0; i < n; i++) { now = from + i; arp_realtime(&a, 0xF8, 0); } }

static void test_swing_under_midi_clock(void) {
    enabled_with_ceg(); arp_set_ext(&a, 1);
    arp_set_beats(&a, 1, 1); arp_set_swing(&a, 1);                     /* 8S: 24-clock pairs, 16 + 8 */
    arp_realtime(&a, 0xFA, 0);
    clear_log();
    numbered_clocks(0, 49);
    CHECK(on_tick(0) == 0 && on_tick(1) == 16 && on_tick(2) == 24 && on_tick(3) == 40 && on_tick(4) == 48);
    CHECK(n_on() == 5);
    CHECK(off_tick(0) == 8 && off_tick(1) == 20 && off_tick(2) == 32 && off_tick(3) == 44);   /* half of 16, of 8 */
    CHECK(strcmp(ons(), "48 52 55 48 52 ") == 0);
    enabled_with_ceg(); arp_set_ext(&a, 1);
    arp_set_beats(&a, 1, 2); arp_set_swing(&a, 1);                     /* 16S: 12-clock pairs, 8 + 4 */
    arp_realtime(&a, 0xFA, 0);
    clear_log();
    numbered_clocks(0, 25);
    CHECK(on_tick(0) == 0 && on_tick(1) == 8 && on_tick(2) == 12 && on_tick(3) == 20 && on_tick(4) == 24);
    CHECK(n_on() == 5);
    CHECK(off_tick(0) == 4 && off_tick(1) == 10 && off_tick(2) == 16 && off_tick(3) == 22);   /* half of 8, of 4 */
    /* a mid-run change lands on the pair grid from Start */
    enabled_with_ceg(); arp_set_ext(&a, 1);
    arp_realtime(&a, 0xFA, 0);
    clear_log();
    numbered_clocks(0, 13);                                            /* 1/8: steps at 0 and 12 */
    arp_set_beats(&a, 1, 1); arp_set_swing(&a, 1);                     /* 8S from clock 13 */
    numbered_clocks(13, 28);                                           /* 13..40 */
    CHECK(n_on() == 5 && on_tick(2) == 16 && on_tick(3) == 24 && on_tick(4) == 40);
}

static void test_four_bar_steps_internal_and_midi_clock(void) {
    reset(); arp_enable(&a, 1);
    arp_set_beats(&a, 16, 1);                                          /* 4 bars at 120 BPM = 8 s per step */
    on(C3); on(E3);
    ticks(3999);
    CHECK(n_on() == 1 && n_sounding() == 1);
    ticks(1);
    CHECK(n_sounding() == 0);                                          /* gate at 4 s */
    ticks(3999);
    CHECK(n_on() == 1);
    ticks(1);
    CHECK(n_on() == 2 && on_note(1) == E3 && on_tick(1) == 8000);
    reset(); arp_enable(&a, 1); arp_set_beats(&a, 16, 1); arp_set_ext(&a, 1);
    on(C3); on(E3);
    arp_realtime(&a, 0xFA, 0);
    clocks(1, 0);
    CHECK(n_on() == 1);
    clocks(191, 0);
    CHECK(n_sounding() == 1);
    clocks(1, 0);                                                      /* clock 192: gate */
    CHECK(n_sounding() == 0);
    clocks(191, 0);
    CHECK(n_on() == 1);
    clocks(1, 0);                                                      /* clock 384: next step */
    CHECK(n_on() == 2 && on_note(1) == E3);
}

/* ---- live sustain (the sequencer runs: the engine sustains live notes under HOLD itself) ---- */
static void test_live_sustain_defers_releases_until_hold_off(void) {
    reset();                                                           /* arp off: the live path */
    arp_hold(&a, 1);
    on(C4); off(C4);
    CHECK(n_off() == 1 && !sounding[C4]);                             /* sustain off: stock's hold would do it */
    arp_set_sustain(&a, 1);
    on(D4); off(D4);
    CHECK(n_off() == 1 && sounding[D4]);                              /* deferred */
    on(E4); off(E4);
    CHECK(sounding[E4]);
    arp_set_sustain(&a, 0);                                            /* the sequencer stopped: still ours until HOLD off */
    CHECK(sounding[D4] && sounding[E4]);
    on(F4); off(F4);
    CHECK(!sounding[F4]);                                              /* a new release is stock's business again */
    arp_hold(&a, 0);
    CHECK(!sounding[D4] && !sounding[E4] && n_off() == 4);            /* HOLD off: the set is released */
    arp_set_sustain(&a, 1); arp_hold(&a, 1);
    on(C4); off(C4);
    CHECK(sounding[C4]);
    clear_log();
    on(C4);                                                            /* pressed again: the old note goes first */
    CHECK(n_off() == 1 && n_on() == 1 && sounding[C4] && log_[0].on == 0 && log_[1].on == 1);
    off(C4);
    CHECK(sounding[C4]);
    arp_all_notes_off(&a);
    CHECK(arp_pool_count(&a) == 0);
    arp_hold(&a, 0);
    CHECK(n_off() == 1);                                               /* nothing left to release */
    arp_set_sustain(&a, 1); arp_hold(&a, 1);
    on(G3); off(G3);
    CHECK(sounding[G3]);
    arp_enable(&a, 1);                                                 /* the arp starts: live notes are cut as always */
    CHECK(!sounding[G3]);
}

/* ---- the chord source (the sequencer's Arpeggiated style feeds the arp a chord) ------------ */
static void test_chord_source_runs_the_pattern_over_the_given_notes(void) {
    static const uint8_t notes[3] = { E4, C4, 67 }, vels[3] = { 90, 100, 80 };
    reset();                                                           /* arp off: the chord source runs anyway */
    arp_chord_set(&a, notes, vels, 3);
    CHECK(n_on() == 1 && on_note(0) == C4 && on_tick(0) == 0 && on_at(0)->vel == 100);   /* sorted: Up from C, at once */
    ticks(1000);
    CHECK(strcmp(ons(), "60 64 67 60 64 ") == 0 && on_tick(4) == 1000);
    on(D4);                                                            /* keys do not join a chord source: live, direct */
    CHECK(on_note(5) == D4 && sounding[D4]);
    ticks(250);
    CHECK(on_note(6) == 67 && sounding[D4]);
    off(D4);
    arp_set_mode(&a, ARP_ASSIGN);                                      /* the given order */
    clear_log(); arp_chord_set(&a, notes, vels, 3);
    ticks(749);
    CHECK(strcmp(ons(), "64 60 67 ") == 0);
    arp_set_mode(&a, ARP_UP);
    arp_chord_set(&a, notes, vels, 0);                                 /* a rest: silence, the clock runs */
    clear_log(); ticks(1000);
    CHECK(nlog == 0);
    arp_chord_clear(&a);
    ticks(1000);
    CHECK(nlog == 0);
    arp_chord_set(&a, notes, vels, 3);
    ticks(10);
    arp_chord_clear(&a);
    CHECK(n_sounding() == 0);                                          /* clear releases */
}

static void test_chord_source_under_midi_clock_counts_from_the_chord(void) {
    static const uint8_t notes[3] = { C4, E4, 67 }, vels[3] = { 100, 100, 100 };
    reset(); arp_set_ext(&a, 1);
    arp_realtime(&a, 0xFA, 0);
    clocks(5, 0);                                                      /* mid-grid */
    arp_rt_accept(&a, 0xF8, 0);                                        /* the chord is set on this clock, as the sequencer does ... */
    arp_chord_set(&a, notes, vels, 3);
    arp_rt_apply(&a, 0xF8);                                            /* ... and the clock is the chord's clock 0 */
    CHECK(n_on() == 1 && on_note(0) == C4);                            /* the chord's first note now, not at the grid */
    clocks(5, 0);
    CHECK(n_sounding() == 1);
    clocks(1, 0);
    CHECK(n_sounding() == 0);                                          /* gate 6 clocks later */
    clocks(6, 0);
    CHECK(n_on() == 2 && on_note(1) == E4);                            /* steps counted from the chord's clock */
}

static void test_realtime_reports_accepted_clocks(void) {
    reset(); arp_set_ext(&a, 1);
    CHECK(arp_realtime(&a, 0xFA, 0) == 1 && arp_realtime(&a, 0xF8, 0) == 1);
    CHECK(arp_realtime(&a, 0xF8, 1) == 0);                             /* the other port is locked out */
    CHECK(arp_realtime(&a, 0xF9, 0) == 0);                             /* not a realtime byte we act on */
    arp_set_ext(&a, 0);
    CHECK(arp_realtime(&a, 0xF8, 0) == 0);                             /* internal clock: ignored */
}

/* ---- assign -------------------------------------------------------------------------- */
static void assign_on(void) { reset(); arp_enable(&a, 1); arp_set_mode(&a, ARP_ASSIGN); }

static void test_assign_plays_the_entered_order_with_each_entrys_velocity(void) {
    assign_on();
    CHECK(ARP_ASSIGN == 4 && ARP_MODES == 5);
    arp_note(&a, LOCAL, G3, 30); arp_note(&a, LOCAL, C3, 60);           /* G steps at once (start rule) */
    arp_note(&a, MIDI, E3, 90); arp_note(&a, LOCAL, D3, 120);
    ticks(1000);                                                       /* 0 G, 250 C, 500 E, 750 D, 1000 G */
    CHECK(strcmp(ons(), "55 48 52 50 55 ") == 0);
    CHECK(on_at(0)->vel == 30 && on_at(1)->vel == 60 && on_at(2)->vel == 90 && on_at(3)->vel == 120);
    CHECK(on_tick(0) == 0 && on_tick(1) == 250);
    reset(); arp_set_mode(&a, ARP_ASSIGN);                             /* entered while the arp is off */
    on(G3); on(C3); on(E3); on(D3);
    clear_log();
    arp_enable(&a, 1);
    ticks(750);
    CHECK(strcmp(ons(), "55 48 52 50 ") == 0);
}

static void test_assign_duplicates_via_hold_and_relatch_replaces(void) {
    assign_on();
    arp_hold(&a, 1);
    on(G3); on(C3); on(E3); on(D3);                                    /* HOLD: picked up at the next step */
    off(C3); off(E3); off(D3);                                         /* latched; G still down */
    arp_note(&a, LOCAL, C3, 40);                                       /* C again: a second entry */
    ticks(1500);                                                       /* 250 G, 500 C, 750 E, 1000 D, 1250 C, 1500 G */
    CHECK(strcmp(ons(), "55 48 52 50 48 55 ") == 0);
    CHECK(on_at(1)->vel == 100 && on_at(4)->vel == 40);
    off(C3); off(G3);                                                  /* all up: everything latched */
    clear_log();
    ticks(250);
    CHECK(n_on() == 1);                                                /* keeps playing */
    on(A4); on(F4);                                                    /* re-latch: replaces, A then F */
    clear_log();
    ticks(1000);
    CHECK(strcmp(ons(), "69 65 69 65 ") == 0);
}

static void test_assign_release_without_hold_removes_the_pitch(void) {
    assign_on();
    on(G3); on(C3); on(E3); on(D3);
    off(E3);
    ticks(1000);
    CHECK(strcmp(ons(), "55 48 50 55 48 ") == 0);                      /* G C D */
    clear_log();                                                       /* C sounding (idx 1) */
    off(G3);                                                           /* removed before the position */
    ticks(750);
    CHECK(strcmp(ons(), "50 48 50 ") == 0);                            /* D still follows C */
    reset(); arp_enable(&a, 1); arp_set_mode(&a, ARP_ASSIGN);
    on(G3); on(C3); on(E3); on(D3);
    ticks(260);                                                        /* G, C sounding */
    off(C3);                                                           /* the sounding entry goes */
    clear_log();
    ticks(500);
    CHECK(strcmp(ons(), "52 50 ") == 0);                               /* E is next, not skipped */
    off(G3); clear_log(); ticks(250);
    CHECK(strcmp(ons(), "52 ") == 0);                                  /* E D: E follows D (wrap) */
    reset(); arp_enable(&a, 1); arp_set_mode(&a, ARP_ASSIGN);
    on(C3); on(E3); on(C3);                                            /* C pressed twice (second source) */
    off(C3);                                                           /* every entry of C goes */
    clear_log();
    ticks(500);
    CHECK(strcmp(ons(), "52 52 ") == 0);
}

static void test_assign_hold_off_drops_latched_entries(void) {
    assign_on();
    arp_hold(&a, 1);
    on(G3); on(C3); on(E3);
    off(C3);                                                           /* latched */
    on(C3); off(C3);                                                   /* C twice, both latched */
    arp_hold(&a, 0);                                                   /* G E remain */
    ticks(1000);
    CHECK(strcmp(ons(), "55 52 55 52 ") == 0);
}

static void test_assign_octaves_per_pass_and_all_notes_off(void) {
    assign_on();
    arp_set_octaves(&a, 2);
    on(G3); on(C3); on(E3); on(D3);
    ticks(2000);
    CHECK(strcmp(ons(), "55 48 52 50 67 60 64 62 55 ") == 0);
    arp_all_notes_off(&a);
    clear_log();
    on(E3);                                                            /* list was cleared: E alone */
    ticks(500);
    CHECK(strcmp(ons(), "52 64 52 ") == 0);
}

static void test_assign_list_holds_32_entries(void) {
    assign_on();
    for (int k = 0; k < 33; k++) on(40 + k);                           /* 33 notes; 40 steps at once */
    CHECK(arp_pool_count(&a) == 33);
    ticks(250 * 32);                                                   /* 41..71, then 40 again */
    char want[512]; int p = 0;
    for (int k = 0; k < 32; k++) p += sprintf(want + p, "%d ", 40 + k);
    sprintf(want + p, "40 ");
    CHECK(strcmp(ons(), want) == 0);                                   /* 72 is in the pool, not the order */
}

/* ---- BPM follows the MIDI clock ------------------------------------------------------ */
/* n clocks, each after `ms` ticks; ms 0 = the 120 BPM pattern 21 21 21 21 21 20 (500 per 24) */
static void spaced_clocks(int n, int ms) {
    static int k;
    while (n-- > 0) { ticks(ms ? ms : (k++ % 6 == 5 ? 20 : 21)); arp_realtime(&a, 0xF8, 0); }
}

static void test_bpm_follows_the_midi_clock_over_a_beat(void) {
    reset(); arp_set_bpm(&a, 77);
    arp_set_ext(&a, 1);
    arp_realtime(&a, 0xFA, 0);
    spaced_clocks(24, 0);                                              /* 23 intervals: not yet a beat */
    CHECK(a.bpm == 77);
    spaced_clocks(1, 0);                                               /* 24 intervals = 500 ms */
    CHECK(a.bpm == 120);
    spaced_clocks(23, 0);
    CHECK(a.bpm == 120);                                               /* steady */
    spaced_clocks(24, 25);                                             /* rolling: 600 ms per beat */
    CHECK(a.bpm == 100);
    arp_set_ext(&a, 0);                                                /* back to int: continues at 100 */
    arp_enable(&a, 1);
    clear_log();
    on(C3); on(E3);
    ticks(600);
    CHECK(n_on() == 3 && on_tick(1) - on_tick(0) == 300 && on_tick(2) - on_tick(1) == 300);   /* 1/8 at 100 */
}

static void test_bpm_window_restarts_on_transport_and_loss(void) {
    reset(); arp_enable(&a, 1);                                        /* also measured with the arp on */
    arp_set_ext(&a, 1);
    arp_realtime(&a, 0xFA, 0);
    spaced_clocks(25, 0);
    CHECK(a.bpm == 120);
    arp_realtime(&a, 0xFA, 0);                                         /* Start: a new window */
    spaced_clocks(24, 25);
    CHECK(a.bpm == 120);
    spaced_clocks(1, 25);
    CHECK(a.bpm == 100);
    arp_realtime(&a, 0xFC, 0);                                         /* Stop */
    spaced_clocks(24, 20);
    CHECK(a.bpm == 100);
    spaced_clocks(1, 20);                                              /* clocks while stopped still measure */
    CHECK(a.bpm == 125);
    arp_realtime(&a, 0xFB, 0);                                         /* Continue */
    spaced_clocks(24, 25);
    CHECK(a.bpm == 125);
    spaced_clocks(1, 25);
    CHECK(a.bpm == 100);
    ticks(1000);                                                       /* clock lost */
    spaced_clocks(24, 20);
    CHECK(a.bpm == 100);
    spaced_clocks(1, 20);
    CHECK(a.bpm == 125);
    arp_set_ext(&a, 0); arp_set_ext(&a, 1);                            /* source toggle */
    spaced_clocks(24, 25);
    CHECK(a.bpm == 125);
    reset(); arp_set_ext(&a, 1);                                       /* arp off: measured too; clamped */
    spaced_clocks(25, 5);                                              /* 120 ms per beat = 500 BPM */
    CHECK(a.bpm == 300);
    spaced_clocks(24, 70);                                             /* 1680 ms = 35.7 BPM */
    CHECK(a.bpm == 40);
    arp_set_ext(&a, 0);                                                /* int: clocks are ignored */
    spaced_clocks(30, 25);
    CHECK(a.bpm == 40);
}

int main(void) {
    test_defaults();
    test_disabled_passes_notes_straight_through();
    test_enable_releases_direct_notes_and_starts_at_once();
    test_disable_releases_step_and_resounds_held_keys_only();
    test_up_at_120_bpm_eighths_with_half_gate();
    test_velocity_is_the_keys_own();
    test_down_updown_and_single_note();
    test_random_is_uniform_over_the_pool_and_may_repeat();
    test_octaves_are_per_pass();
    test_first_key_into_empty_pool_starts_now_and_resets_phase();
    test_key_added_to_running_pattern_waits_for_the_step();
    test_relatch_replaces_pool_without_touching_the_phase();
    test_release_of_sounding_note_is_immediate_and_last_note_silences();
    test_hold_latches_and_hold_off_drops_latched_notes();
    test_hold_off_keeps_keys_still_down();
    test_all_notes_off_clears_pool_but_keeps_hold();
    test_bpm_and_beats_changes_keep_the_phase();
    test_mode_and_octave_change_restart_at_next_step_keeping_phase();
    test_ext_silent_without_clocks_and_steps_every_12_clocks();
    test_stop_continue_start();
    test_notes_never_reset_the_grid_under_ext();
    test_clock_loss_and_port_lock();
    test_beats_change_under_ext_realigns_from_start();
    test_swing_8s_internal_at_120_bpm();
    test_swing_16s_internal_at_120_bpm();
    test_swing_start_rule_begins_a_pair_and_settings_keep_the_half();
    test_swing_under_midi_clock();
    test_bpm_follows_the_midi_clock_over_a_beat();
    test_bpm_window_restarts_on_transport_and_loss();
    test_four_bar_steps_internal_and_midi_clock();
    test_live_sustain_defers_releases_until_hold_off();
    test_chord_source_runs_the_pattern_over_the_given_notes();
    test_chord_source_under_midi_clock_counts_from_the_chord();
    test_realtime_reports_accepted_clocks();
    test_assign_plays_the_entered_order_with_each_entrys_velocity();
    test_assign_duplicates_via_hold_and_relatch_replaces();
    test_assign_release_without_hold_removes_the_pitch();
    test_assign_hold_off_drops_latched_entries();
    test_assign_octaves_per_pass_and_all_notes_off();
    test_assign_list_holds_32_entries();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
