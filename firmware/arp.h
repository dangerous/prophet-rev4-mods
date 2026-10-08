/* Arp engine core (docs/SPEC.md: "Arp engine"): note pool,
 * pattern order, internal/MIDI clock, HOLD latch and re-latch, start rule, seq playback.
 * Portable logic; voices are reached through platform.h (plat_voice_on/off = stock
 * note_on/note_off). The UI (buttons, pot, display, LED) lives in arpui.c. */
#ifndef ARP_H
#define ARP_H

#include <stdint.h>

#define ARP_NONE 0xFF
#define ARP_SEQ_MAX 64                /* seq: steps */
#define ARP_SEQ_CHORD 10              /* seq: notes per step */
#define ARP_SEQ_TIE_MAX 64            /* seq: a step's length in arp steps */
#define ARP_ASG_MAX 32                /* Assign: entries in note-on order */
#define ARP_LOSS_TICKS 1000           /* 1 s without a MIDI clock releases the sounding note */
#define ARP_PPQN 24
#define ARP_BEAT_IVS 24               /* clock intervals measured for the followed BPM */

enum { ARP_UP = 0, ARP_DOWN = 1, ARP_UPDOWN = 2, ARP_RANDOM = 3, ARP_ASSIGN = 4, ARP_MODES = 5 };
enum { ARP_SRC_LOCAL = 1, ARP_SRC_MIDI = 2 };

typedef struct {
    uint8_t  held[128];               /* velocity while the key / MIDI note is down (0 = up) */
    uint8_t  latched[128];            /* velocity kept by HOLD after release (0 = not latched) */
    uint8_t  direct[128];             /* notes sounding directly through stock while the arp is off */
    uint8_t  enabled, hold, mode, octaves, ext;
    uint8_t  accomp;                  /* accompanying: direct notes survive an enable ("Accompany") */
    uint8_t  running;                 /* MIDI transport: Start/Continue seen and no Stop since */
    uint8_t  port;                    /* MIDI clock port lock, ARP_NONE = none */
    uint8_t  sounding;                /* first step note currently sounding, ARP_NONE = none */
    uint8_t  snd_n;                   /* step notes sounding (a seq chord step has several) */
    uint8_t  snd[ARP_SEQ_CHORD];
    uint8_t  idx, pass;               /* pattern position: entry in the base order, octave pass */
    uint8_t  last_base;               /* base pitch of the last step (pool order is pitch-anchored) */
    uint8_t  sounding_base;           /* base pitch of the sounding step note */
    int8_t   dir;                     /* Up/Down direction, +1 or -1 */
    uint8_t  gate_open;               /* step note not yet released by the gate */
    uint8_t  at_start;                /* pattern position is "before the first step" */
    uint16_t bpm;
    uint8_t  beats_num, beats_den;    /* beats per step (per pair of steps with swing), e.g. 1/2 for an eighth */
    uint8_t  swing;                   /* 2:1 swing: steps alternate 2/3 and 1/3 of the pair */
    uint8_t  swing_short;             /* internal clock: the current step is the pair's short one */
    uint32_t acc;                     /* internal clock accumulator, see arp.c */
    uint32_t clocks;                  /* MIDI clocks counted since Start */
    uint16_t loss;                    /* ticks since the last MIDI clock */
    uint32_t rng;                     /* xorshift32 state for Random */
    /* Syn: the BPM follows the clock, measured over the last ARP_BEAT_IVS clock intervals */
    uint16_t clk_iv[ARP_BEAT_IVS];    /* ring of intervals (ticks) */
    uint8_t  clk_n, clk_pos;          /* intervals held, next slot */
    uint8_t  clk_prev;                /* a clock has been seen since the window restarted */
    /* seq: recorded steps — a chord of up to ARP_SEQ_CHORD notes or a rest (seq_n 0), each
     * seq_dur long — played transposed so the lowest note of the first sounding step lands
     * on the trigger key. Std: one step per arp step (seq_dur in arp steps). ArP (seq_arp):
     * each step is a chord held for chord_beats x seq_dur while the arp runs over its notes. */
    uint8_t  seq_len, seq_rec, seq_fresh, seq_trigger;
    uint8_t  seq_hold;                /* Std: arp steps the sounding seq step still has to run (its length - 1) */
    uint8_t  seq_arp;                 /* playback mode: 0 Std, 1 ArP */
    uint8_t  chord_beats;             /* ArP: chord length in beats (1, 2, 4, 8, 16) */
    uint8_t  chord_pos;               /* ArP: the chord (step) being played */
    uint8_t  chord_restart;           /* ArP: the chord clock starts afresh at the next step (sequence (re)start) */
    uint32_t chord_acc;               /* ArP, internal clock: bpm per tick, a boundary at 60000 x beats x dur */
    uint32_t chord_clk;               /* ArP, MIDI clock: clocks since the chord's first step */
    uint8_t  seq_n[ARP_SEQ_MAX], seq_dur[ARP_SEQ_MAX];
    uint8_t  seq_note[ARP_SEQ_MAX][ARP_SEQ_CHORD], seq_vel[ARP_SEQ_MAX][ARP_SEQ_CHORD];
    uint8_t  rec_down[16];            /* record mode: keys recorded into the open step and still down (bitmap) */
    /* Assign: the pool's notes in entered order, duplicates allowed */
    uint8_t  asg_len;
    uint8_t  asg_stay;                /* the entry at idx replaced the one just played: do not advance */
    uint8_t  asg_note[ARP_ASG_MAX], asg_vel[ARP_ASG_MAX];
    uint8_t  order[128], ovel[128];   /* step() scratch: the base order and its velocities */
} arp_t;

