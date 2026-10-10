/* SPDX-License-Identifier: GPL-3.0-only */
/* The factory styles' chord parts against the chords they play over: every pattern of every style, played by
 * the real engine (arr.c, the GM stub recording what reaches the synth) as Main A of a song over each test
 * progression; each note of a Chord 1 / Chord 2 track that sounds an eighth or longer must be a tone of the
 * chord it sounds over, or one of that chord type's allowed tensions (ALLOWED). Deliberate exceptions go in
 * ALLOW (style, track, the interval over the root, the chord type).
 *   style_chords_test [-v]      (-v: every violation, else the first few) */
#include <stdio.h>
#include <string.h>
#include "arr.h"
#include "gm_stub.h"
#include "loops.h"

#define FPT 918.75f                           /* frames a 24th of a beat at 120 BPM: 44100 * 60 / (120 * 24) */
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st, (void)d1, (void)d2; }
uint32_t arr_midi_out_room(void) { return 64u; }
uint32_t arr_midi_out_link(void) { return 0u; }

static chord_t C_(int r, int t) { chord_t c = {(uint8_t)r, (uint8_t)t, CH_NOBASS}; return c; }
typedef struct {
    const char *name;
    int n;
    chord_t c[12];
} prog_t;
static prog_t PROGS[4];
static void progs_init(void)
{
    prog_t pop = {"Pop", 4, {C_(0, CH_MAJ), C_(7, CH_MAJ), C_(9, CH_MIN), C_(5, CH_MAJ)}};
    prog_t blues = {"Blues", 12, {C_(0, CH_7), C_(5, CH_7), C_(0, CH_7), C_(0, CH_7), C_(5, CH_7), C_(5, CH_7),
                                  C_(0, CH_7), C_(0, CH_7), C_(7, CH_7), C_(5, CH_7), C_(0, CH_7), C_(7, CH_7)}};
    prog_t minor = {"Andalusian", 4, {C_(9, CH_MIN), C_(7, CH_MAJ), C_(5, CH_MAJ), C_(4, CH_MAJ)}};
    prog_t types = {"ten types", 10, {C_(0, CH_MAJ), C_(2, CH_MIN), C_(7, CH_7), C_(5, CH_MAJ7), C_(9, CH_MIN7),
                                      C_(0, CH_6), C_(2, CH_SUS4), C_(11, CH_MIN7_B5), C_(1, CH_DIM), C_(3, CH_AUG)}};
    PROGS[0] = pop, PROGS[1] = blues, PROGS[2] = minor, PROGS[3] = types;
}

/* the tensions a part may hold over each type, besides its tones (semitones over the root) */
static uint16_t allowed(int t)
{
    uint16_t m = chord_tones(t);
    switch (t) {
    case CH_MAJ: case CH_ADD9: case CH_MAJ7: case CH_MAJ7_9: return m | 1u << 2 | 1u << 9;
    case CH_6: case CH_AUG: case CH_7_B13: case CH_7_S5: return m | 1u << 2;
    case CH_7: case CH_7_9: case CH_7_13: case CH_7_S11: case CH_7SUS4: case CH_SUS4: return m | 1u << 2 | 1u << 9;
    case CH_7_S9: case CH_7_B9: return m | 1u << 9;
    case CH_MIN: case CH_MADD9: case CH_MIN7: case CH_MIN7_9: case CH_MIN7_11: case CH_MIN6: case CH_MINMAJ7:
        return m | 1u << 2 | 1u << 5;
    case CH_MIN7_B5: return m | 1u << 5 | 1u << 8;
    case CH_DIM: return m | 1u << 2 | 1u << 5 | 1u << 8 | 1u << 11;
    default: return m;
    }
}

/* deliberate exceptions: a style's track may hold this interval over this chord type */
static const struct { const char *style; int trk, interval, type; } ALLOW[] = {
    {"", -1, -1, -1},
};
static int allow_listed(const char *style, int trk, int iv, int type)
{
    for (unsigned i = 0; i < sizeof ALLOW / sizeof ALLOW[0]; i++)
        if (!strcmp(ALLOW[i].style, style) && ALLOW[i].trk == trk && ALLOW[i].interval == iv && ALLOW[i].type == type)
            return 1;
    return 0;
}

