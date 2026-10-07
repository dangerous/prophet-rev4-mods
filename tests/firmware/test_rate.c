/* Host harness for the note-value table (docs/SPEC.md: "Note value"). `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "rate.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { BLANK = 0x25, T = 0x1d, D = 0x0d, B = 0x0b };

/* shortest to longest: MIDI clocks per step and display text */
static const struct { const char *name; int clocks; int d0, d1, d2; } TABLE[RATE_COUNT] = {
    { "1/32",   3,   BLANK, 3, 2 },
    { "1/16T",  4,   1, 6, T },
    { "1/16",   6,   BLANK, 1, 6 },
    { "1/8T",   8,   BLANK, 8, T },
    { "1/16d",  9,   1, 6, D },
    { "1/8",   12,   BLANK, BLANK, 8 },
    { "1/8d",  18,   BLANK, 8, D },
    { "1/4",   24,   BLANK, BLANK, 4 },
    { "1/4d",  36,   BLANK, 4, D },
    { "1/2",   48,   BLANK, BLANK, 2 },
    { "1",     96,   BLANK, BLANK, 1 },
    { "2 bars", 192, BLANK, 2, B },
    { "4 bars", 384, BLANK, 4, B },
};

static rate_t r;

static void test_default_is_eighths_and_zero_state(void) {
    rate_t z; memset(&z, 0, sizeof z);
    rate_init(&r);
    CHECK(memcmp(&z, &r, sizeof r) == 0);
    CHECK(rate_index(&r) == RATE_DEFAULT_INDEX && RATE_DEFAULT_INDEX == 5);
    int num, den; rate_beats(&r, &num, &den);
    CHECK(num == 1 && den == 2);
    uint8_t d[3]; rate_display(&r, d);
    CHECK(d[0] == BLANK && d[1] == BLANK && d[2] == 8);
}

static void test_table_beats_give_integral_clocks_and_display(void) {
    rate_init(&r);
    while (rate_step(&r, -1)) {}
    CHECK(rate_index(&r) == 0);
    for (int i = 0; i < RATE_COUNT; i++) {
        int num = 0, den = 0; uint8_t d[3];
        CHECK(rate_index(&r) == i);
        rate_beats(&r, &num, &den);
        rate_display(&r, d);
        int ok = num >= 1 && den >= 1 && (24 * num) % den == 0 && 24 * num / den == TABLE[i].clocks
                 && d[0] == TABLE[i].d0 && d[1] == TABLE[i].d1 && d[2] == TABLE[i].d2;
        checks++;
        if (!ok) { failures++; printf("FAIL table %s: %d/%d display %02x %02x %02x\n", TABLE[i].name, num, den, d[0], d[1], d[2]); }
        if (i < RATE_COUNT - 1) CHECK(rate_step(&r, +1) == 1);
    }
    CHECK(rate_step(&r, +1) == 0);                   /* clamped at the longest */
    CHECK(rate_index(&r) == RATE_COUNT - 1);
}

static void test_steps_clamp_at_both_ends(void) {
    rate_init(&r);
    for (int i = 0; i < 20; i++) rate_step(&r, -1);
    CHECK(rate_index(&r) == 0 && rate_step(&r, -1) == 0);
    for (int i = 0; i < 20; i++) rate_step(&r, +1);
    CHECK(rate_index(&r) == RATE_COUNT - 1 && rate_step(&r, +1) == 0);
    CHECK(rate_step(&r, -1) == 1 && rate_index(&r) == RATE_COUNT - 2);
}

int main(void) {
    test_default_is_eighths_and_zero_state();
    test_table_beats_give_integral_clocks_and_display();
    test_steps_clamp_at_both_ends();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
