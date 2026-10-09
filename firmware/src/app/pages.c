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
#define K_OLIVE RGB(112, 148, 64)                    /* Fill BA (Fill AB: amber) */
#define K_PLUM RGB(150, 112, 170)                    /* the Intro */
#define K_SLATE RGB(84, 118, 168)                    /* the Ending */

enum { P_SONG, P_PATTERN, P_PATSET, P_TRACK, P_TRACK2, P_MIXER, P_TONE, P_MASTER, P_CHORDS, P_ARRANGE, P_ARRANGE2,
       P_ARRANGE3, P_TEMPLATES, P_TCOPY, P_SETUP, P_PANEL, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Song", "Pattern", "Pattern setup", "Track", "Track keys", "Mixer",
                                              "Mixer tone", "Master FX", "Chords", "Arrange", "Song patterns", "Song patterns",
                                              "Templates", "Copy template", "Setup", "Panel"};
/* the Arrange pages (ENV): the section track, then the six patterns, three a page (Intro, Main A, Main B;
 * Fill AB, Fill BA, Ending) and the style on KNOB 4 */
static int arrange_page(int p) { return p == P_ARRANGE || p == P_ARRANGE2 || p == P_ARRANGE3; }
/* the page buttons: a button holds up to three pages, pressed again: the next (the header's dots: which) */
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_SONG, 1}, {B_SEQ, P_PATTERN, 2}, {B_EDIT, P_TRACK, 2}, {B_FX, P_MIXER, 3},
    {B_LFO, P_CHORDS, 1}, {B_ENV, P_ARRANGE, 3}, {B_ARP, P_TEMPLATES, 2}, {B_GLO, P_SETUP, 2}};
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
static int page_group(int p)                         /* the PAGE_BTN entry page p is on */
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (p >= PAGE_BTN[i].first && p < PAGE_BTN[i].first + PAGE_BTN[i].n)
            return (int)i;
    return 0;
}
static const char *const TRACK_NAME[ARR_TRACKS] = {"D1", "D2", "PC", "BA", "C1", "C2", "C3", "C4"};
/* SEC_* (SEC_FILL: the Fill asked for, AB or BA by the Main playing) */
static const char *const SECT_NAME[SEC_FILL + 1] = {"", "Intro", "Main A", "Main B", "Fill AB", "Fill BA", "Ending", "Fill"};
static const char *const KEY_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const GRID_NAME[ARR_NQ] = {"8", "8T", "16", "16T", "32", "off"};
static const char *const TYPE_NAME[NXT] = {"Bypass", "Parallel", "Chord 1", "Chord 2", "Bass"};
static const char *const TYPE_SHORT[NXT] = {"Bypass", "Parall.", "Chord1", "Chord2", "Bass"};
static const char *const REV_NAME[5] = {"Off", "Large hall", "Hall", "Chamber", "Room"};
static const char *const REV_SHORT[5] = {"off", "L.hall", "Hall", "Chamb.", "Room"};
static const char *const JOB_NAME[4] = {"Erase track", "Copy pattern", "Clear pattern", "Back to style"};
static const char *const SONG_JOB_NAME[2] = {"Clear song", "Copy song"};
static const char *const STEP_NAME[3] = {"beat", "half", "bar"};
static const char *const LIGHTS_NAME[3] = {"off", "on", "notes"};
static const char *const SECT_LETTER[SEC_FILL + 1] = {"-", "I", "A", "B", "AB", "BA", "E", "F"};   /* the Arrange grid */
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#";
#define NNAME_CHARS ((int)sizeof NAME_CHARS - 1)
enum { KM_NONE, KM_PLAY, KM_CHORD };                 /* what a held key does */

static songs_t songs EN_POOL;                        /* the four songs (the selected one plays) */
static pattern_t scratch EN_POOL;                    /* a pattern being loaded, saved or edited */
/* the QY70 bank's styles decoded (qybank.h): the one the song plays, and one more, so another can be decoded while
 * the first still plays (a slot is re-pointed only once its new style is whole) */
static qb_style_t qbuf[2] EN_POOL;

/* the app's state that outlives the screen (ui_init runs again after an update session) */
static struct {
    uint8_t dirty;                                   /* the project changed since it was saved */
    uint8_t song_on;                                 /* the selected song is playing its tracks */
    int8_t tmpl;                                     /* the template last picked, -1 none */
    /* the six slots' patterns (Intro, Main A, Main B, Fill AB, Fill BA, Ending) as the app knows them: the tracks' settings and
     * the pattern's length and name (not its notes). Edits go here first, then into the arranger (ui.c) */
    arr_trk_t trk[ARR_NSECT][ARR_TRACKS];
    struct { char name[8]; uint8_t bars, beats; } hdr[ARR_NSECT];
    uint8_t edited[ARR_NSECT];                       /* edits stored into the slot, not saved yet */
    uint8_t edit_pend;                               /* slots with edits not yet stored into the arranger (bits) */
    uint32_t edit_t;                                 /* when the first of them was made */
    uint32_t gen;                                    /* counts the app's loads and stores (the Pattern page's view) */
    uint8_t songs_dirty;                             /* the songs object changed (a slot's pattern) */
    uint8_t song_edit;                               /* the song changed, not yet given to the arranger */
    uint32_t song_edit_t;
    uint8_t cbeat;                                   /* the Chords / Arrange cursor: a beat of the song (0..255) */
    uint8_t cstep;                                   /* the Chords cursor's step: 0 beat, 1 half bar, 2 bar */
    uint8_t reps;                                    /* Copy template: times */
    uint16_t saved[ARR_NSECT];                       /* each slot's arr_dirty when it was last loaded or saved */
    uint16_t seen[ARR_NSECT];                        /* each slot's arr_dirty as last seen, and when it moved: */
    uint32_t take_t[ARR_NSECT];                      /* the slot's latest take */
    uint8_t song_pend;                               /* a song picked while playing, + 1: loaded at the stop */
    uint32_t chg;                                    /* counts changes (the project's, the takes) */
    uint8_t save_err;                                /* the last save failed at change count err_chg: */
    uint32_t err_chg;                                /* autosave waits for another change */
    int8_t slot_buf[ARR_NSECT];                      /* the qbuf each slot plays (a bank style's section), -1 none */
    uint8_t mute_sent;                               /* the mutes the engine has (arr_track_mute), bit t track t */
} app;

