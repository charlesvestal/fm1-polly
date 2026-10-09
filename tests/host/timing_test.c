/* SPDX-License-Identifier: GPL-3.0-only */
/* The real GM synth (gm.c + Sonivox EAS) and the arranger: notes start on their exact sample.
 *
 *   ref:       a render of plain MIDI (every note at a block start) is bit-identical to the unmodified
 *              engine's (REF_HASH, taken from the engine before the start offsets went in)
 *   offsets:   an isolated note started k samples into a block (k 0..127, six sounds) is silent before k
 *              and sounds from output frame 2k (+-2; plus the sound's own first-sample silence at k = 0)
 *   attack:    a note started k samples in renders its first 128 - k samples exactly as one started at 0
 *   steal:     a note-on stealing a voice started in the same block starts on its own sample; one stealing
 *              a sounding voice at the next block's start
 *   live:      the arranger's held note-offs go before the live notes of a block (the real gm_render)
 *   arranger:  hats from the arranger at 97, 120, 133 BPM start on their exact frame (+-2), also off the old
 *              24-a-beat grid (480 ticks a beat)
 *   sends:     loud notes with full reverb / chorus sends sum past 16 bits; the effects get them clamped
 *              (VMSendClamp), not wrapped round (which burst into noise)
 *   tails:     after the last note the reverb rings out (no drop to silence, falling gradually), and a note
 *              after a long silence renders exactly as one after a different note's long silence (the
 *              effects start clean: nothing frozen replays)
 *   bench:     10 s of a dense pattern, the render's time (timing_test bench: the numbers only)
 *
 *   timing_test           the checks
 *   timing_test print     the reference hash of this build
 *   timing_test bench     the benchmark */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "arr.h"
#include "gm.h"
#include "eas_data.h"
#include "eas_synth.h"

void *gm_host_eas(void);                           /* gm.c, GM_HOST: the synth's state */
static S_VOICE_MGR *vm(void) { return ((S_EAS_DATA *)gm_host_eas())->pVoiceMgr; }
static int voice_of(int ch, int note)             /* the voice playing (or about to) ch / note; -1 none */
{
    for (int v = 0; v < MAX_SYNTH_VOICES; v++) {
        S_SYNTH_VOICE *p = &vm()->voices[v];
        if (p->voiceState == eVoiceStateFree)
            continue;
        if (p->voiceState == eVoiceStateStolen ? (p->nextChannel == ch && p->nextNote == note) : (p->channel == ch && p->note == note))
            return v;
    }
    return -1;
}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
uint32_t arr_midi_out_room(void) { return 1000; }
uint32_t arr_midi_out_link(void) { return 0; }

#define BLK 256                                   /* output frames a render call (the FM-1's) */
static int32_t buf[2 * BLK];

static uint32_t fnv(uint32_t h, const int32_t *p, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = (uint32_t)p[i];
        for (int b = 0; b < 4; b++) {
            h ^= v & 0xFFu;
            h *= 16777619u;
            v >>= 8;
        }
    }
    return h;
}

/* plain MIDI through the queue (so at block starts): programs, sends, bends, the mod wheel, drums, more notes
 * than voices (stealing), note-offs; 3 s */
#define REF_HASH 0xecb193f6u
static uint32_t ref_render(void)
{
    uint32_t h = 2166136261u;
    static const uint8_t prog[4] = {0, 48, 81, 33};
    for (int c = 0; c < 4; c++) {
        gm_midi(0xC0u | (uint32_t)c, prog[c], 0);
        gm_midi(0xB0u | (uint32_t)c, 91, (uint32_t)(20 * c));
        gm_midi(0xB0u | (uint32_t)c, 93, (uint32_t)(10 * c));
        gm_midi(0xB0u | (uint32_t)c, 10, (uint32_t)(30 + 20 * c));
    }
    for (int b = 0; b < 520; b++) {                 /* 520 blocks of 256 frames: 3 s */
        if (b % 13 == 0)
            for (int c = 0; c < 4; c++)
                gm_midi(0x90u | (uint32_t)c, (uint32_t)(48 + c * 5 + (b / 13) % 12), (uint32_t)(60 + c * 15));
        if (b % 13 == 9)
            for (int c = 0; c < 4; c += 2)
                gm_midi(0x80u | (uint32_t)c, (uint32_t)(48 + c * 5 + (b / 13) % 12), 0);
        if (b % 7 == 0)
            gm_midi(0x99u, (uint32_t)(35 + (b / 7) % 12), 100);
        if (b % 31 == 5)
            gm_midi(0xE1u, 0, (uint32_t)(32 + (b / 31) * 7 % 64));
        if (b % 17 == 3)
            gm_midi(0xB2u, 1, (uint32_t)((b * 5) & 127));
        gm_render(buf, BLK, 4096);
        h = fnv(h, buf, 2 * BLK);
    }
    gm_panic();
    for (int b = 0; b < 200; b++)                    /* let everything end */
        gm_render(buf, BLK, 4096);
    return h;
}

