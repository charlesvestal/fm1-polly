/* SPDX-License-Identifier: GPL-3.0-only */
/* The GM module (gm.h): Sonivox EAS behind a MIDI queue, its 22.05 kHz doubled to 44.1. One unit, -O2. */
#include "gm.h"
#include "arr.h"
#include "eas/eas.h"
#include "eas/eas_data.h"
#include "eas/eas_synth.h"
#include "eas/eas_reverb.h"
#include "eas/eas_chorus.h"

#define BARRIER() __asm__ volatile("" ::: "memory")
#define EAS_FRAMES BUFFER_SIZE_IN_MONO_SAMPLES        /* 128 at 22.05 kHz: half of the FM-1's 256 */
_Static_assert(EAS_FRAMES == GM_BLOCK, "gm.h's block is EAS's");

volatile uint8_t gm_voices;
volatile uint8_t gm_program[GM_NCH];
volatile float gm_level;

static EAS_DATA_HANDLE eas;
static EAS_HANDLE stream;
static int ready;

/* the queue: 3-byte messages, the main loop writes, the render reads */
#define QN 256u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
void gm_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;
    q[w % QN] = (st & 0xFFu) | (d1 & 0x7Fu) << 8 | (d2 & 0x7Fu) << 16;
    BARRIER();
    q_w = w + 1;
}
void gm_panic(void)
{
    for (uint32_t ch = 0; ch < GM_NCH; ch++) {
        gm_midi(0xB0u | ch, 123, 0);              /* all notes off */
        gm_midi(0xB0u | ch, 120, 0);              /* all sound off */
    }
}

/* the effects asked for, packed in one word (never torn), a count above them so a repeat is seen too */
static volatile uint32_t fx_req, fx_done;
void gm_fx(uint32_t rt, uint32_t rl, uint32_t ct, uint32_t cl)
{
    uint32_t seq = (fx_req >> 24) + 1u;
    fx_req = (rt > 4u ? 4u : rt) | (rl & 127u) << 3 | (ct & 3u) << 10 | (cl & 127u) << 12 | seq << 24;
}
/* a preset sets the module's own wet and dry: the dry goes back to none (the voices mix it: EAS's sends,
 * _CC_REVERB / _CC_CHORUS). The levels glide to what is asked over FX_RAMP blocks (no click). An effect off
 * (reverb type 0, or a level 0) is off in the synth too: the voices stop feeding it at once, its tail rings
 * while its level glides down, then it stops (idle, it costs nothing, and starts clean: eas_voicemgt.c) */
