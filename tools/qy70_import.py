#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The QY70's preset styles as Polly .sty files, read from your own QY70 main ROM (bring your own ROM).

    python3 tools/qy70_import.py qy70main.bin --list                   # the 128 preset styles
    python3 tools/qy70_import.py qy70main.bin --style Bossa --out DIR   # one style -> DIR/NN-bossa.sty
    python3 tools/qy70_import.py qy70main.bin --all --out DIR           # all of them
    python3 tools/qy70_import.py qy70main.bin --phrase Bossa-1a         # one phrase's events, decoded
    python3 tools/qy70_import.py qy70main.bin --selftest                # the chord mapping against xpose.c

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
                  tempo=(h[12] << 8 | h[13]) / 10.0, notes=[], other={}, ctrl=[])
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
                kind = {0xFB: "control change", 0xFE: "pitch bend", 0xFD: "pressure"}.get(b, "other")
                other[kind] = other.get(kind, 0) + 1
                if b == 0xFB:                     # (time, controller, value)
                    ph["ctrl"].append((t, d[q + 1], d[q + 2]))
                elif b == 0xFE:                   # pitch bend, 14 bits: controller "bend"
                    ph["ctrl"].append((t, "bend", d[q + 2] << 7 | d[q + 1]))
                elif b == 0xFD:
                    ph["ctrl"].append((t, "pressure", d[q + 1]))
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


def cm7_base(typ, root, hikey):
    """the octave shift taking a phrase's source root to C, chosen so that Polly playing it back over the source
    chord gives the original notes: Chord 1 and Bass add the root, less an octave for roots above the high key
    (xpose.c); Chord 2 adds the root; Parallel (no high key in Polly): the nearer C"""
    if typ in ("chord1", "bass") and hikey < 12 and root > hikey:
        return 12 - root
    if typ == "parallel" and root > 6:
        return 12 - root
    return -root


def to_cm7(note, typ, root, chord, hikey=12):
    """a note of a phrase written over (root, chord) moved to the same degree over C M7, in the octave that
    plays back as written over the source chord"""
    base = cm7_base(typ, root, hikey)
    if typ == "parallel":
        return note + base
    semis = QCHORD[chord][1] if chord < len(QCHORD) else CM7
    # the source chord's degrees. A triad's "7th" is only the octave: not a degree (a leading tone over a triad
    # is chromatic). A 6 chord's 6th is its 7th degree, as Polly's tables have it (A over C6 is stored as B: the
    # 6th over a 6 chord, the 7th over a 7th chord, the root over a triad), but not the base of a chromatic note
    # (Bb over C6 is 13 + 1, not 7 + 1, which would pass the octave)
    degs = [dg for dg in DEG_ORDER if not (dg == 6 and semis[6] == 12)]
    rel = (note - root) % 12
    deg, off = None, 0
    if QCHORD[chord][0] == "m7(11)" and rel == 5:
        # the 11th of a quartal m7(11) voicing (R b3 11 b7): stored as the 5th. As an 11th, Polly would play it
        # as a 4th over every major and dominant chord (Chord 1 keeps the degree); as the 5th it fits every chord
        return note - rel + CM7[4] + base
    for dg in degs:
        if semis[dg] % 12 == rel:
            deg = dg
            break
    if deg is None:                                   # chromatic: just above the highest degree below it
        s, deg = max((semis[dg], dg) for dg in degs if semis[dg] <= rel and not (dg == 6 and semis[6] == semis[5]))
        off = rel - s
    # the degree's pitch over C M7, in the octave of (the note's octave over the root) + base
    return note - rel + CM7[deg] + off + base


