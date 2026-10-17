#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Polly's factory styles: .sty text files (styles/README.md) compiled into app/dsp/styles.h.

    python3 tools/make_styles.py styles/*.sty > app/dsp/styles.h
    python3 tools/make_styles.py --check styles/*.sty        # "16 styles, 96 patterns ok", or the errors
    python3 tools/make_styles.py --dump styles/01-pop.sty    # the style back as text (drums expanded)
    python3 tools/make_styles.py --roundtrip styles/*.sty    # dump -> parse -> the same tables
    python3 tools/make_styles.py --grid FILE.mid [BARS]      # a MIDI file's drums as a 16th grid (choosing loops)
    python3 tools/make_styles.py --sizes styles/*.sty        # flash bytes per style

Python 3 standard library only."""
import os
import re
import struct
import sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))


def arr_define(name):
    """a #define of app/dsp/arr.h, so the tool's limits are the engine's"""
    with open(os.path.join(HERE, "..", "app", "dsp", "arr.h")) as f:
        m = re.search(r"^#define\s+%s\s+(\d+)" % name, f.read(), re.M)
    if not m:
        raise SystemExit("make_styles: no %s in arr.h" % name)
    return int(m.group(1))


PPQ = arr_define("ARR_PPQ")     # ticks a beat (480)
NEV = arr_define("ARR_NEV")     # notes a pattern
MAXBARS = 8
NTRK = 8
MAX_FACTORY = arr_define("ARR_MAX_FACTORY")   # factory styles, at most
NSECT = arr_define("ARR_NSECT")
SECTIONS = ["intro", "main a", "main b", "fill ab", "fill ba", "ending"]   # the slots' order (arr.h SEC_*)
SECTION_ALIAS = {"fill": "fill ab"}
MAIN_A = SECTIONS.index("main a")
assert len(SECTIONS) == NSECT
TRACKS = ["d1", "d2", "pc", "ba", "c1", "c2", "c3", "c4"]
TYPES = ["bypass", "parallel", "chord1", "chord2", "bass"]
NOKEY = 12
EV_BYTES = 5                    # a uint32 (tick, note, length code) + a uint8 (velocity, length code's top bit)
LEN_EXACT = 1020                # lengths 1..1020 exact; longer ones in steps of LEN_STEP (exact for multiples of it)
LEN_STEP = 20
LEN_MAX = LEN_EXACT + (2047 - (LEN_EXACT - 1)) * LEN_STEP     # 21580 ticks (45 beats)
TRK_BYTES = 14                  # sty_trk_t
PAT_BYTES = 4 + NTRK * TRK_BYTES

NOTE_PC = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
PC_NAME = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
# GM percussion (key map, channel 10), short names
DRUMS = {
    "kick2": 35, "kick": 36, "rim": 37, "snare": 38, "clap": 39, "esnare": 40, "ftom2": 41, "chh": 42,
    "ftom": 43, "phh": 44, "ltom": 45, "ohh": 46, "lmtom": 47, "hmtom": 48, "crash": 49, "htom": 50,
    "ride": 51, "china": 52, "bell": 53, "tamb": 54, "splash": 55, "cowbell": 56, "crash2": 57,
    "vibraslap": 58, "ride2": 59, "hbongo": 60, "lbongo": 61, "mhconga": 62, "hconga": 63, "lconga": 64,
    "htimbale": 65, "ltimbale": 66, "hagogo": 67, "lagogo": 68, "cabasa": 69, "maracas": 70,
    "swhistle": 71, "lwhistle": 72, "sguiro": 73, "lguiro": 74, "claves": 75, "hblock": 76, "lblock": 77,
    "mcuica": 78, "ocuica": 79, "mtri": 80, "otri": 81, "shaker": 82,
}
DRUM_NAME = {v: k for k, v in DRUMS.items()}
LEN_WORD = {"w": 4 * PPQ, "h": 2 * PPQ, "q": PPQ, "e": PPQ // 2, "s": PPQ // 4, "t": PPQ // 8}
GRID_VEL = {"g": 40, "x": 90, "X": 118}
# the Groove MIDI Dataset's Roland TD-11 notes to General MIDI (the dataset's own mapping, simplified)
TD11 = {22: 42, 26: 46, 40: 38, 58: 43, 59: 51, 55: 49, 57: 49, 52: 49, 50: 50, 48: 48, 45: 45, 47: 45,
        43: 43, 37: 37, 38: 38, 36: 36, 42: 42, 44: 44, 46: 46, 49: 49, 51: 51, 53: 53}


class StyErr(Exception):
    pass


def default_track(i):
    """pattern_empty (project.c): the QY70's default tracks"""
    t = dict(program=0, chan=9, type=0, lo=0, hi=127, hikey=NOKEY, vol=100, pan=64, rev=40, cho=0, mute=0)
    if i == 3:
        t.update(program=33, chan=1, type=4, lo=28, hi=55, hikey=7)
    elif i >= 4:
        t.update(chan=i - 2, type=2, lo=48, hi=84)
        if i == 6:
            t.update(type=3, program=48)
    return t


# ------------------------------------------------------------------ values ---
def note_num(s, drums=False):
    """C4 = 60; Eb3, F#2; a number; a GM drum name"""
    s0 = s
    if s.lower() in DRUMS:
        return DRUMS[s.lower()]
    if re.fullmatch(r"\d+", s):
        return int(s)
    m = re.fullmatch(r"([A-Ga-g])([#b]?)(-?\d)", s)
    if not m:
        raise StyErr("not a note: " + s0)
    n = NOTE_PC[m.group(1).lower()] + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return n + 12 * (int(m.group(3)) + 1)


def note_name(n, drums):
    if drums and n in DRUM_NAME:
        return DRUM_NAME[n]
    return PC_NAME[n % 12] + str(n // 12 - 1)


def length(s):
    if re.fullmatch(r"\d+", s):
        return int(s)
    m = re.fullmatch(r"(\d*)([whqest])(\.?)", s)
    if not m:
        raise StyErr("not a length: " + s)
    v = LEN_WORD[m.group(2)] * (int(m.group(1)) if m.group(1) else 1)
    return v * 3 // 2 if m.group(3) else v


def pitch_class(s):
    if s.lower() == "off":
        return NOKEY
    m = re.fullmatch(r"([A-Ga-g])([#b]?)", s)
    if not m:
        raise StyErr("not a pitch class: " + s)
    return (NOTE_PC[m.group(1).lower()] + {"#": 1, "b": -1, "": 0}[m.group(2)]) % 12


def int_in(s, lo, hi, what):
    try:
        v = int(s)
    except ValueError:
        raise StyErr("%s: not a number: %s" % (what, s))
    if not lo <= v <= hi:
        raise StyErr("%s %d outside %d..%d" % (what, v, lo, hi))
    return v


# -------------------------------------------------------------------- MIDI ---
def read_midi(path):
    """(division, [(abs_tick, channel, note, velocity)] note-ons)"""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"MThd":
        raise StyErr(path + ": not a MIDI file")
    hlen, = struct.unpack(">I", d[4:8])
    _, ntrk, div = struct.unpack(">HHH", d[8:14])
    if div & 0x8000:
        raise StyErr(path + ": SMPTE time not supported")
    pos, notes = 8 + hlen, []
    for _ in range(ntrk):
        if d[pos:pos + 4] != b"MTrk":
            break
        tlen, = struct.unpack(">I", d[pos + 4:pos + 8])
        p, end, t, run = pos + 8, pos + 8 + tlen, 0, 0
        while p < end:
            v = 0
            while True:
                b = d[p]
                p += 1
                v = v << 7 | (b & 0x7F)
                if not b & 0x80:
                    break
            t += v
            st = d[p]
            if st & 0x80:
                p += 1
            else:
                st = run
            if st == 0xFF:
                ln, p = vlq(d, p + 1)
                p += ln
            elif st in (0xF0, 0xF7):
                ln, p = vlq(d, p)
                p += ln
            else:
                run = st
                n = 1 if st & 0xF0 in (0xC0, 0xD0) else 2
                a = d[p:p + n]
                p += n
                if st & 0xF0 == 0x90 and a[1] > 0:
                    notes.append((t, st & 15, a[0], a[1]))
        pos = end
    return div, notes


def vlq(d, p):
    v = 0
    while True:
        b = d[p]
        p += 1
        v = v << 7 | (b & 0x7F)
        if not b & 0x80:
            return v, p


def midi_drums(path, beats, bars, start, quant, velpct, only, skip, shift, scale, remap):
    """a drum loop from a MIDI file: `bars` bars from bar `start` (0-based), at PPQ ticks a beat, each hit on
    the nearest multiple of `quant` ticks (1: as played). shift: ticks of the file (at its own resolution) to
    move every hit by; "auto": the median distance from the beat of the window's hits near a beat (within a
    32nd), so a drummer playing behind or ahead of the click sits on the band's grid (straight or swung alike)"""
    div, notes = read_midi(path)
    n = bars * beats * PPQ
    off = start * beats * div
    win = [(t - off, TD11.get(note, note), vel) for t, ch, note, vel in notes
           if -div // 8 <= t - off < bars * beats * div]
    if shift == "auto":
        q = div
        d = sorted(x for x in ((t + q // 2) % q - q // 2 for t, g, v in win) if abs(x) <= div // 8)
        sh = -d[len(d) // 2] if d else 0
    else:
        sh = int(shift)
    got = {}
    for t, g, vel in win:
        if only and g not in only or g in skip:
            continue
        g = remap.get(g, g)
        tk = round(Fraction((t + sh) * PPQ, div * quant)) * quant
        if -EARLY <= tk < 0:
            tk = 0                       # a downbeat played early
        if not 0 <= tk < n - EARLY:      # the next window's downbeat played early: not ours
            continue
        v = max(1, min(127, round(vel * velpct * scale.get(g, 100) / 10000)))
        got[(tk, g)] = max(got.get((tk, g), 0), v)
    return [(tk, PPQ // 4, g, v) for (tk, g), v in sorted(got.items())]


EARLY = PPQ // 12                # a hit this early (two 288ths of a beat) still counts as on the downbeat
QUANT_DEFAULT = PPQ // 24       # the drums' grid by default: 24 a beat (straight and triplet 16ths, swing)


def drum_quant(s):
    """quant=: ticks (1..PPQ), a length name (s: sixteenths, e: eighths), or off (1 tick: the drummer's own
    timing); default QUANT_DEFAULT"""
    if s is None:
        return QUANT_DEFAULT
    if s.lower() == "off":
        return 1
    if re.fullmatch(r"\d+", s):
        return int_in(s, 1, PPQ, "quant")
    q = length(s)
    if not 1 <= q <= PPQ:
        raise StyErr("quant %s outside 1..%d ticks" % (s, PPQ))
    return q


def grid(path, bars=None):
    div, notes = read_midi(path)
    end = max(t for t, *_ in notes) if notes else 0
    nb = int(end * PPQ / div) // (4 * PPQ) + 1
    if bars:
        nb = min(nb, bars)
    rows = {}
    for t, ch, note, vel in notes:
        g = TD11.get(note, note)
        tk = round(t * 4 / div)          # 16ths
        if tk < nb * 16:
            r = rows.setdefault(g, [" "] * (nb * 16))
            c = "g" if vel < 50 else "x" if vel < 100 else "X"
            r[tk] = c if r[tk] == " " or c > r[tk] else r[tk]
    print("%s (%d PPQ, %d bars)" % (path, div, nb))
    for g in sorted(rows):
        s = "".join(rows[g])
        print("%-7s|%s|" % (DRUM_NAME.get(g, str(g)), "|".join(s[i:i + 16].replace(" ", ".") for i in range(0, len(s), 16))))


# ------------------------------------------------------------------- parse ---
def parse(path, text=None):
    """a style: {name, tempo, hdr, pats: [ {bars, beats, trk: [hdr dict], ev: [[(tick, len, note, vel)] per track]}
    x6 ]}; a section not written is empty (1 bar of Main A's beats, the style's tracks, no notes: "empty")"""
    if text is None:
        with open(path) as f:
            text = f.read()
    base = os.path.dirname(path)
    style = {"name": None, "tempo": 0, "pats": [None] * NSECT, "file": path}
    hdr = [default_track(i) for i in range(NTRK)]    # the style's tracks, before any section
    style["hdr"] = hdr
    pat = None
    trk = None
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = re.sub(r"(^|\s)#.*", "", raw).strip()   # a comment: "#" at the start or after a space (F#4 is a note)
        if not line:
            continue
        try:
            w = line.split()
            k = w[0].lower()
            if line.startswith("["):
                m = re.fullmatch(r"\[\s*([a-z ]+?)\s*\]", line.lower())
                name = m and re.sub(r"\s+", " ", m.group(1))
                name = SECTION_ALIAS.get(name, name)
                if not m or name not in SECTIONS:
                    raise StyErr("unknown section %s (%s)" % (line, " ".join("[%s]" % x for x in SECTIONS)))
                si = SECTIONS.index(name)
                if style["pats"][si]:
                    raise StyErr("section twice: " + line)
                pat = {"bars": 0, "beats": 4, "trk": [dict(h) for h in hdr], "ev": [[] for _ in range(NTRK)],
                       "line": lineno}
                style["pats"][si] = pat
                trk = None
            elif k == "style":
                style["name"] = line.split(None, 1)[1].strip()
                if not re.fullmatch(r"[ -~]{1,8}", style["name"]):
                    raise StyErr("style name: 1..8 printable ASCII characters, not " + style["name"])
            elif k == "tempo":
                style["tempo"] = int_in(w[1], 40, 240, "tempo")
            elif k == "bars" and pat:
                pat["bars"] = int_in(w[1], 1, MAXBARS, "bars")
            elif k == "beats" and pat:
                pat["beats"] = int_in(w[1], 2, 7, "beats")
            elif k in ("bars", "beats"):
                raise StyErr(k + " outside a section")
            elif k == "track":
                name = w[1].lower()
                if name in TRACKS:
                    trk = TRACKS.index(name)
                elif re.fullmatch(r"[1-8]", name):
                    trk = int(name) - 1
                else:
                    raise StyErr("no track " + w[1] + " (d1 d2 pc ba c1 c2 c3 c4)")
                h = (pat["trk"] if pat else hdr)[trk]
                for kv in w[2:]:
                    if "=" not in kv:
                        raise StyErr("expected key=value: " + kv)
                    a, v = kv.split("=", 1)
                    a = a.lower()
                    if a == "type":
                        if v.lower() not in TYPES:
                            raise StyErr("type: one of " + " ".join(TYPES))
                        h["type"] = TYPES.index(v.lower())
                    elif a == "program":
                        h["program"] = int_in(v, 0, 127, "program")
                    elif a == "chan":
                        h["chan"] = int_in(v, 1, 16, "chan") - 1
                    elif a in ("lo", "hi"):
                        h[a] = note_num(v)
                        if not 0 <= h[a] <= 127:
                            raise StyErr(a + " outside 0..127")
                    elif a == "hikey":
                        h["hikey"] = pitch_class(v)
                    elif a in ("vol", "pan", "rev", "cho"):
                        h[a] = int_in(v, 0, 127, a)
                    elif a == "mute":
                        h["mute"] = int_in(v, 0, 1, a)
                    else:
                        raise StyErr("unknown track field " + a)
            elif pat is None:
                raise StyErr("notes before a section: " + line)
            elif trk is None:
                raise StyErr("notes before a track line")
            elif not pat["bars"]:
                raise StyErr("notes before the section's bars")
            elif k == "drums":
                opt = dict(kv.split("=", 1) for kv in w[1:])
                src = opt.get("from")
                if not src:
                    raise StyErr("drums: from=FILE.mid")
                for name in opt:
                    if name not in ("from", "bars", "start", "quant", "vel", "only", "skip", "at", "shift", "scale", "map"):
                        raise StyErr("drums: unknown option " + name)
                nb = int_in(opt.get("bars", str(pat["bars"])), 1, MAXBARS, "bars")
                at = int_in(opt.get("at", "1"), 1, MAXBARS, "at") - 1
                names = lambda s: {note_num(x) for x in s.split(",")} if s else set()
                ev = midi_drums(os.path.join(base, src), pat["beats"], nb, int(opt.get("start", "0")),
                                drum_quant(opt.get("quant")), int(opt.get("vel", "100")),
                                names(opt.get("only")), names(opt.get("skip")), opt.get("shift", "auto"),
                                {note_num(a): int(b) for a, b in (x.split(":") for x in opt["scale"].split(","))}
                                if opt.get("scale") else {},
                                {note_num(a): note_num(b) for a, b in (x.split(":") for x in opt["map"].split(","))}
                                if opt.get("map") else {})
                o = at * pat["beats"] * PPQ
                pat["ev"][trk] += [(t + o, l, n, v, lineno) for t, l, n, v in ev]
            elif k == "grid":
                # grid POS STEP NOTES CHARS [len=L]: a step sequence, one character a step
                glen = None
                if w[-1].startswith("len="):
                    glen = length(w[-1][4:])
                    w = w[:-1]
                if len(w) < 5:
                    raise StyErr("grid POS STEP NOTES STEPS [len=L]")
                t0 = position(w[1], pat)
                step = length(w[2])
                notes = [note_num(x) for x in w[3].split("+")]
                n = 0
                for c in "".join(w[4:]):
                    if c == "|":
                        continue
                    if c in GRID_VEL or c.isdigit() and c != "0":
                        v = GRID_VEL[c] if c in GRID_VEL else int(c) * 14
                        for note in notes:
                            pat["ev"][trk].append((t0 + n * step, glen or step, note, v, lineno))
                    elif c not in ".-":
                        raise StyErr("grid step: . - g x X 1..9, not " + c)
                    n += 1
            else:
                # POS NOTES LEN VEL [xN STEP]
                if len(w) not in (4, 6):
                    raise StyErr("expected POS NOTES LEN VEL [xN STEP]: " + line)
                t0 = position(w[0], pat)
                notes = [note_num(x) for x in w[1].split("+")]
                nlen = length(w[2])
                vels = [int_in(x, 1, 127, "velocity") for x in w[3].split("/")]
                reps, step = 1, 0
                if len(w) == 6:
                    if not re.fullmatch(r"x\d+", w[4]):
                        raise StyErr("repeat: xN STEP")
                    reps, step = int(w[4][1:]), length(w[5])
                for r in range(reps):
                    for n in notes:
                        pat["ev"][trk].append((t0 + r * step, nlen, n, vels[r % len(vels)], lineno))
        except StyErr as e:
            raise StyErr("%s:%d: %s" % (path, lineno, e))
        except OSError as e:
            raise StyErr("%s:%d: %s: %s" % (path, lineno, e.filename, e.strerror))
        except (IndexError, ValueError, TypeError, KeyError) as e:
            raise StyErr("%s:%d: cannot read %r (%s)" % (path, lineno, raw.strip(), e))
    if not style["name"]:
        raise StyErr(path + ": no style NAME line")
    if not style["pats"][MAIN_A]:
        raise StyErr("%s: no [main a] section" % path)
    for i, p in enumerate(style["pats"]):
        if not p:                       # not written: empty (the engine falls back: arr.h arr_section)
            style["pats"][i] = {"bars": 1, "beats": style["pats"][MAIN_A]["beats"], "trk": [dict(h) for h in hdr],
                                "ev": [[] for _ in range(NTRK)], "line": 0, "empty": True}
            continue
        if not p["bars"]:
            raise StyErr("%s: [%s] has no bars" % (path, SECTIONS[i]))
        for t in range(NTRK):           # sorted by tick, then note; the same note on the same tick once
            seen = {}
            for e in p["ev"][t]:
                seen[(e[0], e[2])] = e
            p["ev"][t] = sorted(seen.values(), key=lambda e: (e[0], e[2]))
    return style


def position(s, pat):
    """bar.beat[.tick], 1-based bar and beat"""
    m = re.fullmatch(r"(\d+)\.(\d+)(?:\.(\d+))?", s)
    if not m:
        raise StyErr("not a position (bar.beat.tick): " + s)
    bar, beat, tick = int(m.group(1)), int(m.group(2)), int(m.group(3) or 0)
    if not 1 <= bar <= pat["bars"] or not 1 <= beat <= pat["beats"] or tick >= PPQ:
        raise StyErr("position %s outside %d bars of %d beats" % (s, pat["bars"], pat["beats"]))
    return ((bar - 1) * pat["beats"] + beat - 1) * PPQ + tick


def check(style):
    errs = []
    for i, p in enumerate(style["pats"]):
        n = p["bars"] * p["beats"] * PPQ
        tot = sum(len(e) for e in p["ev"])
        f = style["file"]
        if tot > NEV:
            errs.append("%s:%d: [%s] has %d notes, more than %d (ARR_NEV)" % (f, p["line"], SECTIONS[i], tot, NEV))
        for t in range(NTRK):
            for tk, ln, note, vel, at in p["ev"][t]:
                if not 0 <= note <= 127:
                    errs.append("%s:%d: %s: note %d outside 0..127" % (f, at, TRACKS[t], note))
                if not 0 <= tk < n:
                    errs.append("%s:%d: %s: tick %d past the pattern's end" % (f, at, TRACKS[t], tk))
                if not 1 <= ln <= n:
                    errs.append("%s:%d: %s: length %d outside 1..%d" % (f, at, TRACKS[t], ln, n))
                elif ln > LEN_MAX:
                    errs.append("%s:%d: %s: length %d longer than the tables hold (%d ticks)" % (
                        f, at, TRACKS[t], ln, LEN_MAX))
                elif len_of(len_code(ln)) != ln:
                    print("make_styles: %s:%d: %s: length %d kept as %d in the tables" % (
                        f, at, TRACKS[t], ln, len_of(len_code(ln))), file=sys.stderr)
                if not 1 <= vel <= 127:
                    errs.append("%s:%d: %s: velocity %d outside 1..127" % (f, at, TRACKS[t], vel))
    return errs


# -------------------------------------------------------------------- dump ---
def fmt_pos(t, p):
    beat, tick = divmod(t, PPQ)
    bar, beat = divmod(beat, p["beats"])
    return "%d.%d.%d" % (bar + 1, beat + 1, tick)


def fmt_track(i, h):
    return "track %s type=%s program=%d chan=%d lo=%d hi=%d hikey=%s vol=%d pan=%d rev=%d cho=%d mute=%d" % (
        TRACKS[i], TYPES[h["type"]], h["program"], h["chan"] + 1, h["lo"], h["hi"],
        "off" if h["hikey"] >= NOKEY else PC_NAME[h["hikey"]], h["vol"], h["pan"], h["rev"], h["cho"], h["mute"])


def dump(style):
    out = ["# dumped by tools/make_styles.py from " + os.path.basename(style["file"]), "style " + style["name"]]
    if style["tempo"]:
        out.append("tempo %d" % style["tempo"])
    for t in range(NTRK):               # the style's tracks (an empty section's)
        out.append(fmt_track(t, style["hdr"][t]))
    for i, p in enumerate(style["pats"]):
        if p.get("empty"):
            continue
        out += ["", "[%s]" % SECTIONS[i], "bars %d" % p["bars"], "beats %d" % p["beats"]]
        for t in range(NTRK):
            out.append(fmt_track(t, p["trk"][t]))
            drums = p["trk"][t]["type"] == 0 and p["trk"][t]["chan"] == 9
            for tk, ln, note, vel, _ in p["ev"][t]:
                out.append("%s %s %d %d" % (fmt_pos(tk, p), note_name(note, drums), ln, vel))
    return "\n".join(out) + "\n"


# ----------------------------------------------------------------------- C ---
def len_code(ln):
    """a length's 11-bit code: 1..1020 exact (0..1019); longer, the nearest multiple of LEN_STEP (to LEN_MAX)"""
    if ln <= LEN_EXACT:
        return ln - 1
    if ln > LEN_MAX:                    # check() refuses these
        raise StyErr("length %d longer than the tables hold (%d ticks)" % (ln, LEN_MAX))
    return LEN_EXACT - 1 + max(1, (ln - LEN_EXACT + LEN_STEP // 2) // LEN_STEP)


def len_of(c):
    """the length a code stands for (arr.c's sty_len)"""
    return c + 1 if c < LEN_EXACT else LEN_EXACT + (c - (LEN_EXACT - 1)) * LEN_STEP


def tables(styles):
    """(pattern rows, events, velocities): the data styles.h holds. An event: tick (15 bits) | note << 15 |
    the length code's low 10 bits << 22 (STY_EV); its velocity | the code's 11th bit << 7 (STY_VEL)"""
    pats, ev, vel = [], [], []
    for s in styles:
        for p in s["pats"]:
            first = len(ev)
            trks = []
            for t in range(NTRK):
                h = p["trk"][t]
                trks.append((h["program"], h["chan"], h["type"], h["lo"], h["hi"], h["hikey"], h["vol"], h["pan"],
                             h["rev"], h["cho"], h["mute"], len(p["ev"][t])))
                for tk, ln, note, v, _ in p["ev"][t]:
                    c = len_code(ln)
                    ev.append(tk | note << 15 | (c & 0x3FF) << 22)
                    vel.append(v | (c >> 10) << 7)
            pats.append((p["bars"], p["beats"], first, trks))
    return pats, ev, vel


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_comment(s):
    return s.replace("*/", "* /")


