/* SPDX-License-Identifier: GPL-3.0-only */
/* The arranger engine (arr.h): a pattern's 8 tracks through the transposer over the chord now, on the
 * clock (ours, or MIDI clock in), into the GM synth and MIDI out. Threads and commands as Life's (life.c):
 * the main loop posts, the render drains and runs the sequencer, called by gm_render between EAS blocks.
 * Everything here is in statics: the render runs on the 8 KiB interrupt stack. */
#include <string.h>
#include "arr.h"
#include "gm.h"
#include "templates.h"

#define BARRIER() __asm__ volatile("" ::: "memory")
#define ARR_HELD 8                                 /* notes held a track */
#define RESTRIKE_MIN 12                            /* at a chord change, notes with more ticks left restrike */
#define EXT_TIMEOUT 26460u                         /* frames without a clock in (0.6 s): back to our own */
#define FRAMES_PER_TICK_1BPM 110250u               /* 44100 * 60 / 24 */
#define MAX_ONS 48                                 /* note-ons a block, at most: the interrupt's work bounded */
#define MAX_CATCHUP 4                              /* bunched clocks in played a block; the rest only move on */
#define START_WAIT 8                               /* blocks a start may wait for its sounds to go out */
#define OUT_RESERVE 8u                             /* MIDI out slots kept free beyond the note-offs owed */

volatile uint8_t arr_playing, arr_sect = SEC_A, arr_next_sect, arr_ext;
volatile uint16_t arr_bar, arr_beat_tick;
volatile uint8_t arr_song_bar;
volatile int8_t arr_template_now = -1;
volatile uint32_t arr_chord_now;
volatile uint32_t arr_track_hit[ARR_TRACKS];
volatile uint32_t arr_ticks;

/* ------------------------------------------------------------ commands --- */
enum { C_PLAY, C_TEMPO, C_CHORD, C_SECTION, C_MUTE, C_MOUT, C_CLOCK, C_SONG, C_TEMPLATE, C_KEY, C_LIVE };
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

/* the four slots (Main A, Main B, Fill, Ending). The main loop copies a pattern in with its slot marked
 * busy, then counts the slot's generation up before it releases it; the render, which interrupts the main
 * loop and never the other way round, leaves a busy slot alone and, seeing a new generation, finds its
 * tracks' places in it before it plays anything (sync). */
static pattern_t pat[ARR_NSECT];
static volatile uint8_t busy[ARR_NSECT], gen[ARR_NSECT];
static uint8_t seen[ARR_NSECT];
/* the song, copied in the same way: the render leaves it alone while it is busy */
static song_t song;
static volatile uint8_t song_busy;

void arr_play(int on) { post(C_PLAY, on != 0, 0); }
void arr_set_tempo(int bpm) { post(C_TEMPO, (uint32_t)(bpm < 40 ? 40 : bpm > 240 ? 240 : bpm), 0); }
void arr_set_chord(chord_t c) { post(C_CHORD, arr_chord_pack(c), 0); }
void arr_section(int sec) { post(C_SECTION, (uint32_t)sec, 0); }
void arr_track_mute(int trk, int on) { post(C_MUTE, (uint32_t)trk, on != 0); }
void arr_midi_out_on(int on) { post(C_MOUT, on != 0, 0); }
void arr_clock(int msg) { post(C_CLOCK, (uint32_t)msg, 0); }
void arr_song(const song_t *s, int loop)
{
    if (!s) {
        post(C_SONG, 0, 0);
        return;
    }
    song_busy = 1;
    BARRIER();
    memcpy(&song, s, sizeof song);
    BARRIER();
    song_busy = 0;
    post(C_SONG, 1, loop != 0);
}
void arr_template(int i) { post(C_TEMPLATE, i >= 0 && i < NTEMPLATES ? (uint32_t)i + 1 : 0, 0); }
void arr_set_key(int key) { post(C_KEY, (uint32_t)(((key % 12) + 12) % 12), 0); }
int arr_keys_held(const uint8_t *notes, int n)    /* in the caller's thread: chords.c has no state */
{
    chord_t c;
    if (!notes || n < 3 || !chord_recognize(notes, n, &c))
        return 0;
    post(C_LIVE, arr_chord_pack(c), 0);
    return 1;
}
void arr_load_pattern(int slot, const pattern_t *p)
{
    if (slot < 0 || slot >= ARR_NSECT)
        return;
    busy[slot] = 1;
    BARRIER();
    memcpy(&pat[slot], p, sizeof *p);
    gen[slot]++;
    BARRIER();
    busy[slot] = 0;
}

