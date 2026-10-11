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

/* the pages (the spec's Buttons table): a button opens its first page; SELECT turns them */
enum { P_HOME, P_SONG, P_SONG2, P_SONG3, P_LOOP, P_LOOP2, P_PATTERN, P_PATSET, P_TRACK, P_TRACK2, P_MIXER, P_TONE, P_MASTER,
       P_STYLE, P_STYLE2, P_SETUP, P_PANEL, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Home", "Song", "Song", "Song", "Loop", "Loop", "Pattern", "Pattern setup", "Track",
                                              "Track keys", "Mixer", "Mixer tone", "Master FX", "Style", "Style",
                                              "Setup", "Panel"};
/* the page buttons: a button opens its first page, SELECT the next (the tag's dots: which). HOME's four: HOME, then
 * SONG's three pages */
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_HOME, 4}, {B_ARP, P_LOOP, 2}, {B_SEQ, P_PATTERN, 2}, {B_EDIT, P_TRACK, 2}, {B_LFO, P_MIXER, 2},
    {B_FX, P_MASTER, 1}, {B_ENV, P_STYLE, 2}, {B_GLO, P_SETUP, 2}};
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
static int page_group(int p)                         /* the PAGE_BTN entry page p is on */
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (p >= PAGE_BTN[i].first && p < PAGE_BTN[i].first + PAGE_BTN[i].n)
            return (int)i;
    return 0;
}
/* the tracks: a pattern's eight, then the song's two lead tracks (ARR_LD1, ARR_LD1 + 1) */
static const char *const TRACK_NAME[ARR_ALLTRK] = {"D1", "D2", "PC", "BA", "C1", "C2", "C3", "C4", "LD1", "LD2"};
/* SEC_* (SEC_FILL: the Fill asked for, AB or BA by the Main playing) */
static const char *const SECT_NAME[SEC_FILL + 1] = {"", "Intro", "Main A", "Main B", "Fill AB", "Fill BA", "Ending", "Fill"};
static const char *const KEY_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const GRID_NAME[ARR_NQ] = {"8", "8T", "16", "16T", "32", "off"};
static const char *const TYPE_NAME[NXT] = {"Bypass", "Parallel", "Chord 1", "Chord 2", "Bass"};
static const char *const TYPE_SHORT[NXT] = {"Bypass", "Parall.", "Chord1", "Chord2", "Bass"};
static const char *const REV_NAME[5] = {"Off", "Large hall", "Hall", "Chamber", "Room"};
static const char *const REV_SHORT[5] = {"off", "L.hall", "Hall", "Chamb.", "Room"};
static const char *const LIGHTS_NAME[3] = {"off", "on", "notes"};
static const char *const CLICK_NAME[3] = {"off", "rec", "on"};   /* ARR_CLICK_* */
static const char *const MODE_NAME[3] = {"FREE", "LOOP", "SONG"};   /* ARR_* */
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#";
#define NNAME_CHARS ((int)sizeof NAME_CHARS - 1)
/* a loop's name (mixed case: "Verse", as the factory's "Pop") */
static const char LOOP_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-#";
#define NLOOP_CHARS ((int)sizeof LOOP_CHARS - 1)
/* the cursor's step (app.cstep): a beat, half a bar, a bar */
static const char *const STEP_NAME[3] = {"beat", "half", "bar"};
/* NAME (LOOP page 2): the presets the knob turns through, then "Loop N" (its own number) */
static const char *const LOOP_NAME_LIST[] = {"Verse", "Chorus", "Bridge", "Pre", "Intro", "Outro", "Solo", "Break"};
#define NLOOP_NAMES ((int)(sizeof LOOP_NAME_LIST / sizeof LOOP_NAME_LIST[0]))
enum { KM_NONE, KM_PLAY, KM_CHORD };                 /* what a held key does */

static songs_t songs EN_POOL;                        /* the four songs (the selected one plays) */
/* the selected song's 16 loops (each song has its own: loaded with it, stored in a slot when not the defaults); the
 * engine reads sloops.loop (arr_loops). Written inside songs_write (arr_edit) */
static loops_part_t sloops EN_POOL;
static pattern_t scratch EN_POOL;                    /* a pattern being loaded, saved or edited */
/* the selected song's lead notes (arr.h leads_t, two parts as stored: 7.6 KB) and its leads' sounds; the engine
 * reads it (arr_leads). Written inside arr_edit, gen counted (ui.c lead_write) */
static leads_t leads EN_POOL;
/* the QY70 bank's styles decoded (qybank.h): the one the song plays, and one more, so another can be decoded while
 * the first still plays (a slot is re-pointed only once its new style is whole) */
static qb_style_t qbuf[2] EN_POOL;

/* the app's state that outlives the screen (ui_init runs again after an update session) */
static struct {
    uint8_t dirty;                                   /* the project changed since it was saved */
    uint8_t mode;                                    /* ARR_FREE / ARR_LOOP (HOME's LOOP knob), ARR_SONG (PLAY on SONG) */
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
    uint8_t songs_dirty;                             /* the songs object changed (a song, a slot's pattern) */
    uint8_t lpdirty;                                 /* the song's loops may have changed (songs_write): saved when
                                                      * they differ from what was loaded or saved (lpsum) */
    int8_t lpslot;                                   /* the slot the song's loops are saved in (taken when they first
                                                      * differ from the defaults), -1 none */
    uint32_t lpsum;                                  /* the loops' checksum when loaded or saved */
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
    uint8_t picked;                                  /* app.loop was set since start-up (picked, playing, ARP): until
                                                      * then ARP opens the first empty loop */
    int8_t lslot[LEAD_PARTS];                        /* the slot each lead part of the song is saved in (taken when the
                                                      * part is first needed: no pattern takes it meanwhile), -1 none */
    uint8_t ldirty;                                  /* the lead parts changed since loaded or saved (bit k: part k) */
    uint8_t lead;                                    /* a lead track selected (ALGORITHM past C4): 1 LD1, 2 LD2; 0 none,
                                                      * proj.trk's (proj.trk keeps the pattern's track meanwhile) */
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
    struct { uint8_t loop, prog, song, trk, slot, lrow, ltrk, free, lfrom; } tgt;   /* lrow: a row (255 all), ltrk: a
                                                      * lead 0, 1; free: SLOTS' slot; lfrom: COPY LD's row */
    uint8_t pick_prog, pick_song, pick_lrow;         /* the lists' choices: LOAD PROGRESSION, COPY SONG TO, COPY LD FROM */
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
    int32_t lw_abs, lw_now;                          /* REC writing in time: the latest beat written (arr_beats; -1 none),
                                                      * the one the chord being written lands on */
    uint32_t wrote_t;                                /* a chord written (REC): when, into which loop, on which beat */
    uint8_t wrote_loop, wrote_beat;                  /* (it flashes coral on HOME's bars and LOOP's grid a moment) */
    int8_t loop_seen;                                /* arr_loop_now as last seen (app.loop follows its changes) */
    uint8_t role_was;                                /* what the keys did last frame (REC ends when that changes) */
    uint8_t kmode[NKEYS], knote[NKEYS];              /* each held key: KM_*, its note */
    uint32_t sig[6];                                 /* the bands' hashes: header, main, knobs, tag, footer */
    uint8_t row_follow;                              /* SONG's cursor follows the row playing (from PLAY until ROW turns) */
    uint8_t song_rec;                                /* REC on SONG: the rows written from HOME + keys, the mutes */
    uint32_t ksound;                                 /* stopped: the chord keys sounding on C1's channel (bit = key) */
    uint8_t follow_told, follow_hint;                /* REC kept a track with notes on its chord type: told (bit t), the
                                                      * footer's hint showing (ui.c rec_as_played) */
    uint8_t ksound_ch[NKEYS];                        /* ... the channel each one went to */
    uint8_t click_sent;                              /* the click's mode and the app's REC as the engine has them (+1) */
    uint32_t count_sent;                             /* ... a free take's count-in (arr_count_in) */
    uint32_t slots_until;                            /* a NO FREE SLOT said: until then SEL opens SLOTS (the footer says) */
} ui;
/* one of SONG's three pages */
static int song_pg(int p) { return p >= P_SONG && p <= P_SONG3; }
static int song_page(void) { return song_pg(ui.page); }
/* the track selected: the keys play it, REC records into it, EDIT and the Mixer show it (0..7, or ARR_LD1 + k) */
static int sel_trk(void) { return app.lead ? ARR_LD1 + (app.lead - 1) % ARR_LEADS : proj.trk & 7; }

/* SEL: the screen as a list. Its knobs' values (each page of its button: KNOB rows), then MORE (settings on no
 * page) and the actions (ACTION: do it, PICK: a choice first). Built afresh to draw it; sel and top stay */
