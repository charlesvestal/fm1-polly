/* SPDX-License-Identifier: GPL-3.0-only */
/* The arranger engine (arr.h): a pattern's 8 tracks through the transposer over the chord now, on the
 * clock (ours, or MIDI clock in), into the GM synth and MIDI out. Threads and commands as Life's (life.c):
 * the main loop posts, the render drains and runs the sequencer, called by gm_render between EAS blocks.
 * Everything here is in statics: the render runs on the 8 KiB interrupt stack. */
#include <string.h>
#include "arr.h"
#include "gm.h"
#include "styles.h"

#define BARRIER() __asm__ volatile("" ::: "memory")
#define ARR_HELD 8                                 /* notes held a track */
#define RESTRIKE_MIN (ARR_PPQ / 2)                 /* at a chord change, notes with more ticks left (an eighth) restrike */
#define EXT_TIMEOUT 26460u                         /* frames without a clock in (0.6 s): back to our own */
#define FPT_Q16_1BPM 361267200u                    /* Q16 output frames a tick at 1 BPM: 44100 * 60 / 480 * 65536 */
#define MAX_ONS 48                                 /* note-ons a block, at most: the interrupt's work bounded */
#define MAX_CATCHUP 4                              /* bunched clocks in: the first 4 clocks' ticks sound, the rest only move on */
#define EXT_LAG (6u * ARR_CLOCK_TICKS)             /* clock in: ticks owed beyond which they move on at once (a few
                                                    * clocks bunched by USB are only played faster) */
#define EXT_PEND_MAX (2u * ARR_PPQ)                /* clock in: ticks owed, at most (two beats) */
#define FAR 0x40000000u                            /* render ticks: "not soon" (comparisons are wrap-safe) */
#define START_WAIT 8                               /* blocks a start may wait for its sounds to go out */
#define OUT_RESERVE 8u                             /* MIDI out slots kept free beyond the note-offs owed */

volatile uint8_t arr_playing, arr_sect = SEC_A, arr_next_sect, arr_ext;
volatile uint16_t arr_bar, arr_beat_tick;
volatile uint8_t arr_mode, arr_override;
volatile int8_t arr_loop_now = -1;
volatile uint16_t arr_loop_beat;
volatile uint8_t arr_row, arr_rep;
volatile uint16_t arr_mutes;
volatile uint32_t arr_beats;
volatile uint32_t arr_chord_now;
volatile uint32_t arr_track_hit[ARR_ALLTRK];
volatile uint32_t arr_ticks;

/* ------------------------------------------------------------ commands --- */
enum { C_PLAY, C_TEMPO, C_CHORD, C_SECTION, C_MUTE, C_MOUT, C_CLOCK, C_MODE, C_TRANSPOSE, C_LIVE,
       C_REC, C_NOTE, C_QUANT, C_ERASE };
#define QN 64u
static uint32_t q[QN], qb[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, uint32_t a, uint32_t b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                    /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | (a & 0xFFFFFFu);
    qb[w % QN] = b;
    BARRIER();
    q_w = w + 1;
}

/* the six slots (Intro, Main A, Main B, Fill AB, Fill BA, Ending: a section's is sec - 1). The main loop
 * copies a pattern in with its slot marked busy, then counts the slot's generation up before it releases it;
 * the render, which interrupts the main loop and never the other way round, leaves a busy slot alone and,
 * seeing a new generation, finds its tracks' places in it before it plays anything (sync). */
static pattern_t pat[ARR_NSECT];
/* a slot playing a bank style's section: the style (else 0) and the section; written with the slot busy, read by
 * the render only when it is not */
static const qb_style_t *bk[ARR_NSECT];
static uint8_t bsec[ARR_NSECT];
static volatile uint8_t busy[ARR_NSECT], gen[ARR_NSECT];
static volatile uint8_t ident[ARR_NSECT];         /* counts loads of another pattern (not stores of an edit) */
static uint8_t seen[ARR_NSECT];
/* the chord source's data: the app's loop table (read on beat and bar lines), busy while the app writes it */
static const loop_t *loops;
static volatile uint8_t edit_busy;

void arr_play(int on) { post(C_PLAY, on != 0, 0); }
void arr_set_tempo(int bpm) { post(C_TEMPO, (uint32_t)(bpm < 40 ? 40 : bpm > 240 ? 240 : bpm), 0); }
void arr_set_chord(chord_t c) { post(C_CHORD, arr_chord_pack(c), 0); }
void arr_section(int sec) { post(C_SECTION, (uint32_t)sec, 0); }
void arr_track_mute(int trk, int on) { post(C_MUTE, (uint32_t)trk, on != 0); }
void arr_midi_out_on(int on) { post(C_MOUT, on != 0, 0); }
void arr_clock(int msg) { post(C_CLOCK, (uint32_t)msg, 0); }
void arr_loops(const loop_t *t) { loops = t; }
void arr_edit(int on) { BARRIER(); edit_busy = (uint8_t)(on != 0); BARRIER(); }
void arr_mode_free(void) { post(C_MODE, ARR_FREE, 0); }
void arr_mode_loop(int i)
{
    if (i >= 0 && i < ARR_LOOPS)                   /* (another: ignored) */
        post(C_MODE, ARR_LOOP, (uint32_t)i);
}
static const song_t *volatile sg_want;            /* the song asked for (the main loop's write, taken by C_MODE) */
void arr_mode_song(const song_t *s, int r)
{
    if (!s)
        return;
    sg_want = s;
    BARRIER();
    post(C_MODE, ARR_SONG, (uint32_t)(r < 0 ? 0 : r));
}
void arr_set_transpose(int s) { post(C_TRANSPOSE, (uint32_t)(int32_t)(s < -6 ? -6 : s > 5 ? 5 : s) & 0xFFu, 0); }
static uint32_t keys_prev[4];                      /* the keys held at the last call (the caller's thread) */
int arr_keys_held(const uint8_t *notes, int n)    /* in the caller's thread: chords.c has no state */
{
    uint32_t now[4] = {0, 0, 0, 0}, grew = 0;
    chord_t c;
    if (!notes)
        n = 0;
    for (int i = 0; i < n; i++)
        now[notes[i] >> 5 & 3u] |= 1u << (notes[i] & 31u);
    for (int k = 0; k < 4; k++) {
        grew |= now[k] & ~keys_prev[k];
        keys_prev[k] = now[k];
    }
    if (!grew || n < 3 || !chord_recognize(notes, n, &c))    /* keys lifted: the chord stays (fingered) */
        return 0;
    post(C_LIVE, arr_chord_pack(c), 0);
    return 1;
}
void arr_record(int trk, int on)
{
    post(C_REC, (uint32_t)(trk >= 0 && trk < ARR_ALLTRK ? trk : 0), on == ARR_REC_BAR ? ARR_REC_BAR : on != 0);
}
void arr_key(int note, int on, int vel)
{
    post(C_NOTE, (uint32_t)(note & 0x7F) | (uint32_t)(on ? vel & 0x7F : 0) << 8, 0);
}
void arr_set_quant(int q) { post(C_QUANT, (uint32_t)(q >= 0 && q < ARR_NQ ? q : ARR_Q_16), 0); }
void arr_erase_track(int trk)
{
    if (trk >= 0 && trk < ARR_TRACKS)
        post(C_ERASE, (uint32_t)trk, 0);
}
volatile uint8_t arr_full, arr_recording;
volatile uint8_t arr_rec_wait;
volatile uint16_t arr_rec_bars;
volatile uint16_t arr_dirty[ARR_NSECT];
/* the render writes a slot's pattern only between blocks and counts arr_dirty up after: a copy with the same
 * count before and after was not written meanwhile (the render interrupts this loop, never the other way) */
uint16_t arr_pattern_copy(int slot, pattern_t *out)
{
    uint16_t d;
    if (slot < 0 || slot >= ARR_NSECT)
        return 0;
    do {
        d = arr_dirty[slot];
        BARRIER();
        memcpy(out, &pat[slot], sizeof *out);
        BARRIER();
    } while (d != arr_dirty[slot]);
    return d;
}
void arr_load_pattern(int slot, const pattern_t *p)
{
    if (slot < 0 || slot >= ARR_NSECT)
        return;
    busy[slot] = 1;
    BARRIER();
    memcpy(&pat[slot], p, sizeof *p);
    pat[slot].format = ARR_PAT_FORMAT;
    bk[slot] = 0;
    gen[slot]++;
    ident[slot]++;                                 /* notes being recorded into the one before: dropped */
    arr_full = 0;
    BARRIER();
    busy[slot] = 0;
}
void arr_load_bank(int slot, const qb_style_t *s, int k, const pattern_t *p)
{
    if (slot < 0 || slot >= ARR_NSECT || !s || k < 0 || k >= ARR_NSECT) {
        if (slot >= 0 && slot < ARR_NSECT)
            arr_load_pattern(slot, p);
        return;
    }
    busy[slot] = 1;
    BARRIER();
    memcpy(&pat[slot], p, sizeof *p);
    pat[slot].format = ARR_PAT_FORMAT;
    bk[slot] = s;
    bsec[slot] = (uint8_t)k;
    gen[slot]++;
    ident[slot]++;
    arr_full = 0;
    BARRIER();
    busy[slot] = 0;
}
int arr_store_pattern(int slot, const pattern_t *p, uint16_t expect)
{
    if (slot < 0 || slot >= ARR_NSECT)
        return 0;
    busy[slot] = 1;                                /* from here the render writes nothing into it */
    BARRIER();
    if (arr_dirty[slot] != expect) {               /* recorded into since the copy: copy again */
        BARRIER();
        busy[slot] = 0;
        return 0;
    }
    memcpy(&pat[slot], p, sizeof *p);
    pat[slot].format = ARR_PAT_FORMAT;
    gen[slot]++;
    arr_full = 0;
    BARRIER();
    busy[slot] = 0;
    return 1;
}
#ifdef ARR_TEST
static uint32_t tick_calls;
uint32_t arr_test_tick_calls(void) { return tick_calls; }
#define TICK_CALLED() tick_calls++
#else
#define TICK_CALLED() ((void)0)
#endif
#ifdef ARR_TEST
void arr_test_busy(int slot, int on) { busy[slot] = (uint8_t)(on != 0); }   /* a load caught midway */
#endif

/* ------------------------------------------------------------ the state --- */
typedef struct {
    uint8_t out, in, vel, chan;
    uint8_t mo;                                    /* its note-on went to MIDI out: its note-off goes there too */
    uint8_t fixed;                                 /* from the section before: 1 kept as is, 2 (it was on a
                                                    * Chord 1 / 2 or Bass track) ended at a chord change */
    uint8_t blk;                                   /* the block its note-on went in (blk_count, low byte) */
    uint32_t end;                                  /* the render tick (arr_ticks) it ends at */
} held_t;
typedef struct {
    xstate_t xs;
    uint16_t nx;                                   /* the next event (index into ev; a bank section: into its notes) */
    uint16_t cx;                                   /* a bank section: the next controller event */
    uint32_t rep;                                  /* a bank section: where its phrase's pass started (pattern ticks) */
    uint8_t nh, mute;
    held_t h[ARR_HELD];
} track_t;
static track_t tk[ARR_ALLTRK];                     /* the patterns' tracks, then LD1, LD2 (their notes held) */
static uint8_t cur;                                /* the slot playing */
static uint32_t pos;                               /* ticks into the pattern */
/* the work a tick: most of 480 a beat have nothing due, and only move the place on */
static uint32_t next_end;                          /* the earliest note end (render ticks): never later than it */
static uint32_t next_ev;                           /* the earliest next event of the tracks (pattern ticks) */
static uint8_t wake;                               /* the tracks' places moved: look at the events at the next tick */
static uint8_t clk_div;                            /* clock out: ticks since the last F8, 0..ARR_CLOCK_TICKS - 1 */
static chord_t chord;
static uint8_t mout;                               /* MIDI out on */
static uint8_t clk_out;                            /* we send the clock: since our own start with MIDI out on */
static uint32_t out_owed;                          /* note-offs owed to MIDI out */
#define NSOUND 7                                   /* a track's sounds: program, CC 7, 10, 91, 93, 74, 71 */
static uint8_t out_sent[16][NSOUND];                    /* each channel's sounds as last sent out; 0xFF unknown */
static uint8_t out_pend;                           /* the playing pattern's sounds still to send out */
static uint32_t out_link;                          /* the connection they were sent on */
static int ons_left;                               /* note-ons this block may still start */
static uint8_t blk_count;                          /* counts the blocks (a note struck in this one: held_t blk) */
static uint32_t at;                                /* the tick playing: its sample within the EAS block (gm_midi_now) */
#define LATE_OFFS 64                               /* note-offs held for the next block's start, at most */
static uint16_t late_off[LATE_OFFS];               /* chan << 8 | note */
static int n_late;
static uint8_t start_wait;                         /* a start waiting for its sounds to go out: blocks waited + 1 */
/* the lead tracks (arr.h): the app's notes, read with the busy rule; the player's place in the row playing */
static const leads_t *ld;                          /* the app's lead notes (arr_leads), 0 none */
static uint16_t ld_gen;                            /* their gen when ld_i was found */
static uint8_t ld_ok;                              /* ld_i, ld_end found for ld_row at ld_gen */
static uint8_t ld_row;                             /* the row they are of */
static uint16_t ld_i, ld_end;                      /* the row's next note and its end (indexes into the list) */
static uint32_t ld_tick;                           /* the row's place of the next tick (lead_ev_t pos) */
static uint32_t ld_from;                           /* the first place whose notes are not played or passed yet */
static lead_trk_t ld_snd[ARR_LEADS];               /* the leads' sounds as sent (ld_snd_ok: sent since the last setup) */
static uint8_t ld_snd_ok, ld_out_pend;             /* ... and still owed to MIDI out */
volatile uint32_t arr_row_tick;
static void lead_init(void);
static void lead_row_start(void);
static void lead_sounds(void);
static void lead_sounds_out(void);
static uint32_t lead_sounds_need(void);
static int leads_heard(void);

