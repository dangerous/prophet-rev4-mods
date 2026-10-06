/* Host harness for the seq logic (docs/SPEC.md: "Seq"). A fake platform records every call
 * the logic makes into V5/stock so tests can assert exact behaviour. `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "seq.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake platform ------------------------------------------------------------------ */
enum { EV_NOTE, EV_CLEAR, EV_BUTTON, EV_DISPLAY, EV_OUT, EV_HOLD };
typedef struct { int type, a, b, c, d; } ev_t;
static ev_t log_[4096];
static int nlog;
static int fake_arp_enabled = 1, fake_v5_octaves = 1, fake_globals;

static void push(int t, int a, int b, int c, int d) {
    if (nlog < (int)(sizeof log_ / sizeof *log_)) log_[nlog++] = (ev_t){t, a, b, c, d};
}
int  plat_arp_enabled(void) { return fake_arp_enabled; }
void plat_v5_note(int src, int note, int vel) { push(EV_NOTE, src, note, vel, 0); }
void plat_v5_clear(void) { push(EV_CLEAR, 0, 0, 0, 0); }
void plat_v5_hold(int on) { push(EV_HOLD, on, 0, 0, 0); }
void plat_v5_button(int id, int value) { push(EV_BUTTON, id, value, 0, 0); }
int  plat_v5_octaves(void) { return fake_v5_octaves; }
int  plat_globals_active(void) { return fake_globals; }
void plat_display_int(int v) { push(EV_DISPLAY, v, 0, 0, 0); }
void plat_orig_out(int ctx, int src, int on, int note, int vel) { push(EV_OUT, src, on, note, vel); (void)ctx; }

static int count_type(int t) { int n = 0; for (int i = 0; i < nlog; i++) n += log_[i].type == t; return n; }
static int count_notes(int vel_nonzero) {
    int n = 0;
    for (int i = 0; i < nlog; i++)
        if (log_[i].type == EV_NOTE && ((log_[i].c != 0) == vel_nonzero)) n++;
    return n;
}
static ev_t *last_of(int t) { for (int i = nlog - 1; i >= 0; i--) if (log_[i].type == t) return &log_[i]; return 0; }
static int has_note(int note, int vel) {
    for (int i = 0; i < nlog; i++) if (log_[i].type == EV_NOTE && log_[i].b == note && log_[i].c == vel) return 1;
    return 0;
}
static int has_button(int id, int value) {
    for (int i = 0; i < nlog; i++) if (log_[i].type == EV_BUTTON && log_[i].a == id && log_[i].b == value) return 1;
    return 0;
}
static int has_out(int on, int note) {
    for (int i = 0; i < nlog; i++) if (log_[i].type == EV_OUT && log_[i].b == on && log_[i].c == note) return 1;
    return 0;
}
static void clear_log(void) { nlog = 0; }

/* ---- helpers ------------------------------------------------------------------------- */
enum { LOCAL = 1, MIDI = 2, A440 = 0x0F, PROGRAM1 = 0, PROGRAM2 = 1, PROGRAM6 = 5, PRESS = 1, RELEASE = 2 };
static seq_t s;

static void reset(void) {
    seq_init(&s);
    fake_arp_enabled = 1; fake_v5_octaves = 1; fake_globals = 0;
    clear_log();
}
static void record_ceg(void) {              /* A440 held: C E G (velocities 100, 90, 80) */
    CHECK(seq_button(&s, A440, PRESS) == 0);
    CHECK(seq_note(&s, LOCAL, 60, 100) == 0);
    CHECK(seq_note(&s, LOCAL, 60, 0) == 0);
    CHECK(seq_note(&s, LOCAL, 64, 90) == 0);
    CHECK(seq_note(&s, LOCAL, 64, 0) == 0);
    CHECK(seq_note(&s, LOCAL, 67, 80) == 0);
    CHECK(seq_note(&s, LOCAL, 67, 0) == 0);
    CHECK(seq_button(&s, A440, RELEASE) == 0);
}
static void drain(int ticks) { while (ticks-- > 0) seq_tick(&s); }

