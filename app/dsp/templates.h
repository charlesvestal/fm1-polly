/* SPDX-License-Identifier: GPL-3.0-only */
/* Chord templates: 16 progressions in C, a chord a bar of the template's own beats (4, or 3 for the waltz):
 * the factory progressions, for loops.c's factory loops and LOAD PROGRESSION. */
#pragma once
#include "chords.h"
#define NTEMPLATES 16
typedef struct {
    const char *name;
    const chord_t *bars;                 /* a chord a bar */
    uint8_t nbars, beats;                /* bars, and beats a bar */
} template_t;
extern const template_t TEMPLATES[NTEMPLATES];
