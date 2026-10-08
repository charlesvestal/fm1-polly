/* SPDX-License-Identifier: GPL-3.0-only */
/* The arranger engine (arr.c) against a recording GM stub (gm_stub.c): the notes it sends, their frame times
 * (44.1 kHz output frames), note-offs, chord changes, mutes, MIDI out and MIDI clock in.
 *
 * Timing tolerance: EAS renders whole 256-frame output blocks (128 at 22.05 kHz), so a tick sounds at the block
 * boundary nearest its exact time: +-128 frames (2.9 ms). The test allows +-132 (3 ms). */
#include <stdio.h>
#include <string.h>
#include "arr.h"
#include "gm_stub.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define TOL 132
#define FPT 918.75f                           /* frames a tick at 120 BPM: 44100 * 60 / (120 * 24) */

/* the app's MIDI out, recorded, through a ring like usb.c's: ring_cap slots, ring_drain sent a block; a
 * message that finds it full is dropped (and counted) */
static stub_msg_t out_log[STUB_MAX];
static int out_n, dropped;
static uint32_t ring_cap, ring_used, ring_drain;
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (ring_used >= ring_cap) {
        dropped++;
        return;
    }
    ring_used++;
    if (out_n < STUB_MAX) {
        stub_msg_t m = {stub_frame, (uint8_t)st, (uint8_t)d1, (uint8_t)d2};
        out_log[out_n++] = m;
    }
}
uint32_t arr_midi_out_room(void) { return ring_cap - ring_used; }
static uint32_t link_count;                   /* the USB connection: a new one when it counts up */
uint32_t arr_midi_out_link(void) { return link_count; }

static chord_t ch(int root, int type) { chord_t c = {(uint8_t)root, (uint8_t)type, CH_NOBASS}; return c; }

static pattern_t P;
static void pat_new(void)
{
    memset(&P, 0, sizeof P);
    memcpy(P.name, "test", 5);
    P.bars = 1;
    P.beats = 4;
    for (int i = 0; i < ARR_TRACKS; i++) {
        arr_trk_t *t = &P.trk[i];
        t->chan = i < 3 ? 9 : (uint8_t)(i - 2);
        t->program = i < 3 ? 0 : (uint8_t)(10 + i);
        t->x.type = i < 3 ? XT_BYPASS : XT_CHORD1;
        t->x.lo = 0;
        t->x.hi = 127;
        t->x.hikey = XT_NOKEY;
        t->vol = 100;
        t->pan = 64;
        t->rev = 40;
    }
}
/* events in track order, each track's sorted by tick */
static void add(int trk, int tick, int len, int note)
{
    arr_trk_t *t = &P.trk[trk];
    arr_ev_t e = {(uint16_t)tick, (uint16_t)len, (uint8_t)note, 100};
    if (!t->count)
        t->first = P.nev;
    t->count++;
    P.ev[P.nev++] = e;
}

static void reset(void)
{
    arr_init();
    stub_n = 0;
    stub_frame = 0;
    out_n = 0;
    dropped = 0;
    ring_cap = 100000;
    ring_used = 0;
    ring_drain = 100000;
}
static void run_to(uint32_t frame)              /* render until the output has passed `frame` */
{
    static int32_t buf[2 * 256];
    while (stub_frame < frame) {
        gm_render(buf, 256, 4096);
        ring_used = ring_used > ring_drain ? ring_used - ring_drain : 0;
    }
}
static void run_blocks(int n)
{
    run_to(stub_frame + 256u * (uint32_t)n);
}
/* the first message from index `from` with this status (exact) and data 1; -1 none */
static int find(const stub_msg_t *log, int n, int from, int st, int d1)
{
    for (int i = from; i < n; i++)
        if (log[i].st == st && log[i].d1 == d1)
            return i;
    return -1;
}
static int near(const stub_msg_t *log, int i, float frame)
{
    float d = (float)log[i].frame - frame;
    return i >= 0 && d <= TOL && d >= -TOL;
}
static int is_on(const stub_msg_t *m) { return (m->st & 0xF0) == 0x90 && m->d2; }
static int is_off(const stub_msg_t *m) { return (m->st & 0xF0) == 0x80 || ((m->st & 0xF0) == 0x90 && !m->d2); }
/* every note-on has a later note-off on the same channel and note, and no off is left over */
static int offs_match(const stub_msg_t *log, int n)
{
    static uint8_t used[STUB_MAX];
    int ons = 0, offs = 0;
    memset(used, 0, sizeof used);
    for (int i = 0; i < n; i++) {
        if (is_off(&log[i]))
            offs++;
        if (!is_on(&log[i]))
            continue;
        ons++;
        for (int j = i + 1; j < n; j++)
            if (!used[j] && is_off(&log[j]) && (log[j].st & 15) == (log[i].st & 15) && log[j].d1 == log[i].d1) {
                used[j] = 1;
                break;
            }
    }
    int matched = 0;
    for (int j = 0; j < n; j++)
        matched += used[j];
    return ons == matched && offs == matched && ons > 0;
}