/* a dense pattern: 3 drum tracks on 16ths, 5 chord tracks on 8ths. Ticks and lengths here in units of a 24th of
 * a beat (U ticks) */
#define U (ARR_PPQ / 24)
static pattern_t P;
static void add(int trk, int tick, int len, int note, int vel)
{
    arr_trk_t *t = &P.trk[trk];
    arr_ev_t e = {(uint16_t)(tick * U), (uint16_t)(len * U), (uint8_t)note, (uint8_t)vel};
    if (!t->count)
        t->first = P.nev;
    t->count++;
    P.ev[P.nev++] = e;
}
static void trk_init(int i, int chan, int program, int type, int rev, int cho)
{
    arr_trk_t *t = &P.trk[i];
    t->chan = (uint8_t)chan;
    t->program = (uint8_t)program;
    t->x.type = (uint8_t)type;
    t->x.lo = 0;
    t->x.hi = 127;
    t->x.hikey = XT_NOKEY;
    t->vol = 100;
    t->pan = 64;
    t->rev = (uint8_t)rev;
    t->cho = (uint8_t)cho;
}
static void dense_pattern(void)
{
    memset(&P, 0, sizeof P);
    P.bars = 1;
    P.beats = 4;
    for (int i = 0; i < ARR_TRACKS; i++)
        trk_init(i, i < 3 ? 9 : i - 2, i < 3 ? 0 : 10 * i, i < 3 ? XT_BYPASS : XT_CHORD1, 40, 20);
    for (int tick = 0; tick < 96; tick += 6) {
        add(2, tick, 5, 42, 90);
        if (tick % 24 == 0)
            add(0, tick, 12, 36, 120);
        if (tick % 48 == 24)
            add(1, tick, 6, 38, 110);
    }
    for (int i = 3; i < ARR_TRACKS; i++)
        for (int tick = 0; tick < 96; tick += 12)
            add(i, tick, 22, 60 + (i - 3) * 4, 90);
}

static int bench_voices;
static double bench(void)
{
    double best = 1e9;
    for (int rep = 0; rep < 5; rep++) {
        clock_t t0;
        double s;
        arr_init();
        dense_pattern();
        arr_load_pattern(SEC_A - 1, &P);
        arr_set_tempo(133);
        arr_play(1);
        t0 = clock();
        for (int b = 0; b < 44100 * 10 / BLK; b++)
        {
            gm_render(buf, BLK, 4096);
            if (gm_voices > bench_voices)
                bench_voices = gm_voices;
        }
        s = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (s < best)
            best = s;
        arr_play(0);
        for (int b = 0; b < 400; b++)
            gm_render(buf, BLK, 4096);
    }
    return best;
}

/* ------------------------------------------------------------ a note on its sample --- */
static uint32_t frame_now;                         /* output frames rendered since the start */
static int32_t trace[6 * 44100];                   /* 6 s from mark(): nonzero where either side is */
static uint32_t trace_from, trace_n;
#define SNAP_BLOCKS 8
static int32_t snap[2 * BLK * SNAP_BLOCKS];         /* the first blocks from mark(), both sides */
static void mark(void) { trace_from = frame_now; trace_n = 0; }
static void render_blocks(int n)
{
    while (n-- > 0) {
        gm_render(buf, BLK, 4096);
        if (trace_n < BLK * SNAP_BLOCKS)
            memcpy(&snap[2 * trace_n], buf, sizeof buf);
        for (int i = 0; i < BLK; i++)
            if (trace_n < sizeof trace / sizeof trace[0])
                trace[trace_n++] = buf[2 * i] | buf[2 * i + 1];    /* nonzero when either side is */
        frame_now += BLK;
    }
}
static void quiet(void)                            /* until nothing sounds: the voices, and the effects' tails */
{
    int n = 0;
    while ((gm_voices || vm()->reverbTail || vm()->chorusTail) && n++ < 4000)
        render_blocks(1);
    render_blocks(2);
}
static int first_sound(uint32_t from)              /* the first nonzero frame of the trace from `from`; -1 none */
{
    for (uint32_t i = from; i < trace_n; i++)
        if (trace[i])
            return (int)i;
    return -1;
}
static void sends_off(uint32_t ch)
{
    gm_midi(0xB0u | ch, 91, 0);
    gm_midi(0xB0u | ch, 93, 0);
}

