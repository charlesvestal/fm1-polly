/* SPDX-License-Identifier: GPL-3.0-only */
/* The arranger engine (arr.c) against a recording GM stub (gm_stub.c): the notes it sends, their frame times
 * (44.1 kHz output frames), note-offs, chord changes, mutes, MIDI out and MIDI clock in.
 *
 * Timing: EAS renders whole 256-frame output blocks (128 at 22.05 kHz) and takes MIDI between them; a note-on
 * starts on its sample within the block (gm_midi_now's at, recorded so by the stub): +-1 sample at 22.05 kHz,
 * +-2 output frames (ON_TOL). A note-off goes to the block start nearest its time (+-128 frames); everything
 * else (chord changes) to the start of the block its time falls in, up to 256 frames early (TOL). */
#include <stdio.h>
#include <string.h>
#include "arr.h"
#include "gm_stub.h"
#include "loops.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define ON_TOL 2
#define TOL 256
/* the test patterns are written in units of a 24th of a beat (U ticks: add() scales them), so FPT is a unit's
 * frames; a tick is FPT / U */
#define U (ARR_PPQ / 24)
#define FPT 918.75f                           /* frames a unit at 120 BPM: 44100 * 60 / (120 * 24) */
#define TPF ((float)U / FPT)                  /* ticks a frame at 120 BPM */

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

/* the slots (a section's: sec - 1) */
enum { SI = SEC_INTRO - 1, SA = SEC_A - 1, SB = SEC_B - 1, SFA = SEC_FILL_AB - 1, SFB = SEC_FILL_BA - 1, SE = SEC_END - 1 };

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
/* events in track order, each track's sorted by tick; tick and len in units (U ticks) */
static void add(int trk, int tick, int len, int note)
{
    arr_trk_t *t = &P.trk[trk];
    arr_ev_t e = {(uint16_t)(tick * U), (uint16_t)(len * U), (uint8_t)note, 100};
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
static loop_t LT[ARR_LOOPS];                       /* the app's loop table, here */
static void loops_reset(void)
{
    for (int i = 0; i < ARR_LOOPS; i++)
        loop_clear(&LT[i], i);
    arr_loops(LT);
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
static int is_on(const stub_msg_t *m) { return (m->st & 0xF0) == 0x90 && m->d2; }
static int is_off(const stub_msg_t *m);
/* a note-on on its frame (+-ON_TOL), a note-off at the block start nearest it, anything else at the start of
 * the block its frame falls in */
static int near(const stub_msg_t *log, int i, float frame)
{
    float d;
    if (i < 0)
        return 0;
    d = (float)log[i].frame - frame;
    if (is_on(&log[i]))
        return d <= ON_TOL && d >= -ON_TOL;
    if (is_off(&log[i]))
        return d <= 128.f + ON_TOL && d >= -128.f - ON_TOL;
    return d <= 0.f && d > -TOL;
}
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
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
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
    loops_reset();
    pat_new();
    add(4, 0, 48, 60);                        /* C4 for 48 ticks: cut and restruck at the change */
    add(5, 0, 20, 64);                        /* E4 for 20: 8 left at the change, left to end */
    arr_load_pattern(SA, &P);
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
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
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
    /* muted in the pattern (stored): its sounding note ends at once, as the live mute */
    reset();
    loops_reset();
    pat_new();
    add(4, 0, 90, 60);                        /* C4, nearly the whole bar */
    arr_load_pattern(SA, &P);
    arr_play(1);
    run_to((uint32_t)(24 * FPT));
    {
        uint16_t d = arr_pattern_copy(SA, &P);
        int n0 = stub_n;
        P.trk[4].mute = 1;
        CHECK(arr_store_pattern(SA, &P, d), "stored");
        run_blocks(1);
        i = find(stub_log, stub_n, n0, 0x82, 60);
        CHECK(i >= 0 || find(stub_log, stub_n, n0, 0x92, 60) >= 0, "the pattern's mute ends the note at once");
    }
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match with a pattern's mute");
}

static void test_midi_out(void)
{
    int i, f8 = 0;
    reset();
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
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
    loops_reset();
    arr_load_pattern(SA, &P);
    arr_play(1);
    run_to(44100);
    CHECK(out_n == 0, "MIDI out off: nothing out (%d)", out_n);
}

static void test_clock_in(void)
{
    int i, k;
    reset();
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
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
    loops_reset();
    arr_load_pattern(SA, &P);
    arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    CHECK(arr_ext == 1, "a clock: following it");
    run_to(44100);
    CHECK(arr_ext == 0, "no clock for 0.6 s: our own again");
}

/* MIDI clock in at 120 BPM (an F8 every 918.75 frames, each taken at the block it falls in): a note every 4
 * ticks, so 5 a clock. They are spread between the clocks at the clock's rate (about 184 frames apart, not 5 at
 * once on each clock), and none plays before the clock that allows it (tick 20 k needs clock k + 1) */
static void test_clock_smooth(void)
{
    static uint32_t arrive[200];
    int k = 0, n = 0, bunched = 0, early = 0, wide = 0;
    float next = 0.f, sum = 0.f, lo = 1e9f, hi = 0.f;
    uint32_t prev = 0;
    reset();
    loops_reset();
    pat_new();
    for (int t = 0; t < 96; t++)
        for (int j = 0; j < 5; j++) {
            add(0, t, 1, 36 + j);
            P.ev[P.nev - 1].tick = (uint16_t)(t * U + 4 * j);   /* ticks 0, 4, 8, ...: 2 ticks long */
            P.ev[P.nev - 1].len = 2;
        }
    arr_load_pattern(SA, &P);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    while (k < 200) {
        if ((float)stub_frame >= next) {
            arrive[k++] = stub_frame;
            arr_clock(ARR_CLK_TICK);
            next += 918.75f;
        }
        run_blocks(1);
    }
    for (int i = 0; i < stub_n; i++) {
        uint32_t f = stub_log[i].frame;
        if (stub_log[i].st != 0x99 || !is_on(&stub_log[i]))
            continue;
        if (f < arrive[n * 4 / ARR_CLOCK_TICKS])
            early++;                          /* before the clock that allows its tick */
        if (n > 0 && f == prev)
            bunched++;
        if (n >= 40 && n < 900) {             /* past the first two clocks' measuring */
            float d = (float)(f - prev);
            sum += d;
            lo = d < lo ? d : lo;
            hi = d > hi ? d : hi;
            wide += d < 100.f || d > 320.f;
        }
        prev = f;
        n++;
    }
    printf("  clock in at 120 BPM: %d notes, %.1f frames apart on average (918.75 / 5 = 183.75), %.0f..%.0f\n", n,
           sum / 860.f, lo, hi);
    CHECK(n >= 900, "the notes played (%d)", n);
    CHECK(early == 0, "none before its clock (%d)", early);
    CHECK(bunched == 0, "none on the same frame as the one before (%d)", bunched);
    CHECK(sum / 860.f > 180.f && sum / 860.f < 188.f && wide == 0, "spread evenly: %.1f frames apart, %d far off",
          sum / 860.f, wide);
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
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
    loops_reset();
    pattern_dense();
    arr_load_pattern(SA, &P);
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
    loops_reset();
    pat_new();
    for (int t = 3; t < 8; t++)               /* five tracks, a note every 3 ticks, 30 long: 50 held at once */
        for (int k = 0; k < 96; k += 3)
            add(t, k, 30, 48 + 3 * t + (k / 3) % 7);
    arr_load_pattern(SA, &P);
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
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
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
    loops_reset();
    pat_new();
    for (int t = 0; t < 96; t += 6)
        add(4, t, 3, 60);
    arr_load_pattern(SA, &P);
    arr_play(1);
    run_to(13000);                            /* past tick 12 */
    pat_new();                                /* track 4's events now after track 0's: a stale place would */
    for (int t = 0; t < 96; t += 6)           /* walk track 4 through track 0's kicks */
        add(0, t, 3, 36);
    for (int t = 3; t < 96; t += 6)
        add(4, t, 3, 72);
    n0 = stub_n;
    arr_load_pattern(SA, &P);
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
    loops_reset();
    pat_new();
    for (int t = 0; t < ARR_TRACKS; t++) {    /* 64 notes on tick 0 */
        P.trk[t].x.type = XT_BYPASS;
        for (int k = 0; k < 8; k++)
            add(t, 0, 12, 40 + 8 * t + k);
    }
    arr_load_pattern(SA, &P);
    arr_play(1);
    run_blocks(1);
    CHECK(count_ons(stub_log, 0, stub_n) == 48, "48 note-ons a block at most, got %d", count_ons(stub_log, 0, stub_n));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "capped: offs match");
    /* 20 clocks in one block: the position moves on by them (20 ticks each) at once, the first 4 clocks' notes
     * and the last clock's (paced) play */
    reset();
    loops_reset();
    pattern_dense();
    arr_load_pattern(SA, &P);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    t0 = arr_ticks;
    n0 = stub_n;
    for (int k = 0; k < 20; k++)
        arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    CHECK(arr_ticks == 21u * ARR_CLOCK_TICKS && arr_beat_tick == 21u * ARR_CLOCK_TICKS - 1u, "21 clocks' ticks played (%u, at %u)",
          arr_ticks, arr_beat_tick);
    CHECK(count_ons(stub_log, n0, stub_n) <= 5, "at most 5 played, got %d", count_ons(stub_log, n0, stub_n));
    (void)t0;
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
    loops_reset();
    pattern_busy_downbeat(4, 1);              /* 56 sounds (7 a track), FA, F8, 4 notes and their offs: fit 80 */
    arr_load_pattern(SA, &P);
    ring_cap = 80;
    ring_drain = 80;
    arr_midi_out_on(1);
    arr_play(1);
    run_to(44100 - 200);
    for (i = 0; i < 56; i++)
        ok &= is_sound(&out_log[i]);
    CHECK(ok && count_st(0, 56, 0xC0) == 8, "the 56 sounds first");
    CHECK(out_log[56].st == 0xFA && out_log[57].st == 0xF8, "then FA, then tick 0's F8 (%02X %02X)", out_log[56].st, out_log[57].st);
    CHECK(count_ons(out_log, 58, 62) == 4, "then tick 0's 4 notes");
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
    loops_reset();
    pattern_busy_downbeat(8, 2);              /* 56 sounds and 16 notes do not fit 80 at once: */
    arr_load_pattern(SA, &P);                  /* the sounds now, the start a block later */
    ring_cap = 80;                            /* usb.c's ring, a healthy host reading it every block */
    ring_drain = 80;
    arr_midi_out_on(1);
    arr_play(1);
    run_to(256 + 44100 - 200);
    for (i = 0; i < 56; i++)
        CHECK(is_sound(&out_log[i]) && out_log[i].frame == 0, "the 56 sounds first, in the first block");
    fa = find(out_log, out_n, 0, 0xFA, 0);
    CHECK(fa == 56 && out_log[fa].frame == 256, "FA a block later (%d at %u)", fa, fa >= 0 ? out_log[fa].frame : 0);
    CHECK(out_log[57].st == 0xF8 && count_ons(out_log, 58, 74) == 16, "then F8 and all 16 notes of tick 0");
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
    arr_load_pattern(SB, &P);
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
    loops_reset();
    pattern_busy_downbeat(8, 2);
    arr_load_pattern(SA, &P);
    ring_cap = 80;                            /* (56 sounds, some room for notes) */
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
    CHECK(dropped == 0 && offs_match(out_log, out_n), "nothing dropped, offs match (%d dropped)", dropped);
}

static void test_transport_out(void)
{
    int i, f8 = 0, fc = -1;
    reset();
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
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
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
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

/* every tick a note-on, at tempos whose ticks fall all over the blocks: each on its exact frame (+-ON_TOL) */
static void test_tempos(void)
{
    static const int bpm[] = {40, 97, 120, 133, 240};
    for (int b = 0; b < (int)(sizeof bpm / sizeof bpm[0]); b++) {
        static uint8_t seen_at[128];
        int ons = 0, worst = 0, places = 0, bad = 0;
        float fpt = 110250.f / (float)bpm[b];
        reset();
        loops_reset();
        pat_new();
        for (int t = 0; t < 96; t++)
            add(0, t, 1, 35 + t % 8);
        arr_load_pattern(SA, &P);
        arr_set_tempo(bpm[b]);
        arr_play(1);
        run_to((uint32_t)(192.f * fpt) + 256);   /* two bars */
        memset(seen_at, 0, sizeof seen_at);
        for (int i = 0; i < stub_n; i++) {
            float want, d;
            if (!is_on(&stub_log[i]) || stub_log[i].st != 0x99)
                continue;
            want = (float)ons * fpt;
            d = (float)stub_log[i].frame - want;
            if (d > ON_TOL || d < -ON_TOL)
                bad++;
            if ((int)(d < 0 ? -d : d) > worst)
                worst = (int)(d < 0 ? -d : d);
            seen_at[(stub_log[i].frame % 256u) / 2u] = 1;
            ons++;
        }
        for (int k = 0; k < 128; k++)
            places += seen_at[k];
        CHECK(ons >= 192, "%d BPM: every tick a note (%d)", bpm[b], ons);
        CHECK(bad == 0, "%d BPM: %d note-ons off their frame by more than %d (worst %d)", bpm[b], bad, ON_TOL, worst);
        CHECK(places > 20, "%d BPM: notes at %d places in the block", bpm[b], places);
        printf("  %3d BPM: %d note-ons, worst %d frames off, at %d places in the block\n", bpm[b], ons, worst, places);
    }
}

/* note lengths right on average: note-offs at the block start nearest their time, so a note's length is
 * within +-128 frames of its own and right on average; and a note-off never lands after a note-on of the same
 * note that follows it (the same note again, its off held for the next block, would end the new one).
 * Within 3 frames on average (block-start note-offs were 128 short): a tempo's ticks fall on a few dozen
 * places in the block, not all, so the rounding does not quite average out (97..240 BPM: 1.2 to 2.0) */
static void test_lengths(void)
{
    static const int bpm[] = {97, 120, 133, 240};
    for (int b = 0; b < 4; b++) {
        float fpt = 110250.f / (float)bpm[b], sum = 0.f, worst = 0.f;
        int n = 0, cut = 0;
        reset();
        loops_reset();
        pat_new();
        for (int t = 0; t < 96; t++)                /* track 0: lengths 1..5, eight notes round */
            add(0, t, 1 + t % 5, 35 + t % 8);
        for (int t = 0; t < 96; t += 2)             /* track 1: one note again and again, each ending as the next starts */
            add(1, t, 2, 60);
        arr_load_pattern(SA, &P);
        arr_set_tempo(bpm[b]);
        arr_play(1);
        run_to((uint32_t)(8 * 96 * fpt));          /* eight bars */
        arr_play(0);
        run_blocks(2);
        for (int i = 0; i < stub_n; i++) {
            int len;
            if (!is_on(&stub_log[i]) || stub_log[i].st != 0x99)
                continue;
            for (int j = i + 1; j < stub_n; j++) {
                if (stub_log[j].d1 != stub_log[i].d1 || (stub_log[j].st & 15) != 9)
                    continue;
                if (is_on(&stub_log[j])) {         /* the same note again before its off: cut */
                    cut++;
                    break;
                }
                len = (int)(stub_log[j].frame - stub_log[i].frame);
                if (stub_log[i].d1 != 60 && stub_log[j].frame < (uint32_t)(8 * 96 * fpt) - 512) {
                    /* track 0's: the nominal length from the tick (each note's index in the log is its tick) */
                    float want = (float)(1 + (int)((float)stub_log[i].frame / fpt + 0.5f) % 96 % 5) * fpt, d = (float)len - want;
                    sum += d;
                    if ((d < 0 ? -d : d) > worst)
                        worst = d < 0 ? -d : d;
                    n++;
                }
                break;
            }
        }
        printf("  %3d BPM: %d notes, length off by %.2f frames on average, %.0f at most\n", bpm[b], n, sum / (float)n, worst);
        CHECK(n > 600, "%d BPM: notes measured (%d)", bpm[b], n);
        CHECK(sum / (float)n < 3.f && sum / (float)n > -3.f, "%d BPM: lengths right on average (%.2f frames off)", bpm[b], sum / (float)n);
        CHECK(worst <= 128.f + ON_TOL, "%d BPM: each within half a block (%.0f)", bpm[b], worst);
        CHECK(cut == 0, "%d BPM: %d notes ended by a later note-off of their own", bpm[b], cut);
    }
}

/* ---- sections, songs, templates, live chords (Task 5) ---- */
/* the section slots' patterns: track 0 (drums, channel 10) a note every beat, its own for each slot (Intro 33,
 * A 36, B 38, Fill AB 40, Fill BA 41, Ending 42), `bars` bars long; track 3 (Parallel, channel 2) C3 every beat,
 * so its pitch minus 48 is the chord's root on that beat */
static const int SLOT_NOTE[ARR_NSECT] = {33, 36, 38, 40, 41, 42};
static void load_slot(int slot, int bars, int beats)
{
    pat_new();
    P.bars = (uint8_t)bars;
    P.beats = (uint8_t)beats;
    P.trk[3].x.type = XT_PARALLEL;
    for (int t = 0; t < bars * beats * 24; t += 24)
        add(0, t, 6, SLOT_NOTE[slot]);
    for (int t = 0; t < bars * beats * 24; t += 24)
        add(3, t, 6, 48);
    arr_load_pattern(slot, &P);
}
/* every slot but the Intro (empty: PLAY starts with Main A); the Ending two bars */
static void load_slots(int beats)
{
    for (int s = 0; s < ARR_NSECT; s++)
        if (s != SI)
            load_slot(s, s == SE ? 2 : 1, beats);
}
/* the drum note-on nearest `frame` (+-ON_TOL): which slot's, or -1 none */
static int slot_at(float frame)
{
    for (int i = 0; i < stub_n; i++)
        if (stub_log[i].st == 0x99 && is_on(&stub_log[i]) && near(stub_log, i, frame))
            for (int s = 0; s < ARR_NSECT; s++)
                if (stub_log[i].d1 == SLOT_NOTE[s])
                    return s;
    return -1;
}
/* the probe's root on the beat at `frame`: -1 no note there */
static int root_at(float frame)
{
    for (int i = 0; i < stub_n; i++)
        if (stub_log[i].st == 0x91 && is_on(&stub_log[i]) && near(stub_log, i, frame))
            return stub_log[i].d1 - 48;
    return -1;
}
static int ons_after(float frame)
{
    int n = 0;
    for (int i = 0; i < stub_n; i++)
        n += is_on(&stub_log[i]) && (float)stub_log[i].frame > frame;
    return n;
}
#define BAR (96 * FPT)

static void test_section_on_bar(void)
{
    reset();
    loops_reset();
    load_slots(4);
    arr_play(1);
    run_to((uint32_t)(0.4f * BAR));
    arr_section(SEC_B);
    run_blocks(1);
    CHECK(arr_next_sect == SEC_B && arr_sect == SEC_A, "B queued, A still playing (%d %d)", arr_next_sect, arr_sect);
    run_to((uint32_t)(BAR - 300));
    CHECK(arr_next_sect == SEC_B && arr_sect == SEC_A, "still A just before the bar line");
    CHECK(slot_at(3 * 24 * FPT) == SA, "beat 4 of bar 1 still A");
    run_to((uint32_t)(2 * BAR + 512));
    CHECK(slot_at(BAR) == SB, "B from the bar line (%d)", slot_at(BAR));
    CHECK(arr_sect == SEC_B && arr_next_sect == SEC_SAME, "B playing, nothing queued");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "sections: offs match");
}

