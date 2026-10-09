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
#include "flash.h"
#include "rate.h"
#include "seq.h"
#include "seqmem.h"

#define ARPUI_A440 0x0F
#define ARPUI_GLOBALS 0x0D
#define ARPUI_GROUP 0x20
#define ARPUI_BANK 0x28
#define ARPUI_LED_A440 0x24
#define ARPUI_POT_GLIDE 0x16
#define ARPUI_POT_DECAY 0x11        /* A440 + Amp Decay = the selected generator's gate */
#define ARPUI_POTS 28               /* pot ids 0..27; A440 + any other knob = its id readout (P N) */
#define ARPUI_KEYB 0x08             /* A440 + Keyboard (filter Keyboard Amount) = generator ArP / SEq */
#define ARPUI_VELOCITY 0x0B         /* A440 + Velocity = tap tempo (beside A440: one hand) */
#define ARPUI_AFTERTOUCH 0x0A       /* A440 + Aftertouch = the Seq's chord length (Arpeggiated style) */
#define ARPUI_UNISON 0x19           /* A440 + Unison = the Seq's style CHd / ArP */
#define ARPUI_CHORDS 5              /* chord lengths: Qtr, Half, Whole, 2 bars, 4 bars */
#define ARPUI_TUNE 0x0C             /* A440 + Tune = seq record mode on / off */
#define ARPUI_HOLD 0x0E             /* A440 + HOLD = stock tuning tone (generator stopped); in record mode: rest / tie */
#define ARPUI_SYNC 0x1A             /* A440 + Sync (Osc A Sync, panel id 26) = the read-only flash diagnostic */
#define ARPUI_FLASH_RESULTS 3       /* its results shown in turn: size, area 1, area 2 */
#define ARPUI_BLINK_MS 500          /* record mode: A440 LED on / off time */
#define ARPUI_LED_FIX_MS 100        /* after a replayed A440 press: re-assert the LED (stock's late LED-off) */
#define ARPUI_TAP_MAX_MS 2000       /* a longer gap starts a new tap series */
#define ARPUI_TAP_IVS 4             /* intervals averaged */
/* patch slots: V = on/off + 2 mode + 10 (octaves - 1) + 40 note value index + 520 gate index (0..10399),
 * 93 = V mod 128, 94 = 1 + V div 128 (0 = no arp data) */
#define ARPUI_PARAM_PACK 93
#define ARPUI_PARAM_OCT 94
#define ARPUI_PATCH_HI_MAX 82       /* 94 above this is not arp data */
#define ARPUI_BOOT_TICKS 3000       /* kill-switch window after power-on (3 s; the panel link comes up late) */
#define ARPUI_MIDDLE_C 60           /* the transposition command's zero (the key's number before the octave shift) */
#define ARPUI_RAW_NONE 0xFFFF       /* the pot has not reported since power-on */

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
    uint8_t  tempo_caught;            /* this A440 hold: the glide knob has reached the tempo and sets it */
    uint8_t  gate;                    /* the Arp's gate, index 0..ARP_GATES-1 (saved with the program) */
    uint8_t  pad;
    uint16_t glide_raw;               /* the glide pot's last raw value, tempo or glide (ARPUI_RAW_NONE: unknown) */
    uint8_t  flash_msgs;              /* flash diagnostic readings still to show after the current message */
    uint8_t  flash_next;              /* the reading the next A440 + Sync recalls (0 size, 1 area 1, 2 area 2) */
    uint8_t  flash_prog;              /* the progress readout last shown (64 KB units) */
    uint8_t  seqload;                 /* a sequence block to read once the diagnostic is idle: slot + 1, 0 = none */
    flash_t *flash;                   /* the diagnostic's state (set by the glue / harness) */
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
/* The synth's own hold is suspended ("HOLD while the arp is on"): arp on, record mode, or
 * the sequencer running with SEq selected (the engine then sustains live notes itself). */
int  arpui_suspended(const arpui_t *u, const arp_t *a, const seq_t *q);
/* Pot hooks: raw store (pot, raw 0..1023) and change post. Return 1 if consumed: while A440
 * is held every knob — Glide Rate (tempo), Amp Decay (the selected generator's gate), any
 * other the knob id readout. */
int  arpui_pot_store(arpui_t *u, arp_t *a, seq_t *q, int pot, int raw);
int  arpui_pot_change(arpui_t *u, arp_t *a, seq_t *q, int pot);
/* Every 1 ms: display revert (to `r N` in record mode), LED (the selected generator;
 * blinking in record mode), kill-switch window, tap-tempo clock. */
void arpui_tick(arpui_t *u, arp_t *a, seq_t *q);
/* A program was loaded: apply its arp settings from the patch slots ("Patch memory") and,
 * for a user program with a sequence block of its own, the saved sequence and its settings
 * ("Sequence memory"); the sequencer is never stopped. */
void arpui_program_loaded(arpui_t *u, arp_t *a, seq_t *q);
/* Stock has just stored a program from the panel (Prophet5 AO task): a user program gets
 * its sequence block written — the live sequence and settings, or "no sequence". */
void arpui_program_stored(arpui_t *u, arp_t *a, seq_t *q, int factory, int bank, int group, int prog);

#endif
