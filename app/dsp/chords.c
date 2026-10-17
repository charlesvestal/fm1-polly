/* SPDX-License-Identifier: GPL-3.0-only */
/* Chords (chords.h). */
#include "chords.h"

static const char *const TYPE_NAME[CH_NTYPES] = {"", "add9", "M7", "M7(9)", "6", "aug", "7", "7(9)", "7(#9)", "7(b9)",
    "7(13)", "7(b13)", "7(#11)", "7(#5)", "7sus4", "sus4", "m", "madd9", "m7", "m7(9)", "m7(11)", "m6", "mM7",
    "m7(b5)", "dim", "-"};                 /* (CH_NONE, no chord yet: the one dash the screens show for "no chord") */
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

/* the best fit for the pitch classes `set` (each held one as the root). A candidate must
 * explain every held note (no tone outside the type); among those, an exact match (nothing missing) always beats
 * a match that is missing only its 5th (the one degree a chord can be voiced without), and the simpler type
 * (table order) wins a tie; of the same tier, `pref` (the lowest note) as the root wins: A C E G is Am7, C E G A
 * is C6 (as arrangers read them). 1 = found, in *root, *type */
static int fit(uint32_t set, int pref, int *root, int *type)
{
    int best = -1;
    for (int r = 0; r < 12; r++) {
        if (!(set >> r & 1u))
            continue;
        uint32_t rel = ((set >> r) | (set << (12 - r))) & 0xFFFu;
        for (int t = 0; t < CH_NONE; t++) {
            uint32_t tm = TONES[t], missing = tm & ~rel;
            int score;
            if (rel & ~tm)                           /* a held note this type cannot explain */
                continue;
            if (missing == 0)
                score = 2000 - t;                    /* exact: every tone of the type is held */
            else if (missing == 0x80u)
                score = 1000 - t;                    /* only the 5th missing */
            else
                continue;
            if (r == pref)
                score += 100;
            if (score > best) {
                best = score;
                *root = r;
                *type = t;
            }
        }
    }
    return best >= 0;
}

/* pitch class pc is one of chord (r, t)'s core tones: its root, 3rd (the 4th of a sus4) or 5th (flat or sharp) */
static int core_tone(int r, int t, int pc)
{
    int iv = (pc - r + 12) % 12;
    return iv == 0 || iv == DEG[t][DEG_3] || iv == DEG[t][DEG_5];
}

/* The lowest held note is the bass. All the notes read as one chord (fit, the lowest preferred as the root):
 * when the lowest is its root, 3rd or 5th, that chord (an inversion: E G C is C; A C E G is Am7, a chord on
 * the lowest note). Else (the lowest is its 7th, 6th or a tension, or no chord fits them all) the notes above
 * the lowest make the chord and the lowest is a slash bass: D under C E G is C/D, Bb under C E G is C/Bb, F under
 * C E G is C/F; with fewer than 3 pitch classes above it, or none that fit, the one chord stays. */
int chord_recognize(const uint8_t *notes, int n, chord_t *out)
{
    uint32_t pcs = 0, upper = 0;
    int lo = 128, up_lo = 128, i, r = 0, t = 0, ur, ut, low_pc, all;
    for (i = 0; i < n; i++)
        if (notes[i] < lo)
            lo = notes[i];
    low_pc = lo % 12;
    for (i = 0; i < n; i++) {
        pcs |= 1u << (notes[i] % 12u);
        if (notes[i] != lo) {
            upper |= 1u << (notes[i] % 12u);
            if (notes[i] < up_lo)
                up_lo = notes[i];
        }
    }
    if (popcount12(pcs) < 3)
        return 0;
    all = fit(pcs, low_pc, &r, &t);
    out->bass = CH_NOBASS;
    if ((!all || !core_tone(r, t, low_pc)) && popcount12(upper) >= 3 && !(upper >> low_pc & 1u) &&
        fit(upper, up_lo % 12, &ur, &ut)) {
        r = ur;                                      /* the chord above, over the lowest */
        t = ut;
        out->bass = (uint8_t)low_pc;
    } else if (!all)
        return 0;
    out->root = (uint8_t)r;
    out->type = (uint8_t)t;
    return 1;
}