/* an isolated note started at sample k of a block: silent before, sounding from k (output frame 2k, +-2) */
static void test_offsets(void)
{
    static const struct { uint8_t ch, prog, note; const char *name; } snd[] = {
        {0, 0, 60, "piano C4"}, {1, 16, 64, "organ E4"}, {2, 56, 72, "trumpet C5"},
        {9, 0, 38, "snare"}, {9, 0, 42, "closed hat"}, {9, 0, 36, "kick"},
    };
    static const int ks[] = {0, 1, 37, 64, 100, 127};
    for (int s = 0; s < (int)(sizeof snd / sizeof snd[0]); s++) {
        int f0 = -1;
        if (snd[s].ch != 9)
            gm_midi(0xC0u | snd[s].ch, snd[s].prog, 0);
        sends_off(snd[s].ch);
        for (int j = 0; j < (int)(sizeof ks / sizeof ks[0]); j++) {
            int k = ks[j], f;
            quiet();
            mark();
            render_blocks(1);
            gm_midi_now(0x90u | snd[s].ch, snd[s].note, 127, (uint32_t)k);   /* due k samples into the next block */
            render_blocks(4);
            gm_midi_now(0x80u | snd[s].ch, snd[s].note, 0, 0);
            f = first_sound(0) - BLK;
            if (k == 0)
                f0 = f;
            printf("  %-11s k %3d: first sound at output frame %4d of its block (want %d)\n", snd[s].name, k, f, 2 * k + f0);
            CHECK(first_sound(0) >= BLK, "%s k %d: silent before its block", snd[s].name, k);
            CHECK(f >= 2 * k - 2 && f <= 2 * k + 2 + f0, "%s k %d: first sound at %d, want %d (+-2)", snd[s].name, k, f, 2 * k);
        }
    }
}

/* the attack is the same wherever a note starts: the first 128 - k samples of a note started k samples into a
 * block equal a note started on a block's, sample for sample (EAS's 22.05 kHz samples are gm.c's odd output
 * frames). Each render from a fresh synth: Sonivox leaves a voice's fractional sample phase where its last
 * note left it (WT_StartVoice does not reset phaseFrac), so a note's sub-sample phase depends on the voice's
 * history; a fresh synth gives both notes the same */
