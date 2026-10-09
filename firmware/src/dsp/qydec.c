/* SPDX-License-Identifier: GPL-3.0-only */
/* The QY70 style bank's decoder (qydec.h): LZMA's binary range decoder over the model of tools/qy70_codec.py
 * (code_phrase), which this file follows line for line. */
#include "qydec.h"

static const uint16_t PLEVEL[16] = {24, 64, 140, 280, 520, 900, 1400, 1850, 2048, 2696, 3196, 3576, 3816, 3956,
                                    4032, 4072};
static const uint8_t UNITS[22] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 16, 20, 24, 30, 40, 48, 60, 80, 96, 120, 160, 240};

typedef struct {
    const uint8_t *d, *end;
    uint32_t rng, code;
    uint16_t *p;
} rc_t;

static uint32_t rc_byte(rc_t *r) { return r->d < r->end ? *r->d++ : (r->d++, 0u); }

static int bit(rc_t *r, int i)                        /* i < 0: a fixed 1/2 */
{
    uint32_t p = i >= 0 ? r->p[i] : 2048u, bound = (r->rng >> 12) * p;
    int b;
    if (r->code < bound) {
        r->rng = bound;
        if (i >= 0)
            r->p[i] = (uint16_t)(p + ((4096u - p) >> 4));
        b = 0;
    } else {
        r->code -= bound;
        r->rng -= bound;
        if (i >= 0)
            r->p[i] = (uint16_t)(p - (p >> 4));
        b = 1;
    }
    while (r->rng < (1u << 24)) {
        r->rng <<= 8;
        r->code = (r->code << 8) | rc_byte(r);
    }
    return b;
}

static int tree(rc_t *r, int base, int nbits)
{
    int m = 1;
    while (nbits--)
        m = m << 1 | bit(r, base + m);
    return m;                                          /* the caller drops the top bit */
}
#define TREE(r, base, n) (tree(r, base, n) - (1 << (n)))

static uint32_t uint_(rc_t *r, int base)
{
    int n = 0, j;
    uint32_t v = 1;
    while (n < QY_UK && bit(r, base + n))
        n++;
    for (j = n - 1; j >= 0; j--) {
        int pos = n - 1 - j;
        v = v << 1 | (uint32_t)bit(r, pos < QY_UM ? base + QY_UK + 1 + (n < 15 ? n : 15) * QY_UM + pos : -1);
    }
    return v - 1u;
}

static int32_t sint(rc_t *r, int base)
{
    uint32_t z = uint_(r, base);
    return z & 1u ? -(int32_t)((z + 1u) >> 1) : (int32_t)(z >> 1);
}

