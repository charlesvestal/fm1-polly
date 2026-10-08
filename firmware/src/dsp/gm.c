/* SPDX-License-Identifier: GPL-3.0-only */
/* The GM module (gm.h): Sonivox EAS behind a MIDI queue, its 22.05 kHz doubled to 44.1. One unit, -O2. */
#include "gm.h"
#include "eas/eas.h"
#include "eas/eas_data.h"
#include "eas/eas_synth.h"

#define BARRIER() __asm__ volatile("" ::: "memory")
#define EAS_FRAMES BUFFER_SIZE_IN_MONO_SAMPLES        /* 128 at 22.05 kHz: half of the FM-1's 256 */

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

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t m = q[q_r % QN];
        EAS_U8 b[3] = {(EAS_U8)m, (EAS_U8)(m >> 8), (EAS_U8)(m >> 16)};
        uint32_t st = b[0] & 0xF0u;
        BARRIER();
        q_r++;
        if (st == 0xC0u)
            gm_program[b[0] & 15u] = b[1];
        EAS_WriteMIDIStream(eas, stream, b, (st == 0xC0u || st == 0xD0u) ? 2 : 3);
    }
}

int gm_init(void)
{
    if (EAS_Init(&eas) != EAS_SUCCESS || EAS_OpenMIDIStream(eas, &stream, NULL) != EAS_SUCCESS)
        return -1;
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
    drain();
    while (n >= 2) {
        uint32_t k = n / 2 > EAS_FRAMES ? EAS_FRAMES : n / 2, i;
        EAS_I32 got = 0;
        EAS_Render(eas, pcm, (EAS_I32)k, &got);
        for (i = 0; i < k; i++) {
            int32_t l = (i < (uint32_t)got) ? pcm[2 * i] : 0, r = (i < (uint32_t)got) ? pcm[2 * i + 1] : 0;
            int32_t ml = (l + prev_l) >> 1, mr = (r + prev_r) >> 1;
            int32_t a = l < 0 ? -l : l;
            if ((float)a * (1.f / 32768.f) > peak)
                peak = (float)a * (1.f / 32768.f);
            out[0] = (ml * g) >> 4;               /* 16-bit, gain Q12, to 24-bit: << 8 >> 12 */
            out[1] = (mr * g) >> 4;
            out[2] = (l * g) >> 4;
            out[3] = (r * g) >> 4;
            out += 4;
            prev_l = l;
            prev_r = r;
        }
        n -= 2 * k;
    }
    gm_level = peak;
    gm_voices = (uint8_t)(eas->pVoiceMgr ? eas->pVoiceMgr->activeVoices : 0);
}
