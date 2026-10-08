#include "eas_options.h"
#include "eas_visibility.h"
#include "eas_types.h"
#include "eas_sndlib.h"
#include "eas_report.h"

#include <string.h>

#define DEFAULT_WT_SNDLIB "wt_200k_G"
#define DEFAULT_FM_SNDLIB "GMdblib-3"
#define DEFAULT_HYBRID_SNDLIB "hybrid_22khz_mcu"
#undef DEFAULT_SNDLIB

#ifdef EAS_WT_SYNTH
extern S_EAS easlib_wt_200k_g;
#define DEFAULT_SNDLIB DEFAULT_WT_SNDLIB
#endif
#ifdef EAS_FM_SYNTH
extern S_EAS easlib_fm_gmdblib_3;
#ifndef DEFAULT_SNDLIB
#define DEFAULT_SNDLIB DEFAULT_FM_SNDLIB
#endif
#endif
#ifdef EAS_HYBRID_SYNTH
extern S_EAS easlib_hybrid_22khz_mcu;
#ifndef DEFAULT_SNDLIB
#define DEFAULT_SNDLIB DEFAULT_HYBRID_SNDLIB
#endif
#endif

EAS_PUBLIC const char* EAS_GetDefaultSoundLibrary(E_EAS_SNDLIB_TYPE sndlibType)
{
    switch (sndlibType)
    {
    case EAS_SNDLIB_DEFAULT: // NOLINT(bugprone-branch-clone)
        return DEFAULT_SNDLIB;
#ifdef EAS_WT_SYNTH
    case EAS_SNDLIB_WT:
        return DEFAULT_WT_SNDLIB;
#endif
#ifdef EAS_FM_SYNTH
    case EAS_SNDLIB_FM:
        return DEFAULT_FM_SNDLIB;
#endif
#ifdef EAS_HYBRID_SYNTH
    case EAS_SNDLIB_HYBRID:
        return DEFAULT_HYBRID_SNDLIB;
#endif
    default:
        EAS_Report(_EAS_SEVERITY_WARNING, "EAS_GetDefaultSoundLibrary: Sound library type %d is not available.\n", sndlibType);
        return NULL;
    }
}

EAS_PUBLIC EAS_SNDLIB_HANDLE EAS_GetSoundLibrary(EAS_DATA_HANDLE pEASData, const char* libraryName)
{
    if (libraryName == NULL)
    {
        return NULL;
    }

#ifdef EAS_WT_SYNTH
    if (strcmp(libraryName, DEFAULT_WT_SNDLIB) == 0)
    {
        return (EAS_SNDLIB_HANDLE)&easlib_wt_200k_g;
    }
#endif
#ifdef EAS_FM_SYNTH
    if (strcmp(libraryName, DEFAULT_FM_SNDLIB) == 0)
    {
        return (EAS_SNDLIB_HANDLE)&easlib_fm_gmdblib_3;
    }
#endif
#ifdef EAS_HYBRID_SYNTH
    if (strcmp(libraryName, DEFAULT_HYBRID_SNDLIB) == 0)
    {
        return (EAS_SNDLIB_HANDLE)&easlib_hybrid_22khz_mcu;
    }
#endif

    EAS_Report(_EAS_SEVERITY_WARNING, "EAS_GetSoundLibrary: Sound library '%s' not found.\n", libraryName);
    return NULL;
}
