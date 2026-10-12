/* SPDX-License-Identifier: GPL-3.0-only */
/* firmware/src/app/teststore.c: a test session's objects in RAM. Saved objects load back byte for byte (random,
 * zeros, runs, every length around the code's run and literal limits); a save replaces the object's old one, the
 * others kept; loads cut at max; an empty object is none; a full pool refuses a save and keeps the old one; the
 * calibration (OBJ_PANEL) and everything with the session off go to the flash; starting or ending drops it all.
 *   cc -std=c99 -Ifirmware/src -o teststore_test tests/host/teststore_test.c && ./teststore_test */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "app/plat.h"

static int flash_loads, flash_saves;
int plat_store_load(uint32_t obj, void *dst, uint32_t max) { (void)obj; (void)dst; (void)max; flash_loads++; return -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { (void)obj; (void)src; (void)len; flash_saves++; return 0; }

#include "app/teststore.c"
static int obj_load(uint32_t obj, void *dst, uint32_t max)
{
    return ts.on && obj != OBJ_PANEL ? ts_load(obj, dst, max) : plat_store_load(obj, dst, max);
}
static int obj_save(uint32_t obj, const void *src, uint32_t len)
{
    return ts.on && obj != OBJ_PANEL ? ts_save(obj, src, len) : plat_store_save(obj, src, len);
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static uint8_t a[PLAT_STORE_MAX], b[PLAT_STORE_MAX + 16];

static int same(uint32_t obj, const uint8_t *src, uint32_t len)
{
    memset(b, 0xA5, sizeof b);
    return obj_load(obj, b, PLAT_STORE_MAX) == (int)len && !memcmp(b, src, len);
}

int main(void)
{
    ts_set(1);
    /* shapes: each length 1..300 and some big ones; random, zeros, runs of 2 / 3 / 130 / 131, mixed */
    for (int shape = 0; shape < 6; shape++)
        for (uint32_t len = 1; len <= PLAT_STORE_MAX; len += len < 300 ? 1u : 97u) {
            for (uint32_t i = 0; i < len; i++)
                a[i] = shape == 0 ? (uint8_t)rnd() : shape == 1 ? 0 : shape == 2 ? (uint8_t)(i / 2) :
                       shape == 3 ? (uint8_t)(i / 3) : shape == 4 ? (uint8_t)(i / 131) :
                       (rnd() & 7u) ? 0 : (uint8_t)rnd();
            CHECK(obj_save(OBJ_PAT0 + 3, a, len) == 0, "shape %d len %u: not saved", shape, len);
            CHECK(same(OBJ_PAT0 + 3, a, len), "shape %d len %u: not the same back", shape, len);
        }
    CHECK(ts_find(OBJ_PAT0 + 3) == 0 && ts.used == 5u + (ts_pool[3] | ts_pool[4] << 8u),
          "saves of one object: more than its latest kept (%u bytes)", ts.used);
    /* several objects, one replaced, the others kept */
    {
        static uint8_t o[4][PLAT_STORE_MAX];
        uint32_t len[4] = {108, 944, 2252, 3760};
        uint32_t obj[4] = {OBJ_PROJ, OBJ_SONGS, OBJ_PAT0 + 1, OBJ_PAT0 + 15};
        ts_set(1);
        for (int k = 0; k < 4; k++) {
            for (uint32_t i = 0; i < len[k]; i++)
                o[k][i] = i < 300 ? (uint8_t)rnd() : 0;
            CHECK(obj_save(obj[k], o[k], len[k]) == 0, "object %d not saved", k);
        }
        o[1][7] ^= 0x55;
        CHECK(obj_save(obj[1], o[1], len[1]) == 0, "object 1 not saved again");
        for (int k = 0; k < 4; k++)
            CHECK(same(obj[k], o[k], len[k]), "object %d not the same back after another's save", k);
        CHECK(obj_load(OBJ_PAT0 + 2, b, sizeof b) == -1, "an object never saved: not none");
        memset(b, 0, sizeof b);
        CHECK(obj_load(obj[3], b, 100) == 100 && !memcmp(b, o[3], 100) && b[100] == 0, "a load not cut at max");
        CHECK(obj_save(OBJ_PAT0 + 4, a, 0) == 0 && obj_load(OBJ_PAT0 + 4, b, sizeof b) == -1, "an empty object: not none");
    }
    /* full: refused, the old one kept */
    {
        uint32_t i, n = 0;
        ts_set(1);
        for (i = 0; i < PLAT_STORE_MAX; i++)
            a[i] = (uint8_t)rnd();                    /* (random: no shorter coded) */
        a[0] = 1;
        CHECK(obj_save(OBJ_PAT0, a, 100) == 0, "a small one not saved");
        while (n < 8 && obj_save(OBJ_PAT0 + 1 + n, a, PLAT_STORE_MAX) == 0)
            n++;
        CHECK(n == TS_POOL / PLAT_STORE_MAX, "%u big random objects fit (%u expected)", n, TS_POOL / PLAT_STORE_MAX);
        CHECK(ts.used <= TS_POOL, "pool overrun: %u", ts.used);
        a[0] = 2;
        CHECK(obj_save(OBJ_PAT0 + 1, a, PLAT_STORE_MAX) == -1 || n == 0, "a save past the pool not refused");
        a[0] = 1;
        CHECK(same(OBJ_PAT0 + 1, a, PLAT_STORE_MAX), "a refused save: the old one not kept");
        CHECK(same(OBJ_PAT0, a, 100), "a refused save: another lost");
    }
    /* the calibration and the session off: the flash */
    {
        int l0 = flash_loads, s0 = flash_saves;
        ts_set(1);
        obj_save(OBJ_PANEL, a, 10);
        obj_load(OBJ_PANEL, b, 10);
        CHECK(flash_saves == s0 + 1 && flash_loads == l0 + 1, "the calibration not the flash's in a session");
        obj_save(OBJ_SONGS, a, 10);
        CHECK(flash_saves == s0 + 1, "a session's save reached the flash");
        ts_set(0);
        CHECK(ts.used == 0, "the session's objects kept past its end");
        obj_save(OBJ_SONGS, a, 10);
        obj_load(OBJ_SONGS, b, 10);
        CHECK(flash_saves == s0 + 2 && flash_loads == l0 + 2, "the session off: not the flash");
        ts_set(1);
        CHECK(obj_load(OBJ_SONGS, b, 10) == -1, "a new session not blank");
    }
    printf(fails ? "teststore: %d FAILED\n" : "teststore: all passed\n", fails);
    return fails != 0;
}
