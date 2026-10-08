#include "eas_options.h"
#include "eas_wtengine.h"
#include "eas_report.h"
#include "eas_audioconst.h"
#include "eas_math.h"
#include "eas_synth.h"

#include "log/log.h"
#include <cutils/log.h>

#if defined(_FILTER_ENABLED) && !defined(_FLOAT_DCF)

/* adjust the filter cutoff frequency to the sample rate */
#if defined (_SAMPLE_RATE_8000)
#define FILTER_CUTOFF_FREQ_ADJUST       0
#elif defined (_SAMPLE_RATE_16000)
#define FILTER_CUTOFF_FREQ_ADJUST       1200
#elif defined (_SAMPLE_RATE_20000)
#define FILTER_CUTOFF_FREQ_ADJUST       1586
#elif defined (_SAMPLE_RATE_22050)
#define FILTER_CUTOFF_FREQ_ADJUST       1756
#elif defined (_SAMPLE_RATE_24000)
#define FILTER_CUTOFF_FREQ_ADJUST       1902
#elif defined (_SAMPLE_RATE_32000)
#define FILTER_CUTOFF_FREQ_ADJUST       2400
#elif defined (_SAMPLE_RATE_44100)
#define FILTER_CUTOFF_FREQ_ADJUST       2956
#elif defined (_SAMPLE_RATE_48000)
#define FILTER_CUTOFF_FREQ_ADJUST       3102
#else
#error "_SAMPLE_RATE_XXXXX must be defined to valid rate"
#endif

/*----------------------------------------------------------------------------
 * WT_VoiceFilter
 *----------------------------------------------------------------------------
 * Purpose:
 * Implements a 2-pole IIR filter
 *
 * Inputs:
 *
 * Outputs:
 *
 *----------------------------------------------------------------------------
*/
void WT_VoiceFilter (S_FILTER_CONTROL *pFilter, S_WT_INT_FRAME *pWTIntFrame)
{
    EAS_PCM *pAudioBuffer;
    EAS_I32 k;
    EAS_I32 b1;
    EAS_I32 b2;
    EAS_I32 z1;
    EAS_I32 z2;
    EAS_I32 acc0;
    EAS_I32 acc1;
    EAS_I32 numSamples;

    /* initialize some local variables */
    numSamples = pWTIntFrame->numSamples;
    if (numSamples <= 0) {
        EAS_Report(_EAS_SEVERITY_ERROR, "%s: numSamples <= 0\n", __func__);
        ALOGE("b/26366256");
        android_errorWriteLog(0x534e4554, "26366256");
        return;
    } else if (numSamples > BUFFER_SIZE_IN_MONO_SAMPLES) {
        EAS_Report(_EAS_SEVERITY_ERROR, "%s: numSamples %d > %d BUFFER_SIZE_IN_MONO_SAMPLES\n", __func__, numSamples, BUFFER_SIZE_IN_MONO_SAMPLES);
        ALOGE("b/317780080 clip numSamples %d -> %d", numSamples, BUFFER_SIZE_IN_MONO_SAMPLES);
        android_errorWriteLog(0x534e4554, "317780080");
        numSamples = BUFFER_SIZE_IN_MONO_SAMPLES;
    }
    pAudioBuffer = pWTIntFrame->pAudioBuffer;

    z1 = pFilter->z1;
    z2 = pFilter->z2;
    b1 = -pWTIntFrame->frame.b1;

    /*lint -e{702} <avoid divide> */
    b2 = -pWTIntFrame->frame.b2 >> 1;

    /*lint -e{702} <avoid divide> */
    k = pWTIntFrame->frame.k >> 1;

    while (numSamples--)
    {

        /* do filter calculations */
        acc0 = *pAudioBuffer;
        acc1 = z1 * b1;
        acc1 += z2 * b2;
        acc0 = acc1 + k * acc0;
        z2 = z1;

        /*lint -e{702} <avoid divide> */
        z1 = acc0 >> 14;
        *pAudioBuffer++ = (EAS_I16) z1;
    }

    /* save delay values     */
    pFilter->z1 = (EAS_I16) z1;
    pFilter->z2 = (EAS_I16) z2;
}

