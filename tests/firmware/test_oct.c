/* Host harness for the keyboard octave shift (docs/SPEC.md: "Keyboard octave shift").
 * `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "oct.h"
#include "platform.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake platform ------------------------------------------------------------------ */
enum { EV_INT, EV_HOLD };
static int log_[256][2], nlog;
static void push(int t, int a) { if (nlog < 256) { log_[nlog][0] = t; log_[nlog][1] = a; nlog++; } }
void plat_display_int(int v) { push(EV_INT, v); }
void plat_display_hold(void) { push(EV_HOLD, 0); }
static int count_type(int t) { int n = 0; for (int i = 0; i < nlog; i++) n += log_[i][0] == t; return n; }
static int last_int(void) { for (int i = nlog - 1; i >= 0; i--) if (log_[i][0] == EV_INT) return log_[i][1]; return -999; }
static void clear_log(void) { nlog = 0; }

enum { LO_FREQ = 37, BANK = 0x28, GROUP = 0x20, A440 = 0x0F, PRESS = 1, RELEASE = 2, REPEAT = 3 };
static oct_t o;

static void reset(void) { oct_init(&o); clear_log(); }
static void mod(int value) { oct_button(&o, LO_FREQ, value); }

static void test_zero_state_is_no_shift(void) {
    oct_t z; memset(&z, 0, sizeof z);
    oct_init(&o);
    CHECK(memcmp(&z, &o, sizeof o) == 0 && oct_shift(&o) == 0);
    CHECK(oct_map_key(&o, 60, 1) == 60 && oct_map_key(&o, 60, 0) == 60);
    CHECK(sizeof(oct_t) <= 0xA0);
}

static void test_bank_up_group_down_clamped_with_display(void) {
    reset();
    CHECK(oct_button(&o, LO_FREQ, PRESS) == OCT_CONSUMED);
    CHECK(oct_button(&o, BANK, PRESS) == OCT_CONSUMED);
    CHECK(oct_shift(&o) == 1 && last_int() == 1 && count_type(EV_HOLD) == 1);
    CHECK(oct_button(&o, BANK, RELEASE) == OCT_CONSUMED);
    oct_button(&o, BANK, PRESS); oct_button(&o, BANK, RELEASE);
    oct_button(&o, BANK, PRESS); oct_button(&o, BANK, RELEASE);
    CHECK(oct_shift(&o) == 2 && last_int() == 2);                 /* clamped at +2, still shown */
    CHECK(count_type(EV_INT) == 3);
    for (int i = 0; i < 5; i++) { oct_button(&o, GROUP, PRESS); oct_button(&o, GROUP, RELEASE); }
    CHECK(oct_shift(&o) == -2 && last_int() == -2);
    CHECK(oct_button(&o, LO_FREQ, RELEASE) == OCT_CONSUMED);       /* used as a modifier: no tap */
}

static void test_both_directions_together_reset_to_zero(void) {
    reset();
    mod(PRESS);
    oct_button(&o, BANK, PRESS); oct_button(&o, BANK, RELEASE);
    oct_button(&o, BANK, PRESS); oct_button(&o, BANK, RELEASE);
    CHECK(oct_shift(&o) == 2);
    oct_button(&o, GROUP, PRESS);                                  /* hold Group ... */
    CHECK(oct_shift(&o) == 1);
    oct_button(&o, BANK, PRESS);                                   /* ... and press Bank: reset */
    CHECK(oct_shift(&o) == 0 && last_int() == 0);
    oct_button(&o, BANK, RELEASE); oct_button(&o, GROUP, RELEASE);
    mod(RELEASE);
}

static void test_tap_without_bank_or_group_is_replayed(void) {
    reset();
    CHECK(oct_button(&o, LO_FREQ, PRESS) == OCT_CONSUMED);
    CHECK(oct_button(&o, LO_FREQ, RELEASE) == OCT_REPLAY_TAP);
    CHECK(nlog == 0 && oct_shift(&o) == 0);                       /* a tap shows nothing */
    /* keys played while it is held do not count as "used" */
    mod(PRESS);
    oct_map_key(&o, 60, 1); oct_map_key(&o, 60, 0);
    CHECK(oct_button(&o, LO_FREQ, RELEASE) == OCT_REPLAY_TAP);
}