/* track 0 (D1, channel 10): kicks at 0 and 48; track 4 (Chord 1, channel 3): C4 E4 G4 at 0, 24, 48 */
static void pattern_one(void)
{
    pat_new();
    add(0, 0, 6, 36);
    add(0, 48, 6, 36);
    add(4, 0, 12, 60);
    add(4, 24, 12, 64);
    add(4, 48, 12, 67);
}

static void test_timing(void)
{
    int i;
    reset();
    pattern_one();
    arr_load_pattern(0, &P);
    arr_set_tempo(120);
    arr_set_chord(ch(9, CH_MIN7));
    arr_play(1);
    run_to(44100 + 512);
    CHECK(arr_playing == 1, "playing");
    i = find(stub_log, stub_n, 0, 0xC2, 14);
    CHECK(i >= 0, "track 4's program on channel 3 at the start");
    CHECK(find(stub_log, stub_n, 0, 0xB2, 7) >= 0 && find(stub_log, stub_n, 0, 0xB2, 10) >= 0 &&
              find(stub_log, stub_n, 0, 0xB2, 91) >= 0 && find(stub_log, stub_n, 0, 0xB2, 93) >= 0,
          "track 4's volume, pan, sends");
    i = find(stub_log, stub_n, 0, 0x92, 69);
    CHECK(near(stub_log, i, 0.f), "C4 over Am7 -> A4 at 0, got %d at %u", i, i >= 0 ? stub_log[i].frame : 0);
    i = find(stub_log, stub_n, 0, 0x92, 72);
    CHECK(near(stub_log, i, 24 * FPT), "E4 -> C5 at 0.5 s, got %u", i >= 0 ? stub_log[i].frame : 0);
    i = find(stub_log, stub_n, 0, 0x92, 76);
    CHECK(near(stub_log, i, 48 * FPT), "G4 -> E5 at 1 s, got %u", i >= 0 ? stub_log[i].frame : 0);
    i = find(stub_log, stub_n, 0, 0x99, 36);
    CHECK(near(stub_log, i, 0.f), "kick at 0");
    i = find(stub_log, stub_n, i + 1, 0x99, 36);
    CHECK(near(stub_log, i, 44100.f), "kick at 1 s, got %u", i >= 0 ? stub_log[i].frame : 0);
    i = find(stub_log, stub_n, 0, 0x82, 69);
    CHECK(near(stub_log, i, 12 * FPT), "A4 ends after 12 ticks, got %u", i >= 0 ? stub_log[i].frame : 0);
    /* the loop: 1 bar of 4 = 96 ticks = 2 s */
    run_to(2 * 44100 + 512);
    i = find(stub_log, stub_n, 0, 0x99, 36);
    i = find(stub_log, stub_n, i + 1, 0x99, 36);
    i = find(stub_log, stub_n, i + 1, 0x99, 36);
    CHECK(near(stub_log, i, 88200.f), "the loop: kick again at 2 s, got %u", i >= 0 ? stub_log[i].frame : 0);
    run_to(2 * 44100 + 4000);
    arr_play(0);
    run_blocks(1);
    CHECK(arr_playing == 0, "stopped");
    CHECK(offs_match(stub_log, stub_n), "a note-off for each note-on");
    CHECK(arr_chord_unpack(arr_chord_now).root == 9 && arr_chord_unpack(arr_chord_now).type == CH_MIN7, "the chord for the screen");
}

