/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's pages: the state the input layer (ui.c) and the screen share, and the drawing. Part of the unity
 * build, after project.c and before ui.c; the host simulator includes it too.
 *
 * The screen: a header (the page, the tempo and REC), the main band, the tag (whose the knobs are, the page
 * dots), the four knobs' strip, the encoders' line (what SELECT, ALGORITHM, PRESETS do here) and the footer (what
 * the keys do here). Each band is drawn only when what it
 * shows changed (a hash of it). Over the main band: a help card (a button held alone), a list (SEL). Pages not
 * built yet show their name. */
#include "gm_names.h"

/* the colours, one meaning each (docs/superpowers/specs/2026-10-11-visibility-design.md, "The colours"):
 * teal: sounding now (the bar, beat, row, section playing; the playhead) and nothing else. Coral: you (the cursor, an
 * edit's flash, the knob last turned, REC, a chord you play over a progression, questions and messages). Ink and its
 * neutrals: everything that stays put (names, values, selections, the place PLAY starts). Slate / amber: Groove A /
 * Groove B, on their pills only (Intro, Fills, Ending: neutral outlined pills). Plum: the melodies */
#define K_BG RGB(250, 244, 232)
#define K_PANEL RGB(236, 228, 212)
#define K_PANEL2 RGB(222, 211, 192)                  /* a selected row or strip (with an ink outline) */
#define K_LINE RGB(216, 205, 186)
#define K_DIM RGB(150, 136, 118)
#define K_TEXT RGB(72, 58, 48)
#define K_ACCENT RGB(242, 120, 72)
#define K_TEAL RGB(38, 166, 154)
#define K_TEAL_T RGB(196, 230, 222)
#define K_AMBER RGB(206, 138, 34)                    /* Groove B */
#define K_SLATE RGB(84, 118, 168)                    /* Groove A */
#define K_PLUM RGB(150, 112, 170)                    /* the melodies */
#define K_REC K_ACCENT                                /* REC (its dot, its line): you */

/* the pages (the spec's Buttons table): a button opens its first page; SELECT turns them */
enum { P_HOME, P_SONG, P_SONG2, P_SONG3, P_LOOP, P_LOOP2, P_PATTERN, P_PATSET, P_TRACK, P_TRACK2, P_MIXER, P_TONE, P_MASTER,
       P_STYLE, P_STYLE2, P_SETUP, P_PANEL, NPAGES };
/* (the words on screen: docs/superpowers/specs/2026-10-11-vocabulary-design.md; P_LOOP is the Progression page, P_PATTERN
 * Band, P_TRACK Sound, P_MASTER Effects) */
static const char *const PAGE_NAME[NPAGES] = {"Home", "Song", "Song", "Song", "Progression", "Progression", "Band",
                                              "Groove length", "Sound", "Sound 2", "Mixer", "Mixer tone", "Effects",
                                              "Style", "Style", "Setup", "Status"};
/* the page buttons: a button opens its first page, SELECT the next (the tag's dots: which). HOME's three: HOME, then
 * SONG's two pages (P_SONG3, once a third, is no longer shown: its knobs are on P_SONG2; the numbers stay) */
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_HOME, 3}, {B_ARP, P_LOOP, 2}, {B_SEQ, P_PATTERN, 2}, {B_EDIT, P_TRACK, 2}, {B_LFO, P_MIXER, 2},
    {B_FX, P_MASTER, 1}, {B_ENV, P_STYLE, 2}, {B_GLO, P_SETUP, 2}};
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
static int page_group(int p)                         /* the PAGE_BTN entry page p is on */
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (p >= PAGE_BTN[i].first && p < PAGE_BTN[i].first + PAGE_BTN[i].n)
            return (int)i;
    return 0;
}
static uint8_t app_lead(void);
/* the pages a button has now: EDIT with a melody selected one (Sound: its sound, channel, velocity) */
static int group_n(int g) { return PAGE_BTN[g].btn == B_EDIT && app_lead() ? 1 : PAGE_BTN[g].n; }
/* the tracks (on screen: instruments): a pattern's eight, then the song's two lead tracks (melodies M1, M2: ARR_LD1,
 * ARR_LD1 + 1) */
static const char *const TRACK_NAME[ARR_ALLTRK] = {"D1", "D2", "PC", "BA", "C1", "C2", "C3", "C4", "M1", "M2"};
/* SEC_* (SEC_FILL: the Fill asked for, AB or BA by the Main playing); Main A / B are Groove A / B on screen */
static const char *const SECT_NAME[SEC_FILL + 1] = {"", "Intro", "Groove A", "Groove B", "Fill AB", "Fill BA", "Ending", "Fill"};
static const char *const KEY_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const GRID_NAME[ARR_NQ] = {"8", "8T", "16", "16T", "32", "off"};
static const char *const TYPE_NAME[NXT] = {"Bypass", "Parallel", "Chord 1", "Chord 2", "Bass"};
static const char *const REV_NAME[5] = {"Off", "Large hall", "Hall", "Chamber", "Room"};
static const char *const REV_SHORT[5] = {"off", "L.hall", "Hall", "Chamb.", "Room"};
static const char *const LIGHTS_NAME[3] = {"off", "on", "notes"};
static const char *const CLICK_NAME[3] = {"off", "rec", "on"};   /* ARR_CLICK_* */
static const char *const MODE_NAME[3] = {"free", "progression", "song"};   /* ARR_* */
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-#";
#define NNAME_CHARS ((int)sizeof NAME_CHARS - 1)
/* a loop's name (mixed case: "Verse", as the factory's "Pop") */
static const char LOOP_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-#";
#define NLOOP_CHARS ((int)sizeof LOOP_CHARS - 1)
/* the step writing's step (app.cstep, the Progression list's STEP): a beat, half a bar, a bar */
static const char *const STEP_NAME[3] = {"beat", "half", "bar"};
/* NAME (LOOP page 2): the presets the knob turns through, then "Prog N" (its own number) */
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
    uint8_t cstep;                                   /* the keys' step writing (the list's STEP): 0 a beat, 1 half a bar, 2 a bar */
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
static uint8_t app_lead(void) { return app.lead; }

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
    char ask_q[48];                                  /* the question */
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
    uint8_t rec_wrote;                               /* a chord written since REC (HOME's "chords land ..." goes) */
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
    uint32_t sig[6];                                 /* the bands' hashes: header, main, knobs, tag, footer,
                                                      * the encoders' line */
    uint8_t row_follow;                              /* SONG's cursor follows the row playing (from PLAY until ROW turns) */
    uint8_t song_rec;                                /* REC on SONG: the rows written from HOME + keys, the mutes */
    uint32_t ksound;                                 /* stopped: the chord keys sounding on C1's channel (bit = key) */
    uint8_t follow_told, follow_hint;                /* REC kept a track with notes on its chord type: told (bit t), the
                                                      * footer's hint showing (ui.c rec_as_played) */
    uint8_t ksound_ch[NKEYS];                        /* ... the channel each one went to */
    uint8_t ksound_note[NKEYS];                      /* ... and its note (transposed) */
    uint8_t click_sent;                              /* the click's mode and the app's REC as the engine has them (+1) */
    uint32_t count_sent;                             /* ... a free take's count-in (arr_count_in) */
    uint32_t slots_until;                            /* a NO FREE SLOT said: until then SEL opens SLOTS (the footer says) */
    uint8_t pcur;                                    /* Band's cursor: a beat of the groove, 0.. (BAR / BEAT; the place
                                                      * shown, note editing to come) */
} ui;
/* the edit lights up where it lands (docs/superpowers/specs/2026-10-11-visibility-design.md): flash_watch(), each frame
 * before the drawing, watches what the page shows (cheap hashes) and what changed since the frame before flashes coral
 * FLASH_MS: a progression's beats, the song's sections, a groove's instruments and the selected one's bars, a melody's
 * sections and its section's bars. Knob, key, REC, an action, undo / redo alike. Another page, song, progression,
 * section or instrument: watched afresh, nothing flashes */
#define FLASH_MS 500u
static struct {
    uint32_t id, key;                                /* what is watched (the page's kind, song, progression ...), the
                                                      * inputs it was hashed from (the same: nothing to hash again) */
    uint8_t ok, n;                                   /* hashed; the sections then */
    uint16_t ch[ARR_LOOP_BEATS];                     /* a progression's beats */
    uint32_t h[ARR_ROWS + 1];                        /* the song's sections; a groove's instruments; a melody's sections */
    uint32_t bh[32];                                 /* the bars: the selected instrument's, a melody's section's */
    uint32_t t;                                      /* the flash: since when (0 none) */
    uint64_t beats;                                  /* what flashes: a progression's beats (bit b beat b), */
    uint32_t rows, bars;                             /* sections or instruments, bars */
} fw;
static int flash_on(void) { return fw.t && plat_ms() - fw.t < FLASH_MS; }
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
       A_CLEAR_CHORD, A_STEP, NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {"LOAD PROGRESSION", "CLEAR PROGRESSION", "PLAY FROM SECTION",
                                                  "INSERT SECTION", "DELETE SECTION", "CLEAR SONG", "COPY SONG TO",
                                                  "CLEAR C", "CLEAR", "BACK TO STYLE", "CLEAR M IN SECTION",
                                                  "CLEAR M", "QUANTISE M", "COPY M TO SECTION", "BARS AS THE PROG",
                                                  "MEMORY", "CLEAR CHORD", "STEP"};
#define LIST_ROWS 24
#define LIST_SHOWN 11                                /* rows on screen (the list covers the knobs) */
static struct {
    uint8_t open, n, sel, top;
    uint8_t slots;                                   /* the SLOTS list (SEL's SLOTS: the 16 shared slots) */
    uint8_t back_sel, back_top;                      /* the list it came from: its row (HOME goes back there) */
    struct { const char *name; char val[24]; uint8_t kind, id; } row[LIST_ROWS];
} lst;
/* the 16 shared slots as the MEMORY list shows them ("Pop|Groove A (yours)|S1", "S2 melodies 1/2", "S1 progressions",
 * "free"), made when
 * it opens and after a FREE (ui.c slots_scan: it reads the flash); kind SLOT_* (SLOT_NONE: free) */
static char slot_txt[ARR_USER_PATTERNS][40];
static uint8_t slot_kind[ARR_USER_PATTERNS];

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
    char b[48];
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
/* a section's colour: Groove A slate, Groove B amber; the extras (Intro, Fills, Ending) none (0: a neutral outlined pill) */
static uint16_t sect_colour(int s)
{
    return s == SEC_A ? K_SLATE : s == SEC_B ? K_AMBER : 0;
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
/* an outlined pill (a neutral one: an extra, an instrument): a 1 px ring of c, the background inside */
static int32_t pill_o(int32_t x, int32_t y, int32_t w, const char *s, uint16_t c, uint16_t fg)
{
    if (!w)
        w = text_w(&FONT_S, s) + 16;
    cv_round(x, y, w, 20, 6, c);
    cv_round(x + 1, y + 1, w - 2, 18, 5, K_BG);
    text_c(x + w / 2, y + 2, &FONT_S, s, fg);
    return x + w;
}
/* section s's pill (Groove A, Groove B in their colours; the extras outlined), text t (0: its name), h 20 or less (a
 * small one: FONT_XS) */
static int32_t sect_pill(int32_t x, int32_t y, int32_t w, int32_t h, int s, const char *t)
{
    const felucca_font_t *f = h >= 20 ? &FONT_S : &FONT_XS;
    uint16_t c = sect_colour(s);
    if (!t)
        t = SECT_NAME[s >= SEC_INTRO && s <= SEC_FILL ? s : 0];
    if (!w)
        w = text_w(f, t) + (h >= 20 ? 16 : 10);
    if (c)
        cv_round(x, y, w, h, h >= 20 ? 6 : 5, c);
    else {
        cv_round(x, y, w, h, h >= 20 ? 6 : 5, K_DIM);
        cv_round(x + 1, y + 1, w - 2, h - 2, h >= 20 ? 5 : 4, K_BG);
    }
    text_c(x + w / 2, y + (h >= 20 ? 2 : (h - 17) / 2), f, t, c ? K_BG : K_TEXT);
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
    return (t->chan & 15u) == 9u ? "Drum kit" : GM_PROGRAM[t->program & 127u];
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
#define MAIN_H 120
#define TAG_Y 144                                    /* whose the knobs are, the page dots */
#define TAG_H 12
#define KNB_Y 156
#define KNB_H 56
#define ENC_Y 212                                    /* what SELECT, ALGORITHM, PRESETS do here */
#define ENC_H 12
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
static int32_t segs(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c);
static int32_t segs_w(const felucca_font_t *f, const char *s);
/* the header: the page's name (on HOME, and HOME held, the song: "Home S1"). Right: the song's tempo (or "ext"; not on
 * HOME, whose KNOB 2 says it), REC's dot, "unsaved". The style, a progression's name, the sections: the knobs' and the
 * tag's to say, not the header's. A message replaces it all, a question (SEL: yes, HOME: no) the message */
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
    h = hash(h, (uint32_t)proj.song | (uint32_t)slots << 24 | (uint32_t)nfree << 25);
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
        else if (text_w(&FONT_S, ui.ask_q) <= 228)   /* a long one: smaller, whole */
            cv_text(8, 5, &FONT_S, ui.ask_q, K_BG);
        else                                         /* ("SECTION 3 HAS MELODY NOTES: DELETE?") */
            text_fit(6, 5, &FONT_XS, ui.ask_q, 230, K_BG);
    } else if (msg) {
        int32_t x = cv_text(8, 4, &FONT_B, ui.msg[0], K_TEXT);
        text_fit(x + 8, 4, &FONT_B, ui.msg[1], 232 - (x + 8), K_ACCENT);
    } else if (slots) {                              /* MEMORY: "Memory   3 of 16 free" */
        int32_t x = cv_text(8, 4, &FONT_B, "Memory", K_TEXT);
        if (nfree)
            cat_s(itoa_u((uint32_t)nfree, t), " of 16 free");
        else
            cat_s(t, "none free");
        cv_text(x + 10, 5, &FONT_S, t, nfree ? K_DIM : K_ACCENT);
    } else {
        int32_t r = 232;
        if (sel) {                                   /* HOME held: what it does with the keys, the song's */
            itoa_u(proj.song + 1u, cat_s(t, "HOME + a key|S"));
            segs(8, 4, &FONT_B, t, K_TEXT);
        } else {
            int32_t x = cv_text(8, 4, &FONT_B, PAGE_NAME[ui.page], K_TEXT);
            if (ui.page == P_HOME) {                 /* "Home S1" (the tag starts there too) */
                itoa_u(proj.song + 1u, cat_s(t, " S"));
                cv_text(x, 4, &FONT_B, t, K_TEXT);
            }
        }
        if (ui.page != P_HOME && !sel) {             /* the tempo (HOME: its KNOB 2) */
            if (ext)
                cat_s(t, "ext");
            else
                itoa_u(csong()->tempo, t);
            r -= text_w(&FONT_S, t);
            cv_text(r, 4, &FONT_S, t, K_TEXT);
            r -= 6;
        }
        if (rec) {
            r -= 10;
            cv_round(r, 8, 9, 9, 4, K_REC);
            r -= 6;
        }
        if (un)
            cv_text(r - text_w(&FONT_XS, "unsaved"), 5, &FONT_XS, "unsaved", K_DIM);
    }
    cv_blit(0, 0);
}

/* "P3 Verse": a loop's (a progression's) number and name (trailing spaces cut); "P9" alone while it has its own default
 * name ("Prog 9", as loop_clear names it; "Loop 9" in songs saved before: loops_load renames those): not "P9 Prog 9" */
