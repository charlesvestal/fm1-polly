/* SPDX-License-Identifier: GPL-3.0-only */
/* The QY70 style bank: the directory read at mount, a style's record read and decoded into RAM when it is picked
 * (qybank.h). The main loop's only: none of this runs in the render. Flash through qb_flash_read (the platform's:
 * 256-byte reads with interrupts off on the device, so the audio keeps up). */
#include <string.h>
#include "qybank.h"

/* big buffers in the device's .pool RAM (zeroed at boot; not the 96 KB of .data/.bss) */
#if defined(__ELF__) && !defined(PLAT_HOST)
#define QB_POOL __attribute__((section(".pool")))
#else
#define QB_POOL
#endif

static int status = QB_NONE, count;
static struct {
    char name[9];
    uint8_t beats;
    uint16_t tempo10, hlen, slen;
    uint32_t off, crc;
} ent[QB_MAXSTYLES] QB_POOL;
static uint8_t priors[QB_PRIORS] QB_POOL;
static uint8_t rec[QB_RECMAX] QB_POOL;            /* the directory at mount; a style's record at decode */
static qy_work_t work QB_POOL;

uint32_t qb_crc32(uint32_t c, const void *p, uint32_t n)   /* zlib CRC-32, 4 bits a step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    c = ~c;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

uint32_t qb_sector(uint32_t i)
{
    static const uint32_t R[4][2] = {{QB_R0_LO, QB_R0_HI}, {QB_R1_LO, QB_R1_HI}, {QB_R2_LO, QB_R2_HI}, {QB_R3_LO, QB_R3_HI}};
    for (int r = 0; r < 4; r++) {
        uint32_t lo = R[r][0] == QB_DIR ? QB_DIR + QB_SECTOR : R[r][0], n = (R[r][1] - lo) / QB_SECTOR;
        if (i < n)
            return lo + i * QB_SECTOR;
        i -= n;
    }
    return 0;
}
uint32_t qb_phys(uint32_t l)
{
    uint32_t s = l / QB_SECT_DATA, o = l % QB_SECT_DATA;
    if (l >= QB_CAPACITY)
        return 0;
    return qb_sector(s) + (o < QB_GUARD ? o : o + QB_GUARD_N);
}
/* n logical bytes from l, stepping over the guard bytes and from sector to sector */
static int read_logical(uint32_t l, uint8_t *dst, uint32_t n)
{
    while (n) {
        uint32_t o = l % QB_SECT_DATA, k = (o < QB_GUARD ? QB_GUARD : QB_SECT_DATA) - o;
        if (k > n)
            k = n;
        if (l >= QB_CAPACITY || qb_flash_read(qb_phys(l), dst, k))
            return -1;
        l += k;
        dst += k;
        n -= k;
    }
    return 0;
}

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

int qb_mount(void)
{
    uint32_t n, used, poff, plen;
    int i;
    count = 0;
    status = QB_NONE;
    if (qb_flash_read(QB_DIR, rec, QB_DIR_HEAD))
        return status;
    if (rd32(rec) != QB_MAGIC)
        return status;                             /* erased (0xFF), or never written: no bank */
    if (rd16(rec + 4) != QB_VERSION || rd32(rec + 8) != QB_CODEC) {
        status = QB_OLD;                           /* another format: tools/qy70_install.py again */
        return status;
    }
    status = QB_BAD;
    n = rd16(rec + 6);
    used = rd32(rec + 12);
    poff = rd32(rec + 16);
    plen = rd16(rec + 20);
    if (!n || n > QB_MAXSTYLES || used > QB_CAPACITY || plen != QB_PRIORS || poff + plen > used ||
        qb_flash_read(QB_DIR + QB_DIR_HEAD, rec + QB_DIR_HEAD, QB_DIR_BYTES(n) - QB_DIR_HEAD) ||
        qb_crc32(0, rec, QB_DIR_BYTES(n) - 4u) != rd32(rec + QB_DIR_BYTES(n) - 4u))
        return status;
    if (read_logical(poff, priors, QB_PRIORS) || qb_crc32(0, priors, QB_PRIORS) != rd32(rec + 24))
        return status;
    for (i = 0; i < (int)n; i++) {
        const uint8_t *e = rec + QB_DIR_HEAD + QB_DIR_ENTRY * (uint32_t)i;
        int k = 8;
        memcpy(ent[i].name, e, 8);
        while (k && (ent[i].name[k - 1] == ' ' || ent[i].name[k - 1] == 0))
            k--;
        ent[i].name[k] = 0;
        for (int j = 0; j < k; j++)                  /* printable only (the screen's font) */
            if (ent[i].name[j] < 32 || ent[i].name[j] > 126)
                ent[i].name[j] = '?';
        ent[i].off = rd32(e + 8);
        ent[i].hlen = (uint16_t)rd16(e + 12);
        ent[i].slen = (uint16_t)rd16(e + 14);
        ent[i].crc = rd32(e + 16);
        ent[i].tempo10 = (uint16_t)rd16(e + 20);
        ent[i].beats = e[22];
        if ((uint32_t)ent[i].hlen + ent[i].slen > QB_RECMAX || ent[i].off + ent[i].hlen + ent[i].slen > used)
            return status;
    }
    count = (int)n;
    status = count;
    return status;
}
void qb_unmount(void)
{
    count = 0;
    status = QB_NONE;
}
int qb_status(void) { return status; }
int qb_count(void) { return count; }
const char *qb_name(int i) { return i >= 0 && i < count ? ent[i].name : ""; }
int qb_tempo(int i)
{
    int t = i >= 0 && i < count ? (ent[i].tempo10 + 5) / 10 : 120;
    return t < 40 ? 40 : t > 240 ? 240 : t;
}