def emit(styles, files):
    pats, ev, vel = tables(styles)
    o = ["/* SPDX-License-Identifier: GPL-3.0-only AND CC-BY-4.0 */",
         "/* Polly's factory styles, generated by tools/make_styles.py from %s: do not edit. */"
         % c_comment(" ".join(os.path.basename(f) for f in files)),
         "/* The drum performances in these tables are from the Groove MIDI Dataset (c) Google LLC, CC BY 4.0,",
         " * changed (excerpted, quantised); the rest is Polly's, GPL-3.0-only. See LICENSING.md, styles/README.md. */",
         "/* Included once, by arr.c. Style s's patterns are pattern numbers ARR_FACTORY0 + %ds + section (Intro, Main"
         % NSECT,
         " * A, Main B, Fill AB, Fill BA, Ending; a section not written: no notes). An event (%d ticks a beat): tick | note << 15 | the length code's low 10 bits"
         " << 22" % PPQ,
         " * (STY_EV); its velocity | the code's 11th bit << 7 (STY_VEL). The code: lengths 1..%d exact (code"
         " %d..%d)," % (LEN_EXACT, 0, LEN_EXACT - 1),
         " * longer ones in steps of %d ticks (sty_len). */" % LEN_STEP,
         "#pragma once", "#include <stdint.h>", "",
         "typedef struct {",
         "    uint8_t program, chan, type, lo, hi, hikey, vol, pan, rev, cho, mute, rsv;",
         "    uint16_t count;                      /* its events, after the previous track's */",
         "} sty_trk_t;",
         "typedef struct {",
         "    uint8_t bars, beats;",
         "    uint16_t first;                      /* its first event in STY_EV */",
         "    sty_trk_t trk[8];",
         "} sty_pat_t;",
         "static inline uint32_t sty_len(uint32_t c) { return c < %du ? c + 1u : %du + (c - %du) * %du; }"
         % (LEN_EXACT, LEN_EXACT, LEN_EXACT - 1, LEN_STEP), "",
         "#define STY_COUNT %d" % len(styles),
         "#define STY_NSECT %d" % NSECT,
         "#define STY_NEV %d" % len(ev), ""]
    o.append("static const char STY_NAME[STY_COUNT][9] = {%s};" % ", ".join(c_str(s["name"]) for s in styles))
    o.append("static const uint8_t STY_TEMPO[STY_COUNT] = {%s};" % ", ".join(str(s["tempo"] or 120) for s in styles))
    o.append("static const sty_pat_t STY_PAT[STY_COUNT * STY_NSECT] = {")
    for i, (bars, beats, first, trks) in enumerate(pats):
        o.append("    {%d, %d, %d, {   /* %s %s */" % (bars, beats, first, c_comment(styles[i // NSECT]["name"]),
                                                          SECTIONS[i % NSECT] + (" (empty)" if styles[i // NSECT]["pats"][i % NSECT].get("empty") else "")))
        for t in trks:
            o.append("        {%s, 0, %d}," % (", ".join(str(x) for x in t[:11]), t[11]))
        o.append("    }},")
    o.append("};")
    o.append("static const uint32_t STY_EV[STY_NEV] = {")
    for i in range(0, len(ev), 8):
        o.append("    " + ", ".join("0x%08Xu" % x for x in ev[i:i + 8]) + ",")
    o.append("};")
    o.append("static const uint8_t STY_VEL[STY_NEV] = {")
    for i in range(0, len(vel), 24):
        o.append("    " + ", ".join(str(x) for x in vel[i:i + 24]) + ",")
    o.append("};")
    return "\n".join(o) + "\n"


def sizes(style):
    n = sum(len(e) for p in style["pats"] for e in p["ev"])
    return n, n * EV_BYTES + NSECT * PAT_BYTES + 9 + 1


def ordered(files):
    """the style files in their numbers' order: NN-name.sty is style NN, numbered 1.. with no gaps (a song
    stores style numbers: new styles go at the end, none is ever renumbered)"""
    num = {}
    for f in files:
        m = re.match(r"(\d+)-", os.path.basename(f))
        if not m:
            raise StyErr("%s: the name must start with its style number (NN-name.sty)" % f)
        n = int(m.group(1))
        if n in num:
            raise StyErr("%s: style %d twice (also %s)" % (f, n, num[n]))
        num[n] = f
    for i in range(1, len(num) + 1):
        if i not in num:
            raise StyErr("no style %d: styles are numbered 1..%d with no gaps" % (i, len(num)))
    return [num[i] for i in sorted(num)]


def main(argv):
    mode = "emit"
    if argv and argv[0] in ("--check", "--dump", "--roundtrip", "--grid", "--sizes"):
        mode, argv = argv[0][2:], argv[1:]
    if mode == "grid":
        grid(argv[0], int(argv[1]) if len(argv) > 1 else None)
        return 0
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    if mode in ("emit", "check"):
        try:
            argv = ordered(argv)
        except StyErr as e:
            print("make_styles: %s" % e, file=sys.stderr)
            return 1
    try:
        styles = [parse(f) for f in argv]
    except (StyErr, OSError) as e:
        print("make_styles: %s" % e, file=sys.stderr)
        return 1
    errs = [e for s in styles for e in check(s)]
    total = sum(len(e) for s in styles for p in s["pats"] for e in p["ev"])
    if len(styles) > MAX_FACTORY:
        errs.append("%d styles: a song's pattern numbers hold %d factory styles at most" % (len(styles), MAX_FACTORY))
    if total > 0xFFFF:
        errs.append("%d notes in all, more than the tables' 65535" % total)
    if errs:
        for e in errs:
            print("make_styles: " + e, file=sys.stderr)
        return 1
    if mode == "check":
        print("%d styles, %d patterns ok" % (len(styles), NSECT * len(styles)))
    elif mode == "dump":
        for s in styles:
            sys.stdout.write(dump(s))
    elif mode == "roundtrip":
        for s in styles:
            again = parse(s["file"] + " (dumped)", dump(s))
            if tables([again]) != tables([s]) or again["name"] != s["name"] or again["tempo"] != s["tempo"]:
                print("make_styles: %s: dump -> parse differs" % s["file"], file=sys.stderr)
                return 1
        print("%d styles round trip ok" % len(styles))
    elif mode == "sizes":
        tot = 0
        for s in styles:
            n, b = sizes(s)
            tot += b
            print("%-8s %4d events  %5d bytes  (%s)" % (s["name"], n, b, " ".join(
                "%s %d" % (SECTIONS[i], sum(len(e) for e in p["ev"])) for i, p in enumerate(s["pats"]))))
        print("total %d bytes for %d styles; 16 at this average: %d bytes" % (tot, len(styles), tot * 16 // len(styles)))
    else:
        sys.stdout.write(emit(styles, argv))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
