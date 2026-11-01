/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's flash map: every store object (app.h APP_STORE_MAP, two 4 KiB sectors each) where Polly 0.4 and 0.5 kept
 * them (the pool's move finds them there), the pooled ones' pairs inside the sectors the pool moves from
 * (APP_STORE_POOL_BASE, _OLD); then the layout now: the objects not pooled (two sectors each), the pool's sectors, the
 * resource region's (app.h APP_RES_BASE, APP_RES_SECTORS: after the pool, inside what it frees) and every sector of
 * the QY70 bank (dsp/qybank.h) inside fm1-base's data windows (plat.h PLAT_DATA_OK) or the settings pair at 0xFC000,
 * none of them overlapping another, the update's staging (0xE0000..0xE4FFF) untouched.
 *   cc -std=c99 -Iapp -Iapp/dsp -Ibase/include -o map_test tests/host/map_test.c app/dsp/qybank.c app/dsp/qydec.c */
#include <stdio.h>
#include <string.h>
#include "plat.h"
#include "qybank.h"
enum { OBJ_PROJ, OBJ_PANEL, OBJ_SONGS, OBJ_PAT0, OBJ_NOBJ = OBJ_PAT0 + 16 };
#include "map_defs.h"                              /* app.h's APP_STORE_POOL_* and APP_RES_* lines (tests/run_tests.sh) */
/* the map as app.h has it (taken from app.h by tests/run_tests.sh: app.h itself pulls in the whole app) */
static const uint32_t map[] =
#include "map_line.h"
;
int qb_flash_read(uint32_t off, void *dst, uint32_t n) { (void)off, (void)dst, (void)n; return -1; }

static uint8_t owner[0x100000 / 4096];             /* who has each sector: 0 none, 1.. an object, 100 the bank */
static int fails;

static void take(uint32_t off, int who, const char *what)
{
    uint32_t s = off / 4096u;
    if (off & 0xFFFu) {
        printf("FAIL %s at %#x: not on a sector\n", what, off);
        fails++;
    }
    if (!(PLAT_DATA_OK(off, 4096u) || (off >= 0xFC000u && off + 4096u <= 0xFE000u))) {
        printf("FAIL %s at %#x: outside fm1-base's windows\n", what, off);
        fails++;
    }
    if (off >= 0xE0000u && off < 0xE5000u) {
        printf("FAIL %s at %#x: the update's staging\n", what, off);
        fails++;
    }
    if (owner[s]) {
        printf("FAIL %s at %#x: already %d's\n", what, off, owner[s]);
        fails++;
    }
    owner[s] = (uint8_t)who;
}

int main(void)
{
    uint32_t i;
    if (sizeof map / sizeof map[0] != OBJ_NOBJ) {
        printf("FAIL the map has %u objects, not %d\n", (unsigned)(sizeof map / sizeof map[0]), OBJ_NOBJ);
        return 1;
    }
    if (map[OBJ_PROJ] != 0xFC000u || map[OBJ_PANEL] != 0xDC000u || map[OBJ_SONGS] != 0x97000u) {
        printf("FAIL the project, panel or songs moved from where Polly 0.4 kept them\n");
        fails++;
    }
    for (i = 0; i < 16u; i++)
        if (map[OBJ_PAT0 + i] != 0x99000u + i * 0x2000u) {
            printf("FAIL slot %u moved from %#x\n", i + 1u, 0x99000u + i * 0x2000u);
            fails++;
        }
    for (i = APP_STORE_POOL_OBJ0; i < APP_STORE_POOL_OBJ0 + APP_STORE_POOL_OBJN; i++)
        if (map[i] < APP_STORE_POOL_BASE || map[i] + 8192u > APP_STORE_POOL_BASE + APP_STORE_POOL_OLD * 4096u) {
            printf("FAIL object %u's old pair at %#x: not where the pool moves from\n", i, map[i]);
            fails++;
        }
    for (i = 0; i < OBJ_NOBJ; i++) {
        if (i >= APP_STORE_POOL_OBJ0 && i < APP_STORE_POOL_OBJ0 + APP_STORE_POOL_OBJN)
            continue;                              /* (pooled: the map's pair is where it was, moved at boot) */
        take(map[i], (int)i + 1, "an object's copy A");
        take(map[i] + 4096u, (int)i + 1, "an object's copy B");
    }
    for (i = 0; i < APP_STORE_POOL_N; i++)
        take(APP_STORE_POOL_BASE + i * 4096u, 90, "a pool sector");
#ifndef APP_RES_BASE
#define APP_RES_BASE (APP_STORE_POOL_BASE + APP_STORE_POOL_N * 4096u)
#define APP_RES_SECTORS 0u
#endif
    if (APP_RES_BASE < APP_STORE_POOL_BASE + APP_STORE_POOL_N * 4096u ||
        APP_RES_BASE + APP_RES_SECTORS * 4096u > APP_STORE_POOL_BASE + APP_STORE_POOL_OLD * 4096u) {
        printf("FAIL the resource region is not in the sectors the pool frees\n");
        fails++;
    }
    for (i = 0; i < APP_RES_SECTORS; i++)
        take(APP_RES_BASE + i * 4096u, 91, "a resource sector");
    take(QB_DIR, 100, "the bank's directory");
    for (i = 0; i < QB_NSECT; i++)
        take(qb_sector(i), 100, "a bank sector");
    printf("%s: %d objects (%u pooled in %u sectors), the resource region's %u sectors, the bank's %u sectors + its "
           "directory\n", fails ? "FAIL" : "ok", OBJ_NOBJ, (unsigned)APP_STORE_POOL_OBJN, (unsigned)APP_STORE_POOL_N,
           (unsigned)APP_RES_SECTORS, (unsigned)QB_NSECT);
    return fails != 0;
}
