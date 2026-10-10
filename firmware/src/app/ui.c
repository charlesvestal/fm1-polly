/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's input: the controls (the spec's Buttons and Keys tables), the songs and patterns into the arranger, saving,
 * the lights. Part of the unity build (after pages.c, which holds the shared state and the drawing); the host
 * simulator includes it too.
 *
 *   PLAY            start / stop (SONG: from the SONG screen's row, else row 1)
 *   REC             record into the selected track (SEQ, EDIT; its light: the engine's)
 *   SEL tap         the jobs open: do it
 *   HOME held       the shortcuts on screen (after HOME_HOLD_MS):
 *     + white key 1..16   loop n: MODE LOOP (from the next bar while playing); the loop playing again: FREE
 *     + black key 1..5    Intro, Main A, Main B, Fill (AB from Main A, BA from Main B), Ending, from the next bar
 *     + SELECT            the song's tempo
 *     + PRESETS           the song (while playing: after the stop)
 *   HOME: KNOB 1 the style, 2 the tempo, 3 TRANSPOSE, 4 MODE (FREE / LOOP / SONG): the song's; again: SONG
 *   ARP LOOP (x2), SEQ Pattern (x2), EDIT Track (x2), LFO Mixer (x2), FX Master FX, ENV Style (x2), GLO Setup (x2);
 *   the header's dots: which of a button's pages
 *   the keys        by the page (key_role): chords (HOME, LOOP), the selected track (SEQ, EDIT), the track (LFO's
 *                   black keys), nothing (FX, ENV, GLO)
 *   SAVE            saves (lit: something to save; held: the jobs)        OCT- / OCT+   the keys' octave
 *   SELECT          the button's next / previous page       ALGORITHM   the track       PRESETS   per page
 * MIDI in (USB, TRS) plays all 16 channels; its clock drives the arranger. */

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.msg_first = 0;
#if defined(GM_HOST) && !defined(GM_WEB)        /* the simulator: a message the header cuts is a bug */
    if (8 + text_w(&FONT_B, ui.msg[0]) + 8 + text_w(&FONT_B, ui.msg[1]) > 232) {
        printf("  FAIL message too wide for the header: %s %s\n", ui.msg[0], ui.msg[1]);
        msg_too_wide++;
    }
#endif
}
void ui_say(const char *a, const char *b) { say(a, b); }

static void mark_dirty(void)
{
    app.dirty = 1;
    app.chg++;
    ui.act_t = plat_ms();
}
int ui_dirty(void) { return unsaved(); }

static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (!a || accel_off)
        return s;
    if (a > 1)
        dt /= a;
    m = dt < 15u ? 6u : dt < 30u ? 4u : dt < 60u ? 2u : 1u;
    return s * (int32_t)m;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the keys: 0 = F3; the white and the black ones in order */
static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static int ordinal(const uint8_t *t, int n, int k)
{
    for (int i = 0; i < n; i++)
        if (t[i] == k)
            return i;
    return -1;
}
static int key_note(int k)
{
    int n = 53 + k + 12 * proj.oct;
    return n < 0 ? 0 : n > 127 ? 127 : n;
}

/* ------------------------------------------------- songs and patterns --- */
static void slot_cache(int s, const pattern_t *p)
{
    for (int t = 0; t < ARR_TRACKS; t++)
        app.trk[s][t] = p->trk[t];
    memcpy(app.hdr[s].name, p->name, sizeof p->name);
    app.hdr[s].bars = p->bars;
    app.hdr[s].beats = p->beats;
    app.gen++;
}
/* the slot whose pattern the pages show and edit: the section playing (recording goes there too) */
static int eslot(void) { return sect_slot(arr_sect); }
/* changed since loaded or saved. A slot playing a style's section has nothing to save into: it gets a user
 * pattern of its own at its first change (slot_own); with none free, its changes play until another pattern
 * is picked (Copy pattern, the jobs, keeps them in a user pattern) */
static int slot_unsaved(int s)
{
    return songs.song[proj.song].style[s] < ARR_USER_PATTERNS &&
           (arr_dirty[s] != app.saved[s] || app.edited[s] || (app.edit_pend >> s & 1u));
}

/* edits: into the app's copy at once (the screen), into the arranger at most every EDIT_MS (a store re-sends
 * the pattern's sounds): copy the slot, put the app's settings in, store; refused when it was recorded into
 * meanwhile: again */
#define EDIT_MS 50u
static int slot_own(int s);
static void edited(int s)
{
    slot_own(s);                                 /* a style's section: a user pattern of its own first */
    if (!app.edit_pend)
        app.edit_t = plat_ms();
    app.edit_pend |= (uint8_t)(1u << s);
    app.take_t[s] = plat_ms();
    app.chg++;
    app.gen++;
    ui.act_t = plat_ms();
}
static void edit_apply(int s, pattern_t *p)
{
    for (int t = 0; t < ARR_TRACKS; t++) {
        uint16_t f = p->trk[t].first, c = p->trk[t].count;
        p->trk[t] = app.trk[s][t];
        p->trk[t].first = f;
        p->trk[t].count = c;
    }
    memcpy(p->name, app.hdr[s].name, sizeof p->name);
    p->bars = app.hdr[s].bars;
    p->beats = app.hdr[s].beats;
}
static void edits_flush(void)
{
    for (int s = 0; s < ARR_NSECT; s++) {
        if (!(app.edit_pend >> s & 1u))
            continue;
        for (int tries = 0; tries < 8; tries++) {
            uint16_t d = arr_pattern_copy(s, &scratch);
            edit_apply(s, &scratch);
            if (arr_store_pattern(s, &scratch, d)) {
                app.edit_pend &= (uint8_t)~(1u << s);
                app.edited[s] = 1;
                app.gen++;
                break;
            }
        }
    }
}

/* QY70 bank style b decoded in a qbuf: the one holding it, else the one no slot plays (decoded now: some ms, the
 * render plays on meanwhile); -1: no bank, a damaged record (said), or no buffer free (a song plays one bank style:
 * never) */
static int bank_get(int b)
{
    int i, free_ = -1, rc;
    for (i = 0; i < 2; i++)
        if (qbuf[i].index == b && b >= 0)
            return i;
    for (i = 1; i >= 0; i--) {
        int used = 0;
        for (int s = 0; s < ARR_NSECT; s++)
            used |= app.slot_buf[s] == i;
        if (!used)
            free_ = i;
    }
    if (b < 0 || b >= qb_count()) {
        say("QY70 STYLE", qb_count() ? "not in the bank" : "no bank installed");
        return -1;
    }
    if (free_ < 0)
        return -1;
    qbuf[free_].index = -1;
    if ((rc = qb_decode(b, &qbuf[free_])) != 0) {
        say("QY70 STYLE", rc == QB_E_CRC || rc == QB_E_READ ? "DAMAGED" : "NOT READ");
        qbuf[free_].index = -1;
        return -1;
    }
    return free_;
}
/* the song's mutes to the engine (only the tracks that changed: the command queue is short). In SONG the rows'
 * mutes play (the engine's): the song's are sent again when MODE leaves it (mode_set) */
static void mutes_apply(void)
{
    if (app.mode == ARR_SONG)
        return;
    uint8_t m = songs.song[proj.song].mute, d = m ^ app.mute_sent;
    for (int t = 0; t < ARR_TRACKS; t++)
        if (d >> t & 1u)
            arr_track_mute(t, m >> t & 1u);
    app.mute_sent = m;
}
/* a pattern's own mutes (saved before mutes were the song's) become the song's, and leave the pattern: 1 = some
 * did (a user pattern is then saved without them) */
static int mute_fold(pattern_t *p)
{
    int n = 0;
    for (int t = 0; t < ARR_TRACKS; t++)
        if (p->trk[t].mute) {
            p->trk[t].mute = 0;
            songs.song[proj.song].mute |= (uint8_t)(1u << t);
            n = 1;
        }
    if (n) {
        app.songs_dirty = 1;
        app.chg++;
        mutes_apply();
    }
    return n;
}
/* slot s (Intro, Main A, Main B, Fill AB, Fill BA, Ending) gets pattern `style`: a user pattern, a factory style's
 * (ARR_FACTORY0..: copied out of flash), a QY70 bank style's section (played from its decoded phrases), or an empty
 * one when it has none */
static void slot_load(int s, int style)
{
    int folded;
    if (style >= ARR_BANK0) {
        int b = (style - ARR_BANK0) / ARR_NSECT, k = (style - ARR_BANK0) % ARR_NSECT, i = bank_get(b);
        if (i >= 0) {
            arr_bank_pattern(&qbuf[i], k, &scratch);
            mute_fold(&scratch);
            arr_load_bank(s, &qbuf[i], k, &scratch);
            app.slot_buf[s] = (int8_t)i;
            slot_cache(s, &scratch);
            app.saved[s] = arr_dirty[s];
            app.edited[s] = 0;
            app.edit_pend &= (uint8_t)~(1u << s);
            return;
        }
        pattern_empty(&scratch);
    } else if (style < ARR_USER_PATTERNS ? project_pattern_load(style, &scratch) != 0
                                         : arr_factory_pattern(style, &scratch) != 0)
        pattern_empty(&scratch);
    folded = mute_fold(&scratch);
    arr_load_pattern(s, &scratch);
    app.slot_buf[s] = -1;
    slot_cache(s, &scratch);
    app.saved[s] = arr_dirty[s];
    app.edited[s] = (uint8_t)(folded && style < ARR_USER_PATTERNS);   /* saved again, its mutes moved out */
    app.edit_pend &= (uint8_t)~(1u << s);
}
/* a user pattern holding nothing: never saved, or saved without notes (loaded into scratch to look) */
static int pattern_blank(int p)
{
    uint8_t h[8];
    if (plat_store_load((uint32_t)(OBJ_PAT0 + p), h, sizeof h) < 0)
        return 1;
    return project_pattern_load(p, &scratch) == 0 && scratch.nev == 0;
}
/* the user pattern the copy-on-write takes: one no song plays and that holds nothing (a pattern with notes is
 * never taken, played or not); -1 none */
static int pattern_free(void)
{
    uint32_t used = 0;
    for (int n = 0; n < ARR_SONGS; n++)
        for (int k = 0; k < ARR_NSECT; k++)
            if (songs.song[n].style[k] < ARR_USER_PATTERNS)
                used |= 1u << songs.song[n].style[k];
    for (int p = 0; p < ARR_USER_PATTERNS; p++)
        if (!(used >> p & 1u) && pattern_blank(p))
            return p;
    return -1;
}
/* copy-on-write: before slot s's pattern is changed (recorded into, edited, erased, cleared), a slot playing a
 * style's section gets a user pattern of its own: the first free one (pattern_free) takes the section as it
 * plays now, saved at the next save (or autosave). 0: the slot plays a user pattern; -1: none free (the change
 * plays, unsaved, until another pattern is picked) */
static int slot_own(int s)
{
    song_t *g = &songs.song[proj.song];
    int p;
    char t[8];
    if (app.slot_buf[s] >= 0) {                  /* a QY70 section: a pattern of its notes (up to 600) from here */
        arr_pattern_copy(s, &scratch);
        arr_load_pattern(s, &scratch);
        app.slot_buf[s] = -1;
    }
    if (g->style[s] < ARR_USER_PATTERNS)
        return 0;
    if ((p = pattern_free()) < 0) {
        say("NO FREE PATTERN", "not kept");
        return -1;
    }
    g->style[s] = (uint16_t)p;
    app.edited[s] = 1;                           /* saved, changed or not: the song plays it now */
    app.songs_dirty = 1;
    app.chg++;
    app.gen++;
    t[0] = 'P';
    t[1] = (char)('0' + (p + 1) / 10);
    t[2] = (char)('0' + (p + 1) % 10);
    t[3] = 0;
    say("NEW PATTERN", t);
    return 0;
}
/* MODE: FREE (the keys' chords only), LOOP (app.loop goes round), SONG (the song's rows: from app.row stopped,
 * else from row 1 at the next bar line). Leaving SONG, the song's FREE / LOOP mutes are all sent again (a row's
 * or a hand mute may be sounding) */
static void mode_set(int m)
{
    song_t *g = csong();
    app.mode = (uint8_t)m;
    if (m == ARR_LOOP)
        arr_mode_loop(app.loop);
    else if (m == ARR_SONG)
        arr_mode_song(g, arr_playing ? 0 : app.row);
    else
        arr_mode_free();
    if (m != ARR_SONG) {
        app.mute_sent = (uint8_t)~g->mute;
        mutes_apply();
    }
}
/* the selected song: its six patterns into the slots, its tempo and TRANSPOSE, its mutes, MODE again on it */
static int one_bank_style(song_t *g, int b);
static void song_load(void)
{
    song_t *sg = &songs.song[proj.song];
    for (int s = 0; s < ARR_NSECT; s++)          /* one QY70 style a song: its first's */
        if (sg->style[s] >= ARR_BANK0) {
            if (one_bank_style(sg, (sg->style[s] - ARR_BANK0) / ARR_NSECT))
                app.songs_dirty = 1;
            break;
        }
    for (int s = 0; s < ARR_NSECT; s++)
        slot_load(s, sg->style[s]);
    arr_set_tempo(sg->tempo);
    arr_set_transpose(sg->transpose);
    if (app.row >= ARR_ROWS)
        app.row = 0;
    mode_set(app.mode);                          /* (FREE / LOOP: the song's mutes sent) */
}
/* the takes: when each slot was last recorded into (or erased), so a save knows the newest */
static void watch_takes(void)
{
    for (int s = 0; s < ARR_NSECT; s++) {
        uint16_t d = arr_dirty[s];
        if (d != app.seen[s]) {
            app.seen[s] = d;
            app.take_t[s] = plat_ms();
            app.chg++;
            app.gen++;
        }
    }
}
/* what changed: the project, the songs, and each user pattern recorded into or edited since it was loaded or
 * saved. Slots sharing a user pattern (a song may choose that) get the saved pattern too, by
 * arr_store_pattern (refused while they are recorded into: then they stay unsaved); when two of them were
 * changed, the newest change is kept and the screen says so. */
static int save_all(void)
{
    const song_t *sg = &songs.song[proj.song];
    uint32_t done = 0;
    int rc = 0;
    edits_flush();
    watch_takes();
    if (app.dirty) {
        if (project_save() == 0)
            app.dirty = 0;
        else
            rc = -1;
    }
    if (app.songs_dirty) {
        if (project_songs_save(&songs) == 0)
            app.songs_dirty = 0;
        else
            rc = -1;
    }
    for (int s = 0; s < ARR_NSECT; s++) {
        int st = sg->style[s], k = s, lost = -1;
        uint16_t d;
        if (st >= ARR_USER_PATTERNS || (done >> s & 1u) || !slot_unsaved(s))
            continue;
        for (int j = 0; j < ARR_NSECT; j++)     /* the newest change of the slots sharing it */
            if (sg->style[j] == st) {
                done |= 1u << j;
                if (j != k && slot_unsaved(j) && (int32_t)(app.take_t[j] - app.take_t[k]) >= 0)
                    k = j;
            }
        if (app.edit_pend >> k & 1u) {           /* its edits could not be stored yet: later */
            rc = -1;
            continue;
        }
        d = arr_pattern_copy(k, &scratch);
        if (project_pattern_save(st, &scratch) != 0) {
            rc = -1;
            continue;
        }
        app.saved[k] = d;
        app.edited[k] = 0;
        for (int j = 0; j < ARR_NSECT; j++) {
            uint16_t e;
            if (j == k || sg->style[j] != st)
                continue;
            e = arr_dirty[j];
            if (slot_unsaved(j))
                lost = j;                        /* its changes give way to the newest */
            if (arr_store_pattern(j, &scratch, e)) {
                slot_cache(j, &scratch);
                app.saved[j] = e;
                app.edited[j] = 0;
                app.edit_pend &= (uint8_t)~(1u << j);
            }
        }
        if (lost >= 0) {
            char b[24];
            cat_s(cat_s(b, "kept "), SECT_NAME[k + 1]);
            say("SHARED PATTERN", b);
        }
    }
    return rc;
}

static void fx_apply(void) { gm_fx(proj.rev_type, proj.rev_level, proj.cho_type, proj.cho_level); }

void app_start(void)
{
    memset(&app, 0, sizeof app);                 /* MODE FREE, loop L1, row 1 */
    for (int s = 0; s < ARR_NSECT; s++)
        app.slot_buf[s] = -1;
    qbuf[0].index = qbuf[1].index = -1;
    qb_mount();                                  /* the QY70 style bank, if one is installed */
    project_songs_load(&songs);
    arr_loops(songs.loop);                       /* the engine reads the loops from here (writes: arr_edit) */
    arr_set_quant(proj.quant);
    arr_midi_out_on(proj.mout);                  /* the tracks out on their channels */
    fx_apply();
    song_load();                                 /* (the song's tempo and transpose) */
    arr_record(proj.trk, 0);                     /* the keys play the selected track */
}

/* ------------------------------------------------------------ controls --- */
static void touch(int k)
{
    ui.touched = (int8_t)k;
    ui.touch_until = plat_ms() + 1200u;
}
/* the song's tempo (HOME's KNOB 2, HOME + SELECT) and TRANSPOSE (KNOB 3): kept in the song */
static void song_touched(void)
{
    app.songs_dirty = 1;
    app.chg++;
    ui.act_t = plat_ms();
}
static void set_tempo(int bpm)
{
    song_t *g = csong();
    bpm = clampi(bpm, 40, 240);
    if (bpm == g->tempo)
        return;
    g->tempo = (uint8_t)bpm;
    arr_set_tempo(bpm);
    song_touched();
}
static void set_transpose(int x)
{
    song_t *g = csong();
    x = clampi(x, -6, 5);
    if (x == g->transpose)
        return;
    g->transpose = (int8_t)x;
    arr_set_transpose(x);
    song_touched();
}
static void set_track(int t)
{
    proj.trk = (uint8_t)(t & 7);
    arr_record(proj.trk, arr_recording);         /* recording goes on, into this track */
    say(TRACK_NAME[proj.trk], trk_sound(&app.trk[eslot()][proj.trk]));
    mark_dirty();
}

static void song_job_run(void);
static void song_pick(int d);
/* the keys held as chords (KM_CHORD), to the chord recogniser (every change) */
static void chord_keys(void)
{
    uint8_t n[NKEYS];
    int c = 0;
    for (int i = 0; i < NKEYS; i++)
        if (ui.kmode[i] == KM_CHORD)
            n[c++] = ui.knote[i];
    arr_keys_held(n, c);
}
/* loop i (HOME + white key, PRESETS on HOME / LOOP): MODE LOOP on it (while playing, from the next bar line); the
 * loop playing picked again: FREE */
static void loop_pick(int i)
{
    char t[16];
    if (i < 0 || i >= ARR_LOOPS)
        return;
    if (arr_mode == ARR_LOOP && arr_loop_now == i) {
        mode_set(ARR_FREE);
        say("FREE", "no loop");
        return;
    }
    app.loop = (uint8_t)i;
    mode_set(ARR_LOOP);
    loop_label(i, t);
    say(arr_playing ? "NEXT" : "LOOP", t);
}
/* HOME held + a key: a loop (white 1..16) or a section (black 1..5: Intro, Main A, Main B, Fill, Ending); the
 * other black keys do nothing */
static const uint8_t HOME_SECT[5] = {SEC_INTRO, SEC_A, SEC_B, SEC_FILL, SEC_END};
static void home_key(int k)
{
    int w = ordinal(WHITE_K, 16, k), b = ordinal(BLACK_K, 11, k);
    if (w >= 0)
        loop_pick(w);
    else if (b >= 0 && b < 5) {
        int sec = HOME_SECT[b];
        if (sec == SEC_FILL && arr_playing && (arr_sect == SEC_INTRO || arr_sect == SEC_END)) {
            say("NO FILL", arr_sect == SEC_INTRO ? "in the Intro" : "in the Ending");   /* (the engine's rule) */
            return;
        }
        arr_section(sec);
        if (sec == SEC_FILL && arr_playing)      /* the Fill it will be, by the Main playing */
            sec = arr_sect == SEC_B || arr_sect == SEC_FILL_BA ? SEC_FILL_BA : SEC_FILL_AB;
        say(arr_playing ? "NEXT" : "FIRST", SECT_NAME[sec]);
        ui.msg_first = !arr_playing;             /* "FIRST ..." goes when PLAY starts it */
    }                                            /* black keys 6..11: nothing */
}
/* what the keys do: the screen says (the spec's Keys table) */
enum { KR_NONE, KR_CHORDS, KR_TRACK, KR_PICK };
static int key_role(void)
{
    if (ui.page == P_HOME || ui.page == P_LOOP || ui.page == P_LOOP2)
        return KR_CHORDS;
    if (ui.page == P_PATTERN || ui.page == P_PATSET || ui.page == P_TRACK || ui.page == P_TRACK2)
        return KR_TRACK;
    if (ui.page == P_MIXER || ui.page == P_TONE)
        return KR_PICK;
    return ui.page == P_SONG ? KR_PICK : KR_NONE;
}
static void key_down(int k)
{
    int r = key_role(), b = ordinal(BLACK_K, 11, k);
    ui.knote[k] = (uint8_t)key_note(k);
    ui.kmode[k] = KM_NONE;
    if (ui.btn >> B_HOME & 1u) {                 /* HOME held: a shortcut (its release is no page press) */
        ui.btn_used |= 1u << B_HOME;
        home_key(k);
    } else if (r == KR_CHORDS) {
        ui.kmode[k] = KM_CHORD;
        chord_keys();
    } else if (r == KR_TRACK) {
        ui.kmode[k] = KM_PLAY;
        arr_key(ui.knote[k], 1, 100);            /* the selected track's sound; recorded when recording */
    } else if (r == KR_PICK && ui.page != P_SONG && b >= 0 && b < ARR_TRACKS)
        set_track(b);                            /* the mixer: black key i, track i (SONG's keys: its screen) */
}
static void key_up(int k)
{
    uint8_t m = ui.kmode[k];
    ui.kmode[k] = KM_NONE;
    if (m == KM_PLAY)
        arr_key(ui.knote[k], 0, 0);
    else if (m == KM_CHORD)
        chord_keys();
}

/* the page buttons (PAGE_BTN, pages.c): pressed again, the next page; SELECT turns them both ways */
static int page_button(int b)
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (PAGE_BTN[i].btn == b) {
            int f = PAGE_BTN[i].first, n = PAGE_BTN[i].n;
            ui.page = (uint8_t)(ui.page >= f && ui.page < f + n ? f + (ui.page - f + 1) % n : f);
            ui.touched = -1;
            return 1;
        }
    return 0;
}
static void page_turn(int d)
{
    int g = page_group(ui.page), f = PAGE_BTN[g].first, n = PAGE_BTN[g].n;
    ui.page = (uint8_t)(f + ((ui.page - f + d) % n + n) % n);
    ui.touched = -1;
    ui.job = 0;
}

