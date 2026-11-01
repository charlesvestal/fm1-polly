/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's store on fm1-base's real storage.c (its pool, app.h APP_STORE_POOL) and backup.c over a simulated NOR flash
 * (erase -> 0xFF, a program only clears bits), with Polly's own project.c on top:
 *   the move: a store as Polly 0.5 left it (each object an A/B pair where APP_STORE_MAP has it: some with an older
 *     copy, one with a torn newer copy, one never written) read as it is before the first boot, then moved into the
 *     pool by st_pool_boot: every object byte for byte its 0.5 current copy, project.c loading the songs, patterns,
 *     melodies and the progressions' slot, the sectors past the pool (the resource region's) erased, the project,
 *     the panel's calibration and the QY70 bank's sectors never touched
 *   a power cut at every flash write of the move (a program torn in half), then the next boot: every object as
 *     before; a second cut during that boot too
 *   room: every object at its largest, saved again and again: never MEMORY FULL (-8), two sectors always free
 *   a backup made by Polly 0.5 (its image, encoded here from the format: PLYB, version 2, 18 parts) restored into the
 *     pool; a backup of the pooled store is the same image (0.5 restores it too); a power cut at every write of a
 *     restore, then the boot: each object whole, and restoring again gives the backup
 *   tests/run_tests.sh builds it: cc -Iapp -Iapp/dsp -Ibase/include -Itests/host store_test.c gm_stub.c $ARR */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PLAT_HOST 1
#include "../../app/app.h"
#define FELUCCA_ID "FM-1_300"

static uint8_t nor[0x100000];
static int cut_at = -1, ops;
static jmp_buf cut;
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* where a write may go: the songs' and slots' 34 sectors (the pool and what it frees), the project's pair, the
 * restore's journal record. Anything else (the panel, the QY70 bank, the update's staging): the test fails */
static int writable(uint32_t off, uint32_t n)
{
    return (off >= APP_STORE_POOL_BASE && off + n <= APP_STORE_POOL_BASE + APP_STORE_POOL_OLD * 4096u) ||
           (off >= 0xFC000u && off + n <= 0xFE000u) || (off >= 0xE1000u && off + n <= 0xE2000u);
}
static void op(void)                              /* a flash write: the power may go here */
{
    if (cut_at >= 0 && ops++ == cut_at)
        longjmp(cut, 1);
}
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (!writable(off, 4096) || (off & 0xFFFu)) {
        printf("FAIL erase outside the store: %#x\n", off);
        exit(1);
    }
    op();
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    if (!writable(off, n)) {
        printf("FAIL program outside the store: %#x+%u\n", off, n);
        exit(1);
    }
    if (cut_at >= 0 && ops == cut_at)             /* torn: half of it written */
        for (uint32_t i = 0; i < n / 2; i++)
            nor[off + i] &= s[i];
    op();
    for (uint32_t i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../../base/device/storage.c"

/* the platform's store, as plat_fm1.c has it; ram: a table instead (the fixture made by project.c first) */
static int ram;
static uint8_t tbl[OBJ_NOBJ][PLAT_STORE_MAX];
static int tlen[OBJ_NOBJ];
int plat_store_load(uint32_t o, void *dst, uint32_t max)
{
    uint32_t n;
    if (!ram)
        return st_load(o, dst, max);
    if (o >= OBJ_NOBJ || tlen[o] <= 0)
        return -1;
    n = (uint32_t)tlen[o] > max ? max : (uint32_t)tlen[o];
    memcpy(dst, tbl[o], n);
    return (int)n;
}
int plat_store_head(uint32_t o, void *dst, uint32_t n)
{
    if (!ram)
        return st_load_head(o, dst, n);
    if (o >= OBJ_NOBJ || tlen[o] <= 0 || (uint32_t)tlen[o] < n)
        return -1;
    memcpy(dst, tbl[o], n);
    return (int)n;
}
int plat_store_size(uint32_t o) { return ram ? (o >= OBJ_NOBJ || tlen[o] <= 0 ? -1 : tlen[o]) : st_size(o); }
int plat_store_save(uint32_t o, const void *src, uint32_t n)
{
    if (!ram)
        return st_save(o, src, n);
    if (o >= OBJ_NOBJ || n > PLAT_STORE_MAX)
        return -1;
    memcpy(tbl[o], src, n);
    tlen[o] = (int)n;
    return 0;
}
int plat_store_room(void) { return st_pool_room(); }
void arr_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
uint32_t arr_midi_out_room(void) { return 64; }
uint32_t arr_midi_out_link(void) { return 0; }
#include "../../app/project.c"

/* ota.c for its pack7 (its hooks unused), then backup.c with the test's hooks */
static int ota_wire_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return -1; }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
static void ota_frame_done(void) {}
static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) {}
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../../base/bsp/chip/ota.c"
#include "../../base/device/backup.c"