static void test_restrike(void)
{
    int i, j;
    reset();
    pat_new();
    add(4, 0, 48, 60);                        /* C4 for 48 ticks: cut and restruck at the change */
    add(5, 0, 20, 64);                        /* E4 for 20: 8 left at the change, left to end */
    arr_load_pattern(0, &P);
    arr_set_chord(ch(9, CH_MIN7));
    arr_play(1);
    run_to(12 * 918 + 300);                    /* past tick 12 (0.25 s) */
    arr_set_chord(ch(7, CH_7));
    run_blocks(1);
    i = find(stub_log, stub_n, 0, 0x82, 69);
    j = find(stub_log, stub_n, 0, 0x92, 67);
    CHECK(i >= 0 && j > i, "off A4, then on G4 (%d %d)", i, j);
    CHECK(i >= 0 && stub_log[i].frame >= 12 * FPT - TOL && stub_log[i].frame <= 13 * FPT + TOL, "within a tick of the change");
    CHECK(j >= 0 && stub_log[j].frame <= 13 * FPT + TOL, "G4 within a tick");
    CHECK(find(stub_log, stub_n, 0, 0x93, 71) < 0, "the short E4 (C5 over Am7) is not restruck");
    run_to(44100 + 512);
    i = find(stub_log, stub_n, 0, 0x83, 72);
    CHECK(near(stub_log, i, 20 * FPT), "C5 ends on its own note at tick 20, got %u", i >= 0 ? stub_log[i].frame : 0);
    i = find(stub_log, stub_n, 0, 0x82, 67);
    CHECK(near(stub_log, i, 48 * FPT), "G4 ends at tick 48, got %u", i >= 0 ? stub_log[i].frame : 0);
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "a note-off for each note-on, across the chord change");
}

static void test_mute(void)
{
    int i;
    reset();
    pattern_one();
    arr_load_pattern(0, &P);
    arr_track_mute(0, 1);
    arr_play(1);
    run_to(22050);
    CHECK(find(stub_log, stub_n, 0, 0x99, 36) < 0, "a muted track sends nothing");
    CHECK(find(stub_log, stub_n, 0, 0x92, 60) >= 0, "the others play (C4 over C M7)");
    arr_track_mute(0, 0);
    run_to(44100 + 512);
    i = find(stub_log, stub_n, 0, 0x99, 36);
    CHECK(near(stub_log, i, 44100.f), "unmuted: from the next note, at 1 s, got %u", i >= 0 ? stub_log[i].frame : 0);
    arr_track_mute(4, 1);                     /* muting while a note sounds ends it */
    run_blocks(1);
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match with mutes");
}

static void test_midi_out(void)
{
    int i, f8 = 0;
    reset();
    pattern_one();
    arr_load_pattern(0, &P);
    arr_midi_out_on(1);
    arr_set_chord(ch(9, CH_MIN7));
    arr_play(1);
    run_to(44100 - 200);
    for (i = 0; i < out_n && ((out_log[i].st & 0xF0) == 0xC0 || (out_log[i].st & 0xF0) == 0xB0); i++)
        ;                                     /* the sounds first */
    CHECK(i < out_n && out_log[i].st == 0xFA, "FA at start, after the sounds");
    for (i = 0; i < out_n; i++)
        f8 += out_log[i].st == 0xF8;
    CHECK(f8 == 48, "24 clocks a quarter: 48 in a second at 120, got %d", f8);
    i = find(out_log, out_n, 0, 0x92, 69);
    CHECK(near(out_log, i, 0.f), "A4 out on the track's channel");
    CHECK(find(out_log, out_n, 0, 0x99, 36) >= 0, "the kick out on channel 10");
    arr_play(0);
    run_blocks(1);
    CHECK(out_log[out_n - 1].st == 0xFC, "FC at stop");
    CHECK(offs_match(out_log, out_n), "MIDI out: an off for each on");
    /* off: no notes, no clock */
    reset();
    arr_load_pattern(0, &P);
    arr_play(1);
    run_to(44100);
    CHECK(out_n == 0, "MIDI out off: nothing out (%d)", out_n);
}

