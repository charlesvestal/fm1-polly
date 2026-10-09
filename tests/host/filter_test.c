/* SPDX-License-Identifier: GPL-3.0-only */
/* CC74 (brightness) and CC71 (harmonic content) on the real GM synth (gm.c + Sonivox EAS): they move the voices'
 * low-pass filter (eas_wtsynth.c WT_UpdateFilter, eas_filter.c).
 *
 *   darker:    CC74 low takes the highs out (the high-band share of the energy falls), on a sound without a
 *              filter of its own (EAS bypasses it) and on one with; CC74 at 64 after it the sound as before
 *   resonance: CC71 high gives a peak: the response near the cutoff against the low end rises (the source's
 *              own spectrum cancelled: CC71 127 over CC71 64, band by band)
 *   neutral:   CC74 / CC71 sent at 64, or moved and back at 64: a note renders bit-identical to one never moved
 *   reset:     Reset All Controllers (CC121) puts both back to 64 (and the sound as it is)
 *   zipper:    a held note under CC74 / CC71 sweeps (one value a block) and jumps (127 -> 0 -> 127, a sound
 *              without a filter of its own switched in at 63 and out at 64): no clicks where the coefficients
 *              change (at the block edges, the second difference no larger than inside the blocks)
 *   start:     a note started under CC74 low on a voice a loud resonant note left: the same as on a fresh one
 *              (the filter's history zeroed at its start)
 *   extremes:  every GM program and the drum kit at CC74 0 / 127 x CC71 0 / 127, loud: no blow-up (the level
 *              at most ~2x the sound as it is; the 16-bit filter saturates, not wraps), and silent after
 *
 *   filter_test           the checks */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "arr.h"
#include "gm.h"
#include "eas_data.h"
#include "eas_synth.h"

