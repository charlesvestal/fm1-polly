/* SPDX-License-Identifier: GPL-3.0-only */
/* The transposer: a note of a pattern, written over C M7, to the chord playing now. The five types are the
 * QY70's (manual p.147), the implementation ours. Pure C; per-track state in xstate_t. */
#pragma once
#include <stdint.h>
#include "chords.h"

enum { XT_BYPASS, XT_PARALLEL, XT_CHORD1, XT_CHORD2, XT_BASS, NXT };
#define XT_NOKEY 12

typedef struct {
    uint8_t type;      /* XT_* */
    uint8_t lo, hi;    /* the range: results fold by octaves into lo..hi (0 and 127: no limit) */
    uint8_t hikey;     /* Chord 1 / Bass: roots above this pitch class move the line down an octave; XT_NOKEY off */
} xtrack_t;

typedef struct {
    int16_t last;      /* the last note this track played, -1 none (Chord 2) */
    uint8_t fresh;     /* a chord change since the last note (Bass: the next note is the root) */
} xstate_t;

void xpose_reset(xstate_t *s);
void xpose_chord_changed(xstate_t *s);
/* the note to play for pattern note `note` (0..127) over chord c; -1: none */
int xpose(const xtrack_t *t, xstate_t *s, int note, chord_t c);
/* the inverse, for recording: the pattern note (over C M7) that track type t maps to `note` (a key played)
 * over chord c. Bypass: the key as played; Parallel (and any type over "---"): note - root; Chord 1 / 2 and
 * Bass: the source pitch class whose Chord 1 image has the key's pitch class (chord tones first, then
 * tensions, then chromatic notes), in the octave Chord 1 maps back to the key exactly (high key included);
 * a key outside every image (a chromatic passing note): note - root. Kept in 0..127 by octaves.
 * Keys no pattern note can produce (B over C major, whose 7th is the octave) are stored as note - root and play
 * back moved to a degree of the later chord: the QY's behaviour, by design. */
int xpose_inverse(const xtrack_t *t, int note, chord_t c);
/* "As played" (a Parallel track the app switched at its first REC: arr.h ARR_TF_ASPLAYED): moved by the chord's
 * root taken nearest C, -6..+5 (A is -3, F +5, F# -6), so a line moves by the root's move folded to -6..+5 (over
 * Am, then C: up a third; across F#, up to 11), then folded into the range as any type. Its inverse for
 * recording: note - that root, kept in 0..127 by octaves */
int xpose_root6(int root);
int xpose_played(const xtrack_t *t, int note, chord_t c);
int xpose_played_inverse(int note, chord_t c);
