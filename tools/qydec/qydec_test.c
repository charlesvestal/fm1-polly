/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the QY70 bank decoder: decodes every style of a bank (tools/qy70_codec.py --out) and writes the
 * events as text, in the format of qy70_codec.py's dump(), for the comparison --ctest makes; prints the decode
 * speed and the largest style's decoded size.
 *   qydec_test BANK OUT.txt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qydec.h"

#define MAXN 4096
#define MAXC 4096
static qy_note_t notes[MAXN];
static qy_ctl_t ctl[MAXC];
static qy_phrase_t ph[64];
static qy_work_t work;

static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *b;
    long n;
    unsigned ns, np, p, s;
    double secs = 0;
    long tot_notes = 0, tot_ctl = 0, biggest = 0;
    if (argc < 3 || !(f = fopen(argv[1], "rb")))
        return 2;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n || memcmp(b, "QYB3", 4))
        return 2;
    fclose(f);
    if (!(f = fopen(argv[2], "w")))
        return 2;
    ns = rd16(b + 4), np = rd16(b + 6);
    p = 8 + np;
    for (s = 0; s < ns; s++) {
        unsigned hl = rd16(b + p), dl = rd16(b + p + 2), tm, nph, k;
        const uint8_t *h = b + p + 4;
        char name[9];
        int rc, len = 8;
        clock_t t0;
        long bytes;
        tm = h[14];
        nph = h[15 + 7 * (unsigned)__builtin_popcount(tm)];
        memcpy(name, h, 8);
        while (len && name[len - 1] == ' ')
            len--;
        name[len] = 0;
        t0 = clock();
        for (int rep = 0; rep < 20; rep++)
            rc = qy_decode_style(h + hl, dl, b + 8, (int)nph, ph, notes, MAXN, ctl, MAXC, &work);
        secs += (double)(clock() - t0) / CLOCKS_PER_SEC / 20;
        if (rc) {
            fprintf(stderr, "style %s: decode error %d\n", name, rc);
            return 1;
        }
        fprintf(f, "style %s %u\n", name, nph);
        bytes = 0;
        for (k = 0; k < nph; k++) {
            qy_phrase_t *P = &ph[k];
            fprintf(f, "phrase %d %d %d %d %d %d %d %d\n", P->typ, P->drum, P->lo, P->hi, P->hikey, P->length,
                    P->nnotes, P->nctl);
            for (int i = 0; i < P->nnotes; i++) {
                const qy_note_t *x = &notes[P->note0 + i];
                fprintf(f, "n %d %d %d %d\n", x->t, x->gate, x->key, x->vel);
            }
            for (int i = 0; i < P->nctl; i++) {
                const qy_ctl_t *c = &ctl[P->ctl0 + i];
                fprintf(f, "c %d %d %d\n", c->t, c->kind, c->val);
            }
            tot_notes += P->nnotes, tot_ctl += P->nctl;
            bytes += (long)P->nnotes * (long)sizeof(qy_note_t) + (long)P->nctl * (long)sizeof(qy_ctl_t) +
                     (long)sizeof(qy_phrase_t);
        }
        if (bytes > biggest)
            biggest = bytes;
        p += 4 + hl + dl;
    }
    fclose(f);
    printf("qydec: %u styles, %ld notes, %ld controller events decoded in %.1f ms on this host (%.0f ns a note or "
           "controller event); the largest style decodes to %ld bytes; working memory %u bytes\n",
           ns, tot_notes, tot_ctl, secs * 1000, secs * 1e9 / (double)(tot_notes + tot_ctl), biggest,
           (unsigned)sizeof(qy_work_t));
    return 0;
}