void *gm_host_eas(void);
static S_VOICE_MGR *vm(void) { return ((S_EAS_DATA *)gm_host_eas())->pVoiceMgr; }
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
uint32_t arr_midi_out_room(void) { return 1000; }
uint32_t arr_midi_out_link(void) { return 0; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define BLK 256                                   /* output frames a render call */
static int32_t buf[2 * BLK];
#define MAXS (22050 * 3)
static float rec[MAXS];                           /* the left side at EAS's 22.05 kHz (the odd output frames) */
static int nrec;

static void cc(uint32_t ch, uint32_t c, uint32_t v) { gm_midi(0xB0u | ch, c, v); }
static void render(int blocks)
{
    while (blocks-- > 0) {
        gm_render(buf, BLK, 4096);
        for (int i = 0; i < BLK / 2 && nrec < MAXS; i++)
            rec[nrec++] = (float)buf[2 * (2 * i + 1)];
    }
}
static void fresh(uint32_t ch, uint32_t prog)
{
    gm_init();
    gm_fx(0, 0, 0, 0);
    if (ch != 9)
        gm_midi(0xC0u | ch, prog, 0);
    cc(ch, 91, 0);
    cc(ch, 93, 0);
    render(2);
    nrec = 0;
}
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
static uint32_t note_hash(uint32_t ch, uint32_t note, int blocks)
{
    uint32_t h = 2166136261u;
    gm_midi(0x90u | ch, note, 110);
    for (int b = 0; b < blocks; b++) {
        gm_render(buf, BLK, 4096);
        h = fnv(h, buf, 2 * BLK);
    }
    gm_midi(0x80u | ch, note, 0);
    return h;
}
static int art_cutoff(uint32_t ch, uint32_t note)  /* the sounding voice's articulation cutoff (0: EAS bypasses) */
{
    for (int v = 0; v < MAX_SYNTH_VOICES; v++)
        if (vm()->voices[v].voiceState != eVoiceStateFree && vm()->voices[v].channel == ch && vm()->voices[v].note == note)
            return vm()->pSynth[0]->pEAS->pArticulations[vm()->wtVoices[v].artIndex].filterCutoff;
    return -1;
}

/* the share of energy above ~2 kHz: the first difference's energy over the signal's (a high-pass's share) */
static double hf_share(const float *x, int n)
{
    double e = 0, d = 0;
    for (int i = 1; i < n; i++) {
        e += (double)x[i] * x[i];
        d += (double)(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    }
    return e > 0 ? d / e : 0;
}
static double rms(const float *x, int n)
{
    double e = 0;
    for (int i = 0; i < n; i++)
        e += (double)x[i] * x[i];
    return n ? sqrt(e / n) : 0;
}

/* a held note, CC74 = v from its start; 0.5 s, the last 0.3 s measured */
static double held(uint32_t ch, uint32_t prog, uint32_t note, int c74, int c71, double *level, int *fc)
{
    fresh(ch, prog);
    cc(ch, 74, (uint32_t)c74);
    cc(ch, 71, (uint32_t)c71);
    gm_midi(0x90u | ch, note, 110);
    render(1);
    if (fc)
        *fc = art_cutoff(ch, note);
    render(85);
    gm_midi(0x80u | ch, note, 0);
    if (level)
        *level = rms(rec + nrec - 6615, 6615);
    return hf_share(rec + nrec - 6615, 6615);
}
static void test_darker(void)
{
    static const struct { uint8_t prog, note; const char *name; } snd[] = {
        {81, 57, "saw lead"}, {61, 60, "brass section"}, {56, 67, "trumpet"}, {0, 60, "piano"}, {16, 60, "organ"},
        {6, 60, "harpsichord"}, {30, 52, "dist. guitar"}, {48, 60, "strings"},    /* no filter of their own */
    };
    for (int s = 0; s < (int)(sizeof snd / sizeof snd[0]); s++) {
        double l64, l20, l0, h64, h20, h0, h96;
        int fc;
        h64 = held(0, snd[s].prog, snd[s].note, 64, 64, &l64, &fc);
        h96 = held(0, snd[s].prog, snd[s].note, 96, 64, NULL, NULL);
        h20 = held(0, snd[s].prog, snd[s].note, 20, 64, &l20, NULL);
        h0 = held(0, snd[s].prog, snd[s].note, 0, 64, &l0, NULL);
        printf("  %-13s (own cutoff %5d): high share %.4f at CC74 64, %.4f at 96, %.4f at 20, %.4f at 0; level %.0f, %.0f, %.0f\n",
               snd[s].name, fc, h64, h96, h20, h0, l64, l20, l0);
        CHECK(h20 < h64 * 0.5, "%s: CC74 20 darker (high share %.4f, at 64 %.4f)", snd[s].name, h20, h64);
        CHECK(h0 < h20 && h0 < h64 * 0.15, "%s: CC74 0 darker still (%.4f, at 20 %.4f)", snd[s].name, h0, h20);
        CHECK(h96 >= h64 * 0.9, "%s: CC74 96 not darker (%.4f, at 64 %.4f)", snd[s].name, h96, h64);
        CHECK(l20 > l64 * 0.1 && l20 < l64 * 2, "%s: CC74 20 still sounds (level %.0f, at 64 %.0f)", snd[s].name, l20, l64);
    }
}

/* band energies over 1/3 octaves from 50 Hz (a plain DFT of 8192 samples, Hann) */
#define NB 24
static void bands(const float *x, double *e)
{
    enum { N = 8192 };
    static double w[N];
    memset(e, 0, NB * sizeof *e);
    for (int i = 0; i < N; i++)
        w[i] = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / N));
    for (int k = 1; k < N / 2; k++) {
        double f = 22050.0 * k / N, re = 0, im = 0, c = cos(2 * M_PI * k / N), s = sin(2 * M_PI * k / N), cr = 1, ci = 0;
        int b = (int)floor(3 * log2(f / 50));
        if (b < 0 || b >= NB)
            continue;
        for (int i = 0; i < N; i++) {             /* the twiddle by recurrence, renormalised now and then */
            re += w[i] * cr;
            im -= w[i] * ci;
            double t = cr * c - ci * s;
            ci = cr * s + ci * c;
            cr = t;
            if ((i & 255) == 255) {
                double m = sqrt(cr * cr + ci * ci);
                cr /= m;
                ci /= m;
            }
        }
        e[b] += re * re + im * im;
    }
}
static void test_resonance(void)
{
    static const struct { uint8_t prog, note, c74; const char *name; } snd[] = {
        {81, 36, 40, "saw lead"}, {61, 36, 50, "brass section"}, {38, 36, 64, "synth bass"}, {30, 36, 40, "dist. guitar"},
        {48, 36, 64, "strings"},
    };
    for (int s = 0; s < (int)(sizeof snd / sizeof snd[0]); s++) {
        double a[NB], b[NB], r[NB], peak = 0, low;
        int pb = 0;
        held(0, snd[s].prog, snd[s].note, snd[s].c74, 64, NULL, NULL);
        bands(rec + nrec - 8192, a);
        held(0, snd[s].prog, snd[s].note, snd[s].c74, 127, NULL, NULL);
        bands(rec + nrec - 8192, b);
        for (int i = 0; i < NB; i++) {
            r[i] = a[i] > 1e3 && b[i] > 1e3 ? sqrt(b[i] / a[i]) : 0;
            if (r[i] > peak) {
                peak = r[i];
                pb = i;
            }
        }
        low = r[1] > 0 ? r[1] : r[2];             /* 63..100 Hz: the notes' fundamental (C2) */
        printf("  %-13s CC74 %d: CC71 127 over 64: %.2f at %.0f Hz, %.2f at the fundamental: a %.1f dB peak\n",
               snd[s].name, snd[s].c74, peak, 50 * pow(2, (pb + 0.5) / 3), low, 20 * log10(peak / (low > 0 ? low : 1e-9)));
        CHECK(low > 0 && peak / low > 4.0, "%s: CC71 127 gives a resonant peak (%.1f dB over the low end)", snd[s].name,
              20 * log10(peak / (low > 0 ? low : 1e-9)));
    }
}

