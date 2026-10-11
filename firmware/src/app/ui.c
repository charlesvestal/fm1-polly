/* SPDX-License-Identifier: GPL-3.0-only */
/* Polly's input: the controls (the spec's Buttons and Keys tables), the songs and patterns into the arranger, saving,
 * the lights. Part of the unity build (after pages.c, which holds the shared state and the drawing); the host
 * simulator includes it too.
 *
 *   PLAY            start / stop (SONG: from the SONG screen's row, else row 1)
 *   REC             on its release: what the keys play here (chords on HOME / LOOP, the track on SEQ / EDIT, rows on
 *                   SONG); held for its card, nothing
 *   SEL tap         the screen as a list (SELECT moves, ALGORITHM changes, SEL does an action, HOME goes back);
 *                   a question: SEL yes, HOME no
 *   a button held alone 1 s: its help card (letting go does nothing)
 *   HOME held       the shortcuts on screen (after HOME_HOLD_MS):
 *     + white key 1..16   loop n goes round (from the next bar while playing); the loop playing again: FREE
 *     + black key 1..5    Intro, Main A, Main B, Fill (AB from Main A, BA from Main B), Ending, from the next bar
 *     + SELECT            the song's tempo
 *     + PRESETS           the song (while playing: after the stop)
 *     + REC / + PLAY      undo / redo (the journal: below)
 *   HOME: KNOB 1 the style, 2 the tempo, 3 TRANSPOSE (the song's), 4 LOOP ("-" FREE, L1..L16; "Song" in a song);
 *   SELECT: SONG's three pages (the rows, the row, the song)
 *   (a page button: always its first page; SELECT, and only SELECT, turns the pages)
 *   ARP LOOP (x2), SEQ Pattern (x2), EDIT Track (x2), LFO Mixer (x2), FX Master FX, ENV Style (x2), GLO Setup (x2);
 *   the tag's dots: which of a button's pages
 *   the keys        by the page (key_role): chords (HOME, LOOP), the selected track (SEQ, EDIT), the track (LFO's
 *                   black keys), nothing (FX, ENV, GLO)
 *   SAVE            saves (lit: something to save); held + white key n: copy here (HOME, LOOP: the loop to Ln; SEQ:
 *                   the pattern to P(n))                                    OCT- / OCT+   the keys' octave (LOOP: the cursor)
 *   SELECT          the button's next / previous page      ALGORITHM   the track (D1..C4, LD1, LD2)   PRESETS   per page
 * MIDI in (USB, TRS) plays all 16 channels; its clock drives the arranger. */
#include "journal.c"                            /* undo and redo (HOME + REC, HOME + PLAY) */

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    upcase(ui.msg[0]);                           /* messages are upper case, whole (LOOP L1 POP), as the tags and the
                                                  * footer; titles (the header's page, a loop's name on its screen) mixed */
    upcase(ui.msg[1]);
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
/* NO FREE SLOT (or pattern): said, and for a while SEL opens SLOTS (the footer says so) */
static void say_noslot(const char *a, const char *b)
{
    say(a, b);
    ui.slots_until = ui.msg_until + 3000u;
}

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

/* ================================================================ undo === */
/* HOME + REC undoes, HOME + PLAY redoes (journal.c): the song's settings and rows, its loops, its lead notes, and the
 * patterns the song plays as the app stores them. A step is what changed before a pause (UNDO_QUIET ms with nothing
 * touched or held), a lead take or a chord / row take (REC on while playing), or a pass of a pattern track's
 * recording. Kept in RAM only (the pool), from the song's load: a song change (or a power-off) starts it again.
 * Regions: the song (its song_t), its loops (sloops.loop), its lead notes (leads, up to the sounds' copy), and one
 * pattern: the slot touched last (uj_pat_touch, before any change of it: slot_own, slot_style), its before-image kept
 * in uj_pat and compared with a copy (scratch) at each commit. Not covered: the tempo turned while playing, the
 * project's settings (the Setup page, the master FX, the keys' octave and track) */
#define UNDO_QUIET 1000u
enum { UR_SONG, UR_LOOPS, UR_LEADS, UR_PAT };
static uj_t uj EN_POOL;
static song_t uj_song EN_POOL;                   /* the shadows: the song, its loops, its lead notes, one pattern */
static loop_t uj_loops[ARR_LOOPS] EN_POOL;
static uint8_t uj_leads[__builtin_offsetof(leads_t, trk)] EN_POOL;
static pattern_t uj_pat EN_POOL;
static struct {
    int8_t slot;                                 /* the slot uj_pat follows, -1 none */
    uint8_t passing;                             /* a pattern track recording while playing: its passes counted */
    uint16_t pass;
    char hint[UJ_LABEL + 1];                     /* what the app says the step is ("CLEAR LOOP"): its first */
} ujs;

static void undo_hint(const char *t)            /* the step's name, if it has none yet */
{
    int i;
    if (ujs.hint[0])
        return;
    for (i = 0; i < UJ_LABEL && t[i]; i++)
        ujs.hint[i] = t[i];
    ujs.hint[i] = 0;
}
/* the history from here (the song as it is now): at the start, a song change, the bank written */
static void undo_reset(void)
{
    uj_region(&uj, UR_SONG, csong(), &uj_song, sizeof uj_song);
    uj_region(&uj, UR_LOOPS, sloops.loop, uj_loops, sizeof uj_loops);
    uj_region(&uj, UR_LEADS, &leads, uj_leads, sizeof uj_leads);
    uj_region(&uj, UR_PAT, 0, &uj_pat, sizeof uj_pat);
    for (int r = 0; r < UR_PAT; r++)
        uj_resync(&uj, r);
    uj_clear(&uj);
    ujs.slot = -1;
    ujs.hint[0] = 0;
}
/* the shadows brought to what is there, the history kept: after a save (a lead part's header written, a shared
 * pattern stored into the slots playing it) */
static void undo_sync(void)
{
    for (int r = 0; r < UR_PAT; r++)
        uj_resync(&uj, r);
    if (ujs.slot >= 0)
        arr_pattern_copy(ujs.slot, &uj_pat);
}
/* the changes since the last step, as a step. The pattern followed is compared as it is in the engine (a copy in
 * scratch: callers hold nothing there); when another pattern took its slot meanwhile (a style picked, a user
 * pattern loaded) that is no edit of it (the song's style[] is, and undo loads the pattern again), except the first
 * change of a style's section (copy-on-write: the user pattern starts as the section, its changes are kept) */
