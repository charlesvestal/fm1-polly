/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of backup and restore over USB, the device's side (firmware/src/backup.c), on storage.c over a simulated
 * NOR flash (erase -> 0xFF, a program only clears bits):
 *   backup: the image streamed in frames that each fit the SysEx TX ring, one a main-loop pass, contiguous, the
 *     project, the songs and the 16 slots as stored (absent ones absent), the CRCs right; stopped, the unsaved saved
 *     first; the autosave held while it streams
 *   restore: refused while playing (nothing fetched, nothing written); the cable cut at every read, a changed byte
 *     (payload, record, head, tail), a wrong length, HOME at the question: nothing changed; SEL: everything as backed
 *     up, the app reloaded; the panel's calibration and the flash outside the objects never touched
 *   a power cut at every flash write of a restore (a program torn in half): after the next boot (rs_boot) the set is
 *     the old one or the new one, whole, never a mix
 *   backup_test IMAGE_OUT   (the image, for tests/polly_backup_test.py) */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GM_HOST 1
#include "../firmware/src/app/plat.h"
#define FELUCCA_ID "FM-1_300"

static uint8_t nor[0x100000];
static int fails, cut_at = -1, ops, fetch_left = -1, flip_at = -1, playing, play_after = -1, answer = 1;
static int reloads, flushes, holding, asks, begins, fetches;
static jmp_buf cut;
static uint32_t notes[8], nnotes;

static int in_store(uint32_t off, uint32_t n);
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
    if (!in_store(off, 4096) || (off & 0xFFFu)) {
        printf("erase outside the objects: %#x\n", off);
        exit(1);
    }
    op();
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    if (!in_store(off, n)) {
        printf("program outside the objects: %#x+%u\n", off, n);
        exit(1);
    }
    if (cut_at >= 0 && ops == cut_at) {           /* torn: half of it written */
        for (uint32_t i = 0; i < n / 2; i++)
            nor[off + i] &= s[i];
    }
    op();
    for (uint32_t i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"

/* ota.c for its pack7 (its hooks unused here) */
static int ota_wire_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return -1; }
static int ota_frame_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
static void ota_frame_done(void) {}
static uint32_t t_ms;                             /* the clock (the backup's stall limit) */
static uint32_t ota_now_ms(void) { return t_ms; }
static void ota_idle(void) {}
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../firmware/src/ota.c"

#include "../firmware/src/backup.c"

static int in_store(uint32_t off, uint32_t n)     /* the 18 objects' sectors, the journal record's */
{
    for (uint32_t i = 0; i < BU_NOBJ; i++) {
        uint32_t b = st_sector(bu_obj(i), 0);
        if (off >= b && off + n <= b + 2u * ST_SECTOR)
            return 1;
    }
    return off >= RS_REC && off + n <= RS_REC + ST_SECTOR;
}

/* the host side: the image served, the notes kept */
static uint8_t img[BU_MAXIMG + 64];
static uint32_t img_n;
static int rs_fetch(uint32_t addr, uint8_t *dst, uint32_t n)
{
    fetches++;
    if (fetch_left == 0)
        return -1;
    if (fetch_left > 0)
        fetch_left--;
    if (play_after >= 0 && fetches > play_after)
        playing = 1;
    memset(dst, 0, n);
    if (addr < img_n)
        memcpy(dst, img + addr, addr + n <= img_n ? n : img_n - addr);
    if (flip_at >= 0 && (uint32_t)flip_at >= addr && (uint32_t)flip_at < addr + n)
        dst[flip_at - (int)addr] ^= 0x04;
    return 0;
}
static int rs_note(uint32_t addr)
{
    if (nnotes < 8)
        notes[nnotes++] = addr;
    return 0;
}
static int rs_playing(void) { return playing; }
static void rs_begin(void) { begins++; }
static void rs_show(uint32_t pct) { (void)pct; }
static int rs_ask(void) { asks++; return answer; }
static void rs_reload(void) { reloads++; }
static int rs_rec_erase(void) { return st_erase(RS_REC); }
static int rs_rec_prog(uint32_t off, const void *src, uint32_t n) { return st_prog(RS_REC + off, src, n); }
static int rs_rec_read(uint32_t off, void *dst, uint32_t n) { return st_read(RS_REC + off, dst, n); }

