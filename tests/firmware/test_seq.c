/* Host harness for the sequencer (docs/SPEC.md: "Seq — the sequencer"): storage and
 * capacity, recording with Back, the independent transport under both clocks, the Chords
 * and Arpeggiated styles, transposition. A fake voice allocator logs every note-on/off with
 * its tick. `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "arp.h"
#include "platform.h"
#include "seq.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake voices --------------------------------------------------------------------- */
typedef struct { int on, note, vel, t, live; } ev_t;
static ev_t log_[16384];
static int nlog, now;
static int sounding[128];
static void push(int on, int note, int vel, int live) {
    if (nlog < (int)(sizeof log_ / sizeof *log_)) log_[nlog++] = (ev_t){on, note, vel, now, live};
    if (note >= 0 && note < 128) sounding[note] = on;
}
void plat_voice_on(int src, int note, int vel) { (void)src; push(1, note, vel, 0); }
void plat_voice_off(int src, int note) { (void)src; push(0, note, 0, 0); }        /* a generated note's release */
void plat_live_off(int src, int note) { (void)src; push(0, note, 0, 1); }         /* a live note's release */

static int n_on(void) { int n = 0; for (int i = 0; i < nlog; i++) n += log_[i].on; return n; }
static ev_t *on_at(int k) { for (int i = 0; i < nlog; i++) if (log_[i].on && k-- == 0) return &log_[i]; return 0; }
static int on_note(int k) { ev_t *e = on_at(k); return e ? e->note : -1; }
static int on_tick(int k) { ev_t *e = on_at(k); return e ? e->t : -1; }
static int n_sounding(void) { int n = 0; for (int i = 0; i < 128; i++) n += sounding[i] != 0; return n; }
static int off_at(int note, int t) { for (int i = 0; i < nlog; i++) if (!log_[i].on && log_[i].note == note && log_[i].t == t) return 1; return 0; }
static void clear_log(void) { nlog = 0; }
static const char *ons(void) {
    static char buf[4096]; int p = 0; buf[0] = 0;
    for (int i = 0; i < nlog && p < 4000; i++) if (log_[i].on) p += sprintf(buf + p, "%d ", log_[i].note);
    return buf;
}

/* ---- helpers ------------------------------------------------------------------------- */
enum { LOCAL = ARP_SRC_LOCAL, MIDI = ARP_SRC_MIDI, C4 = 60, D4 = 62, E4 = 64, F4 = 65, G4 = 67, A4 = 69, B4 = 71 };
static arp_t a;
static seq_t q;

static void reset(void) {
    arp_init(&a); seq_init(&q);
    clear_log(); memset(sounding, 0, sizeof sounding); now = 0;
}
static void ticks(int n) { while (n-- > 0) { now++; if (!seq_tick(&q, &a)) arp_tick(&a); } }   /* as the glue's tick: a chord set spends the arp's tick */
static void play(void) { now = 0; clear_log(); seq_start(&q, &a); }   /* a fresh start with t = 0 */
/* a MIDI realtime byte as the glue routes it: the arp decides (clock source, port lock), the
 * sequencer acts first (a chord boundary sets the arp's chord before the arp counts the clock) */
static void rt(int byte) { if (arp_rt_accept(&a, byte, 0)) { seq_realtime(&q, &a, byte); arp_rt_apply(&a, byte); } }
static void clocks(int n) { while (n-- > 0) rt(0xF8); }
static void ron(int note) { seq_rec_note(&q, &a, LOCAL, note, 100); }
static void roff(int note) { seq_rec_note(&q, &a, LOCAL, note, 0); }
/* C, {E G} tied once, rest, B: four events, five timing steps */
static void record_four(void) {
    seq_rec_begin(&q, &a);
    ron(C4); roff(C4);
    ron(E4); ron(G4); seq_rec_rest_tie(&q); roff(E4); roff(G4);
    seq_rec_rest_tie(&q);
    ron(B4); roff(B4);
    seq_rec_end(&q);
    clear_log();
}
/* C major (recorded E, C, G) then F major: two chord events */
static void record_two_chords(void) {
    seq_rec_begin(&q, &a);
    ron(E4); ron(C4); ron(G4); roff(E4); roff(C4); roff(G4);
    ron(F4); ron(A4); ron(72); roff(F4); roff(A4); roff(72);
    seq_rec_end(&q);
    clear_log();
}

