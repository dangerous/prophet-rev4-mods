/* Host harness for the read-only flash diagnostic (docs/SPEC.md: "Flash diagnostic"): the
 * chip-size verdict by aliasing, the blank check of the two unreferenced areas, one
 * 512-byte read per tick, bounds, and that a run always completes. The fake flash has no
 * write at all. `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "flash.h"
#include "platform.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake flash: a 16 MB address window over a chip of fake_size bytes; an address beyond
 * the chip aliases (a 24-bit address wraps on an 8 MB part). Content is 0xFF except inside
 * the listed spans, where byte = (addr * 7 + seed) & 0xFF. --------------------------------- */
typedef struct { uint32_t lo, hi; uint8_t seed; } span_t;
static uint32_t fake_size;
static span_t spans[8];
static int nspans;
static int reads, bad_reads;
static uint32_t max_len, last_off, last_len, fail_off;     /* fail_off: a read covering it fails (0 = none) */

static uint8_t byte_at(uint32_t phys)
{
    for (int i = 0; i < nspans; i++)
        if (spans[i].lo <= phys && phys <= spans[i].hi)
            return (uint8_t)((phys * 7u + spans[i].seed) & 0xFFu);
    return 0xFF;
}

int plat_flash_read(uint32_t off, void *dst, uint32_t len)
{
    reads++;
    last_off = off;
    last_len = len;
    if (len > max_len)
        max_len = len;
    if (off >= 0x1000000u || len > 0xFFFFFFu - off) {    /* stock: 24-bit window, last byte excluded */
        bad_reads++;
        return 3;
    }
    if (fail_off && off <= fail_off && fail_off < off + len)
        return 3;
    for (uint32_t i = 0; i < len; i++)
        ((uint8_t *)dst)[i] = byte_at((off + i) % fake_size);
    return 0;
}

static void reset(uint32_t size)
{
    fake_size = size;
    nspans = 0;
    reads = bad_reads = 0;
    max_len = last_off = last_len = fail_off = 0;
}

static void span(uint32_t lo, uint32_t hi, int seed)
{
    spans[nspans++] = (span_t){lo, hi, (uint8_t)seed};
}

static void refs(void)                                     /* a bootloader and factory programs */
{
    span(FLASH_R1, FLASH_R1 + FLASH_BLOCK - 1, 1);
    span(FLASH_R2, FLASH_R2 + FLASH_BLOCK - 1, 2);
}

static flash_t f;

static int run(void)                                       /* start and tick to completion; returns the ticks */
{
    int n = 0;
    flash_start(&f);
    while (f.running && n < 100000) {
        flash_tick(&f);
        n++;
    }
    return n;
}

enum { TICKS_SIZE = 2 * 2 * (FLASH_BLOCK / FLASH_PIECE),               /* two pairs, ref + upper per piece */
       TICKS_AREA1 = (FLASH_AREA1_HI + 1 - FLASH_AREA1_LO) / FLASH_AREA_PIECE,
       TICKS_AREA2_8M = (FLASH_END_8M + 1 - FLASH_AREA2_LO) / FLASH_AREA_PIECE,
       TICKS_AREA2_16M = (FLASH_END_16M + 1 - FLASH_AREA2_LO + FLASH_AREA_PIECE - 1) / FLASH_AREA_PIECE,
       TICKS_8M = TICKS_SIZE + TICKS_AREA1 + TICKS_AREA2_8M,
       TICKS_16M = TICKS_SIZE + TICKS_AREA1 + TICKS_AREA2_16M };

/* ---- chip size ----------------------------------------------------------------------- */
static void test_8m_part_aliases_and_reads_f8(void) {
    reset(0x800000u); refs(); flash_init(&f);
    CHECK(sizeof(flash_t) <= 0x480);                       /* the room left in the state area */
    int n = run();
    CHECK(flash_take(&f) == 1);
    CHECK(f.size == FLASH_8M && f.area1 == FLASH_EMPTY && f.area2 == FLASH_EMPTY);
    CHECK(n == TICKS_8M && n == 1672 && reads == n);       /* 1.7 s at one piece per tick */
    CHECK(max_len == FLASH_AREA_PIECE && bad_reads == 0);
    CHECK(f.have);                                         /* the readings stay */
    CHECK(last_off + last_len - 1 == FLASH_END_8M);        /* area 2 scanned to the end of 8 MB */
}