static void test_neutral(void)
{
    static const uint8_t progs[] = {81, 61, 0, 48};
    for (int p = 0; p < 4; p++) {
        uint32_t h0, h1, h2;
        fresh(0, progs[p]);
        h0 = note_hash(0, 60, 60);
        fresh(0, progs[p]);
        cc(0, 74, 64);
        cc(0, 71, 64);
        h1 = note_hash(0, 60, 60);
        fresh(0, progs[p]);
        cc(0, 74, 10);                            /* moved under a note, then back: the voices glide home */
        cc(0, 71, 120);
        gm_midi(0x90u, 64, 110);
        render(20);
        cc(0, 74, 64);
        cc(0, 71, 64);
        render(60);
        gm_midi(0x80u, 64, 0);
        render(400);
        for (int v = 0; v < NUM_WT_VOICES; v++) { /* as fresh: Sonivox keeps a free voice's phase fraction */
            vm()->wtVoices[v].phaseFrac = 0;
            vm()->wtVoices[v].filter.z1 = vm()->wtVoices[v].filter.z2 = 0;
            vm()->wtVoices[v].gainLeft = vm()->wtVoices[v].gainRight = 0;
        }
        h2 = note_hash(0, 60, 60);
        CHECK(h0 == h1, "program %d: CC74 / CC71 at 64 bit-identical (0x%08x, 0x%08x)", progs[p], h0, h1);
        CHECK(h0 == h2, "program %d: moved and back at 64 bit-identical (0x%08x, 0x%08x)", progs[p], h0, h2);
    }
}

static void test_reset(void)
{
    uint32_t h0, h1;
    fresh(0, 81);
    h0 = note_hash(0, 60, 40);
    fresh(0, 81);
    cc(0, 74, 5);
    cc(0, 71, 127);
    render(1);                                    /* MIDI goes through gm.c's queue: taken at a render */
    CHECK(vm()->pSynth[0]->channels[0].brightness == 5 && vm()->pSynth[0]->channels[0].harmonic == 127, "CC74 / CC71 taken");
    cc(0, 121, 0);
    render(1);
    CHECK(vm()->pSynth[0]->channels[0].brightness == 64 && vm()->pSynth[0]->channels[0].harmonic == 64,
          "CC121 resets CC74 / CC71 (%d, %d)", vm()->pSynth[0]->channels[0].brightness, vm()->pSynth[0]->channels[0].harmonic);
    h1 = note_hash(0, 60, 40);
    CHECK(h0 == h1, "after CC121 the sound as it is (0x%08x, 0x%08x)", h0, h1);
    cc(0, 74, 5);
    render(1);
    gm_init();                                    /* a system reset */
    CHECK(vm()->pSynth[0]->channels[0].brightness == 64 && vm()->pSynth[0]->channels[0].harmonic == 64, "reset: CC74 / CC71 at 64");
}

