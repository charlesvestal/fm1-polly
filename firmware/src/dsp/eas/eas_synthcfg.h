/*----------------------------------------------------------------------------
 *
 * File:
 * eas_synthcfg.h
 *
 * Contents and purpose:
 * Defines for various synth configurations
 *
 * Copyright Sonic Network Inc. 2004, 2005

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *----------------------------------------------------------------------------
 * Revision Control:
 *   $Revision: 664 $
 *   $Date: 2007-04-25 13:11:22 -0700 (Wed, 25 Apr 2007) $
 *----------------------------------------------------------------------------
*/

#ifndef _EAS_SYNTHCFG_H
#define _EAS_SYNTHCFG_H

#include "eas_options.h"

// TODO: we may need a better way to handle these fx
#ifdef _CHORUS_ENABLED
#define _CC_CHORUS
#endif

#ifdef _REVERB_ENABLED
#define _CC_REVERB
#endif

/* wavetable drums and FM melodic on MCU */
#if (defined(EAS_WT_SYNTH) && defined(EAS_FM_SYNTH)) || defined(EAS_HYBRID_SYNTH)
#define _WT_SYNTH
#define _FM_SYNTH
#define _SECONDARY_SYNTH
#define _HYBRID_SYNTH

#elif defined(EAS_WT_SYNTH)
#define _WT_SYNTH

/* FM on MCU */
#elif defined(EAS_FM_SYNTH)
#define _FM_SYNTH

/* wavetable drums on MCU, wavetable melodic on DSP */
#elif defined(EAS_SPLIT_WT_SYNTH)
#define _WT_SYNTH
#define _SPLIT_ARCHITECTURE

/* wavetable drums on MCU, FM melodic on DSP */
#elif defined(EAS_SPLIT_HYBRID_SYNTH)
#define _WT_SYNTH
#define _FM_SYNTH
#define _SECONDARY_SYNTH
#define _SPLIT_ARCHITECTURE
#define _HYBRID_SYNTH

/* FM synth on DSP */
#elif defined(EAS_SPLIT_FM_SYNTH)
#define _FM_SYNTH
#define _SPLIT_ARCHITECTURE

#else
#error "Unrecognized architecture option"
#endif

#if !defined(_HYBRID_SYNTH)
#define NUM_PRIMARY_VOICES      MAX_SYNTH_VOICES
#if defined(_WT_SYNTH)
#define NUM_WT_VOICES           MAX_SYNTH_VOICES
#endif
#if defined(_FM_SYNTH)
#define NUM_FM_VOICES           MAX_SYNTH_VOICES
#endif
#else // _HYBRID_SYNTH
#if !defined(NUM_PRIMARY_VOICES)
#define NUM_PRIMARY_VOICES      (MAX_SYNTH_VOICES / 2)
#endif

#if !defined(NUM_SECONDARY_VOICES)
#define NUM_SECONDARY_VOICES    (MAX_SYNTH_VOICES - NUM_PRIMARY_VOICES)
#endif

#define NUM_WT_VOICES           NUM_PRIMARY_VOICES
#define NUM_FM_VOICES           NUM_SECONDARY_VOICES
#endif

#endif
