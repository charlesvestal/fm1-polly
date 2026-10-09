#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The QY70's preset styles as Polly .sty files, read from your own QY70 main ROM (bring your own ROM).

    python3 tools/qy70_import.py qy70main.bin --list                   # the 128 preset styles
    python3 tools/qy70_import.py qy70main.bin --style Bossa --out DIR   # one style -> DIR/NN-bossa.sty
    python3 tools/qy70_import.py qy70main.bin --all --out DIR           # all of them
    python3 tools/qy70_import.py qy70main.bin --phrase Bossa-1a         # one phrase's events, decoded

Options: --first N (the first file number, default 17: after Polly's 16 factory styles; --style takes
--number N), --legacy (Polly's 24 ticks a beat and four sections: Main A, Main B, Fill = Fill AB, Ending),
--max-notes N (a pattern's note limit, default 600: past it the quietest short notes go, and the tool says so),
--report (per style: notes, bytes, what was lost).

No Yamaha data is in this file: it finds the tables in the ROM you give it (docs/qy70-import.md has the
format, as we found it), and writes the styles to your disk for your own use. Python 3 standard library only.
"""
import argparse
import os
import re
import struct

QPPQ = 480                       # the QY70's ticks a beat
REC = 0x222                      # a style record (the user pattern bulk header's layout)
NSTYLES = 128
SECTIONS = ["intro", "main a", "main b", "fill ab", "fill ba", "ending"]
TRACKS = ["d1", "d2", "pc", "ba", "c1", "c2", "c3", "c4"]
CATS = {1: "Da", 2: "Db", 5: "Fa", 6: "Fb", 9: "PC", 13: "Ba", 14: "Bb", 18: "Ga", 19: "Gb", 20: "GR",
        23: "KC", 24: "KR", 28: "PD", 29: "BR", 30: "SE"}
# a phrase's TYPE byte -> Polly's type (tools/make_styles.py TYPES)
QTYPE = {0x03: "bypass", 0x90: "chord1", 0x92: "bass", 0x94: "parallel", 0xA0: "chord2"}

# Source chord types (the QY70's codes 0..26) as semitones over the root for R 9 3 11 5 13 7 (Polly's degree
# order, chords.c DEG: a triad's 7th is 12, a 6 chord's 7th its 6th)
QCHORD = [
    ("M7", [0, 2, 4, 5, 7, 9, 11]), ("M", [0, 2, 4, 5, 7, 9, 12]), ("6", [0, 2, 4, 5, 7, 9, 9]),
    ("m7(11)", [0, 2, 3, 5, 7, 9, 10]), ("M7(9)", [0, 2, 4, 5, 7, 9, 11]), ("add9", [0, 2, 4, 5, 7, 9, 12]),
    ("m", [0, 2, 3, 5, 7, 9, 12]), ("m6", [0, 2, 3, 5, 7, 9, 9]), ("m7", [0, 2, 3, 5, 7, 9, 10]),
    ("m7(b5)", [0, 1, 3, 5, 6, 8, 10]), ("mM7", [0, 2, 3, 5, 7, 9, 11]), ("m7(9)", [0, 2, 3, 5, 7, 9, 10]),
    ("madd9", [0, 2, 3, 5, 7, 9, 12]), ("7", [0, 2, 4, 5, 7, 9, 10]), ("7(#5)", [0, 2, 4, 5, 8, 9, 10]),
    ("7(b9)", [0, 1, 4, 5, 7, 9, 10]), ("7(9)", [0, 2, 4, 5, 7, 9, 10]), ("7(#9)", [0, 3, 4, 5, 7, 9, 10]),
    ("7(#11)", [0, 2, 4, 6, 7, 9, 10]), ("7(b13)", [0, 2, 4, 5, 7, 8, 10]), ("7(13)", [0, 2, 4, 5, 7, 9, 10]),
    ("7sus4", [0, 2, 5, 5, 7, 9, 10]), ("sus4", [0, 2, 5, 5, 7, 9, 12]), ("dim", [0, 2, 3, 5, 6, 8, 12]),
    ("aug", [0, 2, 4, 6, 8, 9, 12]), ("6(9)", [0, 2, 4, 5, 7, 9, 9]), ("7(b5)", [0, 2, 4, 5, 6, 9, 10]),
]
CM7 = [0, 2, 4, 5, 7, 9, 11]
DEG_ORDER = [0, 2, 4, 6, 1, 3, 5]          # R 3 5 7 first, then 9 11 13 (a note matching two degrees)
NOTE = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]

# GM percussion names, as tools/make_styles.py reads them
DRUMS = {
    35: "kick2", 36: "kick", 37: "rim", 38: "snare", 39: "clap", 40: "esnare", 41: "ftom2", 42: "chh",
    43: "ftom", 44: "phh", 45: "ltom", 46: "ohh", 47: "lmtom", 48: "hmtom", 49: "crash", 50: "htom",
    51: "ride", 52: "china", 53: "bell", 54: "tamb", 55: "splash", 56: "cowbell", 57: "crash2",
    58: "vibraslap", 59: "ride2", 60: "hbongo", 61: "lbongo", 62: "mhconga", 63: "hconga", 64: "lconga",
    65: "htimbale", 66: "ltimbale", 67: "hagogo", 68: "lagogo", 69: "cabasa", 70: "maracas",
    71: "swhistle", 72: "lwhistle", 73: "sguiro", 74: "lguiro", 75: "claves", 76: "hblock", 77: "lblock",
    78: "mcuica", 79: "ocuica", 80: "mtri", 81: "otri",
}
# XG drum keys outside General MIDI's 35..81, to the nearest GM sound (velocity percent)
XG_DRUM = {
    13: (41, 100), 14: (43, 100),            # surdo mute / open -> floor toms
    15: (76, 100), 16: (39, 100),            # high Q -> wood block, whip slap -> clap
    17: (69, 100), 18: (69, 100),            # scratch push / pull -> cabasa
    19: (39, 80), 20: (37, 70),              # finger snap -> clap, click noise -> side stick
    21: (76, 70), 22: (53, 70),              # metronome click -> wood block, bell -> ride bell
    23: (77, 70), 24: (76, 70),              # sequencer clicks
    25: (38, 50), 26: (38, 40), 27: (38, 70), 28: (38, 40),   # brush tap / swirl / slap / tap swirl
    29: (38, 80), 30: (75, 100),             # snare roll -> snare, castanets -> claves
    31: (38, 70), 32: (37, 100),             # snare soft, sticks -> side stick
    33: (35, 100), 34: (37, 120),            # kick soft -> kick 2, open rim shot -> side stick
    82: (70, 100), 83: (54, 80), 84: (81, 80),            # shaker, jingle bells, bell tree
    85: (75, 100), 86: (41, 100), 87: (43, 100),          # castanets, surdos
}
# XG SFX voices (bank MSB 64) and anything else without a GM program: GM's own effects, by range
def sfx_program(prog):
    return 120 + min(7, prog // 16)


class RomError(Exception):
    pass


# ---------------------------------------------------------------- the ROM's tables

class Rom:
    def __init__(self, data):
        self.d = data
        self.find_phrases()
        self.find_styles()

    def u16(self, o):
        return struct.unpack_from(">H", self.d, o)[0]

    def u32(self, o):
        return struct.unpack_from(">I", self.d, o)[0]

    def find_phrases(self):
        """the phrase pointer table: N big-endian addresses, the first just past the table's end, increasing;
        16 bytes a category before it: the first phrase number of each (category, beat)"""
        d, n = self.d, len(self.d)
        for t in range(0, n - 16, 2):
            v = self.u32(t)
            if v <= t or v >= n or (v - t) % 4 or not 3000 <= (v - t) // 4 <= 6000:
                continue
            cnt = (v - t) // 4
            ptr = [self.u32(t + 4 * i) for i in range(cnt)]
            if all(ptr[i] < ptr[i + 1] < n for i in range(cnt - 1)) and \
                    all(32 <= c < 127 for c in d[ptr[0]:ptr[0] + 12]) and d[ptr[1] - 1] == 0xF2:
                self.ptab, self.nph, self.ptr = t, cnt, ptr + [self.phrase_end(ptr[-1])]
                break
        else:
            raise RomError("no phrase table (is this the QY70's main ROM, qy70main.bin?)")
        base = self.ptab - 0x1FE                  # the (category, beat) starts: 16 bytes a category
        self.cstart = {k: [self.u16(base + 16 * k + 2 * j) for j in range(3)] for k in CATS}
        for k, v in self.cstart.items():
            if not all(x <= self.nph for x in v) or v != sorted(v):
                raise RomError("the phrase category table is not where it was expected")
        self.pname = [d[p:p + 12].decode("latin-1") for p in self.ptr[:self.nph]]

    def phrase_end(self, p):
        e = self.d.find(b"\xF2", p + 28)
        return e + 1

    def find_styles(self):
        d = self.d
        for m in re.finditer(rb"\x00\x00\x00\x00[\x20-\x7e]{8}[\x14\x1c\x2c]", d):
            o = m.start() - 2
            if all(self.style_ok(o + REC * k) for k in range(NSTYLES)):
                self.sbase = o
                return
        raise RomError("no style table (is this the QY70's main ROM, qy70main.bin?)")

    def style_ok(self, o):
        d = self.d
        return (o + REC <= len(d) and d[o + 2:o + 6] == b"\0\0\0\0" and all(32 <= c < 127 for c in d[o + 6:o + 14])
                and d[o + 14] in (0x14, 0x1C, 0x2C) and 250 <= self.u16(o) <= 3000
                and all(d[o + 15 + s] < 32 for s in range(6)))

    # ---- phrases

    def phrase_number(self, flag, num):
        if flag == 0xFE:
            return None
        cat, beat = flag >> 3, (flag & 7) - 1
        if cat not in self.cstart or not 0 <= beat < 3:
            raise RomError("phrase reference %02x %02x: unknown category" % (flag, num))
        i = self.cstart[cat][beat] + num
        if i >= self.nph:
            raise RomError("phrase reference %02x %02x: past the end" % (flag, num))
        return i

    def phrase(self, i):
        """{name, bars, num, msb, lsb, prog, hi, lo, hikey, type, root, chord, tempo, notes: [(t, gate, key, vel)],
        other: {kind: count}}"""
        d, p = self.d, self.ptr[i]
        h = d[p + 12:p + 28]
        ph = dict(name=self.pname[i].rstrip(), bars=h[0] + 1, num=(h[1] >> 3) + 1, msb=h[2], lsb=h[3], prog=h[4],
                  hi=h[5], lo=h[6], hikey=h[7], type=h[8], root=h[9], chord=h[10],
                  tempo=(h[12] << 8 | h[13]) / 10.0, notes=[], other={})
        q, t, notes, other = p + 28, 0, ph["notes"], ph["other"]
        while True:
            b = d[q]
            if b == 0xF2:
                break
            if b < 0x80:
                raise RomError("phrase %d (%s): a data byte where an event was expected" % (i, ph["name"]))
            if b < 0xA0:
                t += b & 0x1F
                q += 1
            elif b < 0xC0:
                t += (b & 0x1F) << 7 | d[q + 1]
                q += 2
            elif b < 0xD0:
                notes.append((t, b & 0x0F, d[q + 1], d[q + 2]))
                q += 3
            elif b < 0xE0:
                notes.append((t, (b & 0x0F) << 7 | d[q + 1], d[q + 2], d[q + 3]))
                q += 4
            elif b < 0xF0:
                notes.append((t, (b & 0x0F) << 14 | d[q + 1] << 7 | d[q + 2], d[q + 3], d[q + 4]))
                q += 5
            else:
                n = {0xFB: 3, 0xFE: 3, 0xFD: 2, 0xF3: 6, 0xF8: 1, 0xF0: 2}.get(b)
                if n is None:
                    raise RomError("phrase %d (%s): unknown event %02x" % (i, ph["name"], b))
                kind = {0xFB: "control change", 0xFE: "pitch bend", 0xFD: "pitch bend (coarse)"}.get(b, "other")
                other[kind] = other.get(kind, 0) + 1
                q += n
        return ph

    def category(self, i):
        """'Db 8beat': the category and beat of phrase i"""
        best = max(((v[j], CATS[k], j) for k, v in self.cstart.items() for j in range(3) if v[j] <= i))
        return "%s %s" % (best[1], ["16beat", "8beat", "3/4"][best[2]])

    # ---- styles

    def style(self, k):
        d, o = self.d, self.sbase + REC * k
        st = dict(index=k, name=d[o + 6:o + 14].decode("ascii").rstrip(), tempo=self.u16(o) / 10.0,
                  num=(d[o + 14] >> 3) + 1, bars=[d[o + 15 + s] + 1 for s in range(6)])
        st["phrase"] = [[self.phrase_number(d[o + 21 + s * 8 + t], d[o + 69 + s * 8 + t]) for t in range(8)]
                        for s in range(6)]
        st["voice"] = [(d[o + 138 + t], d[o + 146 + t], d[o + 154 + t]) for t in range(8)]
        for f, off in (("vol", 170), ("pan", 178), ("rev", 202), ("cho", 210)):
            st[f] = [d[o + off + t] for t in range(8)]
        return st

    def styles(self):
        return [self.style(k) for k in range(NSTYLES)]


# ---------------------------------------------------------------- conversion

def gm_program(msb, lsb, prog):
    """(GM program, drums?, note) for an XG voice"""
    if msb in (126, 127):
        return 0, True, ""
    if msb == 64:
        return sfx_program(prog), False, "XG SFX voice %d -> GM %d" % (prog, sfx_program(prog))
    if msb == 0 and lsb == 0:
        return prog, False, ""
    return prog, False, "XG bank %d/%d program %d -> GM %d" % (msb, lsb, prog, prog)


def to_cm7(note, typ, root, chord):
    """a note of a phrase written over (root, chord) moved to the same degree over C M7, near where it was"""
    base = -root if root <= 6 else 12 - root
    if typ == "parallel":
        return note + base
    semis = QCHORD[chord][1] if chord < len(QCHORD) else CM7
    rel = (note - root) % 12
    deg, off = None, 0
    for dg in DEG_ORDER:
        if semis[dg] % 12 == rel and not (semis[dg] == 12 and dg == 6):
            deg = dg
            break
    if deg is None:                                   # chromatic: just above the highest degree below it
        cands = [(semis[dg], dg) for dg in range(7) if semis[dg] < 12 and semis[dg] <= rel]
        s, deg = max(cands)
        off = rel - s
    pc = (CM7[deg] + off) % 12
    dlt = (pc - note) % 12
    if abs(dlt - 12 - base) < abs(dlt - base):
        dlt -= 12
    return note + dlt


def note_name(n):
    return "%s%d" % (NOTE[n % 12], n // 12 - 1)


def pos(t, beats, ppq):
    bar, r = divmod(t, beats * ppq)
    beat, tick = divmod(r, ppq)
    return "%d.%d.%d" % (bar + 1, beat + 1, tick)


def convert(rom, st, ppq=QPPQ, legacy=False, max_notes=600):
    """a style's .sty text, and a report"""
    rep = dict(notes=0, dropped=0, moved=0, err=0, events_lost={}, cut=0, notes_by_sect=[], voices=set(),
               drums_mapped=set(), sections_dropped=[])
    secs = [1, 2, 3, 5] if legacy else list(range(6))
    names = ["main a", "main b", "fill", "ending"] if legacy else SECTIONS
    if legacy:
        rep["sections_dropped"] = ["intro", "fill ba"]
    beats = st["num"]
    out = ["# %s: the QY70's preset style %d, imported from your ROM by tools/qy70_import.py." % (st["name"], st["index"] + 1),
           "# For your own use: the patterns are Yamaha's.",
           "style %s" % st["name"], "tempo %d" % max(40, min(240, round(st["tempo"]))), ""]

    # the phrases each track plays, per section
    ph = {}
    for s in secs:
        for t in range(8):
            i = st["phrase"][s][t]
            if i is not None and i not in ph:
                ph[i] = rom.phrase(i)

    # a track's sound: the style's track voice, else its phrase's (the first section that has one)
    def voice(t, i):
        v = st["voice"][t]
        if v != (0, 0, 0):
            return v
        p = ph[i]
        return (p["msb"], p["lsb"], p["prog"])

    first = {}
    for t in range(8):                   # the tracks' defaults: Main A's phrase, else Main B's, ...
        for s in sorted(secs, key=lambda s: [1, 2, 3, 4, 0, 5].index(s)):
            if st["phrase"][s][t] is not None:
                first[t] = st["phrase"][s][t]
                break
    # the drum channel's mixer: the first drum track's (d1, d2, pc share channel 10 in Polly)
    drum_trk = [t for t in first if gm_program(*voice(t, first[t]))[1]]
    dref = drum_trk[0] if drum_trk else 0
    dvol = max(1, st["vol"][dref])
    chan_free = [7, 8, 9, 11, 12]
    trk_chan, trk_drum = {}, {}
    for t in range(8):
        if t not in first:
            continue
        prog, drum, why = gm_program(*voice(t, first[t]))
        trk_drum[t] = drum
        if drum:
            trk_chan[t] = 10
        elif t >= 3:
            trk_chan[t] = t - 1          # ba 2, c1..c4 3..6 (Polly's defaults)
        else:
            trk_chan[t] = chan_free.pop(0)
        p = ph[first[t]]
        typ = "bypass" if drum else QTYPE.get(p["type"], "chord1")
        mix = dref if drum else t
        line = "track %s type=%s chan=%d" % (TRACKS[t], typ, trk_chan[t])
        if not drum:
            line += " program=%d lo=%d hi=%d" % (prog, p["lo"], p["hi"])
            if typ in ("chord1", "bass"):
                line += " hikey=%s" % NOTE[p["hikey"] % 12]
        line += " vol=%d pan=%d rev=%d cho=%d" % (st["vol"][mix], st["pan"][mix], st["rev"][mix], st["cho"][mix])
        if why:
            line += "   # " + why
            rep["voices"].add(why)
        out.append(line)
    out.append("")

    for s, sname in zip(secs, names):
        bars = min(8, st["bars"][s])
        if st["bars"][s] > 8:
            rep["cut"] += 1
        plen = bars * beats * QPPQ
        out += ["[%s]" % sname, "bars %d" % bars, "beats %d" % beats]
        sect = []                        # (track, abs tick in ppq, len, note, vel)
        for t in range(8):
            i = st["phrase"][s][t]
            if i is None:
                continue
            p = ph[i]
            prog, drum, _ = gm_program(*voice(t, i))
            drum = trk_drum[t] if t in trk_drum else drum
            typ = "bypass" if drum else QTYPE.get(p["type"], "chord1")
            hdr = "track %s type=%s" % (TRACKS[t], typ)
            if not drum:
                hdr += " program=%d lo=%d hi=%d" % (prog, p["lo"], p["hi"])
                if typ in ("chord1", "bass"):
                    hdr += " hikey=%s" % NOTE[p["hikey"] % 12]
            hdr += "   # %s" % p["name"]
            if not drum and typ != "bypass" and (p["root"], p["chord"]) != (0, 0):
                hdr += ", written over %s%s" % (NOTE[p["root"] % 12], QCHORD[p["chord"]][0] if p["chord"] < len(QCHORD) else "?")
            for k, v in p["other"].items():
                rep["events_lost"][k] = rep["events_lost"].get(k, 0) + v
            loop = p["bars"] * p["num"] * QPPQ
            vscale = st["vol"][t] / dvol if drum and t != dref else 1.0
            evs = []
            for rep_start in range(0, plen, loop):
                for (nt, gate, key, vel) in p["notes"]:
                    if nt >= loop:
                        continue
                    at = rep_start + nt
                    if at >= plen:
                        continue
                    if drum:
                        if key in XG_DRUM:
                            rep["drums_mapped"].add(key)
                            key, pct = XG_DRUM[key]
                            vel = vel * pct // 100
                        vel = round(vel * vscale)
                        gate = max(gate, 1)
                    elif typ != "bypass":
                        key = to_cm7(key, typ, p["root"], p["chord"])
                    if not 0 <= key <= 127:
                        continue
                    vel = max(1, min(127, vel))
                    gate = max(1, min(gate, plen - at))
                    evs.append([at, gate, key, vel])
            sect.append((t, hdr, evs))

        # to Polly's resolution
        total = sum(len(e) for _, _, e in sect)
        for _, _, evs in sect:
            for e in evs:
                if ppq != QPPQ:
                    q = round(e[0] * ppq / QPPQ)
                    rep["err"] += abs(q * QPPQ / ppq - e[0])
                    if q * QPPQ != e[0] * ppq:
                        rep["moved"] += 1
                    e[0] = min(q, bars * beats * ppq - 1)
                    e[1] = max(1, min(round(e[1] * ppq / QPPQ), bars * beats * ppq - e[0]))
            # the same note twice on one tick: the last (as make_styles does)
            seen = {}
            for e in evs:
                seen[(e[0], e[2])] = e
            evs[:] = sorted(seen.values())
        n = sum(len(e) for _, _, e in sect)
        rep["dropped"] += total - n
        if n > max_notes:                # over the limit: the quietest short notes go first
            allev = sorted(((e[3] * 4 + min(e[1], QPPQ), id(e)), e) for _, _, evs in sect for e in evs)
            gone = set(id(e) for _, e in allev[:n - max_notes])
            for _, _, evs in sect:
                evs[:] = [e for e in evs if id(e) not in gone]
            rep["dropped"] += n - max_notes
            n = max_notes
        rep["notes"] += n
        rep["notes_by_sect"].append(n)
        for t, hdr, evs in sect:
            out.append(hdr)
            drum = trk_drum.get(t, False)
            for at, gate, key, vel in evs:
                nm = DRUMS.get(key, str(key)) if drum else note_name(key)
                out.append("%s %s %d %d" % (pos(at, beats, ppq), nm, gate, vel))
        out.append("")
    return "\n".join(out), rep


def slug(name):
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return s or "style"


def size_bytes(rep, nsect):
    """about Polly's flash bytes for the style (5 a note, 4 + 8 x 14 a pattern, 10 the style);
    tools/make_styles.py --sizes has the exact count"""
    return rep["notes"] * 5 + nsect * (4 + 8 * 14) + 10


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--style", action="append", default=[])
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--phrase")
    ap.add_argument("--out")
    ap.add_argument("--first", type=int, default=17)
    ap.add_argument("--number", type=int)
    ap.add_argument("--legacy", action="store_true")
    ap.add_argument("--max-notes", type=int, default=600)
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args(argv)
    with open(a.rom, "rb") as f:
        data = f.read()
    try:
        rom = Rom(data)
    except RomError as e:
        raise SystemExit("qy70_import: %s" % e)
    styles = rom.styles()
    if a.list:
        for st in styles:
            print("%3d %-8s %5.1f bpm %d/4  bars %s" % (st["index"] + 1, st["name"], st["tempo"], st["num"],
                                                       " ".join(str(b) for b in st["bars"])))
        return
    if a.phrase:
        for i, nm in enumerate(rom.pname):
            if nm.rstrip().lower() == a.phrase.lower():
                p = rom.phrase(i)
                print("%d %s: %s, %d bars of %d, voice %d/%d/%d, %s over %s%s, lo %d hi %d hikey %s" % (
                    i, p["name"], rom.category(i), p["bars"], p["num"], p["msb"], p["lsb"], p["prog"],
                    QTYPE.get(p["type"], hex(p["type"])), NOTE[p["root"] % 12], QCHORD[p["chord"]][0],
                    p["lo"], p["hi"], NOTE[p["hikey"] % 12]))
                for (t, g, k, v) in p["notes"]:
                    print("  %s  %-4s gate %d vel %d" % (pos(t, p["num"], QPPQ), note_name(k), g, v))
                if p["other"]:
                    print("  other events: %s" % p["other"])
        return
    pick = styles if a.all else []
    for want in a.style:
        m = [st for st in styles if st["name"].lower() == want.lower().strip()]
        if not m:
            raise SystemExit("qy70_import: no style %r (--list)" % want)
        pick += m
    if not pick:
        ap.error("--list, --phrase, --style NAME or --all")
    if not a.out:
        ap.error("--out DIR")
    os.makedirs(a.out, exist_ok=True)
    ppq = 24 if a.legacy else QPPQ
    nsect = 4 if a.legacy else 6
    tot = 0
    for n, st in enumerate(pick):
        num = a.number if (a.number is not None and len(pick) == 1) else a.first + (st["index"] if a.all else n)
        text, rep = convert(rom, st, ppq, a.legacy, a.max_notes)
        path = os.path.join(a.out, "%02d-%s.sty" % (num, slug(st["name"])))
        with open(path, "w") as f:
            f.write(text)
        b = size_bytes(rep, nsect)
        tot += b
        if a.report or len(pick) == 1:
            lost = ", ".join("%d %s" % (v, k) for k, v in sorted(rep["events_lost"].items()))
            print("%s: %d notes (%s a section), ~%d bytes%s%s%s%s" % (
                path, rep["notes"], "/".join(str(x) for x in rep["notes_by_sect"]), b,
                "; %d moved by quantising (mean %.1f QY ticks)" % (rep["moved"], rep["err"] / max(1, rep["notes"]))
                if ppq != QPPQ else "",
                "; %d notes dropped (doubles, or past --max-notes)" % rep["dropped"] if rep["dropped"] else "",
                "; events not imported: " + lost if lost else "",
                "; " + "; ".join(sorted(rep["voices"])) if rep["voices"] else ""))
    if len(pick) > 1:
        print("%d styles, ~%d bytes" % (len(pick), tot))


if __name__ == "__main__":
    main()
