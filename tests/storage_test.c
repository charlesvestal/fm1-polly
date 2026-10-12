/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */
static uint32_t flip_at = 0xFFFFFFFFu; /* a program covering this byte lands with a bit of it cleared (a bad write) */

static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    if (flip_at - off < n)
        nor[flip_at] &= (uint8_t)(nor[flip_at] - 1u);   /* its lowest set bit cleared */
    return 0;
}
#include "../firmware/src/app/plat.h"     /* POLLY: the object map */
#include "../firmware/src/storage.c"
#include "qybank.h"                          /* the QY70 style bank's flash map */
int qb_flash_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
#define OBJ_PROJECT0 OBJ_PROJ                  /* POLLY has two objects: the project and the panel */
#define OBJ_SETTINGS OBJ_PANEL

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    bad += check("empty flash loads nothing", st_load(OBJ_PROJECT0, got, sizeof got) < 0);
    bad += check("save A", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(OBJ_PROJECT0, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    nor[st_sector(OBJ_PROJECT0, 0) + ST_PAYLOAD_OFF + 10] ^= 0x01;   /* bit rot in one copy */
    nor[st_sector(OBJ_PROJECT0, 1) + ST_PAYLOAD_OFF + 10] ^= 0x01;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(OBJ_PANEL, got, sizeof got) < 0);
    bad += check("settings save/load",
                 st_save(OBJ_SETTINGS, "hello", 5) == 0 && st_load(OBJ_SETTINGS, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    st_save(OBJ_PANEL, a, sizeof a);
    st_save(OBJ_PANEL, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(OBJ_PANEL, &h);
        nor[st_sector(OBJ_PANEL, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(OBJ_PANEL, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(OBJ_PANEL, b, sizeof b) == 0);
    n = st_load(OBJ_PANEL, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    nor[st_sector(OBJ_PANEL, 0) + 8] ^= 0x01;    /* both headers broken */
    nor[st_sector(OBJ_PANEL, 1) + 8] ^= 0x01;
    bad += check("both headers broken -> nothing", st_load(OBJ_PANEL, got, sizeof got) < 0);
    /* Felucca 1.5.1's hardening, on Polly's store */
    {   /* the sequence counter wrapping: 0 after 0xFFFFFFFF is the newer */
        st_hdr_t h;
        uint32_t c;
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_SONGS, a, sizeof a);              /* A: seq 1 */
        st_save(OBJ_SONGS, b, sizeof b);              /* B: seq 2 */
        for (c = 0; c < 2; c++) {                     /* A 0xFFFFFFFF, B 0 (headers rewritten in place) */
            memcpy(&h, nor + st_sector(OBJ_SONGS, c), sizeof h);
            h.seq = c ? 0u : 0xFFFFFFFFu;
            h.hcrc = st_crc32(&h, sizeof h - 4u);
            memcpy(nor + st_sector(OBJ_SONGS, c), &h, sizeof h);
        }
        n = st_load(OBJ_SONGS, got, sizeof got);
        bad += check("seq wrap: 0 after 0xFFFFFFFF wins", n == (int)sizeof b && !memcmp(got, b, sizeof b));
        bad += check("seq wrap: the next save goes over the older",
                     st_save(OBJ_SONGS, a, sizeof a) == 0 && st_current(OBJ_SONGS, &h) == 0 && h.seq == 1u &&
                     st_load(OBJ_SONGS, got, sizeof got) == (int)sizeof a && !memcmp(got, a, sizeof a));
    }
    {   /* a header in the wrong place: another object's, or the other copy's, is no copy of this one */
        st_hdr_t h;
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_PAT0, a, sizeof a);               /* pattern 0, copy A */
        memcpy(nor + st_sector(OBJ_PAT0 + 1, 0), nor + st_sector(OBJ_PAT0, 0), 4096);
        bad += check("another object's sector copied in: not loaded", st_load(OBJ_PAT0 + 1, got, sizeof got) < 0);
        memcpy(nor + st_sector(OBJ_PAT0, 1), nor + st_sector(OBJ_PAT0, 0), 4096);
        memcpy(&h, nor + st_sector(OBJ_PAT0, 1), sizeof h);
        h.seq = 9;                                    /* newer, but it says it is copy A */
        h.hcrc = st_crc32(&h, sizeof h - 4u);
        memcpy(nor + st_sector(OBJ_PAT0, 1), &h, sizeof h);
        bad += check("copy A's header in copy B's sector: ignored", st_current(OBJ_PAT0, &h) == 0 && h.seq == 1u);
        bad += check("objects past the map refused", st_save(OBJ_NOBJ, a, 10) < 0 && st_load(OBJ_NOBJ, got, sizeof got) < 0);
    }
    {   /* an object longer than the buffer: refused, not cut; a head read takes its first bytes */
        memset(nor, 0xFF, sizeof nor);
        memset(got, 0, sizeof got);
        st_save(OBJ_PROJ, a, sizeof a);
        bad += check("oversize load refused (nothing copied)", st_load(OBJ_PROJ, got, sizeof a - 1u) == -1 && got[0] == 0);
        bad += check("head read: the first 100 bytes", st_load_head(OBJ_PROJ, got, 100) == 100 && !memcmp(got, a, 100) &&
                                                        got[100] == 0);
        bad += check("head read longer than the object refused", st_load_head(OBJ_PROJ, got, sizeof a + 1u) == -1);
        bad += check("save over the payload limit refused", st_save(OBJ_PROJ, nor, ST_PAYLOAD_MAX + 1u) < 0);
    }
    {   /* a save whose header reads back wrong (a bad write) is not reported saved; the old copy stays */
        st_hdr_t h;
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_PANEL, a, sizeof a);              /* copy A, seq 1: the next save goes to B */
        flip_at = st_sector(OBJ_PANEL, 1) + 12u;      /* the new header's length, one bit */
        n = st_save(OBJ_PANEL, b, sizeof b);
        flip_at = 0xFFFFFFFFu;
        bad += check("a header written wrong: the save fails", n == -7);
        bad += check("... and the old copy loads", st_current(OBJ_PANEL, &h) == 0 &&
                     st_load(OBJ_PANEL, got, sizeof got) == (int)sizeof a && !memcmp(got, a, sizeof a));
        flip_at = st_sector(OBJ_PANEL, 1) + ST_PAYLOAD_OFF + 5u;   /* a payload byte */
        n = st_save(OBJ_PANEL, b, sizeof b);
        flip_at = 0xFFFFFFFFu;
        bad += check("a payload byte written wrong: the save fails", n == -7 &&
                     st_load(OBJ_PANEL, got, sizeof got) == (int)sizeof a && !memcmp(got, a, sizeof a));
    }
    {   /* POLLY map: every object inside 0x97000..0xDFFFF or at 0xFC000, two sectors, no overlaps */
        uint32_t o, p, ok = 1;
        for (o = 0; o < OBJ_NOBJ; o++) {
            uint32_t a0 = st_sector(o, 0), a1 = st_sector(o, 1) + 4096;
            int in_data = (a0 >= 0x97000 && a1 <= 0xE0000) || (a0 >= 0xFC000 && a1 <= 0xFF000);
            if (!in_data || a1 - a0 != 8192)
                ok = 0;
            for (p = 0; p < o; p++)
                if (a0 < st_sector(p, 1) + 4096 && st_sector(p, 0) < a1)
                    ok = 0;
        }
        bad += check("objects inside Felucca's data area, no overlaps", (int)ok);
        /* the 16 user patterns: 0x99000.. 0xB9000, clear of the panel (0xDC000), the update loader's staging
         * area and record (0xE0000..0xE4FFF) and the app area (< 0x93000) */
        ok = OBJ_NOBJ == OBJ_PAT0 + 16 && st_sector(OBJ_PAT0, 0) == 0x99000u &&
             st_sector(OBJ_PAT0 + 15, 1) + 4096 == 0xB9000u && st_sector(OBJ_SONGS, 0) >= 0x93000u;
        for (o = 0; o < OBJ_NOBJ; o++)
            if (o != OBJ_PROJ && (st_sector(o, 0) < 0x97000u || st_sector(o, 1) + 4096 > 0xE0000u))
                ok = 0;
        bad += check("16 patterns at 0x99000..0xB9000, below the panel", (int)ok);
        for (o = 0; o < 16; o++) {                 /* each one its own: a save into pattern 15 keeps pattern 0 */
            char x[64];
            memset(x, 'a' + (int)o % 26, sizeof x);
            st_save(OBJ_PAT0 + o, x, sizeof x);
        }
        n = st_load(OBJ_PAT0, got, sizeof got);
        ok = n == 64 && got[0] == 'a';
        n = st_load(OBJ_PAT0 + 15, got, sizeof got);
        bad += check("each pattern object its own", (int)ok && n == 64 && got[0] == 'p');
    }
    {   /* the QY70 bank: its sectors in its four regions, none of the store's, the app's (< 0x93000), the songs',
         * the patterns', the panel's, the update loader's (0xE0000..0xE4FFF) or Felucca's globals (0xFC000..) */
        static uint8_t seen[0x100000 / 4096], hit[0x100000];
        uint32_t i, ok = QB_NSECT == 63 && QB_CAPACITY == 63u * 4088u && QB_FLASH_OK(QB_DIR, 4096);
        for (i = 0; i < QB_NSECT; i++) {
            uint32_t a = qb_sector(i);
            if (!a || a & 0xFFFu || !QB_FLASH_OK(a, 4096) || a == QB_DIR || seen[a >> 12]++)
                ok = 0;
            if ((a >= 0x97000u && a < 0xB9000u) || (a >= 0xDC000u && a < 0xDE000u) || (a >= 0xE0000u && a < 0xE5000u) ||
                a < 0x93000u || a >= 0xFC000u)
                ok = 0;
        }
        for (uint32_t o = 0; o < OBJ_NOBJ; o++)
            for (uint32_t c = 0; c < 2; c++)
                if (QB_FLASH_OK(st_sector(o, c), 4096) || (st_sector(o, c) + 4096 > QB_R0_LO && st_sector(o, c) < QB_R0_HI))
                    ok = 0;
        bad += check("bank: 63 data sectors + its directory, clear of the rest", (int)ok);
        /* every logical byte its own flash byte, never one of a sector's guard bytes (0xF00..0xF07) */
        memset(nor, 0xFF, sizeof nor);
        ok = qb_phys(QB_CAPACITY) == 0;
        for (i = 0; i < QB_CAPACITY; i++) {
            uint32_t a = qb_phys(i);
            if (!QB_FLASH_OK(a, 1) || ((a & 0xFFFu) >= 0xF00u && (a & 0xFFFu) < 0xF08u) || hit[a]++)
                ok = 0;
            nor[a] = 0;
        }
        for (i = 0; i < QB_NSECT; i++)               /* written full of zeros: the guard bytes still 0xFF */
            for (uint32_t g = 0; g < 8; g++)
                ok &= nor[qb_sector(i) + 0xF00u + g] == 0xFFu && nor[qb_sector(i) + 0xEFFu] == 0 &&
                      nor[qb_sector(i) + 0xF08u] == 0;
        bad += check("bank: the logical bytes step over the guard bytes", (int)ok);
    }
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    return bad != 0;
}
