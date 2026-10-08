/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's arranger (arr.c): patterns of 8 tracks written over C M7, re-voiced by a chord track; songs of
 * sections and chords. This first part: the data, as it is stored. */
#pragma once
#include <stdint.h>
#include "chords.h"
#include "xpose.h"

/* stored as raw bytes, native (little-endian) order, no packing pragma: every member is naturally
 * aligned by the rsv fields placed by hand, so the layout is stable without padding surprises. */
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

/* ---- the engine. Threads as Life's: the sequencer runs inside the audio render (gm_render calls arr_block
 * before each EAS block); everything else posts commands, drained at the start of a block.
 *
 * Timing: EAS renders only whole blocks of 128 frames at 22.05 kHz (EAS_Render refuses any other count, and
 * its envelopes and LFOs step once a block, by increments the sound set bakes in for that block), so MIDI
 * reaches the synth between blocks, on 256-frame boundaries of the 44.1 kHz output (5.8 ms). The clock itself
 * is exact (a Q16 frames-to-the-next-tick count, no drift): each tick due within a block is played before it,
 * its note-ons with their sample in it (gm_midi_now's at), so they start on their exact sample (+-1 at
 * 22.05 kHz). The rest takes effect at the start of the block the tick falls in: note-offs (a release up to
 * 5.8 ms early), programs and controllers, MIDI out (up to 5.8 ms late: the USB ring is sent from the main
 * loop anyway), and everything when following a MIDI clock in. */
void arr_init(void);
void arr_play(int on);
void arr_set_tempo(int bpm);                     /* 40..240 */
void arr_set_chord(chord_t c);                   /* the chord now (live, or the song's) */
void arr_load_pattern(int slot, const pattern_t *p);   /* slot 0..3: Main A, Main B, Fill, Ending (copied) */
void arr_section(int sec);                       /* SEC_A..SEC_END: from the next bar */
void arr_track_mute(int trk, int on);
void arr_midi_out_on(int on);
enum { ARR_CLK_TICK, ARR_CLK_START, ARR_CLK_CONTINUE, ARR_CLK_STOP };
void arr_clock(int msg);
/* called by gm_render before each EAS block of `frames` output frames (the render's thread): the commands
 * posted since, then the ticks that fall within this block; their notes go to the synth with gm_midi_now,
 * each note-on starting on its sample within the block */
void arr_block(uint32_t frames);
/* provided by the app, called from the render only: MIDI out (a USB ring that drops when full), the ring's
 * free slots, and a count that changes at each new connection (the far side's sounds are then unknown).
 * Priorities: the clock (F8, FA, FC) whenever a note-off owed still fits; note-ons with a reserve to spare;
 * the sounds (programs, CCs) last, what does not fit going out in later blocks. So a note-off owed is never
 * dropped, and a note-on not sent has no note-off sent either. */
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2);
uint32_t arr_midi_out_room(void);
uint32_t arr_midi_out_link(void);

/* state for the screen */
extern volatile uint8_t arr_playing, arr_sect, arr_next_sect, arr_ext;
extern volatile uint16_t arr_bar, arr_beat_tick;     /* position in the playing pattern */
extern volatile uint32_t arr_chord_now;            /* the chord, packed in one word (never torn): arr_chord_unpack */
static inline uint32_t arr_chord_pack(chord_t c) { return (uint32_t)c.root | (uint32_t)c.type << 8 | (uint32_t)c.bass << 16; }
static inline chord_t arr_chord_unpack(uint32_t v)
{
    chord_t c = {(uint8_t)(v & 0xFFu), (uint8_t)(v >> 8 & 0xFFu), (uint8_t)(v >> 16 & 0xFFu)};
    return c;
}
extern volatile uint32_t arr_track_hit[ARR_TRACKS];  /* render ticks of each track's last note-on */
extern volatile uint32_t arr_ticks;                  /* render ticks since arr_init */
