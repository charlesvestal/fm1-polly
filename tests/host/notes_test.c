/* SPDX-License-Identifier: GPL-3.0-only */
/* notes.c: the note editor's operations. A melody's notes along the song (the select order: place, then pitch, a chord's
 * notes one by one; a move across a section's end re-anchored, its root the chord's there; a pitch change; refused onto
 * the same note; the root at a place, with the transpositions); a groove instrument's (the order, enter, the same note
 * replaced, delete keeping the other instruments' notes, a move, full at ARR_NEV) */
#include <stdio.h>
#include <string.h>
#include "arr.h"
#include "leads.h"
#include "loops.h"
#include "notes.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static leads_t L;
static nmap_t M;
static pattern_t P;
static void add(int row, uint32_t pos, int note, int trk)
{
    lead_ev_t e;
    lead_set(&e, row, pos, 240, note, 100, trk, 0);
    leads_insert(&L, &e, LEAD_MAX, 0);
}
static int ordered(void)
{
    for (uint32_t i = 1; i < L.n; i++)
        if (lead_cmp(lead_at(&L, i - 1), lead_at(&L, i)) >= 0)
            return 0;
    return L.row0[ARR_ROWS] == L.n;
}
/* three sections of 4 bars of 4 (7680 ticks), the second x2 */
static void map3(void)
{
    M.n = 3;
    M.start[0] = 0;
    M.len[0] = 7680;
    M.start[1] = 7680;
    M.len[1] = 15360;
    M.start[2] = 23040;
    M.len[2] = 7680;
    M.start[3] = 30720;
}

