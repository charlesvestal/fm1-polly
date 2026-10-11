/* SPDX-License-Identifier: GPL-3.0-only */
/* Undo and redo: a journal of before-images (after X0X's undo.c, over several regions).
 *
 * The undoable state is a few regions of bytes (ui.c: the song, its loops, its lead notes, one pattern). Each has
 * a shadow: the region as it was at the last commit. A commit (the end of a step: ui.c decides when) compares each
 * region with its shadow and stores the runs of bytes that changed, all regions' in one entry, with their previous
 * contents. Undo swaps an entry's bytes with the regions', so the entry then holds what undo took away, and redo
 * swaps them back: one copy serves both directions. A region is swapped one at a time (uj_apply), into the live
 * bytes or a copy of them (a pattern lives in the engine: ui.c swaps a copy and stores it).
 *
 * Entries live in a byte pool, oldest first; a new one drops the oldest until it fits (and there are at most UJ_MAX),
 * and drops whatever was undone and not redone. An entry bigger than the whole pool cannot be kept: the history is
 * emptied (uj_commit says so).
 *
 * Entry: u16 run count, u8 the regions it touches (bit r), u8 0, UJ_LABEL bytes of label (what the app said the step
 * was, NUL-padded; empty: named from its runs); then per run u8 region, u8 aux (the region's at the commit: ui.c, the
 * pattern's slot), u16 offset, u16 length and its bytes, padded to 2. Part of the unity build (ui.c includes it);
 * tests/host/journal_test.c too. */
#include <stdint.h>

#ifndef UJ_POOL
#define UJ_POOL 16384u
#endif
#define UJ_MAX 32
#define UJ_GAP 8u                      /* unchanged bytes between changes cheaper to keep than a new run */
#define UJ_REGIONS 4
#define UJ_LABEL 14
#define UJ_HDR (4u + UJ_LABEL)

typedef struct {
    uint8_t *live, *shadow;            /* live 0: left out of the next commit */
    uint32_t len;
    uint8_t aux;
} uj_region_t;
typedef struct {
    uj_region_t r[UJ_REGIONS];
    uint8_t pool[UJ_POOL];
    uint32_t used;
    uint32_t at[UJ_MAX + 1];           /* entry k starts at pool[at[k]]; at[n] == used */
    int n, cur;                        /* entries kept; how many of them are applied */
} uj_t;

static uint16_t uj_get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void uj_put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* no history (the regions as they are now: the caller brings the shadows up to date, uj_resync) */
static void uj_clear(uj_t *u)
{
    u->used = 0;
    u->n = u->cur = 0;
    u->at[0] = 0;
}
static void uj_region(uj_t *u, int i, void *live, void *shadow, uint32_t len)
{
    u->r[i].live = live;
    u->r[i].shadow = shadow;
    u->r[i].len = len;
}
/* region i's shadow from its live bytes: what it holds now is where its history goes on from */
static void uj_resync(uj_t *u, int i)
{
    uj_region_t *g = &u->r[i];
    if (g->live && g->shadow)
        for (uint32_t k = 0; k < g->len; k++)
            g->shadow[k] = g->live[k];
}

static void uj_drop_oldest(uj_t *u)
{
    uint32_t sz = u->at[1], i;
    for (i = sz; i < u->used; i++)
        u->pool[i - sz] = u->pool[i];
    u->used -= sz;
    for (int k = 0; k < u->n; k++)
        u->at[k] = u->at[k + 1] - sz;
    u->n--;
    u->cur--;
}

/* the run of changes in region g that starts at or after i: 1 = found (*s, *e its first and last byte, short gaps
 * of unchanged bytes taken in), 0 none */
static int uj_run_at(const uj_region_t *g, uint32_t i, uint32_t *s, uint32_t *e)
{
    uint32_t end;
    while (i < g->len && g->live[i] == g->shadow[i])
        i++;
    if (i >= g->len)
        return 0;
    end = i;
    for (;;) {
        uint32_t q, found = 0;
        for (q = end + 1u; q < g->len && q < end + 1u + UJ_GAP; q++)
            if (g->live[q] != g->shadow[q]) {
                found = q;
                break;
            }
        if (!found)
            break;
        end = found;
    }
    *s = i;
    *e = end;
    return 1;
}

/* the changes since the last commit, as one entry named `label` (0 or "": named from its runs). 1 = an entry was
 * made, 0 = nothing changed, -1 = too big to keep (the history is emptied; the regions are the new starting point) */
