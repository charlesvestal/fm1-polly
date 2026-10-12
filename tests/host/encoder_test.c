/* SPDX-License-Identifier: GPL-3.0-only */
/* hal/fm1_input.h's quadrature decoder (fm1__frame) and key debounce (fm1__keys), the input layer Polly takes from
 * Felucca 1.5.1 (github.com/hugelton/Felucca, GPL-3.0; the timed cases follow its tests/input_test.c, issue #126).
 * The scan is the TIMER5 one (fm1_input_tick): a column every 100 us, a frame (11 columns) every 1.1 ms.
 * Encoders (encoder 0, a full quadrature cycle per detent):
 *   - frame by frame: every detent counted whether the scan sees each state for 4 frames, 2 or 1, or skips every
 *     other state (a flick); counter-clockwise; a one-frame glitch at rest counts nothing;
 *   - a knob held mid-click (the complement of the detent) for ~0.45 s, then turned on: one step a click, never two;
 *   - a still knob whose contacts chatter (one, or both together) never steps;
 *   - timed turns through the scan: 12 detents at 50..300 detents/s (even) and 50..150/s (bunched in half a click),
 *     each way, over 40 phases of the scan: every detent (the old decoder lost them above ~225/s); slow turns with
 *     contacts bouncing 1.5 ms: one step a click; 8 right then 6 left at once at 120/s.
 * Keys (every note key and button, at every phase of the scan):
 *   - a clean press counts within 2.3 ms (2 frames) and once; the release within 10 ms;
 *   - quick taps: 20 taps 30 ms apart, held 6 ms, bouncy: all heard (the old 8-frame debounce took ~9 ms to press);
 *   - 2000 bouncy presses: one press and one release each, no note ending while held, none hanging;
 *   - a 150 us glitch plays nothing. */
#include <stdio.h>
#include <string.h>
#include "fm1_input.h"

#define TICK_US 100u
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t rng = 0x12345u;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % n;
}

