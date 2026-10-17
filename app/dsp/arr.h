/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's arranger (arr.c): patterns of 8 tracks written over C M7, re-voiced by a chord track; chord
 * loops, and songs of rows of them. This first part: the data, as it is stored. */
#pragma once
#include <stdint.h>
#include "chords.h"
#include "xpose.h"
#include "qybank.h"

/* stored as raw bytes, native (little-endian) order, no packing pragma: every member is naturally
 * aligned by the rsv fields placed by hand, so the layout is stable without padding surprises. */
#define ARR_TRACKS 8
#define ARR_PPQ 480                      /* ticks a quarter note: the QY70's resolution */
#define ARR_CLOCK_TICKS (ARR_PPQ / 24)   /* ticks a MIDI clock (F8, 24 a quarter): 20 */
#define ARR_MAXBARS 16                   /* a pattern's bars at most (as a loop's): 16 of 7 beats, 53,760 ticks, fit the
                                         * 16-bit ticks; the factory and QY70 styles have 8 at most */
#define ARR_STYLE_MAXBARS 8              /* a QY70 bank section's bars at most (qybank.h) */
#define ARR_NEV 600                      /* notes per pattern */
#define ARR_USER_PATTERNS 16             /* the user's patterns (flash objects); a song's section gets one when it
                                         * is first recorded into or edited (ui.c: copy-on-write) */
#define ARR_SONGS 4
#define ARR_LOOPS 16                      /* L1..L16 */
#define ARR_LOOP_BEATS 64                 /* a loop's beats at most (bars x beats) */
#define ARR_LOOP_BARS 16                  /* a loop's bars at most */
#define ARR_ROWS 32                       /* a song's rows at most */
#define ARR_NSECT 6
/* the sections (the QY70's), each a slot (sec - 1) holding its pattern. SEC_FILL is a command only (arr_section):
 * Fill AB from Main A (or the Intro), Fill BA from Main B */
enum { SEC_SAME, SEC_INTRO, SEC_A, SEC_B, SEC_FILL_AB, SEC_FILL_BA, SEC_END, SEC_FILL };

typedef struct {
    uint16_t tick;                       /* from the pattern's start, ARR_PPQ a beat (16 bars of 7: 53760 at most) */
    uint16_t len;                        /* ticks, 1 .. the pattern's length */
    uint8_t note, vel;
} arr_ev_t;

typedef struct {
    uint8_t program;                     /* GM 0..127 (drum tracks: 0) */
    uint8_t chan;                        /* MIDI channel 0..15 (drums: 9) */
    xtrack_t x;                          /* type, limits, high key */
    uint8_t vol, pan, rev, cho;
    uint8_t mute;                        /* muted in this pattern: the engine honours it, but mutes are the song's now
                                          * (song_t.mute, arr_track_mute); the app moves a pattern's into its song when
                                          * it loads it (patterns saved before) */
    uint8_t bright, reso;                /* CC 74 (brightness: the filter's cutoff), CC 71 (resonance), each stored XOR
                                          * 64: 0 is the neutral 64 (patterns saved before them hold 0) */
    uint8_t flags;                       /* ARR_TF_*: was rsv (0 in patterns saved before) */
    uint16_t first, count;               /* its events: ev[first .. first + count), sorted by tick */
} arr_trk_t;
#define ARR_TF_ASPLAYED 1u               /* the app switched it to Parallel at its first REC (a Chord 1 / 2 or Bass track):
                                          * once; a type set back by hand stays */

typedef struct {
    char name[8];
    uint8_t bars, beats;                 /* 1..ARR_MAXBARS bars of 2..7 beats */
    uint8_t rsv[2];
    arr_trk_t trk[ARR_TRACKS];
    uint16_t nev;
    uint16_t format;                     /* ARR_PAT_FORMAT: set by the engine (arr_load_pattern / arr_store_pattern) */
    arr_ev_t ev[ARR_NEV];
} pattern_t;
#define ARR_PAT_FORMAT 2u                /* 2: 480 ticks a beat (stored patterns of another format are not loaded) */

