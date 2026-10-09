/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's pages: the state the input layer (ui.c) and the screen share, and the drawing. Part of the unity
 * build, after project.c and before ui.c; the host simulator includes it too.
 *
 * The screen: a header (the page, the tempo, CHORD and REC), the main band, and the four knobs' strip. Each
 * band is drawn only when what it shows changed (a hash of it). Pages not built yet show their name. */
#include "gm_names.h"
#include "../dsp/templates.h"

#define K_BG RGB(250, 244, 232)
#define K_PANEL RGB(236, 228, 212)
#define K_LINE RGB(216, 205, 186)
#define K_DIM RGB(150, 136, 118)
#define K_TEXT RGB(72, 58, 48)
#define K_ACCENT RGB(242, 120, 72)
#define K_TEAL RGB(38, 166, 154)
#define K_TEAL_T RGB(196, 230, 222)
#define K_AMBER RGB(226, 166, 60)

enum { P_SONG, P_PATTERN, P_PATSET, P_TRACK, P_TRACK2, P_MIXER, P_MASTER, P_CHORDS, P_ARRANGE, P_TEMPLATES,
       P_TCOPY, P_SETUP, P_PANEL, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Song", "Pattern", "Pattern setup", "Track", "Track keys", "Mixer",
                                              "Master FX", "Chords", "Arrange", "Templates", "Copy template", "Setup",
                                              "Panel"};
static const char *const TRACK_NAME[ARR_TRACKS] = {"D1", "D2", "PC", "BA", "C1", "C2", "C3", "C4"};
static const char *const SECT_NAME[5] = {"", "Main A", "Main B", "Fill", "Ending"};
static const char *const KEY_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const GRID_NAME[ARR_NQ] = {"8", "8T", "16", "16T", "32", "off"};
static const char *const TYPE_NAME[NXT] = {"Bypass", "Parallel", "Chord 1", "Chord 2", "Bass"};
static const char *const TYPE_SHORT[NXT] = {"Bypass", "Parall.", "Chord1", "Chord2", "Bass"};
static const char *const REV_NAME[5] = {"Off", "Large hall", "Hall", "Chamber", "Room"};
static const char *const REV_SHORT[5] = {"off", "L.hall", "Hall", "Chamb.", "Room"};
static const char *const JOB_NAME[3] = {"Erase track", "Copy pattern", "Clear pattern"};
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#";
#define NNAME_CHARS ((int)sizeof NAME_CHARS - 1)
enum { KM_NONE, KM_PLAY, KM_CHORD };                 /* what a held key does */

static songs_t songs EN_POOL;                        /* the four songs (the selected one plays) */
static pattern_t scratch EN_POOL;                    /* a pattern being loaded, saved or edited */

/* the app's state that outlives the screen (ui_init runs again after an update session) */
static struct {
    uint8_t dirty;                                   /* the project changed since it was saved */
    uint8_t song_on;                                 /* the selected song is playing its tracks */
    int8_t tmpl;                                     /* the template last picked, -1 none */
    /* the four slots' patterns (Main A, Main B, Fill, Ending) as the app knows them: the tracks' settings and
     * the pattern's length and name (not its notes). Edits go here first, then into the arranger (ui.c) */
    arr_trk_t trk[ARR_NSECT][ARR_TRACKS];
    struct { char name[8]; uint8_t bars, beats; } hdr[ARR_NSECT];
    uint8_t edited[ARR_NSECT];                       /* edits stored into the slot, not saved yet */
    uint8_t edit_pend;                               /* slots with edits not yet stored into the arranger (bits) */
    uint32_t edit_t;                                 /* when the first of them was made */
    uint32_t gen;                                    /* counts the app's loads and stores (the Pattern page's view) */
    uint8_t songs_dirty;                             /* the songs object changed (a slot's pattern) */
    uint16_t saved[ARR_NSECT];                       /* each slot's arr_dirty when it was last loaded or saved */
    uint16_t seen[ARR_NSECT];                        /* each slot's arr_dirty as last seen, and when it moved: */
    uint32_t take_t[ARR_NSECT];                      /* the slot's latest take */
    uint8_t song_pend;                               /* a song picked while playing, + 1: loaded at the stop */
    uint32_t chg;                                    /* counts changes (the project's, the takes) */
    uint8_t save_err;                                /* the last save failed at change count err_chg: */
    uint32_t err_chg;                                /* autosave waits for another change */
} app;

/* the input's and the screen's state */
static struct {
    uint32_t btn, keys, btn_used, btn_t[NB];
    uint32_t enc_t[NE];
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint8_t page;
    uint8_t chord;                                   /* CHORD mode: the keys enter chords */
    uint8_t stopped;                                 /* frames since the transport stopped */
    uint8_t job, job_sel, job_to;                    /* the jobs open (SAVE held), the one picked, the copy's pattern */
    uint8_t name_cur;                                /* the pattern name's letter edited */
    uint32_t wait_t;                                 /* a queued song change: since when it waits for silence */
    uint8_t kmode[NKEYS], knote[NKEYS];              /* each held key: KM_*, its note */
    uint32_t sig[3];
} ui;

