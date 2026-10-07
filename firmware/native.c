/* Engine glue (docs/SPEC.md: "Arp engine" — Realisation):
 * the functions the patched stock BL/word sites land on, the platform calls the portable
 * modules make into stock, and the hand-over queue from the Prophet5 task to the 1 ms tick.
 * Built freestanding for thumbv7a as a single translation unit at 0x20088000 (tools/fw.py).
 *
 * Task contexts (docs/re/stock-hook-sites.md): the keyboard tick, local notes, panel buttons,
 * pots and MIDI realtime bytes run in the FreeRTOS Timer Service task; MIDI notes, CC 123-127
 * and the hold handler run in the lower-priority Prophet5 task and are queued to the tick.
 * All state is zero in the image and initialised lazily by the first hook. */
#include <stdint.h>

#include "platform.h"
#include "arp.c"
#include "arpui.c"
#include "rate.c"
#include "disp.c"
#include "oct.c"
#include "vhold.c"

/* --- the complete stock interface, one read-only table (tests read it back) ------------- */
enum {
    NI_NOTE_ON,          /* 0x2003EC5D note_on(src, note, vel): the voice allocator */
    NI_NOTE_OFF,         /* 0x2003E95D note_off(src, note) */
    NI_ALL_NOTES_OFF,    /* 0x2003EBE5 all_notes_off() */
    NI_LED,              /* 0x20036825 led_set(led, 0 off / 1 on) */
    NI_DISPLAY3,         /* 0x20037F25 display3(c0, c1, c2) panel codes */
    NI_DISPLAY_INT,      /* 0x20037FF7 display_int(v) */
    NI_DISPLAY_RESTORE,  /* 0x2003818D display_restore(ui): the patch display */
    NI_UI,               /* 0x20057390 the UI object */
    NI_GLOBALS_OPEN,     /* 0x20057438 word != 0 while the Globals menu is open */
    NI_A440_HELD,        /* 0x20079B5C button-held table entry for A440 (u16) */
    NI_HOLD_QUERY,       /* 0x2003B695 merged HOLD button/pedal state () -> 0/1 */
    NI_DSP_POST,         /* 0x2003D325 post a 32-bit word to the voice engine */
    NI_FIFO_COUNT,       /* 0x2003D829 fifo_count(fifo) */
    NI_BUTTON_POST,      /* 0x2003BC31 post a panel button (id, value) */
    NI_POT_STORE,        /* 0x20036B51 store pot raw (pot, raw) */
    NI_POT_CHANGE,       /* 0x2003BC6D post pot change (pot, old, new) */
    NI_MIDI_OUT_ON,      /* 0x20033F85 local key note-on to MIDI Out (cable, ch, note, vel) */
    NI_MIDI_OUT_OFF,     /* 0x20033F39 local key note-off to MIDI Out (cable, ch, note, vel) */
    NI_PARSER_STATE,     /* 0x20034343 MIDI byte-parser state handler the trampoline continues to */
    NI_PARAM_READ,       /* 0x2003CB69 live program parameter read(layer, param) -> u16 */
    NI_PARAM_STORE,      /* 0x2003CEF5 plain program parameter store(layer, param, value) */
    NI_HOLD_OFF,         /* 0x2003B6B1 hold off (both sources): original callee at the program-loaded hook */
    NI_COUNT
};

const volatile uint32_t stock_iface[NI_COUNT] = {
    [NI_NOTE_ON] = 0x2003EC5Du,
    [NI_NOTE_OFF] = 0x2003E95Du,
    [NI_ALL_NOTES_OFF] = 0x2003EBE5u,
    [NI_LED] = 0x20036825u,
    [NI_DISPLAY3] = 0x20037F25u,
    [NI_DISPLAY_INT] = 0x20037FF7u,
    [NI_DISPLAY_RESTORE] = 0x2003818Du,
    [NI_UI] = 0x20057390u,
    [NI_GLOBALS_OPEN] = 0x20057438u,
    [NI_A440_HELD] = 0x20079B5Cu,
    [NI_HOLD_QUERY] = 0x2003B695u,
    [NI_DSP_POST] = 0x2003D325u,
    [NI_FIFO_COUNT] = 0x2003D829u,
    [NI_BUTTON_POST] = 0x2003BC31u,
    [NI_POT_STORE] = 0x20036B51u,
    [NI_POT_CHANGE] = 0x2003BC6Du,
    [NI_MIDI_OUT_ON] = 0x20033F85u,
    [NI_MIDI_OUT_OFF] = 0x20033F39u,
    [NI_PARSER_STATE] = 0x20034343u,
    [NI_PARAM_READ] = 0x2003CB69u,
    [NI_PARAM_STORE] = 0x2003CEF5u,
    [NI_HOLD_OFF] = 0x2003B6B1u,
};

