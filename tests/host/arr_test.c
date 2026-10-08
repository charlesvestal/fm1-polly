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

/* the app's MIDI out, recorded */
static stub_msg_t out_log[STUB_MAX];
static int out_n;
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (out_n < STUB_MAX) {
        stub_msg_t m = {stub_frame, (uint8_t)st, (uint8_t)d1, (uint8_t)d2};
        out_log[out_n++] = m;
    }
}

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
}
static void run_to(uint32_t frame)              /* render until the output has passed `frame` */
{
    static int32_t buf[2 * 256];
    while (stub_frame < frame)
        gm_render(buf, 256, 4096);
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
    CHECK(arr_chord_now.root == 9 && arr_chord_now.type == CH_MIN7, "the chord for the screen");
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
    CHECK(out_n > 0 && out_log[0].st == 0xFA, "FA at start");
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

int main(void)
{
    test_timing();
    test_restrike();
    test_mute();
    test_midi_out();
    test_clock_in();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("arr test passed\n");
    return 0;
}