/* ------------------------------------------------------------ the state --- */
typedef struct {
    uint8_t out, in, vel, chan;
    uint8_t mo;                                    /* its note-on went to MIDI out: its note-off goes there too */
    uint16_t left;                                 /* ticks to its end */
} held_t;
typedef struct {
    xstate_t xs;
    uint16_t nx;                                   /* the next event (index into ev) */
    uint8_t nh, mute;
    held_t h[ARR_HELD];
} track_t;
static track_t tk[ARR_TRACKS];
static uint8_t cur;                                /* the slot playing */
static uint32_t pos;                               /* ticks into the pattern */
static chord_t chord;
static uint8_t mout;                               /* MIDI out on */
static uint8_t clk_out;                            /* we send the clock: since our own start with MIDI out on */
static uint32_t out_owed;                          /* note-offs owed to MIDI out */
static uint8_t out_sent[16][5];                    /* each channel's sounds as last sent out; 0xFF unknown */
static uint8_t out_pend;                           /* the playing pattern's sounds still to send out */
static uint32_t out_link;                          /* the connection they were sent on */
static int ons_left;                               /* note-ons this block may still start */
static uint32_t at;                                /* the tick playing: its sample within the EAS block (gm_midi_now) */
#define LATE_OFFS 64                               /* note-offs held for the next block's start, at most */
static uint16_t late_off[LATE_OFFS];               /* chan << 8 | note */
static int n_late;
static uint8_t start_wait;                         /* a start waiting for its sounds to go out: blocks waited + 1 */

/* sections, songs, templates */
static uint8_t first_tick;                         /* the first tick since a start: no bar or beat behind it */
static uint8_t fill_from = SEC_A;                  /* the Main a fill came from: it goes to the other */
static uint16_t sec_bars;                          /* bars played of the section playing */
static uint8_t song_on, song_loop, song_restart;   /* restart: set while playing, from bar 1 at the next bar line */
static uint8_t sbar;                               /* the song's bar */
static int8_t tmpl = -1;                           /* the template playing, -1 none */
static uint8_t key;                                /* the templates' key, semitones over C */
static uint32_t beat_now, bar_beat0, t_origin;     /* beats since the start: now, at the bar's downbeat, and
                                                    * where the template's first bar fell */

/* our clock: Q16 output frames to the next tick, and a tick's length */
static int32_t ftt;
static uint32_t fpt;
static uint32_t ext_pend, ext_age;

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
 * a release is +-2.9 ms from its time and a note's length right on average. A note-on of the same note before
 * then sends it first (never after the note-on: it would end the new note). */
static void synth_off(uint32_t chan, uint32_t note)
{
    if (at >= GM_BLOCK / 2 && n_late < LATE_OFFS)
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
    synth_off(h->chan, h->out);
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
    h->left = (uint16_t)(len ? len : 1);
    synth_on((uint32_t)chan, (uint32_t)out, h->vel);
    on_out(h);
    arr_track_hit[ti] = arr_ticks;
}
static void all_off(void)
{
    for (int t = 0; t < ARR_TRACKS; t++)
        track_off(&tk[t]);
}