#define SFN(i, type) ((type)(uintptr_t)stock_iface[i])
#define SPTR(i, type) ((type)(uintptr_t)stock_iface[i])

typedef void (*note3_fn)(int, int, int);
typedef void (*note2_fn)(int, int);
typedef void (*void_fn)(void);
typedef int (*scan_fn)(void *);
typedef void (*button_fn)(int, int);
typedef void (*int_fn)(int);
typedef void (*disp3_fn)(int, int, int);
typedef void (*midi4_fn)(int, int, int, int);
typedef void (*pot3_fn)(int, int, int);
typedef int (*query_fn)(void);
typedef void (*word_fn)(uint32_t);
typedef void (*ptr_fn)(void *);
typedef int (*param_read_fn)(int, int);
typedef void (*param_store_fn)(int, int, int);

#define DSP_HOLD_MSG 0x080D0000u          /* the hold handler's voice-engine message | state */

/* --- state: 0x2008E000..0x20090000, zero after every boot ------------------------------- */
typedef struct { uint8_t type, a, b, c; } qev_t;
typedef struct {
    volatile uint8_t wr, rd;              /* single producer (Prophet5 task), single consumer (tick) */
    uint8_t pad[2];
    qev_t ev[64];
} queue_t;
enum { Q_NOTE = 1, Q_HOLD = 2, Q_ANO = 3, Q_PROGRAM = 4 };

#define ARP     ((arp_t *)0x2008E000u)
#define UI      ((arpui_t *)0x2008EA00u)
#define OCT     ((oct_t *)0x2008EA40u)
#define VHOLD   ((vhold_t *)0x2008EAE0u)
#define QUEUE   ((queue_t *)0x2008EB00u)
#define INITED  ((volatile uint8_t *)0x2008ED00u)
_Static_assert(sizeof(arp_t) <= 0xA00, "arp state too large");
_Static_assert(sizeof(arpui_t) <= 0x40, "ui state too large");
_Static_assert(sizeof(oct_t) <= 0xA0, "oct state too large");
_Static_assert(sizeof(vhold_t) <= 0x10, "vhold state too large");
_Static_assert(sizeof(queue_t) <= 0x200, "queue too large");

static void zero_bytes(void *p, unsigned n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}

static void ensure_init(void)
{
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr\n\tcpsid i" : "=r"(cpsr) :: "memory");
    if (!*INITED) {
        arp_init(ARP);
        arpui_init(UI);
        oct_init(OCT);
        vhold_init(VHOLD);
        zero_bytes(QUEUE, sizeof *QUEUE);
        *INITED = 1;
    }
    if (!(cpsr & 0x80))
        __asm__ volatile("cpsie i" ::: "memory");
}

/* Freestanding build, Cortex-A5 without hardware divide: the engine's unsigned '/' and '%'
 * compile to these EABI helpers. Quotient in r0, remainder in r1. */
unsigned __aeabi_uidiv(unsigned n, unsigned d)
{
    unsigned q = 0, bit = 1;
    if (d == 0)
        return 0;
    while (d < n && !(d & 0x80000000u)) {
        d <<= 1;
        bit <<= 1;
    }
    while (bit) {
        if (n >= d) {
            n -= d;
            q |= bit;
        }
        d >>= 1;
        bit >>= 1;
    }
    return q;
}

__attribute__((naked)) void __aeabi_uidivmod(void)
{
    __asm__ volatile(
        "push {r0, r1, r2, lr}\n"
        "bl    __aeabi_uidiv\n"
        "pop  {r1, r2, r3, lr}\n"
        "mul   r3, r0, r2\n"
        "sub   r1, r1, r3\n"
        "bx    lr\n");
}

static int killed(void) { return UI->kill; }
static int stock_hold_active(void) { return SFN(NI_HOLD_QUERY, query_fn)() != 0; }
static void dsp_post(uint32_t word) { SFN(NI_DSP_POST, word_fn)(word); }