#define FX_RAMP 4
static int32_t rev_wet = -1, rev_to, rev_step, cho_lvl = -1, cho_to, cho_step;
static void fx_set(uint32_t v)
{
    uint32_t rt = v & 7u, rl = v >> 3 & 127u, ct = v >> 10 & 3u, cl = v >> 12 & 127u;
    rev_to = rt && rl ? (int32_t)(rl * 258u) : 0;
    cho_to = cl ? (int32_t)(cl * 258u) : 0;
    if (rev_wet < 0)                              /* the first: at once */
        rev_wet = rev_to;
    if (cho_lvl < 0)
        cho_lvl = cho_to;
    rev_step = (rev_to > rev_wet ? rev_to - rev_wet : rev_wet - rev_to) / FX_RAMP + 1;
    cho_step = (cho_to > cho_lvl ? cho_to - cho_lvl : cho_lvl - cho_to) / FX_RAMP + 1;
    if (rt)
        EAS_SetParameter(eas, EAS_MODULE_REVERB, EAS_PARAM_REVERB_PRESET, (EAS_I32)(rt - 1u));
    EAS_SetParameter(eas, EAS_MODULE_REVERB, EAS_PARAM_REVERB_DRY, 0);
    EAS_SetParameter(eas, EAS_MODULE_REVERB, EAS_PARAM_REVERB_WET, rev_wet);
    EAS_SetParameter(eas, EAS_MODULE_CHORUS, EAS_PARAM_CHORUS_PRESET, (EAS_I32)ct);
    EAS_SetParameter(eas, EAS_MODULE_CHORUS, EAS_PARAM_CHORUS_DRY, 0);
    EAS_SetParameter(eas, EAS_MODULE_CHORUS, EAS_PARAM_CHORUS_LEVEL, cho_lvl);
    if (!rev_to && !rev_wet)
        eas->pVoiceMgr->reverbTail = 0;           /* off already: nothing to ring out */
    if (!cho_to && !cho_lvl)
        eas->pVoiceMgr->chorusTail = 0;
    for (int i = 0; i < MAX_VIRTUAL_SYNTHESIZERS; i++) {
        S_SYNTH *s = eas->pVoiceMgr->pSynth[i];
        if (!s)
            continue;
        s->reverbEnabled = (EAS_BOOL)(rev_to != 0);
        s->chorusEnabled = (EAS_BOOL)(cho_to != 0);
    }
}
static int32_t glide(int32_t v, int32_t to, int32_t step)
{
    return v < to ? (v + step > to ? to : v + step) : (v - step < to ? to : v - step);
}
static void fx_ramp(void)                         /* each EAS block */
{
    if (rev_wet >= 0 && rev_wet != rev_to) {
        rev_wet = glide(rev_wet, rev_to, rev_step);
        EAS_SetParameter(eas, EAS_MODULE_REVERB, EAS_PARAM_REVERB_WET, rev_wet);
        if (!rev_wet)
            eas->pVoiceMgr->reverbTail = 0;       /* off and silent: idle now */
    }
    if (cho_lvl >= 0 && cho_lvl != cho_to) {
        cho_lvl = glide(cho_lvl, cho_to, cho_step);
        EAS_SetParameter(eas, EAS_MODULE_CHORUS, EAS_PARAM_CHORUS_LEVEL, cho_lvl);
        if (!cho_lvl)
            eas->pVoiceMgr->chorusTail = 0;
    }
}
#ifdef GM_HOST
int gm_host_fx_settled(void) { return rev_wet == rev_to && cho_lvl == cho_to; }   /* the tests */
#endif

