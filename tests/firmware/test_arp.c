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
    CHECK(a.sounding == ARP_NONE && a.seq_len == 0 && !a.seq_rec);
    CHECK(sizeof(arp_t) <= 0xA00);
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

/* ---- seq ----------------------------------------------------------------------------- */
static void record_cege(void) {
    arp_seq_record(&a, 1);
    on(C4); off(C4); on(E4); off(E4); on(G3 + 12); off(G3 + 12); on(E4); off(E4);
    arp_seq_record(&a, 0);
    clear_log();                                                       /* the recording sounded directly */
}

static void test_seq_records_sounds_directly_and_plays_transposed(void) {
    reset(); arp_enable(&a, 1);
    arp_seq_record(&a, 1);
    CHECK(arp_seq_record_note(&a, LOCAL, C4, 100) == 1);
    CHECK(n_on() == 1 && on_note(0) == C4);                            /* sounds while recording */
    arp_seq_record_note(&a, LOCAL, C4, 0);
    CHECK(n_sounding() == 0);
    CHECK(arp_seq_record_note(&a, LOCAL, E4, 100) == 2); arp_seq_record_note(&a, LOCAL, E4, 0);
    CHECK(arp_seq_record_note(&a, LOCAL, 67, 100) == 3); arp_seq_record_note(&a, LOCAL, 67, 0);
    CHECK(arp_seq_record_note(&a, LOCAL, E4, 100) == 4); arp_seq_record_note(&a, LOCAL, E4, 0);
    arp_seq_record(&a, 0);
    CHECK(a.seq_len == 4);
    clear_log();
    on(D3);                                                            /* trigger: D3 -> D F# A F# */
    ticks(1000);
    CHECK(strcmp(ons(), "50 54 57 54 50 ") == 0);
    clear_log();
    on(F4);                                                            /* D still down: transposes from the next step */
    ticks(500);
    CHECK(strcmp(ons(), "65 69 ") == 0 || strcmp(ons(), "69 72 ") == 0);  /* continues the pattern on F */
    off(F4);
    clear_log();
    ticks(1000);
    CHECK(strstr(ons(), "65") || strstr(ons(), "69") || strstr(ons(), "72"));   /* stays on F while D is down */
    CHECK(strstr(ons(), "50 ") == 0 && strstr(ons(), "54 ") == 0);
}

static void test_seq_octaves_modes_and_clear(void) {
    reset(); arp_enable(&a, 1);
    record_cege();
    arp_set_octaves(&a, 2);
    on(C4);
    ticks(1750);
    CHECK(strcmp(ons(), "60 64 67 64 72 76 79 76 ") == 0);              /* whole sequence, then an octave up */
    arp_set_octaves(&a, 1);
    arp_set_mode(&a, ARP_DOWN);
    clear_log();
    ticks(1000);
    CHECK(strcmp(ons(), "64 67 64 60 ") == 0);                         /* reversed sequence */
    arp_set_mode(&a, ARP_UP);
    arp_seq_clear(&a);
    clear_log();
    ticks(500);
    CHECK(strcmp(ons(), "60 60 ") == 0);                               /* back to the pool: C alone */
}

static void test_seq_restarts_on_fresh_key_and_latches(void) {
    reset(); arp_enable(&a, 1);
    record_cege();
    on(C4);
    ticks(400);                                                        /* C E */
    off(C4);
    clear_log();
    ticks(500);
    CHECK(n_on() == 0);
    on(G3);                                                            /* fresh key: starts now from step 1 */
    CHECK(n_on() == 1 && on_note(0) == G3);
    arp_hold(&a, 1);
    off(G3);
    clear_log();
    ticks(1000);
    CHECK(strcmp(ons(), "59 62 59 55 ") == 0);                         /* keeps playing, latched */
}


