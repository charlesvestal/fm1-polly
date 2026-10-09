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

    /* recognition: every type, every root, root position and every inversion of its tones.
     * Some types share an identical tone set with another type's inversion (the QY resolves these by
     * the bass, same as the slash notation): a 6 chord's tones (R 3 5 6) are exactly a m7 built a minor
     * third below (its 6th read as that chord's root, its root read as that chord's b7) -- so CH_6 at
     * root r and CH_MIN7 at root (r+9)%12 are the same notes, and likewise CH_MIN6 at root r and
     * CH_MIN7_B5 at root (r+9)%12. An augmented triad (CH_AUG) is itself symmetric -- R, 3 and 5 are a
     * stack of major thirds, so its tone set repeats every 4 semitones and "root" C aug, E aug and Ab aug
     * are the same three notes. With its root, 3rd or 5th lowest, the recognised root and type must match
     * the ones the notes were built from, even inverted; for these exceptions, any of the correct alternate
     * readings is accepted.
     * Changed (the lowest note is the bass): with the chord's 7th, 6th or a tension lowest, the notes are no
     * longer read as the whole chord inverted (B C E G was CM7, Bb C E G was C7, D E G C was Cadd9, Bb C E G A
     * was C7(13)) but as the chord the notes above make, over the lowest as a slash bass (C/B, C/Bb, C/D,
     * C6/Bb): the bass is the lowest note, the chord above holds every other note and not the bass. Or, when
     * all the notes make a chord rooted on the lowest, that chord, no slash (A C E G: Am7, the C6's 6th
     * lowest; A C Eb G: Am7(b5), the Cm6's). Unchanged: the notes above make no chord (Bb C Db E G stays
     * C7(b9)), or a reading of the same notes has the lowest as its root, 3rd or 5th (Bb C Eb G stays Eb6) */
    for (int t = 0; t < CH_NONE; t++)
        for (int r = 0; r < 12; r++) {
            uint16_t m = chord_tones(t);
            const int8_t *dg = chord_degrees(t);
            uint8_t core[8];
            int n = 0;
            for (int i = 0; i < 12; i++)
                if (m >> i & 1u)
                    core[n++] = (uint8_t)(i);
            for (int inv = 0; inv < n; inv++) {
                uint8_t notes[8];
                chord_t c;
                int low = core[inv], ok;
                for (int i = 0; i < n; i++) {
                    int k = (i + inv) % n, iv = core[k] + (i + inv >= n ? 12 : 0);
                    notes[i] = (uint8_t)(48 + r + iv);
                }
                int rec = chord_recognize(notes, n, &c);
                if (low == 0 || low == dg[DEG_3] || low == dg[DEG_5]) {   /* root, 3rd or 5th lowest: as before */
                    ok = rec && c.root == r && c.type == t && c.bass == CH_NOBASS;
                    if (!ok && rec && (t == CH_6 || t == CH_MIN7)) {
                        int alt = (t == CH_6) ? (r + 9) % 12 : (r + 3) % 12;
                        ok = (c.type == CH_6 || c.type == CH_MIN7) && c.root == alt;
                    }
                    if (!ok && rec && (t == CH_MIN6 || t == CH_MIN7_B5)) {
                        int alt = (t == CH_MIN6) ? (r + 9) % 12 : (r + 3) % 12;
                        ok = (c.type == CH_MIN6 || c.type == CH_MIN7_B5) && c.root == alt;
                    }
                    if (!ok && rec && t == CH_AUG)
                        ok = c.type == CH_AUG && (c.root == r || c.root == (r + 4) % 12 || c.root == (r + 8) % 12);
                } else {                                 /* a 7th, 6th or tension lowest: the bass */
                    int lpc = (r + low) % 12;
                    uint32_t up = 0, ct = chord_tones(c.type);
                    for (int i = 1; i < n; i++)
                        up |= 1u << (notes[i] % 12u);
                    ct = (ct << c.root | ct >> (12 - c.root)) & 0xFFFu;
                    chord_t u;
                    int iv = (lpc - c.root + 12) % 12, ucore = iv == 0 || iv == chord_degrees(c.type)[DEG_3] ||
                                                              iv == chord_degrees(c.type)[DEG_5];
                    ok = rec && ((c.bass == lpc && (up & ~ct) == 0 && !(ct >> lpc & 1u)) ||
                                 /* the whole chord, read the same or by its alternate (its tones held), with the
                                  * lowest its root, 3rd or 5th (Bb C Eb G: Eb6), or no chord above the lowest
                                  * (Bb C Db E G: C7(b9) stays) */
                                 (c.bass == CH_NOBASS && ct == ((uint32_t)up | 1u << lpc) &&
                                  (ucore || !chord_recognize(notes + 1, n - 1, &u) || u.bass != CH_NOBASS)));
                }
                CHECK(ok, "type %s root %d inversion %d -> %d %s bass %d", chord_type_name(t), r, inv, c.root,
                      chord_type_name(c.type), c.bass);
            }
        }
    /* tone sets two types share: the lowest note held is the root (as arrangers read them) */
    {
        static const struct { uint8_t n[4]; int root, type; const char *name; } L[] = {
            {{57, 60, 64, 67}, 9, CH_MIN7, "A C E G: Am7"},   {{60, 64, 67, 69}, 0, CH_6, "C E G A: C6"},
            {{52, 55, 59, 62}, 4, CH_MIN7, "E G B D: Em7"},   {{55, 59, 62, 65}, 7, CH_7, "G B D F: G7"},
            {{60, 64, 67, 71}, 0, CH_MAJ7, "C E G B: CM7"},   {{53, 57, 60, 64}, 5, CH_MAJ7, "F A C E: FM7"},
            {{50, 53, 57, 60}, 2, CH_MIN7, "D F A C: Dm7"},   {{59, 62, 65, 69}, 11, CH_MIN7_B5, "B D F A: Bm7b5"},
        };
        for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++) {
            chord_t c;
            CHECK(chord_recognize(L[i].n, 4, &c) && c.root == L[i].root && c.type == L[i].type && c.bass == CH_NOBASS,
                  "%s: got %d %s bass %d", L[i].name, c.root, chord_type_name(c.type), c.bass);
        }
    }
    /* a slash bass: a note held below the chord, not one of its tones */
    {
        uint8_t n[4] = {40, 59, 62, 66};   /* E2, then B3 D4 F#4: Bm/E */
        chord_t c;
        CHECK(chord_recognize(n, 4, &c) && c.root == 11 && c.type == CH_MIN && c.bass == 4, "Bm/E: %d %d %d", c.root, c.type, c.bass);
    }
    /* the lowest note is the bass: not a tone of the chord the notes above it make, it is a slash bass, unless
     * all the notes are a chord rooted on it (the 4-note cases above: Am7, Dm7, Em7, ... stay). A chord in an
     * inversion whose lowest note is one of its tones keeps its root, no slash */
    {
        static const struct { uint8_t n[5]; int k, root, type, bass; const char *name; } S[] = {
            {{50, 60, 64, 67}, 4, 0, CH_MAJ, 2, "D under C E G: C/D (was Cadd9)"},
            {{53, 60, 64, 67}, 4, 0, CH_MAJ, 5, "F under C E G: C/F"},
            {{46, 60, 64, 67}, 4, 0, CH_MAJ, 10, "Bb under C E G: C/Bb (was C7)"},
            {{47, 60, 64, 67}, 4, 0, CH_MAJ, 11, "B under C E G: C/B (was CM7)"},
            {{45, 60, 64, 67}, 4, 9, CH_MIN7, CH_NOBASS, "A under C E G: Am7 (a chord on the lowest note)"},
            {{52, 55, 60}, 3, 0, CH_MAJ, CH_NOBASS, "E G C: C, first inversion"},
            {{55, 60, 64}, 3, 0, CH_MAJ, CH_NOBASS, "G C E: C, second inversion"},
            {{43, 60, 64, 67}, 4, 0, CH_MAJ, CH_NOBASS, "G under C E G: C (the 5th, a chord tone)"},
            {{40, 55, 60, 64}, 4, 0, CH_MAJ, CH_NOBASS, "E under G C E: C"},
            {{48, 57, 60, 64}, 4, 9, CH_MIN, CH_NOBASS, "C under A C E: Am (C is its 3rd, held twice)"},
            {{43, 59, 62, 65, 69}, 5, 7, CH_7_9, CH_NOBASS, "G under B D F A: G7(9)"},
        };
        for (unsigned i = 0; i < sizeof S / sizeof S[0]; i++) {
            chord_t c;
            int ok = chord_recognize(S[i].n, S[i].k, &c);
            CHECK(ok && c.root == S[i].root && c.type == S[i].type && c.bass == S[i].bass, "%s: got %d %s bass %d",
                  S[i].name, c.root, chord_type_name(c.type), c.bass);
        }
    }
    {
        uint8_t n[2] = {60, 64};
        chord_t c;
        CHECK(!chord_recognize(n, 2, &c), "two notes: no chord");
    }
    printf("%d checks, %d failures\n%s\n", checks, fails, fails ? "CHORDS TEST FAILED" : "chords test passed");
    return fails ? 1 : 0;
}
