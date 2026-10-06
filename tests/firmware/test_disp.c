/* Host harness for "Display messages" (docs/SPEC.md): every message — the wrapper's own and
 * the arp's (detected as a change of a displayed setting) — reverts to the stock patch
 * display 1.5 s after the last change. `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "disp.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* run n ticks, return how many of them reported expiry */
static int run(disp_t *d, int n) { int e = 0; while (n-- > 0) e += disp_tick(d); return e; }

static void test_zero_state_is_idle_and_fits(void) {
    disp_t z, d; memset(&z, 0, sizeof z);
    disp_init(&d);
    CHECK(memcmp(&z, &d, sizeof d) == 0);
    CHECK(sizeof(disp_t) <= 0x10);
    CHECK(!disp_active(&d));
    CHECK(run(&d, 3000) == 0);                       /* idle: never restores by itself */
}

static void test_message_reverts_once_after_1500_ticks(void) {
    disp_t d; disp_init(&d);
    disp_touch(&d);
    CHECK(disp_active(&d));
    CHECK(run(&d, DISP_TICKS - 1) == 0);
    CHECK(disp_active(&d));
    CHECK(disp_tick(&d) == 1);                       /* exactly at 1.5 s */
    CHECK(!disp_active(&d));
    CHECK(run(&d, 3000) == 0);                       /* and only once */
    CHECK(DISP_TICKS == 1500);
}

static void test_new_message_restarts_the_timer(void) {
    disp_t d; disp_init(&d);
    disp_touch(&d);
    run(&d, 1000);
    disp_touch(&d);
    CHECK(run(&d, DISP_TICKS - 1) == 0);
    CHECK(disp_tick(&d) == 1);
}

static void test_first_observation_primes_without_a_message(void) {
    disp_t d; disp_init(&d);
    disp_observe(&d, 0, 0, 1, 0, 120);               /* boot-time values */
    CHECK(!disp_active(&d));
    disp_observe(&d, 0, 0, 1, 0, 120);
    CHECK(!disp_active(&d));
}

static void test_each_watched_setting_change_is_a_message(void) {
    disp_t d; disp_init(&d);
    disp_observe(&d, 0, 0, 1, 0, 120);
    disp_observe(&d, 1, 0, 1, 0, 120);               /* arp on: V5 shows BPM */
    CHECK(disp_active(&d));
    CHECK(run(&d, DISP_TICKS) == 1);
    disp_observe(&d, 1, 2, 1, 0, 120);               /* mode */
    CHECK(disp_active(&d));
    run(&d, DISP_TICKS);
    disp_observe(&d, 1, 2, 3, 0, 120);               /* octaves */
    CHECK(disp_active(&d));
    run(&d, DISP_TICKS);
    disp_observe(&d, 1, 2, 3, 1, 120);               /* clock source */
    CHECK(disp_active(&d));
    run(&d, DISP_TICKS);
    disp_observe(&d, 1, 2, 3, 1, 121);               /* BPM (pot moving) */
    CHECK(disp_active(&d));
    run(&d, 100);
    disp_observe(&d, 1, 2, 3, 1, 122);               /* still moving: restarts */
    CHECK(run(&d, DISP_TICKS - 1) == 0);
    CHECK(disp_tick(&d) == 1);
    disp_observe(&d, 1, 2, 3, 1, 122);               /* unchanged: nothing */
    CHECK(!disp_active(&d));
}

int main(void) {
    test_zero_state_is_idle_and_fits();
    test_message_reverts_once_after_1500_ticks();
    test_new_message_restarts_the_timer();
    test_first_observation_primes_without_a_message();
    test_each_watched_setting_change_is_a_message();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
