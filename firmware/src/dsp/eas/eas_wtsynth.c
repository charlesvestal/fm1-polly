/*----------------------------------------------------------------------------
 *
 * File:
 * eas_wtsynth.c
 *
 * Contents and purpose:
 * Implements the synthesizer functions.
 *
 * Copyright Sonic Network Inc. 2004

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
 *   $Revision: 795 $
 *   $Date: 2007-08-01 00:14:45 -0700 (Wed, 01 Aug 2007) $
 *----------------------------------------------------------------------------
*/

#define LOG_TAG "SYNTH"
#include "log/log.h"
#include <cutils/log.h>

#include "eas_options.h"
#include "eas_data.h"
#include "eas_report.h"
#include "eas_host.h"
#include "eas_math.h"
#include "eas_synth_protos.h"
#include "eas_wtsynth.h"
#include "eas_pan.h"

#include <string.h>

#ifdef DLS_SYNTHESIZER
#include "eas_dlssynth.h"
#endif

#ifdef _METRICS_ENABLED
#include "eas_perf.h"
#endif

/* local prototypes */
static EAS_RESULT WT_Initialize(S_VOICE_MGR *pVoiceMgr);
static void WT_ReleaseVoice (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, EAS_I32 voiceNum);
static void WT_MuteVoice (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, EAS_I32 voiceNum);
static void WT_SustainPedal (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, S_SYNTH_CHANNEL *pChannel, EAS_I32 voiceNum);
static EAS_RESULT WT_StartVoice (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, EAS_I32 voiceNum, EAS_U16 regionIndex);
static EAS_BOOL WT_UpdateVoice (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, EAS_I32 voiceNum, EAS_I32 *pMixBuffer, EAS_I32 numSamples);
static void WT_UpdateChannel (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, EAS_U8 channel);
static EAS_I32 WT_UpdatePhaseInc (S_WT_VOICE *pWTVoice, const S_ARTICULATION *pArt, S_SYNTH_CHANNEL *pChannel, EAS_I32 pitchCents);
static EAS_I32 WT_UpdateGain (S_SYNTH_VOICE *pVoice, S_WT_VOICE *pWTVoice, const S_ARTICULATION *pArt, S_SYNTH_CHANNEL *pChannel, EAS_I32 gain);
static void WT_UpdateEG1 (S_WT_VOICE *pWTVoice, const S_ENVELOPE *pEnv);
static void WT_UpdateEG2 (S_WT_VOICE *pWTVoice, const S_ENVELOPE *pEnv);

#ifdef EAS_SPLIT_WT_SYNTH
extern EAS_BOOL WTE_StartFrame (EAS_FRAME_BUFFER_HANDLE pFrameBuffer);
extern EAS_BOOL WTE_EndFrame (EAS_FRAME_BUFFER_HANDLE pFrameBuffer, EAS_I32 *pMixBuffer, EAS_I16 masterGain);
#endif

#ifdef _FILTER_ENABLED
static void WT_UpdateFilter (S_WT_VOICE *pWTVoice, S_WT_INT_FRAME *pIntFrame, const S_ARTICULATION *pArt);
#endif

#ifdef _STATS
extern double statsPhaseIncrement;
extern double statsMaxPhaseIncrement;
extern long statsPhaseSampleCount;
extern double statsSampleSize;
extern long statsSampleCount;
#endif

/*----------------------------------------------------------------------------
 * Synthesizer interface
 *----------------------------------------------------------------------------
*/

const S_SYNTH_INTERFACE wtSynth =
{
    WT_Initialize,
    WT_StartVoice,
    WT_UpdateVoice,
    WT_ReleaseVoice,
    WT_MuteVoice,
    WT_SustainPedal,
    WT_UpdateChannel
};

#ifdef EAS_SPLIT_WT_SYNTH
const S_FRAME_INTERFACE wtFrameInterface =
{
    WTE_StartFrame,
    WTE_EndFrame
};
#endif

/*----------------------------------------------------------------------------
 * WT_Initialize()
 *----------------------------------------------------------------------------
 * Purpose:
 *
 * Inputs:
 * pVoice - pointer to voice to initialize
 *
 * Outputs:
 *
 *----------------------------------------------------------------------------
*/
static EAS_RESULT WT_Initialize (S_VOICE_MGR *pVoiceMgr)
{
    EAS_INT i;

    for (i = 0; i < NUM_WT_VOICES; i++)
    {

        pVoiceMgr->wtVoices[i].artIndex = DEFAULT_ARTICULATION_INDEX;

        pVoiceMgr->wtVoices[i].eg1State = DEFAULT_EG1_STATE;
        pVoiceMgr->wtVoices[i].eg1Value = DEFAULT_EG1_VALUE;
        pVoiceMgr->wtVoices[i].eg1Increment = DEFAULT_EG1_INCREMENT;

        pVoiceMgr->wtVoices[i].eg2State = DEFAULT_EG2_STATE;
        pVoiceMgr->wtVoices[i].eg2Value = DEFAULT_EG2_VALUE;
        pVoiceMgr->wtVoices[i].eg2Increment = DEFAULT_EG2_INCREMENT;

        /* left and right gain values are needed only if stereo output */
#if (NUM_OUTPUT_CHANNELS == 2)
        pVoiceMgr->wtVoices[i].gainLeft = DEFAULT_VOICE_GAIN;
        pVoiceMgr->wtVoices[i].gainRight = DEFAULT_VOICE_GAIN;
#endif

        pVoiceMgr->wtVoices[i].phaseFrac = DEFAULT_PHASE_FRAC;
        pVoiceMgr->wtVoices[i].phaseAccum = DEFAULT_PHASE_INT;

#ifdef _FILTER_ENABLED
        memset(&pVoiceMgr->wtVoices[i].filter, 0, sizeof(S_FILTER_CONTROL));
#endif
    }

    return EAS_TRUE;
}

