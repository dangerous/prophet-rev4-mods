/* Wrapper glue: the functions the patched stock BL/word sites land on, plus the platform
 * calls the portable logic (relatch.c, seq.c, rate.c) makes into V5 and the stock OS.
 * Built freestanding for thumbv7a (Cortex-A5) as a single translation unit at
 * WRAPPER_BASE (tools/fw.py).
 *
 * Every hook does its bookkeeping and then calls the V5 entry point the stock code used
 * to call, so the arp sees exactly the events it saw before, in the same order. All
 * external addresses come from the v5_iface table (v5_iface.h). */
#include <stdint.h>

#include "platform.h"
#include "v5_iface.h"
#include "relatch.c"
#include "seq.c"
#include "rate.c"
#include "oct.c"
#include "vhold.c"
#include "disp.c"

const volatile uint32_t v5_iface[IF_COUNT] = {
    [IF_LOCAL_NOTE] = 0x20088C51u,
    [IF_MIDI_NOTE_ON] = 0x20088CFDu,
    [IF_MIDI_NOTE_OFF] = 0x20088D2Du,
    [IF_ALL_NOTES_OFF] = 0x20088E83u,
    [IF_KBD_SCAN] = 0x20088D53u,
    [IF_BUTTON] = 0x20088EABu,
    [IF_INIT_GUARD] = 0x20088C81u,
    [IF_ORIG_OUT] = 0x20089061u,
    [IF_ARP_ENABLED_BYTE] = 0x200894D8u,
    [IF_OCTAVES_BYTE] = 0x200894DFu,
    [IF_OUT_PTR] = 0x200894F0u,
    [IF_GLOBALS_FLAG] = 0x200895E5u,
    [IF_DISPLAY_INT] = 0x20037FF7u,
    [IF_HOLD_EVENT] = 0x20088F37u,
    [IF_RT_SNIFF] = 0x20088F59u,
    [IF_PARSER_STATE] = 0x20034343u,
    [IF_DISPLAY3] = 0x20037F25u,
    [IF_ENGINE_DIV] = 0x200894E2u,
    [IF_ENGINE_TPS] = 0x200894E4u,
    [IF_ENGINE_ACC] = 0x200894E8u,
    [IF_ENGINE_EXTCLK] = 0x200894F8u,
    [IF_ENGINE_CLKLOSS] = 0x200894FCu,
    [IF_DISPLAY_TIMER] = 0x200895E8u,
    [IF_A440_USED] = 0x20089508u,
    [IF_STOCK_MIDI_OUT_ON] = 0x20033F85u,
    [IF_STOCK_MIDI_OUT_OFF] = 0x20033F39u,
    [IF_STOCK_HOLD_QUERY] = 0x2003B695u,
    [IF_STOCK_DSP_POST] = 0x2003D325u,
    [IF_STOCK_DISPLAY_RESTORE] = 0x2003818Du,
    [IF_STOCK_UI] = 0x20057390u,
    [IF_ENGINE_MODE] = 0x200894DDu,
    [IF_ENGINE_BPM] = 0x200894E0u,
};

typedef void (*note3_fn)(int, int, int);
typedef void (*note2_fn)(int, int);
typedef void (*void_fn)(void);
typedef int (*scan_fn)(void *);
typedef void (*button_fn)(int, int);
typedef void (*out_fn)(int, int, int, int, int);
typedef void (*int_fn)(int);
typedef void (*sniff_fn)(int, int);
typedef void (*disp3_fn)(int, int, int);
typedef void (*midi4_fn)(int, int, int, int);
typedef int (*query_fn)(void);
typedef void (*word_fn)(uint32_t);
typedef void (*ptr_fn)(void *);

#define DSP_HOLD_MSG 0x080D0000u          /* stock hold handler's voice-engine message | state */

/* --- wrapper state: top 2 KB of the wrapper record (0x2008B800..0x2008C000), zero after
 * every boot --- */