/*----------------------------------------------------------------------------
 * coef
 *----------------------------------------------------------------------------
 * Table of filter coefficients for low-pass filter
 *----------------------------------------------------------------------------
 *
 * polynomial coefficients are based on 8kHz sampling frequency
 * filter coef b2 = k2 = k2g0*k^0 + k2g1*k^1*(2^x) + k2g2*k^2*(2^x)
 *
 *where k2g0, k2g1, k2g2 are from the truncated power series expansion on theta
 *(k*2^x = theta, but we incorporate the k along with the k2g0, k2g1, k2g2)
 *note: this is a power series in 2^x, not k*2^x
 *where k = (2*pi*440)/8kHz == convert octaves to radians
 *
 *  so actually, the following coefs listed as k2g0, k2g1, k2g2 are really
 *  k2g0*k^0 = k2g0
 *  k2g1*k^1
 *  k2g2*k^2
 *
 *
 * filter coef n1 = numerator = n1g0*k^0 + n1g1*k^1*(2^x) + n1g2*k^2*(2^x) + n1g3*k^3*(2^x)
 *
 *where n1g0, n1g1, n1g2, n1g3 are from the truncated power series expansion on theta
 *(k*2^x = theta, but we incorporate the k along with the n1g0, n1g1, n1g2, n2g3)
 *note: this is a power series in 2^x, not k*2^x
 *where k = (2*pi*440)/8kHz == convert octaves to radians
 *we also include the optimization factor of 0.81
 *
 *  so actually, the following coefs listed as n1g0, n1g1, n1g2, n2g3 are really
 *  n1g0*k^0 = n1g0
 *  n1g1*k^1
 *  n1g2*k^2
 *  n1g3*k^3
 *
 *  NOTE that n1g0 == n1g1 == 0, always, so we only need to store n1g2 and n1g3
 *----------------------------------------------------------------------------
*/

static const EAS_I16 nk1g0 = -32768;
static const EAS_I16 nk1g2 = 1580;
static const EAS_I16 k2g0 = 32767;

static const EAS_I16 k2g1[] =
{
        -11324, /* k2g1[0] = -0.3455751918948761 */
        -10387, /* k2g1[1] = -0.3169878073928751 */
        -9528,  /* k2g1[2] = -0.29076528753345476 */
        -8740,  /* k2g1[3] = -0.2667120011011279 */
        -8017,  /* k2g1[4] = -0.24464850028971705 */
        -7353,  /* k2g1[5] = -0.22441018194495696 */
        -6745,  /* k2g1[6] = -0.20584605955455101 */
        -6187,  /* k2g1[7] = -0.18881763682420102 */
        -5675,  /* k2g1[8] = -0.1731978744360067 */
        -5206,  /* k2g1[9] = -0.15887024228080968 */
        -4775,  /* k2g1[10] = -0.14572785009373057 */
        -4380,  /* k2g1[11] = -0.13367265000706827 */
        -4018,  /* k2g1[12] = -0.1226147050712642 */
        -3685,  /* k2g1[13] = -0.11247151828678581 */
        -3381,  /* k2g1[14] = -0.10316741714122014 */
        -3101,  /* k2g1[15] = -0.0946329890599603 */
        -2844,  /* k2g1[16] = -0.08680456355870586 */
        -2609,  /* k2g1[17] = -0.07962373723441349 */
        -2393,  /* k2g1[18] = -0.07303693805092666 */
        -2195,  /* k2g1[19] = -0.06699502566866912 */
        -2014,  /* k2g1[20] = -0.06145292483669077 */
        -1847,  /* k2g1[21] = -0.056369289112013346 */
        -1694,  /* k2g1[22] = -0.05170619239747895 */
        -1554,  /* k2g1[23] = -0.04742884599684141 */
        -1426,  /* k2g1[24] = -0.043505339076210514 */
        -1308,  /* k2g1[25] = -0.03990640059558053 */
        -1199,  /* k2g1[26] = -0.03660518093435039 */
        -1100,  /* k2g1[27] = -0.03357705158166837 */
        -1009,  /* k2g1[28] = -0.030799421397205727 */
        -926,   /* k2g1[29] = -0.028251568071585884 */
        -849    /* k2g1[30] = -0.025914483529091967 */
};