#define LC_SAME 0xFFFFu                   /* a loop's beat with no chord change ("·") */
/* a chord in 16 bits: root 0..11 (bits 0-3), type CH_* (4-8), bass 0..11 or 15 none (9-12) */
static inline uint16_t lc_pack(chord_t c)
{
    return (uint16_t)((c.root & 15u) | (uint32_t)(c.type & 31u) << 4 | (uint32_t)(c.bass < 12u ? c.bass : 15u) << 9);
}
static inline chord_t lc_unpack(uint16_t v)
{
    chord_t c = {(uint8_t)(v & 15u), (uint8_t)(v >> 4 & 31u), (uint8_t)((v >> 9 & 15u) < 12u ? (v >> 9 & 15u) : CH_NOBASS)};
    return c;
}
typedef struct {
    char name[8];                        /* up to 8 chars, NUL-padded, not terminated */
    uint8_t bars, beats;                 /* 0 bars: empty; 2..7 beats a bar; bars * beats <= ARR_LOOP_BEATS */
    uint8_t rsv[2];
    uint16_t ch[ARR_LOOP_BEATS];          /* beat i of the loop: lc_pack, or LC_SAME */
} loop_t;
/* ---- the lead tracks LD1, LD2: a song's melodies, linear along its rows, playing only in SONG. A note belongs to
 * a row and keeps its pitch as played with the root of the chord sounding when it was played; it plays moved by
 * how far the root sounding has moved since, folded to -6..+5 (lead_shift: same chord, as played; TRANSPOSE and a
 * row's KEY move the root sounding, so they move it too). The chord's quality is not followed. */
#define ARR_LEADS 2
#define ARR_LD1 ARR_TRACKS                /* LD1, LD2 as track numbers: arr_record, arr_track_mute, arr_mutes' bits */
#define ARR_ALLTRK (ARR_TRACKS + ARR_LEADS)
#define LEAD_PART_NOTES 420               /* a part (one shared slot) */
#define LEAD_PARTS 2
#define LEAD_MAX (LEAD_PART_NOTES * LEAD_PARTS)
typedef struct {                          /* 9 bytes, byte-aligned (lead_* below read and write it) */
    uint8_t row;                          /* 0..ARR_ROWS-1 */
    uint8_t pos[3];                       /* its place in the row, beat * ARR_PPQ + tick from the row's first downbeat,
                                           * linear across the row's REPEAT passes (8 x 64 beats: 245,760 at most) */
    uint8_t len[2];                       /* ticks, 1..65535 */
    uint8_t note, vel;                    /* as played, 1..127 */
    uint8_t tr;                           /* bit 7: LD2 (else LD1); bits 0-3: the root sounding when it was played */
} lead_ev_t;
static inline uint32_t lead_pos(const lead_ev_t *e) { return e->pos[0] | (uint32_t)e->pos[1] << 8 | (uint32_t)e->pos[2] << 16; }
static inline uint32_t lead_len(const lead_ev_t *e) { return e->len[0] | (uint32_t)e->len[1] << 8; }
static inline int lead_trk(const lead_ev_t *e) { return e->tr >> 7; }
static inline int lead_root(const lead_ev_t *e) { return e->tr & 15; }
static inline void lead_set(lead_ev_t *e, int row, uint32_t pos, uint32_t len, int note, int vel, int trk, int root)
{
    len = len < 1u ? 1u : len > 0xFFFFu ? 0xFFFFu : len;
    e->row = (uint8_t)row;
    e->pos[0] = (uint8_t)pos;
    e->pos[1] = (uint8_t)(pos >> 8);
    e->pos[2] = (uint8_t)(pos >> 16);
    e->len[0] = (uint8_t)len;
    e->len[1] = (uint8_t)(len >> 8);
    e->note = (uint8_t)(note & 127);
    e->vel = (uint8_t)(vel & 127);
    e->tr = (uint8_t)((trk ? 0x80u : 0u) | ((unsigned)root % 12u));
}
/* a recorded root's move to the root now, folded to -6..+5 (C to F: +5, up a fourth; C to G: -5, down a fourth) */
static inline int lead_shift(int now, int rec)
{
    int d = ((now - rec) % 12 + 12) % 12;
    return d > 5 ? d - 12 : d;
}
/* a lead track's sound and mixer, as a pattern's track's (bright and reso stored XOR 64: 0 is the neutral 64); vel: the
 * velocity its notes are recorded with (the keys have none: the app plays them with it), 1..127 */
typedef struct {
    uint8_t program, chan, vol, pan, rev, cho, bright, reso;
    uint8_t vel, rsv;
} lead_trk_t;
/* a lead part as stored in a shared slot (plat.h OBJ_PAT0..): the header, then n notes (saved: 12 + 9 n bytes). The
 * slots hold user patterns too: a pattern is a bare pattern_t (no header: as patterns were stored before), a lead part
 * starts with LEAD_MAGIC (bytes A5 A5 44 4C: never a pattern's name, which is text) */
