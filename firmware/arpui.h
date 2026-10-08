/* Arp UI (docs/SPEC.md: "Arp engine" — Controls, Display, Robustness; "Seq" — record
 * mode): the A440 button and its combos, seq record mode (A440 + Tune; HOLD / pedal = rest
 * or tie; the `r N` readout and the blinking LED), Glide Rate as tempo, display messages
 * with the 1.5 s revert, the A440 LED and the power-on kill switch. Portable logic over
 * arp.c, rate.c and disp.c; everything external goes through platform.h. The glue decides
 * task context. */
#ifndef ARPUI_H
#define ARPUI_H

#include <stdint.h>

#include "arp.h"
#include "disp.h"
#include "rate.h"

#define ARPUI_A440 0x0F
#define ARPUI_GLOBALS 0x0D
#define ARPUI_GROUP 0x20
#define ARPUI_BANK 0x28
#define ARPUI_LED_A440 0x24
#define ARPUI_POT_GLIDE 0x16
#define ARPUI_VELOCITY 0x0B         /* A440 + Velocity = tap tempo (beside A440: one hand) */
#define ARPUI_TUNE 0x0C             /* A440 + Tune = seq record mode on / off */
#define ARPUI_HOLD 0x0E             /* A440 + HOLD = stock tuning tone (arp off); in record mode: rest / tie */
#define ARPUI_BLINK_MS 500            /* record mode: A440 LED on / off time */
#define ARPUI_LED_FIX_MS 100          /* after a replayed A440 press: re-assert the arp LED (stock's late LED-off) */
#define ARPUI_TAP_MAX_MS 2000         /* a longer gap starts a new tap series */
#define ARPUI_TAP_IVS 4               /* intervals averaged */
#define ARPUI_PARAM_PACK 93           /* patch slot: n * 10 + mode * 2 + on/off (n: Prophet-6 position, or long value 0..2) */
#define ARPUI_PACK_MAX 99             /* larger values of 93 are not arp data (29 with the long flag) */
#define ARPUI_PACK_MAX_LONG 29
#define ARPUI_PARAM_OCT 94            /* patch slot: octaves 1..4 + 4 if the note value is a long one, 0 = no arp data */
#define ARPUI_BOOT_TICKS 3000         /* kill-switch window after power-on (3 s; the panel link comes up late) */

typedef struct {
    rate_t   rate;
    disp_t   disp;
    uint8_t  a440_held;
    uint8_t  a440_used;               /* this hold was a modifier or a recording: no toggle */
    uint8_t  led_on;                  /* A440 LED as last set */
    uint8_t  kill;                    /* kill switch engaged: every hook passes to stock */
    uint16_t boot_ticks;              /* ticks seen since power-on, saturating at the window */
    uint8_t  a440_seen;               /* an A440 press event has been reported since power-on */
    uint8_t  rec;                     /* seq record mode */
    uint8_t  swallow[8];              /* bitmap of buttons whose press was consumed: consume the release too */
    uint32_t ms;                      /* free-running 1 ms tick counter */
    uint32_t tap_last;                /* ms of the last tempo tap */
    uint16_t tap_iv[ARPUI_TAP_IVS];   /* the series' most recent intervals (ms), newest first */
    uint8_t  tap_on;                  /* a tap series is running */
    uint8_t  tap_n;                   /* intervals held, 0..ARPUI_TAP_IVS */
    uint16_t rec_ms;                  /* record mode: ms into the LED blink cycle */
    uint8_t  tone_pending;            /* A440 + HOLD pressed with the arp off: toggle the tone on HOLD's release */
    uint8_t  led_fix;                 /* ms until the arp LED is asserted again after a replayed A440 press */
} arpui_t;

void arpui_init(arpui_t *u);
/* Panel button (id, value 1 press / 2 release / 3 held). Returns 1 if consumed. */
int  arpui_button(arpui_t *u, arp_t *a, int id, int value);
/* A note for the arp (local, after octave shift, or MIDI). Recorded in record mode. */
void arpui_note(arpui_t *u, arp_t *a, int src, int note, int vel);
/* The stock merged HOLD state (button latch | pedal). In record mode an on-transition is a
 * rest or tie (the pedal's way in); it is the arp's hold either way. */
void arpui_hold(arpui_t *u, arp_t *a, int on);
/* The synth's own hold is suspended ("HOLD while the arp is on"): arp on, or record mode. */
int  arpui_suspended(const arpui_t *u, const arp_t *a);
/* Pot hooks: raw store (pot, raw 0..1023) and change post. Return 1 if consumed. */
int  arpui_pot_store(arpui_t *u, arp_t *a, int pot, int raw);
int  arpui_pot_change(arpui_t *u, arp_t *a, int pot);
/* Every 1 ms: display revert (to `r N` in record mode), LED (blinking in record mode),
 * kill-switch window, tap-tempo clock. */
void arpui_tick(arpui_t *u, arp_t *a);
/* A program was loaded: apply its arp settings from the patch slots ("Patch memory"). */
void arpui_program_loaded(arpui_t *u, arp_t *a);

#endif