/* a click from the coefficients changing shows at the block edges (EAS updates them once a block, 128 samples):
 * the largest second difference there over the largest inside the blocks; ~1 for a sound unmoved, 3..4 with
 * CC74 jumps taken in one block (without the glide) */
static double edge_ratio(const float *x, int n)
{
    double edge = 0, in = 0;
    for (int i = 2; i < n; i++) {
        double d = fabs((double)x[i] - 2.0 * x[i - 1] + x[i - 2]);
        int ph = i % GM_BLOCK;
        if (ph <= 3 || ph >= GM_BLOCK - 2)
            edge = d > edge ? d : edge;
        else
            in = d > in ? d : in;
    }
    return in > 0 ? edge / in : 0;
}
static void test_zipper(void)
{
    static const struct { uint8_t prog, note; const char *name; } snd[] = {
        {73, 72, "flute"}, {79, 72, "ocarina"}, {19, 60, "church organ"}, {52, 64, "choir aahs"}, {81, 48, "saw lead"},
        {61, 55, "brass section"},
    };
    static const uint8_t j74[] = {127, 0, 64, 0, 127, 63, 64, 63}, j71[] = {64, 127, 0, 127, 64, 64, 64, 90};
    for (int s = 0; s < (int)(sizeof snd / sizeof snd[0]); s++) {
        double ref, sweep, jump;
        int fc;
        fresh(0, snd[s].prog);                    /* the note unmoved */
        gm_midi(0x90u, snd[s].note, 110);
        render(1);
        fc = art_cutoff(0, snd[s].note);
        render(39);
        nrec = 0;
        render(512);
        ref = edge_ratio(rec, nrec);
        gm_midi(0x80u, snd[s].note, 0);
        fresh(0, snd[s].prog);                    /* swept: one CC74 value a block (5.8 ms), 64 -> 127 -> 0 -> 127 -> 64,
                                                   * CC71 up as it goes */
        gm_midi(0x90u, snd[s].note, 110);
        render(40);
        nrec = 0;
        for (int i = 0; i < 512; i++) {
            int v = i < 64 ? 64 + i : i < 191 ? 127 - (i - 64) : i < 318 ? i - 191 : 127 - (i - 318) / 3;
            cc(0, 74, (uint32_t)v);
            cc(0, 71, (uint32_t)(64 + i / 16));
            render(1);
        }
        sweep = edge_ratio(rec, nrec);
        gm_midi(0x80u, snd[s].note, 0);
        fresh(0, snd[s].prog);                    /* jumps: 127 -> 0 -> 64 -> 0 -> 127 -> 63 -> 64, CC71 0 / 127 */
        gm_midi(0x90u, snd[s].note, 110);
        render(40);
        nrec = 0;
        for (int i = 0; i < 8; i++) {
            cc(0, 74, j74[i]);
            cc(0, 71, j71[i]);
            render(64);
        }
        jump = edge_ratio(rec, nrec);
        gm_midi(0x80u, snd[s].note, 0);
        printf("  %-13s (own cutoff %5d) block-edge clicks: unmoved %.2f, swept %.2f, jumps %.2f\n", snd[s].name, fc, ref,
               sweep, jump);
        CHECK(sweep <= 1.5 && sweep <= ref + 0.5, "%s: a CC74 / CC71 sweep does not zipper (%.2f, unmoved %.2f)", snd[s].name, sweep, ref);
        CHECK(jump <= 1.5 && jump <= ref + 0.5, "%s: CC74 / CC71 jumps glide, no clicks (%.2f, unmoved %.2f)", snd[s].name, jump, ref);
    }
}