static void test_fill(void)
{
    reset();
    loops_reset();
    load_slots(4);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_FILL);                    /* bar 2 the fill, bar 3 Main B */
    run_to((uint32_t)(2.5f * BAR));
    arr_section(SEC_FILL);                    /* bar 4 the fill, bar 5 Main A */
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(slot_at(0) == SA && slot_at(BAR) == SFA && slot_at(1.5f * BAR) == SFA, "A, then Fill AB (%d %d)", slot_at(0), slot_at(BAR));
    CHECK(slot_at(2 * BAR) == SB && slot_at(2.75f * BAR) == SB, "Fill AB goes to B (%d)", slot_at(2 * BAR));
    CHECK(slot_at(3 * BAR) == SFB, "from B: Fill BA (%d)", slot_at(3 * BAR));
    CHECK(slot_at(4 * BAR) == SA && slot_at(4.5f * BAR) == SA, "Fill BA goes to A (%d)", slot_at(4 * BAR));
    CHECK(arr_sect == SEC_A, "A playing");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "fills: offs match");
}

static void test_ending(void)
{
    reset();
    loops_reset();
    load_slots(4);
    pat_new();                                /* the ending: two bars, a long note held over its end */
    P.bars = 2;
    for (int t = 0; t < 192; t += 24)
        add(0, t, 6, 42);
    add(5, 96, 400, 60);
    arr_load_pattern(SE, &P);
    arr_play(1);
    run_to((uint32_t)(0.3f * BAR));
    arr_section(SEC_END);
    run_to((uint32_t)(3 * BAR - 512));
    CHECK(arr_playing == 1 && arr_sect == SEC_END, "the ending plays");
    CHECK(slot_at(BAR) == SE && slot_at(2.75f * BAR) == SE, "both its bars (%d %d)", slot_at(BAR), slot_at(2.75f * BAR));
    run_to((uint32_t)(4 * BAR));
    CHECK(arr_playing == 0, "then it stops");
    CHECK(ons_after(3 * BAR - 4) == 0, "nothing after its end (%d)", ons_after(3 * BAR - 4));
    CHECK(offs_match(stub_log, stub_n), "all notes off at the end (the long one too)");
    {
        int i = find(stub_log, stub_n, 0, 0x83, 60);
        CHECK(near(stub_log, i, 3 * BAR), "the long note ends with the ending, got %u", i >= 0 ? stub_log[i].frame : 0);
    }
    arr_play(1);                              /* played again: from Main A */
    run_blocks(2);
    CHECK(arr_playing == 1 && arr_sect == SEC_A, "PLAY again: Main A (%d)", arr_sect);
    arr_play(0);
    run_blocks(1);
}

/* ---- songs of rows */
static void loop_bars(int i, int beats, int n, const chord_t *c);
static void empty_slot(int slot);
static song_t SG;
static void song_rows(int n, const int *loop, const int *sect, const int *rep)
{
    memset(&SG, 0, sizeof SG);
    SG.nrows = (uint8_t)n;
    for (int r = 0; r < n; r++) {
        SG.row[r].loop = (uint8_t)loop[r];
        SG.row[r].sect = (uint8_t)sect[r];
        SG.row[r].rep = (uint8_t)rep[r];
    }
}
static void test_song(void)
{
    /* bar 1: Am7 for two beats, F on beat 3; bars 2-3: G7; bar 4: G7, Dm on beat 2 (each a one-bar loop) */
    static const int lo[4] = {0, 1, 1, 2}, se[4] = {ROW_A, ROW_A, ROW_B, ROW_B}, re[4] = {1, 1, 1, 1};
    static const int lo3[2] = {3, 3}, se3[2] = {ROW_B_FILL, ROW_A}, re3[2] = {2, 1};
    reset();
    loops_reset();
    load_slots(4);
    loop_clear(&LT[0], 0);
    loop_write(&LT[0], 0, ch(9, CH_MIN7));
    loop_write(&LT[0], 2, ch(5, CH_MAJ));
    loop_clear(&LT[1], 1);
    loop_write(&LT[1], 0, ch(7, CH_7));
    loop_clear(&LT[2], 2);
    loop_write(&LT[2], 0, ch(7, CH_7));
    loop_write(&LT[2], 1, ch(2, CH_MIN));
    song_rows(4, lo, se, re);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(5 * BAR));
    CHECK(root_at(0) == 9 && root_at(24 * FPT) == 9, "Am7, and no change keeps it (%d %d)", root_at(0), root_at(24 * FPT));
    CHECK(root_at(48 * FPT) == 5 && root_at(72 * FPT) == 5, "F on beat 3 (%d)", root_at(48 * FPT));
    CHECK(root_at(BAR) == 7 && root_at(2 * BAR) == 7, "G7 from bar 2 on (%d %d)", root_at(BAR), root_at(2 * BAR));
    CHECK(root_at(3 * BAR) == 7 && root_at(3 * BAR + 24 * FPT) == 2, "Dm on bar 4 beat 2 (%d)", root_at(3 * BAR + 24 * FPT));
    /* (rows now: the change A to B plays Fill AB in row 2's bar, by itself) */
    CHECK(slot_at(0) == SA && slot_at(BAR) == SFA && slot_at(2 * BAR) == SB && slot_at(3 * BAR) == SB,
          "the sections: A, Fill AB, B, B (%d %d %d %d)", slot_at(0), slot_at(BAR), slot_at(2 * BAR), slot_at(3 * BAR));
    CHECK(arr_playing == 0, "stopped at the end of its last row");
    CHECK(ons_after(4 * BAR - 4) == 0, "nothing after bar 4");
    CHECK(offs_match(stub_log, stub_n), "song: offs match");
    /* again: bar 5 is row 1 again */
    reset();
    loops_reset();
    load_slots(4);
    loop_clear(&LT[0], 0);
    loop_write(&LT[0], 0, ch(9, CH_MIN7));
    loop_write(&LT[0], 2, ch(5, CH_MAJ));
    loop_clear(&LT[1], 1);
    loop_write(&LT[1], 0, ch(7, CH_7));
    loop_clear(&LT[2], 2);
    loop_write(&LT[2], 0, ch(7, CH_7));
    loop_write(&LT[2], 1, ch(2, CH_MIN));
    song_rows(4, lo, se, re);
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(arr_playing == 1, "again: still playing");
    CHECK(slot_at(3 * BAR) == SFB, "row 4 before row 1 again (B to A): Fill BA (%d)", slot_at(3 * BAR));
    CHECK(slot_at(4 * BAR) == SA && root_at(4 * BAR) == 9, "row 1 again: A over Am7 (%d %d)", slot_at(4 * BAR), root_at(4 * BAR));
    CHECK(arr_row == 1, "the row for the screen: bar 6 is row 2 (%d)", arr_row);
    arr_play(0);
    run_blocks(1);
    /* a fill in the song: one bar, then the other Main */
    reset();
    loops_reset();
    load_slots(4);
    song_rows(2, lo3, se3, re3);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(3 * BAR + 512));
    CHECK(slot_at(0) == SB && slot_at(BAR) == SFB && slot_at(2 * BAR) == SA, "B, Fill BA, A (%d %d %d)", slot_at(0), slot_at(BAR), slot_at(2 * BAR));
    CHECK(arr_playing == 0, "stopped");
    /* no song: back to free play */
    reset();
    loops_reset();
    load_slots(4);
    arr_mode_song(&SG, 0);
    arr_mode_free();
    arr_play(1);
    run_to((uint32_t)(4 * BAR));
    CHECK(arr_playing == 1 && slot_at(3 * BAR) == SA && arr_mode == ARR_FREE, "song off: Main A loops");
    arr_play(0);
    run_blocks(1);
}

/* a pattern shortened while it plays, its place past the new end: it wraps within the new length and keeps
 * the bar's rhythm, so no bar line comes early and the song's rows stay on time */
static void test_shorten(void)
{
    static const int lo[4] = {0, 1, 2, 3}, se[4] = {ROW_A, ROW_A, ROW_A, ROW_A}, re[4] = {1, 1, 1, 1};
    reset();
    loops_reset();
    load_slot(SA, 2, 4);
    for (int b = 0; b < 4; b++) {
        loop_clear(&LT[b], b);
        loop_write(&LT[b], 0, ch(b + 1, CH_MAJ));  /* row n: root n */
    }
    song_rows(4, lo, se, re);
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(BAR + 30 * FPT));         /* bar 2, beat 2 */
    {
        uint16_t d = arr_pattern_copy(SA, &P);
        P.bars = 1;
        CHECK(arr_store_pattern(SA, &P, d), "shortened");
    }
    run_to((uint32_t)(4 * BAR));
    CHECK(root_at(BAR + 48 * FPT) == 2 && root_at(BAR + 72 * FPT) == 2, "bar 2 plays on to its end (%d %d)",
          root_at(BAR + 48 * FPT), root_at(BAR + 72 * FPT));
    CHECK(root_at(2 * BAR) == 3 && root_at(3 * BAR) == 4, "rows 3 and 4 on time (%d %d)", root_at(2 * BAR),
          root_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
}

/* the table being written (the main loop interrupted midway) as a beat comes: the beat's chord is not lost, it
 * comes at the first block after the write; the edit plays from its beat; a row change during a write is on
 * time (the render's snapshot of the rows), its chord at the first block after. (The old songs' staged copy
 * played a beat's chord on time; the guarantee now is "the first block after the write", as for loops.) */
static void test_song_busy(void)
{
    static const int lo[1] = {0}, se[1] = {ROW_A}, re[1] = {1};
    static const chord_t c[1] = {{0, CH_MAJ, CH_NOBASS}}, g[1] = {{7, CH_MAJ, CH_NOBASS}};
    static const int lo2[2] = {0, 1}, se2[2] = {ROW_A, ROW_B}, re2[2] = {1, 1};
    reset();
    loops_reset();
    load_slots(4);
    loop_clear(&LT[0], 0);
    for (int i = 0; i < 8; i++)
        loop_write(&LT[0], i, ch(i + 1, CH_MAJ));  /* beat n: root n + 1 */
    song_rows(1, lo, se, re);
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(36 * FPT));             /* beat 2, halfway */
    arr_edit(1);
    loop_write(&LT[0], 3, ch(11, CH_MAJ));    /* (what the main loop is writing) */
    run_to((uint32_t)(60 * FPT));             /* beat 3 passes */
    CHECK(arr_chord_unpack(arr_chord_now).root == 2, "busy: beat 2's chord still (%d)", arr_chord_unpack(arr_chord_now).root);
    arr_edit(0);
    run_blocks(1);
    CHECK(arr_chord_unpack(arr_chord_now).root == 3, "the beat during the write: its chord at the first block after (%d)",
          arr_chord_unpack(arr_chord_now).root);
    run_to((uint32_t)(2 * BAR));
    CHECK(root_at(72 * FPT) == 11, "the edit, once written, from its beat (%d)", root_at(72 * FPT));
    CHECK(root_at(BAR) == 5, "on (%d)", root_at(BAR));
    arr_play(0);
    run_blocks(1);
    /* busy across a row change: the section on time, the row's chord at the first block after */
    reset();
    loops_reset();
    load_slots(4);
    empty_slot(SFA);
    empty_slot(SFB);
    loop_bars(0, 4, 1, c);
    loop_bars(1, 4, 1, g);
    song_rows(2, lo2, se2, re2);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(BAR - 300));
    arr_edit(1);
    run_to((uint32_t)(BAR + 300));
    CHECK(arr_chord_unpack(arr_chord_now).root == 0 && arr_row == 1 && slot_at(BAR) == SB, "busy: row 2 and Main B on time, C still (%d %d %d)",
          arr_chord_unpack(arr_chord_now).root, arr_row, slot_at(BAR));
    arr_edit(0);
    run_blocks(1);
    CHECK(arr_chord_unpack(arr_chord_now).root == 7, "released: row 2's G at once");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "song busy: offs match");
}