/* anything not saved: the project, or a user pattern recorded into */
static int unsaved(void)
{
    if (app.dirty || app.songs_dirty)
        return 1;
    for (int s = 0; s < ARR_NSECT; s++)
        if (songs.song[proj.song].style[s] < ARR_USER_PATTERNS &&
            (arr_dirty[s] != app.saved[s] || app.edited[s] || (app.edit_pend >> s & 1u)))
            return 1;
    return 0;
}

/* ------------------------------------------------------------ helpers --- */
static char *itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *cat_s(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
/* s cut to fit w pixels */
static int32_t text_fit(int32_t x, int32_t y, const felucca_font_t *f, const char *s, int32_t w, uint16_t c)
{
    char b[28];
    uint32_t n;
    for (n = 0; n < sizeof b - 1u && s[n]; n++)
        b[n] = s[n];
    b[n] = 0;
    while (n && text_w(f, b) > w)
        b[--n] = 0;
    return cv_text(x, y, f, b, c);
}
static int upper_only(const char *s)
{
    while (*s)
        if ((uint8_t)*s++ > 95u)
            return 0;
    return 1;
}
static uint16_t sect_colour(int s)
{
    return s == SEC_A ? K_TEAL : s == SEC_B ? K_ACCENT : s == SEC_FILL ? K_AMBER : K_DIM;
}
/* a pill with its text, w 0: as wide as the text */
static int32_t pill(int32_t x, int32_t y, int32_t w, const char *s, uint16_t bg, uint16_t fg)
{
    if (!w)
        w = text_w(&FONT_S, s) + 16;
    cv_round(x, y, w, 20, 6, bg);
    text_c(x + w / 2, y + 2, &FONT_S, s, fg);
    return x + w;
}

/* the engine's published place, read as a consistent set (the render may move it between two reads: read
 * again until two readings agree) */
typedef struct {
    int tmpl;                                        /* the template playing, -1 none */
    uint32_t tbar, bar, beat;                        /* its bar; the bar (the song's, with a song) and beat */
    uint32_t chord;                                  /* the chord now, packed */
} place_t;
static void place_read(place_t *p)
{
    place_t q;
    do {
        q.tmpl = arr_template_now;
        q.tbar = arr_template_bar;
        q.bar = app.song_on ? arr_song_bar : arr_bar;
        q.beat = arr_beat_tick / ARR_PPQ;
        q.chord = arr_chord_now;
        p->tmpl = arr_template_now;
        p->tbar = arr_template_bar;
        p->bar = app.song_on ? arr_song_bar : arr_bar;
        p->beat = arr_beat_tick / ARR_PPQ;
        p->chord = arr_chord_now;
    } while (p->tmpl != q.tmpl || p->tbar != q.tbar || p->bar != q.bar || p->beat != q.beat ||
             p->chord != q.chord);
    if (p->tmpl >= NTEMPLATES)
        p->tmpl = -1;
}

/* the chord after the one now: the template's next bar, or the song's next change; 0: none known */
static int next_chord(const place_t *pl, chord_t *out)
{
    int t = pl->tmpl;
    if (t >= 0) {
        const template_t *m = &TEMPLATES[t];
        *out = m->bars[(pl->tbar + 1u) % m->nbars];
        out->root = (uint8_t)((out->root + proj.key) % 12u);
        return 1;
    }
    if (app.song_on) {
        const song_t *s = &songs.song[proj.song];
        uint32_t now = pl->chord, b = pl->bar * 4u + pl->beat, n = s->bars * 4u, i;
        for (i = 1; i <= n; i++) {
            chord_t c = s->chord[(b + i) % n];
            if (c.type < CH_NTYPES && c.root < 12u && arr_chord_pack(c) != now) {
                *out = c;
                return 1;
            }
        }
    }
    return 0;
}

static const char *trk_sound(const arr_trk_t *t)
{
    return (t->chan & 15u) == 9u ? "Drums" : GM_PROGRAM[t->program & 127u];
}
/* "C#4" (C4 = 60) */
static char *note_name(int n, char *b)
{
    static const char *const N[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    char *e = cat_s(b, N[n % 12]);
    if (n < 12)
        return cat_s(e, "-1");
    return itoa_u((uint32_t)(n / 12 - 1), e);
}

/* ------------------------------------------------------------- header --- */
#define HDR_H 24
#define MAIN_Y 24
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

static void draw_header(void)
{
    char t[24], *e;
    int msg = plat_ms() < ui.msg_until, tn = arr_template_now, rec = arr_recording, ext = arr_ext;
    uint32_t h = hash(2166136261u, ui.page | (uint32_t)msg << 8 | (uint32_t)unsaved() << 9 | (uint32_t)ui.chord << 10 |
                                       (uint32_t)rec << 11 | (uint32_t)ext << 12 | (uint32_t)proj.tempo << 16);
    h = hash(h, (uint32_t)(uint8_t)tn | (uint32_t)app.song_on << 8 | (uint32_t)proj.song << 9);
    if (msg)
        h = hash(h, ui.msg_until);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(8, 4, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 4, &FONT_B, ui.msg[1], K_ACCENT);
    } else {
        int32_t x = cv_text(8, 4, &FONT_B, PAGE_NAME[ui.page], K_TEXT), r = 232;
        if (ui.page == P_SONG) {
            e = cat_s(t, " ");
            itoa_u(proj.song + 1u, e);
            x = cv_text(x, 4, &FONT_B, t, K_TEXT);
            if (tn >= 0 && tn < NTEMPLATES)
                text_fit(x + 8, 6, &FONT_XS, TEMPLATES[tn].name, 84 - (x - 64), K_DIM);
        }
        if (unsaved())
            cv_round(x + 4, 10, 5, 5, 2, K_DIM);
        if (ext)
            cat_s(t, "ext");
        else
            itoa_u(proj.tempo, t);
        r -= text_w(&FONT_S, t);
        cv_text(r, 4, &FONT_S, t, K_TEXT);
        if (rec) {
            r -= 14;
            cv_round(r, 8, 9, 9, 4, RGB(214, 64, 52));
        }
        if (ui.chord) {
            int32_t w = text_w(&FONT_XS, "CHORD") + 10;
            r -= w + 6;
            cv_round(r, 4, w, 16, 5, K_TEAL);
            text_c(r + w / 2, 5, &FONT_XS, "CHORD", K_BG);
        }
    }
    cv_blit(0, 0);
}

/* ---------------------------------------------------------- Song page --- */
static void draw_song(void)
{
    uint32_t h, now = arr_ticks, lit = 0, i;
    int sect = arr_sect, next = arr_next_sect, play = arr_playing, blink = (plat_ms() / 300u) & 1u;
    uint32_t slot = (uint32_t)(sect - 1) & 3u, bar, beat;
    place_t pl;
    chord_t nc;
    int has_next;
    char t[24];
    place_read(&pl);
    bar = pl.bar;
    beat = pl.beat;
    has_next = next_chord(&pl, &nc);
    h = hash(2166136261u, pl.chord);
    for (i = 0; i < ARR_TRACKS; i++) {
        uint32_t hit = arr_track_hit[i];
        if (hit && now - hit < 8u)
            lit |= 1u << i;
    }
    h = hash(h, (uint32_t)sect | (uint32_t)next << 4 | (uint32_t)play << 8 | (uint32_t)(next ? blink : 0) << 9 |
                    lit << 10 | (uint32_t)proj.trk << 18 | (uint32_t)ui.chord << 21 | (uint32_t)app.song_on << 22);
    h = hash(h, bar << 8 | beat | (uint32_t)(uint8_t)pl.tmpl << 16 | pl.tbar << 24);
    h = hash(h, has_next ? arr_chord_pack(nc) : 0xFFFFFFu);
    for (i = 0; i < ARR_TRACKS; i++)
        h = hash(h, app.trk[slot][i].program | (uint32_t)app.trk[slot][i].chan << 8 | (uint32_t)app.trk[slot][i].mute << 12);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);

    {   /* the section, the one waiting (NEXT), and where we are */
        int32_t x = pill(8, 2, 0, SECT_NAME[sect >= SEC_A && sect <= SEC_END ? sect : SEC_A], sect_colour(sect), K_BG);
        if (next >= SEC_A && next <= SEC_END) {
            cv_text(x + 8, 5, &FONT_XS, "NEXT", blink ? K_ACCENT : K_DIM);
            x = pill(x + 8 + text_w(&FONT_XS, "NEXT") + 6, 2, 0, SECT_NAME[next], blink ? sect_colour(next) : K_PANEL,
                     blink ? K_BG : K_TEXT);
        }
        itoa_u(bar + 1u, t);
        {
            char *e = t;
            while (*e)
                e++;
            *e++ = '.';
            itoa_u(beat + 1u, e);
        }
        cv_text(232 - text_w(&FONT_M, t), 0, &FONT_M, t, play ? K_TEXT : K_DIM);
    }
    {   /* the chord now, big; the next one */
        chord_name(arr_chord_unpack(pl.chord), t);
        cv_text(10, 28, &FONT_L, t, ui.chord ? K_TEAL : K_TEXT);
        cv_text(160, 30, &FONT_XS, "next", K_DIM);
        if (has_next) {
            chord_name(nc, t);
            cv_text(160, 46, &FONT_B, t, K_DIM);
        } else
            cv_text(160, 48, &FONT_S, ui.chord ? "the keys" : "-", K_LINE);
    }
    {   /* the progression: the template's bars, or the song's, or a hint */
        int32_t y = 72;
        int t_now = pl.tmpl;
        if (t_now >= 0) {
            const template_t *m = &TEMPLATES[t_now];
            int32_t n = m->nbars, w = 224 / n;
            for (i = 0; i < (uint32_t)n; i++) {
                chord_t c = m->bars[i];
                int on = i == pl.tbar;
                c.root = (uint8_t)((c.root + proj.key) % 12u);
                chord_name(c, t);
                cv_round(8 + (int32_t)i * w, y, w - 2, 18, 4, on ? K_TEAL : K_PANEL);
                if (text_w(&FONT_XS, t) <= w - 4)
                    text_c(8 + (int32_t)i * w + (w - 2) / 2, y + 1, &FONT_XS, t, on ? K_BG : K_TEXT);
            }
        } else if (app.song_on) {
            const song_t *s = &songs.song[proj.song];
            uint32_t b0 = bar / 8u * 8u, sec = SEC_A;
            for (i = 0; i <= b0 + 7u && i < s->bars; i++) {
                if (s->sect[i] >= SEC_A && s->sect[i] <= SEC_END)
                    sec = s->sect[i];
                if (i >= b0) {
                    int32_t x = 8 + (int32_t)(i - b0) * 28;
                    cv_round(x, y, 26, 18, 4, i == bar ? sect_colour((int)sec) : K_PANEL);
                    if (s->chord[i * 4u].type < CH_NTYPES) {
                        chord_name(s->chord[i * 4u], t);
                        if (text_w(&FONT_XS, t) <= 24)
                            text_c(x + 13, y + 1, &FONT_XS, t, i == bar ? K_BG : K_TEXT);
                    }
                }
            }
        } else
            text_c(120, y + 1, &FONT_XS, ui.chord ? "Hold a chord on the keys" : "SEL + a white key: a progression",
                   K_DIM);
    }
    for (i = 0; i < ARR_TRACKS; i++) {   /* the eight tracks: lit as they play, with their sounds */
        int32_t x = 6 + (int32_t)(i / 4u) * 116, y = 96 + (int32_t)(i % 4u) * 12;
        int on = (int)(lit >> i & 1u), sel = i == proj.trk, mute = app.trk[slot][i].mute;
        uint16_t fg = on ? K_BG : sel ? K_ACCENT : mute ? K_LINE : K_TEXT;
        const char *name = trk_sound(&app.trk[slot][i]);
        if (on)
            cv_round(x, y, 112, 12, 3, sel ? K_ACCENT : K_TEAL);
        else if (sel)
            cv_round(x, y, 112, 12, 3, K_PANEL);
        cv_text(x + 4, y - 2, &FONT_XS, TRACK_NAME[i], on ? K_BG : K_DIM);
        text_fit(x + 26, y - 2, &FONT_XS, mute ? "muted" : name, 84, fg);
    }
    cv_blit(0, MAIN_Y);
}

