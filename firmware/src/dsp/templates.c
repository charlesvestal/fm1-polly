/* SPDX-License-Identifier: GPL-3.0-only */
/* Chord templates: 16 progressions in C, a chord a bar (beats: 4 a bar; 3 for the waltz). */
#include "templates.h"
#define C_(r, t) {r, t, CH_NOBASS}
static const chord_t T_POP[] = {C_(0, CH_MAJ), C_(7, CH_MAJ), C_(9, CH_MIN), C_(5, CH_MAJ)};       /* I-V-vi-IV */
static const chord_t T_SENS[] = {C_(9, CH_MIN), C_(5, CH_MAJ), C_(0, CH_MAJ), C_(7, CH_MAJ)};      /* vi-IV-I-V */
static const chord_t T_50S[] = {C_(0, CH_MAJ), C_(9, CH_MIN), C_(2, CH_MIN), C_(7, CH_7)};         /* I-vi-ii-V */
static const chord_t T_251[] = {C_(2, CH_MIN7), C_(7, CH_7), C_(0, CH_MAJ7), C_(0, CH_MAJ7)};      /* ii-V-I */
static const chord_t T_BLUES[] = {C_(0, CH_7), C_(5, CH_7), C_(0, CH_7), C_(0, CH_7), C_(5, CH_7), C_(5, CH_7),
                                  C_(0, CH_7), C_(0, CH_7), C_(7, CH_7), C_(5, CH_7), C_(0, CH_7), C_(7, CH_7)};
static const chord_t T_MBLUES[] = {C_(0, CH_MIN7), C_(5, CH_MIN7), C_(0, CH_MIN7), C_(0, CH_MIN7), C_(5, CH_MIN7),
                                   C_(5, CH_MIN7), C_(0, CH_MIN7), C_(0, CH_MIN7), C_(8, CH_7), C_(7, CH_7),
                                   C_(0, CH_MIN7), C_(7, CH_7)};
static const chord_t T_ONEFOUR[] = {C_(0, CH_MAJ), C_(5, CH_MAJ)};                                  /* I-IV */
static const chord_t T_MINOR[] = {C_(9, CH_MIN), C_(5, CH_MAJ), C_(0, CH_MAJ), C_(7, CH_MAJ)};       /* i-VI-III-VII in Am */
static const chord_t T_CANON[] = {C_(0, CH_MAJ), C_(7, CH_MAJ), C_(9, CH_MIN), C_(4, CH_MIN), C_(5, CH_MAJ),
                                  C_(0, CH_MAJ), C_(5, CH_MAJ), C_(7, CH_MAJ)};
static const chord_t T_ANDAL[] = {C_(9, CH_MIN), C_(7, CH_MAJ), C_(5, CH_MAJ), C_(4, CH_MAJ)};       /* Am G F E */
static const chord_t T_MIXO[] = {C_(0, CH_MAJ), C_(10, CH_MAJ), C_(5, CH_MAJ), C_(0, CH_MAJ)};       /* I-bVII-IV */
static const chord_t T_DOOWOP[] = {C_(0, CH_MAJ), C_(9, CH_MIN), C_(5, CH_MAJ), C_(7, CH_MAJ)};      /* I-vi-IV-V */
static const chord_t T_JAZZ[] = {C_(0, CH_MAJ7), C_(9, CH_7), C_(2, CH_MIN7), C_(7, CH_7)};        /* I-VI7-ii-V */
static const chord_t T_VAMP[] = {C_(2, CH_MIN7), C_(7, CH_7)};                                      /* dorian vamp */
static const chord_t T_GOSPEL[] = {C_(5, CH_MAJ), C_(0, CH_MAJ), C_(5, CH_MAJ), C_(0, CH_MAJ)};     /* IV-I */
static const chord_t T_WALTZ[] = {C_(0, CH_MAJ), C_(0, CH_MAJ), C_(7, CH_7), C_(0, CH_MAJ)};
const template_t TEMPLATES[NTEMPLATES] = {
    {"Pop", T_POP, 4, 4}, {"Sensitive", T_SENS, 4, 4}, {"Fifties", T_50S, 4, 4}, {"ii-V-I", T_251, 4, 4},
    {"Blues", T_BLUES, 12, 4}, {"Minor blues", T_MBLUES, 12, 4}, {"One-four", T_ONEFOUR, 2, 4},
    {"Minor", T_MINOR, 4, 4}, {"Canon", T_CANON, 8, 4}, {"Andalusian", T_ANDAL, 4, 4}, {"Mixolydian", T_MIXO, 4, 4},
    {"Doo-wop", T_DOOWOP, 4, 4}, {"Jazz", T_JAZZ, 4, 4}, {"Vamp", T_VAMP, 2, 4}, {"Gospel", T_GOSPEL, 4, 4},
    {"Waltz", T_WALTZ, 4, 3},
};