#define LEAD_MAGIC 0x4C44A5A5u
#define LEAD_KIND 1u
typedef struct {
    uint32_t magic;                       /* LEAD_MAGIC */
    uint8_t kind;                         /* LEAD_KIND (LOOPS_KIND: a song's loops, loops_part_t) */
    uint8_t song, part;                   /* the song it is of (0..ARR_SONGS-1), its part (0, 1) */
    uint8_t rsv;
    uint16_t n;                           /* notes in it (0: the slot is free) */
    uint16_t rsv2;
    lead_ev_t ev[LEAD_PART_NOTES];
} lead_part_t;
typedef struct {                          /* a lead part's header alone (the slots looked at) */
    uint32_t magic;
    uint8_t kind, song, part, rsv;
    uint16_t n, rsv2;
} lead_hdr_t;
#define LEAD_HDR 12u
/* the selected song's lead notes in RAM (the app's, in the pool; the engine reads it, arr_leads): one ordered list
 * (row, place, track, note) kept in the two parts as they are stored, part 0 full before part 1 holds any
 * (lead_at). row0: each row's first note (row r: row0[r] .. row0[r + 1] - 1). trk: the sounds playing (the app's
 * copy of the song's lead[]). The app writes it inside arr_edit(1) .. arr_edit(0) and counts gen up before it
 * lets go; the render reads it only when not busy, and finds its place again when gen moved. */
typedef struct {
    lead_part_t part[LEAD_PARTS];
    uint16_t n;
    uint16_t row0[ARR_ROWS + 1];
    lead_trk_t trk[ARR_LEADS];
    volatile uint16_t gen;
} leads_t;
static inline lead_ev_t *lead_at(leads_t *l, uint32_t i)
{
    return i < LEAD_PART_NOTES ? &l->part[0].ev[i] : &l->part[1].ev[i - LEAD_PART_NOTES];
}
static inline const lead_ev_t *lead_at_c(const leads_t *l, uint32_t i)
{
    return i < LEAD_PART_NOTES ? &l->part[0].ev[i] : &l->part[1].ev[i - LEAD_PART_NOTES];
}
enum { ROW_A, ROW_A_FILL, ROW_B, ROW_B_FILL, ROW_A_NOFILL, ROW_B_NOFILL, ROW_NSECT };   /* a row's section: plain (a fill
                                                  * in its last bar when the next row's groove differs), + a fill (always),
                                                  * no fill (never); the no-fill two after the four saved before */
typedef struct {
    uint8_t loop;                        /* 0..ARR_LOOPS-1 */
    uint8_t sect;                        /* ROW_* */
    uint8_t rep;                         /* plays 1..8 times */
    uint8_t mute;                        /* tracks muted, bit t track t */
    int8_t key;                          /* -6..+5 semitones over the song's transpose */
    uint8_t lmute;                       /* the lead tracks muted, bit 0 LD1, bit 1 LD2 (was rsv) */
} row_t;
typedef struct {
    uint16_t style[ARR_NSECT];           /* the pattern for each section (as before) */
    uint16_t sty;                        /* the song's style (as before) */
    uint8_t nrows;                       /* 0..ARR_ROWS */
    uint8_t intro, ending, at_end;       /* Intro first; Ending last; at the end 0 stop, 1 again */
    uint8_t tempo;                       /* 40..240 */
    int8_t transpose;                    /* -6..+5 */
    uint8_t mute;                        /* the tracks muted in FREE and LOOP */
    uint8_t flags;                       /* SONG_F_*: was rsv (0 in songs saved before) */
    row_t row[ARR_ROWS];
    lead_trk_t lead[ARR_LEADS];          /* LD1, LD2's sounds (their notes: leads_t, in the shared slots) */
} song_t;
#define SONG_F_TEMPO 1u                  /* its tempo was set by hand: a style picked keeps it */
/* the four songs' settings and rows (OBJ_SONGS), and each song's 16 loops (its progressions: its own; in RAM the
 * selected song's, loops_part_t) packed in the rest of the object (loops.h loops_pack: only the loops not as a song
 * starts them, each only its chord changes). The four songs share one pool, prog[]: song n's bytes after songs
 * 0..n-1's, plen[n] of them (0: the defaults). Rarely (dense progressions, ~170 chord changes in a song) they do not
 * fit: that song's go into a shared slot whole, as before (loops_part_t; plen[n] ARR_PLEN_SLOT), so none is lost */
#define ARR_PROG_POOL 2888                /* the object's 3840 bytes less the head, the songs' 936 and plen */
#define ARR_PLEN_SLOT 0xFFFFu             /* plen[n]: song n's loops are in a slot (LOOPS_KIND, its song) */
typedef struct {
    uint32_t magic, format;
    song_t song[ARR_SONGS];
    uint16_t plen[ARR_SONGS];             /* song n's packed loops' bytes in prog[]; 0 the defaults; ARR_PLEN_SLOT */
    uint8_t prog[ARR_PROG_POOL];
} songs_t;
/* a song's loops whole: as the app keeps the selected song's in RAM (the engine reads its loop[], arr_loops), and as
 * stored in a shared slot when they do not fit the songs' pool: the slots' header (as a lead part's: LEAD_MAGIC, kind
 * LOOPS_KIND, its song, n ARR_LOOPS, 0: the slot is free), then the 16 loops */
