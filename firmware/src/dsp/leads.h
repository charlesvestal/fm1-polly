/* SPDX-License-Identifier: GPL-3.0-only */
/* The lead tracks' notes (arr.h leads_t): pure helpers the app and the tests share. No state, no flash (project.c
 * stores the parts). The list is kept in order (row, place, track, note); every change rebuilds row0. The app does
 * each change inside arr_edit(1) .. arr_edit(0) and counts gen up (the render reads the list). */
#pragma once
#include "arr.h"

void leads_clear(leads_t *l);                    /* no notes (the sounds kept) */
/* LD1: a choir (GM 53, Choir Aahs: melodies are mostly sung) on channel 7; LD2: strings (GM 49) on channel 8 (after the patterns' 2..6, 10) */
static inline void lead_trk_default(lead_trk_t *t, int k)
{
    t->program = (uint8_t)(k ? 48 : 52);
    t->chan = (uint8_t)(k ? 7 : 6);
    t->vol = 100;
    t->pan = 64;
    t->rev = 40;
    t->cho = 0;
    t->bright = t->reso = 0;                     /* (XOR 64: the neutral 64) */
    t->vel = 100;                                /* (as the keys play the patterns' tracks) */
    t->rsv = 0;
}
int lead_cmp(const lead_ev_t *a, const lead_ev_t *b);   /* the list's order: < 0, 0 (the same note), > 0 */
void leads_index(leads_t *l);                    /* row0 from the list */
/* a note into its place: the same note (row, place, track, pitch) already there is replaced (its length, velocity,
 * root). Returns 1 added, 0 replaced, -1 full (max notes: the room the app has slots for; LEAD_MAX at most). *at:
 * its index (may be 0) */
int leads_insert(leads_t *l, const lead_ev_t *e, int max, int *at);
/* the notes of row r on (lead track trk, -1 both; r -1: every row) removed; returns how many */
int leads_erase(leads_t *l, int r, int trk);
int leads_count(const leads_t *l, int r, int trk);
/* the notes of row r, track trk (-1 both), each to the nearest line of `grid` ticks from the row's start (a line at
 * `end`, the row's length in ticks, or past it: the line before; end 0: no end); the row kept in order, two notes
 * landing on the same place and pitch: the later kept. Lengths kept. Returns the notes moved (0: nothing changed) */
int leads_quantise(leads_t *l, int r, int trk, uint32_t grid, uint32_t end);
/* the notes at indexes [from, from + n) removed (a part's: SLOTS' FREE); returns how many */
int leads_erase_at(leads_t *l, uint32_t from, uint32_t n);   /* notes of row r (-1 every row), track trk (-1 both) */
/* a row inserted at r (loops.c row_insert): the rows from r one on (the new row has none; a note pushed past the last
 * row goes: their count returned) */
int leads_row_insert(leads_t *l, int r);
/* row r deleted (loops.c row_delete): its notes go, the rows after it one back; returns the notes removed */
int leads_row_delete(leads_t *l, int r);
/* the notes as loaded (part 0's n0, then part 1's n1, each list as stored): notes no row or track has dropped (row
 * past ARR_ROWS, root past 11, pitch or velocity 0), the rest in order, part 0 filled first, row0 built. Returns
 * 1 when anything moved or went (the parts then differ from what is stored) */
int leads_fix(leads_t *l, int n0, int n1);
int leads_parts(int n);                          /* the parts n notes take: 0, 1, 2 */
/* the parts index i (an insert or an erase from it on) changes, of a list of n notes before and after: bits */
unsigned leads_parts_from(int i, int n_before, int n_after);