/* sections, the chord source */
static uint8_t first_tick;                         /* the first tick since a start: no bar or beat behind it */
static uint8_t fill_to = SEC_B;                    /* the Main the fill playing goes to after its bar */
volatile uint8_t arr_ended;                        /* stopped by an Ending or a song's end (Continue: a new start) */
static uint16_t sec_bars;                          /* bars played of the section playing */
static uint8_t chord_owed;                         /* leaving a held Intro / Ending (a loop's start, a transpose):
                                                    * the source's chord sounding there, not only a change */
static uint32_t beat_now, bar_beat0;               /* beats since the start: now, at the bar's downbeat */
static uint8_t mode, mode_next, lp, lp_next;       /* the source, and one asked for (from its bar line) */
static uint8_t mode_wait;                          /* mode_next waits for the bar line */
static int8_t transp;                              /* -6..+5 */
static uint8_t chord_wait;                         /* a beat line found the table busy: its chord owed */
static uint8_t override;                           /* a chord from the keys holds until the source's next change */
static uint32_t lp_origin;                         /* beat_now where the loop's beat 0 fell */
static uint32_t wait_from;                         /* the first beat line chord_wait missed */
static uint16_t lbeat;                             /* the loop's beat sounding (arr_loop_beat, published a block) */
/* SONG */
static const song_t *sg, *sg_next;                 /* the app's song playing; the one asked for (from mode_apply) */
static uint8_t row_from;                           /* the row PLAY or the switch starts at */
static uint8_t rows_live;                          /* the rows' clock runs (not in the song's Intro or Ending) */
static uint8_t rows_wait;                          /* a song with no rows playing: its rows start at the first bar
                                                    * line that finds some (REC on SONG writing the first row) */
static uint8_t row;                                /* the row playing */
static uint32_t row_b0;                            /* beat_now at its first downbeat */
static uint8_t sect_owed;                          /* MIDI Continue into a song: the row's section at the next bar line */
static uint8_t song_end;                           /* the song's Ending playing: sections by hand ignored */
static int8_t xp_pend;                             /* a transpose during a write: the sounding chord's move, owed */
/* the song as the render sees it: its header and the rows it needs (the one playing, the next two: a write
 * across a row change still knows the row after), taken at a block's start when not busy */
static struct {
    uint8_t nrows, intro, ending, at_end;
    row_t cur, next, next2;
    uint16_t cur_len, next_len, next2_len;
} sn;

/* our clock: Q16 output frames to the next tick, and a tick's length (ours; pace: the one in use, ours or
 * the clock in's) */
static int32_t ftt;
static uint32_t fpt, pace;
/* clock in: ticks the clocks allow and not played yet; frames since a clock; clocks this block; the frames
 * between the last two blocks with clocks (since_ok: a clock came before, so ext_since measures something);
 * the clock's rate, Q16 frames a tick (0: not measured yet) */
static uint32_t ext_pend, ext_age, ext_since, ext_clocks, ext_fpt;
static uint8_t since_ok;

/* a track's note to play now, and a key's note to store: "as played" tracks (xpose_played), else the type's */
static int as_played(const arr_trk_t *t) { return (t->flags & ARR_TF_ASPLAYED) && t->x.type == XT_PARALLEL; }
static int trk_xpose(const arr_trk_t *t, xstate_t *xs, int note, chord_t c)
{
    return as_played(t) ? xpose_played(&t->x, note, c) : xpose(&t->x, xs, note, c);
}
static int trk_inverse(const arr_trk_t *t, int key, chord_t c)
{
    return as_played(t) ? xpose_played_inverse(key, c) : xpose_inverse(&t->x, key, c);
}
static uint32_t pat_len(const pattern_t *p) { return (uint32_t)(p->bars ? p->bars : 1) * (p->beats ? p->beats : 4) * ARR_PPQ; }
static uint32_t bar_len(const pattern_t *p) { return (uint32_t)(p->beats ? p->beats : 4) * ARR_PPQ; }
static uint32_t ev_end(const arr_trk_t *t)
{
    uint32_t e = (uint32_t)t->first + t->count;
    return e > ARR_NEV ? ARR_NEV : e;
}

/* ------------------------------------------------------------ MIDI out --- */
/* room for n more messages besides the note-offs owed and the reserve */
static int out_room(uint32_t n) { return arr_midi_out_room() >= out_owed + OUT_RESERVE + n; }
static void out_rt(uint32_t st)                    /* FA, FC, F8: never at the cost of a note-off */
{
    if (arr_midi_out_room() > out_owed)
        arr_midi_out(st, 0, 0);
}

/* ---------------------------------------------------------------- notes --- */
static void held_remove(track_t *t, int i)
{
    t->nh--;
    for (; i < t->nh; i++)
        t->h[i] = t->h[i + 1];
}
static void off_out(held_t *h)
{
    if (h->mo) {
        arr_midi_out(0x80u | h->chan, h->out, 0);
        h->mo = 0;
        out_owed--;
    }
}
static void on_out(held_t *h)
{
    if (mout && out_room(1)) {
        arr_midi_out(0x90u | h->chan, h->out, h->vel);
        h->mo = 1;
        out_owed++;
    }
}
/* the synth takes a note-off at a block's start: one due in a block's second half goes at the next one's, so
 * a release is +-2.9 ms from its time and a note's length right on average; so does one of a note struck in
 * this block (fresh: a note of a few ticks, its note-on later in the block than the block's start). A note-on
 * of the same note before then sends it first (never after the note-on: it would end the new note). */
static void synth_off(uint32_t chan, uint32_t note, int fresh)
{
    if ((fresh || at >= GM_BLOCK / 2) && n_late < LATE_OFFS)
        late_off[n_late++] = (uint16_t)(chan << 8 | note);
    else
        gm_midi_now(0x80u | chan, note, 0, 0);
}
void arr_late_offs(void)                           /* the held note-offs, now (arr.h) */
{
    for (int i = 0; i < n_late; i++)
        gm_midi_now(0x80u | (late_off[i] >> 8), late_off[i] & 0x7Fu, 0, 0);
    n_late = 0;
}
static void synth_on(uint32_t chan, uint32_t note, uint32_t vel)
{
    for (int i = 0; i < n_late; i++)
        if (late_off[i] == (chan << 8 | note)) {
            gm_midi_now(0x80u | chan, note, 0, 0);
            late_off[i] = late_off[--n_late];
            break;
        }
    gm_midi_now(0x90u | chan, note, vel, at);
}
static void note_end(track_t *t, int i)            /* the note-off goes to the note it struck */
{
    held_t *h = &t->h[i];
    synth_off(h->chan, h->out, h->blk == blk_count);
    off_out(h);
    held_remove(t, i);
}
static void track_off(track_t *t)
{
    while (t->nh)
        note_end(t, t->nh - 1);
}
static void note_start(int ti, int in, int out, int vel, int chan, int len)
{
    track_t *t = &tk[ti];
    held_t *h;
    if (ons_left <= 0)
        return;
    ons_left--;
    for (int i = 0; i < t->nh; i++)                /* the same note again: end the first */
        if (t->h[i].out == out && t->h[i].chan == chan) {
            note_end(t, i);
            break;
        }
    if (t->nh >= ARR_HELD)                         /* full: the oldest gives way */
        note_end(t, 0);
    h = &t->h[t->nh++];
    h->out = (uint8_t)out;
    h->in = (uint8_t)in;
    h->vel = (uint8_t)(vel ? vel : 1);
    h->chan = (uint8_t)chan;
    h->mo = 0;
    h->fixed = 0;
    h->blk = blk_count;
    h->end = arr_ticks + (uint32_t)(len ? len : 1);
    if ((int32_t)(h->end - next_end) < 0)
        next_end = h->end;
    synth_on((uint32_t)chan, (uint32_t)out, h->vel);
    on_out(h);
    arr_track_hit[ti] = arr_ticks;
}
static void all_off(void)
{
    for (int t = 0; t < ARR_ALLTRK; t++)
        track_off(&tk[t]);
}

/* ---------------------------------------------------------- controllers --- */
/* a bank phrase's controller kinds (qydec.h QY_NKINDS): bend (its top 7 bits), CC 1, CC 11, CC 10, pressure */
static const uint8_t KIND_ST[QY_NKINDS] = {0xE0, 0xB0, 0xB0, 0xB0, 0xD0, 0xB0, 0xB0};
static const uint8_t KIND_CC[QY_NKINDS] = {0, 1, 11, 10, 0, 74, 71};
_Static_assert(QY_NKINDS == 7, "each controller kind its message (KIND_ST, KIND_CC)");
/* channels a phrase's controllers moved: in the synth; out (MIDI out was on: sent or dropped, the far side's state
 * unknown); and the MIDI-out resets still owed (a full ring: retried each block until sent, as the sounds) */
static uint16_t ctl_dirty, ctl_out_dirty, ctl_out_pend;
static void ctl_send(uint32_t chan, uint32_t kind, uint32_t v)
{
    uint32_t st = KIND_ST[kind] | chan, d1 = KIND_CC[kind], d2 = v;
    if (KIND_ST[kind] == 0xE0u)
        d1 = 0;                                    /* a bend's top 7 bits: the MSB */
    else if (KIND_ST[kind] == 0xD0u)
        d1 = v, d2 = 0;
    gm_midi_now(st, d1, d2, at);
    ctl_dirty |= (uint16_t)(1u << chan);
    if (mout) {
        if (out_room(1))
            arr_midi_out(st, d1, d2);
        ctl_out_dirty |= (uint16_t)(1u << chan);
        if (st == (0xB0u | chan) && (d1 == 10 || d1 == 74 || d1 == 71))   /* the far side's: not the track's now */
            out_sent[chan][d1 == 10 ? 2 : d1 == 74 ? 5 : 6] = 0xFF;
    }
}
/* a reset: bend centred, modulation 0, expression full, pressure 0, sustain off; pan, brightness and resonance the
 * slot playing's (its first track on that channel; centre, 64, without one) */
#define CTL_RESET_N 8
static void ctl_reset_msg(uint32_t c, int i, uint32_t *st, uint32_t *d1, uint32_t *d2)
{
    static const uint8_t ST[CTL_RESET_N] = {0xE0, 0xB0, 0xB0, 0xD0, 0xB0, 0xB0, 0xB0, 0xB0},
                         D1[CTL_RESET_N] = {0, 1, 11, 0, 64, 10, 74, 71}, D2[CTL_RESET_N] = {64, 0, 127, 0, 0, 64, 64, 64};
    *st = ST[i] | c;
    *d1 = D1[i];
    *d2 = D2[i];
    if (i >= 5)                                    /* the track's own: pan, brightness, resonance */
        for (int t = ARR_TRACKS - 1; t >= 0; t--)
            if ((pat[cur].trk[t].chan & 15u) == c)
                *d2 = i == 5 ? pat[cur].trk[t].pan & 127u : i == 6 ? arr_bright(&pat[cur].trk[t]) : arr_reso(&pat[cur].trk[t]);
}
/* the MIDI-out resets owed, as the ring has room for a channel's whole reset (MIDI out off: none owed) */
static void ctl_out_flush(void)
{
    if (!mout) {
        ctl_out_pend = ctl_out_dirty = 0;
        return;
    }
    for (uint32_t c = 0; c < 16 && ctl_out_pend; c++) {
        uint32_t st, d1, d2;
        if (!(ctl_out_pend >> c & 1u))
            continue;
        if (!out_room(CTL_RESET_N))
            return;                                /* the rest at a later block */
        for (int i = 0; i < CTL_RESET_N; i++) {
            ctl_reset_msg(c, i, &st, &d1, &d2);
            arr_midi_out(st, d1, d2);
            if (i >= 5)
                out_sent[c][i == 5 ? 2 : i == 6 ? 5 : 6] = (uint8_t)d2;
        }
        ctl_out_pend &= (uint16_t)~(1u << c);
    }
}
/* the channels in `mask` the phrases moved, back: the synth now, MIDI out now or in later blocks */
static void ctl_reset_mask(uint32_t mask)
{
    for (uint32_t c = 0; c < 16; c++) {
        uint32_t st, d1, d2;
        if (!((mask & ctl_dirty) >> c & 1u))
            continue;
        for (int i = 0; i < CTL_RESET_N; i++) {
            ctl_reset_msg(c, i, &st, &d1, &d2);
            gm_midi_now(st, d1, d2, at);
        }
        ctl_dirty &= (uint16_t)~(1u << c);
    }
    ctl_out_pend |= (uint16_t)(ctl_out_dirty & mask);
    ctl_out_dirty &= (uint16_t)~mask;
    ctl_out_flush();
}
static void ctl_reset(void) { ctl_reset_mask(0xFFFFu); }

/* the playing pattern's sounds: program, volume, pan, sends. To the synth whenever a pattern starts (setup);
 * to MIDI out after the block's notes, only what changed since it was last sent, the rest in later blocks
 * (sounds_out). sound(): track t's message k (0 the program, 1..6 the CCs: volume, pan, reverb, chorus, brightness,
 * resonance) as st | d1 << 8 | d2 << 16 */
