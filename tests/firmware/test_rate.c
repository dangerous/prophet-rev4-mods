/* Host harness for the note-value table (docs/SPEC.md: "Note value", "Patch memory"). `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "rate.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { BLANK = 0x25, T = 0x1d, D = 0x0d, B = 0x0b, S = 0x1c };

/* shortest to longest by average step: beats per step (per pair for swing), swing flag,
 * MIDI clocks per step (per pair), display text and the fixed patch-memory code */
static const struct { const char *name; int num, den, swing, clocks; int d0, d1, d2; int code; } TABLE[] = {
    { "1/32",   1, 8, 0,   3,  BLANK, 3, 2,         0 },
    { "1/16T",  1, 6, 0,   4,  1, 6, T,             1 },
    { "1/16",   1, 4, 0,   6,  BLANK, 1, 6,         2 },
    { "16S",    1, 2, 1,  12,  1, 6, S,            13 },
    { "1/8T",   1, 3, 0,   8,  BLANK, 8, T,         3 },
    { "1/16d",  3, 8, 0,   9,  1, 6, D,             4 },
    { "1/8",    1, 2, 0,  12,  BLANK, BLANK, 8,     5 },
    { "8S",     1, 1, 1,  24,  BLANK, 8, S,        14 },
    { "1/8d",   3, 4, 0,  18,  BLANK, 8, D,         6 },
    { "1/4",    1, 1, 0,  24,  BLANK, BLANK, 4,     7 },
    { "1/4d",   3, 2, 0,  36,  BLANK, 4, D,         8 },
    { "1/2",    2, 1, 0,  48,  BLANK, BLANK, 2,     9 },
    { "1",      4, 1, 0,  96,  BLANK, BLANK, 1,    10 },
    { "2 bars", 8, 1, 0, 192,  BLANK, 2, B,        11 },
    { "4 bars", 16, 1, 0, 384, BLANK, 4, B,        12 },
};
#define NTABLE ((int)(sizeof TABLE / sizeof *TABLE))

static rate_t r;

static void test_default_is_eighths_and_zero_state(void) {
    rate_t z; memset(&z, 0, sizeof z);
    rate_init(&r);
    CHECK(memcmp(&z, &r, sizeof r) == 0);
    CHECK(RATE_COUNT == 15 && NTABLE == RATE_COUNT);
    CHECK(rate_index(&r) == RATE_DEFAULT_INDEX && RATE_DEFAULT_INDEX == 6);
    int num, den; rate_beats(&r, &num, &den);
    CHECK(num == 1 && den == 2 && rate_swing(&r) == 0);
    uint8_t d[3]; rate_display(&r, d);
    CHECK(d[0] == BLANK && d[1] == BLANK && d[2] == 8);
}

static void test_table_beats_swing_clocks_display_and_codes(void) {
    rate_init(&r);
    while (rate_step(&r, -1)) {}
    CHECK(rate_index(&r) == 0);
    int prev_avg2 = 0;                               /* average clocks per step, doubled */
    for (int i = 0; i < RATE_COUNT && i < NTABLE; i++) {
        int num = 0, den = 0; uint8_t d[3];
        CHECK(rate_index(&r) == i);
        rate_beats(&r, &num, &den);
        rate_display(&r, d);
        int sw = rate_swing(&r);
        int ok = num == TABLE[i].num && den == TABLE[i].den && sw == TABLE[i].swing
                 && (24 * num) % den == 0 && 24 * num / den == TABLE[i].clocks
                 && (!sw || (24 * num / den) % 3 == 0)              /* a pair splits 2:1 in clocks */
                 && d[0] == TABLE[i].d0 && d[1] == TABLE[i].d1 && d[2] == TABLE[i].d2
                 && rate_code(i) == TABLE[i].code;
        checks++;
        if (!ok) { failures++; printf("FAIL table %s: %d/%d swing %d display %02x %02x %02x code %d\n",
                                      TABLE[i].name, num, den, sw, d[0], d[1], d[2], rate_code(i)); }
        int avg2 = sw ? TABLE[i].clocks : 2 * TABLE[i].clocks;
        CHECK(avg2 >= prev_avg2);                    /* ordered by average step length */
        prev_avg2 = avg2;
        if (i < RATE_COUNT - 1) CHECK(rate_step(&r, +1) == 1);
    }
    CHECK(rate_step(&r, +1) == 0);                   /* clamped at the longest */
    CHECK(rate_index(&r) == RATE_COUNT - 1);
}

static void test_codes_are_fixed_and_round_trip(void) {
    /* 0..12 = the original thirteen values in their original order; 13 = 16S, 14 = 8S */
    static const char *const BY_CODE[15] = { "1/32", "1/16T", "1/16", "1/8T", "1/16d", "1/8", "1/8d",
                                             "1/4", "1/4d", "1/2", "1", "2 bars", "4 bars", "16S", "8S" };
    for (int c = 0; c < 15; c++) {
        int i = rate_index_from_code(c);
        CHECK(i >= 0 && i < RATE_COUNT);
        if (i >= 0 && i < NTABLE) CHECK(strcmp(TABLE[i].name, BY_CODE[c]) == 0);
        CHECK(i < 0 || rate_code(i) == c);
    }
    CHECK(rate_index_from_code(15) == -1 && rate_index_from_code(-1) == -1);
    CHECK(rate_code(-1) == -1 && rate_code(RATE_COUNT) == -1);
    CHECK(rate_code(RATE_DEFAULT_INDEX) == 5);
}

static void test_steps_clamp_at_both_ends(void) {
    rate_init(&r);
    for (int i = 0; i < 20; i++) rate_step(&r, -1);
    CHECK(rate_index(&r) == 0 && rate_step(&r, -1) == 0);
    for (int i = 0; i < 20; i++) rate_step(&r, +1);
    CHECK(rate_index(&r) == RATE_COUNT - 1 && rate_step(&r, +1) == 0);
    CHECK(rate_step(&r, -1) == 1 && rate_index(&r) == RATE_COUNT - 2);
}

static void test_set_index_validates(void) {
    rate_init(&r);
    CHECK(rate_set_index(&r, 7) == 1 && rate_index(&r) == 7 && rate_swing(&r) == 1);
    CHECK(rate_set_index(&r, 15) == 0 && rate_index(&r) == 7);
    CHECK(rate_set_index(&r, -1) == 0 && rate_index(&r) == 7);
    CHECK(rate_set_index(&r, 14) == 1 && rate_index(&r) == 14);
    CHECK(rate_set_index(&r, 0) == 1 && rate_index(&r) == 0);
}

int main(void) {
    test_default_is_eighths_and_zero_state();
    test_set_index_validates();
    test_table_beats_swing_clocks_display_and_codes();
    test_codes_are_fixed_and_round_trip();
    test_steps_clamp_at_both_ends();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
