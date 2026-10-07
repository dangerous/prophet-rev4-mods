/* Host harness for the note-value module (docs/SPEC.md: "Note value" and "Button id
 * readout"). `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "rate.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake platform ------------------------------------------------------------------ */
enum { EV_D3, EV_HOLD, EV_USED, EV_INT, EV_ACC };
typedef struct { int type, a, b, c; } ev_t;
static ev_t log_[512];
static int nlog;
static void push(int t, int a, int b, int c) { if (nlog < 512) log_[nlog++] = (ev_t){t, a, b, c}; }
void plat_display3(int c0, int c1, int c2) { push(EV_D3, c0, c1, c2); }
void plat_display_hold(void) { push(EV_HOLD, 0, 0, 0); }
void plat_a440_mark_used(void) { push(EV_USED, 0, 0, 0); }
void plat_display_int(int v) { push(EV_INT, v, 0, 0); }
void plat_engine_reset_acc(void) { push(EV_ACC, 0, 0, 0); }
static int count_type(int t) { int n = 0; for (int i = 0; i < nlog; i++) n += log_[i].type == t; return n; }
static ev_t *last_of(int t) { for (int i = nlog - 1; i >= 0; i--) if (log_[i].type == t) return &log_[i]; return 0; }
static void clear_log(void) { nlog = 0; }

enum { BLANK = 0x25, T = 0x1d, D = 0x0d, B = 0x0b };
enum { A440 = 0x0F, PROGRAM7 = 6, PROGRAM8 = 7, PRESS = 1, RELEASE = 2, REPEAT = 3 };

/* index: name, div, tps, real clocks per step, display */
static const struct { const char *name; int div, tps, cps; int d0, d1, d2; } TABLE[RATE_COUNT] = {
    {"1/32",  8, 1000,   3, BLANK, 3, 2},
    {"1/16T", 12, 1000,  4, 1, 6, T},
    {"1/16",  4, 1000,   6, BLANK, 1, 6},
    {"1/8T",  6, 1000,   8, BLANK, 8, T},
    {"1/16d", 8, 3000,   9, 1, 6, D},
    {"1/8",   2, 1000,  12, BLANK, BLANK, 8},
    {"1/8d",  4, 3000,  18, BLANK, 8, D},
    {"1/4",   1, 1000,  24, BLANK, BLANK, 4},
    {"1/4d",  2, 3000,  36, BLANK, 4, D},
    {"1/2",   1, 2000,  48, BLANK, BLANK, 2},
    {"1",     1, 4000,  96, BLANK, BLANK, 1},
    {"2 bars", 1, 8000, 192, BLANK, 2, B},
    {"4 bars", 1, 16000, 384, BLANK, 4, B},
};

static rate_t r;

static void test_default_is_eighths_and_zero_state(void) {
    rate_t z; memset(&z, 0, sizeof z);
    rate_init(&r);
    CHECK(memcmp(&z, &r, sizeof r) == 0);
    CHECK(rate_index(&r) == RATE_DEFAULT_INDEX && RATE_DEFAULT_INDEX == 5);
    int div, tps; rate_params(&r, &div, &tps);
    CHECK(div == 2 && tps == 1000);
    uint8_t d[3]; rate_display(&r, d);
    CHECK(d[0] == BLANK && d[1] == BLANK && d[2] == 8);
}

static void test_table_params_and_display(void) {
    rate_init(&r);
    while (rate_step(&r, -1)) {}
    CHECK(rate_index(&r) == 0);
    for (int i = 0; i < RATE_COUNT; i++) {
        int div, tps; uint8_t d[3];
        CHECK(rate_index(&r) == i);
        rate_params(&r, &div, &tps);
        rate_display(&r, d);
        if (div != TABLE[i].div || tps != TABLE[i].tps || d[0] != TABLE[i].d0 || d[1] != TABLE[i].d1 || d[2] != TABLE[i].d2) {
            failures++; checks++;
            printf("FAIL table %s: div %d tps %d display %02x %02x %02x\n", TABLE[i].name, div, tps, d[0], d[1], d[2]);
        } else checks++;
        if (i < RATE_COUNT - 1) CHECK(rate_step(&r, +1) == 1);
    }
    CHECK(rate_step(&r, +1) == 0);                   /* clamped at the longest */
    CHECK(rate_index(&r) == RATE_COUNT - 1);
}