static const uint8_t SOUND_CC[NSOUND] = {0, 7, 10, 91, 93, 74, 71};
static uint32_t sound(const arr_trk_t *t, int k, uint8_t *v)
{
    uint32_t c = t->chan & 15u;
    *v = k == 0 ? t->program : k == 1 ? t->vol : k == 2 ? t->pan : k == 3 ? t->rev : k == 4 ? t->cho
       : k == 5 ? arr_bright(t) : arr_reso(t);
    return k ? (0xB0u | c) | (uint32_t)SOUND_CC[k] << 8 | (uint32_t)*v << 16 : (0xC0u | c) | (uint32_t)*v << 8;
}
static void setup(void)
{
    const pattern_t *p = &pat[cur];
    ld_snd_ok = 0;                                 /* the leads' sounds again too (in SONG: lead_sounds) */
    if (busy[cur])
        return;
    for (int i = 0; i < ARR_TRACKS; i++)
        for (int k = 0; k < NSOUND; k++) {
            uint8_t v;
            uint32_t m = sound(&p->trk[i], k, &v);
            gm_midi_now(m & 0xFFu, m >> 8 & 0xFFu, m >> 16 & 0xFFu, at);
        }
    out_pend = mout;
}
static void sounds_out(void)
{
    const pattern_t *p = &pat[cur];
    ctl_out_flush();                               /* the controller resets owed, before the sounds */
    lead_sounds_out();
    if (!out_pend || !mout || busy[cur])
        return;
    for (int i = 0; i < ARR_TRACKS; i++)
        for (int k = 0; k < NSOUND; k++) {
            uint8_t v;
            uint32_t m = sound(&p->trk[i], k, &v), c = m & 15u;
            if (out_sent[c][k] == v)
                continue;
            if (!out_room(1))
                return;                            /* the rest in a later block */
            arr_midi_out(m & 0xFFu, m >> 8 & 0xFFu, m >> 16 & 0xFFu);
            out_sent[c][k] = v;
        }
    out_pend = 0;
}
/* at a start: the changed sounds to send, and tick 0's notes, fit the ring now with the note-offs owed and
 * the reserve (each note-on needs room for itself and its note-off, see on_out)? */
static int sounds_fit_first(void)
{
    const pattern_t *p = &pat[cur];
    uint32_t need = 2 + lead_sounds_need();        /* FA and tick 0's F8; the leads' sounds */
    if (busy[cur])
        return 0;
    for (int i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *t = &p->trk[i];
        uint32_t e = ev_end(t);
        for (int k = 0; k < NSOUND; k++) {
            uint8_t v, w;
            uint32_t m = sound(t, k, &v), c = m & 15u;
            int dup = 0;
            for (int j = 0; j < i && !dup; j++)    /* an earlier track sends the same */
                dup = (p->trk[j].chan & 15u) == c && (sound(&p->trk[j], k, &w), w == v);
            need += out_sent[c][k] != v && !dup;
        }
        if (!tk[i].mute && !t->mute)
            for (uint32_t n = t->first; n < e && p->ev[n].tick == 0; n++)
                need += 2;
    }
    return arr_midi_out_room() >= out_owed + OUT_RESERVE + need;
}
/* a bank section's track t: the phrase it plays, or 0 */
static const qy_phrase_t *bphrase(int t)
{
    const qb_style_t *S = bk[cur];
    uint32_t u;
    if (!S)
        return 0;
    u = S->use[bsec[cur]][t];
    return u < S->nph && S->ph[u].length ? &S->ph[u] : 0;
}
static void seek(void)                             /* each track's next event, from pos */
{
    const pattern_t *p = &pat[cur];
    if (bk[cur] && !busy[cur]) {                   /* a bank section: its phrase's pass, the next note and controller */
        const qb_style_t *S = bk[cur];
        for (int i = 0; i < ARR_TRACKS; i++) {
            const qy_phrase_t *P = bphrase(i);
            uint32_t pp, n, c;
            if (!P)
                continue;
            pp = pos % P->length;
            tk[i].rep = pos - pp;
            for (n = P->note0; n < (uint32_t)P->note0 + P->nnotes && S->note[n].t < pp; n++)
                ;
            for (c = P->ctl0; c < (uint32_t)P->ctl0 + P->nctl && S->ctl[c].t < pp; c++)
                ;
            tk[i].nx = (uint16_t)n;
            tk[i].cx = (uint16_t)c;
        }
        wake = 1;
        return;
    }
    for (int i = 0; i < ARR_TRACKS; i++) {
        uint32_t n = p->trk[i].first, e = ev_end(&p->trk[i]);
        while (n < e && p->ev[n].tick < pos)
            n++;
        tk[i].nx = (uint16_t)n;
    }
    wake = 1;
}
static void sync(void)                             /* the playing slot reloaded: its places, its sounds */
{
    if (busy[cur] || seen[cur] == gen[cur])
        return;
    seen[cur] = gen[cur];
    ctl_reset();                                   /* another style's phrases: nothing left bent */
    if (pos >= pat_len(&pat[cur]))                 /* shortened past its place: wrapped within the new length, */
        pos %= pat_len(&pat[cur]);                 /* in step with the bars (a jump to 0 would be a bar line) */
    for (int t = 0; t < ARR_TRACKS; t++)           /* a track muted in the pattern: its notes end now too */
        if (pat[cur].trk[t].mute)
            track_off(&tk[t]);
    seek();
    if (arr_playing)
        setup();
}

/* ------------------------------------------------------------ recording --- */
/* A key held while recording is an entry here: its start (quantised, as a pattern tick and as a render tick,
 * arr_ticks counting: the tick at pos is number arr_ticks until it plays), its note over C M7. Released (or
 * the loop wrapped under it), it waits until its start has been played, then goes into the pattern behind the
 * track's next event, so it plays from the next pass on. */
static const uint16_t QUANT_TICKS[ARR_NQ] = {ARR_PPQ / 2, ARR_PPQ / 3, ARR_PPQ / 4, ARR_PPQ / 6, ARR_PPQ / 8, 1};
enum { R_FREE, R_HELD, R_DONE };
typedef struct {
    uint8_t state, key, note, vel, trk, slot;
    uint8_t ident;                                 /* its slot's pattern (ident) at the key */
    uint8_t pend;                                  /* its start a beat line not played yet: its note over the chord
                                                    * there (rec_resolve), not the one the key was played over */
    uint16_t qpos, len;                            /* its start in the pattern; its length (R_DONE) */
    uint32_t qabs;                                 /* its start in render ticks */
} rec_t;
#define REC_N 8
static rec_t rec[REC_N];
#define LIVE_N 16                                  /* keys sounding (arr_key), each with its channel */
static uint8_t live_key[LIVE_N], live_chan[LIVE_N], live_mo[LIVE_N];
static int n_live;
static uint8_t live_trk = 0xFF, live_cur, live_gen; /* the track whose sounds the keys last had sent */
static uint8_t rec_on, rec_trk, quant = ARR_Q_16;
static uint8_t rec_wait;                           /* recording from the next bar line (arr_rec_wait) */
static uint8_t in_tick;                            /* inside tick(): the tick arr_ticks - 1 is playing */
static uint8_t erase_pend[ARR_NSECT], erase_ident[ARR_NSECT];   /* erases waiting for a load to finish */
static uint32_t tick_now(void) { return in_tick ? arr_ticks - 1 : arr_ticks; }

/* the key's time: the tick nearest the block's start (the tick before pos when it is nearer than pos), as a
 * render tick; *kpos its place in the pattern. no_back: never back across the loop's start (to tick 0) */
static uint32_t key_tick(uint32_t *kpos, int no_back)
{
    uint32_t plen = pat_len(&pat[cur]), r = 0;
    if (!first_tick && !start_wait && ftt > 0 && (uint32_t)ftt > pace / 2 && !(no_back && pos == 0))
        r = 1;
    *kpos = (pos + plen - r) % plen;
    return arr_ticks - r;
}
static void ev_insert(int s, int trk, uint32_t tick, uint32_t len, int note, int vel)
{
    pattern_t *p = &pat[s];
    arr_trk_t *t = &p->trk[trk];
    uint32_t ins = t->count ? t->first : p->nev, e = t->count ? ev_end(t) : p->nev;
    if (e > p->nev)
        return;                                    /* not as stored (project.c checks): left alone */
    for (; ins < e && p->ev[ins].tick <= tick; ins++)
        if (p->ev[ins].tick == tick && p->ev[ins].note == note) {   /* the same again: replaced */
            p->ev[ins].len = (uint16_t)len;
            p->ev[ins].vel = (uint8_t)vel;
            return;
        }
    if (p->nev >= ARR_NEV) {
        arr_full = 1;
        return;
    }
    for (uint32_t i = p->nev; i > ins; i--)       /* the ones after it up one (no memmove in the firmware's libc) */
        p->ev[i] = p->ev[i - 1];
    p->ev[ins].tick = (uint16_t)tick;
    p->ev[ins].len = (uint16_t)len;
    p->ev[ins].note = (uint8_t)note;
    p->ev[ins].vel = (uint8_t)vel;
    p->nev++;
    for (int u = 0; u < ARR_TRACKS; u++)           /* the tracks after it move up one */
        if (u != trk && p->trk[u].first >= ins) {
            p->trk[u].first++;
            if (s == cur)
                tk[u].nx++;
        }
    if (!t->count) {
        t->first = (uint16_t)ins;
        if (s == cur)
            tk[trk].nx = (uint16_t)ins;
    }
    t->count++;
    if (s == cur && tick < pos)                    /* played this pass already: from the next */
        tk[trk].nx++;
    if (s == cur)
        wake = 1;
}
/* a note whose start is a beat line ahead of its key (played a little early, quantised forward): stored over the
 * chord on that beat (a pickup onto a chord change belongs to the new chord), once its beat line has set it */
static void rec_resolve(rec_t *r)
{
    if (r->pend && !busy[r->slot]) {
        r->note = (uint8_t)trk_inverse(&pat[r->slot].trk[r->trk], r->key, chord);
        r->pend = 0;
    }
}
static void rec_due(void)                          /* at a beat line, its chord set: the notes starting on it */
{
    for (int i = 0; i < REC_N; i++)
        if (rec[i].state != R_FREE && rec[i].pend && (int32_t)(rec[i].qabs - tick_now()) <= 0)
            rec_resolve(&rec[i]);
}
static void rec_write(rec_t *r)
{
    const pattern_t *p = &pat[r->slot];
    uint32_t plen = pat_len(p), len = r->len, q = QUANT_TICKS[quant];
    if (r->qpos >= plen || r->trk >= ARR_TRACKS)
        return;
    rec_resolve(r);                                /* (a stop before its beat line: the chord now) */
    r->pend = 0;
    if (len < q)
        len = q;
    if (len > plen - r->qpos)                      /* to the pattern's end at most */
        len = plen - r->qpos;
    ev_insert(r->slot, r->trk, r->qpos, len, r->note, r->vel);
    BARRIER();
    arr_dirty[r->slot]++;
}
static void rec_release(rec_t *r, uint32_t end)
{
    uint32_t len = (int32_t)(end - r->qabs) > 0 ? end - r->qabs : 0;   /* (render ticks wrap: compared as
                                                                         * differences) */
    r->len = (uint16_t)(len > 0xFFFFu ? 0xFFFFu : len);
    r->state = R_DONE;
}
/* the entries due: released ones whose start has played, and held ones whose pass has ended (their length to
 * the pattern's end). all: every one now, held ones ending at the tick now (a stop, a section change: the pass
 * is over). Another pattern loaded into the slot since the key: dropped; the slot being stored (busy): later */
static void rec_flush(int all)
{
    for (int i = 0; i < REC_N; i++) {
        rec_t *r = &rec[i];
        if (r->state == R_FREE)
            continue;
        if (ident[r->slot] != r->ident) {
            r->state = R_FREE;
            continue;
        }
        if (busy[r->slot])
            continue;
        if (r->state == R_HELD) {
            uint32_t pass_end = r->qabs - r->qpos + pat_len(&pat[r->slot]);
            if (all)
                rec_release(r, tick_now());
            else if ((int32_t)(arr_ticks - pass_end) >= 0)
                rec_release(r, pass_end);
            else
                continue;
        }
        if (all || (int32_t)(r->qabs - arr_ticks) < 0) {
            rec_write(r);
            r->state = R_FREE;
        }
    }
}
static void rec_press(int key, int vel)
{
    const pattern_t *p = &pat[cur];
    uint32_t kpos, kabs, plen = pat_len(p), q = QUANT_TICKS[quant], m;
    rec_t *r = 0;
    for (int i = 0; i < REC_N && !r; i++)
        if (rec[i].state == R_FREE)
            r = &rec[i];
    if (!r || busy[cur] || bk[cur])
        return;                                    /* more than REC_N held: this one only sounds (a bank section:
                                                    * the app gives the slot a pattern before recording) */
    kabs = key_tick(&kpos, quant == ARR_Q_OFF);   /* grid off: a key at the loop's start is its tick 0 */
    m = kpos % q;
    if (2 * m >= q) {                              /* to the nearest grid line */
        kpos += q - m;
        kabs += q - m;
    } else {
        kpos -= m;
        kabs -= m;
    }
    if (kpos >= plen)                              /* just before the loop's start: its tick 0 */
        kpos -= plen;
    if (rec_wait && (kpos % bar_len(p) != 0 || (int32_t)(kabs - arr_ticks) < 0))
        return;                                    /* waiting for the bar line: only a key landing on it */
    r->key = (uint8_t)key;
    r->vel = (uint8_t)vel;
    r->trk = rec_trk;
    r->slot = cur;
    r->ident = ident[cur];
    r->note = (uint8_t)trk_inverse(&p->trk[rec_trk], key, chord);
    r->pend = (uint8_t)((int32_t)(kabs - arr_ticks) >= 0 && kpos % ARR_PPQ == 0);   /* a beat line ahead */
    r->qpos = (uint16_t)kpos;
    r->qabs = kabs;
    r->len = 0;
    r->state = R_HELD;
}
/* the selected track's sounds (program, volume, pan, sends) for the keys: to the synth, and to MIDI out what
 * it does not have yet, when they were not sent for this track of this pattern */
