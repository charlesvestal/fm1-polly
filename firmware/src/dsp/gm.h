/* SPDX-License-Identifier: GPL-3.0-only */
/* A GM sound module: Sonivox EAS (dsp/eas/, Apache-2.0; Android's original MIDI synth) with its 200 KB
 * General MIDI sound set (128 programs and the drum kit on channel 10, 8-bit samples at 22.05 kHz),
 * rendered at 22.05 kHz and doubled to the FM-1's 44.1.
 *
 * Threads: gm_render() runs in the audio interrupt and is the only caller of the library. MIDI is posted
 * (gm_midi) and handed to it at the start of a render; the state marked (UI) is written by the render. */
#pragma once
#include <stdint.h>

#define GM_NCH 16

int gm_init(void);                         /* 0 = ready */
void gm_midi(uint32_t st, uint32_t d1, uint32_t d2);   /* a channel message (2 or 3 bytes) */
void gm_panic(void);                       /* all notes off, all channels */
void gm_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);   /* the audio ISR: n stereo frames */

/* (UI) */
extern volatile uint8_t gm_voices;         /* voices sounding */
extern volatile uint8_t gm_program[GM_NCH];/* each channel's program, as the MIDI set it */
extern volatile float gm_level;            /* the output's peak, 0..1, decaying */
