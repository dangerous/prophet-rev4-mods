/* The wrapper's complete external interface: every V5 (Arp Mod V5) and stock address it
 * touches, in one read-only table. `volatile` forces every use to load from the table, so
 * the compiler cannot derive one address from another; tests read the table back from the
 * built binary and compare it word for word (tests/firmware/test_image.py). */
#ifndef V5_IFACE_H
#define V5_IFACE_H

#include <stdint.h>

enum {
    IF_LOCAL_NOTE,        /* 0x20088C51 V5 local note hook: (src=1, note, vel) */
    IF_MIDI_NOTE_ON,      /* 0x20088CFD V5 MIDI note-on hook: (src=2, note, vel) */
    IF_MIDI_NOTE_OFF,     /* 0x20088D2D V5 MIDI note-off hook: (src=2, note) */
    IF_ALL_NOTES_OFF,     /* 0x20088E83 V5 CC123 hook: stock all-notes-off + arp clear */
    IF_KBD_SCAN,          /* 0x20088D53 V5 keyboard-scan tick hook: (fifo) -> count */
    IF_BUTTON,            /* 0x20088EAB V5 panel-button hook: (id, value) */
    IF_INIT_GUARD,        /* 0x20088C81 V5 lazy init (idempotent) */
    IF_ORIG_OUT,          /* 0x20089061 V5 original note output: (ctx, src, on, note, vel) */
    IF_HOLD_HOOK,         /* 0x20089081 V5 hold hook (reads r4) — used by the asm stub */
    IF_ARP_ENABLED_BYTE,  /* 0x200894D8 engine + 0x300 */
    IF_OCTAVES_BYTE,      /* 0x200894DF engine + 0x307 */
    IF_OUT_PTR,           /* 0x200894F0 engine + 0x318: note output function pointer */
    IF_GLOBALS_FLAG,      /* 0x200895E5 V5 hook state + 0x415 */
    IF_DISPLAY_INT,       /* 0x20037FF7 stock 3-digit integer display */
    IF_HOLD_EVENT,        /* 0x20088F37 V5 hold-event entry: (state) -> enqueue hold on/off */
    IF_RT_SNIFF,          /* 0x20088F59 V5 realtime sniff: (byte, port) -> enqueue F8/FA/FB/FC */
    IF_PARSER_STATE,      /* 0x20034343 stock MIDI byte-parser state handler the trampoline continues to */
    IF_DISPLAY3,          /* 0x20037F25 stock 3-character display: (c0, c1, c2) panel codes */
    IF_ENGINE_DIV,        /* 0x200894E2 engine + 0x30a: divisions per beat (u16) */
    IF_ENGINE_TPS,        /* 0x200894E4 engine + 0x30c: ticks per second (u32) */
    IF_ENGINE_ACC,        /* 0x200894E8 engine + 0x310: internal step accumulator (u32) */
    IF_ENGINE_EXTCLK,     /* 0x200894F8 engine + 0x320: clock source (0 int, else MIDI) */
    IF_ENGINE_CLKLOSS,    /* 0x200894FC engine + 0x324: ticks since last MIDI clock (u32) */
    IF_DISPLAY_TIMER,     /* 0x200895E8 V5 hook state + 0x418: display timeout (u16 ticks) */
    IF_A440_USED,         /* 0x20089508 V5 button ctx + 8: A440 hold used as modifier (u8) */
    IF_COUNT
};

extern const volatile uint32_t v5_iface[IF_COUNT];

#define IF_FN(i, type) ((type)(uintptr_t)v5_iface[i])
#define IF_PTR(i, type) ((type)(uintptr_t)v5_iface[i])

#endif
