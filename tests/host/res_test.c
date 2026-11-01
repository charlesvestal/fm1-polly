/* SPDX-License-Identifier: GPL-3.0-only */
/* The factory styles from the resource region (tools/gen_res.py) against the tables compiled in: arr.c built both ways
 * (STY_IN_RES 1: read from the region's STY1 section, found as app.c finds it; 0: styles.h's tables), every factory
 * pattern expanded into OUT; tests/run_tests.sh compares the two. With STY_IN_RES 1 also: the region absent, cut short
 * or another build's (its counts): no factory pattern (-1), arr_styles_there 0.
 *   res_test RES.bin OUT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arr.h"
#ifndef STY_IN_RES
#define STY_IN_RES 0
#endif

void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
uint32_t arr_midi_out_room(void) { return 64; }
uint32_t arr_midi_out_link(void) { return 0; }

static uint8_t res[64 * 1024];
static uint32_t res_n;
#if STY_IN_RES
static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static const uint8_t *styles(uint32_t *len)       /* app.c's res_section(STY1), on the file */
{
    uint32_t cnt;
    *len = 0;
    if (res_n < 8 || rd32(res) != 0x31535250u)
        return 0;
    cnt = res[4] | res[5] << 8;
    for (uint32_t i = 0; i < cnt && 8 + 12 * i + 12 <= res_n; i++) {
        const uint8_t *e = res + 8 + 12 * i;
        if (rd32(e) == 0x31595453u && rd32(e + 4) <= res_n && rd32(e + 8) <= res_n - rd32(e + 4)) {
            *len = rd32(e + 8);
            return res + rd32(e + 4);
        }
    }
    return 0;
}
#endif

int main(int argc, char **argv)
{
    static pattern_t p;
    FILE *f;
    int bad = 0;
    if (argc != 3 || !(f = fopen(argv[1], "rb")))
        return 2;
    res_n = (uint32_t)fread(res, 1, sizeof res, f);
    fclose(f);
#if STY_IN_RES
    arr_sty_res = styles;
#endif
    if (!(f = fopen(argv[2], "wb")))
        return 2;
    for (int n = ARR_FACTORY0; n < ARR_FACTORY0 + ARR_NSECT * arr_styles(); n++) {
        memset(&p, 0, sizeof p);
        bad += arr_factory_pattern(n, &p) != 0;
        fwrite(&p, sizeof p, 1, f);
    }
    fclose(f);
#if STY_IN_RES
    {
        uint32_t keep = res_n;
        bad += !arr_styles_there();
        res_n = 0;                                  /* absent */
        bad += arr_styles_there() || arr_factory_pattern(ARR_FACTORY0, &p) != -1;
        res_n = keep - 2;                           /* cut short (past its pad byte) */
        bad += arr_styles_there() != 0;
        res_n = keep;
        res[20] ^= 1;                               /* another build's: its styles' count */
        bad += arr_styles_there() || arr_factory_pattern(ARR_FACTORY0 + 7, &p) != -1;
        res[20] ^= 1;
        bad += !arr_styles_there();
    }
#endif
    printf("%s: %d factory patterns%s\n", bad ? "FAIL" : "ok", ARR_NSECT * arr_styles(),
           STY_IN_RES ? " from the region; absent, short, another build's: none" : " compiled in");
    return bad != 0;
}