/* ------------------------------------------------------------- jobs --- */
/* SAVE held: a job on the pattern playing (its slot) and the selected track; KNOB 1 picks, KNOB 2 the copy's
 * destination, SEL does it, SAVE (or a page) cancels */
static void jobs_open(void)
{
    int st = songs.song[proj.song].style[eslot()];
    ui.job = 1;
    ui.job_sel = 0;
    ui.job_to = (uint8_t)(st < ARR_USER_PATTERNS ? (st + 1) % ARR_USER_PATTERNS : pattern_free() < 0 ? 0 : pattern_free());
    ui.touched = -1;
}
static void slot_style(int s, int to);
static void pat_name(int st, char *t)            /* "P05" */
{
    t[0] = 'P';
    t[1] = (char)('0' + (st + 1) / 10);
    t[2] = (char)('0' + (st + 1) % 10);
    t[3] = 0;
}
static void job_run(void)
{
    int s = eslot(), st = songs.song[proj.song].style[s];
    char t[8];
    ui.job = 0;
    edits_flush();
    if (ui.job_sel == 3) {                       /* back to the song's style: its section, the user pattern kept */
        int i = style_index(songs.song[proj.song].sty);
        if (st >= ARR_USER_PATTERNS)
            say(SECT_NAME[s + 1], "plays the style");
        else if (i >= 0) {
            slot_style(s, style_number(i, s));   /* (saved first; refused while playing with changes unsaved) */
            if (songs.song[proj.song].style[s] != st) {
                char b[24];
                style_name(songs.song[proj.song].style[s], b);
                say("BACK TO", b);
            }
        }
        return;
    }
    if (ui.job_sel != 1) {                       /* erase, clear: a style's section gets a user pattern first */
        slot_own(s);
        st = songs.song[proj.song].style[s];
    }
    if (ui.job_sel == 0) {                       /* erase the selected track's notes */
        arr_erase_track(proj.trk);
        say("ERASED", TRACK_NAME[proj.trk]);
    } else if (ui.job_sel == 2) {                /* clear the pattern: every note gone, its sounds kept */
        for (int tries = 0; tries < 8; tries++) {
            uint16_t d = arr_pattern_copy(s, &scratch);
            scratch.nev = 0;
            for (int k = 0; k < ARR_TRACKS; k++)
                scratch.trk[k].first = scratch.trk[k].count = 0;
            if (arr_store_pattern(s, &scratch, d)) {
                edited(s);
                app.edit_pend &= (uint8_t)~(1u << s);
                app.edited[s] = 1;
                say("CLEARED", SECT_NAME[s + 1]);
                return;
            }
        }
        say("NOT CLEARED", "RECORDING");
    } else {                                     /* copy it (as it is now) into a user pattern */
        int to = ui.job_to;
        uint16_t d;
        pat_name(to, t);
        if (arr_playing) {                       /* a flash write pauses the audio */
            say("COPY", "STOP FIRST");
            return;
        }
        for (int j = 0; j < ARR_NSECT; j++)      /* another section plays it, changed: those changes first */
            if (j != s && songs.song[proj.song].style[j] == to && slot_unsaved(j)) {
                char a[24];
                cat_s(cat_s(a, t), " UNSAVED IN");
                say(a, SECT_NAME[j + 1]);
                return;
            }
        d = arr_pattern_copy(s, &scratch);
        if (project_pattern_save(to, &scratch) != 0) {
            say("NOT COPIED", "FLASH ERROR");
            return;
        }
        if (to == st) {                          /* into its own: saved */
            app.saved[s] = d;
            app.edited[s] = 0;
        }
        for (int j = 0; j < ARR_NSECT; j++)      /* the other slots playing that pattern get the copy */
            if (j != s && songs.song[proj.song].style[j] == to) {
                arr_load_pattern(j, &scratch);
                slot_cache(j, &scratch);
                app.saved[j] = arr_dirty[j];
            }
        say("COPIED TO", t);
    }
}

