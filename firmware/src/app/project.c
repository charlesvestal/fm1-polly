/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's project: one flash object (OBJ_PROJ): the keys' octave, the track, tempo, key and grid, and the
 * MIDI-in channels' program, volume, pan, reverb and chorus. Patterns and songs have their own objects.
 * Part of the unity build, after app.h. */
#include <stddef.h>
#include "../dsp/loops.h"

project_t proj;

void project_defaults(void)
{
    int c;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    proj.leds = 1;
    proj.trk = 4;                                    /* C1: a piano under the keys */
    proj.quant = ARR_Q_16;
    proj.rev_type = 2;                               /* hall */
    proj.rev_level = 90;
    proj.cho_level = 80;
    proj.clock = 1;
    proj.mout = 1;
    for (c = 0; c < GM_NCH; c++) {
        proj.volume[c] = 100;
        proj.pan[c] = 64;
        proj.reverb[c] = 40;
        proj.chorus[c] = 0;
    }
}

static int project_valid(void)
{
    int c;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT || proj.ch > 15u || proj.oct < -3 || proj.oct > 3 ||
        proj.leds > 2u || proj.song >= ARR_SONGS || proj.pattern >= ARR_USER_PATTERNS ||
        proj.trk >= ARR_TRACKS || proj.quant >= ARR_NQ ||
        proj.rev_type > 4u || proj.rev_level > 127u || proj.cho_type > 3u || proj.cho_level > 127u ||
        proj.clock > 1u || proj.mout > 1u)
        return 0;
    for (c = 0; c < GM_NCH; c++)
        if (proj.program[c] > 127u || proj.volume[c] > 127u || proj.pan[c] > 127u || proj.reverb[c] > 127u ||
            proj.chorus[c] > 127u)
            return 0;
    return 1;
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n >= 8 && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

void project_apply(void)
{
    for (uint32_t c = 0; c < GM_NCH; c++) {
        if (c != 9)
            gm_midi(0xC0u | c, proj.program[c], 0);
        gm_midi(0xB0u | c, 7, proj.volume[c]);
        gm_midi(0xB0u | c, 10, proj.pan[c]);
        gm_midi(0xB0u | c, 91, proj.reverb[c]);
        gm_midi(0xB0u | c, 93, proj.chorus[c]);
    }
}

/* the QY70's default pattern (manual p.140): 2 bars of 4, D1/D2/PC bypass on the drum channel, BA bass,
 * C1/C2/C4 chord 1 and C3 chord 2 (a pad); vol/pan/rev/cho as the project's channel defaults. */
void pattern_empty(pattern_t *p)
{
    int i;
    memset(p, 0, sizeof *p);
    p->bars = 2;
    p->beats = 4;
    p->format = ARR_PAT_FORMAT;
    for (i = 0; i < ARR_TRACKS; i++) {
        arr_trk_t *t = &p->trk[i];
        t->x.lo = 0;
        t->x.hi = 127;
        t->x.hikey = XT_NOKEY;
        t->vol = 100;
        t->pan = 64;
        t->rev = 40;
        t->cho = 0;
    }
    for (i = 0; i < 3; i++) {                        /* D1, D2, PC */
        p->trk[i].chan = 9;
        p->trk[i].x.type = XT_BYPASS;
    }
    p->trk[3].program = 33;                          /* BA */
    p->trk[3].chan = 1;
    p->trk[3].x.type = XT_BASS;
    p->trk[3].x.lo = 28;
    p->trk[3].x.hi = 55;
    p->trk[3].x.hikey = 7;
    for (i = 4; i < ARR_TRACKS; i++) {                /* C1..C4 */
        p->trk[i].chan = (uint8_t)(i - 2);
        p->trk[i].x.type = XT_CHORD1;
        p->trk[i].x.lo = 48;
        p->trk[i].x.hi = 84;
    }
    p->trk[6].x.type = XT_CHORD2;                     /* C3: a pad */
    p->trk[6].program = 48;
}

/* its format (480 ticks a beat: older ones are not loaded), and every field that indexes or sizes something in
 * range, so ev[] is never read out of bounds */
static int pattern_valid(const pattern_t *p)
{
    int t;
    if (p->format != ARR_PAT_FORMAT || p->bars < 1u || p->bars > ARR_MAXBARS || p->beats < 2u || p->beats > 7u ||
        p->nev > ARR_NEV)
        return 0;
    for (t = 0; t < ARR_TRACKS; t++) {
        const arr_trk_t *k = &p->trk[t];
        if (k->x.type >= NXT || k->count > ARR_NEV || k->first + (uint32_t)k->count > p->nev)
            return 0;
    }
    return 1;
}

int project_pattern_load(int i, pattern_t *p)
{
    int n;
    if (i < 0 || i >= ARR_USER_PATTERNS) {
        pattern_empty(p);
        return -1;
    }
    n = plat_store_load((uint32_t)(OBJ_PAT0 + i), p, sizeof *p);
    if (n == (int)sizeof *p && pattern_valid(p))
        return 0;
    pattern_empty(p);
    return -1;
}

