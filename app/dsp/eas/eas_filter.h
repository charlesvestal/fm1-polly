#pragma once
#include "eas_options.h"
#include "eas_types.h"

typedef struct s_wt_int_frame_tag S_WT_INT_FRAME;

#if defined(_FILTER_ENABLED)
/*----------------------------------------------------------------------------
 * S_FILTER_CONTROL data structure
 *----------------------------------------------------------------------------
*/
typedef struct s_filter_control_tag
{
#ifdef _FLOAT_DCF
    EAS_I32     y1;                             /* 1 sample delay output */
    EAS_I32     y2;                             /* 2 sample delay output */
    EAS_I32     x1;                             /* 1 sample delay input */
    EAS_I32     x2;                             /* 2 sample delay input */
#else
    EAS_I16     z1;                             /* 1 sample delay state variable */
    EAS_I16     z2;                             /* 2 sample delay state variable */
    EAS_I16     run;                            /* Polly: FILTER_RUN_* (WT_UpdateFilter) */
#endif
} S_FILTER_CONTROL;

/* Polly: S_FILTER_CONTROL run: how the filter ran in the voice's last block: not (OFF), as EAS (ON), moved by
 * CC74 / CC71 (MOVED: saturating). A filter switched on while a note sounds (a sound without its own filter,
 * brought in by CC74 / CC71) starts from its first input sample (PRIME), not from silence, so the note goes
 * on without a click (eas_filter.c WT_VoiceFilterMoved, eas_wtsynth.c WT_UpdateFilter) */
#define FILTER_RUN_OFF      0
#define FILTER_RUN_ON       1
#define FILTER_RUN_PRIME    2
#define FILTER_RUN_MOVED    3

void WT_VoiceFilter (S_FILTER_CONTROL* pFilter, S_WT_INT_FRAME *pWTIntFrame);
void WT_SetFilterCoeffs (S_WT_INT_FRAME *pIntFrame, EAS_I32 cutoff, EAS_I32 resonance);
void WT_SetFilterCoeffsOpen (S_WT_INT_FRAME *pIntFrame, EAS_I32 offset, EAS_I32 resonance);   /* Polly */
#endif