/*----------------------------------------------------------------------------
 * WT_ReleaseVoice()
 *----------------------------------------------------------------------------
 * Purpose:
 * The selected voice is being released.
 *
 * Inputs:
 * pEASData - pointer to S_EAS_DATA
 * pVoice - pointer to voice to release
 *
 * Outputs:
 * None
 *----------------------------------------------------------------------------
*/
/*lint -esym(715, pVoice) used in some implementations */
static void WT_ReleaseVoice (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, EAS_I32 voiceNum)
{
    S_WT_VOICE *pWTVoice;
    const S_ARTICULATION *pArticulation;

#ifdef DLS_SYNTHESIZER
    if (pVoice->regionIndex & FLAG_RGN_IDX_DLS_SYNTH)
    {
        DLS_ReleaseVoice(pVoiceMgr, pSynth, pVoice, voiceNum);
        return;
    }
#endif

    pWTVoice = &pVoiceMgr->wtVoices[voiceNum];
    pArticulation = &pSynth->pEAS->pArticulations[pWTVoice->artIndex];

    /* release EG1 */
    pWTVoice->eg1State = eEnvelopeStateRelease;
    pWTVoice->eg1Increment = pArticulation->eg1.releaseTime;

    /*
    The spec says we should release EG2, but doing so with the current
    voicing is causing clicks. This fix will need to be coordinated with
    a new sound library release
    */

    /* release EG2 */
    pWTVoice->eg2State = eEnvelopeStateRelease;
    pWTVoice->eg2Increment = pArticulation->eg2.releaseTime;
}

/*----------------------------------------------------------------------------
 * WT_MuteVoice()
 *----------------------------------------------------------------------------
 * Purpose:
 * The selected voice is being muted.
 *
 * Inputs:
 * pVoice - pointer to voice to release
 *
 * Outputs:
 * None
 *----------------------------------------------------------------------------
*/
/*lint -esym(715, pSynth) used in some implementations */
static void WT_MuteVoice (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, EAS_I32 voiceNum)
{

#ifdef DLS_SYNTHESIZER
    if (pVoice->regionIndex & FLAG_RGN_IDX_DLS_SYNTH)
    {
        DLS_MuteVoice(pVoiceMgr, pSynth, pVoice, voiceNum);
        return;
    }
#endif

    /* clear deferred action flags */
    pVoice->voiceFlags &=
        ~(VOICE_FLAG_DEFER_MIDI_NOTE_OFF |
        VOICE_FLAG_SUSTAIN_PEDAL_DEFER_NOTE_OFF |
        VOICE_FLAG_DEFER_MUTE);

    /* set the envelope state */
    pVoiceMgr->wtVoices[voiceNum].eg1State = eEnvelopeStateMuted;
    pVoiceMgr->wtVoices[voiceNum].eg2State = eEnvelopeStateMuted;
}

/*----------------------------------------------------------------------------
 * WT_SustainPedal()
 *----------------------------------------------------------------------------
 * Purpose:
 * The selected voice is held due to sustain pedal
 *
 * Inputs:
 * pVoice - pointer to voice to sustain
 *
 * Outputs:
 * None
 *----------------------------------------------------------------------------
*/
/*lint -esym(715, pChannel) used in some implementations */
static void WT_SustainPedal (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, S_SYNTH_CHANNEL *pChannel, EAS_I32 voiceNum)
{
    S_WT_VOICE *pWTVoice;

#ifdef DLS_SYNTHESIZER
    if (pVoice->regionIndex & FLAG_RGN_IDX_DLS_SYNTH)
    {
        DLS_SustainPedal(pVoiceMgr, pSynth, pVoice, pChannel, voiceNum);
        return;
    }
#endif

    /* don't catch the voice if below the sustain level */
    pWTVoice = &pVoiceMgr->wtVoices[voiceNum];
    if (pWTVoice->eg1Value < pSynth->pEAS->pArticulations[pWTVoice->artIndex].eg1.sustainLevel)
        return;

    /* sustain flag is set, damper pedal is on */
    /* defer releasing this note until the damper pedal is off */
    pWTVoice->eg1State = eEnvelopeStateDecay;
    pVoice->voiceState = eVoiceStatePlay;

    /*
    because sustain pedal is on, this voice
    should defer releasing its note
    */
    pVoice->voiceFlags |= VOICE_FLAG_SUSTAIN_PEDAL_DEFER_NOTE_OFF;

#ifdef _DEBUG_SYNTH
    { /* dpp: EAS_ReportEx(_EAS_SEVERITY_INFO, "WT_SustainPedal: defer note off because sustain pedal is on\n"); */ }
#endif
}