/* ---- defaults, storage, capacity --------------------------------------------------- */
static void test_defaults_and_size(void) {
    reset();
    CHECK(sizeof(seq_t) <= 0x3400);                                   /* 512 events of 24 bytes + state, inside the 16 KB area */
    CHECK(SEQ_STEPS == 512 && SEQ_CHORD == 10);
    CHECK(q.len == 0 && q.total == 0 && !q.playing && !q.rec);
    CHECK(q.style == SEQ_CHORDS && q.order == SEQ_FOR && q.chord_beats == 4 && q.transpose == 0);
    CHECK(q.beats_num == 1 && q.beats_den == 2 && !q.swing);         /* 8th */
    ticks(3000);
    CHECK(nlog == 0);
}

static void test_recording_counts_timing_steps(void) {
    reset();
    seq_rec_begin(&q, &a);
    CHECK(q.rec && seq_rec_note(&q, &a, LOCAL, C4, 90) == 1);             /* returns the total */
    CHECK(n_on() == 1 && on_at(0)->vel == 90);                         /* sounds as played */
    CHECK(seq_rec_note(&q, &a, MIDI, E4, 80) == 1 && q.len == 1 && q.ev[0].n == 2);   /* held together: one chord */
    CHECK(seq_rec_rest_tie(&q) == 1 && q.total == 2 && q.ev[0].dur == 2);         /* a tie: one more step */
    roff(C4); roff(E4);
    CHECK(n_sounding() == 0);
    CHECK(seq_rec_rest_tie(&q) == 0 && q.total == 3 && q.len == 2 && q.ev[1].n == 0 && q.ev[1].dur == 1);   /* a rest */
    ron(G4); roff(G4);
    CHECK(q.total == 4 && q.len == 3 && q.ev[2].note[0] == G4 && q.ev[2].vel[0] == 100);
    seq_rec_end(&q);
    CHECK(!q.rec && q.len == 3 && q.total == 4);
    /* a pitch already in the open chord is not added twice; beyond ten notes sound unrecorded */
    seq_rec_begin(&q, &a);
    ron(C4); seq_rec_note(&q, &a, MIDI, C4, 50);
    CHECK(q.ev[0].n == 1);
    for (int i = 1; i < 12; i++) ron(C4 + i);
    CHECK(q.ev[0].n == 10 && q.total == 1);
    for (int i = 0; i < 12; i++) roff(C4 + i);
    seq_rec_end(&q);
}

static void test_capacity_is_512_timing_steps_in_total(void) {
    reset();
    seq_rec_begin(&q, &a);
    for (int i = 0; i < 520; i++) { ron(30 + i % 60); roff(30 + i % 60); }
    CHECK(q.total == 512 && q.len == 512 && n_on() == 520);           /* the 513th onwards sounds, unrecorded */
    CHECK(seq_rec_rest_tie(&q) == -1 && q.total == 512);              /* no rest either */
    ron(C4);
    CHECK(seq_rec_rest_tie(&q) == -1 && q.total == 512);              /* nor a tie */
    roff(C4);
    seq_rec_end(&q);
    reset();
    seq_rec_begin(&q, &a);
    ron(C4);
    for (int i = 0; i < 600; i++) seq_rec_rest_tie(&q);               /* one chord tied to the limit */
    CHECK(q.len == 1 && q.ev[0].dur == 512 && q.total == 512);
    roff(C4);
    for (int i = 0; i < 300; i++) { ron(D4); roff(D4); }
    CHECK(q.total == 512 && q.len == 1);
    seq_rec_end(&q);
}

