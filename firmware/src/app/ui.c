/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's input: the controls (the spec's Controls table), the songs and patterns into the arranger, saving,
 * the lights. Part of the unity build (after pages.c, which holds the shared state and the drawing); the host
 * simulator includes it too.
 *
 *   PLAY            start / stop                 REC       record into the selected track (its light: the engine's)
 *   SEL tap         CHORD on / off: the keys enter chords, or play the selected track
 *   SEL + black key 1..4   Main A, Main B, Fill, Ending, from the next bar
 *   SEL + white key 1..16  a chord template, at once
 *   HOME Song, SEQ Pattern (x2), EDIT Track, FX Mixer (x2), LFO Chords, ENV Arrange, ARP Templates (x2), GLO Setup (x2)
 *   SAVE            saves (held: the jobs)       OCT- / OCT+   the keys' octave
 *   SELECT          tempo       ALGORITHM   the track       PRESETS   per page (Song: the song)
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
static int eslot(void) { return (arr_sect - 1) & 3; }
static int slot_unsaved(int s) { return arr_dirty[s] != app.saved[s] || app.edited[s] || (app.edit_pend >> s & 1u); }

/* edits: into the app's copy at once (the screen), into the arranger at most every EDIT_MS (a store re-sends
 * the pattern's sounds): copy the slot, put the app's settings in, store; refused when it was recorded into
 * meanwhile: again */
#define EDIT_MS 50u
static void edited(int s)
{
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

/* slot s (Main A, Main B, Fill, Ending) gets pattern `style`: a user pattern (0..15), or (factory styles: a
 * later task) an empty one */
static void slot_load(int s, int style)
{
    if (style >= ARR_USER_PATTERNS || project_pattern_load(style, &scratch) != 0)
        pattern_empty(&scratch);
    arr_load_pattern(s, &scratch);
    slot_cache(s, &scratch);
    app.saved[s] = arr_dirty[s];
    app.edited[s] = 0;
    app.edit_pend &= (uint8_t)~(1u << s);
}
/* the selected song: its four patterns into the slots, and its tracks when it has any (else free play: the
 * templates and the keys' chords) */
static void song_load(void)
{
    const song_t *sg = &songs.song[proj.song];
    for (int s = 0; s < ARR_NSECT; s++)
        slot_load(s, sg->style[s]);
    app.song_on = sg->bars > 0;
    arr_song(app.song_on ? sg : 0, 1);
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
    memset(&app, 0, sizeof app);
    app.tmpl = -1;
    project_songs_load(&songs);
    arr_set_tempo(proj.tempo);
    arr_set_key(proj.key);
    arr_set_quant(proj.quant);
    arr_midi_out_on(1);                          /* the tracks out on their channels */
    fx_apply();
    song_load();
    arr_record(proj.trk, 0);                     /* the keys play the selected track */
}

/* ------------------------------------------------------------ controls --- */
static void touch(int k)
{
    ui.touched = (int8_t)k;
    ui.touch_until = plat_ms() + 1200u;
}
static void set_tempo(int bpm)
{
    bpm = bpm < 40 ? 40 : bpm > 240 ? 240 : bpm;
    if (bpm == proj.tempo)
        return;
    proj.tempo = (uint8_t)bpm;
    arr_set_tempo(bpm);
    mark_dirty();
}
static void pick_template(int t)
{
    if (t < 0 || t >= NTEMPLATES)
        return;
    arr_template(t);
    app.tmpl = (int8_t)t;
    app.song_on = 0;                             /* a template turns the song off */
    say("TEMPLATE", TEMPLATES[t].name);
}
static void set_track(int t)
{
    proj.trk = (uint8_t)(t & 7);
    arr_record(proj.trk, arr_recording);         /* recording goes on, into this track */
    {
        uint32_t s = (uint32_t)(arr_sect - 1) & 3u;
        say(TRACK_NAME[proj.trk], trk_sound(&app.trk[s][proj.trk]));
    }
    mark_dirty();
}

/* the keys held in CHORD mode, to the chord recogniser (every change) */
static void chord_keys(void)
{
    uint8_t n[NKEYS];
    int c = 0;
    for (int i = 0; i < NKEYS; i++)
        if (ui.kmode[i] == KM_CHORD)
            n[c++] = ui.knote[i];
    arr_keys_held(n, c);
}
static void set_chord_mode(int on)
{
    for (int i = 0; i < NKEYS; i++) {
        if (ui.kmode[i] == KM_PLAY)
            arr_key(ui.knote[i], 0, 0);          /* the notes the keys played end */
        ui.kmode[i] = KM_NONE;
        if (on && (ui.keys >> i & 1u))
            ui.kmode[i] = KM_CHORD;              /* keys held now count as pressed anew */
    }
    ui.chord = (uint8_t)on;
    if (on) {
        arr_keys_held(0, 0);
        chord_keys();
    }
}
/* SEL held + a key: a section (black 1..4) or a template (white 1..16) */
static void sel_key(int k)
{
    int w = ordinal(WHITE_K, 16, k), b = ordinal(BLACK_K, 11, k);
    if (w >= 0)
        pick_template(w);
    else if (b >= 0 && b < 4) {
        arr_section(b + 1);
        say(arr_playing ? "NEXT" : "FIRST", SECT_NAME[b + 1]);
    } else if (b == 4)
        say("INTRO", "later");
    else
        say("MARKERS", "later");
}
static void key_down(int k)
{
    ui.knote[k] = (uint8_t)key_note(k);
    if (ui.btn >> B_SEL & 1u) {
        ui.btn_used |= 1u << B_SEL;
        ui.kmode[k] = KM_NONE;
        sel_key(k);
    } else if (ui.chord) {
        ui.kmode[k] = KM_CHORD;
        chord_keys();
    } else {
        ui.kmode[k] = KM_PLAY;
        arr_key(ui.knote[k], 1, 100);            /* the selected track's sound; recorded when recording */
    }
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

/* the page buttons: a button holds up to three pages, pressed again: the next */
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_SONG, 1}, {B_SEQ, P_PATTERN, 2}, {B_EDIT, P_TRACK, 2}, {B_FX, P_MIXER, 2},
    {B_LFO, P_CHORDS, 1}, {B_ENV, P_ARRANGE, 1}, {B_ARP, P_TEMPLATES, 2}, {B_GLO, P_SETUP, 2}};
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
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

/* ------------------------------------------------------------- jobs --- */
/* SAVE held: a job on the pattern playing (its slot) and the selected track; KNOB 1 picks, KNOB 2 the copy's
 * destination, SEL does it, SAVE (or a page) cancels */
static void jobs_open(void)
{
    int st = songs.song[proj.song].style[eslot()];
    ui.job = 1;
    ui.job_sel = 0;
    ui.job_to = (uint8_t)((st < ARR_USER_PATTERNS ? st + 1 : 0) % ARR_USER_PATTERNS);
    ui.touched = -1;
}
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
        uint16_t d = arr_pattern_copy(s, &scratch);
        pat_name(to, t);
        if (project_pattern_save(to, &scratch) != 0) {
            say("NOT COPIED", "FLASH ERROR");
            return;
        }
        if (to == st) {                          /* into its own: saved */
            app.saved[s] = d;
            app.edited[s] = 0;
        }
        for (int j = 0; j < ARR_NSECT; j++)      /* the slots playing that pattern, unless changed */
            if (j != s && songs.song[proj.song].style[j] == to && !slot_unsaved(j)) {
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
    case B_PLAY: arr_play(!arr_playing); break;
    case B_REC:
    {
        int on = !arr_recording;                 /* the state asked for (the engine's follows a block later) */
        arr_record(proj.trk, on);
        say(on ? "RECORD" : "RECORD OFF", on ? TRACK_NAME[proj.trk] : 0);
    }
        break;
    default: break;
    }
}
static void button_up(int b)                     /* a tap: released before it was used as a hold */
{
    switch (b) {
    case B_SEL:
        if (ui.job)
            job_run();
        else
            set_chord_mode(!ui.chord);
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
        jobs_open();
    }
    if ((ui.btn >> B_SEL & 1u) && now - ui.btn_t[B_SEL] > HOLD_MS)
        ui.btn_used |= 1u << B_SEL;              /* SEL held to look at the lights: not a tap */
}