#define LOOPS_KIND 2u
typedef struct {
    uint32_t magic;                       /* LEAD_MAGIC */
    uint8_t kind;                         /* LOOPS_KIND */
    uint8_t song, part, rsv;              /* the song they are of; part 0 */
    uint16_t n;                           /* ARR_LOOPS (0: the slot is free) */
    uint16_t rsv2;
    loop_t loop[ARR_LOOPS];
} loops_part_t;
static inline int row_main(const row_t *r)
{
    return r->sect == ROW_B || r->sect == ROW_B_FILL || r->sect == ROW_B_NOFILL ? SEC_B : SEC_A;
}
static inline int row_fill(const row_t *r) { return r->sect == ROW_A_FILL || r->sect == ROW_B_FILL; }
static inline int row_nofill(const row_t *r) { return r->sect == ROW_A_NOFILL || r->sect == ROW_B_NOFILL; }

/* ---- pattern numbers (a song's style[]): 0..ARR_USER_PATTERNS-1 the user patterns; ARR_FACTORY0 + 6 s + k the
 * factory style s's section k (0 Intro, 1 Main A, 2 Main B, 3 Fill AB, 4 Fill BA, 5 Ending: a slot); ARR_BANK0 +
 * 6 s + k the QY70 bank's style s (qybank.h; bring your own ROM), section k. Fixed bases: a number keeps its
 * meaning when the counts change.
 * The factory styles: the .sty files in styles/, compiled by tools/make_styles.py into styles.h; const in flash.
 * A section a style lacks is an empty pattern (no notes: the engine falls back, arr_section). */
#define ARR_FACTORY0 32
#define ARR_MAX_FACTORY 64
#define ARR_BANK0 512
#define ARR_BANK_STYLES 128
#define ARR_PATTERNS (ARR_BANK0 + ARR_NSECT * ARR_BANK_STYLES)   /* pattern numbers are below this */
_Static_assert(ARR_USER_PATTERNS <= ARR_FACTORY0 && ARR_FACTORY0 + ARR_NSECT * ARR_MAX_FACTORY <= ARR_BANK0,
               "the pattern numbers' ranges do not overlap");
int arr_styles(void);                            /* how many */
const char *arr_style_name(int s);               /* "Pop"; "" for none */
int arr_style_tempo(int s);                      /* its tempo, as written */
/* factory pattern n (ARR_FACTORY0..) expanded into *p (the app then loads it into a slot); -1 for none */
int arr_factory_pattern(int n, pattern_t *p);