void arp_init(arp_t *a);                                 /* power-up defaults, arp off */

/* Events (all from the engine's own task — the glue queues the others). vel 0 = note off. */
void arp_note(arp_t *a, int src, int note, int vel);
void arp_hold(arp_t *a, int on);                         /* stock merged HOLD state */
void arp_all_notes_off(arp_t *a);                        /* MIDI CC 123-127 */
void arp_realtime(arp_t *a, int byte, int port);         /* F8 / FA / FB / FC */
void arp_tick(arp_t *a);                                 /* every 1 ms */

/* Settings */
void arp_enable(arp_t *a, int on);
void arp_set_mode(arp_t *a, int mode);
void arp_set_octaves(arp_t *a, int n);                   /* 1..4 */
void arp_set_bpm(arp_t *a, int bpm);                     /* 40..300 */
void arp_set_beats(arp_t *a, int num, int den);          /* beats per step */
void arp_set_swing(arp_t *a, int on);                    /* 16S / 8S */
void arp_set_ext(arp_t *a, int ext);                     /* clock source: 0 internal, 1 MIDI */
void arp_set_seq_arp(arp_t *a, int on);                  /* sequence playback: 0 Std (chords as blocks), 1 ArP */
void arp_set_acc(arp_t *a, int on);                      /* accompanying ("Accompany") */
/* A note played over the arp: straight to a voice, tracked so its release matches; never
 * the pool's. vel 0 = off. */
void arp_play_direct(arp_t *a, int src, int note, int vel);
void arp_set_chord_beats(arp_t *a, int beats);           /* ArP chord length in beats, 1..16 */

/* Seq record mode (docs/SPEC.md: "Seq"). Entering releases everything sounding and empties
 * the pool; notes then sound directly and are recorded — held together, one chord step;
 * after every key is released, a new step — until leaving, which releases what still
 * sounds. The old sequence stands until the first step is recorded. */
void arp_seq_record(arp_t *a, int on);
int  arp_seq_record_note(arp_t *a, int src, int note, int vel);   /* returns steps recorded */
/* HOLD / pedal while recording: a key of the open step down = tie (returns 1), else a rest
 * (returns 0); -1 when not recording. */
int  arp_seq_rest_tie(arp_t *a);
void arp_seq_clear(arp_t *a);

/* Queries for the UI */
int  arp_pool_count(const arp_t *a);

#endif