static void button_down(int b)
{
    if (page_button(b)) {
        ui.job = 0;
        return;
    }
    switch (b) {
    case B_PLAY:
        if (!arr_playing && app.mode == ARR_SONG)   /* SONG: from the SONG screen's row, else row 1 */
            arr_mode_song(csong(), ui.page == P_SONG ? app.row : 0);
        if (ui.msg_first)                        /* the section asked for first: now playing */
            ui.msg_until = plat_ms();
        ui.msg_first = 0;
        arr_play(!arr_playing);
        break;
    case B_REC:
    {
        int on = !arr_recording, s = eslot();   /* the state asked for (the engine's follows a block later) */
        int st = songs.song[proj.song].style[s];
        char a[16], t[16];
        if (on && key_role() != KR_TRACK) {      /* REC records what the keys play: a track on SEQ, EDIT */
            say("REC", "on SEQ, EDIT");
            break;
        }
        if (on)
            slot_own(s);                         /* recording into a style's section: a user pattern first */
        arr_record(proj.trk, on);
        ui.rec_want = (uint8_t)on;
        ui.rec_seen = 0;
        cat_s(cat_s(a, "RECORD "), TRACK_NAME[proj.trk]);
        if (!on)
            say("RECORD OFF", 0);
        else if (songs.song[proj.song].style[s] != st) {   /* a new user pattern took the section: say which */
            pat_name(songs.song[proj.song].style[s], cat_s(t, "into new "));
            say(a, t);
        } else
            say(a, SECT_NAME[s + 1]);
    }
        break;
    default: break;
    }
}
static void button_up(int b)                     /* a tap: released before it was used as a hold */
{
    switch (b) {
    case B_SEL:
        if (ui.job == 2)
            song_job_run();
        else if (ui.job)
            job_run();
        break;
    case B_HOME:                                 /* HOME on release: no key, SELECT or PRESETS used it */
        ui.job = 0;
        page_button(B_HOME);
        break;
    case B_SAVE: {
        if (ui.job) {
            ui.job = 0;
            say("JOB", "CANCELLED");
            break;
        }
        uint32_t m = ui.msg_until;
        int rc = save_all();
        if (rc != 0)
            say("NOT SAVED", "FLASH ERROR");
        else if (ui.msg_until == m)              /* (else what the save said stays: a shared pattern) */
            say("SAVED", 0);
        break;
    }
    case B_OCTDN: case B_OCTUP:
        proj.oct = (int8_t)(proj.oct + (b == B_OCTUP ? 1 : -1));
        proj.oct = proj.oct < -3 ? -3 : proj.oct > 3 ? 3 : proj.oct;
        {
            char t[4];
            t[0] = proj.oct < 0 ? '-' : '+';
            t[1] = (char)('0' + (proj.oct < 0 ? -proj.oct : proj.oct));
            t[2] = 0;
            say("OCTAVE", t);
        }
        mark_dirty();
        break;
    default: break;
    }
}
#define HOLD_MS 600u
static void holds(void)
{
    uint32_t now = plat_ms();
    if ((ui.btn >> B_SAVE & 1u) && !(ui.btn_used >> B_SAVE & 1u) && now - ui.btn_t[B_SAVE] > HOLD_MS) {
        ui.btn_used |= 1u << B_SAVE;
        if (ui.page >= P_PATTERN && ui.page <= P_MASTER)
            jobs_open();                         /* the pattern's jobs */
        else if (ui.page == P_SONG) {
            ui.job = 2;                          /* the song's */
            ui.job_sel = 0;
            ui.job_to = (uint8_t)((proj.song + 1u) % ARR_SONGS);
            ui.touched = -1;
        } else
            say("JOBS", "on the Pattern, Song pages");
    }
    if ((ui.btn >> B_HOME & 1u) && now - ui.btn_t[B_HOME] >= HOME_HOLD_MS)
        ui.btn_used |= 1u << B_HOME;             /* HOME held to look (the loops, the sections): not a page press */
}