static void test_back_undoes_ties_then_events_and_closes_the_chord(void) {
    reset();
    seq_rec_begin(&q, &a);
    ron(C4); ron(E4); seq_rec_rest_tie(&q); seq_rec_rest_tie(&q); roff(C4); roff(E4);
    seq_rec_rest_tie(&q);                                              /* chord(3), rest */
    CHECK(q.len == 2 && q.total == 4);
    CHECK(seq_rec_back(&q) == 1 && q.len == 1 && q.total == 3);        /* the rest goes */
    CHECK(seq_rec_back(&q) == 1 && q.ev[0].dur == 2 && q.total == 2);  /* 3 -> 2 */
    CHECK(seq_rec_back(&q) == 1 && q.ev[0].dur == 1 && q.total == 1);  /* 2 -> 1 */
    CHECK(seq_rec_back(&q) == 1 && q.len == 0 && q.total == 0);        /* the chord goes */
    CHECK(seq_rec_back(&q) == 0 && q.len == 0);                        /* nothing left */
    ron(C4);                                                           /* an open chord ... */
    CHECK(seq_rec_back(&q) == 1 && q.len == 0);                        /* ... removed while C is still down */
    ron(E4);
    CHECK(q.len == 1 && q.ev[0].n == 1 && q.ev[0].note[0] == E4);     /* C does not join: a new event */
    roff(C4);
    CHECK(!sounding[C4]);                                              /* its release still frees it */
    roff(E4);
    seq_rec_end(&q);
    CHECK(q.len == 1 && q.total == 1);
}

static void test_old_sequence_survives_until_the_first_entry_and_back_respects_that(void) {
    reset();
    record_four();
    seq_rec_begin(&q, &a);
    CHECK(q.len == 4 && q.total == 5);                                 /* the old one stands ... */
    CHECK(seq_rec_back(&q) == 0 && q.len == 4);                        /* ... Back at r 0 does not touch it */
    seq_rec_end(&q);
    CHECK(q.len == 4);                                                 /* nothing entered: kept */
    seq_rec_begin(&q, &a);
    ron(C4); roff(C4);
    CHECK(q.len == 1 && q.total == 1);                                 /* replaced */
    CHECK(seq_rec_back(&q) == 1 && q.len == 0 && q.total == 0);
    seq_rec_end(&q);
    CHECK(q.len == 0);                                                 /* the old one is gone, not resurrected */
}

/* ---- transport: Chords -------------------------------------------------------------- */
static void test_chords_play_forward_with_gate_extensions_and_loop(void) {
    reset(); record_four();
    CHECK(seq_start(&q, &a) == 1 && q.playing);
    CHECK(n_on() == 1 && on_note(0) == C4 && on_tick(0) == 0);        /* the first event at once */
    ticks(125);
    CHECK(n_sounding() == 0);                                          /* gate at half the step */
    ticks(125);
    CHECK(n_on() == 3 && sounding[E4] && sounding[G4] && on_tick(1) == 250);   /* the chord, together */
    ticks(250);
    CHECK(n_on() == 3 && n_sounding() == 2);                           /* tied: no retrigger at 500 */
    ticks(125);
    CHECK(n_sounding() == 0 && off_at(E4, 625) && off_at(G4, 625));    /* released half-way through its last step */
    ticks(125);
    CHECK(n_on() == 3);                                                /* 750: the rest */
    ticks(250);
    CHECK(n_on() == 4 && on_note(3) == B4 && on_tick(3) == 1000);
    ticks(250);
    CHECK(n_on() == 5 && on_note(4) == C4 && on_tick(4) == 1250);      /* round again */
    seq_stop(&q, &a);
    CHECK(!q.playing && n_sounding() == 0);
    ticks(1000);
    CHECK(n_on() == 5);
    seq_start(&q, &a);
    CHECK(on_note(5) == C4);                                           /* a stop restarts at event 1 */
}

