/* SPDX-License-Identifier: GPL-3.0-only */
/* loops.c: packed chords, loop writing (growth, the 64-beat limit), factory loops, song rows */
#include <stdio.h>
#include <string.h>
#include "arr.h"
#include "loops.h"
#include "templates.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static chord_t ch(int r, int t) { chord_t c = {(uint8_t)r, (uint8_t)t, CH_NOBASS}; return c; }

static void test_pack(void)
{
    chord_t c = {9, CH_MIN7, 4}, d = lc_unpack(lc_pack(c)), e = lc_unpack(lc_pack(ch(0, CH_NONE)));
    CHECK(d.root == 9 && d.type == CH_MIN7 && d.bass == 4, "Am7/E round trip");
    CHECK(e.type == CH_NONE && e.bass == CH_NOBASS, "--- round trip, no bass");
    CHECK(lc_pack(ch(11, CH_DIM)) != LC_SAME, "never LC_SAME");
}
static void test_write(void)
{
    loop_t l;
    loop_clear(&l, 8);
    CHECK(l.bars == 0 && l.beats == 4 && loop_len(&l) == 0 && strcmp(l.name, "Loop 9") == 0, "empty, named by its number");
    CHECK(loop_write(&l, 0, ch(9, CH_MIN)) == 0 && l.bars == 1, "beat 0: one bar");
    CHECK(loop_write(&l, 4, ch(5, CH_MAJ)) == 0 && l.bars == 2, "beat 4: two bars");
    CHECK(loop_write(&l, 13, ch(7, CH_MAJ)) == 0 && l.bars == 4, "beat 13: four bars");
    CHECK(l.ch[1] == LC_SAME && l.ch[4] == lc_pack(ch(5, CH_MAJ)), "the rest no change");
    CHECK(loop_write(&l, 64, ch(0, CH_MAJ)) == -1 && l.bars == 4, "past 64 beats: refused");
    CHECK(loop_chord_at(&l, 6).root == 5 && loop_chord_at(&l, 2).root == 9, "the chord sounding at a beat");
    CHECK(loop_chord_at(&l, 0).root == 9, "beat 0: its own");
    l.ch[0] = LC_SAME;
    CHECK(loop_chord_at(&l, 0).root == 7, "beat 0 without a chord: the loop's last (it goes round)");
    loop_set_beats(&l, 7);
    CHECK(l.beats == 7 && l.bars == 4 && loop_len(&l) == 28, "7 beats: 4 bars fit");
    loop_set_bars(&l, 16);
    CHECK(l.bars == 9, "7 beats: 9 bars at most (%d)", l.bars);
    loop_set_beats(&l, 4);
    loop_set_bars(&l, 16);
    CHECK(l.bars == 16 && loop_len(&l) == 64, "4 beats: 16 bars");
    loop_unwrite(&l, 4);
    CHECK(l.ch[4] == LC_SAME, "unwrite beat 4");
    loop_clear(&l, 9);
    CHECK(strncmp(l.name, "Loop 10", 8) == 0, "Loop 10");
    loop_clear(&l, 15);
    CHECK(strncmp(l.name, "Loop 16", 8) == 0, "Loop 16");
    CHECK(loop_chord_at(&l, 0).type == CH_NONE, "an empty loop: no chord");
    loop_set_beats(&l, 7);
    CHECK(loop_write(&l, 63, ch(0, CH_MAJ)) == -1, "7 beats: beat 63 refused");
}
static void test_factory(void)
{
    loop_t l;
    static const int L18[8] = {0, 1, 2, 3, 4, 7, 9, 8};   /* L1..L8: Pop, Ballad, Fifties, ii-V-I, Blues, Minor, Flamenco, Canon */
    for (int i = 0; i < 8; i++)
        CHECK(FACTORY_LOOP[i] == L18[i], "factory loop %d is template %d", i, L18[i]);
    for (int t = 0; t < NTEMPLATES; t++)
        CHECK(strlen(TEMPLATES[t].name) <= 8, "%s: a loop's name, 8 characters at most", TEMPLATES[t].name);
    loop_from_template(&l, 4);
    CHECK(l.bars == 12 && l.beats == 4 && strcmp(l.name, "Blues") == 0, "the blues: 12 bars");
    CHECK(lc_unpack(l.ch[4]).root == 5 && l.ch[5] == LC_SAME, "a chord a bar: F7 on bar 2");
    loop_from_template(&l, 15);
    CHECK(l.bars == 4 && l.beats == 3 && lc_unpack(l.ch[6]).type == CH_7, "the waltz in 3: C7 on bar 3");
}
static void test_rows(void)
{
    song_t g = {0};
    loops_song_clear(&g);
    CHECK(g.nrows == 0 && g.intro == 1 && g.ending == 1 && g.at_end == 0 && g.tempo == 120, "an empty song");
    CHECK(row_tap(&g, 0, 8) == 0 && g.nrows == 1 && g.row[0].loop == 8 && g.row[0].rep == 1 && g.row[0].sect == ROW_A,
          "tap L9: row 1, x1, Main A");
    CHECK(row_tap(&g, 1, 8) == 1 && g.nrows == 2 && g.row[1].loop == 8 && g.row[1].rep == 1 && g.row[0].rep == 1,
          "L9 again at the END row: a row of its own, never a REPEAT");
    CHECK(row_tap(&g, 1, 9) == 1 && g.nrows == 2 && g.row[1].loop == 9, "a tap on the last row replaces its loop");
    g.row[1].sect = ROW_B;
    g.row[1].rep = 2;
    CHECK(row_tap(&g, 1, 9) == 1 && g.row[1].rep == 1 && g.nrows == 2, "the same loop on its row: the row again, no REPEAT");
    g.row[1].sect = ROW_B;
    row_tap(&g, 2, 8);
    row_tap(&g, 3, 9);
    CHECK(g.row[2].sect == ROW_A && g.row[3].sect == ROW_B, "a loop's section from its earlier row");
    CHECK(g.nrows == 4 && row_tap(&g, 5, 9) == -1, "past the END row: nothing");
    CHECK(row_tap(&g, 0, 9) == 0 && g.row[0].loop == 9 && g.row[0].rep == 1, "a tap on an earlier row rewrites it");
    g.row[0].loop = 8;
    CHECK(loop_rows_using(&g, 9) == 2, "L10 in 2 rows");
    row_insert(&g, 1);
    CHECK(g.nrows == 5 && g.row[1].loop == 9 && g.row[2].loop == 9 && g.row[1].sect == g.row[2].sect,
          "insert: a copy of the cursor's row");
    row_delete(&g, 1);
    CHECK(g.nrows == 4 && g.row[1].loop == 9, "delete");
    {
        row_t ra = {0, ROW_A, 1, 0, 0, 0}, raf = {0, ROW_A_FILL, 1, 0, 0, 0};
        row_t rb = {0, ROW_B, 1, 0, 0, 0}, rbf = {0, ROW_B_FILL, 1, 0, 0, 0};
        CHECK(row_main(&ra) == SEC_A && row_main(&raf) == SEC_A && row_main(&rb) == SEC_B && row_main(&rbf) == SEC_B,
              "row_main for each ROW_*");
        CHECK(!row_fill(&ra) && row_fill(&raf) && !row_fill(&rb) && row_fill(&rbf), "row_fill for each ROW_*");
    }
    {   /* SECTION: the loop's other rows on the old Main follow (fills kept); a fill alone changes one row */
        song_t h = {0};
        loops_song_clear(&h);
        row_append(&h, 9);
        row_append(&h, 8);
        row_append(&h, 9);
        h.row[2].sect = ROW_A_FILL;
        row_append(&h, 9);
        h.row[3].sect = ROW_B;
        CHECK(h.nrows == 4 && h.row[2].loop == 9 && h.row[2].rep == 1, "append: separate rows of the same loop");
        CHECK(row_set_sect(&h, 0, ROW_A_FILL) == 0 && h.row[2].sect == ROW_A_FILL && h.row[3].sect == ROW_B,
              "a fill alone: that row");
        CHECK(row_set_sect(&h, 0, ROW_B) == 1 && h.row[2].sect == ROW_B_FILL && h.row[3].sect == ROW_B &&
              h.row[1].sect == ROW_A, "Main A -> B: L10's other Main A row too, its fill kept (%d)", h.row[2].sect);
        CHECK(row_append(&h, 9) == 4 && h.row[4].sect == ROW_B, "append: the loop's section from its earlier row");
        h.row[0] = h.row[1];
        row_insert(&h, 5);
        CHECK(h.nrows == 6 && h.row[5].loop == 9, "insert on the END row: a copy of the last row");
    }
    row_delete(&g, g.nrows - 1);
    CHECK(g.nrows == 3, "deleting the last row");
    g.nrows = ARR_ROWS;
    g.row[ARR_ROWS - 1].loop = 0;
    CHECK(row_tap(&g, ARR_ROWS, 3) == -1, "32 rows at most");
}
static void test_free_length(void)
{
    CHECK(free_rec_bars(0, 4) == 1 && free_rec_bars(4, 4) == 1 && free_rec_bars(5, 4) == 2, "rounded up to bars");
    CHECK(free_rec_bars(200, 4) == 16 && free_rec_bars(200, 7) == 9, "64 beats at most");
    CHECK(free_rec_bars(5, 0) == 3, "bpb 0 clamped to 2, no divide by zero");
}
static void test_shift(void)
{
    chord_t d = {2, CH_MAJ, CH_NOBASS}, c = chord_shift(d, -2), g7e = {7, CH_7, 4}, s;
    CHECK(c.root == 0 && c.type == CH_MAJ && c.bass == CH_NOBASS, "chord_shift({D, maj}, -2) is C, no bass added");
    s = chord_shift(g7e, 5);
    CHECK(s.root == 0 && s.type == CH_7 && s.bass == 9, "G7/E up 5: C7/A (the bass moves too)");
    s = chord_shift(g7e, -19);
    CHECK(s.root == 0 && s.bass == 9, "-19 is +5 (any sign, any size)");
    s = chord_shift(chord_shift(g7e, 3), -3);
    CHECK(s.root == 7 && s.bass == 4, "there and back");
}
static void test_shrink(void)
{
    loop_t l;
    chord_t g = {7, CH_MAJ, CH_NOBASS};
    loop_clear(&l, 0);
    loop_write(&l, 0, g);
    loop_write(&l, 12, g);
    loop_set_bars(&l, 2);
    CHECK(l.ch[12] == LC_SAME, "LENGTH shorter: the beats past the end cleared");
    loop_set_bars(&l, 4);
    CHECK(l.ch[12] == LC_SAME && l.ch[0] != LC_SAME, "growing again: no stale chord comes back");
    loop_write(&l, 60, g);
    loop_set_beats(&l, 7);                        /* 16 bars of 7 > 64: 9 bars, 63 beats */
    CHECK(l.bars == 9 && l.ch[60] != LC_SAME, "BEATS 7: 9 bars, beat 60 still in");
    loop_set_beats(&l, 3);
    CHECK(l.ch[60] == LC_SAME, "BEATS 3 (27 beats): beat 60 cleared");
}
int main(void)
{
    test_shift();
    test_shrink();
    test_pack();
    test_write();
    test_factory();
    test_rows();
    test_free_length();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("loops test passed\n");
    return 0;
}