static void test_16m_part_with_blank_upper_half_reads_f16(void) {
    reset(0x1000000u); refs(); flash_init(&f);
    int n = run();
    CHECK(flash_take(&f) == 1);
    CHECK(f.size == FLASH_16M && f.area1 == FLASH_EMPTY && f.area2 == FLASH_EMPTY);
    CHECK(n == TICKS_16M && n == 9864 && reads == n);      /* 10 s */
    CHECK(bad_reads == 0 && max_len == FLASH_AREA_PIECE);
    CHECK(last_off == 0xFFFC00u && last_len == 0x3FFu);    /* the last piece stops at 0xFFFFFE */
}

static void test_upper_blocks_neither_identical_nor_blank_are_unknown(void) {
    reset(0x1000000u); refs();
    span(FLASH_R1 + FLASH_UPPER, FLASH_R1 + FLASH_UPPER + FLASH_BLOCK - 1, 9);   /* garbage above both */
    span(FLASH_R2 + FLASH_UPPER, FLASH_R2 + FLASH_UPPER + FLASH_BLOCK - 1, 9);
    flash_init(&f);
    int n = run();
    CHECK(flash_take(&f) == 1 && f.size == FLASH_UNKNOWN);
    CHECK(n == TICKS_8M && last_off + last_len - 1 == FLASH_END_8M);   /* unknown: treated as 8 MB */

    reset(0x1000000u); refs();                             /* one pair garbage, the other blank: still unknown */
    span(FLASH_R1 + FLASH_UPPER, FLASH_R1 + FLASH_UPPER + FLASH_BLOCK - 1, 9);
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.size == FLASH_UNKNOWN);

    reset(0x1000000u); refs();                             /* one pair identical, the other garbage: 8 MB */
    span(FLASH_R1 + FLASH_UPPER, FLASH_R1 + FLASH_UPPER + FLASH_BLOCK - 1, 1);   /* same bytes as R1 */
    span(FLASH_R2 + FLASH_UPPER, FLASH_R2 + FLASH_UPPER + FLASH_BLOCK - 1, 9);
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.size == FLASH_8M);
}

static void test_a_blank_reference_block_is_not_used(void) {
    reset(0x800000u);                                      /* no bootloader bytes: R2 alone decides */
    span(FLASH_R2, FLASH_R2 + FLASH_BLOCK - 1, 2);
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.size == FLASH_8M);

    reset(0x1000000u);
    span(FLASH_R2, FLASH_R2 + FLASH_BLOCK - 1, 2);
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.size == FLASH_16M);

    reset(0x800000u);                                      /* nothing usable: unknown (both blank, so both alias) */
    flash_init(&f);
    int n = run();
    CHECK(flash_take(&f) == 1 && f.size == FLASH_UNKNOWN);
    CHECK(n == TICKS_8M);
}

/* ---- the two areas ------------------------------------------------------------------- */
static void test_any_byte_other_than_ff_makes_an_area_used(void) {
    reset(0x800000u); refs();
    span(0x5A0000u, 0x5A0000u, 5);                         /* one byte in the middle of area 1 */
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.area1 == FLASH_USED && f.area2 == FLASH_EMPTY);

    reset(0x800000u); refs();
    span(FLASH_AREA1_LO, FLASH_AREA1_LO, 5);               /* first and last bytes count */
    span(FLASH_END_8M, FLASH_END_8M, 5);
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.area1 == FLASH_USED && f.area2 == FLASH_USED);

    reset(0x800000u); refs();
    span(FLASH_AREA1_HI, FLASH_AREA1_HI, 5);
    span(FLASH_AREA2_LO, FLASH_AREA2_LO, 5);
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.area1 == FLASH_USED && f.area2 == FLASH_USED);

    reset(0x1000000u); refs();
    span(FLASH_END_16M, FLASH_END_16M, 5);                 /* the last readable byte of 16 MB */
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.size == FLASH_16M && f.area2 == FLASH_USED);
}

