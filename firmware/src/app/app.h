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
#define SONGS_FORMAT 7u                  /* 7: songs with their lead tracks' sounds (older: reset; Polly is prerelease) */

typedef struct {
    uint32_t magic, format;
    uint8_t ch;                          /* the channel the keys play, 0..15 */
    int8_t oct;                          /* the keys' octave, -3..3 */
    uint8_t leds;                        /* lights: 0 off, 1 notes and the buttons' glow, 2 notes only */
    uint8_t rsv0;
    uint8_t program[GM_NCH], volume[GM_NCH], pan[GM_NCH], reverb[GM_NCH], chorus[GM_NCH];
    uint8_t song, pattern;                /* the selected song (0..ARR_SONGS-1) and user pattern (0..15) */
    uint8_t trk;                          /* the selected track (0..7): the keys play it, REC records into it */
    uint8_t rsv_tempo;                     /* unused: a song's tempo plays now (songs_t); kept for the layout */
    uint8_t rsv_key;                       /* unused: a song's transpose plays now (songs_t); kept for the layout */
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
/* the 16 shared slots (OBJ_PAT0 + i): each a user pattern (a bare pattern_t, as stored before), a lead part (arr.h
 * lead_part_t, LEAD_MAGIC first), or nothing */
enum { SLOT_NONE, SLOT_PATTERN, SLOT_LEAD, SLOT_OTHER };   /* OTHER: unreadable as either (an old format): counts free */
typedef struct {
    uint8_t kind, song, part;            /* a lead part's song and part */
    uint16_t n;                          /* a pattern's notes (nev), a lead part's */
} slot_info_t;
int project_slot_info(int i, slot_info_t *si, pattern_t *buf);   /* buf: a pattern looked at (its notes counted) */
int project_slot_blank(int i, pattern_t *buf);    /* nothing, or a pattern or lead part with no notes, or OTHER */
/* the first slot none of the songs plays as a pattern, not in `reserved` (bit i: slot i, the leads' own in RAM), and
 * blank; -1 none. The user patterns' copy-on-write and the lead parts both take slots here */
int project_slot_free(const songs_t *s, uint32_t reserved, pattern_t *buf);
/* song n's lead notes from the slots whose header says so, into l (its parts; l->trk untouched); slot[k]: part k's
 * slot, -1 none. Returns 1 when they were put right (leads.c leads_fix: out of order, bad notes, part 0 not full
 * before part 1): saved again from it, -1 for none at all, 0 as stored. The caller holds the engine off (arr_edit) */
int project_leads_load(int song, leads_t *l, int8_t *slot);
/* parts `dirty` (bit k: part k) of song n saved: part k with notes into slot[k] (the caller took it: project_slot_free,
 * when the part was first needed); a part with none into its slot as an empty part (the slot free again), its slot[k]
 * -1. 0 = saved; -1 a flash error; -2 a part with notes and no slot */
int project_leads_save(int song, leads_t *l, int8_t *slot, unsigned dirty);
int project_leads_free(int song);                 /* every slot holding a part of song n: free (CLEAR / COPY SONG) */
/* a slot a pattern must not be saved into: it holds a lead part with notes */
int project_slot_leads(int i);
void pattern_empty(pattern_t *p);
void song_default(song_t *g, int n);                   /* song n, empty: no bars, the first factory style */
int song_style_valid(int st);                          /* a pattern number a song may hold */
int song_style_base(int st);                           /* the style pattern st is a section of: its Intro's number; -1 */
void song_style_derive(song_t *g);                     /* g->sty from its sections (Main A's first), else kept */

void app_start(void);                  /* at boot, after project_load: the songs and patterns into the arranger */
void app_bank_changed(void);           /* the QY70 style bank was written: mounted again, the song reloaded */
void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