static const char *const SECT[ARR_NSECT] = {"Intro", "Main A", "Main B", "Fill AB", "Fill BA", "Ending"};
static const char *const TRK[8] = {"d1", "d2", "pc", "ba", "c1", "c2", "c3", "c4"};
static const char *const NOTE[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static pattern_t P;
static loop_t LT[ARR_LOOPS];
static int verbose, violations, checked, shown, patterns;

int main(int argc, char **argv)
{
    static int32_t buf[2 * 256];
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    progs_init();
    for (int st = 0; st < arr_styles(); st++)
        for (int k = 0; k < ARR_NSECT; k++) {
            int trk_of[16], start[16][128];
            if (arr_factory_pattern(ARR_FACTORY0 + st * ARR_NSECT + k, &P) != 0)
                return 1;
            if (!P.nev)
                continue;                     /* a section the style does not have */
            patterns++;
            for (int c = 0; c < 16; c++)
                trk_of[c] = -1;
            for (int t = 0; t < ARR_TRACKS; t++)
                if ((P.trk[t].x.type == XT_CHORD1 || P.trk[t].x.type == XT_CHORD2) && P.trk[t].count)
                    trk_of[P.trk[t].chan & 15] = t;
            for (int pi = 0; pi < 4; pi++) {
                const prog_t *g = &PROGS[pi];
                int bars = g->n * P.bars, beats = P.beats;
                uint32_t barf, end;
                if (bars > loop_max_bars(beats))
                    bars = loop_max_bars(beats);
                loop_clear(&LT[0], 0);
                LT[0].beats = (uint8_t)beats;
                for (int b = 0; b < bars; b++)
                    loop_write(&LT[0], b * beats, g->c[b % g->n]);
                arr_init();
                stub_n = 0;
                stub_frame = 0;
                arr_set_tempo(120);
                arr_load_pattern(SEC_A - 1, &P);
                arr_loops(LT);
                arr_mode_loop(0);
                arr_play(1);
                memset(start, 0xFF, sizeof start);
                barf = (uint32_t)(FPT * 24.0f * (float)beats + 0.5f);
                end = barf * (uint32_t)bars + barf / 2u;
                while (stub_frame < end) {
                    gm_render(buf, 256, 4096);
                    for (int i = 0; i < stub_n; i++) {
                        const stub_msg_t *m = &stub_log[i];
                        int c = m->st & 15, ty = m->st & 0xF0, t = trk_of[c];
                        if (t < 0 || (ty != 0x90 && ty != 0x80))
                            continue;
                        if (ty == 0x90 && m->d2) {
                            start[c][m->d1] = (int)m->frame;
                            continue;
                        }
                        if (start[c][m->d1] < 0)
                            continue;
                        {
                            uint32_t s0 = (uint32_t)start[c][m->d1], mid = (s0 + m->frame) / 2u;
                            int b = (int)(mid / barf), iv, ok;
                            chord_t ch;
                            start[c][m->d1] = -1;
                            if (m->frame <= s0)    /* the same pitch struck again (two notes folded onto it) */
                                continue;
                            if ((float)(m->frame - s0) < 12.0f * FPT - 300.0f || b >= bars)
                                continue;          /* shorter than an eighth (a block's slack allowed) */
                            ch = g->c[b % g->n];
                            iv = ((int)m->d1 - ch.root + 120) % 12;
                            ok = (allowed(ch.type) >> iv & 1u) || allow_listed(arr_style_name(st), t, iv, ch.type);
                            checked++;
                            if (!ok) {
                                violations++;
                                if (verbose || shown++ < 12) {
                                    char nm[16];
                                    chord_name(ch, nm);
                                    printf("FAIL %s %s %s over %s, bar %d (%s): %s%d is %d over the root\n",
                                           arr_style_name(st), SECT[k], TRK[t], g->name, b + 1, nm, NOTE[m->d1 % 12],
                                           m->d1 / 12 - 1, iv);
                                }
                            }
                        }
                    }
                    stub_n = 0;
                }
            }
        }
    printf("%d chord-part notes checked in %d patterns of %d styles, %d not chord tones or allowed tensions\n", checked,
           patterns, arr_styles(), violations);
    return violations ? 1 : 0;
}