static void test_clock_filter_keeps_steps_exactly_on_real_clocks(void) {
    for (int i = 0; i < RATE_COUNT; i++) {
        rate_init(&r);
        while (rate_index(&r) > i) rate_step(&r, -1);
        while (rate_index(&r) < i) rate_step(&r, +1);
        CHECK(rate_clock(&r, 0xFA) == 1);            /* Start: forwarded once, phase reset */
        int seen = 0, bad = 0, first = -1;
        int steps_real[200]; int nsteps = 0;
        for (int real = 0; real < 384 * 2; real++) {
            int n = rate_clock(&r, 0xF8);
            if (real == 0) first = n;
            for (int k = 0; k < n; k++) {
                if (seen % 12 == 0 && nsteps < 200) steps_real[nsteps++] = real;
                seen++;
            }
        }
        int cps = TABLE[i].cps;
        for (int s = 0; s < nsteps; s++) if (steps_real[s] != s * cps) bad++;
        if (bad || first < 1 || nsteps < 2) { failures++; checks++;
            printf("FAIL clock %s: first=%d steps=%d misaligned=%d\n", TABLE[i].name, first, nsteps, bad); }
        else checks++;
        CHECK(rate_clock(&r, 0xFB) == 1 && rate_clock(&r, 0xFC) == 1 && rate_clock(&r, 0x90) == 1);
    }
}

static void test_buttons_change_rate_only_while_a440_held(void) {
    rate_init(&r); clear_log();
    CHECK(rate_button(&r, 0, PROGRAM8, PRESS) == 0);          /* A440 not held: untouched */
    CHECK(rate_index(&r) == 5 && nlog == 0);
    CHECK(rate_button(&r, 1, PROGRAM8, PRESS) == 0);          /* forwarded so V5 marks A440 used */
    CHECK(rate_index(&r) == 4);
    CHECK(count_type(EV_D3) == 1 && count_type(EV_HOLD) == 1 && count_type(EV_ACC) == 1);
    ev_t *d = last_of(EV_D3);
    CHECK(d && d->a == 1 && d->b == 6 && d->c == D);           /* 16d */
    CHECK(rate_button(&r, 1, PROGRAM8, REPEAT) == 0);          /* repeats ignored */
    CHECK(rate_button(&r, 1, PROGRAM8, RELEASE) == 0);
    CHECK(rate_index(&r) == 4);
    clear_log();
    CHECK(rate_button(&r, 1, PROGRAM7, PRESS) == 0);
    CHECK(rate_index(&r) == 5 && count_type(EV_ACC) == 1);
    clear_log();
    while (rate_step(&r, -1)) {}
    rate_button(&r, 1, PROGRAM8, PRESS);                       /* at the shortest: show, no change */
    CHECK(rate_index(&r) == 0 && count_type(EV_D3) == 1 && count_type(EV_ACC) == 0);
}

static void test_readout_shows_unassigned_button_ids(void) {
    rate_init(&r); clear_log();
    CHECK(rate_button(&r, 1, 0x30, PRESS) == 1);               /* consumed */
    CHECK(count_type(EV_INT) == 1 && last_of(EV_INT)->a == 0x30);
    CHECK(count_type(EV_HOLD) == 1 && count_type(EV_USED) == 1);
    clear_log();
    CHECK(rate_button(&r, 1, 0x30, REPEAT) == 1);              /* swallowed, not re-shown */
    CHECK(rate_button(&r, 1, 0x30, RELEASE) == 1);
    CHECK(nlog == 0);
    /* assigned ids are never read out */
    for (int id = 0; id < 8; id++) CHECK(rate_button(&r, 1, id, RELEASE) == 0);
    CHECK(rate_button(&r, 1, A440, PRESS) == 0);
    CHECK(rate_button(&r, 1, 0x20, PRESS) == 0 && rate_button(&r, 1, 0x28, PRESS) == 0);
    CHECK(rate_button(&r, 1, 0x19, PRESS) == 0);
    CHECK(rate_button(&r, 0, 0x30, PRESS) == 0);               /* A440 not held: normal button */
    CHECK(count_type(EV_INT) == 0);
}

static void test_beats_per_step_give_integral_midi_clocks(void) {
    /* native engine: 1/32 1/16T 1/16 1/8T 1/16d 1/8 1/8d 1/4 1/4d 1/2 1 2bars 4bars */
    static const int clocks[RATE_COUNT] = { 3, 4, 6, 8, 9, 12, 18, 24, 36, 48, 96, 192, 384 };
    rate_t r; rate_init(&r);
    while (rate_step(&r, -1)) {}
    for (int i = 0; i < RATE_COUNT; i++) {
        int num = 0, den = 0;
        CHECK(rate_index(&r) == i);
        rate_beats(&r, &num, &den);
        CHECK(num >= 1 && den >= 1 && (24 * num) % den == 0 && 24 * num / den == clocks[i]);
        rate_step(&r, 1);
    }
    rate_init(&r);
    { int num, den; rate_beats(&r, &num, &den); CHECK(num == 1 && den == 2); }   /* 1/8 */
}

int main(void) {
    test_default_is_eighths_and_zero_state();
    test_table_params_and_display();
    test_beats_per_step_give_integral_midi_clocks();
    test_clock_filter_keeps_steps_exactly_on_real_clocks();
    test_buttons_change_rate_only_while_a440_held();
    test_readout_shows_unassigned_button_ids();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