/* ---- seq: record mode, chord steps, rests, ties (spec "Seq") ---------------------------- */
static void rec_on(int note) { arp_seq_record_note(&a, LOCAL, note, 100); }
static void rec_off(int note) { arp_seq_record_note(&a, LOCAL, note, 0); }
static int off_at(int note, int t) { for (int i = 0; i < nlog; i++) if (!log_[i].on && log_[i].note == note && log_[i].t == t) return 1; return 0; }

static void test_seq_notes_held_together_form_a_chord_step(void) {
    reset(); arp_enable(&a, 1);
    arp_seq_record(&a, 1);
    CHECK(arp_seq_record_note(&a, LOCAL, E4, 90) == 1);                /* E first: the step opens */
    CHECK(arp_seq_record_note(&a, LOCAL, C4, 100) == 1);               /* C and G join while E is down */
    CHECK(arp_seq_record_note(&a, MIDI, 67, 80) == 1);
    CHECK(n_on() == 3 && n_sounding() == 3);                           /* all sound while recording */
    CHECK(arp_seq_record_note(&a, MIDI, C4, 70) == 1 && a.seq_n[0] == 3);   /* a pitch already in the step: not twice */
    rec_off(E4);
    CHECK(arp_seq_record_note(&a, LOCAL, A4, 100) == 1 && a.seq_n[0] == 4);  /* C and G still down: joins */
    rec_off(C4); rec_off(67); rec_off(A4);
    CHECK(n_sounding() == 0);
    CHECK(arp_seq_record_note(&a, LOCAL, D4, 100) == 2);               /* every key released: a new step */
    rec_off(D4);
    arp_seq_record(&a, 0);
    CHECK(a.seq_len == 2 && a.seq_n[0] == 4 && a.seq_n[1] == 1);
    CHECK(a.seq_note[0][0] == E4 && a.seq_vel[0][0] == 90 && a.seq_note[0][1] == C4 && a.seq_vel[0][1] == 100);
    CHECK(a.seq_dur[0] == 1 && a.seq_dur[1] == 1);
    clear_log();
    on(C4);                                                            /* reference = C4, the lowest of step 1 */
    CHECK(n_on() == 4 && n_sounding() == 4);                           /* the chord at once, as recorded */
    CHECK(sounding[C4] && sounding[E4] && sounding[67] && sounding[A4]);
    CHECK(on_at(0)->vel == 90 && on_at(1)->vel == 100 && on_at(2)->vel == 80);
    ticks(124);
    CHECK(n_sounding() == 4);
    ticks(1);
    CHECK(n_sounding() == 0);                                          /* gate: the whole chord released */
    ticks(125);
    CHECK(n_on() == 5 && on_note(4) == D4 && on_tick(4) == 250);
    off(C4);
    clear_log();
    on(D4);                                                            /* trigger D: the chord up a tone */
    CHECK(n_on() == 4 && sounding[D4] && sounding[66] && sounding[A4] && sounding[71]);
}