int project_pattern_save(int i, const pattern_t *p)
{
    if (i < 0 || i >= ARR_USER_PATTERNS)
        return -1;
    return plat_store_save((uint32_t)(OBJ_PAT0 + i), p, sizeof *p);
}

/* an empty song: no rows yet, Intro and Ending on, 120 BPM, on the first factory style (its six sections). A
 * section gets a user pattern of its own when it is first recorded into or edited (ui.c, slot_own) */
void song_default(song_t *g, int n)
{
    (void)n;
    memset(g, 0, sizeof *g);
    for (int i = 0; i < ARR_NSECT; i++)
        g->style[i] = (uint16_t)(ARR_FACTORY0 + i);
    g->sty = ARR_FACTORY0;
    loops_song_clear(g);
    g->tempo = (uint8_t)arr_style_tempo(0);
}
static void songs_defaults(songs_t *s)
{
    memset(s, 0, sizeof *s);
    s->magic = SONGS_MAGIC;
    s->format = SONGS_FORMAT;
    for (int i = 0; i < 8; i++)
        loop_from_template(&s->loop[i], FACTORY_LOOP[i]);
    for (int i = 8; i < ARR_LOOPS; i++)
        loop_clear(&s->loop[i], i);
    for (int n = 0; n < ARR_SONGS; n++)
        song_default(&s->song[n], n);
}

/* a user pattern, a factory style's section, or a QY70 bank style's (kept when the bank is missing: it plays
 * nothing until one is installed) */
int song_style_valid(int st)
{
    return (st >= 0 && st < ARR_USER_PATTERNS) || (st >= ARR_FACTORY0 && st < ARR_FACTORY0 + ARR_NSECT * arr_styles()) ||
           (st >= ARR_BANK0 && st < ARR_PATTERNS);
}

/* the style (its Intro's pattern number) pattern number st is a section of; -1: a user pattern, or none */
int song_style_base(int st)
{
    if (st >= ARR_FACTORY0 && st < ARR_FACTORY0 + ARR_NSECT * arr_styles())
        return st - (st - ARR_FACTORY0) % ARR_NSECT;
    if (st >= ARR_BANK0 && st < ARR_PATTERNS)
        return st - (st - ARR_BANK0) % ARR_NSECT;
    return -1;
}
/* the song's style from the sections playing one: Main A's, else the first's; none: as it was (all six on user
 * patterns keep the style they had, for the sections put back to it) */
void song_style_derive(song_t *g)
{
    int b = song_style_base(g->style[SEC_A - 1]);
    for (int k = 0; k < ARR_NSECT && b < 0; k++)
        b = song_style_base(g->style[k]);
    if (b >= 0)
        g->sty = (uint16_t)b;
}

/* a stored object of another format (or none, or damaged): the defaults (16 loops and songs of rows are new
 * in format 6; Polly is unreleased, so no migration). Else every value that indexes or sizes something kept
 * in range: a loop with beats or bars out of bounds cleared; a song's tempo and transpose defaulted; its
 * rows' loop, sect, rep and key clamped, and nrows at most ARR_ROWS. */
int project_songs_load(songs_t *s)
{
    int n = plat_store_load(OBJ_SONGS, s, sizeof *s);
    if (n != (int)sizeof *s || s->magic != SONGS_MAGIC || s->format != SONGS_FORMAT) {
        songs_defaults(s);
        return -1;
    }
    for (int i = 0; i < ARR_LOOPS; i++) {
        loop_t *l = &s->loop[i];
        if (l->beats < 2u || l->beats > 7u || l->bars > loop_max_bars(l->beats))
            loop_clear(l, i);
    }
    for (int i = 0; i < ARR_SONGS; i++) {
        song_t *g = &s->song[i];
        for (int k = 0; k < ARR_NSECT; k++)          /* a pattern this firmware lacks: the first factory style's */
            if (!song_style_valid(g->style[k]))
                g->style[k] = (uint16_t)(ARR_FACTORY0 + k);
        if (song_style_base(g->sty) != g->sty)       /* a style this firmware lacks: from the sections, else the first */
            g->sty = ARR_FACTORY0;
        song_style_derive(g);
        if (g->nrows > ARR_ROWS)
            g->nrows = ARR_ROWS;
        if (g->tempo < 40u || g->tempo > 240u)
            g->tempo = 120;
        if (g->transpose < -6 || g->transpose > 5)
            g->transpose = 0;
        for (int k = 0; k < ARR_ROWS; k++) {
            row_t *r = &g->row[k];
            if (r->loop >= ARR_LOOPS)
                r->loop = 0;
            if (r->sect >= ROW_NSECT)
                r->sect = ROW_A;
            if (r->rep < 1u || r->rep > 8u)
                r->rep = 1;
            if (r->key < -6 || r->key > 5)
                r->key = 0;
        }
    }
    return 0;
}

int project_songs_save(const songs_t *s) { return plat_store_save(OBJ_SONGS, s, sizeof *s); }
