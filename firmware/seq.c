#include "seq.h"
#include "platform.h"

enum { ID_A440 = 0x0F, ID_PROGRAM1 = 0, ID_PROGRAM4 = 3, ID_PROGRAM6 = 5, PRESS = 1, RELEASE = 2 };
enum { SRC_LOCAL = 1, SRC_MIDI = 2 };
#define SILENT 0xFF                      /* sounding[]: dummy fed but out of range */

static void zero(void *p, unsigned n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}

static int bit_test(const uint32_t *map, int i) { return (map[i >> 5] >> (i & 31)) & 1; }
static void bit_set(uint32_t *map, int i) { map[i >> 5] |= 1u << (i & 31); }
static void bit_clear(uint32_t *map, int i) { map[i >> 5] &= ~(1u << (i & 31)); }

static int popcount_map(const uint32_t *map, int words)
{
    int n = 0;
    while (words--) {
        uint32_t v = *map++;
        while (v) {
            v &= v - 1;
            n++;
        }
    }
    return n;
}

static int seq_keys_down(const seq_t *s) { return popcount_map(&s->keys[0][0], 8); }
static int any_fed(const seq_t *s) { return popcount_map(s->fed, 4) != 0; }

/* dummy d = step k + count * octave m; returns k and sets *m (count > 0, d < count*octaves) */
static int split_dummy(const seq_t *s, int d, int *m)
{
    int k = d, mm = 0;
    if (s->count == 0) {                 /* defensive: never reached with a fed dummy */
        *m = 0;
        return 0;
    }
    while (k >= s->count) {
        k -= s->count;
        mm++;
    }
    *m = mm;
    return k;
}

static void set_desired_all(seq_t *s)
{
    int n = s->count * s->octaves;
    zero(s->desired, sizeof s->desired);
    if (n > 128)
        n = 128;
    for (int d = 0; d < n; d++)
        bit_set(s->desired, d);
}

static void stop_playback(seq_t *s)
{
    zero(s->desired, sizeof s->desired);
    s->playing = 0;
}

static void start_playback(seq_t *s)
{
    set_desired_all(s);
    s->playing = 1;
}

/* V5 forgets everything it holds; so must we. */
static void clear_v5(seq_t *s)
{
    plat_v5_clear();
    zero(s->fed, sizeof s->fed);
    stop_playback(s);
}

static void synth_button(int id)
{
    if (plat_globals_active())
        return;
    plat_v5_button(id, PRESS);
    plat_v5_button(id, RELEASE);
}

static void start_recording(seq_t *s)
{
    if (!s->active) {
        int o = plat_v5_octaves();
        s->saved_v5_octaves = (uint8_t)(o < 1 ? 1 : o > 4 ? 4 : o);
        s->octaves = s->saved_v5_octaves;
    }
    stop_playback(s);                    /* old dummies are released by the ticks */
    s->recording = 1;
    s->active = 0;
    s->count = 0;
    synth_button(ID_PROGRAM1);           /* V5: one octave, and "A440 used" (no toggle) */
}

void seq_init(seq_t *s)
{
    zero(s, sizeof *s);
}

int seq_note(seq_t *s, int src, int note, int vel)
{
    int idx = src == SRC_MIDI;
    int on = vel > 0;
    int before;

    if (note < 0 || note > 127)
        return 0;
    before = seq_keys_down(s);
    if (on)
        bit_set(s->keys[idx], note);
    else
        bit_clear(s->keys[idx], note);

    if (s->a440_held && on) {
        if (!s->recording)
            start_recording(s);
        if (s->count < SEQ_MAX_STEPS) {
            if (s->count == 0)
                s->root = (uint8_t)note;
            s->step_note[s->count] = (uint8_t)note;
            s->step_vel[s->count] = (uint8_t)vel;
            s->count++;
            plat_display_int(s->count);
        }
        bit_set(s->forwarded[idx], note);
        return 0;                                 /* recorded notes sound normally */
    }
    if (s->recording) {                           /* releases while recording */
        bit_clear(s->forwarded[idx], note);
        return 0;
    }
    if (!s->active)
        return 0;
    if (!on && bit_test(s->forwarded[idx], note)) {   /* release we still owe V5 */
        bit_clear(s->forwarded[idx], note);
        if (!s->hold && seq_keys_down(s) == 0)
            stop_playback(s);
        return 0;
    }
    if (!plat_arp_enabled()) {                    /* no arp: keys play normally */
        if (on)
            bit_set(s->forwarded[idx], note);
        else
            bit_clear(s->forwarded[idx], note);
        return 0;
    }
    if (on) {
        if (before == 0) {                        /* first key after all up: fresh start */
            if (s->playing)
                clear_v5(s);
            start_playback(s);
        }
        s->transpose = (int8_t)(note - s->root);
        s->trigger_valid = 1;
        return 1;
    }
    if (!s->hold && seq_keys_down(s) == 0)
        stop_playback(s);
    return 1;
}

