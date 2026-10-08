/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's arranger (arr.c): patterns of 8 tracks written over C M7, re-voiced by a chord track; songs of
 * sections and chords. This first part: the data, as it is stored. */
#pragma once
#include <stdint.h>
#include "chords.h"
#include "xpose.h"

#define ARR_TRACKS 8
#define ARR_PPQ 24
#define ARR_MAXBARS 8
#define ARR_NEV 600                      /* notes per pattern */
#define ARR_USER_PATTERNS 16
#define ARR_SONGS 4
#define ARR_SONGBARS 64
#define ARR_NSECT 4
enum { SEC_SAME, SEC_A, SEC_B, SEC_FILL, SEC_END };

typedef struct {
    uint16_t tick;                       /* from the pattern's start, ARR_PPQ a beat */
    uint16_t len;                        /* ticks */
    uint8_t note, vel;
} arr_ev_t;

typedef struct {
    uint8_t program;                     /* GM 0..127 (drum tracks: 0) */
    uint8_t chan;                        /* MIDI channel 0..15 (drums: 9) */
    xtrack_t x;                          /* type, limits, high key */
    uint8_t vol, pan, rev, cho, mute;
    uint8_t rsv[3];
    uint16_t first, count;               /* its events: ev[first .. first + count), sorted by tick */
} arr_trk_t;

typedef struct {
    char name[8];
    uint8_t bars, beats;                 /* 1..8 bars of 2..7 beats */
    uint8_t rsv[2];
    arr_trk_t trk[ARR_TRACKS];
    uint16_t nev;
    uint16_t rsv2;
    arr_ev_t ev[ARR_NEV];
} pattern_t;

typedef struct {
    uint8_t style[ARR_NSECT];            /* the pattern for Main A, Main B, Fill, Ending: 0..15 user, 16.. factory */
    uint8_t bars;                        /* the song's length, 1..64 */
    uint8_t rsv[3];
    uint8_t sect[ARR_SONGBARS];          /* SEC_* per bar */
    chord_t chord[ARR_SONGBARS * 4];     /* a chord per beat; type CH_NTYPES = no change */
} song_t;

typedef struct {
    uint32_t magic, format;
    song_t song[ARR_SONGS];
} songs_t;

_Static_assert(sizeof(pattern_t) <= 3840, "a pattern fits one flash object");
_Static_assert(sizeof(songs_t) <= 3840, "the songs fit one flash object");