/* the input's and the screen's state */
static struct {
    uint32_t btn, keys, btn_used, btn_t[NB];
    uint32_t enc_t[NE];
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    uint8_t msg_first;                               /* the message says the section PLAY starts with */
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint8_t page;
    uint8_t chord;                                   /* CHORD mode: the keys enter chords */
    uint8_t stopped;                                 /* frames since the transport stopped */
    uint8_t job, job_sel, job_to;                    /* the jobs open (SAVE held), the one picked, the copy's pattern */
    uint8_t name_cur;                                /* the pattern name's letter edited */
    uint32_t wait_t;                                 /* a queued song change: since when it waits for silence */
    uint32_t mute_t;                                 /* the mute knob's last detent */
    uint8_t rec_want, rec_seen;                      /* REC asked on, and the engine seen recording since */
    uint8_t rec_chords;                              /* REC on the Chords page: chords played go into the song */
    uint8_t chord_forced;                            /* REC turned CHORD on (leaving the page turns it off) */
    int16_t centry;                                  /* the beat the chord held was written at, -1 none */
    uint8_t centry_step;                             /* written by step entry: the cursor moves on at release */
    uint8_t clifted;                                 /* a chord key lifted since that chord was written */
    uint8_t cheld;                                   /* chord keys held at the last change */
    uint8_t kmode[NKEYS], knote[NKEYS];              /* each held key: KM_*, its note */
    uint32_t sig[3];
} ui;

static void song_head(const char *what);
/* track t muted: the song's mute (every section; ui.c gives it to the engine) */
static int trk_muted(int t) { return songs.song[proj.song].mute >> (t & 7) & 1u; }

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
    return s == SEC_INTRO ? K_PLUM : s == SEC_A ? K_TEAL : s == SEC_B ? K_ACCENT : s == SEC_FILL_AB || s == SEC_FILL ? K_AMBER
         : s == SEC_FILL_BA ? K_OLIVE : s == SEC_END ? K_SLATE : K_DIM;
}
/* the slot of the section playing (the one the pattern pages show and edit) */
static int sect_slot(int sect) { return sect >= SEC_INTRO && sect <= SEC_END ? sect - 1 : SEC_A - 1; }
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

/* SEL held this long: the keys' shortcuts on screen (draw_sel), and its release is no CHORD tap */
#define SEL_HOLD_MS 400u
static int sel_shown(void)
{
    return (ui.btn >> B_SEL & 1u) && !ui.job && plat_ms() - ui.btn_t[B_SEL] >= SEL_HOLD_MS;
}
static void song_style_text(char *t);
/* the header: the page's name and, for a button with more pages, which of them (dots); on the Song page the
 * song and its style. Right: the tempo (or "ext"), REC's dot, CHORD, "unsaved". A message replaces it all */
static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until, rec = arr_recording || ui.rec_chords, ext = arr_ext, sel = sel_shown();
    int g = page_group(ui.page), un = unsaved();
    uint32_t h = hash(2166136261u, ui.page | (uint32_t)msg << 8 | (uint32_t)un << 9 | (uint32_t)ui.chord << 10 |
                                       (uint32_t)rec << 11 | (uint32_t)ext << 12 | (uint32_t)sel << 13 |
                                       (uint32_t)proj.tempo << 16);
    h = hash(h, (uint32_t)proj.song | (uint32_t)songs.song[proj.song].sty << 8);
    for (int k = 0; k < ARR_NSECT; k++)              /* the sections on user patterns ("Pop, A user") */
        h = hash(h, songs.song[proj.song].style[k] < ARR_USER_PATTERNS);
    if (msg)
        h = hash(h, ui.msg_until);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(8, 4, &FONT_B, ui.msg[0], K_TEXT);
        text_fit(x + 8, 4, &FONT_B, ui.msg[1], 232 - (x + 8), K_ACCENT);
    } else if (sel) {                                /* SEL held: what it does with the keys, PRESETS */
        cv_text(8, 4, &FONT_B, "SEL + a key", K_TEXT);
        cv_text(232 - text_w(&FONT_XS, "PRESETS: song"), 5, &FONT_XS, "PRESETS: song", K_DIM);
    } else {
        int32_t x = cv_text(8, 4, &FONT_B, PAGE_NAME[ui.page], K_TEXT), r = 232;
        if (ui.page == P_SONG) {
            itoa_u(proj.song + 1u, cat_s(t, " "));
            x = cv_text(x, 4, &FONT_B, t, K_TEXT);
        }
        if (PAGE_BTN[g].n > 1) {                     /* page 2 of 3: o * o */
            int32_t d;
            x += 6;
            for (d = 0; d < PAGE_BTN[g].n; d++, x += 9)
                cv_round(x, 10, 6, 6, 3, d == ui.page - PAGE_BTN[g].first ? K_TEAL : K_LINE);
        }
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
        if (ui.page == P_SONG)                       /* the style ("Pop, A user") */
            song_style_text(t);
        if (un && r - 6 - text_w(&FONT_XS, "unsaved") > x + 8 &&   /* (the Song page: the style first; SAVE's lit) */
            (ui.page != P_SONG || r - 6 - text_w(&FONT_XS, "unsaved") - 8 >= x + 8 + text_w(&FONT_B, t))) {
            r -= 6 + text_w(&FONT_XS, "unsaved");
            cv_text(r, 5, &FONT_XS, "unsaved", K_DIM);
        }
        if (ui.page == P_SONG) {                     /* in what room is left: whole, else the style's name alone */
            if (text_w(&FONT_B, t) > r - 8 - (x + 8))
                for (char *c = t; *c; c++)
                    if (*c == ',') {
                        *c = 0;
                        break;
                    }
            text_fit(x + 8, 4, &FONT_B, t, r - 8 - (x + 8), K_TEAL);
        }
    }
    cv_blit(0, 0);
}

