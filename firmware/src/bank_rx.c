/* SPDX-License-Identifier: GPL-3.0-only */
/* The QY70 style bank written from the host (tools/qy70_install.py), over the update's SysEx protocol (ota.c): the
 * host asks with cmd 0x40 (body: the image's bytes and its CRC-32, both u32 little-endian); then, as an update,
 * the device asks for the image piece by piece (cmd 0x30 read requests, 512 bytes at most, each frame
 * checksummed) and writes it. The image (tools/qy70_codec.py device_image): the directory sector (4096 bytes),
 * then the data's logical bytes (qybank.h).
 *
 * Safe against a power loss or a cut cable: the old directory is erased first and the new one written last,
 * after every data byte is written and read back (a CRC-32 of the data as received and as read from the flash,
 * and of the whole image against the host's), so a bank cut off has no directory: no bank (the styles hidden,
 * the songs' bank sections silent) until it is written again. Only the bank's sectors are erased or written
 * (bk_erase / bk_prog refuse the rest); all 63 data sectors are erased, the ones the bank does not fill too. At the end the device tells the host: a read of 0xB0000000 (written,
 * mounted) or 0xBF00xxxx (failed: xxxx the code, BK_E_*).
 *
 * Hooks (main.c on the device, tests/bank_rx_test.c on the host):
 *   bk_fetch(addr, p, n)   the image's bytes from the host (ota_read)       0 = ok
 *   bk_erase(off) bk_prog(off, p, n) bk_read(off, p, n)   the flash            0 = ok
 *   bk_show(pct)           progress, 0..100
 *   bk_done(code)          the result to the host */
#define BK_REQ 0x40u                                  /* the host's request (ota.c framing) */
#define BK_OK_ADDR 0xB0000000u
#define BK_FAIL_ADDR 0xBF000000u
enum { BK_E_SIZE = 1, BK_E_FETCH, BK_E_DIR, BK_E_ERASE, BK_E_PROG, BK_E_CRC, BK_E_VERIFY, BK_E_MOUNT };

static int bk_fetch(uint32_t addr, uint8_t *dst, uint32_t n);
static int bk_erase(uint32_t off);
static int bk_prog(uint32_t off, const void *src, uint32_t n);
static int bk_read(uint32_t off, void *dst, uint32_t n);
static void bk_show(uint32_t pct);
static void bk_done(int code);

static uint8_t bk_dir[QB_SECTOR] __attribute__((aligned(4))) EN_POOL;
static uint8_t bk_buf[512] __attribute__((aligned(4)));

static uint32_t bk_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t bk_rd32(const uint8_t *p) { return bk_rd16(p) | bk_rd16(p + 2) << 16; }

/* the directory (as qybank.c checks it at mount): its styles, *used the data's bytes; 0 if not a bank this
 * firmware can play */
static uint32_t bk_dir_ok(uint32_t len, uint32_t *used)
{
    uint32_t n = bk_rd16(bk_dir + 6);
    *used = bk_rd32(bk_dir + 12);
    if (bk_rd32(bk_dir) != QB_MAGIC || bk_rd16(bk_dir + 4) != QB_VERSION || bk_rd32(bk_dir + 8) != QB_CODEC ||
        !n || n > QB_MAXSTYLES || *used > QB_CAPACITY || *used + QB_SECTOR != len ||
        bk_rd16(bk_dir + 20) != QB_PRIORS ||
        qb_crc32(0, bk_dir, QB_DIR_BYTES(n) - 4u) != bk_rd32(bk_dir + QB_DIR_BYTES(n) - 4u))
        return 0;
    for (uint32_t i = QB_DIR_BYTES(n); i < QB_SECTOR; i++)
        if (bk_dir[i] != 0xFFu)                       /* the rest erased: the guard bytes stay 0xFF */
            return 0;
    return n;
}

/* the bank, written; returns 0 or BK_E_* (the host is told either way) */
static int bank_rx(uint32_t len, uint32_t crc)
{
    uint32_t n, used, i, l, c_img = 0, c_rx = 0, c_fl = 0, nsect;
    int rc = 0;
    if (len <= QB_SECTOR || len > QB_SECTOR + QB_CAPACITY) {
        rc = BK_E_SIZE;
        goto out;
    }
    bk_show(0);
    for (i = 0; i < QB_SECTOR; i += sizeof bk_buf)   /* the directory first: checked before anything is erased */
        if (bk_fetch(i, bk_dir + i, sizeof bk_buf)) {
            rc = BK_E_FETCH;
            goto out;
        }
    if (!(n = bk_dir_ok(len, &used))) {
        rc = BK_E_DIR;
        goto out;
    }
    c_img = qb_crc32(0, bk_dir, QB_SECTOR);
    if (bk_erase(QB_DIR)) {                           /* the old bank gone: from here, none until the end */
        rc = BK_E_ERASE;
        goto out;
    }
    nsect = (used + QB_SECT_DATA - 1u) / QB_SECT_DATA;
    for (i = 0; i < nsect; i++) {
        if (bk_erase(qb_sector(i))) {
            rc = BK_E_ERASE;
            goto out;
        }
        for (l = i * QB_SECT_DATA; l < used && l < (i + 1u) * QB_SECT_DATA;) {
            uint32_t o = l % QB_SECT_DATA, k = (o < QB_GUARD ? QB_GUARD : QB_SECT_DATA) - o;   /* to the guard, or */
            if (k > sizeof bk_buf)                                                            /* the sector's end */
                k = sizeof bk_buf;
            if (k > used - l)
                k = used - l;
            if (bk_fetch(QB_SECTOR + l, bk_buf, k)) {
                rc = BK_E_FETCH;
                goto out;
            }
            c_img = qb_crc32(c_img, bk_buf, k);
            c_rx = qb_crc32(c_rx, bk_buf, k);
            if (bk_prog(qb_phys(l), bk_buf, k)) {
                rc = BK_E_PROG;
                goto out;
            }
            l += k;
        }
        bk_show(i * 90u / nsect);
    }
    for (i = nsect; i < QB_NSECT; i++)              /* the rest of the bank's flash: erased (no stale bytes) */
        if (bk_erase(qb_sector(i))) {
            rc = BK_E_ERASE;
            goto out;
        }
    if (c_img != crc) {                               /* the image, as the host has it */
        rc = BK_E_CRC;
        goto out;
    }
    for (l = 0; l < used;) {                          /* read back */
        uint32_t o = l % QB_SECT_DATA, k = (o < QB_GUARD ? QB_GUARD : QB_SECT_DATA) - o;
        if (k > sizeof bk_buf)
            k = sizeof bk_buf;
        if (k > used - l)
            k = used - l;
        if (bk_read(qb_phys(l), bk_buf, k)) {
            rc = BK_E_VERIFY;
            goto out;
        }
        c_fl = qb_crc32(c_fl, bk_buf, k);
        l += k;
    }
    if (c_fl != c_rx) {
        rc = BK_E_VERIFY;
        goto out;
    }
    bk_show(95);
    for (i = 0; i < QB_DIR_BYTES(n); i += 256u) {    /* the directory, last */
        uint32_t k = QB_DIR_BYTES(n) - i < 256u ? QB_DIR_BYTES(n) - i : 256u;
        if (bk_prog(QB_DIR + i, bk_dir + i, k)) {
            rc = BK_E_PROG;
            goto out;
        }
    }
    if (qb_mount() != (int)n) {
        rc = BK_E_MOUNT;
        goto out;
    }
    bk_show(100);
out:
    bk_done(rc);
    return rc;
}
