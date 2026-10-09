/* SPDX-License-Identifier: GPL-3.0-only */
/* Chords (chords.h). */
#include "chords.h"

static const char *const TYPE_NAME[CH_NTYPES] = {"", "add9", "M7", "M7(9)", "6", "aug", "7", "7(9)", "7(#9)", "7(b9)",
    "7(13)", "7(b13)", "7(#11)", "7(#5)", "7sus4", "sus4", "m", "madd9", "m7", "m7(9)", "m7(11)", "m6", "mM7",
    "m7(b5)", "dim", "---"};
static const char *const NOTE[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

/* per type: semitones over the root for R, 9, 3, 11, 5, 13, 7 (DEG_* order). The chord's own tones where it
 * has them; between them, its implied scale: major types ionian (lydian for 7(#11)), 7ths mixolydian,
 * minors dorian (aeolian for m(b13)-less sets), m7(b5) locrian, dim diminished; a triad's missing 7th folds
 * to the octave (12), a 6 chord's 7th degree is its 6th */
static const int8_t DEG[CH_NTYPES][NDEG] = {
    [CH_MAJ] = {0, 2, 4, 5, 7, 9, 12},      [CH_ADD9] = {0, 2, 4, 5, 7, 9, 12},
    [CH_MAJ7] = {0, 2, 4, 5, 7, 9, 11},     [CH_MAJ7_9] = {0, 2, 4, 5, 7, 9, 11},
    [CH_6] = {0, 2, 4, 5, 7, 9, 9},         [CH_AUG] = {0, 2, 4, 6, 8, 9, 12},
    [CH_7] = {0, 2, 4, 5, 7, 9, 10},        [CH_7_9] = {0, 2, 4, 5, 7, 9, 10},
    [CH_7_S9] = {0, 3, 4, 5, 7, 9, 10},     [CH_7_B9] = {0, 1, 4, 5, 7, 9, 10},
    [CH_7_13] = {0, 2, 4, 5, 7, 9, 10},     [CH_7_B13] = {0, 2, 4, 5, 7, 8, 10},
    [CH_7_S11] = {0, 2, 4, 6, 7, 9, 10},    [CH_7_S5] = {0, 2, 4, 5, 8, 9, 10},
    [CH_7SUS4] = {0, 2, 5, 5, 7, 9, 10},    [CH_SUS4] = {0, 2, 5, 5, 7, 9, 12},
    [CH_MIN] = {0, 2, 3, 5, 7, 9, 12},      [CH_MADD9] = {0, 2, 3, 5, 7, 9, 12},
    [CH_MIN7] = {0, 2, 3, 5, 7, 9, 10},     [CH_MIN7_9] = {0, 2, 3, 5, 7, 9, 10},
    [CH_MIN7_11] = {0, 2, 3, 5, 7, 9, 10},  [CH_MIN6] = {0, 2, 3, 5, 7, 9, 9},
    [CH_MINMAJ7] = {0, 2, 3, 5, 7, 9, 11},  [CH_MIN7_B5] = {0, 1, 3, 5, 6, 8, 10},
    [CH_DIM] = {0, 2, 3, 5, 6, 8, 12},      [CH_NONE] = {0, 2, 4, 5, 7, 9, 11},
};
/* the tones that make each chord (bit i: i semitones over the root), for naming and recognition */
static const uint16_t TONES[CH_NTYPES] = {
    [CH_MAJ] = 0x091, [CH_ADD9] = 0x095, [CH_MAJ7] = 0x891, [CH_MAJ7_9] = 0x895, [CH_6] = 0x291,
    [CH_AUG] = 0x111, [CH_7] = 0x491, [CH_7_9] = 0x495, [CH_7_S9] = 0x499, [CH_7_B9] = 0x493,
    [CH_7_13] = 0x691, [CH_7_B13] = 0x591, [CH_7_S11] = 0x4D1, [CH_7_S5] = 0x511, [CH_7SUS4] = 0x4A1,
    [CH_SUS4] = 0x0A1, [CH_MIN] = 0x089, [CH_MADD9] = 0x08D, [CH_MIN7] = 0x489, [CH_MIN7_9] = 0x48D,
    [CH_MIN7_11] = 0x4A9, [CH_MIN6] = 0x289, [CH_MINMAJ7] = 0x889, [CH_MIN7_B5] = 0x449, [CH_DIM] = 0x049,
    [CH_NONE] = 0x001,
};

const char *chord_type_name(int t) { return TYPE_NAME[(unsigned)t < CH_NTYPES ? t : CH_NONE]; }
const int8_t *chord_degrees(int t) { return DEG[(unsigned)t < CH_NTYPES ? t : CH_NONE]; }
uint16_t chord_tones(int t) { return TONES[(unsigned)t < CH_NTYPES ? t : CH_NONE]; }

static char *cat(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
char *chord_name(chord_t c, char *b)
{
    b = cat(b, NOTE[c.root % 12u]);
    if (c.type != CH_NONE && c.type != CH_MAJ)
        b = cat(b, chord_type_name(c.type));
    if (c.bass != CH_NOBASS && c.bass % 12u != c.root % 12u) {
        b = cat(b, "/");
        b = cat(b, NOTE[c.bass % 12u]);
    }
    return b;
}

static int popcount12(uint32_t m)
{
    int n = 0;
    for (; m; m &= m - 1u)
        n++;
    return n;
}

/* the best fit: each root among the held pitch classes, each type. A candidate must explain every held
 * note (no tone outside the type); among those, an exact match (nothing missing) always beats a match
 * that is missing only its 5th (the one degree a chord can be voiced without), and the simpler type (table
 * order) wins a tie. The lowest held note, if it is not a tone of the winner and lies below all the
 * others, is the slash bass, and is left out of the fit. */
int chord_recognize(const uint8_t *notes, int n, chord_t *out)
{
    uint32_t pcs = 0, upper = 0;
    int lo = 128, i, r, t, best = -1, br = 0, bt = 0, low_pc;
    for (i = 0; i < n; i++)
        if (notes[i] < lo)
            lo = notes[i];
    low_pc = lo % 12;
    for (i = 0; i < n; i++) {
        pcs |= 1u << (notes[i] % 12u);
        if (notes[i] != lo)
            upper |= 1u << (notes[i] % 12u);
    }
    if (popcount12(pcs) < 3)
        return 0;
    for (int pass = 0; pass < 2; pass++) {          /* 0: all notes; 1: the lowest as a slash bass */
        uint32_t set = pass ? upper : pcs;
        if (pass && (popcount12(set) < 3 || (upper >> low_pc & 1u)))
            break;
        for (r = 0; r < 12; r++) {
            if (!(set >> r & 1u))
                continue;
            for (t = 0; t < CH_NONE; t++) {
                uint32_t tm = TONES[t], rel = ((set >> r) | (set << (12 - r))) & 0xFFFu;
                if (rel & ~tm)                       /* a held note this type cannot explain */
                    continue;
                uint32_t missing = tm & ~rel;
                int score;
                if (missing == 0)
                    score = 2000 - t;                /* exact: every tone of the type is held */
                else if (missing == 0x80u)
                    score = 1000 - t;                /* only the 5th missing */
                else
                    continue;
                score -= pass ? 500 : 0;             /* root position wins over reading a slash bass */
                if (!pass && r == low_pc)
                    score += 100;                    /* of the same tier, the lowest note as the root: A C E G
                                                      * is Am7, C E G A is C6 (as arrangers read them) */
                if (score > best) {
                    best = score;
                    br = r;
                    bt = t;
                    out->bass = pass ? (uint8_t)low_pc : CH_NOBASS;
                }
            }
        }
    }
    if (best < 0)
        return 0;
    out->root = (uint8_t)br;
    out->type = (uint8_t)bt;
    return 1;
}