static inline uint8_t arr_bright(const arr_trk_t *t) { return (uint8_t)((t->bright ^ 64u) & 127u); }
static inline uint8_t arr_reso(const arr_trk_t *t) { return (uint8_t)((t->reso ^ 64u) & 127u); }
static inline uint8_t arr_xor64(int v) { return (uint8_t)(((unsigned)v & 127u) ^ 64u); }
_Static_assert(sizeof(pattern_t) <= 3840, "a pattern fits one flash object");
_Static_assert(sizeof(songs_t) == 3840, "the songs and their packed loops fill one flash object");
_Static_assert(sizeof(lead_ev_t) == 9 && sizeof(lead_trk_t) == 10, "a lead note 9 bytes, a lead track's sound 10");
_Static_assert(sizeof(lead_part_t) == LEAD_HDR + 9 * LEAD_PART_NOTES && sizeof(lead_part_t) <= 3840, "a lead part fits a slot");
_Static_assert(sizeof(lead_hdr_t) == LEAD_HDR, "a lead part's header: 12 bytes, as its notes' offset");
_Static_assert((sizeof(pattern_t) - LEAD_HDR) % 9 != 0, "a lead part's length is never a pattern's");
_Static_assert(sizeof(loops_part_t) == LEAD_HDR + ARR_LOOPS * sizeof(loop_t) && sizeof(loops_part_t) != sizeof(pattern_t),
               "a song's loops: the header, then the loops; never a pattern's length");

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
 * programs and controllers, MIDI out (up to 5.8 ms late: the USB ring is sent from the main loop anyway).
 * 480 ticks a beat (up to ~11 in a block at 240 BPM): a tick with nothing due (no event, note end, beat or
 * clock) only moves the place on.
 *
 * MIDI clock: out, an F8 every ARR_CLOCK_TICKS (20) ticks. In, each F8 allows 20 more ticks, played at the
 * rate the clocks come at (the frames between them, smoothed), so the ticks between two clocks are spread
 * evenly, never 20 at once; a little behind (more than a clock's ticks owed), faster; far behind (clocks
 * bunched: more than 6 clocks' ticks owed), at once: the first 4 clocks' worth played, then the place jumps
 * from beat line to beat line (the notes due to end in the jump end, the beat's chord is set, nothing is
 * restruck); never ahead of the clocks: tick 20 k is not played before clock k + 1. */
void arr_init(void);
/* 1 PLAY, 0 stop. ARR_PLAY_COUNT: PLAY with a bar of count-in first (the app's, when it has a recording armed): the
 * transport starts as PLAY does (the section, an Intro, the song's row, as at tick 0; arr_playing) but stands at tick
 * 0 for one bar of the section starting (its beats a bar, the tempo), only the click playing (its downbeat high,
 * whatever the click's mode: a count-in), arr_pre its ticks left; then the band from tick 0, recording armed live
 * from there (a key in the count-in only sounds, except one landing on tick 0: arr_record). MIDI out: the click's
 * notes as always; MIDI start (FA) and the clock (F8) only when the band starts. A stop meanwhile cancels it (no FA,
 * no FC). Following MIDI clock in (arr_ext): no count-in, the far side's start is the start */
#define ARR_PLAY_COUNT 2
void arr_play(int on);
extern volatile uint32_t arr_pre;                  /* PLAY's count-in: its render ticks left, 0 none (the band) */
void arr_set_tempo(int bpm);                     /* 40..240 */
void arr_set_chord(chord_t c);                   /* the chord now (live, or the song's) */
/* slot 0..5 (a section's: sec - 1): Intro, Main A, Main B, Fill AB, Fill BA, Ending (copied) */
void arr_load_pattern(int slot, const pattern_t *p);
/* slot 0..5 plays section k (0..5) of a QY70 bank style, decoded in RAM (qybank.h; the caller keeps *s as it is
 * while any slot plays it: a slot stops playing it when another pattern or style is loaded into it, at once).
 * Each track plays its phrase from the section's start, repeated to fill the section (cut at its end), through
 * the transposer with the phrase's type, limits and high key; its notes are not held to ARR_NEV. Its
 * controllers (bend, modulation, expression, pan, channel pressure: qydec.h) go to the track's channel, synth and
 * MIDI out, never through the transposer; the channels they moved are reset (bend centred, modulation 0,
 * expression 127, pressure 0, sustain off, pan the track's) at a section change, a stop, and a reload of the
 * slot playing. *p: the section as a pattern (arr_bank_pattern): its tracks' sounds and settings (the ones
 * played: an edit stored with arr_store_pattern changes them) and its notes for the screen (not played).
 * Recording never goes into a bank section (the app gives the slot a pattern first). */
void arr_load_bank(int slot, const qb_style_t *s, int k, const pattern_t *p);
/* section k of bank style *s as a pattern: its bars and beats, each track's sounds, mixer, type, limits and high
 * key (the program the section's phrase plays), the phrases repeated to fill the section; past ARR_NEV notes the
 * quietest are dropped (their count returned) */
int arr_bank_pattern(const qb_style_t *s, int k, pattern_t *p);
/* The Intro and the Ending play over a held chord (the QY70's way: their phrases carry their own lead-in and
 * cadence). The Intro's: in LOOP the loop's beat-0 chord (an empty beat 0: the chord sounding there, the loop's
 * last), the loop starting at its beat 0 when the Intro ends (from PLAY or by hand); in SONG its first row's first
 * chord; in FREE the chord as it is. The Ending's: the chord on its first beat (the source's on that beat, else
 * the one before). Either stays until the section ends; the source changes it again from the next section. A
 * chord on the loop's beat 0 is a change there (the Intro's first beat, Main A's first beat after it): it ends a
 * chord from the keys, as any change of the loop does. Chords the player sets (the keys, arr_set_chord, the
 * transpose moved) change the held chord at once, also in the Intro and the Ending. Stopped by its own end (an
 * Ending played out, a song's last row): the section is what PLAY starts with again (a song's row 1's Main, else Main
 * A), arr_sect and arr_bar with it.
 * SEC_INTRO..SEC_END, or SEC_FILL: from the next bar (arr_next_sect shows it meanwhile, SEC_FILL as asked).
 * The Intro plays once, then Main A. Fill AB plays one bar, then Main B; Fill BA one bar, then Main A; SEC_FILL
 * is Fill BA from Main B, else Fill AB (from Main A); a fill during a fill adds a bar; SEC_FILL during the Intro
 * or the Ending is ignored (they play out). An ending plays its pattern once, then stops (Ending again during
 * it: it still stops). A section whose pattern has no
 * notes falls back: no Fill BA, Fill AB (still back to Main A), and the other way round; neither: no fill (the
 * section plays on); no Intro: Main A; no Ending: the stop at that bar line. Notes sounding over a section
 * change keep their voicing to their end, not re-voiced by the new pattern's track types. PLAY starts with the
 * section queued (HOME + black 1 stopped: the Intro); else, in SONG with INTRO on, the Intro when it has notes
 * (not when recording: the pattern being recorded into plays); else as it was: Main A or B, a fill's Main (Fill
 * AB: B, Fill BA: A), else Main A. FREE and LOOP never start with the Intro by themselves. */
void arr_section(int sec);
/* ---- the chord source. FREE: the keys' chords only (held: fingered). LOOP: a loop of the app's table goes
 * round, transposed; SONG: the rows of a song (arr_mode_song). The app's table and song stay put (they are the app's
 * loops_part_t, songs_t): the render reads them on beat and bar lines. The app brackets every write into them with
 * arr_edit(1) / arr_edit(0); a beat line that finds them busy takes its chord at the first block after. */
enum { ARR_FREE, ARR_LOOP, ARR_SONG };
void arr_loops(const loop_t *table);             /* at init: the app's ARR_LOOPS loops */
void arr_edit(int on);
void arr_mode_free(void);
/* loop i (0..ARR_LOOPS-1, another ignored): stopped, from PLAY (MIDI Continue: from the bar it continues in);
 * playing, from the next bar line (its bars count from there). Under an Intro (PLAY's or by hand) its bars count
 * from the Intro's end, the Intro holding its beat-0 chord (arr_section). FREE: at once, the chord staying */
void arr_mode_loop(int i);
/* SONG: song *s (the app's; it stays put) plays its rows from row r: stopped, from PLAY (with its Intro when
 * r is 0 and Intro is on; MIDI Continue: from the bar it continues in, the row's section from the next bar line);
 * playing, from the next bar line (no Intro). Each row plays its loop rep times in its section (Main A or B),
 * its tracks muted as the row says, its chords shifted by the transpose and the row's key. The section changes
 * at the row's first downbeat (the same section plays on); a fill plays in a row's last bar when the next row's
 * section differs, or the row asks for one (from Main B Fill BA, else Fill AB). A section, a fill or a mute set
 * by hand lasts until the next row. The Intro holds the first row's first chord, its mutes the row's. After
 * the last row: the Ending (on), held on the last chord, then the stop; else at_end again: row 0 (no Intro);
 * else the stop. The song's end wins over a section by hand (queued on its bar line, or during its Ending:
 * ignored, as a Fill is). Rows change on the pattern's bar lines: a loop whose bar differs from the pattern's
 * goes round in its own beats, and its row lasts its beats rounded up to the pattern's bar (a 3-beat loop ×2,
 * 6 beats, over 4-beat bars: 2 bars); an empty loop's row: a bar of 4 a pass, the chord unchanged. The engine
 * snapshots the song's header and the rows it needs at each block's start, when not busy (arr_edit): an edit
 * while it plays takes effect at the next block (the playing row's loop or key: its chord from the next beat;
 * a row's mutes and section: from its next start); a start (PLAY, MIDI Start / Continue) asked for during a
 * write waits for the first block after it. A song with no rows plays its sections (its Intro only when on, no
 * loop: arr_loop_now -1); rows written meanwhile start, row 1, at the next bar line (REC on SONG from an empty
 * song). Leaving SONG, the app sends its mutes again (arr_track_mute). */
void arr_mode_song(const song_t *s, int r);
/* the rows marked to go (the Song page's PROG "-"; bit r row r, as the rows are now): the song passes over them, as
 * if they were not there (the row after, the fills and the song's end as it then is); a marked row playing plays out
 * the pass it is in. A start on a marked row starts at the next one that is not (round); all marked: as a song with
 * no rows. Taken with the rows (each block's snapshot): the app sets it inside its arr_edit bracket when the rows
 * move under it */
void arr_rows_skip(uint32_t mask);
/* the rows the app just deleted (bit r row r, as the rows were before: several in one go), inside arr_edit(1) ..
 * arr_edit(0) with the deletion: the song plays on in place, the row playing renumbered (the rows before it gone:
 * its index down); deleted itself, it plays out its pass (marked: as above), then the row that came after it, and
 * meanwhile arr_row is 0xFF (none) and its lead notes (gone with it) are not played */
void arr_rows_deleted(uint32_t mask);
extern volatile uint8_t arr_row, arr_rep;         /* the row playing (a switch waiting: its row; 0xFF: one deleted
                                                   * playing out), its pass (0..rep-1) */
extern volatile uint8_t arr_row_next;             /* SONG: the row that plays after it (the marked passed over),
                                                   * 0xFF none (the song's end) */
extern volatile uint16_t arr_mutes;               /* the tracks muted now (rows and hand mutes), bit t track t; bits 8, 9
                                                   * LD1, LD2 (ARR_LD1 + k) */
/* -6..+5: everything, as an arranger keyboard's: loops and songs, and the keys (their chords, arr_keys_held, and
 * their notes, arr_key; in SONG with the row's key on top, as the stored chords). The chord sounding moves at once
 * by the difference, whatever set it (the keys' too; the Intro's and the Ending's held chord; during a write, at the
 * first block after it); the next chords come transposed. A key held keeps its note (its note-off is the note it
 * sounded). MIDI in is not transposed (it never comes here) */
void arr_set_transpose(int semis);
extern volatile uint8_t arr_mode;                /* ARR_* now (a change asked for: from its bar line) */
extern volatile int8_t arr_loop_now;             /* the loop playing (LOOP, or the song's row), -1 none */
extern volatile uint16_t arr_loop_beat;          /* the beat of that loop sounding (0..len-1) */
extern volatile uint8_t arr_override;            /* a chord from the keys holds until the source's next change */
extern volatile uint32_t arr_beats;              /* beats since PLAY (REC in FREE measures its take with it; a
                                                  * count-in not counted: 0 until the band's tick 0) */
/* live chords (fingered): the keys held, as MIDI notes, called on every change (one caller: the main loop).
 * Recognised (chord_recognize) here, only when a key was pressed (the set grew), and set as the chord; in LOOP
 * and SONG it holds until the source's next chord change; keys lifted one by one never re-recognise. The chord
 * sounding is the one recognised moved by the transpose (+ the row's key in SONG: arr_set_transpose). 1 = a new chord; 0 = none (no key pressed, fewer
 * than 3 pitch classes, or no type fits): the chord stays as it was. The app calls arr_keys_held(0, 0) when
 * chord entry (CHORD) is switched on, so keys already held then count as pressed anew. */
int arr_keys_held(const uint8_t *notes, int n);
/* a track muted (live, in every slot, until unmuted; the app keeps them per song): its notes end at once. trk
 * ARR_LD1, ARR_LD1 + 1: a lead track (in SONG by hand, until the next row, whose lmute then plays) */
void arr_track_mute(int trk, int on);
/* ---- loop recording, into the playing pattern (its slot's copy here: arr_pattern_copy reads it back).
 * arr_key: a key played (vel 0 or on 0: released), from the main loop. It sounds at once on the selected track's
 * channel (the app sends it nowhere else), moved by the transpose (arr_set_transpose: the note heard is the one
 * recorded, here or into a lead); with recording on and playing, it is also written into that track:
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
enum { ARR_Q_8, ARR_Q_8T, ARR_Q_16, ARR_Q_16T, ARR_Q_32, ARR_Q_OFF, ARR_NQ };   /* 240 160 120 80 60 1 ticks */
/* the selected track (also arr_key's sound), recording: 0 off, 1 on at once, ARR_REC_BAR from the next bar line
 * (stopped: from PLAY's downbeat, after its count-in with ARR_PLAY_COUNT). Waiting (arr_rec_wait), the keys only
 * sound, except one whose grid line is that bar line (a key a little early lands on it). Asked again while recording (another track): it goes on. From the
 * bar line recording starts at, arr_rec_bars counts the bars played (0 the first): the pass is arr_rec_bars /
 * the pattern's bars + 1, counted from where recording started */
#define ARR_REC_BAR 2
void arr_record(int trk, int on);
extern volatile uint8_t arr_rec_wait;
extern volatile uint16_t arr_rec_bars;
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
/* (the slot playing given another length, its bars as long as before, in Main A or B: its bar is the chord source's
 * again, the bars since a loop's beat 0, a song's row's first downbeat, else the section's start, modulo its bars; the
 * place in the bar kept. Else a place past a shortened length wraps into it) */
extern volatile uint8_t arr_full, arr_recording;  /* recording: on, until arr_record(.., 0) or (a pattern's track) a
                                                    * section change */
extern volatile uint16_t arr_dirty[ARR_NSECT];
void arr_midi_out_on(int on);
/* ---- the lead tracks (LD1, LD2; their data: leads_t above). In SONG, while a row plays (not in the song's Intro or
 * Ending), its lead notes play at their places (the row's ticks since its first downbeat, linear across its REPEAT
 * passes: a row of x2 has two passes' worth), each moved by lead_shift(the root sounding then, its root), on its lead
 * track's channel with its sounds (sent to the synth when SONG plays and when they change; MIDI out as the
 * patterns' sounds), after the patterns' notes of the same tick (the note-ons a block: MAX_ONS for all). Note-offs
 * and MIDI out as the patterns' notes. Notes sounding at a row change play to their end; a chord change does not
 * move a note sounding. The row's mutes (row_t.lmute) and hand mutes (arr_track_mute ARR_LD1 + k) silence a track
 * (its notes sounding end). The notes are read with the busy rule (arr_edit): the row playing's range (row0) is
 * taken when not busy and again whenever gen moved, from the first place not yet played (a write during a block:
 * its notes due play at the first block after, late, never twice). MIDI clock catch-up: the place jumps on, the
 * notes jumped over are not played (as the patterns'). Leads in a row shortened past its notes: those don't play. */
void arr_leads(const leads_t *l);                /* at init: the app's lead notes (it stays put) */
/* Recording: arr_record(ARR_LD1 + k, on) selects lead k (the keys play it: arr_key, on its channel with its sounds,
 * in any mode) and arms it (on: 1 at once; ARR_REC_BAR from the next bar line, counted in by the click as a track's,
 * only a key landing on that bar line recording before it; stopped: from PLAY's downbeat, after its count-in). It
 * records while a song's row plays:
 * each key played becomes a note of the row playing, at its place (the nearest tick to its block, to the nearest
 * line of the grid, arr_set_quant: a key just before a row's end lands on the next row's tick 0), its length to
 * its release (one grid step at least), its root the chord's sounding at its place (a key a little early, before
 * a beat line: the chord on that beat). Not while stopped, nor in FREE / LOOP, the song's Intro or Ending: the key
 * only sounds. A section change does not end it (leads run along the song). The engine writes nothing of the
 * app's: a note, once released and its place played, is handed over (arr_lead_take) and the app inserts it into
 * its leads_t (leads.c: the same note at the same place replaces), so the render never moves the app's data and the
 * note is never played twice (its place is behind the player's: it sounds from the next time the row plays).
 * A stop, recording off or another track: the keys held end their notes there (one whose place has not played
 * yet: dropped). A full hand-over ring (never, at the app's rate): the note is dropped and arr_lead_lost counts. */
typedef struct {
    uint8_t row, trk, note, vel;         /* trk: 0 LD1, 1 LD2 */
    uint8_t root, rsv[3];                /* the root sounding at its place, 0..11 */
    uint32_t pos, len;                   /* its place in the row (lead_ev_t pos), its length in ticks (1..65535) */
} arr_lnote_t;
int arr_lead_take(arr_lnote_t *n);              /* 1 = a recorded note (main loop) */
extern volatile uint8_t arr_lead_lost;
extern volatile uint32_t arr_row_tick;           /* the row playing's place (lead_ev_t pos): for the screen */
/* ---- the click: a short GM Woodblock (program 115) on MIDI channel 16 (ARR_CLICK_CHAN), on each beat line of the
 * pattern playing (its bar's first beat high, ARR_CLICK_HI; the others ARR_CLICK_LO), at a fixed level under the band
 * (CC 7 ARR_CLICK_VOL, dry), never muted by the tracks' mutes. Its sounds go to the synth with each click (the channel
 * is the click's); MIDI out as the patterns' notes (a note-on only with room for its note-off; its program and volume
 * with the sounds' priority). Mode ARR_CLICK_OFF / REC (while recording: the engine's own, a track or a lead, or the
 * app's REC, `rec`: writing chords, a take) / ON (always while playing). The count-in clicks whatever the mode: PLAY's
 * (ARR_PLAY_COUNT), a track recording waiting for its bar line (arr_rec_wait), and the beats before `beat` (arr_beats) given to arr_count_in (a
 * free take waiting for its bar; 0: none) */
enum { ARR_CLICK_OFF, ARR_CLICK_REC, ARR_CLICK_ON };
#define ARR_CLICK_CHAN 15
#define ARR_CHAN_MAX (ARR_CLICK_CHAN - 1)          /* the tracks' and leads' channels: 1..15, 16 is the click's */
static inline uint8_t arr_chan_fix(uint32_t c)  /* a channel loaded (stored, a bank's): 16 becomes 15 */
{
    c &= 15u;
    return (uint8_t)(c == ARR_CLICK_CHAN ? ARR_CHAN_MAX : c);
}
#define ARR_CLICK_PROG 115
#define ARR_CLICK_HI 84
#define ARR_CLICK_LO 77
#define ARR_CLICK_VOL 72
void arr_click(int mode, int rec);
void arr_count_in(uint32_t beat);
/* MIDI clock in: F8, FA (a start, as PLAY), FB (on from where it stopped; after an Ending or a song's end
 * stopped it: a start, as PLAY), FC */
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
extern volatile uint8_t arr_ended;                /* the last stop was the source's own (an Ending, a song's end) */
extern volatile uint16_t arr_bar, arr_beat_tick;     /* position in the playing pattern */
extern volatile uint32_t arr_chord_now;            /* the chord, packed in one word (never torn): arr_chord_unpack */
static inline uint32_t arr_chord_pack(chord_t c) { return (uint32_t)c.root | (uint32_t)c.type << 8 | (uint32_t)c.bass << 16; }
static inline chord_t arr_chord_unpack(uint32_t v)
{
    chord_t c = {(uint8_t)(v & 0xFFu), (uint8_t)(v >> 8 & 0xFFu), (uint8_t)(v >> 16 & 0xFFu)};
    return c;
}
extern volatile uint32_t arr_track_hit[ARR_ALLTRK];  /* render ticks of each track's last note-on (8, 9: LD1, LD2) */
extern volatile uint32_t arr_ticks;                  /* render ticks since arr_init */