/* SUB: another list (SLOTS); SLOT: a row of that one (id: the slot) */
enum { LK_KNOB, LK_MORE, LK_NAME, LK_ACTION, LK_PICK, LK_SUB, LK_SLOT };
enum { A_LOAD_PROG, A_CLEAR_LOOP, A_PLAY_FROM, A_INSERT_ROW, A_DELETE_ROW, A_CLEAR_SONG, A_COPY_SONG, A_ERASE_TRACK,
       A_CLEAR_PATTERN, A_BACK_TO_STYLE, A_ERASE_LEAD_ROW, A_ERASE_LEAD, A_QUANT_LEAD_ROW, A_COPY_LEAD, A_MATCH_BARS, A_SLOTS,
       NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {"LOAD PROGRESSION", "CLEAR LOOP", "PLAY FROM ROW", "INSERT ROW", "DELETE ROW",
                                                  "CLEAR SONG", "COPY SONG TO", "ERASE TRACK", "CLEAR PATTERN",
                                                  "BACK TO STYLE", "ERASE LD IN ROW", "ERASE LD", "QUANTISE LD ROW",
                                                  "COPY LD FROM ROW", "BARS AS THE LOOP", "SLOTS"};
#define LIST_ROWS 24
#define LIST_SHOWN 7                                 /* rows on screen */
static struct {
    uint8_t open, n, sel, top;
    uint8_t slots;                                   /* the SLOTS list (SEL's SLOTS: the 16 shared slots) */
    uint8_t back_sel, back_top;                      /* the list it came from: its row (HOME goes back there) */
    struct { const char *name; char val[12]; uint8_t kind, id; } row[LIST_ROWS];
} lst;
/* the 16 shared slots as the SLOTS list shows them ("P03|Funk A|S1", "S2 leads 1/2", "S1 loops", "free"), made when
 * it opens and after a FREE (ui.c slots_scan: it reads the flash); kind SLOT_* (SLOT_NONE: free) */
static char slot_txt[ARR_USER_PATTERNS][28];
static uint8_t slot_kind[ARR_USER_PATTERNS];

static void song_head(const char *what);
/* track t muted: in SONG the engine's (the row's, or by hand until the next row); else the song's mute (every
 * section; ui.c gives it to the engine). A lead track (ARR_LD1 + k): the engine's in SONG, else never (leads play
 * only in a song) */
static int trk_muted(int t)
{
    if (t >= ARR_TRACKS)
        return app.mode == ARR_SONG && (arr_mutes >> (t < ARR_ALLTRK ? t : ARR_LD1) & 1u);
    return (app.mode == ARR_SONG ? arr_mutes : songs.song[proj.song].mute) >> (t & 7) & 1u;
}

/* anything not saved: the project, or a user pattern recorded into */
static int unsaved(void)
{
    if (app.dirty || app.songs_dirty || app.ldirty || app.lpdirty)
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
    l = &sloops.loop[pl->loop];
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
/* the lead track selected (app.lead): its sound and mixer, the song's */
static lead_trk_t *sel_lead(void) { return &csong()->lead[(app.lead ? app.lead - 1 : 0) % ARR_LEADS]; }
/* a lead track's sound and mixer as a pattern's track's, for the pages that draw one: type "as played" (Parallel), the
 * whole keyboard, no high key */
static void lead_as_arr(const lead_trk_t *l, arr_trk_t *t)
{
    memset(t, 0, sizeof *t);
    t->program = l->program & 127u;
    t->chan = l->chan & 15u;
    t->x.type = XT_PARALLEL;
    t->x.lo = 0;
    t->x.hi = 127;
    t->x.hikey = XT_NOKEY;
    t->vol = l->vol;
    t->pan = l->pan;
    t->rev = l->rev;
    t->cho = l->cho;
    t->bright = l->bright;
    t->reso = l->reso;
    t->flags = ARR_TF_ASPLAYED;
}
/* the row a lead's erase acts on, and its recording shows: the row playing (a song's rows playing), else SONG's cursor */
static int lead_row(void)
{
    const song_t *g = csong();
    int n = g->nrows;
    if (arr_playing && arr_mode == ARR_SONG && arr_loop_now >= 0 && arr_row < n)
        return arr_row;
    return app.row < n ? app.row : n ? n - 1 : 0;
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
static char *loop_label(int i, char *t);
/* the header: the page's name; on HOME the song and its style. Right: the song's tempo (or "ext"), REC's dot,
 * "unsaved". A message replaces it all, a question (SEL: yes, HOME: no) the message */
static void draw_header(void)
{
    char t[40];
    int msg = plat_ms() < ui.msg_until, rec = arr_recording || ui.rec_chords || ui.take || ui.song_rec, ext = arr_ext;
    int sel = home_shown();
    int un = unsaved(), ask = ui.ask, slots = lst.open && lst.slots, nfree = 0;
    for (int i = 0; slots && i < ARR_USER_PATTERNS; i++)
        nfree += slot_kind[i] == SLOT_NONE;
    uint32_t h = hash(2166136261u, ui.page | (uint32_t)msg << 8 | (uint32_t)un << 9 | (uint32_t)rec << 11 |
                                       (uint32_t)ext << 12 | (uint32_t)sel << 13 | (uint32_t)ask << 14 |
                                       (uint32_t)csong()->tempo << 16);
    h = hash(h, (uint32_t)proj.song | (uint32_t)songs.song[proj.song].sty << 8 | (uint32_t)slots << 24 | (uint32_t)nfree << 25);
    for (int k = 0; k < ARR_NSECT; k++)              /* the sections on user patterns ("Pop, A user") */
        h = hash(h, songs.song[proj.song].style[k] < ARR_USER_PATTERNS);
    if (ui.page == P_LOOP || ui.page == P_LOOP2) {   /* "L3 Verse   in 3 rows" */
        const loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
        h = hash(h, (uint32_t)app.loop | (uint32_t)loop_rows_using(csong(), app.loop % ARR_LOOPS) << 8);
        for (int i = 0; i < 8; i++)
            h = hash(h, (uint8_t)l->name[i]);
    }
    if (song_page())                                 /* "4 rows" */
        h = hash(h, 327680u + csong()->nrows);
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
        if (text_w(&FONT_B, ui.ask_q) <= 224)
            cv_text(8, 4, &FONT_B, ui.ask_q, K_BG);
        else                                         /* a long one ("ROW 3 HAS LEAD NOTES: DELETE?"): smaller, whole */
            text_fit(8, 5, &FONT_S, ui.ask_q, 228, K_BG);
    } else if (msg) {
        int32_t x = cv_text(8, 4, &FONT_B, ui.msg[0], K_TEXT);
        text_fit(x + 8, 4, &FONT_B, ui.msg[1], 232 - (x + 8), K_ACCENT);
    } else if (slots) {                              /* SLOTS: "Slots   3 free" */
        int32_t x = cv_text(8, 4, &FONT_B, "Slots", K_TEXT);
        if (nfree)
            cat_s(itoa_u((uint32_t)nfree, t), " of 16 free");
        else
            cat_s(t, "none free");
        cv_text(x + 10, 5, &FONT_S, t, nfree ? K_DIM : K_ACCENT);
    } else if (sel) {                                /* HOME held: what it does with the keys, PRESETS */
        cv_text(8, 4, &FONT_B, "HOME + a key", K_TEXT);
        cv_text(232 - text_w(&FONT_XS, "PRESETS: song"), 5, &FONT_XS, "PRESETS: song", K_DIM);
    } else {
        int32_t x = cv_text(8, 4, &FONT_B, PAGE_NAME[ui.page], K_TEXT), r = 232;
        if (ui.page == P_HOME || song_page()) {
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
        else if (ui.page == P_LOOP || ui.page == P_LOOP2) {   /* "L3 Verse", unsaved, then "in 3 rows" if room */
            char rt[16];
            int rows = loop_rows_using(csong(), app.loop % ARR_LOOPS);
            int32_t e;
            loop_label(app.loop, t);
            if (un) {
                r -= 6 + text_w(&FONT_XS, "unsaved");
                cv_text(r, 5, &FONT_XS, "unsaved", K_DIM);
            }
            e = text_fit(x + 8, 4, &FONT_B, t, r - 8 - (x + 8), K_TEAL);
            if (rows) {
                cat_s(itoa_u((uint32_t)rows, cat_s(rt, "in ")), rows == 1 ? " row" : " rows");
                if (e + 6 + text_w(&FONT_XS, rt) <= r - 6)
                    cv_text(e + 6, 5, &FONT_XS, rt, K_DIM);
            }
            cv_blit(0, 0);
            return;
        } else if (song_page()) {                    /* "4 rows", "no rows" */
            int n = csong()->nrows;
            if (n)
                cat_s(itoa_u((uint32_t)n, t), n == 1 ? " row" : " rows");
            else
                cat_s(t, "no rows");
        }
        if (un && r - 6 - text_w(&FONT_XS, "unsaved") > x + 8 &&   /* (HOME, LOOP: the extra text first; SAVE's lit) */
            ((ui.page != P_HOME && ui.page != P_LOOP && ui.page != P_LOOP2 && !song_page()) ||
             r - 6 - text_w(&FONT_XS, "unsaved") - 8 >= x + 8 + text_w(&FONT_B, t))) {
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
        } else if (ui.page == P_LOOP || ui.page == P_LOOP2 || song_page())
            text_fit(x + 8, 4, &FONT_B, t, r - 8 - (x + 8), K_TEAL);
    }
    cv_blit(0, 0);
}

/* "L3 Verse": a loop's number and name (trailing spaces cut); "L9" alone while it has its own default name ("Loop 9":
 * not "L9 Loop 9") */
static int loop_name_default(const loop_t *l, int i)
{
    char d[8] = "Loop ";
    if (i >= 9) {
        d[5] = '1';
        d[6] = (char)('0' + (i + 1) % 10);
    } else
        d[5] = (char)('1' + i);
    for (int n = 0; n < 8; n++)
        if (l->name[n] != d[n])
            return 0;
    return 1;
}
static char *loop_label(int i, char *t)
{
    const loop_t *l = &sloops.loop[i % ARR_LOOPS];
    char *e = itoa_u((uint32_t)(i % ARR_LOOPS) + 1u, cat_s(t, "L"));
    if (loop_name_default(l, i % ARR_LOOPS))
        return e;
    *e++ = ' ';
    for (int n = 0; n < 8 && l->name[n]; n++)
        *e++ = l->name[n];
    while (e[-1] == ' ')
        e--;
    *e = 0;
    return e;
}

/* the chord at beat b of loop l, for the knobs and the grid: the beat's own chord if it has one (*has = 1),
 * else the chord sounding there (loop_chord_at; at or past the loop's end: the one sounding at its last beat, not
 * beat 0's round the end); a loop with nothing at all yet (CH_NONE): C maj, so the first turn of a knob has
 * somewhere real to start from */
static chord_t cursor_chord(const loop_t *l, int b, int *has)
{
    int n = loop_len(l);
    chord_t c = loop_chord_at(l, n > 0 && b >= n ? n - 1 : b);
    *has = b >= 0 && b < loop_len(l) && b < ARR_LOOP_BEATS && l->ch[b] != LC_SAME;
    if (c.type == CH_NONE) {
        c.root = 0;
        c.type = CH_MAJ;
        c.bass = CH_NOBASS;
    }
    return c;
}
/* the loop's name, as one of LOOP_NAME_LIST (0..NLOOP_NAMES-1), else NLOOP_NAMES ("Loop N", or a custom name) */
static int loop_name_index(const loop_t *l)
{
    for (int i = 0; i < NLOOP_NAMES; i++) {
        const char *s = LOOP_NAME_LIST[i];
        int j = 0;
        for (; s[j] && l->name[j] == s[j]; j++)
            ;
        if (!s[j] && (j >= 8 || !l->name[j]))
            return i;
    }
    return NLOOP_NAMES;
}
/* the loop's name set to preset idx (0..NLOOP_NAMES-1), or NLOOP_NAMES: "Loop N" (its own number, as loop_clear) */
static void loop_set_name_preset(loop_t *l, int li, int idx)
{
    char n[8] = {0};
    const char *s = n;
    if (idx < NLOOP_NAMES)
        s = LOOP_NAME_LIST[idx];
    else if (li >= 9) {
        n[0] = 'L'; n[1] = 'o'; n[2] = 'o'; n[3] = 'p'; n[4] = ' ';
        n[5] = '1';
        n[6] = (char)('0' + (li + 1) % 10);
    } else {
        n[0] = 'L'; n[1] = 'o'; n[2] = 'o'; n[3] = 'p'; n[4] = ' ';
        n[5] = (char)('1' + li);
    }
    for (int i = 0; i < 8; i++)
        l->name[i] = 0;
    for (int i = 0; i < 8 && s[i]; i++)
        l->name[i] = s[i];
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
/* HOME's place: the engine's, as HOME shows it (stopped: the loop picked from its beat 0, and the chord PLAY starts on,
 * unless chord keys are held: that chord while they are). 1: the chord is the source's (PLAY's), not one from the keys */
static int home_place(place_t *pl)
{
    place_read(pl);
    if (app.mode == ARR_SONG && !csong()->nrows)      /* a song with no rows: no loop (not the last song's) */
        pl->loop = -1;
    if (!arr_playing) {                              /* stopped: where PLAY starts (bar 1; a song's row 1's loop), not
                                                      * where it stopped */
        pl->bar = pl->beat = pl->lbeat = 0;
        if (app.mode == ARR_SONG && csong()->nrows)
            pl->loop = csong()->row[0].loop % ARR_LOOPS;
    }
    if (!arr_playing && app.mode == ARR_LOOP) {      /* stopped: the loop picked, from its beat 0 (PLAY starts it) */
        pl->loop = app.loop % ARR_LOOPS;
        pl->lbeat = 0;
    }
    if (!arr_playing && !ui.cheld && app.mode != ARR_FREE) {   /* stopped: the chord PLAY starts on (the loop's
                                                      * first; in SONG row 1's, in its key), not the last one heard (PLAY
                                                      * ends a chord from the keys in LOOP and SONG) */
        const song_t *g = csong();
        const loop_t *l0 = app.mode == ARR_LOOP ? &sloops.loop[app.loop % ARR_LOOPS]
                         : g->nrows ? &sloops.loop[g->row[0].loop % ARR_LOOPS] : 0;
        chord_t c0;
        if (l0 && (c0 = loop_chord_at(l0, 0)).type != CH_NONE) {
            pl->chord = arr_chord_pack(chord_shift(c0, g->transpose + (app.mode == ARR_SONG ? g->row[0].key : 0)));
            return 1;
        }
    }
    return 0;
}
static uint32_t home_chord(void)                     /* the big chord HOME shows, packed */
{
    place_t pl;
    home_place(&pl);
    return pl.chord;
}
/* HOME while chords are recorded (a new loop's take; REC in LOOP, playing or stepping): the loop as bars, so the
 * hands playing see where they are. A line "REC L9 . bar 3.2" (the take's bar and beat, from its start; REC in
 * LOOP: the loop's), the chord sounding at its right; under it a cell a bar, four to a row (16 bars show, scrolled
 * to the bar playing): the bar's number and its chord (big), or its chords (small: "Am  F"), a dot for none. The
 * bar playing filled teal, its beats marked along its foot; a take's next bar outlined, empty (the take growing);
 * stepping (stopped): the cursor's bar in a coral ring. A chord just written fills its bar coral a moment. Waiting
 * for the take's first bar: "starts at the next bar" and the beats left, big, in bar 1's outline */
#define RG_Y 28                                      /* the bars' top */
#define RG_X 8
#define RG_CW 53                                     /* a bar's cell: four to a row, 4 px apart */
#define FLASH_MS 450u
/* HOME's eight tracks: lit as they play (bit i of lit), with their sounds; home_lit: those hit just now */
static uint32_t home_lit(void)
{
    uint32_t lit = 0, now = arr_ticks;
    for (uint32_t i = 0; i < ARR_ALLTRK; i++) {      /* (8, 9: the leads') */
        uint32_t hit = arr_track_hit[i];
        if (arr_playing && hit && now - hit < 8u)    /* (stopped: nothing plays, nothing lit) */
            lit |= 1u << i;
    }
    return lit;
}
static uint32_t home_tracks_hash(uint32_t h, uint32_t slot, uint32_t lit)
{
    h = hash(h, lit | (uint32_t)sel_trk() << 12);
    for (uint32_t i = 0; i < ARR_TRACKS; i++)
        h = hash(h, app.trk[slot][i].program | (uint32_t)app.trk[slot][i].chan << 8 | (uint32_t)trk_muted((int)i) << 12);
    return h;
}
static void home_tracks(uint32_t slot, uint32_t lit)
{
    for (uint32_t i = 0; i < ARR_TRACKS; i++) {
        int32_t x = 6 + (int32_t)(i / 4u) * 116, y = 87 + (int32_t)(i % 4u) * 11;
        int on = (int)(lit >> i & 1u), sel = (int)i == sel_trk(), mute = trk_muted((int)i);
        uint16_t fg = on ? K_BG : sel ? K_ACCENT : mute ? K_LINE : K_TEXT;
        const char *name = trk_sound(&app.trk[slot][i]);
        if (on)
            cv_round(x, y, 112, 11, 3, sel ? K_ACCENT : K_TEAL);
        else if (sel)
            cv_round(x, y, 112, 11, 3, K_PANEL);
        cv_text(x + 4, y - 2, &FONT_XS, TRACK_NAME[i], on ? K_BG : K_DIM);
        text_fit(x + 26, y - 2, &FONT_XS, mute ? "muted" : name, 84, fg);
    }
}
/* HOME in SONG: LD1 and LD2 (each with notes in the song) at the right of the song's line, in the leads' plum: a note
 * sounding now coral; muted (the row's, or by hand) an outline, struck through */
static void home_leads(uint32_t lit)
{
    for (int k = 0; k < ARR_LEADS; k++) {
        int32_t x = 150 + k * 42, y = STRIP_Y + 15;
        int t = ARR_LD1 + k, on = (int)(lit >> t & 1u), mute = trk_muted(t);
        if (!leads_count(&leads, -1, k))
            continue;
        if (mute) {
            cv_round(x, y, 40, 11, 3, K_LINE);
            cv_round(x + 1, y + 1, 38, 9, 2, K_BG);
            text_c(x + 20, y - 2, &FONT_XS, TRACK_NAME[t], K_DIM);
            cv_rect(x + 8, y + 5, 24, 1, K_DIM);
        } else {
            cv_round(x, y, 40, 11, 3, on ? K_ACCENT : K_PLUM);
            text_c(x + 20, y - 2, &FONT_XS, TRACK_NAME[t], K_BG);
        }
    }
}
static int wrote_flash(int li)
{
    return ui.wrote_t && li == ui.wrote_loop && plat_ms() - ui.wrote_t < FLASH_MS;
}
static void draw_home_rec(void)
{
    place_t pl;
    const loop_t *l;
    int take = ui.take, ovr = arr_override, xp = xpose_now(), li, bpb, len, shown, ghost = 0, focus = -1, fbeat = 0;
    int cursor = 0, wait = 0, cnt = 0, n, rows, vis, top, ch, fl, fbar = -1, i;
    uint32_t h, lit;
    char t[40], *e;
    if (home_place(&pl))
        ovr = 0;
    li = take ? ui.take_loop % ARR_LOOPS : arr_playing && pl.loop >= 0 ? pl.loop : (int)(app.loop % ARR_LOOPS);
    l = &sloops.loop[li];
    bpb = l->beats ? l->beats : 4;
    len = loop_len(l);
    shown = l->bars;
    if (take == 1)
        wait = 1;                                    /* from PLAY */
    else if (take == 2) {
        int32_t tb = (int32_t)(arr_beats - ui.take_b0);
        if (tb < 0) {
            wait = 2;                                /* the count-in: the beats to the take's downbeat */
            cnt = (int)-tb;
        } else {
            focus = (int)tb / bpb;
            fbeat = (int)tb % bpb;
            ghost = 1;
        }
    } else if (arr_playing) {                        /* REC in LOOP, in time: the loop's bar playing */
        if (arr_sect == SEC_INTRO)
            wait = 3;
        else if (ui.mode_wait || pl.loop != li)
            wait = 4;
        else {
            focus = (int)(pl.lbeat / (uint32_t)bpb);
            fbeat = (int)(pl.lbeat % (uint32_t)bpb);
        }
    } else {                                         /* stepping: the cursor's bar (at the end: the bar to come) */
        cursor = 1;
        focus = app.cbeat / bpb;
        fbeat = app.cbeat % bpb;
    }
    if (wait >= 1 && wait <= 2)
        ghost = 1;                                   /* bar 1, to come */
    if (focus + 1 > shown)
        shown = focus + 1;
    n = shown + ghost;
    if (n < 1)
        n = 1;
    rows = (n + 3) / 4;
    vis = rows < 4 ? rows : 4;
    top = (focus + ghost) / 4 - 3;                   /* the bar playing (and the next) in the window */
    if (top > rows - 4)
        top = rows - 4;
    if (top < 0)
        top = 0;
    ch = vis == 1 ? 52 : vis == 2 ? 48 : vis == 3 ? 33 : 24;   /* (one row: the tracks under it, as HOME's) */
    fl = wrote_flash(li);
    if (fl)
        fbar = ui.wrote_beat / bpb;
    h = hash(2166136261u, 0x5EC0u | (uint32_t)li << 16 | (uint32_t)take << 24 | (uint32_t)wait << 26 |
                              (uint32_t)cursor << 29);
    h = hash(h, (uint32_t)(focus + 1) | (uint32_t)fbeat << 8 | (uint32_t)shown << 12 | (uint32_t)cnt << 20 |
                    (uint32_t)(fbar + 1) << 24);
    h = hash(h, pl.chord | (uint32_t)ovr << 24 | (uint32_t)(xp & 31) << 25);
    h = hash(h, (uint32_t)bpb | (uint32_t)len << 4 | (uint32_t)top << 12);
    for (i = 0; i < len && i < ARR_LOOP_BEATS; i++)
        h = hash(h, l->ch[i]);
    lit = vis == 1 ? home_lit() : 0;
    if (vis == 1)
        h = home_tracks_hash(h, (uint32_t)sect_slot(arr_sect), lit);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    if (vis == 1)
        home_tracks((uint32_t)sect_slot(arr_sect), lit);

    {   /* the line: REC's dot, "REC L9 . bar 3.2"; the chord sounding at the right */
        int32_t x;
        cv_round(8, 6, 10, 10, 5, RGB(214, 64, 52));
        loop_label(li, cat_s(t, "REC "));
        if (wait == 1)
            cat_s(t + slen(t), "|starts with PLAY");
        else if (wait == 2)
            cat_s(t + slen(t), "|starts at the next bar");
        else if (wait == 3)
            cat_s(t + slen(t), "|after the Intro");
        else if (wait == 4)
            cat_s(t + slen(t), "|from the bar line");
        else {
            e = itoa_u((uint32_t)focus + 1u, cat_s(t + slen(t), "|bar "));
            *e++ = '.';
            itoa_u((uint32_t)fbeat + 1u, e);
        }
        x = segs(24, 2, &FONT_B, t, K_TEXT);
        if (!wait && pl.chord != 0xFFFFFFu) {    /* the chord sounding, in its box */
            chord_t c = arr_chord_unpack(pl.chord);
            int32_t w;
            if (c.type != CH_NONE) {
                chord_name(c, t);
                w = text_w(&FONT_B, t) + 14;
                if (232 - w > x + 6) {
                    cv_round(232 - w, 1, w, 20, 6, ovr ? K_ACCENT : K_TEAL);
                    cv_text(232 - w + 7, 3, &FONT_B, t, K_BG);
                }
            }
        }
    }
    for (int b = top * 4; b < (top + vis) * 4 && b < n; b++) {   /* the bars */
        int32_t x = RG_X + (b % 4) * (RG_CW + 4), y = RG_Y + (b / 4 - top) * (ch + 2);
        int plays = b == focus && !cursor, cur = b == focus && cursor, out = b >= shown || (cursor && b >= l->bars);
        int flash = b == fbar, nch = 0, k;
        int32_t nx;
        uint16_t fill = flash ? K_ACCENT : plays ? K_TEAL : out ? K_BG : K_PANEL;
        uint16_t fg = flash || plays ? K_BG : K_TEXT;
        char nm[4][12];
        if (cur) {                                   /* the cursor: a coral ring */
            cv_round(x, y, RG_CW, ch, 5, K_ACCENT);
            cv_round(x + 2, y + 2, RG_CW - 4, ch - 4, 3, fill);
        } else if (out && !flash) {                  /* a bar to come: its outline */
            cv_round(x, y, RG_CW, ch, 5, K_LINE);
            cv_round(x + 1, y + 1, RG_CW - 2, ch - 2, 4, fill);
        } else
            cv_round(x, y, RG_CW, ch, 5, fill);
        itoa_u((uint32_t)b + 1u, t);
        nx = cv_text(x + 4, ch >= 33 ? y : y + 4, &FONT_XS, t, flash || plays ? K_BG : out ? K_LINE : K_DIM) + 4;
        for (k = 0; k < bpb; k++) {                  /* its chords */
            int idx = b * bpb + k;
            uint16_t v = idx < len && idx < ARR_LOOP_BEATS ? l->ch[idx] : LC_SAME;
            if (v != LC_SAME && nch < 4)
                chord_name(chord_shift(lc_unpack(v), xp), nm[nch++]);
        }
        if (wait == 2 && b == 0) {                   /* the count-in, big */
            itoa_u((uint32_t)cnt, t);
            text_c(x + RG_CW / 2, y + (ch - 32) / 2, &FONT_L, t, K_ACCENT);
        } else if (nch == 1) {                       /* one chord: as big as fits */
            int32_t tx = ch >= 33 ? x + 4 : nx, tw = RG_CW - (tx - x) - 3;
            if (ch >= 44 && text_w(&FONT_L, nm[0]) <= RG_CW - 6)
                text_c(x + RG_CW / 2, y + 13, &FONT_L, nm[0], fg);
            else if (text_w(&FONT_B, nm[0]) <= tw)
                cv_text(tx, ch >= 33 ? y + ch / 2 - 4 : y + 4, &FONT_B, nm[0], fg);
            else
                text_fit(tx, ch >= 33 ? y + ch / 2 - 3 : y + 4, &FONT_XS, nm[0], tw, fg);
        } else if (nch > 1) {                        /* more: small, in order ("Am  F") */
            e = t;
            for (k = 0; k < nch; k++)
                e = cat_s(k ? cat_s(e, "  ") : e, nm[k]);
            if (ch >= 33 && text_w(&FONT_B, t) <= RG_CW - 7)
                cv_text(x + 4, y + ch / 2 - 4, &FONT_B, t, fg);
            else if (ch >= 33)
                text_fit(x + 4, y + ch / 2 - 3, &FONT_XS, t, RG_CW - 7, fg);
            else
                text_fit(nx, y + 4, &FONT_XS, t, RG_CW - (nx - x) - 3, fg);
        } else if (!out)                             /* none: a dot */
            cv_round(ch >= 33 ? x + RG_CW / 2 - 2 : (nx + x + RG_CW) / 2 - 2, y + ch / 2 - 2 + (ch >= 33 ? 4 : 0), 4, 4, 1,
                     flash || plays ? K_BG : K_DIM);
        if (b == focus) {                            /* its beats along its foot: the one now (and those before) lit */
            int32_t sw = (RG_CW - 10) / bpb;
            for (k = 0; k < bpb; k++)
                cv_rect(x + 5 + k * sw, y + ch - 4, sw - 2, 2,
                        cursor ? (k == fbeat ? K_ACCENT : K_LINE) : k <= fbeat ? K_BG : K_TEAL_T);
        }
    }
    if (rows > 4) {                                  /* the scroll bar: where the window is */
        int32_t th = 4 * (ch + 2) - 2, bh = th * 4 / rows, by = RG_Y + th * top / rows;
        cv_round(236, RG_Y, 3, th, 1, K_PANEL);
        cv_round(236, by, 3, bh, 1, K_DIM);
    }
    cv_blit(0, MAIN_Y);
}
static void draw_home(void)
{
    uint32_t h, lit, i;
    int sect = arr_sect, next = arr_next_sect, play = arr_playing, blink = (plat_ms() / 300u) & 1u, ovr = arr_override;
    int mode = arr_mode, intro, fresh, has_next, take = ui.take;
    uint32_t slot = (uint32_t)sect_slot(sect), bt;
    int32_t tb = 0;
    place_t pl;
    chord_t nc;
    char t[32];
    const loop_t *l;
    if (home_place(&pl))
        ovr = 0;                                     /* (PLAY's chord: the source's colour) */
    if (ui.take || ui.rec_chords) {                  /* chords recorded: the loop as bars */
        draw_home_rec();
        return;
    }
    l = pl.loop >= 0 ? &sloops.loop[pl.loop] : 0;
    bt = l && l->beats ? l->beats : 4u;
    intro = l && play && sect == SEC_INTRO;          /* an Intro holds its chord: the loop starts after it */
    fresh = mode == ARR_FREE && !app.played;         /* nothing heard yet: "Play a chord" */
    has_next = l && !intro && next_chord(&pl, &nc);
    if (take == 2)                                   /* the take's beat (below 0: waiting for its downbeat) */
        tb = (int32_t)(arr_beats - ui.take_b0);
    h = hash(2166136261u, pl.chord);
    lit = home_lit();
    h = hash(h, (uint32_t)sect | (uint32_t)next << 4 | (uint32_t)play << 8 | (uint32_t)(next ? blink : 0) << 9 |
                    (lit & 255u) << 10 | (uint32_t)ovr << 21 | (uint32_t)mode << 22 |
                    (uint32_t)fresh << 24 | (uint32_t)intro << 25 | (uint32_t)take << 26);
    h = hash(h, pl.bar << 8 | pl.beat | (uint32_t)(uint8_t)pl.loop << 16 | pl.lbeat << 24);
    h = hash(hash(h, has_next ? arr_chord_pack(nc) : 0xFFFFFFu), (uint32_t)xpose_now() & 31u);
    h = hash(h, (uint32_t)arr_row | (uint32_t)arr_rep << 8 | (uint32_t)csong()->nrows << 16 | (uint32_t)ui.take_loop << 24);
    {
        uint32_t tbt = sloops.loop[ui.take_loop % ARR_LOOPS].beats ? sloops.loop[ui.take_loop % ARR_LOOPS].beats : 4u;
        h = hash(h, (uint32_t)(tb < 0 ? -1 : tb / (int32_t)tbt));
    }
    h = hash(h, l ? (uint32_t)l->bars | (uint32_t)app.hdr[slot].bars << 8 : app.hdr[slot].bars);
    h = hash(h, (uint32_t)sel_trk() | lit >> 8 << 4 | (uint32_t)(leads.n != 0) << 6 | (uint32_t)trk_muted(ARR_LD1) << 7 |
                    (uint32_t)trk_muted(ARR_LD1 + 1) << 8 | (uint32_t)leads.gen << 9);
    for (i = 0; i < ARR_TRACKS; i++)
        h = hash(h, app.trk[slot][i].program | (uint32_t)app.trk[slot][i].chan << 8 | (uint32_t)trk_muted((int)i) << 12);
    if (l)
        home_strip(&pl, l, intro, &h, 0);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);

    {   /* the section, the one waiting (NEXT), and where we are: "loop bar 2.3 / 4" (the loop's), else "pattern bar
         * 1.1 / 2" (the word when there is room) */
        int32_t x = pill(8, 2, 0, SECT_NAME[slot + 1], sect_colour((int)slot + 1), K_BG), w;
        int inl = l && l->bars && !intro;
        uint32_t bar = inl ? pl.lbeat / bt : pl.bar, beat = inl ? pl.lbeat % bt : pl.beat;
        const char *what = inl ? "loop" : "pattern";
        char *e;
        if (next >= SEC_INTRO && next <= SEC_FILL) {
            cv_text(x + 8, 5, &FONT_XS, "NEXT", blink ? K_ACCENT : K_DIM);
            x = pill(x + 8 + text_w(&FONT_XS, "NEXT") + 6, 2, 0, SECT_NAME[next], blink ? sect_colour(next) : K_PANEL,
                     blink ? K_BG : K_TEXT);
        }
        e = itoa_u(bar + 1u, cat_s(t, "bar "));
        *e++ = '.';
        e = itoa_u(beat + 1u, e);
        itoa_u(inl ? l->bars : app.hdr[slot].bars, cat_s(e, " / "));
        w = text_w(&FONT_S, t);
        cv_text(232 - w, 3, &FONT_S, t, play ? K_TEXT : K_DIM);
        if (232 - w - 4 - text_w(&FONT_XS, what) >= x + 8)
            cv_text(232 - w - 4 - text_w(&FONT_XS, what), 4, &FONT_XS, what, K_DIM);
    }
    {   /* the chord now, big, in its box; the next one */
        cv_round(8, 22, 144, 33, 6, K_PANEL);
        if (fresh)
            text_c(80, 30, &FONT_B, "Play a chord: 3 keys", K_DIM);
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
            itoa_u((uint32_t)tb / (sloops.loop[ui.take_loop % ARR_LOOPS].beats ? sloops.loop[ui.take_loop % ARR_LOOPS].beats : 4u) + 1u,
                   cat_s(e, ": bar "));
        cv_text(8, STRIP_Y, &FONT_XS, t, K_ACCENT);
    } else
        cv_text(8, STRIP_Y, &FONT_XS, mode == ARR_FREE ? "FREE" : MODE_NAME[mode % 3u], K_DIM);
    if (mode == ARR_SONG) {                          /* the song: "row 2 of 4 . x1 of 2" */
        const song_t *g = csong();
        if (g->nrows) {                              /* (stopped: row 1, where PLAY starts) */
            uint32_t r = play ? arr_row % ARR_ROWS : 0u, rp = play ? arr_rep : 0u;
            char *e = itoa_u(r + 1u, cat_s(t, "row "));
            e = itoa_u(g->nrows, cat_s(e, " of "));
            e = itoa_u(rp + 1u, cat_s(e, "|x"));
            itoa_u(g->row[r].rep ? g->row[r].rep : 1u, cat_s(e, " of "));
        } else
            cat_s(t, "no rows yet|SELECT: the SONG page");
        segs(8, STRIP_Y + 14, &FONT_XS, t, K_DIM);
        if (g->nrows && leads.n)                     /* the song's lead tracks with notes: their state */
            home_leads(lit);
    }
    home_tracks(slot, lit);                          /* the eight tracks: lit as they play, with their sounds */
    cv_blit(0, MAIN_Y);
}

/* ---------------------------------------------------------- LOOP page --- */
/* the loop as a grid (the spec's Screens): a row a bar, its number at the left, a cell a beat (a bar of 5-7 beats
 * wraps to two lines, the second's cells under the first's): the chord's name, or a dot for no change; the cursor
 * boxed (coral), the beat playing tinted (teal). The cursor may stand at the loop's end (REC: a chord there makes
 * it longer): its bar shows as an empty row to come. Scrolled so the cursor's bar shows (a scroll bar at the
 * right when not all fit); 4-beat bars: the beats' numbers over their columns. REC writing in time (playing): no
 * cursor, the grid follows the bar playing (its number teal), a chord just written fills its cell coral a moment */
#define LG_X 22                                      /* the cells' left edge (the bar numbers before it) */
#define LG_W 210                                     /* the cells' width (a scroll bar after it) */
static void draw_loop(void)
{
    const loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
    int bpb = l->beats ? l->beats : 4, len = loop_len(l), wrap = bpb >= 5, cbar = app.cbeat / bpb;
    int bars = l->bars, rows, xp = xpose_now(), i, live, fbar, fl;
    int n1 = wrap ? (bpb + 1) / 2 : bpb, cw = LG_W / n1;
    int32_t y0 = wrap ? 2 : 13, ch = 18, gap = wrap ? 6 : 2, rh = (wrap ? 2 * ch + 2 : ch) + gap;
    int32_t visible = (MAIN_H - y0 + gap) / rh, top;
    place_t pl;
    uint32_t pbeat, h;
    char t[16];
    place_read(&pl);
    pbeat = arr_playing && pl.loop == (int)(app.loop % ARR_LOOPS) ? pl.lbeat : 0xFFFFFFFFu;
    live = ui.rec_chords && pbeat != 0xFFFFFFFFu;    /* REC writing in time: the playhead, not the cursor */
    if (live)
        cbar = (int)(pbeat / (uint32_t)bpb);
    rows = live || cbar + 1 <= bars ? bars : cbar + 1;
    fl = wrote_flash((int)(app.loop % ARR_LOOPS));
    fbar = fl ? ui.wrote_beat : -1;                  /* (here: the beat written, flashing) */
    top = cbar - (visible - 1) / 2;                  /* the cursor's bar (writing in time: the bar playing) near the middle */
    if (top > rows - visible)
        top = rows - visible;
    if (top < 0)
        top = 0;
    h = hash(2166136261u, (uint32_t)app.loop | (uint32_t)app.cbeat << 8 | (uint32_t)top << 16 | (uint32_t)rows << 24);
    h = hash(h, (uint32_t)bpb | (uint32_t)(xp + 64) << 8 | (uint32_t)ui.rec_chords << 15 | pbeat << 16);
    h = hash(h, (uint32_t)(fbar + 1) | (uint32_t)live << 8);
    for (i = 0; i < len && i < ARR_LOOP_BEATS; i++)
        h = hash(h, l->ch[i]);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    if (!wrap)                                       /* the beats' numbers */
        for (int k = 0; k < bpb; k++) {
            itoa_u((uint32_t)k + 1u, t);
            text_c(LG_X + k * cw + (cw - 3) / 2, 0, &FONT_XS, t, K_DIM);
        }
    for (i = top; i < rows && i < top + visible; i++) {
        int32_t y = y0 + (i - top) * rh;
        int ghost = i >= bars;                       /* the bar to come (the cursor at the loop's end) */
        itoa_u((uint32_t)i + 1u, t);
        cv_text(LG_X - 5 - text_w(&FONT_XS, t), y + (wrap ? ch - 6 : 3), &FONT_XS, t,
                i == cbar ? live ? K_TEAL : K_TEXT : K_DIM);
        for (int k = 0; k < bpb; k++) {
            int idx = i * bpb + k, line = wrap && k >= n1, col = line ? k - n1 : k;
            int32_t x = LG_X + col * cw, yy = y + (line ? ch + 2 : 0), w = cw - 3;
            uint16_t v = idx < len && idx < ARR_LOOP_BEATS ? l->ch[idx] : LC_SAME;
            int cur = idx == app.cbeat && !live, plays = (uint32_t)idx == pbeat, flash = idx == fbar;
            uint16_t fill = flash ? K_ACCENT : plays ? K_TEAL_T : ghost ? K_BG : K_PANEL;
            if (cur) {                               /* the cursor: a coral ring round the cell */
                cv_round(x, yy, w, ch, 4, K_ACCENT);
                cv_round(x + 2, yy + 2, w - 4, ch - 4, 2, fill);
            } else if (ghost) {                      /* a cell to come: its outline */
                cv_round(x, yy, w, ch, 4, K_LINE);
                cv_round(x + 1, yy + 1, w - 2, ch - 2, 3, fill);
            } else
                cv_round(x, yy, w, ch, 4, fill);
            if (v != LC_SAME) {
                chord_name(chord_shift(lc_unpack(v), xp), t);
                text_fit(x + 5, yy + 3, &FONT_XS, t, w - 8, flash ? K_BG : K_TEXT);
            } else
                cv_round(x + w / 2 - 2, yy + ch / 2 - 2, 4, 4, 1, flash ? K_BG : ghost ? K_LINE : K_DIM);
        }
    }
    if (rows > visible) {                            /* the scroll bar: where the window is */
        int32_t th = MAIN_H - y0 - 4, bh = th * visible / rows, by = y0 + th * top / rows;
        cv_round(236, y0, 3, th, 1, K_PANEL);
        cv_round(236, by, 3, bh < 6 ? 6 : bh, 1, K_DIM);
    }
    if (!len)                                        /* an empty loop: what to do */
        text_c(LG_X + LG_W / 2, y0 + rh + 8, &FONT_XS, ui.rec_chords ? "play a chord" : "REC, then play chords", K_DIM);
    cv_blit(0, MAIN_Y);
}

/* ---------------------------------------------------------- SONG page --- */
/* the song's rows (the spec's Screens): a row a line, # / LOOP / SECTION / REPEAT / MUTES / KEY; the cursor row in a
 * coral ring (white keys write there), the row playing tinted teal with a mark; after the last row the END row (taps
 * there add rows), outlined as a row to come. MUTES: eight squares D1 D2 PC BA | C1 C2 C3 C4, then the lead tracks
 * LD1 LD2 (plum) after a gap, filled = plays, outlined = muted. KEY only when not 0. The columns the page's knobs turn: their headings dark. Page 3 also says
 * the song's Intro, Ending and end. Six rows show, scrolled to the cursor (a scroll bar when more) */
#define SG_Y0 14                                     /* the first row (the headings over it) */
#define SG_RH 16                                     /* a row's pitch */
#define SG_SHOWN 6
#define SG_LOOP 27                                   /* the columns: LOOP's left */
#define SG_SECT 86                                   /* SECTION's colour chip, its name after */
#define SG_REP 160                                   /* REPEAT's right edge */
#define SG_MUTE 168                                  /* the mutes' first square (D1; LD1, LD2 after a gap: SG_LMUTE) */
#define SG_LMUTE (SG_MUTE + 8 * 5 + 2 + 2)
#define SG_KEY 234                                   /* KEY's right edge */
static const char *const ROW_SECT_NAME[ROW_NSECT] = {"Main A", "Main A + fill", "Main B", "Main B + fill"};
static void glyph_play(int32_t x, int32_t y, uint16_t c)    /* the row playing: a small triangle */
{
    for (int j = 0; j < 4; j++)
        cv_rect(x + j, y + j, 1, 7 - 2 * j, c);
}
static void text_r(int32_t xr, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(xr - text_w(f, s), y, f, s, c);
}
/* the song's state while it plays, under the rows: "Intro . then row 1", "Fill AB . into row 3", "Ending . then the
 * stop"; 0 when a row just plays */
static int song_state(char *t)
{
    const song_t *g = csong();
    int s = arr_sect, n = g->nrows, r = arr_row;
    *t = 0;
    if (!arr_playing || arr_mode != ARR_SONG || !n)
        return 0;
    if (s == SEC_INTRO)
        cat_s(t, "then row 1");
    else if (s == SEC_FILL_AB || s == SEC_FILL_BA) {
        int nx = r + 1 < n ? r + 1 : g->at_end ? 0 : -1;
        if (nx < 0)
            return 0;
        itoa_u((uint32_t)nx + 1u, cat_s(t, "into row "));
    } else if (s == SEC_END)
        cat_s(t, "then the stop");
    else
        return 0;
    return s;
}
static void draw_song(void)
{
    const song_t *g = csong();
    int n = g->nrows > ARR_ROWS ? ARR_ROWS : g->nrows, total = n < ARR_ROWS ? n + 1 : n, cur = app.row, pg = ui.page - P_SONG;
    int play = arr_playing && arr_mode == ARR_SONG && arr_loop_now >= 0 && arr_row < n ? arr_row : -1, top, i, st;
    uint32_t h;
    char t[40], stt[24];
    st = song_state(stt);
    if (st == SEC_INTRO || st == SEC_END)
        play = -1;                                   /* (the Intro, the Ending: no row plays yet, any more) */
    if (cur > total - 1)
        cur = total - 1;
    top = cur - (SG_SHOWN - 1) / 2;                  /* the cursor near the middle */
    if (top > total - SG_SHOWN)
        top = total - SG_SHOWN;
    if (top < 0)
        top = 0;
    h = hash(2166136261u, 327680u | (uint32_t)pg | (uint32_t)top << 4 | (uint32_t)cur << 10 | (uint32_t)n << 24);
    h = hash(h, (uint32_t)(play + 1));               /* (its own word: OR-ed into the first, row 1 playing was lost) */
    h = hash(h, (uint32_t)g->intro | (uint32_t)g->ending << 1 | (uint32_t)g->at_end << 2 | (uint32_t)ui.song_rec << 3 |
                    (uint32_t)st << 4 | (uint32_t)arr_row << 8);
    for (i = top; i < n && i < top + SG_SHOWN; i++) {
        const row_t *w = &g->row[i];
        const loop_t *l = &sloops.loop[w->loop % ARR_LOOPS];
        h = hash(h, w->loop | (uint32_t)w->sect << 5 | (uint32_t)w->rep << 8 | (uint32_t)w->mute << 12 |
                        (uint32_t)(uint8_t)w->key << 20 | (uint32_t)(l->bars != 0) << 28 | (uint32_t)(w->lmute & 3u) << 29);
        for (int c = 0; c < 8; c++)
            h = hash(h, (uint8_t)l->name[c]);
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    /* the headings: the knobs' columns dark (page 1 LOOP, SECTION, REPEAT; page 2 KEY) */
    text_r(23, 0, &FONT_XS, "#", K_DIM);
    cv_text(SG_LOOP, 0, &FONT_XS, "LOOP", pg == 0 ? K_TEXT : K_DIM);
    cv_text(SG_SECT, 0, &FONT_XS, "SECTION", pg == 0 ? K_TEXT : K_DIM);
    text_r(SG_REP, 0, &FONT_XS, "REP", pg == 0 ? K_TEXT : K_DIM);
    cv_text(SG_MUTE - 1, 0, &FONT_XS, "MUTES", K_DIM);
    text_r(SG_KEY, 0, &FONT_XS, "KEY", pg == 1 ? K_TEXT : K_DIM);
    for (i = top; i < total && i < top + SG_SHOWN; i++) {
        int32_t y = SG_Y0 + (i - top) * SG_RH, ch = SG_RH - 2;
        int on = i == cur, ghost = i >= n;
        uint16_t fill = i == play ? K_TEAL_T : ghost ? K_BG : K_PANEL;
        if (on) {                                    /* the cursor: a coral ring */
            cv_round(2, y, 232, ch, 4, K_ACCENT);
            cv_round(4, y + 2, 228, ch - 4, 2, fill);
        } else if (ghost) {                          /* the END row: a row to come */
            cv_round(2, y, 232, ch, 4, K_LINE);
            cv_round(3, y + 1, 230, ch - 2, 3, fill);
        } else
            cv_round(2, y, 232, ch, 4, fill);
        if (i == play)
            glyph_play(7, y + 3, K_TEAL);
        if (ghost) {
            cv_text(SG_LOOP, y - 1, &FONT_XS, "END", on ? K_TEXT : K_DIM);
            if (!n)
                cv_text(SG_SECT, y - 1, &FONT_XS, "white keys add rows", K_DIM);
            continue;
        }
        {
            const row_t *w = &g->row[i];
            const loop_t *l = &sloops.loop[w->loop % ARR_LOOPS];
            int32_t x;
            char *e;
            itoa_u((uint32_t)i + 1u, t);
            text_r(23, y - 1, &FONT_XS, t, on || i == play ? K_TEXT : K_DIM);
            /* LOOP: "L9" and its name ("-" an empty loop) */
            itoa_u((uint32_t)(w->loop % ARR_LOOPS) + 1u, cat_s(t, "L"));
            x = cv_text(SG_LOOP, y - 1, &FONT_XS, t, K_DIM) + 3;
            loop_label(w->loop, t);
            for (e = t; *e && *e != ' '; e++)
                ;
            text_fit(x, y - 1, &FONT_XS, l->bars ? (*e ? e + 1 : e) : "empty", SG_SECT - 3 - x, l->bars ? K_TEXT : K_DIM);
            /* SECTION: its colour, "Main B", "+fill" */
            cv_round(SG_SECT, y + 3, 3, ch - 6, 1, sect_colour(row_main(w)));
            x = cv_text(SG_SECT + 6, y - 1, &FONT_XS, row_main(w) == SEC_B ? "Main B" : "Main A", K_TEXT);
            if (row_fill(w))
                cv_text(x + 3, y - 1, &FONT_XS, "+fill", K_ACCENT);
            /* REPEAT: "x2" (x1 dim) */
            itoa_u(w->rep ? w->rep : 1u, cat_s(t, "x"));
            text_r(SG_REP, y - 1, &FONT_XS, t, w->rep > 1 ? K_TEXT : K_DIM);
            /* MUTES: D1 D2 PC BA | C1 C2 C3 C4, LD1 LD2: plays filled (teal, the leads plum), muted a coral outline */
            for (int k = 0; k < ARR_ALLTRK; k++) {
                int32_t mx = k < ARR_TRACKS ? SG_MUTE + k * 5 + (k >= 4 ? 2 : 0) : SG_LMUTE + (k - ARR_TRACKS) * 5, my = y + 3;
                int m = k < ARR_TRACKS ? w->mute >> k & 1u : w->lmute >> (k - ARR_TRACKS) & 1u;
                if (m) {
                    cv_rect(mx, my, 4, 8, K_ACCENT);
                    cv_rect(mx + 1, my + 1, 2, 6, fill);
                } else
                    cv_rect(mx, my, 4, 8, k < ARR_TRACKS ? K_TEAL : K_PLUM);
            }
            /* KEY: "+2", "-3" (0: nothing) */
            if (w->key) {
                t[0] = w->key > 0 ? '+' : '-';
                itoa_u((uint32_t)(w->key > 0 ? w->key : -w->key), t + 1);
                text_r(SG_KEY, y - 1, &FONT_XS, t, K_TEXT);
            }
        }
    }
    if (total > SG_SHOWN) {                          /* the scroll bar: where the window is */
        int32_t th = SG_SHOWN * SG_RH - 2, bh = th * SG_SHOWN / total, by = SG_Y0 + th * top / total;
        cv_round(236, SG_Y0, 3, th, 1, K_PANEL);
        cv_round(236, by, 3, bh < 6 ? 6 : bh, 1, K_DIM);
    }
    if (st) {                                        /* playing its Intro, a fill, its Ending: "Intro . then row 1" */
        int32_t x = pill(8, SG_Y0 + SG_SHOWN * SG_RH, 0, SECT_NAME[st], sect_colour(st), K_BG);
        cv_text(x + 8, SG_Y0 + SG_SHOWN * SG_RH + 1, &FONT_XS, stt, K_TEXT);
    } else if (pg == 2) {                            /* the song: "Intro on . Ending on . at end: stop" */
        char *e = cat_s(t, "Intro ");
        e = cat_s(e, g->intro ? "on|Ending " : "off|Ending ");
        e = cat_s(e, g->ending ? "on|at end: " : "off|at end: ");
        cat_s(e, g->at_end ? "again" : "stop");
        segs(8, SG_Y0 + SG_SHOWN * SG_RH + 2, &FONT_XS, t, K_TEXT);
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
/* the selected track's sounds as the pages draw them: the pattern's track, or the lead's (into buf) */
static const arr_trk_t *sel_arr(arr_trk_t *buf)
{
    if (!app.lead)
        return &app.trk[pslot()][proj.trk & 7];
    lead_as_arr(sel_lead(), buf);
    return buf;
}
/* track i's sounds for the Mixer's strips (0..7 the pattern's, 8, 9 the leads') */
static const arr_trk_t *strip_trk(int i, arr_trk_t *buf)
{
    if (i < ARR_TRACKS)
        return &app.trk[pslot()][i];
    lead_as_arr(&csong()->lead[(i - ARR_TRACKS) % ARR_LEADS], buf);
    return buf;
}
/* the loop the pattern plays along with (playing: the one playing; stopped: what PLAY starts, LOOP's loop or a song's
 * row 1's), in the pattern's bars (its beats rounded up to them), when it is longer than the pattern playing Main A or
 * B: REC's screen says so ("2-bar pattern . loop 4 bars"), SEL makes the pattern as long. 0: not */
static int loop_longer(void)
{
    const song_t *g = csong();
    int s = pslot(), bpb = app.hdr[s].beats ? app.hdr[s].beats : 4, li = -1, n;
    if (arr_sect != SEC_A && arr_sect != SEC_B)
        return 0;
    if (arr_playing)
        li = arr_loop_now;
    else if (app.mode == ARR_LOOP)
        li = app.loop;
    else if (app.mode == ARR_SONG && g->nrows)
        li = g->row[0].loop;
    if (li < 0 || li >= ARR_LOOPS)
        return 0;
    n = (loop_len(&sloops.loop[li]) + bpb - 1) / bpb;
    if (n > ARR_MAXBARS)
        n = ARR_MAXBARS;
    return n > app.hdr[s].bars ? n : 0;
}
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
/* a pattern's name as shown everywhere: its 8 letters, trailing spaces cut ("Funk", "VERSE"); "" none */
static char *pat_name8(const char *nm, char *t)
{
    char *e = t;
    for (int i = 0; i < 8 && nm[i]; i++)
        *e++ = nm[i];
    while (e > t && e[-1] == ' ')
        e--;
    *e = 0;
    return e;
}
/* "P05 Verse", the pattern's number and name (one space between); a factory style's: "Pop A" */
static void pat_title(int s, char *t)
{
    int st = songs.song[proj.song].style[s];
    style_name(st, t);
    if (st >= ARR_USER_PATTERNS)
        return;
    t[3] = ' ';
    if (pat_name8(app.hdr[s].name, t + 4) == t + 4)
        t[3] = 0;                                /* (no name: the number alone) */
}
/* the Pattern page shows 8 bars at most: a longer pattern by halves (the playhead's), "bars 9-16 of 16" */
#define PAT_WIN 8
static void pattern_head(int s, int win)
{
    char t[24];
    int32_t x = pill(8, 0, 0, SECT_NAME[s + 1], sect_colour(s + 1), K_BG);
    pat_title(s, t);
    cv_text(x + 8, 2, &FONT_B, t, K_TEXT);
    {                                            /* "2 bars, 4/4": its length (its notes are written over C M7: the
                                                  * Track page's types play them over the chord) */
        char *e = t;
        if (win >= 0) {                          /* (a window of a longer one: "bars 9-16 of 16") */
            e = itoa_u((uint32_t)win * PAT_WIN + 1u, cat_s(e, "bars "));
            *e++ = '-';
            e = itoa_u((uint32_t)(win * PAT_WIN + PAT_WIN < app.hdr[s].bars ? win * PAT_WIN + PAT_WIN : app.hdr[s].bars), e);
            itoa_u(app.hdr[s].bars, cat_s(e, " of "));
            cv_text(232 - text_w(&FONT_XS, t), 3, &FONT_XS, t, K_DIM);
            return;
        }
        e = itoa_u(app.hdr[s].bars, e);
        e = cat_s(e, app.hdr[s].bars == 1 ? " bar, " : " bars, ");
        *e++ = (char)('0' + app.hdr[s].beats);
        cat_s(e, "/4");
    }
    cv_text(232 - text_w(&FONT_XS, t), 3, &FONT_XS, t, K_DIM);
}
/* the 8 tracks as mini piano rolls over the pattern's length (up to 8 bars; a longer one by halves, the one the
 * playhead is in), the playhead, the mutes */
static void draw_pattern(void)
{
    int s = pslot(), play = arr_playing, i, nb = app.hdr[s].bars, win = -1, wbars = nb;
    uint32_t blen = (uint32_t)app.hdr[s].beats * ARR_PPQ, len, w0 = 0, h, pos, bar, bt;
    const int32_t X0 = 26, W = 208;
    int32_t ph;
    view_refresh(s);
    do {
        bar = arr_bar;
        bt = arr_beat_tick;
    } while (bar != arr_bar);
    pos = bar * blen + bt;
    if (nb > PAT_WIN) {                          /* the half the playhead is in (stopped: where it stopped) */
        win = (int)(bar < (uint32_t)nb ? bar : 0u) / PAT_WIN;
        wbars = nb - win * PAT_WIN < PAT_WIN ? nb - win * PAT_WIN : PAT_WIN;
        w0 = (uint32_t)win * PAT_WIN * blen;
    }
    len = (uint32_t)wbars * blen;
    if (!len)
        len = 1;
    ph = play && pos >= w0 && pos < w0 + len ? X0 + (int32_t)((pos - w0) * (uint32_t)W / len) : -1;
    h = hash(hash(2166136261u, (uint32_t)s | (uint32_t)sel_trk() << 4 | (uint32_t)(ph + 1) << 8), vw.dirty);
    h = hash(hash(h, vw.gen), arr_recording | (uint32_t)(win + 1) << 8);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    pattern_head(s, win);
    for (i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *tr = &view.trk[i];
        int32_t y = 20 + i * 14, b;
        int sel = i == sel_trk(), mute = trk_muted(i);
        uint32_t n, e = (uint32_t)tr->first + tr->count, lo = 127, hi = 0;
        cv_text(4, y - 2, &FONT_XS, TRACK_NAME[i], sel ? K_ACCENT : mute ? K_LINE : K_DIM);
        cv_round(X0, y, W, 12, 3, sel ? (arr_recording ? RGB(246, 214, 204) : K_TEAL_T) : K_PANEL);
        for (b = 1; b < wbars; b++)              /* bar lines */
            cv_rect(X0 + b * W / wbars, y + 1, 1, 10, K_LINE);
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
            if (v->tick < w0 || v->tick >= w0 + len || v->tick >= (uint32_t)nb * blen)
                continue;                        /* another half; past a shortened length: kept, not played */
            x = X0 + (int32_t)((uint32_t)(v->tick - w0) * (uint32_t)W / len);
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
/* SEQ / EDIT while a track is recorded (REC on, or waiting for its bar line): the pattern as bars, HOME's recording
 * view (draw_home_rec) for a track. A line "REC C1 . bar 3/4 . pass 2" (the pattern's bar; the passes counted from
 * the bar recording started on), under it "pass 2: adding" from the second pass (the notes of the passes before play
 * meanwhile), else the track's sound and the grid; then a cell a bar, four to a row (16 bars: four rows): the bar
 * playing teal with its beats along its foot, the track's notes as small ticks in their bars (at their place, by
 * pitch). Waiting: "starts at the next bar" and the beats left, big, in that bar's coral outline (stopped: "starts
 * with PLAY", bar 1 outlined) */
static int trk_rec_shown(void)
{
    return (ui.page == P_PATTERN || ui.page == P_PATSET || ui.page == P_TRACK || ui.page == P_TRACK2) &&
           (arr_recording || ui.rec_want);
}
#define TR_Y 36                                      /* the bars' top */
static void draw_lead_rec(void);
static void draw_trk_rec(void)
{
    int s = pslot(), trk = proj.trk & 7, nb = app.hdr[s].bars, bpb = app.hdr[s].beats, play = arr_playing;
    int wait = !arr_recording || arr_rec_wait, focus = -1, fbeat = 0, next = -1, cnt = 0, pass = 1, rows, ch, b, k;
    int ll = loop_longer(), lbar = -1, lbars = 0;
    uint32_t blen, bar, bt, h, n, e, lo = 127, hi = 0;
    const arr_trk_t *tr;
    char t[48], *p;
    place_t pl;
    if (app.lead) {                                  /* a lead track: the song's row */
        draw_lead_rec();
        return;
    }
    if (nb < 1)
        nb = 1;
    if (bpb < 1)
        bpb = 4;
    blen = (uint32_t)bpb * ARR_PPQ;
    view_refresh(s);
    tr = &view.trk[trk];
    do {
        bar = arr_bar;
        bt = arr_beat_tick;
    } while (bar != arr_bar);
    if (bar >= (uint32_t)nb)
        bar = 0;
    if (play) {
        focus = (int)bar;
        fbeat = (int)(bt / ARR_PPQ);
    }
    if (wait) {                                      /* the bar it starts on, the beats to it */
        next = play ? (focus + 1) % nb : 0;
        cnt = play ? bpb - fbeat : 0;
    } else
        pass = arr_rec_bars / nb + 1;
    place_read(&pl);
    if (play && pl.loop >= 0 && loop_len(&sloops.loop[pl.loop]) > 0) {   /* the loop's bar too ("loop bar 3/4") */
        const loop_t *l = &sloops.loop[pl.loop];
        uint32_t lb = l->beats ? l->beats : 4u;
        lbar = (int)(pl.lbeat / lb);
        lbars = l->bars;
    }
    rows = (nb + 3) / 4;
    ch = (MAIN_H - TR_Y - 2 - 3 * (rows - 1)) / rows;
    if (ch > 72)
        ch = 72;
    e = (uint32_t)tr->first + tr->count;
    if (e > view.nev)
        e = view.nev;
    for (n = tr->first; n < e; n++) {
        if (view.ev[n].note < lo)
            lo = view.ev[n].note;
        if (view.ev[n].note > hi)
            hi = view.ev[n].note;
    }
    h = hash(2166136261u, 0x7EC0u | (uint32_t)s << 16 | (uint32_t)trk << 20 | (uint32_t)wait << 24 | (uint32_t)play << 25);
    h = hash(h, (uint32_t)(focus + 1) | (uint32_t)fbeat << 8 | (uint32_t)(next + 1) << 12 | (uint32_t)cnt << 20);
    h = hash(h, (uint32_t)nb | (uint32_t)bpb << 8 | (uint32_t)pass << 12 | (uint32_t)ll << 20 | (uint32_t)(lbar + 1) << 25);
    h = hash(h, (uint32_t)lbars);
    h = hash(hash(hash(h, vw.dirty), vw.gen), tr->program | (uint32_t)proj.quant << 8);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    {   /* the line: REC's dot, "REC C1 . pass 2 . adding"; under it the pattern's bar (and the loop's: "pattern bar 1/2 .
         * loop bar 3/4"), or a pattern shorter than the loop ("2-bar pattern . loop 4 bars . SEL: 4 bars"), the grid */
        int32_t x;
        cv_round(8, 6, 10, 10, 5, RGB(214, 64, 52));
        p = cat_s(cat_s(t, "REC "), TRACK_NAME[trk]);
        if (wait)
            cat_s(p, play ? "|starts at the next bar" : "|starts with PLAY");
        else
            itoa_u((uint32_t)pass, cat_s(p, "|pass "));
        x = segs(24, 2, &FONT_B, t, K_TEXT);
        if (!wait && pass > 1)                       /* the passes before play meanwhile: this one adds */
            segs(x, 2, &FONT_B, "|adding", K_ACCENT);
        if (ll && pass == 1) {
            p = itoa_u((uint32_t)nb, t);
            p = itoa_u((uint32_t)ll, cat_s(p, "-bar pattern|loop "));
            itoa_u((uint32_t)ll, cat_s(cat_s(p, " bars|SEL: "), ""));
            cat_s(t + slen(t), " bars");
            segs(24, 20, &FONT_XS, t, K_ACCENT);
        } else {
            p = t;
            *p = 0;
            if (!wait) {
                p = itoa_u((uint32_t)focus + 1u, cat_s(p, "pattern bar "));
                p = itoa_u((uint32_t)nb, cat_s(p, "/"));
                if (lbar >= 0) {
                    p = itoa_u((uint32_t)lbar + 1u, cat_s(p, "|loop bar "));
                    p = itoa_u((uint32_t)lbars, cat_s(p, "/"));
                }
                p = cat_s(p, "|");
            } else
                p = cat_s(cat_s(p, trk_sound(tr)), "|");
            cat_s(cat_s(p, "grid "), GRID_NAME[proj.quant % ARR_NQ]);
            segs(24, 20, &FONT_XS, t, K_DIM);
        }
    }
    for (b = 0; b < nb; b++) {                       /* the bars */
        int32_t x = RG_X + (b % 4) * (RG_CW + 4), y = TR_Y + (b / 4) * (ch + 3), ax, aw, ay, ah, nh;
        int plays = b == focus, cin = b == next;
        uint16_t fill = plays ? K_TEAL : K_PANEL, fg = plays ? K_BG : K_TEXT;
        if (cin) {                                   /* the bar recording starts on: a coral ring */
            cv_round(x, y, RG_CW, ch, 5, K_ACCENT);
            cv_round(x + 2, y + 2, RG_CW - 4, ch - 4, 3, plays ? K_TEAL : K_BG);
        } else
            cv_round(x, y, RG_CW, ch, 5, fill);
        itoa_u((uint32_t)b + 1u, t);
        ax = ch >= 30 ? x + 4 : cv_text(x + 4, y + ch / 2 - 7, &FONT_XS, t, plays ? K_BG : K_DIM) + 3;
        if (ch >= 30)
            cv_text(x + 4, y, &FONT_XS, t, plays ? K_BG : cin ? K_ACCENT : K_DIM);
        aw = x + RG_CW - 4 - ax;
        ay = ch >= 30 ? y + 16 : y + 3;
        ah = y + ch - 7 - ay;
        nh = ah >= 30 ? 3 : 2;
        if (cin && cnt) {                            /* the count-in, big */
            itoa_u((uint32_t)cnt, t);
            if (ch >= 36)
                text_c(x + RG_CW / 2, y + (ch - 32) / 2, &FONT_L, t, K_ACCENT);
            else
                text_c(x + RG_CW / 2, y + (ch - 16) / 2, &FONT_B, t, K_ACCENT);
            continue;
        }
        for (n = tr->first; n < e; n++) {            /* the track's notes in this bar: ticks */
            const arr_ev_t *v = &view.ev[n];
            uint32_t b0 = (uint32_t)b * blen;
            int32_t nx, nw, ny;
            if (v->tick < b0 || v->tick >= b0 + blen)
                continue;
            nx = ax + (int32_t)((v->tick - b0) * (uint32_t)aw / blen);
            nw = (int32_t)((uint32_t)v->len * (uint32_t)aw / blen);
            nw = nw < 2 ? 2 : nw > ax + aw - nx ? ax + aw - nx : nw;
            ny = ay + ah - nh - (hi > lo ? (int32_t)((v->note - lo) * (uint32_t)(ah - nh) / (hi - lo)) : (ah - nh) / 2);
            cv_rect(nx, ny, nw, nh, fg);
        }
        if (plays) {                                 /* its beats along its foot: the one now (and those before) lit */
            int32_t sw = (RG_CW - 10) / bpb;
            for (k = 0; k < bpb; k++)
                cv_rect(x + 5 + k * sw, y + ch - 4, sw - 2, 2, k <= fbeat ? K_BG : K_TEAL_T);
        }
    }
    cv_blit(0, MAIN_Y);
}
/* SEQ / EDIT while a lead track is recorded (REC on): where you are in the song. A line "REC LD1 . row 3 . bar 2.3"
 * (the row playing, its bar and beat from the row's first downbeat; else why nothing records yet: "starts with PLAY",
 * "after the Intro", "in a song only" in FREE / LOOP), under it the lead's sound, the grid and its notes in the row;
 * then the row as bars, four to a line (eight show: a longer row by eights, the playing one's), the bar playing teal
 * with its beats along its foot, the lead's notes in the row as ticks (by place and pitch); at the foot the song's
 * rows, a cell each, the one playing teal (waiting: the one it starts in, a coral ring), a dot in those holding this
 * lead's notes; the other lead's notes dim under this one's. REC while playing counts in to the next bar line (its
 * bar ringed, the beats left in it, big; the row's last bar: the next row's cell ringed) */
#define LR_STRIP 114                                 /* the song's rows */
static void draw_lead_rec(void)
{
    static const char *const WHY[8] = {"", "in a song only", "the song has no rows", "starts with PLAY", "after the Intro",
                                       "the song is ending", "from the bar line", "starts at the next bar"};
    const song_t *g = csong();
    int k = (app.lead ? app.lead - 1 : 0) % ARR_LEADS, n = g->nrows, play = arr_playing, r = 0, live, nb = 1, bpb, focus = -1, fbeat = 0;
    int win, wbars, lines, ch, b, why, cnt = 0, cin = -1, cbeats = 0, crow = -1;
    uint32_t blen, rt = 0, h, i, i0 = 0, i1 = 0, lo = 127, hi = 0;
    const lead_trk_t *lt = &g->lead[k];
    char t[48], *p;
    if (n > ARR_ROWS)
        n = ARR_ROWS;
    bpb = app.hdr[pslot()].beats;
    if (bpb < 2 || bpb > 7)
        bpb = 4;
    blen = (uint32_t)bpb * ARR_PPQ;
    live = play && arr_mode == ARR_SONG && n && arr_loop_now >= 0 && arr_sect != SEC_INTRO && arr_sect != SEC_END;
    why = app.mode != ARR_SONG ? 1 : !n ? 2 : !play ? 3 : arr_sect == SEC_INTRO ? 4 : arr_sect == SEC_END ? 5 : !live ? 6
        : arr_rec_wait ? 7 : 0;
    if (n)                                           /* (stopped: PLAY here starts at SONG's cursor row: lead_row) */
        r = play ? (arr_row < n ? arr_row : n - 1) : lead_row();
    if (n) {                                         /* the row: its loop's beats (empty: a bar of 4) x REPEAT, to bars */
        const row_t *w = &g->row[r];
        int per = loop_len(&sloops.loop[w->loop % ARR_LOOPS]);
        uint32_t len = (uint32_t)(per > 0 ? per : 4) * (w->rep ? w->rep : 1u) * ARR_PPQ;
        nb = (int)((len + blen - 1) / blen);
        i0 = leads.row0[r];
        i1 = leads.row0[r + 1];
    }
    if (live) {
        rt = arr_row_tick;
        focus = (int)(rt / blen);
        fbeat = (int)(rt % blen / ARR_PPQ);
        if (focus >= nb)
            focus = nb - 1;
    }
    for (i = i0; i < i1 && i < leads.n; i++) {       /* the row's lead notes (the other lead's too, dim): their span */
        const lead_ev_t *e = lead_at_c(&leads, i);
        cnt += lead_trk(e) == k;
        lo = e->note < lo ? e->note : lo;
        hi = e->note > hi ? e->note : hi;
    }
    if (why == 7) {                                  /* the count-in: the bar it starts on (the next row's first: its cell) */
        cbeats = bpb - fbeat;
        if (focus + 1 < nb)
            cin = focus + 1;
        else
            crow = r + 1 < n ? r + 1 : g->at_end ? 0 : -1;
    }
    win = focus > 0 ? focus / 8 : 0;
    wbars = nb - win * 8 < 8 ? nb - win * 8 : 8;
    lines = (wbars + 3) / 4;
    ch = lines == 1 ? 72 : 36;
    h = hash(2166136261u, 0x1EADu | (uint32_t)k << 16 | (uint32_t)r << 17 | (uint32_t)live << 23 | (uint32_t)why << 24);
    h = hash(h, (uint32_t)(focus + 1) | (uint32_t)fbeat << 8 | (uint32_t)nb << 12 | (uint32_t)win << 20 | (uint32_t)n << 24);
    h = hash(h, (uint32_t)(cin + 1) | (uint32_t)cbeats << 8 | (uint32_t)(crow + 1) << 12);
    h = hash(hash(h, leads.gen | (uint32_t)leads.n << 16), lt->program | (uint32_t)lt->chan << 8 | (uint32_t)proj.quant << 12 |
                                                                (uint32_t)cnt << 16);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    {   /* the line: REC's dot, "REC LD1 . row 3 . bar 2.3"; under it the sound, the grid, the row's notes */
        cv_round(8, 6, 10, 10, 5, RGB(214, 64, 52));
        p = cat_s(cat_s(t, "REC "), TRACK_NAME[ARR_LD1 + k]);
        if (why)
            cat_s(cat_s(p, "|"), WHY[why]);
        else {
            p = itoa_u((uint32_t)r + 1u, cat_s(p, "|row "));
            p = itoa_u((uint32_t)focus + 1u, cat_s(p, "|bar "));
            *p++ = '.';
            itoa_u((uint32_t)fbeat + 1u, p);
        }
        segs(24, 2, &FONT_B, t, K_TEXT);
        if (why == 1)
            cv_text(24, 20, &FONT_XS, "On SONG: PLAY, then REC here", K_ACCENT);
        else {
            p = cat_s(cat_s(t, (lt->chan & 15u) == 9u ? "Drums" : GM_PROGRAM[lt->program & 127u]), "|grid ");
            p = cat_s(p, GRID_NAME[proj.quant % ARR_NQ]);
            if (n) {
                p = itoa_u((uint32_t)cnt, cat_s(p, "|"));
                itoa_u((uint32_t)r + 1u, cat_s(p, cnt == 1 ? " note in row " : " notes in row "));
            }
            segs(24, 20, &FONT_XS, t, K_DIM);
        }
    }
    for (b = 0; b < wbars; b++) {                    /* the row's bars (eight of them at most) */
        int bi = win * 8 + b, plays = bi == focus, start = (why >= 3 && why < 7 && bi == 0) || bi == cin;
        int32_t x = RG_X + (b % 4) * (RG_CW + 4), y = TR_Y + (b / 4) * (ch + 3), ax, aw, ay, ah, nh;
        uint16_t fg = plays ? K_BG : K_TEXT, other = plays ? K_TEAL_T : K_LINE;
        uint32_t b0 = (uint32_t)bi * blen;
        if (start) {                                 /* the bar it starts in: a coral ring */
            cv_round(x, y, RG_CW, ch, 5, K_ACCENT);
            cv_round(x + 2, y + 2, RG_CW - 4, ch - 4, 3, plays ? K_TEAL : K_PANEL);
        } else
            cv_round(x, y, RG_CW, ch, 5, plays ? K_TEAL : K_PANEL);
        itoa_u((uint32_t)bi + 1u, t);
        cv_text(x + 4, y, &FONT_XS, t, plays ? K_BG : start ? K_ACCENT : K_DIM);
        if (bi == cin) {                             /* the count-in, big */
            itoa_u((uint32_t)cbeats, t);
            text_c(x + RG_CW / 2, y + (ch - 32) / 2, &FONT_L, t, K_ACCENT);
            continue;
        }
        ax = x + 4;
        aw = RG_CW - 8;
        ay = y + 16;
        ah = y + ch - 7 - ay;
        nh = ah >= 24 ? 3 : 2;
        for (int pass = 0; pass < 2; pass++)         /* the lead's notes in this bar: ticks (the other lead's first, dim) */
            for (i = i0; i < i1 && i < leads.n; i++) {
                const lead_ev_t *e = lead_at_c(&leads, i);
                uint32_t pos = lead_pos(e), len = lead_len(e);
                int32_t nx, nw, ny;
                if ((lead_trk(e) == k) != pass || pos < b0 || pos >= b0 + blen)
                    continue;
                nx = ax + (int32_t)((pos - b0) * (uint32_t)aw / blen);
                nw = (int32_t)(len * (uint32_t)aw / blen);
                nw = nw < 2 ? 2 : nw > ax + aw - nx ? ax + aw - nx : nw;
                ny = ay + ah - nh - (hi > lo ? (int32_t)((e->note - lo) * (uint32_t)(ah - nh) / (hi - lo)) : (ah - nh) / 2);
                cv_rect(nx, ny, nw, nh, pass ? fg : other);
            }
        if (plays) {                                 /* its beats along its foot: the one now (and those before) lit */
            int32_t sw = (RG_CW - 10) / bpb;
            for (int q = 0; q < bpb; q++)
                cv_rect(x + 5 + q * sw, y + ch - 4, sw - 2, 2, q <= fbeat ? K_BG : K_TEAL_T);
        }
    }
    if (nb > 8) {                                    /* a longer row: which eight ("9-16 of 24"), over the bars' right */
        p = itoa_u((uint32_t)win * 8u + 1u, t);
        *p++ = '-';
        p = itoa_u((uint32_t)(win * 8 + wbars), p);
        itoa_u((uint32_t)nb, cat_s(p, " of "));
        cv_text(232 - text_w(&FONT_XS, t), 20, &FONT_XS, t, K_DIM);
    }
    if (n) {                                         /* the song's rows: the one playing teal, a dot where this lead has notes */
        int32_t pitch = 224 / n > 28 ? 28 : 224 / n, cw = pitch - 2;
        for (int j = 0; j < n; j++) {
            int32_t x = 8 + j * pitch, y = LR_STRIP;
            int on = live && j == r, ring = (!live && why >= 3 && j == r) || j == crow;
            if (ring) {
                cv_round(x, y, cw, 16, 3, K_ACCENT);
                cv_round(x + 1, y + 1, cw - 2, 14, 2, K_PANEL);
            } else
                cv_round(x, y, cw, 16, 3, on ? K_TEAL : K_PANEL);
            if (cw >= 13) {
                itoa_u((uint32_t)j + 1u, t);
                text_c(x + cw / 2, y - 1, &FONT_XS, t, on ? K_BG : K_DIM);
            }
            if (leads_count(&leads, j, k))
                cv_rect(x + cw / 2 - 2, y + 12, 4, 2, on ? K_BG : K_PLUM);
        }
    }
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
    pattern_head(s, -1);
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
/* the selected track's sound and transposition (a lead track: its sound, "as played" over the whole keyboard, fixed) */
static void draw_track(void)
{
    int s = pslot(), k2 = ui.page == P_TRACK2;
    arr_trk_t lb;
    const arr_trk_t *t = sel_arr(&lb);
    uint32_t h = hash(2166136261u, (uint32_t)s | (uint32_t)sel_trk() << 4 | (uint32_t)k2 << 8);
    char a[16], b[16];
    h = hash(h, t->program | (uint32_t)t->chan << 8 | (uint32_t)t->x.type << 16 | (uint32_t)t->x.hikey << 24);
    h = hash(h, t->x.lo | (uint32_t)t->x.hi << 8 | (uint32_t)t->flags << 16 | (uint32_t)(app.lead ? sel_lead()->vel : 0) << 24);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    {
        int32_t x = pill(8, 0, 0, TRACK_NAME[sel_trk()], app.lead ? K_PLUM : K_TEAL, K_BG);
        x = cv_text(x + 8, 2, &FONT_B, app.lead ? "Lead track" : TYPE_NAME[t->x.type % NXT], K_DIM);
        if (t->x.type == XT_PARALLEL && (t->flags & ARR_TF_ASPLAYED))   /* (switched at its first REC: notes as
                                                  * played, moved by the root's move, -6..+5; a lead's always) */
            cv_text(x + 6, 3, &FONT_XS, "as played", K_TEAL);
    }
    if ((t->chan & 15u) == 9u) {
        cv_text(10, 28, &FONT_L, "Drum kit", K_TEXT);
        if (!k2)
            cv_text(10, 64, &FONT_S, "Channel 10: a sound on each key", K_DIM);
    } else {
        int32_t nx;
        int two = 0;
        const char *nm = GM_PROGRAM[t->program & 127u];
        itoa_u(t->program + 1u, a);              /* "81", as the knob (no leading zeros) */
        nx = cv_text(10, 28, &FONT_L, a, K_ACCENT) + 10;
        if (text_w(&FONT_L, nm) <= 230 - nx)     /* the name big: "Clavinet"; too long, on two lines ("Tango" / */
            cv_text(nx, 28, &FONT_L, nm, K_TEXT);   /* "Accordion"; the family line gives way), else smaller */
        else {
            char l1[24];
            uint32_t i, sp = 0;
            for (i = 0; nm[i] && i < sizeof l1 - 1u; i++)
                if ((l1[i] = nm[i]) == ' ') {
                    l1[i] = 0;
                    if (text_w(&FONT_L, l1) <= 230 - nx)
                        sp = i;
                    l1[i] = ' ';
                }
            if (sp && !k2 && text_w(&FONT_L, nm + sp + 1) <= 230 - nx) {
                l1[sp] = 0;
                cv_text(nx, 24, &FONT_L, l1, K_TEXT);
                cv_text(nx, 56, &FONT_L, nm + sp + 1, K_TEXT);
                two = 1;
            } else
                text_fit(nx, 36, &FONT_B, nm, 230 - nx, K_TEXT);
        }
        if (!k2 && !two)
            cv_text(10, 64, &FONT_S, GM_FAMILY[(t->program & 127u) / 8u], K_DIM);
        if (app.lead && !k2 && !two) {           /* the velocity its notes are recorded with */
            itoa_u(sel_lead()->vel, cat_s(a, "velocity "));
            cv_text(230 - text_w(&FONT_S, a), 64, &FONT_S, a, K_TEXT);
        }
    }
    if (app.lead)                                /* a lead: as played, over every key (no range, no high key) */
        cv_text(10, 98, &FONT_XS, "notes as played, over the whole keyboard", K_DIM);
    else {   /* the range over the whole MIDI keyboard */
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
        if (!app.lead)                           /* (a lead has none) */
            cv_text(10, 64, &FONT_S, a, K_TEXT);
        e = cat_s(b, "channel ");
        itoa_u((t->chan & 15u) + 1u, e);
        cv_text(230 - text_w(&FONT_S, b), 64, &FONT_S, b, K_TEXT);
    }
    cv_blit(0, MAIN_Y);
}
/* the ten tracks' strips (the pattern's eight, the song's two leads after a gap, plum): volume, pan, the sends (reverb,
 * then chorus, as the knobs); the selected one tinted */
#define MX_PITCH 23
#define MX_W 21
static int32_t strip_x(int i) { return 3 + i * MX_PITCH + (i >= ARR_TRACKS ? 4 : 0); }
static uint16_t strip_colour(int i)
{
    return trk_muted(i) ? K_LINE : i >= ARR_TRACKS ? K_PLUM : i < 3 ? K_ACCENT : K_TEAL;
}
static void draw_mixer(void)
{
    int s = pslot(), i;
    arr_trk_t lb;
    uint32_t h = hash(2166136261u, (uint32_t)s | (uint32_t)sel_trk() << 4);
    for (i = 0; i < ARR_ALLTRK; i++) {
        const arr_trk_t *t = strip_trk(i, &lb);
        h = hash(h, t->vol | (uint32_t)t->pan << 8 | (uint32_t)t->rev << 16 | (uint32_t)t->cho << 24);
        h = hash(h, (uint32_t)trk_muted(i));
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < ARR_ALLTRK; i++) {
        const arr_trk_t *t = strip_trk(i, &lb);
        int32_t x = strip_x(i), w = MX_W, sel = i == sel_trk(), vh = 66 * t->vol / 127;
        uint16_t c = strip_colour(i);
        char r[6];
        cv_round(x, 0, w, MAIN_H, 5, sel ? K_TEAL_T : K_PANEL);
        text_c(x + w / 2, 0, &FONT_XS, TRACK_NAME[i], sel ? K_TEXT : K_DIM);
        cv_round(x + 5, 18, 11, 68, 4, K_BG);   /* volume */
        if (vh > 0)
            cv_round(x + 5, 18 + 68 - vh - 1, 11, vh, 4, c);
        cv_rect(x + 3, 94, w - 6, 2, K_LINE);   /* pan */
        cv_round(x + 3 + (w - 10) * t->pan / 127, 91, 4, 8, 2, K_TEXT);
        itoa_u(t->rev, r);                       /* the sends: reverb, chorus */
        text_c(x + w / 2, 99, &FONT_XS, r, t->rev ? K_TEXT : K_DIM);
        itoa_u(t->cho, r);
        text_c(x + w / 2, 113, &FONT_XS, r, t->cho ? K_TEXT : K_DIM);
    }
    cv_blit(0, MAIN_Y);
}
/* the tone: each track's brightness (CC 74, the filter's cutoff) and resonance (CC 71), 64 neutral (a line) */
static void draw_tone(void)
{
    int s = pslot(), i;
    arr_trk_t lb;
    uint32_t h = hash(2166136261u, 0x70000u | (uint32_t)s | (uint32_t)sel_trk() << 4);
    for (i = 0; i < ARR_ALLTRK; i++) {
        const arr_trk_t *t = strip_trk(i, &lb);
        h = hash(h, t->bright | (uint32_t)t->reso << 8 | (uint32_t)trk_muted(i) << 16);
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < ARR_ALLTRK; i++) {
        const arr_trk_t *t = strip_trk(i, &lb);
        int32_t x = strip_x(i), w = MX_W, sel = i == sel_trk(), b = arr_bright(t), r = arr_reso(t);
        int32_t bh = 66 * b / 127, rh = 66 * r / 127;
        char v[6];
        cv_round(x, 0, w, MAIN_H, 5, sel ? K_TEAL_T : K_PANEL);
        text_c(x + w / 2, 0, &FONT_XS, TRACK_NAME[i], sel ? K_TEXT : K_DIM);
        cv_round(x + 3, 18, 6, 68, 3, K_BG);    /* brightness */
        cv_round(x + 12, 18, 6, 68, 3, K_BG);   /* resonance */
        if (bh > 0)
            cv_round(x + 3, 18 + 68 - bh - 1, 6, bh, 3, trk_muted(i) ? K_LINE : i >= ARR_TRACKS ? K_PLUM : K_TEAL);
        if (rh > 0)
            cv_round(x + 12, 18 + 68 - rh - 1, 6, rh, 3, trk_muted(i) ? K_LINE : K_AMBER);
        cv_rect(x + 2, 18 + 68 - 34, w - 4, 1, K_DIM); /* 64: neutral */
        itoa_u((uint32_t)b, v);                  /* brightness, resonance (as the knobs) */
        text_c(x + w / 2, 94, &FONT_XS, v, b == 64 ? K_DIM : K_TEXT);
        itoa_u((uint32_t)r, v);
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
        char k[2] = {(char)('1' + i - first), 0};
        if (on)
            cv_round(4, y, 232, 18, 5, K_PANEL);
        cv_round(8, y + 1, 76, 16, 5, sect_colour((int)i + 1));
        text_c(8 + 38, y, &FONT_S, SECT_NAME[i + 1], K_BG);
        pat_title((int)i, t);                    /* "P02 Funk", "Pop A" */
        text_fit(92, y, on ? &FONT_B : &FONT_S, t, 122, K_TEXT);   /* the knobs' three: bold */
        if (on)
            cv_text(222, y + 1, &FONT_XS, k, K_DIM);
    }
    cv_blit(0, MAIN_Y);
}
static void setup_row(int32_t y, const char *name, const char *val)
{
    cv_round(8, y, 224, 24, 6, K_PANEL);
    cv_text(18, y + 3, &FONT_S, name, K_DIM);
    cv_text(222 - text_w(&FONT_B, val), y + 3, &FONT_B, val, K_TEXT);
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
                                       ((uint32_t)(qb_status() + 8) << 12) + ((uint32_t)proj.click << 24));
    char t[16];
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    setup_row(2, "Clock", proj.clock ? "MIDI in" : "internal");
    setup_row(28, "MIDI out", proj.mout ? "on" : "off");
    setup_row(54, "Lights", LIGHTS_NAME[proj.leds % 3u]);
    setup_row(80, "Click", proj.click == ARR_CLICK_ON ? "every beat" : proj.click == ARR_CLICK_REC ? "when recording"
                                                                      : "count-in only");
    bank_text(t);
    setup_row(106, "QY70 styles", t);
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
        h = hash(h, sloops.loop[i].bars | (uint32_t)(uint8_t)sloops.loop[i].name[0] << 8);
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
        const loop_t *l = &sloops.loop[i];
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
            if (*e)
                text_fit(x + 28, y - 1, &FONT_XS, e + 1, 80, K_TEXT);
            else {                                   /* its default name: its length instead ("2 bars") */
                cat_s(itoa_u(l->bars, t), l->bars == 1 ? " bar" : " bars");
                cv_text(x + 28, y - 1, &FONT_XS, t, K_DIM);
            }
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
/* HOME: the song's style, tempo, transpose; LOOP ("-" FREE, L1..L16, "Song" while in the song) */
static void home_knobs(knob_t *k)
{
    char t[8];
    style_knob(&k[0]);
    kn(&k[1], "Tempo", csong()->tempo, 240);
    transpose_knob(&k[2]);
    if (app.mode == ARR_SONG)
        kv(&k[3], "Loop", "Song", ARR_LOOPS, ARR_LOOPS);
    else if (app.mode == ARR_LOOP) {
        itoa_u(app.loop + 1u, cat_s(t, "L"));
        kv(&k[3], "Loop", t, app.loop + 1, ARR_LOOPS);
    } else
        kv(&k[3], "Loop", "-", 0, ARR_LOOPS);
}
static void edit_knobs(knob_t *k)
{
    int s = pslot();
    arr_trk_t lb;
    const arr_trk_t *t = sel_arr(&lb);
    char b[8];
    switch (ui.page) {
    case P_PATTERN:
        kv(&k[0], "Grid", GRID_NAME[proj.quant % ARR_NQ], grid_fill(), ARR_NQ - 1);
        kv(&k[3], "Mute", trk_muted(sel_trk()) ? "on" : "off", trk_muted(sel_trk()), 0);
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
        if (app.lead)                            /* a lead: as played, always */
            kv(&k[1], "Type", "as played", 0, 0);
        else
            kv(&k[1], "Type", TYPE_SHORT[t->x.type % NXT], t->x.type, NXT - 1);
        note_name(t->x.lo, b);
        if (!app.lead)                           /* (a lead: as played over every key, no range to set) */
            kv(&k[2], "Low", b, t->x.lo, 127);
        note_name(t->x.hi, b);
        if (app.lead)                            /* a lead: the velocity its notes are recorded with */
            kn(&k[3], "Velocity", sel_lead()->vel, 127);
        else
            kv(&k[3], "High", b, t->x.hi, 127);
        break;
    case P_TRACK2:
        if (app.lead)
            ;                                    /* (a lead: no high key) */
        else if (t->x.hikey >= XT_NOKEY)
            kv(&k[0], "High key", "off", 0, 0);
        else {
            note_name(t->x.hikey, b);
            b[(b[1] == '#' || b[1] == 'b') ? 2 : 1] = 0;
            kv(&k[0], "High key", b, t->x.hikey, 11);
        }
        kn(&k[1], "Channel", (t->chan & 15) + 1, ARR_CHAN_MAX + 1);
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
    case P_SONG: case P_SONG2: {                     /* the row's (the END row: none yet) */
        const row_t *w = app.row < g->nrows ? &g->row[app.row] : 0;
        if (app.row < g->nrows)
            kn(&k[0], "Row", app.row + 1, g->nrows < ARR_ROWS ? g->nrows + 1 : g->nrows);
        else
            kv(&k[0], "Row", "end", g->nrows + 1, g->nrows + 1);
        if (ui.page == P_SONG2) {
            if (!w)
                kv(&k[1], "Key", "-", 0, 0);
            else {
                b[0] = w->key > 0 ? '+' : '-';
                itoa_u((uint32_t)(w->key < 0 ? -w->key : w->key), b + 1);
                kv(&k[1], "Key", w->key ? b : "0", w->key + 6, 11);
            }
            break;
        }
        if (!w) {
            kv(&k[1], "Loop", "-", 0, 0);
            kv(&k[2], "Section", "-", 0, 0);
            kv(&k[3], "Repeat", "-", 0, 0);
            break;
        }
        itoa_u((uint32_t)(w->loop % ARR_LOOPS) + 1u, cat_s(b, "L"));
        kv(&k[1], "Loop", b, w->loop % ARR_LOOPS, ARR_LOOPS - 1);
        kv(&k[2], "Section", ROW_SECT_NAME[w->sect % ROW_NSECT], w->sect % ROW_NSECT, ROW_NSECT - 1);
        itoa_u(w->rep ? w->rep : 1u, cat_s(b, "x"));
        kv(&k[3], "Repeat", b, w->rep, 8);
        break;
    }
    case P_SONG3:
        kv(&k[0], "Intro", g->intro ? "on" : "off", g->intro, 0);
        kv(&k[1], "Ending", g->ending ? "on" : "off", g->ending, 0);
        kv(&k[2], "At end", g->at_end ? "again" : "stop", g->at_end, 0);
        kn(&k[3], "Song", proj.song + 1, ARR_SONGS);
        break;
    case P_LOOP: {
        const loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
        int has, b = app.cbeat;
        chord_t c = chord_shift(cursor_chord(l, b, &has), xpose_now());
        kv(&k[0], "Root", KEY_NAME[c.root % 12u], c.root, 11);
        kv(&k[1], "Type", !has ? "-" : c.type == CH_MAJ ? "maj" : chord_type_name(c.type), has ? c.type : 0, CH_DIM);
        if (c.bass == CH_NOBASS)
            kv(&k[2], "Bass", "-", 0, 11);
        else
            kv(&k[2], "Bass", KEY_NAME[c.bass % 12u], c.bass, 11);
        kv(&k[3], "Step", STEP_NAME[app.cstep % 3u], app.cstep, 2);
        break;
    }
    case P_LOOP2: {
        const loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
        int bpb = l->beats ? l->beats : 4;
        char v[9];
        kn(&k[0], "Length", l->bars ? l->bars : 1, loop_max_bars(bpb));
        kn(&k[1], "Beats", bpb, 7);
        {
            int n = 0;
            for (int i = 0; i < 8 && l->name[i]; i++)
                v[n++] = l->name[i];
            v[n] = 0;
        }
        kv(&k[2], "Name", v, loop_name_index(l), NLOOP_NAMES);
        break;
    }
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
        kv(&k[3], "Click", CLICK_NAME[proj.click % 3u], proj.click, 2);
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
/* "C1 PIANO", "LD1 SQUARE LEAD": the selected track and its sound */
static char *track_tag(char *t)
{
    arr_trk_t lb;
    cat_s(cat_s(cat_s(t, TRACK_NAME[sel_trk()]), " "), trk_sound(sel_arr(&lb)));
    return upcase(t);
}
/* the tag over the knobs: whose they are (the spec's Screens) */
static void page_tag(char *t)
{
    char *e;
    *t = 0;
    switch (ui.page) {
    case P_HOME:
        if (ui.take)                                 /* a new loop's take: "NEW LOOP L9" (not FREE) */
            itoa_u(ui.take_loop + 1u, cat_s(t, "NEW LOOP L"));
        else if (app.mode == ARR_LOOP) {
            loop_label(app.loop, t);
            upcase(t);
        } else if (app.mode == ARR_SONG && !csong()->nrows)
            cat_s(t, "NO ROWS");
        else if (app.mode == ARR_SONG)
            itoa_u(arr_row + 1u, cat_s(t, "ROW "));
        else
            cat_s(t, "FREE");
        break;
    case P_SONG: case P_SONG2: case P_SONG3: {       /* "ROW 3", "END ROW", "NO ROWS" */
        int n = csong()->nrows;
        if (app.row < n)
            itoa_u(app.row + 1u, cat_s(t, "ROW "));
        else
            cat_s(t, n ? "END ROW" : "NO ROWS");
        break;
    }
    case P_LOOP: case P_LOOP2: {                     /* the loop and where it is (or the cursor): "L3|BAR 2.3", the STEP */
        place_t pl;
        const loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
        uint32_t bt = l->beats ? l->beats : 4u, b = 0;
        place_read(&pl);
        b = arr_playing && pl.loop == app.loop ? pl.lbeat : app.cbeat;   /* (stopped: the cursor's beat) */
        e = itoa_u(app.loop + 1u, cat_s(t, "L"));
        e = itoa_u(b / bt + 1u, cat_s(e, "|BAR "));
        *e++ = '.';
        e = itoa_u(b % bt + 1u, e);
        if (app.cstep < 2)                           /* STEP not a bar: say so ("L10|BAR 1.1|HALF") */
            cat_s(e, app.cstep ? "|HALF" : "|BEAT");
        break;
    }
    case P_PATTERN: case P_PATSET: case P_TRACK: case P_TRACK2:
        track_tag(t);
        break;
    case P_MIXER: case P_TONE:
        cat_s(cat_s(t, "MIXER: "), TRACK_NAME[sel_trk()]);
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
    if (home_shown()) {                              /* (the keys: white a loop, black a section; HOME + REC, + PLAY) */
        cat_s(t, "SELECT: TEMPO|REC: UNDO|PLAY: REDO");   /* (the keys: on screen, the loops and the sections) */
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
    if (!lst.open && plat_ms() < ui.slots_until) {   /* NO FREE SLOT said: where to see what fills them */
        cat_s(t, "NO FREE SLOT|SEL: SLOTS, FREE ONE");
        return;
    }
    if (lst.open && lst.n) {
        int k = lst.row[lst.sel].kind;
        cat_s(t, k == LK_NAME ? "ALGO: LETTER|SEL: NEXT|HOME: BACK"
               : k == LK_ACTION ? "SELECT: MOVE|SEL: DO IT|HOME: BACK"
               : k == LK_PICK ? "ALGO: PICK|SEL: DO IT|HOME: BACK"
               : k == LK_SUB ? "SELECT: MOVE|SEL: OPEN|HOME: BACK"
               : k == LK_SLOT ? (slot_kind[lst.row[lst.sel].id] == SLOT_NONE ? "SELECT: MOVE|HOME: BACK"
                                                                              : "SELECT: MOVE|SEL: FREE|HOME: BACK")
               : "SELECT: MOVE|ALGO: CHANGE|HOME: BACK");
        return;
    }
    switch (ui.page) {                               /* (recording: REC stops it, as it says) */
    case P_HOME:
        cat_s(t, ui.take || ui.rec_chords ? "KEYS: CHORDS|REC: STOP"
                 : app.mode == ARR_FREE ? "KEYS: CHORDS|REC: NEW LOOP" : "KEYS: CHORDS|REC: WRITE");
        break;
    case P_LOOP: case P_LOOP2:
        cat_s(t, ui.rec_chords ? "KEYS: CHORDS|REC: STOP|OCT+: SKIP" : "KEYS: CHORDS|REC: WRITE|OCT+: SKIP");
        break;
    case P_SONG: case P_SONG2: case P_SONG3:
        cat_s(t, ui.song_rec ? "HOME + KEYS: ROWS|REC: STOP" : "WHITE: ROW'S LOOP|BLACK: MUTES");
        break;
    case P_PATTERN: case P_PATSET: case P_TRACK: case P_TRACK2: {   /* "KEYS: C1 PIANO|REC: RECORD" */
        char s[40];
        if (trk_rec_shown() && !app.lead && loop_longer()) {   /* REC's screen: SEL makes it as long as the loop */
            itoa_u((uint32_t)loop_longer(), cat_s(t, "SEL: "));
            cat_s(t + slen(t), " BARS, AS THE LOOP|REC: STOP");
            break;
        }
        if (ui.follow_hint) {                        /* REC kept the track's type (it has notes): how to change that */
            cat_s(t, "ERASE TRACK (SEL) TO PLAY AS PLAYED");
            break;
        }
        track_tag(s);
        for (;;) {
            cat_s(cat_s(cat_s(t, "KEYS: "), s), arr_recording || ui.rec_want ? "|REC: STOP" : "|REC: RECORD");
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
        cat_s(t, "KNOBS: CLOCK, MIDI OUT, LIGHTS, CLICK");
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
#define PG_ANY 0xFFFFFFFFu
#define PG_SONG (PG(P_SONG) | PG(P_SONG2) | PG(P_SONG3))
#define PG_TRACK (PG(P_PATTERN) | PG(P_PATSET) | PG(P_TRACK) | PG(P_TRACK2))
#define CF_LEAD (1u << 31)                           /* (pages past NPAGES) only with a lead track selected */
#define CF_NOLEAD (1u << 30)                         /* only without (PG_ANY: both, either) */
static const struct { uint8_t btn; uint32_t pages; const char *title, *l1, *l2; } CARD[] = {
    {B_ARP, PG_ANY, "Loops", "Your chord loops. Play chords.", "REC writes them: at the cursor, or in time."},
    {B_SEQ, PG_ANY, "Pattern", "The section's eight tracks; SELECT: its length and name.", 0},
    {B_EDIT, PG_ANY & ~CF_NOLEAD, "Lead track", "Its sound; KNOB 4 the velocity its notes are recorded with.",
     "SEL: QUANTISE a row to SEQ's grid. ALGORITHM: the tracks."},
    {B_EDIT, PG_ANY, "Track", "The selected track's sound and range; SELECT: its keys.",
     "ALGORITHM past C4: the lead tracks LD1, LD2."},
    {B_LFO, PG_ANY, "Mixer", "Volume, pan and sends; SELECT: the tone. Black keys pick the track.", 0},
    {B_FX, PG_ANY, "Master FX", "The reverb and the chorus every track sends to.", 0},
    {B_ENV, PG_ANY, "Style", "The six sections' patterns. PRESETS: the next style.", 0},
    {B_GLO, PG_ANY, "Setup", "Clock, MIDI out, lights, the click (off, rec, on); SELECT: the panel.", 0},
    {B_SEL, PG_TRACK, "Lists", "This page as a list; SLOTS: what fills the 16 slots, FREE one.",
     "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SEL, PG_ANY, "Lists", "This page as a list, MORE, actions.", "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SAVE, PG_SONG, "Save", "Tap: save.", "Rows: a white key adds one by itself."},
    {B_SAVE, PG_ANY, "Save", "Tap: save. Hold + a white key: copy here.", 0},
    {B_REC, PG(P_HOME) | PG(P_LOOP) | PG(P_LOOP2), "Write chords",
     "Into the loop playing: at the cursor, or in time as it plays.", "On HOME in FREE: a new loop. HOME + REC: undo."},
    {B_REC, PG_TRACK | CF_NOLEAD, "Record the track", "What the keys play goes into the selected track.",
     "HOME + REC: undo (a pass at a time)."},
    {B_REC, PG_TRACK | CF_LEAD, "Record the lead", "While the song plays, what the keys play goes into the row playing.",
     "PLAY here starts the song; SEL: PLAY FROM ROW on SONG."},
    {B_REC, PG_SONG, "Record the rows", "With PLAY: the loops and sections you pick with HOME + keys,",
     "and the mutes you change. HOME + REC: undo."},
    {B_REC, PG_ANY, "Record", "Nothing to record on this page.", "HOME + REC: undo."},
    {B_PLAY, PG_ANY, "Play", "Start and stop. On SONG: the song from row 1 (from a loop: at the next bar).",
     "HOME + PLAY: redo."},
    {B_OCTDN, PG(P_LOOP) | PG(P_LOOP2), "Cursor", "OCT-: back a step. OCT+: skips a step (the chord goes on).", 0},
    {B_OCTUP, PG(P_LOOP) | PG(P_LOOP2), "Cursor", "OCT+ skips a step (the chord goes on); OCT-: back a step.", 0},
    {B_OCTDN, PG_ANY, "Octave", "The keys an octave down.", "Both held 5 s: update mode."},
    {B_OCTUP, PG_ANY, "Octave", "The keys an octave up.", "Both held 5 s: update mode."},
};
#define NCARDS (sizeof CARD / sizeof CARD[0])
static int card_find(int btn)                        /* the card for btn on this page, -1 none */
{
    for (uint32_t i = 0; i < NCARDS; i++)
        if (CARD[i].btn == btn && (CARD[i].pages >> ui.page & 1u) &&
            (CARD[i].pages & (CF_LEAD | CF_NOLEAD)) != (app.lead ? CF_NOLEAD : CF_LEAD))
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
    uint32_t h = hash(2166136261u, 0xCA000u + ui.card + ((uint32_t)ui.page << 8) + ((uint32_t)app.lead << 16));
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
        return sloops.loop[app.loop % ARR_LOOPS].name;
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
    if (lst.slots) {                                 /* SLOTS: the 16, as scanned */
        for (int i = 0; i < ARR_USER_PATTERNS; i++)
            list_add(slot_txt[i], 0, LK_SLOT, i);
        if (lst.sel >= lst.n)
            lst.sel = (uint8_t)(lst.n - 1);
        return;
    }
    if (keep == P_HOME)
        f = keep, n = 1;
    else if (song_pg(keep))                          /* SONG: its three pages */
        f = P_SONG, n = 3;
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
    } else if (song_pg(keep)) {
        char v[8];
        if (ui.pick_song % ARR_SONGS == proj.song)    /* COPY SONG TO: another song */
            ui.pick_song = (uint8_t)((proj.song + 1u) % ARR_SONGS);
        static char from[20];                        /* "PLAY FROM ROW 3": the cursor's row */
        char r[4];
        itoa_u(ui.pick_song % ARR_SONGS + 1u, cat_s(v, "Song "));
        itoa_u(app.row + 1u, r);
        cat_s(cat_s(cat_s(from, ACTION_NAME[A_PLAY_FROM]), " "), r);
        list_add(from, r, LK_ACTION, A_PLAY_FROM);
        list_add(ACTION_NAME[A_INSERT_ROW], 0, LK_ACTION, A_INSERT_ROW);
        list_add(ACTION_NAME[A_DELETE_ROW], 0, LK_ACTION, A_DELETE_ROW);
        list_add(ACTION_NAME[A_CLEAR_SONG], 0, LK_ACTION, A_CLEAR_SONG);
        list_add(ACTION_NAME[A_COPY_SONG], v, LK_PICK, A_COPY_SONG);
    } else if (keep >= P_PATTERN && keep <= P_MASTER) {
        if (app.lead) {                              /* a lead: its notes in a row ("the cursor's / playing"), all; a row's
                                                      * notes copied into it (the pattern's actions: not the lead's) */
            static char er[24], ea[16], qr[24], cp[24];
            int lr = lead_row(), nr = csong()->nrows;
            char *e = cat_s(cat_s(cat_s(er, "ERASE "), TRACK_NAME[sel_trk()]), " IN ROW ");
            itoa_u((uint32_t)lr + 1u, e);
            cat_s(cat_s(ea, "ERASE "), TRACK_NAME[sel_trk()]);
            e = cat_s(cat_s(cat_s(qr, "QUANTISE "), TRACK_NAME[sel_trk()]), " ROW ");
            itoa_u((uint32_t)lr + 1u, e);
            list_add(er, 0, LK_ACTION, A_ERASE_LEAD_ROW);
            list_add(ea, 0, LK_ACTION, A_ERASE_LEAD);
            list_add(qr, GRID_NAME[proj.quant % ARR_NQ], LK_ACTION, A_QUANT_LEAD_ROW);
            if (nr > 1) {                            /* COPY LD1 TO ROW 3, from (ALGORITHM) another row */
                char v[12];
                if (ui.pick_lrow >= nr || ui.pick_lrow == lr)
                    ui.pick_lrow = (uint8_t)(lr ? lr - 1 : 1);
                e = cat_s(cat_s(cat_s(cp, "COPY "), TRACK_NAME[sel_trk()]), " TO ROW ");
                itoa_u((uint32_t)lr + 1u, e);
                itoa_u(ui.pick_lrow + 1u, cat_s(v, "from "));
                list_add(cp, v, LK_PICK, A_COPY_LEAD);
            }
        } else {
            int lb = keep <= P_TRACK2 ? loop_longer() : 0;
            if (lb) {                                /* a pattern shorter than the loop: as long */
                char v[4];
                itoa_u((uint32_t)lb, v);
                list_add(ACTION_NAME[A_MATCH_BARS], v, LK_ACTION, A_MATCH_BARS);
            }
            list_add(ACTION_NAME[A_ERASE_TRACK], 0, LK_ACTION, A_ERASE_TRACK);
            list_add(ACTION_NAME[A_CLEAR_PATTERN], 0, LK_ACTION, A_CLEAR_PATTERN);
            list_add(ACTION_NAME[A_BACK_TO_STYLE], 0, LK_ACTION, A_BACK_TO_STYLE);
        }
        if (keep <= P_TRACK2)                        /* SEQ, EDIT: what fills the 16 slots */
            list_add(ACTION_NAME[A_SLOTS], 0, LK_SUB, A_SLOTS);
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
        for (const char *c = lst.row[i].name; *c; c++)   /* ("ERASE LD1 IN ROW 3": the row in the name) */
            h = hash(h, (uint8_t)*c);
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
        if (k == LK_SLOT) {                          /* "P03 . Funk A . S1", "S2 leads 1/2", "S1 loops", "free" (dim) */
            int id = lst.row[i].id;
            segs(14, y, &FONT_B, lst.row[i].name, slot_kind[id] == SLOT_NONE ? dim : fg);
            if (slot_kind[id] != SLOT_NONE)
                glyph_enter(208, y + 5, fg);
        } else if (k == LK_KNOB) {
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
            if (k == LK_SUB)                         /* another list */
                glyph_pick(213, y + 4, fg);
            else if (k == LK_PICK) {
            {
                int32_t room = 206 - (14 + text_w(&FONT_B, lst.row[i].name) + 6), w = text_w(&FONT_S, v);
                text_fit(206 - (w < room ? w : room), y, &FONT_S, v, room, fg);
            }
                glyph_pick(213, y + 4, fg);
            } else {
                if (*v)                              /* (QUANTISE: the grid) */
                    cv_text(202 - text_w(&FONT_S, v), y, &FONT_S, v, dim);
                glyph_enter(208, y + 5, fg);
            }
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
    else if (ui.page == P_LOOP || ui.page == P_LOOP2)
        draw_loop();
    else if (trk_rec_shown())
        draw_trk_rec();
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
    else if (song_page())
        draw_song();
    else
        draw_coming();
}