static void test_orders_forward_back_pendulum(void) {
    reset();
    seq_rec_begin(&q, &a);
    ron(C4); roff(C4); ron(D4); roff(D4); ron(E4); roff(E4);
    seq_rec_end(&q); clear_log();
    seq_set_order(&q, SEQ_BACK);
    play(); ticks(999);
    CHECK(strcmp(ons(), "64 62 60 64 ") == 0);
    seq_stop(&q, &a);
    seq_set_order(&q, SEQ_PEND);
    play(); ticks(1749);
    CHECK(strcmp(ons(), "60 62 64 62 60 62 64 ") == 0);               /* ends not repeated */
    seq_stop(&q, &a);
    seq_set_order(&q, SEQ_FOR);
    play(); ticks(999);
    CHECK(strcmp(ons(), "60 62 64 60 ") == 0);
    arp_set_mode(&a, ARP_DOWN); arp_set_octaves(&a, 2);               /* the Arp's settings: no effect, no disturbance */
    ticks(500);
    CHECK(strcmp(ons(), "60 62 64 60 62 64 ") == 0 && on_tick(5) == 1250);
}

static void test_transposition_from_the_next_event_without_disturbing_timing(void) {
    reset(); record_four();
    seq_start(&q, &a);
    ticks(100);
    seq_set_transpose(&q, 2);
    ticks(150);
    CHECK(on_note(1) == E4 + 2 && on_tick(1) == 250 && sounding[G4 + 2]);   /* from the next event */
    CHECK(q.ev[1].note[0] == E4);                                      /* recorded pitches unchanged */
    seq_set_transpose(&q, 60);                                         /* B4 + 60 = 131: silent, duration kept */
    ticks(1000);
    CHECK(n_on() == 4 && on_tick(3) == 1250 && on_note(3) == C4 + 60); /* 1000: silent B; 1250: C + 60 */
    seq_stop(&q, &a);
    CHECK(q.transpose == 60);                                          /* survives stop */
    seq_set_style(&q, &a, SEQ_ARPEGGIATED); seq_set_style(&q, &a, SEQ_CHORDS);
    CHECK(q.transpose == 60);                                          /* and style changes */
    seq_rec_begin(&q, &a);
    CHECK(q.transpose == 60);
    ron(C4); roff(C4);
    CHECK(q.transpose == 0);                                           /* a new recording resets it */
    seq_rec_end(&q);
    seq_set_transpose(&q, -5);
    seq_clear(&q, &a);
    CHECK(q.transpose == 0 && q.len == 0);                             /* so does clear */
}

static void test_note_value_and_style_changes_take_effect_at_the_next_boundary(void) {
    reset(); record_four();
    seq_start(&q, &a);
    ticks(100);
    seq_set_beats(&q, 1, 1);                                           /* quarters from the next event */
    ticks(150);
    CHECK(on_tick(1) == 250);
    ticks(500);
    CHECK(n_on() == 3 && n_sounding() == 2);                           /* 750: the tied chord still sounds (500 ms steps) */
    ticks(500);
    CHECK(n_on() == 3);                                                /* 1250: the rest */
    ticks(500);
    CHECK(n_on() == 4 && on_note(3) == B4 && on_tick(3) == 1750);
    seq_stop(&q, &a);
    reset(); record_two_chords();
    seq_start(&q, &a);
    ticks(300);                                                        /* C chord at 0, F chord at 250 */
    CHECK(n_on() == 6);
    seq_set_style(&q, &a, SEQ_ARPEGGIATED);                            /* playing: release, restart at event 1 on the next step */
    CHECK(n_sounding() == 0);
    ticks(199);
    CHECK(n_on() == 6);
    ticks(1);
    CHECK(n_on() == 7 && on_note(6) == C4 && on_tick(6) == 500);       /* the C chord, arpeggiated from its first note */
    seq_stop(&q, &a);
    seq_set_style(&q, &a, SEQ_CHORDS);
    CHECK(q.style == SEQ_CHORDS && !q.playing);                        /* stopped: just the setting */
}

