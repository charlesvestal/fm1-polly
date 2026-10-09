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

enum { P_SONG, P_PATTERN, P_PATSET, P_TRACK, P_MIXER, P_MASTER, P_CHORDS, P_ARRANGE, P_TEMPLATES, P_TCOPY,
       P_SETUP, P_PANEL, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Song", "Pattern", "Pattern setup", "Track", "Mixer", "Master FX",
                                              "Chords", "Arrange", "Templates", "Copy template", "Setup", "Panel"};
static const char *const TRACK_NAME[ARR_TRACKS] = {"D1", "D2", "PC", "BA", "C1", "C2", "C3", "C4"};
static const char *const SECT_NAME[5] = {"", "Main A", "Main B", "Fill", "Ending"};
static const char *const KEY_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const GRID_NAME[ARR_NQ] = {"8", "8T", "16", "16T", "32", "off"};
enum { KM_NONE, KM_PLAY, KM_CHORD };                 /* what a held key does */

static songs_t songs EN_POOL;                        /* the four songs (the selected one plays) */
static pattern_t scratch EN_POOL;                    /* a pattern being loaded, saved or edited */

/* the app's state that outlives the screen (ui_init runs again after an update session) */
static struct {
    uint8_t dirty;                                   /* the project changed since it was saved */
    uint8_t song_on;                                 /* the selected song is playing its tracks */
    int8_t tmpl;                                     /* the template last picked, -1 none */
    /* the four slots' tracks as loaded (Main A, Main B, Fill, Ending): what the screen names */
    uint8_t tprog[ARR_NSECT][ARR_TRACKS], tchan[ARR_NSECT][ARR_TRACKS], tmute[ARR_NSECT][ARR_TRACKS];
    uint16_t saved[ARR_NSECT];                       /* each slot's arr_dirty when it was last loaded or saved */
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
    uint8_t kmode[NKEYS], knote[NKEYS];              /* each held key: KM_*, its note */
    uint32_t sig[3];
} ui;

/* anything not saved: the project, or a user pattern recorded into */
static int unsaved(void)
{
    if (app.dirty)
        return 1;
    for (int s = 0; s < ARR_NSECT; s++)
        if (songs.song[proj.song].style[s] < ARR_USER_PATTERNS && arr_dirty[s] != app.saved[s])
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

/* the chord after the one now: the template's next bar, or the song's next change; 0: none known */
static int next_chord(chord_t *out)
{
    int t = arr_template_now;
    if (t >= 0 && t < NTEMPLATES) {
        const template_t *m = &TEMPLATES[t];
        *out = m->bars[(arr_template_bar + 1u) % m->nbars];
        out->root = (uint8_t)((out->root + proj.key) % 12u);
        return 1;
    }
    if (app.song_on) {
        const song_t *s = &songs.song[proj.song];
        uint32_t now = arr_chord_now, b = (uint32_t)arr_song_bar * 4u + arr_beat_tick / ARR_PPQ, n = s->bars * 4u, i;
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

/* ------------------------------------------------------------- header --- */
#define HDR_H 24
#define MAIN_Y 24
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

static void draw_header(void)
{
    char t[24], *e;
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(2166136261u, ui.page | (uint32_t)msg << 8 | (uint32_t)unsaved() << 9 | (uint32_t)ui.chord << 10 |
                                       (uint32_t)arr_recording << 11 | (uint32_t)arr_ext << 12 | (uint32_t)proj.tempo << 16);
    h = hash(h, (uint32_t)(uint8_t)arr_template_now | (uint32_t)app.song_on << 8 | (uint32_t)proj.song << 9);
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
            if (arr_template_now >= 0)
                text_fit(x + 8, 6, &FONT_XS, TEMPLATES[arr_template_now].name, 84 - (x - 64), K_DIM);
        }
        if (unsaved())
            cv_round(x + 4, 10, 5, 5, 2, K_DIM);
        if (arr_ext)
            cat_s(t, "ext");
        else
            itoa_u(proj.tempo, t);
        r -= text_w(&FONT_S, t);
        cv_text(r, 4, &FONT_S, t, K_TEXT);
        if (arr_recording) {
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
    uint32_t h = hash(2166136261u, arr_chord_now), now = arr_ticks, lit = 0, i;
    int sect = arr_sect, next = arr_next_sect, play = arr_playing, blink = (plat_ms() / 300u) & 1u;
    uint32_t bar = app.song_on ? arr_song_bar : arr_bar, beat = arr_beat_tick / ARR_PPQ, slot = (uint32_t)(sect - 1) & 3u;
    chord_t nc;
    int has_next = next_chord(&nc);
    char t[24];
    for (i = 0; i < ARR_TRACKS; i++)
        if (arr_track_hit[i] && now - arr_track_hit[i] < 8u)
            lit |= 1u << i;
    h = hash(h, (uint32_t)sect | (uint32_t)next << 4 | (uint32_t)play << 8 | (uint32_t)(next ? blink : 0) << 9 |
                    lit << 10 | (uint32_t)proj.trk << 18 | (uint32_t)ui.chord << 21 | (uint32_t)app.song_on << 22);
    h = hash(h, bar << 8 | beat | (uint32_t)(uint8_t)arr_template_now << 16 | (uint32_t)arr_template_bar << 24);
    h = hash(h, has_next ? arr_chord_pack(nc) : 0xFFFFFFu);
    for (i = 0; i < ARR_TRACKS; i++)
        h = hash(h, app.tprog[slot][i] | (uint32_t)app.tchan[slot][i] << 8 | (uint32_t)app.tmute[slot][i] << 12);
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
        chord_name(arr_chord_unpack(arr_chord_now), t);
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
        int t_now = arr_template_now;
        if (t_now >= 0 && t_now < NTEMPLATES) {
            const template_t *m = &TEMPLATES[t_now];
            int32_t n = m->nbars, w = 224 / n;
            for (i = 0; i < (uint32_t)n; i++) {
                chord_t c = m->bars[i];
                int on = i == arr_template_bar;
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
        int on = (int)(lit >> i & 1u), sel = i == proj.trk, mute = app.tmute[slot][i];
        uint16_t fg = on ? K_BG : sel ? K_ACCENT : mute ? K_LINE : K_TEXT;
        const char *name = app.tchan[slot][i] == 9u ? "Drums" : GM_PROGRAM[app.tprog[slot][i] & 127u];
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

/* ------------------------------------------------------------- knobs --- */
typedef struct {
    const char *name;
    char val[12];
    int32_t num, max;                                /* the bar: num of max (max 0: no bar) */
} knob_t;

static void song_knobs(knob_t *k)
{
    int t = arr_template_now >= 0 ? arr_template_now : app.tmpl;
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

static void draw_knobs(void)
{
    knob_t k[4];
    uint32_t h = hash(2166136261u, ui.page | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    memset(k, 0, sizeof k);
    if (ui.page == P_SONG)
        song_knobs(k);
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
    if (ui.page == P_SONG)
        draw_song();
    else
        draw_coming();
}
