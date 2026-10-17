/* SPDX-License-Identifier: Apache-2.0 */
/* Sonivox EAS version (in place of the CMake-generated eas_version.h): 4.0.2, EmbeddedSynth/sonivox 86bd073 */
#ifndef EAS_VERSION_CMAKE
#define EAS_VERSION_CMAKE
#define MAKE_LIB_VERSION(a,b,c,d) (((((((EAS_U32) a <<8) | (EAS_U32) b) << 8) | (EAS_U32) c) << 8) | (EAS_U32) d)
#define LIB_VERSION MAKE_LIB_VERSION(4,0,2,0)
#endif