static const EAS_I16 k2g2[] =
{
        1957,   /* k2g2[0] = 0.059711106626580836 */
        1646,   /* k2g2[1] = 0.05024063501786333 */
        1385,   /* k2g2[2] = 0.042272226217199664 */
        1165,   /* k2g2[3] = 0.03556764576567844 */
        981,    /* k2g2[4] = 0.029926444346999134 */
        825,    /* k2g2[5] = 0.025179964880280382 */
        694,    /* k2g2[6] = 0.02118630011706455 */
        584,    /* k2g2[7] = 0.01782604998793514 */
        491,    /* k2g2[8] = 0.014998751854573014 */
        414,    /* k2g2[9] = 0.012619876941179595 */
        348,    /* k2g2[10] = 0.010618303146468736 */
        293,    /* k2g2[11] = 0.008934188679954682 */
        246,    /* k2g2[12] = 0.007517182949855368 */
        207,    /* k2g2[13] = 0.006324921212866403 */
        174,    /* k2g2[14] = 0.005321757979794424 */
        147,    /* k2g2[15] = 0.004477701309210577 */
        123,    /* k2g2[16] = 0.00376751612730811 */
        104,    /* k2g2[17] = 0.0031699697655869644 */
        87,     /* k2g2[18] = 0.00266719715992703 */
        74,     /* k2g2[19] = 0.0022441667321724647 */
        62,     /* k2g2[20] = 0.0018882309854916855 */
        52,     /* k2g2[21] = 0.0015887483774966232 */
        44,     /* k2g2[22] = 0.0013367651661223448 */
        37,     /* k2g2[23] = 0.0011247477162958733 */
        31,     /* k2g2[24] = 0.0009463572640678758 */
        26,     /* k2g2[25] = 0.0007962604042473498 */
        22,     /* k2g2[26] = 0.0006699696356181593 */
        18,     /* k2g2[27] = 0.0005637091964589207 */
        16,     /* k2g2[28] = 0.00047430217920125243 */
        13,     /* k2g2[29] = 0.00039907554925166274 */
        11      /* k2g2[30] = 0.00033578022828973666 */
};

static const EAS_I16 n1g2[] =
{
        3170,   /* n1g2[0] = 0.0967319927350769 */
        3036,   /* n1g2[1] = 0.0926446051254155 */
        2908,   /* n1g2[2] = 0.08872992911818503 */
        2785,   /* n1g2[3] = 0.08498066682523227 */
        2667,   /* n1g2[4] = 0.08138982872895201 */
        2554,   /* n1g2[5] = 0.07795072065216213 */
        2446,   /* n1g2[6] = 0.0746569312785634 */
        2343,   /* n1g2[7] = 0.07150232020051943 */
        2244,   /* n1g2[8] = 0.06848100647187474 */
        2149,   /* n1g2[9] = 0.06558735764447099 */
        2058,   /* n1g2[10] = 0.06281597926792246 */
        1971,   /* n1g2[11] = 0.06016170483307614 */
        1888,   /* n1g2[12] = 0.05761958614040857 */
        1808,   /* n1g2[13] = 0.05518488407540374 */
        1732,   /* n1g2[14] = 0.052853059773715245 */
        1659,   /* n1g2[15] = 0.05061976615964251 */
        1589,   /* n1g2[16] = 0.04848083984214659 */
        1521,   /* n1g2[17] = 0.046432293353298 */
        1457,   /* n1g2[18] = 0.04447030771468711 */
        1396,   /* n1g2[19] = 0.04259122531793907 */
        1337,   /* n1g2[20] = 0.040791543106060944 */
        1280,   /* n1g2[21] = 0.03906790604290942 */
        1226,   /* n1g2[22] = 0.037417100858604564 */
        1174,   /* n1g2[23] = 0.035836050059229754 */
        1125,   /* n1g2[24] = 0.03432180618965023 */
        1077,   /* n1g2[25] = 0.03287154633875494 */
        1032,   /* n1g2[26] = 0.03148256687687814 */
        988,    /* n1g2[27] = 0.030152278415589925 */
        946,    /* n1g2[28] = 0.028878200980459685 */
        906,    /* n1g2[29] = 0.02765795938779331 */
        868     /* n1g2[30] = 0.02648927881672521 */
};

