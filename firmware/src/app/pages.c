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
#include "../dsp/notes.h"

/* the colours, one meaning each (docs/superpowers/specs/2026-10-11-visibility-design.md, "The colours"):
 * teal: sounding now (the bar, beat, row, section playing; the playhead) and nothing else. Coral: you (the cursor, an
 * edit's flash, the knob last turned, REC, a chord you play over a progression, questions and messages). Ink and its
 * neutrals: everything that stays put (names, values, selections, the place PLAY starts). Slate / amber: Groove A /
 * Groove B, on their pills only (Intro, Fills, Ending: neutral outlined pills). Plum: the melodies */
#define K_BG RGB(250, 244, 232)
#define K_PANEL RGB(236, 228, 212)
#define K_PANEL2 RGB(222, 211, 192)                  /* a selected row or strip (with an ink outline) */
#define K_LINE RGB(216, 205, 186)
/* dim ink: secondary words (knob names, hints, the tag, the footer's jobs), 5.6:1 on K_BG, 4.8:1 on K_PANEL (WCAG's 4.5
 * for small text); K_FAINT, fainter, only for what may be missed (scroll bars, empty marks, separators, rules) */
#define K_DIM RGB(112, 98, 84)
#define K_FAINT RGB(150, 136, 118)
#define K_TEXT RGB(72, 58, 48)
#define MSG_BAR 1
#define K_ACCENT RGB(242, 120, 72)
#define K_ACCENT_L RGB(248, 196, 172)                /* light coral: a hint where you act (the Song page's insertion line) */
#define K_TEAL RGB(38, 166, 154)
#define K_TEAL_T RGB(196, 230, 222)
#define K_TEAL_M RGB(118, 196, 186)                  /* a playing bar's beats gone by */
#define K_AMBER RGB(206, 138, 34)                    /* Groove B */
#define K_SLATE RGB(84, 118, 168)                    /* Groove A */
#define K_PLUM RGB(150, 112, 170)                    /* the melodies */
#define K_REC K_ACCENT                                /* REC (its dot, its line): you */

/* the pages (the spec's Buttons table): a button opens its first page; SELECT turns them */
enum { P_HOME, P_SONG, P_SONG2, P_SONG3, P_LOOP, P_LOOP2, P_PATTERN, P_PATSET, P_TRACK, P_TRACK2, P_MIXER, P_TONE, P_MASTER,
       P_STYLE, P_STYLE2, P_SETUP, P_PANEL, P_NOTES, NPAGES };
/* (the words on screen: docs/superpowers/specs/2026-10-11-vocabulary-design.md; P_LOOP is the Progression page, P_PATTERN
 * Band, P_TRACK Sound, P_MASTER Effects. P_NOTES, the note editor: SEQ on Band, SEQ again back; SELECT walks its notes,
 * not pages) */
static const char *const PAGE_NAME[NPAGES] = {"Home", "Song", "Song", "Song", "Progression", "Progression", "Band",
                                              "Groove length", "Sound", "Sound 2", "Mixer", "Mixer tone", "Effects",
                                              "Style", "Style", "Setup", "Status", "Notes"};
