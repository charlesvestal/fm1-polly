/* SPDX-License-Identifier: GPL-3.0-only */
/* Chords: the QY70's 26 types (manual p.41) with their tones by degree and the scale each implies, names,
 * and recognition from held notes. Pure C, no state: the transposer, the engine and the UI share it. */
#pragma once
#include <stdint.h>

enum {
    CH_MAJ, CH_ADD9, CH_MAJ7, CH_MAJ7_9, CH_6, CH_AUG, CH_7, CH_7_9, CH_7_S9, CH_7_B9, CH_7_13, CH_7_B13,
    CH_7_S11, CH_7_S5, CH_7SUS4, CH_SUS4, CH_MIN, CH_MADD9, CH_MIN7, CH_MIN7_9, CH_MIN7_11, CH_MIN6,
    CH_MINMAJ7, CH_MIN7_B5, CH_DIM, CH_NONE, CH_NTYPES
};
enum { DEG_R, DEG_9, DEG_3, DEG_11, DEG_5, DEG_13, DEG_7, NDEG };   /* in pitch order over the root */
#define CH_NOBASS 0xFF

typedef struct {
    uint8_t root;   /* 0..11, C = 0 */
    uint8_t type;   /* CH_* */
    uint8_t bass;   /* 0..11, or CH_NOBASS */
} chord_t;

const char *chord_type_name(int type);           /* "", "add9", "M7", ... ("---" for CH_NONE) */
char *chord_name(chord_t c, char *buf);          /* "Bm7/E"; buf >= 12 bytes; returns the end */
/* the chord's pitch for each degree, semitones over the root: its own tones where it has them (root, 3rd,
 * 5th, 7th, and the tensions it names), the implied scale's notes for the rest */
const int8_t *chord_degrees(int type);
uint16_t chord_tones(int type);                  /* bit i: i semitones over the root is a chord tone */
/* recognise held notes (MIDI numbers, any order); 1 = a chord in *out, 0 = none (fewer than 3 distinct
 * pitch classes, or no type fits) */
int chord_recognize(const uint8_t *notes, int n, chord_t *out);
