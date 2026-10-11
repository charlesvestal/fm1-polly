/* SPDX-License-Identifier: GPL-3.0-only */
/* leads.c: the lead notes' list (order, replace the same note, the per-row index, the two parts), erasing, the
 * renumbering on a row inserted or deleted, the list as loaded put right */
#include <stdio.h>
#include <string.h>
#include "arr.h"
#include "leads.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static leads_t L;
static int add(int row, uint32_t pos, int note, int trk)
{
    lead_ev_t e;
    lead_set(&e, row, pos, 240, note, 100, trk, 0);
    return leads_insert(&L, &e, LEAD_MAX, 0);
}
static int ordered(void)
{
    for (uint32_t i = 1; i < L.n; i++)
        if (lead_cmp(lead_at(&L, i - 1), lead_at(&L, i)) >= 0)
            return 0;
    for (int r = 0; r < ARR_ROWS; r++)
        for (uint32_t i = L.row0[r]; i < L.row0[r + 1]; i++)
            if (lead_at(&L, i)->row != r)
                return 0;
    return L.row0[0] == 0 && L.row0[ARR_ROWS] == L.n && L.part[0].n + L.part[1].n == L.n;
}

static void test_note(void)
{
    lead_ev_t e;
    lead_set(&e, 31, 245760u, 70000u, 72, 90, 1, 5);
    CHECK(sizeof e == 9, "9 bytes");
    CHECK(e.row == 31 && lead_pos(&e) == 245760u && lead_len(&e) == 65535u && e.note == 72 && e.vel == 90 &&
              lead_trk(&e) == 1 && lead_root(&e) == 5, "round trip (a length past 16 bits: 65535)");
    lead_set(&e, 0, 0, 0, 60, 1, 0, 11);
    CHECK(lead_len(&e) == 1 && lead_trk(&e) == 0 && lead_root(&e) == 11, "a length of 0: 1");
    CHECK(lead_shift(5, 0) == 5 && lead_shift(7, 0) == -5 && lead_shift(0, 0) == 0 && lead_shift(6, 0) == -6 &&
              lead_shift(0, 6) == -6 && lead_shift(2, 9) == 5 && lead_shift(9, 2) == -5, "the root's move, -6..+5");
}
static void test_insert(void)
{
    int at;
    lead_ev_t e;
    leads_clear(&L);
    CHECK(L.n == 0 && ordered(), "empty");
    CHECK(add(2, 960, 64, 0) == 1 && add(0, 480, 60, 0) == 1 && add(2, 0, 67, 1) == 1 && add(0, 480, 62, 0) == 1 &&
              add(0, 480, 62, 1) == 1, "five added");
    CHECK(L.n == 5 && ordered(), "in order, indexed");
    CHECK(L.row0[0] == 0 && L.row0[1] == 3 && L.row0[2] == 3 && L.row0[3] == 5, "row 1: 3 notes, row 2 none, row 3: 2");
    lead_set(&e, 0, 480, 960, 62, 30, 0, 7);
    CHECK(leads_insert(&L, &e, LEAD_MAX, &at) == 0 && L.n == 5 && at == 1, "the same note at the same place: replaced (%d)", at);
    CHECK(lead_len(lead_at(&L, 1)) == 960 && lead_at(&L, 1)->vel == 30 && lead_root(lead_at(&L, 1)) == 7, "its length, velocity, root");
    CHECK(lead_trk(lead_at(&L, 2)) == 1 && lead_at(&L, 2)->note == 62, "the same pitch on LD2: another note");
    CHECK(leads_count(&L, 0, -1) == 3 && leads_count(&L, 0, 1) == 1 && leads_count(&L, -1, 0) == 3 && leads_count(&L, 1, -1) == 0,
          "counts by row and track");
    CHECK(add(5, 0, 50, 0) == 1 && L.n == 6, "the room: a sixth");
    lead_set(&e, 9, 0, 1, 50, 1, 0, 0);
    CHECK(leads_insert(&L, &e, 6, 0) == -1 && L.n == 6, "max 6: full");
}
static void test_parts(void)
{
    leads_clear(&L);
    for (int i = 0; i < LEAD_MAX; i++)          /* backwards: each goes in first */
        add((LEAD_MAX - 1 - i) / 30, (uint32_t)((LEAD_MAX - 1 - i) % 30) * 120u, 60, 0);
    CHECK(L.n == LEAD_MAX && ordered(), "840 in order across the two parts");
    CHECK(L.part[0].n == LEAD_PART_NOTES && L.part[1].n == LEAD_PART_NOTES, "part 0 full, then part 1");
    CHECK(lead_at(&L, LEAD_PART_NOTES) == &L.part[1].ev[0], "index 420: part 1's first");
    CHECK(add(31, 0, 61, 0) == -1, "841: full");
    CHECK(leads_parts(0) == 0 && leads_parts(420) == 1 && leads_parts(421) == 2, "parts for a count");
    CHECK(leads_parts_from(10, 400, 401) == 1u && leads_parts_from(10, 420, 421) == 3u && leads_parts_from(500, 600, 601) == 2u &&
              leads_parts_from(10, 421, 420) == 3u, "the parts a change touches");
    CHECK(leads_erase(&L, -1, -1) == LEAD_MAX && L.n == 0 && L.part[1].n == 0 && ordered(), "erase all");
}
static void test_erase_rows(void)
{
    leads_clear(&L);
    for (int r = 0; r < 6; r++) {
        add(r, 0, 60 + r, 0);
        add(r, 480, 70 + r, 1);
    }
    add(31, 0, 40, 0);
    CHECK(leads_erase(&L, 2, 0) == 1 && L.n == 12 && leads_count(&L, 2, -1) == 1 && ordered(), "LD1 in row 3: one note");
    CHECK(leads_erase(&L, -1, 1) == 6 && L.n == 6 && leads_count(&L, -1, 1) == 0 && ordered(), "LD2 in every row");
    /* rows inserted at row 2: rows 2.. one on; row 31's note has nowhere to go */
    CHECK(leads_row_insert(&L, 2) == 1 && L.n == 5 && ordered(), "a row inserted: the last row's note goes");
    CHECK(leads_count(&L, 2, -1) == 0 && leads_count(&L, 3, -1) == 0 && lead_at(&L, L.row0[4])->note == 63 &&
              lead_at(&L, L.row0[1])->note == 61, "row 3 (index 2) now empty; row 4 had index 3's (D#4: 63)");
    CHECK(leads_row_delete(&L, 1) == 1 && L.n == 4 && ordered(), "row 2 deleted: its note goes");
    CHECK(lead_at(&L, L.row0[0])->note == 60 && leads_count(&L, 1, -1) == 0 && leads_count(&L, 2, -1) == 0 &&
              lead_at(&L, L.row0[3])->note == 63 && lead_at(&L, L.row0[4])->note == 64 && lead_at(&L, L.row0[5])->note == 65,
          "the rows after it one back (rows 2 and 3 empty: the deleted row's neighbours)");
    CHECK(leads_row_insert(&L, 40) == 0 && leads_row_delete(&L, -1) == 0 && L.n == 4, "rows out of range: nothing");
}
static void test_fix(void)
{
    memset(&L, 0, sizeof L);
    /* as loaded: part 0 holds 3 (out of order, one bad), part 1 holds 2 (part 0 not full: they move up) */
    lead_set(&L.part[0].ev[0], 1, 0, 100, 60, 90, 0, 0);
    lead_set(&L.part[0].ev[1], 0, 480, 100, 62, 90, 0, 0);
    lead_set(&L.part[0].ev[2], 0, 0, 100, 64, 90, 0, 0);
    L.part[0].ev[2].row = 40;                   /* no such row */
    lead_set(&L.part[1].ev[0], 0, 0, 100, 65, 90, 1, 3);
    lead_set(&L.part[1].ev[1], 0, 480, 100, 62, 50, 0, 0);   /* the same note as part 0's second: one kept */
    CHECK(leads_fix(&L, 3, 2) == 1, "moved");
    CHECK(L.n == 3 && ordered() && L.part[1].n == 0, "3 left, in order, all in part 0 (%d)", L.n);
    CHECK(lead_at(&L, 0)->note == 65 && lead_at(&L, 1)->note == 62 && lead_at(&L, 2)->note == 60, "LD2's at 0, D at 480, row 2's C");
    CHECK(lead_at(&L, 1)->vel == 50, "of the same note twice, the later kept");
    CHECK(leads_fix(&L, L.part[0].n, L.part[1].n) == 0 && L.n == 3, "as it is now: nothing moves");
}
static void test_defaults(void)
{
    lead_trk_t t;
    lead_trk_default(&t, 0);
    CHECK(t.program == 80 && t.chan == 6 && t.vol == 100 && t.pan == 64, "LD1: GM 81 on channel 7");
    lead_trk_default(&t, 1);
    CHECK(t.program == 48 && t.chan == 7, "LD2: GM 49 on channel 8");
}
int main(void)
{
    test_note();
    test_insert();
    test_parts();
    test_erase_rows();
    test_fix();
    test_defaults();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("leads test passed\n");
    return 0;
}
