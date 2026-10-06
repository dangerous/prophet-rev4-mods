/* Host harness for the re-latch logic (docs/SPEC.md: "Re-latch under HOLD").
 * Drives relatch_note()/relatch_hold() with key, MIDI and hold events and checks when a
 * clear is requested. Build and run with `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "relatch.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { LOCAL = 1, MIDI = 2 };

static relatch_t s;
static int arp_on;

static int press(int src, int note)   { return relatch_note(&s, arp_on, src, note, 100); }
static int release(int src, int note) { return relatch_note(&s, arp_on, src, note, 0); }

static void reset(int hold, int arp) {
    relatch_init(&s);
    arp_on = arp;
    relatch_hold(&s, hold);
}

static void test_relatch_on_first_key_after_all_released(void) {
    reset(1, 1);
    CHECK(press(LOCAL, 60) == RELATCH_FORWARD);   /* C: nothing latched yet */
    CHECK(press(LOCAL, 64) == RELATCH_FORWARD);
    CHECK(press(LOCAL, 67) == RELATCH_FORWARD);
    CHECK(release(LOCAL, 60) == RELATCH_FORWARD);
    CHECK(release(LOCAL, 64) == RELATCH_FORWARD);
    CHECK(release(LOCAL, 67) == RELATCH_FORWARD);
    CHECK(press(LOCAL, 62) == RELATCH_CLEAR_THEN_FORWARD);  /* D starts a fresh chord */
    CHECK(press(LOCAL, 65) == RELATCH_FORWARD);
    CHECK(press(LOCAL, 69) == RELATCH_FORWARD);
}

static void test_no_clear_while_a_key_is_still_down(void) {
    reset(1, 1);
    press(LOCAL, 60); press(LOCAL, 64); press(LOCAL, 67);
    release(LOCAL, 60); release(LOCAL, 64);          /* G stays down */
    CHECK(press(LOCAL, 62) == RELATCH_FORWARD);
}

static void test_no_clear_when_hold_inactive(void) {
    reset(0, 1);
    press(LOCAL, 60); press(LOCAL, 64);
    release(LOCAL, 60); release(LOCAL, 64);
    CHECK(press(LOCAL, 62) == RELATCH_FORWARD);
}

static void test_no_clear_when_arp_disabled(void) {
    reset(1, 0);
    press(LOCAL, 60); release(LOCAL, 60);
    CHECK(press(LOCAL, 62) == RELATCH_FORWARD);
    /* enabling the arp afterwards: nothing is latched from before */
    arp_on = 1;
    release(LOCAL, 62);
    CHECK(press(LOCAL, 64) == RELATCH_FORWARD);
}

static void test_no_clear_when_nothing_latched(void) {
    reset(1, 1);
    CHECK(press(LOCAL, 60) == RELATCH_FORWARD);      /* very first key */
    release(LOCAL, 60);
    relatch_hold(&s, 0);                             /* HOLD off drops the latched note */
    relatch_hold(&s, 1);
    CHECK(press(LOCAL, 62) == RELATCH_FORWARD);      /* nothing to clear */
    release(LOCAL, 62);
    CHECK(press(LOCAL, 64) == RELATCH_CLEAR_THEN_FORWARD);
}

static void test_hold_off_with_keys_down_keeps_them_latched(void) {
    reset(1, 1);
    press(LOCAL, 60); press(LOCAL, 64);
    release(LOCAL, 60);
    relatch_hold(&s, 0);                             /* C dropped, E still held */
    relatch_hold(&s, 1);
    release(LOCAL, 64);                              /* E now latched under hold */
    CHECK(press(LOCAL, 62) == RELATCH_CLEAR_THEN_FORWARD);
}

static void test_midi_notes_count_as_keys(void) {
    reset(1, 1);
    CHECK(press(MIDI, 48) == RELATCH_FORWARD);       /* DAW holds C2 */
    press(LOCAL, 64); press(LOCAL, 67);
    release(LOCAL, 64); release(LOCAL, 67);
    CHECK(press(LOCAL, 62) == RELATCH_FORWARD);      /* MIDI note still down */
    release(LOCAL, 62);
    release(MIDI, 48);
    CHECK(press(LOCAL, 62) == RELATCH_CLEAR_THEN_FORWARD);
}

static void test_same_note_from_both_sources_counts_twice(void) {
    reset(1, 1);
    press(LOCAL, 60); press(MIDI, 60);
    release(LOCAL, 60);
    CHECK(press(LOCAL, 62) == RELATCH_FORWARD);      /* MIDI 60 still down */
}

static void test_velocity_zero_is_a_release(void) {
    reset(1, 1);
    press(MIDI, 60);
    CHECK(relatch_note(&s, arp_on, MIDI, 60, 0) == RELATCH_FORWARD);
    CHECK(press(MIDI, 62) == RELATCH_CLEAR_THEN_FORWARD);
}

static void test_releases_never_request_clear(void) {
    reset(1, 1);
    press(LOCAL, 60);
    CHECK(release(LOCAL, 60) == RELATCH_FORWARD);
    CHECK(release(LOCAL, 61) == RELATCH_FORWARD);    /* release of a key never seen */
}

static void test_repeated_press_without_release_is_idempotent(void) {
    reset(1, 1);
    press(LOCAL, 60); press(LOCAL, 60);
    release(LOCAL, 60);
    CHECK(press(LOCAL, 62) == RELATCH_CLEAR_THEN_FORWARD);
}

static void test_state_is_small_and_zero_initialised(void) {
    relatch_t z;
    memset(&z, 0, sizeof z);
    relatch_init(&s);
    CHECK(memcmp(&z, &s, sizeof s) == 0);            /* zero RAM after boot == initialised */
    CHECK(sizeof(relatch_t) <= 64);
}

int main(void) {
    test_relatch_on_first_key_after_all_released();
    test_no_clear_while_a_key_is_still_down();
    test_no_clear_when_hold_inactive();
    test_no_clear_when_arp_disabled();
    test_no_clear_when_nothing_latched();
    test_hold_off_with_keys_down_keeps_them_latched();
    test_midi_notes_count_as_keys();
    test_same_note_from_both_sources_counts_twice();
    test_velocity_zero_is_a_release();
    test_releases_never_request_clear();
    test_repeated_press_without_release_is_idempotent();
    test_state_is_small_and_zero_initialised();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
