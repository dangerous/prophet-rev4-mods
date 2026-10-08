/* Arp UI (docs/SPEC.md: "Arp engine" — Controls, Display, Robustness; "Seq" — generator
 * selection, transport, record mode, Back, transposition): the A440 button and its combos
 * acting on the selected generator (ArP: arp.c; SEq: seq.c), seq record mode (A440 + Tune;
 * HOLD / pedal = rest or tie; Group alone = Back; the `r N` readout and the blinking LED),
 * Glide Rate as tempo, display messages with the 1.5 s revert, the A440 LED and the
 * power-on kill switch. Portable logic over arp.c, seq.c, rate.c and disp.c; everything
 * external goes through platform.h. The glue decides task context. */
#ifndef ARPUI_H
#define ARPUI_H

#include <stdint.h>

#include "arp.h"
#include "disp.h"
#include "rate.h"
#include "seq.h"

#define ARPUI_A440 0x0F
#define ARPUI_GLOBALS 0x0D
#define ARPUI_GROUP 0x20
#define ARPUI_BANK 0x28
#define ARPUI_LED_A440 0x24
#define ARPUI_POT_GLIDE 0x16
#define ARPUI_KEYB 0x08             /* A440 + Keyboard (filter Keyboard Amount) = generator ArP / SEq */
#define ARPUI_VELOCITY 0x0B         /* A440 + Velocity = tap tempo (beside A440: one hand) */
#define ARPUI_AFTERTOUCH 0x0A       /* A440 + Aftertouch = the Seq's chord length (Arpeggiated style) */
#define ARPUI_UNISON 0x19           /* A440 + Unison = the Seq's style CHd / ArP */
#define ARPUI_CHORDS 5              /* chord lengths: Qtr, Half, Whole, 2 bars, 4 bars */
#define ARPUI_TUNE 0x0C             /* A440 + Tune = seq record mode on / off */
#define ARPUI_HOLD 0x0E             /* A440 + HOLD = stock tuning tone (generator stopped); in record mode: rest / tie */
#define ARPUI_BLINK_MS 500          /* record mode: A440 LED on / off time */
#define ARPUI_LED_FIX_MS 100        /* after a replayed A440 press: re-assert the LED (stock's late LED-off) */
#define ARPUI_TAP_MAX_MS 2000       /* a longer gap starts a new tap series */
#define ARPUI_TAP_IVS 4             /* intervals averaged */
#define ARPUI_PARAM_PACK 93         /* patch slot: n * 10 + mode * 2 + on/off (n: Prophet-6 position, or long value 0..2) */
#define ARPUI_PACK_MAX 99           /* larger values of 93 are not arp data (29 with the long flag) */
#define ARPUI_PACK_MAX_LONG 29
#define ARPUI_PARAM_OCT 94          /* patch slot: octaves 1..4 + 4 L (long note value); 1.2.0's 8 C + 40 M accepted and ignored; 0 = no arp data */
#define ARPUI_OCT_MAX 80
#define ARPUI_BOOT_TICKS 3000       /* kill-switch window after power-on (3 s; the panel link comes up late) */
#define ARPUI_MIDDLE_C 60           /* the transposition command's zero (the key's number before the octave shift) */

enum { ARPUI_GEN_ARP = 0, ARPUI_GEN_SEQ = 1 };

typedef struct {
    rate_t   rate;                    /* the Arp's note value (saved with the program) */
    rate_t   seq_rate;                /* the Seq's note value (session) */
    disp_t   disp;
    uint8_t  a440_held;
    uint8_t  a440_used;               /* this hold was a modifier or a recording: no toggle */
    uint8_t  led_on;                  /* A440 LED as last set */
    uint8_t  kill;                    /* kill switch engaged: every hook passes to stock */
    uint16_t boot_ticks;              /* ticks seen since power-on, saturating at the window */
    uint8_t  a440_seen;               /* an A440 press event has been reported since power-on */
    uint8_t  rec;                     /* seq record mode */
    uint8_t  gen;                     /* the selected generator, ARPUI_GEN_* */
    uint8_t  cmd_key;                 /* the transposition command key still down, ARP_NONE = none */
    uint8_t  swallow[8];              /* bitmap of buttons whose press was consumed: consume the release too */
    uint32_t ms;                      /* free-running 1 ms tick counter */
    uint32_t tap_last;                /* ms of the last tempo tap */
    uint16_t tap_iv[ARPUI_TAP_IVS];   /* the series' most recent intervals (ms), newest first */
    uint8_t  tap_on;                  /* a tap series is running */
    uint8_t  tap_n;                   /* intervals held, 0..ARPUI_TAP_IVS */
    uint16_t rec_ms;                  /* record mode: ms into the LED blink cycle */
    uint8_t  tone_pending;            /* A440 + HOLD pressed with the generator stopped: toggle the tone on HOLD's release */
    uint8_t  led_fix;                 /* ms until the LED is asserted again after a replayed A440 press */
    uint8_t  hold_arp, hold_stock;    /* the two HOLD latches: the one not in use is remembered here */
} arpui_t;

void arpui_init(arpui_t *u);
/* Panel button (id, value 1 press / 2 release / 3 held). Returns 1 if consumed. */
int  arpui_button(arpui_t *u, arp_t *a, seq_t *q, int id, int value);
/* A local key as the keyboard reports it, before the octave shift. Returns 1 when it is the
 * sequence transposition command (A440 held and not yet used, SEq selected, not recording)
 * or the release of that key: the glue drops it — no sound, no shift. */
int  arpui_key(arpui_t *u, arp_t *a, seq_t *q, int note, int vel);
/* A note (local, after the octave shift, or MIDI): recorded in record mode; live with SEq
 * selected (the arp's direct path); the arp's with ArP. */
void arpui_note(arpui_t *u, arp_t *a, seq_t *q, int src, int note, int vel);
/* The stock merged HOLD state (button latch | pedal). In record mode an on-transition is a
 * rest or tie (the pedal's way in); it is the arp's hold either way. */
void arpui_hold(arpui_t *u, arp_t *a, seq_t *q, int on);
/* MIDI CC 123-127: the sequencer stops and disarms, the arp's pool empties. */
void arpui_all_notes_off(arpui_t *u, arp_t *a, seq_t *q);
/* A MIDI realtime byte (F8 / FA / FB / FC) on a port: the arp decides, the sequencer sees an
 * accepted byte first. */
void arpui_realtime(arpui_t *u, arp_t *a, seq_t *q, int byte, int port);
/* The synth's own hold is suspended ("HOLD while the arp is on"): arp on, or record mode. */
int  arpui_suspended(const arpui_t *u, const arp_t *a);
/* Pot hooks: raw store (pot, raw 0..1023) and change post. Return 1 if consumed. */
int  arpui_pot_store(arpui_t *u, arp_t *a, int pot, int raw);
int  arpui_pot_change(arpui_t *u, arp_t *a, int pot);
/* Every 1 ms: display revert (to `r N` in record mode), LED (the selected generator;
 * blinking in record mode), kill-switch window, tap-tempo clock. */
void arpui_tick(arpui_t *u, arp_t *a, seq_t *q);
/* A program was loaded: apply its arp settings from the patch slots ("Patch memory"); the
 * sequencer keeps its recording and stops. */
void arpui_program_loaded(arpui_t *u, arp_t *a, seq_t *q);

#endif
