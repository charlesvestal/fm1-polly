/* SPDX-License-Identifier: GPL-3.0-only */
/* A GM sound module: Sonivox EAS (dsp/eas/, Apache-2.0; Android's original MIDI synth) with its 200 KB
 * General MIDI sound set (128 programs and the drum kit on channel 10, 8-bit samples at 22.05 kHz),
 * rendered at 22.05 kHz and doubled to the FM-1's 44.1.
 *
 * Threads: gm_render() runs in the audio interrupt and is the only caller of the library. MIDI is posted
 * (gm_midi) and handed to it at the start of a render; the arranger (arr.c), which runs inside the render,
 * writes with gm_midi_now between EAS blocks. The state marked (UI) is written by the render. */
#pragma once
#include <stdint.h>

#define GM_NCH 16

int gm_init(void);                         /* 0 = ready */
void gm_midi(uint32_t st, uint32_t d1, uint32_t d2);   /* a channel message (2 or 3 bytes) */
/* the same, straight into the synth: only from inside gm_render (the arranger, between EAS blocks). The
 * queue above keeps its single producer (the main loop); the render writes the synth itself.
 * at: when it is due within the coming EAS block, in its samples (22.05 kHz, 0..GM_BLOCK-1). A note-on starts
 * on that sample (the voice silent before it, its first samples as a note started on a block renders them);
 * everything else, note-offs included, takes effect at the block's start.
 * A note-on that steals a voice (all 32 busy, or a third of the same note on a channel) starts on its sample
 * too when the voice is already silent (say, itself started in this block); when the voice is still sounding
 * it fades out over this block and the note starts at the next block's start, as Sonivox does. */
#define GM_BLOCK 128                       /* samples an EAS block (22.05 kHz): 256 output frames */
void gm_midi_now(uint32_t st, uint32_t d1, uint32_t d2, uint32_t at);
void gm_panic(void);                       /* all notes off, all channels */
/* the master effects the channels' sends (CC 91, CC 93) feed: reverb 0 off, 1..4 (large hall, hall, chamber,
 * room) and its level, chorus preset 0..3 and its level (0..127; 0 off); taken at the next render. Off costs
 * nothing: the voices stop feeding it */
void gm_fx(uint32_t rev_type, uint32_t rev_level, uint32_t cho_type, uint32_t cho_level);
void gm_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);   /* the audio ISR: n stereo frames */

/* (UI) */
extern volatile uint8_t gm_voices;         /* voices sounding */
extern volatile uint8_t gm_program[GM_NCH];/* each channel's program, as the MIDI set it */
extern volatile float gm_level;            /* the output's peak, 0..1, decaying */
#ifdef GM_HOST
void gm_ref_engine(int on);                /* host tests: render as the unmodified engine (0 dB, no output stage) */
#endif
