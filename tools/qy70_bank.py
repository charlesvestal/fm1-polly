#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A compact phrase bank for imported QY70 styles (prototype: the format the firmware would play from flash).

    python3 tools/qy70_bank.py qy70main.bin --sizes          # bytes per style and in all, step by step
    python3 tools/qy70_bank.py qy70main.bin --test           # decode(encode(style)) against the importer
    python3 tools/qy70_bank.py qy70main.bin --out bank.bin   # the bank, for your own use (Yamaha's music)

Like the QY70 itself, a style is a list of phrase references (6 sections x 8 tracks) plus its tracks' sounds
and mixer; each phrase is stored once, over C M7 (tools/qy70_import.py's conversion), not expanded to the
section's length: the player repeats it to fill the section, as the QY70 does. docs/qy70-import.md has the
format. Python 3 standard library only; no Yamaha data in this file.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qy70_import as q  # noqa: E402

PPQ = q.QPPQ
GATE_UNIT = 10                   # melodic gates are stored in 10-tick units (+-5 ticks: 5 ms at 120 bpm)
DRUM_GATE = 60                   # drums have no stored gate: they play this long
# controllers kept, in a phrase's stream: kind -> (name, the MIDI controller, the smallest step kept)
KINDS = [("bend", None, 1), ("mod", 1, 2), ("expression", 11, 2), ("pan", 10, 1), ("pressure", None, 2),
         ("brightness", 74, 2), ("resonance", 71, 2)]
KIND_OF = {"bend": 0, 1: 1, 11: 2, 10: 3, "pressure": 4, 74: 5, 71: 6}
# Polly's synth (Sonivox EAS, eas_voicemgt.c VMControlChange) answers bank select, 1 (vibrato depth), 7, 10, 11,
# 64, 91, 93, the RPNs (bend range) and 120..127, plus pitch bend and channel pressure (vibrato depth too); the
# QY70 phrases use 1, 10, 11, bend and pressure of those; and CC 74 and 71 (the XG filter's cutoff and resonance,
# 24,500 events), which Polly's EAS now answers too (the per-voice filter): kept as kinds 5 and 6
END, LONG, CTRL = 0xFF, 0xFE, 0xF0


def varint(n):
    out = bytearray()
    while n >= 0x80:
        out.append(n & 0x7F | 0x80)
        n >>= 7
    out.append(n)
    return out


def rd_varint(b, p):
    n = sh = 0
    while True:
        c = b[p]
        p += 1
        n |= (c & 0x7F) << sh
        sh += 7
        if c < 0x80:
            return n, p


# ---------------------------------------------------------------- one phrase

def phrase_events(rom, i, typ, drum, ctrl=True):
    """a phrase as the player sees it: notes [(t, gate, key, vel)] over C M7 (drums mapped to GM), controllers
    [(t, kind, value 0..127)] thinned to what is heard, and its length in ticks"""
    p = rom.phrase(i)
    notes = []
    for (t, g, k, v) in p["notes"]:
        if drum:
            if k in q.XG_DRUM:
                k, pct = q.XG_DRUM[k]
                v = v * pct // 100
            g = DRUM_GATE
        elif typ != "bypass":
            k = q.to_cm7(k, typ, p["root"], p["chord"], p["hikey"])
        if 0 <= k <= 127:
            notes.append((t, max(1, g), k, max(1, min(127, v))))
    notes.sort(key=lambda n: (n[0], n[2]))
    seen = {}
    for n in notes:                       # the same key twice on a tick: the last
        seen[(n[0], n[2])] = n
    notes = sorted(seen.values(), key=lambda n: (n[0], n[2]))
    ctl = []
    if ctrl:
        last = {}
        for (t, c, v) in p["ctrl"]:
            kind = KIND_OF.get(c)
            if kind is None:
                continue
            if c == "bend":
                v >>= 7                   # 14 bits to 7: 1/64 of the bend range (3 cents at +-2 semitones)
            prev = last.get(kind)
            if prev is not None and abs(v - prev[1]) < KINDS[kind][2] and t - prev[0] < PPQ:
                continue                  # a step too small to hear, soon after the last one kept
            last[kind] = (t, v)
            ctl.append((t, kind, v))
    length = p["bars"] * p["num"] * PPQ
    return notes, ctl, length


def best_unit(times):
    """the time unit (ticks) giving the fewest escapes: each event is (steps 0..14 of the unit, -8..+7 ticks)"""
    best = None
    for g in (1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 16, 20, 24, 30, 40, 48, 60, 80, 96, 120, 160, 240):
        esc, gi = 0, 0
        for t in times:
            s = (t + 8) // g - gi
            r = t - (gi + s) * g
            if 0 <= s <= 14 and -8 <= r <= 7:
                gi += s
            else:
                esc += 1
                gi = (t + g // 2) // g
        if best is None or esc < best[0]:
            best = (esc, g)
    return best[1]


def enc_phrase(notes, ctl, length, typ, drum, lo, hi, hikey):
    """bytes: header, then events (time byte, then a note or a controller), END"""
    evs = [(n[0], 0, n) for n in notes] + [(c[0], 1, c) for c in ctl]
    evs.sort(key=lambda e: (e[0], e[1]))
    keys = sorted(set(n[2] for n in notes))
    wide = len(keys) > 15
    vels = [n[3] for n in notes] or [64]
    vmin, vmax = min(vels), max(vels)
    unit = best_unit([e[0] for e in evs])
    melodic = not drum
    flags = q.XT[typ] | drum << 3 | wide << 4 | melodic << 5
    out = bytearray([flags, length // PPQ, unit, vmin, vmax])
    if melodic and typ != "bypass":
        out += bytes([lo, hi, hikey])
    if not wide:
        out += bytes([len(keys)]) + bytes(keys)
    gi, tprev = 0, 0
    for t, isc, e in evs:
        s = (t + 8) // unit - gi
        r = t - (gi + s) * unit
        if 0 <= s <= 14 and -8 <= r <= 7:
            out.append(s << 4 | (r + 8))
            gi += s
        else:
            out.append(LONG)
            out += varint(t - tprev)
            gi = (t + unit // 2) // unit
        tprev = t
        if isc:
            _, kind, v = e
            out += bytes([(CTRL if not wide else 0x80) | kind, v])
            continue
        _, g, k, v = e
        vn = round((v - vmin) * 15 / (vmax - vmin)) if vmax > vmin else 0
        if wide:
            out += bytes([k, vn])
        else:
            out.append(keys.index(k) << 4 | vn)
        if melodic:
            gu = (g + GATE_UNIT // 2) // GATE_UNIT
            if gu < 255:
                out.append(gu)
            else:
                out.append(255)
                out += struct.pack("<H", min(gu, 0xFFFF))
    out.append(END)
    return bytes(out)


def dec_phrase(b, p=0):
    """the reference decoder: (notes, controllers, length, typ, drum, lo, hi, hikey), the end offset"""
    flags, beats, unit, vmin, vmax = b[p:p + 5]
    p += 5
    typ = [k for k, v in q.XT.items() if v == flags & 7][0]
    drum, wide, melodic = flags >> 3 & 1, flags >> 4 & 1, flags >> 5 & 1
    lo, hi, hikey = 0, 127, 12
    if melodic and typ != "bypass":
        lo, hi, hikey = b[p:p + 3]
        p += 3
    keys = []
    if not wide:
        keys = list(b[p + 1:p + 1 + b[p]])
        p += 1 + b[p]
    notes, ctl, gi, t = [], [], 0, 0
    while b[p] != END:
        c = b[p]
        p += 1
        if c == LONG:
            dt, p = rd_varint(b, p)
            t += dt
            gi = (t + unit // 2) // unit
        else:
            gi += c >> 4
            t = gi * unit + (c & 15) - 8
        k = b[p]
        if (not wide and k >= CTRL) or (wide and k >= 0x80):
            ctl.append((t, k & 15, b[p + 1]))
            p += 2
            continue
        if wide:
            key, vn = k, b[p + 1]
            p += 2
        else:
            key, vn = keys[k >> 4], k & 15
            p += 1
        vel = vmin + round(vn * (vmax - vmin) / 15)
        if melodic:
            gu = b[p]
            p += 1
            if gu == 255:
                gu = struct.unpack_from("<H", b, p)[0]
                p += 2
            gate = gu * GATE_UNIT
        else:
            gate = DRUM_GATE
        notes.append((t, gate, key, vel))
    return (notes, ctl, beats * PPQ, typ, drum, lo, hi, hikey), p + 1


# ---------------------------------------------------------------- format 2: bars that repeat an earlier bar

UNITS = (1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 16, 20, 24, 30, 40, 48, 60, 80, 96, 120, 160, 240)


def _quant(notes, vmin, vmax, melodic):
    """the stored form of each note: (t, gate units or 0, key, vel nibble)"""
    out = []
    for (t, g, k, v) in notes:
        vn = round((v - vmin) * 15 / (vmax - vmin)) if vmax > vmin else 0
        gu = (g + GATE_UNIT // 2) // GATE_UNIT if melodic else 0
        out.append((t, gu, k, vn))
    return out


def _enc_times(evs, unit, start):
    """times of events from `start` (a bar line): a byte each (steps 0..14 of the unit, -8..+7 ticks), or LONG +
    the ticks since the last event"""
    out, gi, tp = bytearray(), start // unit if start % unit == 0 else None, start
    res = []
    for t in evs:
        if gi is None:
            gi = (start + unit // 2) // unit
        s = (t + 8) // unit - gi
        r = t - (gi + s) * unit
        if 0 <= s <= 14 and -8 <= r <= 7:
            res.append(bytes([s << 4 | (r + 8)]))
            gi += s
        else:
            res.append(bytes([LONG]) + varint(t - tp))
            gi = (t + unit // 2) // unit
        tp = t
    return res


def _enc_new(notes, unit, start, keys, wide, melodic):
    """new notes of a bar: count, then each: time, key/velocity, gate"""
    out = bytearray(varint(len(notes)))
    for tb, (t, gu, k, vn) in zip(_enc_times([n[0] for n in notes], unit, start), notes):
        out += tb
        out += bytes([k, vn]) if wide else bytes([keys.index(k) << 4 | vn])
        if melodic:
            out += bytes([gu]) if gu < 255 else bytes([255]) + struct.pack("<H", min(gu, 0xFFFF))
    return out


def _match(cur, ref, shift, melodic, tol=(0, 0)):
    """which notes of the reference bar (shifted `shift` ticks) the bar repeats: same key and gate, within
    -7..+7 ticks and -8..+7 velocity steps; returns (kept flags per ref note, deltas, the bar's other notes)"""
    used, kept, deltas, rest = set(), [False] * len(ref), {}, []
    for n in cur:
        best = None
        for j, r in enumerate(ref):
            if j in used or r[2] != n[2] or (melodic and r[1] != n[1]):
                continue
            dt, dv = n[0] - (r[0] + shift), n[3] - r[3]
            if -7 <= dt <= 7 and -8 <= dv <= 7 and (best is None or abs(dt) < best[0]):
                best = (abs(dt), j, dt, dv)
        if best:
            used.add(best[1])
            kept[best[1]] = True
            dt, dv = best[2], best[3]
            if abs(dt) <= tol[0] and abs(dv) <= tol[1]:
                dt = dv = 0               # close enough to play as the reference (TOL)
            deltas[best[1]] = (dt, dv)
        else:
            rest.append(n)
    return kept, deltas, rest


TOL = (0, 0)                     # (ticks, velocity steps) a repeat may differ by and still be stored as exact


def enc_phrase2(notes, ctl, length, typ, drum, lo, hi, hikey, bar, tol=None):
    """format 2: a header, then bar by bar: a bar either lists its notes, or repeats notes of one of the four
    bars before it (a bit mask of the ones it keeps, a byte of timing / velocity change each unless all are
    exact) and lists the rest; then the controllers"""
    keys = sorted(set(n[2] for n in notes))
    wide = len(keys) > 15
    vels = [n[3] for n in notes] or [64]
    vmin, vmax = min(vels), max(vels)
    melodic = not drum
    unit = best_unit([n[0] for n in notes])
    limits = melodic and typ != "bypass" and (lo, hi, hikey) != (0, 127, 12)
    out = bytearray([q.XT[typ] | drum << 3 | wide << 4 | limits << 5, length // bar - 1, bar // PPQ,
                     UNITS.index(unit), vmin, vmax])
    if limits:
        out += bytes([lo, hi, hikey])
    if not wide:
        out += bytes([len(keys)]) + bytes(keys)
    qn = _quant(notes, vmin, vmax, melodic)
    nb = length // bar
    bars = [[n for n in qn if b * bar <= n[0] < (b + 1) * bar] for b in range(nb)]
    for b in range(nb):
        cur = bars[b]
        best = (len(_enc_new(cur, unit, b * bar, keys, wide, melodic)) + 1, 0, None)
        for d in range(1, min(4, b) + 1):
            ref = bars[b - d]
            if not ref:
                continue
            kept, deltas, rest = _match(cur, ref, d * bar, melodic, TOL if tol is None else tol)
            if not any(kept):
                continue
            exact = all(deltas[j] == (0, 0) for j in deltas)
            cost = 1 + (len(ref) + 7) // 8 + (0 if exact else sum(kept)) + \
                len(_enc_new(rest, unit, b * bar, keys, wide, melodic))
            if cost < best[0]:
                best = (cost, d, (kept, deltas, rest, exact))
        _, d, m = best
        if not d:
            out.append(0)
            out += _enc_new(cur, unit, b * bar, keys, wide, melodic)
            continue
        kept, deltas, rest, exact = m
        out.append(d | exact << 3)
        mask = 0
        for j, k in enumerate(kept):
            mask |= k << j
        out += mask.to_bytes((len(kept) + 7) // 8, "little")
        if not exact:
            for j, k in enumerate(kept):
                if k:
                    dt, dv = deltas[j]
                    out.append((dt + 8) << 4 | (dv + 8))
        out += _enc_new(rest, unit, b * bar, keys, wide, melodic)
    # controllers: runs of one kind, (ticks since the last controller event, value) each
    out += _enc_ctl(ctl)
    return bytes(out)


def _enc_ctl(ctl):
    out, i, tp = bytearray(), 0, 0
    runs = []
    while i < len(ctl):
        j = i
        while j < len(ctl) and ctl[j][1] == ctl[i][1] and j - i < 255:
            j += 1
        runs.append(ctl[i:j])
        i = j
    out += varint(len(runs))
    for r in runs:
        out += bytes([r[0][1], len(r)])
        for (t, kind, v) in r:
            out += varint(t - tp) + bytes([v])
            tp = t
    return out


def dec_phrase2(b, p, bar_of=None):
    """the reference decoder of format 2: (notes [(t, gate, key, vel)], controllers, length, typ, drum, lo, hi,
    hikey), the end offset"""
    flags, nbars1, beats, ui, vmin, vmax = b[p:p + 6]
    p += 6
    typ = [k for k, v in q.XT.items() if v == flags & 7][0]
    drum, wide, limits = flags >> 3 & 1, flags >> 4 & 1, flags >> 5 & 1
    melodic = not drum
    unit, bar, nb = UNITS[ui], beats * PPQ, nbars1 + 1
    lo, hi, hikey = 0, 127, 12
    if limits:
        lo, hi, hikey = b[p:p + 3]
        p += 3
    keys = []
    if not wide:
        keys = list(b[p + 1:p + 1 + b[p]])
        p += 1 + b[p]
    bars = []

    def new_notes(p, start):
        n, p = rd_varint(b, p)
        out, gi, t = [], None, start
        for _ in range(n):
            c = b[p]
            p += 1
            if gi is None:
                gi = (start + unit // 2) // unit
            if c == LONG:
                dt, p = rd_varint(b, p)
                t += dt
                gi = (t + unit // 2) // unit
            else:
                gi += c >> 4
                t = gi * unit + (c & 15) - 8
            if wide:
                k, vn = b[p], b[p + 1]
                p += 2
            else:
                k, vn = keys[b[p] >> 4], b[p] & 15
                p += 1
            gu = 0
            if melodic:
                gu = b[p]
                p += 1
                if gu == 255:
                    gu = struct.unpack_from("<H", b, p)[0]
                    p += 2
            out.append((t, gu, k, vn))
        return out, p
    for bi in range(nb):
        h = b[p]
        p += 1
        d, exact = h & 7, h >> 3 & 1
        cur = []
        if d:
            ref = bars[bi - d]
            nm = (len(ref) + 7) // 8
            mask = int.from_bytes(b[p:p + nm], "little")
            p += nm
            for j, r in enumerate(ref):
                if mask >> j & 1:
                    dt = dv = 0
                    if not exact:
                        dt, dv = (b[p] >> 4) - 8, (b[p] & 15) - 8
                        p += 1
                    cur.append((r[0] + d * bar + dt, r[1], r[2], r[3] + dv))
        more, p = new_notes(p, bi * bar)
        cur = sorted(cur + more, key=lambda n: (n[0], n[2]))
        bars.append(cur)
    notes = []
    for cur in bars:
        for (t, gu, k, vn) in cur:
            notes.append((t, gu * GATE_UNIT if melodic else DRUM_GATE, k, vmin + round(vn * (vmax - vmin) / 15)))
    nr, p = rd_varint(b, p)
    ctl, tp = [], 0
    for _ in range(nr):
        kind, cnt = b[p], b[p + 1]
        p += 2
        for _ in range(cnt):
            dt, p = rd_varint(b, p)
            tp += dt
            ctl.append((tp, kind, b[p]))
            p += 1
    return (notes, ctl, nb * bar, typ, drum, lo, hi, hikey), p


# ---------------------------------------------------------------- styles and the bank

def enc_style(st, tracks, uses, offs, use_key):
    """name, tempo, beats, bars, the tracks' sounds and mixer, then the phrase uses (3-byte bank offsets; a use
    whose program differs from its track's carries it)"""
    out = bytearray(st["name"].ljust(8).encode("ascii")[:8])
    out += struct.pack("<H", round(st["tempo"] * 10)) + bytes([st["num"]])
    bars = [min(8, b) - 1 for b in st["bars"]]
    out += bytes([bars[0] | bars[1] << 4, bars[2] | bars[3] << 4, bars[4] | bars[5] << 4])
    tmask = sum(1 << t for t in tracks)
    out.append(tmask)
    for t in sorted(tracks):
        tr = tracks[t]
        out += bytes([tr["chan"] - 1 | tr["drum"] << 7, tr["prog"], tr["vol"], tr["pan"], tr["rev"], tr["cho"],
                      min(255, round(tr["vscale"] * 128))])
    for s in range(6):
        m = sum(1 << t for t in range(8) if uses[s][t])
        out.append(m)
        for t in range(8):
            u = uses[s][t]
            if not u:
                continue
            o = offs[use_key(u)]
            other = u["prog"] != tracks[t]["prog"] and not u["drum"]
            out += struct.pack("<I", o | other << 23)[:3]
            if other:
                out.append(u["prog"])
    return bytes(out)


def build(rom, ctrl=True, styles=None, fmt=2):
    """the bank: (bytes, {style name: its record's size}, {phrase key: encoded size}, stats)"""
    styles = styles or rom.styles()
    plans = [(st,) + q.plan(rom, st) for st in styles]
    blob, offs, sizes = bytearray(), {}, {}

    def use_key(u):
        return (u["i"], u["typ"], u["drum"])
    phrases, barlen = {}, {}
    for st, tracks, uses, ph in plans:
        for s in range(6):
            for t in range(8):
                u = uses[s][t]
                if u and use_key(u) not in phrases:
                    p = ph[u["i"]]
                    notes, ctl, length = phrase_events(rom, u["i"], u["typ"], u["drum"], ctrl)
                    lo, hi = q.limits(p, u["typ"], [n[2] for n in notes])
                    hk = p["hikey"] if u["typ"] in ("chord1", "bass") and p["hikey"] < 12 else 12
                    if u["drum"] or u["typ"] == "bypass":
                        lo, hi, hk = 0, 127, 12
                    phrases[use_key(u)] = (notes, ctl, length, u["typ"], u["drum"], lo, hi, hk)
                    barlen[use_key(u)] = p["num"] * PPQ
    pblob = bytearray()
    for k, v in phrases.items():
        offs[k] = len(pblob)
        e = enc_phrase(*v) if fmt == 1 else enc_phrase2(*v, barlen[k])
        sizes[k] = len(e)
        pblob += e
    srecs = [enc_style(st, tracks, uses, offs, use_key) for st, tracks, uses, ph in plans]
    head = b"QYB1" + struct.pack("<HI", len(srecs), 0)
    sdir = bytearray()
    for r in srecs:
        sdir += struct.pack("<H", len(r))
    blob = head + sdir + b"".join(srecs) + pblob
    stats = dict(phrases=len(phrases), uses=sum(1 for _, _, u, _ in plans for s in u for x in s if x),
                 notes=sum(len(v[0]) for v in phrases.values()), ctrl=sum(len(v[1]) for v in phrases.values()),
                 styles_bytes=len(head) + len(sdir) + sum(map(len, srecs)), phrase_bytes=len(pblob))
    per_style = {}
    for (st, tracks, uses, ph), r in zip(plans, srecs):
        ks = set(use_key(u) for s in uses for u in s if u)
        per_style[st["name"]] = len(r) + 2 + sum(sizes[k] for k in ks)
    return bytes(blob), per_style, phrases, stats


def roundtrip(rom, fmt=2):
    """decode every phrase of the bank and compare with the importer's events: times, keys exact; velocities
    within the 16 steps of the phrase's range; gates within 5 ticks (drums: not stored)"""
    blob, _, phrases, _ = build(rom, fmt=fmt)
    n = struct.unpack_from("<H", blob, 4)[0]
    lens = struct.unpack_from("<%dH" % n, blob, 10)
    pb = blob[10 + 2 * n + sum(lens):]
    p, worst, bad = 0, dict(vel=0, gate=0), 0
    for k, v in phrases.items():
        (dn, dc, dlen, dtyp, ddrum, dlo, dhi, dhk), p = (dec_phrase if fmt == 1 else dec_phrase2)(pb, p)
        notes, ctl, length, typ, drum, lo, hi, hk = v
        ok = (len(dn) == len(notes) and dc == ctl and dlen == length and (dtyp, ddrum, dlo, dhi, dhk) ==
              (typ, drum, lo, hi, hk))
        for a, b in zip(dn, notes):
            ok &= a[0] == b[0] and a[2] == b[2]
            worst["vel"] = max(worst["vel"], abs(a[3] - b[3]))
            if not drum:
                worst["gate"] = max(worst["gate"], abs(a[1] - b[1]) if b[1] < 255 * GATE_UNIT else 0)
        bad += not ok
    return len(phrases), bad, worst


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--sizes", action="store_true")
    ap.add_argument("--test", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--budget", type=float, default=192.0, help="KB of flash for the bank (--sizes: how many fit)")
    a = ap.parse_args(argv)
    with open(a.rom, "rb") as f:
        rom = q.Rom(f.read())
    if a.test:
        n, bad, worst = roundtrip(rom)
        print("bank round trip: %d phrases, %d differ; largest velocity error %d, gate error %d ticks" % (
            n, bad, worst["vel"], worst["gate"]))
        raise SystemExit(1 if bad or worst["gate"] > GATE_UNIT // 2 else 0)
    if a.sizes:
        res = {}
        for fmt in (1, 2):
            for ctrl in (False, True):
                b, per, _, st = build(rom, ctrl=ctrl, fmt=fmt)
                res[fmt, ctrl] = (len(b), per, st)
        print("%-8s %9s %9s" % ("style", "notes", "+ctrl"))
        for name in res[2, False][1]:
            print("%-8s %9d %9d" % (name, res[2, False][1][name], res[2, True][1][name]))
        for fmt, what in ((1, "events in order"), (2, "bars repeating earlier bars")):
            for ctrl in (False, True):
                n, per, st = res[fmt, ctrl]
                fit, acc = 0, 0
                for x in sorted(per.values()):
                    if acc + x > a.budget * 1024 * 0.99:     # 1%: the sector guards (docs)
                        break
                    acc, fit = acc + x, fit + 1
                print("format %d (%s), %s: %d bytes for %d styles (%d phrases, %d notes%s); %d fit in %g KB" % (
                    fmt, what, "with controllers" if ctrl else "notes only", n, len(per), st["phrases"], st["notes"],
                    ", %d controller events" % st["ctrl"] if ctrl else "", fit, a.budget))
        return
    if a.out:
        b, _, _, s = build(rom)
        with open(a.out, "wb") as f:
            f.write(b)
        print("%s: %d bytes" % (a.out, len(b)))


if __name__ == "__main__":
    main()
