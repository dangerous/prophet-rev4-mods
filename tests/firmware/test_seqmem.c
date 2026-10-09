/* Host harness for sequence memory (docs/SPEC.md: "Sequence memory"): the block's format and
 * placement, save and load round trips, "no sequence", staleness (slot, checksum) and
 * corruption, the write discipline (verified writer, whole sectors, inside the blocks only).
 * The fake flash is the upper half of a 16 MB part: the blocks, 0xFF elsewhere.
 * `make test-firmware`. */
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "seqmem.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- fake flash --------------------------------------------------------------------- */
static uint8_t blocks[SEQMEM_SLOTS][SEQMEM_BLOCK];
static int writes, bad_writes, reads;
static uint32_t write_off[16], write_len[16], max_read;
static int params[SEQMEM_PARAMS];

int plat_param_read(int p) { return params[p]; }
void plat_voice_on(int src, int note, int vel) { (void)src; (void)note; (void)vel; }   /* seq.c / arp.c link against these; unused here */
void plat_voice_off(int src, int note) { (void)src; (void)note; }
void plat_live_off(int src, int note) { (void)src; (void)note; }

int plat_flash_read(uint32_t off, void *dst, uint32_t len)
{
    reads++;
    if (len > max_read) max_read = len;
    if (off >= 0x1000000u || len > 0xFFFFFFu - off) return 3;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t a = off + i;
        ((uint8_t *)dst)[i] = (a >= SEQMEM_BASE && a < SEQMEM_END)
            ? blocks[(a - SEQMEM_BASE) / SEQMEM_BLOCK][(a - SEQMEM_BASE) % SEQMEM_BLOCK] : 0xFF;
    }
    return 0;
}

int plat_flash_write(uint32_t off, const void *src, uint32_t len)   /* stock's verified writer */
{
    if (writes < 16) { write_off[writes] = off; write_len[writes] = len; }
    writes++;
    if (off % SEQMEM_SECTOR || len % SEQMEM_SECTOR || len == 0 || off < SEQMEM_BASE || off + len > SEQMEM_END) {
        bad_writes++;                                      /* never outside the blocks, never a partial sector */
        return 4;
    }
    for (uint32_t i = 0; i < len; i++) {
        uint32_t a = off + i;
        blocks[(a - SEQMEM_BASE) / SEQMEM_BLOCK][(a - SEQMEM_BASE) % SEQMEM_BLOCK] = ((const uint8_t *)src)[i];
    }
    return 0;
}

static void reset(void)
{
    memset(blocks, 0xFF, sizeof blocks);
    memset(params, 0, sizeof params);
    writes = bad_writes = reads = 0;
    max_read = 0;
}

/* ---- helpers ------------------------------------------------------------------------- */
static seq_t q, q2;
static uint8_t sector[SEQMEM_SECTOR], piece[SEQMEM_PIECE];

static void fill(seq_t *s, int events, int notes_per_chord, int base_note, int dur)
{
    seq_init(s);
    for (int i = 0; i < events; i++) {
        s->ev[i].n = (uint8_t)notes_per_chord;
        for (int k = 0; k < notes_per_chord; k++) {
            s->ev[i].note[k] = (uint8_t)((base_note + i * 2 + k * 3) % 128);
            s->ev[i].vel[k] = (uint8_t)(40 + (i + k) % 80);
        }
        s->ev[i].dur = (uint16_t)dur;
    }
    s->len = (uint16_t)events;
    s->total = (uint16_t)(events * dur);
}

static int same_events(const seq_t *a, const seq_t *b)
{
    if (a->len != b->len || a->total != b->total) return 0;
    for (int i = 0; i < a->len; i++) {
        if (a->ev[i].n != b->ev[i].n || a->ev[i].dur != b->ev[i].dur) return 0;
        for (int k = 0; k < a->ev[i].n; k++)
            if (a->ev[i].note[k] != b->ev[i].note[k] || a->ev[i].vel[k] != b->ev[i].vel[k]) return 0;
    }
    return 1;
}

static const seqmem_settings_t S1 = { 7, 1, 2, 8, -5, 1, 16 };   /* rate code 7, ArP style, Pnd, 2 bars, -5, SEq, gate 85 % */