static uint8_t img[BU_MAXIMG + 64], got[BU_MAXIMG + 64];
static uint32_t img_n, got_n;
static int reloads;
static int rs_fetch(uint32_t addr, uint8_t *dst, uint32_t n)
{
    memset(dst, 0, n);
    if (addr < img_n)
        memcpy(dst, img + addr, addr + n <= img_n ? n : img_n - addr);
    return 0;
}
static int rs_note(uint32_t addr) { (void)addr; return 0; }
static int rs_playing(void) { return 0; }
static void rs_begin(void) {}
static void rs_show(uint32_t pct) { (void)pct; }
static int rs_ask(void) { return 1; }
static void rs_reload(void) { reloads++; }
static int rs_rec_erase(void) { return st_erase(RS_REC); }
static int rs_rec_prog(uint32_t off, const void *src, uint32_t n) { return st_prog(RS_REC + off, src, n); }
static int rs_rec_read(uint32_t off, void *dst, uint32_t n) { return st_read(RS_REC + off, dst, n); }
static int frames_this_pass;
static int bu_room(void) { return !frames_this_pass; }
static int bu_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[400];
    uint32_t len;
    frames_this_pass++;
    ota_unpack7(p + 1, n - 2, u, sizeof u);
    len = u[3] | u[4] << 8 | (uint32_t)u[5] << 16;
    if (u[2] == BU_REQ) {
        memcpy(got + got_n, u + 10, len - 4);
        got_n += len - 4;
    }
    return 0;
}
static void bu_flush(void) {}
static void bu_hold(int on) { (void)on; }

/* ---------------------------------------------------------------- the fixture --- */
/* each object as Polly 0.5 stored it (made by project.c into the table): its bytes, -1 none */
typedef struct { int n[OBJ_NOBJ]; uint8_t d[OBJ_NOBJ][PLAT_STORE_MAX]; } set_t;
static set_t want, now;
static void snap(set_t *s)
{
    for (uint32_t o = 0; o < OBJ_NOBJ; o++)
        s->n[o] = o == OBJ_PANEL ? -1 : st_load(o, s->d[o], PLAT_STORE_MAX);
}
static int same(const set_t *a, const set_t *b)
{
    for (uint32_t o = 0; o < OBJ_NOBJ; o++)
        if (a->n[o] != b->n[o] || (a->n[o] > 0 && memcmp(a->d[o], b->d[o], (size_t)a->n[o])))
            return 0;
    return 1;
}

static void pattern_with(pattern_t *p, int seed, int nev)
{
    pattern_empty(p);
    p->bars = (uint8_t)(1 + seed % 4);
    for (int t = 0; t < ARR_TRACKS; t++) {
        p->trk[t].first = (uint16_t)p->nev;
        p->trk[t].program = (uint8_t)((seed * 7 + t) & 127);
        for (int i = 0; i < nev / ARR_TRACKS; i++) {
            arr_ev_t *e = &p->ev[p->nev++];
            e->tick = (uint16_t)(i * 120);
            e->len = 60;
            e->note = (uint8_t)(36 + (seed + i + t) % 48);
            e->vel = (uint8_t)(64 + i % 60);
        }
        p->trk[t].count = (uint16_t)(p->nev - p->trk[t].first);
    }
}
static void lead_with(lead_part_t *p, int song, int part, int n)
{
    memset(p, 0, sizeof *p);
    p->magic = LEAD_MAGIC;
    p->kind = LEAD_KIND;
    p->song = (uint8_t)song;
    p->part = (uint8_t)part;
    p->n = (uint16_t)n;
    for (int i = 0; i < n; i++)
        lead_set(&p->ev[i], i / 16, (uint32_t)(i % 16) * 240u, 200, 60 + i % 12, 90, 0, 0);
}
static songs_t S;
static pattern_t P;
static lead_part_t LP;
static loops_part_t LO;
/* what a 0.5 FM-1 might hold: the project, the songs (one song's progressions in a slot), patterns in slots 0..2,
 * 4, 5 and 10..15 (the last ones in sectors the pool frees), melodies in 6 and 7, a song's progressions in 8, a freed
 * melody's header in 9; slot 3 never written */
