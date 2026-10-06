/* Wrapper glue: the functions the patched stock BL sites land on, plus the platform calls
 * the portable logic (relatch.c, seq.c) makes into V5 and the stock OS. Built freestanding
 * for thumbv7a (Cortex-A5) as a single translation unit at WRAPPER_BASE (tools/fw.py).
 *
 * Every hook does its bookkeeping and then calls the V5 entry point the stock code used
 * to call, so the arp sees exactly the events it saw before, in the same order. All
 * external addresses come from the v5_iface table (v5_iface.h). */
#include <stdint.h>

#include "platform.h"
#include "v5_iface.h"
#include "relatch.c"
#include "seq.c"

const volatile uint32_t v5_iface[IF_COUNT] = {
    [IF_LOCAL_NOTE] = 0x20088C51u,
    [IF_MIDI_NOTE_ON] = 0x20088CFDu,
    [IF_MIDI_NOTE_OFF] = 0x20088D2Du,
    [IF_ALL_NOTES_OFF] = 0x20088E83u,
    [IF_KBD_SCAN] = 0x20088D53u,
    [IF_BUTTON] = 0x20088EABu,
    [IF_INIT_GUARD] = 0x20088C81u,
    [IF_ORIG_OUT] = 0x20089061u,
    [IF_HOLD_HOOK] = 0x20089081u,
    [IF_ARP_ENABLED_BYTE] = 0x200894D8u,
    [IF_OCTAVES_BYTE] = 0x200894DFu,
    [IF_OUT_PTR] = 0x200894F0u,
    [IF_GLOBALS_FLAG] = 0x200895E5u,
    [IF_DISPLAY_INT] = 0x20037FF7u,
};

typedef void (*note3_fn)(int, int, int);
typedef void (*note2_fn)(int, int);
typedef void (*void_fn)(void);
typedef int (*scan_fn)(void *);
typedef void (*button_fn)(int, int);
typedef void (*out_fn)(int, int, int, int, int);
typedef void (*int_fn)(int);

/* --- wrapper state: top of the wrapper window, zero after every boot --- */
#define RELATCH ((relatch_t *)0x20089E80u)
#define SEQ     ((seq_t *)0x20089EC0u)
_Static_assert(sizeof(relatch_t) <= 0x40, "relatch state too large");
_Static_assert(sizeof(seq_t) <= 0x140, "seq state too large");

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
void plat_v5_button(int id, int value) { IF_FN(IF_BUTTON, button_fn)(id, value); }
int plat_v5_octaves(void) { return *IF_PTR(IF_OCTAVES_BYTE, volatile const uint8_t *); }
int plat_globals_active(void) { return *IF_PTR(IF_GLOBALS_FLAG, volatile const uint8_t *) != 0; }
void plat_display_int(int value) { IF_FN(IF_DISPLAY_INT, int_fn)(value); }
void plat_orig_out(int ctx, int src, int on, int note, int vel)
{
    IF_FN(IF_ORIG_OUT, out_fn)(ctx, src, on, note, vel);
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

/* --- hooks ------------------------------------------------------------------------- */
static void note_event(int src, int note, int vel, int *consumed, int *clear_first)
{
    int enabled = plat_arp_enabled();
    *consumed = seq_note(SEQ, src, note, vel);
    /* re-latch only governs the normal arp; in seq mode the sequence logic owns restarts */
    *clear_first = relatch_note(RELATCH, enabled && !SEQ->active, src, note, vel)
                   == RELATCH_CLEAR_THEN_FORWARD;
}

/* stock 0x2003BECC: keyboard FIFO consumer -> note_on(1, note, vel) */
void hook_local_note(int src, int note, int vel)
{
    int consumed, clear_first;
    note_event(RELATCH_SRC_LOCAL, note, vel, &consumed, &clear_first);
    if (consumed)
        return;
    if (clear_first)
        plat_v5_clear();
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
        plat_v5_clear();
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

/* stock 0x2003BE9C: 1 kHz keyboard poll -> the arp's tick. Feed dummies first. */
int hook_kbd_scan(void *fifo)
{
    ensure_output_installed();
    seq_tick(SEQ);
    return IF_FN(IF_KBD_SCAN, scan_fn)(fifo);
}

/* stock 0x2003C244: panel button (id, value) */
void hook_button(int id, int value)
{
    if (seq_button(SEQ, id, value))
        return;
    IF_FN(IF_BUTTON, button_fn)(id, value);
}

/* stock 0x2003B294: MIDI CC 123 All Notes Off */
void hook_cc123(void)
{
    seq_all_notes_off(SEQ);
    plat_v5_clear();
}

__attribute__((used)) void hook_hold_glue(int new_state)
{
    relatch_hold(RELATCH, new_state & 1);
    seq_hold(SEQ, new_state & 1);
}

/* stock 0x200396CA: HOLD handler posts its message with r0 = message and r4 = new state.
 * The V5 hook relies on r4 surviving, so this stub is hand-written: it hands r4 to C,
 * restores every register it touched and jumps on to the V5 hook (address from the
 * table, IF_HOLD_HOOK = index 8). */
__attribute__((naked)) void hook_hold(void)
{
    __asm__ volatile(
        "push {r0, r1, r4, lr}\n"
        "mov   r0, r4\n"
        "bl    hook_hold_glue\n"
        "pop  {r0, r1, r4, lr}\n"
        "movw  r12, :lower16:v5_iface\n"
        "movt  r12, :upper16:v5_iface\n"
        "ldr   r12, [r12, #32]\n"
        "bx    r12\n");
}