/* another song: recording off, what changed saved (else nothing changes: the takes would be lost), its
 * patterns loaded */
static void song_change(int s)
{
    char t[12];
    int rec = arr_recording;
    arr_record(proj.trk, 0);
    ui.rec_want = 0;
    if (unsaved() && save_all() != 0) {
        say("NOT SAVED", "SONG KEPT");
        if (rec)
            arr_record(proj.trk, 1);             /* as it was */
        return;
    }
    proj.song = (uint8_t)s;
    app.row = 0;
    song_load();
    mark_dirty();
    itoa_u((uint32_t)s + 1u, cat_s(t, "SONG "));
    say(t, 0);
}

/* ------------------------------------------------------ the song's pages --- */
/* the pattern d steps on from st for slot s: the user patterns P01..P16, then the styles by name, each its
 * pattern for that section (Main A's knob: Pop's Main A, Funk's Main A, ...) */
static int style_step(int s, int st, int d)
{
    int si = style_index(st), i = si >= 0 ? ARR_USER_PATTERNS + si : st < ARR_USER_PATTERNS ? st : 0;
    i = clampi(i + d, 0, ARR_USER_PATTERNS + styles_n() - 1);
    return i < ARR_USER_PATTERNS ? i : style_number(i - ARR_USER_PATTERNS, s);
}
/* a slot's pattern changed to pattern `to` (a user pattern or a style's section; the STYLE pages', the
 * Pattern page's PRESETS); its changes saved first (stopped), or refused while playing (a save pauses the
 * audio) */