static void test_order(void)
{
    nkey_t k = {0, 0};
    int got[8], n = 0;
    leads_clear(&L);
    map3();
    add(1, 480, 64, 0);                          /* song tick 8160 */
    add(0, 960, 67, 0);
    add(0, 960, 60, 0);                          /* a chord at 960: 60 then 67 */
    add(0, 960, 62, 1);                          /* M2's: never M1's */
    add(2, 0, 72, 0);                            /* 23040 */
    add(1, 15360, 50, 0);                        /* past section 2's length: kept, not edited */
    while (nl_near(&L, &M, 0, &k, n ? 1 : 0) && n < 8)
        got[n++] = (int)k.t * 1000 + k.note;
    CHECK(n == 4 && got[0] == 960060 && got[1] == 960067 && got[2] == 8160064 && got[3] == 23040072,
          "M1 in order: place, then pitch (%d: %d %d %d %d)", n, got[0], got[1], got[2], got[3]);
    k.t = 23040;
    k.note = 72;
    CHECK(nl_near(&L, &M, 0, &k, -1) && k.t == 8160 && k.note == 64, "back: the one before");
    CHECK(nl_near(&L, &M, 0, &k, -1) && k.t == 960 && k.note == 67, "back into the chord: its top note");
    k.t = 961;
    k.note = 0;
    CHECK(nl_near(&L, &M, 0, &k, 0) && k.t == 8160, "the first at or after a place");
    k.t = 23041;
    CHECK(!nl_near(&L, &M, 0, &k, 0), "none after the last");
    k.t = 960;
    k.note = 62;
    CHECK(nl_find(&L, &M, 1, k) >= 0 && nl_find(&L, &M, 0, k) < 0, "found by track");
    CHECK(nmap_sect(&M, 7679) == 0 && nmap_sect(&M, 7680) == 1 && nmap_sect(&M, 23039) == 1 && nmap_sect(&M, 30720) < 0,
          "the sections by song tick");
}
static void test_move(void)
{
    nkey_t k = {8160, 64};
    int i, j;
    leads_clear(&L);
    map3();
    add(1, 480, 64, 0);
    add(1, 480, 67, 0);
    i = nl_find(&L, &M, 0, k);
    j = nl_move(&L, &M, i, 8160 + 480, 64, -1);
    CHECK(j >= 0 && lead_pos(lead_at(&L, (uint32_t)j)) == 960 && lead_at(&L, (uint32_t)j)->row == 1 && ordered(),
          "moved on a beat in its section");
    j = nl_move(&L, &M, j, 23040 + 240, 64, 7);  /* into section 3: re-anchored, its root the chord's there */
    CHECK(j >= 0 && lead_at(&L, (uint32_t)j)->row == 2 && lead_pos(lead_at(&L, (uint32_t)j)) == 240 &&
              lead_root(lead_at(&L, (uint32_t)j)) == 7 && lead_len(lead_at(&L, (uint32_t)j)) == 240 &&
              lead_at(&L, (uint32_t)j)->vel == 100 && ordered() && L.row0[2] == 1 && L.row0[3] == 2,
          "across a section's end: its row, place, root; length and velocity kept");
    j = nl_move(&L, &M, j, 7680 - 1, 64, 0);
    CHECK(j >= 0 && lead_at(&L, (uint32_t)j)->row == 0 && lead_pos(lead_at(&L, (uint32_t)j)) == 7679, "back, to section 1's end");
    CHECK(nl_move(&L, &M, j, 30720, 64, 0) == -1, "past the song: refused");
    k.t = 8160;
    k.note = 67;
    i = nl_find(&L, &M, 0, k);
    CHECK(nl_move(&L, &M, i, ~0u, 64, -1) >= 0 && L.n == 2, "a pitch changed, its place kept");
    k.note = 64;
    i = nl_find(&L, &M, 0, k);
    CHECK(i >= 0 && nl_move(&L, &M, i, 7679, 64, -1) == -1, "onto the same note: refused");
    leads_erase_at(&L, (uint32_t)i, 1);
    CHECK(L.n == 1 && ordered(), "deleted");
}
static void test_root(void)
{
    static song_t g;
    static loop_t lp[ARR_LOOPS];
    chord_t f = {5, CH_MAJ, CH_NOBASS}, a = {9, CH_MIN, CH_NOBASS};
    memset(&g, 0, sizeof g);
    loops_default(lp);
    loop_clear(&lp[9], 9);
    loop_write(&lp[9], 0, f);                    /* P10: F, bar 2 Am (2 bars of 4) */
    loop_write(&lp[9], 4, a);
    g.nrows = 2;
    g.row[0].loop = 9;
    g.row[0].rep = 2;
    g.row[1].loop = 9;
    g.row[1].key = 2;
    CHECK(nl_root_at(&g, lp, 0, 0) == 5 && nl_root_at(&g, lp, 0, 4 * 480) == 9 && nl_root_at(&g, lp, 0, 4 * 480 - 1) == 5,
          "the chord on its beat");
    CHECK(nl_root_at(&g, lp, 0, 8 * 480 + 100) == 5, "the second pass: round the progression");
    CHECK(nl_root_at(&g, lp, 1, 4 * 480) == 11, "the section's transpose");
    g.transpose = -6;
    CHECK(nl_root_at(&g, lp, 1, 0) == 1, "and the song's");
}
static int pvalid(void)
{
    for (int t = 0; t < ARR_TRACKS; t++) {
        const arr_trk_t *k = &P.trk[t];
        if (k->first + (uint32_t)k->count > P.nev)
            return 0;
        for (uint32_t i = k->first + 1u; i < (uint32_t)k->first + k->count; i++)
            if (P.ev[i].tick < P.ev[i - 1].tick)
                return 0;
    }
    return 1;
}
static void test_pattern(void)
{
    nkey_t k = {0, 0};
    int i, n = 0, got[6];
    memset(&P, 0, sizeof P);
    P.bars = 2;
    P.beats = 4;
    CHECK(np_insert(&P, 3, 960, 240, 40, 100) >= 0 && np_insert(&P, 3, 0, 240, 43, 100) >= 0 &&
              np_insert(&P, 0, 480, 60, 38, 90) >= 0 && np_insert(&P, 3, 0, 240, 36, 100) >= 0 &&
              np_insert(&P, 7, 0, 960, 60, 80) >= 0 && np_insert(&P, 3, 4000, 240, 30, 100) >= 0,
          "notes in");
    CHECK(P.nev == 6 && P.trk[3].count == 4 && P.trk[0].count == 1 && P.trk[7].count == 1 && pvalid(), "kept per instrument");
    while (np_near(&P, 3, 3840, &k, n ? 1 : 0) && n < 6)
        got[n++] = (int)k.t * 1000 + k.note;
    CHECK(n == 3 && got[0] == 36 && got[1] == 43 && got[2] == 960040, "BA in order, the one past the groove's end left out");
    CHECK(np_insert(&P, 3, 0, 480, 36, 50) >= 0 && P.nev == 6, "the same note: replaced");
    k.t = 0;
    k.note = 36;
    i = np_find(&P, 3, k);
    CHECK(i >= 0 && P.ev[i].len == 480 && P.ev[i].vel == 50, "its length, velocity");
    i = np_move(&P, 3, i, 1440, 36);
    CHECK(i >= 0 && P.ev[i].tick == 1440 && P.ev[i].len == 480 && pvalid(), "moved, in order");
    k.t = 960;
    k.note = 40;
    CHECK(np_move(&P, 3, i, 960, 40) == -1, "onto the same note: refused");
    np_delete(&P, 3, np_find(&P, 3, k));
    CHECK(P.nev == 5 && P.trk[3].count == 3 && P.trk[7].count == 1 && P.ev[P.trk[7].first].note == 60 && pvalid(),
          "deleted: the other instruments' notes kept");
    k.t = 480;
    k.note = 38;
    i = np_find(&P, 0, k);
    CHECK(i >= 0 && np_move(&P, 0, i, ~0u, 40) >= 0 && P.ev[P.trk[0].first].note == 40, "a drum hit's sound changed");
    while (P.nev < ARR_NEV)
        np_insert(&P, 5, P.nev, 10, 60, 100);
    CHECK(np_insert(&P, 2, 0, 10, 60, 100) == -1 && pvalid(), "full: refused");
}

int main(void)
{
    test_order();
    test_move();
    test_root();
    test_pattern();
    printf("notes: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