def limits(p, typ, notes):
    """the track's lo / hi. The QY70's limits as written, except a Chord 2 phrase with no limits (0..127): Polly's
    Chord 2 moves each note to the octave nearest the note before, which with no range walks a voicing up or
    down the keyboard chord by chord (the QY70 keeps the voicing's register), so it gets a window around the
    notes as written (at least an octave and a half, the notes' own span plus a third each side)"""
    lo, hi = p["lo"], p["hi"]
    if typ != "chord2" or lo > 0 or hi < 127 or not notes:
        return lo, hi
    a, b = min(notes), max(notes)
    half = max((b - a + 1) // 2 + 4, 9)
    c = (a + b + 1) // 2
    return max(0, c - half), min(127, c + half)


def note_name(n):
    return "%s%d" % (NOTE[n % 12], n // 12 - 1)


def pos(t, beats, ppq):
    bar, r = divmod(t, beats * ppq)
    beat, tick = divmod(r, ppq)
    return "%d.%d.%d" % (bar + 1, beat + 1, tick)


def plan(rom, st):
    """a style's tracks and phrase uses, as convert() makes them: (tracks, uses), tracks[t] = {chan, drum, prog,
    vol, pan, rev, cho, vscale} for the tracks that play, uses[s][t] = {i, typ, prog, drum} or None"""
    ph = {}
    for s in range(6):
        for t in range(8):
            i = st["phrase"][s][t]
            if i is not None and i not in ph:
                ph[i] = rom.phrase(i)

    def voice(t, i):
        v = st["voice"][t]
        return v if v != (0, 0, 0) else (ph[i]["msb"], ph[i]["lsb"], ph[i]["prog"])
    first = {}
    for t in range(8):
        for s in [1, 2, 3, 4, 0, 5]:
            if st["phrase"][s][t] is not None:
                first[t] = st["phrase"][s][t]
                break
    drums = [t for t in first if gm_program(*voice(t, first[t]))[1]]
    dref = drums[0] if drums else 0
    dvol = max(1, st["vol"][dref])
    free, tracks = [7, 8, 9, 11, 12], {}
    for t in sorted(first):
        prog, drum, _ = gm_program(*voice(t, first[t]))
        chan = 10 if drum else (t - 1 if t >= 3 else free.pop(0))
        mix = dref if drum else t
        tracks[t] = dict(chan=chan, drum=drum, prog=prog, vol=st["vol"][mix], pan=st["pan"][mix],
                         rev=st["rev"][mix], cho=st["cho"][mix],
                         vscale=st["vol"][t] / dvol if drum and t != dref else 1.0)
    uses = [[None] * 8 for _ in range(6)]
    for s in range(6):
        for t in range(8):
            i = st["phrase"][s][t]
            if i is None:
                continue
            prog, _, _ = gm_program(*voice(t, i))
            drum = tracks[t]["drum"]
            uses[s][t] = dict(i=i, prog=prog, drum=drum,
                              typ="bypass" if drum else QTYPE.get(ph[i]["type"], "chord1"))
    return tracks, uses, ph


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

    def conv_keys(p, typ):
        if typ == "bypass":
            return [n[2] for n in p["notes"]]
        return [to_cm7(n[2], typ, p["root"], p["chord"], p["hikey"]) for n in p["notes"]]

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
            line += " program=%d lo=%d hi=%d" % ((prog,) + limits(p, typ, conv_keys(p, typ)))
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
                hdr += " program=%d lo=%d hi=%d" % ((prog,) + limits(p, typ, conv_keys(p, typ)))
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
                        key = to_cm7(key, typ, p["root"], p["chord"], p["hikey"])
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


HARNESS = r"""
#include <stdio.h>
#include "xpose.h"
int main(void)
{
    xtrack_t t = {0, 0, 127, 12};
    xstate_t s;
    chord_t c = {0, 0, CH_NOBASS};
    char line[96];
    int a, b, d, e, f, g;
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == 'P' && sscanf(line + 1, "%d %d %d %d %d %d", &a, &b, &d, &e, &f, &g) == 6) {
            t.type = (uint8_t)a, t.lo = (uint8_t)b, t.hi = (uint8_t)d, t.hikey = (uint8_t)e;
            c.root = (uint8_t)f, c.type = (uint8_t)g;
            xpose_reset(&s);
        } else if (sscanf(line + 1, "%d", &a) == 1)
            printf("%d\n", xpose(&t, &s, a, c));
    }
    return 0;
}
"""
# the QY70's chord codes as Polly's (chords.h CH_*); 6(9) and 7(b5) have no Polly type: 6 and 7
QY_TO_CH = [2, 0, 4, 20, 3, 1, 16, 21, 18, 23, 22, 19, 17, 6, 13, 9, 7, 8, 12, 11, 10, 14, 15, 24, 5, 4, 6]
XT = {"bypass": 0, "parallel": 1, "chord1": 2, "chord2": 3, "bass": 4}


def selftest(rom):
    """every phrase that transposes, stored over C M7 as the importer stores it, played back by Polly's own
    transposer (firmware/src/dsp/xpose.c, compiled here with cc) over the phrase's source chord: Chord 1 and
    Bass must give back the ROM's notes exactly, Chord 2 and Parallel their pitch classes (Polly's Chord 2
    picks octaves by voice leading; Parallel keeps its octave near C). Up to 1% may differ: the chromatic notes
    Polly's degree tables can't keep (a leading tone over a triad, whose 7th is the octave: Polly plays the
    root). 0 = pass."""
    import subprocess
    import tempfile
    dsp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "src", "dsp")
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = os.path.join(tmp, "h.c"), os.path.join(tmp, "h")
        with open(src, "w") as f:
            f.write(HARNESS)
        subprocess.run([os.environ.get("CC", "cc"), "-O1", "-I" + dsp, "-o", exe, src,
                        os.path.join(dsp, "xpose.c"), os.path.join(dsp, "chords.c")], check=True)
        lines, meta = [], []
        for i in range(rom.nph):
            p = rom.phrase(i)
            typ = QTYPE.get(p["type"])
            if typ in (None, "bypass") or p["chord"] >= len(QY_TO_CH):
                continue
            lines.append("P %d 0 127 %d %d %d" % (XT[typ], p["hikey"] if p["hikey"] < 12 else 12, p["root"],
                                                QY_TO_CH[p["chord"]]))
            for j, n in enumerate(sorted(p["notes"], key=lambda n: (n[0], n[2]))):
                lines.append("N %d" % to_cm7(n[2], typ, p["root"], p["chord"], p["hikey"]))
                skip = (j == 0 and typ == "bass") or \
                    (QCHORD[p["chord"]][0] == "m7(11)" and (n[2] - p["root"]) % 12 == 5)
                meta.append((None if skip else typ, n[2]))   # Bass: its first note is the root; m7(11): to_cm7
        out = subprocess.run([exe], input="\n".join(lines) + "\n", capture_output=True, text=True,
                             check=True).stdout.split()
    tot, pc, octv = {}, {}, {}
    for (typ, k), o in zip(meta, out):
        o = int(o)
        if typ is None:
            continue
        tot[typ] = tot.get(typ, 0) + 1
        if o % 12 != k % 12:
            pc[typ] = pc.get(typ, 0) + 1
        elif o != k:
            octv[typ] = octv.get(typ, 0) + 1
    fail = 0
    for typ in sorted(tot):
        bad = pc.get(typ, 0) + (octv.get(typ, 0) if typ in ("chord1", "bass") else 0)
        ok = bad <= tot[typ] // 100
        fail += not ok
        print("selftest %-8s %6d notes over their source chords: %d another pitch class, %d another octave%s" % (
            typ, tot[typ], pc.get(typ, 0), octv.get(typ, 0), "" if ok else "  FAIL"))
    print("selftest %s" % ("FAILED" if fail else "ok"))
    return 1 if fail else 0


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
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    with open(a.rom, "rb") as f:
        data = f.read()
    try:
        rom = Rom(data)
    except RomError as e:
        raise SystemExit("qy70_import: %s" % e)
    styles = rom.styles()
    if a.selftest:
        raise SystemExit(selftest(rom))
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
