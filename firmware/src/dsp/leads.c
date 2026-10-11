/* SPDX-License-Identifier: GPL-3.0-only */
#include "leads.h"

void leads_clear(leads_t *l)
{
    l->n = 0;
    l->part[0].n = l->part[1].n = 0;
    leads_index(l);
}
int lead_cmp(const lead_ev_t *a, const lead_ev_t *b)
{
    uint32_t pa = lead_pos(a), pb = lead_pos(b);
    if (a->row != b->row)
        return a->row < b->row ? -1 : 1;
    if (pa != pb)
        return pa < pb ? -1 : 1;
    if (lead_trk(a) != lead_trk(b))
        return lead_trk(a) < lead_trk(b) ? -1 : 1;
    if (a->note != b->note)
        return a->note < b->note ? -1 : 1;
    return 0;
}
void leads_index(leads_t *l)
{
    uint32_t i = 0;
    if (l->n > LEAD_MAX)
        l->n = LEAD_MAX;
    for (int r = 0; r <= ARR_ROWS; r++) {
        while (i < l->n && lead_at(l, i)->row < r)
            i++;
        l->row0[r] = (uint16_t)i;
    }
    l->row0[ARR_ROWS] = l->n;                    /* (a row past the last: none, leads_fix drops them) */
    l->part[0].n = (uint16_t)(l->n < LEAD_PART_NOTES ? l->n : LEAD_PART_NOTES);
    l->part[1].n = (uint16_t)(l->n - l->part[0].n);
}
int leads_insert(leads_t *l, const lead_ev_t *e, int max, int *at)
{
    uint32_t i, lo = 0, hi = l->n;
    if (max > LEAD_MAX)
        max = LEAD_MAX;
    while (lo < hi) {                            /* the first note not before it */
        uint32_t m = (lo + hi) / 2;
        if (lead_cmp(lead_at(l, m), e) < 0)
            lo = m + 1;
        else
            hi = m;
    }
    i = lo;
    if (at)
        *at = (int)i;
    if (i < l->n && lead_cmp(lead_at(l, i), e) == 0) {
        *lead_at(l, i) = *e;                     /* the same note: replaced */
        return 0;
    }
    if ((int)l->n >= max)
        return -1;
    for (uint32_t k = l->n; k > i; k--)          /* (no memmove in the firmware's libc) */
        *lead_at(l, k) = *lead_at(l, k - 1);
    *lead_at(l, i) = *e;
    l->n++;
    leads_index(l);
    return 1;
}
/* the notes of row r (-1 any) and track trk (-1 either) go; then the rows from rows_from (-1 none) move by `by` (a
 * note moved past the rows goes too); the list compacted in place, in order; returns the notes gone */
static int compact(leads_t *l, int r, int trk, int rows_from, int by)
{
    uint32_t w = 0;
    int gone = 0;
    for (uint32_t i = 0; i < l->n; i++) {
        lead_ev_t e = *lead_at(l, i);
        if ((r < 0 || e.row == r) && (trk < 0 || lead_trk(&e) == trk)) {
            gone++;
            continue;
        }
        if (rows_from >= 0 && e.row >= rows_from) {
            int nr = e.row + by;
            if (nr < 0 || nr >= ARR_ROWS) {
                gone++;
                continue;
            }
            e.row = (uint8_t)nr;
        }
        *lead_at(l, w++) = e;
    }
    l->n = (uint16_t)w;
    leads_index(l);
    return gone;
}
int leads_erase(leads_t *l, int r, int trk)
{
    if (r >= ARR_ROWS)
        return 0;
    if (r < 0 && trk < 0) {
        int n = l->n;
        leads_clear(l);
        return n;
    }
    return compact(l, r, trk, -1, 0);
}
int leads_count(const leads_t *l, int r, int trk)
{
    uint32_t a = r < 0 ? 0 : r < ARR_ROWS ? l->row0[r] : l->n, b = r < 0 ? l->n : r < ARR_ROWS ? l->row0[r + 1] : l->n;
    int n = 0;
    if (trk < 0)
        return (int)(b - a);
    for (uint32_t i = a; i < b; i++)
        n += lead_trk(lead_at_c(l, i)) == trk;
    return n;
}
int leads_row_insert(leads_t *l, int r)
{
    if (r < 0 || r >= ARR_ROWS)
        return 0;
    return compact(l, ARR_ROWS, -1, r, 1);      /* (row ARR_ROWS: no row erased) */
}
int leads_row_delete(leads_t *l, int r)
{
    if (r < 0 || r >= ARR_ROWS)
        return 0;
    {
        int gone = compact(l, r, -1, -1, 0);
        compact(l, ARR_ROWS, -1, r + 1, -1);
        return gone;
    }
}
int leads_fix(leads_t *l, int n0, int n1)
{
    uint32_t w = 0, n;
    int moved = 0;
    n0 = n0 < 0 ? 0 : n0 > LEAD_PART_NOTES ? LEAD_PART_NOTES : n0;
    n1 = n1 < 0 ? 0 : n1 > LEAD_PART_NOTES ? LEAD_PART_NOTES : n1;
    if (n0 < LEAD_PART_NOTES && n1)              /* part 0 filled first: part 1's notes after part 0's (forward: the
                                                  * place written is never past the one read) */
        for (int j = 0; j < n1; j++) {
            *lead_at(l, (uint32_t)(n0 + j)) = l->part[1].ev[j];
            moved = 1;
        }
    n = (uint32_t)(n0 + n1);
    for (uint32_t i = 0; i < n; i++) {           /* the notes no row or track has: gone */
        lead_ev_t e = *lead_at(l, i);
        if (e.row >= ARR_ROWS || lead_root(&e) > 11 || !e.note || e.note > 127 || !e.vel || e.vel > 127 || !lead_len(&e) ||
            (e.tr & 0x70u)) {
            moved = 1;
            continue;
        }
        *lead_at(l, w++) = e;
    }
    for (uint32_t i = 1; i < w; i++) {           /* in order (as stored: nothing moves, one pass) */
        lead_ev_t e = *lead_at(l, i);
        uint32_t k = i;
        while (k > 0 && lead_cmp(lead_at(l, k - 1), &e) > 0) {
            *lead_at(l, k) = *lead_at(l, k - 1);
            k--;
        }
        if (k != i) {
            *lead_at(l, k) = e;
            moved = 1;
        }
    }
    for (uint32_t i = 1; i < w; i++)             /* the same note twice: the later one kept */
        if (lead_cmp(lead_at(l, i - 1), lead_at(l, i)) == 0) {
            for (uint32_t k = i - 1; k + 1 < w; k++)
                *lead_at(l, k) = *lead_at(l, k + 1);
            w--;
            i--;
            moved = 1;
        }
    l->n = (uint16_t)w;
    leads_index(l);
    return moved;
}
int leads_parts(int n) { return n <= 0 ? 0 : n <= LEAD_PART_NOTES ? 1 : 2; }
unsigned leads_parts_from(int i, int nb, int na)
{
    int hi = nb > na ? nb : na;
    unsigned m = 0;
    if (i < LEAD_PART_NOTES && i < hi)
        m |= 1u;
    if (hi > LEAD_PART_NOTES)
        m |= 2u;
    return m;
}