static void test_song_rows(void)
{
    static const chord_t am[2] = {{9, CH_MIN, CH_NOBASS}, {5, CH_MAJ, CH_NOBASS}}, cg[1] = {{0, CH_MAJ, CH_NOBASS}};
    static const int lo[2] = {0, 1}, se[2] = {ROW_A, ROW_B}, re[2] = {2, 1};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 2, am);                        /* L1: Am | F */
    loop_bars(1, 4, 1, cg);                        /* L2: C */
    song_rows(2, lo, se, re);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(2.5f * BAR));
    CHECK(arr_row == 0 && arr_rep == 1 && arr_loop_now == 0, "row 1, its second pass (%d %d %d)", arr_row, arr_rep, arr_loop_now);
    run_to((uint32_t)(6 * BAR + 512));
    CHECK(root_at(0) == 9 && root_at(BAR) == 5 && root_at(2 * BAR) == 9 && root_at(3 * BAR) == 5, "L1 twice");
    CHECK(slot_at(0) == SA && slot_at(2 * BAR) == SA && slot_at(3 * BAR) == SFA, "A, A, A, Fill AB in the row's last bar (%d)", slot_at(3 * BAR));
    CHECK(slot_at(4 * BAR) == SB && root_at(4 * BAR) == 0, "row 2: Main B over C");
    CHECK(arr_playing == 0, "the stop after row 2 (Intro / Ending off)");
    CHECK(offs_match(stub_log, stub_n), "rows: offs match");
    /* one-bar rows A, B, again, without fills: each row its own section (no fill to take its bar) */
    reset();
    loops_reset();
    load_slots(4);
    empty_slot(SFA);
    empty_slot(SFB);
    loop_bars(0, 4, 2, am);
    loop_bars(1, 4, 1, cg);
    song_rows(2, lo, se, re);
    SG.row[0].rep = 1;
    SG.row[0].loop = 1;
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(3 * BAR + 512));
    CHECK(slot_at(0) == SA && slot_at(BAR) == SB && slot_at(2 * BAR) == SA && slot_at(3 * BAR) == SB, "A, B, A, B (%d %d %d %d)",
          slot_at(0), slot_at(BAR), slot_at(2 * BAR), slot_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
}
static void test_song_forced_fill_mutes_key(void)
{
    static const chord_t c[1] = {{0, CH_MAJ, CH_NOBASS}};
    static const int lo[2] = {0, 0}, se[2] = {ROW_A_FILL, ROW_A}, re[2] = {2, 1};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, c);
    song_rows(2, lo, se, re);
    SG.row[0].mute = 1u << 3;                      /* row 1: the probe's track (3) muted */
    SG.row[1].key = 2;                             /* row 2: a tone up */
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    CHECK(arr_mutes == 1u << 3 && root_at(0) == -1, "row 1: track 3 muted (no probe note)");
    arr_track_mute(3, 0);                          /* by hand: on again, until the next row */
    run_to((uint32_t)(1.5f * BAR));
    CHECK(root_at(BAR) == 0 && slot_at(BAR) == SFA && arr_mutes == 0, "unmuted by hand; a forced fill in row 1's last bar");
    run_to((uint32_t)(2.5f * BAR));
    CHECK(arr_row == 1 && slot_at(2 * BAR) == SA && root_at(2 * BAR) == 2 && arr_mutes == 0, "row 2: A, D (key +2), no mutes");
    arr_set_transpose(-1);                         /* the transpose and the key add: C# */
    run_to((uint32_t)(2.9f * BAR));
    CHECK(root_at(2.75f * BAR) == 1, "transpose -1, key +2: C# (%d)", root_at(2.75f * BAR));
    arr_track_mute(5, 1);                          /* a hand mute: until the next row */
    run_blocks(1);
    CHECK(arr_mutes == 1u << 5, "the hand mute shown (%02x)", arr_mutes);
    run_to((uint32_t)(3.5f * BAR));
    CHECK(arr_row == 0 && arr_mutes == 1u << 3 && arr_playing, "again: row 1, its mute back, the hand mute gone (%02x)", arr_mutes);
    CHECK(root_at(3.25f * BAR) == -1, "muted again");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "mutes: offs match");
}
static void test_song_intro_ending(void)
{
    static const chord_t a[1] = {{9, CH_MIN, CH_NOBASS}}, g[1] = {{7, CH_MAJ, CH_NOBASS}};
    static const int lo[2] = {0, 1}, se[2] = {ROW_A, ROW_A}, re[2] = {1, 1};
    reset();
    loops_reset();
    load_slot(SI, 1, 4);
    load_slots(4);
    loop_bars(0, 4, 1, a);
    loop_bars(1, 4, 1, g);
    song_rows(2, lo, se, re);
    SG.intro = SG.ending = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(1.5f * BAR));
    CHECK(arr_row == 0 && arr_sect == SEC_A, "row 1 after the Intro (%d %d)", arr_row, arr_sect);
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(slot_at(0) == SI && root_at(0) == 9, "the Intro over row 1's Am");
    CHECK(slot_at(BAR) == SA && root_at(BAR) == 9 && slot_at(2 * BAR) == SA && root_at(2 * BAR) == 7, "then row 1 (A, Am), row 2 (A, G)");
    CHECK(slot_at(3 * BAR) == SE && root_at(3 * BAR) == 7 && root_at(4 * BAR) == 7, "the Ending over the last chord, G");
    CHECK(arr_playing == 0 && ons_after(5 * BAR - 4) == 0, "then the stop");
    CHECK(offs_match(stub_log, stub_n), "intro, ending: offs match");
    /* from row 2 while playing: at the next bar line (no Intro) */
    reset();
    loops_reset();
    load_slot(SI, 1, 4);
    load_slots(4);
    loop_bars(0, 4, 1, a);
    loop_bars(1, 4, 1, g);
    loop_bars(2, 4, 1, a);
    song_rows(2, lo, se, re);
    SG.intro = SG.ending = 1;
    arr_mode_loop(2);
    arr_play(1);
    run_to((uint32_t)(1.5f * BAR));
    arr_mode_song(&SG, 1);
    run_blocks(1);
    CHECK(arr_mode == ARR_SONG && arr_loop_now == 2, "asked: shown, the loop plays on to the bar line");
    run_to((uint32_t)(2.5f * BAR));
    CHECK(arr_mode == ARR_SONG && arr_row == 1 && root_at(2 * BAR) == 7 && slot_at(2 * BAR) == SA, "row 2 from the bar line");
    arr_play(0);
    run_blocks(1);
    /* stopped: PLAY starts at row r (not 0: no Intro) */
    reset();
    loops_reset();
    load_slot(SI, 1, 4);
    load_slots(4);
    loop_bars(0, 4, 1, a);
    loop_bars(1, 4, 1, g);
    song_rows(2, lo, se, re);
    SG.intro = 1;
    SG.row[1].sect = ROW_B;
    arr_mode_song(&SG, 1);
    run_blocks(1);
    stub_n = 0;
    stub_frame = 0;
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    CHECK(arr_row == 1 && slot_at(0) == SB && root_at(0) == 7, "PLAY at row 2: B over G, no Intro (%d %d %d)", arr_row, slot_at(0), root_at(0));
    arr_play(0);
    run_blocks(1);
    /* again: row 1 again, no Intro */
    reset();
    loops_reset();
    load_slot(SI, 1, 4);
    load_slots(4);
    loop_bars(0, 4, 1, a);
    song_rows(1, lo, se, re);
    SG.intro = SG.ending = SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(3.5f * BAR));
    CHECK(slot_at(0) == SI && slot_at(BAR) == SA && slot_at(2 * BAR) == SA && slot_at(3 * BAR) == SA && arr_playing,
          "Intro, row 1, row 1 again, again: no Intro, no Ending (%d %d %d)", slot_at(BAR), slot_at(2 * BAR), slot_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
}
/* ---- songs: review fixes */
static void test_song_ending_wins(void)       /* the song's end wins over a section by hand */
{
    static const chord_t c[1] = {{0, CH_MAJ, CH_NOBASS}};
    static const int lo[2] = {0, 0}, se[2] = {ROW_A, ROW_A}, re[2] = {1, 1};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, c);
    song_rows(2, lo, se, re);
    SG.ending = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(1.5f * BAR));
    arr_section(SEC_B);                            /* in the last row: the Ending still comes */
    run_to((uint32_t)(5 * BAR));
    CHECK(slot_at(BAR) == SA && slot_at(2 * BAR) == SE && slot_at(3 * BAR) == SE, "A, A, E, E (%d %d %d)", slot_at(BAR),
          slot_at(2 * BAR), slot_at(3 * BAR));
    CHECK(arr_playing == 0 && ons_after(4 * BAR - 4) == 0, "then the stop");
    /* a Main by hand during the song's Ending: ignored, it ends */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, c);
    song_rows(2, lo, se, re);
    SG.ending = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(2.5f * BAR));
    arr_section(SEC_A);
    run_blocks(1);
    CHECK(arr_next_sect == SEC_SAME, "a Main in the song's Ending: ignored (%d)", arr_next_sect);
    run_to((uint32_t)(5 * BAR));
    CHECK(slot_at(3 * BAR) == SE && arr_playing == 0 && ons_after(4 * BAR - 4) == 0, "the Ending plays out and stops (%d)", slot_at(3 * BAR));
    CHECK(offs_match(stub_log, stub_n), "offs match");
}
static void test_song_start_busy(void)        /* PLAY during a write: a block later, with the new song's rows */
{
    static const int lo[1] = {0}, se[1] = {ROW_A}, re[1] = {1};
    static song_t S1;
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 1, 4);
    song_rows(1, lo, se, re);
    SG.row[0].mute = 0xFF;
    S1 = SG;                                       /* song 1: everything muted */
    arr_mode_song(&S1, 0);
    arr_play(1);
    run_blocks(4);
    arr_play(0);
    run_blocks(1);
    song_rows(1, lo, se, re);                      /* song 2: nothing muted, no Intro */
    arr_mode_song(&SG, 0);
    arr_edit(1);
    arr_play(1);
    run_blocks(1);
    arr_edit(0);
    run_blocks(2);
    CHECK(arr_playing && arr_mutes == 0 && arr_sect == SEC_A, "song 2's row 1: no mutes, Main A, no Intro (%02x %d)", arr_mutes, arr_sect);
    arr_play(0);
    run_blocks(1);
    /* the first song ever picked, PLAY during a write: its Intro off is honoured */
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 1, 4);
    song_rows(1, lo, se, re);
    arr_mode_song(&SG, 0);
    arr_edit(1);
    arr_play(1);
    run_blocks(1);
    arr_edit(0);
    run_blocks(2);
    CHECK(arr_playing && arr_sect == SEC_A, "Intro off: Main A (%d)", arr_sect);
    arr_play(0);
    run_blocks(1);
}
static void test_song_busy_fill(void)         /* a write across a row change: the next row's fill still plays */
{
    static const chord_t c[1] = {{0, CH_MAJ, CH_NOBASS}};
    static const int lo[3] = {0, 0, 0}, se[3] = {ROW_A, ROW_A, ROW_B}, re[3] = {1, 1, 1};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, c);
    song_rows(3, lo, se, re);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(BAR - 300));
    arr_edit(1);
    run_to((uint32_t)(BAR + 300));
    arr_edit(0);
    run_to((uint32_t)(3 * BAR + 512));
    CHECK(slot_at(0) == SA && slot_at(BAR) == SFA && slot_at(2 * BAR) == SB, "A, Fill AB (row 2's only bar), B (%d %d %d)", slot_at(0),
          slot_at(BAR), slot_at(2 * BAR));
}
static void test_song_row_edit(void)          /* the playing row's key or loop changed: heard at the next beat */
{
    static const chord_t c[1] = {{0, CH_MAJ, CH_NOBASS}}, g[1] = {{7, CH_MAJ, CH_NOBASS}};
    static const int lo[1] = {0}, se[1] = {ROW_A}, re[1] = {2};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, c);
    loop_bars(1, 4, 1, g);
    song_rows(1, lo, se, re);
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(0.3f * BAR));
    arr_edit(1);
    SG.row[0].key = 2;
    arr_edit(0);
    run_to((uint32_t)(0.8f * BAR));
    CHECK(root_at(0.5f * BAR) == 2, "key +2: D from the next beat (%d)", root_at(0.5f * BAR));
    run_to((uint32_t)(1.3f * BAR));
    arr_edit(1);
    SG.row[0].loop = 1;
    arr_edit(0);
    run_to((uint32_t)(1.8f * BAR));
    CHECK(root_at(1.5f * BAR) == 9, "loop G, key +2: A from the next beat (%d)", root_at(1.5f * BAR));
    arr_play(0);
    run_blocks(1);
}
static void test_song_odd_bars(void)          /* a 3-beat loop over a 4-beat pattern: rows end on the pattern's bar lines */
{
    static const int lo[2] = {0, 1}, se[2] = {ROW_A, ROW_A}, re[2] = {2, 1};
    static const chord_t g[1] = {{7, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(4);
    loop_clear(&LT[0], 0);
    LT[0].beats = 3;
    loop_write(&LT[0], 0, ch(9, CH_MIN));          /* Am F . (3 beats) */
    loop_write(&LT[0], 1, ch(5, CH_MAJ));
    loop_bars(1, 4, 1, g);
    song_rows(2, lo, se, re);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(2.5f * BAR));
    CHECK(root_at(0.75f * BAR) == 9 && root_at(BAR) == 5 && root_at(1.5f * BAR) == 9 && root_at(1.75f * BAR) == 5,
          "the loop goes round in its 3 beats (%d %d %d %d)", root_at(0.75f * BAR), root_at(BAR), root_at(1.5f * BAR), root_at(1.75f * BAR));
    CHECK(root_at(2 * BAR) == 7 && arr_row == 1, "6 beats rounded up to 2 bars: row 2 at bar 3 (%d %d)", root_at(2 * BAR), arr_row);
    arr_play(0);
    run_blocks(1);
}
static void test_transpose_busy(void)         /* a transpose during a write: the first block after */
{
    static const chord_t c2[2] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}};
    static const int lo[1] = {0}, se[1] = {ROW_A}, re[1] = {1};
    for (int song = 0; song < 2; song++) {
        reset();
        loops_reset();
        load_slots(4);
        loop_bars(0, 4, 2, c2);
        song_rows(1, lo, se, re);
        if (song)
            arr_mode_song(&SG, 0);
        else
            arr_mode_loop(0);
        arr_play(1);
        run_to((uint32_t)(0.3f * BAR));
        arr_edit(1);
        arr_set_transpose(2);
        run_blocks(2);
        CHECK(arr_chord_unpack(arr_chord_now).root == 0, "busy: C still (%d)", song);
        arr_edit(0);
        run_blocks(1);
        CHECK(arr_chord_unpack(arr_chord_now).root == 2, "released: D at once (%d: %d)", song, arr_chord_unpack(arr_chord_now).root);
        arr_play(0);
        run_blocks(1);
    }
    /* in a held Intro: the held chord moves (not the loop's chord now) */
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 2, 4);
    loop_bars(0, 4, 2, c2);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(1.3f * BAR));
    arr_edit(1);
    arr_set_transpose(2);
    run_blocks(1);
    arr_edit(0);
    run_blocks(1);
    CHECK(arr_chord_unpack(arr_chord_now).root == 2, "the Intro's C, a tone up (%d)", arr_chord_unpack(arr_chord_now).root);
    arr_play(0);
    run_blocks(1);
}
static void test_song_more(void)
{
    static const chord_t g[1] = {{7, CH_MAJ, CH_NOBASS}}, c[1] = {{0, CH_MAJ, CH_NOBASS}};
    static const int lo[2] = {5, 1}, se[2] = {ROW_A, ROW_A}, re[2] = {1, 1};
    static const int lo3[3] = {0, 0, 0}, se3[3] = {ROW_A, ROW_A, ROW_A}, re3[3] = {1, 2, 1};
    /* a row of an empty loop: a bar of 4, the chord unchanged */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(1, 4, 1, g);
    song_rows(2, lo, se, re);
    arr_set_chord(ch(4, CH_MAJ));
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(1.5f * BAR));
    CHECK(root_at(0) == 4 && root_at(0.75f * BAR) == 4 && root_at(BAR) == 7 && arr_row == 1, "empty loop: E for a bar, then G (%d %d %d)",
          root_at(0), root_at(0.75f * BAR), root_at(BAR));
    arr_play(0);
    run_blocks(1);
    /* a section by hand queued into a row's start: that row plays it (to its end), the next its own */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, c);
    song_rows(3, lo3, se3, re3);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_B);
    run_to((uint32_t)(3.5f * BAR));
    CHECK(slot_at(BAR) == SB && slot_at(2 * BAR) == SFB && slot_at(3 * BAR) == SA, "row 2: B by hand, its last bar Fill BA, row 3: A (%d %d %d)",
          slot_at(BAR), slot_at(2 * BAR), slot_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
    /* a switch to SONG waiting for its bar line: its row shown */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, c);
    song_rows(3, lo3, se3, re3);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_mode_song(&SG, 2);
    run_blocks(1);
    CHECK(arr_mode == ARR_SONG && arr_row == 2, "waiting: row 3 shown (%d)", arr_row);
    arr_play(0);
    run_blocks(1);
}
/* a section or a fill by hand lasts until the next row */
static void test_song_hand(void)
{
    static const chord_t c[1] = {{0, CH_MAJ, CH_NOBASS}};
    static const int lo[2] = {0, 0}, se[2] = {ROW_A, ROW_A}, re[2] = {2, 3};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, c);
    song_rows(2, lo, se, re);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_B);
    run_to((uint32_t)(2.5f * BAR));
    arr_section(SEC_FILL);                         /* row 2's first bar: the fill from its second, then B */
    run_to((uint32_t)(5.5f * BAR));
    CHECK(slot_at(0) == SA && slot_at(BAR) == SB && slot_at(2 * BAR) == SA, "A, B by hand, row 2: A (%d %d %d)", slot_at(0),
          slot_at(BAR), slot_at(2 * BAR));
    CHECK(slot_at(3 * BAR) == SFA && slot_at(4 * BAR) == SB, "a fill by hand: one bar, then B to the row's end (%d %d)",
          slot_at(3 * BAR), slot_at(4 * BAR));
    CHECK(arr_playing == 0, "the stop after row 2");
    CHECK(offs_match(stub_log, stub_n), "hand: offs match");
}