static void undo_commit(void)
{
    int s = ujs.slot;
    uj.r[UR_PAT].live = 0;
    if (s >= 0) {
        uint16_t was = uj_song.style[s], now = csong()->style[s];
        arr_pattern_copy(s, &scratch);
        if (was != now && !(was >= ARR_USER_PATTERNS && now < ARR_USER_PATTERNS))
            memcpy(&uj_pat, &scratch, sizeof uj_pat);
        else {
            uj.r[UR_PAT].live = (uint8_t *)&scratch;
            uj.r[UR_PAT].aux = (uint8_t)s;
        }
    }
    if (arr_playing) {                           /* the tempo turned while playing: not a step */
        uj_song.tempo = csong()->tempo;
        uj_song.flags = csong()->flags;
    }
    uj_commit(&uj, ujs.hint);
    uj.r[UR_PAT].live = 0;
    ujs.hint[0] = 0;
}
/* slot s's pattern is about to change: followed from here (another slot followed: its changes a step first) */
static void uj_pat_touch(int s)
{
    if (s < 0 || s >= ARR_NSECT || ujs.slot == s)
        return;
    if (ujs.slot >= 0)
        undo_commit();
    arr_pattern_copy(s, &uj_pat);
    ujs.slot = (int8_t)s;
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
/* ---- the shared slots (user patterns, lead parts, the songs' loops: project.c) */
/* the slots the song's lead parts and loops hold or have taken (not saved into yet): no pattern takes them */
static uint32_t slots_reserved(void)
{
    uint32_t m = 0;
    for (int k = 0; k < LEAD_PARTS; k++)
        if (app.lslot[k] >= 0 && app.lslot[k] < ARR_USER_PATTERNS)
            m |= 1u << app.lslot[k];
    if (app.lpslot >= 0 && app.lpslot < ARR_USER_PATTERNS)
        m |= 1u << app.lpslot;
    return m;
}
/* slot p holds lead notes or a song's loops (any song's, or the song's taken): never a pattern's */
static int slot_held(int p) { return (slots_reserved() >> p & 1u) || project_slot_held(p); }
/* a slot holding nothing: never saved, or saved without notes (a pattern looked at in scratch), not the song's own */
static int pattern_blank(int p) { return !(slots_reserved() >> p & 1u) && project_slot_blank(p, &scratch); }
/* the slot the copy-on-write (and a lead part) takes: one no song plays and that holds nothing (a pattern or a lead
 * part with notes is never taken, played or not); -1 none */
static int pattern_free(void) { return project_slot_free(&songs, slots_reserved(), &scratch); }
/* copy-on-write: before slot s's pattern is changed (recorded into, edited, erased, cleared), a slot playing a
 * style's section gets a user pattern of its own: the first free one (pattern_free) takes the section as it
 * plays now, saved at the next save (or autosave). 0: the slot plays a user pattern; -1: none free (the change
 * plays, unsaved, until another pattern is picked) */
static int slot_own(int s)
{
    song_t *g = &songs.song[proj.song];
    int p;
    char t[8];
    uj_pat_touch(s);                             /* (undo: the pattern as it was) */
    if (app.slot_buf[s] >= 0) {                  /* a QY70 section: a pattern of its notes (up to 600) from here */
        arr_pattern_copy(s, &scratch);
        arr_load_pattern(s, &scratch);
        app.slot_buf[s] = -1;
    }
    if (g->style[s] < ARR_USER_PATTERNS)
        return 0;
    if ((p = pattern_free()) < 0) {
        say_noslot("NO FREE SLOT", "NOT KEPT");  /* (SLOTS: a slot freed, the change is kept then: slots_retry) */
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
/* ---- the lead tracks' notes (leads): the song's, loaded with it, written as the engine hands recorded notes over,
 * saved at rest with the rest (save_all: only the parts changed) */
/* every write into leads (the engine reads it): held off, done, gen counted */
static void lead_write(int on)
{
    if (on) {
        arr_edit(1);
        return;
    }
    leads.gen++;
    arr_edit(0);
    app.chg++;
    ui.act_t = plat_ms();
}
/* the parts a change from index i touched (the notes before and after) to save */
static void lead_changed(int i, int before)
{
    app.ldirty |= (uint8_t)leads_parts_from(i, before, leads.n);
}
/* the song's leads' sounds (song_t.lead) to the engine's copy: after they are edited, and at a load */
static void lead_sounds_sync(void)
{
    arr_edit(1);
    memcpy(leads.trk, csong()->lead, sizeof leads.trk);
    leads.gen++;
    arr_edit(0);
}
/* the selected song's lead parts from their slots (a song picked: its unsaved changes were saved first) */
static void leads_load(void)
{
    int rc;
    arr_edit(1);
    rc = project_leads_load(proj.song, &leads, app.lslot);
    memcpy(leads.trk, csong()->lead, sizeof leads.trk);
    leads.gen++;
    arr_edit(0);
    app.ldirty = (uint8_t)(rc == 1 ? (1u << LEAD_PARTS) - 1u : 0u);   /* put right (leads_fix): saved so */
}
/* a note into the song's leads (a recorded one, arr_lead_take; a copied one): the same note at the same place
 * replaces; a part fills, the next takes a free slot; none: LEAD FULL (NO FREE SLOT for the first), -1 */
static int lead_put(const lead_ev_t *e)
{
    int at, rc, before = leads.n;
    for (;;) {
        int room = (app.lslot[0] >= 0) * LEAD_PART_NOTES + (app.lslot[1] >= 0) * LEAD_PART_NOTES, k, p;
        lead_write(1);
        rc = leads_insert(&leads, e, room, &at);
        lead_write(0);
        if (rc >= 0)
            break;
        k = app.lslot[0] < 0 ? 0 : 1;            /* full: the next part, in a slot of its own */
        if (room >= LEAD_MAX || (p = pattern_free()) < 0) {
            say_noslot(k ? "LEAD FULL" : "NO FREE SLOT", k ? "NOTES NOT KEPT" : "LEADS NOT KEPT");
            return -1;
        }
        app.lslot[k] = (int8_t)p;
    }
    if (rc == 0)
        app.ldirty |= (uint8_t)(at < LEAD_PART_NOTES ? 1u : 2u);
    else
        lead_changed(at, before);
    return 0;
}
static void lead_add(const arr_lnote_t *n)
{
    lead_ev_t e;
    lead_set(&e, n->row, n->pos, n->len, n->note, n->vel, n->trk, n->root);
    undo_hint(n->trk ? "TAKE LD2" : "TAKE LD1");
    lead_put(&e);
}
static void leads_poll(void)                     /* the notes the engine recorded (ui_frame) */
{
    arr_lnote_t n;
    while (arr_lead_take(&n))
        lead_add(&n);
}
/* ---- the song's loops (sloops): its own 16, loaded with it, saved at rest when they changed; a slot of their own
 * once they differ from the defaults */
static uint32_t loops_sum(void)                  /* FNV-1a over the 16 loops: did they change since loaded or saved */
{
    const uint8_t *b = (const uint8_t *)sloops.loop;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < sizeof sloops.loop; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
static void loops_load(void)                     /* the selected song's (its changes were saved first) */
{
    arr_edit(1);
    project_loops_load(proj.song, &sloops, &app.lpslot);
    arr_edit(0);
    app.lpsum = loops_sum();
    app.lpdirty = 0;
}
/* the loops changed and are not the defaults: a slot for them, if they have none (NO FREE SLOT: they play, not
 * kept). 0: they have one (or need none) */
static int loops_slot_take(void)
{
    int p;
    if (app.lpslot >= 0 || loops_is_default(sloops.loop))
        return 0;
    if ((p = pattern_free()) < 0) {
        say_noslot("NO FREE SLOT", "LOOPS NOT KEPT");
        app.lpsum = loops_sum();                 /* (said once: again at their next change) */
        return -1;
    }
    app.lpslot = (int8_t)p;
    return 0;
}
/* save_all's part: the loops, when they changed (their slot let go when they are the defaults again). A song's loops
 * with no slot free are not kept (said), never a failed save (a song change would be refused) */
static int loops_save(void)
{
    uint32_t sum = loops_sum();
    if (sum == app.lpsum) {
        app.lpdirty = 0;
        return 0;
    }
    if (loops_slot_take() != 0) {
        app.lpdirty = 0;
        return 0;
    }
    if (project_loops_save(proj.song, &sloops, &app.lpslot) != 0)
        return -1;
    app.lpsum = sum;
    app.lpdirty = 0;
    return 0;
}

/* MODE: FREE (the keys' chords only), LOOP (app.loop goes round), SONG (the song's rows: from app.row stopped,
 * else from row 1 at the next bar line). Leaving SONG, the song's FREE / LOOP mutes are all sent again (a row's
 * or a hand mute may be sounding) */
static void chord_rec_stop(void);
/* app.loop changes here only: another loop, its cursor at beat 0, STEP a bar again, no chord entry carried over */
static void app_loop_set(int i)
{
    app.picked = 1;
    if (i == app.loop)
        return;
    app.loop = (uint8_t)(i % ARR_LOOPS);
    app.cbeat = 0;
    app.cstep = 2;
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
static int save_all_now(void)
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
    if (app.lpdirty && loops_save() != 0)        /* the song's loops (the defaults again: their slot let go) */
        rc = -1;
    if (app.ldirty) {                            /* the lead parts changed (a part emptied: its slot let go) */
        if (project_leads_save(proj.song, &leads, app.lslot, app.ldirty) == 0)
            app.ldirty = 0;
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

/* (the changes before it are a step of their own; the save writes headers into the lead parts and may store a shared
 * pattern into the slots playing it: the undo's shadows then follow) */
static int save_all(void)
{
    int rc;
    undo_commit();
    rc = save_all_now();
    undo_sync();
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
    arr_loops(sloops.loop);                      /* the engine reads the song's loops from here (writes: arr_edit) */
    loops_load();
    arr_leads(&leads);                           /* ... and the song's lead notes */
    leads_load();
    arr_set_quant(proj.quant);
    arr_midi_out_on(proj.mout);                  /* the tracks out on their channels */
    fx_apply();
    song_load();                                 /* (the song's tempo and transpose) */
    arr_record(sel_trk(), 0);                    /* the keys play the selected track */
    undo_reset();                                /* nothing to undo at power-on */
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
/* every write into the loops (sloops.loop[]) or a song's rows: the render reads them on beat and bar lines,
 * arr_edit(1) holds it off; done (0), the songs and the song's loops are unsaved (the loops take a slot when they
 * first differ from the defaults: NO FREE SLOT said then) */
static void songs_write(int on)
{
    arr_edit(on);
    if (!on) {
        song_touched();
        app.lpdirty = 1;
        if (app.lpslot < 0 && loops_sum() != app.lpsum)
            loops_slot_take();
    }
}
/* the song's tempo; by hand (KNOB 2, HOME + SELECT) it is the song's own from then on (SONG_F_TEMPO): a style picked
 * after keeps it. A style picked before that brings its tempo (style_tempo_follow) */
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
static void set_tempo_hand(int bpm)
{
    song_t *g = csong();
    if (!(g->flags & SONG_F_TEMPO) && clampi(bpm, 40, 240) != g->tempo)
        g->flags |= SONG_F_TEMPO;
    set_tempo(bpm);
}
static void style_tempo_follow(int i)
{
    if (!(csong()->flags & SONG_F_TEMPO))
        set_tempo(style_tempo(i));
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
/* recording as played: a Chord 1 / 2 or Bass track with no notes, recorded into the first time, becomes "as played"
 * (Parallel, ARR_TF_ASPLAYED: its notes stored over the root, which the engine takes nearest C; they play as played
 * over the chord they were played on, moved by the root's move over another), its range the whole keyboard and no
 * high key (never folded); once (a type set back by hand stays). Stored into the arranger now, before the first note.
 * A track with notes (a style's comping) keeps its type: they would no longer follow the chords. 1: switched, 2: kept
 * (it has notes), 0: nothing to do */
static int rec_as_played(int s, int trk)
{
    arr_trk_t *t = &app.trk[s][trk & 7];
    if ((t->flags & ARR_TF_ASPLAYED) || (t->x.type != XT_CHORD1 && t->x.type != XT_CHORD2 && t->x.type != XT_BASS))
        return 0;
    arr_pattern_copy(s, &scratch);
    if (scratch.trk[trk & 7].count)
        return 2;
    t->x.type = XT_PARALLEL;
    t->x.lo = 0;
    t->x.hi = 127;
    t->x.hikey = XT_NOKEY;
    t->flags |= ARR_TF_ASPLAYED;
    edited(s);
    edits_flush();
    return 1;
}
/* a track kept on its chord type (rec_as_played 2): said once a power-on per track, the footer saying how meanwhile
 * (ui.follow_hint, until REC goes off or another track). 1: say it now */
static int follows_told(int trk)
{
    if (ui.follow_told >> (trk & 7) & 1u)
        return 0;
    ui.follow_told |= (uint8_t)(1u << (trk & 7));
    ui.follow_hint = 1;
    return 1;
}
/* ALGORITHM (and the Mixer's black keys): the track, D1 .. C4, then the song's lead tracks LD1, LD2 (round). REC on goes
 * on into the track picked; a lead records only in a song (in FREE / LOOP: REC off, said) */
static void set_track(int t)
{
    int r, rec = arr_recording || ui.rec_want;
    t = (t % ARR_ALLTRK + ARR_ALLTRK) % ARR_ALLTRK;
    ui.follow_hint = 0;
    if (t >= ARR_TRACKS) {                       /* a lead: proj.trk stays the pattern's track */
        app.lead = (uint8_t)(t - ARR_TRACKS + 1);
        if (rec && app.mode != ARR_SONG) {
            arr_record(t, 0);
            ui.rec_want = ui.rec_seen = 0;
            say("LEADS RECORD", "IN A SONG");
            return;
        }
        arr_trk_t lb;
        arr_record(t, rec);                      /* (recording: it goes on, no count-in again) */
        say(TRACK_NAME[t], trk_sound(sel_arr(&lb)));
        return;
    }
    if (app.lead && rec)
        slot_own(eslot());                       /* from a lead to a pattern's track, recording: a user pattern first */
    app.lead = 0;
    proj.trk = (uint8_t)t;
    if (arr_recording && (r = rec_as_played(eslot(), proj.trk)) != 0) {
        char m[24];
        arr_record(proj.trk, ARR_REC_BAR);       /* recording goes on, into this track */
        if (r == 1)
            say(cat_s(cat_s(m, TRACK_NAME[proj.trk]), ": AS PLAYED"), trk_sound(&app.trk[eslot()][proj.trk]));
        else if (follows_told(proj.trk))
            say(cat_s(cat_s(m, TRACK_NAME[proj.trk]), " FOLLOWS CHORDS"), 0);
        mark_dirty();
        return;
    }
    arr_record(proj.trk, arr_recording ? ARR_REC_BAR : 0);   /* recording goes on, into this track */
    say(TRACK_NAME[proj.trk], trk_sound(&app.trk[eslot()][proj.trk]));
    mark_dirty();
}

static void song_pick(int d);
static void bars_to_loop(void);
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
    const loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
    int bpb = l->beats ? l->beats : 4;
    return app.cstep == 0 ? 1 : app.cstep == 1 ? (bpb / 2 > 1 ? bpb / 2 : 1) : bpb;
}
/* the cursor d (1 / -1) steps on, on the STEP's grid of each bar (bar: its beat 1; half: beats 1 and 3 of 4; beat):
 * off the grid (STEP changed, or REC stopped it on a beat), OCT- goes to the grid point before it and OCT+ to the one
 * after it (4.4 with bar: OCT- 4.1; 3.2 with half: OCT+ 3.3). REC on: it may stand at the loop's end (a chord there
 * makes the loop longer), and a step on from the end skips: the loop grows to hold it, by bars of "no change" (LOOP
 * FULL at the limit). REC off: round the loop. Back past beat 0: the last bar's last grid point */
static void cursor_move(int d)
{
    loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
    int len = loop_len(l), s = step_beats(), bpb = l->beats ? l->beats : 4;
    int bar = app.cbeat / bpb, p = app.cbeat % bpb, b;
    char t[8];
    if (d > 0) {
        p = (p / s + 1) * s;
        if (p >= bpb)
            bar++, p = 0;
    } else if (p % s)
        p = p / s * s;
    else if ((p -= s) < 0)
        bar--, p = (bpb - 1) / s * s;
    b = bar * bpb + p;
    if (d < 0) {
        if (b < 0)
            b = len > 0 ? (len - 1) / bpb * bpb + (bpb - 1) / s * s : 0;
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
    int len = loop_len(&sloops.loop[app.loop % ARR_LOOPS]), m = ui.rec_chords ? len : len > 0 ? len - 1 : 0;
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
    r = loop_write(&sloops.loop[i], b, chord_shift(c, -xpose_now()));
    songs_write(0);
    if (r < 0) {
        loop_name_l(i, t);
        say("LOOP FULL", t);
    } else {
        ui.wrote_t = plat_ms();
        ui.wrote_loop = (uint8_t)i;
        ui.wrote_beat = (uint8_t)b;
    }
    return r;
}
/* the beat nearest now of loop l playing (an empty loop: of the pattern's bar). A bar's first chord played within its
 * first beat (nothing written this REC on or after that downbeat) is the downbeat's, not the next beat's: a chord a
 * little late on the one */
static int live_beat(const loop_t *l)
{
    uint32_t lb, tk, ab, n = (uint32_t)loop_len(l), bpb = l->beats ? l->beats : 4u;
    int up;
    do {
        lb = arr_loop_beat;
        tk = arr_beat_tick;
        ab = arr_beats;
    } while (lb != arr_loop_beat || tk != arr_beat_tick || ab != arr_beats);
    up = tk % ARR_PPQ > ARR_PPQ / 2;
    if (!n)
        return (int)((tk / ARR_PPQ + (uint32_t)up) % bpb);
    if (up && lb % bpb == 0 && ui.lw_abs < (int32_t)ab)
        up = 0;                                  /* (the bar's first chord, late: its downbeat) */
    ui.lw_now = (int32_t)ab + up;                /* (the beat it lands on, counted as arr_beats) */
    return (int)((lb + (uint32_t)up) % n);
}
/* the take's beat now: from its downbeat, the nearest (below 0: before it); a bar's first chord within its first
 * beat: that downbeat (as live_beat) */
static int32_t take_beat(void)
{
    uint32_t b, tk;
    int32_t fb, bpb = sloops.loop[ui.take_loop % ARR_LOOPS].beats ? sloops.loop[ui.take_loop % ARR_LOOPS].beats : 4;
    do {
        b = arr_beats;
        tk = arr_beat_tick;
    } while (b != arr_beats || tk != arr_beat_tick);
    fb = (int32_t)(b - ui.take_b0);
    if (tk % ARR_PPQ > ARR_PPQ / 2 && !(fb >= 0 && fb % bpb == 0 && ui.take_last < fb))
        fb++;
    return fb;
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
        int len = loop_len(&sloops.loop[l]);
        if (arr_sect == SEC_INTRO) {
            say("WRITE", "after the Intro");     /* (the loop has not started) */
            return;
        }
        if (ui.mode_wait || arr_loop_now != l) { /* the loop asked for has not started: its place is not known */
            say("WRITE", "from the bar line");
            return;
        }
        b = live_beat(&sloops.loop[l]);
        if (b == prev && len > 0) {
            b = (b + 1) % len;                   /* a change on the same beat: the next one */
            ui.lw_now++;
        }
        ui.centry = (int8_t)(chord_write(l, b, ch) == 0 ? b : -1);
        ui.centry_step = 0;
        if (ui.centry >= 0 && ui.lw_now > ui.lw_abs)
            ui.lw_abs = ui.lw_now;
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
    ui.lw_abs = -1;                              /* (nothing written yet this REC: live_beat) */
    if (!ui.rec_chords) {                        /* off at the loop's end: the start of the last bar written (a knob
                                                  * there changes that bar's chord, OCT- walks back by bars) */
        const loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
        int len = loop_len(l), bpb = l->beats ? l->beats : 4;
        if (len > 0 && app.cbeat >= len)
            app.cbeat = (uint8_t)((len - 1) / bpb * bpb);
    }
    cursor_clamp();                              /* (an empty loop: beat 0) */
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
    char b[24];
    int i, bpb = clampi(app.hdr[eslot()].beats, 2, 7);
    if (ui.take) {
        take_end();
        return;
    }
    for (i = 0; i < ARR_LOOPS && sloops.loop[i].bars; i++)
        ;
    if (i == ARR_LOOPS) {
        say("NO EMPTY LOOP", "CLEAR ONE (SEL)");
        return;
    }
    ui.take_loop = (uint8_t)i;
    ui.take_any = 0;
    ui.take_last = -1;
    ui.centry = -1;
    if (sloops.loop[i].beats != bpb) {
        songs_write(1);
        loop_set_beats(&sloops.loop[i], bpb);
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
    loop_name_l(i, cat_s(b, "NEW LOOP "));     /* (when it starts: HOME's line says, "starts at the next bar") */
    say("RECORDING", b);
}
/* the take ends: its loop rounded to whole bars and said; 1 = kept (a chord written), 0 = no loop */
static int take_finish(void)
{
    loop_t *l = &sloops.loop[ui.take_loop % ARR_LOOPS];
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
        const loop_t *l = &sloops.loop[ui.take_loop % ARR_LOOPS];
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
    if (arr_mode == ARR_LOOP && (arr_playing ? arr_loop_now == i : app.loop == i)) {   /* stopped: the one picked */
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
/* stopped, a chord played on the keys sounds while they are held (step writing is not blind): each key on C1's
 * channel with C1's sound (the section's), ending at its release. Playing, the band plays the chord */
#define SOUND_TRK 4                              /* C1 */
static void chord_sound(int k, int on)
{
    const arr_trk_t *t = &app.trk[eslot()][SOUND_TRK];
    uint32_t ch = t->chan & 15u;
    if (on) {
        if (arr_playing || ch == 9u || (ui.ksound >> k & 1u))
            return;
        if (!ui.ksound) {                        /* (the first key: C1's sound, as the band would play it) */
            gm_midi(0xC0u | ch, t->program & 127u, 0);
            gm_midi(0xB0u | ch, 7, t->vol & 127u);
        }
        ui.ksound |= 1u << k;
        ui.ksound_ch[k] = (uint8_t)ch;
        gm_midi(0x90u | ch, ui.knote[k], 90);
    } else if (ui.ksound >> k & 1u) {
        ui.ksound &= ~(1u << k);
        gm_midi(0x80u | ui.ksound_ch[k], ui.knote[k], 0);
    }
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
    } else if (ui.btn >> B_SAVE & 1u) {          /* SAVE held + a white key: copy here (no save on its release); on */
        ui.btn_used |= 1u << B_SAVE;             /* SONG nothing: a white key there adds a row by itself */
        if (ordinal(WHITE_K, 16, k) >= 0 && !song_page())
            copy_to(ordinal(WHITE_K, 16, k));
    } else if (r == KR_CHORDS) {
        ui.kmode[k] = KM_CHORD;
        chord_sound(k, 1);
        chord_keys();
    } else if (r == KR_TRACK) {
        ui.kmode[k] = KM_PLAY;
        arr_key(ui.knote[k], 1, app.lead ? clampi(sel_lead()->vel, 1, 127) : 100);   /* the selected track's sound;
                                                 * recorded when recording (a lead: its VELOCITY) */
    } else if (r == KR_PICK && song_page())
        song_key(k);                             /* SONG: white the row's loop, black its mutes */
    else if (r == KR_PICK && b >= 0 && b < ARR_ALLTRK)
        set_track(b);                            /* the mixer: black key i, track i (9, 10: LD1, LD2) */
}
static void key_up(int k)
{
    uint8_t m = ui.kmode[k];
    ui.kmode[k] = KM_NONE;
    if (m == KM_PLAY)
        arr_key(ui.knote[k], 0, 0);
    else if (m == KM_CHORD)
        chord_keys();
    chord_sound(k, 0);
}
/* REC where the keys play chords (HOME, LOOP): a take running ends; in FREE on HOME a new loop's take starts; else
 * chords are written into the loop. In SONG, first MODE LOOP on the row's loop ("SONG PAUSED"; PLAY on SONG goes back);
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
        arr_record(sel_trk(), 0);
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

/* ---- SONG (HOME's next pages): the song's rows. White key n: the row at the cursor plays Ln (row_tap: on the END row
 * a row is added; never a REPEAT, that is the REPEAT knob's), the cursor to the row after; black keys 1-10: the row's
 * mutes (D1 D2 PC BA C1 C2 C3 C4, LD1 LD2; the row playing: heard at once; on the END row: the last row's, the one just
 * added). Every write to the rows goes through songs_write (the render reads them at each block's start). Rows are
 * only set or added here, never moved (INSERT / DELETE ROW: stopped only), so the row playing keeps its index */
static void song_tap(int w)
{
    song_t *g = csong();
    int r;
    songs_write(1);
    r = row_tap(g, app.row, w);
    songs_write(0);
    if (r < 0) {
        say("SONG FULL", "32 ROWS");
        return;
    }
    ui.row_follow = 0;                           /* (writing: the cursor stays where the keys write) */
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
    if (b < 0 || b >= ARR_ALLTRK)
        return;                                  /* black key 11: nothing */
    if (!g->nrows) {
        say("NO ROWS", "WHITE KEYS FIRST");
        return;
    }
    if (r >= g->nrows)
        r = g->nrows - 1;                        /* the END row: the last row (the one just added) */
    songs_write(1);
    if (b >= ARR_TRACKS)                         /* 9, 10: the lead tracks' */
        g->row[r].lmute ^= (uint8_t)(1u << (b - ARR_TRACKS));
    else
        g->row[r].mute ^= (uint8_t)(1u << b);
    songs_write(0);
    on = b >= ARR_TRACKS ? g->row[r].lmute >> (b - ARR_TRACKS) & 1u : g->row[r].mute >> b & 1u;
    if (arr_playing && arr_mode == ARR_SONG && arr_loop_now >= 0 && r == arr_row)
        arr_track_mute(b, on);                   /* the row playing: heard now (its next start would anyway) */
    say(TRACK_NAME[b], on ? "muted" : "plays");
}
/* REC on SONG (with PLAY): the player's changes into the rows. HOME + white key w: the loop goes into the row playing,
 * or, its last bar playing, into the row after it (after the last row: a row added); the song plays on (the engine
 * reads the rows at each block: the row playing's loop is heard from its next beat, the next row's at its start). No rows playing yet (an empty song), or the song's Ending playing: a row
 * at the end (from an empty song it starts at the next bar line). Stopped: as a tap at the cursor */
static int song_rec_loop(int w)
{
    song_t *g = csong();
    int n = g->nrows, r = arr_row, at = n, res;
    char a[12], t[16];
    if (!arr_playing) {
        song_tap(w);
        return 0;
    }
    if (n && arr_loop_now >= 0 && r < n && arr_sect != SEC_END) {   /* (the Ending: the rows are done, a row added) */
        const row_t *cw = &g->row[r];
        const loop_t *l = &sloops.loop[cw->loop % ARR_LOOPS];
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
/* REC on SONG: on (MODE SONG first; playing, from the cursor's row at the next bar line; stopped, PLAY on SONG
 * starts row 1, SEL's PLAY FROM ROW the cursor's) / off */
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
    else if (k == 2 && row_set_sect(g, app.row, to)) {   /* the loop's other rows on the old Main follow */
        char a[16];
        songs_write(0);
        cat_s(loop_name_l(w->loop % ARR_LOOPS, a), " ROWS:");
        say(a, row_main(w) == SEC_B ? "MAIN B" : "MAIN A");
        return;
    } else if (k == 3)
        w->rep = (uint8_t)to;
    songs_write(0);
}

/* the page buttons (PAGE_BTN, pages.c): always the button's first page (HOME: HOME, from SONG's pages too); SELECT,
 * and only SELECT, turns the pages, both ways */
static void arp_open(void);
static int page_button(int b)
{
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (PAGE_BTN[i].btn == b) {
            ui.page = PAGE_BTN[i].first;
            ui.touched = -1;
            if (b == B_ARP)
                arp_open();
            return 1;
        }
    return 0;
}
/* ARP before any loop was picked (start-up, no loop playing): the first empty loop, so writing on its grid never
 * overwrites a factory progression by accident; none empty, the last picked. From then on ARP shows app.loop */
static void arp_open(void)
{
    int i;
    if (app.picked || (arr_playing && arr_loop_now >= 0))
        return;
    for (i = 0; i < ARR_LOOPS && sloops.loop[i].bars; i++)
        ;
    if (i < ARR_LOOPS)
        app_loop_set(i);
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

/* the song from row r (PLAY on SONG: row 1; SEL's PLAY FROM ROW: the cursor's): stopped, at once (row 1 with its
 * Intro when on); while a loop or FREE plays, or the song, from row r at the next bar line */
static void song_play_from(int r)
{
    const song_t *g = csong();
    char t[12];
    if (r >= g->nrows)
        r = 0;
    if (app.mode != ARR_SONG)
        mode_set(ARR_SONG);
    arr_mode_song(g, r);
    ui.row_follow = 1;
    if (arr_playing) {
        itoa_u((uint32_t)r + 1u, cat_s(t, "ROW "));
        say("NEXT", t);
    } else
        arr_play(1);
}
static void song_rec_toggle(void);
/* REC with a lead track selected (SEQ, EDIT): armed in a song (it records while the song's rows play, from the next bar
 * line, counted in; stopped, from PLAY, which starts at SONG's cursor row: a punch in); stopped in FREE / LOOP, the song
 * (MODE SONG) first; playing a loop or FREE not (leads play only in a song). The song stopping disarms it (ui_frame: the
 * next PLAY would record over the song again) */
static void lead_rec_button(int on)
{
    char a[16];
    if (on && app.mode != ARR_SONG && !arr_playing && csong()->nrows)
        mode_set(ARR_SONG);                      /* stopped: the song it records into (PLAY starts it) */
    if (on && app.mode != ARR_SONG) {
        say("LEADS RECORD", "IN A SONG");
        return;
    }
    arr_record(sel_trk(), on ? ARR_REC_BAR : 0);   /* (playing: a count-in to the next bar line; stopped: from PLAY) */
    ui.rec_want = (uint8_t)on;
    ui.rec_seen = 0;
    if (!on)
        say("RECORD OFF", 0);
    else {
        cat_s(cat_s(a, "RECORD "), TRACK_NAME[sel_trk()]);
        say(a, "INTO THE SONG");                 /* (when: the screen's line, "starts at the next bar") */
    }
}
static void button_down(int b)                   /* PLAY: at once; REC: at its release (button_up) */
{
    switch (b) {
    case B_PLAY:
        if (ui.msg_first)                        /* the section asked for first: now playing */
            ui.msg_until = plat_ms();
        ui.msg_first = 0;
        if (song_page() && !(arr_playing && app.mode == ARR_SONG)) {   /* on SONG: the song, from row 1 */
            song_play_from(0);
            break;
        }
        if (!arr_playing && app.mode == ARR_SONG) {   /* else SONG from row 1; a lead armed: from SONG's cursor row
                                                       * (punch in: the row its REC screen rings, from its downbeat) */
            arr_mode_song(csong(), app.lead && (arr_recording || ui.rec_want) ? lead_row() : 0);
            ui.row_follow = 1;
        }
        arr_play(!arr_playing);
        break;
    case B_REC:
    {
        int on = !arr_recording, s = eslot(), r = key_role(), rp = 0;   /* the state asked for (the engine's: a block
                                                                         * later) */
        int st = songs.song[proj.song].style[s];
        char a[24], t[16];
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
        if (app.lead) {                          /* a lead track: into the song's rows as they play */
            lead_rec_button(on);
            break;
        }
        if (on)
            slot_own(s);                         /* recording into a style's section: a user pattern first */
        ui.follow_hint = 0;
        if (on && (rp = rec_as_played(s, proj.trk)) == 1)   /* (before the first note) */
            cat_s(cat_s(a, TRACK_NAME[proj.trk]), ": AS PLAYED");
        else if (rp == 2 && follows_told(proj.trk))
            cat_s(cat_s(a, TRACK_NAME[proj.trk]), " FOLLOWS CHORDS");
        else
            cat_s(cat_s(a, "RECORD "), TRACK_NAME[proj.trk]);
        arr_record(proj.trk, on ? ARR_REC_BAR : 0);   /* from the next bar line (stopped: from PLAY) */
        ui.rec_want = (uint8_t)on;
        ui.rec_seen = 0;
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
static void slots_open(int from_list);
static void slots_back(void);
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
    case B_REC:                                  /* (on release, at once: holding it shows its card, starts nothing) */
        button_down(B_REC);
        break;
    case B_SEL:                                  /* a question: yes; a list: its row; else the list (SLOTS just after */
        if (ui.ask)                              /* a NO FREE SLOT) */
            ask_end(1);
        else if (lst.open)
            list_sel();
        else if (plat_ms() < ui.slots_until) {
            ui.slots_until = 0;
            slots_open(0);
        } else if (trk_rec_shown() && !app.lead && loop_longer())
            bars_to_loop();                      /* REC's screen: "2-bar pattern . loop 4 bars . SEL: 4 bars" */
        else
            list_open();
        break;
    case B_HOME:                                 /* HOME on release: no; back from a list (SLOTS: to its list); else the page */
        if (ui.ask)
            ask_end(0);
        else if (lst.open && lst.slots)
            slots_back();
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
    arr_record(sel_trk(), 0);
    ui.rec_want = 0;
    if (unsaved() && save_all() != 0) {
        say("NOT SAVED", "SONG KEPT");
        if (rec)
            arr_record(sel_trk(), ARR_REC_BAR);  /* as it was */
        return;
    }
    proj.song = (uint8_t)s;
    app.row = 0;
    loops_load();                                /* its own loops (stopped: the engine reads them from the next PLAY) */
    song_load();
    leads_load();
    undo_reset();                                /* undo is the song's: from its load */
    mark_dirty();
    save_all();                                  /* the song picked, kept now: a power-off comes back on it (stopped:
                                                  * the save before already paused nothing that plays) */
    itoa_u((uint32_t)s + 1u, cat_s(t, "SONG "));
    say(t, 0);
}

/* ------------------------------------------------------ the song's pages --- */
/* the pattern d steps on from st for slot s: the user patterns P01..P16, then the styles by name, each its
 * pattern for that section (Main A's knob: Pop's Main A, Funk's Main A, ...) */
static int style_step(int s, int st, int d)
{
    int si = style_index(st), i = si >= 0 ? ARR_USER_PATTERNS + si : st < ARR_USER_PATTERNS ? st : 0, was = i;
    int top = ARR_USER_PATTERNS + styles_n() - 1;
    i = clampi(i + d, 0, top);
    while (i != was && i < ARR_USER_PATTERNS && slot_held(i))   /* (a slot holding lead notes or loops: passed over) */
        i = i + (d > 0 ? 1 : -1) < 0 ? was : i + (d > 0 ? 1 : -1);
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
    if (st >= ARR_USER_PATTERNS && to < ARR_USER_PATTERNS)
        uj_pat_touch(s);                         /* (undo: a user pattern picked for a style's section: its notes kept) */
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
    if (a != main_a && style_index(a) >= 0)      /* another style: its tempo, unless the song's was set by hand */
        style_tempo_follow(style_index(a));
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
    undo_reset();
}
/* a restore over USB (backup.c) wrote the project, the songs and the slots: all of it from the flash again, as at
 * power-on (stopped: the restore is refused while playing) */
void app_restored(void)
{
    project_load();
    project_apply();
    app_start();
    ui_init();
}
/* a backup over USB: stopped, what is not saved yet saved first (the flash is what it sends); the autosave held
 * while it streams (its objects as they were when it started) */
static uint8_t save_hold;
void app_flush(void)
{
    if (!arr_playing && unsaved() && save_all() == 0)
        app.save_err = 0;
}
void app_hold_saves(int on) { save_hold = (uint8_t)(on != 0); }
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
    undo_hint("STYLE");
    for (int s = 0; s < ARR_NSECT; s++)
        if (g->style[s] >= ARR_USER_PATTERNS)
            slot_style(s, style_number(n, s));
    g->sty = (uint16_t)style_number(n, 0);       /* (also with every section on a user pattern) */
    app.songs_dirty = 1;
    app.chg++;
    app.gen++;
    style_tempo_follow(n);                       /* its tempo, unless the song's was set by hand */
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
            proj.click = (uint8_t)clampi(proj.click + d, ARR_CLICK_OFF, ARR_CLICK_ON);
        mark_dirty();
        break;
    case P_LOOP: {                               /* ROOT, TYPE ("·" one below maj: loop_unwrite), BASS, STEP */
        int l = app.loop % ARR_LOOPS, has, b = app.cbeat;
        chord_t c = chord_shift(cursor_chord(&sloops.loop[l], b, &has), xpose_now());
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
                loop_unwrite(&sloops.loop[l], b);
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
        loop_t *l = &sloops.loop[app.loop % ARR_LOOPS];
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
    ui.msg_until = plat_ms();                    /* the footer says SEL: YES, HOME: NO at once (not a message before) */
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
    loop_name_l(l, cat_s(b, "LOAD "));
    undo_hint(b);
    songs_write(1);
    loop_from_template(&sloops.loop[l], t);
    songs_write(0);
    loop_name_l(l, cat_s(b, "INTO "));
    cat_s(a, TEMPLATES[t].name);
    say(upcase(a), b);
}
static void act_load_progression(void)
{
    char q[32];
    if (!sloops.loop[ui.tgt.loop].bars) {
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
    undo_hint("CLEAR LOOP");
    songs_write(1);
    loop_clear(&sloops.loop[l], l);
    songs_write(0);
    loop_name_l(l, t);
    say("CLEARED", t);
}
static void act_clear_loop(void)
{
    char t[8];
    if (!sloops.loop[ui.tgt.loop].bars) {
        loop_name_l(ui.tgt.loop, t);
        say(t, "IS EMPTY");
    } else
        ask_loop("CLEAR ", ui.tgt.loop, do_clear_loop);
}
/* INSERT ROW, DELETE ROW: at the SONG screen's row (row: app.row, or the one a question asked about); stopped only (the
 * rows after the one playing would move). A row with lead notes asks first ("ROW 3 HAS LEAD NOTES: DELETE?") */
static int row_change(int ins, int row)
{
    song_t *g = csong();
    if (arr_playing) {
        say(ins ? "INSERT ROW" : "DELETE ROW", "STOP FIRST");
        return -1;
    }
    if (ins && g->nrows >= ARR_ROWS) {
        say("SONG FULL", "32 ROWS");
        return -1;
    }
    if (!ins && row >= g->nrows) {
        say("DELETE ROW", "NO ROW HERE");
        return -1;
    }
    return 0;
}
static void row_do(int ins, int row);
static void do_delete_row(void)
{
    if (row_change(0, ui.tgt.lrow) == 0)
        row_do(0, ui.tgt.lrow);
}
static void act_row(int ins)
{
    char q[32];
    if (row_change(ins, app.row) != 0)
        return;
    if (!ins && leads_count(&leads, app.row, -1)) {
        ui.tgt.lrow = app.row;
        cat_s(itoa_u(app.row + 1u, cat_s(q, "ROW ")), " HAS LEAD NOTES: DELETE?");
        ask(q, do_delete_row);
        return;
    }
    row_do(ins, app.row);
}
static void row_do(int ins, int row)
{
    song_t *g = csong();
    char t[12];
    itoa_u((uint32_t)row + 1u, cat_s(t, "ROW "));
    undo_hint(ins ? "INSERT ROW" : "DELETE ROW");
    {
        int r = ins ? (row > g->nrows ? g->nrows : row) : row, before = leads.n;
        songs_write(1);                          /* (the rows and the lead notes in one write: their rows move together) */
        if (ins) {
            row_insert(g, r);
            leads_row_insert(&leads, r);         /* (the copied row gets no lead notes) */
        } else {
            row_delete(g, r);
            leads_row_delete(&leads, r);         /* (its notes go) */
        }
        leads.gen++;
        songs_write(0);
        if (before)
            lead_changed(0, before);
    }
    if (app.row >= ARR_ROWS || (!ins && app.row && app.row >= g->nrows))
        app.row = (uint8_t)(g->nrows ? g->nrows - 1 : 0);
    say(ins ? "INSERTED" : "DELETED", t);
}
/* CLEAR SONG: its rows and settings (its patterns, style, tempo, transpose and mutes kept, the loops too) */
static void do_clear_song(void)
{
    song_t *g = csong();
    uint8_t mute = g->mute, tempo = g->tempo, fl = g->flags;
    int8_t xp = g->transpose;
    int before = leads.n;
    undo_hint("CLEAR SONG");
    songs_write(1);                              /* (the engine may be playing its rows) */
    loops_song_clear(g);                         /* (its style and its leads' sounds kept) */
    leads_erase(&leads, -1, -1);                 /* the lead notes go with their rows */
    leads.gen++;
    g->mute = mute;
    g->tempo = tempo;
    g->flags = fl;                               /* (a tempo set by hand stays the song's) */
    g->transpose = xp;
    songs_write(0);
    if (before)
        lead_changed(0, before);
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
/* COPY SONG TO: the song (its rows, settings, styles, loops) into another; its user patterns into ones of the
 * target's; its loops into the target's slot (a free one; the defaults: none, the target's let go) */
static void do_copy_song(void)
{
    song_t *g = csong();
    int to = ui.tgt.song % ARR_SONGS, deflt = loops_is_default(sloops.loop);
    song_t *d = &songs.song[to];
    uint16_t st[ARR_NSECT];
    uint32_t mine = slots_reserved();
    int8_t lp = -1;
    char a[16];
    if (arr_playing) {                           /* (PLAY during the question) a flash write pauses the audio */
        say("COPY", "STOP FIRST");
        return;
    }
    edits_flush();
    if (copy_patterns_pick(to, st) != 0) {
        say_noslot("NOT COPIED", "NO FREE SLOT");
        return;
    }
    for (int i = 0; i < ARR_NSECT; i++)
        if (st[i] < ARR_USER_PATTERNS)
            mine |= 1u << st[i];
    if (!deflt && (lp = (int8_t)project_loops_slot(to)) < 0 &&
        (lp = (int8_t)project_slot_free(&songs, mine, &scratch)) < 0) {   /* the loops' slot: the target's, else free */
        say_noslot("NOT COPIED", "NO FREE SLOT");
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
    if (project_leads_free(to) != 0) {          /* the target's lead notes go with its rows (the source's: not copied) */
        say("FLASH ERROR", "song unchanged");
        return;
    }
    if (deflt ? project_loops_free(to) != 0 : project_loops_save(to, &sloops, &lp) != 0) {
        say("FLASH ERROR", "song unchanged");    /* (the header in RAM says `to` now: the song's next save sets it back) */
        return;
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
/* ERASE LD1 IN ROW n (the row playing, else SONG's cursor), ERASE LD1 (the whole song's): the lead's notes, asked
 * first. tgt.lrow 255: every row */
static void lead_erase_what(char *t)            /* "LD1 IN ROW 3", "LD1" */
{
    char *e = cat_s(t, TRACK_NAME[ARR_LD1 + ui.tgt.ltrk % ARR_LEADS]);
    if (ui.tgt.lrow != 255u)
        itoa_u(ui.tgt.lrow + 1u, cat_s(e, " IN ROW "));
}
static void do_erase_lead(void)
{
    int before = leads.n, n;
    char t[24];
    cat_s(cat_s(t, "ERASE "), TRACK_NAME[ARR_LD1 + ui.tgt.ltrk % ARR_LEADS]);
    undo_hint(t);
    lead_write(1);
    n = leads_erase(&leads, ui.tgt.lrow == 255u ? -1 : ui.tgt.lrow, ui.tgt.ltrk % ARR_LEADS);
    lead_write(0);
    if (n)
        lead_changed(0, before);
    lead_erase_what(t);
    say("ERASED", t);
}
static void act_erase_lead(int in_row)
{
    char q[32], t[24];
    ui.tgt.ltrk = (uint8_t)((app.lead ? app.lead - 1 : 0) % ARR_LEADS);
    ui.tgt.lrow = (uint8_t)(in_row ? lead_row() : 255);
    lead_erase_what(t);
    if (!leads_count(&leads, in_row ? ui.tgt.lrow : -1, ui.tgt.ltrk)) {
        say(t, "HAS NO NOTES");
        return;
    }
    cat_s(cat_s(cat_s(q, "ERASE "), t), "?");
    ask(q, do_erase_lead);
}
/* ERASE TRACK: the selected track's notes in the pattern playing (a style's section: a user pattern first) */
static void do_erase_track(void)
{
    int t = ui.tgt.trk & 7;
    if (eslot() != ui.tgt.slot) {                /* the engine erases in the section playing: another now */
        say("NOT ERASED", "SECTION CHANGED");
        return;
    }
    char h[16];
    edits_flush();
    cat_s(cat_s(h, "ERASE "), TRACK_NAME[t]);
    undo_hint(h);
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
    undo_hint("CLEAR PATTERN");
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
        undo_hint("BACK TO STYLE");
        slot_style(s, style_number(i, s));       /* (saved first; refused while playing with changes unsaved) */
        if (songs.song[proj.song].style[s] != st) {
            char b[24];
            style_name(songs.song[proj.song].style[s], b);
            say("BACK TO", b);
        }
    }
}
/* QUANTISE LD1 ROW 3: the lead's notes in the row (the playing one, else SONG's cursor's) to the GRID (SEQ's); one undo
 * step */
static const uint16_t GRID_TICKS[ARR_NQ] = {ARR_PPQ / 2, ARR_PPQ / 3, ARR_PPQ / 4, ARR_PPQ / 6, ARR_PPQ / 8, 1};
static void act_quant_lead(void)
{
    const song_t *g = csong();
    int k = (app.lead ? app.lead - 1 : 0) % ARR_LEADS, r = lead_row(), n, before = leads.n, from;
    uint32_t end = 0;
    char a[24], *e;
    e = cat_s(cat_s(a, TRACK_NAME[ARR_LD1 + k]), " ROW ");
    itoa_u((uint32_t)r + 1u, e);
    if (proj.quant % ARR_NQ == ARR_Q_OFF) {
        say("GRID IS OFF", "SEQ: GRID");
        return;
    }
    if (r >= g->nrows || !leads_count(&leads, r, k)) {
        say(a, "HAS NO NOTES");
        return;
    }
    {   /* the row's length: its loop's beats (an empty loop: a bar of 4), REPEAT times */
        const row_t *w = &g->row[r];
        int b = loop_len(&sloops.loop[w->loop % ARR_LOOPS]);
        end = (uint32_t)(b > 0 ? b : 4) * (w->rep ? w->rep : 1u) * ARR_PPQ;
    }
    undo_commit();                               /* (what came before: a step of its own) */
    from = leads.row0[r];
    lead_write(1);
    n = leads_quantise(&leads, r, k, GRID_TICKS[proj.quant % ARR_NQ], end);
    lead_write(0);
    if (!n) {
        say(a, "ON THE GRID");
        return;
    }
    lead_changed(from, before);
    e = cat_s(a + slen(a), " TO ");
    cat_s(e, GRID_NAME[proj.quant % ARR_NQ]);
    undo_hint("QUANTISE");
    undo_commit();                               /* one step */
    say("QUANTISED", a);
}

/* COPY LD1 TO ROW 3 (the row playing, else SONG's cursor's) from another row (the list's pick): that row's LD1 notes,
 * as played (their roots kept: they move with row 3's chords), in place of row 3's (asked first when it has some);
 * notes past row 3's end left out. One undo step */
static void do_copy_lead(void)
{
    const song_t *g = csong();
    int k = ui.tgt.ltrk % ARR_LEADS, from = ui.tgt.lfrom, to = ui.tgt.lrow, n, j, before = leads.n, put = 0;
    uint32_t end;
    char a[16], b[24];
    if (from >= g->nrows || to >= g->nrows || from == to)
        return;
    {   /* the target row's length: its loop's beats (an empty loop: a bar of 4), REPEAT times */
        const row_t *w = &g->row[to];
        int bt = loop_len(&sloops.loop[w->loop % ARR_LOOPS]);
        end = (uint32_t)(bt > 0 ? bt : 4) * (w->rep ? w->rep : 1u) * ARR_PPQ;
    }
    undo_commit();                               /* (what came before: a step of its own) */
    cat_s(cat_s(a, "COPY "), TRACK_NAME[ARR_LD1 + k]);
    undo_hint(a);
    lead_write(1);
    j = leads_erase(&leads, to, k);
    lead_write(0);
    if (j)
        lead_changed(0, before);
    n = leads_count(&leads, from, k);
    for (j = 0; j < n; j++) {                    /* the j-th note of the row (found again: inserts move the list) */
        lead_ev_t e;
        int c = 0, found = 0;
        for (uint32_t i = leads.row0[from]; i < leads.row0[from + 1] && i < leads.n; i++) {
            const lead_ev_t *x = lead_at_c(&leads, i);
            if (lead_trk(x) == k && c++ == j) {
                e = *x;
                found = 1;
                break;
            }
        }
        if (!found)
            break;
        if (lead_pos(&e) >= end)
            continue;                            /* (past the row's end: it would not play) */
        e.row = (uint8_t)to;
        if (lead_put(&e) < 0)
            break;
        put++;
    }
    undo_commit();                               /* one step */
    itoa_u((uint32_t)to + 1u, cat_s(itoa_u((uint32_t)from + 1u, cat_s(cat_s(b, TRACK_NAME[ARR_LD1 + k]), " ROW ")), " > ROW "));
    if (put || !n)
        say("COPIED", b);                        /* (else what lead_put said: NO FREE SLOT, LEAD FULL) */
}
static void act_copy_lead(void)
{
    const song_t *g = csong();
    char q[32], *e;
    ui.tgt.ltrk = (uint8_t)((app.lead ? app.lead - 1 : 0) % ARR_LEADS);
    ui.tgt.lrow = (uint8_t)lead_row();
    ui.tgt.lfrom = ui.pick_lrow;
    if (ui.tgt.lfrom >= g->nrows || ui.tgt.lfrom == ui.tgt.lrow)
        return;
    if (!leads_count(&leads, ui.tgt.lfrom, ui.tgt.ltrk)) {
        e = cat_s(cat_s(q, TRACK_NAME[ARR_LD1 + ui.tgt.ltrk]), " ROW ");
        itoa_u(ui.tgt.lfrom + 1u, e);
        say(q, "HAS NO NOTES");
        return;
    }
    if (!leads_count(&leads, ui.tgt.lrow, ui.tgt.ltrk)) {
        do_copy_lead();
        return;
    }
    e = cat_s(cat_s(cat_s(q, "REPLACE "), TRACK_NAME[ARR_LD1 + ui.tgt.ltrk]), " IN ROW ");
    cat_s(itoa_u(ui.tgt.lrow + 1u, e), "?");
    ask(q, do_copy_lead);
}

/* ---- SLOTS: what fills the 16 shared slots, and FREE one */
typedef struct {
    uint8_t kind;                                /* SLOT_NONE (free), SLOT_PATTERN, SLOT_LEAD, SLOT_LOOPS */
    uint8_t song, part;                          /* a lead part's song and part, a song's loops' song */
    uint8_t plays;                               /* a pattern: the songs playing it (bit n) */
    int8_t sect;                                 /* ... the section of the song selected playing it, -1 none */
} slot_who_t;
/* slot i: the song's own (in RAM: its lead parts' and loops' slots, taken or saved), the songs' patterns, else as stored
 * (a pattern no song plays, with notes; a lead part or loops with notes); else free */
static void slot_who(int i, slot_who_t *w)
{
    slot_info_t si;
    memset(w, 0, sizeof *w);
    w->sect = -1;
    for (int k = 0; k < LEAD_PARTS; k++)
        if (app.lslot[k] == i) {
            w->kind = SLOT_LEAD;
            w->song = (uint8_t)proj.song;
            w->part = (uint8_t)k;
            return;
        }
    if (app.lpslot == i) {
        w->kind = SLOT_LOOPS;
        w->song = (uint8_t)proj.song;
        return;
    }
    for (int n = 0; n < ARR_SONGS; n++)
        for (int k = 0; k < ARR_NSECT; k++)
            if (songs.song[n].style[k] == i) {
                w->plays |= (uint8_t)(1u << n);
                if (n == proj.song && w->sect < 0)
                    w->sect = (int8_t)k;
            }
    if (w->plays) {
        w->kind = SLOT_PATTERN;
        return;
    }
    project_slot_info(i, &si, &scratch);
    if ((si.kind == SLOT_LEAD || si.kind == SLOT_LOOPS || si.kind == SLOT_PATTERN) && si.n) {
        w->kind = si.kind;
        w->song = si.song;
        w->part = si.part;
    }
}
static char *song_list(uint8_t plays, char *t)   /* "S1 S3" */
{
    int first = 1;
    *t = 0;
    for (int n = 0; n < ARR_SONGS; n++)
        if (plays >> n & 1u) {
            if (!first)
                *t++ = ' ';
            first = 0;
            *t++ = 'S';
            *t++ = (char)('1' + n);
            *t = 0;
        }
    return t;
}
static int songs_n(uint8_t plays)
{
    int k = 0;
    for (int n = 0; n < ARR_SONGS; n++)
        k += plays >> n & 1u;
    return k;
}
static void slot_text(int i, char *t)            /* "P03|Funk A|S1", "S2 leads 1/2", "S1 loops", "free" (the pattern's
                                                  * name and the section the song plays it as) */
{
    slot_who_t w;
    char *e;
    slot_who(i, &w);
    slot_kind[i] = w.kind;
    *t = 0;
    if (w.kind == SLOT_LEAD) {
        e = cat_s(t, "S");
        *e++ = (char)('1' + w.song);
        *e = 0;
        e = cat_s(e, " leads ");
        *e++ = (char)('1' + w.part);
        cat_s(e, "/2");
    } else if (w.kind == SLOT_LOOPS) {
        e = cat_s(t, "S");
        *e++ = (char)('1' + w.song);
        *e = 0;
        cat_s(e, " loops");
    } else if (w.kind == SLOT_PATTERN) {
        static const char *const SEC_SHORT[ARR_NSECT] = {" Intro", " A", " B", " Fill AB", " Fill BA", " End"};
        const char *nm = 0;
        char b[9], nb[24];
        style_name(i, t);                        /* "P03" */
        if (w.sect >= 0)
            nm = app.hdr[w.sect].name;
        else if (project_pattern_load(i, &scratch) == 0)
            nm = scratch.name;
        if (nm)
            pat_name8(nm, b);
        else
            b[0] = 0;
        e = t + slen(t);
        cat_s(cat_s(nb, b), b[0] && w.sect >= 0 ? SEC_SHORT[w.sect % ARR_NSECT] : "");   /* "Funk A", "VERSE B" */
        if (nb[0])
            e = cat_s(cat_s(e, "|"), nb);
        e = cat_s(e, "|");
        if (w.plays)
            song_list(w.plays, e);
        else
            cat_s(e, "no song");
    } else
        cat_s(t, "free");
}
static void slots_scan(void)
{
    for (int i = 0; i < ARR_USER_PATTERNS; i++)
        slot_text(i, slot_txt[i]);
}
/* SLOTS from a list (HOME: back to it), or from the hint after a NO FREE SLOT (HOME: the page) */
static void slots_open(int from_list)
{
    lst.back_sel = from_list ? lst.sel : 255u;
    lst.back_top = lst.top;
    lst.open = 1;
    lst.slots = 1;
    lst.sel = lst.top = 0;
    slots_scan();
}
static void slots_back(void)
{
    lst.slots = 0;
    if (lst.back_sel == 255u) {
        lst.open = 0;
        return;
    }
    lst.sel = lst.back_sel;
    lst.top = lst.back_top;
}
/* a slot freed: what NO FREE SLOT left playing but not kept takes it now (a section of a style changed: its own user
 * pattern; the song's loops changed: their slot), said in a, b */
static void slots_retry(char *a, char *b)
{
    const song_t *g = csong();
    for (int s = 0; s < ARR_NSECT; s++)
        if (g->style[s] >= ARR_USER_PATTERNS &&
            (app.edited[s] || (app.edit_pend >> s & 1u) || arr_dirty[s] != app.saved[s]) && slot_own(s) == 0 &&
            g->style[s] < ARR_USER_PATTERNS) {
            upcase(cat_s(cat_s(a, SECT_NAME[s + 1]), " KEPT"));
            pat_name(g->style[s], cat_s(b, "IN "));
            app.songs_dirty = 1;
            return;
        }
    if (app.lpslot < 0 && !loops_is_default(sloops.loop) && loops_slot_take() == 0 && app.lpslot >= 0) {
        app.lpdirty = 1;
        cat_s(a, "LOOPS KEPT");
        itoa_u((uint32_t)app.lpslot + 1u, cat_s(b, "IN SLOT "));
    }
}
/* FREE: slot ui.tgt.free let go. A pattern the songs play: those sections back to their song's style, the pattern
 * erased (not undone: its notes are gone); a lead part: its notes erased; a song's loops: back to the factory's (the
 * song selected's: an undo step; another song's: let go in the flash). Saved at once (the slot free for what needs it) */
static void do_free_slot(void)
{
    int i = ui.tgt.free % ARR_USER_PATTERNS;
    slot_who_t w;
    char a[16], b[24];
    if (arr_playing) {                           /* (PLAY during the question) a flash write pauses the audio */
        say("FREE", "STOP FIRST");
        return;
    }
    slot_who(i, &w);
    itoa_u((uint32_t)i + 1u, cat_s(a, "SLOT "));
    cat_s(b, "FREE NOW");
    if (w.kind == SLOT_PATTERN) {
        edits_flush();
        undo_commit();
        for (int n = 0; n < ARR_SONGS; n++) {
            song_t *g = &songs.song[n];
            for (int k = 0; k < ARR_NSECT; k++) {
                int to;
                if (g->style[k] != i)
                    continue;
                to = song_style_base(g->sty) == (int)g->sty ? g->sty + k : ARR_FACTORY0 + k;
                g->style[k] = (uint16_t)to;
                app.songs_dirty = 1;
                if (n == proj.song)
                    slot_load(k, to);
            }
        }
        pattern_empty(&scratch);
        project_pattern_save(i, &scratch);
        save_all_now();
        undo_sync();
        if (w.plays)
            cat_s(song_list(w.plays, b), " BACK TO STYLE");
        app.chg++;
        app.gen++;
    } else if (w.kind == SLOT_LEAD && w.song == proj.song) {
        int k = w.part % LEAD_PARTS;
        undo_hint(k ? "FREE LEADS 2" : "FREE LEADS 1");
        lead_write(1);
        leads_erase_at(&leads, (uint32_t)k * LEAD_PART_NOTES, leads.part[k].n);
        lead_write(0);
        if (k == 0 && leads.part[0].n) {         /* part 1's notes are part 0's now: its slot holds them, this one goes */
            app.lslot[0] = app.lslot[1];
            app.lslot[1] = (int8_t)i;
        }
        app.ldirty = (1u << LEAD_PARTS) - 1u;
        save_all();
        cat_s(b, "LEAD NOTES ERASED");
    } else if (w.kind == SLOT_LOOPS && w.song == proj.song) {
        undo_hint("LOOPS FACTORY");
        songs_write(1);
        loops_default(sloops.loop);
        songs_write(0);
        save_all();
        cat_s(b, "LOOPS: FACTORY");
    } else if (w.kind == SLOT_LEAD || w.kind == SLOT_LOOPS) {
        if (project_slot_clear(i) != 0) {
            say("NOT FREED", "FLASH ERROR");
            return;
        }
    } else
        return;
    slots_retry(a, b);                           /* a change NO FREE SLOT left unkept: kept now */
    lst.open = 1;                                /* (the question closed the list: SLOTS again, as it is now) */
    lst.slots = 1;
    slots_scan();
    say(a, b);
}
static void act_free_slot(int i)
{
    slot_who_t w;
    char q[40], *e;
    if (slot_kind[i % ARR_USER_PATTERNS] == SLOT_NONE)
        return;
    if (arr_playing) {
        say("FREE", "STOP FIRST");
        return;
    }
    ui.tgt.free = (uint8_t)i;
    slot_who(i, &w);
    if (w.kind == SLOT_PATTERN) {
        style_name(i, cat_s(q, "FREE "));
        e = cat_s(q + slen(q), "? ");
        if (songs_n(w.plays) > 1)
            cat_s(itoa_u((uint32_t)songs_n(w.plays), e), " SONGS BACK TO STYLE");
        else if (w.plays)
            cat_s(song_list(w.plays, e), " BACK TO STYLE");
        else
            cat_s(e, "NOTES ERASED");
    } else if (w.kind == SLOT_LEAD) {
        e = cat_s(q, "ERASE S");
        *e++ = (char)('1' + w.song);
        e = cat_s(e, " LEADS ");
        *e++ = (char)('1' + w.part);
        cat_s(e, "/2?");
    } else if (w.kind == SLOT_LOOPS) {
        e = cat_s(q, "S");
        *e++ = (char)('1' + w.song);
        cat_s(e, "'S LOOPS BACK TO FACTORY?");
    } else {
        slots_scan();                            /* (free meanwhile) */
        return;
    }
    ask(q, do_free_slot);
}
static void action_run(int a)
{
    ui.tgt.loop = app.loop;                      /* what it acts on: now (a question's yes: still this) */
    ui.tgt.prog = ui.pick_prog;
    ui.tgt.song = ui.pick_song;
    ui.tgt.trk = proj.trk;                       /* (a pattern's track: a lead's actions take their own) */
    ui.tgt.slot = (uint8_t)eslot();
    switch (a) {
    case A_LOAD_PROG: act_load_progression(); break;
    case A_CLEAR_LOOP: act_clear_loop(); break;
    case A_PLAY_FROM: song_play_from(app.row); break;
    case A_INSERT_ROW: act_row(1); break;
    case A_DELETE_ROW: act_row(0); break;
    case A_CLEAR_SONG: act_clear_song(); break;
    case A_COPY_SONG: act_copy_song(); break;
    case A_ERASE_TRACK: act_erase_track(); break;
    case A_CLEAR_PATTERN: act_clear_pattern(); break;
    case A_BACK_TO_STYLE: act_back_to_style(); break;
    case A_ERASE_LEAD_ROW: act_erase_lead(1); break;
    case A_ERASE_LEAD: act_erase_lead(0); break;
    case A_QUANT_LEAD_ROW: act_quant_lead(); break;
    case A_COPY_LEAD: act_copy_lead(); break;
    case A_MATCH_BARS: bars_to_loop(); break;
    case A_SLOTS:
        slots_open(1);
        return;                                  /* (the list stays: SLOTS in it) */
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
    undo_hint("COPY LOOP");
    songs_write(1);
    sloops.loop[ui.copy_to % ARR_LOOPS] = sloops.loop[ui.copy_src % ARR_LOOPS];
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
    undo_commit();                               /* (a copy into the flash: not undone; the slots given it followed) */
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
    undo_sync();
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
        } else if (sloops.loop[n].bars)
            ask_loop("REPLACE ", n, do_copy_loop);
        else
            do_copy_loop();
    } else if (ui.page == P_PATTERN || ui.page == P_PATSET) {
        if (arr_playing) {                       /* a flash write pauses the audio */
            say("COPY", "STOP FIRST");
            return;
        }
        if (slot_held(n)) {                      /* a slot holding lead notes or a song's loops: never a pattern's */
            slot_info_t si;
            pat_name(n, q);
            project_slot_info(n, &si, &scratch);
            say(q, si.kind == SLOT_LOOPS || n == app.lpslot ? "HOLDS LOOPS" : "HOLDS LEAD NOTES");
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
/* HOME's KNOB 4, LOOP: "-" (FREE: the band follows your chords) or L1..L16 (that loop goes round; while playing from
 * the next bar line, as HOME + a white key). In SONG it shows "Song": a turn leaves the song, from the loop it plays
 * (stopped: the cursor's row's) */
static void loop_knob(int32_t e)
{
    const song_t *g = csong();
    int cur = app.mode == ARR_LOOP ? app.loop + 1 : 0, to;
    if (app.mode == ARR_SONG)
        cur = 1 + (arr_playing && arr_loop_now >= 0 && arr_loop_now < ARR_LOOPS ? arr_loop_now
                   : g->nrows ? g->row[app.row < g->nrows ? app.row : 0].loop % ARR_LOOPS : app.loop);
    to = clampi(cur + accel(EN_K4, e), 0, ARR_LOOPS);
    if (to == cur && app.mode != ARR_SONG)
        return;
    if (!to) {
        mode_set(ARR_FREE);
        return;
    }
    app_loop_set(to - 1);
    mode_set(ARR_LOOP);
}
/* HOME's knobs: the song's style, tempo, TRANSPOSE; LOOP */
static void home_knob(int k, int32_t e)
{
    song_t *g = csong();
    switch (k) {
    case 0: style_turn(accel(EN_K1, e)); break;
    case 1: set_tempo_hand(g->tempo + accel(EN_K2, e)); break;
    case 2: set_transpose(g->transpose + e); break;   /* (no acceleration: 12 steps) */
    case 3: loop_knob(e); break;
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
/* BARS longer: the new bars repeat the pattern's, each track by its own (the fewest bars its notes repeat in over the
 * old bars: a 1-bar groove repeats bar 1, a 2-bar line bars 1-2, bar 3 of 1 2 1 is bar 1 again), so the drums and the
 * comping go on. A track with notes there already (kept past a shortened length) keeps them. Into p (its bars `to`,
 * its notes as they were at `from` bars); the notes added, -1 when they don't fit (ARR_NEV: none added) */
static int bar_same(const pattern_t *p, const arr_trk_t *t, uint32_t a, uint32_t b, uint32_t blen)
{
    uint32_t e = (uint32_t)t->first + t->count, na = 0, nb = 0;
    if (e > p->nev)
        e = p->nev;
    for (uint32_t i = t->first; i < e; i++) {
        const arr_ev_t *x = &p->ev[i];
        uint32_t m;
        if (x->tick / blen == b)
            nb++;
        if (x->tick / blen != a)
            continue;
        na++;
        for (m = t->first; m < e; m++) {         /* its twin in bar b */
            const arr_ev_t *y = &p->ev[m];
            if (y->tick / blen == b && y->tick % blen == x->tick % blen && y->note == x->note && y->vel == x->vel)
                break;                           /* (lengths aside: a copy at the end was cut to it) */
        }
        if (m == e)
            return 0;
    }
    return na == nb;
}
static void pat_insert(pattern_t *p, int trk, uint32_t tick, uint32_t len, int note, int vel)
{
    arr_trk_t *t = &p->trk[trk];
    uint32_t ins = t->count ? t->first : p->nev, e = t->count ? (uint32_t)t->first + t->count : p->nev;
    for (; ins < e && p->ev[ins].tick <= tick; ins++)
        ;
    for (uint32_t i = p->nev; i > ins; i--)
        p->ev[i] = p->ev[i - 1];
    p->ev[ins].tick = (uint16_t)tick;
    p->ev[ins].len = (uint16_t)len;
    p->ev[ins].note = (uint8_t)note;
    p->ev[ins].vel = (uint8_t)vel;
    p->nev++;
    for (int u = 0; u < ARR_TRACKS; u++)
        if (u != trk && p->trk[u].first >= ins)
            p->trk[u].first++;
    if (!t->count)
        t->first = (uint16_t)ins;
    t->count++;
}
static int pattern_repeat(pattern_t *p, int from, int to)
{
    uint32_t blen = (uint32_t)(p->beats ? p->beats : 4) * ARR_PPQ, lo = (uint32_t)from * blen, hi = (uint32_t)to * blen;
    int per[ARR_TRACKS], add = 0;
    if (from < 1 || to <= from)
        return 0;
    for (int t = 0; t < ARR_TRACKS; t++) {
        const arr_trk_t *k = &p->trk[t];
        uint32_t e = (uint32_t)k->first + k->count, src = 0, there = 0;
        per[t] = 0;
        if (e > p->nev)
            e = p->nev;
        for (uint32_t i = k->first; i < e; i++) {
            src += p->ev[i].tick < lo;
            there += p->ev[i].tick >= lo && p->ev[i].tick < hi;
        }
        if (!src || there)
            continue;                            /* nothing to repeat, or its notes there come back */
        for (per[t] = 1; per[t] < from; per[t]++) {
            int j;
            for (j = per[t]; j < from && bar_same(p, k, (uint32_t)j, (uint32_t)(j - per[t]), blen); j++)
                ;
            if (j == from)
                break;
        }
        for (int b = from; b < to; b++)
            for (uint32_t i = k->first; i < e; i++)
                add += p->ev[i].tick / blen == (uint32_t)(b % per[t]);
    }
    if (p->nev + add > ARR_NEV)
        return -1;
    for (int t = 0; t < ARR_TRACKS; t++)
        for (int b = from; b < to && per[t]; b++) {
            uint32_t sb = (uint32_t)(b % per[t]);
            for (uint32_t c = 0; c < p->trk[t].count; c++) {   /* (the source bar's notes: before the new ones) */
                const arr_ev_t *x = &p->ev[p->trk[t].first + c];
                uint32_t tick = (uint32_t)b * blen + x->tick % blen, len = x->len;
                if (x->tick >= lo || x->tick / blen != sb)
                    continue;
                if (len > hi - tick)
                    len = hi - tick;
                pat_insert(p, t, tick, len, x->note, x->vel);
            }
        }
    return add;
}
/* BARS (Pattern setup's KNOB 1, SEL's BARS AS THE LOOP): the section's pattern `to` bars long, stored at once (longer:
 * its bars repeated into the new ones, pattern_repeat; the engine keeps it in step with the loop, arr.h) */
static void bars_set(int s, int to)
{
    to = clampi(to, 1, ARR_MAXBARS);
    if (to == app.hdr[s].bars)
        return;
    app.hdr[s].bars = (uint8_t)to;
    edited(s);                                   /* (a style's section: a user pattern first) */
    for (int tries = 0; tries < 8; tries++) {
        uint16_t d = arr_pattern_copy(s, &scratch);
        int from = scratch.bars, r = 0;
        edit_apply(s, &scratch);
        if (scratch.bars > from && (r = pattern_repeat(&scratch, from, scratch.bars)) < 0) {
            char a[12];
            itoa_u((uint32_t)to, cat_s(a, "BARS "));
            say(a, "FULL: NOT REPEATED");
        }
        if (arr_store_pattern(s, &scratch, d)) {
            app.edit_pend &= (uint8_t)~(1u << s);
            app.edited[s] = 1;
            app.gen++;
            break;
        }
    }
}
/* the pattern as long as the loop it plays with (REC's screen: SEL; the list's BARS AS THE LOOP) */
static void bars_to_loop(void)
{
    int n = loop_longer();
    char a[12];
    if (!n)
        return;
    bars_set(eslot(), n);
    itoa_u((uint32_t)n, cat_s(a, "BARS "));
    say(a, "AS THE LOOP");
}
#define MUTE_BURST_MS 150u                       /* detents closer than this: one turn */
/* a track muted / unmuted: the song's (every section), live; not an edit of the pattern (a style's section stays
 * the style's). In SONG by hand only, until the next row (the rows' mutes are theirs) */
static void mute_toggle(int trk)
{
    if (trk >= ARR_TRACKS && app.mode != ARR_SONG) {   /* a lead: in a song only (its rows' mutes, by hand) */
        say("LEADS PLAY", "IN A SONG");
        return;
    }
    if (app.mode == ARR_SONG) {
        int t = trk < ARR_ALLTRK ? trk : 0, on = !(arr_mutes >> t & 1u);
        song_t *g = csong();
        arr_track_mute(t, on);
        if (ui.song_rec && arr_playing && arr_loop_now >= 0 && arr_row < g->nrows) {   /* REC: into the row playing */
            row_t *w = &g->row[arr_row];
            songs_write(1);
            if (t >= ARR_TRACKS)
                w->lmute = (uint8_t)((w->lmute & ~(1u << (t - ARR_TRACKS))) | (uint32_t)on << (t - ARR_TRACKS));
            else
                w->mute = (uint8_t)((w->mute & ~(1u << t)) | (uint32_t)on << t);
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
/* a lead track's sound or mixer setting moved (the song's: saved with the songs), to the engine's copy */
static void lsnd_set(uint8_t *v, int to, int lo, int hi)
{
    to = clampi(to, lo, hi);
    if (*v == (uint8_t)to)
        return;
    *v = (uint8_t)to;
    lead_sounds_sync();
    song_touched();
}
/* EDIT's and the Mixer's knobs with a lead selected: PROGRAM, VELOCITY; CHANNEL; VOLUME, PAN, REVERB, CHORUS; BRIGHT, RESO. Its type
 * (as played), range (all) and high key (off) are fixed: said */
static void lead_knob(int k, int32_t e)
{
    lead_trk_t *l = sel_lead();
    int d = e > 0 ? 1 : -1;
    switch (ui.page) {
    case P_TRACK:
        if (k == 0) {
            if ((l->chan & 15u) != 9u)
                lsnd_set(&l->program, l->program + accel(EN_K1, e), 0, 127);
        } else if (k == 3)                       /* VELOCITY: the notes recorded from now on */
            lsnd_set(&l->vel, l->vel + accel(EN_K4, e), 1, 127);
        else
            say(TRACK_NAME[sel_trk()], "AS PLAYED, ALL KEYS");
        break;
    case P_TRACK2:
        if (k == 1)
            lsnd_set(&l->chan, l->chan + d, 0, ARR_CHAN_MAX);   /* (16 is the click's) */
        else if (k == 0)
            say(TRACK_NAME[sel_trk()], "AS PLAYED, ALL KEYS");
        break;
    case P_MIXER: {
        uint8_t *v = k == 0 ? &l->vol : k == 1 ? &l->pan : k == 2 ? &l->rev : &l->cho;
        lsnd_set(v, *v + accel(EN_K1 + k, e), 0, 127);
        break;
    }
    case P_TONE:                                 /* brightness, resonance: stored XOR 64 (0 neutral) */
        if (k < 2) {
            uint8_t *v = k == 0 ? &l->bright : &l->reso;
            int to = clampi((*v ^ 64) + accel(EN_K1 + k, e), 0, 127);
            lsnd_set(v, to ^ 64, 0, 127);
        }
        break;
    default: break;
    }
}
static void edit_knob(int k, int32_t e)
{
    int s = eslot(), d = e > 0 ? 1 : -1;
    arr_trk_t *t = &app.trk[s][proj.trk];
    if (app.lead && (ui.page == P_TRACK || ui.page == P_TRACK2 || ui.page == P_MIXER || ui.page == P_TONE)) {
        lead_knob(k, e);
        return;
    }
    switch (ui.page) {
    case P_PATTERN:
        if (k == 0)
            grid_turn(e);                        /* the record grid */
        else if (k == 3)
        {                                        /* a turn, either way: muted / plays; once a burst */
            uint32_t now = plat_ms();
            if (now - ui.mute_t >= MUTE_BURST_MS)
                mute_toggle(sel_trk());
            ui.mute_t = now;
        }
        break;
    case P_PATSET:
        if (k == 0)
            bars_set(s, app.hdr[s].bars + d);   /* (longer: the bars repeated into the new ones) */
        else if (k == 1) {
            int to = clampi(app.hdr[s].beats + d, 2, 7);
            if (to != app.hdr[s].beats) {
                app.hdr[s].beats = (uint8_t)to;
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
            trk_set(&t->chan, t->chan + d, 0, ARR_CHAN_MAX);
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
        /* (the loop the screen shows: LOOP always, HOME in LOOP; the next one is the first detent's. HOME in FREE or
         * SONG shows none: the one last picked first) */
        int shown = app.mode == ARR_LOOP || (app.mode == ARR_FREE && ui.page != P_HOME);
        int n = shown ? clampi(app.loop + d, 0, ARR_LOOPS - 1) : app.loop;
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
    } else if (ui.page == P_TRACK && app.lead) {  /* (a lead's) */
        lead_trk_t *l = sel_lead();
        if ((l->chan & 15u) != 9u) {
            lsnd_set(&l->program, (l->program / 8 + d) * 8, 0, 127);
            touch(0);
        }
    } else if (ui.page == P_TRACK) {             /* a family of eight, as the GM module */
        arr_trk_t *t = &app.trk[eslot()][proj.trk];
        if ((t->chan & 15u) != 9u) {
            trk_set(&t->program, (t->program / 8 + d) * 8, 0, 127);
            touch(0);
        }
    }
}

/* ------------------------------------------------------------- lists --- */
/* SEL: the list, on its first action (PLAY FROM ROW, ERASE, COPY, SLOTS near at hand), as many of them as fit shown;
 * SELECT up to the knobs' rows. No actions: its first row */
static void list_open(void)
{
    int a;
    lst.slots = 0;
    lst.open = 1;
    lst.sel = lst.top = 0;
    list_build();
    if (!lst.n) {
        lst.open = 0;
        say("LIST", "NOTHING HERE");
        return;
    }
    for (a = 0; a < lst.n && lst.row[a].kind != LK_ACTION && lst.row[a].kind != LK_PICK && lst.row[a].kind != LK_SUB; a++)
        ;
    if (a >= lst.n)
        return;
    lst.sel = (uint8_t)a;
    lst.top = (uint8_t)(lst.n - a >= LIST_SHOWN ? a : lst.n > LIST_SHOWN ? lst.n - LIST_SHOWN : 0);
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
        else if (lst.row[lst.sel].id == A_COPY_LEAD) {   /* COPY LD1 TO ROW n: from another row, a step a detent */
            int nr = csong()->nrows, lr = lead_row(), r = ui.pick_lrow;
            if (nr > 1)
                for (int32_t i = e < 0 ? -e : e; i > 0; i--)
                    do
                        r = (r + d + nr) % nr;
                    while (r == lr);
            ui.pick_lrow = (uint8_t)r;
        } else {                                 /* COPY SONG TO: another song, a step a detent */
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
    else if (lst.row[lst.sel].kind == LK_SLOT)
        act_free_slot(lst.row[lst.sel].id);
    else if (lst.row[lst.sel].kind == LK_ACTION || lst.row[lst.sel].kind == LK_PICK || lst.row[lst.sel].kind == LK_SUB) {
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

static void do_undo(int redo);
static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e, ev[NE];
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        int hold = i != B_PLAY;                  /* (on release: unless a key, a knob, a card used it; REC too, so
                                                  * holding it for its card starts nothing) */
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.any_btn = 1;
            ui.card = 0;                         /* another button: the card goes, the ones held are used */
            ui.btn_used |= btn & ~m;
            ui.btn_used &= ~m;
            ui.btn_t[i] = plat_ms();
            if (i == B_HOME && ui.ask) {         /* a question: HOME pressed, held or not, is no (its release: nothing) */
                ask_end(0);
                ui.btn_used |= m;
                continue;
            }
            if ((i == B_REC || i == B_PLAY) && (btn >> B_HOME & 1u) && !ui.ask) {
                do_undo(i == B_PLAY);            /* HOME + REC: undo, HOME + PLAY: redo (no take, no start) */
                ui.btn_used |= m | 1u << B_HOME;
                continue;
            }
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
            set_tempo_hand(csong()->tempo + accel(EN_SELECT, e));
        else if (lst.open)
            list_move(e);                        /* a list: up and down */
        else
            page_turn(e > 0 ? 1 : -1);
    }
    if ((e = ev[EN_ALGO]) != 0) {
        if (lst.open)
            list_change(e);                      /* a list: the value */
        else
            set_track(sel_trk() + (e > 0 ? 1 : -1));   /* (C4, LD1, LD2, D1, ...) */
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
        if (song_page() && g->nrows) {           /* SONG: the row's muted tracks' black keys glow dim (END: the last's) */
            const row_t *w = &g->row[app.row < g->nrows ? app.row : g->nrows - 1];
            for (i = 0; i < ARR_ALLTRK; i++)
                if ((i < ARR_TRACKS ? w->mute >> i : w->lmute >> (i - ARR_TRACKS)) & 1u)
                    dim |= 1u << BLACK_K[i];
        }
    }
    plat_glow(proj.leds == 1);
    plat_keys_dim(dim & ~k);
    plat_play_red(rec);
    plat_leds(b, k);
}

#define AUTOSAVE_QUIET 4000u
static void autosave(void)                       /* at rest only: a flash write pauses the audio */
{
    if (arr_playing || ui.btn || ui.keys || gm_voices || plat_ms() - ui.act_t < AUTOSAVE_QUIET || save_hold || !unsaved())
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

/* ------------------------------------------------------------- undo --- */
/* the steps: committed at a pause (UNDO_QUIET with nothing held, checked every 8th frame); not while a take records
 * (REC on while playing: the take is one step), except a pattern track's, a step each pass (from the bar recording
 * started on) */
static void undo_tick(void)
{
    int rec = arr_recording || ui.rec_want || ui.rec_chords || ui.take || ui.song_rec;
    if (rec && arr_playing) {
        if (arr_recording && !app.lead) {
            int b = clampi(app.hdr[eslot()].bars, 1, ARR_MAXBARS);
            uint16_t pass = (uint16_t)(arr_rec_bars / b);
            char t[12];
            cat_s(cat_s(t, "PASS "), TRACK_NAME[proj.trk & 7]);
            undo_hint(t);
            if (!ujs.passing) {
                ujs.passing = 1;
                ujs.pass = pass;
            } else if (pass != ujs.pass) {
                ujs.pass = pass;
                undo_commit();
            }
        }
        return;
    }
    ujs.passing = 0;
    if (ui.btn || ui.keys || plat_ms() - ui.act_t < UNDO_QUIET || (ui.frame & 7u))
        return;
    undo_commit();
}

/* what step k changed, in words, from its runs: "L9 CHORD", "ROW 3", "C1 SOUND", "MAIN A NOTES", "LEAD NOTES" */
enum { UN_ROW = 1, UN_NROWS, UN_STYLE, UN_STY, UN_TEMPO, UN_XPOSE, UN_MUTES, UN_LEAD, UN_SONG };
static int un_song(uint32_t o, int *idx)
{
    const uint32_t row = __builtin_offsetof(song_t, row), lead = __builtin_offsetof(song_t, lead);
    if (o >= lead) {
        *idx = (int)((o - lead) / sizeof(lead_trk_t));
        return UN_LEAD;
    }
    if (o >= row) {
        *idx = (int)((o - row) / sizeof(row_t));
        return UN_ROW;
    }
    if (o < __builtin_offsetof(song_t, sty)) {
        *idx = (int)(o / 2u);                    /* (a section's pattern) */
        return UN_STYLE;
    }
    if (o < __builtin_offsetof(song_t, nrows))
        return UN_STY;                           /* (the song's style) */
    if (o == __builtin_offsetof(song_t, nrows))
        return UN_NROWS;
    if (o == __builtin_offsetof(song_t, tempo) || o == __builtin_offsetof(song_t, flags))
        return UN_TEMPO;
    if (o == __builtin_offsetof(song_t, transpose))
        return UN_XPOSE;
    if (o == __builtin_offsetof(song_t, mute))
        return UN_MUTES;
    return UN_SONG;
}
static void undo_name(int k, char *t)
{
    int rg, ax, j, mask = uj_mask(&uj, k);
    uint32_t off, len;
    *t = 0;
    if (mask >> UR_PAT & 1u) {                   /* a pattern: its notes, else a track's sound / type / mixer */
        const uint32_t t0 = __builtin_offsetof(pattern_t, trk), t1 = __builtin_offsetof(pattern_t, nev);
        int trk = -2, field = -1, notes = 0, hdr = 0, s = uj_aux(&uj, k, UR_PAT);
        for (j = 0; uj_run(&uj, k, j, &rg, &ax, &off, &len); j++) {
            uint32_t a = off, b = off + len - 1u;
            if (rg != UR_PAT)
                continue;
            if (b >= t1)
                notes = 1;
            if (a < t0)
                hdr = 1;
            if (b >= t0 && a < t1) {
                uint32_t x = (a < t0 ? t0 : a) - t0, y = (b >= t1 ? t1 - 1u : b) - t0;
                int ta = (int)(x / sizeof(arr_trk_t)), tb = (int)(y / sizeof(arr_trk_t));
                uint32_t fa = x % sizeof(arr_trk_t), fb = y % sizeof(arr_trk_t);
                int f = fa < __builtin_offsetof(arr_trk_t, x) ? 0 : fa < __builtin_offsetof(arr_trk_t, vol) ? 1
                      : fa < __builtin_offsetof(arr_trk_t, mute) ? 2 : 3;
                int g = fb < __builtin_offsetof(arr_trk_t, x) ? 0 : fb < __builtin_offsetof(arr_trk_t, vol) ? 1
                      : fb < __builtin_offsetof(arr_trk_t, mute) ? 2 : 3;
                if (fa >= __builtin_offsetof(arr_trk_t, bright) && fa < __builtin_offsetof(arr_trk_t, flags))
                    f = 2;                       /* (brightness, resonance: the Mixer's tone page) */
                if (fb >= __builtin_offsetof(arr_trk_t, bright) && fb < __builtin_offsetof(arr_trk_t, flags))
                    g = 2;
                trk = ta == tb && (trk == -2 || trk == ta) ? ta : -1;
                field = f == g && (field == -1 || field == f) ? f : 4;
            }
        }
        if (notes)
            cat_s(cat_s(t, SECT_NAME[(s < 0 ? 0 : s) % ARR_NSECT + 1]), " NOTES");
        else if (trk >= 0 && !hdr && field <= 2)
            cat_s(cat_s(t, TRACK_NAME[trk & 7]), field == 0 ? " SOUND" : field == 1 ? " TYPE" : " MIXER");
        else
            cat_s(t, "PATTERN");
        return;
    }
    if (mask >> UR_LOOPS & 1u) {                 /* a loop: its chords, else its length, its name */
        int lp = -2, chords = 0, beat = -2, lenf = 0;
        for (j = 0; uj_run(&uj, k, j, &rg, &ax, &off, &len); j++) {
            uint32_t a = off, b = off + len - 1u, ch = __builtin_offsetof(loop_t, ch);
            int la = (int)(a / sizeof(loop_t)), lb = (int)(b / sizeof(loop_t));
            uint32_t fa = a % sizeof(loop_t), fb = b % sizeof(loop_t);
            if (rg != UR_LOOPS)
                continue;
            lp = la == lb && (lp == -2 || lp == la) ? la : -1;
            if (fb >= ch || la != lb) {
                int ba = fa >= ch ? (int)((fa - ch) / 2u) : 0, bb = fb >= ch ? (int)((fb - ch) / 2u) : -2;
                chords = 1;
                beat = la == lb && ba == bb && (beat == -2 || beat == ba) ? ba : -1;
            }
            if (fa < ch && fa >= __builtin_offsetof(loop_t, bars))
                lenf = 1;
        }
        if (lp < 0)
            cat_s(t, "LOOPS");
        else
            cat_s(loop_name_l(lp, t), chords ? (beat >= 0 ? " CHORD" : " CHORDS") : lenf ? " LENGTH" : " NAME");
        return;
    }
    if (mask >> UR_SONG & 1u) {                  /* the song: a row, its style, tempo, transpose, mutes, a lead's sound */
        int what = 0, row = -2, sect = -2;
        for (j = 0; uj_run(&uj, k, j, &rg, &ax, &off, &len); j++) {
            int wa, wb, ia = 0, ib = 0;
            if (rg != UR_SONG)
                continue;
            wa = un_song(off, &ia);
            wb = un_song(off + len - 1u, &ib);
            if (wa == UN_NROWS && wb == UN_NROWS)
                continue;                        /* (a row added or gone: named by its row) */
            if ((wa == UN_STYLE || wa == UN_STY) && (wb == UN_STYLE || wb == UN_STY)) {
                /* the sections' patterns (and the song's style): one section's, or the style */
                int one = wa == UN_STYLE && wb == UN_STYLE && ia == ib;
                sect = one && (sect == -2 || sect == ia) ? ia : -1;
                wa = UN_STYLE;
            } else if (wa == UN_ROW && wb == UN_ROW) {
                row = ia == ib && (row == -2 || row == ia) ? ia : -1;
            } else if (wa != wb || ia != ib)
                wa = UN_SONG;
            what = !what || what == wa ? wa : UN_SONG;
        }
        if (!what)
            what = UN_ROW;                       /* (only the row count: a row gone) */
        switch (what) {
        case UN_ROW:
            if (row >= 0)
                itoa_u((uint32_t)row + 1u, cat_s(t, "ROW "));
            else
                cat_s(t, "ROWS");
            break;
        case UN_STYLE:
            if (sect >= 0)
                cat_s(cat_s(t, SECT_NAME[sect % ARR_NSECT + 1]), " PATTERN");
            else
                cat_s(t, "STYLE");
            break;
        case UN_TEMPO: cat_s(t, "TEMPO"); break;
        case UN_XPOSE: cat_s(t, "TRANSPOSE"); break;
        case UN_MUTES: cat_s(t, "MUTES"); break;
        case UN_LEAD: cat_s(t, "LEAD SOUND"); break;
        default: cat_s(t, "SONG"); break;
        }
        return;
    }
    cat_s(t, "LEAD NOTES");
}

/* HOME + REC (redo 0), HOME + PLAY (1): the step before (after) the state now taken back (put back), while playing
 * too, recording or not. Through the same rules as an edit: the loops, rows and lead notes inside arr_edit, a pattern
 * copied, swapped and stored (arr_store_pattern: again when recorded into meanwhile), the slots whose pattern number
 * changed loaded again (the pattern a step took for a style's section: its notes swapped back in). What changed is
 * unsaved after, so autosave keeps it */
static void do_undo(int redo)
{
    int k, mask, s = -1;
    song_t was;
    char t[24];
    edits_flush();
    undo_commit();                               /* what was just done is a step too */
    k = redo ? uj_redo_at(&uj) : uj_undo_at(&uj);
    if (k < 0) {
        say(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO", 0);
        return;
    }
    mask = uj_mask(&uj, k);
    t[0] = 0;
    uj_label_of(&uj, k, t);
    if (!t[0])
        undo_name(k, t);
    if (mask >> UR_PAT & 1u) {                   /* the pattern first: it may have to wait (then nothing changes) */
        int ok = 0;
        uint8_t *sh;
        s = uj_aux(&uj, k, UR_PAT) % ARR_NSECT;
        if (app.slot_buf[s] >= 0) {              /* (a QY70 section playing: as a pattern, as slot_own makes it) */
            arr_pattern_copy(s, &scratch);
            arr_load_pattern(s, &scratch);
            app.slot_buf[s] = -1;
        }
        sh = ujs.slot == s ? (uint8_t *)&uj_pat : 0;
        for (int tries = 0; tries < 8 && !ok; tries++) {
            uint16_t d = arr_pattern_copy(s, &scratch);
            uj_apply(&uj, k, UR_PAT, (uint8_t *)&scratch, sh);
            if (pattern_valid(&scratch) && arr_store_pattern(s, &scratch, d))
                ok = 1;
            else
                uj_apply(&uj, k, UR_PAT, (uint8_t *)&scratch, sh);   /* (twice: as it was) */
        }
        if (!ok) {
            say(redo ? "REDO" : "UNDO", "NOT NOW");
            return;
        }
        slot_cache(s, &scratch);
        app.edit_pend &= (uint8_t)~(1u << s);
        app.edited[s] = 1;                       /* (saved: into its user pattern, if it plays one) */
    }
    was = *csong();
    if (mask & (1u << UR_SONG | 1u << UR_LOOPS | 1u << UR_LEADS)) {
        songs_write(1);                          /* (the rows, the loops and the lead notes: the render reads them) */
        uj_apply(&uj, k, UR_SONG, (uint8_t *)csong(), (uint8_t *)&uj_song);
        uj_apply(&uj, k, UR_LOOPS, (uint8_t *)sloops.loop, (uint8_t *)uj_loops);
        if (uj_apply(&uj, k, UR_LEADS, (uint8_t *)&leads, uj_leads)) {
            leads_index(&leads);
            leads.gen++;
        }
        songs_write(0);
    }
    uj_moved(&uj, redo);
    {
        song_t *g = csong();
        for (int j = 0; j < ARR_NSECT; j++)      /* a slot given another pattern: loaded (the style's again) */
            if (was.style[j] != g->style[j] && (j != s || g->style[j] >= ARR_USER_PATTERNS)) {
                slot_load(j, g->style[j]);
                if (ujs.slot == j)
                    arr_pattern_copy(j, &uj_pat);
            }
        if (g->tempo != was.tempo)
            arr_set_tempo(g->tempo);
        if (g->transpose != was.transpose)
            arr_set_transpose(g->transpose);
        if (g->mute != was.mute)
            mutes_apply();
        if (memcmp(g->lead, was.lead, sizeof g->lead))
            lead_sounds_sync();
        if (app.row > g->nrows)
            app.row = g->nrows;
    }
    if (mask >> UR_LEADS & 1u) {                 /* the lead parts saved again; a part with notes again gets a slot */
        for (int p = 0; p < LEAD_PARTS; p++)
            if (leads.part[p].n && app.lslot[p] < 0) {
                int f = pattern_free();
                if (f < 0) {
                    say_noslot("NO FREE SLOT", "LEADS NOT KEPT");
                    break;
                }
                app.lslot[p] = (int8_t)f;
            }
        app.ldirty = (1u << LEAD_PARTS) - 1u;
    }
    app.gen++;
    app.chg++;
    ui.act_t = plat_ms();
    say(redo ? "REDO" : "UNDO", t);
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

/* the click (Setup's CLICK) to the engine with the app's REC (writing chords, a take, REC on SONG: the engine knows its
 * own recording), and a free take's count-in (the beats before its downbeat click even with CLICK off) */
static void click_poll(void)
{
    int rec = ui.rec_want || ui.rec_chords || ui.take || ui.song_rec;
    uint8_t m = (uint8_t)(1u + (proj.click % 3u) * 2u + (rec != 0));
    uint32_t cin = ui.take == 2 ? ui.take_b0 : 0;
    if (m != ui.click_sent) {
        ui.click_sent = m;
        arr_click(proj.click % 3u, rec);
    }
    if (cin != ui.count_sent) {
        ui.count_sent = cin;
        arr_count_in(cin);
    }
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
    if (ui.stopped == 1u && app.lead && (arr_recording || ui.rec_want)) {
        arr_record(sel_trk(), 0);                /* a lead's REC: off with the song (a stop, its end) */
        ui.rec_want = ui.rec_seen = 0;
        say("RECORD OFF", "SONG STOPPED");
    }
    if (ui.stopped == 1u && arr_ended && app.mode == ARR_SONG) {
        app.row = 0;                             /* the song ended by itself: SONG's cursor back on row 1 (PLAY there: */
        ui.row_follow = 1;                       /* the song again); a stop by PLAY leaves it where it stopped */
    }
    if (!arr_playing || (int32_t)(arr_beats - ui.mode_at) >= 0)
        ui.mode_wait = 0;                        /* the bar line passed (or stopped: PLAY starts it) */
    rec_follow_keys();
    leads_poll();
    if (!arr_recording && !ui.rec_want)
        ui.follow_hint = 0;                      /* (REC off: the hint goes) */
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
    click_poll();
    undo_tick();
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
