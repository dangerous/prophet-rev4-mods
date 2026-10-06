/* Wrapper glue: the functions the patched stock BL sites land on. Built freestanding for
 * thumbv7a (Cortex-A5) as a single translation unit at WRAPPER_BASE (see tools/fw.py).
 *
 * Every hook does its bookkeeping and then calls the V5 entry point the stock code used
 * to call, so the arp sees exactly the events it saw before, in the same order. */
#include <stdint.h>

#include "relatch.c"

/* --- V5 (Arp Mod V5) and stock facts; each is asserted against the fixture by tests --- */
#define V5_LOCAL_NOTE     ((void (*)(int, int, int))0x20088C51u)   /* (src=1, note, vel) */
#define V5_MIDI_NOTE_ON   ((void (*)(int, int, int))0x20088CFDu)   /* (src=2, note, vel) */
#define V5_MIDI_NOTE_OFF  ((void (*)(int, int))0x20088D2Du)        /* (src=2, note) */
#define V5_ALL_NOTES_OFF  ((void (*)(void))0x20088E83u)             /* stock all-notes-off + arp clear */
#define V5_HOLD_HOOK      0x20089081u                               /* reads new hold state from r4 */
#define ARP_ENABLED_BYTE  ((volatile const uint8_t *)0x200894D8u)   /* V5 engine + 0x300 */

/* --- wrapper state: top of the wrapper window, zero after every boot --- */
#define STATE ((relatch_t *)0x20089F00u)

static int arp_enabled(void)
{
    return *ARP_ENABLED_BYTE != 0;
}

static void note_event(int src, int note, int vel)
{
    if (relatch_note(STATE, arp_enabled(), src, note, vel) == RELATCH_CLEAR_THEN_FORWARD)
        V5_ALL_NOTES_OFF();
}

/* stock 0x2003BECC: keyboard FIFO consumer -> note_on(1, note, vel) */
void hook_local_note(int src, int note, int vel)
{
    note_event(RELATCH_SRC_LOCAL, note, vel);
    V5_LOCAL_NOTE(src, note, vel);
}

/* stock 0x2003B07A: MIDI 0x9n -> note_on(2, note, vel) */
void hook_midi_note_on(int src, int note, int vel)
{
    note_event(RELATCH_SRC_MIDI, note, vel);
    V5_MIDI_NOTE_ON(src, note, vel);
}

/* stock 0x2003B032: MIDI 0x8n -> note_off(2, note) */
void hook_midi_note_off(int src, int note)
{
    note_event(RELATCH_SRC_MIDI, note, 0);
    V5_MIDI_NOTE_OFF(src, note);
}

__attribute__((used)) void hook_hold_glue(int new_state)
{
    relatch_hold(STATE, new_state & 1);
}

/* stock 0x200396CA: HOLD handler posts its message with r0 = message and r4 = new state.
 * The V5 hook relies on r4 surviving, so this stub is hand-written: it hands r4 to C,
 * restores every register it touched and jumps on to the V5 hook. */
__attribute__((naked)) void hook_hold(void)
{
    __asm__ volatile(
        "push {r0, r1, r4, lr}\n"
        "mov   r0, r4\n"
        "bl    hook_hold_glue\n"
        "pop  {r0, r1, r4, lr}\n"
        "movw  r12, #0x9081\n"
        "movt  r12, #0x2008\n"
        "bx    r12\n");
}