/* ---- transport: Arpeggiated ----------------------------------------------------------- */
static void test_arpeggiated_plays_each_chord_for_the_chord_length(void) {
    reset(); record_two_chords();
    seq_set_style(&q, &a, SEQ_ARPEGGIATED);                            /* Whole: 2000 ms = 8 eighths per chord */
    seq_start(&q, &a);
    ticks(3999);
    CHECK(strcmp(ons(), "60 64 67 60 64 67 60 64 65 69 72 65 69 72 65 69 ") == 0);
    CHECK(on_tick(8) == 2000 && n_sounding() == 0);                   /* the last note's gate has closed */
    ticks(1);
    CHECK(n_on() == 17 && on_note(16) == C4 && on_tick(16) == 4000);  /* round again */
    seq_stop(&q, &a);
    CHECK(n_sounding() == 0 && !q.playing);
    arp_set_mode(&a, ARP_DOWN);                                        /* direction inside the chord, chords in order */
    play(); ticks(3999);
    CHECK(strcmp(ons(), "67 64 60 67 64 60 67 64 72 69 65 72 69 65 72 69 ") == 0);
    seq_stop(&q, &a);
    arp_set_mode(&a, ARP_ASSIGN);                                      /* the order recorded: E C G */
    play(); ticks(749);
    CHECK(strcmp(ons(), "64 60 67 ") == 0);
    seq_stop(&q, &a);
    arp_set_mode(&a, ARP_UP); arp_set_octaves(&a, 2);                  /* octave passes inside the chord */
    play(); ticks(999);
    CHECK(strcmp(ons(), "60 64 67 72 ") == 0);
    seq_stop(&q, &a);
    arp_set_octaves(&a, 1);
    seq_set_chord_beats(&q, 1);                                        /* Qtr: two eighths per chord */
    play(); ticks(999);
    CHECK(strcmp(ons(), "60 64 65 69 ") == 0);
    seq_stop(&q, &a);
}

static void test_arpeggiated_ties_rests_velocity_and_transposition(void) {
    reset();
    seq_rec_begin(&q, &a);
    seq_rec_note(&q, &a, LOCAL, C4, 90); seq_rec_note(&q, &a, LOCAL, E4, 50); seq_rec_rest_tie(&q); roff(C4); roff(E4);   /* chord, two chord lengths */
    seq_rec_rest_tie(&q);                                              /* a rest */
    ron(G4); roff(G4);
    seq_rec_end(&q); clear_log();
    seq_set_style(&q, &a, SEQ_ARPEGGIATED); seq_set_chord_beats(&q, 1);   /* Qtr: 500 ms */
    seq_start(&q, &a);
    ticks(2200);                                                       /* C E C E | rest | G G | C ... */
    CHECK(strcmp(ons(), "60 64 60 64 67 67 60 ") == 0);
    CHECK(on_tick(3) == 750 && on_tick(4) == 1500 && on_tick(6) == 2000);
    CHECK(on_at(0)->vel == 90 && on_at(1)->vel == 50);
    seq_set_transpose(&q, 3);                                          /* from the next chord boundary */
    ticks(300);
    CHECK(on_note(7) == E4 && on_tick(7) == 2250);                     /* still the current chord */
    ticks(500);
    CHECK(on_note(8) == 60 && on_tick(8) == 2500);                     /* 2500: second step of the tied chord, untransposed ... */
    ticks(500);
    CHECK(on_note(10) == G4 + 3 && on_tick(10) == 3500);               /* ... 3500: G + 3 after the rest */
}

