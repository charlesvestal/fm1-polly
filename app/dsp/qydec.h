/* SPDX-License-Identifier: GPL-3.0-only */
/* The QY70 style bank's decoder (tools/qy70_codec.py writes the bank; docs/qy70-import.md has the format). One
 * style's range-coded stream is decoded at once into its phrases' notes and controllers, in RAM. Pure C99: no
 * libm, no division but by constants, no 64-bit arithmetic; reads the stream and the priors in place (flash). */
#pragma once
#include <stdint.h>

#define QY_PPQ 480
#define QY_GATE_UNIT 10
#define QY_DRUM_GATE 60
#define QY_NKINDS 7                       /* controllers: bend (top 7 bits), CC 1, CC 11, CC 10, pressure, CC 74, CC 71 */

/* the model's probabilities, in the order tools/qy70_codec.py's Layout takes them */
#define QY_UK 18
#define QY_UM 2
#define QY_USZ (QY_UK + 1 + 16 * QY_UM)
enum {
    QY_P_TYP = 0,
    QY_P_DRUM = QY_P_TYP + 8,
    QY_P_LIM = QY_P_DRUM + 1,
    QY_P_NBARS = QY_P_LIM + 1,
    QY_P_BEATS = QY_P_NBARS + 8,
    QY_P_UNIT = QY_P_BEATS + 8,
    QY_P_VMIN = QY_P_UNIT + 32,
    QY_P_VRNG = QY_P_VMIN + 128,
    QY_P_LO = QY_P_VRNG + 128,
    QY_P_HI = QY_P_LO + 128,
    QY_P_HIKEY = QY_P_HI + 128,
    QY_P_NKEYS = QY_P_HIKEY + 16,
    QY_P_KEY0 = QY_P_NKEYS + 2 * QY_USZ,
    QY_P_KEYD = QY_P_KEY0 + 2 * 128,
    QY_P_HASREF = QY_P_KEYD + 2 * QY_USZ,
    QY_P_REFD = QY_P_HASREF + 4,
    QY_P_EXACT = QY_P_REFD + 2 * QY_USZ,
    QY_P_MASK = QY_P_EXACT + 2,
    QY_P_RDT = QY_P_MASK + 4,
    QY_P_RDV = QY_P_RDT + 2 * 16,
    QY_P_NNEW = QY_P_RDV + 2 * 16,
    QY_P_SAME = QY_P_NNEW + 4 * QY_USZ,
    QY_P_STEP = QY_P_SAME + 4,
    QY_P_RES = QY_P_STEP + 2 * 4 * QY_USZ,
    QY_P_VEL = QY_P_RES + 2 * QY_USZ,
    QY_P_GSAME = QY_P_VEL + 2 * 8 * 3 * 16,
    QY_P_GATE = QY_P_GSAME + 1,
    QY_P_NRUNS = QY_P_GATE + QY_USZ,
    QY_P_KIND = QY_P_NRUNS + QY_USZ,
    QY_P_CNT = QY_P_KIND + 8,
    QY_P_CDT = QY_P_CNT + QY_USZ,
    QY_P_CDTS = QY_P_CDT + QY_NKINDS * QY_USZ,
    QY_P_CV0 = QY_P_CDTS + QY_NKINDS,
    QY_P_CDV = QY_P_CV0 + QY_NKINDS * 128,
    QY_NPRIOR = QY_P_CDV + QY_NKINDS * 3 * QY_USZ,   /* these start from the bank's priors ... */
    QY_P_KEY = QY_NPRIOR,                            /* ... the key trees from 1/2 */
    QY_NPROB = QY_P_KEY + 2 * 4 * 128
};

typedef struct {
    uint16_t t;                   /* ticks from the phrase's start (480 a beat) */
    uint16_t gate;                /* ticks */
    uint8_t key, vel;             /* over C M7 (drums: GM keys) */
} qy_note_t;

typedef struct {
    uint16_t t;
    uint8_t kind, val;            /* QY_NKINDS kinds; 0..127 */
} qy_ctl_t;

typedef struct {
    uint8_t typ, drum, lo, hi, hikey, rsv;   /* typ: xpose.h XT_* order (bypass, parallel, chord1, chord2, bass) */
    uint16_t length;                          /* ticks; the player repeats the phrase to fill its section */
    uint16_t note0, nnotes, ctl0, nctl;       /* its events in the output arrays */
} qy_phrase_t;

typedef struct {                  /* a decoded bar (the references' table) */
    uint16_t note0;
    uint8_t n, drum_beats;        /* notes; drum << 7 | beats */
    uint16_t start;               /* the bar's tick in its phrase */
} qy_bar_t;

typedef struct {                  /* the decoder's working memory (QY_NPROB * 2 bytes + the bar table) */
    uint16_t p[QY_NPROB];
    qy_bar_t bar[400];
} qy_work_t;

/* decode a style's stream (len bytes) into nph phrases; notes / ctl hold at most maxn / maxc events.
 * priors: the bank's packed 4-bit priors (QY_NPRIOR of them). 0, or < 0 on a damaged stream or no room. */
int qy_decode_style(const uint8_t *stream, uint32_t len, const uint8_t *priors, int nph, qy_phrase_t *ph,
                    qy_note_t *notes, int maxn, qy_ctl_t *ctl, int maxc, qy_work_t *w);