#define RELATCH ((relatch_t *)0x2008B800u)
#define SEQ     ((seq_t *)0x2008B840u)
#define RATE    ((rate_t *)0x2008B980u)
#define OCT     ((oct_t *)0x2008B990u)
#define VHOLD   ((vhold_t *)0x2008BA30u)
#define DISP    ((disp_t *)0x2008BA40u)
_Static_assert(sizeof(relatch_t) <= 0x40, "relatch state too large");
_Static_assert(sizeof(seq_t) <= 0x140, "seq state too large");
_Static_assert(sizeof(rate_t) <= 0x10, "rate state too large");
_Static_assert(sizeof(oct_t) <= 0xA0, "oct state too large");
_Static_assert(sizeof(vhold_t) <= 0x10, "vhold state too large");
_Static_assert(sizeof(disp_t) <= 0x10, "disp state too large");

static int stock_hold_active(void) { return IF_FN(IF_STOCK_HOLD_QUERY, query_fn)() != 0; }
static void dsp_post(uint32_t word) { IF_FN(IF_STOCK_DSP_POST, word_fn)(word); }

/* --- platform ---------------------------------------------------------------------- */
int plat_arp_enabled(void) { return *IF_PTR(IF_ARP_ENABLED_BYTE, volatile const uint8_t *) != 0; }
void plat_v5_note(int src, int note, int vel)
{
    if (src == 2)
        IF_FN(IF_MIDI_NOTE_ON, note3_fn)(2, note, vel);
    else
        IF_FN(IF_LOCAL_NOTE, note3_fn)(1, note, vel);
}
void plat_v5_clear(void) { IF_FN(IF_ALL_NOTES_OFF, void_fn)(); }
void plat_v5_hold(int on) { IF_FN(IF_HOLD_EVENT, int_fn)(on); }
void plat_v5_button(int id, int value) { IF_FN(IF_BUTTON, button_fn)(id, value); }
int plat_v5_octaves(void) { return *IF_PTR(IF_OCTAVES_BYTE, volatile const uint8_t *); }
int plat_globals_active(void) { return *IF_PTR(IF_GLOBALS_FLAG, volatile const uint8_t *) != 0; }
void plat_display_int(int value) { IF_FN(IF_DISPLAY_INT, int_fn)(value); }
void plat_display3(int c0, int c1, int c2) { IF_FN(IF_DISPLAY3, disp3_fn)(c0, c1, c2); }
void plat_display_hold(void) { disp_touch(DISP); }   /* reverts to the patch display in 1.5 s */
void plat_a440_mark_used(void) { *IF_PTR(IF_A440_USED, volatile uint8_t *) = 1; }
void plat_engine_reset_acc(void) { *IF_PTR(IF_ENGINE_ACC, volatile uint32_t *) = 0; }
void plat_orig_out(int ctx, int src, int on, int note, int vel)
{
    IF_FN(IF_ORIG_OUT, out_fn)(ctx, src, on, note, vel);
}

/* The arp's clear event also resets its own hold flag (engine + 0x302), so a clear issued
 * while HOLD is active must be followed by "hold on" again (queue order: clear, hold). */
static void relatch_clear(void)
{
    plat_v5_clear();
    plat_v5_hold(1);                    /* re-latch only ever clears while HOLD is active */
}

/* --- note output substitution --------------------------------------------------------- */
void wrapper_output(int ctx, int src, int on, int note, int vel)
{
    seq_output(SEQ, ctx, src, on, note, vel);
}

static void ensure_output_installed(void)
{
    volatile uint32_t *slot = IF_PTR(IF_OUT_PTR, volatile uint32_t *);
    uint32_t want = (uint32_t)(uintptr_t)&wrapper_output;   /* Thumb bit set */
    IF_FN(IF_INIT_GUARD, void_fn)();
    if (*slot != want)
        *slot = want;
}

/* Internal clock: the engine recomputes its step period from (div, tps) every tick. Under
 * MIDI sync it only uses tps for the clock-loss timeout, so V5's values are kept there. */
static void apply_rate(void)
{
    int div = 2, tps = 1000;
    if (*IF_PTR(IF_ENGINE_EXTCLK, volatile const uint8_t *) == 0)
        rate_params(RATE, &div, &tps);
    *IF_PTR(IF_ENGINE_DIV, volatile uint16_t *) = (uint16_t)div;
    *IF_PTR(IF_ENGINE_TPS, volatile uint32_t *) = (uint32_t)tps;
}