static void test_arpeggiated_uneven_rate_is_cut_at_the_boundary(void) {
    reset(); record_two_chords();
    seq_set_style(&q, &a, SEQ_ARPEGGIATED); seq_set_chord_beats(&q, 1);   /* Qtr: 500 ms */
    seq_set_beats(&q, 3, 4);                                           /* dotted eighth: 375 ms, does not divide it */
    seq_start(&q, &a);
    ticks(499);
    CHECK(n_on() == 2 && on_tick(1) == 375 && n_sounding() == 1);
    ticks(1);
    CHECK(n_on() == 3 && on_note(2) == F4 && on_tick(2) == 500 && off_at(E4, 500));   /* cut, next chord on time */
    seq_stop(&q, &a);
    seq_set_beats(&q, 1, 3);                                           /* eighth triplets: 3 per beat, they fit */
    play(); ticks(499);
    CHECK(n_on() == 3 && on_tick(2) == 334);                           /* 167 + 167 with the remainder carried */
    ticks(1);
    CHECK(n_on() == 4 && on_note(3) == F4 && on_tick(3) == 500 && !off_at(G4, 500));   /* nothing cut: the G ended at its gate */
}

/* ---- MIDI clock ----------------------------------------------------------------------- */
static void test_midi_clock_arms_steps_stops_continues_and_disarms(void) {
    reset(); record_four();
    arp_set_ext(&a, 1);
    CHECK(seq_start(&q, &a) == 1 && q.armed && !q.playing);           /* armed: nothing until the clocks */
    ticks(3000);
    CHECK(n_on() == 0);
    rt(0xFA);
    clocks(1);                                                         /* clock 0: event 1 */
    CHECK(n_on() == 1 && on_note(0) == C4 && q.playing);
    clocks(6);
    CHECK(n_sounding() == 0);                                          /* gate at clock 6 */
    clocks(6);                                                         /* clock 12: the tied chord */
    CHECK(n_on() == 3 && sounding[E4]);
    clocks(12);                                                        /* clock 24: no retrigger */
    CHECK(n_on() == 3 && sounding[E4]);
    clocks(6);                                                         /* clock 30: released, half-way through its last step */
    CHECK(n_sounding() == 0);
    rt(0xFC);                                                          /* MIDI Stop while the rest is due: pause */
    clocks(50);
    CHECK(n_on() == 3);
    rt(0xFB);                                                          /* Continue: the rest at 36 from where we were (clock 30 + 6) */
    clocks(6);
    CHECK(n_on() == 3);
    clocks(12);                                                        /* then B at the next step */
    CHECK(n_on() == 4 && on_note(3) == B4);
    rt(0xFA);                                                          /* Start: event 1 on the next clock */
    clocks(1);
    CHECK(n_on() == 5 && on_note(4) == C4);
    seq_stop(&q, &a);                                                  /* panel stop: disarmed */
    CHECK(!q.playing && !q.armed && n_sounding() == 0);
    rt(0xFA); clocks(30); rt(0xFB); clocks(30);
    CHECK(n_on() == 5);                                                /* MIDI transport cannot restart it */
    seq_start(&q, &a);                                                 /* armed again */
    rt(0xFA); clocks(1);
    CHECK(n_on() == 6 && on_note(5) == C4);
}

static void test_midi_continue_resumes_the_remaining_duration(void) {
    reset(); record_four();
    arp_set_ext(&a, 1);
    seq_start(&q, &a); rt(0xFA);
    clocks(13);                                                        /* clock 12: the tied chord (24 clocks) begins */
    CHECK(n_on() == 3 && sounding[E4]);
    rt(0xFC);                                                          /* Stop 1 clock into it */
    CHECK(n_sounding() == 0);
    clocks(100);
    rt(0xFB);
    clocks(12);                                                        /* 12 clocks of the chord done: no retrigger ... */
    CHECK(n_on() == 3 && n_sounding() == 0);
    clocks(12);                                                        /* ... 24 done: the rest */
    CHECK(n_on() == 3);
    clocks(12);
    CHECK(n_on() == 4 && on_note(3) == B4);                            /* then B: the remaining duration was kept */
}