/* a page still to come: its name */
static void draw_coming(void)
{
    uint32_t h = hash(2166136261u, 256u + ui.page);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(16, 24, 208, 96, 6, K_PANEL);
    text_c(120, 44, &FONT_L, PAGE_NAME[ui.page], K_TEXT);
    text_c(120, 86, &FONT_S, "coming", K_DIM);
    cv_blit(0, MAIN_Y);
}


/* ------------------------------------------------- the pattern pages --- */
static pattern_t view EN_POOL;                       /* the pattern drawn (its notes), copied when it changed */
static struct { uint8_t slot; uint16_t dirty; uint32_t gen; uint8_t ok; } vw;
static int pslot(void) { return (arr_sect - 1) & 3; }   /* the slot the pattern pages show: the section playing */
static void view_refresh(int s)
{
    uint16_t d = arr_dirty[s];
    if (vw.ok && vw.slot == s && vw.dirty == d && vw.gen == app.gen)
        return;
    vw.dirty = arr_pattern_copy(s, &view);
    vw.slot = (uint8_t)s;
    vw.gen = app.gen;
    vw.ok = 1;
}
/* "P05 VERSE", the pattern's number and name */
static void pat_title(int s, char *t)
{
    int st = songs.song[proj.song].style[s], i;
    char *e = t;
    if (st < ARR_USER_PATTERNS) {
        *e++ = 'P';
        *e++ = (char)('0' + (st + 1) / 10);
        *e++ = (char)('0' + (st + 1) % 10);
    } else
        e = cat_s(e, "Style");
    *e++ = ' ';
    for (i = 0; i < 8 && app.hdr[s].name[i]; i++)
        *e++ = app.hdr[s].name[i];
    while (e > t && e[-1] == ' ')
        e--;
    *e = 0;
}
static void pattern_head(int s, uint32_t *h)
{
    char t[24];
    int32_t x = pill(8, 0, 0, SECT_NAME[s + 1], sect_colour(s + 1), K_BG);
    (void)h;
    pat_title(s, t);
    cv_text(x + 8, 2, &FONT_B, t, K_TEXT);
    {                                            /* "2 bars, 4/4, CM7": its length and source chord */
        char *e = t;
        *e++ = (char)('0' + app.hdr[s].bars);
        e = cat_s(e, app.hdr[s].bars == 1 ? " bar, " : " bars, ");
        *e++ = (char)('0' + app.hdr[s].beats);
        cat_s(e, "/4, CM7");
    }
    cv_text(232 - text_w(&FONT_XS, t), 3, &FONT_XS, t, K_DIM);
}
/* the 8 tracks as mini piano rolls over the pattern's length, the playhead, the mutes */
static void draw_pattern(void)
{
    int s = pslot(), play = arr_playing, i;
    uint32_t len = (uint32_t)app.hdr[s].bars * app.hdr[s].beats * ARR_PPQ, h, pos, bar, bt;
    const int32_t X0 = 26, W = 208;
    int32_t ph;
    view_refresh(s);
    do {
        bar = arr_bar;
        bt = arr_beat_tick;
    } while (bar != arr_bar);
    pos = bar * app.hdr[s].beats * ARR_PPQ + bt;
    if (!len)
        len = 1;
    ph = play && pos < len ? X0 + (int32_t)(pos * (uint32_t)W / len) : -1;
    h = hash(hash(2166136261u, (uint32_t)s | (uint32_t)proj.trk << 4 | (uint32_t)(ph + 1) << 8), vw.dirty);
    h = hash(hash(h, vw.gen), arr_recording);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    pattern_head(s, &h);
    for (i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *tr = &view.trk[i];
        int32_t y = 22 + i * 15, b;
        int sel = i == proj.trk, mute = app.trk[s][i].mute;
        uint32_t n, e = (uint32_t)tr->first + tr->count, lo = 127, hi = 0;
        cv_text(4, y - 2, &FONT_XS, TRACK_NAME[i], sel ? K_ACCENT : mute ? K_LINE : K_DIM);
        cv_round(X0, y, W, 13, 3, sel ? (arr_recording ? RGB(246, 214, 204) : K_TEAL_T) : K_PANEL);
        for (b = 1; b < app.hdr[s].bars; b++)  /* bar lines */
            cv_rect(X0 + b * W / app.hdr[s].bars, y + 1, 1, 11, K_LINE);
        if (e > view.nev)
            e = view.nev;
        for (n = tr->first; n < e; n++) {
            if (view.ev[n].note < lo)
                lo = view.ev[n].note;
            if (view.ev[n].note > hi)
                hi = view.ev[n].note;
        }
        for (n = tr->first; n < e; n++) {
            const arr_ev_t *v = &view.ev[n];
            int32_t x, w, yy;
            if (v->tick >= len)
                continue;                        /* past a shortened length: kept, not played */
            x = X0 + (int32_t)((uint32_t)v->tick * (uint32_t)W / len);
            w = (int32_t)((uint32_t)v->len * (uint32_t)W / len);
            w = w < 2 ? 2 : w > X0 + W - x ? X0 + W - x : w;
            yy = y + 8 - (hi > lo ? (int32_t)((v->note - lo) * 6u / (hi - lo)) : 3);
            cv_rect(x, yy, w, 3, mute ? K_DIM : i < 3 ? K_ACCENT : K_TEAL);
        }
        if (mute)
            cv_text(X0 + W - text_w(&FONT_XS, "muted") - 4, y - 2, &FONT_XS, "muted", K_DIM);
    }
    if (ph >= 0)
        cv_rect(ph, 20, 1, 8 * 15, K_ACCENT);
    cv_blit(0, MAIN_Y);
}
/* the pattern's length, beats and name */
static void draw_patset(void)
{
    int s = pslot(), i;
    uint32_t h = hash(2166136261u, (uint32_t)s | (uint32_t)ui.name_cur << 4 | (uint32_t)app.hdr[s].bars << 8 |
                                       (uint32_t)app.hdr[s].beats << 12);
    char t[24];
    for (i = 0; i < 8; i++)
        h = hash(h, (uint8_t)app.hdr[s].name[i]);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    pattern_head(s, &h);
    itoa_u(app.hdr[s].bars, t);
    cv_text(16, 22, &FONT_L, t, K_TEXT);
    cv_text(16, 56, &FONT_XS, app.hdr[s].bars == 1 ? "bar" : "bars", K_DIM);
    itoa_u(app.hdr[s].beats, t);
    cv_text(84, 22, &FONT_L, t, K_TEXT);
    cv_text(84, 56, &FONT_XS, "beats a bar", K_DIM);
    cv_round(12, 94, 216, 46, 6, K_PANEL);
    for (i = 0; i < 8; i++) {                    /* the name, a box a letter */
        int32_t x = 18 + i * 26;
        char c[2] = {app.hdr[s].name[i] ? app.hdr[s].name[i] : ' ', 0};
        if (i == ui.name_cur)
            cv_round(x, 99, 22, 32, 4, K_TEAL_T);
        text_c(x + 11, 101, &FONT_M, c, K_TEXT);
        cv_rect(x + 3, 129, 16, 1, i == ui.name_cur ? K_ACCENT : K_LINE);
    }
    cv_text(16, 78, &FONT_XS, "name", K_DIM);
    cv_blit(0, MAIN_Y);
}
/* the selected track's sound and transposition */
static void draw_track(void)
{
    int s = pslot(), k2 = ui.page == P_TRACK2;
    const arr_trk_t *t = &app.trk[s][proj.trk];
    uint32_t h = hash(2166136261u, (uint32_t)s | (uint32_t)proj.trk << 4 | (uint32_t)k2 << 8);
    char a[16], b[16];
    h = hash(h, t->program | (uint32_t)t->chan << 8 | (uint32_t)t->x.type << 16 | (uint32_t)t->x.hikey << 24);
    h = hash(h, t->x.lo | (uint32_t)t->x.hi << 8);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    {
        int32_t x = pill(8, 0, 0, TRACK_NAME[proj.trk], K_TEAL, K_BG);
        cv_text(x + 8, 2, &FONT_B, TYPE_NAME[t->x.type % NXT], K_DIM);
    }
    if ((t->chan & 15u) == 9u) {
        cv_text(10, 28, &FONT_L, "Drum kit", K_TEXT);
        if (!k2)
            cv_text(10, 64, &FONT_S, "Channel 10: a sound on each key", K_DIM);
    } else {
        a[0] = (char)('0' + (t->program + 1) / 100);
        a[1] = (char)('0' + (t->program + 1) / 10 % 10);
        a[2] = (char)('0' + (t->program + 1) % 10);
        a[3] = 0;
        cv_text(10, 28, &FONT_L, a, K_ACCENT);
        if (text_w(&FONT_L, GM_PROGRAM[t->program & 127u]) <= 160)
            cv_text(70, 28, &FONT_L, GM_PROGRAM[t->program & 127u], K_TEXT);
        else
            cv_text(70, 36, &FONT_B, GM_PROGRAM[t->program & 127u], K_TEXT);
        if (!k2)
            cv_text(10, 64, &FONT_S, GM_FAMILY[(t->program & 127u) / 8u], K_DIM);
    }
    {   /* the range over the whole MIDI keyboard */
        int32_t x0 = 10, w = 220, y = 96, lx = x0 + t->x.lo * w / 127, hx = x0 + t->x.hi * w / 127;
        cv_round(x0, y, w, 10, 4, K_PANEL);
        cv_round(lx, y, hx - lx < 4 ? 4 : hx - lx, 10, 4, K_TEAL);
        note_name(t->x.lo, a);
        note_name(t->x.hi, b);
        cv_text(x0, y + 12, &FONT_XS, a, K_DIM);
        cv_text(x0 + w - text_w(&FONT_XS, b), y + 12, &FONT_XS, b, K_DIM);
        cv_text(120 - text_w(&FONT_XS, "range") / 2, y + 12, &FONT_XS, "range", K_LINE);
    }
    if (k2) {
        char *e = cat_s(a, "high key ");
        if (t->x.hikey >= XT_NOKEY)
            cat_s(e, "off");
        else {
            note_name(t->x.hikey, e);
            e[(e[1] == '#' || e[1] == 'b') ? 2 : 1] = 0;   /* the pitch class only */
        }
        cv_text(10, 64, &FONT_S, a, K_TEXT);
        e = cat_s(b, "channel ");
        itoa_u((t->chan & 15u) + 1u, e);
        cv_text(230 - text_w(&FONT_S, b), 64, &FONT_S, b, K_TEXT);
    }
    cv_blit(0, MAIN_Y);
}
/* the 8 tracks' strips: volume, pan, the sends; the selected one outlined */
static void draw_mixer(void)
{
    int s = pslot(), i;
    uint32_t h = hash(2166136261u, (uint32_t)s | (uint32_t)proj.trk << 4);
    for (i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *t = &app.trk[s][i];
        h = hash(h, t->vol | (uint32_t)t->pan << 8 | (uint32_t)t->rev << 16 | (uint32_t)t->cho << 24);
        h = hash(h, t->mute);
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *t = &app.trk[s][i];
        int32_t x = 4 + i * 29, w = 27, sel = i == proj.trk, vh = 76 * t->vol / 127;
        uint16_t c = t->mute ? K_LINE : i < 3 ? K_ACCENT : K_TEAL;
        cv_round(x, 0, w, 144, 5, sel ? K_TEAL_T : K_PANEL);
        text_c(x + w / 2, 0, &FONT_XS, TRACK_NAME[i], sel ? K_TEXT : K_DIM);
        cv_round(x + 8, 20, 11, 78, 4, K_BG);   /* volume */
        if (vh > 0)
            cv_round(x + 8, 20 + 78 - vh - 1, 11, vh, 4, c);
        cv_rect(x + 3, 106, w - 6, 2, K_LINE);  /* pan */
        cv_round(x + 3 + (w - 10) * t->pan / 127, 103, 4, 8, 2, K_TEXT);
        {                                        /* the sends: R reverb, C chorus */
            char r[6];
            itoa_u(t->rev, cat_s(r, "R"));
            text_c(x + w / 2, 112, &FONT_XS, r, K_DIM);
            itoa_u(t->cho, cat_s(r, "C"));
            text_c(x + w / 2, 126, &FONT_XS, r, K_DIM);
        }
    }
    cv_blit(0, MAIN_Y);
}
static void draw_master(void)
{
    uint32_t h = hash(2166136261u, proj.rev_type | (uint32_t)proj.rev_level << 8 | (uint32_t)proj.cho_type << 16 |
                                       (uint32_t)proj.cho_level << 24);
    char t[12];
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(8, 4, 224, 64, 6, K_PANEL);
    cv_text(18, 8, &FONT_XS, "Reverb", K_DIM);
    cv_text(18, 24, &FONT_L, REV_NAME[proj.rev_type <= 4u ? proj.rev_type : 0], K_TEXT);
    cv_round(18, 58, 204, 5, 2, K_LINE);
    cv_round(18, 58, 204 * proj.rev_level / 127 + 2, 5, 2, K_TEAL);
    cv_round(8, 76, 224, 64, 6, K_PANEL);
    cv_text(18, 80, &FONT_XS, "Chorus", K_DIM);
    itoa_u(proj.cho_type + 1u, cat_s(t, "Chorus "));
    cv_text(18, 96, &FONT_L, t, K_TEXT);
    cv_round(18, 130, 204, 5, 2, K_LINE);
    cv_round(18, 130, 204 * proj.cho_level / 127 + 2, 5, 2, K_TEAL);
    cv_blit(0, MAIN_Y);
}
/* the jobs (SAVE held) */
static void draw_jobs(void)
{
    int s = pslot(), i;
    uint32_t h = hash(2166136261u, 512u + ui.job_sel + ((uint32_t)ui.job_to << 4) + ((uint32_t)proj.trk << 8) +
                                       ((uint32_t)s << 12));
    char t[24], *e;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    pattern_head(s, &h);
    for (i = 0; i < 3; i++) {
        int on = i == ui.job_sel;
        int32_t y = 26 + i * 30;
        cv_round(8, y, 224, 26, 6, on ? K_TEAL : K_PANEL);
        cv_text(18, y + 4, &FONT_B, JOB_NAME[i], on ? K_BG : K_TEXT);
        if (i == 0)
            e = cat_s(t, TRACK_NAME[proj.trk]);
        else if (i == 1) {
            e = cat_s(t, "to P");
            *e++ = (char)('0' + (ui.job_to + 1) / 10);
            *e++ = (char)('0' + (ui.job_to + 1) % 10);
            *e = 0;
        } else
            e = cat_s(t, SECT_NAME[s + 1]);
        (void)e;
        cv_text(222 - text_w(&FONT_S, t), y + 4, &FONT_S, t, on ? K_BG : K_DIM);
    }
    text_c(120, 120, &FONT_XS, "SEL: do it      SAVE: cancel", K_DIM);
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------- knobs --- */
typedef struct {
    const char *name;
    char val[12];
    int32_t num, max;                                /* the bar: num of max (max 0: no bar) */
} knob_t;

static void song_knobs(knob_t *k)
{
    int t = arr_template_now;                        /* the template playing, or none ("-") */
    if (t >= NTEMPLATES)
        t = -1;
    k[0].name = "Key";
    cat_s(k[0].val, KEY_NAME[proj.key % 12u]);
    k[0].num = proj.key;
    k[0].max = 11;
    k[1].name = "Tempo";
    itoa_u(proj.tempo, k[1].val);
    k[1].num = proj.tempo - 40;
    k[1].max = 200;
    k[2].name = "Grid";
    cat_s(k[2].val, GRID_NAME[proj.quant % ARR_NQ]);
    k[2].num = ARR_NQ - 1 - proj.quant;
    k[2].max = ARR_NQ - 1;
    k[3].name = "Template";
    if (t >= 0)
        itoa_u((uint32_t)t + 1u, k[3].val);
    else
        cat_s(k[3].val, "-");
    k[3].num = t + 1;
    k[3].max = NTEMPLATES;
}

static void kv(knob_t *k, const char *name, const char *val, int32_t num, int32_t max)
{
    k->name = name;
    cat_s(k->val, val);
    k->num = num;
    k->max = max;
}
static void kn(knob_t *k, const char *name, int32_t v, int32_t max)
{
    k->name = name;
    itoa_u((uint32_t)v, k->val);
    k->num = v;
    k->max = max;
}
static void edit_knobs(knob_t *k)
{
    int s = pslot();
    const arr_trk_t *t = &app.trk[s][proj.trk];
    char b[8];
    switch (ui.page) {
    case P_PATTERN:
        kv(&k[0], "Grid", GRID_NAME[proj.quant % ARR_NQ], ARR_NQ - 1 - proj.quant, ARR_NQ - 1);
        kv(&k[3], "Track", t->mute ? "muted" : "plays", t->mute, 0);
        break;
    case P_PATSET:
        kn(&k[0], "Bars", app.hdr[s].bars, ARR_MAXBARS);
        kn(&k[1], "Beats", app.hdr[s].beats, 7);
        kn(&k[2], "Letter", ui.name_cur + 1, 8);
        b[0] = app.hdr[s].name[ui.name_cur] ? app.hdr[s].name[ui.name_cur] : ' ';
        b[1] = 0;
        kv(&k[3], "Char", b[0] == ' ' ? "_" : b, 0, 0);
        break;
    case P_TRACK:
        if ((t->chan & 15u) == 9u)
            kv(&k[0], "Program", "kit", 0, 0);
        else
            kn(&k[0], "Program", t->program + 1, 128);
        kv(&k[1], "Type", TYPE_SHORT[t->x.type % NXT], t->x.type, NXT - 1);
        note_name(t->x.lo, b);
        kv(&k[2], "Low", b, t->x.lo, 127);
        note_name(t->x.hi, b);
        kv(&k[3], "High", b, t->x.hi, 127);
        break;
    case P_TRACK2:
        if (t->x.hikey >= XT_NOKEY)
            kv(&k[0], "High key", "off", 0, 0);
        else {
            note_name(t->x.hikey, b);
            b[(b[1] == '#' || b[1] == 'b') ? 2 : 1] = 0;
            kv(&k[0], "High key", b, t->x.hikey, 11);
        }
        kn(&k[1], "Channel", (t->chan & 15) + 1, 16);
        break;
    case P_MIXER:
        kn(&k[0], "Volume", t->vol, 127);
        if (t->pan == 64)
            kv(&k[1], "Pan", "C", 64, 127);
        else {
            b[0] = t->pan < 64 ? 'L' : 'R';
            itoa_u((uint32_t)(t->pan < 64 ? 64 - t->pan : t->pan - 64), b + 1);
            kv(&k[1], "Pan", b, t->pan, 127);
        }
        kn(&k[2], "Reverb", t->rev, 127);
        kn(&k[3], "Chorus", t->cho, 127);
        break;
    case P_MASTER:
        kv(&k[0], "Reverb", REV_SHORT[proj.rev_type <= 4u ? proj.rev_type : 0], proj.rev_type, 4);
        kn(&k[1], "Rev lvl", proj.rev_level, 127);
        kn(&k[2], "Chorus", proj.cho_type + 1, 4);
        kn(&k[3], "Cho lvl", proj.cho_level, 127);
        break;
    default: break;
    }
}
static void job_knobs(knob_t *k)
{
    char b[4] = {'P', (char)('0' + (ui.job_to + 1) / 10), (char)('0' + (ui.job_to + 1) % 10), 0};
    kn(&k[0], "Job", ui.job_sel + 1, 3);
    kv(&k[1], "Copy to", b, ui.job_to, ARR_USER_PATTERNS - 1);
}

static void draw_knobs(void)
{
    knob_t k[4];
    uint32_t h = hash(2166136261u, ui.page | (uint32_t)ui.touched << 8 | (uint32_t)ui.job << 16), i;
    int now_touch = plat_ms() < ui.touch_until;
    memset(k, 0, sizeof k);
    if (ui.job)
        job_knobs(k);
    else if (ui.page == P_SONG)
        song_knobs(k);
    else if (ui.page <= P_MASTER)
        edit_knobs(k);
    for (i = 0; i < 4; i++) {
        const char *s = k[i].val;
        while (*s)
            h = hash(h, (uint8_t)*s++);
        h = hash(h, (uint32_t)k[i].num << 8 | (uint32_t)(k[i].name != 0));
    }
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_round(4, 2, 232, KNB_H - 6, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int32_t x = (int32_t)i * 60, cx = x + 30;
        int hot = now_touch && ui.touched == (int)i;
        if (i)
            cv_rect(x, 14, 1, KNB_H - 30, K_LINE);
        if (!k[i].name)
            continue;
        text_c(cx, 6, &FONT_XS, k[i].name, hot ? K_TEXT : K_DIM);
        if (upper_only(k[i].val))
            text_c(cx, 22, &FONT_M, k[i].val, hot ? K_ACCENT : K_TEXT);
        else                                     /* FONT_M has no lower case: "Bb", "off" */
            text_c(cx, 27, &FONT_B, k[i].val, hot ? K_ACCENT : K_TEXT);
        if (k[i].max > 0) {
            int32_t bw = 44 * k[i].num / k[i].max;
            cv_round(cx - 22, 50, 44, 6, 3, K_LINE);
            if (bw > 0)
                cv_round(cx - 22, 50, bw < 6 ? 6 : bw, 6, 3, hot ? K_ACCENT : K_TEAL);
        }
    }
    cv_blit(0, KNB_Y);
}

static void draw_page(void)
{
    if (ui.job)
        draw_jobs();
    else if (ui.page == P_SONG)
        draw_song();
    else if (ui.page == P_PATTERN)
        draw_pattern();
    else if (ui.page == P_PATSET)
        draw_patset();
    else if (ui.page == P_TRACK || ui.page == P_TRACK2)
        draw_track();
    else if (ui.page == P_MIXER)
        draw_mixer();
    else if (ui.page == P_MASTER)
        draw_master();
    else
        draw_coming();
}