/*----------------------------------------------------------------------------
 * WT_StartVoice()
 *----------------------------------------------------------------------------
 * Purpose:
 * Assign the region for the given instrument using the midi key number
 * and the RPN2 (coarse tuning) value. By using RPN2 as part of the
 * region selection process, we reduce the amount a given sample has
 * to be transposed by selecting the closest recorded root instead.
 *
 * This routine is the second half of SynthAssignRegion().
 * If the region was successfully found by SynthFindRegionIndex(),
 * then assign the region's parameters to the voice.
 *
 * Setup and initialize the following voice parameters:
 * m_nRegionIndex
 *
 * Inputs:
 * pVoice - ptr to the voice we have assigned for this channel
 * nRegionIndex - index of the region
 * pEASData - pointer to overall EAS data structure
 *
 * Outputs:
 * success - could find and assign the region for this voice's note otherwise
 * failure - could not find nor assign the region for this voice's note
 *
 * Side Effects:
 * psSynthObject->m_sVoice[].m_nRegionIndex is assigned
 * psSynthObject->m_sVoice[] parameters are assigned
 *----------------------------------------------------------------------------
*/
static EAS_RESULT WT_StartVoice (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, EAS_I32 voiceNum, EAS_U16 regionIndex)
{
    S_WT_VOICE *pWTVoice;
    const S_WT_REGION *pRegion;
    const S_ARTICULATION *pArt;
    S_SYNTH_CHANNEL *pChannel;

#if (NUM_OUTPUT_CHANNELS == 2)
    EAS_INT pan;
#endif

#ifdef EAS_SPLIT_WT_SYNTH
    S_WT_CONFIG wtConfig;
#endif

    /* no samples have been synthesized for this note yet */
    pVoice->regionIndex = regionIndex;
    pVoice->voiceFlags = VOICE_FLAG_NO_SAMPLES_SYNTHESIZED_YET;

    /* get the articulation index for this region */
    pWTVoice = &pVoiceMgr->wtVoices[voiceNum];
    pChannel = &pSynth->channels[pVoice->channel & 15];

    /* update static channel parameters */
    if (pChannel->channelFlags & CHANNEL_FLAG_UPDATE_CHANNEL_PARAMETERS)
        WT_UpdateChannel(pVoiceMgr, pSynth, pVoice->channel & 15);

#ifdef DLS_SYNTHESIZER
    if (pVoice->regionIndex & FLAG_RGN_IDX_DLS_SYNTH)
        return DLS_StartVoice(pVoiceMgr, pSynth, pVoice, voiceNum, regionIndex);
#endif

    pRegion = &(pSynth->pEAS->pWTRegions[regionIndex]);
    pWTVoice->artIndex = pRegion->artIndex;

#ifdef _DEBUG_SYNTH
    { /* dpp: EAS_ReportEx(_EAS_SEVERITY_INFO, "WT_StartVoice: Voice %ld; Region %d\n", (EAS_I32) (pVoice - pVoiceMgr->voices), regionIndex); */ }
#endif

    pArt = &pSynth->pEAS->pArticulations[pWTVoice->artIndex];

    /* MIDI note on puts this voice into attack state */
    pWTVoice->eg1State = eEnvelopeStateAttack;
    pWTVoice->eg1Value = 0;
    pWTVoice->eg1Increment = pArt->eg1.attackTime;
    pWTVoice->eg2State = eEnvelopeStateAttack;
    pWTVoice->eg2Value = 0;
    pWTVoice->eg2Increment = pArt->eg2.attackTime;

    /* init the LFO */
    pWTVoice->modLFO.lfoValue = 0;
    pWTVoice->modLFO.lfoPhase = -pArt->lfoDelay;

    pVoice->gain = 0;

#if (NUM_OUTPUT_CHANNELS == 2)
    /*
    Get the Midi CC10 pan value for this voice's channel
    convert the pan value to an "angle" representation suitable for
    our sin, cos calculator. This representation is NOT necessarily the same
    as the transform in the GM manuals because of our sin, cos calculator.
    "angle" = (CC10 - 64)/128
    */
    pan = (EAS_INT) pSynth->channels[pVoice->channel & 15].pan - 64;
    pan += pArt->pan;
    EAS_CalcPanControl(pan, &pWTVoice->gainLeft, &pWTVoice->gainRight);
#endif

#ifdef _FILTER_ENABLED
    /* clear out the filter states */
    memset(&pWTVoice->filter, 0, sizeof(S_FILTER_CONTROL));
#endif

    /* if this wave is to be generated using noise generator */
    if (pRegion->region.keyGroupAndFlags & REGION_FLAG_USE_WAVE_GENERATOR)
    {
        pWTVoice->prngTmp0 = 4574296;
        pWTVoice->loopStart = WT_NOISE_GENERATOR;
        pWTVoice->prngTmp1 = 4574295;
    }

    /* normal sample */
    else
    {

#ifdef EAS_SPLIT_WT_SYNTH
        if (voiceNum < NUM_PRIMARY_VOICES)
            pWTVoice->phaseAccum = (EAS_U32) pSynth->pEAS->pSamples + pSynth->pEAS->pSampleOffsets[pRegion->waveIndex];
        else
            pWTVoice->phaseAccum = pSynth->pEAS->pSampleOffsets[pRegion->waveIndex];
#else
#if defined (_8_BIT_SAMPLES)
        pWTVoice->phaseAccum = pSynth->pEAS->pSamples + pSynth->pEAS->pSampleOffsets[pRegion->waveIndex];
#else //_16_BIT_SAMPLES
        pWTVoice->phaseAccum = pSynth->pEAS->pSamples + (pSynth->pEAS->pSampleOffsets[pRegion->waveIndex] / 2);
#endif
#endif

        if (pRegion->region.keyGroupAndFlags & REGION_FLAG_IS_LOOPED)
        {
            pWTVoice->loopStart = pWTVoice->phaseAccum + pRegion->loopStart;
            pWTVoice->loopEnd = pWTVoice->phaseAccum + pRegion->loopEnd - 1;
        }
        else {
#if defined (_8_BIT_SAMPLES)
            pWTVoice->loopStart = pWTVoice->loopEnd = pWTVoice->phaseAccum + pSynth->pEAS->pSampleLen[pRegion->waveIndex] - 1;
#else //_16_BIT_SAMPLES
            pWTVoice->loopStart = pWTVoice->loopEnd = pWTVoice->phaseAccum + pSynth->pEAS->pSampleLen[pRegion->waveIndex] / 2 - 1;
#endif
        }
    }

#ifdef EAS_SPLIT_WT_SYNTH
    /* configure off-chip voices */
    if (voiceNum >= NUM_PRIMARY_VOICES)
    {
        wtConfig.phaseAccum = pWTVoice->phaseAccum;
        wtConfig.loopStart = pWTVoice->loopStart;
        wtConfig.loopEnd = pWTVoice->loopEnd;
        wtConfig.gain = pVoice->gain;

#if (NUM_OUTPUT_CHANNELS == 2)
        wtConfig.gainLeft = pWTVoice->gainLeft;
        wtConfig.gainRight = pWTVoice->gainRight;
#endif

        WTE_ConfigVoice(voiceNum - NUM_PRIMARY_VOICES, &wtConfig, pVoiceMgr->pFrameBuffer);
    }
#endif

    return EAS_SUCCESS;
}