/* loop i: n chords, one a bar of `beats` */
static void loop_bars(int i, int beats, int n, const chord_t *c)
{
    loop_clear(&LT[i], i);
    LT[i].beats = (uint8_t)beats;
    for (int b = 0; b < n; b++)
        loop_write(&LT[i], b * beats, c[b]);
}
static void test_loop(void)
{
    static const chord_t pop[4] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}, {9, CH_MIN, CH_NOBASS}, {5, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(2, 4, 4, pop);
    arr_set_transpose(2);                         /* D A Bm G */
    arr_mode_loop(2);
    arr_play(1);
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(root_at(0) == 2 && root_at(BAR) == 9 && root_at(2 * BAR) == 11 && root_at(3 * BAR) == 7, "D A Bm G (%d %d %d %d)",
          root_at(0), root_at(BAR), root_at(2 * BAR), root_at(3 * BAR));
    CHECK(root_at(4 * BAR) == 2 && root_at(5 * BAR) == 9, "round again");
    CHECK(arr_mode == ARR_LOOP && arr_loop_now == 2 && arr_loop_beat == 4, "for the screen (%d %d %d)", arr_mode, arr_loop_now, arr_loop_beat);
    arr_set_transpose(0);
    run_to((uint32_t)(5.5f * BAR + 512));
    CHECK(root_at(5.5f * BAR) == 7, "transpose 0: G (%d)", root_at(5.5f * BAR));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "loop: offs match");
}
static void test_loop_waltz(void)
{
    static const chord_t w[4] = {{0, CH_MAJ, CH_NOBASS}, {0, CH_MAJ, CH_NOBASS}, {7, CH_7, CH_NOBASS}, {0, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(3);
    loop_bars(0, 3, 4, w);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(4 * 72 * FPT + 512));
    CHECK(root_at(2 * 72 * FPT) == 7 && root_at(3 * 72 * FPT) == 0 && root_at(4 * 72 * FPT) == 0, "a chord a 3-beat bar");
    arr_play(0);
    run_blocks(1);
}
static void test_loop_switch(void)                /* picked while playing: from the next bar line */
{
    static const chord_t a[1] = {{9, CH_MIN, CH_NOBASS}}, f[1] = {{5, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, a);
    loop_bars(1, 4, 1, f);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(1.3f * BAR));
    arr_mode_loop(1);
    run_to((uint32_t)(3 * BAR + 512));
    CHECK(root_at(1.5f * BAR) == 9 && root_at(2 * BAR) == 5, "Am to the bar line, then F (%d %d)", root_at(1.5f * BAR), root_at(2 * BAR));
    arr_mode_loop(4);                              /* an empty loop: the chord stays */
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(root_at(4 * BAR) == 5 && arr_loop_now == 4, "empty loop: F stays");
    arr_mode_free();
    run_to((uint32_t)(6 * BAR + 512));
    CHECK(arr_mode == ARR_FREE && arr_loop_now == -1 && root_at(6 * BAR) == 5, "FREE: the chord stays");
    arr_play(0);
    run_blocks(1);
}
static void test_loop_override(void)              /* a played chord holds until the loop's next change */
{
    static const chord_t c4[2] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}};
    static const uint8_t f[] = {53, 57, 60};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 2, c4);                        /* C | G */
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(0.3f * BAR));
    CHECK(arr_keys_held(f, 3) == 1, "F played");
    run_to((uint32_t)(0.9f * BAR));
    CHECK(root_at(0.5f * BAR) == 5 && arr_override == 1, "F holds (%d), override shown", root_at(0.5f * BAR));
    run_to((uint32_t)(1.1f * BAR));
    CHECK(root_at(BAR) == 7 && arr_override == 0, "the loop's G takes over (%d)", root_at(BAR));
    arr_play(0);
    run_blocks(1);
}
static void test_loop_busy(void)                  /* the table busy on a beat line: its chord a block late */
{
    static const chord_t c2[2] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 2, c2);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(BAR - 300));
    arr_edit(1);
    run_to((uint32_t)(BAR + 300));
    CHECK(arr_chord_unpack(arr_chord_now).root == 0, "busy: C still");
    arr_edit(0);
    run_blocks(1);
    CHECK(arr_chord_unpack(arr_chord_now).root == 7, "released: G at once");
    arr_play(0);
    run_blocks(1);
}
static void test_transpose_held(void)            /* a transpose moves the chord sounding, never looks the loop up */
{
    static const chord_t c2[2] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}};
    static const uint8_t f[] = {53, 57, 60};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 2, c2);                        /* C | G */
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(0.05f * BAR));
    arr_keys_held(f, 3);                           /* F on beat 0, the loop's C beat */
    run_to((uint32_t)(0.15f * BAR));
    arr_set_transpose(0);                          /* no-op: the keys' F stays */
    run_to((uint32_t)(0.6f * BAR));
    CHECK(root_at(0.25f * BAR) == 5 && root_at(0.5f * BAR) == 5 && arr_override == 1, "transpose 0 over the keys' F: F (%d %d %d)",
          root_at(0.25f * BAR), root_at(0.5f * BAR), arr_override);
    arr_play(0);
    run_blocks(1);
    /* a 2-bar Intro over C | G: transposed in its bar 2, the held C moves to D (not the loop's A) */
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 2, 4);
    loop_bars(0, 4, 2, c2);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(1.6f * BAR));
    arr_set_transpose(2);
    run_to((uint32_t)(2 * BAR + 512));
    CHECK(arr_sect == SEC_A, "Main A after the Intro");
    CHECK(root_at(1.75f * BAR) == 2, "the Intro's C, a tone up: D (%d)", root_at(1.75f * BAR));
    CHECK(root_at(2 * BAR) == 2, "Main A: the loop's C, a tone up (%d)", root_at(2 * BAR));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "transpose: offs match");
}
/* LOOP under an Intro: the Intro holds the loop's beat-0 chord; the loop starts at its beat 0 when Main A does */
static void test_loop_intro(void)
{
    static const chord_t pop[4] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}, {9, CH_MIN, CH_NOBASS}, {5, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 2, 4);
    loop_bars(0, 4, 4, pop);                       /* C | G | Am | F */
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(3.5f * BAR));
    CHECK(slot_at(0) == SI && root_at(0) == 0 && root_at(BAR) == 0 && root_at(1.75f * BAR) == 0, "the Intro holds C (%d %d)",
          root_at(BAR), root_at(1.75f * BAR));
    CHECK(slot_at(2 * BAR) == SA && root_at(2 * BAR) == 0 && root_at(3 * BAR) == 7, "Main A from the loop's beat 0: C, then G (%d %d)",
          root_at(2 * BAR), root_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
    /* an Intro by hand while the loop plays (on its G): it holds C, then the loop from its beat 0 */
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 1, 4);
    loop_bars(0, 4, 4, pop);
    arr_section(SEC_A);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(1.5f * BAR));
    arr_section(SEC_INTRO);
    run_to((uint32_t)(4.5f * BAR));
    CHECK(root_at(BAR) == 7 && slot_at(2 * BAR) == SI && root_at(2 * BAR) == 0 && root_at(2.75f * BAR) == 0,
          "G, then the Intro over the loop's C (%d %d)", root_at(BAR), root_at(2 * BAR));
    CHECK(slot_at(3 * BAR) == SA && root_at(3 * BAR) == 0 && root_at(4 * BAR) == 7, "Main A from the loop's beat 0: C, G (%d %d)",
          root_at(3 * BAR), root_at(4 * BAR));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "loop intro: offs match");
}
static void test_loop_busy_override(void)       /* a change missed while busy ends the keys' chord at release */
{
    static const chord_t c2[2] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}};
    static const uint8_t f[] = {53, 57, 60};
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 2, c2);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_keys_held(f, 3);
    run_to((uint32_t)(0.9f * BAR));
    arr_edit(1);                                   /* busy across G's bar line, and the beat after */
    run_to((uint32_t)(1.3f * BAR));
    CHECK(arr_chord_unpack(arr_chord_now).root == 5, "busy: F still");
    arr_edit(0);
    run_blocks(1);
    CHECK(arr_chord_unpack(arr_chord_now).root == 7 && arr_override == 0, "released: the missed G, override off (%d %d)",
          arr_chord_unpack(arr_chord_now).root, arr_override);
    arr_play(0);
    run_blocks(1);
}
static void test_loop_edges(void)
{
    static const chord_t pop[4] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}, {9, CH_MIN, CH_NOBASS}, {5, CH_MAJ, CH_NOBASS}};
    /* beat 0 without an entry: PLAY looks round from the end (Am on beat 2 sounds from beat 0) */
    reset();
    loops_reset();
    load_slots(4);
    loop_clear(&LT[3], 3);
    loop_write(&LT[3], 2, ch(9, CH_MIN));
    arr_mode_loop(3);
    arr_play(1);
    run_to((uint32_t)(0.6f * BAR));
    CHECK(root_at(0) == 9 && root_at(0.25f * BAR) == 9, "beat 0 empty: the loop's Am from the start (%d)", root_at(0));
    arr_play(0);
    run_blocks(1);
    /* shortened while playing (2 of 4 bars): it wraps at its new end */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 4, pop);
    arr_mode_loop(0);
    arr_play(1);
    run_to((uint32_t)(1.5f * BAR));
    arr_edit(1);
    LT[0].bars = 2;
    arr_edit(0);
    run_to((uint32_t)(3 * BAR + 512));
    CHECK(root_at(BAR) == 7 && root_at(2 * BAR) == 0 && root_at(3 * BAR) == 7, "C G | C G (%d %d %d)", root_at(BAR),
          root_at(2 * BAR), root_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
    /* picked while stopped, then PLAY: from its beat 0; out of range: ignored */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(1, 4, 4, pop);
    loop_bars(0, 4, 1, pop + 3);
    arr_mode_loop(1);
    arr_mode_loop(ARR_LOOPS);                      /* ignored (not loop 0) */
    arr_play(1);
    run_to((uint32_t)(BAR + 512));
    CHECK(arr_loop_now == 1 && root_at(0) == 0 && root_at(BAR) == 7, "stopped, then PLAY: loop 2 from beat 0 (%d %d %d)",
          arr_loop_now, root_at(0), root_at(BAR));
    arr_mode_loop(4);                              /* an empty loop: its beat shown 0 */
    run_to((uint32_t)(2 * BAR + 512));
    CHECK(arr_loop_now == 4 && arr_loop_beat == 0, "empty loop: beat 0 (%d %d)", arr_loop_now, arr_loop_beat);
    arr_play(0);
    run_blocks(1);
    /* picked while stopped, then MIDI Continue: its chord at the next beat line, though that beat has no entry */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, pop + 2);                   /* Am, one bar */
    arr_set_chord(ch(4, CH_MAJ));
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    for (int k = 0; k < 36; k++) {                 /* a beat and a half */
        arr_clock(ARR_CLK_TICK);
        run_blocks(2);
    }
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
    arr_mode_loop(0);
    run_blocks(1);
    arr_clock(ARR_CLK_CONTINUE);
    for (int k = 0; k < 24; k++) {
        arr_clock(ARR_CLK_TICK);
        run_blocks(2);
    }
    CHECK(arr_chord_unpack(arr_chord_now).root == 9, "Continue into the loop: Am (%d)", arr_chord_unpack(arr_chord_now).root);
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
}
static void test_live_chords(void)
{
    static const uint8_t am[] = {57, 60, 64}, e7[] = {52, 56, 59, 62}, two[] = {60, 64};
    static const chord_t pop[4] = {{0, CH_MAJ, CH_NOBASS}, {7, CH_MAJ, CH_NOBASS}, {9, CH_MIN, CH_NOBASS}, {5, CH_MAJ, CH_NOBASS}};
    reset();
    loops_reset();
    load_slots(4);
    arr_play(1);
    run_to((uint32_t)(0.3f * BAR));
    CHECK(arr_keys_held(am, 3) == 1, "A C E recognised");
    run_to((uint32_t)(BAR + 512));
    CHECK(arr_chord_unpack(arr_chord_now).root == 9 && arr_chord_unpack(arr_chord_now).type == CH_MIN, "Am");
    CHECK(root_at(0.5f * BAR) == 9, "the band follows (%d)", root_at(0.5f * BAR));
    CHECK(arr_keys_held(0, 0) == 0 && arr_keys_held(two, 2) == 0, "released, or two notes: no chord");
    run_to((uint32_t)(2 * BAR + 512));
    CHECK(arr_chord_unpack(arr_chord_now).root == 9 && root_at(2 * BAR) == 9, "fingered: released, Am stays");
    CHECK(arr_keys_held(e7, 4) == 1, "E G# B D");
    run_blocks(2);
    CHECK(arr_chord_unpack(arr_chord_now).root == 4 && arr_chord_unpack(arr_chord_now).type == CH_7, "E7");
    /* a loop picked at 2.5 bars starts at the bar line (C); the keys' Am holds until the loop's next entry */
    loop_bars(0, 4, 4, pop);
    run_to((uint32_t)(2.5f * BAR));
    arr_mode_loop(0);
    run_to((uint32_t)(3.1f * BAR));
    arr_keys_held(am, 3);
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(root_at(3 * BAR) == 0, "the loop from the bar line: C (%d)", root_at(3 * BAR));
    CHECK(root_at(3.5f * BAR) == 9 && root_at(4 * BAR) == 7, "the keys' Am until the loop's G (%d %d)", root_at(3.5f * BAR), root_at(4 * BAR));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "live chords: offs match");
}