/* ---- tests ---------------------------------------------------------------------------- */
static void test_recording_enters_seq_mode_and_marks_a440_used(void) {
    reset();
    fake_v5_octaves = 3;
    record_ceg();
    CHECK(s.active == 1 && s.count == 3 && s.root == 60);
    CHECK(s.octaves == 3 && s.saved_v5_octaves == 3);          /* setting in force applies */
    /* first recorded key: V5 is told "Program 1" (octave 1 + marks A440 as used) */
    CHECK(has_button(PROGRAM1, PRESS) && has_button(PROGRAM1, RELEASE));
    /* step count displayed after each step */
    CHECK(count_type(EV_DISPLAY) == 3 && last_of(EV_DISPLAY)->a == 3);
    /* leaving recording clears whatever the arp held from the audible recording */
    CHECK(count_type(EV_CLEAR) == 1);
    /* the recorded notes were forwarded normally (return 0 asserted in record_ceg) */
}

static void test_a440_tap_without_keys_is_untouched(void) {
    reset();
    CHECK(seq_button(&s, A440, PRESS) == 0);
    CHECK(seq_button(&s, A440, RELEASE) == 0);
    CHECK(s.active == 0 && count_type(EV_BUTTON) == 0 && count_type(EV_CLEAR) == 0);
    /* buttons with A440 held but no sequence: pass through */
    seq_button(&s, A440, PRESS);
    CHECK(seq_button(&s, PROGRAM2, PRESS) == 0);
    CHECK(seq_button(&s, PROGRAM6, PRESS) == 0);
    seq_button(&s, A440, RELEASE);
}

static void test_recording_caps_at_32_and_ignores_releases(void) {
    reset();
    seq_button(&s, A440, PRESS);
    for (int i = 0; i < 40; i++) { seq_note(&s, LOCAL, 40 + i, 100); seq_note(&s, LOCAL, 40 + i, 0); }
    seq_button(&s, A440, RELEASE);
    CHECK(s.count == 32 && last_of(EV_DISPLAY)->a == 32 && count_type(EV_DISPLAY) == 32);
}

static void test_playback_feeds_dummies_and_substitutes_pitches(void) {
    reset();
    record_ceg();
    clear_log();
    CHECK(seq_note(&s, LOCAL, 62, 100) == 1);                 /* trigger D: transpose +2 */
    CHECK(count_type(EV_NOTE) == 0);                          /* nothing until the tick */
    drain(1);
    CHECK(has_note(0, 100) && has_note(1, 90) && has_note(2, 80) && count_notes(1) == 3);
    /* V5 steps through the dummies; the wrapper substitutes transposed pitches */
    seq_output(&s, 0, 0, 1, 0, 100);
    seq_output(&s, 0, 0, 0, 0, 0);
    seq_output(&s, 0, 0, 1, 1, 90);
    seq_output(&s, 0, 0, 0, 1, 0);
    seq_output(&s, 0, 0, 1, 2, 80);
    CHECK(has_out(1, 62) && has_out(0, 62) && has_out(1, 66) && has_out(0, 66) && has_out(1, 69));
    CHECK(!has_out(1, 0) && !has_out(1, 1) && !has_out(1, 2));
    /* velocities are the recorded ones */
    ev_t *e = last_of(EV_OUT);
    CHECK(e && e->d == 80);
}

static void test_transposition_follows_most_recent_key(void) {
    reset();
    record_ceg();
    seq_note(&s, LOCAL, 62, 100);
    drain(1);
    seq_note(&s, LOCAL, 64, 100);                             /* second key: transpose +4 */
    clear_log();
    seq_output(&s, 0, 0, 1, 0, 100);
    CHECK(has_out(1, 64));
    seq_note(&s, LOCAL, 64, 0);                               /* release the trigger, D still down */
    clear_log();
    seq_output(&s, 0, 0, 1, 1, 90);
    CHECK(has_out(1, 68));                                    /* transpose stays +4 */
    CHECK(count_type(EV_CLEAR) == 0);                         /* no restart while a key is down */
}