static void fresh(uint32_t ch, uint32_t prog)
{
    gm_init();                                     /* the host pool starts over (eas_host_fm1.c) */
    if (ch != 9)
        gm_midi(0xC0u | ch, prog, 0);
    sends_off(ch);
    render_blocks(2);
}
static int32_t snap0[2 * BLK * SNAP_BLOCKS];
static void test_attack(void)
{
    static const struct { uint8_t ch, prog, note; const char *name; } snd[] = {
        {0, 0, 60, "piano C4"}, {1, 16, 64, "organ E4"}, {2, 56, 72, "trumpet C5"},
        {9, 0, 38, "snare"}, {9, 0, 42, "closed hat"}, {9, 0, 36, "kick"},
    };
    static const int ks[] = {1, 37, 64, 100, 127};
    for (int s = 0; s < (int)(sizeof snd / sizeof snd[0]); s++) {
        int32_t g1 = 0;
        for (int j = -1; j < (int)(sizeof ks / sizeof ks[0]); j++) {
            int k = j < 0 ? 0 : ks[j], diff = 0, sum0 = 0, sumk = 0, v;
            int32_t g, want;
            fresh(snd[s].ch, snd[s].prog);
            mark();
            render_blocks(1);
            gm_midi_now(0x90u | snd[s].ch, snd[s].note, 127, (uint32_t)k);
            render_blocks(1);
            /* the gain carried into the second block: at k 0 the first block's target g1; at k the ramp's
             * value after 128 - k of its 128 steps, (g1 / 128 in Q16) * (128 - k), not g1 */
            v = voice_of(snd[s].ch, snd[s].note);
            g = v >= 0 ? vm()->voices[v].gain : -1;
            if (k == 0)
                g1 = g;
            want = (int32_t)(((int64_t)g1 * 65536 / 128 * (128 - k)) / 65536);
            CHECK(g == want && (k == 0 || g1 <= 1 || g != g1), "%s k %d: gain carried %d, want %d (g1 %d)", snd[s].name, k, (int)g, (int)want, (int)g1);
            render_blocks(SNAP_BLOCKS - 2);
            gm_midi_now(0x80u | snd[s].ch, snd[s].note, 0, 0);
            if (k == 0) {
                memcpy(snap0, snap, sizeof snap);
                continue;
            }
            for (int i = 0; i < GM_BLOCK - k; i++)
                for (int c = 0; c < 2; c++) {
                    int32_t a = snap0[2 * (BLK + 2 * i + 1) + c], b = snap[2 * (BLK + 2 * (i + k) + 1) + c];
                    diff += a != b;
                    if (i < 8) {
                        sum0 += a < 0 ? -a : a;
                        sumk += b < 0 ? -b : b;
                    }
                }
            CHECK(diff == 0, "%s k %d: the first %d samples as at k 0 (%d differ)", snd[s].name, k, GM_BLOCK - k, diff);
            CHECK(sum0 == sumk, "%s k %d: the first 8 samples' level %d, at k 0 %d", snd[s].name, k, sumk, sum0);
        }
    }
}

/* a note-on that steals a voice started in the same block (a third C4 on a channel: Sonivox keeps two) starts
 * on its own sample: C4 at 20, 50, 100 (the one at 20 stolen) sounds as C4 at 50 and 100 alone */
static void test_steal(void)
{
    static const int ks[][3] = {{20, 50, 100}, {0, 10, 127}, {90, 30, 60}};
    for (int t = 0; t < 3; t++) {
        int diff = 0, first = -1;
        for (int pass = 0; pass < 2; pass++) {
            fresh(0, 0);
            mark();
            render_blocks(1);
            for (int n = pass; n < 3; n++)          /* pass 0: all three; pass 1: the two that stay */
                gm_midi_now(0x90u, 60, 100, (uint32_t)ks[t][n]);
            render_blocks(SNAP_BLOCKS - 1);
            gm_midi_now(0x80u, 60, 0, 0);
            gm_midi_now(0x80u, 60, 0, 0);
            if (pass == 0)
                memcpy(snap0, snap, sizeof snap);
        }
        for (int i = 0; i < 2 * BLK * SNAP_BLOCKS; i++)
            diff += snap0[i] != snap[i];
        for (int i = 0; i < BLK * SNAP_BLOCKS && first < 0; i++)
            if (snap0[2 * i] | snap0[2 * i + 1])
                first = i - BLK;
        printf("  steal %3d %3d %3d: first sound at frame %d of its block, %d samples differ from the two alone\n",
               ks[t][0], ks[t][1], ks[t][2], first, diff);
        CHECK(diff == 0, "steal %d %d %d: as the two notes alone (%d samples differ)", ks[t][0], ks[t][1], ks[t][2], diff);
    }
}

/* a voice still sounding when stolen: it fades out over the block the new note was due in, and the new note
 * starts at the next block's start (its offset not carried there): C4 at block 0, then C4 at 30 and C4 at 90
 * in block 5 (the one from block 0 stolen) */