/* ---- review fixes ---- */
/* a row of rep 4, and a row after it in the same section: the section plays on (no restart) */
static void test_song_marks_continue(void)
{
    static const int lo[1] = {0}, se[1] = {ROW_A}, re[1] = {4};
    static const int lo2[2] = {0, 0}, se2[2] = {ROW_A, ROW_A}, re2[2] = {1, 1};
    reset();
    loops_reset();
    load_slots(4);
    pat_new();                                /* Main A in two bars: 36 in the first, 37 in the second */
    P.bars = 2;
    for (int t = 0; t < 192; t += 24)
        add(0, t, 6, t < 96 ? 36 : 37);
    arr_load_pattern(SA, &P);
    song_rows(1, lo, se, re);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(4 * BAR));
    {
        int i = find(stub_log, stub_n, 0, 0x99, 37), j = find(stub_log, stub_n, i + 1, 0x99, 36);
        CHECK(near(stub_log, i, BAR) && near(stub_log, j, 2 * BAR), "A's second bar, then its first, over a row of rep 4 (%d %d)", i, j);
    }
    arr_play(0);
    run_blocks(1);
    stub_n = 0;
    stub_frame = 0;
    song_rows(2, lo2, se2, re2);
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(1.5f * BAR));
    {
        int i = find(stub_log, stub_n, 0, 0x99, 37);
        CHECK(near(stub_log, i, BAR) && arr_row == 1, "row 2 in the same section: A's second bar plays on (%d %d)", i, arr_row);
    }
    arr_play(0);
    run_blocks(1);
    /* Ending on: it plays once after the last row and stops */
    reset();
    loops_reset();
    load_slots(4);
    song_rows(1, lo, se, re);
    SG.row[0].rep = 1;
    SG.ending = 1;
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(4 * BAR));
    CHECK(slot_at(BAR) == SE && slot_at(2 * BAR) == SE, "the Ending, both its bars");
    CHECK(arr_playing == 0, "the ending stops");
    CHECK(ons_after(3 * BAR - 4) == 0, "nothing after it");
}

static void test_keys_release(void)           /* lifting keys one by one keeps the chord */
{
    static const uint8_t g7[] = {55, 59, 62, 65}, no_g[] = {59, 62, 65}, no_b[] = {62, 65}, c[] = {60, 64, 67};
    reset();
    loops_reset();
    load_slots(4);
    arr_play(1);
    CHECK(arr_keys_held(g7, 4) == 1, "G7");
    run_blocks(2);
    CHECK(arr_chord_unpack(arr_chord_now).root == 7 && arr_chord_unpack(arr_chord_now).type == CH_7, "G7 set");
    CHECK(arr_keys_held(no_g, 3) == 0, "G lifted (B D F): not a new chord");
    arr_keys_held(no_b, 2);
    arr_keys_held(0, 0);
    run_blocks(2);
    CHECK(arr_chord_unpack(arr_chord_now).root == 7 && arr_chord_unpack(arr_chord_now).type == CH_7, "released one by one: G7 stays");
    CHECK(arr_keys_held(c, 3) == 1, "pressed again: C");
    run_blocks(2);
    CHECK(arr_chord_unpack(arr_chord_now).root == 0 && arr_chord_unpack(arr_chord_now).type == CH_MAJ, "C set");
    arr_keys_held(0, 0);
    arr_play(0);
    run_blocks(1);
}

static void test_ending_twice(void)           /* Ending pressed again in its last bar: it still stops */
{
    reset();
    loops_reset();
    load_slots(4);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_END);
    run_to((uint32_t)(2.5f * BAR));
    arr_section(SEC_END);
    run_to((uint32_t)(4 * BAR));
    CHECK(arr_playing == 0, "stopped after one ending");
    CHECK(ons_after(3 * BAR - 4) == 0, "nothing after it");
    CHECK(offs_match(stub_log, stub_n), "offs match");
}

static void test_switch_keeps_voicing(void)   /* a note held over a section change keeps its voicing */
{
    reset();
    loops_reset();
    load_slots(4);
    pat_new();
    P.trk[4].x.type = XT_BYPASS;               /* Main A: track 4 Bypass, C4 held over the bar line */
    add(4, 72, 200, 60);
    arr_load_pattern(SA, &P);
    pat_new();                                /* Main B: track 4 Chord 1, nothing on it */
    arr_load_pattern(SB, &P);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_B);
    run_to((uint32_t)(1.25f * BAR));
    arr_set_chord(ch(9, CH_MIN7));
    run_to((uint32_t)(2 * BAR));
    CHECK(find(stub_log, stub_n, 0, 0x92, 69) < 0, "not re-voiced by Main B's track type");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
}

static void test_catchup_no_restrike(void)    /* a song chord on a dropped clock tick: set, not restruck */
{
    static const int lo[1] = {0}, se[1] = {ROW_A}, re[1] = {1};
    static const int lo2[2] = {0, 1}, se2[2] = {ROW_A, ROW_B}, re2[2] = {1, 1};
    int n0;
    reset();
    loops_reset();
    pat_new();
    add(4, 0, 90, 60);
    arr_load_pattern(SA, &P);
    loop_clear(&LT[0], 0);
    loop_write(&LT[0], 0, ch(0, CH_MAJ7));
    loop_write(&LT[0], 1, ch(9, CH_MIN7));
    song_rows(1, lo, se, re);
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    CHECK(find(stub_log, stub_n, 0, 0x92, 60) >= 0, "C4 at tick 0");
    n0 = stub_n;
    for (int k = 0; k < 30; k++)
        arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    CHECK(arr_chord_unpack(arr_chord_now).root == 9, "Am7 set on the dropped tick 24");
    CHECK(find(stub_log, stub_n, n0, 0x92, 69) < 0, "no restrike on a dropped tick");
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
    /* bunched clocks across a row change: the next row's section and chord */
    reset();
    loops_reset();
    load_slots(4);
    empty_slot(SFA);
    empty_slot(SFB);
    loop_clear(&LT[0], 0);
    loop_write(&LT[0], 0, ch(0, CH_MAJ));
    loop_clear(&LT[1], 1);
    loop_write(&LT[1], 0, ch(9, CH_MIN));
    song_rows(2, lo2, se2, re2);
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    for (int k = 0; k < 90; k++) {             /* bar 1, to beat 4 (3/4 of it) */
        arr_clock(ARR_CLK_TICK);
        run_blocks(1);
    }
    for (int k = 0; k < 30; k++)               /* 600 ticks owed: across the bar line */
        arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    CHECK(arr_row == 1 && arr_sect == SEC_B && arr_chord_unpack(arr_chord_now).root == 9, "row 2: B, Am (%d %d %d)", arr_row, arr_sect,
          arr_chord_unpack(arr_chord_now).root);
    CHECK(arr_ticks == 120u * ARR_CLOCK_TICKS, "all owed ticks moved on (%u)", arr_ticks);
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
}

/* the drum note-ons in order: the k-th one's note */
static int drum_on(int k)
{
    for (int i = 0; i < stub_n; i++)
        if (stub_log[i].st == 0x99 && is_on(&stub_log[i]) && k-- == 0)
            return stub_log[i].d1;
    return -1;
}
static void test_section_clock_in(void)       /* sections on the bar under MIDI clock in */
{
    reset();
    loops_reset();
    load_slots(4);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    for (int k = 0; k < 2 * 96 + 1; k++) {
        if (k == 40)
            arr_section(SEC_B);
        arr_clock(ARR_CLK_TICK);
        run_blocks(1);
        if (k == 95)
            CHECK(arr_next_sect == SEC_B && arr_sect == SEC_A, "B queued until tick 96");
    }
    CHECK(drum_on(3) == 36 && drum_on(4) == 38 && drum_on(8) == 38, "A to beat 4, B from tick 96 (%d %d)", drum_on(3), drum_on(4));
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
}

static void test_fill_variants(void)
{
    /* fill during a fill: one more fill bar, then the other Main */
    reset();
    loops_reset();
    load_slots(4);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(1.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(4 * BAR));
    CHECK(slot_at(BAR) == SFA && slot_at(2 * BAR) == SFA && slot_at(3 * BAR) == SB, "A, fill, fill, B (%d %d %d)",
          slot_at(BAR), slot_at(2 * BAR), slot_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
    /* ending during a fill: the ending at the bar, once */
    reset();
    loops_reset();
    load_slots(4);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(1.5f * BAR));
    arr_section(SEC_END);
    run_to((uint32_t)(5 * BAR));
    CHECK(slot_at(BAR) == SFA && slot_at(2 * BAR) == SE && slot_at(3 * BAR) == SE, "fill, ending (%d %d)", slot_at(2 * BAR), slot_at(3 * BAR));
    CHECK(arr_playing == 0 && ons_after(4 * BAR - 4) == 0, "stopped after the ending");
}

/* ---- six sections: the Intro, Fill AB / BA, and the fallbacks of sections without notes */
static void empty_slot(int slot)
{
    pat_new();
    arr_load_pattern(slot, &P);
}
static void test_intro(void)
{
    /* PLAY with an Intro (2 bars): it plays once, then Main A; PLAY again: the Intro again */
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 2, 4);
    arr_play(1);
    run_to((uint32_t)(3.5f * BAR));
    CHECK(slot_at(0) == SI && slot_at(1.75f * BAR) == SI, "the Intro first, both its bars (%d %d)", slot_at(0), slot_at(1.75f * BAR));
    CHECK(slot_at(2 * BAR) == SA && slot_at(3 * BAR) == SA && arr_sect == SEC_A, "then Main A (%d %d)", slot_at(2 * BAR),
          slot_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
    stub_n = 0;
    stub_frame = 0;
    arr_play(1);
    run_blocks(2);
    CHECK(arr_sect == SEC_INTRO, "PLAY again: the Intro again (%d)", arr_sect);
    arr_play(0);
    run_blocks(1);
    /* queued while playing: from the next bar, once, then Main A (a Fill during it ignored); then a fill from A */
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 1, 4);
    arr_section(SEC_A);                       /* PLAY with Main A queued: Main A, not the Intro */
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    CHECK(arr_sect == SEC_A, "Main A queued before PLAY: no Intro (%d)", arr_sect);
    arr_section(SEC_INTRO);
    run_to((uint32_t)(1.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(2.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(4.5f * BAR));
    CHECK(slot_at(BAR) == SI && slot_at(2 * BAR) == SA && slot_at(3 * BAR) == SFA && slot_at(4 * BAR) == SB,
          "Intro, A, Fill AB, B (%d %d %d %d)", slot_at(BAR), slot_at(2 * BAR), slot_at(3 * BAR), slot_at(4 * BAR));
    arr_play(0);
    run_blocks(1);
    /* recording: PLAY starts with the pattern recorded into, not the Intro */
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 1, 4);
    arr_record(5, 1);
    run_blocks(1);
    arr_play(1);
    run_blocks(2);
    CHECK(arr_sect == SEC_A, "recording: Main A, not the Intro (%d)", arr_sect);
    arr_record(5, 0);
    arr_play(0);
    run_blocks(1);
    /* a song with Intro on: the Intro first, then its row 1; Intro off: row 1 at once (Main B: B) */
    {
        static const int lo[1] = {0}, se[1] = {ROW_A}, re[1] = {4};
        reset();
        loops_reset();
        load_slots(4);
        load_slot(SI, 1, 4);
        song_rows(1, lo, se, re);
        SG.intro = 1;
        arr_mode_song(&SG, 0);
        arr_play(1);
        run_to((uint32_t)(2.5f * BAR));
        CHECK(slot_at(0) == SI && slot_at(BAR) == SA && arr_row == 0 && arr_rep == 1, "a song: the Intro, then A (%d %d, row %d pass %d)",
              slot_at(0), slot_at(BAR), arr_row, arr_rep);
        arr_play(0);
        run_blocks(1);
        SG.intro = 0;
        SG.row[0].sect = ROW_B;
        arr_play(1);
        run_blocks(2);
        CHECK(arr_sect == SEC_B, "Intro off, row 1 Main B: B (%d)", arr_sect);
        arr_play(0);
        run_blocks(1);
    }
    /* no Intro (no notes): PLAY and SEC_INTRO give Main A */
    reset();
    loops_reset();
    load_slots(4);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    CHECK(arr_sect == SEC_A, "no Intro: Main A (%d)", arr_sect);
    arr_section(SEC_B);
    run_to((uint32_t)(1.5f * BAR));
    arr_section(SEC_INTRO);
    run_to((uint32_t)(2.5f * BAR));
    CHECK(slot_at(BAR) == SB && slot_at(2 * BAR) == SA, "SEC_INTRO without one: Main A (%d %d)", slot_at(BAR), slot_at(2 * BAR));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
}
static void test_fill_fallbacks(void)
{
    /* no Fill BA: from B the fill is Fill AB's pattern, and it still goes back to A */
    reset();
    loops_reset();
    load_slots(4);
    empty_slot(SFB);
    arr_section(SEC_B);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(2.5f * BAR));
    CHECK(slot_at(0) == SB && slot_at(BAR) == SFA && slot_at(2 * BAR) == SA, "no Fill BA: B, Fill AB's, A (%d %d %d)",
          slot_at(0), slot_at(BAR), slot_at(2 * BAR));
    arr_play(0);
    run_blocks(1);
    /* no Fill AB: from A, Fill BA's, then B */
    reset();
    loops_reset();
    load_slots(4);
    empty_slot(SFA);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(2.5f * BAR));
    CHECK(slot_at(BAR) == SFB && slot_at(2 * BAR) == SB, "no Fill AB: Fill BA's, then B (%d %d)", slot_at(BAR), slot_at(2 * BAR));
    arr_play(0);
    run_blocks(1);
    /* neither: no fill, Main A plays on */
    reset();
    loops_reset();
    load_slots(4);
    empty_slot(SFA);
    empty_slot(SFB);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_FILL);
    run_blocks(1);
    CHECK(arr_next_sect == SEC_FILL, "the Fill asked for is shown (%d)", arr_next_sect);
    run_to((uint32_t)(2.5f * BAR));
    CHECK(slot_at(BAR) == SA && slot_at(2 * BAR) == SA && arr_sect == SEC_A && arr_next_sect == SEC_SAME,
          "no fills: A plays on (%d %d)", slot_at(BAR), slot_at(2 * BAR));
    arr_play(0);
    run_blocks(1);
    /* a song's forced fill from Main B without a Fill BA: Fill AB's pattern, then the next row's B */
    {
        static const int lo[2] = {0, 0}, se[2] = {ROW_B_FILL, ROW_B}, re[2] = {2, 1};
        reset();
        loops_reset();
        load_slots(4);
        empty_slot(SFB);
        song_rows(2, lo, se, re);
        arr_mode_song(&SG, 0);
        arr_play(1);
        run_to((uint32_t)(3 * BAR + 512));
        CHECK(slot_at(0) == SB && slot_at(BAR) == SFA && slot_at(2 * BAR) == SB, "B, Fill AB's, B (%d %d %d)", slot_at(0),
              slot_at(BAR), slot_at(2 * BAR));
    }
    /* (a song's Intro mark mid-song: gone; a song's Intro plays only at its start) */
    CHECK(offs_match(stub_log, stub_n), "offs match");
}
static void test_no_ending(void)              /* an Ending without notes: stop at the bar line */
{
    reset();
    loops_reset();
    load_slots(4);
    empty_slot(SE);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_END);
    run_to((uint32_t)(2 * BAR));
    CHECK(arr_playing == 0, "stopped");
    CHECK(slot_at(0.75f * BAR) == SA && ons_after(BAR - 4) == 0, "at the end of the bar playing (%d)", ons_after(BAR - 4));
    CHECK(offs_match(stub_log, stub_n), "offs match");
}