static void reset_in(void)
{
    int i;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    for (i = 0; i < (int)FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;
}

/* ---- encoders, frame by frame ---- */
static void frame(uint32_t st)                       /* encoder 0: A = col 0 bit 0, B = col 1 bit 0 */
{
    fm1_in.raw[0] = (uint8_t)((st >> 1) & 1u);
    fm1_in.raw[1] = (uint8_t)(st & 1u);
    fm1__frame();
}
static const uint32_t CW[4] = {1, 3, 2, 0};          /* 0 -> 1 -> 3 -> 2 -> 0: one detent clockwise */

static void reset(void)
{
    int i;
    reset_in();
    for (i = 0; i < 60; i++)                         /* at rest on state 0: the detent (the power-on state) */
        frame(0);
}

/* `det` detents, `per` frames on each state; skip = 1: only every other state is ever scanned */
static int turn(int det, int per, int skip, int dir)
{
    int d, k, p, steps;
    fm1_in.enc_steps[0] = 0;
    for (d = 0; d < det; d++)
        for (k = skip ? 1 : 0; k < 4; k += skip ? 2 : 1)
            for (p = 0; p < per; p++)
                frame(dir > 0 ? CW[k] : CW[(5 - k) & 3] ^ 0);
    for (k = 0; k < 60; k++)
        frame(0);
    steps = fm1_in.enc_steps[0];
    fm1_in.enc_steps[0] = 0;
    return steps;
}

/* ---- encoders, timed (after Felucca's input_test.c): the toggle times of encoder 0's A (0) and B (1) ---- */
#define ENE 8192
static struct { uint32_t t[ENE], n, i, s; } enl[2];
static void en_clear(void) { memset(enl, 0, sizeof enl); }
static void en_add(uint32_t l, uint32_t t) { if (enl[l].n < ENE && (!enl[l].n || t > enl[l].t[enl[l].n - 1u])) enl[l].t[enl[l].n++] = t; }
/* a turn of n detents from t0 (dir +1 clockwise: B A B A from the detent state 0, -1: A B A B), per us a detent,
 * its 4 transitions bunched over w permille of it (1000 = even), each edge bouncing (runs of 30..bmax us) for up
 * to bspan us (at most half the way to the next edge); returns the end */
static uint32_t en_turn(uint32_t t0, int32_t n, int32_t dir, uint32_t per, uint32_t w, uint32_t bspan, uint32_t bmax)
{
    int32_t i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 4; j++) {
            /* edge j at (500 - w/2 + (j + 0.5) w/4) permille of detent i; the next one a quarter of w on, or the
             * next detent's first */
            uint32_t f = 500u - w / 2u + (uint32_t)(2 * j + 1) * w / 8u;
            uint32_t nf = j < 3 ? f + w / 4u : 1000u + 500u - w / 2u + w / 8u;
            uint32_t t = t0 + (uint32_t)i * per + per * f / 1000u, gap = per * (nf - f) / 1000u;
            uint32_t l = (uint32_t)((j & 1) ^ (dir > 0)), end = t + (bspan < gap / 2u ? bspan : gap / 2u);
            en_add(l, t);
            while (bspan && bmax) {                  /* bounce: back and forth in pairs, the new level last */
                uint32_t a = t + 30u + rnd(bmax), b = a + 30u + rnd(bmax);
                if (b >= end)
                    break;
                en_add(l, a);
                en_add(l, b);
                t = b;
            }
        }
    return t0 + per * (uint32_t)n;
}
static uint32_t en_at(uint32_t l, uint32_t us)     /* (us only rises between en_clear and the next) */
{
    while (enl[l].i < enl[l].n && enl[l].t[enl[l].i] <= us) {
        enl[l].i++;
        enl[l].s ^= 1u;
    }
    return enl[l].s;
}
static int32_t en_run(uint32_t until)              /* the TIMER5 scan of encoder 0 up to `until` us: its steps */
{
    const uint8_t *m = FM1_ENC[0];
    uint32_t us, col = 0;
    for (us = 0; us < until; us += TICK_US) {
        fm1_in.raw[col] = 0;
        if (col == m[0])
            fm1_in.raw[col] |= (uint8_t)(en_at(0, us) << m[1]);
        if (col == m[2])
            fm1_in.raw[col] |= (uint8_t)(en_at(1, us) << m[3]);
        col = col + 1u == FM1_NCOL ? 0u : col + 1u;
        if (!col)
            fm1__frame();
    }
    return fm1_in.enc_steps[0];
}