static void lead_press(int key, int vel);
static void lead_release(int key, uint32_t end);
static void lrec_flush(int all);
static void live_sounds(void)
{
    const arr_trk_t *t = &pat[cur].trk[rec_trk % ARR_TRACKS];
    if (rec_trk >= ARR_TRACKS) {                   /* a lead's: its sounds (sent when they changed) */
        lead_sounds();
        live_trk = rec_trk;
        return;
    }
    if (busy[cur] || (live_trk == rec_trk && live_cur == cur && live_gen == gen[cur]))
        return;
    for (int k = 0; k < NSOUND; k++) {
        uint8_t v;
        uint32_t m = sound(t, k, &v), c = m & 15u;
        gm_midi_now(m & 0xFFu, m >> 8 & 0xFFu, m >> 16 & 0xFFu, at);
        if (mout && out_sent[c][k] != v && out_room(1)) {
            arr_midi_out(m & 0xFFu, m >> 8 & 0xFFu, m >> 16 & 0xFFu);
            out_sent[c][k] = v;
        }
    }
    live_trk = rec_trk;
    live_cur = cur;
    live_gen = gen[cur];
}
/* a lead's channel for the keys: as sent, else the app's (not mid-write), else its default */
static uint32_t lead_chan(int k)
{
    if (ld_snd_ok)
        return ld_snd[k].chan & 15u;
    if (ld && !edit_busy)
        return ld->trk[k].chan & 15u;
    return k ? 7u : 6u;
}
static void key_event(int key, int vel)            /* arr_key, in the render: sound it, record it */
{
    uint32_t chan, kpos;
    int i;
    live_sounds();                                 /* (a lead's: before its channel is taken) */
    chan = rec_trk >= ARR_TRACKS ? lead_chan(rec_trk - ARR_LD1) : pat[cur].trk[rec_trk].chan & 15u;
    for (i = 0; i < n_live && live_key[i] != key; i++)
        ;
    if (i < n_live) {                              /* released, or struck again: its note-off first */
        gm_midi_now(0x80u | live_chan[i], (uint32_t)key, 0, 0);
        if (live_mo[i]) {
            arr_midi_out(0x80u | live_chan[i], (uint32_t)key, 0);
            out_owed--;
        }
        n_live--;
        live_key[i] = live_key[n_live];
        live_chan[i] = live_chan[n_live];
        live_mo[i] = live_mo[n_live];
    }
    for (i = 0; i < REC_N; i++)
        if (rec[i].state == R_HELD && rec[i].key == key)
            rec_release(&rec[i], key_tick(&kpos, 0));
    rec_flush(0);
    lead_release(key, key_tick(&kpos, 0));
    if (!vel)
        return;
    if (n_live < LIVE_N) {                         /* to MIDI out as the pattern's notes: room for its note-off */
        live_key[n_live] = (uint8_t)key;
        live_chan[n_live] = (uint8_t)chan;
        live_mo[n_live] = 0;
        synth_on(chan, (uint32_t)key, (uint32_t)vel);
        if (mout && out_room(1)) {
            arr_midi_out(0x90u | chan, (uint32_t)key, (uint32_t)vel);
            live_mo[n_live] = 1;
            out_owed++;
        }
        n_live++;
    }
    if (rec_on && arr_playing && rec_trk >= ARR_TRACKS) {
        if (leads_heard())
            lead_press(key, vel);
    } else if (rec_on && arr_playing)
        rec_press(key, vel);
}
static void erase_track(int s, int trk)
{
    pattern_t *p = &pat[s];
    arr_trk_t *t = &p->trk[trk];
    uint32_t a = t->first, n = t->count;
    for (int i = 0; i < REC_N; i++)                /* its notes being recorded go too */
        if (rec[i].slot == s && rec[i].trk == trk)
            rec[i].state = R_FREE;
    if (n && a + n <= p->nev) {
        for (uint32_t i = a; i + n < p->nev; i++)
            p->ev[i] = p->ev[i + n];
        p->nev = (uint16_t)(p->nev - n);
        for (int u = 0; u < ARR_TRACKS; u++)
            if (u != trk && p->trk[u].first >= a + n)
                p->trk[u].first = (uint16_t)(p->trk[u].first - n);
            else if (u != trk && p->trk[u].first > a)    /* an empty track's place inside it */
                p->trk[u].first = (uint16_t)a;
    }
    t->count = 0;
    t->first = (uint16_t)(a <= p->nev ? a : p->nev);
    if (s == cur)
        seek();
    arr_full = 0;
    BARRIER();
    arr_dirty[s]++;
}
/* the erases asked for (arr_erase_track), each into the slot that was playing then, once it is not being
 * stored; dropped when another pattern was loaded into it meanwhile */
static void erase_apply(void)
{
    for (int s = 0; s < ARR_NSECT; s++) {
        if (!erase_pend[s] || busy[s])
            continue;
        if (erase_ident[s] == ident[s])
            for (int t = 0; t < ARR_TRACKS; t++)
                if (erase_pend[s] >> t & 1u)
                    erase_track(s, t);
        erase_pend[s] = 0;
    }
}

/* ------------------------------------------------------------ the lead tracks --- */
void arr_leads(const leads_t *l) { ld = l; }
static void lead_as_trk(const lead_trk_t *l, arr_trk_t *t)
{
    memset(t, 0, sizeof *t);
    t->program = l->program & 127u;
    t->chan = l->chan & 15u;
    t->vol = l->vol;
    t->pan = l->pan;
    t->rev = l->rev;
    t->cho = l->cho;
    t->bright = l->bright;
    t->reso = l->reso;
}
/* the leads play now: a song's row (not its Intro or Ending) */
static int leads_heard(void) { return mode == ARR_SONG && rows_live && ld; }
/* the leads' sounds, when they are wanted (SONG, or the keys on a lead) and changed, or after a setup: to the synth
 * now, to MIDI out what it lacks (the rest in later blocks: lead_sounds_out). Not while the app writes them */
static void lead_sounds(void)
{
    if (!ld || edit_busy || (mode != ARR_SONG && rec_trk < ARR_TRACKS))
        return;
    if (ld_snd_ok && !memcmp(ld_snd, ld->trk, sizeof ld_snd))
        return;
    memcpy(ld_snd, ld->trk, sizeof ld_snd);
    ld_snd_ok = 1;
    for (int k = 0; k < ARR_LEADS; k++) {
        arr_trk_t t;
        lead_as_trk(&ld_snd[k], &t);
        for (int i = 0; i < NSOUND; i++) {
            uint8_t v;
            uint32_t m = sound(&t, i, &v);
            gm_midi_now(m & 0xFFu, m >> 8 & 0xFFu, m >> 16 & 0xFFu, at);
        }
    }
    ld_out_pend = 1;
    lead_sounds_out();
}
static void lead_sounds_out(void)
{
    if (!mout || !ld_out_pend || !ld_snd_ok)
        return;
    for (int k = 0; k < ARR_LEADS; k++) {
        arr_trk_t t;
        lead_as_trk(&ld_snd[k], &t);
        for (int i = 0; i < NSOUND; i++) {
            uint8_t v;
            uint32_t m = sound(&t, i, &v), c = m & 15u;
            if (out_sent[c][i] == v)
                continue;
            if (!out_room(1))
                return;                            /* the rest in a later block */
            arr_midi_out(m & 0xFFu, m >> 8 & 0xFFu, m >> 16 & 0xFFu);
            out_sent[c][i] = v;
        }
    }
    ld_out_pend = 0;
}
static uint32_t lead_sounds_need(void)             /* (sounds_fit_first: what a start in SONG sends out besides) */
{
    uint32_t n = 0;
    if (!ld || edit_busy || mode != ARR_SONG || !mout)
        return 0;
    for (int k = 0; k < ARR_LEADS; k++) {
        arr_trk_t t;
        lead_as_trk(&ld->trk[k], &t);
        for (int i = 0; i < NSOUND; i++) {
            uint8_t v;
            uint32_t m = sound(&t, i, &v);
            n += out_sent[m & 15u][i] != v;
        }
    }
    return n;
}
/* the row playing's notes from ld_from on: found again when the row or the list (gen) changed. 0: busy */
static int lead_sync(void)
{
    uint32_t a, b, n;
    if (!ld || edit_busy)
        return 0;
    if (ld_row != row) {
        ld_row = row;
        ld_ok = 0;
    }
    if (ld_ok && ld_gen == ld->gen)
        return 1;
    n = ld->n > LEAD_MAX ? LEAD_MAX : ld->n;
    a = ld->row0[ld_row % ARR_ROWS];
    b = ld->row0[ld_row % ARR_ROWS + 1];
    b = b > n ? n : b;
    a = a > b ? b : a;
    while (a < b) {                                /* the first at or after ld_from */
        uint32_t m = (a + b) / 2;
        if (lead_pos(lead_at_c(ld, m)) < ld_from)
            a = m + 1;
        else
            b = m;
    }
    ld_i = (uint16_t)a;
    b = ld->row0[ld_row % ARR_ROWS + 1];
    ld_end = (uint16_t)(b > n ? n : b);
    ld_gen = ld->gen;
    ld_ok = 1;
    return 1;
}
static void lead_row_start(void)                   /* (row_start: the row `row` from its first downbeat, this tick) */
{
    ld_row = row;
    ld_tick = ld_from = 0;
    ld_ok = 0;
}

/* recording into a lead: a key held is an entry (its row and place, quantised; its start as a render tick); released
 * and its place played (its root then taken), it is handed to the app (arr_lead_take) */
typedef struct {
    uint8_t state, key, note, vel, trk, row, root, pend;   /* pend: its place not played yet (its root not known) */
    uint32_t pos, qabs, len;
} lrec_t;
#define LREC_N 8
static lrec_t lrec[LREC_N];
static uint8_t lrec_n;                             /* entries in use */
#define LOUT_N 32
static arr_lnote_t lout[LOUT_N];
static volatile uint32_t lout_w, lout_r;
volatile uint8_t arr_lead_lost;
int arr_lead_take(arr_lnote_t *n)                  /* the main loop's (one reader) */
{
    uint32_t r = lout_r;
    if (r == lout_w)
        return 0;
    BARRIER();
    *n = lout[r % LOUT_N];
    BARRIER();
    lout_r = r + 1;
    return 1;
}
static void lout_put(const lrec_t *r)
{
    uint32_t w = lout_w;
    arr_lnote_t *n;
    if (w - lout_r >= LOUT_N) {
        arr_lead_lost++;
        return;
    }
    n = &lout[w % LOUT_N];
    n->row = r->row;
    n->trk = r->trk;
    n->note = r->note;
    n->vel = r->vel;
    n->root = r->root;
    n->rsv[0] = n->rsv[1] = n->rsv[2] = 0;
    n->pos = r->pos;
    n->len = r->len < 1u ? 1u : r->len > 0xFFFFu ? 0xFFFFu : r->len;
    BARRIER();
    lout_w = w + 1;
}
static void lrec_release(lrec_t *r, uint32_t end)
{
    uint32_t q = QUANT_TICKS[quant], len = (int32_t)(end - r->qabs) > 0 ? end - r->qabs : 0;
    r->len = len < q ? q : len;
    r->state = R_DONE;
}
/* the entries done and resolved: to the app. all (a stop, recording off): the keys held end now, and an entry whose
 * place never played goes */
static void lrec_flush(int all)
{
    if (!lrec_n)
        return;
    for (int i = 0; i < LREC_N; i++) {
        lrec_t *r = &lrec[i];
        if (r->state == R_FREE)
            continue;
        if (r->state == R_HELD) {
            if (!all)
                continue;
            lrec_release(r, tick_now());
        }
        if (r->pend && !all)
            continue;
        if (!r->pend)
            lout_put(r);
        r->state = R_FREE;
        lrec_n--;
    }
}
/* in a tick, its chord set: the entries whose place is this tick take its root */
static void lrec_due(void)
{
    if (!lrec_n)
        return;
    for (int i = 0; i < LREC_N; i++)
        if (lrec[i].state != R_FREE && lrec[i].pend && (int32_t)(lrec[i].qabs - tick_now()) <= 0) {
            lrec[i].root = (uint8_t)(chord.root % 12u);
            lrec[i].pend = 0;
        }
    lrec_flush(0);
}
/* a row's length in ticks: its beats rounded up to the pattern's bar (the row changes on its bar lines) */
static uint32_t row_ticks(void)
{
    uint32_t bar = bar_len(&pat[cur]), t = (uint32_t)sn.cur_len * ARR_PPQ;
    return (t + bar - 1) / bar * bar;
}
static void lead_press(int key, int vel)           /* a key on a lead, recording, a row playing (a block's start) */
{
    lrec_t *r = 0;
    uint32_t q = QUANT_TICKS[quant], back = 0, kt, kabs, m, len;
    uint8_t rw = row;
    for (int i = 0; i < LREC_N && !r; i++)
        if (lrec[i].state == R_FREE)
            r = &lrec[i];
    if (!r)
        return;                                    /* more than LREC_N held: it only sounds */
    if (!first_tick && !start_wait && ftt > 0 && (uint32_t)ftt > pace / 2 && ld_tick > 0)
        back = 1;                                  /* the tick before is nearer the key than the next one */
    kt = ld_tick - back;
    kabs = arr_ticks - back;
    m = kt % q;
    if (2 * m >= q) {                              /* to the nearest grid line */
        kt += q - m;
        kabs += q - m;
    } else {
        kt -= m;
        kabs -= m;
    }
    len = row_ticks();
    if (kt >= len) {                               /* just before the next row: its tick 0 */
        if (row + 1 < sn.nrows)
            rw = (uint8_t)(row + 1);
        else if (sn.at_end)
            rw = 0;
        else
            return;                                /* (the song ends there) */
        kt -= len;
    }
    r->state = R_HELD;
    r->key = (uint8_t)key;
    r->note = (uint8_t)key;
    r->vel = (uint8_t)vel;
    r->trk = (uint8_t)(rec_trk - ARR_LD1);
    r->row = rw;
    r->pos = kt;
    r->qabs = kabs;
    r->len = 0;
    r->pend = (uint8_t)((int32_t)(kabs - arr_ticks) >= 0);   /* its place ahead: its root there */
    r->root = (uint8_t)(chord.root % 12u);
    lrec_n++;
}
static void lead_release(int key, uint32_t end)
{
    for (int i = 0; i < LREC_N; i++)
        if (lrec[i].state == R_HELD && lrec[i].key == key)
            lrec_release(&lrec[i], end);
    lrec_flush(0);
}
/* the tick playing (its chord set, the patterns' notes started): the row's notes due, moved by the root's move
 * since they were played; play 0 (a clock catch-up's dropped tick): passed over */