static void test_song_while_playing(void)     /* a song set mid-bar: from its row 1 at the next bar line */
{
    static const chord_t am[1] = {{9, CH_MIN7, CH_NOBASS}}, f[1] = {{5, CH_MAJ, CH_NOBASS}};
    static const int lo[1] = {0}, se[1] = {ROW_B}, re[1] = {4};
    static const int lo2[1] = {1}, se2[1] = {ROW_A}, re2[1] = {1};
    static song_t S2;
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, am);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    song_rows(1, lo, se, re);
    arr_mode_song(&SG, 0);
    run_blocks(1);
    CHECK(arr_mode == ARR_SONG && arr_row == 0 && arr_sect == SEC_A, "asked: SONG shown, the bar plays on");
    run_to((uint32_t)(1.5f * BAR));
    CHECK(root_at(0.75f * BAR) == 0 && slot_at(0.75f * BAR) == SA, "the bar plays out as it was");
    CHECK(root_at(BAR) == 9 && slot_at(BAR) == SB, "the song's row 1 at the bar line (%d %d)", root_at(BAR), slot_at(BAR));
    CHECK(arr_row == 0, "its row 1 (%d)", arr_row);
    /* another song switched in while this one plays: this one to the bar line, then the other's row 1 */
    run_to((uint32_t)(2.5f * BAR));
    loop_bars(1, 4, 1, f);
    S2 = SG;
    S2.nrows = 1;
    S2.row[0].loop = (uint8_t)lo2[0];
    S2.row[0].sect = (uint8_t)se2[0];
    S2.row[0].rep = (uint8_t)re2[0];
    S2.at_end = 1;
    arr_mode_song(&S2, 0);
    run_to((uint32_t)(3.5f * BAR));
    CHECK(root_at(2.75f * BAR) == 9 && slot_at(2.75f * BAR) == SB, "the first song to the bar line");
    CHECK(root_at(3 * BAR) == 5 && slot_at(3 * BAR) == SA, "then the other's row 1: A over F (%d %d)", root_at(3 * BAR), slot_at(3 * BAR));
    run_to((uint32_t)(6 * BAR));
    CHECK(arr_playing == 1 && root_at(5 * BAR) == 5, "it goes round (again)");
    arr_play(0);
    run_blocks(1);
    /* the first again, played through: four bars, then stopped */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, am);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    song_rows(1, lo, se, re);
    arr_mode_song(&SG, 0);
    run_to((uint32_t)(6 * BAR));
    CHECK(arr_playing == 0 && ons_after(5 * BAR - 4) == 0, "four bars, then stopped");
    CHECK(offs_match(stub_log, stub_n), "offs match");
    /* picked while stopped, then MIDI Continue: the row's chord now, its section from the next bar line */
    reset();
    loops_reset();
    load_slots(4);
    loop_bars(0, 4, 1, am);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    for (int k = 0; k < 36; k++) {             /* a beat and a half */
        arr_clock(ARR_CLK_TICK);
        run_blocks(2);
    }
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
    song_rows(1, lo, se, re);
    arr_mode_song(&SG, 0);
    run_blocks(1);
    arr_clock(ARR_CLK_CONTINUE);
    for (int k = 0; k < 24; k++) {
        arr_clock(ARR_CLK_TICK);
        run_blocks(2);
    }
    CHECK(arr_chord_unpack(arr_chord_now).root == 9 && arr_sect == SEC_A && arr_row == 0, "Continue into the song: Am, A to the bar line (%d %d)",
          arr_chord_unpack(arr_chord_now).root, arr_sect);
    for (int k = 0; k < 48; k++) {
        arr_clock(ARR_CLK_TICK);
        run_blocks(2);
    }
    CHECK(arr_sect == SEC_B && arr_rep == 1, "the row's B from the bar line, its second pass (its first: the bar Continue came in) (%d %d)",
          arr_sect, arr_rep);
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
}

static void test_switch_chord_ends(void)      /* a chord change on a row's bar line ends A's chord notes */
{
    static const int lo[2] = {0, 1}, se[2] = {ROW_A, ROW_B}, re[2] = {1, 1};
    int i;
    reset();
    loops_reset();
    load_slots(4);
    empty_slot(SFA);                          /* (no fill: A, then B) */
    empty_slot(SFB);
    pat_new();                                /* Main A: track 4 Chord 1, C4 held over the bar line */
    add(4, 72, 200, 60);
    arr_load_pattern(SA, &P);
    pat_new();
    arr_load_pattern(SB, &P);
    loop_clear(&LT[0], 0);
    loop_write(&LT[0], 0, ch(0, CH_MAJ7));
    loop_clear(&LT[1], 1);
    loop_write(&LT[1], 0, ch(9, CH_MIN7));
    song_rows(2, lo, se, re);
    arr_mode_song(&SG, 0);
    arr_play(1);
    run_to((uint32_t)(2 * BAR));
    i = find(stub_log, stub_n, 0, 0x82, 60);
    CHECK(near(stub_log, i, BAR), "C4 ends at the change, not at its own end (%u)", i >= 0 ? stub_log[i].frame : 0);
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
}

/* ---- loop recording */
static pattern_t R;                           /* a slot read back */
static int ons_in(int st, int note, float from, float to)
{
    int n = 0;
    for (int i = 0; i < stub_n; i++)
        if (stub_log[i].st == st && stub_log[i].d1 == note && is_on(&stub_log[i]) &&
            (float)stub_log[i].frame >= from && (float)stub_log[i].frame < to)
            n++;
    return n;
}
static void key_at(float tick, int note, int on)   /* the key at the first block from this tick (units) */
{
    run_to((uint32_t)(tick * FPT));
    arr_key(note, on, on ? 90 : 0);
}
/* the tick (from the start, at 120 BPM) nearest the block a key_at(units) lands on */
static int key_tick_at(float units)
{
    uint32_t f = ((uint32_t)(units * FPT) + 255u) / 256u * 256u;
    return (int)((float)f * TPF + 0.5f);
}
static int within(int got, int want, int tol) { return got >= want - tol && got <= want + tol; }
#define KEY_TOL 4                                 /* a key's time: its block's start, to the nearest tick (+- 3) */

static void test_inverse(void)                /* the transposer's inverse, over Am7 */
{
    xtrack_t c1 = {XT_CHORD1, 0, 127, XT_NOKEY}, by = {XT_BYPASS, 0, 127, XT_NOKEY}, pa = {XT_PARALLEL, 0, 127, XT_NOKEY};
    xstate_t xs;
    chord_t am7 = ch(9, CH_MIN7);
    static const int keys[] = {69, 60, 64, 67, 72, 62, 66};
    CHECK(xpose_inverse(&c1, 69, am7) == 60, "A4 over Am7, Chord 1 -> C4 (%d)", xpose_inverse(&c1, 69, am7));
    CHECK(xpose_inverse(&c1, 60, am7) == 52, "C4 (the 3rd) -> E3 (%d)", xpose_inverse(&c1, 60, am7));
    CHECK(xpose_inverse(&c1, 64, am7) == 55, "E4 (the 5th) -> G3 (%d)", xpose_inverse(&c1, 64, am7));
    CHECK(xpose_inverse(&c1, 67, am7) == 59, "G4 (the 7th) -> B3 (%d)", xpose_inverse(&c1, 67, am7));
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        xpose_reset(&xs);
        int n = xpose_inverse(&c1, keys[i], am7);
        CHECK(xpose(&c1, &xs, n, am7) == keys[i], "Chord 1 maps the inverse of %d back to it", keys[i]);
    }
    CHECK(xpose_inverse(&c1, 73, am7) == 64, "C#5, outside every image: note - root (%d)", xpose_inverse(&c1, 73, am7));
    CHECK(xpose_inverse(&by, 38, am7) == 38, "Bypass: as played");
    CHECK(xpose_inverse(&pa, 69, am7) == 60, "Parallel: note - root");
    CHECK(xpose_inverse(&c1, 69, ch(0, CH_MAJ7)) == 69, "over C M7: as played");
}

static void test_record(void)
{
    int i;
    uint16_t d0;
    reset();
    loops_reset();
    pattern_one();                            /* tracks 0 and 4; track 5 (Chord 1, channel 4) empty */
    arr_load_pattern(SA, &P);
    arr_set_tempo(120);
    arr_set_chord(ch(9, CH_MIN7));
    arr_record(5, 1);
    arr_play(1);
    d0 = arr_pattern_copy(SA, &R);
    key_at(25.f, 69, 1);                      /* A4 at tick 25 -> 24 */
    key_at(36.f, 69, 0);                      /* released at 36: 12 ticks */
    key_at(47.f, 72, 1);                      /* C5 at 47 -> 48 (ahead of the key) */
    key_at(49.f, 72, 0);                      /* 1 tick: one quantum */
    run_to((uint32_t)(60 * FPT));
    CHECK(arr_pattern_copy(SA, &R) == d0 + 2, "dirty counted twice");
    CHECK(R.trk[5].count == 2 && R.nev == 7, "two notes in track 5 (%d, nev %d)", R.trk[5].count, R.nev);
    i = R.trk[5].first;
    CHECK(R.ev[i].tick == 24 * U && within(R.ev[i].len, 12 * U, KEY_TOL) && R.ev[i].note == 60 && R.ev[i].vel == 90,
          "A4 stored as C4 at 24, 12 units (%d %d %d)", R.ev[i].tick, R.ev[i].len, R.ev[i].note);
    CHECK(R.ev[i + 1].tick == 48 * U && R.ev[i + 1].len == 6 * U && R.ev[i + 1].note == 64,
          "C5 stored as E4 at 48, 6 units: a quantum (%d %d %d)", R.ev[i + 1].tick, R.ev[i + 1].len, R.ev[i + 1].note);
    CHECK(ons_in(0x93, 69, 0.f, 96 * FPT) == 1 && ons_in(0x93, 72, 0.f, 96 * FPT) == 1,
          "the first pass: the keys only (%d %d)", ons_in(0x93, 69, 0.f, 96 * FPT), ons_in(0x93, 72, 0.f, 96 * FPT));
    i = find(stub_log, stub_n, 0, 0x93, 72);
    CHECK(i >= 0 && stub_log[i].frame < 48 * FPT, "the key sounds at once, not at the grid");
    /* a note held over the loop's end: to the pattern's end */
    key_at(89.f, 76, 1);                      /* E5 at 89 -> 90 */
    key_at(96.f + 10.f, 76, 0);
    run_to((uint32_t)(96 * FPT + 20 * FPT));
    arr_pattern_copy(SA, &R);
    i = R.trk[5].first + 2;
    CHECK(R.trk[5].count == 3 && R.ev[i].tick == 90 * U && R.ev[i].len == 6 * U && R.ev[i].note == 67,
          "E5 held over the wrap: G4 at 90, 6 ticks (%d: %d %d %d)", R.trk[5].count, R.ev[i].tick, R.ev[i].len, R.ev[i].note);
    /* the next pass plays them, as A4 and C5 over Am7 */
    run_to((uint32_t)(2 * 96 * FPT + 512));
    i = find(stub_log, stub_n, 0, 0x93, 69);
    i = find(stub_log, stub_n, i + 1, 0x93, 69);
    CHECK(near(stub_log, i, (96 + 24) * FPT), "C4 plays as A4 on the next pass (%u)", i >= 0 ? stub_log[i].frame : 0);
    i = find(stub_log, stub_n, 0, 0x93, 72);
    i = find(stub_log, stub_n, i + 1, 0x93, 72);
    CHECK(near(stub_log, i, (96 + 48) * FPT), "E4 plays as C5 at 48 (%u)", i >= 0 ? stub_log[i].frame : 0);
    i = find(stub_log, stub_n, 0, 0x93, 76);
    i = find(stub_log, stub_n, i + 1, 0x93, 76);
    CHECK(near(stub_log, i, (96 + 90) * FPT), "G4 plays as E5 at 90 (%u)", i >= 0 ? stub_log[i].frame : 0);
    CHECK(ons_in(0x93, 76, 96 * FPT, (96 + 89) * FPT) == 0, "nothing of E5 early in the second pass");
    /* overdub, third pass: a fourth note joins */
    key_at(2 * 96.f + 12.f, 74, 1);           /* D5 -> the 11th of Am7: F4 */
    key_at(2 * 96.f + 18.f, 74, 0);
    run_to((uint32_t)(4 * 96 * FPT + 512));
    arr_pattern_copy(SA, &R);
    i = R.trk[5].first;
    CHECK(R.trk[5].count == 4 && R.ev[i].tick == 12 * U && R.ev[i].note == 65 && R.ev[i + 1].tick == 24 * U,
          "overdub: D5 as F4 at 12, before the rest (%d: %d %d)", R.trk[5].count, R.ev[i].tick, R.ev[i].note);
    CHECK(ons_in(0x93, 74, (3 * 96) * FPT, (4 * 96) * FPT) == 1 && ons_in(0x93, 69, (3 * 96) * FPT, (4 * 96) * FPT) == 1,
          "the fourth pass plays the old and the new");
    CHECK(R.trk[0].count == 2 && R.trk[4].count == 3 && R.ev[R.trk[4].first].note == 60, "the other tracks intact");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
}

static void test_record_bypass_erase(void)    /* into a track before others: they move up; erase */
{
    int i;
    reset();
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
    arr_set_tempo(120);
    arr_set_chord(ch(9, CH_MIN7));
    arr_record(0, 1);
    arr_set_quant(ARR_Q_8);
    arr_play(1);
    key_at(29.f, 38, 1);                      /* 29 -> 24 on the 1/8 grid */
    key_at(31.f, 38, 0);                      /* 2 ticks: 12 */
    key_at(53.f, 36, 1);                      /* 53 -> 48: the kick already there, replaced */
    key_at(54.f, 36, 0);
    run_to((uint32_t)(60 * FPT));
    arr_pattern_copy(SA, &R);
    CHECK(R.trk[0].count == 3 && R.nev == 6, "a snare in track 0, the kick replaced (%d, %d)", R.trk[0].count, R.nev);
    i = R.trk[0].first;
    CHECK(R.ev[i + 1].tick == 24 * U && R.ev[i + 1].note == 38 && R.ev[i + 1].len == 12 * U, "Bypass: as played, at 24 (%d %d %d)",
          R.ev[i + 1].tick, R.ev[i + 1].note, R.ev[i + 1].len);
    CHECK(R.trk[4].first == 3 && R.ev[3].note == 60 && R.ev[5].note == 67, "track 4 moved up one");
    run_to((uint32_t)(96 * FPT + 60 * FPT));
    CHECK(near(stub_log, find(stub_log, stub_n, find(stub_log, stub_n, 0, 0x99, 38) + 1, 0x99, 38), (96 + 24) * FPT),
          "the snare on the next pass");
    i = find(stub_log, stub_n, 0, 0x92, 76);
    i = find(stub_log, stub_n, i + 1, 0x92, 76);
    CHECK(near(stub_log, i, (96 + 48) * FPT), "track 4 plays on (%u)", i >= 0 ? stub_log[i].frame : 0);
    arr_erase_track(0);
    run_to((uint32_t)(2 * 96 * FPT));
    arr_pattern_copy(SA, &R);
    CHECK(R.trk[0].count == 0 && R.nev == 3 && R.trk[4].first == 0 && R.ev[0].note == 60 && R.ev[2].note == 67,
          "erased: track 4 alone (%d %d %d)", R.trk[0].count, R.nev, R.trk[4].first);
    CHECK(ons_in(0x99, 36, 2 * 96 * FPT, 3 * 96 * FPT) == 0 && ons_in(0x92, 69, 2 * 96 * FPT, 3 * 96 * FPT) == 1,
          "the next pass: no drums, track 4 on");
    run_to((uint32_t)(3 * 96 * FPT));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
}