int seq_button(seq_t *s, int id, int value)
{
    if (id == ID_A440) {
        s->a440_held = value == PRESS;
        if (value == RELEASE && s->recording) {
            s->recording = 0;
            s->active = s->count > 0;
            if (s->active)
                clear_v5(s);                      /* drop what the audible recording left in the arp */
        }
        return 0;
    }
    if (!s->a440_held)
        return 0;
    if (id == ID_PROGRAM6) {
        if (value == PRESS && s->active) {
            clear_v5(s);
            s->active = 0;
            s->count = 0;
            synth_button(s->saved_v5_octaves - 1);   /* restore the arp's own octave setting */
        }
        return 0;                                 /* V5 marks A440 as used */
    }
    if (id >= ID_PROGRAM1 && id <= ID_PROGRAM4 && (s->active || s->recording)) {
        if (value == PRESS) {
            s->octaves = (uint8_t)(id + 1);
            plat_display_int(s->octaves);
            if (s->playing)
                set_desired_all(s);
        }
        return 1;                                 /* V5 stays at one octave */
    }
    return 0;
}

void seq_hold(seq_t *s, int on)
{
    s->hold = on ? 1 : 0;
    if (!s->hold && seq_keys_down(s) == 0)
        stop_playback(s);
}

void seq_all_notes_off(seq_t *s)
{
    zero(s->fed, sizeof s->fed);
    stop_playback(s);
}

void seq_tick(seq_t *s)
{
    int budget = SEQ_FEED_BUDGET;

    if (!plat_arp_enabled()) {
        if (s->playing || any_fed(s))
            clear_v5(s);
        return;
    }
    for (int d = 0; d < 128 && budget > 0; d++) {
        int want = bit_test(s->desired, d);
        int have = bit_test(s->fed, d);
        if (want && !have) {
            int m;
            plat_v5_note(SRC_LOCAL, d, s->step_vel[split_dummy(s, d, &m)]);
            bit_set(s->fed, d);
            budget--;
        } else if (!want && have) {
            plat_v5_note(SRC_LOCAL, d, 0);
            bit_clear(s->fed, d);
            budget--;
        }
    }
}

void seq_output(seq_t *s, int ctx, int src, int on, int note, int vel)
{
    if (note < 0 || note > 127) {
        plat_orig_out(ctx, src, on, note, vel);
        return;
    }
    if (on) {
        int k, m, pitch;
        if (!bit_test(s->fed, note)) {            /* a real note passing through */
            plat_orig_out(ctx, src, 1, note, vel);
            return;
        }
        if (!s->playing || s->count == 0) {       /* stale dummy while stopping: keep quiet */
            s->sounding[note] = SILENT;
            return;
        }
        k = split_dummy(s, note, &m);
        pitch = s->step_note[k] + 12 * m + s->transpose;
        if (pitch < 0 || pitch > 127) {
            s->sounding[note] = SILENT;
            return;
        }
        s->sounding[note] = (uint8_t)(pitch + 1);
        plat_orig_out(ctx, src, 1, pitch, s->step_vel[k]);
        return;
    }
    if (s->sounding[note] == SILENT) {
        s->sounding[note] = 0;
        return;
    }
    if (s->sounding[note]) {
        int pitch = s->sounding[note] - 1;
        s->sounding[note] = 0;
        plat_orig_out(ctx, src, 0, pitch, vel);
        return;
    }
    plat_orig_out(ctx, src, 0, note, vel);
}