/*----------------------------------------------------------------------------
 * WT_CheckSampleEnd
 *----------------------------------------------------------------------------
 * Purpose:
 * Check for end of sample and calculate number of samples to synthesize
 *
 * Inputs:
 *
 * Outputs:
 *
 * Notes:
 *
 *----------------------------------------------------------------------------
*/
EAS_BOOL WT_CheckSampleEnd (S_WT_VOICE *pWTVoice, S_WT_INT_FRAME *pWTIntFrame, EAS_BOOL update)
{
    const EAS_SAMPLE* endPhaseAccum;
    EAS_U32 endPhaseFrac;
    EAS_I32 numSamples;
    EAS_BOOL done = EAS_FALSE;

    /* check to see if we hit the end of the waveform this time */
    /*lint -e{703} use shift for performance */
    endPhaseFrac = pWTVoice->phaseFrac + pWTIntFrame->frame.phaseIncrement * BUFFER_SIZE_IN_MONO_SAMPLES;
    endPhaseAccum = pWTVoice->phaseAccum + GET_PHASE_INT_PART(endPhaseFrac);
    if (endPhaseAccum >= pWTVoice->loopEnd)
    {
        /* calculate how far current ptr is from end */
        numSamples = pWTVoice->loopEnd - pWTVoice->phaseAccum;
        /* now account for the fractional portion */
        /*lint -e{703} use shift for performance */
        numSamples = (numSamples << NUM_PHASE_FRAC_BITS) - (EAS_I32) pWTVoice->phaseFrac;
        if (pWTIntFrame->frame.phaseIncrement) {
            //EAS_I32 oldMethod = 1 + (numSamples / pWTIntFrame->frame.phaseIncrement);
            pWTIntFrame->numSamples =
                (numSamples + pWTIntFrame->frame.phaseIncrement - 1) / pWTIntFrame->frame.phaseIncrement;
            // if (oldMethod != pWTIntFrame->numSamples) {
            //     ALOGE("b/317780080 old %ld new %ld", oldMethod, pWTIntFrame->numSamples);
            //     EAS_Report(_EAS_SEVERITY_DETAIL, "%s: old %d new %d\n", __func__, oldMethod, pWTIntFrame->numSamples);
            // }
        } else {
            pWTIntFrame->numSamples = numSamples;
        }
        if (pWTIntFrame->numSamples < 0) {
            EAS_Report(_EAS_SEVERITY_ERROR, "%s: numSamples <= 0\n", __func__);
            ALOGE("b/26366256");
            android_errorWriteLog(0x534e4554, "26366256");
            pWTIntFrame->numSamples = 0;
        } else if (pWTIntFrame->numSamples > BUFFER_SIZE_IN_MONO_SAMPLES) {
            EAS_Report(_EAS_SEVERITY_ERROR, "%s: numSamples %d > %d BUFFER_SIZE_IN_MONO_SAMPLES\n", __func__, pWTIntFrame->numSamples, BUFFER_SIZE_IN_MONO_SAMPLES);
            ALOGE("b/317780080 clip numSamples %ld -> %d",
                  pWTIntFrame->numSamples, BUFFER_SIZE_IN_MONO_SAMPLES);
            android_errorWriteLog(0x534e4554, "317780080");
            pWTIntFrame->numSamples = BUFFER_SIZE_IN_MONO_SAMPLES;
        }

        /* sound will be done this frame */
        done = EAS_TRUE;
    }

    /* update data for off-chip synth */
    if (update)
    {
        pWTVoice->phaseFrac = endPhaseFrac;
        pWTVoice->phaseAccum = endPhaseAccum;
    }

    return done;
}