/* the page buttons: a button opens its first page, SELECT the next (the tag's dots: which). HOME: Home, one page; on
 * Home, HOME again: Song, two pages (as X0X: the pattern, again the song; P_SONG3, once a third, is no longer shown:
 * its knobs are on P_SONG2; the numbers stay) */
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_HOME, 1}, {B_HOME, P_SONG, 2}, {B_ARP, P_LOOP, 2}, {B_SEQ, P_PATTERN, 2}, {B_EDIT, P_TRACK, 2}, {B_LFO, P_MIXER, 2},
    {B_FX, P_MASTER, 1}, {B_ENV, P_STYLE, 2}, {B_GLO, P_SETUP, 2}};
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
static int page_group(int p)                         /* the PAGE_BTN entry page p is on (Notes: SEQ's) */
{
    if (p == P_NOTES)
        p = P_PATTERN;
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
static const char *const MODE_NAME[3] = {"follow", "progression", "song"};   /* ARR_* */
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
    void (*ask_no)(void);                            /* what HOME does (0: nothing) */
    const char *ask_foot;                            /* the footer's words for it (0: "SEL yes|HOME no") */
    uint32_t yours_t;                                /* a style picked with grooves (yours): when to ask about them (0 none) */
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
    uint8_t msg_kind;                                /* the message's icon (AL_*: from its first word, say) */
    uint8_t msg_no;                                  /* the message showing is a refusal (say_no): its own look */
    uint8_t row_follow;                              /* SONG's cursor follows the row playing (from PLAY until ROW turns) */
    uint8_t sg_top;                                  /* Song's window: its first row (moved only to keep the cursor in it) */
    int8_t sg_anim_r, sg_anim_d;                     /* rows inserted (+1) / deleted (-1) at sg_anim_r: those after slide */
    uint32_t sg_anim_t;                              /* ... from then (SG_ANIM_MS) */
    int8_t sg_scroll;                                /* the window moved by this many rows: they glide (from sg_scroll_t) */
    uint32_t sg_scroll_t;
    uint32_t row_marks;                              /* Song's PROG turned below P1: the sections marked to go (bit r row
                                                      * r), all deleted when the Song pages are left (ui.c marks_poll);
                                                      * the song passes over them meanwhile (arr_rows_skip). RAM only */
    uint32_t marks_sent;                             /* ... as the engine has them */
    uint8_t marks_asked;                             /* ... their melody asked about on leaving (no answer: kept) */
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
    uint8_t pv_want, pv_on, pv_ch, pv_note;          /* Sound's preview (ui.c sound_preview): a sound picked, to play;
                                                      * a note sounding, its channel and note */
    uint32_t pv_t;                                   /* ... when it began (the next waits PREVIEW_GAP_MS, it ends at
                                                      * PREVIEW_MS) */
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
enum { AL_INFO, AL_DONE, AL_UNDO, AL_REDO, AL_ASK, AL_NO };   /* the header's alerts (alert_box) */
static int song_pg(int p) { return p >= P_SONG && p <= P_SONG3; }
/* the Progression page's TYPE knob: "-" (no chord here) first, then the chord types in the order a song needs them
 * (major and minor side by side, then the sevenths, suspended, sixths, ninths, the darker ones, the altered sevenths
 * last); it stops at both ends */
static const uint8_t TYPE_ORDER[] = {CH_MAJ, CH_MIN, CH_7, CH_MIN7, CH_MAJ7, CH_SUS4, CH_7SUS4, CH_6, CH_MIN6, CH_ADD9,
                                     CH_MADD9, CH_MAJ7_9, CH_7_9, CH_MIN7_9, CH_MIN7_11, CH_DIM, CH_MIN7_B5, CH_AUG,
                                     CH_MINMAJ7, CH_7_13, CH_7_B13, CH_7_S9, CH_7_B9, CH_7_S11, CH_7_S5};
#define NTYPE_ORDER ((int)(sizeof TYPE_ORDER / sizeof TYPE_ORDER[0]))
static int type_rank(int type)                       /* its place on the knob (not in the list: the first) */
{
    for (int i = 0; i < NTYPE_ORDER; i++)
        if (TYPE_ORDER[i] == type)
            return i;
    return 0;
}
static int song_page(void) { return song_pg(ui.page); }
/* the track selected: the keys play it, REC records into it, EDIT and the Mixer show it (0..7, or ARR_LD1 + k) */
static int sel_trk(void) { return app.lead ? ARR_LD1 + (app.lead - 1) % ARR_LEADS : proj.trk & 7; }

/* SEL: the screen as a list. Its knobs' values (each page of its button: KNOB rows), then MORE (settings on no
 * page) and the actions (ACTION: do it, PICK: a choice first). Built afresh to draw it; sel and top stay */
/* SUB: another list (SLOTS); SLOT: a row of that one (id: the slot) */
enum { LK_KNOB, LK_MORE, LK_NAME, LK_ACTION, LK_PICK, LK_SUB, LK_SLOT };
enum { A_LOAD_PROG, A_CLEAR_LOOP, A_PLAY_FROM, A_INSERT_ROW, A_DELETE_ROW, A_CLEAR_SONG, A_COPY_SONG, A_ERASE_TRACK,
       A_CLEAR_PATTERN, A_BACK_TO_STYLE, A_ERASE_LEAD_ROW, A_ERASE_LEAD, A_QUANT_LEAD_ROW, A_COPY_LEAD, A_MATCH_BARS, A_SLOTS,
       A_CLEAR_CHORD, A_STEP, A_STYLE_TEMPO, A_FACTORY_RESET, A_NEW_SONG, NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {"LOAD PROGRESSION", "CLEAR PROGRESSION", "PLAY FROM SECTION",
                                                  "INSERT SECTION", "DELETE SECTION", "CLEAR SECTIONS", "COPY SONG TO",
                                                  "CLEAR C", "CLEAR", "BACK TO STYLE", "CLEAR M IN SECTION",
                                                  "CLEAR M", "QUANTISE M", "COPY M TO SECTION", "MATCH PROG LENGTH",
                                                  "MEMORY", "CLEAR CHORD", "STEP", "STYLE TEMPO",
                                                  "FACTORY RESET", "NEW SONG"};
#define LIST_ROWS 24
#define LIST_SHOWN 10                                /* rows on screen (the list covers the knobs) */
static struct {
    uint8_t open, n, sel, top;
    uint8_t slots;                                   /* the SLOTS list (SEL's SLOTS: the 16 shared slots) */
    uint8_t back_sel, back_top;                      /* the list it came from: its row (HOME goes back there) */
    struct { const char *name; char val[24]; uint8_t kind, id; } row[LIST_ROWS];
} lst;
/* the 16 places of memory as the MEMORY list shows them ("S1: Groove A (yours)|Funk", "S2: melodies", "free"; "S1:
 * progressions" only when a song's did not fit the songs' own room), made when it opens and after a FREE (ui.c
 * slots_scan: it reads the flash); kind SLOT_* (SLOT_NONE: free); slot_ord: the list's order, by song (S1's grooves,
 * melodies, then S2's, ..., grooves no song plays), the free places last */
static char slot_txt[ARR_USER_PATTERNS][40];
static uint8_t slot_kind[ARR_USER_PATTERNS];
static uint8_t slot_ord[ARR_USER_PATTERNS];

/* the section Band's Muted knob (ui.c mute_toggle) mutes in, in a song: playing, the section playing (-1: none, the
 * Intro or the Ending); else the Song page's cursor's (nrows: the END row, none) */
static int mute_row(void)
{
    if (arr_playing && arr_mode == ARR_SONG)
        return arr_loop_now >= 0 && arr_row < songs.song[proj.song].nrows ? arr_row : -1;
    return app.row;
}
/* track t muted: in SONG playing the engine's (the row's, or by hand in the Intro / Ending), stopped the Song cursor's
 * section's (what PLAY there plays; the END row: the engine's); else the song's mute (every section; ui.c gives it to
 * the engine). A lead track (ARR_LD1 + k): the same in SONG, else never (leads play only in a song) */
static int trk_muted(int t)
{
    int r = mute_row(), lead = t >= ARR_TRACKS;
    const song_t *g = &songs.song[proj.song];
    if (lead && app.mode != ARR_SONG)
        return 0;
    if (app.mode != ARR_SONG)
        return g->mute >> (t & 7) & 1u;
    if (!arr_playing && r >= 0 && r < g->nrows)
        return lead ? g->row[r].lmute >> ((t - ARR_TRACKS) & 1) & 1u : g->row[r].mute >> (t & 7) & 1u;
    return arr_mutes >> (lead ? (t < ARR_ALLTRK ? t : ARR_LD1) : t & 7) & 1u;
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
/* the y to draw f at so its capitals sit centred in rows top .. top + h - 1 (measured from the fonts' glyphs: the
 * capitals' rows XS 3-11, S and B 2-12, M 3-18, L 4-24; a half pixel goes up, the descenders keep their room) */
static int32_t cap_y(int32_t top, int32_t h, const felucca_font_t *f)
{
    int32_t c2 = f == &FONT_L ? 28 : f == &FONT_M ? 21 : 14;   /* twice the capitals' middle row */
    return (2 * top + h - 1 - c2) >> 1;
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
 * small one, 17: FONT_XS, its capitals 3 px inside the outline) */
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
    text_c(x + w / 2, cap_y(y, h, f), f, t, c ? K_BG : K_TEXT);
    return x + w;
}

static song_t *csong(void) { return &songs.song[proj.song]; }
/* the transposition chords play at now: the song's TRANSPOSE (+ the row's KEY in SONG) */
static int xpose_now(void)
{
    const song_t *g = csong();
    return g->transpose + (app.mode == ARR_SONG && arr_row < g->nrows ? g->row[arr_row].key : 0);
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
#define KNB_H 52
#define ENC_Y 208                                    /* the two bottom lines: what SELECT, ALGORITHM, PRESETS do here, */
#define FOOT_Y 224                                   /* what the keys do here (draw_footer) */

/* PLAY's count-in (the engine's arr_pre): the beats left, 4 3 2 1 (0: none, the band playing); a chord in its last half
 * beat lands on the band's first beat (as a chord a little early on any beat, take_beat) */
static int count_beats(void) { uint32_t p = arr_pre; return (int)((p + ARR_PPQ - 1u) / ARR_PPQ); }
static int count_lands(void) { uint32_t p = arr_pre; return p && p <= ARR_PPQ / 2; }
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
 * tag's to say, not the header's. A message replaces it all, a question (SEL: yes, HOME: no) the message. PLAY's
 * count-in (a recording armed): "Count-in 3", the beats left in coral (REC: you), on every page, over a message */
/* the header's alerts: a rounded box inset from the edges (an alert over the page, not the page), an icon badge at
 * its left: a message "i", done a check, undo / redo an arrow (ink badges on beige); a question "?" (a coral box);
 * a refusal "!" (an ink box, a coral badge) */
#define AL_X 28                                      /* the words, past the badge */
static void alert_box(int kind)
{
    uint16_t fill = kind == AL_ASK ? K_ACCENT : kind == AL_NO ? K_TEXT : K_PANEL;
    uint16_t edge = kind == AL_ASK ? K_TEXT : kind == AL_NO ? K_ACCENT : K_DIM;
    uint16_t badge = kind == AL_ASK ? K_BG : kind == AL_NO ? K_ACCENT : K_TEXT;
    uint16_t mark = kind == AL_ASK ? K_ACCENT : K_BG;
    int32_t bx = 6, by = 4;                          /* the badge, 16 x 16 */
    cv_round(1, 1, 238, HDR_H - 2, 7, edge);
    cv_round(2, 2, 236, HDR_H - 4, 6, fill);
    cv_round(bx, by, 16, 16, 8, badge);
    if (kind == AL_DONE) {                           /* a check */
        for (int j = 0; j < 3; j++)
            cv_rect(bx + 4 + j, by + 7 + j, 2, 2, mark);
        for (int j = 0; j < 5; j++)
            cv_rect(bx + 7 + j, by + 9 - j, 2, 2, mark);
    } else if (kind == AL_UNDO || kind == AL_REDO) { /* a hooked arrow: its head back (undo) or on (redo) */
        int f = kind == AL_REDO, o = f ? -1 : 1;    /* (centred in the badge: undo a pixel right, redo left) */
        cv_rect(bx + 5 + o, by + 5, 6, 2, mark);     /* its top */
        cv_rect(bx + (f ? 4 : 10) + o, by + 5, 2, 6, mark);   /* its turn */
        cv_rect(bx + 5 + o, by + 9, 6, 2, mark);     /* its foot */
        for (int j = 0; j < 3; j++)                  /* the head, at the top's open end */
            cv_rect(bx + (f ? 13 - j : 2 + j) + o, by + 6 - j, 1, 2 * j + 1, mark);
    } else
        text_c(bx + 8, cap_y(by, 16, &FONT_B), &FONT_B, kind == AL_ASK ? "?" : kind == AL_NO ? "!" : "i", mark);
}
static const char *const BTN_LABEL[NB];
static void draw_header(void)
{
    char t[40];
    int msg = plat_ms() < ui.msg_until, rec = arr_recording || ui.rec_chords || ui.take || ui.song_rec, ext = arr_ext;
    int sel = home_shown(), cnt = arr_playing ? count_beats() : 0;
    int un = unsaved(), ask = ui.ask, slots = lst.open && lst.slots, nfree = 0;
    for (int i = 0; slots && i < ARR_USER_PATTERNS; i++)
        nfree += slot_kind[i] == SLOT_NONE;
    uint32_t h = hash(2166136261u, ui.page | (uint32_t)msg << 8 | (uint32_t)un << 9 | (uint32_t)rec << 11 |
                                       (uint32_t)ext << 12 | (uint32_t)sel << 13 | (uint32_t)ask << 14 |
                                       (uint32_t)csong()->tempo << 16);
    h = hash(h, (uint32_t)proj.song | (uint32_t)slots << 24 | (uint32_t)nfree << 25 | (uint32_t)ts.on << 30);
    h = hash(h, (uint32_t)cnt);
    if (msg)
        h = hash(h, ui.msg_until ^ (uint32_t)ui.msg_no << 31);
    for (const char *c = ui.ask_q; ask && *c; c++)
        h = hash(h, (uint8_t)*c);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (ask) {                                       /* the question, over all: a coral box, "?" */
        alert_box(AL_ASK);
        if (text_w(&FONT_B, ui.ask_q) <= 206)
            cv_text(AL_X, 4, &FONT_B, ui.ask_q, K_BG);
        else if (text_w(&FONT_S, ui.ask_q) <= 208)   /* a long one: smaller, whole */
            cv_text(AL_X, 5, &FONT_S, ui.ask_q, K_BG);
        else                                         /* ("SECTION 3 HAS MELODY NOTES: DELETE?") */
            text_fit(AL_X, 5, &FONT_XS, ui.ask_q, 232 - AL_X, K_BG);
    } else if (cnt) {                                /* the count-in: "Count-in 3", REC's dot */
        int32_t x = cv_text(8, 4, &FONT_B, "Count-in", K_TEXT);
        itoa_u((uint32_t)cnt, t);
        cv_text(x + 8, 4, &FONT_B, t, K_ACCENT);
        cv_round(222, 8, 9, 9, 4, K_REC);
    } else if (msg) {                                /* a message, a refusal: a box of its own over the title */
        int no = ui.msg_no;
        int32_t x;
        alert_box(no ? AL_NO : ui.msg_kind);
        x = cv_text(AL_X, 4, &FONT_B, ui.msg[0], no ? K_BG : K_TEXT);
        text_fit(x + 8, 4, &FONT_B, ui.msg[1], 232 - (x + 8), no ? K_ACCENT_L : K_ACCENT);
    } else if (slots) {                              /* MEMORY: "Memory [#########.......] 9 of 16 used": a gauge,
                                                      * a cell a place (used: ink; all used: coral) */
        int used = ARR_USER_PATTERNS - nfree;
        int32_t x = cv_text(8, 4, &FONT_B, "Memory", K_TEXT) + 10;
        for (int i = 0; i < ARR_USER_PATTERNS; i++)
            cv_rect(x + i * 5, 7, 4, 10, i < used ? (nfree ? K_TEXT : K_ACCENT) : K_LINE);
        cat_s(itoa_u((uint32_t)used, t), " of 16 used");
        cv_text(x + ARR_USER_PATTERNS * 5 + 6, 4, &FONT_S, t, nfree ? K_DIM : K_ACCENT);
    } else {
        int32_t r = 232;
        if (ts.on) {                                 /* a test session (polly_remote.py --fresh): nothing is saved */
            int32_t w = text_w(&FONT_XS, "TEST") + 10;
            r -= w;
            cv_round(r, 4, w, 16, 5, K_TEAL);
            cv_text(r + 5, 4, &FONT_XS, "TEST", K_BG);
            r -= 6;
        }
        if (sel) {                                   /* HOME held: what it does with the keys, the song's */
            itoa_u(proj.song + 1u, cat_s(t, "HOME + a key|S"));
            segs(8, 4, &FONT_B, t, K_TEXT);
        } else {
            int32_t x = 8;
            if (ui.page != P_HOME) {                 /* the button that opens it, a keycap before the name ("[SEQ] Band") */
                const char *bn = BTN_LABEL[PAGE_BTN[page_group(ui.page)].btn];
                int32_t w = text_w(&FONT_XS, bn) + 6;
                cv_round(x, 6, w, 13, 2, K_DIM);
                cv_round(x + 1, 7, w - 2, 11, 1, K_BG);
                cv_text(x + 3, cap_y(6, 13, &FONT_XS), &FONT_XS, bn, K_DIM);
                x += w + 6;
            }
            x = cv_text(x, 4, &FONT_B, PAGE_NAME[ui.page], K_TEXT);
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
 * playing tinted light teal in a teal outline (its chord in ink), its beats along its foot (the one now a bold teal
 * block); stopped, bar 1 (where PLAY starts) ringed teal; a chord just
 * written (or put back by undo) fills its bar coral a moment. Listening (draw_home): the section (and the one queued)
 * and where it is; the chord sounding, big (coral: one from the keys, holding until the source's next change; teal:
 * the loop's or the song's) and the next; a line (LOOP: the progression's name, "starts after the Intro" while an
 * Intro holds; SONG: what comes next, the melodies M1 M2 at its right); the bars under it, two rows (scrolled to the
 * bar playing). FREE: the chord alone, bigger ("Play three keys for a chord" until one is heard), and a line: no
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
/* every cursor's ring: 2 px of coral (bars, beats, sections, a sound, a melody's section), the same everywhere */
#define CUR_RING 2
/* a bar cell's big text (a chord, the count-in) at y in a cell ch tall: its capitals centred in the cell, below the
 * bar's number in the corner (its capitals end on row 12: 2 px under them) */
static int32_t bar_y(int32_t y, int32_t ch, const felucca_font_t *f)
{
    int32_t ty = cap_y(y, ch, f), top = f == &FONT_L ? 4 : f == &FONT_XS ? 3 : 2;
    return ty + top < y + 15 ? y + 15 - top : ty;
}
/* the bar playing: a light teal tint inside a teal outline (its chord in ink, readable); the cell w x h at x, y */
static void bar_lit(int32_t x, int32_t y, int32_t w, int32_t h)
{
    cv_round(x, y, w, h, 5, K_TEAL);
    cv_round(x + 2, y + 2, w - 4, h - 4, 4, K_TEAL_T);
}
/* a bar's beats along its foot (a cell RG_CW wide, ch tall at x, y), segments centred: the beat now a bold solid block
 * (6 px), those gone by solid in past (4 px), those to come an outline in past; now -1 none */
static void bar_beats(int32_t x, int32_t y, int32_t ch, int bpb, int now, uint16_t on, uint16_t past)
{
    int32_t gap = bpb >= 6 ? 1 : 2, sw = (RG_CW - 8 + gap) / bpb, x0 = x + (RG_CW - (bpb * sw - gap)) / 2, yb = y + ch - 4;
    for (int k = 0; k < bpb; k++) {
        int32_t sx = x0 + k * sw, w = sw - gap;
        if (k == now)
            cv_rect(sx, yb - 6, w, 6, on);
        else if (k < now)
            cv_rect(sx, yb - 4, w, 4, past);
        else {
            cv_rect(sx, yb - 4, w, 1, past);
            cv_rect(sx, yb - 1, w, 1, past);
            cv_rect(sx, yb - 3, 1, 2, past);
            cv_rect(sx + w - 1, yb - 3, 1, 2, past);
        }
    }
}
/* a cell tall enough for a big chord: its number, the chord's capitals (26 px), the beats along its foot (10 px) under
 * them (shorter, 44 px and up: the middle font for a chord in capitals ("F7"), else the bold one; every cell alike, so
 * the bar playing reads the same as the others) */
#define BIG_CH 52
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
    int32_t room = MAIN_H - 4 - v->y0;              /* (4 px over the tag: as every page's main band) */
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
        uint16_t fill = flash ? K_ACCENT : plays ? K_TEAL_T : out ? K_BG : K_PANEL;
        uint16_t fg = flash ? K_BG : K_TEXT;
        char nm[4][12];
        if (cur) {                                   /* the cursor: a coral ring (CUR_RING px, as every cursor) */
            cv_round(x, y, RG_CW, ch, 5, K_ACCENT);
            cv_round(x + CUR_RING, y + CUR_RING, RG_CW - 2 * CUR_RING, ch - 2 * CUR_RING, 4, fill);
        } else if (st) {                             /* where PLAY starts: an ink ring (no colour: nothing sounds yet) */
            cv_round(x, y, RG_CW, ch, 5, K_TEXT);
            cv_round(x + 2, y + 2, RG_CW - 4, ch - 4, 4, fill);
        } else if (plays && !flash)                  /* the bar playing: tinted, a teal outline */
            bar_lit(x, y, RG_CW, ch);
        else if (out && !flash) {                    /* a bar to come: its outline */
            cv_round(x, y, RG_CW, ch, 5, K_LINE);
            cv_round(x + 1, y + 1, RG_CW - 2, ch - 2, 4, fill);
        } else
            cv_round(x, y, RG_CW, ch, 5, fill);
        itoa_u((uint32_t)b + 1u, t);                 /* its number in the corner, clear of a ring */
        nx = cv_text(x + 3, ch >= 33 ? y + 1 : y + 4, &FONT_XS, t, flash ? K_BG : out ? K_LINE : K_DIM) + 4;
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
            text_c(x + RG_CW / 2, bar_y(y, ch, &FONT_L), &FONT_L, t, K_ACCENT);
        } else if (nch == 1) {                       /* one chord: as big as fits, centred (tall cells) */
            int32_t tx = ch >= 33 ? x + 4 : nx, tw = RG_CW - (tx - x) - 3;
            if (ch >= BIG_CH && text_w(&FONT_L, nm[0]) <= RG_CW - 2 * CUR_RING - 4)   /* (3 px clear of a ring) */
                text_c(x + RG_CW / 2, bar_y(y, ch, &FONT_L), &FONT_L, nm[0], fg);
            else if (ch >= 44 && upper_only(nm[0]) && text_w(&FONT_M, nm[0]) <= RG_CW - 2 * CUR_RING - 4)
                text_c(x + RG_CW / 2, bar_y(y, ch, &FONT_M), &FONT_M, nm[0], fg);   /* (between: no lower case) */
            else if (ch >= 33 && text_w(&FONT_B, nm[0]) <= tw)
                text_c(x + RG_CW / 2, bar_y(y, ch, &FONT_B), &FONT_B, nm[0], fg);
            else if (ch >= 33 && text_w(&FONT_XS, nm[0]) <= tw)
                text_c(x + RG_CW / 2, bar_y(y, ch, &FONT_XS), &FONT_XS, nm[0], fg);
            else if (text_w(&FONT_B, nm[0]) <= tw)
                cv_text(tx, y + 4, &FONT_B, nm[0], fg);
            else
                text_fit(tx, ch >= 33 ? bar_y(y, ch, &FONT_XS) : y + 4, &FONT_XS, nm[0], tw, fg);
        } else if (nch > 1) {                        /* more: small, in order ("Am  F"), centred; playing, the one
                                                      * sounding bright, the others pale */
            const felucca_font_t *f = &FONT_B;
            int32_t tx = ch >= 33 ? x + 4 : nx, ty, room = RG_CW - (tx - x) - 3;
            e = t;
            for (k = 0; k < nch; k++)
                e = cat_s(k ? cat_s(e, "  ") : e, nm[k]);
            if (ch < 33 || text_w(f, t) > room)
                f = &FONT_XS;
            ty = ch >= 33 ? bar_y(y, ch, f) : y + 4;
            if (text_w(f, t) > room)                 /* (too long even small: cut) */
                text_fit(tx, ty, f, t, room, fg);
            else {
                if (ch >= 33)
                    tx = x + (RG_CW - text_w(f, t)) / 2;
                for (k = 0; k < nch; k++)
                    tx = cv_text(tx + (k ? text_w(f, "  ") : 0), ty, f, nm[k],
                                 plays && k != snd ? K_DIM : fg);
            }
        } else if (!out)                             /* none: a dot, in the middle */
            cv_round(ch >= 33 ? x + RG_CW / 2 - 2 : (nx + x + RG_CW) / 2 - 2, y + ch / 2 - 2, 4, 4, 1,
                     flash ? K_BG : K_DIM);
        if (b == v->focus)                           /* its beats along its foot: the one now bold, those before lit */
            bar_beats(x, y, ch, bpb, v->fbeat, flash ? K_BG : v->cursor ? K_ACCENT : K_TEAL,
                      flash ? RGB(250, 196, 170) : v->cursor ? K_LINE : K_TEAL_M);
    }
    if (v->rows > v->vis) {                          /* the scroll bar: where the window is */
        int32_t th = v->vis * (ch + 2) - 2, bh = th * v->vis / v->rows, by = v->y0 + th * v->top / v->rows;
        cv_round(236, v->y0, 3, th, 1, K_PANEL);
        cv_round(236, by, 3, bh, 1, K_FAINT);
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
            cv_round(x, y, 40, 13, 4, K_LINE);
            cv_round(x + 1, y + 1, 38, 11, 3, K_BG);
            text_c(x + 20, cap_y(y, 13, &FONT_XS), &FONT_XS, TRACK_NAME[t], K_DIM);
            cv_rect(x + 8, y + 6, 24, 1, K_DIM);
        } else {
            cv_round(x, y, 40, 13, 4, on ? K_TEAL : K_PLUM);
            text_c(x + 20, cap_y(y, 13, &FONT_XS), &FONT_XS, TRACK_NAME[t], K_BG);
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
    if (take == 1) {
        wait = 1;                                    /* from PLAY */
        if (arr_playing && count_beats()) {          /* PLAY's count-in: its beats left, big, in bar 1 */
            wait = 2;
            v.cnt = count_beats();
        }
    } else if (take == 2) {
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
        if (count_beats()) {                         /* (PLAY's count-in: its beats left, big, in bar 1) */
            wait = 5;
            v.cnt = count_beats();
        } else if (arr_sect == SEC_INTRO)
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
    else if (wait == 2 || wait == 5)
        cat_s(t + slen(t), "|starts at the next bar");
    else if (wait == 3)
        cat_s(t + slen(t), "|after the Intro");
    else if (wait == 4)
        cat_s(t + slen(t), "|from the next bar");
    else {
        e = itoa_u((uint32_t)v.focus + 1u, cat_s(t + slen(t), "|bar "));
        *e++ = '.';
        itoa_u((uint32_t)v.fbeat + 1u, e);
    }
    segs(24, 2, &FONT_B, t, K_TEXT);
    if (hint)                                        /* where a chord lands */
        cv_text(8, 21, &FONT_S, v.cursor ? "chords land at the cursor" : "chords land on the nearest beat", K_DIM);
    home_bars(&v);
    cv_blit(0, MAIN_Y);
}
/* SONG: what comes after the section playing (stopped: after where PLAY starts), into t: "next: P4 Chorus
 * (Am)|Groove B" (the section's progression, its first chord as it will sound, its groove), "next: Fill|P4 Chorus
 * (Am)|Groove B" (a fill in this section's last bar), "next: P3 Verse again", "next: Ending", "next: stop". brief 1:
 * the progression's number alone ("P4 (Am)"); 2: no groove either */
/* the section that plays after section r (-1: from the first), as the engine finds it: the next one not marked to go
 * (ui.row_marks: passed over), after the last with AT END again round from the first (r itself too); -1 none */
static int row_after_m(const song_t *g, int r)
{
    int n = g->nrows > ARR_ROWS ? ARR_ROWS : g->nrows;
    for (int k = r + 1; k < n; k++)
        if (!(ui.row_marks >> k & 1u))
            return k;
    if (g->at_end)
        for (int k = 0; k <= r && k < n; k++)
            if (!(ui.row_marks >> k & 1u))
                return k;
    return -1;
}
static void song_next(char *t, int play, int brief)
{
    const song_t *g = csong();
    int n = g->nrows, r = 0, rp = play ? arr_rep : 0, nr = -1, fill = 0, again = 0, gone = 0;
    int ns = arr_next_sect, sect = arr_sect;         /* (stopped: the section PLAY starts with, the Intro too) */
    const char *what = 0;
    char *e = cat_s(t, "next: ");
    if (play)
        gone = (r = arr_row) >= n;                   /* (a section deleted playing out its pass: none) */
    else
        while (r < n && (ui.row_marks >> r & 1u))    /* (PLAY starts at the first not marked to go) */
            r++;
    if (!n || (r >= n && !gone))
        what = "stop";
    else if (sect == SEC_END)
        what = "stop";
    else if (ns == SEC_END)
        what = "Ending";
    else if (sect == SEC_INTRO)
        nr = row_after_m(g, -1);
    else if (gone)                                   /* (the engine's next: the deleted one's row is not ours) */
        nr = arr_row_next < n ? arr_row_next : -1;
    else if (!(ui.row_marks >> r & 1u) && rp + 1 < (g->row[r].rep ? g->row[r].rep : 1)) {
        nr = r;                                      /* (marked: its pass playing is its last) */
        again = 1;
    } else
        nr = row_after_m(g, r);                      /* (again: section 1, as the engine; the Ending never) */
    if (!what && nr < 0)
        what = g->ending ? "Ending" : "stop";
    if (nr >= 0 && !again && !gone && sect != SEC_INTRO)   /* a fill in the last bar: the row asks, or the groove
                                                            * changes */
        fill = !row_nofill(&g->row[r]) && (row_fill(&g->row[r]) || row_main(&g->row[nr]) != row_main(&g->row[r]));
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
        int32_t room = (leads.n ? home_leads(lit, 23, 0) : 232) - 8 - 8;
        for (int b = 0; b < 3; b++) {
            song_next(line, play, b);
            if (segs_w(&FONT_S, line) <= room)
                break;
        }
    } else if (mode == ARR_SONG)
        cat_s(line, "no sections yet|add them on Song");
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
                text_c(232 - w / 2, cap_y(1, 20, &FONT_B), &FONT_B, t, K_BG);
            }
            *t = 0;
        } else if (mode == ARR_SONG && g->nrows) {   /* (stopped: section 1, where PLAY starts) */
            uint32_t r = play ? arr_row : 0u, rp = play ? arr_rep : 0u, rep = r < g->nrows && g->row[r].rep ? g->row[r].rep : 1u;
            char *e;
            *t = 0;
            if (r < g->nrows) {                      /* (a section deleted playing out its pass: none) */
                e = itoa_u(r + 1u, cat_s(t, "section "));
                e = itoa_u(g->nrows, cat_s(e, " of "));
                if (rep > 1)
                    itoa_u(rep, cat_s(itoa_u(rp + 1u, cat_s(e, "|repeat ")), " of "));
            }
        } else if (alone && play && (slot == SEC_A - 1 || slot == SEC_B - 1)) {   /* FREE: the groove's bar */
            char *e = itoa_u(pl.bar + 1u, cat_s(t, "groove bar "));
            *e++ = '.';
            e = itoa_u(pl.beat + 1u, e);
            itoa_u(app.hdr[slot].bars, cat_s(e, " / "));
        }
        if (*t && 232 - (w = segs_w(&FONT_S, t)) >= x + 8)     /* (a size up: read at arm's length) */
            segs(232 - w, cap_y(1, 20, &FONT_S), &FONT_S, t, play ? K_TEXT : K_DIM);
        else if (*t && 232 - (w = segs_w(&FONT_XS, t)) >= x + 8)
            segs(232 - w, 4, &FONT_XS, t, play ? K_TEXT : K_DIM);
    }
    if (alone) {                                     /* FREE: the chord, big, in a box across; the line under it */
        cv_round(8, 24, 224, HOME_FREE_H, 6, K_PANEL);
        if (fresh)
            text_c(120, cap_y(24, HOME_FREE_H, &FONT_B), &FONT_B, "Play three keys for a chord", K_DIM);
        else {
            chord_name(arr_chord_unpack(pl.chord), t);   /* (teal: the band plays it now) */
            text_c(120, cap_y(24, HOME_FREE_H, &FONT_L), &FONT_L, t, play ? K_TEAL : K_TEXT);
        }
        segs(120 - segs_w(&FONT_S, line) / 2, 24 + HOME_FREE_H + 7, &FONT_S, line, K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    if (*line)                                       /* what comes next (a song), the Intro's wait; the melodies */
        segs(8, 21, segs_w(&FONT_S, line) <= 224 ? &FONT_S : &FONT_XS, line, K_DIM);
    if (mode == ARR_SONG && g->nrows && leads.n)
        home_leads(lit, 23, 1);
    if (l && v.n)                                    /* the bars */
        home_bars(&v);
    else if (l) {
        loop_label(pl.loop, t);
        cat_s(t + slen(t), " is empty");
        cv_text(8, HB_Y + 4, &FONT_S, t, K_DIM);
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
    int bars = l->bars, rows, xp = xpose_now(), i, live, fbar, fl, shown, big;
    int n1 = wrap ? (bpb + 1) / 2 : bpb, cw = LG_W / n1;
    int32_t y0 = wrap ? 2 : 13, ch, gap = wrap ? 6 : 2, rh, visible, top, want;
    want = bars < 1 ? 1 : bars;                      /* (a bar to come past the end: scrolled to, not squeezed in) */
    ch = ((MAIN_H - y0 + gap) / want - gap - (wrap ? 2 : 0)) / (wrap ? 2 : 1);   /* a short one: taller cells (one or
                                                      * two bars: as tall as HOME's, the chords as big when all fit) */
    ch = ch < 18 ? 18 : ch > (wrap ? 24 : 40) ? (wrap ? 24 : 40) : ch;
    rh = (wrap ? 2 * ch + 2 : ch) + gap;
    visible = (MAIN_H - y0 + gap) / rh;
    uint64_t fm = flash_on() ? fw.beats : 0;
    place_t pl;
    uint32_t pbeat, h;
    char t[24];
    place_read(&pl);
    pbeat = arr_playing && !arr_pre && pl.loop == (int)(app.loop % ARR_LOOPS) ? pl.lbeat : 0xFFFFFFFFu;   /* (in
                                                      * PLAY's count-in: none yet, the header counts) */
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
    big = ch >= 40;                                  /* tall cells: every chord big, when every one fits */
    for (i = 0; big && i < len && i < ARR_LOOP_BEATS; i++)
        if (l->ch[i] != LC_SAME) {
            chord_name(chord_shift(lc_unpack(l->ch[i]), xp), t);
            big = text_w(&FONT_L, t) <= cw - 3 - 2 * CUR_RING - 4;   /* (3 px clear of the cursor's ring) */
        }
    if (!wrap)                                       /* the beats' numbers (the cursor's: on coral) */
        for (int k = 0; k < bpb; k++) {
            int32_t cx = LG_X + k * cw + (cw - 3) / 2;
            itoa_u((uint32_t)k + 1u, t);
            if (!live && k == app.cbeat % bpb) {     /* (a badge: 11 px, the digit's capitals 1 px clear) */
                cv_round(cx - 8, 0, 16, 11, 4, K_ACCENT);
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
            int32_t w = text_w(&FONT_XS, t) + 6, by = wrap ? y + ch - 5 : cap_y(y, ch, &FONT_XS) + 2;
            cv_round(LG_X - 3 - w, by, w, 11, 4, live ? K_TEAL : K_ACCENT);
            cv_text(LG_X - w, by - 2, &FONT_XS, t, K_BG);
        } else
            cv_text(LG_X - 5 - text_w(&FONT_XS, t), wrap ? y + ch - 7 : cap_y(y, ch, &FONT_XS), &FONT_XS, t, K_DIM);
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
                cv_round(x + CUR_RING, yy + CUR_RING, w - 2 * CUR_RING, ch - 2 * CUR_RING, 3, fill);
            } else if (ghost && !flash && !plays) {  /* a cell to come: its outline */
                cv_round(x, yy, w, ch, 4, K_LINE);
                cv_round(x + 1, yy + 1, w - 2, ch - 2, 3, fill);
            } else
                cv_round(x, yy, w, ch, 4, fill);
            if (v != LC_SAME) {
                chord_name(chord_shift(lc_unpack(v), xp), t);
                if (big)
                    text_c(x + w / 2, cap_y(yy, ch, &FONT_L), &FONT_L, t, fg);
                else if (ch >= 24 && text_w(&FONT_B, t) <= w - 9)
                    cv_text(x + 5, cap_y(yy, ch, &FONT_B), &FONT_B, t, fg);
                else
                    text_fit(x + 5, cap_y(yy, ch, &FONT_XS), &FONT_XS, t, w - 9, fg);
            } else
                cv_round(x + w / 2 - 2, yy + ch / 2 - 2, 4, 4, 1, flash || plays ? K_BG : ghost ? K_LINE : K_FAINT);
        }
    }
    shown = rows - top < visible ? rows - top : visible;
    (void)shown;                                     /* (the cursor's place: its bar's and beat's numbers lit, coral) */
    if (rows > visible) {                            /* the scroll bar: where the window is */
        int32_t th = MAIN_H - y0 - 4, bh = th * visible / rows, by = y0 + th * top / rows;
        cv_round(236, y0, 3, th, 1, K_PANEL);
        cv_round(236, by, 3, bh < 6 ? 6 : bh, 1, K_FAINT);
    }
    if (!len)                                        /* an empty loop: what to do */
        text_c(LG_X + LG_W / 2, y0 + rh + 18, &FONT_XS, ui.rec_chords ? "play a chord" : "REC, then play chords", K_DIM);
    cv_blit(0, MAIN_Y);
}

/* ---------------------------------------------------------- SONG page --- */
/* the song's rows (on screen: its sections; the spec's Screens): a row a line: its number, PROG ("P3 Fifties"), GROOVE
 * (its colour, Groove A slate, B amber, its letter on it; "+ fill"), REPEAT ("x2"), TRANSPOSE ("+2"; none at 0). The
 * cursor row in a coral ring (white keys write there), the row playing filled teal with a mark; after the last row the
 * END row (taps there add rows), outlined as a row to come. Over the rows, right: where the cursor is, coral ("section 2
 * of 3"; on END what a white key does, dim); left, playing an Intro, a fill, the Ending: "Intro . then section 1". Under
 * them the cursor section's mutes, the black keys' (D1 D2 PC BA | C1 C2 C3 C4 | the melodies M1 M2, plum): plays filled,
 * muted an outline. Four rows show, scrolled to the cursor (a scroll bar when more). A row: a box 16 px; the cursor's
 * ring round it, in the 4 px between the rows */
#define SG_Y0 19                                     /* the first row */
#define SG_RH 20                                     /* a row's pitch */
#define SG_SHOWN 4
#define SG_NUM 20                                    /* #'s right edge */
#define SG_LOOP 26                                   /* PROG's left */
#define SG_SECT 136                                  /* GROOVE's chip */
#define SG_REP 206                                   /* REPEAT's right edge */
#define SG_KEY 232                                   /* TRANSPOSE's right edge */
#define SG_MUTE_Y 102                                /* the cursor section's mutes */
/* the row's section knob (on screen: its groove) */
static const char *const ROW_SECT_NAME[ROW_NSECT] = {"A", "A + fill", "B", "B + fill", "A no fill", "B no fill"};
/* GROOVE's order on the knob: each groove plain (a fill where the groove changes), + fill (always), no fill (never) */
static const uint8_t SECT_ORDER[ROW_NSECT] = {ROW_A, ROW_A_FILL, ROW_A_NOFILL, ROW_B, ROW_B_FILL, ROW_B_NOFILL};
static int sect_rank(int s)
{
    for (int i = 0; i < ROW_NSECT; i++)
        if (SECT_ORDER[i] == s)
            return i;
    return 0;
}
/* the section playing's clock: a ring of radius r round (cx, cy) filled clockwise from 12 o'clock to f/1024 of the way
 * round (the angle a diamond's, |x| + |y|: no trigonometry, a degree or two off at 11 px) */
static void glyph_clock(int32_t cx, int32_t cy, int32_t r, uint32_t f, uint16_t c)
{
    int32_t r2 = (2 * r + 1) * (2 * r + 1), in2 = (2 * r - 2) * (2 * r - 2);
    for (int32_t py = cy - r; py <= cy + r; py++)
        for (int32_t px = cx - r; px <= cx + r; px++) {
            int32_t x = 2 * (px - cx), y = 2 * (cy - py), d2 = x * x + y * y, a = (x < 0 ? -x : x) + (y < 0 ? -y : y);
            uint32_t q;
            if (d2 > r2)
                continue;
            if (d2 > in2) {                          /* the ring */
                cv_rect(px, py, 1, 1, c);
                continue;
            }
            if (!a)
                q = 0;
            else if (x >= 0 && y > 0)
                q = (uint32_t)(256 * x / a);
            else if (x > 0)
                q = 256u + (uint32_t)(256 * -y / a);
            else if (y < 0)
                q = 512u + (uint32_t)(256 * -x / a);
            else
                q = 768u + (uint32_t)(256 * y / a);
            if (q < f)
                cv_rect(px, py, 1, 1, c);
        }
}
/* how far the section playing has gone, 0..1024 (its progression's beats x REPEAT; arr_row_tick its place) */
static uint32_t row_done(const row_t *w)
{
    int per = loop_len(&sloops.loop[w->loop % ARR_LOOPS]);
    uint32_t len = (uint32_t)(per > 0 ? per : 4) * (w->rep ? w->rep : 1u) * ARR_PPQ, t = arr_row_tick;
    return t >= len ? 1024u : t * 1024u / len;   /* (t < 16 bars x 7 beats x 8 passes: no overflow) */
}
static int pslot(void);
/* how far the Intro or the Ending has gone, 0..1024 (the pattern playing: its bars; arr_bar, arr_beat_tick its place) */
static uint32_t pat_done(void)
{
    int s = pslot(), bpb = app.hdr[s].beats ? app.hdr[s].beats : 4;
    uint32_t blen = (uint32_t)bpb * ARR_PPQ, len = (uint32_t)(app.hdr[s].bars ? app.hdr[s].bars : 1) * blen;
    uint32_t t = (uint32_t)arr_bar * blen + arr_beat_tick;
    return t >= len ? 1024u : t * 1024u / len;
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
        int nx = r < n ? row_after_m(g, r) : arr_row_next < n ? arr_row_next : -1;   /* (the marked passed over) */
        if (nx < 0)
            return 0;
        itoa_u((uint32_t)nx + 1u, cat_s(t, "into section "));
    } else if (s == SEC_END)
        cat_s(t, "then stop");
    else
        return 0;
    return s;
}
/* the Song page's line over the rows: the cursor's place in words, coral ("section 3 of 6"; on END what a white key
 * does, dim); left of it, playing its Intro, a fill, its Ending: "Intro . then section 1" (then only the numbers) */
static void song_heading(char *t, const char *stt, int cur, int n, int st)
{
    *t = 0;
    if (cur < n)
        itoa_u((uint32_t)n, cat_s(itoa_u((uint32_t)cur + 1u, cat_s(t, st ? "" : "section ")), " of "));
    if (*t)
        text_r(232, cap_y(0, 15, &FONT_S), &FONT_S, t, K_ACCENT);
    else if (!st)
        text_r(232, cap_y(0, 15, &FONT_S), &FONT_S, "a white key adds a section", K_DIM);
    if (st) {                                    /* (the Intro, the Ending: their clock before the pill) */
        int cl = st == SEC_INTRO || st == SEC_END;
        int32_t x;
        if (cl)
            glyph_clock(10, 7, 6, pat_done(), K_TEXT);
        x = sect_pill(cl ? 20 : 8, 0, 0, 15, st, 0);
        cv_text(x + 6, cap_y(0, 15, &FONT_S), &FONT_S, stt, K_TEXT);
    }
}
#define SG_ANIM_MS 160u
#define SG_GAP 4                                     /* the insertion point's room over the cursor's section */
static void draw_song(void)
{
    int32_t slide, scroll;
    int ants;
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
    top = ui.sg_top;                                 /* the window stays; it moves only to keep the cursor in it */
    if (cur < top)
        top = cur;
    else if (cur > top + SG_SHOWN - 1)
        top = cur - (SG_SHOWN - 1);
    if (top > total - SG_SHOWN)
        top = total - SG_SHOWN;
    if (top < 0)
        top = 0;
    if (top != ui.sg_top) {                          /* the window moves: a row or two glide, more jump */
        int d = top - ui.sg_top;
        ui.sg_scroll = (int8_t)(d >= -2 && d <= 2 ? d : 0);
        ui.sg_scroll_t = plat_ms();
    }
    ui.sg_top = (uint8_t)top;
    {   /* the glide: every row from where it was in the window before to where it is now, SG_ANIM_MS */
        uint32_t el = plat_ms() - ui.sg_scroll_t;
        scroll = ui.sg_scroll && el < SG_ANIM_MS ? (int32_t)((SG_ANIM_MS - el) * SG_RH / SG_ANIM_MS) * ui.sg_scroll : 0;
        if (!scroll)
            ui.sg_scroll = 0;
    }
    {   /* a section inserted or deleted: the rows after it slide into place (down from the one before, up from the
         * one after), SG_ANIM_MS */
        uint32_t el = plat_ms() - ui.sg_anim_t;
        slide = ui.sg_anim_d && el < SG_ANIM_MS ? (int32_t)((SG_ANIM_MS - el) * SG_RH / SG_ANIM_MS) * -ui.sg_anim_d : 0;
        if (!slide)
            ui.sg_anim_d = 0;
    }
    h = hash(2166136261u, 327680u | (uint32_t)pg | (uint32_t)top << 4 | (uint32_t)cur << 10 | (uint32_t)n << 24);
    h = hash(h, (uint32_t)slide ^ (uint32_t)scroll << 12);
    ants = !ui.song_rec && !lst.open && cur <= n ? (int)(plat_ms() / 120u % 7u) : -1;   /* where a white key inserts:
                                                      * above the cursor's section (on END: there) */
    h = hash(h, (uint32_t)(ants + 1));
    h = hash(h, (uint32_t)(play + 1));               /* (its own word: OR-ed into the first, row 1 playing was lost) */
    h = hash(h, (uint32_t)ui.song_rec << 3 | (uint32_t)st << 4 | (uint32_t)arr_row << 8);
    h = hash(h, ui.row_marks);
    if (play >= 0)                                   /* (its clock: 32 steps round) */
        h = hash(h, row_done(&g->row[play]) >> 5);
    else if (st == SEC_INTRO || st == SEC_END)       /* (the Intro's, the Ending's) */
        h = hash(h, pat_done() >> 5);
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
    song_heading(t, stt, cur, n, st);
    for (i = scroll ? top - 2 : top; i < total && i < top + SG_SHOWN + (scroll ? 2 : 0); i++) {   /* (gliding: the rows
                                                      * coming in and going out too, cut at the band's edges below) */
        int32_t y = SG_Y0 + (i - top) * SG_RH + (slide && i >= ui.sg_anim_r + (ui.sg_anim_d > 0) ? slide : 0) + scroll;
        int32_t ch = 16;
        if (i < 0)
            continue;
        if (ants >= 0 && i >= cur)                   /* (the insertion point's gap: the cursor's row and after, 4 down) */
            y += SG_GAP;
        int32_t ty = cap_y(y, ch, &FONT_S);
        int on = i == cur, ghost = i >= n, pl = i == play, fl = i < ARR_ROWS && (fm >> i & 1u);
        /* the section playing filled teal, an edit coral a moment; the cursor a thick coral ring round it (into the
         * gaps between the rows) */
        uint16_t fill = fl ? K_ACCENT : pl ? K_TEAL : ghost ? K_BG : K_PANEL, fg = fl || pl ? K_BG : K_TEXT;
        uint16_t dim = fl || pl ? K_BG : K_DIM;
        if (slide && ui.sg_anim_d > 0 && i == ui.sg_anim_r) {   /* the section going in: it grows open in its place
                                                      * as the rows after are pushed down (its words when nearly whole) */
            int32_t gh = ch * (SG_RH + slide) / SG_RH;
            if (gh < ch - 3) {
                if (gh > 1)
                    cv_round(2, y + (ch - gh) / 2, 232, gh, gh > 8 ? 5 : gh / 2, fill);
                continue;
            }
        }
        if (on)                                      /* the cursor: its ring round the row */
            cv_round(2 - CUR_RING, y - CUR_RING, 232 + 2 * CUR_RING, ch + 2 * CUR_RING, 6, K_ACCENT);
        if (ghost) {                                 /* the END row: a row to come */
            cv_round(2, y, 232, ch, 5, on ? K_BG : K_LINE);
            cv_round(3, y + 1, 230, ch - 2, 4, fill);
        } else
            cv_round(2, y, 232, ch, 5, fill);
        if (pl)                                      /* the section playing: its clock, in place of its number */
            glyph_clock(12, y + 8, 6, row_done(&g->row[i]), K_BG);
        if (ghost) {
            cv_text(SG_LOOP, ty, &FONT_S, "END", on ? K_TEXT : K_DIM);
            continue;
        }
        if (ui.row_marks >> i & 1u) {                /* marked to go: "-", struck through, what happens (playing: its
                                                      * clock, dim: it plays out its pass) */
            cv_round(2, y, 232, ch, 5, K_BG);
            cv_round(3, y + 1, 230, ch - 2, 4, K_BG);
            itoa_u((uint32_t)i + 1u, t);
            if (pl)
                glyph_clock(12, y + 8, 6, row_done(&g->row[i]), K_DIM);
            else
                text_r(SG_NUM, ty, &FONT_S, t, K_DIM);
            cv_text(SG_LOOP, ty, &FONT_S, "-  section removed", K_DIM);
            cv_rect(SG_LOOP - 4, y + ch / 2, 232 - SG_LOOP, 1, K_DIM);
            continue;
        }
        {
            const row_t *w = &g->row[i];
            const loop_t *l = &sloops.loop[w->loop % ARR_LOOPS];
            int32_t x;
            uint16_t sc = sect_colour(row_main(w));
            char *e;
            itoa_u((uint32_t)i + 1u, t);
            if (!pl)
                text_r(SG_NUM, ty, &FONT_S, t, on || fl ? fg : K_DIM);
            /* PROG: "P9" and its name ("empty" an empty loop) */
            itoa_u((uint32_t)(w->loop % ARR_LOOPS) + 1u, cat_s(t, "P"));
            x = cv_text(SG_LOOP, ty, &FONT_S, t, dim) + 4;
            loop_label(w->loop, t);
            for (e = t; *e && *e != ' '; e++)
                ;
            text_fit(x, ty, &FONT_B, l->bars ? (*e ? e + 1 : e) : "empty", SG_SECT - (w->mute || w->lmute ? 22 : 6) - x,
                     l->bars ? fg : dim);
            if (w->mute || w->lmute) {               /* MUTES: an ink "M" before the groove, as the instruments' pills */
                cv_round(SG_SECT - 16, y + 2, 13, ch - 4, 3, fl || pl ? K_BG : K_TEXT);
                text_c(SG_SECT - 9, cap_y(y + 2, ch - 4, &FONT_XS), &FONT_XS, "M", fl || pl ? fill : K_BG);
            }
            /* GROOVE: its colour with its letter on it, "+ fill" */
            cv_round(SG_SECT, y + 2, 15, ch - 4, 3, fl || pl ? K_BG : sc);
            text_c(SG_SECT + 7, cap_y(y + 2, ch - 4, &FONT_XS), &FONT_XS, row_main(w) == SEC_B ? "B" : "A", fl || pl ? fill : K_BG);
            if (row_fill(w))
                cv_text(SG_SECT + 20, ty, &FONT_S, "+ fill", fg);
            else if (row_nofill(w))
                cv_text(SG_SECT + 20, ty, &FONT_S, "no fill", dim);
            /* REPEAT: "x2" (x1 dim) */
            itoa_u(w->rep ? w->rep : 1u, cat_s(t, "x"));
            text_r(SG_REP, ty, &FONT_S, t, w->rep > 1 ? fg : dim);
            /* TRANSPOSE: "+2", "-3" (0: nothing) */
            if (w->key) {
                t[0] = w->key > 0 ? '+' : '-';
                itoa_u((uint32_t)(w->key > 0 ? w->key : -w->key), t + 1);
                text_r(SG_KEY, ty, &FONT_B, t, fg);
            }
        }
    }
    if (ants >= 0 && cur >= top && cur < top + SG_SHOWN && !slide && !scroll) {   /* the insertion point: marching ants in the
                                                      * gap over the cursor's ring, an arrowhead at its left */
        int32_t ly = SG_Y0 + (cur - top) * SG_RH - 2;   /* (mid-gap, a pixel clear of the ring below) */
        for (int32_t x = 9; x < 232; x++)        /* light coral ants, marching slowly */
            if ((x + 7 - ants) % 7 < 4)
                cv_rect(x, ly, 1, 2, K_ACCENT_L);
        for (int j = 0; j < 3; j++)                  /* a small arrowhead pointing in, as light */
            cv_rect(3 + j, ly - 2 + j, 1, 6 - 2 * j, K_ACCENT_L);
    }
    if (scroll) {                                    /* (gliding rows cut at the band's edges: the line over the rows
                                                      * drawn again, the mutes' place cleared before they are drawn) */
        cv_rect(0, 0, 240, SG_Y0 - CUR_RING - 1, K_BG);
        cv_rect(0, SG_Y0 + SG_SHOWN * SG_RH + SG_GAP, 240, MAIN_H - (SG_Y0 + SG_SHOWN * SG_RH + SG_GAP), K_BG);
        song_heading(t, stt, cur, n, st);
    }
    if (total > SG_SHOWN) {                          /* the scroll bar: where the window is */
        int32_t th = SG_SHOWN * SG_RH - 4, bh = th * SG_SHOWN / total, by = SG_Y0 + th * top / total;
        cv_round(236, SG_Y0, 3, th, 1, K_PANEL);
        cv_round(236, by, 3, bh < 6 ? 6 : bh, 1, K_FAINT);
    }
    if (cur < n) {   /* the cursor section's mutes (the black keys'): plays filled (ink; the melodies plum), muted outlined */
        const row_t *w = &g->row[cur];
        int32_t x = 9;
        for (int k = 0; k < ARR_ALLTRK; k++) {
            int m = k < ARR_TRACKS ? w->mute >> k & 1u : w->lmute >> (k - ARR_TRACKS) & 1u;
            uint16_t c = k < ARR_TRACKS ? K_TEXT : K_PLUM;
            if (k == 4 || k == ARR_TRACKS)
                x += 5;                              /* (drums | chords | melodies) */
            if (m) {
                cv_round(x, SG_MUTE_Y, 20, 15, 3, K_FAINT);
                cv_round(x + 1, SG_MUTE_Y + 1, 18, 13, 2, K_BG);
            } else
                cv_round(x, SG_MUTE_Y, 20, 15, 3, c);
            text_c(x + 10, cap_y(SG_MUTE_Y, 15, &FONT_XS), &FONT_XS, TRACK_NAME[k], m ? K_FAINT : K_BG);
            x += 22;
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
static int32_t pattern_head(int s, int win, int len)
{
    char t[24];
    int32_t x = home_pill(8, 0, s + 1, 0);
    if (!len)
        return x;
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
    if (win >= 0)                                    /* (Band: a window's bars after the pill; its length: Groove length's) */
        return cv_text(x + 8, cap_y(0, 20, &FONT_S), &FONT_S, t, K_DIM);
    cv_text(232 - text_w(&FONT_XS, t), 3, &FONT_XS, t, K_DIM);
    return x;
}
/* the 8 tracks as mini piano rolls over the pattern's length (up to 8 bars; a longer one by halves: playing, the one the
 * playhead is in; stopped, the cursor's), 12 px a row, the names in S; a drum's hits short ticks; the mutes (struck
 * through), an instrument with no notes faded; the selected instrument's row tinted. The cursor (BAR / BEAT): a thick
 * coral line down the instruments, its place on coral in the head over it ("bar 1.3"); the playhead a teal line; an
 * instrument whose notes or mute just changed: its row coral a moment */
#define PAT_Y 25                                     /* the first instrument's row */
#define PAT_RH 12                                    /* a row's pitch */
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
    {   /* the head: the groove's pill (a window of a longer one: "bars 9-16 of 16" after it); the cursor's place on coral,
         * over the cursor's line ("bar 2.3"; off this half: at the side), right of those */
        int32_t hx = win >= 0 ? pattern_head(s, win, 1) : pattern_head(s, -1, 0), w, px;
        char *e = itoa_u((uint32_t)(cur / bpb) + 1u, cat_s(t, "bar "));
        *e++ = '.';
        itoa_u((uint32_t)(cur % bpb) + 1u, e);
        w = text_w(&FONT_S, t) + 12;
        if (cx >= 0)
            px = cx - w / 2;
        else
            px = cur * ARR_PPQ < (int)w0 ? 0 : 240;
        px = px < hx + 8 ? hx + 8 : px > 232 - w ? 232 - w : px;
        cv_round(px, 0, w, 20, 6, K_ACCENT);
        cv_text(px + 6, cap_y(0, 20, &FONT_S), &FONT_S, t, K_BG);
    }
    for (i = 0; i < ARR_TRACKS; i++) {
        const arr_trk_t *tr = &view.trk[i];
        int32_t y = PAT_Y + i * PAT_RH, b, bh = PAT_RH - 1;
        int sel = i == sel_trk(), mute = trk_muted(i), fl = fm >> i & 1u, empty = !tr->count;
        int drums = (app.trk[s][i].chan & 15u) == 9u;
        uint32_t n, e = (uint32_t)tr->first + tr->count, lo = 127, hi = 0;
        /* its name (muted: pale, struck through, as HOME's melodies; no notes: faint); its row: the selected one darker,
         * ink-outlined (recording into it: coral, REC's), an empty one an outline */
        cv_text(3, cap_y(y, bh, &FONT_S), sel ? &FONT_B : &FONT_S, TRACK_NAME[i],
                sel ? K_TEXT : mute || empty ? K_FAINT : K_DIM);
        if (mute)
            cv_rect(2, y + bh / 2, 18, 1, K_TEXT);
        if (sel && !fl) {
            cv_round(X0 - 1, y - 1, W + 2, bh + 2, 3, arr_recording ? K_ACCENT : K_TEXT);
            cv_round(X0, y, W, bh, 3, arr_recording ? RGB(246, 214, 204) : K_PANEL2);
        } else if (empty && !fl) {
            cv_round(X0, y, W, bh, 3, K_LINE);
            cv_round(X0 + 1, y + 1, W - 2, bh - 2, 2, K_BG);
        } else
            cv_round(X0, y, W, bh, 3, fl ? K_ACCENT : K_PANEL);
        for (b = 1; b < wbars; b++)              /* bar lines */
            cv_rect(X0 + b * W / wbars, y + 1, 1, bh - 2, fl ? K_BG : K_LINE);
        if (e > view.nev)
            e = view.nev;
        for (n = tr->first; n < e; n++) {
            if (view.ev[n].note < lo)
                lo = view.ev[n].note;
            if (view.ev[n].note > hi)
                hi = view.ev[n].note;
        }
        for (n = tr->first; n < e; n++) {        /* the notes by place and pitch; a drum's a short tick (not a smear) */
            const arr_ev_t *v = &view.ev[n];
            int32_t x, w, yy;
            if (v->tick < w0 || v->tick >= w0 + len || v->tick >= (uint32_t)nb * blen)
                continue;                        /* another half; past a shortened length: kept, not played */
            x = X0 + (int32_t)((uint32_t)(v->tick - w0) * (uint32_t)W / len);
            w = drums ? 2 : (int32_t)((uint32_t)v->len * (uint32_t)W / len) - 1;
            w = w < 2 ? 2 : w > X0 + W - x ? X0 + W - x : w;
            yy = y + bh - 5 - (hi > lo ? (int32_t)((v->note - lo) * (uint32_t)(bh - 7) / (hi - lo)) : (bh - 7) / 2);
            cv_rect(x, yy, w, 3, fl ? K_BG : mute ? sel ? K_FAINT : K_LINE : K_TEXT);   /* (muted: pale; on the selected row's darker ground, a step less) */
        }
    }
    if (ph >= 0)                                 /* the playhead: teal */
        cv_rect(ph, PAT_Y - 2, 2, YB - PAT_Y + 3, K_TEAL);
    if (cx >= 0)                                 /* the cursor: a thick coral line (its place: the head's) */
        cv_rect(cx - 1, PAT_Y - 3, 3, YB - PAT_Y + 4, K_ACCENT);
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
    int wait = !arr_recording || arr_rec_wait, focus = -1, fbeat = 0, next = -1, cnt = 0, pass = 1, rows, ch, b;
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
    if (play && arr_pre) {                           /* PLAY's count-in: bar 1, its beats left */
        focus = -1;
        next = 0;
        cnt = count_beats();
    } else if (wait) {                               /* the bar it starts on, the beats to it */
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
            segs(24, 19, &FONT_S, t, K_DIM);
        }
    }
    for (b = 0; b < nb; b++) {                       /* the bars */
        int32_t x = RG_X + (b % 4) * (RG_CW + 4), y = TR_Y + (b / 4) * (ch + 3), ax, aw, ay, ah, nh;
        int plays = b == focus, cin = b == next, fl = fbars >> b & 1u;
        uint16_t fill = fl ? K_ACCENT : plays ? K_TEAL_T : K_PANEL, fg = fl ? K_BG : K_TEXT;
        if (cin) {                                   /* the bar recording starts on: the cursor's coral ring */
            cv_round(x, y, RG_CW, ch, 5, K_ACCENT);
            cv_round(x + CUR_RING, y + CUR_RING, RG_CW - 2 * CUR_RING, ch - 2 * CUR_RING, 4, plays ? K_TEAL_T : K_BG);
        } else if (plays && !fl)                     /* the bar playing: tinted, a teal outline */
            bar_lit(x, y, RG_CW, ch);
        else
            cv_round(x, y, RG_CW, ch, 5, fill);
        itoa_u((uint32_t)b + 1u, t);
        ax = ch >= 30 ? x + 4 : cv_text(x + 3, cap_y(y, ch, &FONT_XS), &FONT_XS, t, fl ? K_BG : K_DIM) + 3;
        if (ch >= 30)                                /* its number in the corner (as HOME's bars) */
            cv_text(x + 3, y + 1, &FONT_XS, t, fl ? K_BG : cin ? K_ACCENT : K_DIM);
        aw = x + RG_CW - 4 - ax;
        ay = ch >= 30 ? y + 16 : y + 3;
        ah = y + ch - 11 - ay;                       /* (clear of the beats along the foot) */
        nh = ah >= 30 ? 3 : 2;
        if (cin && cnt) {                            /* the count-in, big */
            itoa_u((uint32_t)cnt, t);
            if (ch >= 36)
                text_c(x + RG_CW / 2, bar_y(y, ch, &FONT_L), &FONT_L, t, K_ACCENT);
            else
                text_c(x + RG_CW / 2, cap_y(y, ch, &FONT_B), &FONT_B, t, K_ACCENT);
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
        if (plays)                                   /* its beats along its foot: the one now bold, those before lit */
            bar_beats(x, y, ch, bpb, fbeat, fl ? K_BG : K_TEAL, fl ? RGB(250, 196, 170) : K_TEAL_M);
    }
    cv_blit(0, MAIN_Y);
}
/* A melody's view (Sound with M1 or M2 selected; SEQ / EDIT while it records): the song as one line, its notes kept by
 * section (docs/superpowers/specs/2026-10-11-leads-and-recording-design.md). Over it one readout, "song bar 9 . Chorus
 * bar 1" (stopped: where PLAY FROM SECTION and REC's punch in start, coral; playing: the bar and beat, teal; recording:
 * where the next note lands, coral). Then the whole song end to end, a box a section as wide as its bars (REPEAT
 * counted; 5 px at least), tinted by its groove (slate A, amber B, light), its progression's name on it ("Chorus x2",
 * shortened to fit; a run of the same progression too narrow for each: one name over the run), the melody's notes as
 * ticks along its foot (plum; the other melody's pale), the playhead a teal line through it, the section shown outlined
 * (teal playing, the coral cursor stopped), a mark under it where the lane below is. The lane: four bars of the song
 * from the one before the place on, across the sections' boundaries (a divider, the next section's name on its first
 * bar), the notes as ticks (by place and pitch; the other melody's pale under this one's), the bar playing teal with
 * its beats, the cursor's bar ringed coral, REC's count-in big in the bar it starts on, notes just recorded flashing
 * their bars. Intro and Ending are not sections: not on the line */
typedef struct {
    int n, bpb;
    uint32_t blen, total;                            /* a bar's ticks; the song's bars */
    uint16_t b0[ARR_ROWS + 1];                       /* each section's first bar in the song (b0[n]: total) */
    uint32_t len[ARR_ROWS];                          /* each section's ticks (its progression's beats x REPEAT) */
} smap_t;
static void song_map(smap_t *m)
{
    const song_t *g = csong();
    int bpb = app.hdr[pslot()].beats;
    m->n = g->nrows > ARR_ROWS ? ARR_ROWS : g->nrows;
    m->bpb = bpb < 2 || bpb > 7 ? 4 : bpb;
    m->blen = (uint32_t)m->bpb * ARR_PPQ;
    m->total = 0;
    for (int i = 0; i < m->n; i++) {                 /* (an empty progression: a bar of 4, as the engine) */
        const row_t *w = &g->row[i];
        int per = loop_len(&sloops.loop[w->loop % ARR_LOOPS]);
        m->len[i] = (uint32_t)(per > 0 ? per : 4) * (w->rep ? w->rep : 1u) * ARR_PPQ;
        m->b0[i] = (uint16_t)m->total;
        m->total += (m->len[i] + m->blen - 1) / m->blen;
    }
    m->b0[m->n] = (uint16_t)m->total;
}
static int smap_sect(const smap_t *m, uint32_t bar)  /* the section song bar `bar` is in */
{
    int s = 0;
    while (s + 1 < m->n && m->b0[s + 1] <= bar)
        s++;
    return s;
}
static uint32_t bytes_hash(uint32_t h, const void *p, uint32_t n);
/* what flash_watch watches for a melody (its kind 4 in the low bits): the song, the melody, the sections' bars */
static uint32_t lead_watch_id(const smap_t *m, int k)
{
    uint32_t h = bytes_hash(hash(2166136261u, (uint32_t)proj.song | (uint32_t)k << 4 | (uint32_t)m->bpb << 8),
                            m->b0, (uint32_t)(m->n + 1) * sizeof m->b0[0]);
    return (h & ~15u) | 4u;
}
/* a section's name on the line: its progression's ("Chorus"; its own default name its number, "P9"), into t */
static char *sect_label(int loop, char *t)
{
    char b[24], *s = b;
    loop_label(loop, b);
    while (*s && *s != ' ')
        s++;
    return cat_s(t, *s ? s + 1 : b);
}
/* where the view is: the section, the playhead's bar in the song and its beat (live), REC's count-in, the bar the
 * readout names */
typedef struct {
    int r, live, why, focus, fbeat, cin, cbeats, at, atbeat;
} lplace_t;
static void lead_place(const smap_t *m, lplace_t *v)
{
    const song_t *g = csong();
    int n = m->n, play = arr_playing;
    v->live = play && arr_mode == ARR_SONG && n && arr_loop_now >= 0 && arr_sect != SEC_INTRO && arr_sect != SEC_END;
    v->why = app.mode != ARR_SONG ? 1 : !n ? 2 : !play ? 3 : arr_sect == SEC_INTRO ? 4 : arr_sect == SEC_END ? 5
           : !v->live ? 6 : arr_rec_wait ? 7 : 0;
    v->r = !n ? 0 : play ? (arr_row < n ? arr_row : n - 1) : lead_row();
    v->focus = v->cin = -1;
    v->fbeat = v->cbeats = 0;
    if (v->live) {
        uint32_t rt = arr_row_tick, nb = (uint32_t)(m->b0[v->r + 1] - m->b0[v->r]), fb = rt / m->blen;
        v->focus = (int)(m->b0[v->r] + (fb >= nb ? nb - 1 : fb));
        v->fbeat = (int)(rt % m->blen / ARR_PPQ);
    }
    if (v->why == 7) {                               /* the count-in: the bar it starts on (the song's next) */
        v->cbeats = m->bpb - v->fbeat;
        v->cin = (uint32_t)v->focus + 1u < m->total ? v->focus + 1 : g->at_end ? 0 : -1;
    }
    if (v->why == 7 && arr_pre) {                    /* PLAY's count-in: the section's first bar, its beats left */
        v->live = 0;
        v->focus = -1;
        v->fbeat = 0;
        v->cin = m->b0[v->r];
        v->cbeats = count_beats();
    }
    v->at = v->cin >= 0 ? v->cin : v->live ? v->focus : n ? m->b0[v->r] : 0;
    v->atbeat = v->cin < 0 && v->live ? v->fbeat : -1;
}
/* the readout: "song bar 9|Chorus bar 1" ("|": a dot), the beat while playing ("Chorus bar 3.2"); t 48 bytes at least
 * (the longest: "song bar 14336|" + a name of 8 + " bar 448.7") */
static void lead_where_at(const smap_t *m, const lplace_t *v, char *t)
{
    int s;
    char *p;
    if (!m->n) {
        cat_s(t, "no sections yet: add them on Song");
        return;
    }
    s = smap_sect(m, (uint32_t)v->at);
    p = cat_s(itoa_u((uint32_t)v->at + 1u, cat_s(t, "song bar ")), "|");
    p = itoa_u((uint32_t)(v->at - m->b0[s]) + 1u, cat_s(sect_label(csong()->row[s].loop, p), " bar "));
    if (v->atbeat >= 0) {
        *p++ = '.';
        itoa_u((uint32_t)v->atbeat + 1u, p);
    }
}
static void lead_where(char *t)                      /* (the simulator's expect) */
{
    smap_t m;
    lplace_t v;
    song_map(&m);
    lead_place(&m, &v);
    lead_where_at(&m, &v, t);
}
/* the line's boxes: x and width a section, as wide as its bars, 5 px at least (32 sections of 5: 1 px apart) */
#define OV_X 8
#define OV_W 224
static int32_t ov_y = 37, ov_h = 22, ov_bars = 4;  /* (Notes: higher, smaller, the lane's bars) */
#define OV_Y ov_y
#define OV_H ov_h
#define OV_MIN 5
#define LN_Y 66                                      /* the lane's four bars */
#define LN_H 51
#define K_SLATE_L RGB(205, 212, 222)                 /* Groove A, B as a light tint (the song's line) */
#define K_AMBER_L RGB(240, 218, 182)
#define K_PLUM_P RGB(200, 178, 208)                  /* the other melody, pale */
static void strip_layout(const smap_t *m, int16_t *sx, int16_t *sw)
{
    int n = m->n, gap = n > 16 ? 1 : 2, i;
    int32_t left = OV_W - (n - 1) * gap, x = OV_X;
    uint32_t bars = m->total, acc = 0, pre;
    uint8_t fix[ARR_ROWS] = {0};
    for (int again = 1; again && bars;) {            /* those under the least: the least, the rest shared again */
        again = 0;
        for (i = 0; i < n; i++) {
            uint32_t nb = (uint32_t)(m->b0[i + 1] - m->b0[i]);
            if (!fix[i] && nb * (uint32_t)left < (uint32_t)OV_MIN * bars) {
                fix[i] = 1;
                left -= OV_MIN;
                bars -= nb;
                again = 1;
            }
        }
    }
    for (i = 0; i < n; i++) {
        int32_t w = OV_MIN;
        if (!fix[i]) {
            pre = acc;
            acc += (uint32_t)(m->b0[i + 1] - m->b0[i]);
            w = (int32_t)(acc * (uint32_t)left / bars - pre * (uint32_t)left / bars);
        }
        sx[i] = (int16_t)x;
        sw[i] = (int16_t)w;
        x += w + gap;
    }
}
static int32_t line_x(const smap_t *m, const int16_t *sx, const int16_t *sw, int s, uint32_t tick)
{
    uint32_t len = (uint32_t)(m->b0[s + 1] - m->b0[s]) * m->blen;
    return sx[s] + (int32_t)((tick > len ? len : tick) * (uint32_t)sw[s] / (len ? len : 1u));
}
/* a name fitted to w px: "Chorus x2", "Chorus", "Cho", "C" (nothing: 0) */
static int label_fit(char *t, int loop, int rep, int32_t w)
{
    char *e = sect_label(loop, t);
    uint32_t n = slen(t);
    if (rep > 1) {
        itoa_u((uint32_t)rep, cat_s(e, " x"));
        if (text_w(&FONT_XS, t) <= w)
            return 1;
        *e = 0;
    }
    while (n && text_w(&FONT_XS, t) > w)
        t[--n] = 0;
    while (n && t[n - 1] == ' ')
        t[--n] = 0;
    return n != 0;
}
static void draw_song_line(const smap_t *m, const lplace_t *v, int k, uint32_t frows, uint32_t w0)
{
    const song_t *g = csong();
    int16_t sx[ARR_ROWS], sw[ARR_ROWS];
    int n = m->n, i, j;
    char t[24];
    strip_layout(m, sx, sw);
    for (i = 0; i < n; i++) {                        /* the boxes: the groove's tint (an edit: coral) */
        int fl = frows >> i & 1u;
        cv_round(sx[i], OV_Y, sw[i], OV_H, sw[i] >= 8 ? 3 : 1,
                 fl ? K_ACCENT : row_main(&g->row[i]) == SEC_B ? K_AMBER_L : K_SLATE_L);
    }
    for (i = 0; i < n; i = j) {                      /* the names: a section each, or one over a run of the same
                                                      * progression too narrow for each */
        int all = 1, reps = 0;
        for (j = i; j < n && g->row[j].loop == g->row[i].loop; j++) {
            int rep = g->row[j].rep ? g->row[j].rep : 1;
            reps += rep;
            if (rep > 1)
                itoa_u((uint32_t)rep, cat_s(sect_label(g->row[j].loop, t), " x"));
            else
                sect_label(g->row[j].loop, t);
            all = all && text_w(&FONT_XS, t) <= sw[j] - 5;
        }
        if (all || j - i == 1) {
            for (int s = i; s < j; s++)
                if (label_fit(t, g->row[s].loop, g->row[s].rep, sw[s] - 5))
                    cv_text(sx[s] + 3, OV_Y, &FONT_XS, t, frows >> s & 1u ? K_BG : K_TEXT);
        } else if (label_fit(t, g->row[i].loop, reps, sx[j - 1] + sw[j - 1] - sx[i] - 5))
            cv_text(sx[i] + 3, OV_Y, &FONT_XS, t, K_TEXT);
    }
    for (int pass = 0; pass < 2; pass++)             /* the notes: ticks along the foot (the other melody's first, pale) */
        for (i = 0; i < n; i++)
            for (j = leads.row0[i]; j < leads.row0[i + 1] && j < (int)leads.n; j++) {
                const lead_ev_t *e = lead_at_c(&leads, (uint32_t)j);
                uint32_t pos = lead_pos(e);
                int32_t x;
                if ((lead_trk(e) == k) != pass || pos >= m->len[i])
                    continue;
                x = line_x(m, sx, sw, i, pos);
                if (x > sx[i] + sw[i] - 1)
                    x = sx[i] + sw[i] - 1;
                if (pass)
                    cv_rect(x, OV_Y + OV_H - 8, 1, 6, frows >> i & 1u ? K_BG : K_PLUM);
                else
                    cv_rect(x, OV_Y + OV_H - 5, 1, 3, K_PLUM_P);
            }
    if (n) {                                         /* the section shown: outlined (teal playing, coral the cursor) */
        int r = v->r;
        uint16_t c = v->live ? K_TEAL : K_ACCENT;
        int32_t x = sx[r] - 2, w = sw[r] + 4, y = OV_Y - 2, h = OV_H + 4;
        cv_rect(x, y, w, 2, c);
        cv_rect(x, y + h - 2, w, 2, c);
        cv_rect(x, y, 2, h, c);
        cv_rect(x + w - 2, y, 2, h, c);
    }
    if (n && v->live) {                              /* the playhead: a teal line through the song */
        int s = smap_sect(m, (uint32_t)v->focus);
        uint32_t tick = (uint32_t)(v->focus - m->b0[s]) * m->blen + (uint32_t)v->fbeat * ARR_PPQ;
        cv_rect(line_x(m, sx, sw, s, tick), OV_Y - 3, 2, OV_H + 6, K_TEAL);
    }
    if (n) {                                         /* where the lane is: a mark under the line */
        uint32_t w1 = w0 + (uint32_t)ov_bars > m->total ? m->total : w0 + (uint32_t)ov_bars;
        int s0 = smap_sect(m, w0), s1 = smap_sect(m, w1 - 1u);
        int32_t x0 = line_x(m, sx, sw, s0, (w0 - m->b0[s0]) * m->blen);
        int32_t x1 = line_x(m, sx, sw, s1, (w1 - m->b0[s1]) * m->blen);
        cv_rect(x0, OV_Y + OV_H + 3, x1 - x0 > 2 ? x1 - x0 : 2, 2, K_FAINT);
    }
}
static void draw_lead(int rec)
{
    static const char *const WHY[8] = {"", "in a song only", "no sections yet", "starts with PLAY", "after the Intro",
                                       "the song is ending", "from the next bar", "starts at the next bar"};
    const song_t *g = csong();
    int k = (app.lead ? app.lead - 1 : 0) % ARR_LEADS, b, nl, s;
    uint32_t h, i, lo = 127, hi = 0, fbars = 0, frows = 0, w0 = 0, sh;
    const lead_trk_t *lt = &g->lead[k];
    smap_t m;
    lplace_t v;
    char t[48], *p;
    song_map(&m);
    lead_place(&m, &v);
    if (m.n) {                                       /* the lane: from the bar before the place, four (the song's last) */
        uint32_t a = (uint32_t)(v.live ? v.focus : m.b0[v.r]);
        w0 = a ? a - 1u : 0u;
        if (w0 + 4u > m.total)
            w0 = m.total > 4u ? m.total - 4u : 0u;
    }
    nl = (int)(m.total - w0 < 4u ? m.total - w0 : 4u);
    if (flash_on() && fw.id == lead_watch_id(&m, k)) {
        fbars = fw.bars;                             /* (what just changed: sections, song bars mod 32) */
        frows = fw.rows;
    }
    sh = 2166136261u;                                /* the song's shape and names as the line shows them */
    for (s = 0; s < m.n; s++) {
        const row_t *w = &g->row[s];
        sh = hash(sh, w->loop | (uint32_t)w->rep << 8 | (uint32_t)row_main(w) << 16 | (uint32_t)m.b0[s + 1] << 18);
        sh = bytes_hash(sh, sloops.loop[w->loop % ARR_LOOPS].name, 8);
    }
    for (s = 0; s < m.n; s++)                        /* the lane's notes (both melodies): their span of pitches */
        if (m.b0[s + 1] > w0 && m.b0[s] < w0 + 4u)
            for (i = leads.row0[s]; i < leads.row0[s + 1] && i < leads.n; i++) {
                const lead_ev_t *e = lead_at_c(&leads, i);
                uint32_t bar = m.b0[s] + lead_pos(e) / m.blen;
                if (lead_pos(e) >= m.len[s] || bar < w0 || bar >= w0 + 4u)
                    continue;
                lo = e->note < lo ? e->note : lo;
                hi = e->note > hi ? e->note : hi;
            }
    h = hash(2166136261u, 0x1EADu | (uint32_t)k << 16 | (uint32_t)v.r << 17 | (uint32_t)v.live << 23 | (uint32_t)v.why << 24);
    h = hash(hash(hash(hash(h, fbars), frows), (uint32_t)rec), sh);
    h = hash(h, (uint32_t)(v.focus + 1) | (uint32_t)v.fbeat << 16 | (uint32_t)m.n << 20 | (uint32_t)v.cbeats << 26);
    h = hash(hash(h, (uint32_t)(v.cin + 1) | w0 << 16), lo | hi << 8);
    h = hash(hash(h, leads.gen | (uint32_t)leads.n << 16), lt->program | (uint32_t)lt->chan << 8 | (uint32_t)proj.quant << 12);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    lead_where_at(&m, &v, t);
    if (!rec) {   /* Sound's head, "M1  Synth Lead  GM 81"; under it the readout (stopped: the cursor, coral; playing, teal) */
        int32_t x;
        sound_head(TRACK_NAME[ARR_LD1 + k], lt->program & 127u, (lt->chan & 15u) == 9u, 1);
        x = segs(8, 19, &FONT_S, t, !m.n ? K_DIM : v.live ? K_TEAL : K_ACCENT);
        if (m.n && (v.why == 1 || v.why == 4 || v.why == 5)) {   /* (not in a song, the Intro, the Ending: why it's
                                                                 * the cursor's place) */
            cat_s(cat_s(t, "|"), WHY[v.why]);
            if (x + segs_w(&FONT_S, t) <= 232)
                segs(x, 19, &FONT_S, t, K_DIM);
        }
    } else {   /* REC's dot, "REC M1" (and why nothing records yet); under it the readout, coral: where the next note
                * lands; the grid at its right */
        char r1[40];
        cv_round(8, 6, 10, 10, 5, K_REC);
        p = cat_s(cat_s(r1, "REC "), TRACK_NAME[ARR_LD1 + k]);
        if (v.why)
            cat_s(cat_s(p, "|"), WHY[v.why]);
        segs(24, 2, &FONT_B, r1, K_TEXT);
        if (v.why == 1)
            cv_text(8, 19, &FONT_S, "Start the song on Song, then REC", K_ACCENT);
        else {
            int32_t x = segs(8, 19, &FONT_S, t, m.n ? K_ACCENT : K_DIM), gw;
            cat_s(cat_s(r1, "grid "), GRID_NAME[proj.quant % ARR_NQ]);
            gw = text_w(&FONT_S, r1);
            if (x + 8 <= 232 - gw)
                cv_text(232 - gw, 19, &FONT_S, r1, K_DIM);
        }
    }
    if (!m.n) {
        cv_blit(0, MAIN_Y);
        return;
    }
    draw_song_line(&m, &v, k, frows, w0);
    for (b = 0; b < nl; b++) {                       /* the lane: four bars of the song */
        uint32_t sb = w0 + (uint32_t)b, b0, bi;
        int sc = smap_sect(&m, sb), plays = (int)sb == v.focus, first = sb == m.b0[sc];
        int start = ((rec ? v.why >= 3 : !v.live) && v.why < 7 && (int)sb == m.b0[v.r]) || (int)sb == v.cin;
        int fl = (fbars >> (sb & 31u) & 1u) && (frows >> sc & 1u);
        int32_t x = RG_X + b * (RG_CW + 4), y = LN_Y, ch = LN_H, ax, aw, ay, ah, nh;
        uint16_t fg = fl ? K_BG : K_PLUM, other = fl ? RGB(250, 196, 170) : K_PLUM_P;
        uint16_t fill = fl ? K_ACCENT : plays ? K_TEAL_T : K_PANEL;
        bi = sb - m.b0[sc];
        b0 = bi * m.blen;
        if (start) {                                 /* the bar it starts in (the cursor): the cursor's coral ring */
            cv_round(x, y, RG_CW, ch, 5, K_ACCENT);
            cv_round(x + CUR_RING, y + CUR_RING, RG_CW - 2 * CUR_RING, ch - 2 * CUR_RING, 4, fill);
        } else if (plays && !fl)                     /* the bar playing: tinted, a teal outline */
            bar_lit(x, y, RG_CW, ch);
        else
            cv_round(x, y, RG_CW, ch, 5, fill);
        if (first) {                                 /* a section's first bar: a divider before it, its name */
            if (b)
                cv_rect(x - 3, y - 3, 2, ch + 6, K_TEXT);
            label_fit(t, g->row[sc].loop, 0, RG_CW - 8);
            cv_text(x + 4, y + 1, &FONT_XS, t, fl ? K_BG : start ? K_ACCENT : K_TEXT);
        } else {                                     /* else its number in the section */
            itoa_u(bi + 1u, t);
            cv_text(x + 4, y + 1, &FONT_XS, t, fl ? K_BG : start ? K_ACCENT : K_DIM);
        }
        if ((int)sb == v.cin) {                      /* the count-in, big */
            itoa_u((uint32_t)v.cbeats, t);
            text_c(x + RG_CW / 2, bar_y(y, ch, &FONT_L), &FONT_L, t, K_ACCENT);
            continue;
        }
        ax = x + 4;
        aw = RG_CW - 8;
        ay = y + 16;
        ah = y + ch - 11 - ay;                       /* (clear of the beats along the foot) */
        nh = ah >= 24 ? 3 : 2;
        for (int pass = 0; pass < 2; pass++)         /* the notes in this bar: ticks (the other melody's first, pale) */
            for (i = leads.row0[sc]; i < leads.row0[sc + 1] && i < leads.n; i++) {
                const lead_ev_t *e = lead_at_c(&leads, i);
                uint32_t pos = lead_pos(e), len = lead_len(e);
                int32_t nx, nw, ny;
                if ((lead_trk(e) == k) != pass || pos < b0 || pos >= b0 + m.blen || pos >= m.len[sc])
                    continue;
                nx = ax + (int32_t)((pos - b0) * (uint32_t)aw / m.blen);
                nw = (int32_t)(len * (uint32_t)aw / m.blen);
                nw = nw < 2 ? 2 : nw > ax + aw - nx ? ax + aw - nx : nw;
                ny = ay + ah - nh - (hi > lo ? (int32_t)((e->note - lo) * (uint32_t)(ah - nh) / (hi - lo)) : (ah - nh) / 2);
                cv_rect(nx, ny, nw, nh, pass ? fg : other);
            }
        if (plays)                                   /* its beats along its foot: the one now bold, those before lit */
            bar_beats(x, y, ch, m.bpb, v.fbeat, fl ? K_BG : K_TEAL, fl ? RGB(250, 196, 170) : K_TEAL_M);
    }
    cv_blit(0, MAIN_Y);
}
/* ---- Notes (SEQ on Band; SEQ again: Band): the note editor, after Melodee's SEQ > NOTES (keremimo, GPL-3.0,
 * github.com/keremimo/melodee: its piano roll and its selection, ui_graph.c notes_window / notes_bar and seq_notes.c,
 * adapted to Polly's notes and colours). The selected instrument's or melody's notes as a piano roll over a window of
 * the groove or the song (PRESETS zooms: 8, 4, 2, 1 bars, an instrument down to a beat; the window holds the cursor,
 * recording the playhead): by place and pitch, a melody's plum over the other melody's pale, an instrument's ink; the
 * cursor a coral line at its place on the grid (its cell capped coral along the foot), the selected note ringed coral,
 * the playhead teal, the notes an edit just changed flashing (ne_watch). Over it the readout, coral: "M1 . song bar 9 . Chorus
 * bar 1.3 . E5" ("BA . bar 2.3 . A2"; a drum's sound by name, "Snare"); for a melody the song line between, its window
 * marked under it. Notes are selected, not steps: SELECT goes through them in time order (a chord's notes one by one);
 * KNOB 1 moves the cursor through empty places too, where the keys enter notes (ui.c, editor.c) */
static struct {
    uint32_t t;                                      /* the cursor: a place on the grid (song ticks / the groove's) */
    nkey_t k;                                        /* the note selected (has) */
    uint8_t has, zoom, kind, entering, erasing;
    uint16_t who;                                    /* whose notes (song, instrument, groove): another, afresh */
    uint32_t er;                                     /* erasing: the place erased up to */
} ne = {0, {0, 0}, 0, 2, 0, 0, 0, 0xFFFFu, 0};
static smap_t ne_sm;                                 /* (a melody's song: bars, and in song ticks) */
static nmap_t ne_m;
static uint32_t ne_grid(void)                        /* the cursor's step: Band's GRID (off: a 32nd) */
{
    static const uint16_t G[ARR_NQ] = {240, 160, 120, 80, 60, 60};
    return G[proj.quant % ARR_NQ];
}
static int ne_trk(void) { return app.lead ? (app.lead - 1) % ARR_LEADS : proj.trk & 7; }
static uint32_t ne_blen(void) { return app.lead ? ne_sm.blen : (uint32_t)pat_beats(pslot()) * ARR_PPQ; }
static uint32_t ne_end(void)
{
    return app.lead ? ne_m.start[ne_m.n] : (uint32_t)(app.hdr[pslot()].bars ? app.hdr[pslot()].bars : 1u) * ne_blen();
}
static int ne_drums(void) { return !app.lead && (app.trk[pslot()][proj.trk & 7].chan & 15u) == 9u; }
static int ne_find(void)
{
    return app.lead ? nl_find(&leads, &ne_m, ne_trk(), ne.k) : np_find(&view, ne_trk(), ne.k);
}
/* the selected note, if the Bass rule plays the chord's root in its place (xpose.c: the first note of a Bass-type
 * instrument after a chord change, the root whatever it was written as): a Bass-type track's (not switched to "as
 * played"), not written as a C, the first of its track in a bar's first beat (where the progression's chords change). Notes says
 * so by the note, and asks once a groove at an edit there (editor.c ne_lands_ask) */
static int ne_lands(void)
{
    const arr_trk_t *k = &app.trk[pslot()][proj.trk & 7], *vt = &view.trk[proj.trk & 7];
    const arr_ev_t *x;
    int i;
    uint32_t b0, bl;
    if (app.lead || !ne.has || k->x.type != XT_BASS || (k->flags & ARR_TF_ASPLAYED) || (i = ne_find()) < 0)
        return 0;
    x = &view.ev[i];
    bl = (uint32_t)(app.hdr[pslot()].beats ? app.hdr[pslot()].beats : 4) * ARR_PPQ;
    if (x->note % 12 == 0 || x->tick % bl >= ARR_PPQ)   /* (a bar's first beat: where a progression's chords change) */
        return 0;
    b0 = x->tick - x->tick % ARR_PPQ;
    for (uint32_t j = vt->first; j < (uint32_t)vt->first + vt->count && j < view.nev; j++)
        if (view.ev[j].tick >= b0 && view.ev[j].tick < x->tick)
            return 0;
    return 1;
}
static int ne_near(nkey_t *k, int dir)
{
    return app.lead ? nl_near(&leads, &ne_m, ne_trk(), k, dir) : np_near(&view, ne_trk(), ne_end(), k, dir);
}
static void ne_pick(void)                            /* the cursor's cell: its first note selected (none: an empty place) */
{
    nkey_t k = {ne.t, 0};
    ne.has = (uint8_t)(ne_near(&k, 0) && k.t < ne.t + ne_grid());
    if (ne.has)
        ne.k = k;
}
/* the song's map, the notes as they are now; the cursor in range on the grid, its note still there (an undo, a take, an
 * erase: else the cell's first). Another song, instrument or groove: the cursor afresh, at the melody readout's place
 * (the Song page's cursor section, the bar playing) or Band's cursor */
static void ne_ctx(void)
{
    uint32_t g = ne_grid(), end;
    uint16_t who = (uint16_t)(sel_trk() | proj.song << 4 | (app.lead ? 7 : pslot()) << 6);
    if (app.lead) {
        song_map(&ne_sm);
        ne_m.n = ne_sm.n;
        for (int s = 0; s <= ne_sm.n; s++) {
            ne_m.start[s] = ne_sm.b0[s] * ne_sm.blen;
            if (s < ne_sm.n)
                ne_m.len[s] = ne_sm.len[s];
        }
    } else
        view_refresh(pslot());
    end = ne_end();
    if (who != ne.who) {
        ne.who = who;
        ne.kind = ne.has = 0;
        if (app.lead) {
            lplace_t v;
            lead_place(&ne_sm, &v);
            ne.t = (uint32_t)v.at * ne_sm.blen;
        } else
            ne.t = (uint32_t)pat_cur(pslot()) * ARR_PPQ;
    }
    ne.t = ne.t / g * g;
    if (ne.t + g > end)
        ne.t = end > g ? (end - 1u) / g * g : 0;
    if (!ne.has || ne.k.t / g * g != ne.t || ne_find() < 0)
        ne_pick();
}
static uint32_t ne_ph(void)                          /* the playhead's place; ~0 none */
{
    uint32_t b, bt;
    if (app.lead) {
        lplace_t v;
        lead_place(&ne_sm, &v);
        return v.live && arr_row < ne_m.n ? ne_m.start[arr_row] + arr_row_tick : ~0u;
    }
    if (!arr_playing)
        return ~0u;
    do {
        b = arr_bar;
        bt = arr_beat_tick;
    } while (b != arr_bar);
    return b % (app.hdr[pslot()].bars ? app.hdr[pslot()].bars : 1u) * ne_blen() + bt;
}
static uint32_t ne_span(void)                        /* the window: 8, 4, 2, 1 bars, a beat (the whole, if shorter) */
{
    uint32_t sp = ne.zoom < 4 ? (8u >> ne.zoom) * ne_blen() : ARR_PPQ, end = ne_end();
    return end && sp > end ? end : sp;
}
static void ne_name(int note, char *t)               /* a pitch, "E5"; a drum's sound, "Snare" */
{
    if (ne_drums() && note >= GM_DRUM0 && note < GM_DRUM0 + 47)
        cat_s(t, GM_DRUM[note - GM_DRUM0]);
    else
        note_name(note, t);
}
/* the readout (t 80 bytes: "ERASING M1|song bar 4096|" + a name of 8 + " bar 448.7|Mute triangle"); full 0: without
 * the song's bar */
static void ne_where(char *t, int full)
{
    uint32_t blen = ne_blen(), b = ne.t / blen;
    char *p = cat_s(cat_s(t, ne.erasing ? "ERASING " : ""), TRACK_NAME[sel_trk()]);
    if (app.lead) {
        int s;
        if (!ne_m.n) {
            cat_s(p, "|no sections yet: add them on Song");
            return;
        }
        s = smap_sect(&ne_sm, b);
        if (full)
            p = itoa_u(b + 1u, cat_s(p, "|song bar "));
        p = cat_s(sect_label(csong()->row[s].loop, cat_s(p, "|")), " bar ");
        b -= ne_sm.b0[s];
    } else
        p = cat_s(p, "|bar ");
    p = itoa_u(b + 1u, p);
    *p++ = '.';
    p = itoa_u(ne.t % blen / ARR_PPQ + 1u, p);
    if (ne.has) {
        p = cat_s(p, "|");
        ne_name(ne.k.note, p);
        if (ne_lands())                              /* (heard as the root where the chord changes on its beat) */
            cat_s(p + slen(p), ", heard as the root");
    }
}
#define NL_X 8
#define NL_W 224
static struct { uint32_t w0, span, lo, hi; int32_t ly, lh, nh; } nd;   /* the lane being drawn */
static int32_t ne_x(uint32_t t) { return NL_X + (int32_t)((t < nd.w0 ? 0u : t - nd.w0) * NL_W / nd.span); }
/* an edit on Notes lights the notes it changed, not their bars (the visibility spec): the selected track's notes in
 * the window and a window either side (a note moved out of it) kept, as place, length, pitch, velocity; when they
 * change, those new or changed glow coral FLASH_MS (more as they come: those still there stay lit), a note gone with
 * nothing new in its place its coral outline. More notes than kept (a busy drum part): the selected note lights */
#define NF_SNAP 128
#define NF_MAX 12
typedef struct { uint32_t t; uint16_t len; uint8_t note, vel; } nf_ev_t;
static struct {
    uint32_t ctx, gen, lo, hi;                       /* whose notes and the map; their generation; the range kept */
    uint8_t ok, n, over, nf, gone;                   /* (over: more than NF_SNAP in the range) */
    uint32_t t;                                      /* the flash: since when */
    nf_ev_t ev[NF_SNAP];                             /* the range's notes */
    nf_ev_t f[NF_MAX];                               /* the notes flashing (gone: outlines) */
} nfw;
static nf_ev_t nf_nxt[NF_SNAP];
static int nf_on(void) { return nfw.nf && nfw.t && plat_ms() - nfw.t < FLASH_MS; }
static int nf_same(const nf_ev_t *a, const nf_ev_t *b)
{
    return a->t == b->t && a->len == b->len && a->note == b->note && a->vel == b->vel;
}
static int nf_in(const nf_ev_t *e, const nf_ev_t *l, int n)
{
    for (int i = 0; i < n; i++)
        if (nf_same(e, &l[i]))
            return 1;
    return 0;
}
static int nf_lit(uint32_t t, int note)              /* a note flashing (not an outline) */
{
    if (!nf_on() || nfw.gone)
        return 0;
    for (int i = 0; i < nfw.nf; i++)
        if (nfw.f[i].t == t && nfw.f[i].note == note)
            return 1;
    return 0;
}
static int nf_take(nf_ev_t *l, uint32_t lo, uint32_t hi, uint8_t *over)   /* the range's notes into l (NF_SNAP) */
{
    int n = 0, k = ne_trk();
    *over = 0;
    if (app.lead)
        for (uint32_t i = 0; i < leads.n; i++) {
            const lead_ev_t *e = lead_at_c(&leads, i);
            uint32_t t;
            if (lead_trk(e) != k || (t = nl_time(&ne_m, e)) == ~0u || t < lo || t >= hi)
                continue;
            if (n == NF_SNAP) {
                *over = 1;
                break;
            }
            l[n].t = t;
            l[n].len = (uint16_t)lead_len(e);
            l[n].note = e->note;
            l[n++].vel = e->vel;
        }
    else {
        const arr_trk_t *tr = &view.trk[k];
        uint32_t end = ne_end();
        for (uint32_t i = tr->first; i < (uint32_t)tr->first + tr->count && i < view.nev; i++) {
            const arr_ev_t *e = &view.ev[i];
            if (e->tick >= end || e->tick < lo || e->tick >= hi)
                continue;
            if (n == NF_SNAP) {
                *over = 1;
                break;
            }
            l[n].t = e->tick;
            l[n].len = e->len;
            l[n].note = e->note;
            l[n++].vel = e->vel;
        }
    }
    return n;
}
static void ne_watch(void)                           /* a frame: what changed among the notes flashes (nfw) */
{
    uint32_t ctx = hash(hash(ne.who, nd.span), ne_end()), gen, lo, hi, a, b;
    nf_ev_t f[NF_MAX];
    int n, i, nf = 0, gone = 0;
    uint8_t over;
    if (app.lead)
        for (i = 0; i <= ne_m.n; i++)
            ctx = hash(ctx, ne_m.start[i]);
    gen = app.lead ? hash(hash(2166136261u, leads.gen), leads.n) : hash(hash(2166136261u, vw.dirty), vw.gen);
    lo = nd.w0 >= nd.span ? nd.w0 - nd.span : 0u;
    hi = nd.w0 + 2u * nd.span;
    if (nfw.ok && nfw.ctx == ctx && nfw.gen == gen && nfw.lo == lo)
        return;
    n = nf_take(nf_nxt, lo, hi, &over);
    if (nfw.ok && nfw.ctx == ctx && nfw.gen != gen) {
        a = lo > nfw.lo ? lo : nfw.lo;               /* (in both ranges: a note across the edge is not new) */
        b = hi < nfw.hi ? hi : nfw.hi;
        if (nf_on() && !nfw.gone)                    /* those lit still there stay lit */
            for (i = 0; i < nfw.nf; i++)
                if (nf < NF_MAX && nf_in(&nfw.f[i], nf_nxt, n))
                    f[nf++] = nfw.f[i];
        if (!over && !nfw.over) {
            for (i = 0; i < n && nf < NF_MAX; i++)  /* new or changed */
                if (nf_nxt[i].t >= a && nf_nxt[i].t < b && !nf_in(&nf_nxt[i], nfw.ev, nfw.n) && !nf_in(&nf_nxt[i], f, nf))
                    f[nf++] = nf_nxt[i];
            if (!nf)                                 /* only gone: their outlines */
                for (i = 0; i < nfw.n && nf < NF_MAX; i++)
                    if (nfw.ev[i].t >= a && nfw.ev[i].t < b && !nf_in(&nfw.ev[i], nf_nxt, n))
                        f[nf++] = nfw.ev[i], gone = 1;
        } else if (ne.has)                           /* (too many to tell: the selected) */
            for (i = 0; i < n && !nf; i++)
                if (nf_nxt[i].t == ne.k.t && nf_nxt[i].note == ne.k.note)
                    f[nf++] = nf_nxt[i];
        if (nf) {
            memcpy(nfw.f, f, (uint32_t)nf * sizeof f[0]);
            nfw.nf = (uint8_t)nf;
            nfw.gone = (uint8_t)gone;
            nfw.t = plat_ms() | 1u;
        }
    } else if (nfw.ctx != ctx)
        nfw.nf = 0;                                  /* (other notes: nothing flashes) */
    memcpy(nfw.ev, nf_nxt, (uint32_t)n * sizeof nf_nxt[0]);
    nfw.n = (uint8_t)n;
    nfw.over = over;
    nfw.ctx = ctx;
    nfw.gen = gen;
    nfw.lo = lo;
    nfw.hi = hi;
    nfw.ok = 1;
}
static int ne_box(uint32_t t, uint32_t len, int note, int32_t *x, int32_t *w, int32_t *y)   /* a note's box; 0 out */
{
    uint32_t e = t + len;
    if (t >= nd.w0 + nd.span || e <= nd.w0)
        return 0;
    *x = ne_x(t);
    *w = ne_x(e > nd.w0 + nd.span ? nd.w0 + nd.span : e) - *x - 1;
    *w = *w < 3 ? 3 : *w > NL_X + NL_W - *x ? NL_X + NL_W - *x : *w;
    *y = nd.ly + nd.lh - 3 - nd.nh - (int32_t)(((uint32_t)note - nd.lo) * (uint32_t)(nd.lh - 18 - nd.nh) / (nd.hi - nd.lo));
    return 1;
}
static void ne_one(int pass, uint32_t t, uint32_t len, int note, int mine)
{
    int32_t x, w, y;
    uint32_t e = t + len;
    if (t >= nd.w0 + nd.span || e <= nd.w0)
        return;
    if (!pass) {                                     /* (their pitches' span) */
        nd.lo = (uint32_t)note < nd.lo ? (uint32_t)note : nd.lo;
        nd.hi = (uint32_t)note > nd.hi ? (uint32_t)note : nd.hi;
        return;
    }
    if (pass < 3 && (mine != pass - 1 || (mine && ne.has && t == ne.k.t && note == ne.k.note)))
        return;                                      /* (pass 1 the other melody's, 2 this one's, 3 the selected) */
    ne_box(t, len, note, &x, &w, &y);
    if (pass == 3) {                                 /* the selected: ringed coral (the cursor's) */
        cv_rect(x - 3, y - 3, w + 6, nd.nh + 6, K_ACCENT);
        cv_rect(x - 1, y - 1, w + 2, nd.nh + 2, K_BG);
    }
    cv_rect(x, y, w, nd.nh, mine && nf_lit(t, note) ? K_ACCENT : !mine ? K_PLUM_P : app.lead ? K_PLUM : K_TEXT);
}
static void ne_glow(int over)                        /* the notes an edit changed: a pale coral glow under them (the
                                                      * selected: round its ring), over them a note gone's outline */
{
    int32_t x, w, y;
    if (!nf_on())
        return;
    for (int i = 0; i < nfw.nf; i++) {
        const nf_ev_t *f = &nfw.f[i];
        int sel = ne.has && f->t == ne.k.t && f->note == ne.k.note, g = sel ? 5 : 2;
        if (over != nfw.gone || f->note < nd.lo || f->note > nd.hi ||
            !ne_box(f->t, ne_drums() ? 1u : f->len, f->note, &x, &w, &y))
            continue;
        if (!nfw.gone)
            cv_rect(x - g, y - g, w + 2 * g, nd.nh + 2 * g, RGB(250, 196, 170));
        else {
            cv_rect(x, y, w, 1, K_ACCENT);
            cv_rect(x, y + nd.nh - 1, w, 1, K_ACCENT);
            cv_rect(x, y, 1, nd.nh, K_ACCENT);
            cv_rect(x + w - 1, y, 1, nd.nh, K_ACCENT);
        }
    }
}
static void ne_each(int pass)                        /* the window's notes, a pass (ne_one) */
{
    int k = ne_trk();
    if (app.lead)
        for (uint32_t i = 0; i < leads.n; i++) {
            const lead_ev_t *e = lead_at_c(&leads, i);
            uint32_t t = nl_time(&ne_m, e);
            if (t != ~0u)
                ne_one(pass, t, lead_len(e), e->note, lead_trk(e) == k);
        }
    else {
        const arr_trk_t *tr = &view.trk[k];
        uint32_t end = ne_end();
        for (uint32_t i = tr->first; i < (uint32_t)tr->first + tr->count && i < view.nev; i++)
            if (view.ev[i].tick < end)
                ne_one(pass, view.ev[i].tick, ne_drums() ? 1u : view.ev[i].len, view.ev[i].note, 1);
    }
}
static void draw_notes(void)
{
    const song_t *g = csong();
    int lead = app.lead != 0, k = ne_trk();
    uint32_t span, end, blen, ph, gr, h, b;
    char t[80];
    ne_ctx();
    span = ne_span();
    end = ne_end();
    blen = ne_blen();
    ph = ne_ph();
    gr = ne_grid();
    nd.w0 = (arr_recording && ph != ~0u ? ph : ne.t) / span * span;
    nd.span = span;
    nd.ly = lead ? 49 : 22;
    nd.lh = 118 - nd.ly;
    ne_watch();                                      /* (an edit here: its notes flash, not the bars: nfw) */
    h = hash(hash(2166136261u, ne.t), ne.k.t);
    h = hash(h, ne.k.note | (uint32_t)ne.has << 8 | (uint32_t)ne.zoom << 9 | (uint32_t)k << 12 | (uint32_t)lead << 16 |
                (uint32_t)ne.erasing << 17 | (uint32_t)proj.quant << 18 | (uint32_t)arr_recording << 22);
    h = hash(hash(hash(hash(h, nd.w0), ph == ~0u || ph < nd.w0 ? ~0u : (ph - nd.w0) * NL_W / span), nfw.t), (uint32_t)nf_on());
    h = hash(hash(h, lead ? leads.gen | (uint32_t)leads.n << 16 : vw.dirty | vw.gen << 16), end);
    h = bytes_hash(hash(h, app.gen), g->row, (uint32_t)g->nrows * sizeof(row_t));
    if (lead)
        for (int s = 0; s < ne_m.n; s++)
            h = bytes_hash(hash(h, ne_m.start[s]), sloops.loop[g->row[s].loop % ARR_LOOPS].name, 8);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    ne_where(t, 1);                                  /* the readout (too long: without the song's bar) */
    if (segs_w(&FONT_S, t) > NL_W)
        ne_where(t, 0);
    segs(NL_X, 1, &FONT_S, t, K_ACCENT);
    if (lead && !ne_m.n) {
        cv_blit(0, MAIN_Y);
        return;
    }
    if (lead) {                                      /* the song line, its section the cursor's (playing: teal's) */
        lplace_t v;
        int s = nmap_sect(&ne_m, ne.t);
        lead_place(&ne_sm, &v);
        if (!v.live && s >= 0)
            v.r = s;
        ov_y = 20;
        ov_h = 20;
        ov_bars = (int32_t)(span / blen ? span / blen : 1u);
        draw_song_line(&ne_sm, &v, k, 0u, nd.w0 / blen);
        if (nf_on()) {                               /* an edit's notes: a small coral mark each on the line */
            int16_t sx[ARR_ROWS], sw[ARR_ROWS];
            strip_layout(&ne_sm, sx, sw);
            for (int i = 0; i < nfw.nf; i++) {
                int fs = nmap_sect(&ne_m, nfw.f[i].t);
                if (fs >= 0)
                    cv_rect(line_x(&ne_sm, sx, sw, fs, nfw.f[i].t - ne_m.start[fs]) - 1, OV_Y + OV_H - 10, 3, 9, K_ACCENT);
            }
        }
        ov_y = 37;
        ov_h = 22;
        ov_bars = 4;
    }
    cv_round(NL_X - 3, nd.ly, NL_W + 6, nd.lh, 4, K_PANEL);
    for (b = nd.w0 / blen; b * blen < nd.w0 + span; b++) {   /* the bars: flashing, past the end, lines, names */
        uint32_t a = b * blen, s = lead ? (uint32_t)smap_sect(&ne_sm, b) : 0u;
        int32_t x0 = ne_x(a), x1 = ne_x(a + blen > nd.w0 + span ? nd.w0 + span : a + blen);
        int first = lead && b == ne_sm.b0[s];
        if (a >= end) {
            cv_rect(x0, nd.ly, x1 - x0 + 3, nd.lh, K_BG);
            break;
        }
        if (span <= 2 * blen)
            for (uint32_t q = a + ARR_PPQ; q < a + blen && q < nd.w0 + span; q += ARR_PPQ)
                if (q > nd.w0)
                    cv_rect(ne_x(q), nd.ly + 14, 1, nd.lh - 16, K_LINE);
        if (a < nd.w0)
            continue;
        if (a > nd.w0)
            cv_rect(x0 - (first ? 1 : 0), nd.ly + 1, first ? 2 : 1, nd.lh - 2, first ? K_TEXT : K_FAINT);
        if (first)
            label_fit(t, g->row[s].loop, 0, x1 - x0 - 6);
        else
            itoa_u(b - (lead ? ne_sm.b0[s] : 0u) + 1u, t);
        cv_text(x0 + 3, nd.ly, &FONT_XS, t, first ? K_TEXT : K_DIM);
    }
    nd.lo = 127;                                     /* the pitches: an octave at least, around the notes */
    nd.hi = 0;
    ne_each(0);
    if (ne.has)
        ne_one(0, ne.k.t, 1, ne.k.note, 1);
    if (nd.hi < nd.lo)
        nd.lo = nd.hi = 60;
    if (nd.hi - nd.lo < 12) {
        uint32_t c = (nd.lo + nd.hi) / 2u;
        nd.lo = c < 6u ? 0u : c - 6u > 115u ? 115u : c - 6u;
        nd.hi = nd.lo + 12u;
    }
    nd.nh = (nd.lh - 18) / (int32_t)(nd.hi - nd.lo + 1u);
    nd.nh = nd.nh < 3 ? 3 : nd.nh > 6 ? 6 : nd.nh;
    ne_glow(0);                                      /* (under the cursor: it stays whole) */
    if (ne.t >= nd.w0 && ne.t < nd.w0 + span) {      /* the cursor: a coral line, its cell's cap along the foot */
        int32_t cx = ne_x(ne.t), cw = ne_x(ne.t + gr) - cx;
        cv_rect(cx - 1, nd.ly - 3, 3, nd.lh + 3, K_ACCENT);
        if (cw >= 6)
            cv_rect(cx, nd.ly + nd.lh - 4, cw, 4, K_ACCENT);
    }
    ne_each(1);
    ne_each(2);
    ne_glow(1);
    if (ne.has) {
        int i = ne_find();
        uint32_t len = i < 0 ? 1u : lead ? lead_len(lead_at_c(&leads, (uint32_t)i)) : ne_drums() ? 1u : view.ev[i].len;
        ne_one(3, ne.k.t, len, ne.k.note, 1);
    }
    if (ph != ~0u && ph >= nd.w0 && ph < nd.w0 + span)   /* the playhead */
        cv_rect(ne_x(ph), nd.ly - 3, 2, nd.lh + 3, K_TEAL);
    cv_blit(0, MAIN_Y);
}
/* Groove length: the groove's bars as a picture (a cell a bar, its number at the top, its beats along its foot; the bars
 * the Bars knob would add, up to 16, as ghosts), its name under it as a word (Place: the letter Letter turns, boxed) */
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
        int32_t x = 8 + (i % 8) * 28, y = 25 + (i / 8) * 27, w = 25, sw = (w - 6) / bpb;
        if (i < nb) {
            cv_round(x, y, w, 24, 4, K_PANEL2);
            itoa_u((uint32_t)i + 1u, t);         /* its number at the top, its beats along its foot */
            text_c(x + w / 2, y + 1, &FONT_XS, t, K_TEXT);
            for (int k = 0; k < bpb; k++)
                cv_rect(x + 3 + k * sw + (w - 6 - bpb * sw) / 2, y + 18, sw - 1, 3, K_TEXT);
        } else {                                     /* a bar it could have: a ghost */
            cv_round(x, y, w, 24, 4, K_LINE);
            cv_round(x + 1, y + 1, w - 2, 22, 3, K_BG);
        }
    }
    {   /* the name as a word, the place Letter turns boxed (a coral ring: the cursor); the places after it faint dots */
        static const int32_t BY = 82, BH = 30;
        int last = -1;
        int32_t pw[8], tw = 0, x;
        for (i = 0; i < 8; i++)
            if (app.hdr[s].name[i] && app.hdr[s].name[i] != ' ')
                last = i;
        if (ui.name_cur > last)
            last = ui.name_cur;
        for (i = 0; i < 8; i++) {
            char c[2] = {app.hdr[s].name[i] ? app.hdr[s].name[i] : ' ', 0};
            pw[i] = c[0] == ' ' ? 14 : text_w(&FONT_M, c) + 6;
            if (i <= last)
                tw += pw[i];
        }
        cv_round(12, BY - 5, 216, BH + 10, 6, K_PANEL);   /* (to row 116) */
        x = 120 - (tw + (7 - last) * 8) / 2;
        for (i = 0; i < 8; i++) {
            char c[2] = {app.hdr[s].name[i] ? app.hdr[s].name[i] : ' ', 0};
            if (i > last) {
                cv_round(x + 2, BY + BH - 7, 3, 3, 1, K_FAINT);
                x += 8;
                continue;
            }
            if (i == ui.name_cur) {
                cv_round(x - 1, BY - 2, pw[i] + 2, BH + 4, 5, K_ACCENT);
                cv_round(x + 1, BY, pw[i] - 2, BH, 4, K_PANEL);
            }
            if (c[0] != ' ')
                text_c(x + pw[i] / 2, cap_y(BY, BH, &FONT_M), &FONT_M, c, K_TEXT);
            x += pw[i];
        }
    }
    cv_blit(0, MAIN_Y);
}
/* Sound's browser: the family of eight around the sound, two columns of four (1-4, 5-8), the sound the cursor (a coral
 * ring; the knob's value, the tag's name: the same sound, named once more here as where you are among its family) */
#define SB_Y 25                                      /* the first name's y; a row 21 px */
#define SB_RH 21
#define SB_CW 110                                    /* a column (the longest name, "Tango Accordion": 98 px) */
/* a name's box at its y: 23 px from 4 above, its capitals 4 px inside the ring's 2 (as at its sides), its descenders
 * clear; 2 px into the gaps between the rows */
#define SB_BOX(y) ((y) - 4)
#define SB_BH 23
static void sound_list(uint32_t prog)
{
    uint32_t f = prog & 120u, i;
    for (i = 0; i < 8u; i++) {
        int32_t x = 8 + (int32_t)(i / 4u) * (SB_CW + 4), y = SB_Y + (int32_t)(i % 4u) * SB_RH;
        if (f + i == prog) {
            cv_round(x, SB_BOX(y), SB_CW, SB_BH, 5, K_ACCENT);
            cv_round(x + CUR_RING, SB_BOX(y) + CUR_RING, SB_CW - 2 * CUR_RING, SB_BH - 2 * CUR_RING, 4, K_BG);
        }
        text_fit(x + 5, y, &FONT_S, GM_PROGRAM[f + i], SB_CW - 12, K_TEXT);
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
    char a[40];                                      /* ("roots above C#: an octave down": 31) */
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
            cv_text(8, 40, &FONT_S, "Drums: no high key", K_DIM);
            cv_blit(0, MAIN_Y);
            return;
        }
        for (int n = 0; n < 12; n++) {               /* the keys, in a row (the black ones short, dark) */
            int32_t x = x0 + n * kw, over = hk < XT_NOKEY && n > hk;
            uint16_t c = n == hk ? K_ACCENT : over && works ? K_PANEL2 : K_PANEL;
            cv_round(x, y0, kw - 2, BLK[n] ? 22 : 34, 2, BLK[n] ? (n == hk ? K_ACCENT : over && works ? K_FAINT : K_TEXT) : c);
            text_c(x + (kw - 2) / 2, y0 + 36, &FONT_XS, KEY_NAME[n], n == hk ? K_ACCENT : K_DIM);
        }
        if (hk >= XT_NOKEY)
            cat_s(a, "off: roots stay where they are");
        else {
            cat_s(cat_s(cat_s(a, "roots above "), KEY_NAME[hk % 12]), " drop an octave");
        }
        cv_text(8, 84, &FONT_S, a, works ? K_TEXT : K_DIM);
        if (!works)
            cv_text(8, 100, &FONT_XS, "(only for Chord 1 and Bass types)", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    if (drums) {   /* the kit: one entry, no family (nothing to step through), an ink outline: it stays */
        cv_round(8, SB_BOX(SB_Y), SB_CW, SB_BH, 5, K_TEXT);
        cv_round(9, SB_BOX(SB_Y) + 1, SB_CW - 2, SB_BH - 2, 4, K_BG);
        cv_text(13, SB_Y, &FONT_S, "Drum kit", K_TEXT);
        cv_text(8, SB_Y + SB_RH + 4, &FONT_S, "Channel 10: a sound on each key", K_DIM);
    } else {
        sound_list(t->program & 127u);
        if (!lead) {   /* the range over the whole MIDI keyboard, as a bar across (its ends: the Low, High knobs') */
            int32_t y = SB_BOX(SB_Y + 3 * SB_RH) + SB_BH + 3, x0 = 8, w = 224, lx, hx;
            lx = x0 + t->x.lo * w / 127;
            hx = x0 + t->x.hi * w / 127;
            cv_round(x0, y, w, 6, 3, K_PANEL);
            cv_round(lx, y, hx - lx < 6 ? 6 : hx - lx, 6, 3, K_DIM);
        }
    }
    cv_blit(0, MAIN_Y);
}
/* the ten instruments' strips (the pattern's eight, the song's two melodies after a gap, plum): volume, pan; the selected
 * one darker, ink-outlined, its number big under it; muted ones pale. Their sends: the Effects page's */
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
#define MX_H 95                                      /* the strips' height; the selected one's numbers under them */
/* the selected strip's number(s), big, centred under it (kept on the screen); b: a second one at its right */
static void mx_number(int i, const char *a, const char *b)
{
    int32_t cx = strip_x(i) + MX_W / 2, w = text_w(&FONT_M, a) + (b ? 10 + text_w(&FONT_M, b) : 0), x;
    x = cx - w / 2;
    x = x < 4 ? 4 : x > 236 - w ? 236 - w : x;
    x = cv_text(x, cap_y(MX_H + 1, MAIN_H - MX_H - 1, &FONT_M), &FONT_M, a, trk_muted(i) ? K_DIM : K_TEXT);
    if (b)
        cv_text(x + 10, cap_y(MX_H + 1, MAIN_H - MX_H - 1, &FONT_M), &FONT_M, b, trk_muted(i) ? K_DIM : K_TEXT);
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
        int32_t x = strip_x(i), w = MX_W, vh = 58 * t->vol / 127;
        strip_bg(i, i == sel_trk(), MX_H);
        cv_round(x + 5, 16, 11, 60, 4, K_BG);   /* volume */
        if (vh > 0)
            cv_round(x + 5, 16 + 60 - vh - 1, 11, vh, 4, strip_colour(i));
        cv_rect(x + 3, 84, w - 6, 2, K_LINE);   /* pan */
        cv_round(x + 3 + (w - 10) * t->pan / 127, 81, 4, 8, 2, K_TEXT);
    }
    {   /* the selected strip's volume, big, under it (the others: their bars alone) */
        char r[6];
        itoa_u(strip_trk(sel_trk(), &lb)->vol, r);
        mx_number(sel_trk(), r, 0);
    }
    cv_blit(0, MAIN_Y);
}
/* the tone: each track's brightness (CC 74, the filter's cutoff) and resonance (CC 71), 64 neutral (a line); the selected
 * strip's two numbers big under it */
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
        int32_t bh = 70 * b / 127, rh = 70 * r / 127;
        strip_bg(i, i == sel_trk(), MX_H);
        cv_round(x + 3, 16, 6, 72, 3, K_BG);    /* brightness */
        cv_round(x + 12, 16, 6, 72, 3, K_BG);   /* resonance */
        if (bh > 0)
            cv_round(x + 3, 16 + 72 - bh - 1, 6, bh, 3, strip_colour(i));
        if (rh > 0)
            cv_round(x + 12, 16 + 72 - rh - 1, 6, rh, 3, trk_muted(i) ? K_LINE : K_FAINT);
        cv_rect(x + 2, 16 + 72 - 36, w - 4, 1, K_FAINT); /* 64: neutral */
    }
    {   /* the selected strip's brightness and resonance, big, under it (as its two bars) */
        const arr_trk_t *t = strip_trk(sel_trk(), &lb);
        char a[6], b[6];
        itoa_u((uint32_t)arr_bright(t), a);
        itoa_u((uint32_t)arr_reso(t), b);
        mx_number(sel_trk(), a, b);
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
            cv_round(x + 12, 16 + 78 - chh - 1, 6, chh, 3, trk_muted(i) ? K_LINE : K_FAINT);
    }
    {   /* the key: whose sends, which bar is which */
        int32_t x;
        cat_s(cat_s(t, SECT_NAME[s + 1]), "'s sends:");
        x = cv_text(8, 102, &FONT_XS, t, K_DIM) + 8;
        cv_round(x, 108, 6, 6, 2, K_TEXT);
        x = cv_text(x + 9, 102, &FONT_XS, "reverb", K_TEXT) + 10;
        cv_round(x, 108, 6, 6, 2, K_FAINT);
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
    cv_round(4, 1 + (int32_t)first * 19, 232, 3 * 19, 6, K_PANEL);   /* the knobs' three: one panel */
    for (i = 0; i < ARR_NSECT; i++) {
        int32_t y = 1 + (int32_t)i * 19;         /* (a row 19 px, the pill 17 in it; the name on the pill's baseline) */
        int on = i >= first && i < first + 3u;
        sect_pill(8, y + 1, 76, 17, (int)i + 1, 0);
        pat_title((int)i, t);                    /* "Pop", "Funk (yours)" */
        text_fit(92, y + 1, on ? &FONT_B : &FONT_S, t, 138, on ? K_TEXT : K_DIM);   /* the knobs' three: bold */
    }
    cv_blit(0, MAIN_Y);
}
static int32_t wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c);
static int32_t wrap_do(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c, int draw);
static void setup_row(int32_t y, const char *name, const char *val)
{
    cv_round(8, y, 224, 22, 6, K_PANEL);
    cv_text(18, cap_y(y, 22, &FONT_S), &FONT_S, name, K_DIM);
    cv_text(222 - text_w(&FONT_B, val), cap_y(y, 22, &FONT_B), &FONT_B, val, K_TEXT);
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
    static const char *const LIGHTS[3] = {"Only the buttons light.", "The keys light their notes; the buttons glow.",
                                          "The keys light their notes; the buttons don't glow."};
    static const char *const CLICK[3] = {"A click in the count-in only.", "A click in the count-in and while recording.",
                                         "A click on every beat."};
    *name = k == 0 ? "Clock" : k == 1 ? "MIDI out" : k == 2 ? "Lights" : "Click";
    *what = k == 0 ? (proj.clock ? "Follows incoming MIDI clock; keeps its own tempo without it."
                                 : "Keeps its own tempo and ignores incoming MIDI clock.")
          : k == 1 ? (proj.mout ? "Sends each instrument on its own channel, and the clock." : "Sends nothing over MIDI.")
          : k == 2 ? LIGHTS[proj.leds % 3u] : CLICK[proj.click % 3u];
}
static uint8_t setup_shown;                          /* what Setup's box shows: 1 the four knobs' jobs (none turned yet
                                                      * on the page), 2 + k knob k's, in words */
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
    setup_shown = (uint8_t)(k + 2);
    if (k < 0) {   /* no knob turned yet (the page just opened): what each of the four is for, a line each, the name in
                    * ink, its job dim (17 px apart, centred down the box as the block below) */
        static const char *const NM[4] = {"Clock", "MIDI out", "Lights", "Click"};
        static const char *const JOB[4] = {"follow MIDI clock in", "send the band over MIDI", "the keys and buttons",
                                           "when the click plays"};
        int32_t top = 1 + (88 - (3 * 17 + 11)) / 2, x = 18 + text_w(&FONT_B, NM[1]) + 10;   /* (the jobs in a column,
                                                      * after the longest name) */
        for (int i = 0; i < 4; i++) {
            cv_text(18, top - 2 + i * 17, &FONT_B, NM[i], K_TEXT);
            cv_text(x, top - 2 + i * 17, &FONT_S, JOB[i], K_DIM);
        }
    } else {   /* in the box, centred down it (as HOME's chord box): the knob's name and what it does. A block: its first
                * capitals' top to its last line's baseline, lines 17 px apart, the words 21 px under the name */
        const char *nm = 0, *what = "";
        int32_t lines, top;
        setup_says(k, &nm, &what);
        lines = wrap_do(0, 0, 204, &FONT_S, what, 0, 0) / 17;
        top = 1 + (88 - ((nm ? 21 : 0) + (lines - 1) * 17 + 11)) / 2;   /* the block's capitals' top */
        if (nm)
            cv_text(18, top - 2, &FONT_B, nm, K_TEXT);
        wrap(18, top - 2 + (nm ? 21 : 0), 204, &FONT_S, what, nm ? K_TEXT : K_DIM);
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
    cv_text(134, 6, &FONT_XS, "Voices", K_DIM);
    itoa_u(v, t);
    cv_text(134, 24, &FONT_L, t, K_TEXT);
    cv_round(8, 74, 224, 44, 6, K_PANEL);
    cv_text(18, 76, &FONT_XS, "Version", K_DIM);
    cv_text(18, 92, &FONT_B, "Polly " GM_VERSION, K_TEXT);
    itoa_u(x, cat_s(t, "Xruns "));
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
        text_c(px + pw[i] / 2, -3, &FONT_XS, d, K_DIM);   /* its black key's number */
        if (r) {                                     /* (the cursor's ring, round the pill) */
            cv_round(px - CUR_RING, 12 - CUR_RING, pw[i] + 2 * CUR_RING, 17 + 2 * CUR_RING, 7, RING[r]);
            cv_round(px, 12, pw[i], 17, 6, K_BG);
        }
        sect_pill(px, 12, pw[i], 17, SECT[i], 0);
        px += pw[i] + gap;
    }
    {   /* the progressions with chords, down then across: two columns in S, 14 px a row (more than fit: three in XS, 12
         * px); the ring's colour on the number, a badge (as Progression's bar and beat), the name in its colour. The empty
         * ones on one line under them, dim ("P9-P16 empty"): the used ones get the room (rows to 17 px) */
        int used = 0, nempty = 0, rows, big, k = 0;
        int32_t y0 = 33, rh, cw;
        const felucca_font_t *f;
        char t[48], *e;
        for (i = 0; i < ARR_LOOPS; i++)
            if (sloops.loop[i].bars || held_ring(i))
                used++;
        nempty = ARR_LOOPS - used;
        rows = (MAIN_H - y0 - (nempty ? 15 : 0)) / 14;
        big = used <= 2 * rows;
        f = big ? &FONT_S : &FONT_XS;
        rh = big ? 14 : 12;
        cw = big ? 116 : 77;
        if (!big)
            rows = (used + 2) / 3;
        else {                                       /* (rows as tall as there is room for, to 17 px) */
            rows = (used + 1) / 2;
            rh = rows ? (MAIN_H - y0 - (nempty ? 16 : 0)) / rows : 14;
            rh = rh > 17 ? 17 : rh;
        }
        for (i = 0; i < ARR_LOOPS; i++) {
            const loop_t *l = &sloops.loop[i];
            int r = held_ring(i);
            uint16_t fg = r ? RING[r] : K_TEXT;
            int32_t x, y, nw = text_w(f, "P16") + 6;
            if (!l->bars && !r)
                continue;
            x = 8 + (k / rows) * cw;
            y = y0 + (k % rows) * rh;
            k++;
            itoa_u((uint32_t)i + 1u, cat_s(t, "P"));
            if (r) {                                 /* (the badge: the capitals 1 px clear) */
                cv_round(x, y, nw, rh - 1, 4, RING[r]);
                cv_text(x + 3, cap_y(y, rh - 1, f), f, t, K_BG);
            } else
                cv_text(x + 3, cap_y(y, rh - 1, f), f, t, K_DIM);
            if (l->bars) {
                loop_label(i, t);
                for (e = t; *e && *e != ' '; e++)
                    ;
                if (*e)
                    text_fit(x + nw + 4, cap_y(y, rh - 1, f), f, e + 1, cw - nw - 8, fg);
                else {                               /* its default name: its length instead ("2 bars") */
                    cat_s(itoa_u(l->bars, t), l->bars == 1 ? " bar" : " bars");
                    cv_text(x + nw + 4, cap_y(y, rh - 1, f), f, t, r ? fg : K_DIM);
                }
            } else
                cv_text(x + nw + 4, cap_y(y, rh - 1, f), f, "empty", fg);
        }
        if (nempty) {                                /* "P9-P16 empty", "P9, P11-P16 empty" (too long: "7 empty") */
            int32_t y = y0 + rows * rh + 2;
            e = t;
            for (i = 0; i < ARR_LOOPS; i++) {
                int j = i;
                if (sloops.loop[i].bars || held_ring(i))
                    continue;
                while (j + 1 < ARR_LOOPS && !sloops.loop[j + 1].bars && !held_ring(j + 1))
                    j++;
                if (e - t > 30)
                    break;
                if (e != t)
                    e = cat_s(e, ", ");
                e = itoa_u((uint32_t)i + 1u, cat_s(e, "P"));
                if (j > i)
                    e = itoa_u((uint32_t)j + 1u, cat_s(e, j > i + 1 ? "-P" : ", P"));
                i = j;
            }
            cat_s(e, " empty");
            if (i < ARR_LOOPS || text_w(&FONT_S, t) > 224)
                cat_s(itoa_u((uint32_t)nempty, t), " empty");
            cv_text(11, cap_y(y, 14, &FONT_S), &FONT_S, t, K_DIM);
        }
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
    int i = song_style(), y = 0;
    for (int s = 0; s < ARR_NSECT; s++)
        y |= songs.song[proj.song].style[s] < ARR_USER_PATTERNS;
    if (i >= 0 && y) {                               /* a groove or more plays your copy: "House (yours)" */
        char t[24];
        cat_s(cat_s(t, style_label(i)), " (yours)");
        kv(k, style_bank(i) ? "QY style" : "Style", t, i + 1, styles_n());
    } else if (i >= 0)
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
/* HOME: the song's style, tempo, transpose; PROG ("follow" FREE, P1..P16, "Song" while in the song) */
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
        kv(&k[3], "Prog", "follow", 0, ARR_LOOPS);
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
        {   /* in a song the section's: "Muted in 2" (the section playing, stopped the Song cursor's; on the END row
             * none: "-"); the Intro / Ending playing, or outside a song: "Muted" */
            static char mn[16];
            int r = mute_row(), m = trk_muted(sel_trk());
            if (app.mode == ARR_SONG && r >= csong()->nrows)
                kv(&k[3], "Muted", "-", 0, 0);
            else {
                if (app.mode == ARR_SONG && r >= 0)
                    itoa_u((uint32_t)r + 1u, cat_s(mn, "Muted in "));
                else
                    cat_s(mn, "Muted");
                kv(&k[3], mn, m ? "yes" : "no", m, 0);
            }
        }
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
        if (!w) {                                    /* (END: PROG up adds a section) */
            kv(&k[1], "Prog", "-", 0, ARR_LOOPS);
            kv(&k[2], "Groove", "-", 0, 0);
            kv(&k[3], "Repeat", "-", 0, 0);
            break;
        }
        itoa_u((uint32_t)(w->loop % ARR_LOOPS) + 1u, cat_s(b, "P"));
        if (ui.row_marks >> app.row & 1u)            /* marked: "-", gone when the Song pages are left */
            kv(&k[1], "Prog", "-", 0, ARR_LOOPS);
        else
            kv(&k[1], "Prog", b, w->loop % ARR_LOOPS + 1, ARR_LOOPS);
        kv(&k[2], "Groove", ROW_SECT_NAME[w->sect % ROW_NSECT], sect_rank(w->sect), ROW_NSECT - 1);
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
        kv(&k[3], "Type", !has ? "-" : c.type == CH_MAJ ? "maj" : chord_type_name(c.type), has ? type_rank(c.type) + 1 : 0,
           NTYPE_ORDER);
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
/* Notes: PLACE (the cursor: bar.beat, a step past the beat ".2"), the selected note's PITCH (a drum's sound by name),
 * LENGTH (in beats: "1/2", "1 1/2"), VELOCITY; "-" at an empty place */
static void ne_knobs(knob_t *k)
{
    char b[24], *e;
    uint32_t g = ne_grid(), blen, len, w, r, x, y;
    int i, vel;
    ne_ctx();
    blen = ne_blen();
    e = itoa_u(ne.t / blen + 1u, b);
    *e++ = '.';
    e = itoa_u(ne.t % blen / ARR_PPQ + 1u, e);
    if (ne.t % ARR_PPQ / g) {
        *e++ = '.';
        itoa_u(ne.t % ARR_PPQ / g + 1u, e);
    }
    kv(&k[0], "Place", b, (int32_t)(ne.t / g), (int32_t)(ne_end() / g));
    if ((i = ne.has ? ne_find() : -1) < 0) {
        kv(&k[1], "Pitch", "-", 0, 0);
        kv(&k[2], "Length", "-", 0, 0);
        kv(&k[3], "Velocity", "-", 0, 0);
        return;
    }
    if (app.lead) {
        const lead_ev_t *v = lead_at_c(&leads, (uint32_t)i);
        len = lead_len(v);
        vel = v->vel;
    } else {
        len = view.ev[i].len;
        vel = view.ev[i].vel;
    }
    b[0] = 0;
    ne_name(ne.k.note, b);
    kv(&k[1], "Pitch", b, ne.k.note, 127);
    w = len / ARR_PPQ;                               /* whole beats and a fraction, reduced */
    r = len % ARR_PPQ;
    for (x = r, y = ARR_PPQ; y;) {
        uint32_t q = x % y;
        x = y;
        y = q;
    }
    e = b;
    if (w || !r)
        e = itoa_u(w, b);
    if (r) {
        if (w)
            *e++ = ' ';
        e = itoa_u(r / x, e);
        *e++ = '/';
        itoa_u(ARR_PPQ / x, e);
    }
    kv(&k[2], "Length", b, (int32_t)(len < 4u * ARR_PPQ ? len : 4u * ARR_PPQ), 4 * ARR_PPQ);
    kn(&k[3], "Velocity", vel, 127);
}
/* the knobs of page ui.page (the strip; a list's KNOB rows) */
static void page_knobs(knob_t *k)
{
    memset(k, 0, 4 * sizeof *k);
    if (ui.page == P_NOTES)
        ne_knobs(k);
    else if (ui.page == P_HOME)
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
        for (s = k[i].name; s && *s; s++)            /* (a name that changes: Band's "Muted in 2") */
            h = hash(h, (uint8_t)*s);
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
        text_c(cx, 0, &FONT_XS, k[i].name, hot ? K_TEXT : K_DIM);
        /* the value: its capitals centred between the name's (rows 3-11) and the bar (41): rows 12-39 */
        if (upper_only(k[i].val) && text_w(&FONT_M, k[i].val) <= 56)
            text_c(cx, cap_y(12, 28, &FONT_M), &FONT_M, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_B, k[i].val) <= 54)   /* FONT_M has no lower case: "Bb", "off" */
            text_c(cx, cap_y(12, 28, &FONT_B), &FONT_B, k[i].val, hot ? K_ACCENT : K_TEXT);
        else if (text_w(&FONT_XS, k[i].val) <= 54)  /* a long name: "Andalusian", "8BtRock1" */
            text_c(cx, cap_y(12, 28, &FONT_XS), &FONT_XS, k[i].val, hot ? K_ACCENT : K_TEXT);
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
            if (sp) {                                /* (the two lines' capitals, rows 15-36, centred too) */
                l1[sp] = 0;
                text_c(cx, 12, &FONT_XS, l1, hot ? K_ACCENT : K_TEXT);
                if (text_w(&FONT_XS, l1 + sp + 1) <= 54)
                    text_c(cx, 25, &FONT_XS, l1 + sp + 1, hot ? K_ACCENT : K_TEXT);
                else
                    text_fit(x + 3, 25, &FONT_XS, l1 + sp + 1, 54, hot ? K_ACCENT : K_TEXT);
            } else
                text_fit(x + 3, cap_y(12, 28, &FONT_XS), &FONT_XS, k[i].val, 54, hot ? K_ACCENT : K_TEXT);
        }
        if (k[i].max > 0) {
            int32_t bw = 44 * k[i].num / k[i].max;
            cv_round(cx - 22, 41, 44, 6, 3, K_LINE);
            if (bw > 0)
                cv_round(cx - 22, 41, bw < 6 ? 6 : bw, 6, 3, hot ? K_ACCENT : K_FAINT);
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
                    cv_rect(x + 4 + (j < 4 ? j : 6 - j), y + 6 + j, 2, 1, K_FAINT);
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
                                                      * "S1>follow", "S1>new P9" */
        e = crumb_song(t);
        if (ui.take)                                 /* a new loop's take (not FREE) */
            itoa_u(ui.take_loop + 1u, cat_s(e, ">new P"));
        else if (app.mode == ARR_LOOP)
            loop_label(app.loop, cat_s(e, ">"));
        else if (app.mode == ARR_FREE)
            cat_s(e, ">follow");
        break;
    case P_SONG: case P_SONG2: case P_SONG3:         /* "S1" (the cursor's section: by the cursor) */
        crumb_song(t);
        break;
    case P_LOOP: case P_LOOP2:                       /* "S1>P3 Verse" (the cursor: on the grid, its bar and beat lit) */
        loop_label(app.loop, cat_s(crumb_song(t), ">"));
        break;
    case P_PATTERN: case P_PATSET: case P_TRACK: case P_TRACK2: case P_MIXER: case P_TONE: case P_NOTES:
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
    int g = page_group(ui.page), n = ui.page == P_NOTES ? 1 : group_n(g), d = ui.page - PAGE_BTN[g].first;
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
    if (x - 16 >= 24)                                /* the rule (a stub of one: none) */
        cv_rect(8, 6, x - 16, 1, K_LINE);
    cv_blit(0, TAG_Y);
}

