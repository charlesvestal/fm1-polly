/* SPDX-License-Identifier: GPL-3.0-only */
/* Notes, the note editor: its controls (docs/superpowers/specs/2026-10-12-note-editor-design.md). After Melodee's SEQ >
 * NOTES (keremimo, GPL-3.0, github.com/keremimo/melodee: ui_input.c, seq_notes.c): notes are selected, not steps;
 * EDIT deletes, held while recording erases what the playhead passes; SEL + SELECT moves. The page and its state: pages.c
 * (ne, ne_ctx, draw_notes); the operations: dsp/notes.c. Part of the unity build (ui.c includes it).
 *
 *   SEQ (on Band)    Notes; SEQ again: Band
 *   SELECT           the next / previous note, in time order (a chord's notes one by one)
 *   KNOB 1 PLACE     the cursor, a grid step (Band's GRID; off: a 32nd) a detent, empty places too
 *   KNOB 2 PITCH     the selected note's pitch (a drum's sound)
 *   KNOB 3 LENGTH    its length, a grid step a detent (SEL held: a 48th of a beat)
 *   KNOB 4 VELOCITY  its velocity
 *   PRESETS          the window: 8, 4, 2, 1 bars (an instrument's: a beat too)
 *   SEL + SELECT / KNOB 1    the selected note moved a grid step a detent (a melody's across a section's end: into
 *                    that section, its root the chord's there)
 *   EDIT             the selected note deleted; held while recording (playing): the notes the playhead passes go
 *   keys             on a note: heard; at an empty place: entered there (a melody's with the root sounding there,
 *                    as recorded), held together a chord; all up: the cursor a grid step on. Recording: as recorded
 * Every edit is an undo step (another kind of edit, or another note, ends the one before). An instrument's edit makes its
 * groove yours first (copy-on-write, as recording), and is stored into the arranger as recording's are */

enum { NE_PITCH = 1, NE_LEN, NE_VEL, NE_MOVE, NE_DEL, NE_ADD, NE_ERASE };
static void ne_kind(int kind)                    /* an edit of this kind: the one before a step of its own first */
{
    if (kind != ne.kind || kind >= NE_DEL)
        undo_commit();
    ne.kind = (uint8_t)kind;
}
/* the groove's pattern changed: a copy, op on it (NE_ERASE: the notes from t to `to`), stored (again when recorded
 * into meanwhile); its index after (-1 refused: the same note there, full) */