static void slot_style(int s, int to)
{
    int st = songs.song[proj.song].style[s], main_a = songs.song[proj.song].style[SEC_A - 1], a;
    char t[24];
    if (to == st)
        return;
    if (to >= ARR_BANK0 && bank_get((to - ARR_BANK0) / ARR_NSECT) < 0)
        return;                                  /* missing or damaged (said): nothing changes */
    if (slot_unsaved(s)) {
        if (arr_playing) {
            say("UNSAVED", "SAVE FIRST");
            return;
        }
        if (save_all() != 0) {
            say("NOT SAVED", "PATTERN KEPT");
            return;
        }
    }
    songs.song[proj.song].style[s] = (uint16_t)to;
    app.songs_dirty = 1;
    app.chg++;
    slot_load(s, to);
    if (to >= ARR_BANK0 && one_bank_style(&songs.song[proj.song], (to - ARR_BANK0) / ARR_NSECT))
        for (int j = 0; j < ARR_NSECT; j++)      /* the song's other QY70 sections: this style's now */
            if (j != s && songs.song[proj.song].style[j] >= ARR_BANK0 && app.slot_buf[j] != app.slot_buf[s])
                slot_load(j, songs.song[proj.song].style[j]);
    song_style_derive(&songs.song[proj.song]);   /* the song's style: its sections' (Main A's first) */
    a = songs.song[proj.song].style[SEC_A - 1];
    if (a != main_a && style_index(a) >= 0 && !songs.song[proj.song].nrows)   /* a style for a new song: its tempo */
        set_tempo(style_tempo(style_index(a)));
    style_name(to, t);
    say(SECT_NAME[s + 1], t);
}
/* a song plays one QY70 bank style at a time (the app keeps one decoded, and one more for a switch): its sections
 * of another bank style become style b's same section. 1: one changed */
