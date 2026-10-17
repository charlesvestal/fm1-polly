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
    char n[8] = "Prog ";
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
void loops_default_one(loop_t *l, int i)
{
    if (i < 8)
        loop_from_template(l, FACTORY_LOOP[i]);
    else
        loop_clear(l, i);
}
void loops_default(loop_t *t)
{
    for (int i = 0; i < ARR_LOOPS; i++)
        loops_default_one(&t[i], i);
}
int loops_is_default(const loop_t *t)
{
    loop_t d;
    for (int i = 0; i < ARR_LOOPS; i++) {
        loops_default_one(&d, i);
        if (memcmp(&d, &t[i], sizeof d))
            return 0;
    }
    return 1;
}
/* the packed form (loops.h): byte o of the output, kept only while it fits */
static int put(uint8_t *d, int o, int max, uint32_t v)
{
    if (d && o < max)
        d[o] = (uint8_t)v;
    return o + 1;
}
int loops_pack(const loop_t *t, uint8_t *d, int max)
{
    loop_t def;
    int o = 0;
    for (int i = 0; i < ARR_LOOPS; i++) {
        const loop_t *l = &t[i];
        int nch = 0, named;
        loops_default_one(&def, i);
        if (!memcmp(&def, l, sizeof def))
            continue;                                 /* as the song starts it: nothing */
        named = memcmp(def.name, l->name, sizeof l->name) != 0;
        for (int b = 0; b < ARR_LOOP_BEATS; b++)
            nch += l->ch[b] != LC_SAME;
        o = put(d, o, max, (uint32_t)i | (named ? 0x80u : 0u));
        o = put(d, o, max, l->bars);
        o = put(d, o, max, l->beats);
        o = put(d, o, max, (uint32_t)nch);
        for (int k = 0; named && k < (int)sizeof l->name; k++)
            o = put(d, o, max, (uint8_t)l->name[k]);
        for (int b = 0; b < ARR_LOOP_BEATS; b++)
            if (l->ch[b] != LC_SAME) {
                o = put(d, o, max, (uint32_t)b);
                o = put(d, o, max, l->ch[b] & 0xFFu);
                o = put(d, o, max, (uint32_t)l->ch[b] >> 8);
            }
    }
    return o > max ? -1 : o;
}
int loops_unpack(loop_t *t, const uint8_t *s, int n)
{
    int o = 0, last = -1;
    loops_default(t);
    while (o < n) {
        int i, named, bars, beats, nch, prev = -1;
        loop_t *l;
        if (o + 4 > n)
            goto bad;
        i = s[o] & 0x7F;
        named = s[o] >> 7;
        bars = s[o + 1];
        beats = s[o + 2];
        nch = s[o + 3];
        o += 4;
        if (i >= ARR_LOOPS || i <= last || beats < 2 || beats > 7 || bars > loop_max_bars(beats) ||
            nch > ARR_LOOP_BEATS || o + (named ? 8 : 0) + 3 * nch > n)
            goto bad;                                 /* (in order, each once, in range, whole) */
        last = i;
        l = &t[i];
        l->bars = (uint8_t)bars;
        l->beats = (uint8_t)beats;
        if (named) {
            memcpy(l->name, s + o, sizeof l->name);
            o += 8;
        }
        for (int b = 0; b < ARR_LOOP_BEATS; b++)
            l->ch[b] = LC_SAME;
        for (int k = 0; k < nch; k++, o += 3) {
            int b = s[o];
            uint16_t v = (uint16_t)(s[o + 1] | s[o + 2] << 8);
            if (b >= ARR_LOOP_BEATS || b <= prev || v == LC_SAME)
                goto bad;
            prev = b;
            l->ch[b] = v;
        }
    }
    return 0;
bad:
    loops_default(t);
    return -1;
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
static int in_loop(const loop_t *l, int b, int k)  /* bar b's beat k is a beat of l */
{
    int bpb = l->beats ? l->beats : 4;
    return b < l->bars && k < bpb && b * bpb + k < ARR_LOOP_BEATS;
}
static void keep_take(const loop_t *l, loop_keep_t *k)   /* the cells inside l: l's */
{
    int bpb = l->beats ? l->beats : 4;
    for (int b = 0; b < ARR_LOOP_BARS; b++)
        for (int i = 0; i < 7; i++)
            if (in_loop(l, b, i))
                k->c[b][i] = l->ch[b * bpb + i];
}
void loop_keep_init(loop_keep_t *k, const loop_t *l)
{
    for (int b = 0; b < ARR_LOOP_BARS; b++)
        for (int i = 0; i < 7; i++)
            k->c[b][i] = LC_SAME;
    keep_take(l, k);
    k->bars = k->got = l->bars;
}
void loop_relayout(loop_t *l, loop_keep_t *k, int bars, int beats)
{
    int m;
    if (l->bars != k->got)                       /* (changed since: an undo, a write growing it) */
        k->bars = l->bars;
    keep_take(l, k);
    if (bars >= 0)
        k->bars = (uint8_t)(bars > ARR_LOOP_BARS ? ARR_LOOP_BARS : bars);
    l->beats = (uint8_t)(beats < 2 ? 2 : beats > 7 ? 7 : beats);
    m = loop_max_bars(l->beats);
    l->bars = (uint8_t)(k->bars > m ? m : k->bars);
    for (int b = 0; b < ARR_LOOP_BEATS; b++)
        l->ch[b] = LC_SAME;
    for (int b = 0; b < l->bars; b++)
        for (int i = 0; i < l->beats; i++)
            l->ch[b * l->beats + i] = k->c[b][i];
    k->got = l->bars;
}
int loop_hidden(const loop_t *l, const loop_keep_t *k)
{
    int n = 0;
    for (int b = 0; b < ARR_LOOP_BARS; b++)
        for (int i = 0; i < 7; i++)
            n += k->c[b][i] != LC_SAME && !in_loop(l, b, i);
    return n;
}
void loop_set_beats(loop_t *l, int beats)
{
    loop_keep_t k;
    loop_keep_init(&k, l);
    loop_relayout(l, &k, l->bars, beats);
}
void loops_song_clear(song_t *g)
{
    uint16_t style[ARR_NSECT], sty = g->sty;
    lead_trk_t lead[ARR_LEADS];
    memcpy(style, g->style, sizeof style);
    memcpy(lead, g->lead, sizeof lead);
    memset(g, 0, sizeof *g);
    memcpy(g->style, style, sizeof style);
    memcpy(g->lead, lead, sizeof lead);
    g->sty = sty;
    g->intro = g->ending = 1;
    g->tempo = 120;
}
static row_t row_new(const song_t *g, int r, int loop)
{
    row_t w = {(uint8_t)loop, ROW_A, 1, 0, 0, 0};
    for (int k = r - 1; k >= 0; k--)
        if (g->row[k].loop == loop) {
            w.sect = row_main(&g->row[k]) == SEC_B ? ROW_B : ROW_A;
            break;
        }
    return w;
}
int row_tap(song_t *g, int r, int loop)
{
    if (r < 0 || r > g->nrows || r >= ARR_ROWS || loop < 0 || loop >= ARR_LOOPS)
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
    g->row[r] = r < g->nrows ? g->row[r + 1] : r > 0 ? g->row[r - 1] : row_new(g, 0, 0);
    g->nrows++;
}
int row_append(song_t *g, int loop)
{
    if (g->nrows >= ARR_ROWS || loop < 0 || loop >= ARR_LOOPS)
        return -1;
    g->row[g->nrows] = row_new(g, g->nrows, loop);
    return g->nrows++;
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
