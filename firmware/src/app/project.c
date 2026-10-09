/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's project: one flash object (OBJ_PROJ): the keys' octave, the track, tempo, key and grid, and the
 * MIDI-in channels' program, volume, pan, reverb and chorus. Patterns and songs have their own objects.
 * Part of the unity build, after app.h. */
project_t proj;

void project_defaults(void)
{
    int c;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    proj.leds = 1;
    proj.trk = 4;                                    /* C1: a piano under the keys */
    proj.tempo = 120;
    proj.quant = ARR_Q_16;
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
        proj.trk >= ARR_TRACKS || proj.tempo < 40u || proj.tempo > 240u || proj.key > 11u || proj.quant >= ARR_NQ)
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

/* every field that indexes or sizes something is in range, so ev[] is never read out of bounds */
static int pattern_valid(const pattern_t *p)
{
    int t;
    if (p->bars < 1u || p->bars > ARR_MAXBARS || p->beats < 2u || p->beats > 7u || p->nev > ARR_NEV)
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

static void songs_defaults(songs_t *s)
{
    memset(s, 0, sizeof *s);
    s->magic = SONGS_MAGIC;
    s->format = SONGS_FORMAT;
}

int project_songs_load(songs_t *s)
{
    int n = plat_store_load(OBJ_SONGS, s, sizeof *s);
    if (n >= 8 && s->magic == SONGS_MAGIC && s->format == SONGS_FORMAT)
        return 0;
    songs_defaults(s);
    return -1;
}

int project_songs_save(const songs_t *s) { return plat_store_save(OBJ_SONGS, s, sizeof *s); }
