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
 * 22.05 kHz). Note-offs go to the block start nearest their time (+-2.9 ms: lengths right on average, a
 * note-off never before its note-on). The rest takes effect at the start of the block the tick falls in:
 * programs and controllers, MIDI out (up to 5.8 ms late: the USB ring is sent from the main loop anyway),
 * and everything when following a MIDI clock in. */
void arr_init(void);
void arr_play(int on);
void arr_set_tempo(int bpm);                     /* 40..240 */
void arr_set_chord(chord_t c);                   /* the chord now (live, or the song's) */
void arr_load_pattern(int slot, const pattern_t *p);   /* slot 0..3: Main A, Main B, Fill, Ending (copied) */
/* SEC_A..SEC_END: from the next bar (arr_next_sect shows it meanwhile). A fill plays one bar, then the other
 * Main (from A: B; from B: A); a fill during a fill adds a bar. An ending plays its pattern once, then stops
 * (Ending again during it: it still stops). Notes sounding over a section change keep their voicing to their
 * end, not re-voiced by the new pattern's track types. PLAY starts with the section
 * queued, else Main A or B as it was (after a fill or an ending: Main A). */
void arr_section(int sec);
/* a song (copied; 0: none, back to free play): its section track switches sections at its bars, its chord
 * track sets the chord on its beats (the first 4 beats of a bar; CH_NTYPES keeps the last). A section mark
 * switches only to a section other than the one playing (marks over a region play it on). It stops at the
 * end of its last bar, or with loop goes round. It starts at bar 1 with PLAY, or at the next bar line when
 * set while playing (a song set again while it plays keeps its place). A section queued by arr_section wins
 * over the section track's mark at that bar. A song turns a template off. The app loads its four patterns
 * (style) into the slots with arr_load_pattern. */
void arr_song(const song_t *s, int loop);
/* a chord template (templates.h) as the chord track, looping, transposed by the key; -1: off (the chord
 * stays). Picked while playing, its first chord sounds at once and its bars count from the playing bar's
 * downbeat; a template's bar is its own beats (3 for the waltz). Turns a song off; the transport is the
 * app's (arr_play). */
void arr_template(int i);
/* 0..11 semitones over C: the templates' key. Key transposes templates only: songs are entered at real pitch */
void arr_set_key(int key);
/* live chords (fingered): the keys held, as MIDI notes, called on every change (one caller: the main loop).
 * Recognised (chord_recognize) here, only when a key was pressed (the set grew), and set as the chord, ending
 * a template; keys lifted one by one never re-recognise. 1 = a new chord; 0 = none (no key pressed, fewer
 * than 3 pitch classes, or no type fits): the chord stays as it was. The app calls arr_keys_held(0, 0) when
 * chord entry (CHORD) is switched on, so keys already held then count as pressed anew. */
int arr_keys_held(const uint8_t *notes, int n);
void arr_track_mute(int trk, int on);
/* ---- loop recording, into the playing pattern (its slot's copy here: arr_pattern_copy reads it back).
 * arr_key: a key played (vel 0 or on 0: released), from the main loop. It sounds at once on the selected track's
 * channel (the app sends it nowhere else); with recording on and playing, it is also written into that track:
 * its start the nearest tick to the key's block, quantised to the nearest grid line (a key just before the
 * loop's start lands on tick 0), its length to the release (at least one quantum), stored over C M7 by the
 * inverse of the track's type over the chord now (xpose_inverse). A note still held when the loop wraps ends
 * at the pattern's end (the key still sounds). A note is written into the pattern only once its start has
 * been played in the pass it was recorded in, so it sounds from the next pass (the key alone this pass);
 * overdubs add to what is there (the same note on the same tick: replaced). Pattern full (ARR_NEV): the note
 * is dropped and arr_full set, until an erase or a load. Stopping, a section change or recording off end the
 * notes held. A section change (a fill, an ending, a song's mark, arr_section) also ends recording: the notes
 * held are written into the pattern that played, and arr_recording goes to 0 (the app shows REC off), so
 * nothing records into another pattern unasked. With the grid off, a key at the loop's start is its tick 0,
 * never the last tick of the pass before. A pattern loaded into the slot (arr_load_pattern) while a note is
 * held or waiting drops that note; a store (arr_store_pattern) keeps it, written after the store. The keys
 * sound with the selected track's sounds (sent to the synth, and MIDI out, at arr_record and when the track
 * or pattern changed), and go to MIDI out on its channel when MIDI out is on (as the pattern's notes: a
 * note-on only with room for its note-off). */
enum { ARR_Q_8, ARR_Q_8T, ARR_Q_16, ARR_Q_16T, ARR_Q_32, ARR_Q_OFF, ARR_NQ };   /* 12 8 6 4 3 1 ticks */
void arr_record(int trk, int on);                /* the selected track (also arr_key's sound), recording on */
void arr_key(int note, int on, int vel);
void arr_set_quant(int q);                       /* ARR_Q_*, ARR_Q_16 at init */
/* the playing pattern's track: its notes removed (while that slot is being stored: right after) */
void arr_erase_track(int trk);
/* a slot's pattern as it is now, recordings included (main loop); returns its arr_dirty count, which counts
 * up at every recorded note and erase (save it when it moved) */
uint16_t arr_pattern_copy(int slot, pattern_t *out);
/* an edit of the pattern in a slot (sounds, track types, ...) put back, recording or not: copy, edit, store
 * with the count the copy returned. 0: the slot was recorded into (or erased) since, nothing stored: copy
 * again, edit again, store again:
 *     do { d = arr_pattern_copy(s, &p); edit(&p); } while (!arr_store_pattern(s, &p, d));
 * (arr_load_pattern is for another pattern: it overwrites whatever was recorded) */
int arr_store_pattern(int slot, const pattern_t *p, uint16_t expect);
extern volatile uint8_t arr_full, arr_recording;  /* recording: on, until arr_record(.., 0) or a section change */
extern volatile uint16_t arr_dirty[ARR_NSECT];
void arr_midi_out_on(int on);
enum { ARR_CLK_TICK, ARR_CLK_START, ARR_CLK_CONTINUE, ARR_CLK_STOP };
void arr_clock(int msg);
/* called by gm_render before each EAS block of `frames` output frames (the render's thread): the commands
 * posted since, then the ticks that fall within this block; their notes go to the synth with gm_midi_now,
 * each note-on starting on its sample within the block */
void arr_block(uint32_t frames);
/* called by gm_render first, before the MIDI queued by the main loop: the note-offs the arranger held from the
 * last block's second half (arr.h's Timing), so they never end a note the keys or MIDI in start now */
void arr_late_offs(void);
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
extern volatile uint8_t arr_song_bar;               /* the song's bar playing (0..63), with a song */
extern volatile int8_t arr_template_now;            /* the template playing, -1 none */
extern volatile uint32_t arr_chord_now;            /* the chord, packed in one word (never torn): arr_chord_unpack */
static inline uint32_t arr_chord_pack(chord_t c) { return (uint32_t)c.root | (uint32_t)c.type << 8 | (uint32_t)c.bass << 16; }
static inline chord_t arr_chord_unpack(uint32_t v)
{
    chord_t c = {(uint8_t)(v & 0xFFu), (uint8_t)(v >> 8 & 0xFFu), (uint8_t)(v >> 16 & 0xFFu)};
    return c;
}
extern volatile uint32_t arr_track_hit[ARR_TRACKS];  /* render ticks of each track's last note-on */
extern volatile uint32_t arr_ticks;                  /* render ticks since arr_init */