static void test_bytes_outside_the_areas_do_not_count(void) {
    reset(0x800000u); refs();
    span(FLASH_AREA1_LO - 1, FLASH_AREA1_LO - 1, 5);       /* the tuning tables' last byte */
    span(FLASH_AREA1_HI + 1, FLASH_AREA1_HI + 1, 5);       /* the first user program */
    span(FLASH_AREA2_LO - 1, FLASH_AREA2_LO - 1, 5);       /* image X copy B's last byte */
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.area1 == FLASH_EMPTY && f.area2 == FLASH_EMPTY);

    reset(0x1000000u); refs();
    span(0xFFFFFFu, 0xFFFFFFu, 5);                         /* the window's last byte: not readable, not checked */
    flash_init(&f); run();
    CHECK(flash_take(&f) == 1 && f.area2 == FLASH_EMPTY && bad_reads == 0);
}

/* ---- pacing and lifecycle ------------------------------------------------------------ */
static void test_one_piece_per_tick_only_while_running(void) {
    reset(0x800000u); refs(); flash_init(&f);
    flash_tick(&f); flash_tick(&f);
    CHECK(reads == 0 && !f.running && flash_take(&f) == 0);   /* idle: nothing read, nothing to take */
    CHECK(flash_start(&f) == 1 && f.running && reads == 0);   /* starting reads nothing yet */
    for (int i = 0; i < 100; i++) flash_tick(&f);
    CHECK(reads == 100 && f.running && flash_take(&f) == 0);
    CHECK(flash_start(&f) == 0);                           /* a second start is ignored */
    int n = 100;
    while (f.running && n < 100000) { flash_tick(&f); n++; }
    CHECK(n == TICKS_8M && reads == n);                    /* the ignored start did not restart the run */
    CHECK(flash_take(&f) == 1 && f.size == FLASH_8M);
    CHECK(flash_take(&f) == 0);                            /* taken once */
    flash_tick(&f); flash_tick(&f);
    CHECK(reads == n && !f.running);                       /* done: no more reads */
    CHECK(flash_start(&f) == 0);                           /* no new run this session */
}

static void test_position_follows_the_read_and_a_run_cannot_restart_once_done(void) {
    reset(0x800000u); refs(); flash_init(&f);
    CHECK(flash_pos(&f) == 0 && !f.have);
    flash_start(&f);
    CHECK(flash_pos(&f) == FLASH_R1);                      /* the next read: R1's first piece */
    flash_tick(&f);
    CHECK(flash_pos(&f) == FLASH_R1 + FLASH_UPPER);        /* then the piece 8 MB higher */
    flash_tick(&f);
    CHECK(flash_pos(&f) == FLASH_R1 + FLASH_PIECE);
    for (int i = 2; i < TICKS_SIZE; i++) flash_tick(&f);
    CHECK(flash_pos(&f) == FLASH_AREA1_LO);                /* area 1 begins */
    flash_tick(&f);
    CHECK(flash_pos(&f) == FLASH_AREA1_LO + FLASH_AREA_PIECE);
    while (f.running) flash_tick(&f);
    CHECK(f.have && flash_take(&f) == 1);
    CHECK(flash_start(&f) == 0 && !f.running);             /* the readings are final for the session */
    CHECK(f.size == FLASH_8M && reads == TICKS_8M);
}

static void test_a_failed_read_does_not_stall_a_run(void) {
    reset(0x800000u); refs();
    fail_off = 0x5A0000u;                                  /* the piece covering this offset fails */
    flash_init(&f);
    int n = run();
    CHECK(!f.running && flash_take(&f) == 1 && n == TICKS_8M);
    CHECK(f.area1 == FLASH_USED);                          /* an unreadable piece is not blank */
}

int main(void)
{
    test_8m_part_aliases_and_reads_f8();
    test_16m_part_with_blank_upper_half_reads_f16();
    test_upper_blocks_neither_identical_nor_blank_are_unknown();
    test_a_blank_reference_block_is_not_used();
    test_any_byte_other_than_ff_makes_an_area_used();
    test_bytes_outside_the_areas_do_not_count();
    test_one_piece_per_tick_only_while_running();
    test_position_follows_the_read_and_a_run_cannot_restart_once_done();
    test_a_failed_read_does_not_stall_a_run();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures != 0;
}