static void test_release_all_without_hold_stops(void) {
    reset();
    record_ceg();
    seq_note(&s, LOCAL, 62, 100);
    drain(1);
    clear_log();
    CHECK(seq_note(&s, LOCAL, 62, 0) == 1);
    drain(1);
    CHECK(has_note(0, 0) && has_note(1, 0) && has_note(2, 0) && count_notes(0) == 3);
    CHECK(s.playing == 0);
    drain(2);
    CHECK(count_type(EV_NOTE) == 3);                          /* nothing more */
}

static void test_hold_latches_and_first_key_after_all_up_restarts(void) {
    reset();
    record_ceg();
    seq_hold(&s, 1);
    seq_note(&s, LOCAL, 62, 100);
    drain(1);
    clear_log();
    seq_note(&s, LOCAL, 62, 0);
    drain(2);
    CHECK(count_type(EV_NOTE) == 0 && s.playing == 1);        /* latched: keeps playing */
    seq_note(&s, LOCAL, 65, 100);                             /* new chord: restart, transpose +5 */
    CHECK(count_type(EV_CLEAR) == 1);
    drain(1);
    CHECK(count_notes(1) == 3);                               /* re-fed after the clear */
    clear_log();
    seq_output(&s, 0, 0, 1, 0, 100);
    CHECK(has_out(1, 65));
}

static void test_hold_off_with_no_keys_stops(void) {
    reset();
    record_ceg();
    seq_hold(&s, 1);
    seq_note(&s, LOCAL, 62, 100);
    drain(1);
    seq_note(&s, LOCAL, 62, 0);
    clear_log();
    seq_hold(&s, 0);
    drain(1);
    CHECK(count_notes(0) == 3 && s.playing == 0);
}

static void test_octave_expansion_whole_sequence_per_octave(void) {
    reset();
    fake_v5_octaves = 2;
    record_ceg();                                             /* octaves = 2 -> 6 dummies */
    seq_note(&s, LOCAL, 60, 100);                             /* transpose 0 */
    drain(1);
    CHECK(count_notes(1) == 6 && has_note(3, 100) && has_note(5, 80));
    clear_log();
    seq_output(&s, 0, 0, 1, 4, 90);                           /* dummy 4 = step 1, octave 1 */
    CHECK(has_out(1, 64 + 12));
    seq_output(&s, 0, 0, 1, 2, 80);
    CHECK(has_out(1, 67));
}

static void test_octave_change_while_playing_is_consumed_and_applied(void) {
    reset();
    record_ceg();
    seq_note(&s, LOCAL, 60, 100);
    drain(1);
    clear_log();
    seq_button(&s, A440, PRESS);
    CHECK(seq_button(&s, PROGRAM2, PRESS) == 1);              /* consumed: V5 stays at o 1 */
    CHECK(seq_button(&s, PROGRAM2, RELEASE) == 1);
    CHECK(last_of(EV_DISPLAY) && last_of(EV_DISPLAY)->a == 2);
    drain(1);
    CHECK(has_note(3, 100) && has_note(4, 90) && has_note(5, 80) && count_notes(1) == 3);
    clear_log();
    CHECK(seq_button(&s, PROGRAM1, PRESS) == 1);
    drain(1);
    CHECK(has_note(3, 0) && has_note(4, 0) && has_note(5, 0) && count_notes(0) == 3);
    seq_button(&s, A440, RELEASE);
    CHECK(s.active == 1);                                     /* A440 release after buttons: no change */
}