/* ---- keys: one contact, a list of times (us) at which it toggles, starting open ---- */
#define NEDGE 1024
static struct { uint32_t t[NEDGE], n, i, s; } ct;
static void add_edge(uint32_t us) { if (ct.n < NEDGE) ct.t[ct.n++] = us; }
static int contact_at(uint32_t us)                  /* (us only rises within a run) */
{
    while (ct.i < ct.n && ct.t[ct.i] <= us) {
        ct.i++;
        ct.s ^= 1u;
    }
    return (int)ct.s;
}
/* a bouncy run from t (the first edge: the change), settled to `final` within `span` us; the last edge's time */
static uint32_t bounce(uint32_t t, uint32_t span, int final)
{
    uint32_t end = t + span, last = t;
    int s = final;
    add_edge(t);
    while (span) {
        uint32_t run = 30u + rnd(670u);
        if (t + run >= end)
            break;
        t += run;
        add_edge(t);
        s ^= 1;
        last = t;
    }
    if (s != final) {                              /* settle */
        t += 30u + rnd(200u);
        add_edge(t);
        last = t;
    }
    return last;
}
static uint32_t sim_us, sim_col;
static int key_col, key_row;
static void locate(uint32_t id)
{
    int r, p;
    for (r = 0; r < 6; r++)
        for (p = 0; p < (int)FM1_NCOL; p++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                key_row = r;
                key_col = p;
            }
}
static void tick(void)                             /* one TIMER5 tick, as fm1_input_tick: column p read, its keys */
{
    uint32_t p = sim_col;
    fm1_in.raw[p] = (uint8_t)((int)p == key_col && contact_at(sim_us) ? 1u << key_row : 0u);
    sim_col = p + 1u == FM1_NCOL ? 0u : p + 1u;
    fm1__keys(p);
    if (sim_col == 0u)
        fm1__frame();
    sim_us += TICK_US;
}
static uint32_t state_of(uint32_t id)
{
    return id >= 14u ? (fm1_in.notes >> (id - 14u)) & 1u : (fm1_in.buttons >> id) & 1u;
}
static void key_reset(void)
{
    reset_in();
    memset(&ct, 0, sizeof ct);
}
/* run to `until` us: rises / falls of id's state, its press edges */
static uint32_t rises, falls, edges, first_rise_us, last_fall_us, early_fall, held_from, held_to;
static void run(uint32_t id, uint32_t until)
{
    while (sim_us < until) {
        uint32_t was = state_of(id), now;
        tick();
        now = state_of(id);
        if (now && !was) {
            rises++;
            if (!first_rise_us)
                first_rise_us = sim_us;
        }
        if (!now && was) {
            falls++;
            last_fall_us = sim_us;
            if (sim_us > held_from && sim_us < held_to)
                early_fall++;
        }
        if (id >= 14u ? fm1_in.notes_pressed : fm1_in.pressed) {
            edges++;
            fm1_in.notes_pressed = fm1_in.pressed = 0;
        }
    }
}
static void counters_clear(void) { rises = falls = edges = first_rise_us = last_fall_us = early_fall = held_from = held_to = 0; }

