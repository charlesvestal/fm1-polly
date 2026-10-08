/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include "xpose.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define C4 60

static chord_t ch(int root, int type) { chord_t c = {(uint8_t)root, (uint8_t)type, CH_NOBASS}; return c; }
static int x(int type, int note, chord_t c)
{
    xtrack_t t = {(uint8_t)type, 0, 127, XT_NOKEY};
    xstate_t s;
    xpose_reset(&s);
    return xpose(&t, &s, note, c);
}

int main(void)
{
    for (int r = 0; r < 12; r++)
        for (int n = 30; n < 90; n++)
            CHECK(x(XT_BYPASS, n, ch(r, CH_MIN7)) == n, "bypass");
    CHECK(x(XT_PARALLEL, C4, ch(9, CH_MIN)) == 69 && x(XT_PARALLEL, 64, ch(9, CH_MIN)) == 73, "parallel +9");

    {   /* Chord 1: C E G B over Am7, G7, Dm */
        int in[4] = {60, 64, 67, 71}, am7[4] = {69, 72, 76, 79}, g7[4] = {67, 71, 74, 77}, dm[4] = {62, 65, 69, 74};
        for (int i = 0; i < 4; i++) {
            CHECK(x(XT_CHORD1, in[i], ch(9, CH_MIN7)) == am7[i], "Am7 %d -> %d", in[i], x(XT_CHORD1, in[i], ch(9, CH_MIN7)));
            CHECK(x(XT_CHORD1, in[i], ch(7, CH_7)) == g7[i], "G7 %d -> %d", in[i], x(XT_CHORD1, in[i], ch(7, CH_7)));
            CHECK(x(XT_CHORD1, in[i], ch(2, CH_MIN)) == dm[i], "Dm %d -> %d", in[i], x(XT_CHORD1, in[i], ch(2, CH_MIN)));
        }
        CHECK(x(XT_CHORD1, 62, ch(9, CH_MIN7)) == 71, "the 9th over Am7: B4");
        CHECK(x(XT_CHORD1, 65, ch(7, CH_7)) == 72, "the 11th over G7: C5");
    }
    {   /* Chord 2: nearest the last note */
        xtrack_t t = {XT_CHORD2, 0, 127, XT_NOKEY};
        xstate_t s;
        xpose_reset(&s);
        CHECK(xpose(&t, &s, 64, ch(0, CH_MAJ)) == 64, "E over C");
        CHECK(x(XT_CHORD1, 64, ch(9, CH_MAJ)) == 73, "Chord 1: E4 over A is C#5");
        CHECK(xpose(&t, &s, 64, ch(9, CH_MAJ)) == 61, "Chord 2: nearest E4, C#4, got %d", xpose(&t, &s, 64, ch(9, CH_MAJ)));
    }
    {   /* Bass: the first note after a change is the root, or the slash bass */
        xtrack_t t = {XT_BASS, 0, 127, XT_NOKEY};
        xstate_t s;
        chord_t gE = {7, CH_MAJ, 4};
        xpose_reset(&s);
        xpose_chord_changed(&s);
        CHECK(xpose(&t, &s, 40, ch(9, CH_MIN)) == 45, "first after change: A2");
        CHECK(xpose(&t, &s, 43, ch(9, CH_MIN)) == 52, "then Chord 1: G2 (the 5th) -> E3");
        xpose_chord_changed(&s);
        CHECK(xpose(&t, &s, 43, gE) == 52, "slash bass: G/E -> E3 (nearest the Chord 1 result D3), got %d", xpose(&t, &s, 43, gE));
    }
    {   /* limits and high key */
        xtrack_t t = {XT_CHORD1, 48, 72, XT_NOKEY};
        xstate_t s;
        xpose_reset(&s);
        CHECK(xpose(&t, &s, 71, ch(9, CH_MIN7)) == 67, "79 above 72: down to 67");
        t.hikey = 5;
        CHECK(xpose(&t, &s, 60, ch(7, CH_MAJ)) == 55, "high key F: G's root is above: C4 -> G3");
        CHECK(xpose(&t, &s, 60, ch(4, CH_MAJ)) == 64, "E's root is not: C4 -> E4");
    }
    printf("%d checks, %d failures\n%s\n", checks, fails, fails ? "XPOSE TEST FAILED" : "xpose test passed");
    return fails ? 1 : 0;
}