/* the backup's frames: decoded and put together as the host does */
static int room = 1, frames_this_pass;
static uint8_t got[BU_MAXIMG + 64];
static uint32_t got_n, end_len, end_crc, max_wire, frames;
static int end_seen, end_status, end_flags, gap;
static int bu_room(void)                          /* a frame in the ring: full until the next pass; room -1: USB gone */
{
    return room < 0 ? -1 : room && !frames_this_pass;
}
static int bu_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[400];
    uint32_t d, len, s = 0;
    frames_this_pass++;
    frames++;
    if (n > max_wire)
        max_wire = n;
    d = ota_unpack7(p + 1, n - 2, u, sizeof u);
    len = u[3] | u[4] << 8 | (uint32_t)u[5] << 16;
    if (p[0] != 0xF0 || p[n - 1] != 0xF7 || u[0] != 0 || u[1] != 0x59 || len + 7u != d)
        gap = 1;
    for (uint32_t i = 0; i < len; i++)
        s += u[6 + i];
    if ((uint8_t)~s != u[6 + len])
        gap = 1;
    if (u[2] == BU_REQ) {
        uint32_t off = bu_rd32(u + 6);
        if (off != got_n)
            gap = 1;
        memcpy(got + got_n, u + 10, len - 4);
        got_n += len - 4;
    } else if (u[2] == BU_END) {
        end_seen = 1;
        end_len = bu_rd32(u + 6);
        end_crc = bu_rd32(u + 10);
        end_status = u[14];
        end_flags = u[15];
    }
    return 0;
}
static void bu_flush(void) { flushes++; }
static void bu_hold(int on) { holding = on; }