/* another song: recording off, what changed saved (else nothing changes: the takes would be lost), its
 * patterns loaded */
static void song_change(int s)
{
    char t[12];
    int rec = arr_recording;
    arr_record(proj.trk, 0);
    if (unsaved() && save_all() != 0) {
        say("NOT SAVED", "SONG KEPT");
        if (rec)
            arr_record(proj.trk, 1);             /* as it was */
        return;
    }
    proj.song = (uint8_t)s;
    song_load();
    mark_dirty();
    itoa_u((uint32_t)s + 1u, cat_s(t, "SONG "));
    say(t, app.song_on ? 0 : "empty");
}

/* the four knobs on the page shown */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void song_knob(int k, int32_t e)
{
    switch (k) {
    case 0:
        proj.key = (uint8_t)((proj.key + (e > 0 ? 1 : 11)) % 12u);
        arr_set_key(proj.key);
        mark_dirty();
        break;
    case 1: set_tempo(proj.tempo + accel(EN_K2, e)); break;
    case 2: {
        int q = proj.quant + (e > 0 ? -1 : 1);   /* clockwise: finer */
        if (q >= 0 && q < ARR_NQ) {
            proj.quant = (uint8_t)q;
            arr_set_quant(q);
            mark_dirty();
        }
        break;
    }
    case 3: {
        int t = arr_template_now;                /* from none: the last one picked, or the first */
        t = t >= 0 && t < NTEMPLATES ? t + (e > 0 ? 1 : -1) : app.tmpl >= 0 ? app.tmpl : 0;
        if (t >= 0 && t < NTEMPLATES)
            pick_template(t);
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
static void edit_knob(int k, int32_t e)
{
    int s = eslot(), d = e > 0 ? 1 : -1;
    arr_trk_t *t = &app.trk[s][proj.trk];
    switch (ui.page) {
    case P_PATTERN:
        if (k == 0)
            song_knob(2, e);                     /* the grid, as on the Song page */
        else if (k == 3)
            trk_set(&t->mute, e > 0, 0, 1);      /* clockwise: muted, back: playing */
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
    case P_MASTER: {
        uint8_t *v = k == 0 ? &proj.rev_type : k == 1 ? &proj.rev_level : k == 2 ? &proj.cho_type : &proj.cho_level;
        int to = k == 1 || k == 3 ? clampi(*v + accel(EN_K1 + k, e), 0, 127) : clampi(*v + d, 0, 3);
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
    if (ui.job) {                                /* the jobs: KNOB 1 the job, KNOB 2 the copy's pattern */
        if (k == 0)
            ui.job_sel = (uint8_t)clampi(ui.job_sel + (e > 0 ? 1 : -1), 0, 2);
        else if (k == 1)
            ui.job_to = (uint8_t)((ui.job_to + (e > 0 ? 1 : ARR_USER_PATTERNS - 1)) % ARR_USER_PATTERNS);
        touch(k);
        return;
    }
    if (ui.page == P_SONG)
        song_knob(k, e);
    else if (ui.page <= P_MASTER)
        edit_knob(k, e);
    else
        return;
    touch(k);
}
/* PRESETS on the Pattern pages: the pattern the section playing plays (the song's choice); while playing only
 * when its changes are saved (a save would pause the audio) */
static void pattern_pick(int32_t e)
{
    int s = eslot(), st = songs.song[proj.song].style[s], to;
    char t[8];
    to = st >= ARR_USER_PATTERNS ? 0 : clampi(st + (e > 0 ? 1 : -1), 0, ARR_USER_PATTERNS - 1);
    if (to == st)
        return;
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
    songs.song[proj.song].style[s] = (uint8_t)to;
    app.songs_dirty = 1;
    app.chg++;
    slot_load(s, to);
    pat_name(to, t);
    say(SECT_NAME[s + 1], t);
}
static void presets(int32_t e)
{
    if (ui.page == P_SONG) {
        int s = (app.song_pend ? app.song_pend - 1 : proj.song) + (e > 0 ? 1 : -1);
        char t[12];
        if (s < 0 || s >= ARR_SONGS)
            return;
        if (arr_playing) {                       /* a save (flash) would pause the audio: at the stop */
            app.song_pend = (uint8_t)(s == proj.song ? 0 : s + 1);
            itoa_u((uint32_t)s + 1u, cat_s(t, "SONG "));
            say(t, s == proj.song ? "stays" : "at stop");
        } else
            song_change(s);
    } else if (ui.page == P_PATTERN || ui.page == P_PATSET)
        pattern_pick(e);
    else if (ui.page == P_TRACK) {               /* a family of eight, as the GM module */
        arr_trk_t *t = &app.trk[eslot()][proj.trk];
        if ((t->chan & 15u) != 9u) {
            trk_set(&t->program, (t->program / 8 + (e > 0 ? 1 : -1)) * 8, 0, 127);
            touch(0);
        }
    }
}

static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, t = st & 0xF0u;
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
        int hold = i == B_SEL || i == B_SAVE || i == B_OCTDN || i == B_OCTUP;
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
        set_tempo(proj.tempo + accel(EN_SELECT, e));
        if (ui.page == P_SONG)
            touch(1);
    }
    if ((e = plat_enc(EN_ALGO)) != 0)
        set_track(proj.trk + (e > 0 ? 1 : -1));
    if ((e = plat_enc(EN_PRESET)) != 0)
        presets(e);
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0)
            knob((int)i, e);
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
    if (ui.chord)
        b |= 1u << B_SEL;
    for (i = 0; i < NPAGE_BTN; i++)
        if (ui.page >= PAGE_BTN[i].first && ui.page < PAGE_BTN[i].first + PAGE_BTN[i].n)
            b |= 1u << PAGE_BTN[i].btn;
    if (ui.btn >> B_SEL & 1u) {                  /* SEL held: the section and the template playing */
        int n = arr_next_sect, s = n >= SEC_A && n <= SEC_END ? n : arr_sect, t = arr_template_now;
        if (s >= SEC_A && s <= SEC_END)
            k |= 1u << BLACK_K[s - 1];
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

void ui_frame(void)
{
    input();
    watch_takes();
    ui.stopped = arr_playing ? 0 : ui.stopped < 255u ? ui.stopped + 1u : 255u;
    if (app.edit_pend && plat_ms() - app.edit_t >= EDIT_MS)
        edits_flush();
    /* a song picked while playing: now, stopped for a frame (not a PLAY the engine has yet to see) and the
     * voices ended (a save pauses the audio) */
    if (app.song_pend && ui.stopped > 1u && !gm_voices) {
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