static void test_record_early(void)           /* a short note played just before its grid line */
{
    reset();
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
    arr_set_tempo(120);
    arr_set_chord(ch(9, CH_MIN7));
    arr_record(5, 1);
    arr_play(1);
    key_at(46.6f, 67, 1);                     /* G4 -> 48, released before tick 48 plays */
    run_blocks(1);
    arr_key(67, 0, 0);
    key_at(93.6f, 64, 1);                     /* E4 just before the loop's end: its tick 0 */
    run_blocks(1);
    arr_key(64, 0, 0);
    run_to((uint32_t)(96 * FPT + 512));
    arr_pattern_copy(SA, &R);
    CHECK(R.trk[5].count == 2 && R.ev[R.trk[5].first].tick == 0 && R.ev[R.trk[5].first + 1].tick == 48 * U,
          "stored at 0 and 48 (%d)", R.trk[5].count);
    CHECK(ons_in(0x93, 67, 0.f, 96 * FPT) == 1, "G4 sounds once in the pass it was played (%d)", ons_in(0x93, 67, 0.f, 96 * FPT));
    CHECK(ons_in(0x93, 64, 0.f, (96 + 12) * FPT) == 1, "E4 not again on the downbeat after it (%d)", ons_in(0x93, 64, 0.f, (96 + 12) * FPT));
    run_to((uint32_t)(2 * 96 * FPT + 512));
    CHECK(ons_in(0x93, 67, 96 * FPT, 2 * 96 * FPT) == 1 && ons_in(0x93, 64, 1.f, 2 * 96 * FPT + 512) == 2,
          "both play from the next pass");
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
}

static void test_record_full(void)
{
    reset();
    loops_reset();
    pat_new();
    for (int i = 0; i < ARR_NEV - 1; i++)
        add(1, i * 96 / ARR_NEV, 1, 40);
    arr_load_pattern(SA, &P);
    arr_track_mute(1, 1);
    arr_record(5, 1);
    arr_set_quant(ARR_Q_OFF);
    arr_play(1);
    key_at(10.f, 60, 1);
    key_at(12.f, 60, 0);
    key_at(20.f, 62, 1);
    key_at(22.f, 62, 0);
    run_to((uint32_t)(30 * FPT));
    arr_pattern_copy(SA, &R);
    CHECK(R.nev == ARR_NEV && R.trk[5].count == 1 && R.ev[R.trk[5].first].tick == key_tick_at(10.f) &&
              R.ev[R.trk[5].first].len == key_tick_at(12.f) - key_tick_at(10.f),
          "the first fits, quantise off: at its tick %d, %d ticks (%d %d)", key_tick_at(10.f),
          key_tick_at(12.f) - key_tick_at(10.f), R.ev[R.trk[5].first].tick, R.ev[R.trk[5].first].len);
    CHECK(arr_full == 1, "the second is dropped: full");
    arr_erase_track(5);
    run_blocks(1);
    CHECK(arr_full == 0, "an erase makes room");
    arr_full = 1;
    arr_load_pattern(SA, &P);
    CHECK(arr_full == 0, "a load clears it");
    arr_play(0);
    run_blocks(1);
}

void arr_test_busy(int slot, int on);         /* arr.c with ARR_TEST: a slot caught mid-store */
static void rec_start(int trk)                /* pattern_one in slot 0, Am7, recording into trk, playing */
{
    reset();
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
    arr_set_tempo(120);
    arr_set_chord(ch(9, CH_MIN7));
    arr_record(trk, 1);
    arr_play(1);
}

static void test_inverse_types(void)          /* Chord 2, Bass, high key */
{
    xtrack_t c2 = {XT_CHORD2, 0, 127, XT_NOKEY}, ba = {XT_BASS, 0, 127, XT_NOKEY}, hk = {XT_CHORD1, 0, 127, 5};
    xstate_t xs;
    chord_t am7 = ch(9, CH_MIN7), g7 = ch(7, CH_7);
    CHECK(xpose_inverse(&c2, 69, am7) == 60 && xpose_inverse(&c2, 72, am7) == 64, "Chord 2: as Chord 1");
    CHECK(xpose_inverse(&ba, 45, am7) == 36, "Bass: A2 over Am7 -> C2 (%d)", xpose_inverse(&ba, 45, am7));
    CHECK(xpose_inverse(&ba, 50, g7) == 43, "Bass: D3 (the 5th of G7) -> G2 (%d)", xpose_inverse(&ba, 50, g7));
    CHECK(xpose_inverse(&hk, 57, am7) == 60, "high key F, over Am7 the line folds down: A3 -> C4 (%d)", xpose_inverse(&hk, 57, am7));
    xpose_reset(&xs);
    CHECK(xpose(&hk, &xs, 60, am7) == 57, "and back");
    CHECK(xpose_inverse(&hk, 67, g7) == 72, "high key F, over G7: G4 -> C5 (%d)", xpose_inverse(&hk, 67, g7));
    CHECK(xpose_inverse(&c2, 71, ch(0, CH_MAJ)) == 71 - 0, "B over C major (no 7th): note - root");
}

static void test_record_triplets(void)
{
    int i;
    rec_start(5);
    arr_set_quant(ARR_Q_8T);
    key_at(13.f, 69, 1);                      /* 13 -> 16 on the 8-tick grid */
    key_at(19.f, 69, 0);                      /* 3 ticks: one quantum, 8 */
    arr_set_quant(ARR_Q_16T);
    key_at(21.f, 72, 1);                      /* 21 -> 20 on the 4-tick grid */
    key_at(30.f, 72, 0);                      /* 10 ticks */
    run_to((uint32_t)(40 * FPT));
    arr_pattern_copy(SA, &R);
    i = R.trk[5].first;
    CHECK(R.trk[5].count == 2 && R.ev[i].tick == 16 * U && R.ev[i].len == 8 * U && R.ev[i + 1].tick == 20 * U &&
              within(R.ev[i + 1].len, 10 * U, KEY_TOL),
          "1/8T at 16 (8 ticks), 1/16T at 20 (10): %d %d, %d %d", R.ev[i].tick, R.ev[i].len, R.ev[i + 1].tick, R.ev[i + 1].len);
    arr_play(0);
    run_blocks(1);
}

/* grid off: a key at the loop's start is tick 0. At 120 BPM a pass is 88200 frames; the fourth pass starts 32
 * frames after a block start (352768 = 1378 x 256), so a key there finds the pass before's last tick played
 * and tick 0 next, but nearer the last (a tick is 45.9 frames) */
static void test_record_off_grid_start(void)
{
    const uint32_t blk = 1378u * 256u, pass = 4u * 96u * U;
    rec_start(5);
    arr_set_quant(ARR_Q_OFF);
    run_to(blk);
    arr_key(69, 1, 90);
    run_to(blk + 4096u);
    arr_key(69, 0, 0);
    run_to(blk + 30000u);
    arr_pattern_copy(SA, &R);
    CHECK(R.trk[5].count == 1 && R.ev[R.trk[5].first].tick == 0 &&
              R.ev[R.trk[5].first].len == (int)((float)(blk + 4096u) * TPF + 0.5f) - (int)pass,
          "stored at 0, to its release (%d: %d %d)", R.trk[5].count, R.ev[R.trk[5].first].tick, R.ev[R.trk[5].first].len);
    CHECK(ons_in(0x93, 69, 0.f, blk + 30000.f) == 1, "the key only, this pass");
    run_to(441000u + 512u);
    CHECK(ons_in(0x93, 69, 441000.f - 4, 441000.f + 4) == 1, "on the next pass's downbeat");
    arr_play(0);
    run_blocks(1);
}

static void test_record_busy(void)            /* a note due, and an erase, while the slot is being stored */
{
    rec_start(5);
    key_at(25.f, 69, 1);
    run_to((uint32_t)(30 * FPT));
    arr_test_busy(SA, 1);
    arr_key(69, 0, 0);
    arr_erase_track(0);
    run_to((uint32_t)(40 * FPT));
    arr_pattern_copy(SA, &R);
    CHECK(R.trk[5].count == 0 && R.trk[0].count == 2, "nothing written while busy");
    arr_test_busy(SA, 0);
    run_blocks(1);
    arr_pattern_copy(SA, &R);
    CHECK(R.trk[5].count == 1 && R.ev[R.trk[5].first].tick == 24 * U && R.ev[R.trk[5].first].note == 60,
          "the note, kept, written after (%d)", R.trk[5].count);
    CHECK(R.trk[0].count == 0, "the erase, after");
    arr_play(0);
    run_blocks(1);
}

static void test_record_load(void)            /* another pattern loaded under a held note: it is dropped */
{
    uint16_t d;
    rec_start(5);
    key_at(25.f, 69, 1);
    run_to((uint32_t)(28 * FPT));
    arr_load_pattern(SA, &P);
    d = arr_pattern_copy(SA, &R);
    key_at(36.f, 69, 0);
    run_to((uint32_t)(60 * FPT));
    CHECK(arr_pattern_copy(SA, &R) == d && R.trk[5].count == 0, "not written into the new pattern (%d)", R.trk[5].count);
    arr_play(0);
    run_blocks(1);
}

static void test_store(void)                  /* copy, edit, store: refused when recorded into meanwhile */
{
    uint16_t d0, d1;
    static pattern_t E;
    rec_start(5);
    d0 = arr_pattern_copy(SA, &E);
    key_at(25.f, 69, 1);
    key_at(36.f, 69, 0);
    run_to((uint32_t)(40 * FPT));
    E.trk[5].vol = 50;
    CHECK(arr_store_pattern(SA, &E, d0) == 0, "refused: recorded into since the copy");
    d1 = arr_pattern_copy(SA, &E);
    CHECK(E.trk[5].count == 1, "the note still there");
    E.trk[5].vol = 50;
    key_at(50.f, 72, 1);                      /* held over the store */
    CHECK(arr_store_pattern(SA, &E, d1) == 1, "stored with the count of the copy");
    key_at(60.f, 72, 0);
    run_to((uint32_t)(70 * FPT));
    arr_pattern_copy(SA, &R);
    CHECK(R.trk[5].vol == 50 && R.trk[5].count == 2 && R.ev[R.trk[5].first + 1].tick == 48 * U,
          "the edit, and the note held over the store, kept (%d %d)", R.trk[5].vol, R.trk[5].count);
    arr_play(0);
    run_blocks(1);
}

static void test_record_section(void)         /* a section change ends recording, at its tick */
{
    reset();
    loops_reset();
    pattern_one();
    P.bars = 2;                               /* the switch at bar 2, mid-pattern */
    arr_load_pattern(SA, &P);
    pat_new();
    arr_load_pattern(SB, &P);
    arr_set_tempo(120);
    arr_set_chord(ch(9, CH_MIN7));
    arr_record(5, 1);
    arr_play(1);
    key_at(50.f, 69, 1);                      /* -> 48, held over the bar line */
    arr_section(SEC_B);
    run_to((uint32_t)(100 * FPT));
    CHECK(arr_recording == 0 && arr_sect == SEC_B, "recording off at the switch");
    key_at(110.f, 69, 0);
    key_at(120.f, 72, 1);
    key_at(130.f, 72, 0);
    run_to((uint32_t)(140 * FPT));
    arr_pattern_copy(SA, &R);
    CHECK(R.trk[5].count == 1 && R.ev[R.trk[5].first].tick == 48 * U && R.ev[R.trk[5].first].len == 48 * U,
          "the held note ends at the switch: 48 ticks (%d)", R.ev[R.trk[5].first].len);
    arr_pattern_copy(SB, &R);
    CHECK(R.trk[5].count == 0, "nothing into Main B");
    arr_play(0);
    run_blocks(1);
}

static void test_live_keys(void)              /* the keys: the track's sounds, MIDI out */
{
    int i;
    reset();
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
    arr_midi_out_on(1);
    arr_record(5, 0);
    run_blocks(1);
    CHECK(find(stub_log, stub_n, 0, 0xC3, 15) >= 0 && find(stub_log, stub_n, 0, 0xB3, 7) >= 0,
          "track 5's program and volume to the synth at once");
    CHECK(find(out_log, out_n, 0, 0xC3, 15) >= 0, "and to MIDI out");
    arr_key(64, 1, 100);
    run_blocks(1);
    arr_key(64, 0, 0);
    run_blocks(1);
    i = find(stub_log, stub_n, 0, 0x93, 64);
    CHECK(i >= 0 && find(stub_log, stub_n, i + 1, 0x83, 64) > i, "the key on channel 4, and its end");
    i = find(out_log, out_n, 0, 0x93, 64);
    CHECK(i >= 0 && find(out_log, out_n, i + 1, 0x83, 64) > i, "and out on channel 4");
    ring_cap = ring_used + 4;                 /* no room beyond the reserve */
    arr_key(65, 1, 100);
    run_blocks(1);
    arr_key(65, 0, 0);
    run_blocks(1);
    CHECK(find(out_log, out_n, 0, 0x93, 65) < 0 && find(out_log, out_n, 0, 0x83, 65) < 0 &&
              find(stub_log, stub_n, 0, 0x93, 65) >= 0, "no room: not out (nor its note-off), but heard");
}

/* ---- review fixes (sections and timing) */
uint32_t arr_test_tick_calls(void);           /* arr.c with ARR_TEST: tick() calls since arr_init */
/* the k-th note-on of channel ch, note n from log index `from`, and its note-off after it: their frames */
static int on_off(int st, int note, int k, uint32_t *on, uint32_t *off)
{
    for (int i = 0; i < stub_n; i++)
        if (stub_log[i].st == st && stub_log[i].d1 == note && is_on(&stub_log[i]) && k-- == 0) {
            *on = stub_log[i].frame;
            for (int j = i + 1; j < stub_n; j++)
                if (is_off(&stub_log[j]) && (stub_log[j].st & 15) == (st & 15) && stub_log[j].d1 == note) {
                    *off = stub_log[j].frame;
                    return 1;
                }
            return 0;
        }
    return 0;
}
/* the longest pattern (8 bars of 7): a note on its last tick (26879) longer than what is left, held over the
 * wrap; the pattern starts again on time */