static int one_bank_style(song_t *g, int b)
{
    int changed = 0;
    for (int j = 0; j < ARR_NSECT; j++)
        if (g->style[j] >= ARR_BANK0 && (g->style[j] - ARR_BANK0) / ARR_NSECT != b) {
            g->style[j] = (uint16_t)(ARR_BANK0 + ARR_NSECT * b + j);
            changed = 1;
        }
    return changed;
}
/* the QY70 bank written (bank_rx.c) or loaded (the simulator): mounted again, the song's patterns again */
void app_bank_changed(void)
{
    for (int s = 0; s < ARR_NSECT; s++)          /* no slot plays a decoded style: both buffers free */
        if (app.slot_buf[s] >= 0) {
            pattern_empty(&scratch);
            arr_load_pattern(s, &scratch);
            app.slot_buf[s] = -1;
        }
    qbuf[0].index = qbuf[1].index = -1;
    qb_mount();
    song_load();
}
static void style_all(int n);
/* the Style knob (HOME's KNOB 1; the STYLE pages' KNOB 4, PRESETS there): d styles on from the
 * song's (a QY70 style with no bank: from the first) */
static void style_turn(int d)
{
    int i = style_index(songs.song[proj.song].sty);
    int n = clampi((i < 0 ? -1 : i) + d, 0, styles_n() - 1);
    if (n != i)
        style_all(n);
}
/* the song's style becomes style n (0..styles_n()-1): the sections playing a style's pattern play n's now; a
 * section on a user pattern (recorded into, edited) keeps it (the jobs' Back to style puts it back). Never a
 * save: a style's section has nothing unsaved (its first change gives it a user pattern) */
static void style_all(int n)
{
    song_t *g = &songs.song[proj.song];
    if (style_bank(n) && bank_get(n - arr_styles()) < 0)
        return;                                  /* a damaged QY70 style (said): nothing changes */
    for (int s = 0; s < ARR_NSECT; s++)
        if (g->style[s] >= ARR_USER_PATTERNS)
            slot_style(s, style_number(n, s));
    g->sty = (uint16_t)style_number(n, 0);       /* (also with every section on a user pattern) */
    app.songs_dirty = 1;
    app.chg++;
    app.gen++;
    if (!g->nrows)                               /* a style for a new song: its tempo */
        set_tempo(style_tempo(n));
    say(style_bank(n) ? "QY70 STYLE" : "STYLE", style_label(n));
}
/* another song, d on (PRESETS with HOME held); while playing it waits for the stop (a
 * save, a flash write, would pause the audio) */
static void song_pick(int d)
{
    int s = (app.song_pend ? app.song_pend - 1 : proj.song) + (d > 0 ? 1 : -1);
    char t[12];
    if (s < 0 || s >= ARR_SONGS)
        return;
    if (arr_playing) {
        app.song_pend = (uint8_t)(s == proj.song ? 0 : s + 1);
        itoa_u((uint32_t)s + 1u, cat_s(t, "SONG "));
        say(t, s == proj.song ? "stays" : "after the stop");
    } else
        song_change(s);
}
/* the other pages' knobs: STYLE (three sections a page, KNOB 4 the style), Setup (SONG, LOOP: their screens) */
static void song_page_knob(int k, int32_t e)
{
    song_t *g = csong();
    int d = e > 0 ? 1 : -1;
    switch (ui.page) {
    case P_STYLE: case P_STYLE2: {
        int first = ui.page == P_STYLE2 ? 3 : 0;
        if (k < 3)
            slot_style(first + k, style_step(first + k, g->style[first + k], accel(EN_K1 + k, e)));
        else
            style_turn(accel(EN_K4, e));
        break;
    }
    case P_SETUP:
        if (k == 0)
            proj.clock = (uint8_t)(d > 0);
        else if (k == 1) {
            proj.mout = (uint8_t)(d > 0);
            arr_midi_out_on(proj.mout);
        } else if (k == 2)
            proj.leds = (uint8_t)clampi(proj.leds + d, 0, 2);
        else
            break;
        mark_dirty();
        break;
    default: break;
    }
}
/* Copy song: the target plays the same styles' sections; each user pattern of the source is copied into a user
 * pattern of the target's own: the target's own one in that section when no other song plays it, else a blank
 * one no other song plays (pattern_blank: one with notes is never taken); -1: none left */
static int copy_patterns_pick(int to, uint16_t *st)
{
    const song_t *g = csong(), *d = &songs.song[to];
    uint32_t other = 0, taken = 0;
    for (int n = 0; n < ARR_SONGS; n++)          /* the user patterns the songs but the target play */
        for (int j = 0; j < ARR_NSECT && n != to; j++)
            if (songs.song[n].style[j] < ARR_USER_PATTERNS)
                other |= 1u << songs.song[n].style[j];
    for (int i = 0; i < ARR_NSECT; i++) {
        int pick = -1;
        st[i] = g->style[i];
        if (g->style[i] >= ARR_USER_PATTERNS)
            continue;                            /* a style's section: the same */
        if (d->style[i] < ARR_USER_PATTERNS && !((other | taken) >> d->style[i] & 1u))
            pick = d->style[i];
        for (int p = 0; p < ARR_USER_PATTERNS && pick < 0; p++)
            if (!((other | taken) >> p & 1u) && pattern_blank(p))
                pick = p;
        if (pick < 0)
            return -1;
        st[i] = (uint16_t)pick;
        taken |= 1u << pick;
    }
    return 0;
}
/* the song's jobs (SAVE held on SONG): clear it (its rows and settings; its patterns, style, tempo, transpose and
 * mutes kept, the loops too), or copy it to another */
static void song_job_run(void)
{
    song_t *g = csong();
    ui.job = 0;
    if (ui.job_sel == 0) {
        uint8_t mute = g->mute, tempo = g->tempo;
        int8_t xp = g->transpose;
        arr_edit(1);                             /* (the engine may be playing its rows) */
        loops_song_clear(g);                     /* (its style kept) */
        g->mute = mute;
        g->tempo = tempo;
        g->transpose = xp;
        arr_edit(0);
        app.row = 0;
        song_touched();
        say("CLEARED", "the song");
    } else {
        int to = ui.job_to % ARR_SONGS;
        song_t *d = &songs.song[to];
        uint16_t st[ARR_NSECT];
        char a[16];
        if (to == proj.song) {
            say("COPY", "SAME SONG");
            return;
        }
        if (arr_playing) {                       /* it writes patterns: a flash write pauses the audio */
            say("COPY", "STOP FIRST");
            return;
        }
        edits_flush();
        if (copy_patterns_pick(to, st) != 0) {
            say("NOT COPIED", "NO FREE PATTERN");
            return;
        }
        for (int i = 0; i < ARR_NSECT; i++) {    /* the patterns' contents, as they play now; the song after */
            if (st[i] >= ARR_USER_PATTERNS)
                continue;
            arr_pattern_copy(i, &scratch);
            if (project_pattern_save(st[i], &scratch) != 0) {
                say("FLASH ERROR", "song unchanged");
                return;
            }
        }
        *d = *g;
        memcpy(d->style, st, sizeof st);
        app.songs_dirty = 1;
        app.chg++;
        itoa_u((uint32_t)to + 1u, cat_s(itoa_u(proj.song + 1u, cat_s(a, "SONG ")), " > "));
        say(a, "copied");
    }
}