static void test_program6_clears_and_restores_octaves(void) {
    reset();
    fake_v5_octaves = 3;
    record_ceg();
    seq_note(&s, LOCAL, 62, 100);
    drain(1);
    clear_log();
    seq_button(&s, A440, PRESS);
    CHECK(seq_button(&s, PROGRAM6, PRESS) == 0);              /* forwarded: V5 marks A440 used */
    CHECK(s.active == 0 && s.count == 0 && s.playing == 0);
    CHECK(count_type(EV_CLEAR) == 1);
    CHECK(has_button(PROGRAM1 + 2, PRESS) && has_button(PROGRAM1 + 2, RELEASE));  /* back to o 3 */
    CHECK(seq_button(&s, A440, RELEASE) == 0);
    drain(2);
    CHECK(count_type(EV_NOTE) == 0);                          /* V5 was cleared; nothing to release */
    CHECK(seq_note(&s, LOCAL, 64, 100) == 0);                 /* normal mode again */
}

static void test_arp_disabled_in_seq_mode_passes_notes_through(void) {
    reset();
    record_ceg();
    fake_arp_enabled = 0;
    clear_log();
    CHECK(seq_note(&s, LOCAL, 62, 100) == 0);
    drain(1);
    CHECK(count_type(EV_NOTE) == 0 && s.playing == 0);
    CHECK(seq_note(&s, LOCAL, 62, 0) == 0);
    /* arp switched off while dummies were fed: wrapper clears V5 and forgets them */
    fake_arp_enabled = 1;
    seq_note(&s, LOCAL, 62, 100);
    drain(1);
    CHECK(count_notes(1) == 3);
    fake_arp_enabled = 0;
    clear_log();
    drain(1);
    CHECK(count_type(EV_CLEAR) == 1 && s.playing == 0);
    drain(1);
    CHECK(count_type(EV_CLEAR) == 1);                         /* only once */
}

static void test_output_passes_real_notes_unchanged(void) {
    reset();
    record_ceg();
    clear_log();
    seq_output(&s, 0, 1, 1, 60, 100);                         /* no dummies fed: real note */
    seq_output(&s, 0, 1, 0, 60, 0);
    CHECK(has_out(1, 60) && has_out(0, 60) && count_type(EV_OUT) == 2);
    reset();
    seq_output(&s, 0, 2, 1, 1, 100);                          /* not even in seq mode */
    CHECK(has_out(1, 1));
}

static void test_feed_is_budgeted_per_tick(void) {
    reset();
    fake_v5_octaves = 4;
    seq_button(&s, A440, PRESS);
    for (int i = 0; i < 32; i++) { seq_note(&s, LOCAL, 36 + i, 100); seq_note(&s, LOCAL, 36 + i, 0); }
    seq_button(&s, A440, RELEASE);                            /* 32 steps x 4 octaves = 128 dummies */
    seq_note(&s, LOCAL, 36, 100);
    clear_log();
    drain(1);
    CHECK(count_type(EV_NOTE) == SEQ_FEED_BUDGET);
    drain(7);
    CHECK(count_notes(1) == 128);
    seq_output(&s, 0, 0, 1, 127, 100);                        /* dummy 127 = step 31, octave 3 */
    CHECK(has_out(1, 36 + 31 + 36));
}

static void test_cc123_forgets_dummies_and_next_key_restarts(void) {
    reset();
    record_ceg();
    seq_hold(&s, 1);
    seq_note(&s, LOCAL, 62, 100);
    drain(1);
    seq_note(&s, LOCAL, 62, 0);
    seq_all_notes_off(&s);
    clear_log();
    drain(2);
    CHECK(count_type(EV_NOTE) == 0 && s.playing == 0);
    seq_note(&s, LOCAL, 62, 100);
    CHECK(count_type(EV_CLEAR) == 0);                         /* nothing latched, no clear needed */
    drain(1);
    CHECK(count_notes(1) == 3);
}