static void test_seq_rest_and_tie_with_internal_clock(void) {
    reset(); arp_enable(&a, 1);
    record_cege();                                                     /* an old sequence */
    arp_seq_record(&a, 1);
    CHECK(arp_seq_rest_tie(&a) == 0 && a.seq_len == 1 && a.seq_n[0] == 0);   /* a rest first discards the old one too */
    rec_on(C4); rec_off(C4);
    CHECK(a.seq_len == 2);
    CHECK(arp_seq_rest_tie(&a) == 0 && a.seq_len == 3);                /* no key down: rest */
    rec_on(E4);
    CHECK(arp_seq_rest_tie(&a) == 1 && arp_seq_rest_tie(&a) == 1);     /* key down: ties */
    CHECK(a.seq_len == 4 && a.seq_dur[3] == 3 && a.seq_dur[2] == 1);
    rec_off(E4);
    CHECK(arp_seq_rest_tie(&a) == 0 && a.seq_len == 5);                /* released: a rest again */
    arp_seq_record(&a, 0);
    clear_log();
    on(C4);                                                            /* rest, C, rest, E(3), rest */
    ticks(249);
    CHECK(n_on() == 0);                                                /* the rest: silence */
    ticks(1);
    CHECK(n_on() == 1 && on_note(0) == C4 && on_tick(0) == 250);
    ticks(125);
    CHECK(n_sounding() == 0);                                          /* gate at 375 */
    ticks(375);                                                        /* 750: E, three steps long */
    CHECK(n_on() == 2 && on_note(1) == E4 && on_tick(1) == 750);
    ticks(500);                                                        /* 1250: no retrigger at 1000 or 1250 */
    CHECK(n_on() == 2 && n_sounding() == 1 && sounding[E4]);
    ticks(124);
    CHECK(n_sounding() == 1);
    ticks(1);
    CHECK(n_sounding() == 0 && off_at(E4, 1375));                      /* released half-way through its last step */
    ticks(125);                                                        /* 1500: the final rest */
    CHECK(n_on() == 2);
    ticks(250);                                                        /* 1750: round again, the first rest */
    CHECK(n_on() == 2);
    ticks(250);
    CHECK(n_on() == 3 && on_note(2) == C4 && on_tick(2) == 2000);
}

static void test_seq_tie_under_midi_clock(void) {
    reset(); arp_enable(&a, 1);
    arp_seq_record(&a, 1);
    rec_on(C4); rec_off(C4);
    arp_seq_rest_tie(&a);                                              /* rest */
    rec_on(E4); arp_seq_rest_tie(&a); arp_seq_rest_tie(&a); rec_off(E4);   /* E, three steps */
    arp_seq_record(&a, 0);
    clear_log();                                                       /* the recording sounded directly */
    arp_set_ext(&a, 1);
    on(C4);
    arp_realtime(&a, 0xFA, 0);
    clocks(1, 0);                                                      /* clock 0: C */
    CHECK(n_on() == 1 && on_note(0) == C4);
    clocks(6, 0);                                                      /* clock 6: gate off */
    CHECK(n_sounding() == 0);
    clocks(6, 0);                                                      /* clock 12: rest */
    CHECK(n_on() == 1);
    clocks(12, 0);                                                     /* clock 24: E */
    CHECK(n_on() == 2 && on_note(1) == E4);
    clocks(24, 0);                                                     /* clocks 36 and 48: no retrigger, still sounding */
    CHECK(n_on() == 2 && n_sounding() == 1);
    clocks(5, 0);                                                      /* clock 53 */
    CHECK(n_sounding() == 1);
    clocks(1, 0);                                                      /* clock 54: the gate of its last step */
    CHECK(n_sounding() == 0);
    clocks(6, 0);                                                      /* clock 60: C again */
    CHECK(n_on() == 3 && on_note(2) == C4);
}

static void test_seq_direction_modes_keep_each_steps_chord_and_length(void) {
    int t0;
    reset(); arp_enable(&a, 1);
    arp_seq_record(&a, 1);
    rec_on(C4); rec_off(C4);
    arp_seq_rest_tie(&a);
    rec_on(E4); arp_seq_rest_tie(&a); arp_seq_rest_tie(&a); rec_off(E4);   /* C, rest, E(3) */
    arp_seq_record(&a, 0);
    clear_log();                                                       /* the recording sounded directly */
    arp_set_mode(&a, ARP_DOWN);
    on(C4);
    ticks(1300);                                                       /* E(3) @0, rest @750, C @1000, E @1250 */
    CHECK(strcmp(ons(), "64 60 64 ") == 0);
    CHECK(on_tick(0) == 0 && off_at(E4, 625) && on_tick(1) == 1000 && on_tick(2) == 1250);
    off(C4);
    arp_set_mode(&a, ARP_RANDOM);
    clear_log();
    on(C4);
    ticks(5000);
    for (int i = 0; i < nlog; i++) CHECK(log_[i].note == C4 || log_[i].note == E4);
    off(C4);
    arp_seq_record(&a, 1);
    rec_on(C4); rec_off(C4); rec_on(D4); rec_off(D4); rec_on(E4); arp_seq_rest_tie(&a); rec_off(E4);   /* C, D, E(2) */
    arp_seq_record(&a, 0);
    arp_set_mode(&a, ARP_UPDOWN);
    clear_log();
    t0 = now;
    on(C4);
    ticks(2000);                                                       /* 0 250 500(E, two steps) 1000 1250 1500 1750 */
    CHECK(strcmp(ons(), "60 62 64 62 60 62 64 ") == 0);
    CHECK(on_tick(2) == t0 + 500 && off_at(E4, t0 + 875) && on_tick(3) == t0 + 1000);
}

