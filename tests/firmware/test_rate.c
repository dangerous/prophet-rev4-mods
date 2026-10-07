/* Host harness for the note-value table (docs/SPEC.md: "Note value", "Patch memory"). `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "rate.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

enum { BLANK = 0x25, T = 0x1d, D = 0x0d, S = 0x1c };

/* the Prophet-6 list in its order, longest first (index 0 = Half): beats per step (per pair for
 * swing), swing flag, MIDI clocks per step (per pair), display text and the patch-memory code */
static const struct { const char *name; int num, den, swing, clocks; int d0, d1, d2; int code; } TABLE[] = {
    { "Half",   2, 1, 0,  48,  BLANK, BLANK, 2,     9 },
    { "Qtr",    1, 1, 0,  24,  BLANK, BLANK, 4,     7 },
    { "8th D",  3, 4, 0,  18,  BLANK, 8, D,         6 },
    { "8th",    1, 2, 0,  12,  BLANK, BLANK, 8,     5 },
    { "8th S",  1, 1, 1,  24,  BLANK, 8, S,        14 },
    { "8th T",  1, 3, 0,   8,  BLANK, 8, T,         3 },
    { "16th",   1, 4, 0,   6,  BLANK, 1, 6,         2 },
    { "16th S", 1, 2, 1,  12,  1, 6, S,            13 },
    { "16th T", 1, 6, 0,   4,  1, 6, T,             1 },
    { "32nd",   1, 8, 0,   3,  BLANK, 3, 2,         0 },
};
#define NTABLE ((int)(sizeof TABLE / sizeof *TABLE))

/* swing pairs split 2:1 in clocks: 8th S 16 + 8, 16th S 8 + 4 */
static const int SWING_LONG[NTABLE] = { 0, 0, 0, 0, 16, 0, 0, 8, 0, 0 };

static rate_t r;

static void test_default_is_eighths_and_zero_state(void) {
    rate_t z; memset(&z, 0, sizeof z);
    rate_init(&r);
    CHECK(memcmp(&z, &r, sizeof r) == 0);
    CHECK(RATE_COUNT == 10 && NTABLE == RATE_COUNT);
    CHECK(rate_index(&r) == RATE_DEFAULT_INDEX && RATE_DEFAULT_INDEX == 3);
    int num, den; rate_beats(&r, &num, &den);
    CHECK(num == 1 && den == 2 && rate_swing(&r) == 0);
    uint8_t d[3]; rate_display(&r, d);
    CHECK(d[0] == BLANK && d[1] == BLANK && d[2] == 8);
}

/* walk from the longest (Program 7 = -1 to the end) to the shortest with +1 (Program 8) */
static void test_table_order_beats_swing_clocks_display_and_codes(void) {
    rate_init(&r);
    while (rate_step(&r, -1)) {}
    CHECK(rate_index(&r) == 0);
    for (int i = 0; i < RATE_COUNT && i < NTABLE; i++) {
        int num = 0, den = 0; uint8_t d[3];
        CHECK(rate_index(&r) == i);
        rate_beats(&r, &num, &den);
        rate_display(&r, d);
        int sw = rate_swing(&r);
        int ok = num == TABLE[i].num && den == TABLE[i].den && sw == TABLE[i].swing
                 && (24 * num) % den == 0 && 24 * num / den == TABLE[i].clocks
                 && (!sw || ((24 * num / den) % 3 == 0 && (24 * num / den) / 3 * 2 == SWING_LONG[i]))
                 && d[0] == TABLE[i].d0 && d[1] == TABLE[i].d1 && d[2] == TABLE[i].d2
                 && rate_code(i) == TABLE[i].code;
        checks++;
        if (!ok) { failures++; printf("FAIL table %s: %d/%d swing %d display %02x %02x %02x code %d\n",
                                      TABLE[i].name, num, den, sw, d[0], d[1], d[2], rate_code(i)); }
        if (i < RATE_COUNT - 1) CHECK(rate_step(&r, +1) == 1);
    }
    CHECK(rate_step(&r, +1) == 0);                   /* clamped at the shortest (32nd) */
    CHECK(rate_index(&r) == RATE_COUNT - 1);
}

static void test_codes_are_fixed_and_legacy_codes_map_to_the_nearest_value(void) {
    /* code -> value: written codes of the ten values, read-only legacy codes 4, 8, 10, 11, 12 */
    static const char *const BY_CODE[15] = {
        "32nd", "16th T", "16th", "8th T", "16th" /* 1/16d */, "8th", "8th D", "Qtr",
        "Qtr" /* 1/4d */, "Half", "Half" /* whole */, "Half" /* 2 bars */, "Half" /* 4 bars */,
        "16th S", "8th S" };
    static const int WRITTEN[15] = { 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1 };
    for (int c = 0; c < 15; c++) {
        int i = rate_index_from_code(c);
        CHECK(i >= 0 && i < RATE_COUNT);
        if (i >= 0 && i < NTABLE && strcmp(TABLE[i].name, BY_CODE[c]) != 0) {
            checks++; failures++; printf("FAIL code %d -> %s, want %s\n", c, TABLE[i].name, BY_CODE[c]);
        }
        CHECK(i < 0 || (rate_code(i) == c) == WRITTEN[c]);
    }
    for (int i = 0; i < RATE_COUNT; i++) CHECK(rate_index_from_code(rate_code(i)) == i);
    CHECK(rate_index_from_code(15) == -1 && rate_index_from_code(-1) == -1 && rate_index_from_code(16) == -1);
    CHECK(rate_code(-1) == -1 && rate_code(RATE_COUNT) == -1);
    CHECK(rate_code(RATE_DEFAULT_INDEX) == 5);
}

static void test_steps_clamp_at_both_ends(void) {
    rate_init(&r);
    for (int i = 0; i < 20; i++) rate_step(&r, -1);  /* longer (Program 7) */
    CHECK(rate_index(&r) == 0 && rate_step(&r, -1) == 0);
    for (int i = 0; i < 20; i++) rate_step(&r, +1);  /* shorter (Program 8) */
    CHECK(rate_index(&r) == RATE_COUNT - 1 && rate_step(&r, +1) == 0);
    CHECK(rate_step(&r, -1) == 1 && rate_index(&r) == RATE_COUNT - 2);
}

static void test_set_index_validates(void) {
    rate_init(&r);
    CHECK(rate_set_index(&r, 4) == 1 && rate_index(&r) == 4 && rate_swing(&r) == 1);   /* 8th S */
    CHECK(rate_set_index(&r, 10) == 0 && rate_index(&r) == 4);
    CHECK(rate_set_index(&r, -1) == 0 && rate_index(&r) == 4);
    CHECK(rate_set_index(&r, 9) == 1 && rate_index(&r) == 9);
    CHECK(rate_set_index(&r, 0) == 1 && rate_index(&r) == 0);
}

int main(void) {
    test_default_is_eighths_and_zero_state();
    test_set_index_validates();
    test_table_order_beats_swing_clocks_display_and_codes();
    test_codes_are_fixed_and_legacy_codes_map_to_the_nearest_value();
    test_steps_clamp_at_both_ends();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
