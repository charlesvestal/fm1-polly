/* SPDX-License-Identifier: GPL-3.0-only */
/* GM app: the project (everything that is saved) and the app's entry points. Included by the unity
 * build after gfx.c (drawing helpers in scope) and by the host simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/gm.h"
#include "../dsp/arr.h"

#ifndef GM_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define GM_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x314D4750u           /* "PGM1" */
#define PROJ_FORMAT 5u                   /* 5: 480 ticks a beat (patterns and songs of older formats are reset) */
#define SONGS_MAGIC 0x314E4753u          /* "SGN1" */
#define SONGS_FORMAT 3u

typedef struct {
    uint32_t magic, format;
    uint8_t ch;                          /* the channel the keys play, 0..15 */
    int8_t oct;                          /* the keys' octave, -3..3 */
    uint8_t leds;                        /* lights: 0 off, 1 notes and the buttons' glow, 2 notes only */
    uint8_t rsv0;
    uint8_t program[GM_NCH], volume[GM_NCH], pan[GM_NCH], reverb[GM_NCH], chorus[GM_NCH];
    uint8_t song, pattern;                /* the selected song (0..ARR_SONGS-1) and user pattern (0..23) */
    uint8_t trk;                          /* the selected track (0..7): the keys play it, REC records into it */
    uint8_t tempo;                        /* 40..240 bpm */
    uint8_t key;                          /* the templates' key, 0..11 over C */
    uint8_t quant;                        /* the recording grid, ARR_Q_* */
    uint8_t rev_type, rev_level;          /* the master reverb: 0 off, 1..4 (large hall, hall, chamber, room); 0..127 */
    uint8_t cho_type, cho_level;          /* the master chorus: preset 0..3; 0..127 (0 off) */
    uint8_t clock;                        /* 0 internal (MIDI clock in ignored), 1 MIDI (followed when it comes) */
    uint8_t mout;                         /* MIDI out: the tracks on their channels, the clock */
    uint8_t rsv[4];
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every channel's program and controllers to the synth */

int project_pattern_load(int i, pattern_t *p);         /* user pattern i, 0 = loaded, else empty */
int project_pattern_save(int i, const pattern_t *p);
int project_songs_load(songs_t *s);
int project_songs_save(const songs_t *s);
void pattern_empty(pattern_t *p);
void song_default(song_t *g, int n);                   /* song n, empty */                      /* 2 bars of 4, 8 tracks, the default sounds and types */

void app_start(void);                  /* at boot, after project_load: the songs and patterns into the arranger */
void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
