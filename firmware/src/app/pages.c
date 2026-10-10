/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's pages: the state the input layer (ui.c) and the screen share, and the drawing. Part of the unity
 * build, after project.c and before ui.c; the host simulator includes it too.
 *
 * The screen: a header (the page, the tempo and REC), the main band, the tag (whose the knobs are, the page
 * dots), the four knobs' strip and the footer (what the keys do here). Each band is drawn only when what it
 * shows changed (a hash of it). Over the main band: a help card (a button held alone), a list (SEL). Pages not
 * built yet show their name. */
#include "gm_names.h"

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

/* the pages (the spec's Buttons table): a button opens its first page; pressed again, or SELECT turned, its next */
enum { P_HOME, P_SONG, P_LOOP, P_LOOP2, P_PATTERN, P_PATSET, P_TRACK, P_TRACK2, P_MIXER, P_TONE, P_MASTER,
       P_STYLE, P_STYLE2, P_SETUP, P_PANEL, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Home", "Song", "Loop", "Loop", "Pattern", "Pattern setup", "Track",
                                              "Track keys", "Mixer", "Mixer tone", "Master FX", "Style", "Style",
                                              "Setup", "Panel"};
/* the page buttons: a button holds up to two pages, pressed again (or SELECT): the next (the tag's dots: which) */
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_HOME, 2}, {B_ARP, P_LOOP, 2}, {B_SEQ, P_PATTERN, 2}, {B_EDIT, P_TRACK, 2}, {B_LFO, P_MIXER, 2},
    {B_FX, P_MASTER, 1}, {B_ENV, P_STYLE, 2}, {B_GLO, P_SETUP, 2}};
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
static const char *const LIGHTS_NAME[3] = {"off", "on", "notes"};
static const char *const MODE_NAME[3] = {"FREE", "LOOP", "SONG"};   /* ARR_* */
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#";
#define NNAME_CHARS ((int)sizeof NAME_CHARS - 1)
/* a loop's name (mixed case: "Verse", as the factory's "Pop") */
static const char LOOP_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-#";
#define NLOOP_CHARS ((int)sizeof LOOP_CHARS - 1)
enum { KM_NONE, KM_PLAY, KM_CHORD };                 /* what a held key does */

static songs_t songs EN_POOL;                        /* the four songs (the selected one plays) */
static pattern_t scratch EN_POOL;                    /* a pattern being loaded, saved or edited */
/* the QY70 bank's styles decoded (qybank.h): the one the song plays, and one more, so another can be decoded while
 * the first still plays (a slot is re-pointed only once its new style is whole) */
static qb_style_t qbuf[2] EN_POOL;

/* the app's state that outlives the screen (ui_init runs again after an update session) */
static struct {
    uint8_t dirty;                                   /* the project changed since it was saved */
    uint8_t mode;                                    /* MODE (HOME's KNOB 4): ARR_FREE, ARR_LOOP, ARR_SONG */
    uint8_t loop;                                    /* the loop picked (LOOP plays it), 0..ARR_LOOPS-1 */
    uint8_t row;                                     /* the SONG screen's row, 0..ARR_ROWS-1 */
    /* the six slots' patterns (Intro, Main A, Main B, Fill AB, Fill BA, Ending) as the app knows them: the tracks' settings and
     * the pattern's length and name (not its notes). Edits go here first, then into the arranger (ui.c) */
    arr_trk_t trk[ARR_NSECT][ARR_TRACKS];
    struct { char name[8]; uint8_t bars, beats; } hdr[ARR_NSECT];
    uint8_t edited[ARR_NSECT];                       /* edits stored into the slot, not saved yet */
    uint8_t edit_pend;                               /* slots with edits not yet stored into the arranger (bits) */
    uint32_t edit_t;                                 /* when the first of them was made */
    uint32_t gen;                                    /* counts the app's loads and stores (the Pattern page's view) */
    uint8_t songs_dirty;                             /* the songs object changed (loops, a song, a slot's pattern) */
    uint16_t saved[ARR_NSECT];                       /* each slot's arr_dirty when it was last loaded or saved */
    uint16_t seen[ARR_NSECT];                        /* each slot's arr_dirty as last seen, and when it moved: */
    uint32_t take_t[ARR_NSECT];                      /* the slot's latest take */
    uint8_t song_pend;                               /* a song picked while playing, + 1: loaded at the stop */
    uint32_t chg;                                    /* counts changes (the project's, the takes) */
    uint8_t save_err;                                /* the last save failed at change count err_chg: */
    uint32_t err_chg;                                /* autosave waits for another change */
    int8_t slot_buf[ARR_NSECT];                      /* the qbuf each slot plays (a bank style's section), -1 none */
    uint8_t mute_sent;                               /* the mutes the engine has (arr_track_mute), bit t track t */
    uint8_t cbeat;                                   /* the chord cursor: a beat of app.loop, 0..ARR_LOOP_BEATS-1 (REC on
                                                      * HOME / LOOP writes there while stopped) */
    uint8_t cstep;                                   /* the cursor's step: 0 a beat, 1 half a bar, 2 a bar */
    uint8_t played;                                  /* a chord was heard since start-up (the keys', a loop's, a song's):
                                                      * HOME stops saying "Play a chord" */
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
    uint8_t stopped;                                 /* frames since the transport stopped */
    uint8_t name_cur;                                /* the name's letter edited (Pattern setup, a list's NAME) */
    uint8_t any_btn;                                 /* a button pressed since start-up (the footer's first words) */
    uint8_t card;                                    /* a help card on screen: its button + 1 (a button held alone) */
    uint8_t ask;                                     /* a question on screen (SEL yes, HOME no): */
    char ask_q[32];                                  /* the question */
    void (*ask_yes)(void);                           /* what SEL does */
    uint8_t copy_to, copy_src;                       /* SAVE + white key: the loop / pattern asked about, the loop copied */
    /* what an action acts on, taken when it is asked for (a question's yes acts on what it asked about) */
    struct { uint8_t loop, prog, song, trk, slot; } tgt;
    uint8_t pick_prog, pick_song;                    /* the lists' choices: LOAD PROGRESSION, COPY SONG TO */
    uint32_t wait_t;                                 /* a queued song change: since when it waits for silence */
    uint32_t mute_t;                                 /* the mute knob's last detent */
    uint8_t rec_want, rec_seen;                      /* REC asked on, and the engine seen recording since */
    /* REC on HOME / LOOP: chords written into the loop (stopped: at the cursor; playing: on the nearest beat) */
    uint8_t rec_chords;
    int8_t centry;                                   /* the beat this hold of the keys wrote, -1 none */
    uint8_t centry_step;                             /* ... written at the cursor (it moves on when the keys are all up) */
    uint8_t clifted, cheld;                          /* a key of that chord lifted since; the chord keys held */
    /* REC on HOME in FREE: a new loop's take. 0 off, 1 waiting for PLAY, 2 on (from beat take_b0 of arr_beats) */
    uint8_t take, take_loop, take_any;               /* the loop it fills (the first empty one); a chord written */
    uint32_t take_b0;
    int32_t take_pos;                                /* its beat as last seen (the transport stopped: its end) */
    uint8_t mode_wait;                               /* a MODE asked for while playing waits for the bar line: */
    uint32_t mode_at;                                /* arr_beats there (writing in time waits for it too) */
    int16_t take_last;                               /* the latest beat written */
    int8_t loop_seen;                                /* arr_loop_now as last seen (app.loop follows its changes) */
    uint8_t role_was;                                /* what the keys did last frame (REC ends when that changes) */
    uint8_t kmode[NKEYS], knote[NKEYS];              /* each held key: KM_*, its note */
    uint32_t sig[6];                                 /* the bands' hashes: header, main, knobs, tag, footer */
} ui;

/* SEL: the screen as a list. Its knobs' values (each page of its button: KNOB rows), then MORE (settings on no
 * page) and the actions (ACTION: do it, PICK: a choice first). Built afresh to draw it; sel and top stay */