static const EAS_I16 n1g3[] =
{
        -548,   /* n1g3[0] = -0.016714088475899017 */
        -481,   /* n1g3[1] = -0.014683605122742116 */
        -423,   /* n1g3[2] = -0.012899791676436092 */
        -371,   /* n1g3[3] = -0.01133268185193299 */
        -326,   /* n1g3[4] = -0.00995594976868754 */
        -287,   /* n1g3[5] = -0.008746467702146129 */
        -252,   /* n1g3[6] = -0.00768391756106361 */
        -221,   /* n1g3[7] = -0.006750449563854721 */
        -194,   /* n1g3[8] = -0.005930382380083576 */
        -171,   /* n1g3[9] = -0.005209939699767622 */
        -150,   /* n1g3[10] = -0.004577018805123356 */
        -132,   /* n1g3[11] = -0.004020987256990177 */
        -116,   /* n1g3[12] = -0.003532504280467257 */
        -102,   /* n1g3[13] = -0.00310336384922047 */
        -89,    /* n1g3[14] = -0.002726356832432369 */
        -78,    /* n1g3[15] = -0.002395149888601605 */
        -69,    /* n1g3[16] = -0.0021041790717285314 */
        -61,    /* n1g3[17] = -0.0018485563625771063 */
        -53,    /* n1g3[18] = -0.001623987554831628 */
        -47,    /* n1g3[19] = -0.0014267001167177025 */
        -41,    /* n1g3[20] = -0.0012533798162347005 */
        -36,    /* n1g3[21] = -0.0011011150453668693 */
        -32,    /* n1g3[22] = -0.0009673479079754438 */
        -28,    /* n1g3[23] = -0.0008498312496971563 */
        -24,    /* n1g3[24] = -0.0007465909079943587 */
        -21,    /* n1g3[25] = -0.0006558925481952733 */
        -19,    /* n1g3[26] = -0.0005762125284029567 */
        -17,    /* n1g3[27] = -0.0005062123038325457 */
        -15,    /* n1g3[28] = -0.0004447159405951901 */
        -13,    /* n1g3[29] = -0.00039069036118270117 */
        -11     /* n1g3[30] = -0.00034322798979677605 */
};

/*----------------------------------------------------------------------------
 * WT_SetFilterCoeffs()
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
void WT_SetFilterCoeffs (S_WT_INT_FRAME *pIntFrame, EAS_I32 cutoff, EAS_I32 resonance)
{
    EAS_I32 temp;

    /* subtract the A5 offset and the sampling frequency */
    cutoff -= FILTER_CUTOFF_FREQ_ADJUST + A5_PITCH_OFFSET_IN_CENTS;

    /* limit the cutoff frequency */
    if (cutoff > FILTER_CUTOFF_MAX_PITCH_CENTS)
        cutoff = FILTER_CUTOFF_MAX_PITCH_CENTS;
    else if (cutoff < FILTER_CUTOFF_MIN_PITCH_CENTS)
        cutoff = FILTER_CUTOFF_MIN_PITCH_CENTS;

    /*
    Convert the cutoff, which has had A5 subtracted, using the 2^x approx
    Note, this cutoff is related to theta cutoff by
    theta = k * 2^x
    We use 2^x and incorporate k in the power series coefs instead
    */
    cutoff = EAS_Calculate2toX(cutoff);

    /* calculate b2 coef */
    temp = k2g1[resonance] + MULT_AUDIO_COEF(cutoff, k2g2[resonance]);
    temp = k2g0 + MULT_AUDIO_COEF(cutoff, temp);
    pIntFrame->frame.b2 = temp;

    /* calculate b1 coef */
    temp = MULT_AUDIO_COEF(cutoff, nk1g2);
    temp = nk1g0 + MULT_AUDIO_COEF(cutoff, temp);
    temp += MULT_AUDIO_COEF(temp, pIntFrame->frame.b2);
    pIntFrame->frame.b1 = temp >> 1;

    /* calculate K coef */
    temp = n1g2[resonance] + MULT_AUDIO_COEF(cutoff, n1g3[resonance]);
    temp = MULT_AUDIO_COEF(cutoff, temp);
    temp = MULT_AUDIO_COEF(cutoff, temp);
    pIntFrame->frame.k = temp;
}

#endif