static void make_fixture(void)
{
    int8_t gs;
    ram = 1;
    memset(tlen, 0, sizeof tlen);
    project_defaults();
    proj.song = 2;
    proj.trk = 6;
    proj.program[3] = 33;
    project_save();
    project_songs_load(&S);                         /* (none: the defaults) */
    S.song[0].tempo = 96;
    S.song[0].nrows = 3;
    S.song[0].row[1].loop = 2;
    S.song[0].row[2].rep = 4;
    S.song[0].style[SEC_A - 1] = 0;                 /* Main A on slot 0's pattern */
    S.song[1].style[SEC_B - 1] = 11;
    S.song[1].transpose = -3;
    project_loops_load(&S, 2, &LO, &gs);            /* song 2's progressions, edited, in slot 8 */
    LO.loop[0].ch[0] = lc_pack((chord_t){5, 1, CH_NOBASS});
    loops_head(&LO, 2);
    obj_save(OBJ_PAT0 + 8, &LO, sizeof LO);
    S.plen[2] = ARR_PLEN_SLOT;
    project_songs_save(&S);
    for (int i = 0; i < ARR_USER_PATTERNS; i++)
        if (i < 3 || i == 4 || i == 5 || i >= 10) {
            pattern_with(&P, i, i == 15 ? ARR_NEV : 16 + 24 * i);
            project_pattern_save(i, &P);
        }
    lead_with(&LP, 0, 0, 40);
    obj_save(OBJ_PAT0 + 6, &LP, LEAD_HDR + 9u * LP.n);
    lead_with(&LP, 1, 0, 7);
    obj_save(OBJ_PAT0 + 7, &LP, LEAD_HDR + 9u * LP.n);
    lead_with(&LP, 3, 0, 0);
    obj_save(OBJ_PAT0 + 9, &LP, LEAD_HDR);
    for (uint32_t o = 0; o < OBJ_NOBJ; o++) {
        want.n[o] = tlen[o] > 0 ? tlen[o] : -1;
        memcpy(want.d[o], tbl[o], PLAT_STORE_MAX);
    }
    want.n[OBJ_PANEL] = -1;
    ram = 0;
}

/* a copy as 0.5's storage.c wrote it: copy (0 A, 1 B) of obj at its APP_STORE_MAP place, seq; torn: its payload
 * changed after the commit record (the record's CRC no longer the payload's) */
static void old_copy(uint32_t obj, uint32_t copy, uint32_t seq, const uint8_t *p, uint32_t len, int torn)
{
    static const uint32_t map[OBJ_NOBJ] = APP_STORE_MAP;
    uint32_t at = map[obj] + copy * ST_SECTOR;
    st_hdr_t h;
    memset(nor + at, 0xFF, ST_SECTOR);
    memcpy(nor + at + ST_PAYLOAD_OFF, p, len);
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)copy;
    h.seq = seq;
    h.len = len;
    h.crc = st_crc32(p, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + at, &h, sizeof h);
    if (torn)
        nor[at + ST_PAYLOAD_OFF + len / 2] ^= 0x10;
}
static uint8_t qy_was[0x100000];
/* the NOR as 0.5 left it. Objects alternate: some with only an A copy, some with an older A and the current B, some
 * an older B and the current A (seq 3 over 2), slot 12 with a torn newer B over its good A; the QY70 bank's area and
 * the panel's calibration hold data that must never change */