static void test_clock_in(void)
{
    int i, k;
    reset();
    pattern_one();
    arr_load_pattern(0, &P);
    arr_midi_out_on(1);
    arr_set_chord(ch(9, CH_MIN7));
    arr_clock(ARR_CLK_START);
    run_blocks(4);
    CHECK(arr_playing == 1 && arr_ext == 1, "started by the clock");
    CHECK(find(stub_log, stub_n, 0, 0x92, 69) < 0, "nothing before the first clock");
    for (k = 0; k < 30; k++) {                /* a slow clock: 4 blocks a tick */
        arr_clock(ARR_CLK_TICK);
        if (k == 0)
            run_blocks(1), i = stub_n;
        else
            run_blocks(4);
        if (k == 0)
            CHECK(find(stub_log, i, 0, 0x92, 69) >= 0, "the first clock is tick 0");
        if (k == 23)
            CHECK(find(stub_log, stub_n, 0, 0x92, 72) < 0, "tick 24 not before the 25th clock");
        if (k == 24)
            CHECK(find(stub_log, stub_n, 0, 0x92, 72) >= 0, "tick 24 at the 25th clock");
    }
    for (i = 0; i < out_n; i++)
        CHECK(out_log[i].st != 0xF8 && out_log[i].st != 0xFA, "no clock out while following one");
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
    CHECK(arr_playing == 0, "stopped by the clock");
    CHECK(offs_match(stub_log, stub_n), "offs match after a clock stop");
    /* the clock gone: back to our own */
    reset();
    arr_load_pattern(0, &P);
    arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    CHECK(arr_ext == 1, "a clock: following it");
    run_to(44100);
    CHECK(arr_ext == 0, "no clock for 0.6 s: our own again");
}

static int count_ons(const stub_msg_t *log, int from, int to)
{
    int n = 0;
    for (int i = from; i < to; i++)
        n += is_on(&log[i]);
    return n;
}
/* a note on track 4 every tick (C4, 1 tick long), and pattern_one's */
static void pattern_dense(void)
{
    pat_new();
    add(0, 0, 6, 36);
    add(0, 48, 6, 36);
    for (int t = 0; t < 96; t++)
        add(4, t, 1, 60);
}

static void test_stop_same_block(void)        /* a clock and a stop in one block: no note after the stop */
{
    int n0;
    reset();
    pattern_dense();
    arr_load_pattern(0, &P);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    for (int k = 0; k < 5; k++) {
        arr_clock(ARR_CLK_TICK);
        run_blocks(1);
    }
    n0 = stub_n;
    arr_clock(ARR_CLK_TICK);                  /* F8 then FC, from a DAW, within one block */
    arr_clock(ARR_CLK_STOP);
    run_blocks(2);
    CHECK(count_ons(stub_log, n0, stub_n) == 0, "F8 + FC: no note-on after the stop");
    CHECK(offs_match(stub_log, stub_n), "F8 + FC: offs match");
    /* the same with our own stop */
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    n0 = stub_n;
    arr_clock(ARR_CLK_TICK);
    arr_play(0);
    run_blocks(2);
    CHECK(arr_playing == 0, "stopped");
    CHECK(count_ons(stub_log, n0, stub_n) == 0, "F8 + arr_play(0): no note-on after the stop");
    CHECK(offs_match(stub_log, stub_n), "F8 + arr_play(0): offs match");
}