static void test_seq_reference_is_the_lowest_note_of_the_first_sounding_step(void) {
    reset(); arp_enable(&a, 1);
    arp_seq_record(&a, 1);
    arp_seq_rest_tie(&a);                                              /* a rest first */
    rec_on(A4); rec_on(D4); rec_off(A4); rec_off(D4);                  /* then a chord: D, the lowest, is the reference */
    arp_seq_record(&a, 0);
    clear_log();                                                       /* the recording sounded directly */
    on(C4);
    ticks(250);
    CHECK(n_on() == 2 && on_tick(0) == 250 && on_tick(1) == 250);
    CHECK((on_note(0) == C4 && on_note(1) == 67) || (on_note(0) == 67 && on_note(1) == C4));
}

static void test_seq_out_of_range_notes_are_silent_but_the_step_keeps_its_place(void) {
    reset(); arp_enable(&a, 1);
    arp_seq_record(&a, 1);
    rec_on(C4); rec_on(125); rec_off(C4); rec_off(125);                /* a chord of C4 and a very high note */
    rec_on(D4); rec_off(D4);
    arp_seq_record(&a, 0);
    clear_log();                                                       /* the recording sounded directly */
    on(F4);                                                            /* +5: 65 and 130 */
    ticks(300);
    CHECK(strcmp(ons(), "65 67 ") == 0 && on_tick(1) == 250);         /* 130 silent; F sounds, G follows in its place */
}

static void test_seq_record_mode_entry_and_exit(void) {
    int k;
    enabled_with_ceg();                                                /* arp on, C E G, C sounding */
    ticks(10);
    clear_log();
    arp_seq_record(&a, 1);
    CHECK(has_off(C3) && n_sounding() == 0 && arp_pool_count(&a) == 0);   /* entry: step released, pool emptied */
    k = nlog;
    off(C3); off(E3); off(G3);
    ticks(1000);
    CHECK(nlog == k);                                                  /* keys down at entry: ignored, nothing plays */
    rec_on(D4);
    CHECK(sounding[D4]);
    arp_seq_record(&a, 0);                                             /* leave with D still down */
    CHECK(has_off(D4) && n_sounding() == 0 && a.seq_len == 1);
    k = nlog;
    ticks(1000);
    off(D4);
    CHECK(nlog == k);                                                  /* D down at exit: ignored until pressed again */
    on(D4);
    CHECK(n_on() == 2 && on_note(1) == D4 && sounding[D4]);           /* the one-step sequence runs from D */
    reset();                                                           /* arp off: keys sounding directly are released on entry */
    on(C3);
    arp_seq_record(&a, 1);
    CHECK(has_off(C3) && n_sounding() == 0);
    arp_seq_record(&a, 0);
    CHECK(a.seq_len == 0);
    record_cege();                                                     /* leaving without a step keeps the old sequence */
    arp_seq_record(&a, 1);
    arp_seq_record(&a, 0);
    CHECK(a.seq_len == 4);
}

