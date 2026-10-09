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
#include "templates.h"

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
    arr_load_pattern(SA, &P);
    arr_play(1);
    run_to(44100);
    CHECK(out_n == 0, "MIDI out off: nothing out (%d)", out_n);
}

static void test_clock_in(void)
{
    int i, k;
    reset();
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
    pattern_busy_downbeat(4, 1);              /* 40 sounds, FA, F8, 4 notes and their offs: fits 64 */
    arr_load_pattern(SA, &P);
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
    arr_load_pattern(SA, &P);                  /* the sounds now, the start a block later */
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
    pattern_busy_downbeat(8, 2);
    arr_load_pattern(SA, &P);
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

static song_t S;
static void song_new(int bars)
{
    memset(&S, 0, sizeof S);
    S.bars = (uint8_t)bars;
    for (int i = 0; i < ARR_SONGBARS * 4; i++)
        S.chord[i] = ch(0, CH_NTYPES);        /* no change */
}
static void test_song(void)
{
    reset();
    load_slots(4);
    song_new(4);
    S.sect[0] = SEC_A;
    S.sect[2] = SEC_B;
    S.chord[0] = ch(9, CH_MIN7);              /* bar 1: Am7 for two beats, F on beat 3 */
    S.chord[2] = ch(5, CH_MAJ);
    S.chord[4] = ch(7, CH_7);                 /* bar 2: G7 */
    S.chord[3 * 4 + 1] = ch(2, CH_MIN);       /* bar 4 beat 2: Dm */
    arr_song(&S, 0);
    arr_play(1);
    run_to((uint32_t)(5 * BAR));
    CHECK(root_at(0) == 9 && root_at(24 * FPT) == 9, "Am7, and no change keeps it (%d %d)", root_at(0), root_at(24 * FPT));
    CHECK(root_at(48 * FPT) == 5 && root_at(72 * FPT) == 5, "F on beat 3 (%d)", root_at(48 * FPT));
    CHECK(root_at(BAR) == 7 && root_at(2 * BAR) == 7, "G7 from bar 2 on (%d %d)", root_at(BAR), root_at(2 * BAR));
    CHECK(root_at(3 * BAR) == 7 && root_at(3 * BAR + 24 * FPT) == 2, "Dm on bar 4 beat 2 (%d)", root_at(3 * BAR + 24 * FPT));
    CHECK(slot_at(0) == SA && slot_at(BAR) == SA && slot_at(2 * BAR) == SB && slot_at(3 * BAR) == SB,
          "the section track: A A B B (%d %d %d %d)", slot_at(0), slot_at(BAR), slot_at(2 * BAR), slot_at(3 * BAR));
    CHECK(arr_playing == 0, "stopped at the end of its last bar");
    CHECK(ons_after(4 * BAR - 4) == 0, "nothing after bar 4");
    CHECK(offs_match(stub_log, stub_n), "song: offs match");
    /* looping: bar 5 is bar 1 again */
    reset();
    load_slots(4);
    arr_song(&S, 1);
    arr_play(1);
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(arr_playing == 1, "looping: still playing");
    CHECK(slot_at(4 * BAR) == SA && root_at(4 * BAR) == 9, "bar 1 again: A over Am7 (%d %d)", slot_at(4 * BAR), root_at(4 * BAR));
    CHECK(arr_song_bar == 1, "the song's bar for the screen (%d)", arr_song_bar);
    /* a fill in the song: one bar, then the other Main */
    reset();
    load_slots(4);
    song_new(3);
    S.sect[0] = SEC_B;
    S.sect[1] = SEC_FILL_BA;
    arr_song(&S, 0);
    arr_play(1);
    run_to((uint32_t)(3 * BAR + 512));
    CHECK(slot_at(0) == SB && slot_at(BAR) == SFB && slot_at(2 * BAR) == SA, "B, Fill BA, A (%d %d %d)", slot_at(0), slot_at(BAR), slot_at(2 * BAR));
    CHECK(arr_playing == 0, "stopped");
    /* no song: back to free play */
    reset();
    load_slots(4);
    arr_song(&S, 0);
    arr_song(0, 0);
    arr_play(1);
    run_to((uint32_t)(4 * BAR));
    CHECK(arr_playing == 1 && slot_at(3 * BAR) == SA, "song off: Main A loops");
    arr_play(0);
    run_blocks(1);
}

/* a pattern shortened while it plays, its place past the new end: it wraps within the new length and keeps
 * the bar's rhythm, so no bar line comes early and the song's bars stay on time */
static void test_shorten(void)
{
    reset();
    load_slot(SA, 2, 4);
    song_new(4);
    for (int b = 0; b < 4; b++)
        S.chord[b * 4] = ch(b + 1, CH_MAJ);   /* bar n: root n */
    arr_song(&S, 1);
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
    CHECK(root_at(2 * BAR) == 3 && root_at(3 * BAR) == 4, "bars 3 and 4 on time (%d %d)", root_at(2 * BAR),
          root_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
}

/* a song edit's copy in progress (the main loop interrupted midway) as a beat comes: the beat's chord still
 * plays (the render's own copy of the song is never the one being written) */
void arr_test_song_busy(int on);
static void test_song_busy(void)
{
    reset();
    load_slots(4);
    song_new(2);
    for (int i = 0; i < 8; i++)
        S.chord[i] = ch(i + 1, CH_MAJ);       /* beat n: root n + 1 */
    arr_song(&S, 1);
    arr_play(1);
    run_to((uint32_t)(36 * FPT));             /* beat 2, halfway */
    arr_test_song_busy(1);
    S.chord[3] = ch(11, CH_MAJ);              /* (what the main loop is writing) */
    run_to((uint32_t)(60 * FPT));             /* beat 3 passes */
    arr_test_song_busy(0);
    arr_song(&S, 1);
    run_to((uint32_t)(2 * BAR));
    CHECK(root_at(48 * FPT) == 3, "the beat during the copy plays its chord (%d)", root_at(48 * FPT));
    CHECK(root_at(72 * FPT) == 11, "the edit, once copied, from its beat (%d)", root_at(72 * FPT));
    CHECK(root_at(BAR) == 5, "on (%d)", root_at(BAR));
    arr_play(0);
    run_blocks(1);
}

static void test_templates(void)
{
    static const char *names[NTEMPLATES] = {"Pop", "Sensitive", "Fifties", "ii-V-I", "Blues", "Minor blues", "One-four",
                                            "Minor", "Canon", "Andalusian", "Mixolydian", "Doo-wop", "Jazz", "Vamp",
                                            "Gospel", "Waltz"};
    for (int t = 0; t < NTEMPLATES; t++) {
        const template_t *m = &TEMPLATES[t];
        int ok = m->nbars >= 2 && m->nbars <= 12 && (m->beats == 3 || m->beats == 4);
        for (int b = 0; b < m->nbars && ok; b++)
            ok = m->bars[b].root < 12 && m->bars[b].type < CH_NONE && m->bars[b].bass == CH_NOBASS;
        CHECK(ok && strcmp(m->name, names[t]) == 0, "template %d (%s)", t, m->name);
    }
    CHECK(TEMPLATES[4].nbars == 12 && TEMPLATES[15].beats == 3, "the blues 12 bars, the waltz in 3");
    /* Pop in D (key 2): D A Bm G, a chord a bar, looping */
    reset();
    load_slots(4);
    arr_set_key(2);
    arr_template(0);
    arr_play(1);
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(root_at(0) == 2 && root_at(72 * FPT) == 2, "bar 1: D (%d)", root_at(0));
    CHECK(root_at(BAR) == 9 && root_at(2 * BAR) == 11 && root_at(3 * BAR) == 7, "A Bm G (%d %d %d)", root_at(BAR),
          root_at(2 * BAR), root_at(3 * BAR));
    CHECK(root_at(4 * BAR) == 2 && root_at(5 * BAR) == 9, "loops (%d %d)", root_at(4 * BAR), root_at(5 * BAR));
    CHECK(arr_chord_unpack(arr_chord_now).root == 9 && arr_chord_unpack(arr_chord_now).type == CH_MAJ, "the chord for the screen");
    CHECK(arr_playing == 1, "a template loops on");
    arr_set_key(0);                           /* the key: the next beat in C */
    run_to((uint32_t)(5.5f * BAR + 512));
    CHECK(root_at(5.5f * BAR) == 7, "key C: G (%d)", root_at(5.5f * BAR));
    arr_template(-1);                         /* off: the chord stays */
    run_to((uint32_t)(7 * BAR + 512));
    CHECK(root_at(6 * BAR) == 7 && root_at(7 * BAR) == 7, "template off: the chord stays (%d)", root_at(7 * BAR));
    arr_play(0);
    run_blocks(1);
    /* the waltz in 3, on a pattern in 3, key F: F F C7 F */
    reset();
    load_slots(3);
    arr_set_key(5);
    arr_template(15);
    arr_play(1);
    run_to((uint32_t)(4 * 72 * FPT + 512));
    CHECK(root_at(0) == 5 && root_at(72 * FPT) == 5 && root_at(2 * 72 * FPT) == 0 && root_at(3 * 72 * FPT) == 5 &&
              root_at(4 * 72 * FPT) == 5, "the waltz in F, a chord a 3-beat bar (%d %d %d)", root_at(0), root_at(2 * 72 * FPT),
          root_at(3 * 72 * FPT));
    arr_play(0);
    run_blocks(1);
    /* picked while playing: its first chord at once, its bars with the pattern's */
    reset();
    load_slots(4);
    arr_set_chord(ch(7, CH_7));
    arr_play(1);
    run_to((uint32_t)(1.3f * BAR));
    CHECK(root_at(BAR) == 7, "G7 before the template");
    arr_template(2);                          /* Fifties in C: C Am Dm G7 */
    run_to((uint32_t)(3 * BAR + 512));
    CHECK(root_at(1.5f * BAR) == 0 && root_at(2 * BAR) == 9 && root_at(3 * BAR) == 2, "from the next beat; Am at the bar (%d %d %d)",
          root_at(1.5f * BAR), root_at(2 * BAR), root_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "templates: offs match");
}

static void test_live_chords(void)
{
    static const uint8_t am[] = {57, 60, 64}, e7[] = {52, 56, 59, 62}, two[] = {60, 64};
    reset();
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
    /* a live chord takes over from a template */
    arr_template(0);
    run_to((uint32_t)(3.1f * BAR));
    arr_keys_held(am, 3);
    run_to((uint32_t)(5 * BAR + 512));
    CHECK(root_at(4 * BAR) == 9 && root_at(5 * BAR) == 9, "the keys end the template (%d %d)", root_at(4 * BAR), root_at(5 * BAR));
    arr_play(0);
    run_blocks(1);
    CHECK(offs_match(stub_log, stub_n), "live chords: offs match");
}

/* ---- review fixes ---- */
static void test_song_marks_continue(void)    /* a mark on every bar of a region: the section plays on */
{
    reset();
    load_slots(4);
    pat_new();                                /* Main A in two bars: 36 in the first, 37 in the second */
    P.bars = 2;
    for (int t = 0; t < 192; t += 24)
        add(0, t, 6, t < 96 ? 36 : 37);
    arr_load_pattern(SA, &P);
    song_new(4);
    for (int b = 0; b < 4; b++)
        S.sect[b] = SEC_A;
    arr_song(&S, 0);
    arr_play(1);
    run_to((uint32_t)(4 * BAR));
    {
        int i = find(stub_log, stub_n, 0, 0x99, 37);
        CHECK(near(stub_log, i, BAR), "A's second bar plays under a second A mark (%d)", i);
    }
    /* END marked on both bars of a two-bar ending: it plays once and stops */
    reset();
    load_slots(4);
    song_new(8);
    for (int b = 0; b < 8; b++)
        S.sect[b] = SEC_END;
    arr_song(&S, 0);
    arr_play(1);
    run_to((uint32_t)(3 * BAR));
    CHECK(arr_playing == 0, "the ending stops under its own marks");
    CHECK(ons_after(2 * BAR - 4) == 0, "nothing after it");
}

static void test_keys_release(void)           /* lifting keys one by one keeps the chord */
{
    static const uint8_t g7[] = {55, 59, 62, 65}, no_g[] = {59, 62, 65}, no_b[] = {62, 65}, c[] = {60, 64, 67};
    reset();
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

static void test_template_now(void)           /* the template for the screen, stopped too */
{
    reset();
    arr_template(3);
    run_blocks(1);
    CHECK(arr_template_now == 3, "stopped: template 4 shown (%d)", arr_template_now);
    arr_template(-1);
    run_blocks(1);
    CHECK(arr_template_now == -1, "off");
}

static void test_ending_twice(void)           /* Ending pressed again in its last bar: it still stops */
{
    reset();
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
    int n0;
    reset();
    pat_new();
    add(4, 0, 90, 60);
    arr_load_pattern(SA, &P);
    song_new(4);
    S.chord[0] = ch(0, CH_MAJ7);
    S.chord[1] = ch(9, CH_MIN7);
    arr_song(&S, 1);
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
    /* queued while playing: from the next bar, once, then Main A; a fill from the Intro: Fill AB, then B */
    reset();
    load_slots(4);
    load_slot(SI, 1, 4);
    arr_section(SEC_A);                       /* PLAY with Main A queued: Main A, not the Intro */
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    CHECK(arr_sect == SEC_A, "Main A queued before PLAY: no Intro (%d)", arr_sect);
    arr_section(SEC_INTRO);
    run_to((uint32_t)(1.5f * BAR));
    arr_section(SEC_FILL);
    run_to((uint32_t)(4.5f * BAR));
    CHECK(slot_at(BAR) == SI && slot_at(2 * BAR) == SFA && slot_at(3 * BAR) == SB, "Intro, Fill AB, B (%d %d %d)",
          slot_at(BAR), slot_at(2 * BAR), slot_at(3 * BAR));
    arr_play(0);
    run_blocks(1);
    /* recording: PLAY starts with the pattern recorded into, not the Intro */
    reset();
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
    /* a song with no mark on its bar 1: the Intro first; one marked Main B: Main B */
    reset();
    load_slots(4);
    load_slot(SI, 1, 4);
    song_new(4);
    arr_song(&S, 0);
    arr_play(1);
    run_to((uint32_t)(2.5f * BAR));
    CHECK(slot_at(0) == SI && slot_at(BAR) == SA && arr_song_bar == 2, "a song: the Intro on its bar 1, then A (%d %d, bar %d)",
          slot_at(0), slot_at(BAR), arr_song_bar);
    arr_play(0);
    run_blocks(1);
    S.sect[0] = SEC_B;
    arr_song(&S, 0);
    arr_play(1);
    run_blocks(2);
    CHECK(arr_sect == SEC_B, "a song marked Main B on bar 1: B (%d)", arr_sect);
    arr_play(0);
    run_blocks(1);
    /* no Intro (no notes): PLAY and SEC_INTRO give Main A */
    reset();
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
    /* a song's Fill AB mark from Main B: Fill AB, then Main B */
    reset();
    load_slots(4);
    song_new(3);
    S.sect[0] = SEC_B;
    S.sect[1] = SEC_FILL_AB;
    arr_song(&S, 0);
    arr_play(1);
    run_to((uint32_t)(3 * BAR + 512));
    CHECK(slot_at(0) == SB && slot_at(BAR) == SFA && slot_at(2 * BAR) == SB, "B, Fill AB, B (%d %d %d)", slot_at(0),
          slot_at(BAR), slot_at(2 * BAR));
    /* a song's Intro mark mid-song: the Intro once, then A */
    reset();
    load_slots(4);
    load_slot(SI, 1, 4);
    song_new(4);
    S.sect[0] = SEC_B;
    S.sect[2] = SEC_INTRO;
    arr_song(&S, 0);
    arr_play(1);
    run_to((uint32_t)(4 * BAR + 512));
    CHECK(slot_at(BAR) == SB && slot_at(2 * BAR) == SI && slot_at(3 * BAR) == SA, "B, B, Intro, A (%d %d %d)", slot_at(BAR),
          slot_at(2 * BAR), slot_at(3 * BAR));
    CHECK(offs_match(stub_log, stub_n), "offs match");
}
static void test_no_ending(void)              /* an Ending without notes: stop at the bar line */
{
    reset();
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

static void test_song_while_playing(void)     /* a song set mid-bar: from its bar 1 at the next bar line */
{
    reset();
    load_slots(4);
    arr_play(1);
    run_to((uint32_t)(0.5f * BAR));
    song_new(4);
    S.sect[0] = SEC_B;
    S.chord[0] = ch(9, CH_MIN7);
    arr_song(&S, 0);
    run_to((uint32_t)(1.5f * BAR));
    CHECK(root_at(0.75f * BAR) == 0 && slot_at(0.75f * BAR) == SA, "the bar plays out as it was");
    CHECK(root_at(BAR) == 9 && slot_at(BAR) == SB, "the song's bar 1 at the bar line (%d %d)", root_at(BAR), slot_at(BAR));
    CHECK(arr_song_bar == 0, "its bar 1 (%d)", arr_song_bar);
    run_to((uint32_t)(6 * BAR));
    CHECK(arr_playing == 0 && ons_after(5 * BAR - 4) == 0, "four bars, then stopped");
}

static void test_template_beats(void)         /* a 4-beat template on a 3-beat pattern: a chord every 4 beats */
{
    reset();
    load_slots(3);
    arr_template(0);                          /* Pop in C: C G Am F */
    arr_play(1);
    run_to((uint32_t)(4 * 72 * FPT));
    CHECK(root_at(72 * FPT) == 0 && root_at(96 * FPT) == 7 && root_at(192 * FPT) == 9,
          "the template's own bars of 4 (%d %d %d)", root_at(72 * FPT), root_at(96 * FPT), root_at(192 * FPT));
    arr_play(0);
    run_blocks(1);
}

static void test_switch_chord_ends(void)      /* a chord change on a section's bar line ends A's chord notes */
{
    int i;
    reset();
    load_slots(4);
    pat_new();                                /* Main A: track 4 Chord 1, C4 held over the bar line */
    add(4, 72, 200, 60);
    arr_load_pattern(SA, &P);
    pat_new();
    arr_load_pattern(SB, &P);
    song_new(4);
    S.sect[0] = SEC_A;
    S.sect[1] = SEC_B;
    S.chord[0] = ch(0, CH_MAJ7);
    S.chord[4] = ch(9, CH_MIN7);
    arr_song(&S, 0);
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
    test_templates();
    test_live_chords();
    test_song_marks_continue();
    test_keys_release();
    test_template_now();
    test_ending_twice();
    test_switch_keeps_voicing();
    test_catchup_no_restrike();
    test_section_clock_in();
    test_fill_variants();
    test_song_while_playing();
    test_intro();
    test_fill_fallbacks();
    test_no_ending();
    test_template_beats();
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
