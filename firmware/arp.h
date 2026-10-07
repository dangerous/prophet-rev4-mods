/* Arp engine core (docs/SPEC.md: "Arp engine"): note pool,
 * pattern order, internal/MIDI clock, HOLD latch and re-latch, start rule, seq playback.
 * Portable logic; voices are reached through platform.h (plat_voice_on/off = stock
 * note_on/note_off). The UI (buttons, pot, display, LED) lives in arpui.c. */
#ifndef ARP_H
#define ARP_H

#include <stdint.h>

#define ARP_NONE 0xFF
#define ARP_SEQ_MAX 32
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
    uint8_t  running;                 /* MIDI transport: Start/Continue seen and no Stop since */
    uint8_t  port;                    /* MIDI clock port lock, ARP_NONE = none */
    uint8_t  sounding;                /* step note currently sounding, ARP_NONE = none */
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
    /* seq: recorded steps, played transposed so step 0 lands on the trigger key */
    uint8_t  seq_len, seq_rec, seq_fresh, seq_trigger;
    uint8_t  seq_note[ARP_SEQ_MAX], seq_vel[ARP_SEQ_MAX];
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

/* Seq recording: notes played while recording sound directly and are appended (up to
 * ARP_SEQ_MAX); end with at least one step makes the sequence the base order. */
void arp_seq_record(arp_t *a, int on);
int  arp_seq_record_note(arp_t *a, int src, int note, int vel);   /* returns steps recorded */
void arp_seq_clear(arp_t *a);

/* Queries for the UI */
int  arp_pool_count(const arp_t *a);

#endif
