/* SPDX-License-Identifier: GPL-3.0-only */
/* The arranger engine (arr.h): a pattern's 8 tracks through the transposer over the chord now, on the
 * clock (ours, or MIDI clock in), into the GM synth and MIDI out. Threads and commands as Life's (life.c):
 * the main loop posts, the render drains and runs the sequencer, called by gm_render between EAS blocks.
 * Everything here is in statics: the render runs on the 8 KiB interrupt stack. */
#include <string.h>
#include "arr.h"
#include "gm.h"

#define BARRIER() __asm__ volatile("" ::: "memory")
#define ARR_HELD 8                                 /* notes held a track */
#define RESTRIKE_MIN 12                            /* at a chord change, notes with more ticks left restrike */
#define EXT_TIMEOUT 26460u                         /* frames without a clock in (0.6 s): back to our own */
#define FRAMES_PER_TICK_1BPM 110250u               /* 44100 * 60 / 24 */

volatile uint8_t arr_playing, arr_sect = SEC_A, arr_next_sect, arr_ext;
volatile uint16_t arr_bar, arr_beat_tick;
volatile chord_t arr_chord_now;
volatile uint32_t arr_track_hit[ARR_TRACKS];
volatile uint32_t arr_ticks;

/* ------------------------------------------------------------ commands --- */
enum { C_PLAY, C_TEMPO, C_CHORD, C_LOADED, C_SECTION, C_MUTE, C_MOUT, C_CLOCK };
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
 * busy; the render, which interrupts the main loop and never the other way round, leaves a busy slot alone. */
static pattern_t pat[ARR_NSECT];
static volatile uint8_t busy[ARR_NSECT];

void arr_play(int on) { post(C_PLAY, on != 0, 0); }
void arr_set_tempo(int bpm) { post(C_TEMPO, (uint32_t)(bpm < 40 ? 40 : bpm > 240 ? 240 : bpm), 0); }
void arr_set_chord(chord_t c) { post(C_CHORD, (uint32_t)c.root | (uint32_t)c.type << 8 | (uint32_t)c.bass << 16, 0); }
void arr_section(int sec) { post(C_SECTION, (uint32_t)sec, 0); }
void arr_track_mute(int trk, int on) { post(C_MUTE, (uint32_t)trk, on != 0); }
void arr_midi_out_on(int on) { post(C_MOUT, on != 0, 0); }
void arr_clock(int msg) { post(C_CLOCK, (uint32_t)msg, 0); }
void arr_load_pattern(int slot, const pattern_t *p)
{
    if (slot < 0 || slot >= ARR_NSECT)
        return;
    busy[slot] = 1;
    BARRIER();
    memcpy(&pat[slot], p, sizeof *p);
    BARRIER();
    busy[slot] = 0;
    post(C_LOADED, (uint32_t)slot, 0);
}