/* --- hooks ------------------------------------------------------------------------- */
static void note_event(int src, int note, int vel, int *consumed, int *clear_first)
{
    int enabled = plat_arp_enabled();
    *consumed = seq_note(SEQ, src, note, vel);
    /* re-latch only governs the normal arp; in seq mode the sequence logic owns restarts */
    *clear_first = relatch_note(RELATCH, enabled && !SEQ->active, src, note, vel)
                   == RELATCH_CLEAR_THEN_FORWARD;
}

/* stock 0x2003BECC: keyboard FIFO consumer -> note_on(1, note, vel). The octave shift
 * is applied here, before re-latch/seq/V5 see the key. */
void hook_local_note(int src, int note, int vel)
{
    int consumed, clear_first;
    note = oct_map_key(OCT, note, vel > 0);
    if (note < 0)
        return;
    note_event(RELATCH_SRC_LOCAL, note, vel, &consumed, &clear_first);
    if (consumed)
        return;
    if (clear_first)
        relatch_clear();
    IF_FN(IF_LOCAL_NOTE, note3_fn)(src, note, vel);
}

/* stock 0x2003B07A: MIDI 0x9n -> note_on(2, note, vel) */
void hook_midi_note_on(int src, int note, int vel)
{
    int consumed, clear_first;
    note_event(RELATCH_SRC_MIDI, note, vel, &consumed, &clear_first);
    if (consumed)
        return;
    if (clear_first)
        relatch_clear();
    IF_FN(IF_MIDI_NOTE_ON, note3_fn)(src, note, vel);
}

/* stock 0x2003B032: MIDI 0x8n -> note_off(2, note) */
void hook_midi_note_off(int src, int note)
{
    int consumed, clear_first;
    note_event(RELATCH_SRC_MIDI, note, 0, &consumed, &clear_first);
    if (consumed)
        return;
    IF_FN(IF_MIDI_NOTE_OFF, note2_fn)(src, note);
}

/* "Display messages": while a message is up, V5's own display timer is held above zero so
 * it never draws its status text; at expiry the stock patch display is restored. V5's
 * messages are detected as changes of the settings it displays. */
static void display_tick(int enabled)
{
    volatile uint16_t *v5_timer = IF_PTR(IF_DISPLAY_TIMER, volatile uint16_t *);
    disp_observe(DISP, enabled,
                 *IF_PTR(IF_ENGINE_MODE, volatile const uint8_t *),
                 *IF_PTR(IF_OCTAVES_BYTE, volatile const uint8_t *),
                 *IF_PTR(IF_ENGINE_EXTCLK, volatile const uint8_t *),
                 *IF_PTR(IF_ENGINE_BPM, volatile const uint16_t *));
    if (disp_active(DISP))
        *v5_timer = 2;
    if (disp_tick(DISP)) {
        *v5_timer = 0;
        IF_FN(IF_STOCK_DISPLAY_RESTORE, ptr_fn)(IF_PTR(IF_STOCK_UI, void *));
    }
}

/* stock 0x2003BE9C: 1 kHz keyboard poll -> the arp's tick. Feed dummies first. */
int hook_kbd_scan(void *fifo)
{
    int enabled, post;
    ensure_output_installed();
    enabled = plat_arp_enabled();
    /* "HOLD while the arp is on": re-sync the voice engine's hold on enable/disable */
    post = vhold_tick(VHOLD, enabled, stock_hold_active());
    if (post >= 0)
        dsp_post(DSP_HOLD_MSG | (uint32_t)post);
    apply_rate();
    seq_tick(SEQ);
    display_tick(enabled);
    return IF_FN(IF_KBD_SCAN, scan_fn)(fifo);
}

/* stock 0x2003BEFA / 0x2003BF16: keyboard FIFO consumer -> local key to MIDI Out as note-on /
 * note-off (cable_mask, channel, note, vel). Same per-key mapping as the note hook, so MIDI
 * Out follows the shifted keyboard; reached with Local Control on or off. */
void hook_local_midi_out_on(int cable, int ch, int note, int vel)
{
    note = oct_map_key(OCT, note, 1);
    if (note < 0)
        return;
    IF_FN(IF_STOCK_MIDI_OUT_ON, midi4_fn)(cable, ch, note, vel);
}