static void test_start(void)
{
    uint32_t h0, h1;
    fresh(0, 81);
    cc(0, 74, 20);
    h0 = note_hash(0, 60, 40);
    fresh(0, 81);
    cc(0, 74, 127);                               /* a loud resonant note first, its filter ringing at the rails */
    cc(0, 71, 127);
    cc(0, 7, 127);
    gm_midi(0x90u, 36, 127);
    render(60);
    gm_midi(0x80u, 36, 0);
    render(400);
    cc(0, 7, 100);
    cc(0, 74, 20);
    cc(0, 71, 64);
    render(200);
    for (int v = 0; v < NUM_WT_VOICES; v++) {     /* as fresh, except the filter's history */
        vm()->wtVoices[v].phaseFrac = 0;
        vm()->wtVoices[v].gainLeft = vm()->wtVoices[v].gainRight = 0;
    }
    h1 = note_hash(0, 60, 40);
    CHECK(h0 == h1, "a note started under CC74 20: the same after a loud resonant note (0x%08x, 0x%08x)", h0, h1);
}

/* the largest sample-to-sample step */
static float maxstep(const float *x, int n)
{
    float m = 0;
    for (int i = 1; i < n; i++) {
        float d = fabsf(x[i] - x[i - 1]);
        m = d > m ? d : m;
    }
    return m;
}
static void test_extremes(void)
{
    static const uint8_t set[4][2] = {{0, 0}, {0, 127}, {127, 127}, {127, 0}};
    double worst = 0, worst_step = 0;
    int worst_p = 0, worst_s = 0, bad_tail = 0, step_p = 0, step_s = 0;
    double st0;
    for (int p = 0; p <= 128; p++) {               /* 128: the drum kit */
        uint32_t ch = p == 128 ? 9 : 0;
        double l0, l;
        fresh(ch, p == 128 ? 0 : (uint32_t)p);
        cc(ch, 7, 127);
        for (int n = 0; n < 4; n++)
            gm_midi(0x90u | ch, (uint32_t)(p == 128 ? 36 + n * 3 : 30 + n * 19), 127);
        render(70);
        l0 = rms(rec, nrec);
        st0 = maxstep(rec, nrec);
        gm_panic();
        render(4);
        for (int s = 0; s < 4; s++) {
            fresh(ch, p == 128 ? 0 : (uint32_t)p);
            cc(ch, 7, 127);
            cc(ch, 74, set[s][0]);
            cc(ch, 71, set[s][1]);
            for (int n = 0; n < 4; n++)
                gm_midi(0x90u | ch, (uint32_t)(p == 128 ? 36 + n * 3 : 30 + n * 19), 127);
            render(70);
            l = rms(rec, nrec);
            if (st0 > 0 && maxstep(rec, nrec) / st0 > worst_step) {
                worst_step = maxstep(rec, nrec) / st0;
                step_p = p;
                step_s = s;
            }
            if (l0 > 50 && l / l0 > worst) {
                worst = l / l0;
                worst_p = p;
                worst_s = s;
            }
            CHECK(l <= l0 * 2.5 + 100, "program %d at CC74 %d CC71 %d: level %.0f, as it is %.0f", p, set[s][0], set[s][1], l, l0);
            gm_panic();
            render(300);
            nrec = 0;
            render(4);
            if (rms(rec, nrec) > 1.0)
                bad_tail++;
        }
    }
    printf("  extremes: 129 sounds x 4, the most a level rose: %.2fx (program %d at CC74 %d CC71 %d)\n", worst, worst_p,
           set[worst_s][0], set[worst_s][1]);
    /* a 16-bit filter wrapping round jumps across the whole swing: 17x a sound's own largest step (contrabass at
     * CC74 0, before the saturation); opening a dark sound right up legitimately ~5x */
    printf("  extremes: the largest sample step over the sound's own: %.1fx (program %d at CC74 %d CC71 %d)\n", worst_step,
           step_p, set[step_s][0], set[step_s][1]);
    CHECK(worst_step < 8.0, "no steps from the filter wrapping round: %.1fx the sound's own", worst_step);
    CHECK(bad_tail == 0, "silent after: %d sounds still sounding", bad_tail);
}

int main(void)
{
    if (gm_init() != 0) {
        printf("FAIL: gm_init\n");
        return 1;
    }
    arr_init();
    test_darker();
    test_resonance();
    test_neutral();
    test_reset();
    test_zipper();
    test_start();
    test_extremes();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