/* the record grid (Pattern page KNOB 1): clockwise finer, 1/8 .. 1/32, then off */
static void grid_turn(int32_t e)
{
    int q = proj.quant + (e > 0 ? 1 : -1);
    if (q >= 0 && q < ARR_NQ) {
        proj.quant = (uint8_t)q;
        arr_set_quant(q);
        mark_dirty();
    }
}
/* HOME's knobs: the song's style, tempo, TRANSPOSE and MODE */
static void home_knob(int k, int32_t e)
{
    song_t *g = csong();
    switch (k) {
    case 0: style_turn(accel(EN_K1, e)); break;
    case 1: set_tempo(g->tempo + accel(EN_K2, e)); break;
    case 2: set_transpose(g->transpose + e); break;   /* (no acceleration: 12 steps) */
    case 3: {
        int m = clampi(app.mode + e, ARR_FREE, ARR_SONG);
        if (m != app.mode)
            mode_set(m);
        break;
    }
    default: break;
    }
}
/* an 8-bit setting of the selected track (of the pattern playing) moved, kept in lo..hi */
static void trk_set(uint8_t *v, int to, int lo, int hi)
{
    to = clampi(to, lo, hi);
    if (*v == (uint8_t)to)
        return;
    *v = (uint8_t)to;
    edited(eslot());
}
#define MUTE_BURST_MS 150u                       /* detents closer than this: one turn */
/* a track muted / unmuted: the song's (every section), live; not an edit of the pattern (a style's section stays
 * the style's). In SONG by hand only, until the next row (the rows' mutes are theirs) */
static void mute_toggle(int trk)
{
    if (app.mode == ARR_SONG) {
        arr_track_mute(trk & 7, !(arr_mutes >> (trk & 7) & 1u));
        ui.act_t = plat_ms();
        return;
    }
    csong()->mute ^= (uint8_t)(1u << (trk & 7));
    mutes_apply();
    app.songs_dirty = 1;
    app.chg++;
    app.gen++;
    ui.act_t = plat_ms();
}
static void edit_knob(int k, int32_t e)
{
    int s = eslot(), d = e > 0 ? 1 : -1;
    arr_trk_t *t = &app.trk[s][proj.trk];
    switch (ui.page) {
    case P_PATTERN:
        if (k == 0)
            grid_turn(e);                        /* the record grid */
        else if (k == 3)
        {                                        /* a turn, either way: muted / plays; once a burst */
            uint32_t now = plat_ms();
            if (now - ui.mute_t >= MUTE_BURST_MS)
                mute_toggle(proj.trk);
            ui.mute_t = now;
        }
        break;
    case P_PATSET:
        if (k == 0 || k == 1) {
            uint8_t *v = k == 0 ? &app.hdr[s].bars : &app.hdr[s].beats;
            int to = clampi(*v + d, k == 0 ? 1 : 2, k == 0 ? ARR_MAXBARS : 7);
            if (to != *v) {
                *v = (uint8_t)to;
                edited(s);
            }
        } else if (k == 2)
            ui.name_cur = (uint8_t)clampi(ui.name_cur + d, 0, 7);
        else {
            char *c = &app.hdr[s].name[ui.name_cur];
            int i = 0;
            while (i < NNAME_CHARS && NAME_CHARS[i] != (*c ? *c : ' '))
                i++;
            i = ((i + accel(EN_K4, e)) % NNAME_CHARS + NNAME_CHARS) % NNAME_CHARS;
            *c = NAME_CHARS[i];
            edited(s);
        }
        break;
    case P_TRACK:
        if (k == 0) {
            if ((t->chan & 15u) != 9u)
                trk_set(&t->program, t->program + accel(EN_K1, e), 0, 127);
        } else if (k == 1)
            trk_set(&t->x.type, t->x.type + d, 0, NXT - 1);
        else if (k == 2)
            trk_set(&t->x.lo, t->x.lo + accel(EN_K3, e), 0, t->x.hi);
        else
            trk_set(&t->x.hi, t->x.hi + accel(EN_K4, e), t->x.lo, 127);
        break;
    case P_TRACK2:
        if (k == 0)
            trk_set(&t->x.hikey, t->x.hikey + d, 0, XT_NOKEY);
        else if (k == 1)
            trk_set(&t->chan, t->chan + d, 0, 15);
        break;
    case P_MIXER: {
        uint8_t *v = k == 0 ? &t->vol : k == 1 ? &t->pan : k == 2 ? &t->rev : &t->cho;
        trk_set(v, *v + accel(EN_K1 + k, e), 0, 127);
        break;
    }
    case P_TONE:                                 /* brightness, resonance: stored XOR 64 (0 neutral) */
        if (k < 2) {
            uint8_t *v = k == 0 ? &t->bright : &t->reso;
            int to = clampi((*v ^ 64) + accel(EN_K1 + k, e), 0, 127);
            trk_set(v, to ^ 64, 0, 127);
        }
        break;
    case P_MASTER: {
        uint8_t *v = k == 0 ? &proj.rev_type : k == 1 ? &proj.rev_level : k == 2 ? &proj.cho_type : &proj.cho_level;
        int to = k == 1 || k == 3 ? clampi(*v + accel(EN_K1 + k, e), 0, 127) : clampi(*v + d, 0, k == 0 ? 4 : 3);
        if (to != *v) {
            *v = (uint8_t)to;
            fx_apply();
            mark_dirty();
        }
        break;
    }
    default: break;
    }
}
static void knob(int k, int32_t e)
{
    if (ui.job) {                                /* the jobs: KNOB 1 the job, KNOB 2 the copy's pattern / song */
        int n = ui.job == 2 ? ARR_SONGS : ARR_USER_PATTERNS;
        if (k == 0)
            ui.job_sel = (uint8_t)clampi(ui.job_sel + (e > 0 ? 1 : -1), 0, ui.job == 2 ? 1 : 3);
        else if (k == 1)
            ui.job_to = (uint8_t)(((ui.job_to + accel(EN_K2, e)) % n + n) % n);
        touch(k);
        return;
    }
    if (ui.page == P_HOME)
        home_knob(k, e);
    else if (ui.page >= P_PATTERN && ui.page <= P_MASTER)
        edit_knob(k, e);
    else if (ui.page != P_PANEL)
        song_page_knob(k, e);
    else
        return;
    touch(k);
}
/* PRESETS on the Pattern pages: the pattern the section playing plays (the song's choice); while playing only
 * when its changes are saved (a save would pause the audio) */