void hook_local_midi_out_off(int cable, int ch, int note, int vel)
{
    note = oct_map_key(OCT, note, 0);
    if (note < 0)
        return;
    IF_FN(IF_STOCK_MIDI_OUT_OFF, midi4_fn)(cable, ch, note, vel);
}

/* stock 0x2003EACE: note_off asks whether HOLD is active before releasing a voice. While the
 * arp is on it must hear "no", or every arp step would be sustained. */
int hook_hold_query(void)
{
    return vhold_query(plat_arp_enabled(), stock_hold_active());
}

/* stock 0x2003C244: panel button (id, value 1 press / 2 release / 3 held) */
void hook_button(int id, int value)
{
    int act = oct_button(OCT, id, value);
    if (act == OCT_REPLAY_TAP) {        /* Lo Freq tapped: give the stock its tap now */
        plat_v5_button(OCT_MOD_ID, 1);
        plat_v5_button(OCT_MOD_ID, 2);
        return;
    }
    if (act == OCT_CONSUMED)
        return;
    if (seq_button(SEQ, id, value))
        return;
    if (rate_button(RATE, SEQ->a440_held, id, value))
        return;
    IF_FN(IF_BUTTON, button_fn)(id, value);
}

/* stock 0x2003B294: MIDI CC 123 All Notes Off */
void hook_cc123(void)
{
    seq_all_notes_off(SEQ);
    plat_v5_clear();
}

/* What V5's hold hook did (post the voice-engine message, then its hold event), with the
 * wrapper's bookkeeping first and the message withheld while the arp is on ("HOLD while the
 * arp is on"): the arp latches, the synth's own hold stays off. */
__attribute__((used)) void hook_hold_dispatch(uint32_t msg, int new_state)
{
    int on = new_state & 1;
    relatch_hold(RELATCH, on);
    seq_hold(SEQ, on);
    if (vhold_post_on_hold_change(plat_arp_enabled()))
        dsp_post(msg);
    plat_v5_hold(on);
}

/* stock 0x200396CA: HOLD handler posts its message with r0 = message and r4 = new state,
 * and uses r4 afterwards. Hand-written so r4 is handed to C and preserved. */
__attribute__((naked)) void hook_hold(void)
{
    __asm__ volatile(
        "push {r4, lr}\n"
        "mov   r1, r4\n"
        "bl    hook_hold_dispatch\n"
        "pop  {r4, pc}\n");
}

/* MIDI realtime bytes reach V5's sniff through the stock byte-parser state table. Under
 * MIDI sync the arp steps every 12 clocks it sees, so this hands it a filtered stream for
 * the selected note value and keeps its clock-loss timer fed for withheld clocks. */
__attribute__((used)) void hook_rt_glue(int byte, int port)
{
    int n = 1;
    byte &= 0xFF;
    if (*IF_PTR(IF_ENGINE_EXTCLK, volatile const uint8_t *) != 0) {
        n = rate_clock(RATE, byte);
        if (byte == 0xF8)
            *IF_PTR(IF_ENGINE_CLKLOSS, volatile uint32_t *) = 0;
    }
    while (n-- > 0)
        IF_FN(IF_RT_SNIFF, sniff_fn)(byte, port);
}

/* Installed at the stock MIDI-parser state-table entries V5 used (1, 3, 4, 5). Mirrors
 * V5's trampoline: the parser's r0 holds byte - 0xF0 and r4 the port; everything is
 * restored before continuing to the stock state handler (IF_PARSER_STATE = index 16). */
__attribute__((naked)) void hook_rt_trampoline(void)
{
    __asm__ volatile(
        "push {r0, r1, r2, r3, r4, r5, r12, lr}\n"
        "add   r0, r0, #0xf0\n"
        "mov   r1, r4\n"
        "bl    hook_rt_glue\n"
        "pop  {r0, r1, r2, r3, r4, r5, r12, lr}\n"
        "sub   sp, sp, #8\n"
        "str   r0, [sp]\n"
        "movw  r0, :lower16:v5_iface\n"
        "movt  r0, :upper16:v5_iface\n"
        "ldr   r0, [r0, #60]\n"
        "str   r0, [sp, #4]\n"
        "pop  {r0, pc}\n");
}
_Static_assert(IF_PARSER_STATE * 4 == 60, "hook_rt_trampoline loads v5_iface[IF_PARSER_STATE]");