static void lay_old(void)
{
    static uint8_t older[PLAT_STORE_MAX];
    memset(nor, 0xFF, sizeof nor);
    for (uint32_t i = 0x93000u; i < 0x97000u; i++)
        nor[i] = (uint8_t)(i * 13u);
    for (uint32_t i = 0xB9000u; i < 0xE0000u; i++)   /* the bank, the panel at 0xDC000, the bank's directory */
        nor[i] = (uint8_t)(i * 7u ^ (i >> 9));
    for (uint32_t i = 0xE5000u; i < 0xFC000u; i++)
        nor[i] = (uint8_t)(i * 3u);
    for (uint32_t o = 0; o < OBJ_NOBJ; o++) {
        uint32_t n = (uint32_t)want.n[o];
        if (o == OBJ_PANEL || want.n[o] < 0)
            continue;
        for (uint32_t i = 0; i < n; i++)
            older[i] = (uint8_t)(want.d[o][i] ^ 0x5A);
        switch (o % 3) {
        case 0:
            old_copy(o, 0, 1, want.d[o], n, 0);
            break;
        case 1:
            old_copy(o, 0, 1, older, n, 0);
            old_copy(o, 1, 2, want.d[o], n, 0);
            break;
        default:
            old_copy(o, 1, 2, older, n, 0);
            old_copy(o, 0, 3, want.d[o], n, 0);
        }
        if (o == OBJ_PAT0 + 12) {
            old_copy(o, 0, 4, want.d[o], n, 0);
            old_copy(o, 1, 5, older, n, 1);           /* a save cut short in 0.5: its A stays current */
        }
    }
    memcpy(qy_was, nor, sizeof nor);
    st_pool_scan = APP_STORE_POOL_OLD;               /* (a firmware just updated: nothing moved yet) */
}
static int outside_same(void)                     /* the bank, the panel, the staging: as they were */
{
    return !memcmp(nor + 0x93000u, qy_was + 0x93000u, 0x4000u) &&
           !memcmp(nor + 0xB9000u, qy_was + 0xB9000u, 0xE0000u - 0xB9000u) &&
           !memcmp(nor + 0xE5000u, qy_was + 0xE5000u, 0xFC000u - 0xE5000u);
}
static int region_erased(void)                    /* past the pool: nothing (the resource region's to take) */
{
    for (uint32_t i = APP_STORE_POOL_BASE + APP_STORE_POOL_N * ST_SECTOR;
         i < APP_STORE_POOL_BASE + APP_STORE_POOL_OLD * ST_SECTOR; i++)
        if (nor[i] != 0xFF)
            return 0;
    return 1;
}
/* project.c on what the flash holds: the songs, every pattern slot, the melodies, the slot's progressions */
static int polly_loads(void)
{
    static leads_t L;
    int8_t slot[2], gs;
    int ok = project_load() == 0 && proj.song == 2 && proj.trk == 6 && proj.program[3] == 33;
    ok &= project_songs_load(&S) == 0 && S.song[0].tempo == 96 && S.song[0].nrows == 3 && S.song[0].row[2].rep == 4 &&
          S.song[1].transpose == -3 && S.plen[2] == ARR_PLEN_SLOT;
    for (int i = 0; i < ARR_USER_PATTERNS; i++)
        if (i < 3 || i == 4 || i == 5 || i >= 10)
            ok &= project_pattern_load(i, &P) == 0 && P.nev == (i == 15 ? ARR_NEV : 16 + 24 * i) / ARR_TRACKS * ARR_TRACKS;
    ok &= project_pattern_load(3, &P) != 0;           /* (never written) */
    ok &= project_leads_load(0, &L, slot) == 0 && slot[0] == 6 && L.part[0].n == 40;
    ok &= project_leads_load(1, &L, slot) == 0 && slot[0] == 7 && L.part[0].n == 7;
    ok &= project_leads_load(3, &L, slot) == -1;      /* (its header alone: freed) */
    ok &= project_loops_load(&S, 2, &LO, &gs) == 0 && gs == 8 && LO.loop[0].ch[0] == lc_pack((chord_t){5, 1, CH_NOBASS});
    return ok;
}

/* a backup image (the format, encoded here as Polly 0.5's backup.c sent it): the head (magic, version, count, flags
 * 0, the head's CRC), each part (id, length, CRC, bytes), the CRC of everything before */
static uint32_t encode(const set_t *s, uint8_t *out)
{
    static const uint16_t ids[] = APP_BACKUP_OBJS;
    uint32_t pos = 16;
    bu_wr32(out, 0x42594C50u);                        /* "PLYB" */
    bu_wr16(out + 4, 2);
    bu_wr16(out + 6, (uint32_t)(sizeof ids / sizeof ids[0]));
    bu_wr32(out + 8, 0);
    bu_wr32(out + 12, bu_crc(0, out, 12));
    for (uint32_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        uint32_t n = s->n[ids[i]] < 0 ? 0 : (uint32_t)s->n[ids[i]];
        bu_wr16(out + pos, ids[i]);
        bu_wr16(out + pos + 2, n);
        bu_wr32(out + pos + 4, bu_crc(0, s->d[ids[i]], n));
        memcpy(out + pos + 8, s->d[ids[i]], n);
        pos += 8 + n;
    }
    bu_wr32(out + pos, bu_crc(0, out, pos));
    return pos + 4;
}

