#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A QY70-format style bank with no Yamaha data in it, for the tests: Polly's own 16 factory styles (styles/*.sty)
coded as bank styles, plus controllers (a bend, a modulation swell, an expression swell on Main A) and a dense
style ("Dense": short phrases repeated to fill their sections, and a Main A of over 600 notes, more than a
pattern holds).

    python3 tools/qy70_codec.py --synthetic --image FILE      # the bank as the FM-1 stores it

styles() returns what qy70_codec.build_from() takes. Python 3 standard library only."""
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_styles as ms  # noqa: E402
import qy70_codec as c  # noqa: E402

PPQ = c.PPQ
TYPES = ms.TYPES


def _track_of(trk, drum):
    """a style's track (qy70_codec.head): channel 1-based"""
    return dict(chan=trk["chan"] + 1, drum=int(drum), prog=0 if drum else trk["program"], vol=trk["vol"],
                pan=trk["pan"], rev=trk["rev"], cho=trk["cho"], vscale=1.0)


def _phrase(notes, ctl, length, typ, drum, lo, hi, hikey, blen):
    if drum or typ == "bypass":
        lo, hi, hikey = 0, 127, 12
    return c.prep(sorted(notes, key=lambda n: (n[0], n[2])), ctl, length, typ, drum, lo, hi, hikey, blen)


def from_sty(path, controllers=True):
    """a .sty as a bank style: (st, tracks, uses, order, seen). One phrase a section and track (the section's
    length); type, limits and program per section; the mixer from Main A"""
    sty = ms.parse(path)
    pats = sty["pats"]
    main = pats[1]
    beats = main["beats"]
    tracks, uses, order, seen = {}, [[None] * 8 for _ in range(6)], [], {}
    for t in range(8):
        if any(p["ev"][t] for p in pats):
            h = main["trk"][t]
            tracks[t] = _track_of(h, h["chan"] == 9)
    for s in [1, 2, 3, 4, 0, 5]:
        p = pats[s]
        if p.get("empty") or p["beats"] != beats:
            continue
        length = p["bars"] * beats * PPQ
        for t in range(8):
            if not p["ev"][t] or t not in tracks:
                continue
            h = p["trk"][t]
            drum = tracks[t]["drum"]
            typ = TYPES[h["type"]]
            notes = [(e[0], max(1, e[1]), e[2], e[3]) for e in p["ev"][t]]
            ctl = []
            if controllers and s == 1:
                ctl = _controllers(t, length)
            key = (s * 8 + t, typ, drum)
            seen[key] = len(order)
            order.append((key, _phrase(notes, ctl, length, typ, drum, h["lo"], h["hi"], h["hikey"], beats * PPQ)))
            uses[s][t] = dict(i=s * 8 + t, typ=typ, prog=0 if drum else h["program"], drum=drum)
    bars = [p["bars"] if not p.get("empty") and p["beats"] == beats else 1 for p in pats]
    st = dict(name=sty["name"][:8], tempo=sty["tempo"], num=beats, bars=bars)
    return st, tracks, uses, order, seen


def _controllers(t, length):
    """Main A's: the bass a bend down and back in its first beat (kind 0), C1 a modulation swell (1), C3 an
    expression swell across the section (2)"""
    if t == 3:
        return [(0, 0, 64), (60, 0, 56), (120, 0, 48), (240, 0, 56), (360, 0, 64)]
    if t == 4:
        return [(0, 1, 0), (PPQ, 1, 40), (2 * PPQ, 1, 80), (3 * PPQ, 1, 0)]
    if t == 6:
        n = 8
        return [(length * i // n, 2, 40 + 80 * i // (n - 1)) for i in range(n)]
    return []


def dense():
    """"Dense": 4/4; Main A 8 bars from 1-bar phrases (hi-hat 32nds, a second hat, shaker 16ths, a bass line):
    over 600 notes; Main B 4 bars from a 2-bar chord phrase and a 1-bar drum phrase; the other sections empty"""
    blen = 4 * PPQ
    tracks = {0: _track_of(dict(chan=9, program=0, vol=100, pan=64, rev=20, cho=0), True),
              1: _track_of(dict(chan=9, program=0, vol=100, pan=64, rev=20, cho=0), True),
              2: _track_of(dict(chan=9, program=0, vol=100, pan=64, rev=20, cho=0), True),
              3: _track_of(dict(chan=1, program=33, vol=100, pan=64, rev=10, cho=0), False),
              4: _track_of(dict(chan=2, program=0, vol=90, pan=50, rev=40, cho=0), False)}
    uses, order, seen = [[None] * 8 for _ in range(6)], [], {}

    def add(s, t, i, notes, typ, length, lo=0, hi=127, hikey=12, ctl=()):
        drum = tracks[t]["drum"]
        key = (i, typ, drum)
        if key not in seen:
            seen[key] = len(order)
            order.append((key, _phrase(notes, list(ctl), length, typ, drum, lo, hi, hikey, blen)))
        uses[s][t] = dict(i=i, typ=typ, prog=tracks[t]["prog"], drum=drum)
    hats = [(k * 60, 30, 42, 60 + (k % 4) * 15) for k in range(32)]
    hats2 = [(k * 60 + 30, 30, 44, 50) for k in range(32)]
    shaker = [(k * 120, 60, 70, 70 + (k % 2) * 20) for k in range(16)]
    bass = [(k * 240, 200, [36, 36, 43, 40][k % 4], 100) for k in range(8)]
    for t, i, notes, typ in ((0, 1, hats, "bypass"), (1, 2, hats2, "bypass"), (2, 3, shaker, "bypass"),
                             (3, 4, bass, "bass")):
        add(1, t, i, notes, typ, blen, 28, 55, 7) if t == 3 else add(1, t, i, notes, typ, blen)
    chords = [(0, 900, 60, 90), (0, 900, 64, 90), (0, 900, 67, 90), (blen, 900, 62, 80), (blen, 900, 65, 80),
              (blen, 900, 69, 80)]
    add(2, 4, 5, chords, "chord1", 2 * blen, 48, 84, 12, ctl=[(0, 2, 30), (blen, 2, 100), (2 * blen - 1, 2, 60)])
    add(2, 0, 1, hats, "bypass", blen)
    st = dict(name="Dense", tempo=120, num=4, bars=[1, 8, 4, 1, 1, 1])
    return st, tracks, uses, order, seen


def styles():
    files = sorted(glob.glob(os.path.join(HERE, "..", "styles", "*.sty")))
    return [from_sty(f) for f in files] + [dense()]


if __name__ == "__main__":
    blob, per, stats = c.build_from(styles())
    print("%d styles, %d bytes; %d notes, %d controller events" % (len(per), len(blob), stats["notes"], stats["ctrl"]))
