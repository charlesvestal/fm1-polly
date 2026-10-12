/* SPDX-License-Identifier: GPL-3.0-only */
/* The note editor's operations (the app's Notes page; its design after Melodee's SEQ > NOTES, by keremimo, GPL-3.0,
 * github.com/keremimo/melodee: notes are selected, not steps, in time order, a chord's notes one by one): a melody's
 * notes (leads_t) along the song, and a groove instrument's (pattern_t). Pure: no state, no engine (the app does each
 * change inside arr_edit, or on a pattern copy it stores). A note is named by its place and pitch (nkey_t): the order
 * is place, then pitch; two notes never share both (a move or a pitch onto one is refused). */
#pragma once
#include "arr.h"
#include "leads.h"

typedef struct {
    uint32_t t;                                   /* its place: song ticks (a melody), the groove's ticks (an instrument) */
    uint8_t note;
} nkey_t;
/* a song's sections end to end in song ticks: section s from start[s] (its first bar's downbeat; start[n] the song's
 * end), len[s] its ticks (its progression x REPEAT; a melody's notes from len on are kept, not played, not edited) */
typedef struct {
    int n;
    uint32_t start[ARR_ROWS + 1], len[ARR_ROWS];
} nmap_t;
int nmap_sect(const nmap_t *m, uint32_t t);       /* the section song tick t plays in, -1 none (past a section's end) */
uint32_t nl_time(const nmap_t *m, const lead_ev_t *e);   /* a melody note's song tick (its section's past the map: ~0) */
/* melody trk's (0 M1, 1 M2) note k: its index in the list, -1 none */
int nl_find(const leads_t *l, const nmap_t *m, int trk, nkey_t k);
/* the next note in order: dir > 0 the first after k, < 0 the last before it, 0 the first at or after it; 1 = found (k
 * set to it) */
int nl_near(const leads_t *l, const nmap_t *m, int trk, nkey_t *k, int dir);
/* note i moved to song tick t, into the section there (a move across a section's end re-anchors it: its row, its place
 * in it, root (0..11; -1 kept) the chord's sounding there), length and velocity kept; or its pitch changed (t ~0: its
 * place kept). Returns its index now; -1 refused: no section there, or the same note already there */
int nl_move(leads_t *l, const nmap_t *m, int i, uint32_t t, int note, int root);
/* the root sounding at section r's place pos: its progression's chord on that beat (round its length; none: C) + the
 * song's TRANSPOSE + the section's own, 0..11 */
int nl_root_at(const song_t *g, const loop_t *loops, int r, uint32_t pos);

/* instrument trk's notes in p, those before `end` (the groove's length: past it kept, not played) */
int np_find(const pattern_t *p, int trk, nkey_t k);
int np_near(const pattern_t *p, int trk, uint32_t end, nkey_t *k, int dir);
/* a note in, in order (the same note at the same tick: its length and velocity replaced); its index, -1 full (ARR_NEV) */
int np_insert(pattern_t *p, int trk, uint32_t tick, uint32_t len, int note, int vel);
void np_delete(pattern_t *p, int trk, int i);
/* note i to tick t (t ~0: kept) and pitch `note`; its index now, -1 the same note already there */
int np_move(pattern_t *p, int trk, int i, uint32_t t, int note);
