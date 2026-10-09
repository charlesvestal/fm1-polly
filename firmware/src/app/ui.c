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
    for (int t = 0; t < ARR_TRACKS; t++) {
        app.tprog[s][t] = p->trk[t].program;
        app.tchan[s][t] = p->trk[t].chan;
        app.tmute[s][t] = p->trk[t].mute;
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
        }
    }
}
/* what changed: the project, and each user pattern recorded into since it was loaded or saved. Slots sharing
 * a user pattern (a song may choose that) get the saved pattern too, by arr_store_pattern (refused while
 * they are recorded into: then they stay unsaved); when two of them were recorded into, the newest take is
 * kept and the screen says so. */
static int save_all(void)
{
    const song_t *sg = &songs.song[proj.song];
    uint32_t done = 0;
    int rc = 0;
    watch_takes();
    if (app.dirty) {
        if (project_save() == 0)
            app.dirty = 0;
        else
            rc = -1;
    }
    for (int s = 0; s < ARR_NSECT; s++) {
        int st = sg->style[s], k = s, lost = -1;
        uint16_t d;
        if (st >= ARR_USER_PATTERNS || (done >> s & 1u) || arr_dirty[s] == app.saved[s])
            continue;
        for (int j = 0; j < ARR_NSECT; j++)     /* the newest take of the slots sharing it */
            if (sg->style[j] == st) {
                done |= 1u << j;
                if (j != k && arr_dirty[j] != app.saved[j] && (int32_t)(app.take_t[j] - app.take_t[k]) >= 0)
                    k = j;
            }
        d = arr_pattern_copy(k, &scratch);
        if (project_pattern_save(st, &scratch) != 0) {
            rc = -1;
            continue;
        }
        app.saved[k] = d;
        for (int j = 0; j < ARR_NSECT; j++) {
            uint16_t e;
            if (j == k || sg->style[j] != st)
                continue;
            e = arr_dirty[j];
            if (e != app.saved[j])
                lost = j;                        /* its takes give way to the newest */
            if (arr_store_pattern(j, &scratch, e)) {
                slot_cache(j, &scratch);
                app.saved[j] = e;
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

void app_start(void)
{
    memset(&app, 0, sizeof app);
    app.tmpl = -1;
    project_songs_load(&songs);
    arr_set_tempo(proj.tempo);
    arr_set_key(proj.key);
    arr_set_quant(proj.quant);
    arr_midi_out_on(1);                          /* the tracks out on their channels */
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
        say(TRACK_NAME[proj.trk], app.tchan[s][proj.trk] == 9u ? "Drums" : GM_PROGRAM[app.tprog[s][proj.trk] & 127u]);
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
    {B_HOME, P_SONG, 1}, {B_SEQ, P_PATTERN, 2}, {B_EDIT, P_TRACK, 1}, {B_FX, P_MIXER, 2},
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

static void button_down(int b)
{
    if (page_button(b))
        return;
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
    case B_SEL: set_chord_mode(!ui.chord); break;
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
        say("JOBS", "coming");
    }
    if ((ui.btn >> B_SEL & 1u) && now - ui.btn_t[B_SEL] > HOLD_MS)
        ui.btn_used |= 1u << B_SEL;              /* SEL held to look at the lights: not a tap */
}

/* another song: recording off, what changed saved (else nothing changes: the takes would be lost), its
 * patterns loaded */
static void song_change(int s)
{
    char t[12];
    arr_record(proj.trk, 0);
    if (unsaved() && save_all() != 0) {
        say("NOT SAVED", "SONG KEPT");
        return;
    }
    proj.song = (uint8_t)s;
    song_load();
    mark_dirty();
    itoa_u((uint32_t)s + 1u, cat_s(t, "SONG "));
    say(t, app.song_on ? 0 : "empty");
}

/* the four knobs on the page shown */
static void knob(int k, int32_t e)
{
    if (ui.page != P_SONG)
        return;
    touch(k);
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
    if (app.song_pend && ui.stopped > 1u) {      /* a song picked while playing: now (stopped for a frame, */
        int s = app.song_pend - 1;               /* not a PLAY the engine has yet to see) */
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