/* ------------------------------------------------------------ the state --- */
typedef struct {
    uint8_t out, in, vel, chan;
    uint8_t mo;                                    /* sent to MIDI out too: its off goes there as well */
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
static uint8_t mout;

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

/* ---------------------------------------------------------------- notes --- */
static void send(uint32_t st, uint32_t d1, uint32_t d2, int to_out)
{
    gm_midi_now(st, d1, d2);
    if (to_out)
        arr_midi_out(st, d1, d2);
}
static void held_remove(track_t *t, int i)
{
    t->nh--;
    for (; i < t->nh; i++)
        t->h[i] = t->h[i + 1];
}
static void note_end(track_t *t, int i)            /* the note-off goes to the note it struck */
{
    held_t *h = &t->h[i];
    send(0x80u | h->chan, h->out, 0, h->mo);
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
    h->mo = mout;
    h->left = (uint16_t)(len ? len : 1);
    send(0x90u | (uint32_t)chan, (uint32_t)out, h->vel, mout);
    arr_track_hit[ti] = arr_ticks;
}
static void all_off(void)
{
    for (int t = 0; t < ARR_TRACKS; t++)
        track_off(&tk[t]);
}

/* the playing pattern's sounds: program, volume, pan, sends; once, when it starts */
static void setup(void)
{
    const pattern_t *p = &pat[cur];
    if (busy[cur])
        return;
    for (int i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *t = &p->trk[i];
        uint32_t c = t->chan & 15u;
        send(0xC0u | c, t->program, 0, mout);
        send(0xB0u | c, 7, t->vol, mout);
        send(0xB0u | c, 10, t->pan, mout);
        send(0xB0u | c, 91, t->rev, mout);
        send(0xB0u | c, 93, t->cho, mout);
    }
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

/* ------------------------------------------------------------ the clock --- */
static void transport_start(void)
{
    pos = 0;
    ftt = 0;                                       /* tick 0 at once */
    ext_pend = 0;
    for (int t = 0; t < ARR_TRACKS; t++) {
        track_off(&tk[t]);
        xpose_reset(&tk[t].xs);
        xpose_chord_changed(&tk[t].xs);            /* Bass: the first note is the root */
    }
    arr_sect = (uint8_t)(cur + 1);
    seek();
    setup();
    arr_playing = 1;
}
static void transport_stop(void)
{
    arr_playing = 0;
    all_off();
}

static void tick(void)
{
    const pattern_t *p = &pat[cur];
    uint32_t len, bar;
    arr_ticks++;
    for (int t = 0; t < ARR_TRACKS; t++)           /* notes ending first */
        for (int i = tk[t].nh - 1; i >= 0; i--)
            if (--tk[t].h[i].left == 0)
                note_end(&tk[t], i);
    if (pos % bar_len(p) == 0 && arr_next_sect != SEC_SAME && arr_next_sect <= SEC_END) {
        cur = (uint8_t)(arr_next_sect - 1);        /* the section changes on the bar (Task 5: fills, endings) */
        arr_sect = arr_next_sect;
        arr_next_sect = SEC_SAME;
        p = &pat[cur];
        pos = 0;
        seek();
        setup();
    }
    if (!busy[cur])
        for (int t = 0; t < ARR_TRACKS; t++) {
            const arr_trk_t *tr = &p->trk[t];
            uint32_t e = ev_end(tr), n = tk[t].nx;
            int audible = !tk[t].mute && !tr->mute;
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
    if (mout && !arr_ext)
        arr_midi_out(0xF8u, 0, 0);                 /* clock out, 24 a quarter: our tick */
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
    arr_chord_now = c;
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
            send(0x80u | h->chan, h->out, 0, h->mo);
            if (out < 0) {
                held_remove(k, i);
                continue;
            }
            h->out = (uint8_t)out;
            h->mo = mout;
            send(0x90u | h->chan, h->out, h->vel, mout);
        }
    }
}

static void clock_in(int msg)
{
    if (msg == ARR_CLK_TICK) {
        arr_ext = 1;
        ext_age = 0;
        if (arr_playing && ext_pend < 2u * ARR_PPQ)
            ext_pend++;
    } else if (msg == ARR_CLK_START) {
        transport_start();
        arr_ext = 1;
        ext_age = 0;
    } else if (msg == ARR_CLK_CONTINUE) {
        if (!arr_playing) {
            seek();
            setup();
        }
        arr_playing = 1;
        arr_ext = 1;
        ext_age = 0;
    } else if (msg == ARR_CLK_STOP)
        transport_stop();
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN], b = qb[q_r % QN], a = c & 0xFFFFFFu;
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_PLAY:
            if (a) {
                if (mout && !arr_ext)
                    arr_midi_out(0xFAu, 0, 0);     /* first: the sounds and tick 0's notes follow */
                transport_start();
            } else if (arr_playing) {
                transport_stop();
                if (mout && !arr_ext)
                    arr_midi_out(0xFCu, 0, 0);
            }
            break;
        case C_TEMPO:                              /* Q16 frames a tick, exact: 110250 / bpm */
            fpt = (FRAMES_PER_TICK_1BPM / a) << 16 | ((FRAMES_PER_TICK_1BPM % a) << 16) / a;
            break;
        case C_CHORD: {
            chord_t ch = {(uint8_t)(a & 0xFFu), (uint8_t)(a >> 8 & 0xFFu), (uint8_t)(a >> 16 & 0xFFu)};
            chord_change(ch);
            break;
        }
        case C_LOADED:
            if (arr_playing && a == cur) {
                if (pos >= pat_len(&pat[cur]))
                    pos = 0;
                seek();
                setup();
            }
            break;
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
        case C_MOUT: mout = (uint8_t)a; break;
        case C_CLOCK: clock_in((int)a); break;
        default: break;
        }
    }
}

/* ---------------------------------------------------------------- the render's hook --- */
void arr_block(uint32_t frames)
{
    const int32_t half = (int32_t)(frames << 15);  /* half a block, Q16 */
    drain();
    if (arr_ext) {
        while (ext_pend) {                         /* the clocks that came in since the last block */
            ext_pend--;
            tick();
        }
        ext_age += frames;
        if (ext_age > EXT_TIMEOUT) {               /* the clock is gone: ours again, a tick from now */
            arr_ext = 0;
            ext_pend = 0;
            ftt = (int32_t)fpt;
        }
        return;
    }
    if (!arr_playing)
        return;
    while (ftt < half) {                           /* ticks due in [-half, +half): at this block's start */
        tick();
        ftt += (int32_t)fpt;
    }
    ftt -= (int32_t)(frames << 16);
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
    }
    for (int t = 0; t < ARR_TRACKS; t++) {
        xpose_reset(&tk[t].xs);
        arr_track_hit[t] = 0;
    }
    chord.root = 0;
    chord.type = CH_MAJ7;                          /* patterns are written over C M7: as written */
    chord.bass = CH_NOBASS;
    arr_chord_now = chord;
    cur = 0;
    pos = 0;
    mout = 0;
    ftt = 0;
    fpt = (FRAMES_PER_TICK_1BPM / 120u) << 16 | ((FRAMES_PER_TICK_1BPM % 120u) << 16) / 120u;
    ext_pend = ext_age = 0;
    arr_playing = 0;
    arr_ext = 0;
    arr_sect = SEC_A;
    arr_next_sect = SEC_SAME;
    arr_bar = arr_beat_tick = 0;
    arr_ticks = 0;
}
