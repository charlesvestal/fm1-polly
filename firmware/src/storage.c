/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * X0X / POLLY: object map changed (see below).
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) (the SMK-37 map and the store's owner) */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload (from offset 256),
 * then the 32-byte header at offset 0 last. The header is the commit record
 * (magic, object, copy, seq, length, payload CRC, header CRC); on load the
 * valid copy with the newest seq wins (across the counter's wrap), so a torn
 * write leaves the previous copy in charge.
 * From Felucca 1.5.1 (github.com/hugelton/Felucca, src/storage.c): the newest-copy compare that survives the
 * wrap, the object / copy check of the header, an oversize object refused (st_load) instead of cut, the whole
 * header read back after a save. Polly keeps its own magic and map, and reads the first bytes of an object on
 * purpose (a slot's header: st_load_head).
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x30583058u                   /* "X0X0": never mistaken for a Felucca object */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map, POLLY (plat.h OBJ_*): the project at 0xFC000 (Felucca's FL_GLOB), the panel calibration at
 * 0xDC000 (Felucca's user-preset area), the songs at 0x97000 and the 16 user patterns from 0x99000 to
 * 0xB9000, two sectors apiece, inside Felucca's data area 0x97000..0xDFFFF. Nothing above 0xDFFFF:
 * 0xE0000.. is the update loader's staging area (its record at 0xE4F00). The QY70 style bank (qybank.h, written
 * by bank_rx.c, not by this store) has the rest: 0x93000..0x96FFF, 0xB9000..0xDBFFF, 0xDE000..0xDFFFF and
 * 0xE5000..0xFBFFF. The app area ends below
 * 0x93000 (the update loader writes only [0x4000, 0x93000)). The magic is X0X's, so a project is told
 * apart by its own magic (project.c). */
typedef struct {
    uint32_t magic;
    uint16_t type, slot;                             /* the object; the copy it is (0 A, 1 B) */
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

#if BOARD_SMK37
/* SMK-37: the 19 objects (38 sectors) in three windows outside every protected range (fm1_flash_map.h), A/B
 * neighbours, in object order: the project, the panel, the songs and slots 1-6 from 0x9D000 (to 0xAEFFF, the
 * object area every SMK port shares), slots 7-11 from 0xB0000 (FL_DATA, after the owner tag at 0xAF000), slots
 * 12-16 from 0xC2000 (FL_EXT, the stock USRFLASH). Never near BTIF / USRTRIM (0xC0000..0xC1FFF), USR (0xF4000..)
 * or key_mac (0xFF000). */
#define ST_SMK_BASE 0x9D000u
#define ST_SMK_OBJ_HI 0xAF000u                 /* the object area every SMK port shares (0x9D000..0xAEFFF) */
#define ST_SMK_N0 9u                           /* objects there */
#define ST_SMK_B1 0xB0000u
#define ST_SMK_N1 5u
#define ST_SMK_B2 0xC2000u
#define ST_SMK_N2 5u
_Static_assert(ST_SMK_N0 + ST_SMK_N1 + ST_SMK_N2 == OBJ_NOBJ, "SMK store: every object has a place");
_Static_assert(ST_SMK_BASE + 2u * ST_SMK_N0 * ST_SECTOR <= ST_SMK_OBJ_HI, "SMK store: objects past 0xAF000");
_Static_assert(ST_SMK_B1 >= ST_SMK_OBJ_HI + ST_SECTOR && ST_SMK_B1 + 2u * ST_SMK_N1 * ST_SECTOR <= FL_DATA_HI,
               "SMK store: the second window past FL_DATA");
_Static_assert(ST_SMK_B2 >= FL_EXT_LO && ST_SMK_B2 + 2u * ST_SMK_N2 * ST_SECTOR <= FL_EXT_HI,
               "SMK store: the third window past FL_EXT");
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    uint32_t o = obj % OBJ_NOBJ, c = copy & 1u;
    if (o < ST_SMK_N0)
        return ST_SMK_BASE + (o * 2u + c) * ST_SECTOR;
    if (o < ST_SMK_N0 + ST_SMK_N1)
        return ST_SMK_B1 + ((o - ST_SMK_N0) * 2u + c) * ST_SECTOR;
    return ST_SMK_B2 + ((o - ST_SMK_N0 - ST_SMK_N1) * 2u + c) * ST_SECTOR;
}
/* every sector Polly stores in on the SMK-37 (the owner change clears them all) */
static const uint32_t ST_SMK_REGION[3][2] = {{ST_SMK_BASE, ST_SMK_OBJ_HI},
                                             {ST_SMK_B1, ST_SMK_B1 + 2u * ST_SMK_N1 * ST_SECTOR},
                                             {ST_SMK_B2, ST_SMK_B2 + 2u * ST_SMK_N2 * ST_SECTOR}};

/* The owner of the store. Several FM-1 firmwares are ported to the SMK-37 with the same board layer and
 * this same store area, each with its own object formats: a firmware must never parse another's data. The
 * sector after the object area (0xAF000) holds "SMK37DATA" + the firmware family (FELUCCA_DATA_FAMILY,
 * build.py; Polly: "POLLY"). Ours: load as ever. None (erased, data from before the tag) or another
 * family's: clear every window above and take the store (FELUCCA_DATA_UNTAGGED_OK = 0: Polly never trusts
 * untagged data). Calibration, BT pairing and key_mac are outside them: never touched.
 * ST_IDLE() runs between the erases (the device: the watchdog). */
#ifndef FELUCCA_DATA_FAMILY
#define FELUCCA_DATA_FAMILY "POLLY"
#endif
#ifndef FELUCCA_DATA_UNTAGGED_OK
#define FELUCCA_DATA_UNTAGGED_OK 0
#endif
#ifndef ST_IDLE
#define ST_IDLE() ((void)0)
#endif
#define ST_OWNER_OFF ST_SMK_OBJ_HI
_Static_assert(ST_OWNER_OFF + ST_SECTOR <= ST_SMK_B1, "SMK store owner: inside the objects");
static uint8_t st_owner_cleared;                /* (the UI may say so) */
static void st_owner_check(void)
{
    static const char FAM[] = FELUCCA_DATA_FAMILY;
    uint8_t tag[16], want[16];
    uint32_t i, erased = 1, ours = 1, s, r;
    _Static_assert(sizeof FAM >= 2u && sizeof FAM <= 8u, "FELUCCA_DATA_FAMILY: 1..7 characters");
    for (i = 0; i < 16u; i++)
        want[i] = i < 9u ? (uint8_t)"SMK37DATA"[i] : i - 9u < sizeof FAM - 1u ? (uint8_t)FAM[i - 9u] : 0u;
    if (st_read(ST_OWNER_OFF, tag, 16))
        return;
    for (i = 0; i < 16u; i++) {
        erased &= tag[i] == 0xFFu;
        ours &= tag[i] == want[i];
    }
    if (ours)
        return;
    if (!erased || !FELUCCA_DATA_UNTAGGED_OK) {     /* another firmware's store (or untagged): clear it */
        for (r = 0; r < 3u; r++)
            for (s = ST_SMK_REGION[r][0]; s < ST_SMK_REGION[r][1]; s += ST_SECTOR) {
                ST_IDLE();
                st_erase(s);
            }
        st_owner_cleared = 1;
    }
    ST_IDLE();
    st_erase(ST_OWNER_OFF);
    st_prog(ST_OWNER_OFF, want, 16);
}
#else
static uint32_t obj_base(uint32_t obj)
{
    if (obj == OBJ_PANEL)
        return 0xDC000u;
    if (obj == OBJ_PROJ)
        return 0xFC000u;
    if (obj == OBJ_SONGS)
        return 0x97000u;
    return 0x99000u + (obj - OBJ_PAT0) * 2u * ST_SECTOR;
}

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    return obj_base(obj) + copy * ST_SECTOR;
}
#endif

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_NOBJ || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > ST_PAYLOAD_MAX ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the valid one with the newest seq (A on a tie), -1 when
 * neither is valid. Newest: B's seq a step of less than half the counter
 * after A's, so a counter that wraps (0xFFFFFFFF, then 0) still picks the
 * new copy. Headers first, so only the winner's payload is read (it is left
 * in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* the object into dst: head 0, the whole of it (refused, -1, when it is longer than max: never cut short, so a
 * caller that wants all of it cannot take a part for the whole); head 1, its first max bytes (-1 when it is
 * shorter). Returns the bytes loaded, or -1 (none; an empty copy, as a restore of an absent object writes it
 * (backup.c), is none too). The whole payload's CRC is checked either way */
static int st_load_part(uint32_t obj, void *dst, uint32_t max, int head)
{
    uint32_t i;
    st_hdr_t h;
    if (st_current(obj, &h) < 0 || !h.len || (head ? h.len < max : h.len > max))
        return -1;
    if (head)
        h.len = max;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}
static int st_load(uint32_t obj, void *dst, uint32_t max) { return st_load_part(obj, dst, max, 0); }
static int st_load_head(uint32_t obj, void *dst, uint32_t n) { return st_load_part(obj, dst, n, 1); }

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    if (obj >= OBJ_NOBJ || len > ST_PAYLOAD_MAX)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    base = st_sector(obj, cur == 0 ? 1u : 0u);       /* write the other copy */
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED; the whole header as written */
        st_hdr_t chk;
        uint32_t c = cur == 0 ? 1u : 0u;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}