static void pattern_pick(int32_t e)
{
    int s = eslot();
    slot_style(s, style_step(s, songs.song[proj.song].style[s], e > 0 ? 1 : -1));
}
static void presets(int32_t e)
{
    int d = e > 0 ? 1 : -1;
    if (ui.btn >> B_HOME & 1u) {                 /* HOME held: the song, on any page */
        ui.btn_used |= 1u << B_HOME;
        song_pick(d);
    } else if (ui.page == P_HOME || ui.page == P_LOOP || ui.page == P_LOOP2) {   /* the next loop (MODE LOOP) */
        int n = app.mode == ARR_LOOP ? clampi(app.loop + d, 0, ARR_LOOPS - 1) : app.loop;
        if (app.mode != ARR_LOOP || n != app.loop)
            loop_pick(n);
    } else if (ui.page == P_PATTERN || ui.page == P_PATSET)
        pattern_pick(e);
    else if (ui.page == P_STYLE || ui.page == P_STYLE2) {   /* the style browser: all six from the next style */
        style_turn(d);
        touch(3);
    } else if (ui.page == P_TRACK) {             /* a family of eight, as the GM module */
        arr_trk_t *t = &app.trk[eslot()][proj.trk];
        if ((t->chan & 15u) != 9u) {
            trk_set(&t->program, (t->program / 8 + d) * 8, 0, 127);
            touch(0);
        }
    }
}

static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, t = st & 0xF0u;
        if (st >= 0xF8u && !proj.clock)
            continue;                            /* internal clock: the clock in ignored */
        if (st == 0xF8u)
            arr_clock(ARR_CLK_TICK);
        else if (st == 0xFAu)
            arr_clock(ARR_CLK_START);
        else if (st == 0xFBu)
            arr_clock(ARR_CLK_CONTINUE);
        else if (st == 0xFCu)
            arr_clock(ARR_CLK_STOP);
        if (st < 0x80u || st >= 0xF0u)
            continue;                            /* sysex and the rest: not for the synth */
        gm_midi(st, d1, d2);                     /* all 16 channels, as the GM module */
        if (t == 0xC0u && (st & 15u) != 9u)
            proj.program[st & 15u] = (uint8_t)d1;
        else if (t == 0xB0u) {
            uint32_t c = st & 15u;
            if (d1 == 7)
                proj.volume[c] = (uint8_t)d2;
            else if (d1 == 10)
                proj.pan[c] = (uint8_t)d2;
            else if (d1 == 91)
                proj.reverb[c] = (uint8_t)d2;
            else if (d1 == 93)
                proj.chorus[c] = (uint8_t)d2;
        }
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        int hold = i == B_SEL || i == B_SAVE || i == B_OCTDN || i == B_OCTUP || i == B_HOME;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            ui.btn_t[i] = plat_ms();
            if (!hold)
                button_down((int)i);
        } else if (hold && !(ui.btn_used & m))
            button_up((int)i);
    }
    if ((btn & (1u << B_OCTDN | 1u << B_OCTUP)) == (1u << B_OCTDN | 1u << B_OCTUP))
        ui.btn_used |= 1u << B_OCTDN | 1u << B_OCTUP;   /* both: update mode (main loop), not ours */
    holds();
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u) {
            if (keys >> i & 1u)
                key_down((int)i);
            else
                key_up((int)i);
        }
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (ui.btn >> B_HOME & 1u) {             /* HOME held: the tempo */
            ui.btn_used |= 1u << B_HOME;
            set_tempo(csong()->tempo + accel(EN_SELECT, e));
        } else
            page_turn(e > 0 ? 1 : -1);
    }
    if ((e = plat_enc(EN_ALGO)) != 0)
        set_track(proj.trk + (e > 0 ? 1 : -1));
    if ((e = plat_enc(EN_PRESET)) != 0)
        presets(e);
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            if (ui.btn >> B_HOME & 1u)
                ui.btn_used |= 1u << B_HOME;     /* HOME used with a knob: its release turns no page */
            knob((int)i, e);
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
}

/* ------------------------------------------------------------- lights --- */
static void leds(void)
{
    uint32_t b = 0, k = 0, i;
    int rec = arr_recording;
    if (arr_playing)
        b |= 1u << B_PLAY;
    if (rec)
        b |= 1u << B_REC;
    if (unsaved())
        b |= 1u << B_SAVE;                       /* something to save */
    for (i = 0; i < NPAGE_BTN; i++)
        if (ui.page >= PAGE_BTN[i].first && ui.page < PAGE_BTN[i].first + PAGE_BTN[i].n)
            b |= 1u << PAGE_BTN[i].btn;
    if (ui.btn >> B_HOME & 1u) {                 /* HOME held: the section (next, else playing) and the loop playing */
        int n = arr_next_sect, s = n >= SEC_INTRO && n <= SEC_FILL ? n : arr_sect, t = arr_loop_now;
        static const int8_t KEY_OF[SEC_FILL + 1] = {-1, 0, 1, 2, 3, 3, 4, 3};   /* SEC_* -> black key */
        if (s >= SEC_INTRO && s <= SEC_FILL)
            k |= 1u << BLACK_K[KEY_OF[s]];
        if (t >= 0 && t < 16)
            k |= 1u << WHITE_K[t];
    } else if (proj.leds)
        k = ui.keys;
    plat_glow(proj.leds == 1);
    plat_play_red(rec);
    plat_leds(b, k);
}

#define AUTOSAVE_QUIET 4000u
static void autosave(void)                       /* at rest only: a flash write pauses the audio */
{
    if (arr_playing || ui.btn || ui.keys || gm_voices || plat_ms() - ui.act_t < AUTOSAVE_QUIET || !unsaved())
        return;
    if (app.save_err && app.err_chg == app.chg)
        return;                                  /* it failed: again only after another change */
    if (save_all() == 0) {
        app.save_err = 0;
        return;
    }
    if (!app.save_err)
        say("NOT SAVED", "FLASH ERROR");         /* once, until a save works again */
    app.save_err = 1;
    app.err_chg = app.chg;
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.act_t = plat_ms();
    lcd_fill(0, 0, 240, 240, K_BG);
}

#define SONG_WAIT_MS 2000u
void ui_frame(void)
{
    input();
    watch_takes();
    if (ui.rec_want) {                           /* the engine ends a take at a section change: say so */
        if (arr_recording)
            ui.rec_seen = 1;
        else if (ui.rec_seen) {
            ui.rec_want = ui.rec_seen = 0;
            say("RECORD OFF", "section ended");
        }
    }
    ui.stopped = arr_playing ? 0 : ui.stopped < 255u ? ui.stopped + 1u : 255u;
    if (app.edit_pend && plat_ms() - app.edit_t >= EDIT_MS)
        edits_flush();
    /* a song picked while playing: now, stopped for a frame (not a PLAY the engine has yet to see) and the
     * voices ended (a save pauses the audio) */
    if (!app.song_pend || ui.stopped <= 1u)
        ui.wait_t = plat_ms();
    else if (!gm_voices || plat_ms() - ui.wait_t > SONG_WAIT_MS) {   /* (a long tail: 2 s at most) */
        int s = app.song_pend - 1;
        app.song_pend = 0;
        song_change(s);
    }
    autosave();
    draw_header();
    draw_page();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
