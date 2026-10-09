/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the QY70 bank's writer (firmware/src/bank_rx.c) against a simulated NOR flash (erase -> 0xFF, a
 * program only clears bits, a page never wraps) and a host serving a bank image (tools/qy70_codec.py --image):
 * written, mounted, every style decoded; nothing outside the bank's sectors touched, every sector's guard bytes
 * 0xFF; the cable cut midway, a byte changed in transit, a damaged directory, a failed program: no half bank is
 * ever mounted (the old one kept when nothing was erased yet).
 *   bank_rx_test IMAGE */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qybank.h"
#define EN_POOL

static uint8_t nor[0x100000], before[0x100000];
static uint8_t *img;
static uint32_t img_n;
static int fetch_left = -1, prog_fail = -1, flip_at = -1, done_code = -99, fails, prog_count;
static uint32_t last_prog;
static uint32_t fetches;

int qb_flash_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int bk_fetch(uint32_t addr, uint8_t *dst, uint32_t n)
{
    fetches++;
    if (fetch_left == 0)
        return -1;                                /* the cable cut */
    if (fetch_left > 0)
        fetch_left--;
    if (addr > img_n || n > img_n - addr) {
        memset(dst, 0, n);                        /* past the image (the directory's last piece): zeros */
        if (addr < img_n)
            memcpy(dst, img + addr, img_n - addr);
    } else
        memcpy(dst, img + addr, n);
    if (flip_at >= 0 && (uint32_t)flip_at >= addr && (uint32_t)flip_at < addr + n)
        dst[flip_at - (int)addr] ^= 0x20;         /* a byte changed in transit */
    return 0;
}
static int bk_erase(uint32_t off)
{
    if (!QB_FLASH_OK(off, QB_SECTOR) || (off & 0xFFFu)) {
        printf("erase outside the bank: %#x\n", off);
        exit(1);
    }
    memset(nor + off, 0xFF, QB_SECTOR);
    return 0;
}
static int bk_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    if (!QB_FLASH_OK(off, n)) {
        printf("program outside the bank: %#x+%u\n", off, n);
        exit(1);
    }
    if (prog_fail == 0)
        return -9;
    prog_count++;
    last_prog = off;
    if (prog_fail > 0)
        prog_fail--;
    for (uint32_t i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
static int bk_read(uint32_t off, void *dst, uint32_t n) { return qb_flash_read(off, dst, n); }
static int begins;
static void bk_begin(void) { begins++; }
static void bk_show(uint32_t pct) { (void)pct; }
static void bk_done(int code, int begun) { done_code = code; (void)begun; }
#include "../firmware/src/bank_rx.c"

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
    return ok;
}
static uint32_t crc_img(void) { return qb_crc32(0, img, img_n); }
static int outside_same(void)                     /* every byte outside the bank's sectors as before */
{
    for (uint32_t a = 0; a < sizeof nor; a++)
        if (!QB_FLASH_OK(a, 1) && nor[a] != before[a])
            return 0;
    return 1;
}
static int guards_ff(void)
{
    for (uint32_t i = 0; i < QB_NSECT; i++)
        for (uint32_t g = 0; g < QB_GUARD_N; g++)
            if (nor[qb_sector(i) + QB_GUARD + g] != 0xFFu)
                return 0;
    for (uint32_t g = 0; g < QB_GUARD_N; g++)
        if (nor[QB_DIR + QB_GUARD + g] != 0xFFu)
            return 0;
    return 1;
}
static qb_style_t st;