static void test_out_ring(void)               /* MIDI out never drops a note-off, however full the ring */
{
    int synth_ons, out_ons, pcs = 0, pcs2 = 0, i;
    reset();
    pat_new();
    for (int t = 3; t < 8; t++)               /* five tracks, a note every 3 ticks, 30 long: 50 held at once */
        for (int k = 0; k < 96; k += 3)
            add(t, k, 30, 48 + 3 * t + (k / 3) % 7);
    arr_load_pattern(0, &P);
    ring_cap = 64;                            /* usb.c's ring */
    ring_drain = 0;                           /* the host not reading for a second */
    arr_midi_out_on(1);
    arr_play(1);
    run_to(44100);
    ring_drain = 1;                           /* then slowly */
    run_to(3 * 44100);
    arr_play(0);
    run_to(4 * 44100);
    synth_ons = count_ons(stub_log, 0, stub_n);
    out_ons = count_ons(out_log, 0, out_n);
    CHECK(dropped == 0, "nothing dropped by the ring (%d)", dropped);
    CHECK(out_ons > 0 && out_ons < synth_ons, "some notes out, the rest only in the synth (%d of %d)", out_ons, synth_ons);
    CHECK(offs_match(out_log, out_n), "MIDI out: an off for each on sent");
    CHECK(offs_match(stub_log, stub_n), "the synth: an off for each on");
    /* the sounds out: at a start, only what changed */
    reset();
    pattern_one();
    arr_load_pattern(0, &P);
    arr_midi_out_on(1);
    arr_play(1);
    run_blocks(2);
    for (i = 0; i < out_n; i++)
        pcs += (out_log[i].st & 0xF0) == 0xC0;
    CHECK(pcs == 6, "programs out once a channel (6 channels), got %d", pcs);
    arr_play(0);
    run_blocks(1);
    i = out_n;
    arr_play(1);
    run_blocks(2);
    for (; i < out_n; i++)
        pcs2 += (out_log[i].st & 0xF0) == 0xC0 || (out_log[i].st & 0xF0) == 0xB0;
    CHECK(pcs2 == 0, "started again: no sounds out, none changed (%d)", pcs2);
    CHECK(find(stub_log, stub_n, 0, 0xC2, 14) >= 0, "the synth gets them");
}

static void test_reload(void)                 /* a pattern reloaded while it plays: no stale positions */
{
    int n0, i, ok = 1;
    reset();
    pat_new();
    for (int t = 0; t < 96; t += 6)
        add(4, t, 3, 60);
    arr_load_pattern(0, &P);
    arr_play(1);
    run_to(13000);                            /* past tick 12 */
    pat_new();                                /* track 4's events now after track 0's: a stale place would */
    for (int t = 0; t < 96; t += 6)           /* walk track 4 through track 0's kicks */
        add(0, t, 3, 36);
    for (int t = 3; t < 96; t += 6)
        add(4, t, 3, 72);
    n0 = stub_n;
    arr_load_pattern(0, &P);
    run_to(44100);
    for (i = n0; i < stub_n; i++)
        if (is_on(&stub_log[i]) && !(stub_log[i].st == 0x99 && stub_log[i].d1 == 36) &&
            !(stub_log[i].st == 0x92 && stub_log[i].d1 == 72))
            ok = 0;
    CHECK(ok, "after the reload only the new pattern's notes, on their channel");
    i = find(stub_log, stub_n, n0, 0x99, 36);
    CHECK(near(stub_log, i, 18 * FPT), "from its next event (tick 18), got %u", i >= 0 ? stub_log[i].frame : 0);
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "reload: offs match");
}

static void test_bounds(void)                 /* the work a block is bounded */
{
    int n0;
    uint32_t t0;
    reset();
    pat_new();
    for (int t = 0; t < ARR_TRACKS; t++) {    /* 64 notes on tick 0 */
        P.trk[t].x.type = XT_BYPASS;
        for (int k = 0; k < 8; k++)
            add(t, 0, 12, 40 + 8 * t + k);
    }
    arr_load_pattern(0, &P);
    arr_play(1);
    run_blocks(1);
    CHECK(count_ons(stub_log, 0, stub_n) == 48, "48 note-ons a block at most, got %d", count_ons(stub_log, 0, stub_n));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "capped: offs match");
    /* 20 clocks in one block: the position moves 20, 4 of them play */
    reset();
    pattern_dense();
    arr_load_pattern(0, &P);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    t0 = arr_ticks;
    n0 = stub_n;
    for (int k = 0; k < 20; k++)
        arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    CHECK(arr_ticks - t0 == 20 && arr_beat_tick == 20, "20 ticks on (%u, at %u)", arr_ticks - t0, arr_beat_tick);
    CHECK(count_ons(stub_log, n0, stub_n) <= 4, "at most 4 played, got %d", count_ons(stub_log, n0, stub_n));
    arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    CHECK(find(stub_log, stub_n, n0, 0x92, 60) >= 0, "playing on");
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "bunched: offs match");
}