/*----------------------------------------------------------------------------
 * WT_UpdateVoice()
 *----------------------------------------------------------------------------
 * Purpose:
 * Synthesize a block of samples for the given voice.
 * Use linear interpolation.
 *
 * Inputs:
 * pEASData - pointer to overall EAS data structure
 *
 * Outputs:
 * number of samples actually written to buffer
 *
 * Side Effects:
 * - samples are added to the presently free buffer
 *
 *----------------------------------------------------------------------------
*/
static EAS_BOOL WT_UpdateVoice (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, S_SYNTH_VOICE *pVoice, EAS_I32 voiceNum, EAS_I32 *pMixBuffer, EAS_I32  numSamples)
{
    S_WT_VOICE *pWTVoice;
    S_WT_INT_FRAME intFrame;
    S_SYNTH_CHANNEL *pChannel;
    const S_WT_REGION *pWTRegion;
    const S_ARTICULATION *pArt;
    EAS_I32 temp;
    EAS_BOOL done;

#ifdef DLS_SYNTHESIZER
    if (pVoice->regionIndex & FLAG_RGN_IDX_DLS_SYNTH)
        return DLS_UpdateVoice(pVoiceMgr, pSynth, pVoice, voiceNum, pMixBuffer, numSamples);
#endif
    /* establish pointers to critical data */
    pWTVoice = &pVoiceMgr->wtVoices[voiceNum];
    pWTRegion = &pSynth->pEAS->pWTRegions[pVoice->regionIndex & REGION_INDEX_MASK];
    pArt = &pSynth->pEAS->pArticulations[pWTVoice->artIndex];
    pChannel = &pSynth->channels[pVoice->channel & 15];
    intFrame.prevGain = pVoice->gain;

    /* update the envelopes */
    WT_UpdateEG1(pWTVoice, &pArt->eg1);
    WT_UpdateEG2(pWTVoice, &pArt->eg2);

    /* update the LFO */
    WT_UpdateLFO(&pWTVoice->modLFO, pArt->lfoFreq);

#ifdef _FILTER_ENABLED
/*
    // calculate filter if library uses filter //
    if (pSynth->pEAS->libAttr & LIB_FORMAT_FILTER_ENABLED) {
        WT_UpdateFilter(pWTVoice, &intFrame, pArt);
    } else {
#ifdef _FLOAT_DCF
        intFrame.frame.b02 = 0.0f;
#else
        intFrame.frame.k = 0;
#endif
    }
*/
    // The LIB_FORMAT_FILTER_ENABLED flag is just bogus. It seems to be 8-bit samples flag.
    WT_UpdateFilter(pWTVoice, &intFrame, pArt);
#endif

    /* update the gain */
    intFrame.frame.gainTarget = WT_UpdateGain(pVoice, pWTVoice, pArt, pChannel, pWTRegion->gain);

    /* calculate base pitch*/
    temp = pChannel->staticPitch + pWTRegion->tuning;

    /* include global transpose */
    if (pChannel->channelFlags & CHANNEL_FLAG_RHYTHM_CHANNEL)
        temp += pVoice->note * 100;
    else
        temp += (pVoice->note + pSynth->globalTranspose) * 100;
    intFrame.frame.phaseIncrement = WT_UpdatePhaseInc(pWTVoice, pArt, pChannel, temp);
    if (pWTVoice->loopStart == WT_NOISE_GENERATOR) {
        temp = 0;
    } else {
        temp = pWTVoice->loopEnd - pWTVoice->loopStart;
    }
    if (temp != 0) {
        temp = temp << NUM_PHASE_FRAC_BITS;
        if (intFrame.frame.phaseIncrement > temp) {
            ALOGW("%p phaseIncrement=%d", pWTVoice, (int)intFrame.frame.phaseIncrement);
            EAS_Report(_EAS_SEVERITY_WARNING, "WT_UpdateVoice: phase increment larger than loop region, rounded (%d>%d)", (int)intFrame.frame.phaseIncrement, (int)temp);
            intFrame.frame.phaseIncrement %= temp;
        }
    }

    /* call into engine to generate samples */
    intFrame.pAudioBuffer = pVoiceMgr->voiceBuffer;
    intFrame.pMixBuffer = pMixBuffer;
    intFrame.numSamples = numSamples;

    /* check for end of sample */
    if ((pWTVoice->loopStart != WT_NOISE_GENERATOR) && (pWTVoice->loopStart == pWTVoice->loopEnd))
        done = WT_CheckSampleEnd(pWTVoice, &intFrame, (EAS_BOOL) (voiceNum >= NUM_PRIMARY_VOICES));
    else
        done = EAS_FALSE;

    if (intFrame.numSamples < 0) intFrame.numSamples = 0;

    if (intFrame.numSamples > BUFFER_SIZE_IN_MONO_SAMPLES)
        intFrame.numSamples = BUFFER_SIZE_IN_MONO_SAMPLES;

    if (intFrame.numSamples < BUFFER_SIZE_IN_MONO_SAMPLES)
        memset(pMixBuffer + intFrame.numSamples * NUM_OUTPUT_CHANNELS, 0, (BUFFER_SIZE_IN_MONO_SAMPLES - intFrame.numSamples) * NUM_OUTPUT_CHANNELS * sizeof(EAS_I32));

#ifdef EAS_SPLIT_WT_SYNTH
    if (voiceNum < NUM_PRIMARY_VOICES)
    {
#ifndef _SPLIT_WT_TEST_HARNESS
        WT_ProcessVoice(pWTVoice, &intFrame);
#endif
    }
    else
        WTE_ProcessVoice(voiceNum - NUM_PRIMARY_VOICES, &intFrame.frame, pVoiceMgr->pFrameBuffer);
#else
    WT_ProcessVoice(pWTVoice, &intFrame);
#endif

    /* clear flag */
    pVoice->voiceFlags &= ~VOICE_FLAG_NO_SAMPLES_SYNTHESIZED_YET;

    /* if voice has finished, set flag for voice manager */
    if ((pVoice->voiceState != eVoiceStateStolen) && (pWTVoice->eg1State == eEnvelopeStateMuted))
        done = EAS_TRUE;

    /* if the update interval has elapsed, then force the current gain to the next
     * gain since we never actually reach the next gain when ramping -- we just get
     * very close to the target gain.
     */
    pVoice->gain = (EAS_I16) intFrame.frame.gainTarget;

    return done;
}

