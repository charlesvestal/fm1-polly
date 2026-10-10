/* SPDX-License-Identifier: GPL-3.0-only */
#include <string.h>
#include "loops.h"
#include "templates.h"

const uint8_t FACTORY_LOOP[8] = {0, 1, 2, 3, 4, 7, 9, 8};

int loop_len(const loop_t *l) { return l->bars * (l->beats ? l->beats : 4); }
int loop_max_bars(int beats)
{
    int m = ARR_LOOP_BEATS / (beats < 2 ? 2 : beats);
    return m > ARR_LOOP_BARS ? ARR_LOOP_BARS : m;
}
static void name_set(loop_t *l, const char *s)
{
    memset(l->name, 0, sizeof l->name);
    for (int i = 0; i < (int)sizeof l->name && s[i]; i++)
        l->name[i] = s[i];
}
void loop_clear(loop_t *l, int i)
{
    char n[8] = "Loop ";
    memset(l, 0, sizeof *l);
    l->beats = 4;
    if (i >= 9) {
        n[5] = '1';
        n[6] = (char)('0' + (i + 1) % 10);
    } else
        n[5] = (char)('1' + i);
    name_set(l, n);
    for (int b = 0; b < ARR_LOOP_BEATS; b++)
        l->ch[b] = LC_SAME;
}
void loop_from_template(loop_t *l, int t)
{
    const template_t *m = &TEMPLATES[t];
    loop_clear(l, 0);
    name_set(l, m->name);
    l->beats = m->beats;
    l->bars = m->nbars;
    for (int b = 0; b < m->nbars; b++)
        l->ch[b * m->beats] = lc_pack(m->bars[b]);
}
int loop_write(loop_t *l, int b, chord_t c)
{
    int bpb = l->beats ? l->beats : 4;
    if (b < 0 || b >= loop_max_bars(bpb) * bpb)
        return -1;
    if (b >= loop_len(l))
        l->bars = (uint8_t)(b / bpb + 1);
    l->ch[b] = lc_pack(c);
    return 0;
}
void loop_unwrite(loop_t *l, int b)
{
    if (b >= 0 && b < ARR_LOOP_BEATS)
        l->ch[b] = LC_SAME;
}
chord_t loop_chord_at(const loop_t *l, int b)
{
    int n = loop_len(l);
    chord_t none = {0, CH_NONE, CH_NOBASS};
    if (n > ARR_LOOP_BEATS)              /* a damaged object: never read past ch[] */
        n = ARR_LOOP_BEATS;
    if (n <= 0)
        return none;
    b %= n;
    if (b < 0)
        b += n;
    for (int k = 0; k < n; k++) {
        int i = (b - k + n) % n;
        if (l->ch[i] != LC_SAME)
            return lc_unpack(l->ch[i]);
    }
    return none;
}
static void clear_past(loop_t *l)                 /* the beats past its end: no change (growth starts clean) */
{
    for (int b = loop_len(l); b < ARR_LOOP_BEATS; b++)
        l->ch[b] = LC_SAME;
}
void loop_set_bars(loop_t *l, int bars)
{
    int m = loop_max_bars(l->beats);
    l->bars = (uint8_t)(bars < 1 ? 1 : bars > m ? m : bars);
    clear_past(l);
}
void loop_set_beats(loop_t *l, int beats)
{
    l->beats = (uint8_t)(beats < 2 ? 2 : beats > 7 ? 7 : beats);
    if (l->bars > loop_max_bars(l->beats))
        l->bars = (uint8_t)loop_max_bars(l->beats);
    clear_past(l);
}
void loops_song_clear(song_t *g)
{
    uint16_t style[ARR_NSECT], sty = g->sty;
    memcpy(style, g->style, sizeof style);
    memset(g, 0, sizeof *g);
    memcpy(g->style, style, sizeof style);
    g->sty = sty;
    g->intro = g->ending = 1;
    g->tempo = 120;
}
static row_t row_new(const song_t *g, int r, int loop)
{
    row_t w = {(uint8_t)loop, ROW_A, 1, 0, 0, 0};
    for (int k = r - 1; k >= 0; k--)
        if (g->row[k].loop == loop) {
            w.sect = g->row[k].sect >= ROW_B ? ROW_B : ROW_A;
            break;
        }
    return w;
}
int row_tap(song_t *g, int r, int loop)
{
    int last = g->nrows - 1;
    if ((r == g->nrows || r == last) && last >= 0 && g->row[last].loop == loop) {
        if (g->row[last].rep < 8)              /* the same loop again at the end: REPEAT + 1 */
            g->row[last].rep++;
        return last;
    }
    if (r < 0 || r > g->nrows || r >= ARR_ROWS)
        return -1;
    g->row[r] = row_new(g, r, loop);
    if (r == g->nrows)
        g->nrows++;
    return r;
}
void row_insert(song_t *g, int r)              /* (no memmove in the firmware: by hand) */
{
    if (g->nrows >= ARR_ROWS || r < 0 || r > g->nrows)
        return;
    for (int k = g->nrows; k > r; k--)
        g->row[k] = g->row[k - 1];
    g->row[r] = r > 0 ? g->row[r - 1] : row_new(g, 0, 0);
    g->nrows++;
}
void row_delete(song_t *g, int r)
{
    if (r < 0 || r >= g->nrows)
        return;
    for (int k = r; k < g->nrows - 1; k++)
        g->row[k] = g->row[k + 1];
    g->nrows--;
}
int loop_rows_using(const song_t *g, int loop)
{
    int n = 0;
    for (int r = 0; r < g->nrows; r++)
        n += g->row[r].loop == loop;
    return n;
}
int free_rec_bars(int beats, int bpb)
{
    int bars, m;
    bpb = bpb < 2 ? 2 : bpb > 7 ? 7 : bpb;
    bars = (beats + bpb - 1) / bpb;
    m = loop_max_bars(bpb);
    return bars < 1 ? 1 : bars > m ? m : bars;
}
chord_t chord_shift(chord_t c, int by)
{
    by = (by % 12 + 12) % 12;
    c.root = (uint8_t)((c.root + by) % 12);
    if (c.bass < 12u)
        c.bass = (uint8_t)((c.bass + by) % 12);
    return c;
}