static void test_steal_sounding(void)
{
    int v, vs;
    int32_t g1;
    fresh(0, 0);                                   /* g1: a C4's first-block gain, started on a block */
    gm_midi_now(0x90u, 60, 100, 0);
    render_blocks(1);
    v = voice_of(0, 60);
    g1 = v >= 0 ? vm()->voices[v].gain : -1;
    fresh(0, 0);
    gm_midi_now(0x90u, 60, 100, 0);
    render_blocks(5);
    vs = voice_of(0, 60);
    gm_midi_now(0x90u, 60, 100, 30);
    gm_midi_now(0x90u, 60, 100, 90);
    CHECK(vs >= 0 && vm()->voices[vs].voiceState == eVoiceStateStolen && vm()->voices[vs].nextStartDelay == 90,
          "the sounding C4's voice stolen for the one at 90");
    render_blocks(1);                              /* block 5: it fades */
    CHECK(vs >= 0 && vm()->voices[vs].voiceState == eVoiceStateStolen && vm()->voices[vs].nextStartDelay == 0,
          "after its fade block, still stolen, the offset dropped");
    render_blocks(1);                              /* block 6: the new note from the block's start */
    CHECK(vs >= 0 && vm()->voices[vs].voiceState != eVoiceStateStolen && vm()->voices[vs].note == 60 &&
              vm()->voices[vs].gain == g1,
          "the new C4 started at block 6's start: a full first block (gain %d, want %d)", vs >= 0 ? vm()->voices[vs].gain : -1, (int)g1);
}

/* a note-off the arranger holds for the next block goes before the live MIDI of that block: through the real
 * gm_render, an organ C4 from the arranger (tick 0, 1 tick: its off due 150 frames into block 3, held for
 * block 4) and a live C4 on the same channel posted for block 4 keep sounding */
static void test_live_after_held_off(void)
{
    int v;
    fresh(3, 16);
    memset(&P, 0, sizeof P);
    P.bars = 1;
    P.beats = 4;
    for (int i = 0; i < ARR_TRACKS; i++)
        trk_init(i, i < 3 ? 9 : i - 2, 16, XT_BYPASS, 0, 0);
    trk_init(0, 3, 16, XT_BYPASS, 0, 0);
    add(0, 0, 1, 60, 100);
    arr_init();
    arr_load_pattern(SEC_A - 1, &P);
    arr_set_tempo(120);
    arr_play(1);
    render_blocks(4);                              /* blocks 0..3: the C4, its off held */
    gm_midi(0x93u, 60, 100);                       /* the live C4, for block 4 */
    render_blocks(1);
    arr_play(0);
    render_blocks(40);                             /* 0.23 s */
    v = voice_of(3, 60);
    CHECK(v >= 0 && vm()->voices[v].voiceState == eVoiceStatePlay, "the live C4 still sounds (voice %d, state %d)",
          v, v >= 0 ? vm()->voices[v].voiceState : -1);
    gm_midi(0x83u, 60, 0);
    render_blocks(1);
    quiet();
}

/* the arranger on the real synth: a hat at ticks 0, 25, 49, 74 of a bar, two bars, at tempos whose ticks fall
 * all over the blocks: each hit starts on its exact frame (+-2) */
static void test_arranger(void)
{
    static const int bpm[] = {97, 120, 133};
    static const int hits[] = {0, 25 * U + 7, 49 * U + 7, 74 * U + 7};   /* ticks: off the old 24-a-beat grid */
    for (int b = 0; b < 3; b++) {
        float fpt = 110250.f / (float)bpm[b];
        int found = 0, bad = 0, worst = 0, run = 1000;
        memset(&P, 0, sizeof P);
        P.bars = 1;
        P.beats = 4;
        for (int i = 0; i < ARR_TRACKS; i++)
            trk_init(i, i < 3 ? 9 : i - 2, 0, i < 3 ? XT_BYPASS : XT_CHORD1, 0, 0);
        for (int i = 0; i < 4; i++) {
            add(0, 0, 1, 42, 127);
            P.ev[P.nev - 1].tick = (uint16_t)hits[i];
        }
        quiet();
        arr_init();
        arr_load_pattern(SEC_A - 1, &P);
        arr_set_tempo(bpm[b]);
        arr_play(1);
        mark();                                    /* tick 0: the next block's start */
        render_blocks((int)(192.f * fpt) / BLK + 1);
        arr_play(0);
        for (uint32_t i = 0; i < trace_n; i++) {   /* onsets: sound after 64 frames of silence */
            if (trace[i] && run >= 64) {
                int n = found % 4, bar = found / 4;
                float want = (float)(hits[n] + 96 * U * bar) * fpt / (float)U, d = (float)i - want;
                int ad = (int)(d < 0 ? -d - 0.5f : d + 0.5f);
                if (ad > worst)
                    worst = ad;
                if (ad > 2)
                    bad++;
                found++;
            }
            run = trace[i] ? 0 : run + 1;
        }
        printf("  arranger %3d BPM: %d hits, worst %d frames off their exact time\n", bpm[b], found, worst);
        CHECK(found >= 8, "%d BPM: 8 hits heard (and the third bar's first), got %d", bpm[b], found);
        CHECK(bad == 0, "%d BPM: %d hits more than 2 frames off (worst %d)", bpm[b], bad, worst);
        quiet();
    }
}

