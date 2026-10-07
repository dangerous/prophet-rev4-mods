/* Host harness for the native arp engine core (docs/SPEC.md: "Native arp engine (stock 2.1.0
 * base)" — Note pool, Pattern, Clock, Output). A fake voice allocator records every note-on /
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
    CHECK(a.sounding == ARP_NONE && a.seq_len == 0);
    CHECK(sizeof(arp_t) <= 0x200);
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
    CHECK(repeats > 10);                                               /* no repeat avoidance (V5 behaviour) */
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
    test_seq_records_sounds_directly_and_plays_transposed();
    test_seq_octaves_modes_and_clear();
    test_seq_restarts_on_fresh_key_and_latches();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