/* the footer: what the keys do here (the spec's Keys table); in a list, a question, HOME held: what does what. Parts
 * "|"-separated, each a control and its job ("KEYS chords|REC write"; "HOME+KEYS sections": both held), the controls drawn
 * as pills (draw_footer); a part whose first word is no control is plain words ("let go: nothing changes") */
static void page_footer(char *t)
{
    *t = 0;
    if (ui.ask) {                                    /* (a message meanwhile: here, the question stays up) */
        if (plat_ms() < ui.msg_until) {
            cat_s(cat_s(cat_s(t, ui.msg[0]), " "), ui.msg[1]);
            upcase(t);
        } else
            cat_s(t, ui.ask_foot ? ui.ask_foot : "SEL yes|HOME no");
        return;
    }
    if (home_shown()) {                              /* (HOME held: REC undoes, PLAY redoes; the keys: on screen, the
                                                      * progressions and the sections; SELECT, PRESETS: the encoders') */
        cat_s(t, "REC undo|PLAY redo");
        return;
    }
    if (ui.card) {
        cat_s(t, "let go: nothing changes");
        return;
    }
    if (!ui.any_btn && ui.page == P_HOME && app.mode == ARR_FREE && !ui.take) {   /* the first screen: REC's job, and
                                                      * where the rest is said (its main band: "Play a chord") */
        cat_s(t, "REC new prog|hold a button: help");
        return;
    }
    if (!lst.open && plat_ms() < ui.slots_until) {   /* MEMORY FULL said: where to see what fills it */
        cat_s(t, "memory full|SEL memory: free one");
        return;
    }
    if (lst.open && lst.n) {                         /* (a list: the encoders' line is the list's; all said here) */
        int k = lst.row[lst.sel].kind;
        cat_s(t, k == LK_NAME ? "ALGO letter|SEL next|HOME back"
               : k == LK_ACTION ? "SELECT move|SEL do it|HOME back"
               : k == LK_PICK && lst.row[lst.sel].id == A_STEP ? "SELECT move|ALGO set|HOME back"
               : k == LK_PICK ? "ALGO pick|SEL do it|HOME back"
               : k == LK_SUB ? "SELECT move|SEL open|HOME back"
               : k == LK_SLOT ? (slot_kind[lst.row[lst.sel].id] == SLOT_NONE ? "SELECT move|HOME back"
                                                                              : "SELECT move|SEL free|HOME back")
               : "SELECT move|ALGO set|HOME back");
        return;
    }
    switch (ui.page) {                               /* (recording: REC stops it, as it says) */
    case P_HOME:
        cat_s(t, ui.take || ui.rec_chords ? "KEYS chords|REC stop"
                 : app.mode == ARR_FREE ? "KEYS chords|REC new progression" : "KEYS chords|REC write");
        break;
    case P_LOOP: case P_LOOP2:
        cat_s(t, ui.rec_chords ? "KEYS chords|REC stop|OCT cursor" : "KEYS chords|REC write|OCT cursor");
        break;
    case P_SONG: case P_SONG2: case P_SONG3:
        cat_s(t, ui.song_rec ? "HOME+KEYS sections|REC stop" : "WHITE progression|BLACK mutes");
        break;
    case P_PATTERN: case P_PATSET: case P_TRACK: case P_TRACK2:   /* "KEYS C1|REC record" (its sound: on the page) */
        if (trk_rec_shown() && !app.lead && loop_longer()) {   /* REC's screen: SEL makes it as long as the loop */
            itoa_u((uint32_t)loop_longer(), cat_s(t, "SEL make it "));
            cat_s(t + slen(t), " bars|REC stop");
            break;
        }
        if (ui.follow_hint) {                        /* REC kept the track's type (it has notes): how to change that */
            cat_s(cat_s(cat_s(t, "SEL clear "), TRACK_NAME[sel_trk()]), " to record as played");
            break;
        }
        cat_s(cat_s(cat_s(t, "KEYS "), TRACK_NAME[sel_trk()]), arr_recording || ui.rec_want ? "|REC stop" : "|REC record");
        if (ui.page == P_PATTERN)                    /* (Band: SEQ again, its notes to fix) */
            cat_s(t + slen(t), "|SEQ notes");
        break;
    case P_NOTES:                                    /* the keys: hear a note, enter one at an empty place; recording:
                                                      * as recorded, EDIT held erases */
        if (arr_recording || ui.rec_want)
            cat_s(cat_s(cat_s(t, "KEYS "), TRACK_NAME[sel_trk()]), "|REC stop|EDIT held: erase");
        else
            cat_s(t, ne.has ? "KEYS hear|EDIT delete|SEL+SELECT move" : "KEYS enter|SEQ Band");
        break;
    case P_MIXER: case P_TONE:
        cat_s(t, "BLACK instrument");
        break;
    default:                                         /* (Effects, Style, Setup, Status: the keys do nothing) */
        break;
    }
}