/* ---- slots and checksum ---------------------------------------------------------------- */
static void test_slot_mapping(void) {
    CHECK(seqmem_slot(0, 0, 0, 0) == 0);
    CHECK(seqmem_slot(0, 0, 0, 7) == 7);
    CHECK(seqmem_slot(0, 0, 1, 0) == 8);                  /* (5 bank + group) * 8 + prog */
    CHECK(seqmem_slot(0, 1, 0, 0) == 40);
    CHECK(seqmem_slot(0, 4, 4, 7) == 199);
    CHECK(seqmem_slot(1, 0, 0, 0) == -1);                 /* factory: no block */
    CHECK(seqmem_slot(0, 5, 0, 0) == -1 && seqmem_slot(0, 0, 5, 0) == -1 && seqmem_slot(0, 0, 0, 8) == -1);
    CHECK(SEQMEM_BASE + 199 * SEQMEM_BLOCK + SEQMEM_BLOCK == SEQMEM_END && SEQMEM_END == 0xB20000u);
}

static void test_checksum_sums_the_99_parameters(void) {
    reset();
    CHECK(seqmem_checksum() == 0);
    params[0] = 1; params[98] = 200; params[50] = 127;
    CHECK(seqmem_checksum() == 328);
    params[98] = 201;
    CHECK(seqmem_checksum() == 329);
}

/* ---- round trips --------------------------------------------------------------------- */
static void test_save_then_load_restores_events_and_settings(void) {
    seqmem_settings_t s = {0};
    reset();
    params[3] = 9; params[93] = 41;
    fill(&q, 4, 3, 60, 2);
    q.ev[1].n = 0;                                         /* a rest */
    q.ev[2].dur = 3;
    q.total = 2 + 2 + 3 + 2;
    CHECK(seqmem_save(5, &q, &S1, sector) == 0);
    CHECK(writes == 1 && bad_writes == 0 && write_off[0] == SEQMEM_BASE + 5 * SEQMEM_BLOCK && write_len[0] == SEQMEM_SECTOR);
    CHECK(memcmp(blocks[5], "PSQ2", 4) == 0 && blocks[5][7] == 5 && (blocks[5][6] & 1) == 1);
    CHECK(SEQMEM_HEADER == 17 && blocks[5][16] == 16);                  /* the gate, last of the settings */
    CHECK(blocks[5][8] == (uint8_t)(50) && blocks[5][9] == 0);          /* checksum 9 + 41 */
    seq_init(&q2);
    CHECK(seqmem_load(5, &q2, &s, piece) == SEQMEM_LOADED);
    CHECK(same_events(&q, &q2));
    CHECK(s.rate_code == 7 && s.style == 1 && s.order == 2 && s.chord_beats == 8 && s.transpose == -5 && s.gen == 1 && s.gate == 16);
    CHECK(max_read <= SEQMEM_PIECE);
}

static void test_the_largest_sequence_fits_and_takes_three_sectors(void) {
    seqmem_settings_t s = {0};
    reset();
    fill(&q, 512, 10, 20, 1);                              /* 512 ten-note chords: 17 + 512 * 23 = 11,793 bytes */
    CHECK(seqmem_encode(&q, &S1, 0, 0, sector, 0) == 17 + 512 * 23);
    CHECK(seqmem_save(199, &q, &S1, sector) == 0);
    CHECK(writes == 3 && bad_writes == 0);                 /* one verified write per sector, three sectors */
    for (int k = 0; k < 3; k++)
        CHECK(write_off[k] == SEQMEM_BASE + 199 * SEQMEM_BLOCK + k * SEQMEM_SECTOR && write_len[k] == SEQMEM_SECTOR);
    seq_init(&q2);
    CHECK(seqmem_load(199, &q2, &s, piece) == SEQMEM_LOADED && same_events(&q, &q2));
    CHECK(q2.len == 512 && q2.total == 512);
}

static void test_no_sequence_is_saved_explicitly_and_leaves_the_live_one_alone(void) {
    seqmem_settings_t s = S1;
    reset();
    seq_init(&q);                                          /* nothing recorded */
    CHECK(seqmem_save(2, &q, &S1, sector) == 0 && writes == 1 && bad_writes == 0);
    CHECK(memcmp(blocks[2], "PSQ2", 4) == 0 && (blocks[2][6] & 1) == 0);
    fill(&q2, 3, 2, 50, 1);
    CHECK(seqmem_load(2, &q2, &s, piece) == SEQMEM_NONE);
    CHECK(q2.len == 3 && q2.ev[0].n == 2 && s.rate_code == 7);   /* untouched */
}

