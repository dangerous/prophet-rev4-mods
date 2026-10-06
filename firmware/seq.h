/* Seq — step-recorded sequence (docs/SPEC.md: "Seq"). Portable logic; the wrapper's hooks
 * call these and act on the return values. Uses platform.h for everything external. */
#ifndef SEQ_H
#define SEQ_H

#include <stdint.h>

#define SEQ_MAX_STEPS 32
#define SEQ_FEED_BUDGET 16          /* dummy events fed to V5 per tick */
#define SEQ_NO_PITCH 0xFF

typedef struct {
    /* recording */
    uint8_t  a440_held;
    uint8_t  recording;
    uint8_t  active;                /* a sequence exists */
    uint8_t  count;                 /* steps recorded */
    uint8_t  root;                  /* first recorded pitch */
    uint8_t  octaves;               /* 1..4, spans the whole sequence */
    uint8_t  saved_v5_octaves;      /* V5's setting when seq mode was entered */
    uint8_t  hold;
    /* playback */
    uint8_t  playing;               /* dummies are (being) fed */
    uint8_t  trigger_valid;
    int8_t   transpose;
    uint8_t  pad;
    uint8_t  step_note[SEQ_MAX_STEPS];
    uint8_t  step_vel[SEQ_MAX_STEPS];
    uint32_t keys[2][4];            /* keys down per source (1 local, 2 MIDI) */
    uint32_t forwarded[2][4];       /* real notes we let through to V5 and still owe a release */
    uint32_t desired[4];            /* dummies that should be held in V5 */
    uint32_t fed[4];                /* dummies currently held in V5 */
    uint8_t  sounding[128];         /* dummy -> real pitch + 1 sounding, 0 none, 0xFF silent */
} seq_t;

void seq_init(seq_t *s);
/* Note event from a hook. Returns 1 if consumed (caller must not forward it), 0 to forward. */
int  seq_note(seq_t *s, int src, int note, int vel);
/* Button event from the panel hook. Returns 1 if consumed (not forwarded to V5), 0 to forward. */
int  seq_button(seq_t *s, int id, int value);
void seq_hold(seq_t *s, int on);
void seq_tick(seq_t *s);                      /* once per keyboard-scan tick, before V5's tick */
void seq_all_notes_off(seq_t *s);             /* CC 123 observed */
/* Installed as V5's note output: substitutes dummies, passes everything else through. */
void seq_output(seq_t *s, int ctx, int src, int on, int note, int vel);

#endif