static int count_st(int from, int to, int st)  /* MIDI out messages with this status high nibble (or exact F8) */
{
    int n = 0;
    for (int i = from; i < to; i++)
        n += st >= 0xF0 ? out_log[i].st == st : (out_log[i].st & 0xF0) == st;
    return n;
}
/* every track on its own channel with its own program (40 sounds to send); `per` notes on tick 0 on the
 * first `ntr` tracks */
static void pattern_busy_downbeat(int ntr, int per)
{
    pat_new();
    for (int t = 0; t < ARR_TRACKS; t++) {
        P.trk[t].chan = (uint8_t)t;
        P.trk[t].program = (uint8_t)(20 + t);
        P.trk[t].x.type = XT_BYPASS;
        for (int k = 0; k < per && t < ntr; k++)
            add(t, 0, 12, 50 + 2 * t + k);
    }
}
static int is_sound(const stub_msg_t *m) { return (m->st & 0xF0) == 0xC0 || (m->st & 0xF0) == 0xB0; }

static void test_sounds_first(void)           /* at a start, the sounds before FA when they fit */
{
    int i, ok = 1;
    reset();
    pattern_busy_downbeat(4, 1);              /* 40 sounds, FA, F8, 4 notes and their offs: fits 64 */
    arr_load_pattern(0, &P);
    ring_cap = 64;
    ring_drain = 64;
    arr_midi_out_on(1);
    arr_play(1);
    run_to(44100 - 200);
    for (i = 0; i < 40; i++)
        ok &= is_sound(&out_log[i]);
    CHECK(ok && count_st(0, 40, 0xC0) == 8, "the 40 sounds first");
    CHECK(out_log[40].st == 0xFA && out_log[41].st == 0xF8, "then FA, then tick 0's F8 (%02X %02X)", out_log[40].st, out_log[41].st);
    CHECK(count_ons(out_log, 42, 46) == 4, "then tick 0's 4 notes");
    CHECK(count_st(0, out_n, 0xF8) == 48, "48 F8 in the first second at 120, got %d", count_st(0, out_n, 0xF8));
    CHECK(dropped == 0, "nothing dropped");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(out_log, out_n), "offs match");
}

static void test_clock_first(void)            /* the clock is never squeezed out by notes or sounds */
{
    int i, fa;
    reset();
    pattern_busy_downbeat(8, 2);              /* 40 sounds and 16 notes do not fit 64 at once: */
    arr_load_pattern(0, &P);                  /* the sounds now, the start a block later */
    ring_cap = 64;                            /* usb.c's ring, a healthy host reading it every block */
    ring_drain = 64;
    arr_midi_out_on(1);
    arr_play(1);
    run_to(256 + 44100 - 200);
    for (i = 0; i < 40; i++)
        CHECK(is_sound(&out_log[i]) && out_log[i].frame == 0, "the 40 sounds first, in the first block");
    fa = find(out_log, out_n, 0, 0xFA, 0);
    CHECK(fa == 40 && out_log[fa].frame == 256, "FA a block later (%d at %u)", fa, fa >= 0 ? out_log[fa].frame : 0);
    CHECK(out_log[41].st == 0xF8 && count_ons(out_log, 42, 58) == 16, "then F8 and all 16 notes of tick 0");
    CHECK(count_st(0, out_n, 0xF8) == 48, "48 F8 in the second from FA at 120, got %d", count_st(0, out_n, 0xF8));
    i = 0;
    while (i < stub_n && !is_on(&stub_log[i]))
        i++;
    CHECK(i < stub_n && stub_log[i].frame == 256, "the synth starts with it (%u)", i < stub_n ? stub_log[i].frame : 0);
    CHECK(dropped == 0, "nothing dropped");
    /* a new USB connection: the sounds again */
    i = out_n;
    link_count++;
    run_blocks(4);
    CHECK(count_st(i, out_n, 0xC0) == 8, "a new connection: the programs again (%d)", count_st(i, out_n, 0xC0));
    /* a section change: Main B's changed sounds go out at its bar */
    for (int t = 0; t < ARR_TRACKS; t++)
        P.trk[t].program = (uint8_t)(40 + t);
    P.trk[0].program = 20;                    /* one unchanged: not sent again */
    arr_load_pattern(1, &P);
    i = out_n;
    arr_section(SEC_B);
    run_to(2 * 44100 + 4 * 256);
    CHECK(arr_sect == SEC_B, "Main B playing");
    CHECK(count_st(i, out_n, 0xC0) == 7 && count_st(i, out_n, 0xB0) == 0, "Main B: its 7 new programs out (%d, %d CCs)",
          count_st(i, out_n, 0xC0), count_st(i, out_n, 0xB0));
    CHECK(find(out_log, out_n, i, 0xC1, 41) >= 0, "track 1's new program");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(out_log, out_n), "offs match");
}