void VMSendClamp(const EAS_I32 *sum, EAS_PCM *out);    /* eas_voicemgt.c (Polly) */
static void test_sends(void)
{
    static EAS_I32 sum[2 * GM_BLOCK];
    static EAS_PCM out[2 * GM_BLOCK];
    int32_t most = 0, bad = 0;
    quiet();
    for (uint32_t c = 0; c < 8; c++) {           /* a loud saw cluster, every send full */
        gm_midi(0xC0u | c, 81, 0);
        gm_midi(0xB0u | c, 7, 127);
        gm_midi(0xB0u | c, 11, 127);
        gm_midi(0xB0u | c, 91, 127);
        gm_midi(0xB0u | c, 93, 127);
        for (uint32_t n = 0; n < 4; n++)
            gm_midi(0x90u | c, 40 + c * 5 + n * 3, 127);
    }
    for (int b = 0; b < 40; b++) {
        render_blocks(1);
        for (int i = 0; i < 2 * GM_BLOCK; i++) {
            int32_t v = vm()->reverbSendSum[i];
            v = v < 0 ? -v : v;
            most = v > most ? v : most;
        }
    }
    CHECK(most > 32767, "full sends sum past 16 bits (the case the clamp is for): most %d", (int)most);
    for (int i = 0; i < 2 * GM_BLOCK; i++)
        sum[i] = (i - GM_BLOCK) * 700;           /* -89600 .. 88900 */
    VMSendClamp(sum, out);
    for (int i = 0; i < 2 * GM_BLOCK; i++) {
        int32_t w = sum[i] > 32767 ? 32767 : sum[i] < -32768 ? -32768 : sum[i];
        bad += out[i] != w;
    }
    CHECK(bad == 0, "the sends clamped to 16 bits: %d samples not", (int)bad);
    for (uint32_t c = 0; c < 8; c++) {
        gm_midi(0xB0u | c, 123, 0);
        sends_off(c);
    }
    quiet();
}

