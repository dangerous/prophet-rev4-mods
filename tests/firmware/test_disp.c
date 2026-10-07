/* Host harness for "Display messages" (docs/SPEC.md): every message reverts to the stock
 * patch display 1.5 s after the last one. `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "disp.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

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

int main(void) {
    test_zero_state_is_idle_and_fits();
    test_message_reverts_once_after_1500_ticks();
    test_new_message_restarts_the_timer();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures ? 1 : 0;
}