#ifdef GM_HOST
int16_t gm_host_heard[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   /* the tests: each
                                                  * channel's last note-on into the synth (-1 none) */
uint32_t gm_host_ons[16];                        /* ... and its note-ons counted */
#endif
static void synth_write(uint32_t m, uint32_t at)
{
    EAS_U8 b[3] = {(EAS_U8)m, (EAS_U8)(m >> 8), (EAS_U8)(m >> 16)};
    uint32_t st = b[0] & 0xF0u;
    if (st == 0xC0u)
        gm_program[b[0] & 15u] = b[1];
#ifdef GM_HOST
    if (st == 0x90u && b[2]) {
        gm_host_heard[b[0] & 15u] = (int16_t)b[1];
        gm_host_ons[b[0] & 15u]++;
    }
#endif
    /* a note-on's start within the next block: the voices it starts read it (WT_StartVoice); EAS parses the
     * message at once, so it is cleared again straight after */
    eas->pVoiceMgr->startDelay = (EAS_U8)(st == 0x90u && b[2] ? (at < GM_BLOCK ? at : GM_BLOCK - 1) : 0);
    EAS_WriteMIDIStream(eas, stream, b, (st == 0xC0u || st == 0xD0u) ? 2 : 3);
    eas->pVoiceMgr->startDelay = 0;
}
static void drain(void)
{
    while (q_r != q_w) {
        uint32_t m = q[q_r % QN];
        BARRIER();
        q_r++;
        synth_write(m, 0);
    }
}
/* the arranger's notes: it runs inside the render, so it writes the synth itself rather than through the
 * queue, which keeps one producer (the main loop) */
void gm_midi_now(uint32_t st, uint32_t d1, uint32_t d2, uint32_t at)
{
    synth_write((st & 0xFFu) | (d1 & 0x7Fu) << 8 | (d2 & 0x7Fu) << 16, at);
}
#ifdef GM_HOST
void *gm_host_eas(void) { return eas; }          /* the tests (timing_test.c): the synth's state */
#endif

/* Gain staging. EAS mixes its voices in 32 bits, then hard-clips to 16 at its master volume, before our
 * gain: at its default 0 dB, six loud notes reached the rail (ten: hundreds of samples a chord). So EAS runs
 * 6 dB down (ten notes at velocity 127 peak near -3 dBFS there), and the 24-bit stage here makes the 6 dB up,
 * through a soft knee above -2.5 dBFS instead of a hard edge. (As in Ctl.) */
#define GM_EAS_VOLUME 94                          /* EAS volume: 100 = 0 dB, 1 dB a step */
#define FS24 8388607                              /* 24-bit full scale */
#define KNEE 6291456                              /* 0.75 of it */
static inline int32_t soft24(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    float d, y;
    if (a <= KNEE)
        return x;
    d = (float)(a - KNEE) * (1.f / (float)(FS24 - KNEE));
    y = (float)KNEE + (float)(FS24 - KNEE) * d / (1.f + d);   /* slope 1 at the knee, never past full scale */
    return x < 0 ? -(int32_t)y : (int32_t)y;
}
#ifdef GM_HOST
/* the timing test's reference render: the engine as it was (EAS at 0 dB, no headroom stage) */
static int ref_engine;
void gm_ref_engine(int on)
{
    ref_engine = on;
    EAS_SetVolume(eas, NULL, on ? 100 : GM_EAS_VOLUME);
}
#endif

int gm_init(void)
{
    if (EAS_Init(&eas) != EAS_SUCCESS || EAS_OpenMIDIStream(eas, &stream, NULL) != EAS_SUCCESS)
        return -1;
    EAS_SetVolume(eas, NULL, GM_EAS_VOLUME);
    ready = 1;
    return 0;
}

/* 2x up: each EAS sample and the halfway point to the next (linear), one sample late */
static EAS_PCM pcm[EAS_FRAMES * 2];
static int32_t prev_l, prev_r;
void gm_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const int32_t g = (int32_t)gain_q12;          /* Q12 */
    float peak = gm_level * 0.97f;
    if (!ready) {
        for (uint32_t i = 0; i < 2 * n; i++)
            out[i] = 0;
        return;
    }
    {
        uint32_t f = fx_req;
        if (f != fx_done) {
            fx_done = f;
            fx_set(f);
        }
    }
    arr_late_offs();                              /* the arranger's held note-offs before the live notes */
    drain();
    while (n >= 2) {
        uint32_t k = n / 2 > EAS_FRAMES ? EAS_FRAMES : n / 2, i;
        EAS_I32 got = 0;
        /* the arranger's ticks due within this block, their notes into the synth now: EAS takes MIDI only
         * between whole blocks (EAS_Render renders exactly EAS_FRAMES, its envelopes step once a block), and
         * starts each note-on on its sample within the block (gm_midi_now's at) */
        arr_block(2 * k);
        fx_ramp();
        EAS_Render(eas, pcm, (EAS_I32)k, &got);
        for (i = 0; i < k; i++) {
            int32_t l = (i < (uint32_t)got) ? pcm[2 * i] : 0, r = (i < (uint32_t)got) ? pcm[2 * i + 1] : 0;
            int32_t ml, mr, a;
#ifdef GM_HOST
            if (ref_engine) {
                ml = (l + prev_l) >> 1, mr = (r + prev_r) >> 1;
                out[0] = (ml * g) >> 4;
                out[1] = (mr * g) >> 4;
                out[2] = (l * g) >> 4;
                out[3] = (r * g) >> 4;
                out += 4;
                prev_l = l;
                prev_r = r;
                continue;
            }
#endif
            ml = (l + prev_l) >> 1, mr = (r + prev_r) >> 1;
            a = l < 0 ? -l : l;
            if ((float)a * (1.f / 16384.f) > peak)    /* after the 6 dB made up, MASTER full */
                peak = (float)a * (1.f / 16384.f);
            out[0] = soft24((ml * g) >> 3);       /* 16-bit, gain Q12, to 24-bit (<< 8 >> 12), +6 dB */
            out[1] = soft24((mr * g) >> 3);
            out[2] = soft24((l * g) >> 3);
            out[3] = soft24((r * g) >> 3);
            out += 4;
            prev_l = l;
            prev_r = r;
        }
        n -= 2 * k;
    }
    gm_level = peak;
    gm_voices = (uint8_t)(eas->pVoiceMgr ? eas->pVoiceMgr->activeVoices : 0);
}