static void test_seq_capacity_64_steps_10_notes_64_ties(void) {
    reset();
    arp_seq_record(&a, 1);
    for (int i = 0; i < 70; i++) { rec_on(30 + i % 40); rec_off(30 + i % 40); }
    CHECK(a.seq_len == 64 && n_on() == 70);                            /* beyond 64: sounds, not recorded */
    arp_seq_clear(&a);
    clear_log();
    for (int i = 0; i < 12; i++) rec_on(40 + i);
    CHECK(a.seq_len == 1 && a.seq_n[0] == 10 && n_on() == 12);         /* beyond 10 in a step: sounds, not recorded */
    for (int i = 0; i < 70; i++) arp_seq_rest_tie(&a);
    CHECK(a.seq_dur[0] == 64);
    for (int i = 0; i < 12; i++) rec_off(40 + i);
    CHECK(n_sounding() == 0);
    arp_seq_record(&a, 0);
}

static void test_seq_all_notes_off_enable_and_clear_during_record_mode(void) {
    reset();
    arp_seq_record(&a, 1);
    rec_on(C4);
    arp_all_notes_off(&a);
    CHECK(has_off(C4) && a.seq_len == 1 && a.seq_rec);                 /* CC 123: notes cut, recording kept */
    rec_off(C4);
    rec_on(E4); rec_off(E4);
    CHECK(a.seq_len == 2);
    rec_on(G3);
    clear_log();
    arp_enable(&a, 1);                                                 /* a program load while recording */
    CHECK(a.enabled && nlog == 0 && sounding[G3]);                     /* nothing cut, nothing started */
    rec_off(G3);
    CHECK(has_off(G3));
    arp_seq_clear(&a);                                                 /* Program 6 while recording */
    CHECK(a.seq_len == 0 && a.seq_rec);
    rec_on(A4); rec_off(A4);
    CHECK(a.seq_len == 1);
    arp_seq_record(&a, 0);
    clear_log();
    on(C4);
    CHECK(n_on() == 1 && on_note(0) == C4);                            /* the one-step sequence, on C */
}

/* ---- seq: arpeggiated playback (spec "Seq — Arpeggiated playback") -------------------- */
/* C major (recorded E, C, G), then F major: two chord steps */
static void record_two_chords(void) {
    arp_seq_record(&a, 1);
    rec_on(E4); rec_on(C4); rec_on(67); rec_off(E4); rec_off(C4); rec_off(67);
    rec_on(F4); rec_on(A4); rec_on(72); rec_off(F4); rec_off(A4); rec_off(72);
    arp_seq_record(&a, 0);
    clear_log();
}
static void arp_mode_on(int chord_beats) {
    reset(); arp_enable(&a, 1);
    record_two_chords();
    arp_set_seq_arp(&a, 1);
    arp_set_chord_beats(&a, chord_beats);
}

static void test_arp_mode_plays_each_chord_for_the_chord_length(void) {
    arp_mode_on(4);                                                    /* Whole: 4 beats = 2000 ms = 8 eighths */
    CHECK(a.seq_arp == 1 && a.chord_beats == 4);
    on(C4);                                                            /* trigger = reference: no transposition */
    ticks(3999);
    CHECK(strcmp(ons(), "60 64 67 60 64 67 60 64 65 69 72 65 69 72 65 69 ") == 0);   /* Up over C, then over F */
    CHECK(on_tick(7) == 1750 && on_tick(8) == 2000 && on_tick(15) == 3750);
    ticks(1);
    CHECK(n_on() == 17 && on_note(16) == C4 && on_tick(16) == 4000);  /* round again */
    CHECK(n_sounding() == 1);                                          /* one note at a time, as the arp */
    off(C4);
    CHECK(n_sounding() == 0);
}