static int check(const char *what, int ok)
{
    printf("%-66s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
    return ok;
}

/* the objects as st_load gives them */
typedef struct { int n[BU_NOBJ]; uint8_t d[BU_NOBJ][ST_PAYLOAD_MAX]; } set_t;
static set_t set_a, set_b, now;
static void snap(set_t *s)
{
    for (uint32_t i = 0; i < BU_NOBJ; i++)
        s->n[i] = st_load(bu_obj(i), s->d[i], ST_PAYLOAD_MAX);
}
static int same(const set_t *a, const set_t *b)
{
    for (uint32_t i = 0; i < BU_NOBJ; i++)
        if (a->n[i] != b->n[i] || (a->n[i] > 0 && memcmp(a->d[i], b->d[i], (size_t)a->n[i])))
            return 0;
    return 1;
}
static uint8_t panel_was[2 * ST_SECTOR];
static int panel_same(void) { return !memcmp(nor + st_sector(OBJ_PANEL, 0), panel_was, sizeof panel_was); }

static void fill(uint8_t *p, uint32_t n, uint32_t seed)
{
    for (uint32_t i = 0; i < n; i++)
        p[i] = (uint8_t)((i * 31u + seed * 7u) ^ (i >> 5));
}
static void save(uint32_t obj, uint32_t n, uint32_t seed)
{
    static uint8_t b[ST_PAYLOAD_MAX];
    fill(b, n, seed);
    if (st_save(obj, b, n))
        printf("save failed\n");
}

static int backup(int play)
{
    int passes = 0;
    got_n = 0;
    end_seen = gap = 0;
    frames = max_wire = 0;
    bu_start(play);
    while (bu.on && passes < 100000) {
        frames_this_pass = 0;
        room = passes % 3 != 1;                   /* the ring busy now and then */
        bu_step();
        if (frames_this_pass > 1)
            gap = 1;
        passes++;
    }
    return passes;
}
static int restore(void)
{
    nnotes = 0;
    fetches = 0;
    return restore_rx(img_n, bu_crc(0, img, img_n), 0);
}

int main(int argc, char **argv)
{
    FILE *f;
    int rc;
    memset(nor, 0x5A, sizeof nor);                /* not erased anywhere: a write outside would show */
    for (uint32_t i = 0; i < BU_NOBJ; i++) {
        st_erase(st_sector(bu_obj(i), 0));
        st_erase(st_sector(bu_obj(i), 1));
    }
    st_erase(RS_REC);
    memset(nor + st_sector(OBJ_PANEL, 0), 0x33, sizeof panel_was);   /* the calibration: never touched */
    memcpy(panel_was, nor + st_sector(OBJ_PANEL, 0), sizeof panel_was);

    /* set A: the project, the songs (whole), 9 slots of all sizes; the rest absent */
    save(OBJ_PROJ, 120, 1);
    save(OBJ_SONGS, ST_PAYLOAD_MAX, 2);
    save(OBJ_SONGS, ST_PAYLOAD_MAX, 3);           /* (its B copy current) */
    for (uint32_t i = 0; i < 9; i++)
        save(OBJ_PAT0 + i * 2u - (i > 4), 16u + i * 431u, 10 + i);
    snap(&set_a);

    /* ---- backup */
    flushes = 0;
    backup(0);
    check("backup: an end frame, status 0, the frames contiguous", end_seen && end_status == 0 && !gap);
    check("... each frame fits the SysEx ring whole (<= 64 packets)", max_wire <= 64u * 3u);
    check("... stopped: the unsaved saved first; the autosave held, then free", flushes == 1 && holding == 0);
    check("... the length and the CRC-32 of the whole", end_len == got_n && end_crc == bu_crc(0, got, got_n));
    {
        uint32_t pos = BU_HEAD, ok = bu_rd32(got) == BU_MAGIC && bu_rd16(got + 6) == BU_NOBJ &&
                                      bu_rd32(got + 12) == bu_crc(0, got, 12) && bu_rd32(got + 8) == 0;
        for (uint32_t i = 0; i < BU_NOBJ && ok; i++) {
            uint32_t n = bu_rd16(got + pos + 2);
            ok = bu_rd16(got + pos) == bu_obj(i) && (int)n == (set_a.n[i] < 0 ? 0 : set_a.n[i]) &&
                 bu_rd32(got + pos + 4) == bu_crc(0, got + pos + 8, n) &&
                 (!n || !memcmp(got + pos + 8, set_a.d[i], n));
            pos += BU_REC + n;
        }
        check("... the image: the head, 18 records as stored (absent: 0 bytes)", ok);
        check("... the tail: the CRC-32 of all before", pos + 4u == got_n && bu_rd32(got + pos) == bu_crc(0, got, pos));
        printf("  %u bytes in %u frames (largest %u wire bytes)\n", got_n, frames, max_wire);
    }
    memcpy(img, got, got_n);
    img_n = got_n;
    if (argc > 1 && (f = fopen(argv[1], "wb"))) {
        fwrite(img, 1, img_n, f);
        fclose(f);
    }
    flushes = 0;
    backup(1);
    check("backup while playing: nothing saved first, flagged", flushes == 0 && end_flags == 1 && !gap &&
                                                              end_len == img_n && !memcmp(got + 16, img + 16, img_n - 20));

    {                                             /* a cut cable: the ring never drains / USB gone */
        int passes = 0, held_long;
        got_n = 0;
        end_seen = 0;
        t_ms = 1000;
        bu_start(1);
        for (int k = 0; k < 5; k++) {             /* a few frames out */
            frames_this_pass = 0;
            room = 1;
            bu_step();
            t_ms += 10;
        }
        room = 0;                                 /* then the ring full for good */
        while (bu.on && passes < 1000) {
            frames_this_pass = 0;
            bu_step();
            t_ms += 10;
            passes++;
        }
        held_long = passes * 10 >= (int)BU_STALL_MS && passes * 10 <= (int)BU_STALL_MS + 20;
        check("backup stalled (the ring never drains): given up after ~2 s, autosave free",
              !bu.on && !holding && !end_seen && held_long);
        bu_start(1);
        frames_this_pass = 0;
        room = 1;
        bu_step();
        room = -1;                                /* unplugged / suspended */
        frames_this_pass = 0;
        bu_step();
        check("... USB gone: given up at once, autosave free", !bu.on && !holding && !end_seen);
        room = 1;
        t_ms = 0;
    }

    /* set B: other contents, an absent slot filled, a project of another size */
    save(OBJ_PROJ, 140, 4);
    save(OBJ_SONGS, 3000, 5);
    save(OBJ_PAT0 + 1, 2000, 6);                  /* absent in A */
    save(OBJ_PAT0 + 4, 100, 7);
    save(OBJ_PAT0 + 15, 3840, 8);
    snap(&set_b);
    check("set B differs from A", !same(&set_a, &set_b));

    /* ---- refused, failed: nothing changes */
    playing = 1;
    ops = 0;
    cut_at = -1;
    rc = restore();
    playing = 0;
    snap(&now);
    check("playing: STOP FIRST (RS_E_PLAYING), told, nothing read", rc == RS_E_PLAYING && fetches == 0 && nnotes == 1 &&
                                                                    notes[0] == (RS_FAIL_ADDR | RS_E_PLAYING));
    check("... nothing changed", same(&now, &set_b));
    {
        int all = 1, n_fetch;
        fetch_left = -1;
        answer = 0;                               /* (to count the reads; answered no) */
        restore();
        n_fetch = fetches;
        for (int k = 0; k < n_fetch; k++) {       /* the cable cut at every read */
            fetch_left = k;
            asks = 0;
            rc = restore();
            snap(&now);
            all &= rc == RS_E_FETCH && asks == 0 && same(&now, &set_b) && nnotes == 1;
        }
        fetch_left = -1;
        printf("  %d reads a restore\n", n_fetch);
        check("the cable cut at each read: RS_E_FETCH, never asked, nothing changed", all);
    }
    answer = 1;
    {
        int all = 1;
        uint32_t spots[] = {20, BU_HEAD + BU_REC + 5, BU_HEAD + BU_REC + 119, 2000, img_n - 300, img_n - 10};
        for (uint32_t k = 0; k < sizeof spots / sizeof spots[0]; k++) {
            flip_at = (int)spots[k];
            rc = restore();
            snap(&now);
            all &= rc != 0 && same(&now, &set_b);
        }
        flip_at = (int)(img_n - 2);               /* the tail */
        rc = restore();
        all &= rc == RS_E_TOTAL;
        flip_at = 3;                              /* the head */
        rc = restore();
        all &= rc == RS_E_HEAD;
        flip_at = BU_HEAD + 1;                    /* the first record's id */
        rc = restore();
        all &= rc == RS_E_OBJ;
        flip_at = BU_HEAD + BU_REC + 30;          /* the project's payload */
        rc = restore();
        all &= rc == RS_E_CRC;
        flip_at = -1;
        snap(&now);
        check("a byte changed (payload, record, head, tail): refused, nothing changed", all && same(&now, &set_b));
    }
    rc = restore_rx(img_n + 4, bu_crc(0, img, img_n), 0);
    snap(&now);
    check("a wrong length: refused, nothing changed", rc != 0 && same(&now, &set_b));
    rc = restore_rx(img_n, bu_crc(0, img, img_n) ^ 1u, 0);
    snap(&now);
    check("the host's CRC-32 not the image's: RS_E_TOTAL, nothing changed", rc == RS_E_TOTAL && same(&now, &set_b));
    play_after = 5;                               /* PLAY (MIDI start) during the transfer */
    rc = restore();
    play_after = -1;
    playing = 0;
    snap(&now);
    check("started playing meanwhile: RS_E_PLAYING, nothing changed", rc == RS_E_PLAYING && same(&now, &set_b));
    answer = 0;
    asks = reloads = 0;
    rc = restore();
    snap(&now);
    check("HOME at the question: RS_E_NO, asked once, the host told first", rc == RS_E_NO && asks == 1 && nnotes == 2 &&
                                                                         notes[0] == RS_ASK_ADDR &&
                                                                         notes[1] == (RS_FAIL_ADDR | RS_E_NO));
    check("... nothing changed, nothing reloaded", same(&now, &set_b) && reloads == 0);

    /* ---- SEL: restored */
    answer = 1;
    begins = 0;
    rc = restore();
    snap(&now);
    check("SEL: restored (0), the host told, the app reloaded", rc == 0 && nnotes == 2 && notes[1] == RS_OK_ADDR &&
                                                               reloads == 1 && begins == 1);
    check("... every object as backed up (the slot absent in A: absent)", same(&now, &set_a) && now.n[3] < 0);
    check("... the panel's calibration untouched", panel_same());
    check("... the journal record erased", nor[RS_REC] == 0xFF && nor[RS_REC + 299] == 0xFF);
    backup(0);
    check("... a backup of it is the image again", got_n == img_n && !memcmp(got, img, img_n));
    save(OBJ_PAT0 + 3, 50, 9);
    {
        uint8_t b[64];
        fill(b, 50, 9);
        check("... saving after it works (the next seq)", st_load(OBJ_PAT0 + 3, now.d[0], 64) == 50 &&
                                                         !memcmp(now.d[0], b, 50));
    }

    /* ---- a power cut at every flash write: old or new after the boot, whole */
    {
        static uint8_t start[sizeof nor];
        int k, n_old = 0, n_new = 0, mixed = 0, total;
        save(OBJ_PROJ, 140, 4);
        save(OBJ_SONGS, 3000, 5);
        save(OBJ_PAT0 + 1, 2000, 6);
        save(OBJ_PAT0 + 4, 100, 7);
        save(OBJ_PAT0 + 15, 3840, 8);
        snap(&set_b);
        memcpy(start, nor, sizeof nor);
        ops = 0;
        cut_at = 1 << 30;                         /* counting only */
        restore();
        total = ops;
        for (k = 0; k < total; k++) {
            memcpy(nor, start, sizeof nor);
            ops = 0;
            cut_at = k;
            if (!setjmp(cut)) {
                restore();
                printf("cut %d: not reached\n", k);
                mixed++;
                continue;
            }
            cut_at = -1;
            rs_boot();                            /* the next power-on */
            snap(&now);
            if (same(&now, &set_a))
                n_new++;
            else if (same(&now, &set_b))
                n_old++;
            else
                mixed++;
            if (nor[RS_REC] != 0xFF)
                mixed++;
        }
        cut_at = -1;
        printf("  %d flash writes a restore: cut before the record %d (old), after %d (new, finished at boot)\n", total,
               n_old, n_new);
        check("a power cut at each write: the old set or the new, whole, never a mix", !mixed && n_old && n_new);
        check("... the panel's calibration untouched through all of it", panel_same());
    }
    printf("%s\n", fails ? "BACKUP TEST FAILED" : "backup test passed");
    return fails != 0;
}