/* ---- staleness and corruption --------------------------------------------------------- */
static void test_erased_wrong_slot_or_changed_program_is_no_valid_block(void) {
    seqmem_settings_t s = {0};
    reset();
    fill(&q2, 2, 1, 70, 1);
    CHECK(seqmem_load(7, &q2, &s, piece) == SEQMEM_NONE && q2.len == 2);   /* erased */
    fill(&q, 3, 2, 60, 1);
    params[10] = 5;
    CHECK(seqmem_save(7, &q, &S1, sector) == 0);
    CHECK(seqmem_load(8, &q2, &s, piece) == SEQMEM_NONE && q2.len == 2);   /* another program's slot */
    memcpy(blocks[8], blocks[7], SEQMEM_BLOCK);            /* a copied block still names slot 7 */
    CHECK(seqmem_load(8, &q2, &s, piece) == SEQMEM_NONE && q2.len == 2);
    params[10] = 6;                                        /* the program was overwritten */
    CHECK(seqmem_load(7, &q2, &s, piece) == SEQMEM_NONE && q2.len == 2);
    params[10] = 5;
    CHECK(seqmem_load(7, &q2, &s, piece) == SEQMEM_LOADED && q2.len == 3);
}

static void test_corrupt_blocks_are_rejected(void) {
    seqmem_settings_t s = {0};
    reset();
    fill(&q, 3, 2, 60, 1);
    CHECK(seqmem_save(1, &q, &S1, sector) == 0);
    uint8_t good[SEQMEM_BLOCK];
    memcpy(good, blocks[1], SEQMEM_BLOCK);
    blocks[1][0] = 'X';                                    /* magic */
    fill(&q2, 1, 1, 40, 1);
    CHECK(seqmem_load(1, &q2, &s, piece) == SEQMEM_NONE && q2.len == 1);
    memcpy(blocks[1], good, SEQMEM_BLOCK);
    blocks[1][4] = 0xFF; blocks[1][5] = 0x7F;              /* length beyond the block */
    CHECK(seqmem_load(1, &q2, &s, piece) == SEQMEM_NONE && q2.len == 1);
    memcpy(blocks[1], good, SEQMEM_BLOCK);
    blocks[1][19] = 11;                                    /* first event: eleven notes */
    CHECK(seqmem_load(1, &q2, &s, piece) == SEQMEM_NONE && q2.len == 1);
    memcpy(blocks[1], good, SEQMEM_BLOCK);
    blocks[1][20] = 128;                                   /* first note out of range */
    CHECK(seqmem_load(1, &q2, &s, piece) == SEQMEM_NONE && q2.len == 1);
    memcpy(blocks[1], good, SEQMEM_BLOCK);
    blocks[1][17] = 0; blocks[1][18] = 0;                  /* a zero duration */
    CHECK(seqmem_load(1, &q2, &s, piece) == SEQMEM_NONE && q2.len == 1);
    memcpy(blocks[1], good, SEQMEM_BLOCK);
    blocks[1][4] = 17; blocks[1][5] = 0;                   /* header only, but the flag says a sequence */
    CHECK(seqmem_load(1, &q2, &s, piece) == SEQMEM_NONE && q2.len == 1);
    memcpy(blocks[1], good, SEQMEM_BLOCK);
    blocks[1][16] = 20;                                    /* a gate beyond 100 % */
    CHECK(seqmem_load(1, &q2, &s, piece) == SEQMEM_NONE && q2.len == 1);
    memcpy(blocks[1], good, SEQMEM_BLOCK);
    blocks[1][3] = '1';                                    /* a PSQ1 block, before the gate: no valid block */
    CHECK(seqmem_load(1, &q2, &s, piece) == SEQMEM_NONE && q2.len == 1);
    memcpy(blocks[1], good, SEQMEM_BLOCK);
    CHECK(seqmem_load(1, &q2, &s, piece) == SEQMEM_LOADED && q2.len == 3);
}

static void test_a_write_error_is_reported_and_nothing_else_is_written(void) {
    reset();
    fill(&q, 3, 2, 60, 1);
    CHECK(seqmem_save(200, &q, &S1, sector) != 0);         /* no such slot: refused, no write */
    CHECK(seqmem_save(-1, &q, &S1, sector) != 0 && writes == 0);
}

int main(void)
{
    test_slot_mapping();
    test_checksum_sums_the_99_parameters();
    test_save_then_load_restores_events_and_settings();
    test_the_largest_sequence_fits_and_takes_three_sectors();
    test_no_sequence_is_saved_explicitly_and_leaves_the_live_one_alone();
    test_erased_wrong_slot_or_changed_program_is_no_valid_block();
    test_corrupt_blocks_are_rejected();
    test_a_write_error_is_reported_and_nothing_else_is_written();
    printf("%s: %d checks, %d failures\n", __FILE__, checks, failures);
    return failures != 0;
}