static void lead_tick(int play)
{
    uint32_t rt = ld_tick++;
    lrec_due();
    if (!leads_heard() || !lead_sync())
        return;                                    /* (busy: its notes due play at the first tick after) */
    while (ld_i < ld_end) {
        const lead_ev_t *e = lead_at_c(ld, ld_i);
        int t, out;
        if (lead_pos(e) > rt)
            break;
        ld_i++;
        t = lead_trk(e);
        if (!play || tk[ARR_LD1 + t].mute)
            continue;
        out = e->note + lead_shift(chord.root, lead_root(e));
        if (out >= 0 && out <= 127)
            note_start(ARR_LD1 + t, e->note, out, e->vel, ld->trk[t].chan & 15, (int)lead_len(e));
    }
    ld_from = rt + 1;
}
static void lead_skip(void)                        /* the place jumped on (skip): the notes passed over */
{
    ld_from = ld_tick;
    ld_ok = 0;
}
static void lead_init(void)
{
    ld_gen = 0;
    ld_ok = 0;
    ld_row = 0;
    ld_i = ld_end = 0;
    ld_tick = ld_from = 0;
    ld_snd_ok = ld_out_pend = 0;
    memset(lrec, 0, sizeof lrec);
    lrec_n = 0;
    lout_w = lout_r = 0;
    arr_lead_lost = 0;
    arr_row_tick = 0;
}

/* ------------------------------------------------------------ sections --- */
#define SEC_STOP 0xFFu                             /* resolve(): an Ending without notes: stop */
static int is_fill(uint32_t s) { return s == SEC_FILL_AB || s == SEC_FILL_BA; }
static int is_sect(uint32_t s) { return s >= SEC_INTRO && s <= SEC_END; }
/* a section's pattern has no notes (one being loaded counts as having some) */
static int sect_empty(uint32_t s) { return !busy[s - 1] && pat[s - 1].nev == 0; }
/* the section a request plays (SEC_INTRO .. SEC_FILL), from the section playing: SEC_FILL is Fill BA from Main B
 * (back to A), else Fill AB (to B), and during a fill one more bar of it; a fill without notes gives way to the
 * other (still going to the Main asked for), neither: SEC_SAME (no fill, the section plays on). An Intro without
 * notes: Main A; an Ending without notes: SEC_STOP (stop at the bar line). *to: the Main a fill goes to */
static uint8_t resolve(uint32_t s, uint8_t *to)
{
    if (s == SEC_FILL) {
        if (is_fill(arr_sect))
            return arr_sect;
        s = arr_sect == SEC_B ? SEC_FILL_BA : SEC_FILL_AB;
    }
    if (is_fill(s)) {
        *to = s == SEC_FILL_AB ? SEC_B : SEC_A;
        if (sect_empty(s))
            s = s == SEC_FILL_AB ? SEC_FILL_BA : SEC_FILL_AB;
        return sect_empty(s) ? SEC_SAME : (uint8_t)s;
    }
    if (s == SEC_INTRO && sect_empty(s))
        return SEC_A;
    if (s == SEC_END && sect_empty(s))
        return SEC_STOP;
    return is_sect(s) ? (uint8_t)s : SEC_SAME;
}

/* ------------------------------------------------------------ the clock --- */
static int lp_len(const loop_t *l)                 /* its beats (never past ch[], whatever the table holds) */
{
    int n = l->bars * (l->beats ? l->beats : 4);
    return n < 0 ? 0 : n > ARR_LOOP_BEATS ? ARR_LOOP_BEATS : n;
}
static const loop_t *row_loop(const row_t *w) { return &loops[w->loop % ARR_LOOPS]; }
static uint16_t row_len(const row_t *w)            /* a row's beats: its loop's (an empty loop: a bar of 4), rep times */
{
    int n = lp_len(row_loop(w));
    return (uint16_t)((n > 0 ? n : 4) * (w->rep ? w->rep : 1));
}
static void row_mutes(uint32_t m);
static uint32_t row_mute_bits(const row_t *w) { return w->mute | (uint32_t)(w->lmute & 3u) << ARR_LD1; }
/* the song's header and rows `row` and the one after it (row 0 after the last), when the app is not writing
 * (the main loop never runs inside the render: busy means it was interrupted mid-write; the last snapshot stays) */
static void song_snap(void)
{
    uint8_t n, n2;
    if (!sg || edit_busy)
        return;
    sn.nrows = sg->nrows > ARR_ROWS ? ARR_ROWS : sg->nrows;
    sn.intro = sg->intro;
    sn.ending = sg->ending;
    sn.at_end = sg->at_end;
    if (!sn.nrows) {                               /* none (cleared while playing): rows start again at the first */
        if (rows_live)                             /* bar line that finds some; the old row's mutes go */
            row_mutes(0);
        rows_live = 0;
        rows_wait = 1;
        return;
    }
    if (row >= sn.nrows)                           /* rows deleted under it: row 1 */
        row = 0;
    n = (uint8_t)(row + 1 < sn.nrows ? row + 1 : 0);
    n2 = (uint8_t)(n + 1 < sn.nrows ? n + 1 : 0);
    if (rows_live && (sg->row[row].loop != sn.cur.loop || sg->row[row].key != sn.cur.key))
        chord_owed = 1;                            /* the playing row's loop or key changed: heard at the next beat */
    sn.cur = sg->row[row];
    sn.next = sg->row[n];
    sn.next2 = sg->row[n2];
    sn.cur_len = row_len(&sn.cur);
    sn.next_len = row_len(&sn.next);
    sn.next2_len = row_len(&sn.next2);
}
static void row_mutes(uint32_t m)                  /* a row's mutes (a hand mute lasts to here): the tracks', LD1, LD2's */
{
    for (int t = 0; t < ARR_ALLTRK; t++) {
        uint8_t on = (uint8_t)(m >> t & 1u);
        if (on && !tk[t].mute) {
            track_off(&tk[t]);
            if (t < ARR_TRACKS)                    /* (the leads send no controllers) */
                ctl_reset_mask(1u << (pat[cur].trk[t].chan & 15u));
        }
        tk[t].mute = on;
    }
}
/* the row `row` starting, its first downbeat at beat b0: its mutes, its chord owed, a played chord's hold over;
 * returns its section */
static uint8_t row_start(uint32_t b0)
{
    row_b0 = b0;
    rows_live = 1;
    song_end = 0;
    row_mutes(row_mute_bits(&sn.cur));
    lead_row_start();
    chord_owed = 1;
    override = 0;
    return (uint8_t)row_main(&sn.cur);
}
/* the source asked for (mode_next) starts: its beat 0 at beat `origin` (an Intro playing: from its end,
 * switch_to); the chord sounding there owed (a beat with no entry of its own still takes the loop's), a played
 * chord's hold over. A song: its row_from, the rows' clock started by the caller (row_start) */
static void mode_apply(uint32_t origin)
{
    mode = mode_next;
    lp = lp_next;
    mode_wait = 0;
    lp_origin = origin;
    override = 0;
    chord_owed = 1;
    if (mode == ARR_SONG) {
        sg = sg_next;
        row = row_from;
        rows_live = rows_wait = 0;
        sect_owed = song_end = 0;
        song_snap();
    }
}
/* PLAY: the section queued; else a song's Intro (on, from row 1) or its row's section; else (not recording)
 * the Intro, when it has notes; else the Main as it was (after anything else: Main A). A source asked for
 * starts here, its beat 0 PLAY's (an Intro: from its end, switch_to); a song from row_from */
static void transport_start(void)
{
    uint8_t s = arr_next_sect, to = fill_to;
    rec_flush(1);
    arr_ended = 0;
    chord_owed = 0;
    first_tick = 1;
    sec_bars = 0;
    arr_rec_bars = 0;                              /* (recording: its passes from here) */
    beat_now = bar_beat0 = 0;
    if (mode_wait)
        mode_apply(0);
    lp_origin = 0;
    override = 0;
    chord_wait = 0;
    if (mode == ARR_SONG) {
        row = row_from;
        rows_live = 0;
        sect_owed = song_end = 0;
        song_snap();                               /* (never busy: drain holds a start back during a write) */
        rows_wait = !sn.nrows;                     /* none yet: from the first bar line with some */
    }
    if (mode == ARR_SONG && sn.nrows) {
        if (!s && sn.intro && row == 0 && !rec_on && !sect_empty(SEC_INTRO)) {
            s = SEC_INTRO;                         /* the Intro holds row 1's first chord (source_chord) */
            row_mutes(row_mute_bits(&sn.cur));
        } else {
            uint8_t m = row_start(0);              /* the row's section from beat 0 (one queued by hand wins) */
            if (!s)
                s = m;
        }
    } else if (!s && !rec_on && mode == ARR_SONG && sn.intro && !sect_empty(SEC_INTRO))
        s = SEC_INTRO;                             /* (a song with no rows yet: its Intro if on; FREE, LOOP: none) */
    if (s)
        s = resolve(s, &to);
    if (!is_sect(s))                               /* none, no fill, no Ending: as it was (a fill: its Main) */
        s = is_fill(cur + 1u) ? fill_to : cur == SEC_B - 1 ? SEC_B : SEC_A;
    fill_to = to;
    arr_next_sect = SEC_SAME;
    cur = (uint8_t)(s - 1);
    pos = 0;
    ftt = 0;                                       /* tick 0 at once */
    ext_pend = 0;
    clk_div = 0;                                   /* tick 0's F8 */
    for (int t = 0; t < ARR_ALLTRK; t++) {
        track_off(&tk[t]);
        xpose_reset(&tk[t].xs);
        xpose_chord_changed(&tk[t].xs);            /* Bass: the first note is the root */
    }
    arr_sect = (uint8_t)(cur + 1);
    seen[cur] = gen[cur];
    seek();
    setup();
    arr_playing = 1;
}
static void transport_stop(void)
{
    rec_flush(1);                                  /* the notes being recorded end here */
    lrec_flush(1);
    arr_playing = 0;
    arr_ended = 0;
    start_wait = 0;
    ext_pend = 0;                                  /* no tick after a stop */
    all_off();
    ctl_reset();
}
static void stop_ours(void)                        /* a stop of ours (PLAY, an ending, a song's end): FC out */
{
    int started = !start_wait;
    transport_stop();
    if (clk_out && started)
        out_rt(0xFCu);
    clk_out = 0;
}

static void chord_change(chord_t c, int restrike);
static int follows_chord(uint8_t type) { return type == XT_CHORD1 || type == XT_CHORD2 || type == XT_BASS; }
/* a section from its start, now (on a bar line). Notes still sounding from the section before are never
 * re-voiced by the new pattern's track types: they keep their pitch to their end, except that those from a
 * Chord 1 / 2 or Bass track end at the next chord change (with more than RESTRIKE_MIN left), not clash */
static void switch_to(uint8_t sec)
{
    rec_flush(1);                                  /* recording: the pass ends here, and recording too */
    if (rec_trk < ARR_TRACKS) {                    /* (it would go on into the other pattern; a lead's goes on: it */
        rec_on = arr_recording = 0;                /* runs along the song) */
        rec_wait = arr_rec_wait = 0;
    }
    ctl_reset();                                   /* the phrases' bends and swells end with their section */
    if ((arr_sect == SEC_INTRO || arr_sect == SEC_END) && sec != arr_sect)
        chord_owed = 1;                            /* the source's chords held meanwhile: its latest, at once */
    if (arr_sect == SEC_INTRO && sec != arr_sect)
        lp_origin = beat_now;                      /* a loop counts its bars from the Intro's end (its beat 0 now) */
    for (int t = 0; t < ARR_TRACKS; t++)
        for (int i = 0; i < tk[t].nh; i++)
            tk[t].h[i].fixed = (uint8_t)(busy[cur] || !follows_chord(pat[cur].trk[t].x.type) ? 1 : 2);
    cur = (uint8_t)(sec - 1);
    arr_sect = sec;
    sec_bars = 0;
    pos = 0;
    seen[cur] = gen[cur];
    seek();
    setup();
}
/* a song's rows at a bar line: a row starting (its section the mark), the Ending or the end after the last
 * row, a fill in a row's last bar (the mark). fresh: the song switched in at this downbeat; hand: a section
 * queued by hand (during the song's Intro: the rows start with it). 0: the song's end, stopped here; 2: its
 * Ending starts here (it wins over a section by hand, as the stop does) */
