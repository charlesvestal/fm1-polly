/* SPDX-License-Identifier: Apache-2.0 */
/* Sonivox EAS build options for the FM-1 (in place of the CMake-generated eas_options.h): the wavetable
 * synth with its 200 KB GM sound set (8-bit samples, 22050 Hz), reverb, chorus and the filter; live MIDI
 * only (the SMF parser stays: the library expects it). */
#ifndef EAS_OPTIONS_H
#define EAS_OPTIONS_H
#define EAS_WT_SYNTH
#define NUM_OUTPUT_CHANNELS 2
#ifndef MAX_SYNTH_VOICES
#define MAX_SYNTH_VOICES 32
#endif
#define _FILTER_ENABLED
#define _REVERB_ENABLED
#define _CHORUS_ENABLED
#define _8_BIT_SAMPLES
#define _SAMPLE_RATE_22050
#endif