static void test_held_modifier_shows_the_shift_and_is_no_tap(void) {
    reset();
    mod(PRESS); oct_button(&o, BANK, PRESS); oct_button(&o, BANK, RELEASE); mod(RELEASE);   /* shift 1 */
    clear_log();
    mod(PRESS);
    CHECK(nlog == 0);                                              /* the press alone shows nothing yet */
    CHECK(oct_button(&o, LO_FREQ, REPEAT) == OCT_CONSUMED);        /* the panel reports it held */
    CHECK(last_int() == 1 && count_type(EV_INT) == 1 && count_type(EV_HOLD) == 1);   /* the current shift, unchanged */
    CHECK(oct_button(&o, LO_FREQ, REPEAT) == OCT_CONSUMED);
    CHECK(count_type(EV_INT) == 2 && count_type(EV_HOLD) == 2);   /* every repeat renews the message */
    CHECK(oct_button(&o, LO_FREQ, RELEASE) == OCT_CONSUMED);       /* a hold is not a tap: no replay */
    CHECK(oct_shift(&o) == 1);
    clear_log();
    mod(PRESS); oct_button(&o, LO_FREQ, REPEAT);                   /* held, then shifted */
    oct_button(&o, GROUP, PRESS); oct_button(&o, GROUP, RELEASE);
    CHECK(oct_shift(&o) == 0 && last_int() == 0);
    CHECK(oct_button(&o, LO_FREQ, RELEASE) == OCT_CONSUMED);
    mod(PRESS);                                                    /* and a plain tap still is one */
    CHECK(oct_button(&o, LO_FREQ, RELEASE) == OCT_REPLAY_TAP);
}

static void test_other_buttons_pass_through_when_not_held(void) {
    reset();
    CHECK(oct_button(&o, BANK, PRESS) == OCT_FORWARD);
    CHECK(oct_button(&o, GROUP, PRESS) == OCT_FORWARD);
    CHECK(oct_button(&o, A440, PRESS) == OCT_FORWARD);
    CHECK(oct_button(&o, 3, PRESS) == OCT_FORWARD);
    mod(PRESS);
    CHECK(oct_button(&o, A440, PRESS) == OCT_FORWARD);             /* only Bank/Group are ours */
    CHECK(oct_button(&o, 3, PRESS) == OCT_FORWARD);
    CHECK(oct_button(&o, BANK, REPEAT) == OCT_CONSUMED);           /* repeats swallowed, no change */
    CHECK(oct_shift(&o) == 0 && count_type(EV_INT) == 0);
    mod(RELEASE);
    /* a stray Bank release after the modifier went up is forwarded */
    CHECK(oct_button(&o, BANK, RELEASE) == OCT_FORWARD);
}

static void test_keys_are_shifted_and_releases_use_the_press_time_shift(void) {
    reset();
    mod(PRESS); oct_button(&o, BANK, PRESS); oct_button(&o, BANK, RELEASE); mod(RELEASE);
    CHECK(oct_map_key(&o, 60, 1) == 72);
    CHECK(oct_map_key(&o, 60, 1) == 72);                           /* second reader (MIDI out) agrees */
    mod(PRESS); oct_button(&o, GROUP, PRESS); oct_button(&o, GROUP, RELEASE);
    oct_button(&o, GROUP, PRESS); oct_button(&o, GROUP, RELEASE); mod(RELEASE);
    CHECK(oct_shift(&o) == -1);
    CHECK(oct_map_key(&o, 60, 0) == 72);                           /* release follows the press */
    CHECK(oct_map_key(&o, 60, 0) == 72);
    CHECK(oct_map_key(&o, 60, 1) == 48);                           /* new press uses the new shift */
    CHECK(oct_map_key(&o, 60, 0) == 48);
    CHECK(oct_map_key(&o, 100, 0) == 100 - 12);                    /* never pressed: current shift */
}

static void test_out_of_range_keys_are_silent_on_and_off(void) {
    reset();
    mod(PRESS); oct_button(&o, BANK, PRESS); oct_button(&o, BANK, RELEASE);
    oct_button(&o, BANK, PRESS); oct_button(&o, BANK, RELEASE); mod(RELEASE);
    CHECK(oct_map_key(&o, 120, 1) < 0);
    CHECK(oct_map_key(&o, 120, 0) < 0);
    CHECK(oct_map_key(&o, 103, 1) == 127 && oct_map_key(&o, 103, 0) == 127);
    CHECK(oct_map_key(&o, -1, 1) < 0 && oct_map_key(&o, 128, 1) < 0);
}

static void test_modifier_is_lo_freq_and_keyboard_amount_is_ordinary_again(void) {
    reset();
    CHECK(OCT_MOD_ID == 37);                             /* Osc B Lo Freq, one hand from Bank/Group */
    CHECK(oct_button(&o, 8, PRESS) == OCT_FORWARD);      /* the former modifier passes through */
    CHECK(oct_button(&o, BANK, PRESS) == OCT_FORWARD);
    CHECK(oct_button(&o, 8, RELEASE) == OCT_FORWARD);
    CHECK(oct_shift(&o) == 0);
}

int main(void) {
    test_zero_state_is_no_shift();
    test_bank_up_group_down_clamped_with_display();
    test_both_directions_together_reset_to_zero();
    test_tap_without_bank_or_group_is_replayed();
    test_held_modifier_shows_the_shift_and_is_no_tap();
    test_other_buttons_pass_through_when_not_held();
    test_keys_are_shifted_and_releases_use_the_press_time_shift();
    test_out_of_range_keys_are_silent_on_and_off();
    test_modifier_is_lo_freq_and_keyboard_amount_is_ordinary_again();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
