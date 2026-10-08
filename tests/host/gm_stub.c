/* SPDX-License-Identifier: GPL-3.0-only */
/* A test double for gm.c: no synth; gm_render runs the arranger as gm.c does (arr_block before each EAS
 * block of 256 output frames) and every message that reaches the synth is recorded with the frame it sounds
 * at: a note-on at its sample within the block (2 output frames each), the rest at the block's start. */
#include <string.h>
#include "gm_stub.h"
#include "arr.h"

volatile uint8_t gm_voices;
volatile uint8_t gm_program[GM_NCH];
volatile float gm_level;

stub_msg_t stub_log[STUB_MAX];
int stub_n;
uint32_t stub_frame;

static void rec(uint32_t st, uint32_t d1, uint32_t d2, uint32_t at)
{
    if (stub_n < STUB_MAX) {
        stub_msg_t m = {stub_frame, (uint8_t)st, (uint8_t)d1, (uint8_t)d2};
        if ((st & 0xF0u) == 0x90u && d2)          /* as gm.c: a note-on on its sample, the rest at the block */
            m.frame += 2u * (at < GM_BLOCK ? at : GM_BLOCK - 1);
        stub_log[stub_n++] = m;
    }
}
int gm_init(void) { return 0; }
void gm_midi(uint32_t st, uint32_t d1, uint32_t d2) { rec(st, d1, d2, 0); }
void gm_midi_now(uint32_t st, uint32_t d1, uint32_t d2, uint32_t at) { rec(st, d1, d2, at); }
void gm_panic(void) {}
void gm_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    (void)gain_q12;
    memset(out, 0, 2 * n * sizeof *out);
    arr_late_offs();                          /* as gm.c: before the queued MIDI */
    while (n >= 2) {
        uint32_t k = n > 256u ? 256u : n;     /* gm.c: 128 EAS frames = 256 output frames */
        arr_block(k);
        stub_frame += k;
        n -= k;
    }
}