static void test_start_cap(void)              /* a stalled host: the start waits 8 blocks at most */
{
    int fa;
    reset();
    pattern_busy_downbeat(8, 2);
    arr_load_pattern(0, &P);
    ring_cap = 64;
    ring_drain = 0;
    arr_midi_out_on(1);
    arr_play(1);
    run_to(20 * 256);
    fa = find(out_log, out_n, 0, 0xFA, 0);
    CHECK(fa >= 0 && out_log[fa].frame <= 9 * 256, "started anyway (%u)", fa >= 0 ? out_log[fa].frame : 0);
    CHECK(count_ons(stub_log, 0, stub_n) == 16, "the synth plays");
    ring_drain = 64;
    arr_play(0);
    run_to(44100);
    CHECK(dropped == 0 && offs_match(out_log, out_n), "nothing dropped, offs match");
}

static void test_transport_out(void)
{
    int i, f8 = 0, fc = -1;
    reset();
    pattern_one();
    arr_load_pattern(0, &P);
    arr_play(1);
    run_to(22050);
    CHECK(out_n == 0, "MIDI out off: nothing");
    arr_midi_out_on(1);                       /* on while playing: FA, then the clock */
    run_to(44100);
    CHECK(out_n > 0 && out_log[0].st == 0xFA, "FA when MIDI out comes on");
    for (i = 0; i < out_n; i++)
        f8 += out_log[i].st == 0xF8;
    CHECK(f8 >= 20, "then F8s (%d)", f8);
    arr_midi_out_on(0);                       /* off: FC, no more clock */
    run_to(66150);
    for (i = 0; i < out_n; i++)
        if (out_log[i].st == 0xFC)
            fc = i;
    CHECK(fc >= 0, "FC when MIDI out goes off");
    for (i = fc + 1; i < out_n; i++)
        CHECK(out_log[i].st != 0xF8 && is_off(&out_log[i]), "after FC only the note-offs owed");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(out_log, out_n), "out on and off: offs match");
    /* an external clock gone: our own clock plays on, but nothing new is sent until the next start */
    reset();
    pattern_one();
    arr_load_pattern(0, &P);
    arr_midi_out_on(1);
    arr_clock(ARR_CLK_START);
    for (int k = 0; k < 3; k++) {
        arr_clock(ARR_CLK_TICK);
        run_blocks(1);
    }
    run_to(2 * 44100);
    CHECK(arr_ext == 0 && arr_playing == 1, "the clock gone: ours, still playing");
    f8 = 0;
    for (i = 0; i < out_n; i++)
        f8 += out_log[i].st == 0xF8 || out_log[i].st == 0xFA;
    CHECK(f8 == 0, "no clock out after falling back (%d)", f8);
}

int main(void)
{
    test_timing();
    test_restrike();
    test_mute();
    test_midi_out();
    test_clock_in();
    test_stop_same_block();
    test_out_ring();
    test_reload();
    test_bounds();
    test_transport_out();
    test_sounds_first();
    test_clock_first();
    test_start_cap();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("arr test passed\n");
    return 0;
}