static int bitlen(uint32_t v)
{
    int n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

static uint8_t vdq(int n, int vmin, int vr) { return (uint8_t)(vr ? vmin + (n * vr * 2 + 15) / 30 : vmin); }

static void sort_notes(qy_note_t *a, int n)            /* by time, then key (insertion: a bar is short) */
{
    for (int i = 1; i < n; i++) {
        qy_note_t x = a[i];
        int j = i - 1;
        while (j >= 0 && (a[j].t > x.t || (a[j].t == x.t && a[j].key > x.key))) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = x;
    }
}

int qy_decode_style(const uint8_t *stream, uint32_t len, const uint8_t *priors, int nph, qy_phrase_t *ph,
                    qy_note_t *notes, int maxn, qy_ctl_t *ctl, int maxc, qy_work_t *w)
{
    rc_t r;
    int nn = 0, nc = 0, nbar = 0, i;
    for (i = 0; i < QY_NPROB; i++)
        w->p[i] = i < QY_NPRIOR ? PLEVEL[(priors[i >> 1] >> (4 * (i & 1))) & 15] : 2048u;
    r.d = stream, r.end = stream + len, r.rng = 0xFFFFFFFFu, r.code = 0, r.p = w->p;
    for (i = 0; i < 4; i++)
        r.code = (r.code << 8) | rc_byte(&r);
    for (int pi = 0; pi < nph; pi++) {
        qy_phrase_t *P = &ph[pi];
        uint8_t keys[128];
        int typ = TREE(&r, QY_P_TYP, 3), drum = bit(&r, QY_P_DRUM), lim = bit(&r, QY_P_LIM);
        int nbars = TREE(&r, QY_P_NBARS, 3) + 1, beats = TREE(&r, QY_P_BEATS, 3) + 1, blen = beats * QY_PPQ;
        int ui = TREE(&r, QY_P_UNIT, 5), unit, vmin, vr, nk, kb, lo = 0, hi = 127, hikey = 12;
        if (ui >= 22)
            return -1;
        unit = UNITS[ui];
        vmin = TREE(&r, QY_P_VMIN, 7);
        vr = TREE(&r, QY_P_VRNG, 7);
        if (lim) {
            lo = TREE(&r, QY_P_LO, 7);
            hi = TREE(&r, QY_P_HI, 7);
            hikey = TREE(&r, QY_P_HIKEY, 4);
        }
        nk = (int)uint_(&r, QY_P_NKEYS + drum * QY_USZ) + 1;
        if (nk > 128)
            return -2;
        keys[0] = (uint8_t)TREE(&r, QY_P_KEY0 + drum * 128, 7);
        for (i = 1; i < nk; i++) {
            uint32_t k = keys[i - 1] + uint_(&r, QY_P_KEYD + drum * QY_USZ) + 1u;
            if (k > 127u)
                return -9;                             /* a key past 127: damaged */
            keys[i] = (uint8_t)k;
        }
        kb = bitlen((uint32_t)(nk - 1));
        if (kb < 1)
            kb = 1;
        P->typ = (uint8_t)typ, P->drum = (uint8_t)drum, P->lo = (uint8_t)lo, P->hi = (uint8_t)hi;
        P->hikey = (uint8_t)hikey, P->rsv = 0, P->length = (uint16_t)(nbars * blen), P->note0 = (uint16_t)nn;
        for (int b = 0; b < nbars; b++) {
            int start = b * blen, bar0 = nn, has = 0, ne = 0, e;
            uint8_t db = (uint8_t)(drum << 7 | beats);
            /* the eligible earlier bars, newest first: same kind and length, with notes */
            for (e = nbar - 1; e >= 0; e--)
                if (w->bar[e].drum_beats == db && w->bar[e].n) {
                    ne = 1;
                    break;
                }
            if (ne)
                has = bit(&r, QY_P_HASREF + drum * 2 + (b == 0));
            if (has) {
                uint32_t d = uint_(&r, QY_P_REFD + drum * QY_USZ);
                const qy_bar_t *rb = 0;
                int exact, prev = 1;
                for (e = nbar - 1; e >= 0; e--)
                    if (w->bar[e].drum_beats == db && w->bar[e].n && d-- == 0) {
                        rb = &w->bar[e];
                        break;
                    }
                if (!rb)
                    return -3;
                exact = bit(&r, QY_P_EXACT + drum);
                for (int j = 0; j < rb->n; j++) {
                    const qy_note_t *x = &notes[rb->note0 + j];
                    int k = bit(&r, QY_P_MASK + drum * 2 + prev), dt = 0, dv = 0, v;
                    prev = k;
                    if (!k)
                        continue;
                    if (!exact) {
                        dt = TREE(&r, QY_P_RDT + drum * 16, 4) - 8;
                        dv = TREE(&r, QY_P_RDV + drum * 16, 4) - 8;
                    }
                    if (nn >= maxn)
                        return -4;
                    v = x->vel + 2 * dv;
                    {
                        int32_t tt = (int32_t)x->t - rb->start + start + dt;
                        if (tt < start || tt >= start + blen)
                            return -11;                /* outside its bar: damaged */
                        notes[nn].t = (uint16_t)tt;
                    }
                    notes[nn].gate = x->gate;
                    notes[nn].key = x->key;
                    notes[nn].vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
                    nn++;
                }
            }
            {
                uint32_t nnew = uint_(&r, QY_P_NNEW + drum * 2 * QY_USZ + has * QY_USZ);
                uint32_t gi = (uint32_t)start / (uint32_t)unit;
                int32_t t = 0;
                int same = 0, gprev = -1, kprev = 0, sprev = 0;
                for (uint32_t k = 0; k < nnew; k++) {
                    int s = 0, ki, vn, ph_, place, gu = 0;
                    if (k)
                        s = bit(&r, QY_P_SAME + drum * 2 + same);
                    same = s;
                    if (!s) {
                        uint32_t st = uint_(&r, QY_P_STEP + (drum * 4 + (sprev < 3 ? sprev : 3)) * QY_USZ);
                        sprev = st > 3u ? 3 : (int)st;
                        if (st > 0xFFFFu || gi + st > 0xFFFFu)
                            return -10;                /* the grid past any phrase: damaged */
                        gi += st;
                        t = (int32_t)(gi * (uint32_t)unit) + sint(&r, QY_P_RES + drum * QY_USZ);
                        if (t < start || t >= start + blen)
                            return -11;
                    }
                    ki = TREE(&r, QY_P_KEY + (drum * 4 + (kprev < 3 ? kprev : 3)) * 128, kb);
                    kprev = ki;
                    ph_ = (t - start) % QY_PPQ;
                    place = ph_ <= 12 || ph_ >= QY_PPQ - 12 ? 0 : (ph_ - QY_PPQ / 2 <= 12 && QY_PPQ / 2 - ph_ <= 12) ? 1 : 2;
                    vn = TREE(&r, QY_P_VEL + ((drum * 8 + (ki < 7 ? ki : 7)) * 3 + place) * 16, 4);
                    if (!drum) {
                        int gs = gprev >= 0 ? bit(&r, QY_P_GSAME) : 0;
                        gu = gs ? gprev : (int)uint_(&r, QY_P_GATE);
                        gprev = gu;
                    }
                    if (nn >= maxn)
                        return -4;
                    notes[nn].t = (uint16_t)t;
                    notes[nn].gate = (uint16_t)(drum ? QY_DRUM_GATE : gu * QY_GATE_UNIT);
                    notes[nn].key = (uint8_t)(ki < nk ? keys[ki] & 127u : 0u);
                    notes[nn].vel = vdq(vn, vmin, vr);
                    nn++;
                }
            }
            sort_notes(&notes[bar0], nn - bar0);
            if (nbar >= (int)(sizeof w->bar / sizeof w->bar[0]))
                return -5;
            w->bar[nbar].note0 = (uint16_t)bar0, w->bar[nbar].n = (uint8_t)(nn - bar0);
            w->bar[nbar].drum_beats = db, w->bar[nbar].start = (uint16_t)start;
            if (nn - bar0 > 255)
                return -6;
            nbar++;
        }
        P->nnotes = (uint16_t)(nn - P->note0);
        P->ctl0 = (uint16_t)nc;
        {
            uint32_t nr = uint_(&r, QY_P_NRUNS);
            int tp = 0;
            for (uint32_t ri = 0; ri < nr; ri++) {
                int kind = TREE(&r, QY_P_KIND, 3), vp = -1, dtp = -1, dvs = 1;
                uint32_t cnt = uint_(&r, QY_P_CNT) + 1u;
                if (kind >= QY_NKINDS)
                    return -7;
                for (uint32_t j = 0; j < cnt; j++) {
                    int dt, v;
                    if (dtp >= 0 && bit(&r, QY_P_CDTS + kind))
                        dt = dtp;
                    else
                        dt = (int)uint_(&r, QY_P_CDT + kind * QY_USZ);
                    dtp = dt;
                    tp += dt;
                    if (tp > 0xFFFF)
                        return -12;
                    if (vp < 0)
                        v = TREE(&r, QY_P_CV0 + kind * 128, 7);
                    else {
                        int dv = (int)sint(&r, QY_P_CDV + (kind * 3 + dvs) * QY_USZ);
                        dvs = dv < 0 ? 0 : dv == 0 ? 1 : 2;
                        v = vp + dv;
                    }
                    if (v < 0 || v > 127)
                        return -12;                    /* a value outside 0..127: damaged */
                    vp = v;
                    if (nc >= maxc)
                        return -8;
                    ctl[nc].t = (uint16_t)tp, ctl[nc].kind = (uint8_t)kind, ctl[nc].val = (uint8_t)v;
                    nc++;
                }
            }
        }
        P->nctl = (uint16_t)(nc - P->ctl0);
    }
    return 0;
}