static void test_arp_mode_direction_octaves_and_assign_apply_inside_the_chord(void) {
    int t0;
    arp_mode_on(2);                                                    /* Half: 4 eighths per chord */
    arp_set_mode(&a, ARP_DOWN);
    on(C4);
    ticks(1999);                                                       /* two chords, the ninth note lands at 2000 */
    CHECK(strcmp(ons(), "67 64 60 67 72 69 65 72 ") == 0);             /* Down inside, chords forward */
    off(C4);
    arp_set_mode(&a, ARP_UPDOWN);
    clear_log(); on(C4); ticks(1999);
    CHECK(strcmp(ons(), "60 64 67 64 65 69 72 69 ") == 0);
    off(C4);
    arp_set_mode(&a, ARP_ASSIGN);                                      /* the order the notes were recorded: E C G */
    clear_log(); on(C4); ticks(1999);
    CHECK(strcmp(ons(), "64 60 67 64 65 69 72 65 ") == 0);
    off(C4);
    arp_set_mode(&a, ARP_UP); arp_set_octaves(&a, 2);                  /* passes within the chord */
    clear_log(); on(C4); ticks(1999);
    CHECK(strcmp(ons(), "60 64 67 72 65 69 72 77 ") == 0);
    off(C4);
    arp_set_octaves(&a, 1);
    arp_set_mode(&a, ARP_RANDOM);
    clear_log(); t0 = now; on(C4); ticks(999);
    for (int i = 0; i < nlog; i++) CHECK(log_[i].note == C4 || log_[i].note == E4 || log_[i].note == 67);   /* first chord only */
    ticks(1000);
    for (int i = 0; i < nlog; i++) if (log_[i].on && log_[i].t >= t0 + 1000) CHECK(log_[i].note == F4 || log_[i].note == A4 || log_[i].note == 72);
}

static void test_arp_mode_ties_rests_and_velocity(void) {
    reset(); arp_enable(&a, 1);
    arp_seq_record(&a, 1);
    arp_seq_record_note(&a, LOCAL, C4, 90); arp_seq_record_note(&a, LOCAL, E4, 50);
    arp_seq_rest_tie(&a);                                              /* tied: two chord lengths */
    rec_off(C4); rec_off(E4);
    arp_seq_rest_tie(&a);                                              /* a rest */
    rec_on(67); rec_off(67);                                           /* a one-note chord */
    arp_seq_record(&a, 0);
    arp_set_seq_arp(&a, 1); arp_set_chord_beats(&a, 1);                /* Qtr: 2 eighths */
    clear_log();
    on(C4);
    ticks(2200);                                                       /* C E C E (0..750) | rest (1000..1250) | G G (1500, 1750) | C ... */
    CHECK(strcmp(ons(), "60 64 60 64 67 67 60 ") == 0);
    CHECK(on_tick(3) == 750 && on_tick(4) == 1500 && on_tick(5) == 1750 && on_tick(6) == 2000);
    CHECK(on_at(0)->vel == 90 && on_at(1)->vel == 50);
}

static void test_arp_mode_under_midi_clock(void) {
    arp_mode_on(4);                                                    /* Whole = 96 clocks; eighths = 12 */
    arp_set_ext(&a, 1);
    on(C4);
    arp_realtime(&a, 0xFA, 0);
    clocks(1, 0);
    CHECK(n_on() == 1 && on_note(0) == C4);
    clocks(95, 0);                                                     /* through clock 95: 8 notes of C */
    CHECK(n_on() == 8 && on_note(7) == E4);
    clocks(1, 0);                                                      /* clock 96: F chord */
    CHECK(n_on() == 9 && on_note(8) == F4);
    clocks(96, 0);
    CHECK(n_on() == 17 && on_note(16) == C4);                          /* round again on the grid */
}

static void test_arp_mode_uneven_note_value_is_cut_at_the_chord_boundary(void) {
    arp_mode_on(1);                                                    /* Qtr: 500 ms */
    arp_set_beats(&a, 3, 4);                                           /* 8th D: 375 ms steps, not dividing it */
    on(C4);
    ticks(499);
    CHECK(n_on() == 2 && on_tick(1) == 375 && n_sounding() == 1);     /* E sounding, its gate would be at 562 */
    ticks(1);
    CHECK(n_sounding() == 1 && n_on() == 3 && on_note(2) == F4 && on_tick(2) == 500);   /* cut: F starts on time */
    CHECK(off_at(E4, 500));
}