static void test_move(void)
{
    lay_old();
    snap(&now);
    CHECK(same(&now, &want), "before the first boot: every object read where 0.5 left it");
    CHECK(polly_loads(), "... and Polly loads it");
    CHECK(st_pool_boot() == 0 && st_pool_scan == APP_STORE_POOL_N, "the move: every copy into the pool");
    snap(&now);
    CHECK(same(&now, &want), "... every object byte for byte its 0.5 current copy (slot 12: its A, the B torn)");
    CHECK(polly_loads(), "... the songs, patterns, melodies and the slot's progressions load");
    CHECK(region_erased(), "... 0xAB000..0xB8FFF erased: free for the resource region");
    CHECK(outside_same(), "... the QY70 bank, the panel's calibration, the staging untouched");
    CHECK(st_pool_boot() == 0 && same(&now, &want), "the next boot: nothing more to move");
}

static void test_cuts(void)
{
    static uint8_t start[sizeof nor];
    int total, bad = 0, bad2 = 0, n2 = 0;
    lay_old();
    memcpy(start, nor, sizeof nor);
    ops = 0;
    cut_at = 1 << 30;                                 /* counting */
    st_pool_boot();
    total = ops;
    for (int k = 0; k < total; k++) {
        memcpy(nor, start, sizeof nor);
        st_pool_scan = APP_STORE_POOL_OLD;
        ops = 0;
        cut_at = k;
        if (!setjmp(cut)) {
            st_pool_boot();
            bad++;
            continue;
        }
        cut_at = -1;
        snap(&now);                                    /* (between the boots: lookups still scan the old pairs) */
        bad += !same(&now, &want);
        st_pool_scan = APP_STORE_POOL_OLD;            /* the next power-on */
        bad += st_pool_boot() != 0 || st_pool_scan != APP_STORE_POOL_N;
        snap(&now);
        bad += !same(&now, &want) || !region_erased() || !outside_same();
        if (k % 7 == 3) {                             /* cut again, during that boot's move */
            static uint8_t mid[sizeof nor];
            int t2;
            memcpy(nor, start, sizeof nor);
            st_pool_scan = APP_STORE_POOL_OLD;
            ops = 0;
            cut_at = k;
            if (!setjmp(cut)) {
                st_pool_boot();
                continue;
            }
            memcpy(mid, nor, sizeof nor);
            st_pool_scan = APP_STORE_POOL_OLD;
            ops = 0;
            cut_at = 1 << 30;
            st_pool_boot();
            t2 = ops;
            for (int j = 0; j < t2; j += 3) {
                memcpy(nor, mid, sizeof nor);
                st_pool_scan = APP_STORE_POOL_OLD;
                ops = 0;
                cut_at = j;
                if (!setjmp(cut)) {
                    st_pool_boot();
                    continue;
                }
                cut_at = -1;
                st_pool_scan = APP_STORE_POOL_OLD;
                st_pool_boot();
                snap(&now);
                bad2 += !same(&now, &want) || st_pool_scan != APP_STORE_POOL_N || !region_erased();
                n2++;
            }
        }
    }
    cut_at = -1;
    printf("  the move: %d flash writes; %d second cuts\n", total, n2);
    CHECK(!bad, "a power cut at each write of the move: after the next boot every object as before (%d bad)", bad);
    CHECK(!bad2, "... and a second cut during that boot's move: the same (%d bad)", bad2);
}