static int song_bar(int fresh, int hand, uint8_t *mark)
{
    uint32_t b = bar_len(&pat[cur]) / ARR_PPQ;
    if (!sn.nrows) {                               /* no rows yet: they start when there are some */
        rows_wait = 1;
        return 1;
    }
    if (arr_sect == SEC_END)                       /* (the Ending, the song's or by hand: it plays out) */
        return 1;
    if (rows_wait && arr_sect != SEC_INTRO)        /* rows written while it played: row 1 from this downbeat */
        fresh = 1;
    rows_wait = 0;
    if (fresh)                                     /* switched while playing: row_from from this downbeat */
        *mark = row_start(beat_now);
    else if (!first_tick && !rows_live && arr_sect == SEC_INTRO && (pos == 0 || hand))
        *mark = row_start(beat_now);               /* the Intro played once: row 1 */
    else if (!first_tick && rows_live && beat_now - row_b0 >= sn.cur_len) {
        if (row + 1 < sn.nrows || sn.at_end) {     /* the next row (or row 1 again) */
            row = (uint8_t)(row + 1 < sn.nrows ? row + 1 : 0);
            sn.cur = sn.next;
            sn.cur_len = sn.next_len;
            sn.next = sn.next2;                    /* (kept if the app is writing now) */
            sn.next_len = sn.next2_len;
            song_snap();                           /* and the ones after it */
            *mark = row_start(beat_now);
        } else if (sn.ending && !sect_empty(SEC_END)) {
            rows_live = 0;                         /* the Ending: the last chord held */
            song_end = 1;
            *mark = SEC_END;
            return 2;
        } else {
            stop_ours();
            arr_ended = 1;
            return 0;
        }
    }
    if (sect_owed && *mark == SEC_SAME)
        *mark = sect_owed;
    sect_owed = 0;
    if (rows_live) {                               /* the row's last bar: a fill into a different section, or asked */
        uint32_t left = sn.cur_len - (beat_now - row_b0);
        uint8_t from = *mark ? *mark : arr_sect == SEC_A || arr_sect == SEC_B ? arr_sect : (uint8_t)row_main(&sn.cur);
        uint8_t f = from == SEC_B ? SEC_FILL_BA : SEC_FILL_AB, to;
        int last = row + 1 >= sn.nrows && !sn.at_end;
        if (left > 0 && left <= b && !last && (row_fill(&sn.cur) || row_main(&sn.next) != from) && resolve(f, &to) != SEC_SAME)
            *mark = f;                             /* (no fill with notes: a row starting keeps its section) */
    }
    return 1;
}
/* a bar line: a source asked for starts, the section next, an ending's end. 0: stopped here.
 * The section next: one queued (arr_section) switches, and restarts the section playing unless it is at its
 * start; Ending queued during the ending lets it end. Else a song's mark (song_bar), only when it differs from
 * the section playing. Else a fill's return to its Main after its bar (not under a Fill mark: the fill goes
 * on), the Intro's to Main A after it played once (not under an Intro mark), or the ending's stop after it
 * played once. What is asked for goes through resolve() (SEC_FILL by the Main playing, the fallbacks of
 * sections without notes). */
static int bar_line(void)
{
    uint8_t next = arr_next_sect, mark = SEC_SAME;
    int fresh = 0;
    if (!first_tick)
        sec_bars++;
    if (mode_wait && !(mode_next == ARR_SONG && edit_busy)) {   /* a source asked for: its beat 0 is this
                                                                 * downbeat (a song: its rows not mid-write) */
        mode_apply(beat_now);
        fresh = mode == ARR_SONG;
    }
    if (next == SEC_END && arr_sect == SEC_END)
        next = SEC_SAME;                           /* the ending again: it plays out and stops */
    if (next == SEC_FILL && (arr_sect == SEC_INTRO || arr_sect == SEC_END))
        next = SEC_SAME;                           /* a Fill in the Intro or the Ending: they play out */
    if (mode == ARR_SONG) {
        int r = song_bar(fresh, next != SEC_SAME, &mark);
        if (!r)
            return 0;
        if (r == 2)
            next = SEC_SAME;                       /* the song's Ending: not a section by hand */
    }
    if (next == SEC_SAME && mark != arr_sect)
        next = mark;
    if (next == SEC_SAME && !first_tick) {
        if (is_fill(arr_sect) && sec_bars >= 1 && !is_fill(mark))
            next = fill_to;
        else if (arr_sect == SEC_INTRO && pos == 0 && mark != SEC_INTRO)
            next = SEC_A;                          /* the intro played once */
        else if (arr_sect == SEC_END && pos == 0) {    /* the ending played once */
            stop_ours();
            arr_ended = 1;
            return 0;
        }
    }
    arr_next_sect = SEC_SAME;
    if (next) {
        uint8_t to = fill_to, asked = next;
        next = resolve(next, &to);
        if (next == SEC_STOP) {                    /* an Ending without notes: the end is here */
            stop_ours();
            arr_ended = 1;
            return 0;
        }
        if (next && (next != arr_sect || (pos != 0 && next == asked))) {   /* (a fallback to the section
                                                                              * playing: it plays on) */
            fill_to = to;
            switch_to(next);
        }
    }
    bar_beat0 = beat_now;
    return 1;
}
static chord_t shifted(chord_t c, int by)          /* a stored chord, transposed */
{
    by = ((by % 12) + 12) % 12;
    c.root = (uint8_t)((c.root + by) % 12u);
    if (c.bass < 12u)
        c.bass = (uint8_t)((c.bass + by) % 12u);
    return c;
}
/* the loop's chord on this beat: its own entry (a change: it ends an override), or with `owed` (the loop's
 * first beat, after a held section) the one sounding there. 0: nothing to do */
static int loop_beat(const loop_t *l, uint32_t b, int owed, int by, int play)
{
    int n = lp_len(l);
    uint16_t v;
    if (n <= 0) {
        lbeat = 0;
        return 0;
    }
    b %= (uint32_t)n;
    lbeat = (uint16_t)b;
    v = l->ch[b];
    if (v == LC_SAME && !owed)
        return 0;
    if (v == LC_SAME) {                            /* the latest before it, round from the end */
        for (int k = 1; k < n && v == LC_SAME; k++)
            v = l->ch[((int)b - k + n) % n];
        if (v == LC_SAME)
            return 0;
        if (override)                              /* (owed: a played chord still wins until a real change) */
            return 0;
    }
    override = 0;
    chord_change(shifted(lc_unpack(v), by), play);
    return 1;
}
/* a beat line missed while the table was busy (wait_from .. now) had an entry of its own in loop l (its beat 0
 * at beat `origin`): a change, which ends a played chord's hold (the table is read after the write, not during it) */
static int missed_change(const loop_t *l, uint32_t origin)
{
    int n = lp_len(l);
    uint32_t b = wait_from < origin ? origin : wait_from;
    for (int k = 0; k < n && b + (uint32_t)k <= beat_now; k++)
        if (l->ch[(b + (uint32_t)k - origin) % (uint32_t)n] != LC_SAME)
            return 1;
    return 0;
}
/* the source's chord on a beat line (or owed: the one sounding now); the table busy: at the first block after */
static void source_chord(int play)
{
    if (edit_busy) {
        if (!chord_wait)
            wait_from = beat_now;
        chord_wait = 1;                            /* the first block after the app's write */
        return;
    }
    chord_wait = 0;
    if (mode == ARR_LOOP && arr_sect == SEC_INTRO)  /* the Intro: the loop's beat-0 chord, held */
        loop_beat(&loops[lp], 0, 1, transp, play);
    else if (mode == ARR_LOOP)
        loop_beat(&loops[lp], beat_now - lp_origin, beat_now == lp_origin || chord_owed, transp, play);
    else if (mode == ARR_SONG && sn.nrows && (rows_live || arr_sect == SEC_INTRO))
        /* a row's loop from its first downbeat, in its key; the song's Intro: its first row's first chord */
        loop_beat(row_loop(&sn.cur), rows_live ? beat_now - row_b0 : 0, chord_owed || !rows_live, transp + sn.cur.key, play);
    chord_owed = 0;                                /* (the song's Ending: the last chord held) */
}
/* a beat line: the source's chord; on a tick dropped in a clock-in catch-up (play 0) the chord is set but
 * nothing restruck */
static void beat_chord(int play)
{
    if ((arr_sect == SEC_INTRO || arr_sect == SEC_END) && (pos || sec_bars))
        return;                                    /* the Intro and the Ending: the chord of their first beat, held */
    source_chord(play);
}

/* the notes whose end is due (the tick arr_ticks), and the next end */
static void ends_due(void)
{
    uint32_t m = arr_ticks + FAR;
    for (int t = 0; t < ARR_ALLTRK; t++)
        for (int i = tk[t].nh - 1; i >= 0; i--) {
            uint32_t e = tk[t].h[i].end;
            if ((int32_t)(e - arr_ticks) <= 0)
                note_end(&tk[t], i);
            else if ((int32_t)(e - m) < 0)
                m = e;
        }
    next_end = m;
}
/* the events on the tick pos (the ones before it, passed over: a place moved on by a reload), and the
 * earliest event after it */
/* a bank section's events at pos: each track's phrase, its pass from tk.rep (a new pass: from its start; one
 * entered midway, a reload, a skip: from pos, the ones before passed over), and the next event's place */
static void bank_events(int play)
{
    const pattern_t *p = &pat[cur];
    const qb_style_t *S = bk[cur];
    uint32_t nx = 0xFFFFFFFFu;
    for (int t = 0; t < ARR_TRACKS; t++) {
        const qy_phrase_t *P = bphrase(t);
        const arr_trk_t *tr = &p->trk[t];
        uint32_t L, pp, base, n, c, ne, ce, a;
        int audible = play && !tk[t].mute && !tr->mute;
        if (!P)
            continue;
        L = P->length;
        pp = pos % L;
        base = pos - pp;
        n = tk[t].nx;
        c = tk[t].cx;
        ne = (uint32_t)P->note0 + P->nnotes;
        ce = (uint32_t)P->ctl0 + P->nctl;
        if (base != tk[t].rep || n < P->note0 || n > ne || c < P->ctl0 || c > ce) {   /* a new pass */
            tk[t].rep = base;
            for (n = P->note0; n < ne && S->note[n].t < pp; n++)
                ;
            for (c = P->ctl0; c < ce && S->ctl[c].t < pp; c++)
                ;
        }
        for (; c < ce && S->ctl[c].t <= pp; c++)  /* the controllers first: a bend before its note */
            if (audible && S->ctl[c].kind < QY_NKINDS)
                ctl_send(tr->chan & 15u, S->ctl[c].kind, S->ctl[c].val & 127u);
        for (; n < ne && S->note[n].t <= pp; n++) {
            const qy_note_t *e = &S->note[n];
            int out, vel;
            if (e->t != pp || !audible)
                continue;
            out = xpose(&tr->x, &tk[t].xs, e->key & 127, chord);
            vel = (int)((uint32_t)e->vel * S->trk[t].vscale >> 7);
            if (out >= 0)
                note_start(t, e->key & 127, out, vel < 1 ? 1 : vel > 127 ? 127 : vel, tr->chan & 15, e->gate);
        }
        tk[t].nx = (uint16_t)n;
        tk[t].cx = (uint16_t)c;
        a = base + (n < ne ? S->note[n].t : L);    /* the next note, else the next pass */
        if (c < ce && base + S->ctl[c].t < a)
            a = base + S->ctl[c].t;
        if (a < nx)
            nx = a;
    }
    next_ev = nx;
}
static void events(int play)
{
    const pattern_t *p = &pat[cur];
    uint32_t nx = 0xFFFFFFFFu;
    wake = 0;
    if (busy[cur]) {                               /* being loaded: none now, look again at the next tick */
        wake = 1;
        return;
    }
    if (bk[cur]) {
        bank_events(play);
        return;
    }
    for (int t = 0; t < ARR_TRACKS; t++) {
        const arr_trk_t *tr = &p->trk[t];
        uint32_t e = ev_end(tr), n = tk[t].nx;
        int audible = play && !tk[t].mute && !tr->mute;
        for (; n < e && p->ev[n].tick <= pos; n++) {
            const arr_ev_t *ev = &p->ev[n];
            int out;
            if (ev->tick != pos || !audible)
                continue;
            out = trk_xpose(tr, &tk[t].xs, ev->note, chord);
            if (out >= 0)
                note_start(t, ev->note, out, ev->vel, tr->chan & 15, ev->len);
        }
        tk[t].nx = (uint16_t)n;
        if (n < e && p->ev[n].tick < nx)
            nx = p->ev[n].tick;
    }
    next_ev = nx;
}
/* k ticks with nothing played (clocks in bunched far behind), none of them a beat line (the place stops at the
 * next one at most): the notes that end in them end, the events in them are passed over (wake), the F8 count
 * and the place move on */
