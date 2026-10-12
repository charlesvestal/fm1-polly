/* SPDX-License-Identifier: GPL-3.0-only */
/* The note editor's operations (notes.h). The selection order (place, then pitch: a chord's notes one by one) and
 * "a note is named, not counted" are after Melodee's NOTES (keremimo, GPL-3.0: seq_notes.c notes_before /
 * notes_cycle); the data here is Polly's own. */
#include "notes.h"
#include "loops.h"

int nmap_sect(const nmap_t *m, uint32_t t)
{
    for (int s = 0; s < m->n; s++)
        if (t >= m->start[s] && t - m->start[s] < m->len[s])
            return s;
    return -1;
}
uint32_t nl_time(const nmap_t *m, const lead_ev_t *e)
{
    uint32_t p = lead_pos(e);
    return e->row < m->n && p < m->len[e->row] ? m->start[e->row] + p : ~0u;
}
int nl_find(const leads_t *l, const nmap_t *m, int trk, nkey_t k)
{
    int s = nmap_sect(m, k.t);
    if (s < 0)
        return -1;
    for (uint32_t i = l->row0[s]; i < l->row0[s + 1] && i < l->n; i++) {
        const lead_ev_t *e = lead_at_c(l, i);
        if (lead_trk(e) == trk && e->note == k.note && nl_time(m, e) == k.t)
            return (int)i;
    }
    return -1;
}
/* the order's key: place, then pitch (song ticks < 2^24: fits) */
static uint32_t okey(uint32_t t, int note) { return t << 7 | (uint32_t)(note & 127); }
static int better(uint32_t x, uint32_t at, uint32_t best, int found, int dir)
{
    if (dir > 0 ? x <= at : dir < 0 ? x >= at : x < at)
        return 0;
    return !found || (dir < 0 ? x > best : x < best);
}
int nl_near(const leads_t *l, const nmap_t *m, int trk, nkey_t *k, int dir)
{
    uint32_t at = okey(k->t, k->note), best = 0;
    int found = 0;
    for (uint32_t i = 0; i < l->n; i++) {
        const lead_ev_t *e = lead_at_c(l, i);
        uint32_t t = nl_time(m, e), x;
        if (lead_trk(e) != trk || t == ~0u)
            continue;
        x = okey(t, e->note);
        if (better(x, at, best, found, dir)) {
            best = x;
            found = 1;
        }
    }
    if (found) {
        k->t = best >> 7;
        k->note = (uint8_t)(best & 127u);
    }
    return found;
}
int nl_move(leads_t *l, const nmap_t *m, int i, uint32_t t, int note, int root)
{
    lead_ev_t e, n;
    nkey_t k;
    int s, j, at = -1;
    if (i < 0 || i >= (int)l->n)
        return -1;
    e = *lead_at(l, (uint32_t)i);
    if (t == ~0u)
        t = nl_time(m, &e);
    if ((s = nmap_sect(m, t)) < 0)
        return -1;
    k.t = t;
    k.note = (uint8_t)(note & 127);
    if ((j = nl_find(l, m, lead_trk(&e), k)) >= 0)
        return j == i ? i : -1;
    leads_erase_at(l, (uint32_t)i, 1);
    lead_set(&n, s, t - m->start[s], lead_len(&e), note, e.vel, lead_trk(&e), root < 0 ? lead_root(&e) : root);
    leads_insert(l, &n, LEAD_MAX, &at);          /* (one out, one in: never full) */
    return at;
}
int nl_root_at(const song_t *g, const loop_t *loops, int r, uint32_t pos)
{
    const row_t *w = &g->row[r % ARR_ROWS];
    const loop_t *lp = &loops[w->loop % ARR_LOOPS];
    int n = loop_len(lp);
    chord_t c = loop_chord_at(lp, n > 0 ? (int)(pos / ARR_PPQ % (uint32_t)n) : 0);
    int root = c.type == CH_NONE ? 0 : c.root;
    return ((root + g->transpose + w->key) % 12 + 12) % 12;
}

int np_find(const pattern_t *p, int trk, nkey_t k)
{
    const arr_trk_t *t = &p->trk[trk & 7];
    for (uint32_t i = t->first; i < (uint32_t)t->first + t->count && i < p->nev; i++)
        if (p->ev[i].tick == k.t && p->ev[i].note == k.note)
            return (int)i;
    return -1;
}
int np_near(const pattern_t *p, int trk, uint32_t end, nkey_t *k, int dir)
{
    const arr_trk_t *t = &p->trk[trk & 7];
    uint32_t at = okey(k->t, k->note), best = 0;
    int found = 0;
    for (uint32_t i = t->first; i < (uint32_t)t->first + t->count && i < p->nev; i++) {
        uint32_t x = okey(p->ev[i].tick, p->ev[i].note);
        if (p->ev[i].tick < end && better(x, at, best, found, dir)) {
            best = x;
            found = 1;
        }
    }
    if (found) {
        k->t = best >> 7;
        k->note = (uint8_t)(best & 127u);
    }
    return found;
}
int np_insert(pattern_t *p, int trk, uint32_t tick, uint32_t len, int note, int vel)
{
    arr_trk_t *t = &p->trk[trk & 7];
    nkey_t k = {tick, (uint8_t)note};
    int j = np_find(p, trk, k);
    uint32_t ins, e;
    if (j < 0 && p->nev >= ARR_NEV)
        return -1;
    if (j < 0) {
        ins = t->count ? t->first : p->nev;
        e = t->count ? (uint32_t)t->first + t->count : p->nev;
        for (; ins < e && p->ev[ins].tick <= tick; ins++)
            ;
        for (uint32_t i = p->nev; i > ins; i--)
            p->ev[i] = p->ev[i - 1];
        p->nev++;
        for (int u = 0; u < ARR_TRACKS; u++)
            if (u != (trk & 7) && p->trk[u].first >= ins)
                p->trk[u].first++;
        if (!t->count)
            t->first = (uint16_t)ins;
        t->count++;
        j = (int)ins;
    }
    p->ev[j].tick = (uint16_t)tick;
    p->ev[j].len = (uint16_t)(len < 1u ? 1u : len > 0xFFFFu ? 0xFFFFu : len);
    p->ev[j].note = (uint8_t)(note & 127);
    p->ev[j].vel = (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel);
    return j;
}
void np_delete(pattern_t *p, int trk, int i)
{
    arr_trk_t *t = &p->trk[trk & 7];
    if (i < (int)t->first || i >= (int)t->first + t->count || i >= (int)p->nev)
        return;
    for (uint32_t k = (uint32_t)i; k + 1u < p->nev; k++)
        p->ev[k] = p->ev[k + 1];
    p->nev--;
    t->count--;
    for (int u = 0; u < ARR_TRACKS; u++)
        if (u != (trk & 7) && p->trk[u].first > i)
            p->trk[u].first--;
}
int np_move(pattern_t *p, int trk, int i, uint32_t t, int note)
{
    arr_ev_t x;
    nkey_t k;
    int j;
    if (i < 0 || i >= (int)p->nev)
        return -1;
    x = p->ev[i];
    k.t = t == ~0u ? x.tick : t;
    k.note = (uint8_t)(note & 127);
    if ((j = np_find(p, trk, k)) >= 0)
        return j == i ? i : -1;
    np_delete(p, trk, i);
    return np_insert(p, trk, k.t, x.len, k.note, x.vel);
}