static void test_midi_notes_record_and_trigger(void) {
    reset();
    seq_button(&s, A440, PRESS);
    CHECK(seq_note(&s, MIDI, 48, 70) == 0);
    seq_note(&s, MIDI, 48, 0);
    seq_button(&s, A440, RELEASE);
    CHECK(s.active && s.root == 48);
    clear_log();
    CHECK(seq_note(&s, MIDI, 55, 100) == 1);
    drain(1);
    CHECK(has_note(0, 70));
    seq_output(&s, 0, 0, 1, 0, 70);
    CHECK(has_out(1, 55));
}

static void test_out_of_range_pitch_is_silent(void) {
    reset();
    seq_button(&s, A440, PRESS);
    seq_note(&s, LOCAL, 60, 100); seq_note(&s, LOCAL, 60, 0);
    seq_note(&s, LOCAL, 120, 100); seq_note(&s, LOCAL, 120, 0);
    seq_button(&s, A440, RELEASE);
    seq_note(&s, LOCAL, 72, 100);                             /* transpose +12 -> 132 for step 2 */
    drain(1);
    clear_log();
    seq_output(&s, 0, 0, 1, 1, 100);
    seq_output(&s, 0, 0, 0, 1, 0);
    CHECK(count_type(EV_OUT) == 0);                           /* neither on nor off reaches the voices */
    seq_output(&s, 0, 0, 1, 0, 100);
    CHECK(has_out(1, 72));
}

static void test_releases_of_recorded_keys_are_forwarded_after_recording(void) {
    reset();
    seq_button(&s, A440, PRESS);
    seq_note(&s, LOCAL, 60, 100);                             /* held while A440 is released */
    seq_button(&s, A440, RELEASE);
    CHECK(s.active == 1);
    CHECK(seq_note(&s, LOCAL, 60, 0) == 0);                   /* owed release goes to V5 */
    CHECK(seq_note(&s, LOCAL, 60, 100) == 1);                 /* now a trigger */
}

static void test_new_recording_replaces_sequence_and_stops_playback(void) {
    reset();
    record_ceg();
    seq_hold(&s, 1);
    seq_note(&s, LOCAL, 62, 100);
    drain(1);
    seq_note(&s, LOCAL, 62, 0);
    clear_log();
    seq_button(&s, A440, PRESS);
    CHECK(seq_note(&s, LOCAL, 50, 100) == 0);                 /* recording restarts, audible */
    drain(1);
    CHECK(count_notes(0) == 3 && s.playing == 0);             /* old dummies released */
    seq_note(&s, LOCAL, 50, 0);
    seq_button(&s, A440, RELEASE);
    CHECK(s.count == 1 && s.root == 50);
}

static void test_globals_active_suppresses_synthetic_buttons(void) {
    reset();
    fake_globals = 1;
    record_ceg();
    CHECK(count_type(EV_BUTTON) == 0);
    CHECK(s.active == 1);
}

static void test_stale_dummy_events_while_stopping_are_silent(void) {
    reset();
    record_ceg();
    seq_note(&s, LOCAL, 62, 100);
    drain(1);                                                 /* dummies fed */
    seq_note(&s, LOCAL, 62, 0);                               /* stop requested, offs not yet sent */
    clear_log();
    seq_output(&s, 0, 0, 1, 1, 90);                           /* V5 steps once more */
    CHECK(count_type(EV_OUT) == 0);                           /* swallowed, not substituted */
    seq_output(&s, 0, 0, 0, 1, 0);
    CHECK(count_type(EV_OUT) == 0);                           /* matching off swallowed too */
    drain(1);
    CHECK(count_notes(0) == 3);                               /* dummies released as normal */
}

static void test_fed_dummy_with_no_steps_cannot_hang(void) {
    reset();
    record_ceg();
    s.count = 0;                                              /* defensive: unreachable state */
    s.fed[0] = 1u << 5;
    s.playing = 1;
    seq_output(&s, 0, 0, 1, 5, 100);                          /* must return promptly */
    seq_output(&s, 0, 0, 0, 5, 0);
    CHECK(count_type(EV_OUT) == 0);
}