/*----------------------------------------------------------------------------
 * WT_UpdatePhaseInc()
 *----------------------------------------------------------------------------
 * Purpose:
 * Calculate the phase increment
 *
 * Inputs:
 * pVoice - pointer to the voice being updated
 * psRegion - pointer to the region
 * psArticulation - pointer to the articulation
 * nChannelPitchForThisVoice - the portion of the pitch that is fixed for this
 *                  voice during the duration of this synthesis
 * pEASData - pointer to overall EAS data structure
 *
 * Outputs:
 *
 * Side Effects:
 * set the phase increment for this voice
 *----------------------------------------------------------------------------
*/
static EAS_I32 WT_UpdatePhaseInc (S_WT_VOICE *pWTVoice, const S_ARTICULATION *pArt, S_SYNTH_CHANNEL *pChannel, EAS_I32 pitchCents)
{
    EAS_I32 temp;

    /*pitchCents due to CC1 = LFO * (CC1 / 128) * DEFAULT_LFO_MOD_WHEEL_TO_PITCH_CENTS */
    temp = MULT_EG1_EG1(DEFAULT_LFO_MOD_WHEEL_TO_PITCH_CENTS,
        ((pChannel->modWheel) << (NUM_EG1_FRAC_BITS -7)));

    /* pitchCents due to channel pressure = LFO * (channel pressure / 128) * DEFAULT_LFO_CHANNEL_PRESSURE_TO_PITCH_CENTS */
    temp += MULT_EG1_EG1(DEFAULT_LFO_CHANNEL_PRESSURE_TO_PITCH_CENTS,
         ((pChannel->channelPressure) << (NUM_EG1_FRAC_BITS -7)));

    /* now multiply the (channel pressure + CC1) pitch values by the LFO value */
    temp = MULT_EG1_EG1(pWTVoice->modLFO.lfoValue, temp);

    /*
    add in the LFO pitch due to
    channel pressure and CC1 along with
    the LFO pitch, the EG2 pitch, and the
    "static" pitch for this voice on this channel
    */
    temp += pitchCents +
        (MULT_EG1_EG1(pWTVoice->eg2Value, pArt->eg2ToPitch)) +
        (MULT_EG1_EG1(pWTVoice->modLFO.lfoValue, pArt->lfoToPitch));

    /* convert from cents to linear phase increment */
    return EAS_Calculate2toX(temp);
}

/*----------------------------------------------------------------------------
 * WT_UpdateChannel()
 *----------------------------------------------------------------------------
 * Purpose:
 * Calculate and assign static channel parameters
 * These values only need to be updated if one of the controller values
 * for this channel changes
 *
 * Inputs:
 * nChannel - channel to update
 * pEASData - pointer to overall EAS data structure
 *
 * Outputs:
 *
 * Side Effects:
 * - the given channel's static gain and static pitch are updated
 *----------------------------------------------------------------------------
*/
/*lint -esym(715, pVoiceMgr) reserved for future use */
static void WT_UpdateChannel (S_VOICE_MGR *pVoiceMgr, S_SYNTH *pSynth, EAS_U8 channel)
{
    EAS_I32 staticGain;
    EAS_I32 pitchBend;
    S_SYNTH_CHANNEL *pChannel;

    pChannel = &pSynth->channels[channel];

    /*
    nChannelGain = (CC7 * CC11)^2  * master volume
    where CC7 == 100 by default, CC11 == 127, master volume == 32767
    */
    staticGain = MULT_EG1_EG1((pChannel->volume) * 32768 / 127,
        (pChannel->expression) * 32768 / 127);

    /* staticGain has to be squared */
    staticGain = MULT_EG1_EG1(staticGain, staticGain);

    pChannel->staticGain = (EAS_I16) SATURATE_EG1(MULT_EG1_EG1(staticGain, pSynth->masterVolume));

    /*
    calculate pitch bend: RPN0 * ((2*pitch wheel)/16384  -1)
    However, if we use the EG1 macros, remember that EG1 has a full
    scale value of 32768 (instead of 16384). So instead of multiplying
    by 2, multiply by 4 (left shift by 2), and subtract by 32768 instead
    of 16384. This utilizes the fact that the EG1 macro places a binary
    point 15 places to the left instead of 14 places.
    */
    /*lint -e{703} <avoid multiply for performance>*/
    pitchBend =
        (((EAS_I32)(pChannel->pitchBend) << 2)
        - 32768);

    pChannel->staticPitch =
        MULT_EG1_EG1(pitchBend, pChannel->pitchBendSensitivity);

    /* if this is not a drum channel, then add in the per-channel tuning */
    if (!(pChannel->channelFlags & CHANNEL_FLAG_RHYTHM_CHANNEL))
        pChannel->staticPitch += pChannel->finePitch + (pChannel->coarsePitch * 100);

    /* clear update flag */
    pChannel->channelFlags &= ~CHANNEL_FLAG_UPDATE_CHANNEL_PARAMETERS;
    return;
}

