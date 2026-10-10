/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's input: the controls (the spec's Buttons and Keys tables), the songs and patterns into the arranger, saving,
 * the lights. Part of the unity build (after pages.c, which holds the shared state and the drawing); the host
 * simulator includes it too.
 *
 *   PLAY            start / stop (SONG: from the SONG screen's row, else row 1)
 *   REC             record into the selected track (SEQ, EDIT; its light: the engine's)
 *   SEL tap         the screen as a list (SELECT moves, ALGORITHM changes, SEL does an action, HOME goes back);
 *                   a question: SEL yes, HOME no
 *   a button held alone 1 s: its help card (letting go does nothing)
 *   HOME held       the shortcuts on screen (after HOME_HOLD_MS):
 *     + white key 1..16   loop n: MODE LOOP (from the next bar while playing); the loop playing again: FREE
 *     + black key 1..5    Intro, Main A, Main B, Fill (AB from Main A, BA from Main B), Ending, from the next bar
 *     + SELECT            the song's tempo
 *     + PRESETS           the song (while playing: after the stop)
 *   HOME: KNOB 1 the style, 2 the tempo, 3 TRANSPOSE, 4 MODE (FREE / LOOP / SONG): the song's; again: SONG
 *   ARP LOOP (x2), SEQ Pattern (x2), EDIT Track (x2), LFO Mixer (x2), FX Master FX, ENV Style (x2), GLO Setup (x2);
 *   the tag's dots: which of a button's pages
 *   the keys        by the page (key_role): chords (HOME, LOOP), the selected track (SEQ, EDIT), the track (LFO's
 *                   black keys), nothing (FX, ENV, GLO)
 *   SAVE            saves (lit: something to save); held + white key n: copy here (HOME, LOOP: the loop to Ln; SEQ:
 *                   the pattern to P(n))                                  OCT- / OCT+   the keys' octave
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
 * is picked (SAVE + a white key on SEQ keeps them in a user pattern) */
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
static void chord_rec_stop(void);
/* app.loop changes here only: another loop, its cursor at beat 0 and no chord entry carried over */
static void app_loop_set(int i)
{
    if (i == app.loop)
        return;
    app.loop = (uint8_t)(i % ARR_LOOPS);
    app.cbeat = 0;
    ui.centry = -1;
}
static void mode_set(int m)
{
    song_t *g = csong();
    if (m != app.mode)
        chord_rec_stop();                        /* writing chords was for the mode before (REC's own changes: off) */
    if (m != ARR_SONG)
        ui.song_rec = 0;                         /* (REC on SONG writes the song's rows: in SONG only) */
    if (m == ARR_SONG && m != app.mode)
        ui.row_follow = 1;                       /* SONG's cursor follows the song as it plays */
    if (arr_playing && m != ARR_FREE) {          /* LOOP / SONG start at the next bar line: until then the loop playing */
        uint32_t nb, tk;                         /* is not the one asked for (chord_played writes nothing in time) */
        do {
            nb = arr_beats;
            tk = arr_beat_tick;
        } while (nb != arr_beats || tk != arr_beat_tick);
        ui.mode_at = nb - tk / ARR_PPQ + (uint32_t)clampi(app.hdr[eslot()].beats, 1, 7);
        ui.mode_wait = 1;
    }
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
    app.cstep = 2;                               /* the chord cursor steps a bar */
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
/* every write into the loops (songs.loop[]) or a song's rows: the render reads them on beat and bar lines,
 * arr_edit(1) holds it off; done (0), the songs are unsaved */
static void songs_write(int on)
{
    arr_edit(on);
    if (!on)
        song_touched();
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

static void song_pick(int d);
static void copy_to(int n);
static void list_open(void);
static void list_sel(void);
static void ask_end(int yes);
/* ---- writing chords (the spec's REC on HOME and LOOP) into the loop playing (stopped: app.loop, the LOOP screen's).
 * Stopped: at the cursor (app.cbeat), which moves on a STEP when the chord's keys are all up; playing: on the
 * nearest beat of the loop as it plays (arr_loop_beat, the next one past half a beat, round the end), overdubbed.
 * While the chord's keys stay held, a chord growing (A C E, then G) rewrites the same beat; a key lifted and a
 * new chord: the next beat (playing: the next beat if it is the same one; stopped: the next step). A chord is stored
 * minus the transposition it plays at (xpose_now: TRANSPOSE, + the row's KEY in SONG), so it plays back as played.
 * Writing past the end makes the loop longer, a bar at a time (loop_write); past 64 beats: LOOP FULL.
 * REC on HOME in FREE is a new loop's take (free_take_*): its chords land on the nearest beat from its start. */
static char *loop_name_l(int i, char *t);
static void take_end(void);
/* the cursor's step: a beat, half a bar (at least a beat), a bar of app.loop */
static int step_beats(void)
{
    const loop_t *l = &songs.loop[app.loop % ARR_LOOPS];
    int bpb = l->beats ? l->beats : 4;
    return app.cstep == 0 ? 1 : app.cstep == 1 ? (bpb / 2 > 1 ? bpb / 2 : 1) : bpb;
}
/* the cursor d steps on. REC on: it may stand at the loop's end (a chord there makes the loop longer), and a step on
 * from the end skips: the loop grows to hold it, by bars of "no change" (LOOP FULL at the limit). REC off: round
 * the loop. Back past beat 0: the last step */
static void cursor_move(int d)
{
    loop_t *l = &songs.loop[app.loop % ARR_LOOPS];
    int len = loop_len(l), s = step_beats(), bpb = l->beats ? l->beats : 4, b = app.cbeat + d * s;
    char t[8];
    if (d < 0) {
        if (b < 0)
            b = len > 0 ? (len - 1) / s * s : 0;
    } else if (!ui.rec_chords)
        b = len > 0 ? b % len : 0;
    else if (b >= loop_max_bars(bpb) * bpb) {
        loop_name_l(app.loop % ARR_LOOPS, t);
        say("LOOP FULL", t);
        return;
    } else if (b > len && app.cbeat < len)
        b = len;                                 /* (a step past what is left: the end first) */
    else if (b > len) {                          /* from the end: a skip, the loop grows to hold it */
        songs_write(1);
        loop_set_bars(l, (b + bpb - 1) / bpb);
        songs_write(0);
    }
    app.cbeat = (uint8_t)b;
}
/* the cursor kept in app.loop: REC on, at most at its end (a chord there makes it longer); REC off, on a beat of it
 * (a ROOT / TYPE / BASS turn writes there: never past the end, growing the loop) */
static void cursor_clamp(void)
{
    int len = loop_len(&songs.loop[app.loop % ARR_LOOPS]), m = ui.rec_chords ? len : len > 0 ? len - 1 : 0;
    if (app.cbeat > m)
        app.cbeat = (uint8_t)m;
}
/* the loop the keys write into: the one playing (playing), else app.loop */
static int rec_loop(void)
{
    int ln = arr_loop_now;
    return arr_playing && ln >= 0 && ln < ARR_LOOPS ? ln : app.loop % ARR_LOOPS;
}
/* chord c (as played) on beat b of loop i: 0 written, -1 LOOP FULL (said) */
static int chord_write(int i, int b, chord_t c)
{
    int r;
    char t[8];
    songs_write(1);
    r = loop_write(&songs.loop[i], b, chord_shift(c, -xpose_now()));
    songs_write(0);
    if (r < 0) {
        loop_name_l(i, t);
        say("LOOP FULL", t);
    }
    return r;
}
/* the beat nearest now of loop l playing (an empty loop: of the pattern's bar) */
static int live_beat(const loop_t *l)
{
    uint32_t lb, tk, n = (uint32_t)loop_len(l), bpb = l->beats ? l->beats : 4u;
    do {
        lb = arr_loop_beat;
        tk = arr_beat_tick;
    } while (lb != arr_loop_beat || tk != arr_beat_tick);
    if (!n)
        return (int)((tk / ARR_PPQ + (tk % ARR_PPQ > ARR_PPQ / 2)) % bpb);
    return (int)((lb + (tk % ARR_PPQ > ARR_PPQ / 2)) % n);
}
/* the take's beat now: from its downbeat, the nearest (below 0: before it) */
static int32_t take_beat(void)
{
    uint32_t b, tk;
    do {
        b = arr_beats;
        tk = arr_beat_tick;
    } while (b != arr_beats || tk != arr_beat_tick);
    return (int32_t)(b - ui.take_b0) + (tk % ARR_PPQ > ARR_PPQ / 2);
}
/* a chord recognised on the keys (chord_keys): written when REC writes chords, or into the take */
static void chord_played(const uint8_t *n, int cnt)
{
    chord_t ch;
    int prev = ui.centry, l, b;
    if ((!ui.take && !ui.rec_chords) || !chord_recognize(n, cnt, &ch))
        return;
    l = ui.take ? ui.take_loop : rec_loop();
    if (prev >= 0 && !ui.clifted) {              /* the chord still being built: its beat again */
        chord_write(l, prev, ch);
        return;
    }
    ui.clifted = 0;
    if (ui.take) {                               /* the take: from its downbeat (waiting: it sounds, not written) */
        int32_t tb = ui.take == 2 && arr_playing ? take_beat() : -1;
        if (tb < 0)
            return;
        if (tb == prev)
            tb++;
        if (chord_write(l, (int)tb, ch) < 0) {
            take_end();                          /* (full: the take ends there) */
            return;
        }
        ui.centry = (int8_t)tb;
        ui.centry_step = 0;
        ui.take_any = 1;
        if (tb > ui.take_last)
            ui.take_last = (int16_t)tb;
        return;
    }
    if (arr_playing) {                           /* in time: the nearest beat */
        int len = loop_len(&songs.loop[l]);
        if (arr_sect == SEC_INTRO) {
            say("WRITE", "after the Intro");     /* (the loop has not started) */
            return;
        }
        if (ui.mode_wait || arr_loop_now != l) { /* the loop asked for has not started: its place is not known */
            say("WRITE", "from the bar line");
            return;
        }
        b = live_beat(&songs.loop[l]);
        if (b == prev && len > 0)
            b = (b + 1) % len;                   /* a change on the same beat: the next one */
        ui.centry = (int8_t)(chord_write(l, b, ch) == 0 ? b : -1);
        ui.centry_step = 0;
        return;
    }
    if (prev >= 0 && ui.centry_step)             /* a key lifted, a new chord with keys still held: the next step */
        cursor_move(1);
    ui.centry = (int8_t)(chord_write(l, app.cbeat, ch) == 0 ? app.cbeat : -1);
    ui.centry_step = 1;
}
/* the keys held as chords (KM_CHORD), to the chord recogniser (every change); a chord heard: written (REC); the keys
 * all up after a chord written at the cursor: it moves on a step */
static void chord_keys(void)
{
    uint8_t n[NKEYS];
    int c = 0;
    for (int i = 0; i < NKEYS; i++)
        if (ui.kmode[i] == KM_CHORD)
            n[c++] = ui.knote[i];
    if (c < ui.cheld && ui.centry >= 0)
        ui.clifted = 1;                          /* a key up: the chord written is done */
    ui.cheld = (uint8_t)c;
    if (arr_keys_held(n, c)) {
        app.played = 1;
        chord_played(n, c);
    }
    if (!c && ui.centry >= 0) {
        if (ui.centry_step)
            cursor_move(1);
        ui.centry = -1;
        ui.clifted = 0;
    }
}
/* REC writing chords on / off (HOME and LOOP in LOOP mode) */
static void loop_rec_toggle(void)
{
    char t[16];
    ui.rec_chords = (uint8_t)!ui.rec_chords;
    ui.centry = -1;
    ui.clifted = 0;
    cursor_clamp();                              /* (off: back from the end onto the loop's last beat) */
    if (!ui.rec_chords) {
        say("WRITE OFF", 0);
        return;
    }
    loop_name_l(rec_loop(), cat_s(t, "INTO "));
    say("WRITE CHORDS", t);
}
/* ---- REC on HOME in FREE: a new loop. Into the first empty loop (its beats a bar the pattern's); from the next
 * downbeat (playing) or from PLAY; REC again ends it, rounded up to whole bars (free_rec_bars), and it plays (MODE
 * LOOP): "NEW LOOP L9". The transport stopping, or the keys leaving chords (another screen), ends it too. Nothing
 * written: no loop */
static void free_take_toggle(void)
{
    char a[12], b[24];
    int i, bpb = clampi(app.hdr[eslot()].beats, 2, 7);
    if (ui.take) {
        take_end();
        return;
    }
    for (i = 0; i < ARR_LOOPS && songs.loop[i].bars; i++)
        ;
    if (i == ARR_LOOPS) {
        say("NO EMPTY LOOP", "CLEAR ONE (SEL)");
        return;
    }
    ui.take_loop = (uint8_t)i;
    ui.take_any = 0;
    ui.take_last = -1;
    ui.centry = -1;
    if (songs.loop[i].beats != bpb) {
        songs_write(1);
        loop_set_beats(&songs.loop[i], bpb);
        songs_write(0);
    }
    if (arr_playing) {                           /* from the next downbeat of the pattern playing */
        uint32_t nb, tk;
        do {
            nb = arr_beats;
            tk = arr_beat_tick;
        } while (nb != arr_beats || tk != arr_beat_tick);
        ui.take_b0 = nb - tk / ARR_PPQ + (uint32_t)bpb;
        ui.take = 2;
    } else
        ui.take = 1;                             /* from PLAY (take_poll) */
    loop_name_l(i, cat_s(a, "REC "));
    cat_s(b, arr_playing ? "from the next bar" : "from PLAY");
    say(a, b);
}
/* the take ends: its loop rounded to whole bars and said; 1 = kept (a chord written), 0 = no loop */
static int take_finish(void)
{
    loop_t *l = &songs.loop[ui.take_loop % ARR_LOOPS];
    int bpb = l->beats ? l->beats : 4, n = ui.take == 2 && arr_playing ? take_beat() : ui.take_pos;
    char t[8];
    ui.take = 0;
    ui.centry = -1;
    if (!ui.take_any) {
        say("NO CHORDS", "no new loop");
        return 0;
    }
    if (n < ui.take_last + 1)
        n = ui.take_last + 1;
    songs_write(1);
    loop_set_bars(l, free_rec_bars(n, bpb));
    songs_write(0);
    loop_name_l(ui.take_loop, t);
    say("NEW LOOP", t);
    return 1;
}
/* ... and it plays (MODE LOOP) */
static void take_end(void)
{
    if (take_finish()) {
        app_loop_set(ui.take_loop);
        mode_set(ARR_LOOP);
    }
}
/* writing chords stops: the take ends (kept, MODE left as it is), REC's writing off */
static void chord_rec_stop(void)
{
    if (ui.take)
        take_finish();
    ui.rec_chords = 0;
    ui.centry = -1;
}
/* each frame: the take waiting starts with PLAY; on, it follows the transport (stopped: it ends) and ends full */
static void take_poll(void)
{
    if (ui.take == 1 && arr_playing) {
        ui.take = 2;                             /* PLAY: beat 0 is the take's first */
        ui.take_b0 = 0;
        ui.take_pos = 0;
    } else if (ui.take == 2) {
        const loop_t *l = &songs.loop[ui.take_loop % ARR_LOOPS];
        int bpb = l->beats ? l->beats : 4;
        if (!arr_playing)
            take_end();
        else if ((ui.take_pos = take_beat()) >= loop_max_bars(bpb) * bpb)
            take_end();                          /* 64 beats: as long as a loop gets */
    }
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
    app_loop_set(i);
    mode_set(ARR_LOOP);
    loop_label(i, t);
    say(arr_playing ? "NEXT" : "LOOP", t);
}
/* HOME held + a key: a loop (white 1..16) or a section (black 1..5: Intro, Main A, Main B, Fill, Ending); the
 * other black keys do nothing */
static const uint8_t HOME_SECT[5] = {SEC_INTRO, SEC_A, SEC_B, SEC_FILL, SEC_END};
static int song_rec_loop(int w);
static void song_rec_sect(int sec);
static void home_key(int k)
{
    int w = ordinal(WHITE_K, 16, k), b = ordinal(BLACK_K, 11, k);
    if (w >= 0 && ui.song_rec && app.mode == ARR_SONG)
        song_rec_loop(w);                        /* REC on SONG: the loop into the song's row, the song plays on */
    else if (w >= 0)
        loop_pick(w);
    else if (b >= 0 && b < 5) {
        int sec = HOME_SECT[b];
        if (sec == SEC_FILL && arr_playing && (arr_sect == SEC_INTRO || arr_sect == SEC_END)) {
            say("NO FILL", arr_sect == SEC_INTRO ? "in the Intro" : "in the Ending");   /* (the engine's rule) */
            return;
        }
        arr_section(sec);
        if (ui.song_rec && app.mode == ARR_SONG && (sec == SEC_A || sec == SEC_B))
            song_rec_sect(sec);                  /* REC on SONG: the row's section too */
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
    return song_page() ? KR_PICK : KR_NONE;
}
static void song_key(int k);
static void key_down(int k)
{
    int r = key_role(), b = ordinal(BLACK_K, 11, k);
    ui.knote[k] = (uint8_t)key_note(k);
    ui.kmode[k] = KM_NONE;
    if (ui.ask && (ui.btn >> B_HOME & 1u || ui.btn >> B_SAVE & 1u)) {
        ui.btn_used |= ui.btn;                   /* a question: no shortcut, no copy (SEL yes, HOME no) */
        return;
    }
    if (ui.btn >> B_HOME & 1u) {                 /* HOME held: a shortcut (its release is no page press) */
        ui.btn_used |= 1u << B_HOME;
        home_key(k);
    } else if (ui.btn >> B_SAVE & 1u) {          /* SAVE held + a white key: copy here (no save on its release) */
        ui.btn_used |= 1u << B_SAVE;
        if (ordinal(WHITE_K, 16, k) >= 0 && !song_page())
            copy_to(ordinal(WHITE_K, 16, k));    /* (SONG: nothing to copy, nothing happens) */
    } else if (r == KR_CHORDS) {
        ui.kmode[k] = KM_CHORD;
        chord_keys();
    } else if (r == KR_TRACK) {
        ui.kmode[k] = KM_PLAY;
        arr_key(ui.knote[k], 1, 100);            /* the selected track's sound; recorded when recording */
    } else if (r == KR_PICK && song_page())
        song_key(k);                             /* SONG: white the row's loop, black its mutes */
    else if (r == KR_PICK && b >= 0 && b < ARR_TRACKS)
        set_track(b);                            /* the mixer: black key i, track i */
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
/* REC where the keys play chords (HOME, LOOP): a take running ends; in FREE on HOME a new loop's take starts; else
 * chords are written into the loop. In SONG, first MODE LOOP on the row's loop ("SONG PAUSED", MODE SONG goes back);
 * on LOOP in FREE, MODE LOOP on the loop it shows (you edit what you hear) */
static void rec_chords_button(void)
{
    char t[16];
    if (ui.take || (!ui.rec_chords && app.mode == ARR_FREE && ui.page == P_HOME)) {
        free_take_toggle();
        return;
    }
    if (!ui.rec_chords && app.mode == ARR_SONG) {
        const song_t *g = csong();
        int ln = arr_loop_now;
        app_loop_set(ln >= 0 && ln < ARR_LOOPS ? ln
                     : g->nrows ? g->row[app.row < g->nrows ? app.row : 0].loop % ARR_LOOPS : app.loop);
        mode_set(ARR_LOOP);
        loop_rec_toggle();
        loop_name_l(app.loop, cat_s(t, "EDITING "));
        say("SONG PAUSED", t);
        return;
    }
    if (!ui.rec_chords && app.mode == ARR_FREE)
        mode_set(ARR_LOOP);
    loop_rec_toggle();
}
/* REC records what the keys play on this screen (principle 2): when they stop doing that (another screen), it ends.
 * Track REC ends leaving SEQ / EDIT; writing chords (and a new loop's take, which keeps what it has) ends leaving
 * HOME / LOOP. (One rule, so REC never records into what the keys no longer play) */
static void rec_follow_keys(void)
{
    int r = key_role();
    if (r == ui.role_was)
        return;
    ui.role_was = (uint8_t)r;
    if (r != KR_TRACK && (ui.rec_want || arr_recording)) {
        arr_record(proj.trk, 0);
        ui.rec_want = ui.rec_seen = 0;
        say("RECORD OFF", 0);
    }
    if (r != KR_CHORDS) {
        if (ui.take)
            take_end();
        else if (ui.rec_chords) {
            ui.rec_chords = 0;
            ui.centry = -1;
            say("WRITE OFF", 0);
        }
    }
}

/* ---- SONG (HOME twice): the song's rows. White key n: the row at the cursor plays Ln (row_tap: after the last row a
 * row is added; the last row's loop again: its REPEAT + 1, the cursor stays), the cursor to the row after; black
 * keys 1-8: the row's mutes (D1 D2 PC BA C1 C2 C3 C4; the row playing: heard at once). Every write to the rows
 * goes through songs_write (the render reads them at each block's start). Rows are only set or added here, never
 * moved (INSERT / DELETE ROW: stopped only), so the row playing keeps its index */
static void song_tap(int w)
{
    song_t *g = csong();
    int n = g->nrows, last = n - 1, r;
    int bump = (app.row == n || app.row == last) && last >= 0 && g->row[last].loop == w;
    songs_write(1);
    r = row_tap(g, app.row, w);
    songs_write(0);
    if (r < 0) {
        say("SONG FULL", "32 ROWS");
        return;
    }
    ui.row_follow = 0;                           /* (writing: the cursor stays where the keys write) */
    if (!bump)
        app.row = (uint8_t)(r + 1 <= g->nrows ? r + 1 : g->nrows);
}
/* the row the mutes and sections REC writes go to: playing (rows started), the one playing; else the cursor's */
static int song_rec_row(void)
{
    return ui.song_rec && arr_playing && arr_mode == ARR_SONG && arr_loop_now >= 0 ? arr_row : app.row;
}
static void song_key(int k)
{
    song_t *g = csong();
    int w = ordinal(WHITE_K, 16, k), b = ordinal(BLACK_K, 11, k), r = song_rec_row(), on;
    if (w >= 0) {
        song_tap(w);
        return;
    }
    if (b < 0 || b >= ARR_TRACKS)
        return;                                  /* black keys 9-11: nothing */
    if (r >= g->nrows) {
        say("END ROW", "NO MUTES");
        return;
    }
    songs_write(1);
    g->row[r].mute ^= (uint8_t)(1u << b);
    songs_write(0);
    on = g->row[r].mute >> b & 1u;
    if (arr_playing && arr_mode == ARR_SONG && arr_loop_now >= 0 && r == arr_row)
        arr_track_mute(b, on);                   /* the row playing: heard now (its next start would anyway) */
    say(TRACK_NAME[b], on ? "muted" : "plays");
}
/* REC on SONG (with PLAY): the player's changes into the rows. HOME + white key w: the loop goes into the row playing,
 * or, its last bar playing, into the row after it (after the last row: a row added; the last row's loop again: its
 * REPEAT + 1); the song plays on (the engine reads the rows at each block: the row playing's loop is heard from its
 * next beat, the next row's at its start). No rows playing yet (an empty song): a row at the end, which starts at
 * the next bar line. Stopped: as a tap at the cursor */
static int song_rec_loop(int w)
{
    song_t *g = csong();
    int n = g->nrows, r = arr_row, at = n, res;
    char a[12], t[16];
    if (!arr_playing) {
        song_tap(w);
        return 0;
    }
    if (n && arr_loop_now >= 0 && r < n) {
        const row_t *cw = &g->row[r];
        const loop_t *l = &songs.loop[cw->loop % ARR_LOOPS];
        int per = loop_len(l) > 0 ? loop_len(l) : 4, len = per * (cw->rep ? cw->rep : 1);
        int bpb = clampi(app.hdr[eslot()].beats, 1, 7), el, left;
        uint32_t lb, tk, rp;
        do {
            lb = arr_loop_beat;
            tk = arr_beat_tick;
            rp = arr_rep;
        } while (lb != arr_loop_beat || tk != arr_beat_tick || rp != arr_rep);
        el = (int)rp * per + (int)lb;            /* beats into the row */
        left = bpb - (int)(tk / ARR_PPQ) % bpb;  /* beats to the bar line (rows end on the pattern's bar lines) */
        at = arr_sect != SEC_INTRO && el + left >= len ? r + 1 : r;
    }
    songs_write(1);
    if (at < n) {
        g->row[at].loop = (uint8_t)w;
        res = at;
    } else
        res = row_tap(g, n, w);
    songs_write(0);
    if (res < 0) {
        say("SONG FULL", "32 ROWS");
        return -1;
    }
    itoa_u((uint32_t)res + 1u, cat_s(a, "ROW "));
    loop_label(w, t);
    say(a, t);
    return res;
}
/* REC on SONG, HOME + black 2 / 3 (Main A / B): the row's section too (its "+ fill" kept) */
static void song_rec_sect(int sec)
{
    song_t *g = csong();
    int r = song_rec_row();
    if (r >= g->nrows)
        return;
    songs_write(1);
    g->row[r].sect = (uint8_t)((sec == SEC_B ? ROW_B : ROW_A) + row_fill(&g->row[r]));
    songs_write(0);
}
/* REC on SONG: on (MODE SONG first: stopped, from the cursor's row at PLAY) / off */
static void song_rec_toggle(void)
{
    const song_t *g = csong();
    if (ui.song_rec) {
        ui.song_rec = 0;
        say("REC OFF", 0);
        return;
    }
    if (app.mode != ARR_SONG) {
        mode_set(ARR_SONG);
        if (arr_playing)                         /* (from the next bar line) */
            arr_mode_song(g, app.row < g->nrows ? app.row : 0);
    }
    ui.song_rec = 1;
    say("REC ROWS", arr_playing ? "HOME + keys" : "PLAY, HOME + keys");
}
/* SONG's knobs: ROW (the END row after the last), LOOP, SECTION, REPEAT; ROW, KEY; INTRO, ENDING, AT END, SONG */
static void song_knob(int k, int32_t e)
{
    song_t *g = csong();
    row_t *w = app.row < g->nrows ? &g->row[app.row] : 0;
    int d = e > 0 ? 1 : -1, v = 0, to = 0;
    uint8_t *f = 0;
    if (ui.page == P_SONG3) {
        if (k == 3) {
            song_pick(d);
            return;
        }
        f = k == 0 ? &g->intro : k == 1 ? &g->ending : &g->at_end;
        if (*f == (uint8_t)(d > 0))
            return;
        songs_write(1);
        *f = (uint8_t)(d > 0);
        songs_write(0);
        return;
    }
    if (k == 0) {                                /* ROW: the cursor (it stops following the song) */
        app.row = (uint8_t)clampi(app.row + accel(EN_K1, e), 0, g->nrows);
        ui.row_follow = 0;
        return;
    }
    if (!w)
        return;                                  /* the END row: nothing to turn */
    if (ui.page == P_SONG2) {
        if (k != 1)
            return;
        v = w->key;
        to = clampi(v + (int)e, -6, 5);   /* (12 steps: no acceleration) */
    } else if (k == 1) {
        v = w->loop;
        to = clampi(v + accel(EN_K2, e), 0, ARR_LOOPS - 1);
    } else if (k == 2) {
        v = w->sect;
        to = clampi(v + (int)e, 0, ROW_NSECT - 1);
    } else {
        v = w->rep;
        to = clampi(v + (int)e, 1, 8);
    }
    if (to == v)
        return;
    songs_write(1);
    if (ui.page == P_SONG2)
        w->key = (int8_t)to;
    else if (k == 1)
        w->loop = (uint8_t)to;
    else if (k == 2)
        w->sect = (uint8_t)to;
    else
        w->rep = (uint8_t)to;
    songs_write(0);
}

/* the page buttons (PAGE_BTN, pages.c): pressed again, the next page; SELECT turns them both ways */
static int page_button(int b)
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (PAGE_BTN[i].btn == b) {
            int f = PAGE_BTN[i].first, n = PAGE_BTN[i].n;
            if (b == B_HOME)                     /* HOME -> SONG (its pages one stop) -> HOME */
                ui.page = (uint8_t)(ui.page == P_HOME ? P_SONG : P_HOME);
            else
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
}

static void slot_style(int s, int to);
static void pat_name(int st, char *t)            /* "P05" */
{
    t[0] = 'P';
    t[1] = (char)('0' + (st + 1) / 10);
    t[2] = (char)('0' + (st + 1) % 10);
    t[3] = 0;
}

static void song_rec_toggle(void);
static void button_down(int b)                   /* PLAY, REC: at once */
{
    switch (b) {
    case B_PLAY:
        if (!arr_playing && song_page()) {       /* on SONG: the song, from the cursor's row */
            const song_t *g = csong();
            int r = app.row < g->nrows ? app.row : 0;
            if (app.mode != ARR_SONG)
                mode_set(ARR_SONG);
            arr_mode_song(g, r);
            ui.row_follow = 1;
        } else if (!arr_playing && app.mode == ARR_SONG) {   /* else SONG from row 1 */
            arr_mode_song(csong(), 0);
            ui.row_follow = 1;
        }
        if (ui.msg_first)                        /* the section asked for first: now playing */
            ui.msg_until = plat_ms();
        ui.msg_first = 0;
        arr_play(!arr_playing);
        break;
    case B_REC:
    {
        int on = !arr_recording, s = eslot(), r = key_role();   /* the state asked for (the engine's: a block later) */
        int st = songs.song[proj.song].style[s];
        char a[16], t[16];
        if (r == KR_CHORDS) {                    /* HOME, LOOP: chords into the loop, or a new loop */
            rec_chords_button();
            break;
        }
        if (song_page()) {                       /* SONG: the rows, from HOME + keys and the mutes */
            song_rec_toggle();
            break;
        }
        if (on && r != KR_TRACK) {               /* REC records what the keys play: a track on SEQ, EDIT */
            say("REC", "not here");
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
static void button_up(int b)                     /* a tap: released before it was used (a key, a knob, its card) */
{
    if (lst.open && b == PAGE_BTN[page_group(ui.page)].btn && b != B_HOME) {
        ui.ask = 0;                              /* the list's own button: the list goes, the page stays */
        lst.open = 0;
        return;
    }
    if (b != B_HOME && b != B_SEL && b != B_SAVE && page_button(b)) {   /* another page: the list, a question go */
        ui.ask = 0;
        lst.open = 0;
        return;
    }
    switch (b) {
    case B_SEL:                                  /* a question: yes; a list: its row; else the list */
        if (ui.ask)
            ask_end(1);
        else if (lst.open)
            list_sel();
        else
            list_open();
        break;
    case B_HOME:                                 /* HOME on release: no; back from a list; else the page */
        if (ui.ask)
            ask_end(0);
        else if (lst.open)
            lst.open = 0;
        else
            page_button(B_HOME);
        break;
    case B_SAVE: {
        uint32_t m = ui.msg_until;
        int rc = save_all();
        if (rc != 0)
            say("NOT SAVED", "FLASH ERROR");
        else if (ui.msg_until == m)              /* (else what the save said stays: a shared pattern) */
            say("SAVED", 0);
        break;
    }
    case B_OCTDN: case B_OCTUP:
        if (ui.page == P_LOOP || ui.page == P_LOOP2) {   /* the cursor: OCT+ skips a step on, OCT- steps back */
            cursor_move(b == B_OCTUP ? 1 : -1);
            break;
        }
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
#define CARD_MS 1000u
static void holds(void)
{
    uint32_t now = plat_ms();
    for (int b = 0; b < NB; b++)                 /* a button held alone, unused: its card (its release does nothing) */
        if (ui.btn == 1u << b && !ui.keys && b != B_HOME && !(ui.btn_used >> b & 1u) && now - ui.btn_t[b] >= CARD_MS) {
            ui.card = (uint8_t)(b + 1);
            ui.btn_used |= 1u << b;
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
 * section on a user pattern (recorded into, edited) keeps it (the lists' BACK TO STYLE puts it back). Never a
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
    case P_SONG: case P_SONG2: case P_SONG3:
        song_knob(k, e);
        break;
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
    case P_LOOP: {                               /* ROOT, TYPE ("·" one below maj: loop_unwrite), BASS, STEP */
        int l = app.loop % ARR_LOOPS, has, b = app.cbeat;
        chord_t c = chord_shift(cursor_chord(&songs.loop[l], b, &has), xpose_now());
        if (k == 3) {                             /* STEP: beat, half, bar (no write) */
            app.cstep = (uint8_t)(((int)app.cstep + d) % 3 + 3) % 3;
            break;
        }
        if (k == 0)
            c.root = (uint8_t)((c.root + 12 + d) % 12);
        else if (k == 1) {
            int t = (has ? (int)c.type : -1) + d;
            if (t < -1)
                t = CH_DIM;
            else if (t > CH_DIM)
                t = -1;
            if (t < 0) {
                songs_write(1);
                loop_unwrite(&songs.loop[l], b);
                songs_write(0);
                break;
            }
            c.type = (uint8_t)t;
        } else {
            int cur = c.bass == CH_NOBASS ? -1 : (int)c.bass;
            cur += d;
            if (cur < -1)
                cur = 11;
            else if (cur > 11)
                cur = -1;
            c.bass = cur < 0 ? CH_NOBASS : (uint8_t)cur;
        }
        chord_write(l, b, c);
        break;
    }
    case P_LOOP2: {                              /* LENGTH, BEATS ("LENGTH 9: 64 BEATS MAX" when it shrank), NAME */
        loop_t *l = &songs.loop[app.loop % ARR_LOOPS];
        int bpb = l->beats ? l->beats : 4, step = accel(EN_K1 + k, e);
        if (k == 0) {
            int m = loop_max_bars(bpb), to = clampi((l->bars ? l->bars : 1) + step, 1, m);
            if (to == l->bars)
                break;
            songs_write(1);
            loop_set_bars(l, to);
            songs_write(0);
        } else if (k == 1) {
            int to = clampi(bpb + step, 2, 7), before = l->bars;
            if (to == bpb)
                break;
            songs_write(1);
            loop_set_beats(l, to);
            songs_write(0);
            if (l->bars < before) {
                char a[16];
                itoa_u((uint32_t)l->bars, cat_s(a, "LENGTH "));
                cat_s(a, ":");
                say(a, "64 BEATS MAX");
            }
        } else if (k == 2) {
            int idx = ((loop_name_index(l) + step) % (NLOOP_NAMES + 1) + NLOOP_NAMES + 1) % (NLOOP_NAMES + 1);
            songs_write(1);
            loop_set_name_preset(l, app.loop % ARR_LOOPS, idx);
            songs_write(0);
        }
        cursor_clamp();                          /* shorter: the cursor back in (a ROOT turn past it would grow it) */
        break;
    }
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
/* ------------------------------------------------------- the actions --- */
/* a question in the header (an action that loses notes or chords): SEL yes (yes() runs, the list closes), HOME no */
static void ask(const char *q, void (*yes)(void))
{
    uint32_t i;
    for (i = 0; i < sizeof ui.ask_q - 1u && q[i]; i++)
        ui.ask_q[i] = q[i];
    ui.ask_q[i] = 0;
    ui.ask = 1;
    ui.ask_yes = yes;
}
static void ask_end(int yes)
{
    void (*f)(void) = ui.ask_yes;
    ui.ask = 0;
    ui.ask_yes = 0;
    if (yes) {
        lst.open = 0;
        if (f)
            f();
    }
}
static char *loop_name_l(int i, char *t) { return itoa_u((uint32_t)i + 1u, cat_s(t, "L")); }   /* "L3" */
/* "WORDS L3?" */
static void ask_loop(const char *what, int i, void (*yes)(void))
{
    char q[32];
    cat_s(loop_name_l(i, cat_s(q, what)), "?");
    ask(q, yes);
}

/* LOAD PROGRESSION: one of the 16 factory progressions into the loop (its name too) */
static void do_load_prog(void)
{
    int t = ui.tgt.prog % NTEMPLATES, l = ui.tgt.loop % ARR_LOOPS;
    char a[16], b[16];
    songs_write(1);
    loop_from_template(&songs.loop[l], t);
    songs_write(0);
    loop_name_l(l, cat_s(b, "INTO "));
    cat_s(a, TEMPLATES[t].name);
    say(upcase(a), b);
}
static void act_load_progression(void)
{
    char q[32];
    if (!songs.loop[ui.tgt.loop].bars) {
        do_load_prog();
        return;
    }
    cat_s(cat_s(cat_s(q, "LOAD "), TEMPLATES[ui.tgt.prog % NTEMPLATES].name), " INTO ");
    upcase(q);
    ask_loop(q, ui.tgt.loop, do_load_prog);
}
static void do_clear_loop(void)
{
    char t[8];
    int l = ui.tgt.loop % ARR_LOOPS;
    songs_write(1);
    loop_clear(&songs.loop[l], l);
    songs_write(0);
    loop_name_l(l, t);
    say("CLEARED", t);
}
static void act_clear_loop(void)
{
    char t[8];
    if (!songs.loop[ui.tgt.loop].bars) {
        loop_name_l(ui.tgt.loop, t);
        say(t, "IS EMPTY");
    } else
        ask_loop("CLEAR ", ui.tgt.loop, do_clear_loop);
}
/* INSERT ROW, DELETE ROW: at the SONG screen's row; stopped only (the rows after the one playing would move) */
static void act_row(int ins)
{
    song_t *g = csong();
    char t[12];
    if (arr_playing) {
        say(ins ? "INSERT ROW" : "DELETE ROW", "STOP FIRST");
        return;
    }
    if (ins && g->nrows >= ARR_ROWS) {
        say("SONG FULL", "32 ROWS");
        return;
    }
    if (!ins && app.row >= g->nrows) {
        say("DELETE ROW", "NO ROW HERE");
        return;
    }
    itoa_u(app.row + 1u, cat_s(t, "ROW "));
    songs_write(1);
    if (ins)
        row_insert(g, app.row > g->nrows ? g->nrows : app.row);
    else
        row_delete(g, app.row);
    songs_write(0);
    if (app.row >= ARR_ROWS || (!ins && app.row && app.row >= g->nrows))
        app.row = (uint8_t)(g->nrows ? g->nrows - 1 : 0);
    say(ins ? "INSERTED" : "DELETED", t);
}
/* CLEAR SONG: its rows and settings (its patterns, style, tempo, transpose and mutes kept, the loops too) */
static void do_clear_song(void)
{
    song_t *g = csong();
    uint8_t mute = g->mute, tempo = g->tempo;
    int8_t xp = g->transpose;
    songs_write(1);                              /* (the engine may be playing its rows) */
    loops_song_clear(g);                         /* (its style kept) */
    g->mute = mute;
    g->tempo = tempo;
    g->transpose = xp;
    songs_write(0);
    app.row = 0;
    say("CLEARED", "the song");
}
static void act_clear_song(void)
{
    char q[16];
    if (!csong()->nrows) {
        do_clear_song();
        return;
    }
    cat_s(itoa_u(proj.song + 1u, cat_s(q, "CLEAR SONG ")), "?");
    ask(q, do_clear_song);
}
/* COPY SONG TO: the song (its rows, settings, styles) into another; its user patterns into ones of the target's */
static void do_copy_song(void)
{
    song_t *g = csong();
    int to = ui.tgt.song % ARR_SONGS;
    song_t *d = &songs.song[to];
    uint16_t st[ARR_NSECT];
    char a[16];
    if (arr_playing) {                           /* (PLAY during the question) a flash write pauses the audio */
        say("COPY", "STOP FIRST");
        return;
    }
    edits_flush();
    if (copy_patterns_pick(to, st) != 0) {
        say("NOT COPIED", "NO FREE PATTERN");
        return;
    }
    for (int i = 0; i < ARR_NSECT; i++) {        /* the patterns' contents, as they play now; the song after */
        if (st[i] >= ARR_USER_PATTERNS)
            continue;
        arr_pattern_copy(i, &scratch);
        if (project_pattern_save(st[i], &scratch) != 0) {
            say("FLASH ERROR", "song unchanged");
            return;
        }
    }
    songs_write(1);                              /* (not the song playing: no need, but every row write goes so) */
    *d = *g;
    memcpy(d->style, st, sizeof st);
    songs_write(0);
    itoa_u((uint32_t)to + 1u, cat_s(itoa_u(proj.song + 1u, cat_s(a, "SONG ")), " > "));
    say(a, "copied");
}
static void act_copy_song(void)
{
    int to = ui.tgt.song % ARR_SONGS;
    char q[20];
    if (to == proj.song) {
        say("COPY", "SAME SONG");
        return;
    }
    if (arr_playing) {                           /* it writes patterns: a flash write pauses the audio */
        say("COPY", "STOP FIRST");
        return;
    }
    if (!songs.song[to].nrows) {
        do_copy_song();
        return;
    }
    cat_s(itoa_u((uint32_t)to + 1u, cat_s(q, "REPLACE SONG ")), "?");
    ask(q, do_copy_song);
}
/* ERASE TRACK: the selected track's notes in the pattern playing (a style's section: a user pattern first) */
static void do_erase_track(void)
{
    int t = ui.tgt.trk & 7;
    if (eslot() != ui.tgt.slot) {                /* the engine erases in the section playing: another now */
        say("NOT ERASED", "SECTION CHANGED");
        return;
    }
    edits_flush();
    slot_own(ui.tgt.slot);
    arr_erase_track(t);
    say("ERASED", TRACK_NAME[t]);
}
static void act_erase_track(void)
{
    char q[16];
    int t = ui.tgt.trk & 7;
    arr_pattern_copy(ui.tgt.slot, &scratch);
    if (!scratch.trk[t].count) {
        say(TRACK_NAME[t], "HAS NO NOTES");
        return;
    }
    cat_s(cat_s(cat_s(q, "ERASE "), TRACK_NAME[t]), "?");
    ask(q, do_erase_track);
}
/* CLEAR PATTERN: every note gone, its sounds kept */
static void do_clear_pattern(void)
{
    int s = ui.tgt.slot % ARR_NSECT;
    edits_flush();
    slot_own(s);
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
}
static void act_clear_pattern(void)
{
    char q[24];
    int s = ui.tgt.slot % ARR_NSECT;
    arr_pattern_copy(s, &scratch);
    if (!scratch.nev) {
        say(SECT_NAME[s + 1], "HAS NO NOTES");
        return;
    }
    cat_s(cat_s(cat_s(q, "CLEAR "), SECT_NAME[s + 1]), "?");
    upcase(q);
    ask(q, do_clear_pattern);
}
/* BACK TO STYLE: the section plays the song's style again (its user pattern kept, saved first) */
static void act_back_to_style(void)
{
    int s = ui.tgt.slot % ARR_NSECT, st = songs.song[proj.song].style[s], i = style_index(songs.song[proj.song].sty);
    edits_flush();
    if (st >= ARR_USER_PATTERNS)
        say(SECT_NAME[s + 1], "plays the style");
    else if (i >= 0) {
        slot_style(s, style_number(i, s));       /* (saved first; refused while playing with changes unsaved) */
        if (songs.song[proj.song].style[s] != st) {
            char b[24];
            style_name(songs.song[proj.song].style[s], b);
            say("BACK TO", b);
        }
    }
}
static void action_run(int a)
{
    ui.tgt.loop = app.loop;                      /* what it acts on: now (a question's yes: still this) */
    ui.tgt.prog = ui.pick_prog;
    ui.tgt.song = ui.pick_song;
    ui.tgt.trk = proj.trk;
    ui.tgt.slot = (uint8_t)eslot();
    switch (a) {
    case A_LOAD_PROG: act_load_progression(); break;
    case A_CLEAR_LOOP: act_clear_loop(); break;
    case A_INSERT_ROW: act_row(1); break;
    case A_DELETE_ROW: act_row(0); break;
    case A_CLEAR_SONG: act_clear_song(); break;
    case A_COPY_SONG: act_copy_song(); break;
    case A_ERASE_TRACK: act_erase_track(); break;
    case A_CLEAR_PATTERN: act_clear_pattern(); break;
    case A_BACK_TO_STYLE: act_back_to_style(); break;
    default: break;
    }
    if (!ui.ask)                                 /* done (or said why not): back to the page */
        lst.open = 0;
}

/* SAVE + white key n (principle 5): HOME, LOOP: the loop playing into Ln; SEQ: the pattern into P(n). Asks first
 * when the target holds chords / notes */
static void do_copy_loop(void)
{
    char a[8], b[20];
    songs_write(1);
    songs.loop[ui.copy_to % ARR_LOOPS] = songs.loop[ui.copy_src % ARR_LOOPS];
    songs_write(0);
    loop_name_l(ui.copy_src, a);
    loop_name_l(ui.copy_to, cat_s(b, "COPIED TO "));
    say(a, b);
}
static void do_copy_pattern(void)
{
    int s = ui.tgt.slot % ARR_NSECT, st = songs.song[proj.song].style[s], to = ui.copy_to % ARR_USER_PATTERNS;
    uint16_t d;
    char a[24], t[20];
    if (arr_playing) {                           /* (PLAY during the question) a flash write pauses the audio */
        say("COPY", "STOP FIRST");
        return;
    }
    pat_name(to, cat_s(t, "COPIED TO "));
    edits_flush();
    for (int j = 0; j < ARR_NSECT; j++)          /* another section plays it, changed: those changes first */
        if (j != s && songs.song[proj.song].style[j] == to && slot_unsaved(j)) {
            pat_name(to, a);
            cat_s(a + 3, " UNSAVED IN");
            say(a, SECT_NAME[j + 1]);
            return;
        }
    d = arr_pattern_copy(s, &scratch);
    if (project_pattern_save(to, &scratch) != 0) {
        say("NOT COPIED", "FLASH ERROR");
        return;
    }
    if (to == st) {                              /* into its own: saved */
        app.saved[s] = d;
        app.edited[s] = 0;
    }
    for (int j = 0; j < ARR_NSECT; j++)          /* the other slots playing that pattern get the copy */
        if (j != s && songs.song[proj.song].style[j] == to) {
            arr_load_pattern(j, &scratch);
            slot_cache(j, &scratch);
            app.saved[j] = arr_dirty[j];
        }
    style_name(st, a);
    say(upcase(a), t);
}
static void copy_to(int n)
{
    char q[16];
    ui.copy_to = (uint8_t)n;
    ui.tgt.slot = (uint8_t)eslot();
    if (list_loops()) {
        /* the loop playing: in SONG the row's; else app.loop, the loop the LOOP screen shows (in FREE too: the
         * one last picked) */
        int src = app.mode == ARR_SONG && arr_loop_now >= 0 && arr_loop_now < ARR_LOOPS ? arr_loop_now : app.loop;
        ui.copy_src = (uint8_t)src;
        if (n == src) {
            loop_name_l(n, q);
            say(q, "IS THIS LOOP");
        } else if (songs.loop[n].bars)
            ask_loop("REPLACE ", n, do_copy_loop);
        else
            do_copy_loop();
    } else if (ui.page == P_PATTERN || ui.page == P_PATSET) {
        if (arr_playing) {                       /* a flash write pauses the audio */
            say("COPY", "STOP FIRST");
            return;
        }
        if (n != songs.song[proj.song].style[ui.tgt.slot] && !pattern_blank(n)) {
            pat_name(n, cat_s(q, "REPLACE "));
            cat_s(q + 11, "?");
            ask(q, do_copy_pattern);
        } else
            do_copy_pattern();
    } else
        say("COPY", "ON HOME, LOOP, SEQ");
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
        int t = trk & 7, on = !(arr_mutes >> t & 1u);
        song_t *g = csong();
        arr_track_mute(t, on);
        if (ui.song_rec && arr_playing && arr_loop_now >= 0 && arr_row < g->nrows) {   /* REC: into the row playing */
            songs_write(1);
            g->row[arr_row].mute = (uint8_t)((g->row[arr_row].mute & ~(1u << t)) | (uint32_t)on << t);
            songs_write(0);
        }
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
    } else if (song_page()) {                    /* the next row (the END row after the last) */
        app.row = (uint8_t)clampi(app.row + d, 0, csong()->nrows);
        ui.row_follow = 0;
        touch(0);
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

/* ------------------------------------------------------------- lists --- */
static void list_open(void)
{
    lst.open = 1;
    lst.sel = lst.top = 0;
    list_build();
    if (!lst.n) {
        lst.open = 0;
        say("LIST", "NOTHING HERE");
    }
}
/* SELECT in a list: d rows on (over the MORE heading), the rows shown following */
static void list_move(int d)
{
    int s = lst.sel;
    list_build();
    while (d) {
        int step = d > 0 ? 1 : -1, t = s + step;
        while (t >= 0 && t < lst.n && lst.row[t].kind == LK_MORE)
            t += step;
        if (t < 0 || t >= lst.n)
            break;
        s = t;
        d -= step;
    }
    lst.sel = (uint8_t)s;
    if (lst.sel < lst.top)
        lst.top = lst.sel;
    if (lst.sel == lst.top && lst.top && lst.row[lst.top - 1].kind == LK_MORE)
        lst.top--;                               /* the heading over its row */
    if (lst.sel >= lst.top + LIST_SHOWN)
        lst.top = (uint8_t)(lst.sel - LIST_SHOWN + 1);
}
/* ALGORITHM on a NAME row: the letter at the cursor (a loop's: through songs_write) */
static void name_turn(int32_t e)
{
    char *nm = list_name();
    int loop = ui.page == P_LOOP || ui.page == P_LOOP2, c = ui.name_cur & 7, i = 0;
    const char *set = loop ? LOOP_CHARS : NAME_CHARS;
    int n = loop ? NLOOP_CHARS : NNAME_CHARS;
    if (!nm)
        return;
    while (i < n && set[i] != (nm[c] ? nm[c] : ' '))
        i++;
    i = ((i % n + e) % n + n) % n;
    if (loop)
        songs_write(1);
    for (int j = 0; j < c; j++)                  /* (no end before the letter) */
        if (!nm[j])
            nm[j] = ' ';
    nm[c] = set[i];
    if (loop)
        songs_write(0);
    else
        edited(pslot());
}
/* ALGORITHM in a list: the row's value (a knob's turn, on its page), the letter, the choice */
static void list_change(int32_t e)
{
    int d = e > 0 ? 1 : -1;
    list_build();
    if (!lst.n)
        return;
    switch (lst.row[lst.sel].kind) {
    case LK_KNOB: {
        uint8_t keep = ui.page;
        ui.page = (uint8_t)(lst.row[lst.sel].id >> 2);
        knob(lst.row[lst.sel].id & 3, e);
        ui.page = keep;
        ui.touched = -1;
        break;
    }
    case LK_NAME:
        name_turn(e);
        break;
    case LK_PICK:
        if (lst.row[lst.sel].id == A_LOAD_PROG)
            ui.pick_prog = (uint8_t)(((ui.pick_prog + e) % NTEMPLATES + NTEMPLATES) % NTEMPLATES);
        else {                                   /* COPY SONG TO: another song, a step a detent */
            int to = ui.pick_song;
            for (int32_t i = e < 0 ? -e : e; i > 0; i--)
                do
                    to = (to + d + ARR_SONGS) % ARR_SONGS;
                while (to == proj.song);
            ui.pick_song = (uint8_t)to;
        }
        break;
    default: break;
    }
}
/* SEL in a list: an action runs; on NAME, the next letter */
static void list_sel(void)
{
    list_build();
    if (!lst.n)
        return;
    if (lst.row[lst.sel].kind == LK_NAME)
        ui.name_cur = (uint8_t)((ui.name_cur + 1) & 7);
    else if (lst.row[lst.sel].kind == LK_ACTION || lst.row[lst.sel].kind == LK_PICK) {
        action_run(lst.row[lst.sel].id);
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
    int32_t e, ev[NE];
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        int hold = i != B_PLAY && i != B_REC;    /* (on release: unless a key, a knob, a card used it) */
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.any_btn = 1;
            ui.card = 0;                         /* another button: the card goes, the ones held are used */
            ui.btn_used |= btn & ~m;
            ui.btn_used &= ~m;
            ui.btn_t[i] = plat_ms();
            if (!hold)
                button_down((int)i);
        } else {
            if (ui.card == i + 1u)
                ui.card = 0;                     /* its card: gone, nothing else */
            if (hold && !(ui.btn_used & m))
                button_up((int)i);
        }
    }
    if ((btn & (1u << B_OCTDN | 1u << B_OCTUP)) == (1u << B_OCTDN | 1u << B_OCTUP))
        ui.btn_used |= 1u << B_OCTDN | 1u << B_OCTUP;   /* both: update mode (main loop), not ours */
    holds();
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS; i++)
        if (ch >> i & 1u) {
            if (keys >> i & 1u) {
                ui.card = 0;                     /* a key with a button held: the combination, no card */
                ui.btn_used |= btn;
                key_down((int)i);
            } else
                key_up((int)i);
        }
    midi_in();
    for (i = 0; i < NE; i++)
        if ((ev[i] = plat_enc((int)i)) != 0) {   /* a knob with a button held: the combination, no card */
            ui.card = 0;
            ui.btn_used |= btn;
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    if (ui.ask)                                  /* a question: SEL yes, HOME no; the knobs change nothing */
        return;
    if ((e = ev[EN_SELECT]) != 0) {
        if (ui.btn >> B_HOME & 1u)               /* HOME held: the tempo */
            set_tempo(csong()->tempo + accel(EN_SELECT, e));
        else if (lst.open)
            list_move(e);                        /* a list: up and down */
        else
            page_turn(e > 0 ? 1 : -1);
    }
    if ((e = ev[EN_ALGO]) != 0) {
        if (lst.open)
            list_change(e);                      /* a list: the value */
        else
            set_track(proj.trk + (e > 0 ? 1 : -1));
    }
    if ((e = ev[EN_PRESET]) != 0)
        presets(e);
    for (i = 0; i < 4; i++)
        if ((e = ev[EN_K1 + i]) != 0)
            knob((int)i, e);                     /* (HOME held: used, its release turns no page) */
}

/* ------------------------------------------------------------- lights --- */
static void leds(void)
{
    uint32_t b = 0, k = 0, i;
    int rec = arr_recording || ui.rec_chords || ui.take || ui.song_rec;
    uint32_t dim = 0;
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
    } else {
        const song_t *g = csong();
        if (proj.leds)
            k = ui.keys;
        if (song_page() && app.row < g->nrows)   /* SONG: the row's muted tracks' black keys glow dim */
            for (i = 0; i < ARR_TRACKS; i++)
                if (g->row[app.row].mute >> i & 1u)
                    dim |= 1u << BLACK_K[i];
    }
    plat_glow(proj.leds == 1);
    plat_keys_dim(dim & ~k);
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
    ui.centry = -1;
    ui.loop_seen = -1;
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
    if (!arr_playing || (int32_t)(arr_beats - ui.mode_at) >= 0)
        ui.mode_wait = 0;                        /* the bar line passed (or stopped: PLAY starts it) */
    rec_follow_keys();
    if (ui.song_rec && !song_page()) {           /* REC on SONG writes from SONG: leaving it ends it */
        ui.song_rec = 0;
        say("REC OFF", 0);
    }
    if (arr_playing && ui.row_follow && app.mode == ARR_SONG && arr_mode == ARR_SONG && arr_loop_now >= 0 &&
        arr_row < csong()->nrows)
        app.row = arr_row;                       /* SONG's cursor follows the row playing (until ROW turns) */
    take_poll();
    {   /* app.loop follows the loop playing as it changes (a loop or a song's row): LOOP shows what you hear; a loop
         * picked and waiting for the bar line stays picked (no change seen yet) */
        int ln = arr_loop_now;
        if (ln != ui.loop_seen) {
            ui.loop_seen = (int8_t)ln;
            if (ln >= 0 && ln < ARR_LOOPS && app.mode != ARR_FREE)
                app_loop_set(ln);
        }
        if (arr_playing && ln >= 0)
            app.played = 1;                      /* a loop or a song gave the chord */
    }
    if (app.edit_pend && plat_ms() - app.edit_t >= EDIT_MS)
        edits_flush();
    /* a song picked while playing: now, stopped for a frame (not a PLAY the engine has yet to see) and the
     * voices ended (a save pauses the audio) */
    if (!app.song_pend || ui.stopped <= 1u)
        ui.wait_t = plat_ms();
    else if (!gm_voices || plat_ms() - ui.wait_t > SONG_WAIT_MS) {   /* (a long tail: 2 s at most) */
        int s = app.song_pend - 1;
        app.song_pend = 0;
        ui.ask = 0;                              /* (a question was about the song before) */
        song_change(s);
    }
    autosave();
    draw_header();
    draw_page();
    draw_tag();
    draw_knobs();
    draw_footer();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