/* --- queue: Prophet5 task -> tick -------------------------------------------------------- */
static void q_push(int type, int a, int b, int c)
{
    queue_t *q = QUEUE;
    unsigned w = q->wr, n = (w + 1) & 63;
    if (n == q->rd)
        return;                                            /* full: the newest event is dropped */
    q->ev[w].type = (uint8_t)type;
    q->ev[w].a = (uint8_t)a;
    q->ev[w].b = (uint8_t)b;
    q->ev[w].c = (uint8_t)c;
    __asm__ volatile("" ::: "memory");
    q->wr = (uint8_t)n;
}

static void q_drain(void)
{
    queue_t *q = QUEUE;
    while (q->rd != q->wr) {
        qev_t e = q->ev[q->rd];
        __asm__ volatile("" ::: "memory");
        q->rd = (uint8_t)((q->rd + 1) & 63);
        switch (e.type) {
        case Q_NOTE: arpui_note(UI, ARP, e.a, e.b, e.c); break;
        case Q_HOLD: arpui_hold(UI, ARP, e.a); break;
        case Q_ANO:  arp_all_notes_off(ARP); break;
        case Q_PROGRAM: arpui_program_loaded(UI, ARP); break;
        default: break;
        }
    }
}

/* --- platform ---------------------------------------------------------------------------- */
void plat_voice_on(int src, int note, int vel) { SFN(NI_NOTE_ON, note3_fn)(src, note, vel); }
void plat_voice_off(int src, int note) { SFN(NI_NOTE_OFF, note2_fn)(src, note); }
void plat_led(int led, int on) { SFN(NI_LED, button_fn)(led, on != 0); }
void plat_display3(int c0, int c1, int c2) { SFN(NI_DISPLAY3, disp3_fn)(c0, c1, c2); }
void plat_display_int(int v) { SFN(NI_DISPLAY_INT, int_fn)(v); }
void plat_display_restore(void) { SFN(NI_DISPLAY_RESTORE, ptr_fn)(SPTR(NI_UI, void *)); }
int  plat_globals_open(void) { return *SPTR(NI_GLOBALS_OPEN, volatile const uint32_t *) != 0; }
int  plat_a440_down(void) { return *SPTR(NI_A440_HELD, volatile const uint16_t *) != 0; }
void plat_display_hold(void) { disp_touch(&UI->disp); }   /* oct.c: the shift readout reverts too */
int  plat_param_read(int param) { return SFN(NI_PARAM_READ, param_read_fn)(0, param); }          /* layer A */
void plat_param_store(int param, int value) { SFN(NI_PARAM_STORE, param_store_fn)(0, param, value); }

/* --- hooks: Timer Service task ----------------------------------------------------------- */
/* stock 0x2003BE9C: the 1 ms keyboard poll. Everything the engine does happens here. */
int hook_kbd_scan(void *fifo)
{
    ensure_init();
    arpui_tick(UI, ARP);                                   /* kill switch first: it must get its
                                                              chance before any engine code runs */
    if (!killed()) {
        int post;
        q_drain();
        post = vhold_tick(VHOLD, arpui_suspended(UI, ARP), stock_hold_active());
        if (post >= 0)
            dsp_post(DSP_HOLD_MSG | (uint32_t)post);       /* "HOLD while the arp is on" */
        arp_tick(ARP);
    }
    return SFN(NI_FIFO_COUNT, scan_fn)(fifo);
}

/* stock 0x2003BECC: keyboard FIFO consumer -> note_on(1, note, vel) */
void hook_local_note(int src, int note, int vel)
{
    ensure_init();
    if (killed()) {
        SFN(NI_NOTE_ON, note3_fn)(src, note, vel);
        return;
    }
    note = oct_map_key(OCT, note, vel > 0);
    if (note < 0)
        return;
    arpui_note(UI, ARP, src, note, vel);
}

/* stock 0x2003BEFA / 0x2003BF16: local key to MIDI Out (cable_mask, channel, note, vel) */
void hook_local_midi_out_on(int cable, int ch, int note, int vel)
{
    ensure_init();
    if (!killed()) {
        note = oct_map_key(OCT, note, 1);
        if (note < 0)
            return;
    }
    SFN(NI_MIDI_OUT_ON, midi4_fn)(cable, ch, note, vel);
}

void hook_local_midi_out_off(int cable, int ch, int note, int vel)
{
    ensure_init();
    if (!killed()) {
        note = oct_map_key(OCT, note, 0);
        if (note < 0)
            return;
    }
    SFN(NI_MIDI_OUT_OFF, midi4_fn)(cable, ch, note, vel);
}