/* the record's header (qy70_codec.py head()) into *s; its bytes, or -1 */
static int header(const uint8_t *h, uint32_t n, qb_style_t *s)
{
    uint32_t p = 15;
    int t, k, nt = 0;
    if (n < 16)
        return -1;
    memset(s->name, 0, sizeof s->name);
    memcpy(s->name, h, 8);
    for (k = 8; k && (s->name[k - 1] == ' ' || s->name[k - 1] == 0); k--)
        s->name[k - 1] = 0;
    s->tempo10 = (uint16_t)rd16(h + 8);
    s->beats = h[10];
    if (s->beats < 2 || s->beats > 7)
        return -1;
    for (k = 0; k < 6; k++)
        s->bars[k] = (uint8_t)((h[11 + k / 2] >> (4 * (k & 1)) & 15u) + 1u);
    s->tmask = h[14];
    for (t = 0; t < 8; t++) {
        qb_trk_t *r = &s->trk[t];
        if (!(s->tmask >> t & 1u)) {               /* not played: silent defaults on the track's own channel */
            static const uint8_t CH[8] = {9, 9, 9, 1, 2, 3, 4, 5};
            memset(r, 0, sizeof *r);
            r->chan = CH[t];
            r->drum = r->chan == 9;
            r->vol = 100;
            r->pan = 64;
            r->vscale = 128;
            continue;
        }
        if (p + 7 > n)
            return -1;
        r->chan = h[p] & 15u;
        if (r->chan == 15u)                        /* channel 16 is the click's (arr.h ARR_CLICK_CHAN): 15 */
            r->chan = 14u;
        r->drum = h[p] >> 7;
        r->prog = h[p + 1] & 127u;
        r->vol = h[p + 2] & 127u;
        r->pan = h[p + 3] & 127u;
        r->rev = h[p + 4] & 127u;
        r->cho = h[p + 5] & 127u;
        r->vscale = h[p + 6];
        p += 7;
        nt++;
    }
    if (p >= n)
        return -1;
    s->nph = h[p++];
    if (!s->nph || s->nph > QB_MAXPH)
        return -1;
    for (k = 0; k < 6; k++)
        for (t = 0; t < 8; t++) {
            uint8_t u;
            if (p >= n)
                return -1;
            u = h[p++];
            s->use[k][t] = 0xFF;
            s->prog[k][t] = s->trk[t].prog;
            if (u == 0xFF)
                continue;
            if ((u & 127u) >= s->nph || !(s->tmask >> t & 1u))
                return -1;
            s->use[k][t] = u & 127u;
            if (u & 128u) {                        /* this use's own program */
                if (p >= n)
                    return -1;
                s->prog[k][t] = h[p++] & 127u;
            }
        }
    (void)nt;
    return (int)p;
}

int qb_decode(int i, qb_style_t *out)
{
    uint32_t hl, sl;
    if (i < 0 || i >= count)
        return QB_E_INDEX;
    out->index = -1;
    hl = ent[i].hlen;
    sl = ent[i].slen;
    if (read_logical(ent[i].off, rec, hl + sl))
        return QB_E_READ;
    if (qb_crc32(0, rec, hl + sl) != ent[i].crc)
        return QB_E_CRC;
    if (header(rec, hl, out) != (int)hl)
        return QB_E_HEAD;
    if (qy_decode_style(rec + hl, sl, priors, out->nph, out->ph, out->note, QB_MAXN, out->ctl, QB_MAXC, &work))
        return QB_E_DECODE;
    out->nnotes = out->nctl = 0;
    for (int k = 0; k < out->nph; k++) {
        const qy_phrase_t *P = &out->ph[k];
        if (P->note0 + P->nnotes > out->nnotes)
            out->nnotes = (uint16_t)(P->note0 + P->nnotes);
        if (P->ctl0 + P->nctl > out->nctl)
            out->nctl = (uint16_t)(P->ctl0 + P->nctl);
        if (!P->length)
            return QB_E_DECODE;
    }
    out->index = (int16_t)i;
    return 0;
}
