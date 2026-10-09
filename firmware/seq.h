/* The sequencer (docs/SPEC.md: "Seq — the sequencer"): the event list and the recorder (with
 * Back), the transport under the internal and the MIDI clock, the Chords renderer, and the
 * Arpeggiated renderer through the arp's chord source. Portable logic; the UI (arpui.c)
 * selects the generator and routes keys, buttons and ticks. Voices through platform.h:
 * generated notes release with plat_voice_off (flagged for the hold query), live notes
 * through the arp's live path. */
#ifndef SEQ_H
#define SEQ_H

#include <stdint.h>

#include "arp.h"

#define SEQ_STEPS 512                 /* capacity: timing steps in total (chords, rests, ties) */
#define SEQ_CHORD 10                  /* notes per chord */
#define SEQ_LOSS_TICKS 1000           /* 1 s without a MIDI clock releases the generated notes */

enum { SEQ_CHORDS = 0, SEQ_ARPEGGIATED = 1 };
enum { SEQ_FOR = 0, SEQ_BACK = 1, SEQ_PEND = 2, SEQ_ORDERS = 3 };

typedef struct {
    uint8_t  n;                       /* notes, 0 = a rest */
    uint8_t  note[SEQ_CHORD];
    uint8_t  vel[SEQ_CHORD];
    uint8_t  pad;
    uint16_t dur;                     /* timing steps: 1 + ties */
} seq_ev_t;

typedef struct {
    seq_ev_t ev[SEQ_STEPS];
    uint16_t len;                     /* events */
    uint16_t total;                   /* timing steps used: the sum of the durations */
    uint8_t  style, order, chord_beats;
    int8_t   transpose;
    uint8_t  beats_num, beats_den, swing;          /* the Seq's note value */
    uint8_t  swing_short;             /* internal clock: the current step is the pair's short one */
    uint8_t  pend;                    /* a note value change waits for the next event */
    uint8_t  pend_num, pend_den, pend_swing;
    uint8_t  rec, fresh;              /* recording; the old sequence stands until the first entry */
    uint8_t  playing;                 /* events are being played */
    uint8_t  armed;                   /* MIDI clock: started by A440 and not stopped since */
    uint8_t  paused;                  /* MIDI clock: a MIDI Stop, no Continue yet */
    uint8_t  restart;                 /* begin at event 1 at the next step boundary (a style change) */
    uint16_t pos;                     /* the event playing */
    uint16_t remain;                  /* Chords: timing steps left of the event, the current one included */
    int8_t   dir;                     /* Pnd direction */
    uint8_t  gate_open;
    uint8_t  snd_n;
    uint8_t  snd[SEQ_CHORD];          /* Chords: the generated notes sounding */
    uint32_t acc;                     /* internal step clock, as the arp's; the MIDI step grid is the arp's clock count */
    uint32_t chord_acc;               /* Arpeggiated, internal: bpm per tick, a boundary at 60000 x beats x dur */
    uint32_t chord_clk;               /* Arpeggiated, MIDI: clocks since the chord began */
    uint16_t loss;                    /* ticks since the last MIDI clock */
    uint8_t  rec_down[16];            /* record mode: keys of the open chord still down (bitmap) */
} seq_t;

void seq_init(seq_t *q);
int  seq_running(const seq_t *q);                        /* playing, or armed under MIDI clock (the LED) */

/* Transport. seq_start returns 0 when there is nothing to play. */
int  seq_start(seq_t *q, arp_t *a);
void seq_stop(seq_t *q, arp_t *a);
/* Every 1 ms, before arp_tick. Returns 1 when a chord was handed to the arp in this tick:
 * the set is the arp's step for this ms, so the caller skips arp_tick. */
int  seq_tick(seq_t *q, arp_t *a);
void seq_realtime(seq_t *q, arp_t *a, int byte);         /* an accepted F8 / FA / FB / FC, before arp_rt_apply */
void seq_all_notes_off(seq_t *q, arp_t *a);              /* CC 123-127: stop, disarm, keep the recording */
void seq_clear(seq_t *q, arp_t *a);                      /* stop, empty the sequence, transposition 0 */
/* The events were replaced from outside (sequence memory): playing, the new sequence takes
 * over at the next step boundary from event 1 (generated notes released then); stopped or
 * armed, the next start / grid step plays it. */
void seq_replaced(seq_t *q, arp_t *a);

/* Settings (session: not saved) */
void seq_set_style(seq_t *q, arp_t *a, int style);
void seq_set_order(seq_t *q, int order);
void seq_set_beats(seq_t *q, int num, int den);          /* the Seq's note value; from the next event while playing */
void seq_set_swing(seq_t *q, int on);
void seq_set_chord_beats(seq_t *q, int beats);           /* Arpeggiated: 1..16 */
void seq_set_transpose(seq_t *q, int semis);             /* from the next event / chord boundary */

/* Record mode. Notes sound through the arp's live path (the arp is off while recording) and
 * are recorded: held together, one chord; HOLD = rest or tie; Back undoes. Returns are the
 * timing steps recorded (seq_rec_note), 1 tie / 0 rest / -1 refused (seq_rec_rest_tie),
 * 1 undone / 0 nothing (seq_rec_back). */
void seq_rec_begin(seq_t *q, arp_t *a);
void seq_rec_end(seq_t *q);
int  seq_rec_note(seq_t *q, arp_t *a, int src, int note, int vel);
int  seq_rec_rest_tie(seq_t *q);
int  seq_rec_back(seq_t *q);
int  seq_rec_count(const seq_t *q);                      /* the readout: 0 while the old sequence stands */

#endif