static void test_midi_clock_arpeggiated_chord_boundaries_on_the_grid(void) {
    reset(); record_two_chords();
    seq_set_style(&q, &a, SEQ_ARPEGGIATED);
    arp_set_ext(&a, 1);
    seq_start(&q, &a); rt(0xFA);
    clocks(1);
    CHECK(n_on() == 1 && on_note(0) == C4);
    clocks(95);
    CHECK(n_on() == 8 && on_note(7) == E4);
    clocks(1);                                                         /* clock 96: the F chord */
    CHECK(n_on() == 9 && on_note(8) == F4);
    clocks(96);
    CHECK(n_on() == 17 && on_note(16) == C4);
}

static void test_clock_loss_releases_and_recovery_restarts_an_armed_sequence(void) {
    reset(); record_four();
    arp_set_ext(&a, 1);
    seq_start(&q, &a); rt(0xFA);
    clocks(13);
    CHECK(sounding[E4]);
    ticks(1000);                                                       /* 1 s without a clock */
    CHECK(n_sounding() == 0 && q.armed);
    clocks(1);                                                         /* clocks return: from event 1 */
    CHECK(on_note(n_on() - 1) == C4);
}

/* ---- review findings 2026-10-08 (regressions) ------------------------------------------- */
static void test_arming_under_midi_clock_waits_for_the_grids_next_step(void) {
    reset(); record_four();
    arp_set_ext(&a, 1);
    rt(0xFA);
    clocks(5);                                                         /* the DAW runs: clocks 0..4 */
    seq_start(&q, &a);                                                 /* armed mid-grid */
    clocks(7);                                                         /* clocks 5..11: not a boundary */
    CHECK(n_on() == 0 && q.armed && !q.playing);
    clocks(1);                                                         /* clock 12: the next eighth */
    CHECK(n_on() == 1 && on_note(0) == C4 && q.playing);
    clocks(12);
    CHECK(n_on() == 3 && sounding[E4]);                                /* clock 24: the chord, on the grid */
    seq_stop(&q, &a);
    clocks(5);                                                         /* clocks 37..41 while stopped */
    seq_start(&q, &a);
    clocks(6);                                                         /* 42..47 */
    CHECK(n_on() == 3);
    clocks(1);                                                         /* 48 */
    CHECK(n_on() == 4 && on_note(3) == C4);
}

static void test_midi_continue_resumes_the_arpeggio_where_it_stopped(void) {
    reset(); record_two_chords();
    seq_set_style(&q, &a, SEQ_ARPEGGIATED);
    arp_set_ext(&a, 1);
    seq_start(&q, &a); rt(0xFA);
    clocks(25);                                                        /* clock 0 C, 12 E, 24 G */
    CHECK(n_on() == 3 && on_note(2) == 67);
    rt(0xFC);                                                          /* Stop on the G */
    CHECK(n_sounding() == 0);
    clocks(40);
    rt(0xFB);                                                          /* Continue: nothing retriggers ... */
    CHECK(n_on() == 3 && n_sounding() == 0);
    clocks(11);                                                        /* ... the step in progress runs out (11 clocks were left) */
    CHECK(n_on() == 3);
    clocks(1);
    CHECK(n_on() == 4 && on_note(3) == C4);                            /* then the pattern goes on: C after G */
    clocks(59);
    CHECK(n_on() == 8);                                                /* E G C E */
    clocks(1);                                                         /* 96 counted clocks since the chord began */
    CHECK(n_on() == 9 && on_note(8) == F4);                            /* the next chord, its remaining duration kept */
}

static void test_a_note_value_queued_while_playing_does_not_outlive_a_stop(void) {
    reset(); record_four();
    seq_start(&q, &a); ticks(100);
    seq_set_beats(&q, 1, 1);                                           /* quarters, pending */
    seq_set_swing(&q, 1);
    CHECK(q.beats_den == 2 && !q.swing);                               /* still 8ths until the next event */
    seq_stop(&q, &a);
    CHECK(q.beats_num == 1 && q.beats_den == 1 && q.swing);            /* the stop resolves what was queued */
    seq_set_beats(&q, 1, 4);                                           /* sixteenths, chosen while stopped */
    seq_set_swing(&q, 0);
    play(); ticks(125);
    CHECK(q.beats_den == 4 && !q.swing);                               /* the newer choice stands */
    CHECK(n_on() == 3 && on_tick(1) == 125);                           /* 16ths: the chord at 125 ms */
}

