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
#endif
} S_FILTER_CONTROL;

void WT_VoiceFilter (S_FILTER_CONTROL* pFilter, S_WT_INT_FRAME *pWTIntFrame);
void WT_SetFilterCoeffs (S_WT_INT_FRAME *pIntFrame, EAS_I32 cutoff, EAS_I32 resonance);
#endif