static void test_room(void)
{
    static uint8_t big[PLAT_STORE_MAX];
    int minroom = 99, bad = 0;
    memset(nor, 0xFF, sizeof nor);
    st_pool_scan = APP_STORE_POOL_OLD;
    st_pool_boot();
    for (int round = 0; round < 5; round++)
        for (uint32_t o = OBJ_SONGS; o < OBJ_NOBJ; o++) {
            int r;
            memset(big, (int)(o * 5u + (uint32_t)round), sizeof big);
            r = plat_store_room();
            if (r < minroom)
                minroom = r;
            bad += st_save(o, big, PLAT_STORE_MAX) != 0;
        }
    CHECK(!bad && minroom >= 2, "every object at its largest, saved 5 times: never MEMORY FULL, %d sectors free at least",
          minroom);
    CHECK(plat_store_room() == (int)(APP_STORE_POOL_N - APP_STORE_POOL_OBJN), "... with all of them: %d free",
          plat_store_room());
}

static void test_backup(void)
{
    static uint8_t start[sizeof nor];
    static set_t other;
    int bad = 0, total;
    /* a 0.5 FM-1's own backup (the fixture's objects), restored into the pooled store after the move; the radio's
     * keys (OBJ_BLE, pooled, not in a backup) kept */
    lay_old();
    st_pool_boot();
    img_n = encode(&want, img);
    memset(other.d[OBJ_BLE], 0xB1, 200);
    st_save(OBJ_BLE, other.d[OBJ_BLE], 200);
    for (uint32_t o = OBJ_SONGS; o < OBJ_PAT0 + 16u; o++) {   /* the FM-1 holds other songs now */
        memset(other.d[o], (int)o, 1000);
        st_save(o, other.d[o], 1000);
    }
    reloads = 0;
    CHECK(restore_rx(img_n, bu_crc(0, img, img_n), 0) == 0 && reloads == 1, "a 0.5 backup restored into the pool");
    snap(&now);
    CHECK(now.n[OBJ_BLE] == 200 && !memcmp(now.d[OBJ_BLE], other.d[OBJ_BLE], 200), "... the Bluetooth keys kept");
    now.n[OBJ_BLE] = -1;
    CHECK(same(&now, &want) && polly_loads(), "... every object as backed up; Polly loads it (slot 3 absent again)");
    /* a backup of the pooled store: the same image as 0.5 made of the same objects */
    got_n = 0;
    bu_start(0);
    for (int pass = 0; bu.on && pass < 100000; pass++) {
        frames_this_pass = 0;
        bu_step();
    }
    CHECK(got_n == img_n && !memcmp(got, img, img_n), "a backup of the pooled store: 0.5's image, byte for byte "
          "(the keys not in it)");
    /* a power cut at every write of a restore (over other songs), then the boot: each object whole; again: the backup */
    for (uint32_t o = OBJ_SONGS; o < OBJ_PAT0 + 16u; o++)
        st_save(o, other.d[o], 1000);
    memcpy(start, nor, sizeof nor);
    ops = 0;
    cut_at = 1 << 30;
    restore_rx(img_n, bu_crc(0, img, img_n), 0);
    total = ops;
    for (int k = 0; k < total; k++) {
        memcpy(nor, start, sizeof nor);
        ops = 0;
        cut_at = k;
        if (!setjmp(cut)) {
            restore_rx(img_n, bu_crc(0, img, img_n), 0);
            bad++;
            continue;
        }
        cut_at = -1;
        rs_boot();
        st_pool_scan = APP_STORE_POOL_OLD;
        st_pool_boot();
        snap(&now);
        bad += now.n[OBJ_BLE] != 200;                 /* (the keys: never touched) */
        for (uint32_t o = OBJ_SONGS; o < OBJ_PAT0 + 16u; o++)
            bad += !(now.n[o] < 0 || (now.n[o] == want.n[o] && !memcmp(now.d[o], want.d[o], (size_t)now.n[o])) ||
                     (now.n[o] == 1000 && !memcmp(now.d[o], other.d[o], 1000)));
        bad += restore_rx(img_n, bu_crc(0, img, img_n), 0) != 0;
        snap(&now);
        now.n[OBJ_BLE] = -1;
        bad += !same(&now, &want);
    }
    cut_at = -1;
    printf("  a restore: %d flash writes\n", total);
    CHECK(!bad, "a power cut at each write of a restore: each object whole; restoring again gives the backup (%d bad)",
          bad);
    CHECK(outside_same(), "... the QY70 bank and the panel's calibration untouched through all of it");
}

int main(void)
{
    make_fixture();
    test_move();
    test_cuts();
    test_room();
    test_backup();
    printf("%d checks, %d failed\n", checks, fails);
    if (fails)
        return 1;
    printf("store test passed\n");
    return 0;
}