static void test_arpeggiated_chord_boundaries_carry_the_remainder(void) {
    reset(); record_two_chords();
    seq_set_style(&q, &a, SEQ_ARPEGGIATED); seq_set_chord_beats(&q, 1);   /* a beat per chord */
    seq_set_beats(&q, 4, 1);                                           /* one arp note per chord */
    arp_set_bpm(&a, 137);                                              /* 437.96 ms per beat: a remainder every time */
    play(); ticks(60000);                                              /* a minute = 137 beats exactly */
    CHECK(n_on() == 138 && on_tick(137) == 60000);                     /* the 137th boundary on the minute, not 6 ms late */
    CHECK(on_tick(1) == 438 && on_tick(2) == 876);                     /* 437.96 rounded up, then carried */
}

/* ---- clear, all notes off, record mode and transport ---------------------------------- */
static void test_recording_stops_playback_and_ends_stopped(void) {
    reset(); record_four();
    seq_start(&q, &a); ticks(300);
    CHECK(q.playing && n_sounding() == 2);
    seq_rec_begin(&q, &a);
    CHECK(!q.playing && n_sounding() == 0 && q.rec);                   /* generated notes released */
    ron(C4); roff(C4);
    seq_rec_end(&q);
    CHECK(!q.playing && !q.rec && q.len == 1);                         /* stopped: the next start plays it */
    seq_start(&q, &a);
    CHECK(q.playing && on_note(n_on() - 1) == C4);
}

static void test_clear_and_all_notes_off(void) {
    reset(); record_four();
    seq_set_transpose(&q, 4);
    seq_start(&q, &a); ticks(300);
    seq_all_notes_off(&q, &a);
    CHECK(!q.playing && n_sounding() == 0 && q.len == 4);              /* stopped, recording kept */
    seq_start(&q, &a); ticks(10);
    seq_clear(&q, &a);
    CHECK(!q.playing && q.len == 0 && q.total == 0 && q.transpose == 0 && n_sounding() == 0);
    CHECK(seq_start(&q, &a) == 0 && !q.playing);                       /* nothing to play */
    seq_rec_begin(&q, &a); ron(C4);
    seq_all_notes_off(&q, &a);
    CHECK(q.rec && q.len == 1);                                        /* recording kept too */
    roff(C4); seq_rec_end(&q);
}

int main(void) {
    test_defaults_and_size();
    test_recording_counts_timing_steps();
    test_capacity_is_512_timing_steps_in_total();
    test_back_undoes_ties_then_events_and_closes_the_chord();
    test_old_sequence_survives_until_the_first_entry_and_back_respects_that();
    test_chords_play_forward_with_gate_extensions_and_loop();
    test_orders_forward_back_pendulum();
    test_transposition_from_the_next_event_without_disturbing_timing();
    test_note_value_and_style_changes_take_effect_at_the_next_boundary();
    test_arpeggiated_plays_each_chord_for_the_chord_length();
    test_arpeggiated_ties_rests_velocity_and_transposition();
    test_arpeggiated_uneven_rate_is_cut_at_the_boundary();
    test_midi_clock_arms_steps_stops_continues_and_disarms();
    test_midi_continue_resumes_the_remaining_duration();
    test_midi_clock_arpeggiated_chord_boundaries_on_the_grid();
    test_clock_loss_releases_and_recovery_restarts_an_armed_sequence();
    test_arming_under_midi_clock_waits_for_the_grids_next_step();
    test_midi_continue_resumes_the_arpeggio_where_it_stopped();
    test_a_note_value_queued_while_playing_does_not_outlive_a_stop();
    test_arpeggiated_chord_boundaries_carry_the_remainder();
    test_recording_stops_playback_and_ends_stopped();
    test_clear_and_all_notes_off();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