/* the encoders' line: what turning SELECT, ALGORITHM, PRESETS changes here, as input() and presets() do it
 * ("SELECT page|ALGO instrument|PRESET family": a part's first word the encoder, the rest what it changes; where you are is
 * the tag's and the dots' job, not this line's). A part is left out where its encoder does nothing, and ALG where no
 * instrument is on screen (it still picks one: the pages that show it say so); a question, a list: none (the footer
 * says what SELECT, ALGORITHM, SEL, HOME do there) */
static void enc_line(char *t)
{
    int g = page_group(ui.page), n = group_n(g), held = home_shown(), p = ui.page;
    arr_trk_t lb;
    char *e = t;
    *t = 0;
    if (ui.ask || (lst.open && lst.n) || ui.card)    /* (a card: a knob turned with its button held is a combination) */
        return;
    if (held) {                                      /* HOME held: the tempo, the song */
        cat_s(t, "SELECT tempo|PRESET song");
        return;
    }
    if (p == P_NOTES) {                              /* (SELECT: the notes, not pages) */
        cat_s(t, "SELECT note|ALGO instrument|PRESET zoom");
        return;
    }
    if (n > 1)                                       /* the button's pages */
        e = cat_s(t, "SELECT page");
    else if (p == P_HOME)                            /* (HOME again: Song) */
        e = cat_s(t, "HOME song");
    if (p >= P_PATTERN && p <= P_TONE)               /* the instrument (or melody) the keys and the page show */
        e = cat_s(e, *t ? "|ALGO instrument" : "ALGO instrument");
    {
        const char *a = p == P_HOME || p == P_LOOP || p == P_LOOP2 ? "prog"
                        : song_pg(p) ? "section"
                        : p == P_PATTERN || p == P_PATSET ? "groove"
                        : p == P_STYLE || p == P_STYLE2 ? "style"
                        : p == P_TRACK && (sel_arr(&lb)->chan & 15u) != 9u ? "family"   /* (drums: none) */
                        : 0;
        if (a)
            cat_s(cat_s(e, *t ? "|PRESET " : "PRESET "), a);
    }
}