enum { LK_KNOB, LK_MORE, LK_NAME, LK_ACTION, LK_PICK };
enum { A_LOAD_PROG, A_CLEAR_LOOP, A_INSERT_ROW, A_DELETE_ROW, A_CLEAR_SONG, A_COPY_SONG, A_ERASE_TRACK,
       A_CLEAR_PATTERN, A_BACK_TO_STYLE, NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {"LOAD PROGRESSION", "CLEAR LOOP", "INSERT ROW", "DELETE ROW",
                                                  "CLEAR SONG", "COPY SONG TO", "ERASE TRACK", "CLEAR PATTERN",
                                                  "BACK TO STYLE"};
#define LIST_ROWS 24
#define LIST_SHOWN 7                                 /* rows on screen */
static struct {
    uint8_t open, n, sel, top;
    struct { const char *name; char val[12]; uint8_t kind, id; } row[LIST_ROWS];
} lst;

static void song_head(const char *what);
/* track t muted: in SONG the engine's (the row's, or by hand until the next row); else the song's mute (every
 * section; ui.c gives it to the engine) */
static int trk_muted(int t)
{
    return (app.mode == ARR_SONG ? arr_mutes : songs.song[proj.song].mute) >> (t & 7) & 1u;
}

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
static uint32_t slen(const char *s)                  /* (the firmware's libc is the device's own: no strlen) */
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
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

static song_t *csong(void) { return &songs.song[proj.song]; }
/* the transposition chords play at now: the song's TRANSPOSE (+ the row's KEY in SONG) */
static int xpose_now(void)
{
    const song_t *g = csong();
    return g->transpose + (app.mode == ARR_SONG && g->nrows ? g->row[arr_row % ARR_ROWS].key : 0);
}

/* the engine's published place, read as a consistent set (the render may move it between two reads: read
 * again until two readings agree) */
typedef struct {
    int loop;                                        /* the loop playing, -1 none */
    uint32_t lbeat;                                  /* its beat sounding */
    uint32_t bar, beat;                              /* the pattern's bar and beat */
    uint32_t chord;                                  /* the chord now, packed */
} place_t;
static void place_read(place_t *p)
{
    place_t q;
    do {
        q.loop = arr_loop_now;
        q.lbeat = arr_loop_beat;
        q.bar = arr_bar;
        q.beat = arr_beat_tick / ARR_PPQ;
        q.chord = arr_chord_now;
        p->loop = arr_loop_now;
        p->lbeat = arr_loop_beat;
        p->bar = arr_bar;
        p->beat = arr_beat_tick / ARR_PPQ;
        p->chord = arr_chord_now;
    } while (p->loop != q.loop || p->lbeat != q.lbeat || p->bar != q.bar || p->beat != q.beat || p->chord != q.chord);
    if (p->loop >= ARR_LOOPS)
        p->loop = -1;
}

/* the chord after the one now: the loop's next change (as it sounds: transposed); 0: none known */
static int next_chord(const place_t *pl, chord_t *out)
{
    const loop_t *l;
    int n, i;
    if (pl->loop < 0)
        return 0;
    l = &songs.loop[pl->loop];
    n = loop_len(l);
    for (i = 1; i <= n; i++) {
        uint16_t v = l->ch[(pl->lbeat + (uint32_t)i) % (uint32_t)n];
        if (v != LC_SAME) {
            chord_t c = chord_shift(lc_unpack(v), xpose_now());
            if (arr_chord_pack(c) == pl->chord)
                continue;
            *out = c;
            return 1;
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
#define MAIN_H 132
#define TAG_Y 156                                    /* whose the knobs are, the page dots */
#define TAG_H 12
#define KNB_Y 168
#define KNB_H 56
#define FOOT_Y 224                                   /* what the keys do here */
#define FOOT_H 16

/* HOME held this long: the loops and the sections on screen (draw_home_held), and its release is no page press */
#define HOME_HOLD_MS 400u
static int home_shown(void)
{
    return (ui.btn >> B_HOME & 1u) && !ui.ask && plat_ms() - ui.btn_t[B_HOME] >= HOME_HOLD_MS;
}
static void song_style_text(char *t);
/* the header: the page's name; on HOME the song and its style. Right: the song's tempo (or "ext"), REC's dot,
 * "unsaved". A message replaces it all, a question (SEL: yes, HOME: no) the message */
static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until, rec = arr_recording || ui.rec_chords || ui.take, ext = arr_ext, sel = home_shown();
    int un = unsaved(), ask = ui.ask;
    uint32_t h = hash(2166136261u, ui.page | (uint32_t)msg << 8 | (uint32_t)un << 9 | (uint32_t)rec << 11 |
                                       (uint32_t)ext << 12 | (uint32_t)sel << 13 | (uint32_t)ask << 14 |
                                       (uint32_t)csong()->tempo << 16);
    h = hash(h, (uint32_t)proj.song | (uint32_t)songs.song[proj.song].sty << 8);
    for (int k = 0; k < ARR_NSECT; k++)              /* the sections on user patterns ("Pop, A user") */
        h = hash(h, songs.song[proj.song].style[k] < ARR_USER_PATTERNS);
    if (msg)
        h = hash(h, ui.msg_until);
    for (const char *c = ui.ask_q; ask && *c; c++)
        h = hash(h, (uint8_t)*c);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (ask) {                                       /* the question, over all */
        cv_round(0, 0, 240, HDR_H, 0, K_ACCENT);
        text_fit(8, 4, &FONT_B, ui.ask_q, 224, K_BG);
    } else if (msg) {
        int32_t x = cv_text(8, 4, &FONT_B, ui.msg[0], K_TEXT);
        text_fit(x + 8, 4, &FONT_B, ui.msg[1], 232 - (x + 8), K_ACCENT);
    } else if (sel) {                                /* HOME held: what it does with the keys, PRESETS */
        cv_text(8, 4, &FONT_B, "HOME + a key", K_TEXT);
        cv_text(232 - text_w(&FONT_XS, "PRESETS: song"), 5, &FONT_XS, "PRESETS: song", K_DIM);
    } else {
        int32_t x = cv_text(8, 4, &FONT_B, PAGE_NAME[ui.page], K_TEXT), r = 232;
        if (ui.page == P_HOME || ui.page == P_SONG) {
            itoa_u(proj.song + 1u, cat_s(t, " S"));
            x = cv_text(x, 4, &FONT_B, t, K_TEXT);
        }
        if (ext)
            cat_s(t, "ext");
        else
            itoa_u(csong()->tempo, t);
        r -= text_w(&FONT_S, t);
        cv_text(r, 4, &FONT_S, t, K_TEXT);
        if (rec) {
            r -= 14;
            cv_round(r, 8, 9, 9, 4, RGB(214, 64, 52));
        }
        if (ui.page == P_HOME)                       /* the style ("Pop, A user") */
            song_style_text(t);
        if (un && r - 6 - text_w(&FONT_XS, "unsaved") > x + 8 &&   /* (HOME: the style first; SAVE's lit) */
            (ui.page != P_HOME || r - 6 - text_w(&FONT_XS, "unsaved") - 8 >= x + 8 + text_w(&FONT_B, t))) {
            r -= 6 + text_w(&FONT_XS, "unsaved");
            cv_text(r, 5, &FONT_XS, "unsaved", K_DIM);
        }
        if (ui.page == P_HOME) {                     /* in what room is left: whole, else the style's name alone */
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

/* "L3 Verse": a loop's number and name (trailing spaces cut) */
static char *loop_label(int i, char *t)
{
    const loop_t *l = &songs.loop[i % ARR_LOOPS];
    char *e = itoa_u((uint32_t)(i % ARR_LOOPS) + 1u, cat_s(t, "L"));
    *e++ = ' ';
    for (int n = 0; n < 8 && l->name[n]; n++)
        *e++ = l->name[n];
    while (e[-1] == ' ')
        e--;
    *e = 0;
    return e;
}

/* ---------------------------------------------------------- HOME page --- */
/* HOME (the spec's Screens): the section (and the one queued) and where it is; the chord sounding, big (coral: one
 * from the keys, holding until the source's next change; teal: the loop's or the song's; "Play a chord" in FREE
 * until one is heard) and the next; the strip: the loop's beats from the playhead's bar (two bars at most, a dot
 * for no change, the beat playing underlined; REC stopped: from the cursor's bar, the cursor boxed), "starts after
 * the Intro" while an Intro holds, FREE (or the new loop's take) in FREE; in SONG the row and its pass; the eight
 * tracks, lit as they play */
#define STRIP_Y 56
static int32_t segs(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c);
/* the strip (draw 0: only its hash, into *hp) */
static void home_strip(const place_t *pl, const loop_t *l, int intro, uint32_t *hp, int draw)
{
    uint32_t bt = l->beats ? l->beats : 4u, len = (uint32_t)loop_len(l), from, nb, k, h = *hp;
    int cur_on = ui.rec_chords && !arr_playing && pl->loop == app.loop, xp = xpose_now();
    int32_t x0, cw, y = STRIP_Y, cut_until = 0;
    char t[16];
    itoa_u((uint32_t)pl->loop + 1u, cat_s(t, "L"));
    x0 = 8 + text_w(&FONT_XS, t) + 6;
    if (draw)
        cv_text(8, y, &FONT_XS, t, K_TEXT);
    if (intro || !len) {
        if (draw)
            cv_text(x0, y, &FONT_XS, intro ? "starts after the Intro" : cur_on ? "empty: play a chord" : "empty", K_DIM);
        *hp = hash(h, (uint32_t)intro << 1 | (uint32_t)cur_on);
        return;
    }
    from = cur_on ? app.cbeat / bt * bt : pl->lbeat % len / bt * bt;
    nb = cur_on ? (from >= len ? 1u : len - from > bt ? 2u : 1u) : len > bt ? 2u : 1u;
    cw = (232 - x0 - 7 * ((int32_t)nb - 1)) / (int32_t)(bt * nb);
    h = hash(h, from | len << 8 | bt << 16 | (uint32_t)cur_on << 24 | (uint32_t)app.cbeat << 25);
    for (k = 0; k < bt * nb; k++) {
        uint32_t b = from + k, idx = cur_on ? b : b % len, j;
        int32_t x = x0 + (int32_t)k * cw + (k >= bt ? 7 : 0), room = cw - 3;
        uint16_t v = idx < len && idx < ARR_LOOP_BEATS ? l->ch[idx] : LC_SAME;
        int playing = arr_playing && !cur_on && idx == pl->lbeat, cursor = cur_on && idx == app.cbeat;
        h = hash(h, v | (uint32_t)playing << 16);
        if (!draw)
            continue;
        if (k == bt)                                 /* the bar line */
            cv_rect(x - 4, y + 2, 1, 12, K_LINE);
        if (cursor) {
            cv_round(x - 1, y, cw, 16, 3, K_ACCENT);
            cv_round(x, y + 1, cw - 2, 14, 2, K_BG);
        }
        if (v != LC_SAME) {                          /* its name, over the beats of no change after it (in its bar) */
            for (j = k + 1; j < bt * nb && j / bt == k / bt; j++) {
                uint32_t i2 = cur_on ? from + j : (from + j) % len;
                if ((i2 < len && i2 < ARR_LOOP_BEATS && l->ch[i2] != LC_SAME) || (cur_on && i2 == app.cbeat))
                    break;
                room += cw;
            }
            chord_name(chord_shift(lc_unpack(v), xp), t);
            cut_until = text_fit(x + 1, y - 1, &FONT_XS, t, room, playing ? K_TEAL : K_TEXT) + 2;
        } else if (x + 3 >= cut_until)
            cv_round(x + 3, y + 8, 3, 3, 1, idx < len ? K_DIM : K_LINE);
        if (playing)
            cv_rect(x, y + 15, cw - 3, 2, K_ACCENT);
    }
    *hp = h;
}
static void draw_home(void)
{
    uint32_t h, now = arr_ticks, lit = 0, i;
    int sect = arr_sect, next = arr_next_sect, play = arr_playing, blink = (plat_ms() / 300u) & 1u, ovr = arr_override;
    int mode = arr_mode, intro, fresh, has_next, take = ui.take;
    uint32_t slot = (uint32_t)sect_slot(sect), bt;
    int32_t tb = 0;
    place_t pl;
    chord_t nc;
    char t[32];
    const loop_t *l;
    place_read(&pl);
    if (!play && app.mode == ARR_LOOP) {             /* stopped: the loop picked, from its beat 0 (PLAY starts it) */
        pl.loop = app.loop % ARR_LOOPS;
        pl.lbeat = 0;
    }
    l = pl.loop >= 0 ? &songs.loop[pl.loop] : 0;
    bt = l && l->beats ? l->beats : 4u;
    intro = l && play && sect == SEC_INTRO;          /* an Intro holds its chord: the loop starts after it */
    fresh = mode == ARR_FREE && !app.played;         /* nothing heard yet: "Play a chord" */
    has_next = l && !intro && next_chord(&pl, &nc);
    if (take == 2)                                   /* the take's beat (below 0: waiting for its downbeat) */
        tb = (int32_t)(arr_beats - ui.take_b0);
    h = hash(2166136261u, pl.chord);
    for (i = 0; i < ARR_TRACKS; i++) {
        uint32_t hit = arr_track_hit[i];
        if (play && hit && now - hit < 8u)       /* (stopped: nothing plays, nothing lit) */
            lit |= 1u << i;
    }
    h = hash(h, (uint32_t)sect | (uint32_t)next << 4 | (uint32_t)play << 8 | (uint32_t)(next ? blink : 0) << 9 |
                    lit << 10 | (uint32_t)proj.trk << 18 | (uint32_t)ovr << 21 | (uint32_t)mode << 22 |
                    (uint32_t)fresh << 24 | (uint32_t)intro << 25 | (uint32_t)take << 26);
    h = hash(h, pl.bar << 8 | pl.beat | (uint32_t)(uint8_t)pl.loop << 16 | pl.lbeat << 24);
    h = hash(hash(h, has_next ? arr_chord_pack(nc) : 0xFFFFFFu), (uint32_t)xpose_now() & 31u);
    h = hash(h, (uint32_t)arr_row | (uint32_t)arr_rep << 8 | (uint32_t)csong()->nrows << 16 | (uint32_t)ui.take_loop << 24);
    {
        uint32_t tbt = songs.loop[ui.take_loop % ARR_LOOPS].beats ? songs.loop[ui.take_loop % ARR_LOOPS].beats : 4u;
        h = hash(h, (uint32_t)(tb < 0 ? -1 : tb / (int32_t)tbt));
    }
    h = hash(h, l ? (uint32_t)l->bars | (uint32_t)app.hdr[slot].bars << 8 : app.hdr[slot].bars);
    for (i = 0; i < ARR_TRACKS; i++)
        h = hash(h, app.trk[slot][i].program | (uint32_t)app.trk[slot][i].chan << 8 | (uint32_t)trk_muted((int)i) << 12);
    if (l)
        home_strip(&pl, l, intro, &h, 0);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);

    {   /* the section, the one waiting (NEXT), and where we are: "bar 2.3 / 4", the loop's, else the pattern's */
        int32_t x = pill(8, 2, 0, SECT_NAME[slot + 1], sect_colour((int)slot + 1), K_BG);
        int inl = l && l->bars && !intro;
        uint32_t bar = inl ? pl.lbeat / bt : pl.bar, beat = inl ? pl.lbeat % bt : pl.beat;
        char *e;
        if (next >= SEC_INTRO && next <= SEC_FILL) {
            cv_text(x + 8, 5, &FONT_XS, "NEXT", blink ? K_ACCENT : K_DIM);
            pill(x + 8 + text_w(&FONT_XS, "NEXT") + 6, 2, 0, SECT_NAME[next], blink ? sect_colour(next) : K_PANEL,
                 blink ? K_BG : K_TEXT);
        }
        e = itoa_u(bar + 1u, cat_s(t, "bar "));
        *e++ = '.';
        e = itoa_u(beat + 1u, e);
        itoa_u(inl ? l->bars : app.hdr[slot].bars, cat_s(e, " / "));
        cv_text(232 - text_w(&FONT_S, t), 3, &FONT_S, t, play ? K_TEXT : K_DIM);
    }
    {   /* the chord now, big, in its box; the next one */
        cv_round(8, 22, 144, 33, 6, K_PANEL);
        if (fresh)
            text_c(80, 30, &FONT_B, "Play a chord", K_DIM);
        else {
            chord_name(arr_chord_unpack(pl.chord), t);
            text_c(80, 22, &FONT_L, t, ovr ? K_ACCENT : K_TEAL);
        }
        if (l && !intro) {
            cv_text(162, 22, &FONT_XS, "next", K_DIM);
            if (has_next) {
                chord_name(nc, t);
                text_fit(162, 35, &FONT_B, t, 70, K_TEXT);
            } else
                cv_text(162, 35, &FONT_B, "-", K_DIM);
        }
    }
    if (l)                                           /* the strip: the loop's beats */
        home_strip(&pl, l, intro, &h, 1);
    else if (take) {                                 /* FREE, REC: the new loop's take */
        char *e = itoa_u(ui.take_loop + 1u, cat_s(t, "New loop L"));
        if (take == 1)
            cat_s(e, ": from PLAY");
        else if (tb < 0)
            cat_s(e, ": from the next bar");
        else
            itoa_u((uint32_t)tb / (songs.loop[ui.take_loop % ARR_LOOPS].beats ? songs.loop[ui.take_loop % ARR_LOOPS].beats : 4u) + 1u,
                   cat_s(e, ": bar "));
        cv_text(8, STRIP_Y, &FONT_XS, t, K_ACCENT);
    } else
        cv_text(8, STRIP_Y, &FONT_XS, mode == ARR_FREE ? "FREE" : MODE_NAME[mode % 3u], K_DIM);
    if (mode == ARR_SONG) {                          /* the song: "row 2 of 4 . x1 of 2" */
        const song_t *g = csong();
        if (g->nrows) {
            char *e = itoa_u(arr_row % ARR_ROWS + 1u, cat_s(t, "row "));
            e = itoa_u(g->nrows, cat_s(e, " of "));
            e = itoa_u(arr_rep + 1u, cat_s(e, "|x"));
            itoa_u(g->row[arr_row % ARR_ROWS].rep, cat_s(e, " of "));
        } else
            cat_s(t, "the song has no rows");
        segs(8, STRIP_Y + 14, &FONT_XS, t, K_DIM);
    }
    for (i = 0; i < ARR_TRACKS; i++) {   /* the eight tracks: lit as they play, with their sounds */
        int32_t x = 6 + (int32_t)(i / 4u) * 116, y = 87 + (int32_t)(i % 4u) * 11;
        int on = (int)(lit >> i & 1u), sel = i == proj.trk, mute = trk_muted((int)i);
        uint16_t fg = on ? K_BG : sel ? K_ACCENT : mute ? K_LINE : K_TEXT;
        const char *name = trk_sound(&app.trk[slot][i]);
        if (on)
            cv_round(x, y, 112, 11, 3, sel ? K_ACCENT : K_TEAL);
        else if (sel)
            cv_round(x, y, 112, 11, 3, K_PANEL);
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
        int32_t y = 20 + i * 14, b;
        int sel = i == proj.trk, mute = trk_muted(i);
        uint32_t n, e = (uint32_t)tr->first + tr->count, lo = 127, hi = 0;
        cv_text(4, y - 2, &FONT_XS, TRACK_NAME[i], sel ? K_ACCENT : mute ? K_LINE : K_DIM);
        cv_round(X0, y, W, 12, 3, sel ? (arr_recording ? RGB(246, 214, 204) : K_TEAL_T) : K_PANEL);
        for (b = 1; b < app.hdr[s].bars; b++)  /* bar lines */
            cv_rect(X0 + b * W / app.hdr[s].bars, y + 1, 1, 10, K_LINE);
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
            yy = y + 8 - (hi > lo ? (int32_t)((v->note - lo) * 7u / (hi - lo)) : 3);
            cv_rect(x, yy, w, 3, mute ? K_DIM : i < 3 ? K_ACCENT : K_TEAL);
        }
        if (mute)
            cv_text(X0 + W - text_w(&FONT_XS, "muted") - 4, y - 2, &FONT_XS, "muted", K_DIM);
    }
    if (ph >= 0)
        cv_rect(ph, 18, 1, 8 * 14, K_ACCENT);
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
    cv_text(16, 18, &FONT_L, t, K_TEXT);
    cv_text(16, 50, &FONT_XS, app.hdr[s].bars == 1 ? "bar" : "bars", K_DIM);
    itoa_u(app.hdr[s].beats, t);
    cv_text(84, 18, &FONT_L, t, K_TEXT);
    cv_text(84, 50, &FONT_XS, "beats a bar", K_DIM);
    cv_text(228 - text_w(&FONT_XS, "name"), 50, &FONT_XS, "name", K_DIM);
    cv_round(12, 70, 216, 46, 6, K_PANEL);
    for (i = 0; i < 8; i++) {                    /* the name, a box a letter */
        int32_t x = 18 + i * 26;
        char c[2] = {app.hdr[s].name[i] ? app.hdr[s].name[i] : ' ', 0};
        if (i == ui.name_cur)
            cv_round(x, 75, 22, 32, 4, K_TEAL_T);
        text_c(x + 11, 77, &FONT_M, c, K_TEXT);
        cv_rect(x + 3, 105, 16, 1, i == ui.name_cur ? K_ACCENT : K_LINE);
    }
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
        int32_t x = 4 + i * 29, w = 27, sel = i == proj.trk, vh = 66 * t->vol / 127;
        uint16_t c = trk_muted(i) ? K_LINE : i < 3 ? K_ACCENT : K_TEAL;
        cv_round(x, 0, w, MAIN_H, 5, sel ? K_TEAL_T : K_PANEL);
        text_c(x + w / 2, 0, &FONT_XS, TRACK_NAME[i], sel ? K_TEXT : K_DIM);
        cv_round(x + 8, 18, 11, 68, 4, K_BG);   /* volume */
        if (vh > 0)
            cv_round(x + 8, 18 + 68 - vh - 1, 11, vh, 4, c);
        cv_rect(x + 3, 94, w - 6, 2, K_LINE);   /* pan */
        cv_round(x + 3 + (w - 10) * t->pan / 127, 91, 4, 8, 2, K_TEXT);
        {                                        /* the sends: R reverb, C chorus */
            char r[6];
            itoa_u(t->rev, cat_s(r, "R"));
            text_c(x + w / 2, 99, &FONT_XS, r, K_DIM);
            itoa_u(t->cho, cat_s(r, "C"));
            text_c(x + w / 2, 113, &FONT_XS, r, K_DIM);
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
        int32_t bh = 66 * b / 127, rh = 66 * r / 127;
        char v[6];
        cv_round(x, 0, w, MAIN_H, 5, sel ? K_TEAL_T : K_PANEL);
        text_c(x + w / 2, 0, &FONT_XS, TRACK_NAME[i], sel ? K_TEXT : K_DIM);
        cv_round(x + 5, 18, 7, 68, 3, K_BG);    /* brightness */
        cv_round(x + 15, 18, 7, 68, 3, K_BG);   /* resonance */
        if (bh > 0)
            cv_round(x + 5, 18 + 68 - bh - 1, 7, bh, 3, trk_muted(i) ? K_LINE : K_TEAL);
        if (rh > 0)
            cv_round(x + 15, 18 + 68 - rh - 1, 7, rh, 3, trk_muted(i) ? K_LINE : K_AMBER);
        cv_rect(x + 3, 18 + 68 - 34, w - 6, 1, K_DIM); /* 64: neutral */
        itoa_u((uint32_t)b, cat_s(v, "B"));
        text_c(x + w / 2, 94, &FONT_XS, v, b == 64 ? K_DIM : K_TEXT);
        itoa_u((uint32_t)r, cat_s(v, "R"));
        text_c(x + w / 2, 110, &FONT_XS, v, r == 64 ? K_DIM : K_TEXT);
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
    cv_round(8, 2, 224, 61, 6, K_PANEL);
    cv_text(18, 4, &FONT_XS, "Reverb", K_DIM);
    cv_text(18, 18, &FONT_L, REV_NAME[proj.rev_type <= 4u ? proj.rev_type : 0], K_TEXT);
    cv_round(18, 53, 204, 5, 2, K_LINE);
    cv_round(18, 53, 204 * proj.rev_level / 127 + 2, 5, 2, K_TEAL);
    cv_round(8, 68, 224, 61, 6, K_PANEL);
    cv_text(18, 70, &FONT_XS, "Chorus", K_DIM);
    itoa_u(proj.cho_type + 1u, cat_s(t, "Chorus "));
    cv_text(18, 84, &FONT_L, t, K_TEXT);
    cv_round(18, 119, 204, 5, 2, K_LINE);
    cv_round(18, 119, 204 * proj.cho_level / 127 + 2, 5, 2, K_TEAL);
    cv_blit(0, MAIN_Y);
}
/* -------------------------------------------------- the song's pages --- */
static void song_head(const char *what)
{
    char t[32], *e;
    const song_t *g = csong();
    e = itoa_u(proj.song + 1u, cat_s(t, "Song "));
    e = cat_s(e, ", ");
    if (g->nrows)
        cat_s(itoa_u(g->nrows, e), g->nrows == 1 ? " row" : " rows");
    else
        cat_s(e, "no rows");
    cv_text(8, 0, &FONT_XS, t, K_DIM);
    if (what)
        cv_text(232 - text_w(&FONT_XS, what), 0, &FONT_XS, what, K_DIM);
}
/* STYLE (ENV): the song's six patterns, a row each; the three the knobs turn on this page (P_STYLE: Intro, Main A,
 * Main B; P_STYLE2: the fills and the Ending) lit, numbered by their knob */
static void draw_style(void)
{
    const song_t *g = csong();
    uint32_t first = ui.page == P_STYLE2 ? 3u : 0u, i;
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
        int32_t y = 16 + (int32_t)i * 19;
        int on = i >= first && i < first + 3u;
        char *e, k[2] = {(char)('1' + i - first), 0};
        if (on)
            cv_round(4, y, 232, 18, 5, K_PANEL);
        cv_round(8, y + 1, 76, 16, 5, sect_colour((int)i + 1));
        text_c(8 + 38, y, &FONT_S, SECT_NAME[i + 1], K_BG);
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
        text_fit(92, y, on ? &FONT_B : &FONT_S, t, 122, K_TEXT);   /* the knobs' three: bold */
        if (on)
            cv_text(222, y + 1, &FONT_XS, k, K_DIM);
    }
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
static void draw_setup(void)
{
    uint32_t h = hash(2166136261u, 8192u + proj.clock + ((uint32_t)proj.mout << 2) + ((uint32_t)proj.leds << 8) +
                                       ((uint32_t)(qb_status() + 8) << 12));
    char t[16];
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    setup_row(2, "Clock", proj.clock ? "MIDI in" : "internal");
    setup_row(30, "MIDI out", proj.mout ? "on" : "off");
    setup_row(58, "Lights", LIGHTS_NAME[proj.leds % 3u]);
    bank_text(t);
    setup_row(86, "QY70 styles", t);
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
    cv_round(8, 78, 224, 50, 6, K_PANEL);
    cv_text(18, 82, &FONT_XS, "version", K_DIM);
    cv_text(18, 98, &FONT_B, "Polly " GM_VERSION, K_TEXT);
    itoa_u(x, cat_s(t, "xruns "));
    cv_text(222 - text_w(&FONT_XS, t), 82, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}

/* HOME held: the keys' shortcuts. The five sections (black keys 1-5; the one playing, or next, outlined), then
 * the 16 loops (white keys 1-16) in two columns by name, "-" for an empty one; the one playing outlined teal, the
 * one asked for (from the next bar line) coral */
static void draw_home_held(void)
{
    static const uint8_t SECT[5] = {SEC_INTRO, SEC_A, SEC_B, SEC_FILL, SEC_END};
    static const char *const LABEL[5] = {"Intro", "Main A", "Main B", "Fill", "Ending"};
    int n = arr_next_sect, cur = n >= SEC_INTRO && n <= SEC_FILL ? n : arr_sect, ln = arr_loop_now, i;
    int q = app.mode == ARR_LOOP && ln != app.loop ? app.loop : -1;
    uint32_t h = hash(2166136261u, 32768u + (uint32_t)cur + ((uint32_t)(uint8_t)ln << 8) + ((uint32_t)(uint8_t)q << 16));
    for (i = 0; i < ARR_LOOPS; i++)
        h = hash(h, songs.loop[i].bars | (uint32_t)(uint8_t)songs.loop[i].name[0] << 8);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    if (cur == SEC_FILL_AB || cur == SEC_FILL_BA)
        cur = SEC_FILL;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < 5; i++) {
        int32_t x = 8 + i * 45;
        if (SECT[i] == cur)
            cv_round(x - 2, 0, 45, 24, 6, K_TEXT);
        cv_round(x, 2, 41, 20, 6, sect_colour(SECT[i]));
        text_c(x + 20, 4, &FONT_XS, LABEL[i], K_BG);
    }
    for (i = 0; i < ARR_LOOPS; i++) {                /* two columns, down then across */
        int32_t x = 6 + (i / 8) * 116, y = 28 + (i % 8) * 13;
        const loop_t *l = &songs.loop[i];
        char t[16], *e;
        if (i == ln || i == q) {
            cv_round(x, y, 112, 13, 4, i == ln ? K_TEAL : K_ACCENT);
            cv_round(x + 2, y + 2, 108, 9, 3, K_BG);
        }
        itoa_u((uint32_t)i + 1u, cat_s(t, "L"));
        cv_text(x + 4, y - 1, &FONT_XS, t, K_DIM);
        if (l->bars) {
            loop_label(i, t);
            for (e = t; *e && *e != ' '; e++)
                ;
            text_fit(x + 28, y - 1, &FONT_XS, *e ? e + 1 : e, 80, K_TEXT);
        } else
            cv_text(x + 28, y - 1, &FONT_XS, "-", K_LINE);
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
/* the Style knob (HOME's KNOB 1, the STYLE pages' KNOB 4): the song's style (a QY70 bank style's
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
/* TRANSPOSE (the song's, -6..+5; loops and songs, not the keys' chords): its note, "C", "D", "Bb" */
static void transpose_knob(knob_t *k)
{
    int x = csong()->transpose;
    kv(k, "Transpose", KEY_NAME[(x + 12) % 12], x + 6, 11);
}
/* HOME: the song's style, tempo, transpose and MODE */
static void home_knobs(knob_t *k)
{
    style_knob(&k[0]);
    kn(&k[1], "Tempo", csong()->tempo, 240);
    transpose_knob(&k[2]);
    kv(&k[3], "Mode", MODE_NAME[app.mode % 3u], app.mode, 2);
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
/* the other pages: STYLE, Setup; SONG and LOOP are names only until their screens come */
static void song_page_knobs(knob_t *k)
{
    const song_t *g = csong();
    char b[12];
    switch (ui.page) {
    case P_SONG:
        kv(&k[0], "Row", "-", 0, 0);
        kv(&k[1], "Loop", "-", 0, 0);
        kv(&k[2], "Section", "-", 0, 0);
        kv(&k[3], "Repeat", "-", 0, 0);
        break;
    case P_LOOP:
        kv(&k[0], "Root", "-", 0, 0);
        kv(&k[1], "Type", "-", 0, 0);
        kv(&k[2], "Bass", "-", 0, 0);
        kv(&k[3], "Step", "-", 0, 0);
        break;
    case P_LOOP2:
        kv(&k[0], "Length", "-", 0, 0);
        kv(&k[1], "Beats", "-", 0, 0);
        kv(&k[2], "Name", "-", 0, 0);
        break;
    case P_STYLE: case P_STYLE2: {
        int first = ui.page == P_STYLE2 ? 3 : 0;
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
    case P_SETUP:
        kv(&k[0], "Clock", proj.clock ? "MIDI" : "intern.", proj.clock, 1);
        kv(&k[1], "MIDI out", proj.mout ? "on" : "off", proj.mout, 1);
        kv(&k[2], "Lights", LIGHTS_NAME[proj.leds % 3u], proj.leds, 2);
        break;
    default: break;
    }
}
/* the knobs of page ui.page (the strip; a list's KNOB rows) */
static void page_knobs(knob_t *k)
{
    memset(k, 0, 4 * sizeof *k);
    if (ui.page == P_HOME)
        home_knobs(k);
    else if (ui.page >= P_PATTERN && ui.page <= P_MASTER)
        edit_knobs(k);
    else
        song_page_knobs(k);
}

static void draw_knobs(void)
{
    knob_t k[4];
    uint32_t h = hash(2166136261u, ui.page | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    page_knobs(k);
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
    cv_round(4, 0, 232, KNB_H - 2, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int32_t x = (int32_t)i * 60, cx = x + 30;
        int hot = now_touch && ui.touched == (int)i;
        if (i)
            cv_rect(x, 10, 1, KNB_H - 22, K_LINE);
        if (!k[i].name)
            continue;
        text_c(cx, 1, &FONT_XS, k[i].name, hot ? K_TEXT : K_DIM);
        if (upper_only(k[i].val) && text_w(&FONT_M, k[i].val) <= 58)
            text_c(cx, 14, &FONT_M, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_B, k[i].val) <= 58)   /* FONT_M has no lower case: "Bb", "off" */
            text_c(cx, 19, &FONT_B, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_XS, k[i].val) <= 58)  /* a long name: "Andalusian", "8BtRock1" */
            text_c(cx, 20, &FONT_XS, k[i].val, hot ? K_ACCENT : K_TEXT);
        else
            text_fit(x + 1, 20, &FONT_XS, k[i].val, 58, hot ? K_ACCENT : K_TEXT);
        if (k[i].max > 0) {
            int32_t bw = 44 * k[i].num / k[i].max;
            cv_round(cx - 22, 43, 44, 6, 3, K_LINE);
            if (bw > 0)
                cv_round(cx - 22, 43, bw < 6 ? 6 : bw, 6, 3, hot ? K_ACCENT : K_TEAL);
        }
    }
    cv_blit(0, KNB_Y);
}


/* ------------------------------------------------- tag, footer, cards --- */
/* text in parts: "KEYS: CHORDS|REC: WRITE" draws a dot between the parts (the font has no middle dot) */
#define SEP_W 13
static int32_t segs_w(const felucca_font_t *f, const char *s)
{
    char b[48];
    int32_t w = 0;
    uint32_t n = 0;
    for (;; s++) {
        if (*s == '|' || !*s) {
            b[n] = 0;
            w += text_w(f, b);
            n = 0;
            if (!*s)
                return w;
            w += SEP_W;
        } else if (n < sizeof b - 1u)
            b[n++] = *s;
    }
}
static int32_t segs(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    char b[48];
    uint32_t n = 0;
    for (;; s++) {
        if (*s == '|' || !*s) {
            b[n] = 0;
            x = cv_text(x, y, f, b, c);
            n = 0;
            if (!*s)
                return x;
            cv_round(x + 5, y + 8, 3, 3, 1, c);
            x += SEP_W;
        } else if (n < sizeof b - 1u)
            b[n++] = *s;
    }
}
static char *upcase(char *s)
{
    for (char *c = s; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
    return s;
}
/* "C1 PIANO": the selected track and its sound */
static char *track_tag(char *t)
{
    cat_s(cat_s(cat_s(t, TRACK_NAME[proj.trk]), " "), trk_sound(&app.trk[pslot()][proj.trk]));
    return upcase(t);
}
/* the tag over the knobs: whose they are (the spec's Screens) */
static void page_tag(char *t)
{
    char *e;
    *t = 0;
    switch (ui.page) {
    case P_HOME:
        if (app.mode == ARR_LOOP) {
            loop_label(app.loop, t);
            upcase(t);
        } else if (app.mode == ARR_SONG)
            itoa_u(arr_row + 1u, cat_s(t, "ROW "));
        else
            cat_s(t, "FREE");
        break;
    case P_SONG:
        itoa_u(app.row + 1u, cat_s(t, "ROW "));
        break;
    case P_LOOP: case P_LOOP2: {                     /* the loop and where it is: "L3|BAR 2.3" */
        place_t pl;
        const loop_t *l = &songs.loop[app.loop % ARR_LOOPS];
        uint32_t bt = l->beats ? l->beats : 4u, b = 0;
        place_read(&pl);
        if (pl.loop == app.loop)
            b = pl.lbeat;
        e = itoa_u(app.loop + 1u, cat_s(t, "L"));
        e = itoa_u(b / bt + 1u, cat_s(e, "|BAR "));
        *e++ = '.';
        itoa_u(b % bt + 1u, e);
        break;
    }
    case P_PATTERN: case P_PATSET: case P_TRACK: case P_TRACK2:
        track_tag(t);
        break;
    case P_MIXER: case P_TONE:
        cat_s(cat_s(t, "MIXER: "), TRACK_NAME[proj.trk]);
        break;
    case P_MASTER:
        cat_s(t, "MASTER");
        break;
    case P_STYLE: case P_STYLE2:
        cat_s(cat_s(t, "STYLE: "), song_style() < 0 ? "-" : style_label(song_style()));
        upcase(t);
        break;
    default:
        cat_s(t, "SETUP");
        break;
    }
}
static void draw_tag(void)
{
    char t[40];
    int g = page_group(ui.page), n = PAGE_BTN[g].n, d = ui.page - PAGE_BTN[g].first;
    int32_t r = 232, x, w;
    uint32_t h;
    page_tag(t);
    h = hash(2166136261u, ui.page);
    for (const char *c = t; *c; c++)
        h = hash(h, (uint8_t)*c);
    if (h == ui.sig[3])
        return;
    ui.sig[3] = h;
    cv_begin(240, TAG_H, K_BG);
    if (n > 1) {                                     /* which of the button's pages: o * */
        r -= n * 9 - 3;
        for (int i = 0; i < n; i++)
            cv_round(r + i * 9, 3, 6, 6, 3, i == d ? K_TEAL : K_LINE);
        r -= 8;
    }
    while ((w = segs_w(&FONT_XS, t)) > r - 48 && *t)   /* (a long sound's name: cut) */
        t[slen(t) - 1] = 0;
    x = r - w;
    segs(x, -2, &FONT_XS, t, K_TEXT);
    cv_rect(8, 6, x - 16, 1, K_LINE);
    cv_blit(0, TAG_Y);
}

/* the footer: what the keys do here (the spec's Keys table); in a list, a question, HOME held: what does what */
static void page_footer(char *t)
{
    *t = 0;
    if (ui.ask) {                                    /* (a message meanwhile: here, the question stays up) */
        if (plat_ms() < ui.msg_until) {
            cat_s(cat_s(cat_s(t, ui.msg[0]), " "), ui.msg[1]);
            upcase(t);
        } else
            cat_s(t, "SEL: YES|HOME: NO");
        return;
    }
    if (home_shown()) {
        cat_s(t, "WHITE: A LOOP|BLACK: A SECTION");
        return;
    }
    if (ui.card) {
        cat_s(t, "LET GO: NOTHING CHANGES");
        return;
    }
    if (!ui.any_btn) {
        cat_s(t, "HOLD ANY BUTTON: WHAT IT DOES");
        return;
    }
    if (lst.open && lst.n) {
        int k = lst.row[lst.sel].kind;
        cat_s(t, k == LK_NAME ? "ALGO: LETTER|SEL: NEXT|HOME: BACK"
               : k == LK_ACTION ? "SELECT: MOVE|SEL: DO IT|HOME: BACK"
               : k == LK_PICK ? "ALGO: PICK|SEL: DO IT|HOME: BACK" : "SELECT: MOVE|ALGO: CHANGE|HOME: BACK");
        return;
    }
    switch (ui.page) {
    case P_HOME:
        cat_s(t, app.mode == ARR_FREE ? "KEYS: CHORDS|REC: NEW LOOP" : "KEYS: CHORDS|REC: WRITE");
        break;
    case P_LOOP: case P_LOOP2:
        cat_s(t, "KEYS: CHORDS|REC: WRITE|OCT+: SKIP");
        break;
    case P_SONG:
        cat_s(t, "WHITE: ROW'S LOOP|BLACK: MUTES");
        break;
    case P_PATTERN: case P_PATSET: case P_TRACK: case P_TRACK2: {   /* "KEYS: C1 PIANO|REC: RECORD" */
        char s[40];
        track_tag(s);
        for (;;) {
            cat_s(cat_s(cat_s(t, "KEYS: "), s), "|REC: RECORD");
            if (segs_w(&FONT_XS, t) <= 224 || !*s)
                break;
            s[slen(s) - 1] = 0;                    /* a long sound's name: cut */
        }
        break;
    }
    case P_MIXER: case P_TONE:
        cat_s(t, "BLACK: TRACK");
        break;
    case P_MASTER:
        cat_s(t, "KNOBS: REVERB AND CHORUS");
        break;
    case P_STYLE: case P_STYLE2:
        cat_s(t, "KNOBS: THE SECTIONS|PRESETS: NEXT STYLE");
        break;
    case P_SETUP:
        cat_s(t, "KNOBS: CLOCK, MIDI OUT, LIGHTS");
        break;
    default:
        cat_s(t, "CPU, VOICES, VERSION");
        break;
    }
}
static void draw_footer(void)
{
    char t[64];
    uint32_t h = 2166136261u;
    page_footer(t);
    for (const char *c = t; *c; c++)
        h = hash(h, (uint8_t)*c);
    if (h == ui.sig[4])
        return;
    ui.sig[4] = h;
#if defined(GM_HOST) && !defined(GM_WEB)        /* the simulator: a footer cut is a bug */
    if (8 + segs_w(&FONT_XS, t) > 236) {
        printf("  FAIL footer too wide: %s\n", t);
        msg_too_wide++;
    }
#endif
    cv_begin(240, FOOT_H, K_BG);
    segs(8, -1, &FONT_XS, t, K_DIM);
    cv_blit(0, FOOT_Y);
}

/* the help cards: a button held alone for a second says what it does on this page (pages: bits of P_*). HOME
 * has none: held, it shows the loops and the sections (draw_home_held) */
#define PG(p) (1u << (p))
#define PG_ANY 0xFFFFu
static const struct { uint8_t btn; uint16_t pages; const char *title, *l1, *l2; } CARD[] = {
    {B_ARP, PG_ANY, "Loops", "Your chord loops. Play chords.", "REC writes them: at the cursor, or in time."},
    {B_SEQ, PG_ANY, "Pattern", "The section's eight tracks; again: its length and name.", 0},
    {B_EDIT, PG_ANY, "Track", "The selected track's sound and range; again: its keys.", 0},
    {B_LFO, PG_ANY, "Mixer", "Volume, pan and sends; again: the tone. Black keys pick the track.", 0},
    {B_FX, PG_ANY, "Master FX", "The reverb and the chorus every track sends to.", 0},
    {B_ENV, PG_ANY, "Style", "The six sections' patterns. PRESETS: the next style.", 0},
    {B_GLO, PG_ANY, "Setup", "Clock, MIDI out, lights; again: the panel.", 0},
    {B_SEL, PG_ANY, "Lists", "This page as a list, MORE, actions.", "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SAVE, PG_ANY, "Save", "Tap: save. Hold + a white key: copy here.", 0},
    {B_REC, PG(P_HOME) | PG(P_LOOP) | PG(P_LOOP2), "Write chords",
     "Into the loop playing: at the cursor, or in time as it plays.", "On HOME in FREE: a new loop."},
    {B_REC, PG(P_PATTERN) | PG(P_PATSET) | PG(P_TRACK) | PG(P_TRACK2), "Record the track",
     "What the keys play goes into the selected track.", 0},
    {B_REC, PG(P_SONG), "Record the song's rows", "With PLAY: the loops and sections you pick.", 0},
    {B_REC, PG_ANY, "Record", "Nothing to record on this page.", 0},
    {B_PLAY, PG_ANY, "Play", "Start and stop. In SONG: from the row on the SONG screen.", 0},
    {B_OCTDN, PG(P_LOOP) | PG(P_LOOP2), "Cursor", "OCT-: back a step. OCT+: skips a step (the chord goes on).", 0},
    {B_OCTUP, PG(P_LOOP) | PG(P_LOOP2), "Cursor", "OCT+ skips a step (the chord goes on); OCT-: back a step.", 0},
    {B_OCTDN, PG_ANY, "Octave", "The keys an octave down.", "Both held 5 s: update mode."},
    {B_OCTUP, PG_ANY, "Octave", "The keys an octave up.", "Both held 5 s: update mode."},
};
#define NCARDS (sizeof CARD / sizeof CARD[0])
static int card_find(int btn)                        /* the card for btn on this page, -1 none */
{
    for (uint32_t i = 0; i < NCARDS; i++)
        if (CARD[i].btn == btn && (CARD[i].pages >> ui.page & 1u))
            return (int)i;
    return -1;
}
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
/* s in lines of w pixels (at spaces), from y; the y after */
static int32_t wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c)
{
    char line[48];
    while (*s) {
        uint32_t n = 0, cut = 0;
        while (s[n] && n < sizeof line - 1u) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (text_w(f, line) > w)
                break;
            n++;
            if (!s[n] || s[n] == ' ')
                cut = n;
        }
        if (!cut)
            cut = n ? n : 1;                         /* one word too long: cut it */
        line[cut] = 0;
        cv_text(x, y, f, line, c);
        y += 17;
        s += cut;
        while (*s == ' ')
            s++;
    }
    return y;
}
static void draw_card(void)
{
    int i = card_find(ui.card - 1);
    uint32_t h = hash(2166136261u, 0xCA000u + ui.card + ((uint32_t)ui.page << 8));
    int32_t x, y;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(8, 4, 224, MAIN_H - 8, 8, K_PANEL);
    x = pill(18, 14, 0, BTN_LABEL[(ui.card - 1) % NB], K_TEAL, K_BG);
    if (i >= 0) {
        cv_text(x + 10, 15, &FONT_B, CARD[i].title, K_TEXT);
        y = wrap(20, 44, 200, &FONT_S, CARD[i].l1, K_TEXT);
        if (CARD[i].l2)
            wrap(20, y + 4, 200, &FONT_S, CARD[i].l2, K_DIM);
    }
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------- lists --- */
static void list_add(const char *name, const char *val, int kind, int id)
{
    if (lst.n >= LIST_ROWS)
        return;
    lst.row[lst.n].name = name;
    lst.row[lst.n].val[0] = 0;
    if (val)
        for (int i = 0; i < 11 && val[i]; i++)
            lst.row[lst.n].val[i] = val[i], lst.row[lst.n].val[i + 1] = 0;
    lst.row[lst.n].kind = (uint8_t)kind;
    lst.row[lst.n].id = (uint8_t)id;
    lst.n++;
}
static int list_loops(void) { return ui.page == P_HOME || ui.page == P_LOOP || ui.page == P_LOOP2; }
/* the NAME a list edits: the loop's (LOOP), the pattern's (Pattern setup); 0 none */
static char *list_name(void)
{
    if (ui.page == P_LOOP || ui.page == P_LOOP2)
        return songs.loop[app.loop % ARR_LOOPS].name;
    if (ui.page == P_PATTERN || ui.page == P_PATSET)
        return app.hdr[pslot()].name;
    return 0;
}
/* the list of this screen: HOME and SONG on their own, the other buttons' pages together */
static void list_build(void)
{
    int keep = ui.page, g = page_group(keep), f = PAGE_BTN[g].first, n = PAGE_BTN[g].n;
    knob_t k[4];
    char *nm = list_name();
    lst.n = 0;
    if (keep == P_HOME || keep == P_SONG)
        f = keep, n = 1;
    for (int p = f; p < f + n; p++) {
        ui.page = (uint8_t)p;
        page_knobs(k);
        for (int i = 0; i < 4; i++) {
            int seen = 0;
            for (int j = 0; j < lst.n; j++)          /* a knob on both pages (STYLE's Style): once */
                seen |= k[i].name && same(lst.row[j].name, k[i].name) && same(lst.row[j].val, k[i].val);
            if (k[i].name && !seen)
                list_add(k[i].name, k[i].val, LK_KNOB, p << 2 | i);
        }
    }
    ui.page = (uint8_t)keep;
    if (nm) {
        char v[9];
        for (int i = 0; i < 8; i++)
            v[i] = nm[i] ? nm[i] : ' ';
        v[8] = 0;
        list_add("MORE", 0, LK_MORE, 0);
        list_add("Letters", v, LK_NAME, 0);
    }
    if (list_loops()) {
        list_add(ACTION_NAME[A_LOAD_PROG], TEMPLATES[ui.pick_prog % NTEMPLATES].name, LK_PICK, A_LOAD_PROG);
        list_add(ACTION_NAME[A_CLEAR_LOOP], 0, LK_ACTION, A_CLEAR_LOOP);
    } else if (keep == P_SONG) {
        char v[8];
        if (ui.pick_song % ARR_SONGS == proj.song)    /* COPY SONG TO: another song */
            ui.pick_song = (uint8_t)((proj.song + 1u) % ARR_SONGS);
        itoa_u(ui.pick_song % ARR_SONGS + 1u, cat_s(v, "Song "));
        list_add(ACTION_NAME[A_INSERT_ROW], 0, LK_ACTION, A_INSERT_ROW);
        list_add(ACTION_NAME[A_DELETE_ROW], 0, LK_ACTION, A_DELETE_ROW);
        list_add(ACTION_NAME[A_CLEAR_SONG], 0, LK_ACTION, A_CLEAR_SONG);
        list_add(ACTION_NAME[A_COPY_SONG], v, LK_PICK, A_COPY_SONG);
    } else if (keep >= P_PATTERN && keep <= P_MASTER) {
        list_add(ACTION_NAME[A_ERASE_TRACK], 0, LK_ACTION, A_ERASE_TRACK);
        list_add(ACTION_NAME[A_CLEAR_PATTERN], 0, LK_ACTION, A_CLEAR_PATTERN);
        list_add(ACTION_NAME[A_BACK_TO_STYLE], 0, LK_ACTION, A_BACK_TO_STYLE);
    }
    if (lst.sel >= lst.n)
        lst.sel = (uint8_t)(lst.n ? lst.n - 1 : 0);
}
static void glyph_enter(int32_t x, int32_t y, uint16_t c)   /* the action's mark */
{
    cv_rect(x + 8, y, 2, 7, c);
    cv_rect(x + 2, y + 5, 8, 2, c);
    cv_rect(x + 2, y + 3, 1, 6, c);
    cv_rect(x + 1, y + 4, 1, 4, c);
    cv_rect(x, y + 5, 1, 2, c);
}
static void glyph_pick(int32_t x, int32_t y, uint16_t c)    /* a choice first */
{
    for (int j = 0; j < 4; j++)
        cv_rect(x + j, y + j, 1, 9 - 2 * j, c);
}
#define LIST_RH 18
static void draw_list(void)
{
    uint32_t h = hash(2166136261u, 0x4C000u | lst.sel | (uint32_t)lst.top << 8 | (uint32_t)ui.name_cur << 16), i;
    list_build();
    h = hash(h, lst.n);
    for (i = 0; i < lst.n; i++) {
        h = hash(h, lst.row[i].kind | (uint32_t)lst.row[i].id << 4);
        for (const char *c = lst.row[i].val; *c; c++)
            h = hash(h, (uint8_t)*c);
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = lst.top; i < lst.n && i < lst.top + LIST_SHOWN; i++) {
        int32_t y = 3 + (int32_t)(i - lst.top) * LIST_RH;
        int on = i == lst.sel, k = lst.row[i].kind;
        uint16_t fg = on ? K_BG : K_TEXT, dim = on ? K_BG : K_DIM;
        const char *v = lst.row[i].val;
        if (k == LK_MORE) {
            int32_t x = cv_text(14, y + 1, &FONT_XS, "MORE", K_DIM);
            cv_rect(x + 6, y + 9, 222 - x, 1, K_LINE);
            continue;
        }
        cv_round(6, y, 222, LIST_RH - 1, 5, on ? K_TEAL : K_PANEL);
        if (k == LK_KNOB) {
            cv_text(14, y, &FONT_S, lst.row[i].name, dim);
            text_fit(218 - text_w(&FONT_B, v), y, &FONT_B, v, 120, fg);
        } else if (k == LK_NAME) {                   /* the letters, the one ALGORITHM turns underlined */
            cv_text(14, y, &FONT_S, lst.row[i].name, dim);
            for (int j = 0; j < 8; j++) {
                char c[2] = {v[j] == ' ' ? '_' : v[j], 0};
                int32_t x = 100 + j * 15;
                text_c(x + 6, y, &FONT_B, c, v[j] == ' ' ? dim : fg);
                if (on && j == ui.name_cur)
                    cv_rect(x, y + 14, 13, 2, K_ACCENT);
            }
        } else {
            cv_text(14, y, &FONT_B, lst.row[i].name, fg);
            if (k == LK_PICK) {
            {
                int32_t room = 206 - (14 + text_w(&FONT_B, lst.row[i].name) + 6), w = text_w(&FONT_S, v);
                text_fit(206 - (w < room ? w : room), y, &FONT_S, v, room, fg);
            }
                glyph_pick(213, y + 4, fg);
            } else
                glyph_enter(208, y + 5, fg);
        }
    }
    if (lst.n > LIST_SHOWN) {                        /* where in the list */
        int32_t th = LIST_SHOWN * LIST_RH - 2, bh = th * LIST_SHOWN / lst.n, by = th * lst.top / lst.n;
        cv_round(232, 3, 4, th, 2, K_PANEL);
        cv_round(232, 3 + by, 4, bh, 2, K_DIM);
    }
    cv_blit(0, MAIN_Y);
}

static void draw_page(void)
{
    if (ui.card)
        draw_card();
    else if (home_shown())
        draw_home_held();
    else if (lst.open)
        draw_list();
    else if (ui.page == P_HOME)
        draw_home();
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
    else if (ui.page == P_STYLE || ui.page == P_STYLE2)
        draw_style();
    else if (ui.page == P_SETUP)
        draw_setup();
    else if (ui.page == P_PANEL)
        draw_panel();
    else
        draw_coming();                               /* SONG, LOOP: their screens come later */
}