int main(int argc, char **argv)
{
    FILE *f;
    int n, all = 1, rc;
    if (argc < 2 || !(f = fopen(argv[1], "rb")))
        return 2;
    fseek(f, 0, SEEK_END);
    img_n = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    img = malloc(img_n);
    if (!img || fread(img, 1, img_n, f) != img_n)
        return 2;
    fclose(f);
    for (uint32_t a = 0; a < sizeof nor; a++)     /* the rest of the flash: something not 0xFF */
        nor[a] = (uint8_t)(a * 7u + 3u);
    memcpy(before, nor, sizeof nor);

    rc = bank_rx(img_n, crc_img(), 0);
    n = qb_mount();
    check("written: 0, the host told", rc == 0 && done_code == 0);
    check("mounted", n > 0);
    for (int i = 0; i < n; i++)
        all &= qb_decode(i, &st) == 0;
    check("every style decodes", all);
    check("nothing outside the bank's sectors touched", outside_same());
    check("every bank sector's guard bytes 0xFF", guards_ff());
    printf("  %u requests to the host for %u bytes\n", fetches, img_n);

    rc = bank_rx(img_n, crc_img(), 0);               /* again, over the bank */
    check("written again over itself", rc == 0 && qb_mount() == n);

    fetch_left = 30;                              /* the cable cut after 30 pieces: no bank, not half a one */
    rc = bank_rx(img_n, crc_img(), 0);
    fetch_left = -1;
    check("cut midway: BK_E_FETCH, the host told", rc == BK_E_FETCH && done_code == BK_E_FETCH);
    check("... no bank mounted (QB_NONE)", qb_mount() == QB_NONE && qb_count() == 0);
    check("... the guard bytes still 0xFF", guards_ff());
    rc = bank_rx(img_n, crc_img(), 0);
    check("then written whole", rc == 0 && qb_mount() == n);

    flip_at = (int)img_n - 100;                   /* a byte changed in transit (the frame's checksum missed it) */
    rc = bank_rx(img_n, crc_img(), 0);
    flip_at = -1;
    check("a byte changed in transit: BK_E_CRC, no bank", rc == BK_E_CRC && qb_mount() == QB_NONE);
    bank_rx(img_n, crc_img(), 0);

    img[40] ^= 1;                                 /* a damaged directory: refused before anything is erased */
    rc = bank_rx(img_n, qb_crc32(0, img, img_n), 0);
    img[40] ^= 1;
    check("a damaged directory: BK_E_DIR, the old bank kept", rc == BK_E_DIR && qb_mount() == n);
    rc = bank_rx(img_n + 4, crc_img(), 0);
    check("the wrong length: refused, the old bank kept", rc == BK_E_DIR && qb_mount() == n);
    rc = bank_rx(100, 0, 0);
    check("too short: BK_E_SIZE", rc == BK_E_SIZE && qb_mount() == n);
    {   /* a style entry pointing past the data (its directory's CRC made right): refused before anything is erased,
         * and nothing stopped */
        uint32_t nn = img[6] | img[7] << 8, dl = 32u + 24u * nn, c;
        int b0 = begins, began = -1;
        uint8_t save[4];
        memcpy(save, img + 32 + 24 * (nn - 1) + 8, 4);
        img[32 + 24 * (nn - 1) + 10] = 0x7F;      /* its offset: far past the data */
        c = qb_crc32(0, img, dl);
        memcpy(img + dl, &c, 4);
        rc = bank_rx(img_n, qb_crc32(0, img, img_n), &began);
        memcpy(img + 32 + 24 * (nn - 1) + 8, save, 4);
        c = qb_crc32(0, img, dl);
        memcpy(img + dl, &c, 4);
        check("a style past the data: BK_E_DIR, the old bank kept", rc == BK_E_DIR && qb_mount() == n);
        check("... and nothing stopped (no begin)", begins == b0 && began == 0);
    }

    {   /* the directory's first 256 bytes (its magic) written last: a cut before them leaves no bank */
        static int total;
        prog_count = 0;
        bank_rx(img_n, crc_img(), 0);
        total = prog_count;
        check("the last program of a write: the directory's start", last_prog == QB_DIR);
        prog_fail = total - 1;
        rc = bank_rx(img_n, crc_img(), 0);
        prog_fail = -1;
        check("cut before the directory's first 256 bytes: no bank", rc == BK_E_PROG && qb_mount() == QB_NONE);
        bank_rx(img_n, crc_img(), 0);
    }
    prog_fail = 10;                               /* the flash fails */
    rc = bank_rx(img_n, crc_img(), 0);
    prog_fail = -1;
    check("a program fails: BK_E_PROG, no bank", rc == BK_E_PROG && qb_mount() == QB_NONE);
    check("nothing outside the bank's sectors touched, through all of it", outside_same());
    printf("%s\n", fails ? "BANK RX TEST FAILED" : "bank rx test passed");
    return fails != 0;
}