static int hold_reasserted_after_clear(void) {
    /* exactly one EV_CLEAR, followed (later in the log) by exactly one EV_HOLD(1) */
    int clears = 0, holds = 0, clear_at = -1, hold_at = -1;
    for (int i = 0; i < nlog; i++) {
        if (log_[i].type == EV_CLEAR) { clears++; clear_at = i; }
        if (log_[i].type == EV_HOLD) { holds++; hold_at = i; if (log_[i].a != 1) return 0; }
    }
    return clears == 1 && holds == 1 && hold_at > clear_at;
}

static void test_clears_reassert_hold_to_the_arp(void) {
    /* the arp's clear event also resets its own hold flag (V5 CC123 semantics) */
    reset();
    record_ceg();
    seq_hold(&s, 1);
    seq_note(&s, LOCAL, 62, 100); drain(1); seq_note(&s, LOCAL, 62, 0);
    clear_log();
    seq_note(&s, LOCAL, 65, 100);                             /* restart under hold */
    CHECK(hold_reasserted_after_clear());

    clear_log();
    seq_button(&s, A440, PRESS);
    seq_button(&s, PROGRAM6, PRESS);                          /* clear via Program 6 under hold */
    CHECK(hold_reasserted_after_clear());
    seq_button(&s, A440, RELEASE);

    clear_log();
    seq_button(&s, A440, PRESS);
    seq_note(&s, LOCAL, 60, 100); seq_note(&s, LOCAL, 60, 0);
    seq_button(&s, A440, RELEASE);                            /* end of recording under hold */
    CHECK(hold_reasserted_after_clear());
}

static void test_clears_do_not_touch_hold_when_inactive(void) {
    reset();
    record_ceg();                                             /* hold off: clear at end of recording */
    CHECK(count_type(EV_CLEAR) == 1 && count_type(EV_HOLD) == 0);
    seq_button(&s, A440, PRESS);
    seq_button(&s, PROGRAM6, PRESS);
    seq_button(&s, A440, RELEASE);
    CHECK(count_type(EV_CLEAR) == 2 && count_type(EV_HOLD) == 0);
}

static void test_state_fits_and_is_zero_initialised(void) {
    seq_t z;
    memset(&z, 0, sizeof z);
    seq_init(&s);
    CHECK(memcmp(&z, &s, sizeof s) == 0);
    CHECK(sizeof(seq_t) <= 0x140);
}

int main(void) {
    test_recording_enters_seq_mode_and_marks_a440_used();
    test_a440_tap_without_keys_is_untouched();
    test_recording_caps_at_32_and_ignores_releases();
    test_playback_feeds_dummies_and_substitutes_pitches();
    test_transposition_follows_most_recent_key();
    test_release_all_without_hold_stops();
    test_hold_latches_and_first_key_after_all_up_restarts();
    test_hold_off_with_no_keys_stops();
    test_octave_expansion_whole_sequence_per_octave();
    test_octave_change_while_playing_is_consumed_and_applied();
    test_program6_clears_and_restores_octaves();
    test_arp_disabled_in_seq_mode_passes_notes_through();
    test_output_passes_real_notes_unchanged();
    test_feed_is_budgeted_per_tick();
    test_cc123_forgets_dummies_and_next_key_restarts();
    test_midi_notes_record_and_trigger();
    test_out_of_range_pitch_is_silent();
    test_releases_of_recorded_keys_are_forwarded_after_recording();
    test_new_recording_replaces_sequence_and_stops_playback();
    test_globals_active_suppresses_synthetic_buttons();
    test_stale_dummy_events_while_stopping_are_silent();
    test_fed_dummy_with_no_steps_cannot_hang();
    test_clears_reassert_hold_to_the_arp();
    test_clears_do_not_touch_hold_when_inactive();
    test_state_fits_and_is_zero_initialised();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
