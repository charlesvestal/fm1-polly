/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the QY70 style bank (dsp/qybank.c, dsp/qydec.c) on a simulated flash: a bank image (tools/qy70_codec.py
 * --image: the synthetic one of Polly's own styles, or one made from a QY70 ROM when QY70_ROM is set) written as
 * the FM-1 writes it, mounted, every style decoded; a damaged record, directory, an old version; the decode's
 * time and memory.
 *   bank_test IMAGE [STYLES]      STYLES: the count expected (the synthetic bank: 17) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qybank.h"

static uint8_t flash[0x100000];
int qb_flash_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof flash || n > sizeof flash - off)
        return -1;
    memcpy(dst, flash + off, n);
    return 0;
}
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t *img;
static long img_n;
static void load(void)                            /* the image, as bank_rx.c writes it */
{
    memset(flash, 0xFF, sizeof flash);
    memcpy(flash + QB_DIR, img, QB_SECTOR);
    for (long i = QB_SECTOR; i < img_n; i++)
        flash[qb_phys((uint32_t)(i - QB_SECTOR))] = img[i];
}
static qb_style_t st;

int main(int argc, char **argv)
{
    FILE *f;
    int n, want = argc > 2 ? atoi(argv[2]) : -1;
    if (argc < 2 || !(f = fopen(argv[1], "rb")))
        return 2;
    fseek(f, 0, SEEK_END);
    img_n = ftell(f);
    fseek(f, 0, SEEK_SET);
    img = malloc((size_t)img_n);
    if (!img || fread(img, 1, (size_t)img_n, f) != (size_t)img_n)
        return 2;
    fclose(f);

    memset(flash, 0xFF, sizeof flash);
    CHECK(qb_mount() == QB_NONE && qb_count() == 0, "an erased directory: no bank");
    load();
    n = qb_mount();
    CHECK(n > 0 && (want < 0 || n == want), "mounted: %d styles (%d expected)", n, want);
    {   /* every style decodes; the largest, the time */
        long biggest = 0, notes = 0, ctl = 0;
        double secs = 0, worst = 0;
        int big = 0;
        for (int i = 0; i < n; i++) {
            clock_t t0 = clock();
            int rc = qb_decode(i, &st);
            double dt = (double)(clock() - t0) / CLOCKS_PER_SEC;
            long bytes = (long)st.nnotes * (long)sizeof(qy_note_t) + (long)st.nctl * (long)sizeof(qy_ctl_t) +
                         (long)st.nph * (long)sizeof(qy_phrase_t);
            CHECK(rc == 0 && st.index == i, "style %d (%s) decodes: %d", i, qb_name(i), rc);
            CHECK(!strcmp(st.name, qb_name(i)), "its name %s / %s", st.name, qb_name(i));
            CHECK(st.beats >= 2 && st.beats <= 7, "%s: %d beats", st.name, st.beats);
            for (int t = 0; t < 8; t++)
                CHECK(st.trk[t].chan != 15, "%s: track %d on channel 16 (the click's)", st.name, t);
            for (int k = 0; k < 6; k++)
                for (int t = 0; t < 8; t++)
                    if (st.use[k][t] != 0xFF) {
                        const qy_phrase_t *P = &st.ph[st.use[k][t]];
                        CHECK(st.tmask >> t & 1u, "%s: section %d track %d plays a phrase on a track it lacks", st.name, k, t);
                        CHECK(P->length > 0 && P->note0 + P->nnotes <= st.nnotes, "%s: phrase in range", st.name);
                        for (int j = 1; j < P->nnotes; j++)
                            CHECK(st.note[P->note0 + j].t >= st.note[P->note0 + j - 1].t, "%s: notes in time order", st.name);
                        for (int j = 1; j < P->nctl; j++)
                            CHECK(st.ctl[P->ctl0 + j].t >= st.ctl[P->ctl0 + j - 1].t, "%s: controllers in time order", st.name);
                    }
            secs += dt;
            if (dt > worst)
                worst = dt;
            if (bytes > biggest)
                biggest = bytes, big = i;
            notes += st.nnotes;
            ctl += st.nctl;
        }
        printf("bank: %d styles, %ld notes, %ld controller events; decode %.2f ms in all, %.3f ms the slowest; the "
               "largest decoded: %s, %ld bytes (a buffer holds %u); working memory %u + record %u + priors %u bytes\n",
               n, notes, ctl, secs * 1e3, worst * 1e3, qb_name(big), biggest, (unsigned)sizeof(qb_style_t),
               (unsigned)sizeof(qy_work_t), QB_RECMAX, (unsigned)QB_PRIORS);
    }
    if (want == 17) {                             /* the synthetic bank (tools/qybank_synth.py) */
        int dense = -1, pop = -1;
        for (int i = 0; i < n; i++) {
            if (!strcmp(qb_name(i), "Dense"))
                dense = i;
            if (!strcmp(qb_name(i), "Pop"))
                pop = i;
        }
        CHECK(pop == 0 && qb_tempo(pop) == 105, "Pop first, at 105 (%d %d)", pop, qb_tempo(pop));
        CHECK(dense == 16 && qb_tempo(dense) == 120, "Dense last, at 120");
        if (qb_decode(dense, &st) == 0) {
            uint32_t len = (uint32_t)st.bars[1] * st.beats * 480u, tot = 0;
            for (int t = 0; t < 8; t++)
                if (st.use[1][t] != 0xFF) {
                    const qy_phrase_t *P = &st.ph[st.use[1][t]];
                    tot += P->nnotes * (len / P->length);
                    CHECK(P->length < len, "Dense Main A: track %d's phrase shorter than its section", t);
                }
            CHECK(st.bars[1] == 8 && tot > 600, "Dense Main A: 8 bars, %u notes as it plays (> 600)", tot);
        }
        if (qb_decode(pop, &st) == 0) {
            const qy_phrase_t *B = &st.ph[st.use[1][3]];
            CHECK(B->nctl == 5 && st.ctl[B->ctl0].kind == 0 && st.ctl[B->ctl0 + 2].val == 48,
                  "Pop Main A's bass: its bend (%d events)", B->nctl);
            CHECK(st.trk[0].chan == 9 && st.trk[0].drum && st.trk[3].chan == 1 && st.trk[3].prog == 33,
                  "Pop's tracks: drums on 10, the bass on 2 (program 34)");
        }
    }
    {   /* a damaged record: that style refused, the others play */
        uint32_t off = (uint32_t)(img[QB_DIR_HEAD + 8] | img[QB_DIR_HEAD + 9] << 8 | img[QB_DIR_HEAD + 10] << 16);
        load();
        flash[qb_phys(off + 20)] ^= 0x40;
        CHECK(qb_mount() == n, "a damaged record: still mounted");
        CHECK(qb_decode(0, &st) == QB_E_CRC, "... style 0 refused (CRC)");
        CHECK(n < 2 || qb_decode(1, &st) == 0, "... style 1 plays");
        CHECK(qb_decode(n, &st) == QB_E_INDEX, "no style past the last");
    }
    {   /* damaged streams (the CRC bypassed: the decoder alone): never a key past 127, a note outside its phrase, a
         * controller value past 127; a damage it cannot tell is still a well-formed style */
        static qy_phrase_t ph[QB_MAXPH];
        static qy_note_t nt[QB_MAXN];
        static qy_ctl_t cl[QB_MAXC];
        static qy_work_t w;
        static uint8_t buf[QB_RECMAX];
        uint32_t seed = 12345u, bad = 0, ok = 0;
        for (int it = 0; it < 3000; it++) {
            const uint8_t *e = img + QB_DIR_HEAD + QB_DIR_ENTRY * (uint32_t)(it % n);
            uint32_t off = e[8] | e[9] << 8 | (uint32_t)e[10] << 16, hl = e[12] | e[13] << 8, sl = e[14] | e[15] << 8;
            const uint8_t *h = img + QB_SECTOR + off;
            int nph = h[15 + 7 * __builtin_popcount(h[14])];
            memcpy(buf, h + hl, sl);
            for (int f = 0; f < 1 + it % 3; f++) {
                seed = seed * 1103515245u + 12345u;
                buf[(seed >> 8) % sl] ^= (uint8_t)(1u << (seed >> 4 & 7u));
            }
            if (qy_decode_style(buf, sl, img + QB_SECTOR, nph, ph, nt, QB_MAXN, cl, QB_MAXC, &w))
                continue;
            ok++;
            for (int k = 0; k < nph; k++)
                for (int j = 0; j < ph[k].nnotes; j++)
                    bad += nt[ph[k].note0 + j].key > 127 || nt[ph[k].note0 + j].t >= ph[k].length;
            for (int k = 0; k < nph; k++)
                for (int j = 0; j < ph[k].nctl; j++)
                    bad += cl[ph[k].ctl0 + j].val > 127;
        }
        CHECK(!bad, "damaged streams: %u decoded anyway, %u events out of range", ok, bad);
    }
    load();                                       /* a damaged directory: no bank */
    flash[QB_DIR + QB_DIR_HEAD + 30] ^= 1;
    CHECK(qb_mount() == QB_BAD && qb_count() == 0 && !*qb_name(0), "a damaged directory: QB_BAD, no styles");
    load();                                       /* damaged priors */
    flash[qb_phys(3)] ^= 1;
    CHECK(qb_mount() == QB_BAD, "damaged priors: QB_BAD");
    load();                                       /* another version */
    flash[QB_DIR + 4] = 2;
    CHECK(qb_mount() == QB_OLD && qb_count() == 0, "another version: QB_OLD");
    load();
    CHECK(qb_mount() == n, "back");
    printf("%s\n", fails ? "BANK TEST FAILED" : "bank test passed");
    return fails != 0;
}
