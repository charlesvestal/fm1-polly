/* SPDX-License-Identifier: GPL-3.0-only */
/* The transposer (xpose.h). The source chord is C M7 over the C major scale: each pitch class is a degree
 * (C R, D 9, E 3, F 11, G 5, A 13, B 7) or a chromatic note just above one (C# = R+1, Eb = 9+1, F# = 11+1,
 * Ab = 5+1, Bb = 13+1). Chord 1 moves each degree to the same degree of the target (chords.c), keeping the
 * chromatic offset and the octave over the root. */
#include "xpose.h"

static const uint8_t SRC_DEG[12] = {DEG_R, DEG_R, DEG_9, DEG_9, DEG_3, DEG_11, DEG_11, DEG_5, DEG_5, DEG_13, DEG_13, DEG_7};
static const uint8_t SRC_SEMI[NDEG] = {0, 2, 4, 5, 7, 9, 11};

void xpose_reset(xstate_t *s)
{
    s->last = -1;
    s->fresh = 1;
}
void xpose_chord_changed(xstate_t *s) { s->fresh = 1; }

static int fold(int n, int lo, int hi)
{
    while (n > hi && n - 12 >= 0)
        n -= 12;
    while (n < lo && n + 12 <= 127)
        n += 12;
    return n;
}
static int nearest(int pc_note, int target)          /* pc_note moved by octaves nearest target */
{
    while (pc_note - target > 6)
        pc_note -= 12;
    while (target - pc_note > 6)
        pc_note += 12;
    return pc_note;
}

/* Chord 1's degree-to-degree map; also the base of Chord 2 and Bass, which is why the high key (Chord 1 and
 * Bass only) is decided here from the track's type */
static int chord1(const xtrack_t *t, int note, chord_t c)
{
    int rel = note % 12, base = note - rel, d = SRC_DEG[rel], off = rel - SRC_SEMI[d], shift = c.root;
    if ((t->type == XT_CHORD1 || t->type == XT_BASS) && t->hikey < XT_NOKEY && c.root > t->hikey)
        shift -= 12;
    return base + shift + chord_degrees(c.type)[d] + off;
}

int xpose(const xtrack_t *t, xstate_t *s, int note, chord_t c)
{
    int out;
    xtrack_t plain;                               /* (here, not a compound literal in the if: that one ends with it) */
    if (t->type == XT_BYPASS)
        return note;                              /* drums: as written, no limits */
    if (c.type == CH_NONE) {                      /* "---", no chord: every type only moves to the root */
        plain = (xtrack_t){XT_PARALLEL, t->lo, t->hi, XT_NOKEY};
        t = &plain;
    }
    switch (t->type) {
    case XT_PARALLEL: out = note + c.root; break;
    case XT_CHORD2:
        out = chord1(t, note, c);
        if (s->last >= 0)
            out = nearest(out, s->last);
        break;
    case XT_BASS:
        out = chord1(t, note, c);
        if (s->fresh)
            out = nearest(out - (out % 12) + (c.bass != CH_NOBASS ? c.bass : c.root), out);
        break;
    default:
        out = chord1(t, note, c);
        break;
    }
    out = fold(out, t->lo, t->hi);
    if (out < 0 || out > 127)
        return -1;
    s->last = (int16_t)out;
    s->fresh = 0;
    return out;
}

/* the source pitch classes in the order a key is matched against their images: R 3 5 7, 9 11 13, chromatic */
static const uint8_t INV_ORDER[12] = {0, 4, 7, 11, 2, 5, 9, 1, 3, 6, 8, 10};

int xpose_inverse(const xtrack_t *t, int note, chord_t c)
{
    int n = note - c.root;                        /* Parallel's inverse, and the fallback */
    if (t->type == XT_BYPASS)
        return note;
    if (c.type != CH_NONE && t->type != XT_PARALLEL) {
        int shift = c.root;
        if ((t->type == XT_CHORD1 || t->type == XT_BASS) && t->hikey < XT_NOKEY && c.root > t->hikey)
            shift -= 12;
        for (int i = 0; i < 12; i++) {
            int rel = INV_ORDER[i], d = SRC_DEG[rel], img = shift + chord_degrees(c.type)[d] + rel - SRC_SEMI[d];
            if ((note - img) % 12 == 0) {         /* chord1(n) == note: n's octave base + img */
                n = note - img + rel;
                break;
            }
        }
    }
    while (n < 0)
        n += 12;
    while (n > 127)
        n -= 12;
    return n;
}

int xpose_root6(int root)
{
    root %= 12;
    return root > 5 ? root - 12 : root;
}
int xpose_played(const xtrack_t *t, int note, chord_t c)
{
    int out = fold(note + xpose_root6(c.root), t->lo, t->hi);
    return out < 0 || out > 127 ? -1 : out;
}
int xpose_played_inverse(int note, chord_t c)
{
    int n = note - xpose_root6(c.root);
    while (n < 0)
        n += 12;
    while (n > 127)
        n -= 12;
    return n;
}