static void skip(uint32_t k)
{
    const pattern_t *p = &pat[cur];
    uint32_t bar = bar_len(p), last;
    arr_ticks += k;
    clk_div = (uint8_t)((clk_div + k) % ARR_CLOCK_TICKS);
    if ((int32_t)(next_end - arr_ticks) <= 0)
        ends_due();
    pos += k;
    last = pos - 1u;
    arr_bar = (uint16_t)(last / bar);
    arr_beat_tick = (uint16_t)(last % bar);
    if (pos >= pat_len(p)) {
        pos = 0;
        seek();
    }
    wake = 1;
    rec_flush(0);
    ld_tick += k;                                  /* the leads' notes in the jump: passed over */
    lead_skip();
}
/* one tick; play 0: the position moves on and notes end, nothing new starts (clocks in too bunched). Most
 * ticks have nothing due: no note ends (next_end), no event (next_ev), no beat line, no F8 out */
static void tick(int play)
{
    const pattern_t *p = &pat[cur];
    uint32_t bar;
    TICK_CALLED();
    arr_ticks++;
    in_tick = 1;
    if (clk_out && !arr_ext && clk_div == 0)
        out_rt(0xF8u);                             /* clock out first, 24 a quarter: a lost F8 is never made up */
    if (++clk_div >= ARR_CLOCK_TICKS)
        clk_div = 0;
    if ((int32_t)(next_end - arr_ticks) <= 0)      /* notes ending first */
        ends_due();
    if (pos % ARR_PPQ == 0) {                      /* a beat line (first_tick: pos 0) */
        if (!first_tick)
            beat_now++;
        arr_beats = beat_now;
        if (pos % bar_len(p) == 0) {
            if (!bar_line()) {                     /* sections change on the bar; an end stops here */
                in_tick = 0;
                return;
            }
            if (rec_on && rec_wait)                /* recording from this bar line */
                rec_wait = arr_rec_wait = 0;
            else if (rec_on && !first_tick)
                arr_rec_bars++;
        }
        first_tick = 0;
        p = &pat[cur];
        beat_chord(play);                          /* the beat's chord before its notes */
        rec_due();
    }
    if (wake || pos >= next_ev)
        events(play);
    lead_tick(play);                               /* the leads after the patterns (MAX_ONS for all) */
    bar = bar_len(p);
    arr_bar = (uint16_t)(pos / bar);
    arr_beat_tick = (uint16_t)(pos % bar);        /* ticks into the bar */
    if (++pos >= pat_len(p)) {
        pos = 0;
        seek();
    }
    in_tick = 0;
    rec_flush(0);                                  /* recorded notes whose start has played */
}

/* a new chord: re-voice the Chord 1 / 2 and Bass notes with time left (restrike); the rest end as they were */
static void chord_change(chord_t c, int restrike)
{
    const pattern_t *p = &pat[cur];
    if (c.root == chord.root && c.type == chord.type && c.bass == chord.bass)
        return;
    chord = c;
    arr_chord_now = arr_chord_pack(c);
    for (int t = 0; t < ARR_TRACKS; t++) {
        track_t *k = &tk[t];
        const xtrack_t *x = &p->trk[t].x;
        xpose_chord_changed(&k->xs);
        for (int i = k->nh - 1; i >= 0; i--)       /* the section before's chord notes: they end */
            if (k->h[i].fixed == 2 && (int32_t)(k->h[i].end - arr_ticks) > RESTRIKE_MIN)
                note_end(k, i);
        if (!restrike || busy[cur] || !(x->type == XT_CHORD1 || x->type == XT_CHORD2 || x->type == XT_BASS))
            continue;
        for (int i = k->nh - 1; i >= 0; i--) {
            held_t *h = &k->h[i];
            int out;
            if ((int32_t)(h->end - arr_ticks) <= RESTRIKE_MIN || h->fixed)
                continue;
            out = xpose(x, &k->xs, h->in, c);
            if (out == h->out)
                continue;
            if (out < 0 || ons_left <= 0) {        /* no new pitch, or no more note-ons this block: it ends */
                note_end(k, i);
                continue;
            }
            ons_left--;
            synth_off(h->chan, h->out, h->blk == blk_count);
            off_out(h);
            h->out = (uint8_t)out;
            h->blk = blk_count;
            synth_on(h->chan, h->out, h->vel);
            on_out(h);
        }
    }
}

static void clock_in(int msg)
{
    start_wait = 0;                                /* following a clock: its start, not ours */
    if (msg == ARR_CLK_TICK) {
        arr_ext = 1;
        clk_out = 0;                               /* following a clock: ours is not sent */
        ext_age = 0;
        ext_clocks++;
        if (arr_playing && ext_pend < EXT_PEND_MAX)
            ext_pend += ARR_CLOCK_TICKS;
    } else if (msg == ARR_CLK_START) {
        clk_out = 0;
        transport_start();
        arr_ext = 1;
        ext_age = 0;
    } else if (msg == ARR_CLK_CONTINUE) {
        if (!arr_playing && arr_ended)             /* after an Ending (or a song's end): from the start */
            transport_start();
        else if (!arr_playing) {
            seek();
            setup();
            if (mode_wait) {                       /* a source picked while stopped: its bar 1 is this bar */
                mode_apply(bar_beat0);
                if (mode == ARR_SONG && sn.nrows)  /* a song's row: its section from the next bar line */
                    sect_owed = row_start(bar_beat0);
                    ld_tick = ld_from = pos % bar_len(&pat[cur]);   /* (the row from this bar's downbeat) */
            }
        }
        clk_out = 0;
        arr_playing = 1;
        arr_ext = 1;
        ext_age = 0;
    } else if (msg == ARR_CLK_STOP)
        transport_stop();
}

/* the chord sounding is the source's (playing, no played chord holding, a loop with chords; not mid-write) */
static int src_chords(void)
{
    return arr_playing && !override && !edit_busy &&
           ((mode == ARR_LOOP && lp_len(&loops[lp]) > 0) || (mode == ARR_SONG && sn.nrows && lp_len(row_loop(&sn.cur)) > 0));
}
static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN], a = c & 0xFFFFFFu, b = qb[q_r % QN];
        if (edit_busy && (mode_wait ? mode_next : mode) == ARR_SONG &&
            ((c >> 24 == C_PLAY && a) || (c >> 24 == C_CLOCK && (a == ARR_CLK_START || a == ARR_CLK_CONTINUE))))
            break;                                 /* a song's start during a write: at the first block after (its
                                                    * rows snapshot then), the commands after it with it */
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_PLAY:
            if (a) {
                clk_out = mout && !arr_ext;
                transport_start();
                if (clk_out)                       /* the far side's sounds before its first downbeat */
                    start_wait = 1;                /* (arr_block: then FA, and tick 0, F8 and its notes) */
            } else if (arr_playing)
                stop_ours();
            break;
        case C_TEMPO:                              /* Q16 frames a tick: 5512.5 / bpm (to 1/65536 frame) */
            fpt = FPT_Q16_1BPM / a;
            break;
        case C_CHORD: chord_change(arr_chord_unpack(a), 1); break;
        case C_SECTION:                            /* (a Fill in the Intro or the Ending: ignored) */
            if (a >= SEC_INTRO && a <= SEC_FILL &&
                !(a == SEC_FILL && arr_playing && (arr_sect == SEC_INTRO || arr_sect == SEC_END)) &&
                !(a != SEC_END && arr_playing && mode == ARR_SONG && song_end))   /* (the song's Ending plays out) */
                arr_next_sect = (uint8_t)a;
            break;
        case C_MUTE:
            if (a < ARR_ALLTRK) {
                tk[a].mute = (uint8_t)b;
                if (b) {
                    track_off(&tk[a]);
                    if (a < ARR_TRACKS)
                        ctl_reset_mask(1u << (pat[cur].trk[a].chan & 15u));   /* muted mid-bend: not left bent */
                }
            }
            break;
        case C_MOUT:
            if (a && !mout) {
                memset(out_sent, 0xFF, sizeof out_sent);   /* what the far side has: unknown */
                ld_out_pend = 1;
                mout = 1;
                out_pend = arr_playing;
                if (arr_playing && !arr_ext) {     /* playing on our clock: start the far side now */
                    clk_out = 1;
                    clk_div = 0;                   /* its first F8 at our next tick */
                    out_rt(0xFAu);
                }
            } else if (!a && mout) {
                if (clk_out && !start_wait)
                    out_rt(0xFCu);
                start_wait = 0;
                clk_out = 0;
                mout = 0;                          /* the notes already out still get their note-offs */
            }
            break;
        case C_CLOCK: clock_in((int)a); break;
        case C_MODE:
            if ((a == ARR_LOOP || a == ARR_SONG) && !loops)
                break;
            if (a == ARR_FREE) {                   /* FREE: at once (the chord stays) */
                mode = ARR_FREE;
                mode_wait = 0;
                override = 0;
                break;
            }
            mode_next = (uint8_t)a;                /* from PLAY, Continue, or the next bar line (mode_apply) */
            if (a == ARR_SONG) {
                sg_next = sg_want;                 /* (the latest asked for: posted after it was written) */
                row_from = (uint8_t)(b < ARR_ROWS ? b : 0);
            } else
                lp_next = (uint8_t)b;
            mode_wait = 1;
            break;
        case C_TRANSPOSE: {                        /* the chord sounding moves by the difference now (a played one
                                                    * stays): never looked up again, so a held Intro's moves too */
            int was = transp;
            transp = (int8_t)(a & 0xFFu);
            if (edit_busy)                         /* the table being written: the move at the first block after */
                xp_pend = (int8_t)(xp_pend + transp - was);
            else if (src_chords())
                chord_change(shifted(chord, transp - was), 1);
            break;
        }
        case C_LIVE:                               /* the keys: until the source's next change */
            chord_change(arr_chord_unpack(a), 1);
            override = mode != ARR_FREE;
            break;
        case C_REC:
            if (rec_on && (!b || a != rec_trk))    /* a lead's keys held end their notes: off, or another track */
                lrec_flush(1);
            rec_trk = (uint8_t)a;
            live_sounds();                         /* the track's sounds for the keys, now */
            arr_recording = (uint8_t)(b != 0);
            if (rec_on && !b) {                    /* off: the keys held end their notes now */
                uint32_t kpos;
                for (int i = 0; i < REC_N; i++)
                    if (rec[i].state == R_HELD)
                        rec_release(&rec[i], key_tick(&kpos, 0));
                rec_flush(!arr_playing);
            }
            if (!b || !rec_on)                     /* on again (another track): as it was */
                rec_wait = (uint8_t)(b == ARR_REC_BAR);
            else if (b == 1)
                rec_wait = 0;
            if (b && !rec_on)
                arr_rec_bars = 0;
            if (rec_trk >= ARR_TRACKS)             /* a lead: no bar line to wait for (it records while a row plays) */
                rec_wait = 0;
            arr_rec_wait = rec_wait;
            rec_on = (uint8_t)(b != 0);
            break;
        case C_NOTE: key_event((int)(a & 0x7Fu), (int)(a >> 8 & 0x7Fu)); break;
        case C_QUANT: quant = (uint8_t)a; break;
        case C_ERASE:
            if (erase_pend[cur] && erase_ident[cur] != ident[cur])
                erase_pend[cur] = 0;
            erase_pend[cur] |= (uint8_t)(1u << a);
            erase_ident[cur] = ident[cur];
            erase_apply();
            break;
        default: break;
        }
    }
}