static void test_arp_mode_new_trigger_from_the_next_step_and_fresh_key_restarts(void) {
    arp_mode_on(4);
    on(C4);
    ticks(300);
    on(D4);                                                            /* new trigger, C still down */
    ticks(200);
    CHECK(n_on() == 3 && on_note(2) == 69);                            /* step at 500: G + 2 */
    ticks(1500);
    CHECK(on_note(8) == 67 && on_tick(8) == 2000);                     /* chord boundary unchanged: F + 2 at 2000 */
    arp_hold(&a, 1);
    off(C4); off(D4);                                                  /* latched: keeps playing */
    clear_log();
    on(E4); off(E4);                                                   /* re-latch: the first chord from its first note, on E, at the next step */
    ticks(250);
    CHECK(n_on() == 1 && on_note(0) == E4 && on_tick(0) == 2250);
    ticks(2000);
    CHECK(on_note(8) == 69 && on_tick(8) == 4250);                     /* the second chord, a chord length later */
}

static void test_arp_mode_change_takes_effect_and_without_a_sequence_changes_nothing(void) {
    arp_mode_on(4);
    on(C4);
    ticks(600);                                                        /* C E G */
    arp_set_seq_arp(&a, 0);                                            /* back to POL: from the next step, from the first step */
    ticks(400);
    CHECK(n_on() == 9 && on_tick(3) == 750 && on_tick(5) == 750 && on_tick(6) == 1000);   /* C chord as a block at 750, F at 1000 */
    off(C4);
    reset(); arp_enable(&a, 1); arp_set_seq_arp(&a, 1);                /* no sequence: the plain arp */
    chord_ceg();
    ticks(600);
    CHECK(strcmp(ons(), "48 52 55 ") == 0);
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

static void test_assign_with_a_sequence_plays_the_recorded_order(void) {
    reset(); arp_enable(&a, 1);
    record_cege();
    arp_set_mode(&a, ARP_ASSIGN);
    on(C4);
    ticks(1000);
    CHECK(strcmp(ons(), "60 64 67 64 60 ") == 0);
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
    test_seq_records_sounds_directly_and_plays_transposed();
    test_seq_octaves_modes_and_clear();
    test_seq_restarts_on_fresh_key_and_latches();
    test_seq_notes_held_together_form_a_chord_step();
    test_seq_rest_and_tie_with_internal_clock();
    test_seq_tie_under_midi_clock();
    test_seq_direction_modes_keep_each_steps_chord_and_length();
    test_seq_reference_is_the_lowest_note_of_the_first_sounding_step();
    test_seq_out_of_range_notes_are_silent_but_the_step_keeps_its_place();
    test_seq_record_mode_entry_and_exit();
    test_seq_capacity_64_steps_10_notes_64_ties();
    test_seq_all_notes_off_enable_and_clear_during_record_mode();
    test_arp_mode_plays_each_chord_for_the_chord_length();
    test_arp_mode_direction_octaves_and_assign_apply_inside_the_chord();
    test_arp_mode_ties_rests_and_velocity();
    test_arp_mode_under_midi_clock();
    test_arp_mode_uneven_note_value_is_cut_at_the_chord_boundary();
    test_arp_mode_new_trigger_from_the_next_step_and_fresh_key_restarts();
    test_arp_mode_change_takes_effect_and_without_a_sequence_changes_nothing();
    test_assign_plays_the_entered_order_with_each_entrys_velocity();
    test_assign_duplicates_via_hold_and_relatch_replaces();
    test_assign_release_without_hold_removes_the_pitch();
    test_assign_hold_off_drops_latched_entries();
    test_assign_octaves_per_pass_and_all_notes_off();
    test_assign_with_a_sequence_plays_the_recorded_order();
    test_assign_list_holds_32_entries();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
