// Separated from original eas_sndlib.h to avoid exposing DLS structures publicly

#ifndef _EAS_DLSLIB_H
#define _EAS_DLSLIB_H

#include "eas_types.h"
#include "eas_synthcfg.h"
#include "eas_sndlib.h"

/*----------------------------------------------------------------------------
 * DLS envelope data structure
 *----------------------------------------------------------------------------
*/
typedef struct s_dls_envelope_tag
{
    EAS_I16         delayTime;
    EAS_I16         attackTime;
    EAS_I16         holdTime;
    EAS_I16         decayTime;
    EAS_I16         sustainLevel;
    EAS_I16         releaseTime;
    EAS_I16         velToAttack;
    EAS_I16         keyNumToDecay;
    EAS_I16         keyNumToHold;
} S_DLS_ENVELOPE;

/*----------------------------------------------------------------------------
 * DLS articulation data structure
 *----------------------------------------------------------------------------
*/

typedef struct s_dls_articulation_tag
{
    S_LFO_PARAMS    modLFO;
    S_LFO_PARAMS    vibLFO;

    S_DLS_ENVELOPE  eg1;
    S_DLS_ENVELOPE  eg2;

    EAS_I16         eg1ShutdownTime;

    EAS_I16         filterCutoff;
    EAS_I16         modLFOToFc;
    EAS_I16         modLFOCC1ToFc;
    EAS_I16         modLFOChanPressToFc;
    EAS_I16         eg2ToFc;
    EAS_I16         velToFc;
    EAS_I16         keyNumToFc;

    EAS_I16         modLFOToGain;
    EAS_I16         modLFOCC1ToGain;
    EAS_I16         modLFOChanPressToGain;

    EAS_I16         tuning;
    EAS_I16         keyNumToPitch;
    EAS_I16         vibLFOToPitch;
    EAS_I16         vibLFOCC1ToPitch;
    EAS_I16         vibLFOChanPressToPitch;
    EAS_I16         modLFOToPitch;
    EAS_I16         modLFOCC1ToPitch;
    EAS_I16         modLFOChanPressToPitch;
    EAS_I16         eg2ToPitch;

    /* pad to 4-byte boundary */
    EAS_U16         pad;

    EAS_I8          pan;
    EAS_U16         filterQandFlags;

#ifdef _CC_REVERB
    EAS_I16         reverbSend;
    EAS_I16         cc91ToReverbSend;
#endif

#ifdef _CC_CHORUS
    EAS_I16         chorusSend;
    EAS_I16         cc93ToChorusSend;
#endif
} S_DLS_ARTICULATION;

/* flags in filterQandFlags
 * NOTE: Q is stored in bottom 5 bits
 */
#define FLAG_DLS_VELOCITY_SENSITIVE     0x8000
#define FILTER_Q_MASK                   0x7fff

/*----------------------------------------------------------------------------
 * DLS region data structure
 *----------------------------------------------------------------------------
*/
typedef struct s_dls_region_tag
{
    S_WT_REGION     wtRegion;
    EAS_U8          velLow;
    EAS_U8          velHigh;
} S_DLS_REGION;


enum {
    DLSLIB_TYPE_DLS = 0x00,
    DLSLIB_TYPE_SF2 = 0x10
};
/*----------------------------------------------------------------------------
 * DLS data structure
 *
 * pDLSPrograms         pointer to array of DLS programs
 * pDLSRegions          pointer to array of DLS regions
 * pDLSArticulations    pointer to array of DLS articulations
 * pSampleLen           pointer to array of sample lengths
 * ppSamples            pointer to array of sample pointers
 * numDLSPrograms       number of DLS programs
 * numDLSRegions        number of DLS regions
 * numDLSArticulations  number of DLS articulations
 * numDLSSamples        number of DLS samples
 *----------------------------------------------------------------------------
*/
typedef struct s_eas_dls_tag
{
    S_PROGRAM           *pDLSPrograms;
    S_DLS_REGION        *pDLSRegions;
    S_DLS_ARTICULATION  *pDLSArticulations;
    EAS_U32             *pDLSSampleLen; // in bytes // TODO: in samples may be better?
    EAS_U32             *pDLSSampleOffsets; // in bytes
    EAS_SAMPLE          *pDLSSamples;
    EAS_U16             numDLSPrograms;
    EAS_U16             numDLSRegions;
    EAS_U16             numDLSArticulations;
    EAS_U16             numDLSSamples;
    EAS_U8              refCount;
    EAS_U8              libType;
} S_DLS;


#endif