static double rms(const int32_t *b, int n)       /* n stereo frames */
{
    double a = 0;
    for (int i = 0; i < 2 * n; i++)
        a += (double)b[i] * (double)b[i];
    return a / (2 * n) > 0 ? __builtin_sqrt(a / (2 * n)) : 0;
}
static void test_tails(void)
{
    double w[10];
    int blocks = 0, minblk = -1;
    double minr = 1e30;
    quiet();
    gm_midi(0xC0u, 0, 0);
    gm_midi(0xB0u, 91, 127);
    gm_midi(0xB0u, 93, 0);
    gm_midi(0x90u, 60, 110);
    render_blocks(52);                           /* 300 ms */
    gm_midi(0x80u, 60, 0);
    while (gm_voices && blocks++ < 2000)
        render_blocks(1);
    for (int i = 0; i < 10; i++) {               /* 50 ms windows (9 blocks of 256 frames) */
        double a = 0;
        for (int b = 0; b < 9; b++) {
            double r;
            render_blocks(1);
            r = rms(buf, BLK);
            a += r * r;
            if (i == 0 && r < minr) {
                minr = r;
                minblk = b;
            }
        }
        w[i] = __builtin_sqrt(a / 9);
    }
    CHECK(minr > 1.0, "the tail sounds through the first 50 ms after the last voice (block %d: rms %.1f)", minblk, minr);
    for (int i = 0; i < 9; i++)
        CHECK(w[i + 1] > w[i] * 0.25 && w[i + 1] < w[i] * 1.5, "the tail falls gradually: window %d rms %.1f, then %.1f",
              i, w[i], w[i + 1]);
    CHECK(w[9] < w[0], "the tail falls (%.1f to %.1f)", w[0], w[9]);
    sends_off(0);
    quiet();
}
static uint32_t note_after_silence(int prog_before)
{
    uint32_t h = 2166136261u;
    gm_midi(0xC0u, (uint32_t)prog_before, 0);    /* a different note first, its tail rung out */
    gm_midi(0xB0u, 91, 127);
    gm_midi(0xB0u, 93, 127);
    gm_midi(0x90u, 48 + (uint32_t)prog_before % 24u, 127);
    render_blocks(40);
    gm_midi(0x80u, 48 + (uint32_t)prog_before % 24u, 0);
    quiet();
    /* Sonivox keeps a free voice's phase fraction, filter state and gains from its last note (upstream; heard
     * as nothing, but not bit-exact): as fresh, so only the effects' state can differ */
    for (int v = 0; v < NUM_WT_VOICES; v++) {
        vm()->wtVoices[v].filter.z1 = vm()->wtVoices[v].filter.z2 = 0;
        vm()->wtVoices[v].phaseFrac = 0;
        vm()->wtVoices[v].gainLeft = vm()->wtVoices[v].gainRight = 0;
    }
    gm_midi(0xC0u, 0, 0);                        /* then the same note */
    gm_midi(0x90u, 64, 100);
    for (int b = 0; b < 60; b++) {
        render_blocks(1);
        h = fnv(h, buf, 2 * BLK);
    }
    gm_midi(0x80u, 64, 0);
    sends_off(0);
    quiet();
    return h;
}
int gm_host_fx_settled(void);                    /* gm.c, GM_HOST */
/* the reverb switched off while it rings: its level glides down over a few blocks (no click), then it idles */
static void test_fx_off(void)
{
    double r0, r1;
    quiet();
    gm_fx(2, 100, 0, 0);
    gm_midi(0xB0u, 91, 127);
    gm_midi(0x90u, 60, 110);
    render_blocks(40);
    gm_midi(0x80u, 60, 0);
    while (gm_voices)
        render_blocks(1);
    render_blocks(1);
    r0 = rms(buf, BLK);
    gm_fx(0, 100, 0, 0);
    render_blocks(1);
    r1 = rms(buf, BLK);
    CHECK(r0 > 1.0 && r1 > r0 * 0.2, "switched off: the tail glides down, not cut (rms %.1f, then %.1f)", r0, r1);
    render_blocks(4);
    CHECK(gm_host_fx_settled() && vm()->reverbTail == 0, "then idle (tail %d)", vm()->reverbTail);
    sends_off(0);
    quiet();
}
static void test_clean_start(void)
{
    uint32_t a = note_after_silence(81), b = note_after_silence(48);
    CHECK(a == b, "a note after a long silence: the same, whatever rang before (0x%08x, 0x%08x)", a, b);
}

int main(int argc, char **argv)
{
    uint32_t h;
    if (gm_init() != 0) {
        printf("FAIL: gm_init\n");
        return 1;
    }
    arr_init();
    h = ref_render();
    if (argc > 1 && !strcmp(argv[1], "print")) {
        printf("ref hash 0x%08x\n", h);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "bench")) {
        static const struct { uint8_t rt, rl, cl; const char *name; } fx[4] = {
            {2, 90, 80, "reverb and chorus"}, {2, 90, 0, "reverb, chorus off"}, {0, 90, 80, "chorus, reverb off"},
            {0, 90, 0, "both off"}};
        {                                       /* silence, the tails rung out: the effects idle */
            clock_t t0;
            quiet();
            t0 = clock();
            for (int b = 0; b < 44100 * 10 / BLK; b++)
                gm_render(buf, BLK, 4096);
            printf("10 s of silence, the effects idle: %.1f ms to render\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
        }
        for (int i = 0; i < 4; i++) {           /* every track sends to both (40, 20); the master decides */
            double s;
            gm_fx(fx[i].rt, fx[i].rl, 0, fx[i].cl);
            s = bench();
            printf("10 s of a dense pattern (up to %d voices), %s: %.1f ms to render (best of 5)\n", bench_voices,
                   fx[i].name, s * 1000.0);
        }
        return 0;
    }
    test_offsets();
    test_attack();
    test_steal();
    test_steal_sounding();
    test_live_after_held_off();
    test_arranger();
    test_sends();
    test_tails();
    test_clean_start();
    test_fx_off();                               /* last: it sets the effects (gm_fx) */
    CHECK(h == REF_HASH, "plain MIDI renders as the unmodified engine: hash 0x%08x, want 0x%08x", h, REF_HASH);
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
