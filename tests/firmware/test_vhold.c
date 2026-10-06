/* Host harness for "HOLD while the arp is on" (docs/SPEC.md): while the arp is enabled the
 * synth's own hold is suspended — stock note_off is told hold is off, the voice-engine hold
 * message is withheld, and enable/disable transitions re-sync the voice engine.
 * `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "vhold.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_zero_state_means_arp_off(void) {
    vhold_t z, s; memset(&z, 0, sizeof z);
    vhold_init(&s);
    CHECK(memcmp(&z, &s, sizeof s) == 0);
    CHECK(sizeof(vhold_t) <= 0x10);
}

static void test_note_off_sees_hold_off_only_while_enabled(void) {
    CHECK(vhold_query(1, 1) == 0);      /* arp on, HOLD on: stock must release the step note */
    CHECK(vhold_query(1, 0) == 0);
    CHECK(vhold_query(0, 1) == 1);      /* arp off: stock hold as before */
    CHECK(vhold_query(0, 0) == 0);
    CHECK(vhold_query(0, 7) == 1);      /* any non-zero stock answer is "on" */
}

static void test_hold_message_withheld_while_enabled(void) {
    CHECK(vhold_post_on_hold_change(1) == 0);
    CHECK(vhold_post_on_hold_change(0) == 1);
}

static void test_transitions_resync_the_voice_engine_only_with_hold_active(void) {
    vhold_t s; vhold_init(&s);
    CHECK(vhold_tick(&s, 0, 0) == -1);
    CHECK(vhold_tick(&s, 0, 1) == -1);               /* HOLD pressed with the arp off: stock did it */
    CHECK(vhold_tick(&s, 1, 1) == 0);                /* arp on while HOLD active: voice hold off */
    CHECK(vhold_tick(&s, 1, 1) == -1);               /* no repeats */
    CHECK(vhold_tick(&s, 1, 0) == -1);               /* HOLD released while on: handled by the hook */
    CHECK(vhold_tick(&s, 1, 1) == -1);
    CHECK(vhold_tick(&s, 0, 1) == 1);                /* arp off while HOLD active: voice hold on */
    CHECK(vhold_tick(&s, 0, 1) == -1);
    CHECK(vhold_tick(&s, 1, 0) == -1);               /* transitions with HOLD inactive post nothing */
    CHECK(vhold_tick(&s, 0, 0) == -1);
}

int main(void) {
    test_zero_state_means_arp_off();
    test_note_off_sees_hold_off_only_while_enabled();
    test_hold_message_withheld_while_enabled();
    test_transitions_resync_the_voice_engine_only_with_hold_active();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
