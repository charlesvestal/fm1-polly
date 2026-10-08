/* SPDX-License-Identifier: GPL-3.0-only */
/* gm_stub.c: the GM module's API (gm.h) recording what reaches the synth */
#pragma once
#include <stdint.h>
#include "gm.h"

typedef struct {
    uint32_t frame;                      /* output frames (44.1 kHz) since the start, when it sounds (gm_stub.c) */
    uint8_t st, d1, d2;
} stub_msg_t;
#define STUB_MAX 4096
extern stub_msg_t stub_log[STUB_MAX];
extern int stub_n;
extern uint32_t stub_frame;