/* the two bottom lines: the encoders' parts, then the keys' (the footer's), each part a control as a pill (an ink
 * outline, as the panel's printed labels and the help cards' button) and its job after it, a size up and in ink.
 * Everything on one line where it fits (centred down the two); else the encoders' on the first, the keys' on the
 * second (a part moves over when a line is too long). A list: its footer alone, on the second line */
#define BOT_LH 16                                    /* a line: its pill 15 px (XS's capitals 3 px inside the ring) */
#define BOT_PH 15
#define BOT_PAD 3                                    /* a button's text from its keycap's sides */
#define BOT_PAD_E 5                                  /* an encoder's from its round ends */
#define BOT_JOB 3                                    /* the pill to its job */
#define BOT_GAP 10                                   /* the least between two parts */
#define BOT_X 6                                      /* the lines' left; 228 px wide */
#define BOT_W 228
#define BOT_PARTS 8
static const char *const CTL_WORD[] = {"SELECT", "ALGO", "PRESET", "SEL", "HOME", "REC", "PLAY", "OCT", "KEYS", "WHITE",
                                       "BLACK", "SAVE", "EDIT", "SEQ"};
static int ctl_is(const char *s, uint32_t n)         /* s[0..n) names a control */
{
    for (uint32_t i = 0; i < sizeof CTL_WORD / sizeof CTL_WORD[0]; i++) {
        const char *w = CTL_WORD[i];
        uint32_t j = 0;
        while (j < n && w[j] && w[j] == s[j])
            j++;
        if (j == n && !w[j])
            return 1;
    }
    return 0;
}
/* a part's controls: the length of its first word if that is controls joined by '+' ("HOME+KEYS"), else 0 */
static uint32_t ctl_len(const char *s)
{
    uint32_t n = 0, a = 0;
    for (;; n++)
        if (s[n] == '+' || s[n] == ' ' || !s[n]) {
            if (n == a || !ctl_is(s + a, n - a))
                return 0;
            if (s[n] != '+')
                return n;
            a = n + 1;
        }
}
/* part s (n chars) drawn at x, y (draw 0: measured); its width. job: the font of the words */
static int32_t bot_part(int32_t x, int32_t y, const char *s, uint32_t n, const felucca_font_t *job, int draw)
{
    char b[48];
    uint32_t c = ctl_len(s), a = 0, i;
    int32_t x0 = x;
    if (c > n)
        c = 0;
    for (i = 0; c && i <= c; i++)                    /* the pills */
        if (i == c || s[i] == '+') {
            uint32_t k;
            int32_t w;
            for (k = 0; k < i - a && k < sizeof b - 1u; k++)
                b[k] = s[a + k];
            b[k] = 0;
            int enc = same(b, "SELECT") || same(b, "ALGO") || same(b, "PRESET");   /* (the three encoders) */
            int32_t pad = enc ? BOT_PAD_E : BOT_PAD;
            w = text_w(&FONT_XS, b) + 2 * pad;
            if (draw) {                              /* an encoder round, a button or key a keycap */
                cv_round(x, y, w, BOT_PH, enc ? 7 : 2, K_TEXT);
                cv_round(x + 1, y + 1, w - 2, BOT_PH - 2, enc ? 6 : 1, K_BG);
                cv_text(x + pad, cap_y(y, BOT_PH, &FONT_XS), &FONT_XS, b, K_TEXT);
            }
            x += w;
            if (i < c) {                             /* "+" between two held together */
                if (draw)
                    cv_text(x + 2, cap_y(y, BOT_PH, &FONT_XS), &FONT_XS, "+", K_TEXT);
                x += text_w(&FONT_XS, "+") + 4;
            }
            a = i + 1;
        }
    if (c) {
        c++;                                         /* (past the space) */
        if (c >= n)
            return x - x0;
        x += BOT_JOB;
    }
    for (i = 0; c + i < n && i < sizeof b - 1u; i++)
        b[i] = s[c + i];
    b[i] = 0;
    if (draw)
        cv_text(x, cap_y(y, BOT_PH, job), job, b, c ? K_TEXT : K_DIM);
    return x + text_w(job, b) - x0;
}
static int ne0(const char *e)                        /* the encoders' parts */
{
    int n = *e != 0;
    for (; *e; e++)
        n += *e == '|';
    return n;
}
typedef struct { const char *s[BOT_PARTS]; uint32_t n[BOT_PARTS]; int32_t w[BOT_PARTS]; int cnt; } bot_t;
static void bot_split(bot_t *p, const char *t, const felucca_font_t *job)
{
    while (*t && p->cnt < BOT_PARTS) {
        uint32_t n = 0;
        while (t[n] && t[n] != '|')
            n++;
        p->s[p->cnt] = t;
        p->n[p->cnt] = n;
        p->w[p->cnt] = bot_part(0, 0, t, n, job, 0);
        p->cnt++;
        t += n;
        if (*t)
            t++;
    }
}
static int32_t bot_w(const bot_t *p, int a, int b)   /* parts a..b-1 on a line, at the least gap */
{
    int32_t w = 0;
    for (int i = a; i < b; i++)
        w += p->w[i] + (i > a ? BOT_GAP : 0);
    return w;
}
static void bot_line(const bot_t *p, int a, int b, int32_t y, const felucca_font_t *job)
{
    int32_t tw = 0, gap = 0, x = BOT_X;
    for (int i = a; i < b; i++)
        tw += p->w[i];
    if (b - a > 1) {
        gap = (BOT_W - tw) / (b - a - 1);
        gap = gap > 28 ? 28 : gap;
    }
    for (int i = a; i < b; i++)
        x += bot_part(x, y, p->s[i], p->n[i], job, 1) + gap;
}
static void draw_footer(void)
{
    char t[64], e[64];
    int list = lst.open && !ui.card && !home_shown(), ne = 0, split = -1, k;
    const felucca_font_t *job = &FONT_S;
    bot_t p;
    uint32_t h = hash(2166136261u, (uint32_t)list);
    page_footer(t);
    enc_line(e);
    if (list)
        *e = 0;
    for (const char *c = t; *c; c++)
        h = hash(h, (uint8_t)*c);
    for (const char *c = e; *c; c++)
        h = hash(h, (uint8_t)*c ^ 0x80u);
    if (h == ui.sig[4])
        return;
    ui.sig[4] = h;
    /* the words a size up; where nothing fits (a long hint), the encoders' last parts left out, then XS (a bug: below) */
    for (int f = 0; f < 2 && split < 0; f++)
        for (int drop = 0; drop <= ne0(e) && split < 0; drop++) {
            job = f ? &FONT_XS : &FONT_S;
            memset(&p, 0, sizeof p);
            bot_split(&p, e, job);
            p.cnt -= drop;
            ne = p.cnt;
            bot_split(&p, t, job);
            if (bot_w(&p, 0, p.cnt) <= BOT_W)
                split = 0;                           /* one line */
            else if (!list)                          /* two: at the encoders' end, else the nearest that fits */
                for (int d = 0; d < p.cnt && split < 0; d++)
                    for (int sg = -1; sg <= 1 && split < 0; sg += 2) {
                        k = ne + sg * d;
                        if (k > 0 && k < p.cnt && bot_w(&p, 0, k) <= BOT_W && bot_w(&p, k, p.cnt) <= BOT_W)
                            split = k;
                    }
        }
#if defined(GM_HOST) && !defined(GM_WEB)        /* the simulator: a line cut, or crowded, is a bug */
    if (split < 0 || job != &FONT_S) {
        printf("  FAIL bottom lines too wide: %s / %s\n", e, t);
        msg_too_wide++;
    }
#endif
    if (split < 0)
        split = p.cnt > 1 ? ne > 0 && ne < p.cnt ? ne : 1 : 0;
    if (list) {
        cv_begin(240, BOT_LH, K_BG);
        bot_line(&p, 0, p.cnt, 0, job);
        cv_blit(0, FOOT_Y);
        return;
    }
    cv_begin(240, 2 * BOT_LH, K_BG);
    if (!split)
        bot_line(&p, 0, p.cnt, BOT_LH / 2, job);
    else {
        bot_line(&p, 0, split, 0, job);
        bot_line(&p, split, p.cnt, BOT_LH, job);
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
    {B_ARP, PG_ANY, "Progression", "Your chords, 1 to 16 bars, played in a loop. Play chords on the keys.",
     "REC writes them at the cursor (BAR, BEAT), or in time as it plays."},
    {B_SEQ, PG(P_NOTES), "Notes", "SELECT picks a note. The knobs set place, pitch, length, velocity.",
     "EDIT deletes it. SEL + SELECT moves it. SEQ goes back to Band."},
    {B_EDIT, PG(P_NOTES), "Delete", "Tap to delete the selected note. HOME + REC brings it back.",
     "Hold it while recording to erase notes as they play."},
    {B_SEQ, PG_ANY, "Band", "The groove's notes by instrument. BAR and BEAT move the cursor.",
     "SELECT for length and name; SEQ again to fix notes."},
    {B_EDIT, PG_ANY & ~CF_NOLEAD, "Melody", "The melody's sound, channel and velocity, and its notes in the song.",
     "SEL has QUANTISE. ALGORITHM picks an instrument."},
    {B_EDIT, PG_ANY, "Sound", "The instrument's sound and its range.",
     "SELECT for high key and channel; ALGORITHM past C4 for M1, M2."},
    {B_LFO, PG_ANY, "Mixer", "Volume, pan and effect sends. Turn SELECT for the tone. Black keys pick the instrument.", 0},
    {B_FX, PG_ANY, "Effects", "The reverb and the chorus every instrument sends to.", 0},
    {B_ENV, PG_ANY, "Style", "Which style plays the grooves, Intro, fills and Ending. Turn PRESETS for the next style.", 0},
    {B_GLO, PG_ANY, "Setup", "Clock, MIDI out, lights and the click. Turn SELECT for the status page.", 0},
    {B_SEL, PG_TRACK, "Lists", "This page as a list. MEMORY shows what fills the 16 places.",
     "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SEL, PG_ANY, "Lists", "The page's knobs, then its actions. SEL runs the one picked.",
     "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SAVE, PG_SONG, "Save", "Tap to save.", "Nothing to copy on this page. A white key on its own adds a section."},
    {B_SAVE, PG_ANY, "Save", "Tap to save. Hold SAVE and press a white key to copy to that place.", 0},
    {B_REC, PG(P_HOME), "Write chords", "Writes into the progression playing, at the cursor or in time.",
     "With none playing, starts a new one. HOME + REC undoes."},
    {B_REC, PG(P_LOOP) | PG(P_LOOP2), "Write chords", "Writes into the progression, at the cursor or in time as it plays.",
     "HOME + REC undoes."},
    {B_REC, PG_TRACK | CF_NOLEAD, "Record the instrument", "What the keys play goes into the selected instrument.",
     "HOME + REC undoes one pass at a time."},
    {B_REC, PG_TRACK | CF_LEAD, "Record the melody", "While the song plays, what you play goes into the section playing.",
     "PLAY starts the song; PLAY FROM SECTION is in Song's list."},
    {B_REC, PG_SONG, "Record the sections",
     "While the song plays, records what you pick with HOME + keys, and your mutes.", "HOME + REC undoes."},
    {B_REC, PG_ANY, "Record", "Nothing to record on this page.", "HOME + REC undoes."},
    {B_PLAY, PG_ANY, "Play", "Starts and stops. On Song, plays from section 1, at the next bar if a progression is playing.",
     "HOME + PLAY redoes."},
    {B_OCTDN, PG(P_LOOP) | PG(P_LOOP2), "Cursor", "Moves the cursor back a beat. The chords don't change.",
     "OCT+ moves it forward; BAR and BEAT jump to a place."},
    {B_OCTUP, PG(P_LOOP) | PG(P_LOOP2), "Cursor", "Moves the cursor forward a beat. The chords don't change.",
     "OCT- moves it back; BAR and BEAT jump to a place."},
    {B_OCTDN, PG_ANY, "Octave", "The keys an octave down.", "Hold both for 5 seconds for update mode."},
    {B_OCTUP, PG_ANY, "Octave", "The keys an octave up.", "Hold both for 5 seconds for update mode."},
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
/* s in lines of w pixels (at spaces), from y; the y after (draw 0: only measured) */
static int32_t wrap_do(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c, int draw)
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
        if (draw)
            cv_text(x, y, f, line, c);
        y += 17;
        s += cut;
        while (*s == ' ')
            s++;
    }
    return y;
}
static int32_t wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c)
{
    return wrap_do(x, y, w, f, s, c, 1);
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
    {   /* the block centred down the card: the pill (its top) to the last line's baseline; the words 30 px under the
         * pill's top, lines 17 px apart, the second part 4 px further */
        int32_t n = i < 0 ? 0 : wrap_do(0, 0, 200, &FONT_S, CARD[i].l1, 0, 0) / 17 +
                                (CARD[i].l2 ? wrap_do(0, 0, 200, &FONT_S, CARD[i].l2, 0, 0) / 17 : 0);
        int32_t bh = i < 0 ? 20 : 42 + (n - 1) * 17 + (CARD[i].l2 ? 4 : 0), top = 4 + (MAIN_H - 8 - bh) / 2;
        if (top < 10)
            top = 10;
        x = pill_o(18, top, 0, BTN_LABEL[(ui.card - 1) % NB], K_TEXT, K_TEXT);   /* (the button: an ink outline) */
        y = top + 30;
    }
    if (i >= 0) {
        cv_text(x + 10, cap_y(y - 30, 20, &FONT_B), &FONT_B, CARD[i].title, K_TEXT);   /* (on the pill's baseline) */
        y = wrap(20, y, 200, &FONT_S, CARD[i].l1, K_TEXT);
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
    if (lst.slots) {                                 /* MEMORY: the 16, as scanned, by song (slot_ord) */
        for (int i = 0; i < ARR_USER_PATTERNS; i++)
            list_add(slot_txt[slot_ord[i]], 0, LK_SLOT, slot_ord[i]);
        if (lst.sel >= lst.n)
            lst.sel = (uint8_t)(lst.n - 1);
        return;
    }
    if (keep == P_HOME || keep == P_NOTES)
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
        if (keep == P_HOME && (csong()->flags & SONG_F_TEMPO) && song_style() >= 0 &&
            style_tempo(song_style()) != csong()->tempo) {   /* the tempo set by hand, not the style's: the style's (first:
                                                      * the list opens on it) */
            static char st[4];
            itoa_u((uint32_t)style_tempo(song_style()), st);
            list_add(ACTION_NAME[A_STYLE_TEMPO], st, LK_ACTION, A_STYLE_TEMPO);
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
        list_add(ACTION_NAME[A_CLEAR_SONG], 0, LK_ACTION, A_CLEAR_SONG);   /* (the sections and melodies) */
        list_add(ACTION_NAME[A_NEW_SONG], 0, LK_ACTION, A_NEW_SONG);       /* (all of the song, as on a new FM-1) */
        list_add(ACTION_NAME[A_COPY_SONG], v, LK_PICK, A_COPY_SONG);
    } else if ((keep >= P_PATTERN && keep <= P_TONE) || keep == P_NOTES) {   /* (Effects: every song's, no instrument: no
                                                      * actions) */
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
    } else if (keep == P_SETUP || keep == P_PANEL)   /* Setup: everything as new (asks) */
        list_add(ACTION_NAME[A_FACTORY_RESET], 0, LK_ACTION, A_FACTORY_RESET);
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
#define LIST_RH 20                                   /* (a row 19 px: its capitals 4 px clear of its edges, the
                                                      * descenders inside) */
#define LIST_Y 24                                    /* the list covers the main band, the tag, the knobs and the encoders'
                                                      * line: LIST_SHOWN rows from here to the footer */
#define LIST_H (FOOT_Y - LIST_Y)
/* the list's rows, drawn oy px down (it is drawn in two canvases: the first MAIN_H rows, then the rest) */
static void list_paint(int32_t oy)
{
    uint32_t i;
    for (i = lst.top; i < lst.n && i < lst.top + LIST_SHOWN; i++) {
        int32_t y = 1 + (int32_t)(i - lst.top) * LIST_RH - oy, ty = cap_y(y, LIST_RH - 1, &FONT_S);   /* (S, B, XS: y + 2) */
        int on = i == lst.sel, k = lst.row[i].kind;
        uint16_t fg = on ? K_BG : K_TEXT, dim = on ? K_LINE : K_DIM;
        const char *v = lst.row[i].val;
        if (k == LK_MORE) {
            int32_t x = cv_text(14, ty, &FONT_XS, "MORE", K_DIM);
            cv_rect(x + 6, ty + 7, 222 - x, 1, K_LINE);
            continue;
        }
        cv_round(6, y, 222, LIST_RH - 1, 5, on ? K_TEXT : K_PANEL);   /* (the selection: ink, light text) */
        if (k == LK_SLOT) {                          /* "3  S1: Groove A (yours)|Funk", "S2: melodies", "free" (dim): its
                                                      * number (SAVE + white key 3 copies a groove
                                                      * there), what it holds (a long one smaller) */
            int id = lst.row[i].id;
            char nb[4];
            itoa_u((uint32_t)id + 1u, nb);
            text_r(30, ty, &FONT_XS, nb, dim);
            if (segs_w(&FONT_B, lst.row[i].name) <= 200 - 36)
                segs(36, ty, &FONT_B, lst.row[i].name, slot_kind[id] == SLOT_NONE ? dim : fg);
            else
                segs(36, ty, &FONT_S, lst.row[i].name, slot_kind[id] == SLOT_NONE ? dim : fg);
            if (slot_kind[id] != SLOT_NONE)
                glyph_enter(208, ty + 3, fg);
        } else if (k == LK_KNOB) {
            cv_text(14, ty, &FONT_S, lst.row[i].name, dim);
            text_fit(218 - text_w(&FONT_B, v), ty, &FONT_B, v, 120, fg);
        } else if (k == LK_NAME) {                   /* the letters, the one ALGORITHM turns underlined */
            cv_text(14, ty, &FONT_S, lst.row[i].name, dim);
            for (int j = 0; j < 8; j++) {
                char c[2] = {v[j] == ' ' ? '_' : v[j], 0};
                int32_t x = 100 + j * 15;
                text_c(x + 6, ty, &FONT_B, c, v[j] == ' ' ? dim : fg);
                if (on && j == ui.name_cur)
                    cv_rect(x, ty + 14, 13, 2, K_ACCENT);
            }
        } else {
            int32_t ne = cv_text(14, ty, &FONT_B, lst.row[i].name, fg);
            if (k == LK_SUB)                         /* another list */
                glyph_pick(213, ty + 3, fg);
            else if (k == LK_PICK) {
                int32_t room = 206 - (ne + 6), w = text_w(&FONT_S, v);
                text_fit(206 - (w < room ? w : room), ty, &FONT_S, v, room, fg);
                glyph_pick(213, ty + 3, fg);
            } else {
                if (*v && 202 - text_w(&FONT_S, v) >= ne + 6)   /* (QUANTISE: the grid, when there is room) */
                    cv_text(202 - text_w(&FONT_S, v), ty, &FONT_S, v, dim);
                glyph_enter(208, ty + 3, fg);
            }
        }
    }
    if (lst.n > LIST_SHOWN) {                        /* where in the list */
        int32_t th = LIST_SHOWN * LIST_RH - 2, bh = th * LIST_SHOWN / lst.n, by = th * lst.top / lst.n;
        cv_round(232, 1 - oy, 4, th, 2, K_PANEL);
        cv_round(232, 1 + by - oy, 4, bh, 2, K_FAINT);
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
static int lead_view_shown(void) { return app.lead && (ui.page == P_TRACK || ui.page == P_NOTES || trk_rec_shown()); }
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
    if (!app.lead && (ui.page == P_PATTERN || ui.page == P_NOTES || trk_rec_shown())) {   /* a groove: its instruments,
                                                      * the selected one's bars */
        int s = pslot(), trk = proj.trk & 7;
        uint32_t blen;
        view_refresh(s);
        id = 3u | (uint32_t)proj.song << 4 | (uint32_t)s << 8 | (uint32_t)trk << 12;   /* (another groove picked: its
                                                      * instruments that differ flash) */
        {
            uint32_t mm = 0;                         /* (the mutes as shown: trk_muted) */
            for (i = 0; i < ARR_TRACKS; i++)
                mm |= (uint32_t)trk_muted(i) << i;
            key = hash(hash(hash(2166136261u, vw.dirty), vw.gen), mm);
        }
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
    if (lead_view_shown()) {                         /* a melody: its sections, its bars in the song (mod 32: the lane's
                                                      * four are told apart by their sections too) */
        int k = (app.lead - 1) % ARR_LEADS;
        uint32_t j;
        smap_t m;
        song_map(&m);
        n = m.n;
        id = lead_watch_id(&m, k);                   /* (the sections' lengths changed: watched afresh) */
        key = hash(hash(2166136261u, leads.gen), leads.n | (uint32_t)n << 16);   /* (gen and n apart: a note out
                                                      * as gen goes on never hashes the same) */
        if (fw.ok && fw.id == id && fw.key == key)
            return;
        for (i = 0; i < 32; i++)
            bh[i] = 2166136261u;
        for (i = 0; i < n; i++) {
            uint32_t hh = 2166136261u;
            for (j = leads.row0[i]; j < leads.row0[i + 1] && j < leads.n; j++) {
                const lead_ev_t *e = lead_at_c(&leads, j);
                uint32_t x, sb;
                if (lead_trk(e) != k)
                    continue;
                x = hash(hash(lead_pos(e), e->note), lead_len(e));
                hh = hash(hh, x);
                if (lead_pos(e) < m.len[i]) {
                    sb = (m.b0[i] + lead_pos(e) / m.blen) & 31u;
                    bh[sb] = hash(bh[sb], x);
                }
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
    else if (ui.page == P_NOTES)
        draw_notes();
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