static int ne_band(int op, uint32_t t, int note, uint32_t len, int vel, uint32_t to)
{
    int s = pslot(), trk = proj.trk & 7, j = -1;
    slot_own(s);                                 /* (a style's groove: yours first) */
    edits_flush();
    for (int tries = 0; tries < 8; tries++) {
        uint16_t d = arr_pattern_copy(s, &scratch);
        int i = np_find(&scratch, trk, ne.k);
        arr_trk_t *tr = &scratch.trk[trk];
        j = i;
        if (op == NE_ADD)
            j = np_insert(&scratch, trk, t, len, note, vel);
        else if (op == NE_ERASE)
            for (j = tr->first + tr->count - 1; j >= (int)tr->first; j--)
                if (scratch.ev[j].tick >= t && scratch.ev[j].tick < to)
                    np_delete(&scratch, trk, j);
        if (i >= 0 && op == NE_MOVE)
            j = np_move(&scratch, trk, i, t, note);
        else if (i >= 0 && op == NE_DEL)
            np_delete(&scratch, trk, i);
        else if (i >= 0 && op == NE_LEN)
            scratch.ev[i].len = (uint16_t)len;
        else if (i >= 0 && op == NE_VEL)
            scratch.ev[i].vel = (uint8_t)vel;
        if (j < 0 && op != NE_ERASE)
            break;
        if (arr_store_pattern(s, &scratch, d)) {
            app.edited[s] = 1;
            break;
        }
    }
    app.gen++;
    mark_dirty();
    if (j < 0 && op == NE_ADD)
        say("GROOVE FULL", "600 NOTES");
    return j;
}
/* the melody's notes changed (inside arr_edit, as recording's: lead_write); returns the note's index after, -1 refused */
static int ne_lead(int op, uint32_t t, int note, uint32_t len, int vel, uint32_t to)
{
    int i = ne_find(), j = i, before = leads.n, k = ne_trk();
    lead_ev_t e;
    if (op == NE_ADD) {
        int s = nmap_sect(&ne_m, t);
        const row_t *w = &csong()->row[s < 0 ? 0 : s];
        if (s < 0)
            return -1;
        lead_set(&e, s, t - ne_m.start[s], len, clampi(note + csong()->transpose + w->key, 0, 127), vel, k,
                 nl_root_at(csong(), sloops.loop, s, t - ne_m.start[s]));
        ne.k.t = t;
        ne.k.note = e.note;
        return lead_put(&e) < 0 ? -1 : 0;
    }
    if (i < 0)
        return -1;
    lead_write(1);
    if (op == NE_MOVE) {                         /* (into another section: its root there) */
        int s0 = nmap_sect(&ne_m, ne.k.t), s1 = nmap_sect(&ne_m, t);
        j = nl_move(&leads, &ne_m, i, t, note,
                    s1 >= 0 && s1 != s0 ? nl_root_at(csong(), sloops.loop, s1, t - ne_m.start[s1]) : -1);
    } else if (op == NE_DEL)
        leads_erase_at(&leads, (uint32_t)i, 1);
    else {
        lead_ev_t *x = lead_at(&leads, (uint32_t)i);
        lead_set(x, x->row, lead_pos(x), op == NE_LEN ? len : lead_len(x), x->note, op == NE_VEL ? vel : x->vel,
                 lead_trk(x), lead_root(x));
    }
    lead_write(0);
    lead_changed(j >= 0 && j < i ? j : i, before);
    (void)to;
    return j;
}
/* an edit of the selected note (NE_ADD: a new one at t; NE_ERASE: the notes from t to `to`); the selection follows it */
static int ne_do(int op, uint32_t t, int note, uint32_t len, int vel, uint32_t to)
{
    int j = app.lead ? ne_lead(op, t, note, len, vel, to) : ne_band(op, t, note, len, vel, to);
    if (j >= 0 && (op == NE_MOVE || op == NE_ADD)) {
        ne.k.t = t;
        if (app.lead && op == NE_ADD)
            note = ne.k.note;                    /* (as stored: transposed) */
        ne.k.note = (uint8_t)note;
        ne.has = 1;
        ne.t = t / ne_grid() * ne_grid();
    }
    if (op >= NE_DEL)
        undo_commit();                           /* (a note in or out: a step at once, never part of a take after) */
    ne_ctx();
    return j;
}
/* the selected note moved `steps` grid steps (over the same note, and a melody's over the gaps between sections) */
static void ne_move(int32_t steps)
{
    uint32_t g = ne_grid(), end = ne_end();
    int32_t t;
    int d = steps > 0 ? 1 : -1;
    if (!ne.has)
        return;
    ne_kind(NE_MOVE);
    t = (int32_t)ne.k.t;
    for (int n = 0; steps && n < 256; n++) {
        t += d * (int32_t)g;
        if (t < 0 || (uint32_t)t >= end)
            break;
        if (app.lead && nmap_sect(&ne_m, (uint32_t)t) < 0)
            continue;
        if (ne_do(NE_MOVE, (uint32_t)t, ne.k.note, 0, 0, 0) >= 0)
            steps -= d;
    }
}
static void ne_select(int32_t e)                 /* SELECT: the next notes (SEL held: the selected one moved) */
{
    nkey_t k;
    int d = e > 0 ? 1 : -1, dir;
    ne_ctx();
    if (ui.btn >> B_SEL & 1u) {
        ne_move(e);
        return;
    }
    k = ne.k;
    dir = d;
    if (!ne.has) {                               /* (an empty place: the first after it, the last before) */
        k.t = ne.t;
        k.note = 0;
        dir = d > 0 ? 0 : -1;
    }
    for (int32_t n = e < 0 ? -e : e; n > 0 && ne_near(&k, dir); n--, dir = d) {
        ne.k = k;
        ne.has = 1;
        ne.t = k.t / ne_grid() * ne_grid();
    }
    ne.kind = 0;
}
static void ne_knob(int kn, int32_t e)
{
    uint32_t g = ne_grid(), end, len = 0, mx;
    int i, d = e > 0 ? 1 : -1, fine = ui.btn >> B_SEL & 1u, vel = 0;
    ne_ctx();
    end = ne_end();
    if (kn == 0 && fine) {
        ne_move(accel(EN_K1, e));
        return;
    }
    if (kn == 0) {                               /* PLACE: the cursor (its cell's first note selected) */
        int32_t t = (int32_t)ne.t + accel(EN_K1, e) * (int32_t)g;
        ne.t = t < 0 ? 0u : (uint32_t)t;
        ne.has = 0;
        ne.kind = 0;
        ne_ctx();
        return;
    }
    if ((i = ne.has ? ne_find() : -1) < 0)
        return;
    if (app.lead) {
        len = lead_len(lead_at_c(&leads, (uint32_t)i));
        vel = lead_at_c(&leads, (uint32_t)i)->vel;
    } else {
        len = view.ev[i].len;
        vel = view.ev[i].vel;
    }
    if (kn == 1) {                               /* PITCH: past a note at the same place (the same note: never two) */
        int n = clampi(ne.k.note + accel(EN_K2, e), 0, 127);
        ne_kind(NE_PITCH);
        while (n != ne.k.note && ne_do(NE_MOVE, ne.k.t, n, 0, 0, 0) < 0 && n + d >= 0 && n + d <= 127)
            n += d;
    } else if (kn == 2) {                        /* LENGTH: a grid step (SEL held: fine), the groove's length at most */
        uint32_t st = fine ? 10u : g, lo = fine ? 10u : len < g ? len : g;
        int32_t nl = (int32_t)len + accel(EN_K3, e) * (int32_t)st;
        mx = app.lead ? 0xFFFFu : end;
        ne_kind(NE_LEN);
        ne_do(NE_LEN, 0, 0, nl < (int32_t)lo ? lo : (uint32_t)nl > mx ? mx : (uint32_t)nl, 0, 0);
    } else {
        ne_kind(NE_VEL);
        ne_do(NE_VEL, 0, 0, 0, clampi(vel + accel(EN_K4, e), 1, 127), 0);
    }
}
static void ne_delete(void)                      /* EDIT: the selected note goes (the cell's next, if any, selected) */
{
    ne_ctx();
    if (!ne.has)
        return;
    ne_kind(NE_DEL);
    ne_do(NE_DEL, 0, 0, 0, 0, 0);
    ne.has = 0;
    ne_ctx();
}
/* a key on Notes (not recording): on a note heard only (the caller sounds it); at an empty place entered, with the keys
 * held with it (a chord); all up: the cursor on (ne_key_up) */