/* ---------------------------------------------------------------- the render's hook --- */
static void block(uint32_t frames)
{
    const int32_t end = (int32_t)(frames << 16);   /* the block's end, Q16 */
    ons_left = MAX_ONS;                            /* chord changes in the commands count too */
    blk_count++;
    arr_late_offs();                               /* the last block's late note-offs (gm_render sends them first) */
    if (mode == ARR_SONG)
        song_snap();                               /* the rows as they are now (not mid-write) */
    drain();
    lead_sounds();                                 /* the leads' sounds when wanted and changed (not mid-write) */
    if (xp_pend && !edit_busy) {                   /* a transpose during a write: the chord sounding moves now */
        if (src_chords())
            chord_change(shifted(chord, xp_pend), 1);
        xp_pend = 0;
    }
    if (chord_wait && !edit_busy && arr_playing) {
        if ((mode == ARR_LOOP && arr_sect == SEC_INTRO && loops[lp].ch[0] != LC_SAME) ||   /* (the Intro plays only
                                                                                            * the loop's beat 0) */
            (mode == ARR_LOOP && arr_sect != SEC_INTRO && missed_change(&loops[lp], lp_origin)) ||
            (mode == ARR_SONG && rows_live && missed_change(row_loop(&sn.cur), row_b0)))
            override = 0;                          /* the change missed ends a played chord's hold */
        chord_owed = 1;                            /* the chord sounding now */
        source_chord(1);
    }
    erase_apply();                                 /* what waited for a store to finish */
    rec_flush(0);
    sync();
    ctl_out_flush();                               /* controller resets MIDI out still owes (playing or not) */
    if (arr_midi_out_link() != out_link) {        /* a new connection: its sounds unknown */
        out_link = arr_midi_out_link();
        memset(out_sent, 0xFF, sizeof out_sent);
        out_pend = mout && arr_playing;
        ld_out_pend = 1;
    }
    if (arr_ext) {
        if (ext_clocks) {                          /* clocks came: their rate, from the frames since the last */
            if (since_ok && ext_since <= EXT_TIMEOUT) {
                uint32_t f = (ext_since << 16) / ext_clocks / ARR_CLOCK_TICKS;
                ext_fpt = ext_fpt ? (3u * ext_fpt + f) / 4u : f;
            }
            since_ok = 1;
            ext_since = 0;
            ext_clocks = 0;
        }
        if (ext_pend > EXT_LAG) {                  /* far behind (clocks bunched): to a clock's ticks at once: */
            uint32_t n = 0;                        /* the first 4 clocks' ticks play, then the place jumps from */
            at = 0;                                /* beat line to beat line (each a silent tick: its chord set) */
            while (ext_pend > ARR_CLOCK_TICKS && arr_playing) {
                uint32_t k = ext_pend - ARR_CLOCK_TICKS, room = ARR_PPQ - pos % ARR_PPQ;
                if (n >= MAX_CATCHUP * ARR_CLOCK_TICKS && pos % ARR_PPQ && !first_tick) {
                    k = k < room ? k : room;
                    skip(k);
                    ext_pend -= k;
                    continue;
                }
                ext_pend--;
                tick(n++ < MAX_CATCHUP * ARR_CLOCK_TICKS);
            }
            ftt = 0;
        }
        pace = ext_fpt ? ext_fpt : fpt;            /* a clock's ticks in a clock's time; more owed: faster */
        if (ext_pend > ARR_CLOCK_TICKS)
            pace = pace / ext_pend * ARR_CLOCK_TICKS;
        while (ftt < end && ext_pend && arr_playing) {
            int32_t f = ftt < 0 ? 0 : ftt;
            at = (uint32_t)(f + (1 << 16)) >> 17;
            if (at >= frames / 2)
                at = frames / 2 - 1;
            ext_pend--;
            tick(1);
            ftt += (int32_t)pace;
        }
        if (ftt < end)                             /* the ticks allowed all played: the next when a clock */
            ftt = end;                             /* comes, at that block's start */
        at = 0;
        ftt -= end;
        sounds_out();
        ext_since += frames;
        ext_age += frames;
        if (ext_age > EXT_TIMEOUT) {               /* the clock is gone: ours again, a tick from now; */
            arr_ext = 0;                           /* no clock out until our own next start */
            ftt = (int32_t)fpt;
            pace = fpt;
            ext_fpt = 0;
            since_ok = 0;
            ext_pend = 0;
        }
        return;
    }
    if (!arr_playing)
        return;
    if (start_wait) {                              /* our start with MIDI out: the sounds first; the start */
        sounds_out();                              /* (FA, tick 0) when tick 0's notes fit after them, a few */
        if ((out_pend || !sounds_fit_first()) && start_wait <= START_WAIT) {   /* blocks later at most */
            start_wait++;
            return;                                /* the synth waits too: everything starts together */
        }
        start_wait = 0;
        out_rt(0xFAu);
    }
    pace = fpt;
    while (ftt < end && arr_playing) {             /* the ticks due within this block, each note-on on its */
        int32_t f = ftt < 0 ? 0 : ftt;             /* sample: output frames / 2, rounded (EAS's 22.05 kHz) */
        at = (uint32_t)(f + (1 << 16)) >> 17;
        if (at >= frames / 2)
            at = frames / 2 - 1;
        tick(1);
        ftt += (int32_t)fpt;
    }
    at = 0;                                        /* the rest (commands, MIDI clock in): the block's start */
    ftt -= end;
    sounds_out();
}
void arr_block(uint32_t frames)
{
    block(frames);
    arr_mode = mode_wait ? mode_next : mode;       /* for the screen: together, after the block's ticks */
    arr_loop_now = mode == ARR_LOOP ? (int8_t)lp : mode == ARR_SONG && sn.nrows && !rows_wait ? (int8_t)(sn.cur.loop % ARR_LOOPS) : -1;
    arr_loop_beat = lbeat;
    arr_override = override;
    arr_row = mode_wait && mode_next == ARR_SONG ? row_from : row;   /* (a switch waiting: its row) */
    {
        uint32_t per = sn.cur_len / (sn.cur.rep ? sn.cur.rep : 1), k = 0;
        if (mode == ARR_SONG && rows_live && per)
            k = (beat_now - row_b0) / per;
        arr_rep = (uint8_t)(k < sn.cur.rep ? k : sn.cur.rep ? sn.cur.rep - 1u : 0u);
    }
    {
        uint16_t m = 0;
        for (int t = 0; t < ARR_ALLTRK; t++)
            m |= (uint16_t)(tk[t].mute << t);
        arr_mutes = m;
    }
    arr_row_tick = mode == ARR_SONG && rows_live ? ld_tick : 0;
}

void arr_init(void)                                /* at boot, before the render runs */
{
    q_w = q_r = 0;
    memset(tk, 0, sizeof tk);
    for (int i = 0; i < ARR_NSECT; i++) {
        memset(&pat[i], 0, sizeof pat[i]);
        pat[i].bars = 1;
        pat[i].beats = 4;
        pat[i].format = ARR_PAT_FORMAT;
        busy[i] = 0;
        gen[i] = seen[i] = 0;
        ident[i] = 0;
        bk[i] = 0;
    }
    for (int t = 0; t < ARR_ALLTRK; t++) {
        xpose_reset(&tk[t].xs);
        arr_track_hit[t] = 0;
    }
    lead_init();
    ctl_dirty = ctl_out_dirty = ctl_out_pend = 0;
    chord_owed = 0;
    chord.root = 0;
    chord.type = CH_MAJ7;                          /* patterns are written over C M7: as written */
    chord.bass = CH_NOBASS;
    arr_chord_now = arr_chord_pack(chord);
    cur = SEC_A - 1;
    pos = 0;
    mout = clk_out = 0;
    out_owed = 0;
    memset(out_sent, 0xFF, sizeof out_sent);
    out_pend = 0;
    out_link = arr_midi_out_link();
    ftt = 0;
    at = 0;
    n_late = 0;
    fpt = pace = FPT_Q16_1BPM / 120u;
    ext_pend = ext_age = ext_since = ext_clocks = ext_fpt = 0;
    since_ok = 0;
    next_end = FAR;
    next_ev = 0;
    wake = 1;
    clk_div = 0;
    arr_playing = 0;
    arr_ext = 0;
    arr_sect = SEC_A;
    arr_next_sect = SEC_SAME;
    arr_bar = arr_beat_tick = 0;
    arr_ticks = 0;
    memset(keys_prev, 0, sizeof keys_prev);
    memset(rec, 0, sizeof rec);
    n_live = 0;
    rec_on = rec_trk = rec_wait = 0;
    arr_recording = arr_rec_wait = 0;
    arr_rec_bars = 0;
    live_trk = 0xFF;
    in_tick = 0;
    memset(erase_pend, 0, sizeof erase_pend);
    quant = ARR_Q_16;
    arr_full = 0;
    for (int i = 0; i < ARR_NSECT; i++)
        arr_dirty[i] = 0;
    mode = mode_next = ARR_FREE;                   /* (loops kept: arr_loops is called once by the app) */
    lp = lp_next = 0;
    mode_wait = 0;
    transp = 0;
    override = 0;
    chord_wait = 0;
    lp_origin = wait_from = 0;
    lbeat = 0;
    arr_mode = ARR_FREE;
    arr_loop_now = -1;
    arr_loop_beat = 0;
    arr_override = 0;
    arr_beats = 0;
    sg = sg_next = 0;
    sg_want = 0;
    row_from = rows_live = rows_wait = row = sect_owed = song_end = 0;
    xp_pend = 0;
    row_b0 = 0;
    memset(&sn, 0, sizeof sn);
    arr_row = arr_rep = arr_mutes = 0;
    first_tick = 0;
    fill_to = SEC_B;
    sec_bars = 0;
    beat_now = bar_beat0 = 0;
}

/* ------------------------------------------------------------ factory styles --- */
_Static_assert(STY_NSECT == ARR_NSECT, "styles.h: a pattern for each section");
_Static_assert(STY_COUNT <= ARR_MAX_FACTORY, "the factory styles' pattern numbers end below the bank's");
int arr_styles(void) { return STY_COUNT; }
const char *arr_style_name(int s) { return s >= 0 && s < STY_COUNT ? STY_NAME[s] : ""; }
int arr_style_tempo(int s) { return s >= 0 && s < STY_COUNT ? STY_TEMPO[s] : 120; }

int arr_factory_pattern(int n, pattern_t *p)
{
    const sty_pat_t *f;
    uint32_t e;
    int t, i;
    n -= ARR_FACTORY0;
    if (n < 0 || n >= STY_COUNT * ARR_NSECT)
        return -1;
    f = &STY_PAT[n];
    memset(p, 0, sizeof *p);
    p->format = ARR_PAT_FORMAT;
    for (i = 0; i < 8 && STY_NAME[n / ARR_NSECT][i]; i++)
        p->name[i] = STY_NAME[n / ARR_NSECT][i];
    p->bars = f->bars;
    p->beats = f->beats;
    e = f->first;
    for (t = 0; t < ARR_TRACKS; t++) {
        const sty_trk_t *s = &f->trk[t];
        arr_trk_t *k = &p->trk[t];
        k->program = s->program;
        k->chan = s->chan;
        k->x.type = s->type;
        k->x.lo = s->lo;
        k->x.hi = s->hi;
        k->x.hikey = s->hikey;
        k->vol = s->vol;
        k->pan = s->pan;
        k->rev = s->rev;
        k->cho = s->cho;
        k->mute = s->mute;
        k->first = p->nev;
        k->count = s->count;
        for (i = 0; i < s->count && p->nev < ARR_NEV; i++, e++) {
            arr_ev_t *v = &p->ev[p->nev++];
            v->tick = (uint16_t)(STY_EV[e] & 0x7FFFu);
            v->note = (uint8_t)(STY_EV[e] >> 15 & 0x7Fu);
            v->len = (uint16_t)sty_len(STY_EV[e] >> 22 | (uint32_t)(STY_VEL[e] >> 7) << 10);
            v->vel = (uint8_t)(STY_VEL[e] & 0x7Fu);
        }
        k->count = (uint16_t)(p->nev - k->first);
    }
    return 0;
}

/* ------------------------------------------------------------ bank styles --- */
int arr_bank_pattern(const qb_style_t *S, int k, pattern_t *p)
{
    static uint16_t hist[128];                     /* (static: the render's stack is small, and so is the loop's) */
    uint32_t slen, keep = 0, edge = 128, extra = 0, total = 0;
    int t, i, dropped = 0;
    memset(p, 0, sizeof *p);
    p->format = ARR_PAT_FORMAT;
    if (k < 0 || k >= ARR_NSECT)
        k = SEC_A - 1;
    for (i = 0; i < 8 && S->name[i]; i++)
        p->name[i] = S->name[i];
    p->bars = S->bars[k] < 1 ? 1 : S->bars[k] > ARR_STYLE_MAXBARS ? ARR_STYLE_MAXBARS : S->bars[k];
    p->beats = S->beats < 2 ? 4 : S->beats > 7 ? 7 : S->beats;
    slen = (uint32_t)p->bars * p->beats * ARR_PPQ;
    memset(hist, 0, sizeof hist);
    for (t = 0; t < ARR_TRACKS; t++) {             /* the velocities, as played: the quietest go past ARR_NEV */
        uint32_t u = S->use[k][t];
        const qy_phrase_t *P = u < S->nph ? &S->ph[u] : 0;
        if (!P || !P->length)
            continue;
        for (uint32_t base = 0; base < slen; base += P->length)
            for (uint32_t j = 0; j < P->nnotes && base + S->note[P->note0 + j].t < slen; j++) {
                uint32_t v = (uint32_t)S->note[P->note0 + j].vel * S->trk[t].vscale >> 7;
                hist[v > 127 ? 127 : v < 1 ? 1 : v]++;
                total++;
            }
    }
    if (total > ARR_NEV) {                         /* the loudest ARR_NEV: all from velocity edge up, `extra` at edge - 1 */
        for (edge = 128; edge > 1 && keep + hist[edge - 1] <= ARR_NEV; edge--)
            keep += hist[edge - 1];
        extra = ARR_NEV - keep;
    } else
        edge = 0;
    for (t = 0; t < ARR_TRACKS; t++) {
        arr_trk_t *r = &p->trk[t];
        const qb_trk_t *q = &S->trk[t];
        uint32_t u = S->use[k][t];
        const qy_phrase_t *P = u < S->nph ? &S->ph[u] : 0;
        r->chan = q->chan & 15u;
        r->program = q->drum ? 0 : S->prog[k][t] & 127u;
        r->vol = q->vol;
        r->pan = q->pan;
        r->rev = q->rev;
        r->cho = q->cho;
        r->x.type = P && P->typ < NXT ? P->typ : XT_BYPASS;
        r->x.lo = P ? P->lo : 0;
        r->x.hi = P ? P->hi : 127;
        r->x.hikey = P && P->hikey <= XT_NOKEY ? P->hikey : XT_NOKEY;
        r->first = p->nev;
        if (!P || !P->length)
            continue;
        for (uint32_t base = 0; base < slen; base += P->length)
            for (uint32_t j = 0; j < P->nnotes; j++) {
                const qy_note_t *e = &S->note[P->note0 + j];
                uint32_t tick = base + e->t, v = (uint32_t)e->vel * q->vscale >> 7, len = e->gate;
                if (tick >= slen)
                    break;
                v = v > 127 ? 127 : v < 1 ? 1 : v;
                if (v < edge && !(v == edge - 1 && extra)) {
                    dropped++;
                    continue;
                }
                if (v < edge)
                    extra--;
                if (p->nev >= ARR_NEV) {
                    dropped++;
                    continue;
                }
                p->ev[p->nev].tick = (uint16_t)tick;
                p->ev[p->nev].len = (uint16_t)(len < 1 ? 1 : len > slen ? slen : len);
                p->ev[p->nev].note = e->key & 127u;
                p->ev[p->nev].vel = (uint8_t)v;
                p->nev++;
            }
        r->count = (uint16_t)(p->nev - r->first);
    }
    return dropped;
}