/*----------------------------------------------------------------------------
 * WT_UpdateGain()
 *----------------------------------------------------------------------------
 * Purpose:
 * Calculate and assign static voice parameters as part of WT_UpdateVoice()
 *
 * Inputs:
 * pVoice - ptr to the synth voice that we want to synthesize
 * pEASData - pointer to overall EAS data structure
 *
 * Outputs:
 *
 * Side Effects:
 * - various voice parameters are calculated and assigned
 *
 *----------------------------------------------------------------------------
*/
static EAS_I32 WT_UpdateGain (S_SYNTH_VOICE *pVoice, S_WT_VOICE *pWTVoice, const S_ARTICULATION *pArt, S_SYNTH_CHANNEL *pChannel, EAS_I32 gain)
{
    EAS_I32 lfoGain;
    EAS_I32 temp;

    /*
    If this voice was stolen, then the velocity is actually
    for the new note, not the note that we are currently ramping down.
    So we really shouldn't use this velocity. However, that would require
    more memory to store the velocity value, and the improvement may
    not be sufficient to warrant the added memory.
    */
    /* velocity is fixed at note start for a given voice and must be squared */
    temp = (pVoice->velocity) << (NUM_EG1_FRAC_BITS - 7);
    temp = MULT_EG1_EG1(temp, temp);

    /* region gain is fixed as part of the articulation */
    temp = MULT_EG1_EG1(temp, gain);

    /* include the channel gain */
    temp = MULT_EG1_EG1(temp, pChannel->staticGain);

    /* calculate LFO gain using an approximation for 10^x */
    lfoGain = MULT_EG1_EG1(pWTVoice->modLFO.lfoValue, pArt->lfoToGain);
    lfoGain = MULT_EG1_EG1(lfoGain, LFO_GAIN_TO_CENTS);

    /* convert from a dB-like value to linear gain */
    lfoGain = EAS_Calculate2toX(lfoGain);
    temp = MULT_EG1_EG1(temp, lfoGain);

    /* calculate the voice's gain */
    temp = (EAS_I16)MULT_EG1_EG1(temp, pWTVoice->eg1Value);

    return temp;
}

/*----------------------------------------------------------------------------
 * WT_UpdateEG1()
 *----------------------------------------------------------------------------
 * Purpose:
 * Calculate the EG1 envelope for the given voice (but do not update any
 * state)
 *
 * Inputs:
 * pVoice - ptr to the voice whose envelope we want to update
 * nVoice - this voice's number - used only for debug
 * pEASData - pointer to overall EAS data structure
 *
 * Outputs:
 * nValue - the envelope value
 *
 * Side Effects:
 * - updates EG1 state value for the given voice
 *----------------------------------------------------------------------------
*/
static void WT_UpdateEG1 (S_WT_VOICE *pWTVoice, const S_ENVELOPE *pEnv)
{
    EAS_I32 temp;

    switch (pWTVoice->eg1State)
    {
        case eEnvelopeStateAttack:
            temp = pWTVoice->eg1Value + pWTVoice->eg1Increment;

            /* check if we have reached peak amplitude */
            if (temp >= SYNTH_FULL_SCALE_EG1_GAIN)
            {
                /* limit the volume */
                temp = SYNTH_FULL_SCALE_EG1_GAIN;

                /* prepare to move to decay state */
                pWTVoice->eg1State = eEnvelopeStateDecay;
                pWTVoice->eg1Increment = pEnv->decayTime;
            }

            break;

        /* exponential decay */
        case eEnvelopeStateDecay:
            temp = MULT_EG1_EG1(pWTVoice->eg1Value, pWTVoice->eg1Increment);

            /* check if we have reached sustain level */
            if (temp <= pEnv->sustainLevel)
            {
                /* enforce the sustain level */
                temp = pEnv->sustainLevel;

                /* if sustain level is zero, skip sustain & release the voice */
                if (temp > 0)
                    pWTVoice->eg1State = eEnvelopeStateSustain;

                /* move to sustain state */
                else
                    pWTVoice->eg1State = eEnvelopeStateMuted;
            }

            break;

        case eEnvelopeStateSustain:
            return;

        case eEnvelopeStateRelease:
            temp = MULT_EG1_EG1(pWTVoice->eg1Value, pWTVoice->eg1Increment);

            /* if we hit zero, this voice isn't contributing any audio */
            if (temp <= 0)
            {
                temp = 0;
                pWTVoice->eg1State = eEnvelopeStateMuted;
            }
            break;

        /* voice is muted, set target to zero */
        case eEnvelopeStateMuted:
            temp = 0;
            break;

        case eEnvelopeStateInvalid:
        default:
            temp = 0;
#ifdef  _DEBUG_SYNTH
            { /* dpp: EAS_ReportEx(_EAS_SEVERITY_INFO, "WT_UpdateEG1: error, %d is an unrecognized state\n",
                pWTVoice->eg1State); */ }
#endif
            break;

    }

    pWTVoice->eg1Value = (EAS_I16) temp;
}