/* stock 0x2003C244: panel button (id, value 1 press / 2 release / 3 held) */
void hook_button(int id, int value)
{
    ensure_init();
    if (!killed()) {
        int act = oct_button(OCT, id, value);
        if (act == OCT_REPLAY_TAP) {                       /* Lo Freq tapped: stock gets its tap now */
            SFN(NI_BUTTON_POST, button_fn)(OCT_MOD_ID, 1);
            SFN(NI_BUTTON_POST, button_fn)(OCT_MOD_ID, 2);
            return;
        }
        if (act == OCT_CONSUMED)
            return;
        if (arpui_button(UI, ARP, id, value))
            return;
    }
    SFN(NI_BUTTON_POST, button_fn)(id, value);
}

/* stock 0x2003C292 / 0x2003C2A6: pot raw store and pot change post */
void hook_pot_store(int pot, int raw)
{
    ensure_init();
    if (!killed() && arpui_pot_store(UI, ARP, pot, raw))
        return;
    SFN(NI_POT_STORE, button_fn)(pot, raw);
}

void hook_pot_change(int pot, int old, int new_value)
{
    ensure_init();
    if (!killed() && arpui_pot_change(UI, ARP, pot))
        return;
    SFN(NI_POT_CHANGE, pot3_fn)(pot, old, new_value);
}

/* MIDI realtime bytes: the parser's status table sends F8/FA/FB/FC here with r0 = byte - 0xF0
 * and r4 = port; everything is restored before continuing to the stock state handler. */
__attribute__((used)) void hook_rt_glue(int byte, int port)
{
    ensure_init();
    if (!killed())
        arp_realtime(ARP, byte & 0xFF, port);
}

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
        "movw  r0, :lower16:stock_iface\n"
        "movt  r0, :upper16:stock_iface\n"
        "ldr   r0, [r0, #72]\n"
        "str   r0, [sp, #4]\n"
        "pop  {r0, pc}\n");
}
_Static_assert(NI_PARSER_STATE * 4 == 72, "hook_rt_trampoline loads stock_iface[NI_PARSER_STATE]");

/* --- hooks: Prophet5 task (queued to the tick) ------------------------------------------- */
/* stock 0x2003B07A: MIDI note_on(2, note, vel) */
void hook_midi_note_on(int src, int note, int vel)
{
    ensure_init();
    if (killed()) {
        SFN(NI_NOTE_ON, note3_fn)(src, note, vel);
        return;
    }
    q_push(Q_NOTE, src, note, vel);
}

/* stock 0x2003B032: MIDI note_off(2, note) */
void hook_midi_note_off(int src, int note)
{
    ensure_init();
    if (killed()) {
        SFN(NI_NOTE_OFF, note2_fn)(src, note);
        return;
    }
    q_push(Q_NOTE, src, note, 0);
}

/* stock 0x2003B294 / 0x2003B144: CC 123-127 -> all_notes_off(), then the pool is cleared */
void hook_cc123(void)
{
    ensure_init();
    SFN(NI_ALL_NOTES_OFF, void_fn)();
    if (!killed())
        q_push(Q_ANO, 0, 0, 0);
}

/* stock 0x200396CA: the hold handler's voice-engine message, r4 = merged HOLD state. The
 * message is withheld while the arp is on ("HOLD while the arp is on"); the state is queued. */
__attribute__((used)) void hook_hold_dispatch(uint32_t msg, int new_state)
{
    int on = new_state & 1;
    ensure_init();
    if (killed() || vhold_post_on_hold_change(arpui_suspended(UI, ARP)))
        dsp_post(msg);
    if (!killed())
        q_push(Q_HOLD, on, 0, 0);
}

__attribute__((naked)) void hook_hold(void)
{
    __asm__ volatile(
        "push {r4, lr}\n"
        "mov   r1, r4\n"
        "bl    hook_hold_dispatch\n"
        "pop  {r4, pc}\n");
}

/* stock 0x2003D15C: the end of the program-apply routine (every load path): hold off, then
 * the engine learns the program's arp settings ("Patch memory") */
void hook_program_loaded(void)
{
    ensure_init();
    SFN(NI_HOLD_OFF, void_fn)();
    if (!killed())
        q_push(Q_PROGRAM, 0, 0, 0);
}

/* stock 0x2003EACE: note_off asks whether HOLD is active (both tasks) */
int hook_hold_query(void)
{
    int stock;
    ensure_init();
    stock = stock_hold_active();
    return killed() ? stock : vhold_query(arpui_suspended(UI, ARP), stock);
}