static int uj_commit(uj_t *u, const char *label)
{
    uint32_t sz = UJ_HDR, runs = 0, s, e, i;
    uint8_t *p, mask = 0;
    for (int r = 0; r < UJ_REGIONS; r++) {               /* first pass: the runs and the entry's size */
        const uj_region_t *g = &u->r[r];
        if (!g->live || !g->shadow)
            continue;
        for (i = 0; uj_run_at(g, i, &s, &e); i = e + 1u) {
            runs++;
            mask |= (uint8_t)(1u << r);
            sz += 6u + ((e - s + 1u + 1u) & ~1u);
        }
    }
    if (!runs)
        return 0;
    u->n = u->cur;                                        /* what was undone is gone */
    u->used = u->at[u->n];
    if (sz > UJ_POOL) {
        for (int r = 0; r < UJ_REGIONS; r++)
            uj_resync(u, r);
        uj_clear(u);
        return -1;
    }
    while (u->n > 0 && (u->n >= UJ_MAX || u->used + sz > UJ_POOL))
        uj_drop_oldest(u);
    p = u->pool + u->used;
    uj_put16(p, runs);
    p[2] = mask;
    p[3] = 0;
    for (i = 0; i < UJ_LABEL; i++)
        p[4 + i] = 0;
    for (i = 0; label && i < UJ_LABEL && label[i]; i++)
        p[4 + i] = (uint8_t)label[i];
    p += UJ_HDR;
    for (int r = 0; r < UJ_REGIONS; r++) {               /* second pass, the same runs: the bytes before into the
                                                          * entry, the shadow brought up to date */
        uj_region_t *g = &u->r[r];
        if (!g->live || !g->shadow)
            continue;
        for (i = 0; uj_run_at(g, i, &s, &e); i = e + 1u) {
            p[0] = (uint8_t)r;
            p[1] = g->aux;
            uj_put16(p + 2, s);
            uj_put16(p + 4, e - s + 1u);
            p += 6;
            for (uint32_t k = s; k <= e; k++) {
                *p++ = g->shadow[k];
                g->shadow[k] = g->live[k];
            }
            if ((e - s + 1u) & 1u)
                *p++ = 0;
        }
    }
    u->used += sz;
    u->n++;
    u->cur = u->n;
    u->at[u->n] = u->used;
    return 1;
}

/* entry k: the regions it touches (bit r), its label (UJ_LABEL bytes, NUL-padded: copy it out), its runs */
static int uj_mask(const uj_t *u, int k) { return u->pool[u->at[k] + 2]; }
static void uj_label_of(const uj_t *u, int k, char *t)
{
    const uint8_t *p = u->pool + u->at[k] + 4;
    int i;
    for (i = 0; i < UJ_LABEL && p[i]; i++)
        t[i] = (char)p[i];
    t[i] = 0;
}
/* run j of entry k (0..): 1 = there is one (its region, aux, offset, length) */
static int uj_run(const uj_t *u, int k, int j, int *region, int *aux, uint32_t *off, uint32_t *len)
{
    const uint8_t *p = u->pool + u->at[k];
    int runs = uj_get16(p);
    if (j < 0 || j >= runs)
        return 0;
    p += UJ_HDR;
    for (;;) {
        uint32_t n = uj_get16(p + 4);
        if (!j--) {
            *region = p[0];
            *aux = p[1];
            *off = uj_get16(p + 2);
            *len = n;
            return 1;
        }
        p += 6u + ((n + 1u) & ~1u);
    }
}
/* the aux of region r's runs in entry k (the first's), -1 none */
static int uj_aux(const uj_t *u, int k, int r)
{
    int rg, ax;
    uint32_t o, l;
    for (int j = 0; uj_run(u, k, j, &rg, &ax, &o, &l); j++)
        if (rg == r)
            return ax;
    return -1;
}

/* entry k's runs of region r swapped with live (the region's bytes, or a copy of them), and shadow (0: none) set to
 * what live holds after. Swapping twice is no change (a copy that could not be stored: swapped back). Returns the
 * runs swapped */
static int uj_apply(uj_t *u, int k, int r, uint8_t *live, uint8_t *shadow)
{
    uint8_t *p = u->pool + u->at[k];
    int runs = uj_get16(p), done = 0;
    p += UJ_HDR;
    for (int j = 0; j < runs; j++) {
        uint32_t off = uj_get16(p + 2), n = uj_get16(p + 4);
        if (p[0] == r)
            for (uint32_t i = 0; i < n; i++) {
                uint8_t t = live[off + i];
                live[off + i] = p[6 + i];
                if (shadow)
                    shadow[off + i] = p[6 + i];
                p[6 + i] = t;
            }
        done += p[0] == r;
        p += 6u + ((n + 1u) & ~1u);
    }
    return done;
}

/* the entry an undo takes back (cur - 1) / a redo puts back (cur): -1 none. The caller applies its regions, then
 * uj_moved */
static int uj_undo_at(const uj_t *u) { return u->cur > 0 ? u->cur - 1 : -1; }
static int uj_redo_at(const uj_t *u) { return u->cur < u->n ? u->cur : -1; }
static void uj_moved(uj_t *u, int redo) { u->cur += redo ? 1 : -1; }