static int loop_name_is(const loop_t *l, int i, const char *w)
{
    char d[8] = {w[0], w[1], w[2], w[3], ' ', 0, 0, 0};
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
static int loop_name_default(const loop_t *l, int i) { return loop_name_is(l, i, "Prog"); }
static char *loop_label(int i, char *t)
{
    const loop_t *l = &sloops.loop[i % ARR_LOOPS];
    char *e = itoa_u((uint32_t)(i % ARR_LOOPS) + 1u, cat_s(t, "P"));
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
/* the loop's name, as one of LOOP_NAME_LIST (0..NLOOP_NAMES-1), else NLOOP_NAMES ("Prog N", or a custom name) */
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
/* the loop's name set to preset idx (0..NLOOP_NAMES-1), or NLOOP_NAMES: "Prog N" (its own number, as loop_clear) */
static void loop_set_name_preset(loop_t *l, int li, int idx)
{
    char n[8] = {0};
    const char *s = n;
    if (idx < NLOOP_NAMES)
        s = LOOP_NAME_LIST[idx];
    else if (li >= 9) {
        n[0] = 'P'; n[1] = 'r'; n[2] = 'o'; n[3] = 'g'; n[4] = ' ';
        n[5] = '1';
        n[6] = (char)('0' + (li + 1) % 10);
    } else {
        n[0] = 'P'; n[1] = 'r'; n[2] = 'o'; n[3] = 'g'; n[4] = ' ';
        n[5] = (char)('1' + li);
    }
    for (int i = 0; i < 8; i++)
        l->name[i] = 0;
    for (int i = 0; i < 8 && s[i]; i++)
        l->name[i] = s[i];
}

/* ---------------------------------------------------------- HOME page --- */
/* HOME (the spec's Screens) shows the progression as bars, the same recording or listening (home_bars): a cell a bar,
 * four to a row, the bar's number and its chord (big), or its chords (small: "Am  F"), a dot for none; the bar
 * playing filled teal, its beats marked along its foot; stopped, bar 1 (where PLAY starts) ringed teal; a chord just
 * written (or put back by undo) fills its bar coral a moment. Listening (draw_home): the section (and the one queued)
 * and where it is; the chord sounding, big (coral: one from the keys, holding until the source's next change; teal:
 * the loop's or the song's) and the next; a line (LOOP: the progression's name, "starts after the Intro" while an
 * Intro holds; SONG: what comes next, the melodies M1 M2 at its right); the bars under it, two rows (scrolled to the
 * bar playing). FREE: the chord alone, bigger ("Play a chord: 3 keys" until one is heard), and a line: no
 * progression, the band follows your chords */
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
#define RG_X 8
#define RG_CW 53                                     /* a bar's cell: four to a row, 4 px apart */
#define HB_Y 24                                      /* the bars' top: under the first line (a progression playing) */
#define HB_Y2 40                                     /* ... under two (a song: what comes next; recording: where) */
/* the bars as HOME draws them: loop l's (len beats, bpb a bar), n cells (its shown bars, then bars to come,
 * outlined: from out_from), rows of ch px from y0, vis of the rows rows from top; focus the bar playing (cursor:
 * stepping, the cursor's bar ringed coral instead) and fbeat its beat, -1 none; start the bar PLAY starts on
 * (stopped: an ink ring), -1 none; fbar the bar flashing, -1 none; cnt > 0: the count-in, big, in bar 1 */
typedef struct {
    const loop_t *l;
    int32_t y0;
    int len, bpb, n, shown, out_from, rows, vis, top, ch, focus, fbeat, cursor, start, fbar, cnt, xp;
} bars_t;
static uint32_t bars_hash(uint32_t h, const bars_t *v)
{
    h = hash(h, (uint32_t)(v->focus + 1) | (uint32_t)v->fbeat << 8 | (uint32_t)v->shown << 12 | (uint32_t)v->cnt << 20 |
                    (uint32_t)(v->fbar + 1) << 24);
    h = hash(h, (uint32_t)v->bpb | (uint32_t)v->len << 4 | (uint32_t)v->top << 12 | (uint32_t)(v->start + 1) << 20 |
                    (uint32_t)v->cursor << 28 | (uint32_t)v->vis << 29);
    h = hash(h, (uint32_t)(v->xp & 31) | (uint32_t)v->out_from << 8 | (uint32_t)v->ch << 16 | (uint32_t)v->n << 24);
    h = hash(h, (uint32_t)v->y0 | (uint32_t)v->rows << 8);
    for (int i = 0; i < v->len && i < ARR_LOOP_BEATS; i++)
        h = hash(h, v->l->ch[i]);
    return h;
}
/* the rows of bars that fit from y0 to the band's foot: two rows of four (eight bars) without scrolling, a progression
 * of four bars or fewer one row, as tall as fits (to 64 px); the window over the bar in focus (and the next) */
static void bars_fit(bars_t *v, int ahead)
{
    int32_t room = MAIN_H - v->y0;
    if (v->n < 1)
        v->n = 1;
    v->rows = (v->n + 3) / 4;
    v->vis = v->rows < 2 ? v->rows : 2;
    v->ch = v->vis == 1 ? room : (room - 2) / 2;
    if (v->ch > 64)
        v->ch = 64;
    v->top = v->focus >= 0 ? (v->focus + ahead) / 4 - (ahead ? 1 : 0) : 0;
    if (v->top > v->rows - v->vis)
        v->top = v->rows - v->vis;
    if (v->top < 0)
        v->top = 0;
}
static void home_bars(const bars_t *v)
{
    const loop_t *l = v->l;
    int bpb = v->bpb, len = v->len, ch = v->ch;
    char t[64], *e;                                  /* (four slash chords in a bar: 51) */
    for (int b = v->top * 4; b < (v->top + v->vis) * 4 && b < v->n; b++) {
        int32_t x = RG_X + (b % 4) * (RG_CW + 4), y = v->y0 + (b / 4 - v->top) * (ch + 2), nx;
        int plays = b == v->focus && !v->cursor, cur = b == v->focus && v->cursor, out = b >= v->out_from;
        int flash = b == v->fbar, st = b == v->start && !flash, nch = 0, k, snd = -1;
        uint16_t fill = flash ? K_ACCENT : plays ? K_TEAL : out ? K_BG : K_PANEL;
        uint16_t fg = flash || plays ? K_BG : K_TEXT;
        char nm[4][12];
        if (cur) {                                   /* the cursor: a coral ring */
            cv_round(x, y, RG_CW, ch, 5, K_ACCENT);
            cv_round(x + 3, y + 3, RG_CW - 6, ch - 6, 3, fill);   /* (3 px: seen at arm's length) */
        } else if (st) {                             /* where PLAY starts: an ink ring (no colour: nothing sounds yet) */
            cv_round(x, y, RG_CW, ch, 5, K_TEXT);
            cv_round(x + 2, y + 2, RG_CW - 4, ch - 4, 4, fill);
        } else if (out && !flash) {                  /* a bar to come: its outline */
            cv_round(x, y, RG_CW, ch, 5, K_LINE);
            cv_round(x + 1, y + 1, RG_CW - 2, ch - 2, 4, fill);
        } else
            cv_round(x, y, RG_CW, ch, 5, fill);
        itoa_u((uint32_t)b + 1u, t);
        nx = cv_text(x + 4, ch >= 33 ? y : y + 4, &FONT_XS, t, flash || plays ? K_BG : out ? K_LINE : K_DIM) + 4;
        for (k = 0; k < bpb; k++) {                  /* its chords (and which sounds now: the last at or before the beat) */
            int idx = b * bpb + k;
            uint16_t c = idx < len && idx < ARR_LOOP_BEATS ? l->ch[idx] : LC_SAME;
            if (c != LC_SAME && nch < 4) {
                if (plays && k <= v->fbeat)
                    snd = nch;
                chord_name(chord_shift(lc_unpack(c), v->xp), nm[nch++]);
            }
        }
        if (v->cnt > 0 && b == 0) {                  /* the count-in, big */
            itoa_u((uint32_t)v->cnt, t);
            text_c(x + RG_CW / 2, y + (ch - 32) / 2, &FONT_L, t, K_ACCENT);
        } else if (nch == 1) {                       /* one chord: as big as fits */
            int32_t tx = ch >= 33 ? x + 4 : nx, tw = RG_CW - (tx - x) - 3;
            if (ch >= 44 && text_w(&FONT_L, nm[0]) <= RG_CW - 6)
                text_c(x + RG_CW / 2, y + (ch - 32) / 2 + 3, &FONT_L, nm[0], fg);
            else if (text_w(&FONT_B, nm[0]) <= tw)
                cv_text(tx, ch >= 33 ? y + ch / 2 - 4 : y + 4, &FONT_B, nm[0], fg);
            else
                text_fit(tx, ch >= 33 ? y + ch / 2 - 3 : y + 4, &FONT_XS, nm[0], tw, fg);
        } else if (nch > 1) {                        /* more: small, in order ("Am  F"); playing, the one sounding
                                                      * bright, the others pale */
            const felucca_font_t *f = &FONT_B;
            int32_t tx = ch >= 33 ? x + 4 : nx, ty, room = RG_CW - (tx - x) - 3;
            e = t;
            for (k = 0; k < nch; k++)
                e = cat_s(k ? cat_s(e, "  ") : e, nm[k]);
            if (ch < 33 || text_w(f, t) > room)
                f = &FONT_XS;
            ty = ch >= 33 ? y + ch / 2 - (f == &FONT_B ? 4 : 3) : y + 4;
            if (text_w(f, t) > room)                 /* (too long even small: cut) */
                text_fit(tx, ty, f, t, room, fg);
            else
                for (k = 0; k < nch; k++)
                    tx = cv_text(tx + (k ? text_w(f, "  ") : 0), ty, f, nm[k],
                                 plays && k != snd ? K_TEAL_T : fg);
        } else if (!out)                             /* none: a dot */
            cv_round(ch >= 33 ? x + RG_CW / 2 - 2 : (nx + x + RG_CW) / 2 - 2, y + ch / 2 - 2 + (ch >= 33 ? 4 : 0), 4, 4, 1,
                     flash || plays ? K_BG : K_DIM);
        if (b == v->focus) {                         /* its beats along its foot: the one now (and those before) lit */
            int32_t sw = (RG_CW - 10) / bpb;
            for (k = 0; k < bpb; k++)
                cv_rect(x + 5 + k * sw, y + ch - 4, sw - 2, 2,
                        v->cursor ? (k == v->fbeat ? K_ACCENT : K_LINE) : k <= v->fbeat ? K_BG : K_TEAL_T);
        }
    }
    if (v->rows > v->vis) {                          /* the scroll bar: where the window is */
        int32_t th = v->vis * (ch + 2) - 2, bh = th * v->vis / v->rows, by = v->y0 + th * v->top / v->rows;
        cv_round(236, v->y0, 3, th, 1, K_PANEL);
        cv_round(236, by, 3, bh, 1, K_DIM);
    }
}
/* the bar flashing in loop li (a chord just written; another change, an undo's: its first bar), -1 none */
static int wrote_flash(int li)
{
    return ui.wrote_t && li == ui.wrote_loop && plat_ms() - ui.wrote_t < FLASH_MS;
}
static int flash_bar(int li, int bpb)
{
    if (wrote_flash(li))
        return ui.wrote_beat / bpb;
    if (flash_on() && fw.id == (1u | (uint32_t)proj.song << 4 | (uint32_t)li << 8) && fw.beats)
        return __builtin_ctzll(fw.beats) / bpb;
    return -1;
}
/* HOME's lit tracks: bit i, hit just now (the melodies' M1 M2: bits 8, 9) */
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
/* HOME in SONG: M1 and M2 (the melodies, each with notes in the song) at the right of line y: plum, a note sounding
 * now teal; muted (the section's, or by hand) an outline, struck through. The x they start at (232: none) */
static int32_t home_leads(uint32_t lit, int32_t y, int draw)
{
    int32_t x0 = 232;
    for (int k = 0; k < ARR_LEADS; k++) {
        int32_t x = 150 + k * 42;
        int t = ARR_LD1 + k, on = (int)(lit >> t & 1u), mute = trk_muted(t);
        if (!leads_count(&leads, -1, k))
            continue;
        if (x < x0)
            x0 = x;
        if (!draw)
            continue;
        if (mute) {
            cv_round(x, y, 40, 10, 3, K_LINE);
            cv_round(x + 1, y + 1, 38, 8, 2, K_BG);
            text_c(x + 20, y - 2, &FONT_XS, TRACK_NAME[t], K_DIM);
            cv_rect(x + 8, y + 5, 24, 1, K_DIM);
        } else {
            cv_round(x, y, 40, 10, 3, on ? K_TEAL : K_PLUM);
            text_c(x + 20, y - 2, &FONT_XS, TRACK_NAME[t], K_BG);
        }
    }
    return x0;
}
/* HOME while chords are recorded (a new loop's take; REC in LOOP, playing or stepping): laid out as a song listening,
 * the same bars. A line "REC P9 . bar 3.2" (the take's bar and beat, from its start; REC in LOOP: the loop's); under it
 * where a chord lands (until the first is written); the bars (two rows, scrolled to the bar playing): a take's next bar
 * outlined, empty (the take growing); stepping (stopped): the cursor's bar in a coral ring. Waiting for the take's
 * first bar: "starts at the next bar" and the beats left, big, in bar 1's outline */
static void draw_home_rec(void)
{
    place_t pl;
    const loop_t *l;
    int take = ui.take, li, ghost = 0, wait = 0, hint;
    uint32_t h;
    char t[48], *e;                                  /* ("REC P16 <name>|starts at the next bar": 40) */
    bars_t v;
    memset(&v, 0, sizeof v);
    v.focus = v.start = -1;
    home_place(&pl);
    li = take ? ui.take_loop % ARR_LOOPS : arr_playing && pl.loop >= 0 ? pl.loop : (int)(app.loop % ARR_LOOPS);
    l = &sloops.loop[li];
    v.l = l;
    v.bpb = l->beats ? l->beats : 4;
    v.len = loop_len(l);
    v.xp = xpose_now();
    v.shown = l->bars;
    if (take == 1)
        wait = 1;                                    /* from PLAY */
    else if (take == 2) {
        int32_t tb = (int32_t)(arr_beats - ui.take_b0);
        if (tb < 0) {
            wait = 2;                                /* the count-in: the beats to the take's downbeat */
            v.cnt = (int)-tb;
        } else {
            v.focus = (int)tb / v.bpb;
            v.fbeat = (int)tb % v.bpb;
            ghost = 1;
        }
    } else if (arr_playing) {                        /* REC in LOOP, in time: the loop's bar playing */
        if (arr_sect == SEC_INTRO)
            wait = 3;
        else if (ui.mode_wait || pl.loop != li)
            wait = 4;
        else {
            v.focus = (int)(pl.lbeat / (uint32_t)v.bpb);
            v.fbeat = (int)(pl.lbeat % (uint32_t)v.bpb);
        }
    } else {                                         /* stepping: the cursor's bar (at the end: the bar to come) */
        v.cursor = 1;
        v.focus = app.cbeat / v.bpb;
        v.fbeat = app.cbeat % v.bpb;
    }
    if (wait >= 1 && wait <= 2)
        ghost = 1;                                   /* bar 1, to come */
    if (v.focus + 1 > v.shown)
        v.shown = v.focus + 1;
    v.out_from = v.cursor ? l->bars : v.shown;
    v.n = v.shown + ghost;
    v.y0 = HB_Y2;
    bars_fit(&v, ghost);
    v.fbar = flash_bar(li, v.bpb);
    hint = !ui.rec_wrote;                            /* where chords land: until the first is written */
    h = hash(2166136261u, 0x5EC0u | (uint32_t)li << 16 | (uint32_t)take << 24 | (uint32_t)wait << 26 | (uint32_t)hint << 29);
    h = bars_hash(h, &v);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    /* the line: REC's dot, "REC P9 . bar 3.2" */
    cv_round(8, 6, 10, 10, 5, K_REC);
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
        e = itoa_u((uint32_t)v.focus + 1u, cat_s(t + slen(t), "|bar "));
        *e++ = '.';
        itoa_u((uint32_t)v.fbeat + 1u, e);
    }
    segs(24, 2, &FONT_B, t, K_TEXT);
    if (hint)                                        /* where a chord lands */
        cv_text(8, 22, &FONT_XS, v.cursor ? "chords land at the cursor" : "chords land on the nearest beat", K_DIM);
    home_bars(&v);
    cv_blit(0, MAIN_Y);
}
/* SONG: what comes after the section playing (stopped: after where PLAY starts), into t: "next: P4 Chorus
 * (Am)|Groove B" (the section's progression, its first chord as it will sound, its groove), "next: Fill|P4 Chorus
 * (Am)|Groove B" (a fill in this section's last bar), "next: P3 Verse again", "next: Ending", "next: stop". brief 1:
 * the progression's number alone ("P4 (Am)"); 2: no groove either */
static void song_next(char *t, int play, int brief)
{
    const song_t *g = csong();
    int n = g->nrows, r = play ? arr_row % ARR_ROWS : 0, rp = play ? arr_rep : 0, nr = -1, fill = 0, again = 0;
    int ns = arr_next_sect, sect = arr_sect;         /* (stopped: the section PLAY starts with, the Intro too) */
    const char *what = 0;
    char *e = cat_s(t, "next: ");
    if (!n)
        what = "stop";
    else if (sect == SEC_END)
        what = "stop";
    else if (ns == SEC_END)
        what = "Ending";
    else if (sect == SEC_INTRO)
        nr = 0;
    else if (rp + 1 < (g->row[r].rep ? g->row[r].rep : 1)) {
        nr = r;
        again = 1;
    } else if (r + 1 < n)
        nr = r + 1;
    else if (g->ending)
        what = "Ending";
    else if (g->at_end)
        nr = 0;
    else
        what = "stop";
    if (nr >= 0 && !again && sect != SEC_INTRO)       /* a fill in the last bar: the row asks, or the groove changes */
        fill = row_fill(&g->row[r]) || row_main(&g->row[nr]) != row_main(&g->row[r]);
    if (ns == SEC_FILL_AB || ns == SEC_FILL_BA || ns == SEC_FILL)
        fill = 1;                                    /* (one asked for by hand) */
    if (sect == SEC_FILL_AB || sect == SEC_FILL_BA)
        fill = 0;                                    /* (playing now) */
    if (what) {
        cat_s(e, what);
        return;
    }
    if (fill)
        e = cat_s(e, "Fill|");
    if (brief)
        e = itoa_u((uint32_t)g->row[nr].loop % ARR_LOOPS + 1u, cat_s(e, "P"));
    else
        e = loop_label(g->row[nr].loop % ARR_LOOPS, e);
    if (again) {
        cat_s(e, " again");
        return;
    }
    {                                                /* its first chord, as it will sound: " (Am)" */
        chord_t c = loop_chord_at(&sloops.loop[g->row[nr].loop % ARR_LOOPS], 0);
        if (c.type != CH_NONE) {
            e = cat_s(e, " (");
            chord_name(chord_shift(c, g->transpose + g->row[nr].key), e);
            e = cat_s(e + slen(e), ")");
        }
    }
    if (brief < 2)
        cat_s(cat_s(e, "|"), SECT_NAME[row_main(&g->row[nr])]);
}
#define HOME_FREE_H 58                               /* FREE: the chord's box, taller */
/* the section playing's pill, "(yours)" on it when it plays your copy: "Groove A (yours)" */
static int32_t home_pill(int32_t x, int32_t y, int sect, uint16_t ring)
{
    char t[24];
    int s = sect >= SEC_INTRO && sect <= SEC_END ? sect : SEC_A;
    cat_s(t, SECT_NAME[s]);
    if (csong()->style[s - 1] < ARR_USER_PATTERNS)
        cat_s(t + slen(t), " (yours)");
    if (ring) {
        int32_t w = text_w(&FONT_S, t) + 16;
        cv_round(x - 2, y - 2, w + 4, 24, 7, ring);
        cv_round(x, y, w, 20, 6, K_BG);
    }
    return sect_pill(x, y, 0, 20, s, t);
}
static void draw_home(void)
{
    uint32_t h, lit, bt, slot = (uint32_t)sect_slot(arr_sect);
    int sect = arr_sect, next = arr_next_sect, play = arr_playing, blink = (plat_ms() / 300u) & 1u, ovr = arr_override;
    int mode = arr_mode, intro, held, fresh, alone, yours = 0;
    place_t pl;
    char t[48], line[48];                            /* (the longest line: 38 + 1) */
    const loop_t *l;
    const song_t *g = csong();
    bars_t v;
    if (home_place(&pl))
        ovr = 0;                                     /* (PLAY's chord: the source's) */
    if (ui.take || ui.rec_chords) {                  /* chords recorded: the loop as bars, the REC line over them */
        draw_home_rec();
        return;
    }
    l = pl.loop >= 0 ? &sloops.loop[pl.loop] : 0;
    bt = l && l->beats ? l->beats : 4u;
    intro = l && play && sect == SEC_INTRO;          /* an Intro holds its chord: the loop starts after it */
    held = intro || (l && play && sect == SEC_END);  /* (an Ending holds the last: no bar playing) */
    fresh = mode == ARR_FREE && !app.played;         /* nothing heard yet: "Play a chord" */
    alone = !l;                                      /* FREE (a song with no sections): the chord alone, bigger */
    lit = home_lit();
    for (int k = 0; k < ARR_NSECT; k++)
        yours |= (g->style[k] < ARR_USER_PATTERNS) << k;
    memset(&v, 0, sizeof v);
    v.focus = v.start = v.fbar = -1;
    line[0] = 0;                                     /* the second line */
    if (mode == ARR_SONG && g->nrows) {              /* what comes next, as long as fits by M1 M2 */
        int32_t room = (leads.n ? home_leads(lit, 25, 0) : 232) - 8 - 8;
        for (int b = 0; b < 3; b++) {
            song_next(line, play, b);
            if (segs_w(&FONT_XS, line) <= room)
                break;
        }
    } else if (mode == ARR_SONG)
        cat_s(line, "no sections yet|SELECT: the Song page");
    else if (intro)
        cat_s(line, "the progression starts after the Intro");
    else if (alone)
        cat_s(line, "the band follows your chords");
    if (l) {                                         /* the bars: under the line(s), two rows, the one playing on top */
        v.l = l;
        v.y0 = *line ? HB_Y2 : HB_Y;
        v.bpb = (int)bt;
        v.len = loop_len(l);
        v.xp = xpose_now();
        v.shown = v.n = v.out_from = l->bars;
        if (play && !held && v.len) {
            v.focus = (int)(pl.lbeat % (uint32_t)v.len / bt);
            v.fbeat = (int)(pl.lbeat % (uint32_t)v.len % bt);
        } else if (v.n && !(play && sect == SEC_END))
            v.start = 0;                             /* stopped (an Intro playing): where PLAY starts */
        bars_fit(&v, 0);
        v.fbar = flash_bar(pl.loop, v.bpb);
    }
    h = hash(2166136261u, alone || ovr ? pl.chord : 0u);
    h = hash(h, (uint32_t)sect | (uint32_t)next << 4 | (uint32_t)play << 8 | (uint32_t)(next ? blink : 0) << 9 |
                    (lit >> 8 & 3u) << 10 | (uint32_t)ovr << 21 | (uint32_t)mode << 22 |
                    (uint32_t)fresh << 24 | (uint32_t)intro << 25 | (uint32_t)held << 26 | (uint32_t)yours << 27);
    if (alone && play)
        h = hash(h, pl.bar << 8 | pl.beat | (uint32_t)app.hdr[slot].bars << 16);
    h = hash(h, (uint32_t)arr_row | (uint32_t)arr_rep << 8 | (uint32_t)g->nrows << 16);
    h = hash(h, (uint32_t)(leads.n != 0) | (uint32_t)trk_muted(ARR_LD1) << 1 | (uint32_t)trk_muted(ARR_LD1 + 1) << 2 |
                    (uint32_t)leads.gen << 3);
    for (const char *c = line; *c; c++)
        h = hash(h, (uint8_t)*c);
    if (l)
        h = bars_hash(h, &v);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    {   /* the first line: the section (yours: said on it), the one waiting (NEXT); at the right, a song's place
         * ("section 2 of 4 . repeat 1 of 2"), FREE's groove bar playing ("groove bar 1.1 / 2"), or the chord you play
         * over a progression (coral: yours, until the progression's next change) */
        int32_t x = home_pill(8, 1, (int)slot + 1, 0), w;
        if (next >= SEC_INTRO && next <= SEC_FILL) {
            cv_text(x + 8, 4, &FONT_XS, "NEXT", blink ? K_ACCENT : K_DIM);
            x = sect_pill(x + 8 + text_w(&FONT_XS, "NEXT") + 6, 1, 0, 20, next, 0);
        }
        *t = 0;
        if (ovr && !alone) {                         /* your chord over the progression */
            chord_name(arr_chord_unpack(pl.chord), t);
            w = text_w(&FONT_B, t) + 16;
            if (232 - w >= x + 8) {
                cv_round(232 - w, 1, w, 20, 6, K_ACCENT);
                text_c(232 - w / 2, 2, &FONT_B, t, K_BG);
            }
            *t = 0;
        } else if (mode == ARR_SONG && g->nrows) {   /* (stopped: section 1, where PLAY starts) */
            uint32_t r = play ? arr_row % ARR_ROWS : 0u, rp = play ? arr_rep : 0u, rep = g->row[r].rep ? g->row[r].rep : 1u;
            char *e = itoa_u(r + 1u, cat_s(t, "section "));
            e = itoa_u(g->nrows, cat_s(e, " of "));
            if (rep > 1)
                itoa_u(rep, cat_s(itoa_u(rp + 1u, cat_s(e, "|repeat ")), " of "));
        } else if (alone && play && (slot == SEC_A - 1 || slot == SEC_B - 1)) {   /* FREE: the groove's bar */
            char *e = itoa_u(pl.bar + 1u, cat_s(t, "groove bar "));
            *e++ = '.';
            e = itoa_u(pl.beat + 1u, e);
            itoa_u(app.hdr[slot].bars, cat_s(e, " / "));
        }
        if (*t && 232 - (w = segs_w(&FONT_XS, t)) >= x + 8)
            segs(232 - w, 4, &FONT_XS, t, play ? K_TEXT : K_DIM);
    }
    if (alone) {                                     /* FREE: the chord, big, in a box across; the line under it */
        cv_round(8, 24, 224, HOME_FREE_H, 6, K_PANEL);
        if (fresh)
            text_c(120, 24 + HOME_FREE_H / 2 - 9, &FONT_B, "Play a chord: 3 keys", K_DIM);
        else {
            chord_name(arr_chord_unpack(pl.chord), t);   /* (teal: the band plays it now) */
            text_c(120, 24 + HOME_FREE_H / 2 - 17, &FONT_L, t, play ? K_TEAL : K_TEXT);
        }
        segs(120 - segs_w(&FONT_XS, line) / 2, 24 + HOME_FREE_H + 8, &FONT_XS, line, K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    if (*line)                                       /* what comes next (a song), the Intro's wait; the melodies */
        segs(8, 22, &FONT_XS, line, K_DIM);
    if (mode == ARR_SONG && g->nrows && leads.n)
        home_leads(lit, 25, 1);
    if (l && v.n)                                    /* the bars */
        home_bars(&v);
    else if (l) {
        loop_label(pl.loop, t);
        cat_s(t + slen(t), " is empty");
        cv_text(8, HB_Y + 4, &FONT_XS, t, K_DIM);
    }
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
    int bars = l->bars, rows, xp = xpose_now(), i, live, fbar, fl, shown;
    int n1 = wrap ? (bpb + 1) / 2 : bpb, cw = LG_W / n1;
    int32_t y0 = wrap ? 2 : 13, ch, gap = wrap ? 6 : 2, rh, visible, top, want;
    want = bars < 1 ? 1 : bars;                      /* (a bar to come past the end: scrolled to, not squeezed in) */
    ch = ((MAIN_H - y0 + gap) / want - gap - (wrap ? 2 : 0)) / (wrap ? 2 : 1);   /* a short one: taller cells */
    ch = ch < 18 ? 18 : ch > (wrap ? 24 : 28) ? (wrap ? 24 : 28) : ch;
    rh = (wrap ? 2 * ch + 2 : ch) + gap;
    visible = (MAIN_H - y0 + gap) / rh;
    uint64_t fm = flash_on() ? fw.beats : 0;
    place_t pl;
    uint32_t pbeat, h;
    char t[24];
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
    h = hash(h, (uint32_t)ch);
    h = hash(hash(hash(h, (uint32_t)(fbar + 1) | (uint32_t)live << 8), (uint32_t)fm), (uint32_t)(fm >> 32));
    for (i = 0; i < len && i < ARR_LOOP_BEATS; i++)
        h = hash(h, l->ch[i]);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    if (!wrap)                                       /* the beats' numbers (the cursor's: on coral) */
        for (int k = 0; k < bpb; k++) {
            int32_t cx = LG_X + k * cw + (cw - 3) / 2;
            itoa_u((uint32_t)k + 1u, t);
            if (!live && k == app.cbeat % bpb) {
                cv_round(cx - 8, 0, 16, 12, 4, K_ACCENT);
                text_c(cx, -2, &FONT_XS, t, K_BG);
            } else
                text_c(cx, -2, &FONT_XS, t, K_DIM);
        }
    for (i = top; i < rows && i < top + visible; i++) {
        int32_t y = y0 + (i - top) * rh;
        int ghost = i >= bars;                       /* the bar to come (the cursor at the loop's end) */
        itoa_u((uint32_t)i + 1u, t);
        if (i == cbar) {                             /* the cursor's bar (writing in time: the bar playing): its number on
                                                      * coral (teal) */
            int32_t w = text_w(&FONT_XS, t) + 6;
            cv_round(LG_X - 3 - w, y + (wrap ? ch - 5 : 3), w, 12, 4, live ? K_TEAL : K_ACCENT);
            cv_text(LG_X - w, y + (wrap ? ch - 7 : 1), &FONT_XS, t, K_BG);
        } else
            cv_text(LG_X - 5 - text_w(&FONT_XS, t), y + (wrap ? ch - 6 : 3), &FONT_XS, t, K_DIM);
        for (int k = 0; k < bpb; k++) {
            int idx = i * bpb + k, line = wrap && k >= n1, col = line ? k - n1 : k;
            int32_t x = LG_X + col * cw, yy = y + (line ? ch + 2 : 0), w = cw - 3;
            uint16_t v = idx < len && idx < ARR_LOOP_BEATS ? l->ch[idx] : LC_SAME;
            int cur = idx == app.cbeat && !live, plays = (uint32_t)idx == pbeat;
            int flash = idx == fbar || (idx < ARR_LOOP_BEATS && (fm >> idx & 1u));
            /* the playhead: teal; the cursor: a thick coral ring (round the playhead's teal when both); an edit: coral */
            uint16_t fill = flash ? K_ACCENT : plays ? K_TEAL : ghost ? K_BG : K_PANEL, fg = flash || plays ? K_BG : K_TEXT;
            if (cur) {
                cv_round(x, yy, w, ch, 4, K_ACCENT);
                cv_round(x + 3, yy + 3, w - 6, ch - 6, 2, fill);
            } else if (ghost && !flash && !plays) {  /* a cell to come: its outline */
                cv_round(x, yy, w, ch, 4, K_LINE);
                cv_round(x + 1, yy + 1, w - 2, ch - 2, 3, fill);
            } else
                cv_round(x, yy, w, ch, 4, fill);
            if (v != LC_SAME) {
                chord_name(chord_shift(lc_unpack(v), xp), t);
                if (ch >= 24 && text_w(&FONT_B, t) <= w - 9)
                    cv_text(x + 5, yy + (ch - 16) / 2, &FONT_B, t, fg);
                else
                    text_fit(x + 5, yy + (ch - 15) / 2, &FONT_XS, t, w - 9, fg);
            } else
                cv_round(x + w / 2 - 2, yy + ch / 2 - 2, 4, 4, 1, flash || plays ? K_BG : ghost ? K_LINE : K_DIM);
        }
    }
    shown = rows - top < visible ? rows - top : visible;
    (void)shown;                                     /* (the cursor's place: its bar's and beat's numbers lit, coral) */
    if (rows > visible) {                            /* the scroll bar: where the window is */
        int32_t th = MAIN_H - y0 - 4, bh = th * visible / rows, by = y0 + th * top / rows;
        cv_round(236, y0, 3, th, 1, K_PANEL);
        cv_round(236, by, 3, bh < 6 ? 6 : bh, 1, K_DIM);
    }
    if (!len)                                        /* an empty loop: what to do */
        text_c(LG_X + LG_W / 2, y0 + rh + 18, &FONT_XS, ui.rec_chords ? "play a chord" : "REC, then play chords", K_DIM);
    cv_blit(0, MAIN_Y);
}

/* ---------------------------------------------------------- SONG page --- */
/* the song's rows (on screen: its sections; the spec's Screens): a row a line, # / PROG / GROOVE / REP / MUTE / TRANSP;
 * the cursor row in a coral ring (white keys write there), the row playing filled teal with a mark; after the last row
 * the END row (taps there add rows), outlined as a row to come. GROOVE: its colour (Groove A slate, B amber) and letter.
 * MUTE: eight squares D1 D2 PC BA | C1 C2 C3 C4, then the melodies M1 M2 (plum) after a gap, filled = plays, outlined =
 * muted. TRANSP: the section's own ("+2"; 0 dim). The columns the page's knobs turn: their headings dark. Six rows
 * show, scrolled to the cursor (a scroll bar when more) */
#define SG_Y0 13                                     /* the first row (the headings over it) */
#define SG_RH 15                                     /* a row's pitch */
#define SG_SHOWN 6
#define SG_NUM 17                                    /* #'s right edge */
#define SG_LOOP 21                                   /* the columns: PROG's left */
#define SG_SECT 82                                   /* GROOVE's colour chip, its letter after */
#define SG_REP 144                                   /* REPEAT's right edge */
#define SG_MUTE 149                                  /* the mutes' first square (D1; M1, M2 after a gap: SG_LMUTE) */
#define SG_MW 3                                      /* a mute square's width, its pitch SG_MW + 1 */
#define SG_LMUTE (SG_MUTE + 8 * (SG_MW + 1) + 2 + 2)
#define SG_KEY 231                                   /* the transposition's right edge */
/* the row's section knob (on screen: its groove) */
static const char *const ROW_SECT_NAME[ROW_NSECT] = {"A", "A + fill", "B", "B + fill"};
static void glyph_play(int32_t x, int32_t y, uint16_t c)    /* the row playing: a small triangle */
{
    for (int j = 0; j < 4; j++)
        cv_rect(x + j, y + j, 1, 7 - 2 * j, c);
}
static void text_r(int32_t xr, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(xr - text_w(f, s), y, f, s, c);
}
/* the song's state while it plays, under the rows: "Intro . then section 1", "Fill AB . into section 3", "Ending . then
 * stop"; 0 when a row just plays */
static int song_state(char *t)
{
    const song_t *g = csong();
    int s = arr_sect, n = g->nrows, r = arr_row;
    *t = 0;
    if (!arr_playing || arr_mode != ARR_SONG || !n)
        return 0;
    if (s == SEC_INTRO)
        cat_s(t, "then section 1");
    else if (s == SEC_FILL_AB || s == SEC_FILL_BA) {
        int nx = r + 1 < n ? r + 1 : g->at_end ? 0 : -1;
        if (nx < 0)
            return 0;
        itoa_u((uint32_t)nx + 1u, cat_s(t, "into section "));
    } else if (s == SEC_END)
        cat_s(t, "then stop");
    else
        return 0;
    return s;
}
static void draw_song(void)
{
    const song_t *g = csong();
    int n = g->nrows > ARR_ROWS ? ARR_ROWS : g->nrows, total = n < ARR_ROWS ? n + 1 : n, cur = app.row, pg = ui.page - P_SONG;
    int play = arr_playing && arr_mode == ARR_SONG && arr_loop_now >= 0 && arr_row < n ? arr_row : -1, top, i, st;
    uint32_t h, fm = flash_on() ? fw.rows : 0u;
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
    h = hash(h, (uint32_t)ui.song_rec << 3 | (uint32_t)st << 4 | (uint32_t)arr_row << 8);
    for (i = top; i < n && i < top + SG_SHOWN; i++) {
        const row_t *w = &g->row[i];
        const loop_t *l = &sloops.loop[w->loop % ARR_LOOPS];
        h = hash(h, w->loop | (uint32_t)w->sect << 5 | (uint32_t)w->rep << 8 | (uint32_t)w->mute << 12 |
                        (uint32_t)(uint8_t)w->key << 20 | (uint32_t)(l->bars != 0) << 28 | (uint32_t)(w->lmute & 3u) << 29);
        for (int c = 0; c < 8; c++)
            h = hash(h, (uint8_t)l->name[c]);
    }
    h = hash(h, fm);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    /* the headings: the knobs' columns dark (page 1 PROG, GROOVE, REP; page 2 TRANSP) */
    text_r(SG_NUM, 0, &FONT_XS, "#", K_DIM);
    cv_text(SG_LOOP, 0, &FONT_XS, "PROG", pg == 0 ? K_TEXT : K_DIM);
    cv_text(SG_SECT, 0, &FONT_XS, "GROOVE", pg == 0 ? K_TEXT : K_DIM);
    text_r(SG_REP, 0, &FONT_XS, "REP", pg == 0 ? K_TEXT : K_DIM);
    cv_text(SG_MUTE, 0, &FONT_XS, "MUTE", K_DIM);
    text_r(SG_KEY, 0, &FONT_XS, "TRANSP", pg == 1 ? K_TEXT : K_DIM);
#if defined(GM_HOST) && !defined(GM_WEB)            /* the simulator: a column over another is a bug */
    if (SG_LMUTE + 2 * (SG_MW + 1) > SG_KEY - text_w(&FONT_XS, "TRANSP") || SG_SECT + text_w(&FONT_XS, "GROOVE") >
        SG_REP - text_w(&FONT_XS, "REP") || SG_REP > SG_MUTE) {
        printf("  FAIL song columns overlap\n");
        msg_too_wide++;
    }
#endif
    for (i = top; i < total && i < top + SG_SHOWN; i++) {
        int32_t y = SG_Y0 + (i - top) * SG_RH, ch = SG_RH - 2;
        int on = i == cur, ghost = i >= n, pl = i == play, fl = i < ARR_ROWS && (fm >> i & 1u);
        /* the section playing filled teal, an edit coral a moment; the cursor a thick coral ring round it (into the
         * gaps between the rows) */
        uint16_t fill = fl ? K_ACCENT : pl ? K_TEAL : ghost ? K_BG : K_PANEL, fg = fl || pl ? K_BG : K_TEXT;
        uint16_t dim = fl || pl ? K_BG : K_DIM;
        if (on) {
            cv_round(1, y - 1, 234, ch + 2, 5, K_ACCENT);
            cv_round(4, y + 2, 228, ch - 4, 2, fill);
        } else if (ghost) {                          /* the END row: a row to come */
            cv_round(2, y, 232, ch, 4, K_LINE);
            cv_round(3, y + 1, 230, ch - 2, 3, fill);
        } else
            cv_round(2, y, 232, ch, 4, fill);
        if (pl)
            glyph_play(5, y + 3, K_BG);
        if (ghost) {
            cv_text(SG_LOOP, y - 1, &FONT_XS, "END", on ? K_TEXT : K_DIM);
            if (!n)
                cv_text(SG_LOOP + 24, y - 1, &FONT_XS, "a white key adds a section", K_DIM);
            continue;
        }
        {
            const row_t *w = &g->row[i];
            const loop_t *l = &sloops.loop[w->loop % ARR_LOOPS];
            int32_t x;
            uint16_t sc = sect_colour(row_main(w));
            char *e;
            itoa_u((uint32_t)i + 1u, t);
            text_r(SG_NUM, y - 1, &FONT_XS, t, on || fl || pl ? fg : K_DIM);
            /* PROG: "P9" and its name ("empty" an empty loop) */
            itoa_u((uint32_t)(w->loop % ARR_LOOPS) + 1u, cat_s(t, "P"));
            x = cv_text(SG_LOOP, y - 1, &FONT_XS, t, dim) + 3;
            loop_label(w->loop, t);
            for (e = t; *e && *e != ' '; e++)
                ;
            text_fit(x, y - 1, &FONT_XS, l->bars ? (*e ? e + 1 : e) : "empty", SG_SECT - 3 - x, l->bars ? fg : dim);
            /* GROOVE: its colour, "A" / "B", "+fill" */
            cv_round(SG_SECT, y + 2, 4, ch - 4, 1, fl || pl ? K_BG : sc);
            x = cv_text(SG_SECT + 7, y - 1, &FONT_XS, row_main(w) == SEC_B ? "B" : "A", fg);
            if (row_fill(w))
                cv_text(x + 3, y - 1, &FONT_XS, "+fill", dim);
            /* REPEAT: "x2" (x1 dim) */
            itoa_u(w->rep ? w->rep : 1u, cat_s(t, "x"));
            text_r(SG_REP, y - 1, &FONT_XS, t, w->rep > 1 ? fg : dim);
            /* MUTE: D1 D2 PC BA | C1 C2 C3 C4, M1 M2: plays filled (ink, the melodies plum), muted an outline */
            for (int k = 0; k < ARR_ALLTRK; k++) {
                int32_t mx = k < ARR_TRACKS ? SG_MUTE + k * (SG_MW + 1) + (k >= 4 ? 2 : 0) : SG_LMUTE + (k - ARR_TRACKS) * (SG_MW + 1);
                int32_t my = y + 3;
                int m = k < ARR_TRACKS ? w->mute >> k & 1u : w->lmute >> (k - ARR_TRACKS) & 1u;
                uint16_t c = fl || pl ? K_BG : k < ARR_TRACKS ? K_TEXT : K_PLUM;
                if (m) {
                    cv_rect(mx, my, SG_MW, 8, fl || pl ? K_BG : K_DIM);
                    cv_rect(mx + 1, my + 1, SG_MW - 2, 6, fill);
                } else
                    cv_rect(mx, my, SG_MW, 8, c);
            }
            /* TRANSP: "+2", "-3", "0" (dim) */
            if (w->key) {
                t[0] = w->key > 0 ? '+' : '-';
                itoa_u((uint32_t)(w->key > 0 ? w->key : -w->key), t + 1);
            } else
                cat_s(t, "0");
            text_r(SG_KEY, y - 1, &FONT_XS, t, w->key ? fg : dim);
        }
    }
    if (total > SG_SHOWN) {                          /* the scroll bar: where the window is */
        int32_t th = SG_SHOWN * SG_RH - 2, bh = th * SG_SHOWN / total, by = SG_Y0 + th * top / total;
        cv_round(236, SG_Y0, 3, th, 1, K_PANEL);
        cv_round(236, by, 3, bh < 6 ? 6 : bh, 1, K_DIM);
    }
    {   /* under the rows, page 1: the cursor's place in words, coral ("section 3 of 6"; on END what a white key does,
         * dim); left of it, playing its Intro, a fill, its Ending: "Intro . then section 1" */
        int32_t y = SG_Y0 + SG_SHOWN * SG_RH + 1;
        *t = 0;
        if (pg == 0 && cur < n)                      /* (a pill on the left: only the numbers) */
            itoa_u((uint32_t)n, cat_s(itoa_u((uint32_t)cur + 1u, cat_s(t, st ? "" : "section ")), " of "));
        if (*t)
            text_r(232, y - 1, &FONT_XS, t, K_ACCENT);
        else if (pg == 0 && n && !st)
            text_r(232, y - 1, &FONT_XS, "a white key adds a section", K_DIM);
        if (st) {
            int32_t x = sect_pill(8, y, 0, 15, st, 0);
            cv_text(x + 8, y - 1, &FONT_XS, stt, K_TEXT);
        }
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
 * B: REC's screen says so ("2-bar groove . prog 4 bars"), SEL makes the pattern as long. 0: not */
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
/* the song's style and the sections on user patterns (yours: the style's no longer): "Funk", "QY Bossa", "Pop|Groove A
 * (yours)" (one), "Pop|3 (yours)", "Pop|all (yours)" ("|": a dot on screen) */
static void song_style_text(char *t)
{
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
        cat_s(cat_s(cat_s(e, "|"), SECT_NAME[one + 1]), " (yours)");
    else if (n == ARR_NSECT)
        cat_s(e, "|all (yours)");
    else if (n)
        cat_s(itoa_u((uint32_t)n, cat_s(e, "|")), " (yours)");
}

/* a song's pattern number by its style: "Pop", "QY Bossa" (a QY70 bank style's; "QY -" with no bank), "(yours)" (a
 * user pattern: its number is never on screen; pat_title adds its name); the section is the caller's to say; up to 16
 * characters */
static void style_name(int st, char *t)
{
    int i = style_index(st);
    *t = 0;
    if (st >= 0 && st < ARR_USER_PATTERNS)
        cat_s(t, "(yours)");
    else if (i >= 0)
        cat_s(style_bank(i) ? cat_s(t, "QY ") : t, style_label(i));
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
/* "Pop (yours)", slot s's user pattern by its name (no name: "(yours)"); a style's: "Pop", "QY Bossa" */
static void pat_title(int s, char *t)
{
    int st = songs.song[proj.song].style[s];
    char *e;
    if (st >= ARR_USER_PATTERNS) {
        style_name(st, t);
        return;
    }
    e = pat_name8(app.hdr[s].name, t);
    cat_s(e == t ? e : cat_s(e, " "), "(yours)");
}
/* the Pattern page shows 8 bars at most: a longer pattern by halves (the playhead's), "bars 9-16 of 16" */
#define PAT_WIN 8
/* the groove's pill ("Groove A", "Groove A (yours)": your copy plays) and, len: its length at the right ("2 bars, 4/4",
 * a window of a longer one "bars 9-16 of 16") */
static void pattern_head(int s, int win, int len)
{
    char t[24];
    home_pill(8, 0, s + 1, 0);
    if (!len)
        return;
    {
        char *e = t;
        if (win >= 0) {                          /* (a window of a longer one: "bars 9-16 of 16") */
            e = itoa_u((uint32_t)win * PAT_WIN + 1u, cat_s(e, "bars "));
            *e++ = '-';
            e = itoa_u((uint32_t)(win * PAT_WIN + PAT_WIN < app.hdr[s].bars ? win * PAT_WIN + PAT_WIN : app.hdr[s].bars), e);
            itoa_u(app.hdr[s].bars, cat_s(e, " of "));
        } else {
            e = itoa_u(app.hdr[s].bars, e);
            e = cat_s(e, app.hdr[s].bars == 1 ? " bar, " : " bars, ");
            *e++ = (char)('0' + app.hdr[s].beats);
            cat_s(e, "/4");
        }
    }
    cv_text(232 - text_w(&FONT_XS, t), 3, &FONT_XS, t, K_DIM);
}
/* the 8 tracks as mini piano rolls over the pattern's length (up to 8 bars; a longer one by halves: playing, the one the
 * playhead is in; stopped, the cursor's), the mutes; the selected instrument's row tinted. The cursor (BAR / BEAT): a
 * thick coral line down the instruments, its place under it ("bar 1 . beat 3" on coral); the playhead a teal line; an
 * instrument whose notes or mute just changed: its row coral a moment */
#define PAT_Y 21                                     /* the first instrument's row */
#define PAT_RH 10                                    /* a row's pitch */
static int pat_beats(int s) { return app.hdr[s].beats ? app.hdr[s].beats : 4; }
static int pat_cur(int s)                            /* Band's cursor, kept in the groove */
{
    int n = (app.hdr[s].bars ? app.hdr[s].bars : 1) * pat_beats(s);
    return ui.pcur < n ? ui.pcur : n - 1;
}
static void draw_pattern(void)
{
    int s = pslot(), play = arr_playing, i, nb = app.hdr[s].bars, win = -1, wbars = nb, bpb = pat_beats(s), cur = pat_cur(s);
    uint32_t blen = (uint32_t)bpb * ARR_PPQ, len, w0 = 0, h, pos, bar, bt, fm = flash_on() ? fw.rows : 0u;
    const int32_t X0 = 26, W = 208, YB = PAT_Y + ARR_TRACKS * PAT_RH - 1;
    int32_t ph, cx;
    char t[24];
    view_refresh(s);
    do {
        bar = arr_bar;
        bt = arr_beat_tick;
    } while (bar != arr_bar);
    pos = bar * blen + bt;
    if (nb > PAT_WIN) {                          /* the half the playhead is in (stopped: the cursor's) */
        win = play ? (int)(bar < (uint32_t)nb ? bar : 0u) / PAT_WIN : cur / bpb / PAT_WIN;
        wbars = nb - win * PAT_WIN < PAT_WIN ? nb - win * PAT_WIN : PAT_WIN;
        w0 = (uint32_t)win * PAT_WIN * blen;
    }
    len = (uint32_t)wbars * blen;
    if (!len)
        len = 1;
    ph = play && pos >= w0 && pos < w0 + len ? X0 + (int32_t)((pos - w0) * (uint32_t)W / len) : -1;
    cx = (uint32_t)cur * ARR_PPQ >= w0 && (uint32_t)cur * ARR_PPQ < w0 + len
       ? X0 + (int32_t)(((uint32_t)cur * ARR_PPQ - w0) * (uint32_t)W / len) : -1;
    h = hash(hash(2166136261u, (uint32_t)s | (uint32_t)sel_trk() << 4 | (uint32_t)(ph + 1) << 8), vw.dirty);
    h = hash(hash(h, vw.gen), arr_recording | (uint32_t)(win + 1) << 8 | (uint32_t)cur << 16);
    h = hash(h, fm | (uint32_t)(cx + 1) << 8);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    pattern_head(s, win, 1);
    for (i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *tr = &view.trk[i];
        int32_t y = PAT_Y + i * PAT_RH, b;
        int sel = i == sel_trk(), mute = trk_muted(i), fl = fm >> i & 1u;
        uint32_t n, e = (uint32_t)tr->first + tr->count, lo = 127, hi = 0;
        /* its name (muted: pale, struck through, as HOME's melodies); its row: the selected one darker, ink-outlined
         * (recording into it: coral, REC's) */
        cv_text(4, y - 4, &FONT_XS, TRACK_NAME[i], sel ? K_TEXT : mute ? K_LINE : K_DIM);
        if (mute)
            cv_rect(3, y + 4, 15, 1, K_DIM);
        if (sel && !fl) {
            cv_round(X0 - 1, y - 1, W + 2, PAT_RH + 1, 3, arr_recording ? K_ACCENT : K_TEXT);
            cv_round(X0, y, W, PAT_RH - 1, 3, arr_recording ? RGB(246, 214, 204) : K_PANEL2);
        } else
            cv_round(X0, y, W, PAT_RH - 1, 3, fl ? K_ACCENT : K_PANEL);
        for (b = 1; b < wbars; b++)              /* bar lines */
            cv_rect(X0 + b * W / wbars, y + 1, 1, PAT_RH - 3, fl ? K_BG : K_LINE);
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
            yy = y + 5 - (hi > lo ? (int32_t)((v->note - lo) * 4u / (hi - lo)) : 2);
            cv_rect(x, yy, w, 3, fl ? K_BG : mute ? K_LINE : K_TEXT);
        }
    }
    if (ph >= 0)                                 /* the playhead: teal */
        cv_rect(ph, PAT_Y - 2, 2, YB - PAT_Y + 3, K_TEAL);
    {   /* the cursor: a thick coral line, its place under it on coral, "bar 2.3" (off this half: the place alone, at
         * the side); never over the instruments' names */
        char *e = itoa_u((uint32_t)(cur / bpb) + 1u, cat_s(t, "bar "));
        int32_t w, px;
        *e++ = '.';
        itoa_u((uint32_t)(cur % bpb) + 1u, e);
        w = text_w(&FONT_XS, t) + 10;
        if (cx >= 0) {
            cv_rect(cx - 1, PAT_Y - 2, 3, YB - PAT_Y + 3, K_ACCENT);
            px = cx - w / 2;
            px = px < X0 ? X0 : px > 238 - w ? 238 - w : px;
        } else
            px = cur * ARR_PPQ < (int)w0 ? X0 : 238 - w;
        cv_round(px, YB + 3, w, 14, 4, K_ACCENT);
        cv_text(px + 5, YB + 1, &FONT_XS, t, K_BG);
    }
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
static void draw_lead(int rec);
static void sound_head(const char *trk, uint32_t prog, int drums, int lead);
static void draw_lead_rec(void) { draw_lead(1); }
static void draw_trk_rec(void)
{
    int s = pslot(), trk = proj.trk & 7, nb = app.hdr[s].bars, bpb = app.hdr[s].beats, play = arr_playing;
    int wait = !arr_recording || arr_rec_wait, focus = -1, fbeat = 0, next = -1, cnt = 0, pass = 1, rows, ch, b, k;
    int ll = loop_longer(), lbar = -1, lbars = 0;
    uint32_t blen, bar, bt, h, n, e, lo = 127, hi = 0, fbars = 0;
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
    if (play && pl.loop >= 0 && loop_len(&sloops.loop[pl.loop]) > 0) {   /* the loop's bar too ("prog bar 3/4") */
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
    h = hash(hash(h, vw.dirty), vw.gen);
    if (flash_on() && (fw.id & 15u) == 3u)
        fbars = fw.bars;                             /* (notes just recorded: their bars) */
    h = hash(h, fbars);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    {   /* the line: REC's dot, "REC C1 . pass 2 . adding"; under it the pattern's bar (and the loop's: "groove bar 1/2 .
         * prog bar 3/4"), or a pattern shorter than the loop ("2-bar groove . prog 4 bars . SEL: 4 bars"), the grid */
        int32_t x;
        cv_round(8, 6, 10, 10, 5, K_REC);
        p = cat_s(cat_s(t, "REC "), TRACK_NAME[trk]);
        if (wait)
            cat_s(p, play ? "|starts at the next bar" : "|starts with PLAY");
        else
            itoa_u((uint32_t)pass, cat_s(p, "|pass "));
        x = segs(24, 2, &FONT_B, t, K_TEXT);
        if (!wait && pass > 1)                       /* the passes before play meanwhile: this one adds */
            segs(x, 2, &FONT_B, "|adding", K_ACCENT);
        if (!wait) {                                 /* where: the groove's bar, the progression's (a groove shorter
                                                      * than the progression: the footer says SEL makes it as long) */
            p = itoa_u((uint32_t)focus + 1u, cat_s(t, "groove bar "));
            p = itoa_u((uint32_t)nb, cat_s(p, " / "));
            if (lbar >= 0) {
                p = itoa_u((uint32_t)lbar + 1u, cat_s(p, "|prog bar "));
                itoa_u((uint32_t)lbars, cat_s(p, " / "));
            }
            segs(24, 20, &FONT_XS, t, K_DIM);
        }
    }
    for (b = 0; b < nb; b++) {                       /* the bars */
        int32_t x = RG_X + (b % 4) * (RG_CW + 4), y = TR_Y + (b / 4) * (ch + 3), ax, aw, ay, ah, nh;
        int plays = b == focus, cin = b == next, fl = fbars >> b & 1u;
        uint16_t fill = fl ? K_ACCENT : plays ? K_TEAL : K_PANEL, fg = plays || fl ? K_BG : K_TEXT;
        if (cin) {                                   /* the bar recording starts on: a thick coral ring */
            cv_round(x, y, RG_CW, ch, 5, K_ACCENT);
            cv_round(x + 3, y + 3, RG_CW - 6, ch - 6, 3, plays ? K_TEAL : K_BG);
        } else
            cv_round(x, y, RG_CW, ch, 5, fill);
        itoa_u((uint32_t)b + 1u, t);
        ax = ch >= 30 ? x + 4 : cv_text(x + 4, y + ch / 2 - 7, &FONT_XS, t, plays || fl ? K_BG : K_DIM) + 3;
        if (ch >= 30)
            cv_text(x + 4, y, &FONT_XS, t, plays || fl ? K_BG : cin ? K_ACCENT : K_DIM);
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
/* SEQ / EDIT while a lead track is recorded (REC on): where you are in the song. A line "REC M1 . section 3 . bar 2.3"
 * (the row playing, its bar and beat from the row's first downbeat; else why nothing records yet: "starts with PLAY",
 * "after the Intro", "in a song only" in FREE / LOOP), under it the lead's sound, the grid and its notes in the row;
 * then the row as bars, four to a line (eight show: a longer row by eights, the playing one's), the bar playing teal
 * with its beats along its foot, the lead's notes in the row as ticks (by place and pitch); at the foot the song's
 * rows, a cell each, the one playing teal (waiting: the one it starts in, a coral ring), a dot in those holding this
 * lead's notes; the other lead's notes dim under this one's. REC while playing counts in to the next bar line (its
 * bar ringed, the beats left in it, big; the row's last bar: the next row's cell ringed) */
#define LR_STRIP (MAIN_H - 16)                       /* the song's rows */
/* (rec 0: Sound with a melody selected, not recording: the same view, the sound over it, the cursor (SONG's section,
 * from its bar 1: where PLAY and REC start) ringed coral and said in words, coral; playing, the place teal) */
static void draw_lead(int rec)
{
    static const char *const WHY[8] = {"", "in a song only", "no sections yet", "starts with PLAY", "after the Intro",
                                       "the song is ending", "from the bar line", "starts at the next bar"};
    const song_t *g = csong();
    int k = (app.lead ? app.lead - 1 : 0) % ARR_LEADS, n = g->nrows, play = arr_playing, r = 0, live, nb = 1, bpb, focus = -1, fbeat = 0;
    int win, wbars, lines, ch, b, why, cnt = 0, cin = -1, cbeats = 0, crow = -1;
    uint32_t blen, rt = 0, h, i, i0 = 0, i1 = 0, lo = 127, hi = 0, fbars = 0, frows = 0;
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
    ch = lines == 1 ? 64 : 31;
    if (flash_on() && fw.id >> 12 == ((uint32_t)r | (uint32_t)bpb << 8) && (fw.id & 15u) == 4u) {
        fbars = fw.bars;                             /* (what just changed: this section's) */
        frows = fw.rows;
    }
    h = hash(2166136261u, 0x1EADu | (uint32_t)k << 16 | (uint32_t)r << 17 | (uint32_t)live << 23 | (uint32_t)why << 24);
    h = hash(hash(hash(h, fbars), frows), (uint32_t)rec);
    h = hash(h, (uint32_t)(focus + 1) | (uint32_t)fbeat << 8 | (uint32_t)nb << 12 | (uint32_t)win << 20 | (uint32_t)n << 24);
    h = hash(h, (uint32_t)(cin + 1) | (uint32_t)cbeats << 8 | (uint32_t)(crow + 1) << 12);
    h = hash(hash(h, leads.gen | (uint32_t)leads.n << 16), lt->program | (uint32_t)lt->chan << 8 | (uint32_t)proj.quant << 12 |
                                                                (uint32_t)cnt << 16);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    if (!rec) {   /* the line: "M1  Synth Lead  GM 81" (Sound's head); under it where: "section 2 . bar 1" (the cursor, coral; playing,
                   * "section 3 . bar 2.3", teal), the notes in the section */
        int32_t x;                                   /* (the head: Sound's, its family; the sound: the knob's value) */
        sound_head(TRACK_NAME[ARR_LD1 + k], lt->program & 127u, (lt->chan & 15u) == 9u, 1);
        if (!n)
            cv_text(10, 20, &FONT_XS, "no sections yet (Song page)", K_DIM);
        else {
            p = itoa_u((uint32_t)r + 1u, cat_s(t, "section "));
            if (live) {
                p = itoa_u((uint32_t)focus + 1u, cat_s(p, "|bar "));
                *p++ = '.';
                itoa_u((uint32_t)fbeat + 1u, p);
            } else
                cat_s(p, "|bar 1");
            x = segs(10, 20, &FONT_XS, t, live ? K_TEAL : K_ACCENT);
            if (why == 1 || (why >= 4 && why <= 5))  /* (not in a song, the Intro, the Ending: why bar 1 is ringed) */
                cat_s(cat_s(t, "|"), WHY[why]);
            else {
                p = itoa_u((uint32_t)cnt, cat_s(t, "|"));
                cat_s(p, cnt == 1 ? " note here" : " notes here");
            }
            segs(x, 20, &FONT_XS, t, K_DIM);
        }
    } else {   /* the line: REC's dot, "REC M1 . section 3 . bar 2.3"; under it the sound, the grid, the row's notes */
        cv_round(8, 6, 10, 10, 5, K_REC);
        p = cat_s(cat_s(t, "REC "), TRACK_NAME[ARR_LD1 + k]);
        if (why)
            cat_s(cat_s(p, "|"), WHY[why]);
        else {
            p = itoa_u((uint32_t)r + 1u, cat_s(p, "|section "));
            p = itoa_u((uint32_t)focus + 1u, cat_s(p, "|bar "));
            *p++ = '.';
            itoa_u((uint32_t)fbeat + 1u, p);
        }
        segs(24, 2, &FONT_B, t, K_TEXT);
        if (why == 1)
            cv_text(24, 20, &FONT_XS, "On Song: PLAY, then REC here", K_ACCENT);
        else {
            p = cat_s(cat_s(t, "grid "), GRID_NAME[proj.quant % ARR_NQ]);   /* (its sound: the tag's) */
            if (n) {
                p = itoa_u((uint32_t)cnt, cat_s(p, "|"));
                itoa_u((uint32_t)r + 1u, cat_s(p, cnt == 1 ? " note in section " : " notes in section "));
            }
            segs(24, 20, &FONT_XS, t, K_DIM);
        }
    }
    for (b = 0; b < wbars; b++) {                    /* the row's bars (eight of them at most) */
        int bi = win * 8 + b, plays = bi == focus, start = ((rec ? why >= 3 : !live && n) && why < 7 && bi == 0) || bi == cin;
        int fl = bi < 32 && (fbars >> bi & 1u);
        int32_t x = RG_X + (b % 4) * (RG_CW + 4), y = TR_Y + (b / 4) * (ch + 3), ax, aw, ay, ah, nh;
        uint16_t fg = plays || fl ? K_BG : K_TEXT, other = fl ? RGB(250, 196, 170) : plays ? K_TEAL_T : K_LINE;
        uint16_t fill = fl ? K_ACCENT : plays ? K_TEAL : K_PANEL;
        uint32_t b0 = (uint32_t)bi * blen;
        if (start) {                                 /* the bar it starts in (the cursor): a thick coral ring */
            cv_round(x, y, RG_CW, ch, 5, K_ACCENT);
            cv_round(x + 3, y + 3, RG_CW - 6, ch - 6, 3, fill);
        } else
            cv_round(x, y, RG_CW, ch, 5, fill);
        itoa_u((uint32_t)bi + 1u, t);
        cv_text(x + 5, y + (start ? 1 : 0), &FONT_XS, t, plays || fl ? K_BG : start ? K_ACCENT : K_DIM);
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
            int on = live && j == r, ring = (!live && (why >= 3 || !rec) && j == r) || j == crow, fl = frows >> j & 1u;
            uint16_t fill = fl ? K_ACCENT : on ? K_TEAL : K_PANEL;
            if (ring) {
                cv_round(x, y, cw, 16, 3, K_ACCENT);
                cv_round(x + 2, y + 2, cw - 4, 12, 2, fill);
            } else
                cv_round(x, y, cw, 16, 3, fill);
            if (cw >= 13) {
                itoa_u((uint32_t)j + 1u, t);
                text_c(x + cw / 2, y - 1, &FONT_XS, t, on || fl ? K_BG : ring ? K_ACCENT : K_DIM);
            }
            if (leads_count(&leads, j, k))
                cv_rect(x + cw / 2 - 2, y + 12, 4, 2, on || fl ? K_BG : K_PLUM);
        }
    }
    cv_blit(0, MAIN_Y);
}
/* Groove length: the groove's bars as a picture (a cell a bar, its beats marked in it; the bars the Bars knob would add,
 * up to 16, as ghosts), its name under it, a box a letter (Place: the one Letter turns) */
static void draw_patset(void)
{
    int s = pslot(), i, nb = app.hdr[s].bars, bpb = app.hdr[s].beats;
    uint32_t h = hash(2166136261u, (uint32_t)s | (uint32_t)ui.name_cur << 4 | (uint32_t)nb << 8 | (uint32_t)bpb << 16);
    char t[24];
    h = hash(h, (uint32_t)csong()->style[s]);
    for (i = 0; i < 8; i++)
        h = hash(h, (uint8_t)app.hdr[s].name[i]);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    pattern_head(s, -1, 0);
    if (bpb < 2 || bpb > 7)
        bpb = 4;
    for (i = 0; i < ARR_MAXBARS; i++) {              /* the bars: two rows of eight */
        int32_t x = 8 + (i % 8) * 28, y = 24 + (i / 8) * 22, w = 25, sw = (w - 6) / bpb;
        if (i < nb) {
            cv_round(x, y, w, 19, 4, K_PANEL2);
            for (int k = 0; k < bpb; k++)            /* its beats */
                cv_rect(x + 3 + k * sw + (w - 6 - bpb * sw) / 2, y + 14, sw - 1, 2, K_TEXT);
            itoa_u((uint32_t)i + 1u, t);
            cv_text(x + 3, y - 1, &FONT_XS, t, K_TEXT);
        } else {                                     /* a bar it could have: a ghost */
            cv_round(x, y, w, 19, 4, K_LINE);
            cv_round(x + 1, y + 1, w - 2, 17, 3, K_BG);
        }
    }
    cv_round(12, 70, 216, 46, 6, K_PANEL);
    for (i = 0; i < 8; i++) {                    /* the name, a box a letter */
        int32_t x = 18 + i * 26;
        char c[2] = {app.hdr[s].name[i] ? app.hdr[s].name[i] : ' ', 0};
        if (i == ui.name_cur)
            cv_round(x, 75, 22, 32, 4, K_PANEL2);
        text_c(x + 11, 77, &FONT_M, c, K_TEXT);
        cv_rect(x + 3, 105, 16, i == ui.name_cur ? 2 : 1, i == ui.name_cur ? K_ACCENT : K_LINE);
    }
    cv_blit(0, MAIN_Y);
}
/* Sound's browser: the family of eight around the sound, two columns of four (1-4, 5-8), the sound the cursor (a coral
 * ring; the knob's value, the tag's name: the same sound, named once more here as where you are among its family) */
#define SB_Y 24                                      /* the list's top; a row 17 px */
#define SB_RH 17
#define SB_CW 110                                    /* a column (the longest name, "Tango Accordion": 98 px) */
static void sound_list(uint32_t prog)
{
    uint32_t f = prog & 120u, i;
    for (i = 0; i < 8u; i++) {
        int32_t x = 8 + (int32_t)(i / 4u) * (SB_CW + 4), y = SB_Y + (int32_t)(i % 4u) * SB_RH;
        if (f + i == prog) {
            cv_round(x, y, SB_CW, SB_RH - 1, 4, K_ACCENT);
            cv_round(x + 2, y + 2, SB_CW - 4, SB_RH - 5, 3, K_BG);
        }
        text_fit(x + 6, y - 1, &FONT_S, GM_PROGRAM[f + i], SB_CW - 10, K_TEXT);
    }
}
/* the Sound page's head: the instrument's pill (a melody's plum), the family's name (the list's heading; a melody's
 * plum), the sound's General MIDI number small at the right ("GM 9"); drums: "Drums", no number */
static void sound_head(const char *trk, uint32_t prog, int drums, int lead)
{
    char a[12];
    int32_t x = lead ? pill(8, 0, 0, trk, K_PLUM, K_BG) : pill_o(8, 0, 0, trk, K_TEXT, K_TEXT), gx = 232;
    if (!drums) {
        itoa_u(prog + 1u, cat_s(a, "GM "));
        gx -= text_w(&FONT_XS, a);
        cv_text(gx, 3, &FONT_XS, a, K_DIM);
    }
    text_fit(x + 8, 2, &FONT_B, drums ? "Drums" : GM_FAMILY[(prog & 127u) / 8u], gx - 8 - x - 8, lead ? K_PLUM : K_TEXT);
}
/* a melody's Sound page while its Sound knob turns (or PRE steps the families): the browser, over the song view */
static int sound_browsing(void) { return app.lead && ui.touched == 0 && plat_ms() < ui.touch_until; }
/* the selected instrument's sound (Sound): the head (its family, the General MIDI number), the family's eight sounds
 * (the browser), the keys it plays over (the range over the whole MIDI keyboard; drums: the kit, a sound on each key;
 * a melody: no range, its browser only while the Sound knob turns). Sound 2: what the high key does, as a picture:
 * an octave, the roots above the high key an octave down (Chord 1 and Bass only) */
static void draw_track(void)
{
    int s = pslot(), k2 = ui.page == P_TRACK2;
    arr_trk_t lb;
    const arr_trk_t *t = sel_arr(&lb);
    int drums = (t->chan & 15u) == 9u, lead = app.lead != 0;
    uint32_t h = hash(2166136261u, (uint32_t)s | (uint32_t)sel_trk() << 4 | (uint32_t)k2 << 8);
    char a[40], b[16];                               /* ("roots above C#: an octave down": 31) */
    h = hash(h, t->program | (uint32_t)t->chan << 8 | (uint32_t)t->x.type << 16 | (uint32_t)t->x.hikey << 24);
    h = hash(h, t->x.lo | (uint32_t)t->x.hi << 8 | (uint32_t)t->flags << 16);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    if (k2) {   /* the pill; the General MIDI number, small, at the right ("GM 9"); the sound's name by the pill */
        int32_t x = pill_o(8, 0, 0, TRACK_NAME[sel_trk()], K_TEXT, K_TEXT);
        if (!drums) {
            itoa_u(t->program + 1u, cat_s(a, "GM "));
            cv_text(232 - text_w(&FONT_XS, a), 3, &FONT_XS, a, K_DIM);
        }
        text_fit(x + 8, 2, &FONT_B, drums ? "Drum kit" : GM_PROGRAM[t->program & 127u], 180 - x, K_TEXT);
    } else
        sound_head(TRACK_NAME[sel_trk()], t->program & 127u, drums, lead);
    if (k2) {   /* the high key: an octave C..B, the roots above it shaded (an octave down), it marked */
        static const uint8_t BLK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
        int hk = t->x.hikey, works = t->x.type == XT_CHORD1 || t->x.type == XT_BASS;
        int32_t x0 = 16, kw = 17, y0 = 30;
        if (drums) {
            cv_text(10, 40, &FONT_S, "Drums: no high key", K_DIM);
            cv_blit(0, MAIN_Y);
            return;
        }
        for (int n = 0; n < 12; n++) {               /* the keys, in a row (the black ones short, dark) */
            int32_t x = x0 + n * kw, over = hk < XT_NOKEY && n > hk;
            uint16_t c = n == hk ? K_ACCENT : over && works ? K_PANEL2 : K_PANEL;
            cv_round(x, y0, kw - 2, BLK[n] ? 22 : 34, 2, BLK[n] ? (n == hk ? K_ACCENT : over && works ? K_DIM : K_TEXT) : c);
            text_c(x + (kw - 2) / 2, y0 + 36, &FONT_XS, KEY_NAME[n], n == hk ? K_ACCENT : K_DIM);
        }
        if (hk >= XT_NOKEY)
            cat_s(a, "off: every root where it is");
        else {
            cat_s(cat_s(cat_s(a, "roots above "), KEY_NAME[hk % 12]), ": an octave down");
        }
        cv_text(10, 84, &FONT_S, a, works ? K_TEXT : K_DIM);
        if (!works)
            cv_text(10, 100, &FONT_XS, "(it moves Chord 1 and Bass only)", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    if (drums) {   /* the kit: one entry, no family (nothing to step through), ink: it stays */
        cv_round(8, SB_Y, SB_CW, SB_RH - 1, 4, K_TEXT);
        cv_round(9, SB_Y + 1, SB_CW - 2, SB_RH - 3, 3, K_BG);
        cv_text(14, SB_Y - 1, &FONT_S, "Drum kit", K_TEXT);
        cv_text(10, SB_Y + SB_RH + 6, &FONT_S, "Channel 10: a sound on each key", K_DIM);
    } else {
        sound_list(t->program & 127u);
        if (!lead) {   /* the range over the whole MIDI keyboard, its two ends at its sides ("G3 ==== G5") */
            int32_t y = SB_Y + 4 * SB_RH + 6, x0 = 38, w = 164, lx, hx;
            note_name(t->x.lo, a);
            note_name(t->x.hi, b);
            lx = x0 + t->x.lo * w / 127;
            hx = x0 + t->x.hi * w / 127;
            cv_round(x0, y + 5, w, 6, 3, K_PANEL);
            cv_round(lx, y + 5, hx - lx < 6 ? 6 : hx - lx, 6, 3, K_DIM);
            cv_text(x0 - 6 - text_w(&FONT_XS, a), y, &FONT_XS, a, K_DIM);
            cv_text(x0 + w + 6, y, &FONT_XS, b, K_DIM);
        }
    }
    cv_blit(0, MAIN_Y);
}
/* the ten instruments' strips (the pattern's eight, the song's two melodies after a gap, plum): volume (its number
 * under it), pan; the selected one darker, ink-outlined; muted ones pale. Their sends: the Effects page's */
#define MX_PITCH 23
#define MX_W 21
static int32_t strip_x(int i) { return 3 + i * MX_PITCH + (i >= ARR_TRACKS ? 4 : 0); }
static uint16_t strip_colour(int i)
{
    return trk_muted(i) ? K_LINE : i >= ARR_TRACKS ? K_PLUM : K_TEXT;
}
static void strip_bg(int i, int sel, int32_t h)      /* a strip's ground (h tall), its name */
{
    int32_t x = strip_x(i);
    if (sel) {
        cv_round(x - 1, 0, MX_W + 2, h, 5, K_TEXT);
        cv_round(x, 1, MX_W, h - 2, 4, K_PANEL2);
    } else
        cv_round(x, 0, MX_W, h, 5, K_PANEL);
    text_c(x + MX_W / 2, 0, &FONT_XS, TRACK_NAME[i], sel ? K_TEXT : K_DIM);
}
static void draw_mixer(void)
{
    int s = pslot(), i;
    arr_trk_t lb;
    uint32_t h = hash(2166136261u, (uint32_t)s | (uint32_t)sel_trk() << 4);
    for (i = 0; i < ARR_ALLTRK; i++) {
        const arr_trk_t *t = strip_trk(i, &lb);
        h = hash(h, t->vol | (uint32_t)t->pan << 8);
        h = hash(h, (uint32_t)trk_muted(i));
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < ARR_ALLTRK; i++) {
        const arr_trk_t *t = strip_trk(i, &lb);
        int32_t x = strip_x(i), w = MX_W, vh = 56 * t->vol / 127;
        char r[6];
        strip_bg(i, i == sel_trk(), MAIN_H);
        cv_round(x + 5, 16, 11, 58, 4, K_BG);   /* volume */
        if (vh > 0)
            cv_round(x + 5, 16 + 58 - vh - 1, 11, vh, 4, strip_colour(i));
        cv_rect(x + 3, 80, w - 6, 2, K_LINE);   /* pan */
        cv_round(x + 3 + (w - 10) * t->pan / 127, 77, 4, 8, 2, K_TEXT);
        itoa_u(t->vol, r);                       /* the volume, as the knob */
        text_c(x + w / 2, 90, &FONT_XS, r, trk_muted(i) ? K_DIM : K_TEXT);
    }
    cv_blit(0, MAIN_Y);
}
/* the tone: each track's brightness (CC 74, the filter's cutoff) and resonance (CC 71), 64 neutral (a line; the numbers
 * dim there) */
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
        int32_t x = strip_x(i), w = MX_W, b = arr_bright(t), r = arr_reso(t);
        int32_t bh = 56 * b / 127, rh = 56 * r / 127;
        char v[6];
        strip_bg(i, i == sel_trk(), MAIN_H);
        cv_round(x + 3, 16, 6, 58, 3, K_BG);    /* brightness */
        cv_round(x + 12, 16, 6, 58, 3, K_BG);   /* resonance */
        if (bh > 0)
            cv_round(x + 3, 16 + 58 - bh - 1, 6, bh, 3, strip_colour(i));
        if (rh > 0)
            cv_round(x + 12, 16 + 58 - rh - 1, 6, rh, 3, trk_muted(i) ? K_LINE : K_DIM);
        cv_rect(x + 2, 16 + 58 - 29, w - 4, 1, K_DIM); /* 64: neutral */
        itoa_u((uint32_t)b, v);                  /* brightness, resonance (as the knobs) */
        text_c(x + w / 2, 80, &FONT_XS, v, b == 64 ? K_DIM : K_TEXT);
        itoa_u((uint32_t)r, v);
        text_c(x + w / 2, 96, &FONT_XS, v, r == 64 ? K_DIM : K_TEXT);
    }
    cv_blit(0, MAIN_Y);
}
/* Effects: the reverb and the chorus every instrument sends to (the knobs: their kind and level); the main band the
 * sends, the instruments of the groove playing (and the melodies) as the Mixer's strips: reverb (ink), chorus (dim) */
static void draw_master(void)
{
    int s = pslot(), i;
    arr_trk_t lb;
    uint32_t h = hash(2166136261u, 0x3A000u | (uint32_t)s);
    char t[32];
    for (i = 0; i < ARR_ALLTRK; i++) {
        const arr_trk_t *tr = strip_trk(i, &lb);
        h = hash(h, tr->rev | (uint32_t)tr->cho << 8 | (uint32_t)trk_muted(i) << 16);
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < ARR_ALLTRK; i++) {
        const arr_trk_t *tr = strip_trk(i, &lb);
        int32_t x = strip_x(i), rh = 76 * tr->rev / 127, chh = 76 * tr->cho / 127;
        strip_bg(i, 0, 98);
        cv_round(x + 3, 16, 6, 78, 3, K_BG);    /* reverb send */
        cv_round(x + 12, 16, 6, 78, 3, K_BG);   /* chorus send */
        if (rh > 0)
            cv_round(x + 3, 16 + 78 - rh - 1, 6, rh, 3, trk_muted(i) ? K_LINE : i >= ARR_TRACKS ? K_PLUM : K_TEXT);
        if (chh > 0)
            cv_round(x + 12, 16 + 78 - chh - 1, 6, chh, 3, trk_muted(i) ? K_LINE : K_DIM);
    }
    {   /* the key: whose sends, which bar is which */
        int32_t x;
        cat_s(cat_s(t, SECT_NAME[s + 1]), "'s sends:");
        x = cv_text(8, 102, &FONT_XS, t, K_DIM) + 8;
        cv_round(x, 108, 6, 6, 2, K_TEXT);
        x = cv_text(x + 9, 102, &FONT_XS, "reverb", K_TEXT) + 10;
        cv_round(x, 108, 6, 6, 2, K_DIM);
        cv_text(x + 9, 102, &FONT_XS, "chorus", K_TEXT);
    }
    cv_blit(0, MAIN_Y);
}
/* -------------------------------------------------- the song's pages --- */
/* STYLE (ENV): the song's six sections, a row each: its pill, the style it plays ("Pop", "Funk (yours)": your copy). The
 * three the knobs turn on this page (P_STYLE: Intro, Groove A, Groove B; P_STYLE2: the fills and the Ending) on a panel,
 * bold (in the knobs' order) */
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
    for (i = 0; i < ARR_NSECT; i++) {
        int32_t y = 2 + (int32_t)i * 19;
        int on = i >= first && i < first + 3u;
        if (on)
            cv_round(4, y - 1, 232, 19, 5, K_PANEL);
        sect_pill(8, y + 1, 76, 15, (int)i + 1, 0);
        pat_title((int)i, t);                    /* "Pop", "Funk (yours)" */
        text_fit(92, y, on ? &FONT_B : &FONT_S, t, 138, on ? K_TEXT : K_DIM);   /* the knobs' three: bold */
    }
    cv_blit(0, MAIN_Y);
}
static int32_t wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c);
static void setup_row(int32_t y, const char *name, const char *val)
{
    cv_round(8, y, 224, 22, 6, K_PANEL);
    cv_text(18, y + 2, &FONT_S, name, K_DIM);
    cv_text(222 - text_w(&FONT_B, val), y + 2, &FONT_B, val, K_TEXT);
}
static void bank_text(char *t)                       /* the QY70 bank: "128 styles", "none", "damaged", "old format" */
{
    int n = qb_status();
    if (n > 0)
        cat_s(itoa_u((uint32_t)n, t), n == 1 ? " style" : " styles");
    else
        cat_s(t, n == QB_NONE ? "none" : n == QB_OLD ? "old format" : "damaged");
}
/* Setup: what the knob last turned does, in words (its knob: its value); the QY70 bank (no knob: installed from a
 * computer) */
static void setup_says(int k, const char **name, const char **what)
{
    static const char *const LIGHTS[3] = {"Off: no lights but the buttons'.", "The keys light their notes; the buttons glow.",
                                          "The keys light their notes; no glow."};
    static const char *const CLICK[3] = {"A click in the count-in only.", "A click while recording, and the count-in.",
                                         "A click on every beat."};
    *name = k == 0 ? "Clock" : k == 1 ? "MIDI out" : k == 2 ? "Lights" : "Click";
    *what = k == 0 ? (proj.clock ? "Follows MIDI clock in when it comes; its own tempo when not."
                                 : "Its own tempo; MIDI clock in is ignored.")
          : k == 1 ? (proj.mout ? "Every instrument out on its channel, and the clock." : "Nothing out over MIDI.")
          : k == 2 ? LIGHTS[proj.leds % 3u] : CLICK[proj.click % 3u];
}
static void draw_setup(void)
{
    int k = ui.touched >= 0 && ui.touched < 4 ? ui.touched : -1;
    uint32_t h = hash(2166136261u, 8192u + proj.clock + ((uint32_t)proj.mout << 2) + ((uint32_t)proj.leds << 8) +
                                       ((uint32_t)(qb_status() + 8) << 12) + ((uint32_t)proj.click << 24));
    char t[16];
    h = hash(h, (uint32_t)(k + 1));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(8, 1, 224, 88, 6, K_PANEL);
    if (k < 0)
        wrap(18, 8, 204, &FONT_S, "Turn a knob: what it does shows here.", K_DIM);
    else {
        const char *nm, *what;
        setup_says(k, &nm, &what);
        cv_text(18, 4, &FONT_B, nm, K_TEXT);
        wrap(18, 26, 204, &FONT_S, what, K_TEXT);
    }
    bank_text(t);
    setup_row(95, "QY70 styles", t);
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
    cv_round(8, 2, 108, 66, 6, K_PANEL);
    cv_round(124, 2, 108, 66, 6, K_PANEL);
    cv_text(18, 6, &FONT_XS, "CPU", K_DIM);
    cat_s(itoa_u(cpu, t), "%");
    cv_text(18, 24, &FONT_L, t, cpu > 80u ? K_ACCENT : K_TEXT);
    cv_text(134, 6, &FONT_XS, "voices", K_DIM);
    itoa_u(v, t);
    cv_text(134, 24, &FONT_L, t, K_TEXT);
    cv_round(8, 74, 224, 44, 6, K_PANEL);
    cv_text(18, 76, &FONT_XS, "version", K_DIM);
    cv_text(18, 92, &FONT_B, "Polly " GM_VERSION, K_TEXT);
    itoa_u(x, cat_s(t, "xruns "));
    cv_text(222 - text_w(&FONT_XS, t), 76, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}

/* HOME held: the keys' shortcuts. The five sections (black keys 1-5, their numbers over them: Intro, Groove A, Groove B,
 * Fill, Ending; each pill as wide as its name, spread over the width), then the 16 progressions (white keys 1-16) in
 * two columns by name, "-" for an empty one. Rings: teal what plays now (only while playing), coral what you asked for
 * (from the next bar line), ink where PLAY starts (stopped) */
static int held_ring(int i)                          /* progression i's: 0 none, 1 teal, 2 coral, 3 ink */
{
    int ln = arr_loop_now, play = arr_playing && ln >= 0 && ln < ARR_LOOPS;
    if (play && i == ln)
        return 1;
    if (app.mode == ARR_LOOP && i == app.loop)
        return play ? 2 : 3;
    return 0;
}
static int held_sect_ring(int s)                     /* section s's pill (SEC_FILL: either fill): the same colours */
{
    int n = arr_next_sect, cur = arr_sect;
    if (cur == SEC_FILL_AB || cur == SEC_FILL_BA)
        cur = SEC_FILL;
    if (n == SEC_FILL_AB || n == SEC_FILL_BA)
        n = SEC_FILL;
    if (n >= SEC_INTRO && n <= SEC_FILL && s == n)
        return arr_playing ? 2 : 3;
    if (s == cur && !(n >= SEC_INTRO && n <= SEC_FILL && !arr_playing))
        return arr_playing ? 1 : 3;
    return 0;
}
static void draw_home_held(void)
{
    static const uint8_t SECT[5] = {SEC_INTRO, SEC_A, SEC_B, SEC_FILL, SEC_END};
    static const uint16_t RING[4] = {0, K_TEAL, K_ACCENT, K_TEXT};
    int32_t pw[5], tw = 0, gap, px = 8;
    int i;
    uint32_t h = hash(2166136261u, 32768u + (uint32_t)arr_playing);
    for (i = 0; i < 5; i++)
        h = hash(h, (uint32_t)held_sect_ring(SECT[i]));
    for (i = 0; i < ARR_LOOPS; i++)
        h = hash(h, sloops.loop[i].bars | (uint32_t)(uint8_t)sloops.loop[i].name[0] << 8 | (uint32_t)held_ring(i) << 16);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    for (i = 0; i < 5; i++)
        tw += pw[i] = text_w(&FONT_XS, SECT_NAME[SECT[i]]) + 10;
    gap = (224 - tw) / 4;
    for (i = 0; i < 5; i++) {
        char d[2] = {(char)('1' + i), 0};
        int r = held_sect_ring(SECT[i]);
        text_c(px + pw[i] / 2, -4, &FONT_XS, d, K_DIM);   /* its black key's number */
        if (r) {
            cv_round(px - 2, 8, pw[i] + 4, 19, 6, RING[r]);
            cv_round(px, 10, pw[i], 15, 5, K_BG);
        }
        sect_pill(px, 10, pw[i], 15, SECT[i], 0);
        px += pw[i] + gap;
    }
    for (i = 0; i < ARR_LOOPS; i++) {                /* two columns, down then across */
        int32_t x = 6 + (i / 8) * 116, y = 31 + (i % 8) * 11;
        const loop_t *l = &sloops.loop[i];
        char t[16], *e;
        int r = held_ring(i);
        uint16_t fg = r == 1 ? K_BG : K_TEXT, dim = r == 1 ? K_BG : K_DIM;
        if (r == 1)                                  /* playing: filled teal (as Song's section playing) */
            cv_round(x, y - 1, 112, 12, 4, K_TEAL);
        else if (r) {                                /* asked for, where PLAY starts: a ring */
            cv_round(x, y - 1, 112, 12, 4, RING[r]);
            cv_round(x + 1, y, 110, 10, 3, K_BG);
        }
        itoa_u((uint32_t)i + 1u, cat_s(t, "P"));
        cv_text(x + 4, y - 3, &FONT_XS, t, dim);
        if (l->bars) {
            loop_label(i, t);
            for (e = t; *e && *e != ' '; e++)
                ;
            if (*e)
                text_fit(x + 28, y - 3, &FONT_XS, e + 1, 80, fg);
            else {                                   /* its default name: its length instead ("2 bars") */
                cat_s(itoa_u(l->bars, t), l->bars == 1 ? " bar" : " bars");
                cv_text(x + 28, y - 3, &FONT_XS, t, dim);
            }
        } else
            cv_text(x + 28, y - 3, &FONT_XS, "-", r == 1 ? K_BG : K_LINE);
    }
    cv_blit(0, MAIN_Y);
}

/* ------------------------------------------------------------- knobs --- */
typedef struct {
    const char *name;
    char val[24];
    int32_t num, max;                                /* the bar: num of max (max 0: no bar) */
} knob_t;

static void kv(knob_t *k, const char *name, const char *val, int32_t num, int32_t max)
{
    uint32_t i;
    k->name = name;
    for (i = 0; i < sizeof k->val - 1u && val[i]; i++)   /* (a sound's name: to 23 letters, fitted when drawn) */
        k->val[i] = val[i];
    k->val[i] = 0;
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
/* HOME: the song's style, tempo, transpose; PROG ("-" FREE, P1..P16, "Song" while in the song) */
static void home_knobs(knob_t *k)
{
    char t[8];
    style_knob(&k[0]);
    kn(&k[1], "Tempo", csong()->tempo, 240);
    transpose_knob(&k[2]);
    if (app.mode == ARR_SONG)
        kv(&k[3], "Prog", "Song", ARR_LOOPS, ARR_LOOPS);
    else if (app.mode == ARR_LOOP) {
        itoa_u(app.loop + 1u, cat_s(t, "P"));
        kv(&k[3], "Prog", t, app.loop + 1, ARR_LOOPS);
    } else
        kv(&k[3], "Prog", "-", 0, ARR_LOOPS);
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
        kn(&k[1], "Bar", pat_cur(s) / pat_beats(s) + 1, app.hdr[s].bars ? app.hdr[s].bars : 1);   /* the cursor */
        kn(&k[2], "Beat", pat_cur(s) % pat_beats(s) + 1, pat_beats(s));
        kv(&k[3], "Mute", trk_muted(sel_trk()) ? "muted" : "plays", trk_muted(sel_trk()), 0);
        break;
    case P_PATSET:
        kn(&k[0], "Bars", app.hdr[s].bars, ARR_MAXBARS);
        kn(&k[1], "Beats", app.hdr[s].beats, 7);
        kn(&k[2], "Place", ui.name_cur + 1, 8);     /* the name's letter: where, which */
        b[0] = app.hdr[s].name[ui.name_cur] ? app.hdr[s].name[ui.name_cur] : ' ';
        b[1] = 0;
        kv(&k[3], "Letter", b[0] == ' ' ? "_" : b, 0, 0);
        break;
    case P_TRACK:                                /* SOUND: its name (the number: on the page, small) */
        if ((t->chan & 15u) == 9u)
            kv(&k[0], "Sound", "Drum kit", 0, 0);
        else
            kv(&k[0], "Sound", GM_PROGRAM[t->program & 127u], t->program + 1, 128);
        if (app.lead) {                          /* a melody: as played over every key, always (no type, no range); its
                                                  * channel, the velocity its notes are recorded with */
            kn(&k[1], "Channel", (t->chan & 15) + 1, ARR_CHAN_MAX + 1);
            kn(&k[3], "Velocity", sel_lead()->vel, 127);
            break;
        }
        kv(&k[1], "Type", t->x.type == XT_PARALLEL && (t->flags & ARR_TF_ASPLAYED) ? "as played" : TYPE_NAME[t->x.type % NXT],
           t->x.type, NXT - 1);
        if ((t->chan & 15u) == 9u)               /* (drums: a sound on each key, no range) */
            break;
        note_name(t->x.lo, b);
        kv(&k[2], "Low", b, t->x.lo, 127);
        note_name(t->x.hi, b);
        kv(&k[3], "High", b, t->x.hi, 127);
        break;
    case P_TRACK2:
        if (app.lead || (t->chan & 15u) == 9u)
            ;                                    /* (a melody, drums: no high key) */
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
        kn(&k[1], "Level", proj.rev_level, 127);   /* (the reverb's: its knob is the one before) */
        kn(&k[2], "Chorus", proj.cho_type + 1, 4);
        kn(&k[3], "Level", proj.cho_level, 127);
        break;
    default: break;
    }
}
/* the other pages: STYLE, Setup; SONG and LOOP are names only until their screens come */
static void song_page_knobs(knob_t *k)
{
    const song_t *g = csong();
    char b[24];
    switch (ui.page) {
    case P_SONG: {                                   /* the cursor's section (the END row: none yet) */
        const row_t *w = app.row < g->nrows ? &g->row[app.row] : 0;
        if (app.row < g->nrows)
            kn(&k[0], "Section", app.row + 1, g->nrows < ARR_ROWS ? g->nrows + 1 : g->nrows);
        else
            kv(&k[0], "Section", "end", g->nrows + 1, g->nrows + 1);
        if (!w) {
            kv(&k[1], "Prog", "-", 0, 0);
            kv(&k[2], "Groove", "-", 0, 0);
            kv(&k[3], "Repeat", "-", 0, 0);
            break;
        }
        itoa_u((uint32_t)(w->loop % ARR_LOOPS) + 1u, cat_s(b, "P"));
        kv(&k[1], "Prog", b, w->loop % ARR_LOOPS, ARR_LOOPS - 1);
        kv(&k[2], "Groove", ROW_SECT_NAME[w->sect % ROW_NSECT], w->sect % ROW_NSECT, ROW_NSECT - 1);
        itoa_u(w->rep ? w->rep : 1u, cat_s(b, "x"));
        kv(&k[3], "Repeat", b, w->rep, 8);
        break;
    }
    case P_SONG2: {                                  /* the cursor section's own transposition (on top of the song's);
                                                      * the song's Intro, Ending, end */
        const row_t *w = app.row < g->nrows ? &g->row[app.row] : 0;
        if (!w)
            kv(&k[0], "Transpose", "-", 0, 0);
        else {
            b[0] = w->key > 0 ? '+' : '-';
            itoa_u((uint32_t)(w->key < 0 ? -w->key : w->key), b + 1);
            kv(&k[0], "Transpose", w->key ? b : "0", w->key + 6, 11);
        }
        kv(&k[1], "Intro", g->intro ? "on" : "off", g->intro, 0);
        kv(&k[2], "Ending", g->ending ? "on" : "off", g->ending, 0);
        kv(&k[3], "At end", g->at_end ? "again" : "stop", g->at_end, 0);
        break;
    }
    case P_LOOP: {                                   /* BAR, BEAT (the cursor: up to the end while REC writes), ROOT, TYPE */
        const loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
        int has, b = app.cbeat, bpb = l->beats ? l->beats : 4, len = loop_len(l);
        int m = ui.rec_chords ? len : len > 0 ? len - 1 : 0;
        chord_t c = chord_shift(cursor_chord(l, b, &has), xpose_now());
        kn(&k[0], "Bar", b / bpb + 1, m / bpb + 1);
        kn(&k[1], "Beat", b % bpb + 1, bpb);
        kv(&k[2], "Root", KEY_NAME[c.root % 12u], c.root, 11);
        kv(&k[3], "Type", !has ? "-" : c.type == CH_MAJ ? "maj" : chord_type_name(c.type), has ? c.type : 0, CH_DIM);
        break;
    }
    case P_LOOP2: {                                  /* LENGTH, BEATS, BASS (the cursor's chord), NAME */
        const loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
        int bpb = l->beats ? l->beats : 4, has;
        chord_t c = chord_shift(cursor_chord(l, app.cbeat, &has), xpose_now());
        char v[9];
        kn(&k[0], "Length", l->bars ? l->bars : 1, loop_max_bars(bpb));
        kn(&k[1], "Beats", bpb, 7);
        if (c.bass == CH_NOBASS)
            kv(&k[2], "Bass", "-", 0, 11);
        else
            kv(&k[2], "Bass", KEY_NAME[c.bass % 12u], c.bass, 11);
        {
            int n = 0;
            for (int i = 0; i < 8 && l->name[i]; i++)
                v[n++] = l->name[i];
            v[n] = 0;
        }
        if (loop_name_default(l, app.loop % ARR_LOOPS))   /* its own default name: its number, as everywhere ("P9") */
            itoa_u(app.loop % ARR_LOOPS + 1u, cat_s(v, "P"));
        kv(&k[3], "Name", v, loop_name_index(l), NLOOP_NAMES);
        break;
    }
    case P_STYLE: case P_STYLE2: {
        int first = ui.page == P_STYLE2 ? 3 : 0;
        for (int i = 0; i < 3; i++) {
            int st = g->style[first + i], si = style_index(st);
            char y[24];
            /* a style's knob: its name (the label is the section); a user pattern's: "Funk (yours)", as its row */
            if (st < ARR_USER_PATTERNS) {
                pat_title(first + i, y);
                b[0] = 0;
                for (int c = 0; c < 23 && y[c]; c++)
                    b[c] = y[c], b[c + 1] = 0;
            } else
                style_name(st, b);                   /* (a QY70 style with no bank: "QY -") */
            kv(&k[i], SECT_NAME[first + i + 1], si < 0 ? b : style_label(si), si < 0 ? st : ARR_USER_PATTERNS + si,
               ARR_USER_PATTERNS + styles_n() - 1);
        }
        style_knob(&k[3]);                       /* all six from one style */
        break;
    }
    case P_SETUP:
        kv(&k[0], "Clock", proj.clock ? "MIDI" : "internal", proj.clock, 1);
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

static int knobs_shown(void)                         /* the strip has a knob in it */
{
    knob_t k[4];
    page_knobs(k);
    return k[0].name || k[1].name || k[2].name || k[3].name;
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
    if (!(k[0].name || k[1].name || k[2].name || k[3].name)) {   /* a page with no knobs (Status): no strip */
        cv_blit(0, KNB_Y);
        return;
    }
    cv_round(4, 0, 232, KNB_H - 2, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int32_t x = (int32_t)i * 60, cx = x + 30;
        int hot = now_touch && ui.touched == (int)i;
        if (i)
            cv_rect(x, 10, 1, KNB_H - 22, K_LINE);
        if (!k[i].name)
            continue;
        text_c(cx, 1, &FONT_XS, k[i].name, hot ? K_TEXT : K_DIM);
        if (upper_only(k[i].val) && text_w(&FONT_M, k[i].val) <= 56)
            text_c(cx, 14, &FONT_M, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_B, k[i].val) <= 54)   /* FONT_M has no lower case: "Bb", "off" */
            text_c(cx, 19, &FONT_B, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_XS, k[i].val) <= 54)  /* a long name: "Andalusian", "8BtRock1" */
            text_c(cx, 20, &FONT_XS, k[i].val, hot ? K_ACCENT : K_TEXT);
        else {                                       /* longer: two lines, at a space ("Acoustic / Grand") */
            char l1[24];
            uint32_t n, sp = 0;
            for (n = 0; k[i].val[n] && n < sizeof l1 - 1u; n++)
                if ((l1[n] = k[i].val[n]) == ' ') {
                    l1[n] = 0;
                    if (text_w(&FONT_XS, l1) <= 54)
                        sp = n;
                    l1[n] = ' ';
                }
            l1[n] = 0;
            if (sp) {
                l1[sp] = 0;
                text_c(cx, 13, &FONT_XS, l1, hot ? K_ACCENT : K_TEXT);
                if (text_w(&FONT_XS, l1 + sp + 1) <= 54)
                    text_c(cx, 27, &FONT_XS, l1 + sp + 1, hot ? K_ACCENT : K_TEXT);
                else
                    text_fit(x + 3, 27, &FONT_XS, l1 + sp + 1, 54, hot ? K_ACCENT : K_TEXT);
            } else
                text_fit(x + 3, 20, &FONT_XS, k[i].val, 54, hot ? K_ACCENT : K_TEXT);
        }
        if (k[i].max > 0) {
            int32_t bw = 44 * k[i].num / k[i].max;
            cv_round(cx - 22, 43, 44, 6, 3, K_LINE);
            if (bw > 0)
                cv_round(cx - 22, 43, bw < 6 ? 6 : bw, 6, 3, hot ? K_ACCENT : K_DIM);
        }
    }
    cv_blit(0, KNB_Y);
}


/* ------------------------------------------------- tag, footer, cards --- */
/* text in parts: "KEYS: CHORDS|REC: WRITE" draws a dot between the parts (the font has no middle dot); the tag's
 * breadcrumb "S1>Pop>Groove A>C1 Piano" a small chevron (a ">" drawn as the font has no "›") */
#define SEP_W 13
static int32_t segs_w(const felucca_font_t *f, const char *s)
{
    char b[48];
    int32_t w = 0;
    uint32_t n = 0;
    for (;; s++) {
        if (*s == '|' || *s == '>' || !*s) {
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
        if (*s == '|' || *s == '>' || !*s) {
            b[n] = 0;
            x = cv_text(x, y, f, b, c);
            n = 0;
            if (!*s)
                return x;
            if (*s == '>')                           /* the chevron: 4 px wide, 7 high, on the dot's line */
                for (int j = 0; j < 7; j++)
                    cv_rect(x + 4 + (j < 4 ? j : 6 - j), y + 6 + j, 2, 1, K_DIM);
            else
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
/* the tag over the knobs: where you are, a breadcrumb from the song down ('>' a chevron on screen; the spec's
 * hierarchy: song > style > groove > instrument; song > progression; song > section; song > melody). Shortened from the
 * left when long (draw_tag) */
static char *crumb_song(char *t) { return itoa_u(proj.song + 1u, cat_s(t, "S")); }
/* "S1>Pop>Groove A>C1 Acoustic Grand", "S1>Pop>Groove A (yours)>C1 ..." (the section playing a user pattern: its name
 * first, the song's style when it has none), "S1>M1 Square Lead" (a melody: the song's, no style) */
static void track_crumb(char *t)
{
    arr_trk_t lb;
    int s = pslot(), st = csong()->style[s];
    char *e = cat_s(crumb_song(t), ">");
    if (!app.lead) {
        if (st < ARR_USER_PATTERNS) {
            char *n = pat_name8(app.hdr[s].name, e);
            if (n == e)
                n = cat_s(e, song_style() < 0 ? "-" : style_label(song_style()));
            e = cat_s(cat_s(cat_s(n, ">"), SECT_NAME[s + 1]), " (yours)>");
        } else {
            style_name(st, e);
            e = cat_s(cat_s(e + slen(e), ">"), SECT_NAME[s + 1]);
            e = cat_s(e, ">");
        }
    }
    cat_s(cat_s(cat_s(e, TRACK_NAME[sel_trk()]), " "), trk_sound(sel_arr(&lb)));
}
static void page_tag(char *t)
{
    char *e;
    *t = 0;
    switch (ui.page) {
    case P_HOME:                                     /* "S1>P3 Verse", "S1" (the song: its place is HOME's to say),
                                                      * "S1>free", "S1>new P9" */
        e = crumb_song(t);
        if (ui.take)                                 /* a new loop's take (not FREE) */
            itoa_u(ui.take_loop + 1u, cat_s(e, ">new P"));
        else if (app.mode == ARR_LOOP)
            loop_label(app.loop, cat_s(e, ">"));
        else if (app.mode == ARR_FREE)
            cat_s(e, ">free");
        break;
    case P_SONG: case P_SONG2: case P_SONG3:         /* "S1" (the cursor's section: by the cursor) */
        crumb_song(t);
        break;
    case P_LOOP: case P_LOOP2:                       /* "S1>P3 Verse" (the cursor: on the grid, its bar and beat lit) */
        loop_label(app.loop, cat_s(crumb_song(t), ">"));
        break;
    case P_PATTERN: case P_PATSET: case P_TRACK: case P_TRACK2: case P_MIXER: case P_TONE:
        track_crumb(t);
        break;
    case P_MASTER:                                   /* (the effects every song shares) */
        cat_s(t, "all songs>Effects");
        break;
    case P_STYLE: case P_STYLE2:                     /* "S1>Pop" */
        e = cat_s(crumb_song(t), ">");
        if (song_style() < 0)
            cat_s(e, "QY -");
        else
            style_name(style_number(song_style(), 0), e);
        break;
    case P_SETUP:
        cat_s(t, "all songs>Setup");
        break;
    default:
        cat_s(t, "all songs>Status");
        break;
    }
}
/* the tag drawn, a rule before it: a long one loses its first crumbs ("S1>", then the style), then letters at its end */
static void draw_tag(void)
{
    char t[80], *v = t;
    int g = page_group(ui.page), n = group_n(g), d = ui.page - PAGE_BTN[g].first;
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
            cv_round(r + i * 9, 3, 6, 6, 3, i == d ? K_TEXT : K_LINE);
        r -= 8;
    }
    while ((w = segs_w(&FONT_XS, v)) > r - 16 && *v) {
        char *c = v;
        while (*c && *c != '>')
            c++;
        if (*c)
            v = c + 1;                               /* the first crumb goes */
        else
            v[slen(v) - 1] = 0;                      /* (one left: a long sound's name cut) */
    }
    x = r - w;
    segs(x, -2, &FONT_XS, v, K_TEXT);
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
    if (home_shown()) {                              /* (the keys: white a progression, black a section; HOME + REC, + PLAY) */
        cat_s(t, "REC: UNDO|PLAY: REDO");           /* (the keys: on screen, the progressions and the sections; SELECT,
                                                      * PRESETS: the encoders' line) */
        return;
    }
    if (ui.card) {
        cat_s(t, "LET GO: NOTHING CHANGES");
        return;
    }
    if (!ui.any_btn && ui.page == P_HOME && app.mode == ARR_FREE && !ui.take) {   /* the first screen: REC's job, and
                                                      * where the rest is said (its main band: "Play a chord") */
        cat_s(t, "REC: NEW PROG|HOLD A BUTTON: HELP");
        return;
    }
    if (!lst.open && plat_ms() < ui.slots_until) {   /* MEMORY FULL said: where to see what fills it */
        cat_s(t, "MEMORY FULL|SEL: MEMORY, FREE ONE");
        return;
    }
    if (lst.open && lst.n) {                         /* (a list: the encoders' line is the list's; all said here) */
        int k = lst.row[lst.sel].kind;
        cat_s(t, k == LK_NAME ? "ALG: LETTER|SEL: NEXT|HOME: BACK"
               : k == LK_ACTION ? "SELECT: MOVE|SEL: DO IT|HOME: BACK"
               : k == LK_PICK && lst.row[lst.sel].id == A_STEP ? "SELECT: MOVE|ALG: CHANGE|HOME: BACK"
               : k == LK_PICK ? "ALG: CHANGE|SEL: DO IT|HOME: BACK"
               : k == LK_SUB ? "SELECT: MOVE|SEL: OPEN|HOME: BACK"
               : k == LK_SLOT ? (slot_kind[lst.row[lst.sel].id] == SLOT_NONE ? "SELECT: MOVE|HOME: BACK"
                                                                              : "SELECT: MOVE|SEL: FREE|HOME: BACK")
               : "SELECT: MOVE|ALG: CHANGE|HOME: BACK");
        return;
    }
    switch (ui.page) {                               /* (recording: REC stops it, as it says) */
    case P_HOME:
        cat_s(t, ui.take || ui.rec_chords ? "KEYS: CHORDS|REC: STOP"
                 : app.mode == ARR_FREE ? "KEYS: CHORDS|REC: NEW PROGRESSION" : "KEYS: CHORDS|REC: WRITE");
        break;
    case P_LOOP: case P_LOOP2:
        cat_s(t, ui.rec_chords ? "KEYS: CHORDS|REC: STOP|OCT: CURSOR" : "KEYS: CHORDS|REC: WRITE|OCT: CURSOR");
        break;
    case P_SONG: case P_SONG2: case P_SONG3:
        cat_s(t, ui.song_rec ? "HOME + KEYS: SECTIONS|REC: STOP" : "WHITE: PROGRESSION|BLACK: MUTES");
        break;
    case P_PATTERN: case P_PATSET: case P_TRACK: case P_TRACK2:   /* "KEYS: C1|REC: RECORD" (its sound: on the page) */
        if (trk_rec_shown() && !app.lead && loop_longer()) {   /* REC's screen: SEL makes it as long as the loop */
            itoa_u((uint32_t)loop_longer(), cat_s(t, "SEL: "));
            cat_s(t + slen(t), " BARS, AS THE PROG|REC: STOP");
            break;
        }
        if (ui.follow_hint) {                        /* REC kept the track's type (it has notes): how to change that */
            cat_s(cat_s(cat_s(t, "CLEAR "), TRACK_NAME[sel_trk()]), " (SEL) TO PLAY AS PLAYED");
            break;
        }
        cat_s(cat_s(cat_s(t, "KEYS: "), TRACK_NAME[sel_trk()]), arr_recording || ui.rec_want ? "|REC: STOP" : "|REC: RECORD");
        break;
    case P_MIXER: case P_TONE:
        cat_s(t, "BLACK: INSTRUMENT");
        break;
    default:                                         /* (Effects, Style, Setup, Status: the keys do nothing) */
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

/* the encoders' line, over the footer: what turning SELECT, ALGORITHM, PRESETS changes here, as input() and
 * presets() do it ("SELECT page|ALG instrument|PRE family": a part's first word the encoder, the rest what it changes;
 * "|" between parts; where you are is the tag's and the dots' job, not this line's). A part is left out where its
 * encoder does nothing, and ALG where no instrument is on screen (it still picks one: the pages that show it say so);
 * a question, a list: none (the footer says what SELECT, ALGORITHM, SEL, HOME do there) */
static void enc_line(char *t)
{
    int g = page_group(ui.page), n = group_n(g), held = home_shown(), p = ui.page;
    arr_trk_t lb;
    char *e = t;
    *t = 0;
    if (ui.ask || (lst.open && lst.n))
        return;
    if (held) {                                      /* HOME held: the tempo, the song */
        cat_s(t, "SELECT tempo|PRE song");
        return;
    }
    if (n > 1)                                       /* the button's pages */
        e = cat_s(t, "SELECT page");
    if (p >= P_PATTERN && p <= P_TONE)               /* the instrument (or melody) the keys and the page show */
        e = cat_s(e, *t ? "|ALG instrument" : "ALG instrument");
    {
        const char *a = p == P_HOME || p == P_LOOP || p == P_LOOP2 ? "prog"
                        : song_pg(p) ? "section"
                        : p == P_PATTERN || p == P_PATSET ? "groove"
                        : p == P_STYLE || p == P_STYLE2 ? "style"
                        : p == P_TRACK && (sel_arr(&lb)->chan & 15u) != 9u ? "family"   /* (drums: none) */
                        : 0;
        if (a)
            cat_s(cat_s(e, *t ? "|PRE " : "PRE "), a);
    }
}
/* the line drawn: each part's encoder dim, its job in the text's colour, the parts spread over the width (a dot
 * between them) */
static void draw_enc(void)
{
    char t[64], *v[3];
    int32_t w[3], tw = 0, gap, x = 8;
    int n = 0;
    uint32_t h = 2166136261u;
    enc_line(t);
    for (const char *c = t; *c; c++)
        h = hash(h, (uint8_t)*c);
    if (h == ui.sig[5])
        return;
    ui.sig[5] = h;
    for (char *c = t; *c && n < 3;) {                /* the parts */
        v[n] = c;
        while (*c && *c != '|')
            c++;
        if (*c)
            *c++ = 0;
        tw += w[n] = text_w(&FONT_XS, v[n]);
        n++;
    }
    gap = n > 1 ? (224 - tw) / (n - 1) : 0;
    if (gap > 28)
        gap = 28;
#if defined(GM_HOST) && !defined(GM_WEB)        /* the simulator: a line cut, or crowded, is a bug */
    if (n > 1 && gap < 10) {
        printf("  FAIL encoder line too wide: %s | %s | %s (%d px)\n", v[0], n > 1 ? v[1] : "", n > 2 ? v[2] : "", (int)tw);
        msg_too_wide++;
    }
#endif
    cv_begin(240, ENC_H, K_BG);
    for (int i = 0; i < n; i++) {
        char *sp = v[i];
        while (*sp && *sp != ' ')
            sp++;
        if (*sp)
            *sp++ = 0;
        x = cv_text(x, -2, &FONT_XS, v[i], K_DIM);
        if (*sp)
            x = cv_text(x + 3, -2, &FONT_XS, sp, K_TEXT);
        if (i < n - 1) {
            cv_round(x + gap / 2 - 1, 5, 3, 3, 1, K_DIM);
            x += gap;
        }
    }
    cv_blit(0, ENC_Y);
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
    {B_ARP, PG_ANY, "Progression", "Your chords, 1-16 bars, going round. Play chords.",
     "REC writes them: at the cursor (BAR, BEAT), or in time."},
    {B_SEQ, PG_ANY, "Band", "The groove's notes, instrument by instrument; BAR, BEAT: the cursor.",
     "SELECT: its length and name."},
    {B_EDIT, PG_ANY & ~CF_NOLEAD, "Melody", "Its sound, channel and velocity; its notes in the section.",
     "SEL: QUANTISE. ALGORITHM: the instruments."},
    {B_EDIT, PG_ANY, "Sound", "The instrument's sound and range; SELECT: high key, channel.",
     "ALGORITHM past C4: the melodies M1, M2."},
    {B_LFO, PG_ANY, "Mixer", "Volume, pan and sends; SELECT: the tone. Black keys pick the instrument.", 0},
    {B_FX, PG_ANY, "Effects", "The reverb and the chorus every instrument sends to.", 0},
    {B_ENV, PG_ANY, "Style", "Which style plays the grooves, the intro, fills and ending. PRESETS: the next style.", 0},
    {B_GLO, PG_ANY, "Setup", "Clock, MIDI out, lights, the click (off, rec, on); SELECT: the status.", 0},
    {B_SEL, PG_TRACK, "Lists", "This page as a list; MEMORY: what fills the 16 places, FREE one.",
     "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SEL, PG_ANY, "Lists", "The page's knobs, then what you can do here: SEL does it.",
     "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SAVE, PG_SONG, "Save", "Tap: save.", "Nothing to copy here: a white key on its own adds a section."},
    {B_SAVE, PG_ANY, "Save", "Tap: save. Hold + a white key: copy here.", 0},
    {B_REC, PG(P_HOME) | PG(P_LOOP) | PG(P_LOOP2), "Write chords",
     "Into the progression playing: at the cursor, or in time as it plays.",
     "On HOME in free: a new progression. HOME + REC: undo."},
    {B_REC, PG_TRACK | CF_NOLEAD, "Record the instrument", "What the keys play goes into the selected instrument.",
     "HOME + REC: undo (a pass at a time)."},
    {B_REC, PG_TRACK | CF_LEAD, "Record the melody",
     "While the song plays, what the keys play goes into the section playing.",
     "PLAY here starts the song; SEL: PLAY FROM SECTION on Song."},
    {B_REC, PG_SONG, "Record the sections", "With PLAY: the progressions and grooves you pick with HOME + keys,",
     "and the mutes you change. HOME + REC: undo."},
    {B_REC, PG_ANY, "Record", "Nothing to record on this page.", "HOME + REC: undo."},
    {B_PLAY, PG_ANY, "Play", "Start and stop. On Song: the song from section 1 (from a progression: at the next bar).",
     "HOME + PLAY: redo."},
    {B_OCTDN, PG(P_LOOP) | PG(P_LOOP2), "Cursor", "Moves the cursor a beat back (OCT+ a beat on); the chords stay as they are.",
     "BAR and BEAT go straight there."},
    {B_OCTUP, PG(P_LOOP) | PG(P_LOOP2), "Cursor", "Moves the cursor a beat on (OCT- a beat back); the chords stay as they are.",
     "BAR and BEAT go straight there."},
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
    x = pill_o(18, 14, 0, BTN_LABEL[(ui.card - 1) % NB], K_TEXT, K_TEXT);   /* (the button: an ink outline) */
    if (i >= 0) {
        cv_text(x + 10, 15, &FONT_B, CARD[i].title, K_TEXT);
        y = wrap(20, 44, 200, &FONT_S, CARD[i].l1, K_TEXT);
        if (CARD[i].l2)
            y = wrap(20, y + 4, 200, &FONT_S, CARD[i].l2, K_DIM);
#if defined(GM_HOST) && !defined(GM_WEB)        /* the simulator: a card's words past its box are a bug */
        if (y - 1 > MAIN_H - 4) {                   /* (y: past the last line's 17 px; the box ends at MAIN_H - 4) */
            printf("  FAIL card too long: %s\n", CARD[i].title);
            msg_too_wide++;
        }
#endif
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
        for (int i = 0; i < 23 && val[i]; i++)
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
    int keep = ui.page, g = page_group(keep), f = PAGE_BTN[g].first, n = group_n(g);
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
    else if (song_pg(keep))                          /* SONG: its two pages */
        f = P_SONG, n = 2;
    for (int p = f; p < f + n; p++) {
        ui.page = (uint8_t)p;
        page_knobs(k);
        if (p == P_STYLE) {                          /* Style: the style first, then the sections in order */
            list_add(k[3].name, k[3].val, LK_KNOB, p << 2 | 3);
            k[3].name = 0;
        }
        if (p == P_PATSET && nm)                     /* (Place, Letter: the list's Letters row does both) */
            k[2].name = k[3].name = 0;
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
        if (keep != P_HOME) {                        /* Progression: the cursor's chord cleared (first: the list opens
                                                      * on it), the keys' step */
            list_add(ACTION_NAME[A_CLEAR_CHORD], 0, LK_ACTION, A_CLEAR_CHORD);
            list_add(ACTION_NAME[A_STEP], STEP_NAME[app.cstep % 3u], LK_PICK, A_STEP);
        }
        list_add(ACTION_NAME[A_LOAD_PROG], TEMPLATES[ui.pick_prog % NTEMPLATES].name, LK_PICK, A_LOAD_PROG);
        if (keep != P_HOME || app.mode == ARR_LOOP)  /* (HOME free or in the song: no progression of its own to clear) */
            list_add(ACTION_NAME[A_CLEAR_LOOP], 0, LK_ACTION, A_CLEAR_LOOP);
    } else if (song_pg(keep)) {
        char v[8];
        if (ui.pick_song % ARR_SONGS == proj.song)    /* COPY SONG TO: another song */
            ui.pick_song = (uint8_t)((proj.song + 1u) % ARR_SONGS);
        static char from[24];                        /* "PLAY FROM SECTION 3": the cursor's row */
        char r[4];
        itoa_u(ui.pick_song % ARR_SONGS + 1u, cat_s(v, "S"));
        itoa_u(app.row + 1u, r);
        cat_s(cat_s(cat_s(from, ACTION_NAME[A_PLAY_FROM]), " "), r);
        list_add(from, r, LK_ACTION, A_PLAY_FROM);
        list_add(ACTION_NAME[A_INSERT_ROW], 0, LK_ACTION, A_INSERT_ROW);
        list_add(ACTION_NAME[A_DELETE_ROW], 0, LK_ACTION, A_DELETE_ROW);
        list_add(ACTION_NAME[A_CLEAR_SONG], 0, LK_ACTION, A_CLEAR_SONG);
        list_add(ACTION_NAME[A_COPY_SONG], v, LK_PICK, A_COPY_SONG);
    } else if (keep >= P_PATTERN && keep <= P_TONE) {   /* (Effects: every song's, no instrument: no actions) */
        if (app.lead) {                              /* a melody: its notes in a section ("the cursor's / playing"), all;
                                                      * a section's notes copied into it (the groove's actions: not the
                                                      * melody's) */
            static char er[28], ea[16], qr[28], cp[28];
            int lr = lead_row(), nr = csong()->nrows;
            char *e = cat_s(cat_s(cat_s(er, "CLEAR "), TRACK_NAME[sel_trk()]), " IN SECTION ");
            itoa_u((uint32_t)lr + 1u, e);
            cat_s(cat_s(ea, "CLEAR "), TRACK_NAME[sel_trk()]);
            e = cat_s(cat_s(cat_s(qr, "QUANTISE "), TRACK_NAME[sel_trk()]), " IN SECTION ");   /* (the grid at the right) */
            itoa_u((uint32_t)lr + 1u, e);
            list_add(er, 0, LK_ACTION, A_ERASE_LEAD_ROW);
            list_add(ea, 0, LK_ACTION, A_ERASE_LEAD);
            list_add(qr, GRID_NAME[proj.quant % ARR_NQ], LK_ACTION, A_QUANT_LEAD_ROW);
            if (nr > 1) {                            /* COPY M1 TO SECTION 3, from (ALGORITHM) another section */
                char v[12];
                if (ui.pick_lrow >= nr || ui.pick_lrow == lr)
                    ui.pick_lrow = (uint8_t)(lr ? lr - 1 : 1);
                e = cat_s(cat_s(cat_s(cp, "COPY "), TRACK_NAME[sel_trk()]), " TO SECTION ");
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
            static char cl[20], ct[12];              /* "CLEAR GROOVE A": the section playing's notes; "CLEAR C1": the
                                                      * instrument's in it */
            cat_s(cat_s(cat_s(cl, ACTION_NAME[A_CLEAR_PATTERN]), " "), SECT_NAME[pslot() + 1]);
            upcase(cl);
            cat_s(cat_s(ct, "CLEAR "), TRACK_NAME[sel_trk()]);
            list_add(ct, 0, LK_ACTION, A_ERASE_TRACK);
            list_add(cl, 0, LK_ACTION, A_CLEAR_PATTERN);
            list_add(ACTION_NAME[A_BACK_TO_STYLE], 0, LK_ACTION, A_BACK_TO_STYLE);
        }
        if (keep <= P_TRACK2)                        /* SEQ, EDIT: what fills the 16 places of memory */
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
#define LIST_Y 24                                    /* the list covers the main band, the tag, the knobs and the encoders'
                                                      * line: LIST_SHOWN rows from here to the footer */
#define LIST_H (FOOT_Y - LIST_Y)
/* the list's rows, drawn oy px down (it is drawn in two canvases: the first MAIN_H rows, then the rest) */
static void list_paint(int32_t oy)
{
    uint32_t i;
    for (i = lst.top; i < lst.n && i < lst.top + LIST_SHOWN; i++) {
        int32_t y = 1 + (int32_t)(i - lst.top) * LIST_RH - oy;
        int on = i == lst.sel, k = lst.row[i].kind;
        uint16_t fg = on ? K_BG : K_TEXT, dim = on ? K_LINE : K_DIM;
        const char *v = lst.row[i].val;
        if (k == LK_MORE) {
            int32_t x = cv_text(14, y + 1, &FONT_XS, "MORE", K_DIM);
            cv_rect(x + 6, y + 9, 222 - x, 1, K_LINE);
            continue;
        }
        cv_round(6, y, 222, LIST_RH - 1, 5, on ? K_TEXT : K_PANEL);   /* (the selection: ink, light text) */
        if (k == LK_SLOT) {                          /* "3  S1|Groove A (yours)|Funk", "S2|melodies 1/2", "S1|progressions",
                                                      * "free" (dim): its number (SAVE + white key 3 copies a groove
                                                      * there), what it holds (a long one smaller) */
            int id = lst.row[i].id;
            char nb[4];
            itoa_u((uint32_t)id + 1u, nb);
            text_r(30, y + 1, &FONT_XS, nb, dim);
            if (segs_w(&FONT_B, lst.row[i].name) <= 200 - 36)
                segs(36, y, &FONT_B, lst.row[i].name, slot_kind[id] == SLOT_NONE ? dim : fg);
            else
                segs(36, y + 1, &FONT_S, lst.row[i].name, slot_kind[id] == SLOT_NONE ? dim : fg);
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
            int32_t ne = cv_text(14, y, &FONT_B, lst.row[i].name, fg);
            if (k == LK_SUB)                         /* another list */
                glyph_pick(213, y + 4, fg);
            else if (k == LK_PICK) {
                int32_t room = 206 - (ne + 6), w = text_w(&FONT_S, v);
                text_fit(206 - (w < room ? w : room), y, &FONT_S, v, room, fg);
                glyph_pick(213, y + 4, fg);
            } else {
                if (*v && 202 - text_w(&FONT_S, v) >= ne + 6)   /* (QUANTISE: the grid, when there is room) */
                    cv_text(202 - text_w(&FONT_S, v), y, &FONT_S, v, dim);
                glyph_enter(208, y + 5, fg);
            }
        }
    }
    if (lst.n > LIST_SHOWN) {                        /* where in the list */
        int32_t th = LIST_SHOWN * LIST_RH - 2, bh = th * LIST_SHOWN / lst.n, by = th * lst.top / lst.n;
        cv_round(232, 1 - oy, 4, th, 2, K_PANEL);
        cv_round(232, 1 + by - oy, 4, bh, 2, K_DIM);
    }
}
static void draw_list(void)
{
    uint32_t h = hash(2166136261u, 0x4C000u | lst.sel | (uint32_t)lst.top << 8 | (uint32_t)ui.name_cur << 16), i;
    list_build();
    h = hash(h, lst.n);
    for (i = 0; i < lst.n; i++) {
        h = hash(h, lst.row[i].kind | (uint32_t)lst.row[i].id << 4);
        for (const char *c = lst.row[i].name; *c; c++)   /* ("CLEAR M1 IN SECTION 3": the row in the name) */
            h = hash(h, (uint8_t)*c);
        for (const char *c = lst.row[i].val; *c; c++)
            h = hash(h, (uint8_t)*c);
    }
    ui.sig[2] = ui.sig[3] = ui.sig[5] = 0;           /* (the bands it covers: drawn again when it closes) */
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    list_paint(0);
    cv_blit(0, LIST_Y);
    cv_begin(240, LIST_H - MAIN_H, K_BG);
    list_paint(MAIN_H);
    cv_blit(0, LIST_Y + MAIN_H);
}

/* ------------------------------------------------------- the flashes --- */
static void flash_add(uint64_t beats, uint32_t rows, uint32_t bars)
{
    if (!(beats | rows | bars))
        return;
    if (!flash_on())
        fw.beats = 0, fw.rows = fw.bars = 0;
    fw.beats |= beats;
    fw.rows |= rows;
    fw.bars |= bars;
    fw.t = plat_ms() | 1u;
}
static uint32_t bytes_hash(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = hash(h, *b++);
    return h;
}
/* the section a melody's view shows: the one playing, else SONG's cursor's (draw_lead_rec's) */
static int lead_shown_row(void)
{
    int n = csong()->nrows > ARR_ROWS ? ARR_ROWS : csong()->nrows;
    if (!n)
        return 0;
    return arr_playing ? (arr_row < n ? arr_row : n - 1) : lead_row();
}
static int lead_view_shown(void) { return app.lead && (ui.page == P_TRACK || trk_rec_shown()); }
/* a frame: what the page shows, hashed; what changed flashes (the first look, or another thing: only hashed) */
static void flash_watch(void)
{
    const song_t *g = csong();
    uint32_t id, key, h[ARR_ROWS + 1], bh[32], rows = 0, bars = 0;
    uint64_t beats = 0;
    int i, n = 0, nb = 0, home = ui.page == P_HOME && (ui.take || ui.rec_chords);
    if (ui.page == P_LOOP || ui.page == P_LOOP2 || home) {   /* a progression's beats (HOME recording chords: the
                                                      * one its bars show, draw_home_rec's) */
        place_t pl;
        int li = app.loop % ARR_LOOPS, len;
        const loop_t *l;
        if (home) {
            home_place(&pl);
            li = ui.take ? ui.take_loop % ARR_LOOPS : arr_playing && pl.loop >= 0 ? pl.loop : li;
        }
        l = &sloops.loop[li];
        len = loop_len(l);
        id = 1u | (uint32_t)proj.song << 4 | (uint32_t)li << 8;
        if (fw.ok && fw.id == id)
            for (i = 0; i < ARR_LOOP_BEATS; i++)
                if ((i < len ? l->ch[i] : LC_SAME) != fw.ch[i])
                    beats |= (uint64_t)1 << i;
        for (i = 0; i < ARR_LOOP_BEATS; i++)
            fw.ch[i] = i < len ? l->ch[i] : LC_SAME;
        fw.id = id;
        fw.ok = 1;
        flash_add(beats, 0, 0);
        return;
    }
    if (song_page()) {                               /* the song's sections */
        n = g->nrows > ARR_ROWS ? ARR_ROWS : g->nrows;
        id = 2u | (uint32_t)proj.song << 4;
        for (i = 0; i <= ARR_ROWS; i++)
            h[i] = i < n ? bytes_hash(2166136261u, &g->row[i], sizeof(row_t)) : 0u;
        key = hash(bytes_hash(2166136261u, h, sizeof h), (uint32_t)n);
        if (fw.ok && fw.id == id && fw.key != key) {
            int f = 0;
            while (f < ARR_ROWS && h[f] == fw.h[f])
                f++;
            int moved = 0;
            if (n == fw.n + 1) {                     /* a section in: only it (those after it moved down) */
                moved = 1;
                for (i = f + 1; i < n && moved; i++)
                    moved = h[i] == fw.h[i - 1];
            } else if (n + 1 == fw.n) {              /* one out: the one in its place now (or the one before) */
                moved = 1;
                for (i = f; i < n && moved; i++)
                    moved = h[i] == fw.h[i + 1];
            }
            if (moved)
                rows = f < n ? 1u << f : f ? 1u << (f - 1) : 0u;
            else
                for (i = f; i < ARR_ROWS; i++)
                    rows |= h[i] != fw.h[i] ? 1u << i : 0u;
        }
        memcpy(fw.h, h, sizeof h);
        fw.n = (uint8_t)n;
        fw.id = id;
        fw.key = key;
        fw.ok = 1;
        flash_add(0, rows, 0);
        return;
    }
    if (!app.lead && (ui.page == P_PATTERN || trk_rec_shown())) {   /* a groove: its instruments, the selected one's bars */
        int s = pslot(), trk = proj.trk & 7;
        uint32_t blen;
        view_refresh(s);
        id = 3u | (uint32_t)proj.song << 4 | (uint32_t)s << 8 | (uint32_t)trk << 12;   /* (another groove picked: its
                                                      * instruments that differ flash) */
        key = hash(hash(hash(2166136261u, vw.dirty), vw.gen), (app.mode == ARR_SONG ? arr_mutes : g->mute) & 0xFFu);
        if (fw.ok && fw.id == id && fw.key == key)
            return;
        nb = app.hdr[s].bars < 1 ? 1 : app.hdr[s].bars > 16 ? 16 : app.hdr[s].bars;
        blen = (uint32_t)(app.hdr[s].beats ? app.hdr[s].beats : 4) * ARR_PPQ;
        for (i = 0; i < 32; i++)
            bh[i] = 2166136261u;
        for (i = 0; i < ARR_TRACKS; i++) {
            const arr_trk_t *tr = &view.trk[i];
            uint32_t e = (uint32_t)tr->first + tr->count, k, hh = hash(2166136261u, (uint32_t)trk_muted(i));
            if (e > view.nev)
                e = view.nev;
            for (k = tr->first; k < e; k++) {
                const arr_ev_t *v = &view.ev[k];
                uint32_t x = hash(hash(v->tick, v->note), v->len);
                hh = hash(hh, x);
                if (i == trk && v->tick / blen < 32u)
                    bh[v->tick / blen] = hash(bh[v->tick / blen], x);
            }
            h[i] = hh;
        }
        if (fw.ok && fw.id == id) {
            for (i = 0; i < ARR_TRACKS; i++)
                rows |= h[i] != fw.h[i] ? 1u << i : 0u;
            for (i = 0; i < nb; i++)
                bars |= bh[i] != fw.bh[i] ? 1u << i : 0u;
        }
        memcpy(fw.h, h, ARR_TRACKS * sizeof h[0]);
        memcpy(fw.bh, bh, sizeof bh);
        fw.id = id;
        fw.key = key;
        fw.ok = 1;
        flash_add(0, rows, bars);
        return;
    }
    if (lead_view_shown()) {                         /* a melody: its sections, its section's bars */
        int k = (app.lead - 1) % ARR_LEADS, r = lead_shown_row(), bpb = app.hdr[pslot()].beats;
        uint32_t blen, j;
        n = g->nrows > ARR_ROWS ? ARR_ROWS : g->nrows;
        if (bpb < 2 || bpb > 7)
            bpb = 4;
        blen = (uint32_t)bpb * ARR_PPQ;
        id = 4u | (uint32_t)proj.song << 4 | (uint32_t)k << 8 | (uint32_t)r << 12 | (uint32_t)bpb << 20;
        key = hash(leads.gen, leads.n | (uint32_t)n << 16);
        if (fw.ok && fw.id == id && fw.key == key)
            return;
        for (i = 0; i < 32; i++)
            bh[i] = 2166136261u;
        for (i = 0; i < n; i++) {
            uint32_t hh = 2166136261u;
            for (j = leads.row0[i]; j < leads.row0[i + 1] && j < leads.n; j++) {
                const lead_ev_t *e = lead_at_c(&leads, j);
                uint32_t x;
                if (lead_trk(e) != k)
                    continue;
                x = hash(hash(lead_pos(e), e->note), lead_len(e));
                hh = hash(hh, x);
                if (i == r && lead_pos(e) / blen < 32u)
                    bh[lead_pos(e) / blen] = hash(bh[lead_pos(e) / blen], x);
            }
            h[i] = hh;
        }
        if (fw.ok && fw.id == id && fw.n == n) {
            for (i = 0; i < n; i++)
                rows |= h[i] != fw.h[i] ? 1u << i : 0u;
            for (i = 0; i < 32; i++)
                bars |= bh[i] != fw.bh[i] ? 1u << i : 0u;
        }
        memcpy(fw.h, h, (uint32_t)n * sizeof h[0]);
        memcpy(fw.bh, bh, sizeof bh);
        fw.n = (uint8_t)n;
        fw.id = id;
        fw.key = key;
        fw.ok = 1;
        flash_add(0, rows, bars);
        return;
    }
    fw.ok = 0;                                       /* (a page with nothing watched) */
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
    else if (ui.page == P_TRACK && app.lead && !sound_browsing())
        draw_lead(0);                                /* (a melody: where its notes are in the song) */
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