static void ne_key(int note)
{
    ne_ctx();
    if (ne.has && !ne.entering)
        return;
    ne.entering = 1;
    ne_kind(NE_ADD);
    if (ne_do(NE_ADD, ne.t, note, ne_grid(), app.lead ? sel_lead()->vel : 100, 0) < 0 && app.lead)
        say("NO SECTION", "HERE");
}
static void ne_key_up(void)
{
    for (int k = 0; k < NKEYS; k++)
        if (ui.kmode[k] == KM_PLAY)
            return;
    if (!ne.entering)
        return;
    ne.entering = 0;
    ne.t += ne_grid();
    ne.has = 0;
    ne.kind = 0;
    ne_ctx();
}
/* erasing: the selected instrument's or melody's notes from a to b go */
static void ne_erase(uint32_t a, uint32_t b)
{
    int k = ne_trk(), lo = -1, before = leads.n;
    if (!app.lead) {
        const arr_trk_t *tr = &view.trk[k];
        for (uint32_t i = tr->first; i < (uint32_t)tr->first + tr->count && i < view.nev; i++)
            if (view.ev[i].tick >= a && view.ev[i].tick < b) {
                ne_band(NE_ERASE, a, 0, 0, 0, b);
                return;
            }
        return;
    }
    for (int j = (int)leads.n - 1; j >= 0; j--) {
        const lead_ev_t *x = lead_at_c(&leads, (uint32_t)j);
        uint32_t xt = nl_time(&ne_m, x);
        if (lead_trk(x) != k || xt == ~0u || xt < a || xt >= b)
            continue;
        if (lo < 0)
            lead_write(1);
        leads_erase_at(&leads, (uint32_t)j, 1);
        lo = j;
    }
    if (lo >= 0) {
        lead_write(0);
        lead_changed(lo, before);
    }
}
/* each frame: EDIT held on Notes while recording (and playing): the notes the playhead passes go, from where it was when
 * EDIT went down (round the groove's or the song's end) */
static void ne_tick(void)
{
    uint32_t now;
    if (ui.page != P_NOTES || !(ui.btn >> B_EDIT & 1u) || !arr_recording || !arr_playing || arr_rec_wait || lst.open) {
        ne.erasing = 0;
        return;
    }
    ne_ctx();
    if ((now = ne_ph()) == ~0u)
        return;
    ui.btn_used |= 1u << B_EDIT;                 /* (its release deletes nothing, no card) */
    ui.card = 0;
    if (!ne.erasing) {                           /* (from the playhead's grid step: the note there too) */
        ne.erasing = 1;
        ne.er = now / ne_grid() * ne_grid();
    }
    if (now >= ne.er)
        ne_erase(ne.er, now + 1u);
    else if (ne.er - now > ne_grid()) {         /* (round the end) */
        ne_erase(ne.er, ne_end());
        ne_erase(0, now + 1u);
    } else
        return;
    ne.er = now + 1u;
}