/* the playing pattern's sounds: program, volume, pan, sends. To the synth whenever a pattern starts (setup);
 * to MIDI out after the block's notes, only what changed since it was last sent, the rest in later blocks
 * (sounds_out). sound(): track t's message k (0 the program, 1..4 the CCs) as st | d1 << 8 | d2 << 16 */
static const uint8_t SOUND_CC[5] = {0, 7, 10, 91, 93};
static uint32_t sound(const arr_trk_t *t, int k, uint8_t *v)
{
    uint32_t c = t->chan & 15u;
    *v = k == 0 ? t->program : k == 1 ? t->vol : k == 2 ? t->pan : k == 3 ? t->rev : t->cho;
    return k ? (0xB0u | c) | (uint32_t)SOUND_CC[k] << 8 | (uint32_t)*v << 16 : (0xC0u | c) | (uint32_t)*v << 8;
}
static void setup(void)
{
    const pattern_t *p = &pat[cur];
    if (busy[cur])
        return;
    for (int i = 0; i < ARR_TRACKS; i++)
        for (int k = 0; k < 5; k++) {
            uint8_t v;
            uint32_t m = sound(&p->trk[i], k, &v);
            gm_midi_now(m & 0xFFu, m >> 8 & 0xFFu, m >> 16 & 0xFFu, at);
        }
    out_pend = mout;
}
static void sounds_out(void)
{
    const pattern_t *p = &pat[cur];
    if (!out_pend || !mout || busy[cur])
        return;
    for (int i = 0; i < ARR_TRACKS; i++)
        for (int k = 0; k < 5; k++) {
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
    uint32_t need = 2;                             /* FA and tick 0's F8 */
    if (busy[cur])
        return 0;
    for (int i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *t = &p->trk[i];
        uint32_t e = ev_end(t);
        for (int k = 0; k < 5; k++) {
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
static void seek(void)                             /* each track's next event, from pos */
{
    const pattern_t *p = &pat[cur];
    for (int i = 0; i < ARR_TRACKS; i++) {
        uint32_t n = p->trk[i].first, e = ev_end(&p->trk[i]);
        while (n < e && p->ev[n].tick < pos)
            n++;
        tk[i].nx = (uint16_t)n;
    }
}
static void sync(void)                             /* the playing slot reloaded: its places, its sounds */
{
    if (busy[cur] || seen[cur] == gen[cur])
        return;
    seen[cur] = gen[cur];
    if (pos >= pat_len(&pat[cur]))
        pos = 0;
    seek();
    if (arr_playing)
        setup();
}

/* ------------------------------------------------------------ the clock --- */
static void transport_start(void)
{
    uint8_t s = arr_next_sect;                     /* the section queued; a song's first; else the Main */
    if (s < SEC_A || s > SEC_END) {                /* playing (after a fill or an ending: Main A) */
        s = cur == SEC_B - 1 ? SEC_B : SEC_A;
        if (song_on && !song_busy && song.sect[0] >= SEC_A && song.sect[0] <= SEC_END)
            s = song.sect[0];
    }
    if (s == SEC_FILL)
        fill_from = arr_sect == SEC_B ? SEC_B : SEC_A;
    arr_next_sect = SEC_SAME;
    cur = (uint8_t)(s - 1);
    first_tick = 1;
    sec_bars = 0;
    sbar = 0;
    song_restart = 0;
    arr_song_bar = 0;
    beat_now = bar_beat0 = t_origin = 0;
    pos = 0;
    ftt = 0;                                       /* tick 0 at once */
    ext_pend = 0;
    for (int t = 0; t < ARR_TRACKS; t++) {
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
    arr_playing = 0;
    start_wait = 0;
    ext_pend = 0;                                  /* no tick after a stop */
    all_off();
}
static void stop_ours(void)                        /* a stop of ours (PLAY, an ending, a song's end): FC out */
{
    int started = !start_wait;
    transport_stop();
    if (clk_out && started)
        out_rt(0xFCu);
    clk_out = 0;
}

static void chord_change(chord_t c);
static void switch_to(uint8_t sec)                 /* a section from its start, now (on a bar line) */
{
    if (sec == SEC_FILL && (arr_sect == SEC_A || arr_sect == SEC_B))
        fill_from = arr_sect;
    cur = (uint8_t)(sec - 1);
    arr_sect = sec;
    sec_bars = 0;
    pos = 0;
    seen[cur] = gen[cur];
    seek();
    setup();
}
/* a bar line: the song's bar, the section next (queued, the song's mark, a fill's return), a song's or an
 * ending's end. 0: stopped here */
static int bar_line(void)
{
    uint8_t next = arr_next_sect;
    if (!first_tick)
        sec_bars++;
    if (song_on) {
        if (song_restart)
            sbar = 0;
        else if (!first_tick)
            sbar++;
        if (!song_busy) {
            uint8_t n = song.bars < 1 ? 1 : song.bars > ARR_SONGBARS ? ARR_SONGBARS : song.bars;
            if (sbar >= n) {
                if (!song_loop) {
                    stop_ours();
                    return 0;
                }
                sbar = 0;
            }
            if (next == SEC_SAME && (!first_tick || song_restart) && song.sect[sbar] >= SEC_A && song.sect[sbar] <= SEC_END)
                next = song.sect[sbar];
        }
        song_restart = 0;
        arr_song_bar = sbar;
    }
    if (next == SEC_SAME && !first_tick) {
        if (arr_sect == SEC_FILL && sec_bars >= 1)
            next = fill_from == SEC_B ? SEC_A : SEC_B;
        else if (arr_sect == SEC_END && pos == 0) {    /* the ending played once */
            stop_ours();
            return 0;
        }
    }
    arr_next_sect = SEC_SAME;
    if (next >= SEC_A && next <= SEC_END && (next != arr_sect || pos != 0))
        switch_to(next);                           /* the same section at its own start: it plays on */
    bar_beat0 = beat_now;
    return 1;
}
static chord_t tmpl_chord(void)                    /* the template's chord on the beat now, in the key */
{
    const template_t *m = &TEMPLATES[tmpl];
    chord_t c = m->bars[(beat_now - t_origin) / m->beats % m->nbars];
    c.root = (uint8_t)((c.root + key) % 12u);
    return c;
}
static void beat_chord(void)                       /* a beat line: the song's chord, or the template's */
{
    uint32_t beat = pos % bar_len(&pat[cur]) / ARR_PPQ;
    if (song_on) {
        if (!song_busy && !song_restart && beat < 4) {
            chord_t c = song.chord[sbar * 4u + beat];
            if (c.type < CH_NTYPES && c.root < 12)
                chord_change(c);
        }
    } else if (tmpl >= 0)
        chord_change(tmpl_chord());
}

/* one tick; play 0: the position moves on and notes end, nothing new starts (clocks in too bunched) */
static void tick(int play)
{
    const pattern_t *p = &pat[cur];
    uint32_t len, bar;
    arr_ticks++;
    if (clk_out && !arr_ext)
        out_rt(0xF8u);                             /* clock out first, 24 a quarter: a lost F8 is never made up */
    for (int t = 0; t < ARR_TRACKS; t++)           /* notes ending first */
        for (int i = tk[t].nh - 1; i >= 0; i--)
            if (--tk[t].h[i].left == 0)
                note_end(&tk[t], i);
    if (pos % ARR_PPQ == 0 && !first_tick)
        beat_now++;
    if (pos % bar_len(p) == 0 && !bar_line())      /* sections change on the bar; an end stops here */
        return;
    first_tick = 0;
    p = &pat[cur];
    if (pos % ARR_PPQ == 0)                        /* the beat's chord before its notes */
        beat_chord();
    if (!busy[cur])
        for (int t = 0; t < ARR_TRACKS; t++) {
            const arr_trk_t *tr = &p->trk[t];
            uint32_t e = ev_end(tr), n = tk[t].nx;
            int audible = play && !tk[t].mute && !tr->mute;
            for (; n < e && p->ev[n].tick <= pos; n++) {
                const arr_ev_t *ev = &p->ev[n];
                int out;
                if (ev->tick != pos || !audible)
                    continue;
                out = xpose(&tr->x, &tk[t].xs, ev->note, chord);
                if (out >= 0)
                    note_start(t, ev->note, out, ev->vel, tr->chan & 15, ev->len);
            }
            tk[t].nx = (uint16_t)n;
        }
    len = pat_len(p);
    bar = bar_len(p);
    arr_bar = (uint16_t)(pos / bar);
    arr_beat_tick = (uint16_t)(pos % bar);        /* ticks into the bar */
    if (++pos >= len) {
        pos = 0;
        seek();
    }
}

/* a new chord: re-voice the Chord 1 / 2 and Bass notes with time left; the rest end as they were */
static void chord_change(chord_t c)
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
        if (busy[cur] || !(x->type == XT_CHORD1 || x->type == XT_CHORD2 || x->type == XT_BASS))
            continue;
        for (int i = k->nh - 1; i >= 0; i--) {
            held_t *h = &k->h[i];
            int out;
            if (h->left <= RESTRIKE_MIN)
                continue;
            out = xpose(x, &k->xs, h->in, c);
            if (out == h->out)
                continue;
            if (out < 0 || ons_left <= 0) {        /* no new pitch, or no more note-ons this block: it ends */
                note_end(k, i);
                continue;
            }
            ons_left--;
            synth_off(h->chan, h->out);
            off_out(h);
            h->out = (uint8_t)out;
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
        if (arr_playing && ext_pend < 2u * ARR_PPQ)
            ext_pend++;
    } else if (msg == ARR_CLK_START) {
        clk_out = 0;
        transport_start();
        arr_ext = 1;
        ext_age = 0;
    } else if (msg == ARR_CLK_CONTINUE) {
        if (!arr_playing) {
            seek();
            setup();
        }
        clk_out = 0;
        arr_playing = 1;
        arr_ext = 1;
        ext_age = 0;
    } else if (msg == ARR_CLK_STOP)
        transport_stop();
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN], a = c & 0xFFFFFFu, b = qb[q_r % QN];
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
        case C_TEMPO:                              /* Q16 frames a tick, exact: 110250 / bpm */
            fpt = (FRAMES_PER_TICK_1BPM / a) << 16 | ((FRAMES_PER_TICK_1BPM % a) << 16) / a;
            break;
        case C_CHORD: chord_change(arr_chord_unpack(a)); break;
        case C_SECTION:
            if (a >= SEC_A && a <= SEC_END)
                arr_next_sect = (uint8_t)a;
            break;
        case C_MUTE:
            if (a < ARR_TRACKS) {
                tk[a].mute = (uint8_t)b;
                if (b)
                    track_off(&tk[a]);
            }
            break;
        case C_MOUT:
            if (a && !mout) {
                memset(out_sent, 0xFF, sizeof out_sent);   /* what the far side has: unknown */
                mout = 1;
                out_pend = arr_playing;
                if (arr_playing && !arr_ext) {     /* playing on our clock: start the far side now */
                    clk_out = 1;
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
        case C_SONG:
            if (a) {
                song_restart = arr_playing && !song_on;    /* a new song while playing: from the next bar */
                song_on = 1;
                song_loop = (uint8_t)b;
                tmpl = -1;
            } else
                song_on = song_restart = 0;
            break;
        case C_TEMPLATE:
            if (a) {
                tmpl = (int8_t)(a - 1);
                song_on = song_restart = 0;
                t_origin = arr_playing ? bar_beat0 : beat_now;   /* its first bar: the bar playing */
                chord_change(tmpl_chord());        /* its chord at once */
            } else
                tmpl = -1;
            break;
        case C_KEY:
            key = (uint8_t)a;
            if (tmpl >= 0)
                chord_change(tmpl_chord());
            break;
        case C_LIVE:                               /* the keys take over from a template */
            tmpl = -1;
            chord_change(arr_chord_unpack(a));
            break;
        default: break;
        }
    }
}

/* ---------------------------------------------------------------- the render's hook --- */
void arr_block(uint32_t frames)
{
    const int32_t end = (int32_t)(frames << 16);   /* the block's end, Q16 */
    ons_left = MAX_ONS;                            /* chord changes in the commands count too */
    arr_late_offs();                               /* the last block's late note-offs (gm_render sends them first) */
    drain();
    sync();
    if (arr_midi_out_link() != out_link) {        /* a new connection: its sounds unknown */
        out_link = arr_midi_out_link();
        memset(out_sent, 0xFF, sizeof out_sent);
        out_pend = mout && arr_playing;
    }
    if (arr_ext) {
        int n = 0;
        while (ext_pend && arr_playing) {          /* the clocks that came in since the last block */
            ext_pend--;
            tick(n++ < MAX_CATCHUP);
        }
        ext_pend = 0;
        sounds_out();
        ext_age += frames;
        if (ext_age > EXT_TIMEOUT) {               /* the clock is gone: ours again, a tick from now; */
            arr_ext = 0;                           /* no clock out until our own next start */
            ftt = (int32_t)fpt;
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
    while (ftt < end && arr_playing) {             /* the ticks due within this block, each note-on on its */
        int32_t f = ftt < 0 ? 0 : ftt;             /* sample: output frames / 2, rounded (EAS's 22.05 kHz) */
        at = (uint32_t)(f + (1 << 16)) >> 17;
        if (at >= frames / 2)
            at = frames / 2 - 1;
        tick(1);
        ftt += (int32_t)fpt;
    }
    at = 0;                                        /* the rest (commands, MIDI clock in): the block's start */
    arr_template_now = tmpl;
    ftt -= end;
    sounds_out();
}

void arr_init(void)                                /* at boot, before the render runs */
{
    q_w = q_r = 0;
    memset(tk, 0, sizeof tk);
    for (int i = 0; i < ARR_NSECT; i++) {
        memset(&pat[i], 0, sizeof pat[i]);
        pat[i].bars = 1;
        pat[i].beats = 4;
        busy[i] = 0;
        gen[i] = seen[i] = 0;
    }
    for (int t = 0; t < ARR_TRACKS; t++) {
        xpose_reset(&tk[t].xs);
        arr_track_hit[t] = 0;
    }
    chord.root = 0;
    chord.type = CH_MAJ7;                          /* patterns are written over C M7: as written */
    chord.bass = CH_NOBASS;
    arr_chord_now = arr_chord_pack(chord);
    cur = 0;
    pos = 0;
    mout = clk_out = 0;
    out_owed = 0;
    memset(out_sent, 0xFF, sizeof out_sent);
    out_pend = 0;
    out_link = arr_midi_out_link();
    ftt = 0;
    at = 0;
    n_late = 0;
    fpt = (FRAMES_PER_TICK_1BPM / 120u) << 16 | ((FRAMES_PER_TICK_1BPM % 120u) << 16) / 120u;
    ext_pend = ext_age = 0;
    arr_playing = 0;
    arr_ext = 0;
    arr_sect = SEC_A;
    arr_next_sect = SEC_SAME;
    arr_bar = arr_beat_tick = 0;
    arr_ticks = 0;
    song_busy = 0;
    song_on = song_loop = song_restart = 0;
    sbar = 0;
    arr_song_bar = 0;
    tmpl = -1;
    arr_template_now = -1;
    key = 0;
    first_tick = 0;
    fill_from = SEC_A;
    sec_bars = 0;
    beat_now = bar_beat0 = t_origin = 0;
}
