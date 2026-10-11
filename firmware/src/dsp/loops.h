/* SPDX-License-Identifier: GPL-3.0-only */
/* Loops and song rows (arr.h's types): pure helpers the app and the tests share. No state. */
#pragma once
#include "arr.h"
extern const uint8_t FACTORY_LOOP[8];            /* L1..L8's factory progressions: TEMPLATES indices */
int loop_len(const loop_t *l);                    /* its beats: bars * beats */
int loop_max_bars(int beats);                     /* ARR_LOOP_BEATS / beats, at most ARR_LOOP_BARS */
void loop_clear(loop_t *l, int i);                /* empty (0 bars of 4), named "Loop <i+1>" */
void loop_from_template(loop_t *l, int t);        /* TEMPLATES[t]: a chord a bar, its beats, its name */
/* chord c on beat b: the loop grows (bar by bar) to hold it. 0 = written, -1 = past ARR_LOOP_BEATS */
int loop_write(loop_t *l, int b, chord_t c);
void loop_unwrite(loop_t *l, int b);              /* beat b back to LC_SAME */
chord_t loop_chord_at(const loop_t *l, int b);    /* the chord sounding on beat b (the latest at or before
                                                   * it, round from the end); CH_NONE type if it has none */
void loop_set_bars(loop_t *l, int bars);          /* 1..loop_max_bars; the beats past the end cleared (LC_SAME) */
void loop_set_beats(loop_t *l, int beats);        /* 2..7: the bars shrink to fit 64 beats; past the end cleared */
void loops_song_clear(song_t *g);                 /* no rows; Intro and Ending on; stop at the end; 120 BPM;
                                                   * keeps its style and its lead tracks' sounds */
/* white key `loop` tapped on the SONG screen at row r: row r is set (r the END row, one after the last: a new row
 * at the end; never a REPEAT + 1, REPEAT is the knob's), its section the one this loop had in an earlier row, else
 * Main A; returns the row; -1 when full */
int row_tap(song_t *g, int r, int loop);
void row_insert(song_t *g, int r);                /* a copy of row r (the cursor's; the END row: of the last row,
                                                   * none: a default row) at r, the rows from r one down */
int row_append(song_t *g, int loop);              /* a new row of `loop` at the end, its section as row_tap's; the
                                                   * row, -1 when full */
/* row r's SECTION (ROW_*) set; when its Main (A / B) changed, the loop's other rows on the old Main get the new one
 * (their "+ fill" kept): the number of those */
int row_set_sect(song_t *g, int r, int sect);
void row_delete(song_t *g, int r);
int loop_rows_using(const song_t *g, int loop);   /* rows of g that play it */
int free_rec_bars(int beats, int beats_a_bar);    /* REC in FREE: the bars the take rounds up to (1..max) */
/* c moved by `by` semitones (root and bass; any sign): a stored chord as it sounds (+ the transpose), or a chord
 * played as it is stored (- the transpose) */
chord_t chord_shift(chord_t c, int by);