static void test_long_wrap(void)
{
    const float fpt = 44100.f * 60.f / (240.f * ARR_PPQ);
    uint32_t on, off;
    reset();
    loops_reset();
    pat_new();
    P.bars = 8;
    P.beats = 7;
    add(0, 0, 1, 38);
    add(1, 0, 1, 36);
    P.ev[1].tick = 26879;
    P.ev[1].len = 500;
    arr_load_pattern(SA, &P);
    arr_set_tempo(240);
    arr_play(1);
    run_to((uint32_t)(27600.f * fpt));
    CHECK(on_off(0x99, 36, 0, &on, &off), "the last tick's note, and its off");
    CHECK(near(stub_log, find(stub_log, stub_n, 0, 0x99, 36), 26879.f * fpt), "on the last tick (%u, want %.0f)", on, 26879.f * fpt);
    CHECK((float)off > (26879.f + 500.f) * fpt - 130.f && (float)off < (26879.f + 500.f) * fpt + 130.f,
          "held 500 ticks, over the wrap (%u, want %.0f)", off, (26879.f + 500.f) * fpt);
    {
        int i = find(stub_log, stub_n, 0, 0x99, 38);
        i = find(stub_log, stub_n, i + 1, 0x99, 38);
        CHECK(near(stub_log, i, 26880.f * fpt), "the pattern again at 26880 (%u)", i >= 0 ? stub_log[i].frame : 0);
    }
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
}
/* notes of 1 to 5 ticks (shorter than a block): each note-off reaches the synth at or after its note-on */
static void test_tiny_notes(void)
{
    static const int bpm[] = {120, 240};
    for (int b = 0; b < 2; b++) {
        int bad = 0, n = 0;
        reset();
        loops_reset();
        pat_new();
        for (int i = 0; i < 40; i++) {
            add(0, 0, 1, 35 + i % 20);
            P.ev[P.nev - 1].tick = (uint16_t)(i * 37);
            P.ev[P.nev - 1].len = (uint16_t)(1 + i % 5);
        }
        arr_load_pattern(SA, &P);
        arr_set_tempo(bpm[b]);
        arr_play(1);
        run_to((uint32_t)(1480.f * 44100.f * 60.f / ((float)bpm[b] * ARR_PPQ)) + 512u);
        arr_play(0);
        run_blocks(2);
        for (int i = 0; i < 40; i++) {
            uint32_t on, off;
            if (on_off(0x99, 35 + i % 20, i / 20, &on, &off)) {
                n++;
                bad += off < on;
            }
        }
        CHECK(n == 40 && bad == 0, "%d BPM: 1-5 tick notes: %d of %d note-offs before their note-on", bpm[b], bad, n);
        CHECK(offs_match(stub_log, stub_n), "%d BPM: offs match", bpm[b]);
    }
}
/* arr_ticks wrapping round (after 25 days at 240 BPM) while a note is recorded */
static void test_ticks_wrap(void)
{
    reset();
    loops_reset();
    pattern_one();
    arr_load_pattern(SA, &P);
    arr_set_chord(ch(9, CH_MIN7));
    arr_record(5, 1);
    run_blocks(1);
    arr_ticks = 0xFFFFFFFFu - 600u;           /* the wrap between the key and its release */
    stub_frame = 0;
    arr_play(1);
    key_at(25.f, 69, 1);
    key_at(36.f, 69, 0);
    run_to((uint32_t)(60 * FPT));
    arr_pattern_copy(SA, &R);
    CHECK(arr_ticks < 2000u, "wrapped (%u)", arr_ticks);
    CHECK(R.trk[5].count == 1 && R.ev[R.trk[5].first].tick == 24 * U && within(R.ev[R.trk[5].first].len, 12 * U, KEY_TOL),
          "recorded across the wrap: at 24 units, 12 long (%d: %d %d)", R.trk[5].count, R.ev[R.trk[5].first].tick,
          R.ev[R.trk[5].first].len);
    arr_play(0);
    run_blocks(1);
}
/* MIDI clock in at 120 BPM from `from` for n clocks, each `jit` frames early or late at random (taken at the
 * block it falls in); returns the frame after the last */
static uint32_t rnd = 12345u;
static float clocks(float t, int n, float per, float jit)
{
    for (int k = 0; k < n; k++) {
        float at;
        rnd = rnd * 1103515245u + 12345u;
        at = t + jit * ((float)(rnd >> 16 & 1023u) / 512.f - 1.f);
        run_to((uint32_t)(at < 0 ? 0 : at));
        arr_clock(ARR_CLK_TICK);
        t += per;
    }
    return t;
}
/* the note-ons of channel 10 from log index `from`: frames, in order; returns how many */
static int drum_frames(int from, uint32_t *f, int max)
{
    int n = 0;
    for (int i = from; i < stub_n && n < max; i++)
        if (stub_log[i].st == 0x99 && is_on(&stub_log[i]))
            f[n++] = stub_log[i].frame;
    return n;
}
static void pattern_every(int step)           /* a note every `step` ticks for a bar (drums), 2 ticks long */
{
    pat_new();
    for (int t = 0; t < 4 * ARR_PPQ; t += step) {
        add(0, 0, 1, 36 + (t / step) % 8);
        P.ev[P.nev - 1].tick = (uint16_t)t;
        P.ev[P.nev - 1].len = 2;
    }
}
static void test_clock_in_more(void)
{
    static uint32_t f[2000];
    int n, bad, i0;
    float t, sum;
    /* jitter: clocks up to 250 frames early or late; the notes stay in order, spread, about right on average */
    reset();
    loops_reset();
    pattern_every(4);
    arr_load_pattern(SA, &P);
    arr_clock(ARR_CLK_START);
    t = clocks(256.f, 200, 918.75f, 250.f);
    run_to((uint32_t)t);
    n = drum_frames(0, f, 2000);
    bad = 0;
    sum = 0.f;
    for (int i = 41; i < n; i++) {
        bad += f[i] < f[i - 1];
        if (i < 900)
            sum += (float)(f[i] - f[i - 1]);
    }
    printf("  clock in with jitter: %d notes, %.1f frames apart (183.75)\n", n, sum / (900.f - 41.f));
    CHECK(n >= 990 && bad == 0 && sum / 859.f > 178.f && sum / 859.f < 190.f, "jitter: %d notes, %d out of order, %.1f apart",
          n, bad, sum / 859.f);
    /* the tempo changes (120 -> 180 BPM): within a beat the notes follow the new rate */
    i0 = stub_n;
    t = clocks(t, 96, 612.5f, 0.f);
    run_to((uint32_t)t);
    n = drum_frames(i0, f, 2000);
    sum = 0.f;
    for (int i = 200; i < n; i++)
        sum += (float)(f[i] - f[i - 1]);
    printf("  clock in 120 -> 180 BPM: %.1f frames apart after a beat (122.5)\n", sum / (float)(n - 200));
    CHECK(n > 400 && sum / (float)(n - 200) > 118.f && sum / (float)(n - 200) < 127.f, "180 BPM: %.1f apart", sum / (float)(n - 200));
    /* Continue: stopped mid-bar (FC), continued (FB): from where it was, at the next clock */
    {
        uint32_t bt;
        arr_clock(ARR_CLK_STOP);
        run_blocks(2);
        bt = arr_beat_tick;
        CHECK(arr_playing == 0, "stopped");
        arr_clock(ARR_CLK_CONTINUE);
        run_blocks(2);
        CHECK(arr_playing == 1 && arr_beat_tick == bt, "continued, waiting for a clock at %u (%u)", bt, arr_beat_tick);
        i0 = stub_n;
        t = clocks((float)stub_frame, 4, 918.75f, 0.f);
        run_to((uint32_t)t);
        CHECK(arr_beat_tick >= bt + 60u && arr_beat_tick <= bt + 80u, "on from there (%u, was %u)", arr_beat_tick, bt);
        CHECK(drum_frames(i0, f, 2000) >= 15, "playing on");
    }
    /* the clocks stop mid-beat (no FC), a long note held: nothing runs ahead of them; then our own clock
     * (0.6 s), the note ends, nothing left sounding after a stop */
    reset();
    loops_reset();
    pat_new();
    add(4, 0, 90, 60);
    add(0, 12, 1, 36);
    arr_load_pattern(SA, &P);
    arr_clock(ARR_CLK_START);
    t = clocks(256.f, 6, 918.75f, 0.f);
    run_to((uint32_t)t + 2000u);
    CHECK(arr_ticks == 6u * ARR_CLOCK_TICKS && arr_ext == 1, "stalled with the clocks (%u ticks)", arr_ticks);
    CHECK(find(stub_log, stub_n, 0, 0x99, 36) < 0, "the note at 12 units not played ahead of the clocks");
    run_to((uint32_t)t + 30000u + 44100u);
    CHECK(arr_ext == 0 && arr_playing == 1 && find(stub_log, stub_n, 0, 0x99, 36) >= 0, "then on our own clock");
    arr_play(0);
    run_blocks(2);
    CHECK(offs_match(stub_log, stub_n), "the held note ended (offs match)");
}
/* a few clocks bunched (5, from USB): played faster, not all at once */
static void test_clock_small_bunch(void)
{
    static uint32_t f[200];
    int n0, n, same = 0;
    float t;
    reset();
    loops_reset();
    pattern_every(4);
    arr_load_pattern(SA, &P);
    arr_clock(ARR_CLK_START);
    t = clocks(256.f, 40, 918.75f, 0.f);
    run_to((uint32_t)t);
    n0 = stub_n;
    for (int k = 0; k < 5; k++)
        arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    n = drum_frames(n0, f, 200);
    for (int i = 1; i < n; i++)
        same += f[i] == f[i - 1];
    CHECK(n >= 3 && same == 0, "5 bunched clocks: %d notes this block, %d on the same frame as the one before", n, same);
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
}
/* many clocks bunched (47: 940 ticks owed): the place jumps on (a few tick() calls, not 940), the notes that
 * would have ended in the jump end, nothing restruck */
static void test_catchup_jump(void)
{
    static const int lo[1] = {0}, se[1] = {ROW_A}, re[1] = {2};
    uint32_t c0, t0;
    int n0, offs = 0, rs = 0;
    reset();
    loops_reset();
    pat_new();
    add(4, 0, 30, 60);                        /* C4 to tick 600: ends inside the jump */
    for (int t = 0; t < 96; t++)
        add(0, t, 1, 35 + t % 8);
    arr_load_pattern(SA, &P);
    loop_clear(&LT[0], 0);
    loop_write(&LT[0], 0, ch(0, CH_MAJ7));
    loop_write(&LT[0], 1, ch(9, CH_MIN7));    /* on tick 480, inside the jump: set, not restruck */
    song_rows(1, lo, se, re);
    SG.at_end = 1;
    arr_mode_song(&SG, 0);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    c0 = arr_test_tick_calls();
    t0 = arr_ticks;
    n0 = stub_n;
    for (int k = 0; k < 47; k++)
        arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    for (int i = n0; i < stub_n; i++) {
        offs += stub_log[i].st == 0x82 && stub_log[i].d1 == 60;
        rs += stub_log[i].st == 0x92 && is_on(&stub_log[i]);
    }
    printf("  47 bunched clocks: %u ticks on in %u tick() calls\n", arr_ticks - t0, arr_test_tick_calls() - c0);
    CHECK(arr_ticks == 48u * ARR_CLOCK_TICKS, "all owed ticks moved on (%u)", arr_ticks);
    CHECK(arr_test_tick_calls() - c0 < 150u, "in %u tick() calls", arr_test_tick_calls() - c0);
    CHECK(offs == 1 && rs == 0, "the note that ended in the jump got its off (%d), none restruck (%d)", offs, rs);
    CHECK(arr_chord_unpack(arr_chord_now).root == 9, "the chord on the jumped beat set");
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "offs match");
}
/* a Fill during the Intro or the Ending is ignored (they play out); a stop during a fill restarts on its Main;
 * Continue after an Ending: from the start, as PLAY */
static void test_section_decisions(void)
{
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 2, 4);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(3.5f * BAR));
    CHECK(slot_at(BAR) == SI && slot_at(2 * BAR) == SA && slot_at(3 * BAR) == SA && arr_next_sect == SEC_SAME,
          "Fill during the Intro: ignored, the Intro into Main A (%d %d %d)", slot_at(BAR), slot_at(2 * BAR), slot_at(3 * BAR));
    arr_section(SEC_END);
    run_to((uint32_t)(4.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(7 * BAR));
    CHECK(slot_at(4 * BAR) == SE && slot_at(5 * BAR) == SE && arr_playing == 0, "Fill during the Ending: ignored, it ends (%d %d)",
          slot_at(4 * BAR), slot_at(5 * BAR));
    /* stopped in Fill AB: PLAY starts Main B (no Intro now) */
    empty_slot(SI);
    arr_play(1);
    run_to(stub_frame + (uint32_t)(0.5f * BAR));
    arr_section(SEC_FILL);
    run_to(stub_frame + (uint32_t)(0.7f * BAR));
    CHECK(arr_sect == SEC_FILL_AB, "in Fill AB (%d)", arr_sect);
    arr_play(0);
    run_blocks(1);
    arr_play(1);
    run_blocks(1);
    CHECK(arr_sect == SEC_B, "stopped in Fill AB: PLAY on Main B (%d)", arr_sect);
    arr_play(0);
    run_blocks(1);
    /* Continue after an Ending stopped it: the start again, as PLAY (the Intro first) */
    reset();
    loops_reset();
    load_slots(4);
    load_slot(SI, 1, 4);
    arr_clock(ARR_CLK_START);
    run_blocks(1);
    arr_section(SEC_END);
    for (int k = 0; k < 4 * 96 && arr_playing; k++) {
        arr_clock(ARR_CLK_TICK);
        run_blocks(1);
    }
    CHECK(arr_playing == 0, "the Ending stopped it");
    arr_clock(ARR_CLK_CONTINUE);
    run_blocks(1);
    CHECK(arr_playing == 1 && arr_sect == SEC_INTRO && arr_ticks >= 0u, "Continue after the Ending: the Intro, from its start (%d)",
          arr_sect);
    arr_clock(ARR_CLK_TICK);
    run_blocks(1);
    CHECK(arr_bar == 0 && arr_beat_tick < ARR_CLOCK_TICKS, "at bar 1 (%u %u)", arr_bar, arr_beat_tick);
    arr_clock(ARR_CLK_STOP);
    run_blocks(1);
}


int main(void)
{
    test_timing();
    test_tempos();
    test_lengths();
    test_restrike();
    test_mute();
    test_midi_out();
    test_clock_in();
    test_clock_smooth();
    test_stop_same_block();
    test_out_ring();
    test_reload();
    test_bounds();
    test_transport_out();
    test_sounds_first();
    test_clock_first();
    test_start_cap();
    test_section_on_bar();
    test_fill();
    test_ending();
    test_song();
    test_song_rows();
    test_song_forced_fill_mutes_key();
    test_song_intro_ending();
    test_song_hand();
    test_song_ending_wins();
    test_song_start_busy();
    test_song_busy_fill();
    test_song_row_edit();
    test_song_odd_bars();
    test_transpose_busy();
    test_song_more();
    test_live_chords();
    test_loop();
    test_loop_waltz();
    test_loop_switch();
    test_loop_override();
    test_loop_busy();
    test_transpose_held();
    test_loop_busy_override();
    test_loop_intro();
    test_loop_edges();
    test_song_marks_continue();
    test_keys_release();
    test_ending_twice();
    test_switch_keeps_voicing();
    test_catchup_no_restrike();
    test_section_clock_in();
    test_fill_variants();
    test_song_while_playing();
    test_intro();
    test_fill_fallbacks();
    test_no_ending();
    test_long_wrap();
    test_tiny_notes();
    test_ticks_wrap();
    test_clock_in_more();
    test_clock_small_bunch();
    test_catchup_jump();
    test_section_decisions();
    test_switch_chord_ends();
    test_inverse();
    test_record();
    test_record_bypass_erase();
    test_record_early();
    test_record_full();
    test_inverse_types();
    test_record_triplets();
    test_record_off_grid_start();
    test_record_busy();
    test_record_load();
    test_store();
    test_shorten();
    test_song_busy();
    test_record_section();
    test_live_keys();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("arr test passed\n");
    return 0;
}