int main(void)
{
    int per, got;
    static const uint32_t CCW[4] = {2, 3, 1, 0};
    /* ---- encoders, frame by frame ---- */
    for (per = 4; per >= 1; per /= 2) {
        reset();
        got = turn(10, per, 0, 1);
        CHECK(got == 10, "10 detents clockwise, %d frames a state: %d", per, got);
    }
    {   /* a flick: one detent seen whole, then every other state skipped */
        int k, d;
        reset();
        for (k = 0; k < 4; k++)
            frame(CW[k]);
        for (d = 0; d < 9; d++)
            for (k = 1; k < 4; k += 2)
                frame(CW[k]);
        for (k = 0; k < 60; k++)
            frame(0);
        got = fm1_in.enc_steps[0];
        CHECK(got == 10, "a flick (1 detent whole, then 9 with every other state skipped): %d", got);
    }
    {   /* counter-clockwise, one frame a state */
        int d, k;
        reset();
        for (d = 0; d < 10; d++)
            for (k = 0; k < 4; k++)
                frame(CCW[k]);
        for (k = 0; k < 60; k++)
            frame(0);
        got = fm1_in.enc_steps[0];
        CHECK(got == -10, "10 detents counter-clockwise, one frame a state: %d", got);
    }
    {   /* a bounce at rest: one frame of state 1, back to 0 */
        int k;
        reset();
        frame(1);
        frame(0);
        for (k = 0; k < 60; k++)
            frame(0);
        CHECK(fm1_in.enc_steps[0] == 0, "a one-frame glitch at rest counted %d", fm1_in.enc_steps[0]);
    }
    {   /* held mid-click (state 3, the detent's complement) for ~0.45 s, back, then 5 clicks: 5 steps, not 10 (the
         * old decoder took a state still for 40 frames as a second detent; now only ~1 s still is the detent) */
        int k;
        reset();
        frame(1);
        for (k = 0; k < 400; k++)
            frame(3);
        frame(1);
        frame(0);
        for (k = 0; k < 60; k++)
            frame(0);
        fm1_in.enc_steps[0] = 0;
        got = turn(5, 3, 0, 1);
        CHECK(got == 5, "5 clicks after the knob was held mid-click: %d", got);
    }
    {   /* a still knob, one contact chattering or both lost together (1..5 frames), ~1 min a rest state */
        uint32_t rest, mode, f, g, moved = 0;
        for (mode = 0; mode < 3u; mode++)
            for (rest = 0; rest < 4u; rest++) {
                reset_in();
                for (f = 0, g = 0; f < 50000u; f++) {
                    uint32_t st = rest;
                    if (g) {
                        st ^= mode == 0u ? 2u : mode == 1u ? 1u : 3u;
                        g--;
                    } else if (rnd(300) == 0u) {
                        g = 1u + rnd(5);
                    }
                    if (f < 100u)
                        st = rest;                     /* (the power-on state is the detent) */
                    frame(st);
                    moved |= fm1_in.enc_steps[0] != 0;
                }
            }
        CHECK(!moved, "a still knob with chattering contacts stepped");
    }
    /* ---- encoders, timed turns through the scan ---- */
    {
        static const uint32_t EVEN[] = {50, 100, 150, 200, 250, 300}, BUNCH[] = {50, 100, 150}, SLOW[] = {3, 8, 20, 40};
        uint32_t r, ph, d, bad = 0, n = 0;
        for (r = 0; r < 9u; r++)
            for (ph = 0; ph < 40u; ph++)
                for (d = 0; d < 2u; d++) {
                    int32_t dir = d ? -1 : 1, s;
                    uint32_t rate = r < 6u ? EVEN[r] : BUNCH[r - 6u];
                    reset_in();
                    en_clear();
                    s = en_run(en_turn(5000u + ph * 53u, 12, dir, 1000000u / rate, r < 6u ? 1000u : 500u, 0u, 0u) + 20000u);
                    n++;
                    if (s != 12 * dir) {
                        bad++;
                        if (bad <= 3u)
                            printf("  %u/s %s, phase %u: %d of %d\n", (unsigned)rate, r < 6u ? "even" : "bunched",
                                   (unsigned)ph, (int)s, (int)(12 * dir));
                    }
                }
        CHECK(!bad, "12-detent turns up to 300/s (even), 150/s (bunched): %u of %u miscounted", (unsigned)bad, (unsigned)n);
        for (bad = 0, r = 0; r < 4u; r++)
            for (ph = 0; ph < 20u; ph++)
                for (d = 0; d < 2u; d++) {
                    int32_t dir = d ? -1 : 1;
                    reset_in();
                    en_clear();
                    bad += en_run(en_turn(5000u + ph * 71u, 6, dir, 1000000u / SLOW[r], ph & 1u ? 1000u : 400u, 1500u, 400u) +
                                  20000u) != 6 * dir;
                }
        CHECK(!bad, "slow turns (3..40/s), contacts bouncing 1.5 ms: %u not one step a click", (unsigned)bad);
        for (bad = 0, ph = 0; ph < 40u; ph++) {
            uint32_t t;
            int32_t s1;
            reset_in();
            en_clear();
            t = en_turn(5000u + ph * 37u, 8, 1, 1000000u / 120u, 700u, 200u, 60u);   /* 8 right, 6 left at once */
            s1 = en_run(t + 2000u);
            t = en_turn(t + 2000u, 6, -1, 1000000u / 120u, 700u, 200u, 60u);
            reset_in();                                 /* (replayed from the start: the same scan) */
            enl[0].i = enl[1].i = enl[0].s = enl[1].s = 0;
            bad += s1 != 8 || en_run(t + 20000u) != 2;
        }
        CHECK(!bad, "120/s, bouncing 0.2 ms, 8 right then 6 left: %u not 8 then 2", (unsigned)bad);
    }
    /* ---- keys ---- */
    {   /* a clean press of every note key and button, at every phase of the scan */
        uint32_t id, ph, bad = 0, lat_max = 0, rel_max = 0;
        for (id = 0; id < FM1_NKEY; id++) {
            locate(id);
            for (ph = 0; ph < FM1_NCOL * TICK_US; ph += 37u) {
                uint32_t t0 = 10000u + ph;
                key_reset();
                sim_us = 0;
                sim_col = 0;
                add_edge(t0);
                add_edge(t0 + 100000u);
                counters_clear();
                held_from = t0;
                held_to = t0 + 100000u;
                run(id, t0 + 130000u);
                bad += rises != 1u || falls != 1u || edges != 1u;
                if (first_rise_us - t0 > lat_max)
                    lat_max = first_rise_us - t0;
                if (last_fall_us - (t0 + 100000u) > rel_max)
                    rel_max = last_fall_us - (t0 + 100000u);
            }
        }
        printf("keys: clean press -> counted in <= %u us, release -> off in <= %u us\n", (unsigned)lat_max, (unsigned)rel_max);
        CHECK(!bad, "clean presses: %u not one press and one release", (unsigned)bad);
        CHECK(lat_max <= 2300u, "a clean press took %u us (> 2 frames)", (unsigned)lat_max);
        CHECK(rel_max <= 10000u, "a release took %u us (> 10 ms)", (unsigned)rel_max);
    }
    {   /* quick taps: 20 taps 30 ms apart, held 6 ms (bouncing 1 ms on the way down, 1.5 ms up), every key */
        uint32_t id, t, bad = 0;
        for (id = 0; id < FM1_NKEY; id++) {
            locate(id);
            key_reset();
            sim_us = rnd(1100u);
            sim_col = rnd(FM1_NCOL);
            for (t = 0; t < 20u; t++) {
                bounce(5000u + t * 30000u, 1000u, 1);
                bounce(5000u + t * 30000u + 6000u, 1500u, 0);
            }
            counters_clear();
            run(id, 5000u + 20u * 30000u + 20000u);
            bad += rises != 20u || edges != 20u || falls != 20u;
        }
        CHECK(!bad, "20 quick taps (6 ms held, 30 ms apart): %u keys missed some", (unsigned)bad);
    }
    {   /* 2000 bouncy presses and releases */
        uint32_t k, onebad = 0, early = 0, hang = 0;
        for (k = 0; k < 2000u; k++) {
            uint32_t id = rnd(FM1_NKEY), t0, tb, tr, tl;
            locate(id);
            key_reset();
            sim_us = rnd(1100u);
            sim_col = rnd(FM1_NCOL);
            t0 = 5000u + rnd(1100u);
            tb = bounce(t0, rnd(3000u), 1);        /* settled closed from tb */
            tr = tb + 15000u + rnd(300000u);
            tl = bounce(tr, rnd(5000u), 0);        /* settled open from tl */
            counters_clear();
            held_from = tb;
            held_to = tr;
            run(id, tl + 12000u);
            onebad += rises != 1u || falls != 1u || edges != 1u;
            early += early_fall;
            hang += state_of(id);
        }
        CHECK(!onebad && !early && !hang, "2000 bouncy presses: %u not once, %u ended early, %u hanging",
              (unsigned)onebad, (unsigned)early, (unsigned)hang);
    }
    {   /* a stray closed sample: no note */
        uint32_t g, any = 0;
        locate(20);
        for (g = 0; g < FM1_NCOL * TICK_US; g += 13u) {
            key_reset();
            sim_us = 0;
            sim_col = 0;
            add_edge(5000u + g);
            add_edge(5150u + g);
            counters_clear();
            run(20, 40000u);
            any += rises + edges;
        }
        CHECK(!any, "a 150 us glitch played %u notes", (unsigned)any);
    }
    printf(fails ? "encoder + keys: %d FAILED\n" : "encoder + keys: ok\n", fails);
    return fails != 0;
}
