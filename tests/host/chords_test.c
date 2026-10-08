/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <string.h>
#include "chords.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *name(int root, int type, int bass)
{
    static char b[16];
    chord_t c = {(uint8_t)root, (uint8_t)type, (uint8_t)bass};
    chord_name(c, b);
    return b;
}

int main(void)
{
    /* names */
    CHECK(!strcmp(name(0, CH_MAJ, CH_NOBASS), "C"), "%s", name(0, CH_MAJ, CH_NOBASS));
    CHECK(!strcmp(name(9, CH_MIN7, CH_NOBASS), "Am7"), "%s", name(9, CH_MIN7, CH_NOBASS));
    CHECK(!strcmp(name(6, CH_7_B9, CH_NOBASS), "F#7(b9)"), "%s", name(6, CH_7_B9, CH_NOBASS));
    CHECK(!strcmp(name(11, CH_MIN7, 4), "Bm7/E"), "%s", name(11, CH_MIN7, 4));
    CHECK(!strcmp(name(7, CH_NONE, CH_NOBASS), "G"), "%s", name(7, CH_NONE, CH_NOBASS));
    CHECK(!strcmp(name(2, CH_MAJ, 2), "D"), "slash equal to the root is not shown: %s", name(2, CH_MAJ, 2));
    CHECK(!strcmp(chord_type_name(CH_MINMAJ7), "mM7"), "mM7");

    /* degree tables: the chord's own tones where it has them */
    {
        const int8_t *d = chord_degrees(CH_MAJ7);
        CHECK(d[DEG_R] == 0 && d[DEG_3] == 4 && d[DEG_5] == 7 && d[DEG_7] == 11, "M7 tones");
        CHECK(d[DEG_9] == 2 && d[DEG_11] == 5 && d[DEG_13] == 9, "M7: ionian between");
        d = chord_degrees(CH_MIN7);
        CHECK(d[DEG_3] == 3 && d[DEG_7] == 10 && d[DEG_9] == 2 && d[DEG_11] == 5 && d[DEG_13] == 9, "m7: dorian");
        d = chord_degrees(CH_7);
        CHECK(d[DEG_3] == 4 && d[DEG_7] == 10 && d[DEG_13] == 9, "7: mixolydian");
        d = chord_degrees(CH_MIN7_B5);
        CHECK(d[DEG_5] == 6 && d[DEG_9] == 1 && d[DEG_13] == 8, "m7(b5): locrian");
        d = chord_degrees(CH_7_B9);
        CHECK(d[DEG_9] == 1, "7(b9) names its 9th");
        d = chord_degrees(CH_SUS4);
        CHECK(d[DEG_3] == 5, "sus4: the 3rd degree is the 4th");
        d = chord_degrees(CH_6);
        CHECK(d[DEG_7] == 9, "6: the 7th degree is the 6th");
        d = chord_degrees(CH_MAJ);
        CHECK(d[DEG_7] == 12, "a triad: the 7th degree folds to the octave");
        d = chord_degrees(CH_DIM);
        CHECK(d[DEG_3] == 3 && d[DEG_5] == 6 && d[DEG_7] == 12, "dim triad");
    }
    CHECK(chord_tones(CH_MAJ7) == (1u << 0 | 1u << 4 | 1u << 7 | 1u << 11), "M7 tone mask");

    /* recognition: every type, every root, root position and every inversion of its core tones.
     * Some types share an identical tone set with another type's inversion (the QY resolves these by
     * the bass, same as the slash notation): a 6 chord's tones (R 3 5 6) are exactly a m7 built a minor
     * third below (its 6th read as that chord's root, its root read as that chord's b7) -- so CH_6 and
     * CH_MIN7 trade places across inversions, and likewise CH_MIN6 and CH_MIN7_B5. Everywhere else, the
     * An augmented triad (CH_AUG) is itself symmetric -- R, 3 and 5 are a stack of major thirds, so its
     * tone set repeats every 4 semitones and "root" C aug, E aug and Ab aug are the same three notes; any
     * of the three readings is accepted.
     * recognised root and type must match the one the notes were built from, even inverted. */
    for (int t = 0; t < CH_NONE; t++)
        for (int r = 0; r < 12; r++) {
            uint16_t m = chord_tones(t);
            uint8_t core[8];
            int n = 0;
            for (int i = 0; i < 12; i++)
                if (m >> i & 1u)
                    core[n++] = (uint8_t)(i);
            for (int inv = 0; inv < n; inv++) {
                uint8_t notes[8];
                chord_t c;
                for (int i = 0; i < n; i++) {
                    int k = (i + inv) % n, iv = core[k] + (i + inv >= n ? 12 : 0);
                    notes[i] = (uint8_t)(48 + r + iv);
                }
                int ok = chord_recognize(notes, n, &c) && c.root == r && c.type == t;
                if (!ok && (t == CH_6 || t == CH_MIN7))
                    ok = chord_recognize(notes, n, &c) && (c.type == CH_6 || c.type == CH_MIN7);
                if (!ok && (t == CH_MIN6 || t == CH_MIN7_B5))
                    ok = chord_recognize(notes, n, &c) && (c.type == CH_MIN6 || c.type == CH_MIN7_B5);
                if (!ok && t == CH_AUG)
                    ok = chord_recognize(notes, n, &c) && c.type == CH_AUG &&
                         (c.root == r || c.root == (r + 4) % 12 || c.root == (r + 8) % 12);
                CHECK(ok, "type %s root %d inversion %d -> %d %d", chord_type_name(t), r, inv, c.root, c.type);
            }
        }
    /* a slash bass: a note held below the chord, not one of its tones */
    {
        uint8_t n[4] = {40, 59, 62, 66};   /* E2, then B3 D4 F#4: Bm/E */
        chord_t c;
        CHECK(chord_recognize(n, 4, &c) && c.root == 11 && c.type == CH_MIN && c.bass == 4, "Bm/E: %d %d %d", c.root, c.type, c.bass);
    }
    {
        uint8_t n[2] = {60, 64};
        chord_t c;
        CHECK(!chord_recognize(n, 2, &c), "two notes: no chord");
    }
    printf("%d checks, %d failures\n%s\n", checks, fails, fails ? "CHORDS TEST FAILED" : "chords test passed");
    return fails ? 1 : 0;
}