/* ---------------------------------------------------------- Song page --- */
static void draw_song(void)
{
    uint32_t h, now = arr_ticks, lit = 0, i;
    int sect = arr_sect, next = arr_next_sect, play = arr_playing, blink = (plat_ms() / 300u) & 1u;
    uint32_t slot = (uint32_t)sect_slot(sect), bar, beat;
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
        if (play && hit && now - hit < 8u)       /* (stopped: nothing plays, nothing lit) */
            lit |= 1u << i;
    }
    h = hash(h, (uint32_t)sect | (uint32_t)next << 4 | (uint32_t)play << 8 | (uint32_t)(next ? blink : 0) << 9 |
                    lit << 10 | (uint32_t)proj.trk << 18 | (uint32_t)ui.chord << 21 | (uint32_t)app.song_on << 22);
    h = hash(h, bar << 8 | beat | (uint32_t)(uint8_t)pl.tmpl << 16 | pl.tbar << 24);
    h = hash(h, has_next ? arr_chord_pack(nc) : 0xFFFFFFu);
    for (i = 0; i < ARR_TRACKS; i++)
        h = hash(h, app.trk[slot][i].program | (uint32_t)app.trk[slot][i].chan << 8 | (uint32_t)trk_muted((int)i) << 12);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);

    {   /* the section, the one waiting (NEXT), and where we are */
        int32_t x = pill(8, 2, 0, SECT_NAME[slot + 1], sect_colour((int)slot + 1), K_BG);
        if (next >= SEC_INTRO && next <= SEC_FILL) {
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
            cv_text(160, 48, &FONT_S, ui.chord ? "the keys" : "-", K_DIM);
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
                if (s->sect[i] >= SEC_INTRO && s->sect[i] <= SEC_END)
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
            text_c(120, y + 1, &FONT_XS, ui.chord ? "Hold a chord on the keys" : "Hold SEL: progressions, sections",
                   K_DIM);
    }
    for (i = 0; i < ARR_TRACKS; i++) {   /* the eight tracks: lit as they play, with their sounds */
        int32_t x = 6 + (int32_t)(i / 4u) * 116, y = 96 + (int32_t)(i % 4u) * 12;
        int on = (int)(lit >> i & 1u), sel = i == proj.trk, mute = trk_muted((int)i);
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
static int pslot(void) { return sect_slot(arr_sect); }   /* the slot the pattern pages show: the section playing */
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
/* ---- the styles, in the order the knobs step through them: the factory styles (pattern numbers ARR_FACTORY0..),
 * then the QY70 bank's (ARR_BANK0..; bring your own ROM: none until it is installed), by name. A style index i
 * (0..styles_n()-1) and a section k make pattern number style_number(i, k) */
static int styles_n(void) { return arr_styles() + qb_count(); }
static int style_bank(int i) { return i >= arr_styles(); }   /* a QY70 bank style */
static int style_index(int st)                       /* the style a pattern number is a section of, -1 none */
{
    if (st >= ARR_FACTORY0 && st < ARR_FACTORY0 + ARR_NSECT * arr_styles())
        return (st - ARR_FACTORY0) / ARR_NSECT;
    if (st >= ARR_BANK0 && st < ARR_BANK0 + ARR_NSECT * qb_count())
        return arr_styles() + (st - ARR_BANK0) / ARR_NSECT;
    return -1;
}
static int style_number(int i, int k)
{
    return i < arr_styles() ? ARR_FACTORY0 + ARR_NSECT * i + k : ARR_BANK0 + ARR_NSECT * (i - arr_styles()) + k;
}
static const char *style_label(int i) { return style_bank(i) ? qb_name(i - arr_styles()) : arr_style_name(i); }
static int style_tempo(int i) { return style_bank(i) ? qb_tempo(i - arr_styles()) : arr_style_tempo(i); }
/* the song's style (song_t.sty: the Style knob's), 0..styles_n()-1; -1: a QY70 style with no bank installed */
static int song_style(void) { return style_index(songs.song[proj.song].sty); }
/* the song's style and the sections on user patterns (the style's no longer): "Funk", "QY Bossa", "Pop, A user"
 * (Main A), "Pop, 3 user", "Pop, all user" */
static void song_style_text(char *t)
{
    static const char *const LETTER[ARR_NSECT] = {"I", "A", "B", "AB", "BA", "E"};
    const song_t *g = &songs.song[proj.song];
    int i = song_style(), n = 0, one = 0;
    char *e;
    *t = 0;
    e = i < 0 ? cat_s(t, "QY -") : cat_s(style_bank(i) ? cat_s(t, "QY ") : t, style_label(i));
    for (int k = 0; k < ARR_NSECT; k++)
        if (g->style[k] < ARR_USER_PATTERNS) {
            n++;
            one = k;
        }
    if (n == 1)
        cat_s(cat_s(cat_s(e, ", "), LETTER[one]), " user");
    else if (n == ARR_NSECT)
        cat_s(e, ", all user");
    else if (n)
        cat_s(itoa_u((uint32_t)n, cat_s(e, ", ")), " user");
}

/* a song's pattern number by name: "P05" (a user pattern), "Pop Intro", "Pop A", "Pop B", "Pop Fill AB", "Pop
 * Fill BA", "Pop End" (a style's), "QY Bossa A" (a QY70 bank style's; "QY -" with no bank); up to 20 characters */
static void style_name(int st, char *t)
{
    static const char *const SEC_SHORT[ARR_NSECT] = {" Intro", " A", " B", " Fill AB", " Fill BA", " End"};
    char *e = t;
    int i = style_index(st);
    *t = 0;
    if (st >= 0 && st < ARR_USER_PATTERNS) {
        *e++ = 'P';
        *e++ = (char)('0' + (st + 1) / 10);
        *e++ = (char)('0' + (st + 1) % 10);
        *e = 0;
    } else if (i >= 0)
        cat_s(cat_s(style_bank(i) ? cat_s(t, "QY ") : t, style_label(i)), SEC_SHORT[(st - (style_bank(i) ? ARR_BANK0 : ARR_FACTORY0)) % ARR_NSECT]);
    else
        cat_s(t, st >= ARR_BANK0 ? "QY -" : "-");
}
/* "P05 VERSE", the pattern's number and name; a factory style's: "Pop A" */
static void pat_title(int s, char *t)
{
    int st = songs.song[proj.song].style[s], i;
    char *e = t;
    style_name(st, t);
    if (st >= ARR_USER_PATTERNS)
        return;
    e += 3;
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
        int sel = i == proj.trk, mute = trk_muted(i);
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
        cv_text(120 - text_w(&FONT_XS, "range") / 2, y + 12, &FONT_XS, "range", K_DIM);
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
        h = hash(h, (uint32_t)trk_muted(i));
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *t = &app.trk[s][i];
        int32_t x = 4 + i * 29, w = 27, sel = i == proj.trk, vh = 76 * t->vol / 127;
        uint16_t c = trk_muted(i) ? K_LINE : i < 3 ? K_ACCENT : K_TEAL;
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
/* the tone: each track's brightness (CC 74, the filter's cutoff) and resonance (CC 71), 64 neutral (a line) */
static void draw_tone(void)
{
    int s = pslot(), i;
    uint32_t h = hash(2166136261u, 0x70000u | (uint32_t)s | (uint32_t)proj.trk << 4);
    for (i = 0; i < ARR_TRACKS; i++)
        h = hash(h, app.trk[s][i].bright | (uint32_t)app.trk[s][i].reso << 8 | (uint32_t)trk_muted(i) << 16);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *t = &app.trk[s][i];
        int32_t x = 4 + i * 29, w = 27, sel = i == proj.trk, b = arr_bright(t), r = arr_reso(t);
        int32_t bh = 76 * b / 127, rh = 76 * r / 127;
        char v[6];
        cv_round(x, 0, w, 144, 5, sel ? K_TEAL_T : K_PANEL);
        text_c(x + w / 2, 0, &FONT_XS, TRACK_NAME[i], sel ? K_TEXT : K_DIM);
        cv_round(x + 5, 20, 7, 78, 3, K_BG);    /* brightness */
        cv_round(x + 15, 20, 7, 78, 3, K_BG);   /* resonance */
        if (bh > 0)
            cv_round(x + 5, 20 + 78 - bh - 1, 7, bh, 3, trk_muted(i) ? K_LINE : K_TEAL);
        if (rh > 0)
            cv_round(x + 15, 20 + 78 - rh - 1, 7, rh, 3, trk_muted(i) ? K_LINE : K_AMBER);
        cv_rect(x + 3, 20 + 78 - 39, w - 6, 1, K_DIM); /* 64: neutral */
        itoa_u((uint32_t)b, cat_s(v, "B"));
        text_c(x + w / 2, 104, &FONT_XS, v, b == 64 ? K_DIM : K_TEXT);
        itoa_u((uint32_t)r, cat_s(v, "R"));
        text_c(x + w / 2, 120, &FONT_XS, v, r == 64 ? K_DIM : K_TEXT);
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
static void draw_song_jobs(void)
{
    uint32_t h = hash(2166136261u, 768u + ui.job_sel + ((uint32_t)ui.job_to << 4) + ((uint32_t)proj.song << 8)), i;
    char t[24];
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    song_head(0);
    for (i = 0; i < 2; i++) {
        int on = i == ui.job_sel;
        int32_t y = 26 + (int32_t)i * 30;
        cv_round(8, y, 224, 26, 6, on ? K_TEAL : K_PANEL);
        cv_text(18, y + 4, &FONT_B, SONG_JOB_NAME[i], on ? K_BG : K_TEXT);
        if (i == 0)
            itoa_u(proj.song + 1u, cat_s(t, "song "));
        else
            itoa_u(ui.job_to % ARR_SONGS + 1u, cat_s(t, "to song "));
        cv_text(222 - text_w(&FONT_S, t), y + 4, &FONT_S, t, on ? K_BG : K_DIM);
    }
    text_c(120, 120, &FONT_XS, "SEL: do it      SAVE: cancel", K_DIM);
    cv_blit(0, MAIN_Y);
}
static void draw_jobs(void)
{
    int s = pslot(), i;
    uint32_t h = hash(2166136261u, 512u + ui.job_sel + ((uint32_t)ui.job_to << 4) + ((uint32_t)proj.trk << 8) +
                                       ((uint32_t)s << 12) + ((uint32_t)songs.song[proj.song].sty << 16));
    char t[24], *e;
    if (ui.job == 2) {
        draw_song_jobs();
        return;
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    pattern_head(s, &h);
    for (i = 0; i < 4; i++) {
        int on = i == ui.job_sel;
        int32_t y = 22 + i * 27;
        cv_round(8, y, 224, 26, 6, on ? K_TEAL : K_PANEL);
        cv_text(18, y + 4, &FONT_B, JOB_NAME[i], on ? K_BG : K_TEXT);
        if (i == 0)
            e = cat_s(t, TRACK_NAME[proj.trk]);
        else if (i == 1) {
            e = cat_s(t, "to P");
            *e++ = (char)('0' + (ui.job_to + 1) / 10);
            *e++ = (char)('0' + (ui.job_to + 1) % 10);
            *e = 0;
        } else if (i == 2)
            e = cat_s(t, SECT_NAME[s + 1]);
        else if (song_style() < 0)                   /* back to the song's style: its section, "Pop A" */
            e = cat_s(t, "-");
        else
            style_name(style_number(song_style(), s), t);
        (void)e;
        cv_text(222 - text_w(&FONT_S, t), y + 4, &FONT_S, t, on ? K_BG : K_DIM);
    }
    text_c(120, 130, &FONT_XS, "SEL: do it      SAVE: cancel", K_DIM);
    cv_blit(0, MAIN_Y);
}

/* -------------------------------------------------- the song's pages --- */
static song_t *csong(void) { return &songs.song[proj.song]; }
static void song_head(const char *what)
{
    char t[32], *e;
    const song_t *g = csong();
    e = itoa_u(proj.song + 1u, cat_s(t, "Song "));
    e = cat_s(e, ", ");
    if (g->bars) {
        e = itoa_u(g->bars, e);
        e = cat_s(e, g->bars == 1 ? " bar" : " bars");
        cat_s(e, g->loop ? ", loops" : "");
    } else
        cat_s(e, "empty");
    cv_text(8, 0, &FONT_XS, t, K_DIM);
    if (what)
        cv_text(232 - text_w(&FONT_XS, what), 0, &FONT_XS, what, K_DIM);
}
/* the section in effect at bar b (the last mark at or before it; Main A before any) */
static int sect_at(const song_t *g, int b)
{
    for (; b >= 0; b--)
        if (g->sect[b] >= SEC_INTRO && g->sect[b] <= SEC_END)
            return g->sect[b];
    return SEC_A;
}
/* the song's bars as rows of 4 beats, the chords where they change; the cursor; the beat playing */
static void draw_chords(void)
{
    const song_t *g = csong();
    int cb = app.cbeat / 4, first = cb - 3, i, b, play = arr_playing && app.song_on;
    uint32_t pb = play ? (uint32_t)arr_song_bar * 4u + arr_beat_tick / ARR_PPQ : 0xFFFFu;
    uint32_t h = hash(2166136261u, app.cbeat | (uint32_t)app.cstep << 8 | (uint32_t)g->bars << 12 | pb << 20);
    char t[24];
    h = hash(hash(h, ui.rec_chords | (uint32_t)proj.song << 4 | (uint32_t)g->loop << 8), app.gen);
    if (first > ARR_SONGBARS - 7)
        first = ARR_SONGBARS - 7;
    if (first < 0)
        first = 0;
    for (b = first; b < first + 7; b++)
        for (i = 0; i < 4; i++)
            h = hash(h, arr_chord_pack(g->chord[b * 4 + i]) | (uint32_t)g->sect[b] << 24);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cat_s(cat_s(t, "step: "), STEP_NAME[app.cstep % 3u]);
    song_head(ui.rec_chords ? "recording chords" : t);
    for (b = first; b < first + 7; b++) {
        int32_t y = 18 + (b - first) * 18;
        uint16_t sc = sect_colour(sect_at(g, b));
        itoa_u((uint32_t)b + 1u, t);
        cv_text(22 - text_w(&FONT_XS, t), y, &FONT_XS, t, b < g->bars ? K_DIM : K_LINE);
        cv_rect(25, y + 2, 3, 14, b < g->bars ? sc : K_PANEL);
        for (i = 0; i < 4; i++) {
            int k = b * 4 + i, cur = k == app.cbeat, on = (uint32_t)k == pb;
            int32_t x = 32 + i * 51;
            const chord_t *c = &g->chord[k];
            cv_round(x, y + 1, 49, 16, 4, cur ? (ui.rec_chords ? K_ACCENT : K_TEAL) : on ? K_TEAL_T : K_PANEL);
            if (c->type < CH_NTYPES) {
                chord_name(*c, t);
                if (text_w(&FONT_XS, t) <= 47)
                    text_c(x + 24, y, &FONT_XS, t, cur ? K_BG : b < g->bars ? K_TEXT : K_DIM);
                else
                    text_fit(x + 2, y, &FONT_XS, t, 45, cur ? K_BG : K_TEXT);
            } else
                text_c(x + 24, y, &FONT_XS, ".", cur ? K_BG : K_LINE);
        }
    }
    cv_blit(0, MAIN_Y);
}
/* the section strip: 8 bars a row, the marks lettered, the bars in effect coloured, the cursor outlined */
static void draw_arrange(void)
{
    const song_t *g = csong();
    int cb = app.cbeat / 4, b, play = arr_playing && app.song_on, pbar = play ? arr_song_bar : -1;
    uint32_t h = hash(2166136261u, (uint32_t)cb | (uint32_t)g->bars << 8 | (uint32_t)(pbar + 1) << 16 |
                                       (uint32_t)g->loop << 24 | (uint32_t)proj.song << 25);
    for (b = 0; b < ARR_SONGBARS; b++)
        h = hash(h, g->sect[b]);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    {
        char t[12];
        itoa_u((uint32_t)cb + 1u, cat_s(t, "bar "));
        song_head(t);
    }
    for (b = 0; b < ARR_SONGBARS; b += 8) {      /* row labels: each row's first bar */
        char t[4];
        itoa_u((uint32_t)b + 1u, t);
        cv_text(20 - text_w(&FONT_XS, t), 16 + (b / 8) * 15, &FONT_XS, t, K_DIM);
    }
    for (b = 0; b < ARR_SONGBARS; b++) {
        int32_t x = 24 + (b % 8) * 27, y = 18 + (b / 8) * 15;
        int in = b < g->bars, cur = b == cb, se = sect_at(g, b), mark = g->sect[b] >= SEC_INTRO && g->sect[b] <= SEC_END;
        if (cur)
            cv_round(x - 1, y - 1, 27, 15, 4, K_TEXT);
        cv_round(x, y, 25, 13, 3, in ? sect_colour(se) : K_PANEL);
        if (mark)
            text_c(x + 12, y - 2, &FONT_XS, SECT_LETTER[g->sect[b]], in ? K_BG : K_DIM);
        if (b == pbar)
            cv_round(x + 19, y + 4, 4, 4, 2, K_BG);
    }
    cv_blit(0, MAIN_Y);
}
/* the song's six patterns, a row each; the three the knobs turn on this page (P_ARRANGE2: Intro, Main A, Main B;
 * P_ARRANGE3: the fills and the Ending) lit, numbered by their knob */
static void draw_arrange2(void)
{
    const song_t *g = csong();
    uint32_t first = ui.page == P_ARRANGE3 ? 3u : 0u, i;
    uint32_t h = hash(2166136261u, 1024u + proj.song + (first << 4));
    char t[28];
    for (i = 0; i < ARR_NSECT; i++)
        h = hash(h, g->style[i]);
    h = hash(h, app.gen);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    song_head(first ? "fills, ending" : "intro, mains");
    for (i = 0; i < ARR_NSECT; i++) {
        int32_t y = 17 + (int32_t)i * 21;
        int on = i >= first && i < first + 3u;
        char *e, k[2] = {(char)('1' + i - first), 0};
        if (on)
            cv_round(4, y, 232, 20, 5, K_PANEL);
        pill(8, y, 76, SECT_NAME[i + 1], sect_colour((int)i + 1), K_BG);
        style_name(g->style[i], t);
        e = t;
        while (*e)
            e++;
        if (g->style[i] < ARR_USER_PATTERNS) {   /* its name, as loaded */
            *e++ = ' ';
            for (int n = 0; n < 8 && app.hdr[i].name[n]; n++)
                *e++ = app.hdr[i].name[n];
            *e = 0;
        }
        text_fit(92, y + (on ? 0 : 1), on ? &FONT_B : &FONT_S, t, 122, K_TEXT);   /* the knobs' three: bold */
        if (on)
            cv_text(222, y + 3, &FONT_XS, k, K_DIM);
    }
    cv_blit(0, MAIN_Y);
}
/* the 16 templates, two columns; the cursor, the one playing */
static void draw_templates(void)
{
    int tn = arr_template_now, cur = app.tmpl < 0 ? 0 : app.tmpl, i;
    uint32_t h = hash(2166136261u, 2048u + (uint32_t)(tn + 1) + ((uint32_t)cur << 8) + ((uint32_t)proj.key << 16));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < NTEMPLATES; i++) {
        int32_t x = 6 + (i / 8) * 116, y = 2 + (i % 8) * 17;
        int on = i == cur;
        char t[24];
        itoa_u((uint32_t)i + 1u, t);
        cv_round(x, y, 112, 16, 4, on ? K_TEAL : K_PANEL);
        cv_text(x + 4, y, &FONT_XS, t, on ? K_BG : K_DIM);
        text_fit(x + 22, y, &FONT_XS, TEMPLATES[i].name, 84, on ? K_BG : K_TEXT);
        if (i == tn)
            cv_round(x + 102, y + 6, 5, 5, 2, on ? K_BG : K_ACCENT);
    }
    cv_blit(0, MAIN_Y);
}
static void draw_tcopy(void)
{
    int cur = app.tmpl < 0 ? 0 : app.tmpl, i, n;
    const template_t *m = &TEMPLATES[cur];
    uint32_t h = hash(2166136261u, 4096u + (uint32_t)cur + ((uint32_t)app.cbeat << 8) + ((uint32_t)app.reps << 16) +
                                       ((uint32_t)proj.key << 20) + ((uint32_t)proj.song << 24));
    char t[32], *e;
    int32_t w;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_text(8, 4, &FONT_B, m->name, K_TEXT);
    n = m->nbars;
    w = 224 / (n < 4 ? 4 : n);
    for (i = 0; i < n; i++) {
        chord_t c = m->bars[i];
        c.root = (uint8_t)((c.root + proj.key) % 12u);
        chord_name(c, t);
        cv_round(8 + i * w, 30, w - 2, 18, 4, K_PANEL);
        if (text_w(&FONT_XS, t) <= w - 4)
            text_c(8 + i * w + (w - 2) / 2, 31, &FONT_XS, t, K_TEXT);
    }
    e = itoa_u(proj.song + 1u, cat_s(t, "into song "));
    e = itoa_u(app.cbeat / 4u + 1u, cat_s(e, " from bar "));
    cv_text(8, 60, &FONT_S, t, K_TEXT);
    e = itoa_u((uint32_t)(n * (app.reps ? app.reps : 1)), t);
    e = cat_s(e, " bars");
    if (m->beats != 4)
        cat_s(e, ", a chord a bar");
    cv_text(8, 80, &FONT_S, t, K_DIM);
    text_c(120, 116, &FONT_XS, "SEL: copy it", K_DIM);
    cv_blit(0, MAIN_Y);
}
static void setup_row(int32_t y, const char *name, const char *val)
{
    cv_round(8, y, 224, 26, 6, K_PANEL);
    cv_text(18, y + 4, &FONT_S, name, K_DIM);
    cv_text(222 - text_w(&FONT_B, val), y + 4, &FONT_B, val, K_TEXT);
}
static void bank_text(char *t)                       /* the QY70 bank: "128 styles", "none", "damaged", "old format" */
{
    int n = qb_status();
    if (n > 0)
        cat_s(itoa_u((uint32_t)n, t), n == 1 ? " style" : " styles");
    else
        cat_s(t, n == QB_NONE ? "none" : n == QB_OLD ? "old format" : "damaged");
}
static int key_signed(void) { return proj.key > 5u ? (int)proj.key - 12 : (int)proj.key; }
static char *key_num(char *t)                    /* "-3", "0", "+5" */
{
    int k = key_signed();
    char *e = t;
    if (k > 0)
        *e++ = '+';
    if (k < 0)
        *e++ = '-';
    return itoa_u((uint32_t)(k < 0 ? -k : k), e);
}
static void key_text(char *t)                    /* "A (-3)" */
{
    char *e = cat_s(t, KEY_NAME[proj.key % 12u]);
    e = cat_s(e, " (");
    e = key_num(e);
    cat_s(e, ")");
}
static void draw_setup(void)
{
    uint32_t h = hash(2166136261u, 8192u + proj.clock + ((uint32_t)proj.mout << 2) + ((uint32_t)proj.key << 4) +
                                       ((uint32_t)proj.leds << 8) + ((uint32_t)(qb_status() + 8) << 12));
    char t[16];
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    setup_row(2, "Clock", proj.clock ? "MIDI in" : "internal");
    setup_row(30, "MIDI out", proj.mout ? "on" : "off");
    key_text(t);
    setup_row(58, "Key (templates)", t);
    setup_row(86, "Lights", LIGHTS_NAME[proj.leds % 3u]);
    bank_text(t);
    setup_row(114, "QY70 styles", t);
    cv_blit(0, MAIN_Y);
}
static void draw_panel(void)
{
    uint32_t cpu = plat_cpu_pct(), v = gm_voices, x = plat_xruns();
    uint32_t h = hash(2166136261u, 16384u + cpu + (v << 8) + (x << 16));
    char t[16];
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(8, 2, 108, 70, 6, K_PANEL);
    cv_round(124, 2, 108, 70, 6, K_PANEL);
    cv_text(18, 6, &FONT_XS, "CPU", K_DIM);
    cat_s(itoa_u(cpu, t), "%");
    cv_text(18, 24, &FONT_L, t, cpu > 80u ? K_ACCENT : K_TEXT);
    cv_text(134, 6, &FONT_XS, "voices", K_DIM);
    itoa_u(v, t);
    cv_text(134, 24, &FONT_L, t, K_TEXT);
    cv_round(8, 80, 224, 60, 6, K_PANEL);
    cv_text(18, 84, &FONT_XS, "version", K_DIM);
    cv_text(18, 100, &FONT_B, "Polly " GM_VERSION, K_TEXT);
    itoa_u(x, cat_s(t, "xruns "));
    cv_text(222 - text_w(&FONT_XS, t), 84, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}

/* SEL held: the keys' shortcuts. The black keys 1-5 (the sections, the one playing or next outlined), the white
 * keys 1-16 (the templates, the one playing lit) */
static void draw_sel(void)
{
    static const uint8_t SECT[5] = {SEC_INTRO, SEC_A, SEC_B, SEC_FILL, SEC_END};
    static const char *const LABEL[5] = {"Intro", "Main A", "Main B", "Fill", "Ending"};
    int n = arr_next_sect, cur = n >= SEC_INTRO && n <= SEC_FILL ? n : arr_sect, tn = arr_template_now, i;
    uint32_t h = hash(2166136261u, 32768u + (uint32_t)cur + ((uint32_t)(uint8_t)tn << 8) + ((uint32_t)proj.key << 16));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    if (cur == SEC_FILL_AB || cur == SEC_FILL_BA)
        cur = SEC_FILL;
    cv_begin(240, MAIN_H, K_BG);
    cv_text(8, 0, &FONT_XS, "black keys 1-5: a section, next bar", K_DIM);
    for (i = 0; i < 5; i++) {
        int32_t x = 8 + i * 45;
        uint16_t c = sect_colour(SECT[i]);
        if (SECT[i] == cur)
            cv_round(x - 2, 14, 45, 24, 6, K_TEXT);
        cv_round(x, 16, 41, 20, 6, c);
        text_c(x + 20, 18, &FONT_XS, LABEL[i], K_BG);
    }
    cv_text(8, 42, &FONT_XS, "white keys 1-16: a progression, now", K_DIM);
    for (i = 0; i < NTEMPLATES; i++) {               /* three columns, down then across */
        int32_t x = 6 + (i / 6) * 78, y = 58 + (i % 6) * 14;
        int on = i == tn;
        char t[4];
        if (on)
            cv_round(x, y + 1, 76, 14, 4, K_TEAL);
        itoa_u((uint32_t)i + 1u, t);
        cv_text(x + 3, y, &FONT_XS, t, on ? K_BG : K_DIM);
        text_fit(x + 18, y, &FONT_XS, TEMPLATES[i].name, 57, on ? K_BG : K_TEXT);
    }
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------- knobs --- */
typedef struct {
    const char *name;
    char val[12];
    int32_t num, max;                                /* the bar: num of max (max 0: no bar) */
} knob_t;

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
/* the Style knob (the Song page's KNOB 1, the Song patterns pages' KNOB 4): the song's style (a QY70 bank style's
 * label says so), also when sections play user patterns (they keep them) */
static void style_knob(knob_t *k)
{
    int i = song_style();
    if (i >= 0)
        kv(k, style_bank(i) ? "QY style" : "Style", style_label(i), i + 1, styles_n());
    else
        kv(k, "QY style", "-", 0, styles_n());
}
/* the Grid knob's bar: fills clockwise, as the grid gets finer (1/8 empty .. off full) */
static int grid_fill(void) { return proj.quant % ARR_NQ; }
/* the key (templates are transposed by it; -6..+5): its note, "C", "D", "Bb" */
static void key_knob(knob_t *k) { kv(k, "Key", KEY_NAME[proj.key % 12u], key_signed() + 6, 11); }
static void song_knobs(knob_t *k)
{
    int t = arr_template_now;                        /* the template playing, or none ("-") */
    if (t >= NTEMPLATES)
        t = -1;
    style_knob(&k[0]);
    kv(&k[1], "Template", t >= 0 ? TEMPLATES[t].name : "-", t + 1, NTEMPLATES);
    key_knob(&k[2]);
    kv(&k[3], "Grid", GRID_NAME[proj.quant % ARR_NQ], grid_fill(), ARR_NQ - 1);
}
static void edit_knobs(knob_t *k)
{
    int s = pslot();
    const arr_trk_t *t = &app.trk[s][proj.trk];
    char b[8];
    switch (ui.page) {
    case P_PATTERN:
        kv(&k[0], "Grid", GRID_NAME[proj.quant % ARR_NQ], grid_fill(), ARR_NQ - 1);
        kv(&k[3], "Mute", trk_muted(proj.trk) ? "on" : "off", trk_muted(proj.trk), 0);
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
    case P_TONE:
        kn(&k[0], "Bright", arr_bright(t), 127);
        kn(&k[1], "Reso", arr_reso(t), 127);
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
static void song_page_knobs(knob_t *k)
{
    const song_t *g = csong();
    const chord_t *c = &g->chord[app.cbeat];
    char b[12];
    switch (ui.page) {
    case P_CHORDS:
        if (c->type < CH_NTYPES) {
            kv(&k[0], "Root", KEY_NAME[c->root % 12u], c->root, 11);
            kv(&k[1], "Type", c->type == CH_MAJ ? "maj" : chord_type_name(c->type), c->type, CH_NONE);
            kv(&k[2], "Bass", c->bass == CH_NOBASS ? "-" : KEY_NAME[c->bass % 12u], c->bass == CH_NOBASS ? 0 : c->bass + 1, 12);
        } else {
            kv(&k[0], "Root", "-", 0, 0);
            kv(&k[1], "Type", "same", 0, 0);
            kv(&k[2], "Bass", "-", 0, 0);
        }
        kv(&k[3], "Step", STEP_NAME[app.cstep % 3u], app.cstep, 2);
        break;
    case P_ARRANGE: {
        int se = g->sect[app.cbeat / 4] <= SEC_END ? g->sect[app.cbeat / 4] : SEC_SAME;
        static const char *const SHORT[SEC_END + 1] = {"-", "Intro", "A", "B", "Fill AB", "Fill BA", "End"};
        kv(&k[0], "Section", SHORT[se], se, SEC_END);
        kn(&k[1], "Length", g->bars, ARR_SONGBARS);
        kv(&k[2], "Loop", g->loop ? "on" : "off", g->loop, 1);
        kn(&k[3], "Song", (app.song_pend ? app.song_pend - 1 : proj.song) + 1, ARR_SONGS);
        break;
    }
    case P_ARRANGE2: case P_ARRANGE3: {
        int first = ui.page == P_ARRANGE3 ? 3 : 0;
        for (int i = 0; i < 3; i++) {
            static const char *const N[ARR_NSECT] = {"Intro", "Main A", "Main B", "Fill AB", "Fill BA", "Ending"};
            int st = g->style[first + i], si = style_index(st);
            style_name(st < ARR_USER_PATTERNS ? st : 0, b);   /* a style's knob: its name (the label is the section) */
            kv(&k[i], N[first + i], si < 0 ? b : style_label(si), si < 0 ? st : ARR_USER_PATTERNS + si,
               ARR_USER_PATTERNS + styles_n() - 1);
        }
        style_knob(&k[3]);                       /* all six from one style */
        break;
    }
    case P_TEMPLATES: case P_TCOPY: {
        int cur = app.tmpl < 0 ? 0 : app.tmpl;
        kn(&k[0], "Template", cur + 1, NTEMPLATES);
        if (ui.page == P_TEMPLATES)
            key_knob(&k[1]);
        else {
            kn(&k[1], "Times", app.reps ? app.reps : 1, 8);
            kn(&k[2], "From bar", app.cbeat / 4 + 1, ARR_SONGBARS);
        }
        break;
    }
    case P_SETUP: {
        kv(&k[0], "Clock", proj.clock ? "MIDI" : "intern.", proj.clock, 1);
        kv(&k[1], "MIDI out", proj.mout ? "on" : "off", proj.mout, 1);
        key_knob(&k[2]);
        kv(&k[3], "Lights", LIGHTS_NAME[proj.leds % 3u], proj.leds, 2);
        break;
    }
    default: break;
    }
}
static void job_knobs(knob_t *k)
{
    char b[4] = {'P', (char)('0' + (ui.job_to + 1) / 10), (char)('0' + (ui.job_to + 1) % 10), 0};
    if (ui.job == 2) {
        kn(&k[0], "Job", ui.job_sel + 1, 2);
        kn(&k[1], "To song", ui.job_to % ARR_SONGS + 1, ARR_SONGS);
        return;
    }
    kn(&k[0], "Job", ui.job_sel + 1, 4);
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
    else
        song_page_knobs(k);
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
        if (upper_only(k[i].val) && text_w(&FONT_M, k[i].val) <= 58)
            text_c(cx, 22, &FONT_M, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_B, k[i].val) <= 58)   /* FONT_M has no lower case: "Bb", "off" */
            text_c(cx, 27, &FONT_B, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_XS, k[i].val) <= 58)  /* a long name: "Andalusian", "8BtRock1" */
            text_c(cx, 28, &FONT_XS, k[i].val, hot ? K_ACCENT : K_TEXT);
        else
            text_fit(x + 1, 28, &FONT_XS, k[i].val, 58, hot ? K_ACCENT : K_TEXT);
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
    else if (sel_shown())
        draw_sel();
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
    else if (ui.page == P_TONE)
        draw_tone();
    else if (ui.page == P_MASTER)
        draw_master();
    else if (ui.page == P_CHORDS)
        draw_chords();
    else if (ui.page == P_ARRANGE)
        draw_arrange();
    else if (ui.page == P_ARRANGE2 || ui.page == P_ARRANGE3)
        draw_arrange2();
    else if (ui.page == P_TEMPLATES)
        draw_templates();
    else if (ui.page == P_TCOPY)
        draw_tcopy();
    else if (ui.page == P_SETUP)
        draw_setup();
    else if (ui.page == P_PANEL)
        draw_panel();
    else
        draw_coming();
}