/*----------------------------------------------------------------------------
 * WT_UpdateEG2()
 *----------------------------------------------------------------------------
 * Purpose:
 * Update the EG2 envelope for the given voice
 *
 * Inputs:
 * pVoice - ptr to the voice whose envelope we want to update
 * pEASData - pointer to overall EAS data structure
 *
 * Outputs:
 *
 * Side Effects:
 * - updates EG2 values for the given voice
 *----------------------------------------------------------------------------
*/

static void WT_UpdateEG2 (S_WT_VOICE *pWTVoice, const S_ENVELOPE *pEnv)
{
    EAS_I32 temp;

    switch (pWTVoice->eg2State)
    {
        case eEnvelopeStateAttack:
            temp = pWTVoice->eg2Value + pWTVoice->eg2Increment;

            /* check if we have reached peak amplitude */
            if (temp >= SYNTH_FULL_SCALE_EG1_GAIN)
            {
                /* limit the volume */
                temp = SYNTH_FULL_SCALE_EG1_GAIN;

                /* prepare to move to decay state */
                pWTVoice->eg2State = eEnvelopeStateDecay;

                pWTVoice->eg2Increment = pEnv->decayTime;
            }

            break;

            /* implement linear pitch decay in cents */
        case eEnvelopeStateDecay:
            temp = pWTVoice->eg2Value -pWTVoice->eg2Increment;

            /* check if we have reached sustain level */
            if (temp <= pEnv->sustainLevel)
            {
                /* enforce the sustain level */
                temp = pEnv->sustainLevel;

                /* prepare to move to sustain state */
                pWTVoice->eg2State = eEnvelopeStateSustain;
            }
            break;

        case eEnvelopeStateSustain:
            return;

        case eEnvelopeStateRelease:
            temp = pWTVoice->eg2Value - pWTVoice->eg2Increment;

            if (temp <= 0)
            {
                temp = 0;
                pWTVoice->eg2State = eEnvelopeStateMuted;
            }

            break;

        /* voice is muted, set target to zero */
        case eEnvelopeStateMuted:
            temp = 0;
            break;

        case eEnvelopeStateInvalid:
        default:
            temp = 0;
#ifdef  _DEBUG_SYNTH
            { /* dpp: EAS_ReportEx(_EAS_SEVERITY_INFO, "WT_UpdateEG2: error, %d is an unrecognized state\n",
                pWTVoice->eg2State); */ }
#endif
            break;
    }

    pWTVoice->eg2Value = (EAS_I16) temp;
}

/*----------------------------------------------------------------------------
 * WT_UpdateLFO ()
 *----------------------------------------------------------------------------
 * Purpose:
 * Calculate the LFO for the given voice
 *
 * Inputs:
 * pLFO         - ptr to the LFO data
 * phaseInc     - phase increment
 *
 * Outputs:
 *
 * Side Effects:
 * - updates LFO values for the given voice
 *----------------------------------------------------------------------------
*/
// triangle wave LFO
// (0, 0) (8192, 32767) (16384, -1) (24576, -32768) (32767, -4)
// x is added by phaseInc per BUFFER_SIZE_IN_MONO_SAMPLES
// phaseinc = (32768*f)/(srate/bufsize)
void WT_UpdateLFO (S_LFO_CONTROL *pLFO, EAS_I16 phaseInc)
{
    /* To save memory, if m_nPhaseValue is negative, we are in the
     * delay phase, and m_nPhaseValue represents the time left
     * in the delay.
     */
     if (pLFO->lfoPhase < 0)
     {
        pLFO->lfoPhase++;
        return;
     }

    /* calculate LFO output from phase value */
    /*lint -e{701} Use shift for performance */
    pLFO->lfoValue = (EAS_I16) (pLFO->lfoPhase << 2);
    /*lint -e{502} <shortcut to turn sawtooth into triangle wave> */
    if ((pLFO->lfoPhase > 0x1fff) && (pLFO->lfoPhase < 0x6000))
        pLFO->lfoValue = ~pLFO->lfoValue;

    /* update LFO phase */
    pLFO->lfoPhase = (pLFO->lfoPhase + phaseInc) & 0x7fff;
}

#ifdef _FILTER_ENABLED
/*----------------------------------------------------------------------------
 * WT_UpdateFilter()
 *----------------------------------------------------------------------------
 * Purpose:
 * Update the Filter parameters
 *
 * Inputs:
 * pVoice - ptr to the voice whose filter we want to update
 * pEASData - pointer to overall EAS data structure
 *
 * Outputs:
 *
 * Side Effects:
 * - updates Filter values for the given voice
 *----------------------------------------------------------------------------
*/
static void WT_UpdateFilter (S_WT_VOICE *pWTVoice, S_WT_INT_FRAME *pIntFrame, const S_ARTICULATION *pArt)
{
    EAS_I32 cutoff;

    /* no need to calculate filter coefficients if it is bypassed */
    if (pArt->filterCutoff == DEFAULT_EAS_FILTER_CUTOFF_FREQUENCY)
    {
#ifdef _FLOAT_DCF
        pIntFrame->frame.b02 = 0.0f;
#else
        pIntFrame->frame.k = 0;
#endif
        return;
    }

    /* determine the dynamic cutoff frequency */
    cutoff = MULT_EG1_EG1(pWTVoice->eg2Value, pArt->eg2ToFc);
    cutoff += pArt->filterCutoff;

    WT_SetFilterCoeffs(pIntFrame, cutoff, pArt->filterQ);
}
#endif


