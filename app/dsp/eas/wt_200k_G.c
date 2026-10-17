/*----------------------------------------------------------------------------
 *
 * Filename: wt_200k_G.c
 * Source: wt_200k_G.dls
 * CmdLine: -w wt_200k_G.c -l wt_200k_G.log -ce -cf wt_200k_G.dls -w -l -ce -cf wt_200k_G.dls
 * Purpose: Wavetable sound libary
 *
 * Copyright (c) 2009 Sonic Network Inc.
 *
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
 *   $Revision: 960 $
 *   $Date: 2009-03-18 15:08:29 -0500 (Wed, 18 Mar 2009) $
 *----------------------------------------------------------------------------
*/

/*----------------------------------------------------------------------------
 *
 * Filename: wt_44khz.c
 * Purpose: Wavetable sound libary
 *
 * Copyright (C) 2008 The Android Open Source Project
 *
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
 */

#include "eas_options.h"
#include "eas_sndlib.h"

/*----------------------------------------------------------------------------
 * Programs
 *----------------------------------------------------------------------------
*/
static const S_PROGRAM eas_programs[] =
{
    { 7864320, 0 } /* program 0 */
}; /* end Programs */

/*----------------------------------------------------------------------------
 * Banks
 *----------------------------------------------------------------------------
*/
static const S_BANK eas_banks[] =
{
    { /* bank 0 */
        30976,
        {
            291, 324, 314, 334, 202, 319, 95, 195,
            107, 92, 371, 89, 87, 85, 135, 82,
            200, 192, 130, 267, 193, 302, 207, 210,
            128, 125, 190, 120, 118, 213, 221, 271,
            80, 78, 308, 164, 220, 310, 166, 167,
            186, 182, 181, 179, 160, 178, 176, 115,
            155, 153, 151, 149, 75, 73, 374, 111,
            252, 254, 258, 305, 256, 157, 146, 137,
            249, 237, 245, 241, 274, 262, 260, 265,
            172, 171, 309, 277, 284, 307, 136, 344,
            173, 168, 345, 353, 346, 70, 110, 311,
            357, 144, 104, 67, 364, 367, 64, 288,
            142, 140, 98, 355, 133, 123, 61, 113,
            285, 280, 279, 278, 370, 286, 359, 283,
            101, 236, 163, 235, 234, 233, 232, 231,
            162, 363, 230, 281, 165, 229, 109, 228
        }
    }
}; /* end Banks */

#ifdef _SAMPLE_RATE_44100
#include "wt_44khz.c"
#else
#include "wt_22khz.c"
#endif

#include "wt_200k_samples.c"

/*----------------------------------------------------------------------------
 * S_EAS
 *----------------------------------------------------------------------------
*/

// Can't be a 'const EAS_U32' before GCC 8.1
#ifdef _SAMPLE_RATE_44100
#define SAMPLE_RATE	0xAC44
#else
#define SAMPLE_RATE	0x5622
#endif

const S_EAS easlib_wt_200k_g = {
    0x01534145,

#if defined (_8_BIT_SAMPLES)
    0x00100000 | SAMPLE_RATE,
#else //_16_BIT_SAMPLES
    0x00200000 | SAMPLE_RATE,
#endif

    eas_banks,
    eas_programs,
    eas_regions,
    eas_articulations,
    eas_sampleLengths,
    eas_sampleOffsets,
    eas_samples,
    0,
    1,
    1,
    377,
    185,
    150,
    0
}; /* end S_EAS */

/*----------------------------------------------------------------------------
 * Statistics
 *
 * Number of banks: 1
 * Number of programs: 1
 * Number of regions: 377
 * Number of articulations: 185
 * Number of samples: 150
 * Size of sample pool: 212050
 *----------------------------------------------------------------------------
*/
