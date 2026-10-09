#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The QY70 style bank, compressed: one range-coded stream a style, decoded into RAM when a style is picked.

    python3 tools/qy70_codec.py qy70main.bin --out bank.qyb      # the bank (for your own use: Yamaha's music)
    python3 tools/qy70_codec.py qy70main.bin --sizes             # bytes per style, all, and what fits where
    python3 tools/qy70_codec.py qy70main.bin --test              # Python decode == the importer's events
    python3 tools/qy70_codec.py qy70main.bin --ctest             # the device's C decoder (firmware/src/dsp/qydec.c)
                                                                 # == this one
    python3 tools/qy70_codec.py qy70main.bin --image FILE        # the bank as the FM-1 stores it (qybank.h)
    python3 tools/qy70_codec.py --synthetic --image FILE         # a test bank from Polly's own styles (no ROM)

The model (docs/qy70-import.md): each style's phrases are coded in order with adaptive binary contexts (LZMA's
range coder: 12-bit probabilities, shifts and multiplies only, no division) started from priors trained on the
bank and stored once in it. A bar is coded either as its notes (time as steps of the phrase's grid plus the
humanised offset, the key as an index into the phrase's keys, velocity in 16 steps of the phrase's range, gate
in 10-tick units) or as a repeat of any earlier bar of the style with the same length and kind (a kept / dropped
bit per note, small timing and velocity changes) plus its other notes. Controllers (bend, modulation,
expression, pan, pressure) as runs of time and value changes. Python 3 standard library only; no Yamaha data
in this file.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qy70_import as q  # noqa: E402
import qy70_bank as qb  # noqa: E402

PPQ = q.QPPQ
GATE_UNIT = qb.GATE_UNIT
DRUM_GATE = qb.DRUM_GATE
UNITS = qb.UNITS
SHIFT = 4                        # probability adaptation rate (LZMA uses 5; 4 suits short phrases)
NKINDS = len(qb.KINDS)
MAGIC = b"QYB3"                  # 3: CC 74 and 71 kept (kinds 5, 6)

# ---------------------------------------------------------------- the model's probability layout (the C decoder
# mirrors it: firmware/src/dsp/qydec.h)
UK = 18                          # unary prefix of an unsigned code (up to 2^17 - 2)
UM = 2                           # mantissa bits with their own probabilities (the rest: fixed 1/2)
USZ = UK + 1 + 16 * UM           # probabilities an unsigned-code context uses


class Layout:
    def __init__(self):
        self.n = 0
        self.prior_end = None

    def take(self, n):
        b = self.n
        self.n += n
        return b


L = Layout()
P_TYP = L.take(8)                # tree 3
P_DRUM = L.take(1)
P_LIM = L.take(1)
P_NBARS = L.take(8)              # tree 3
P_BEATS = L.take(8)              # tree 3
P_UNIT = L.take(32)              # tree 5
P_VMIN = L.take(128)             # tree 7
P_VRNG = L.take(128)             # tree 7
P_LO = L.take(128)
P_HI = L.take(128)
P_HIKEY = L.take(16)             # tree 4
P_NKEYS = L.take(2 * USZ)        # uint, by drum
P_KEY0 = L.take(2 * 128)         # tree 7, by drum
P_KEYD = L.take(2 * USZ)         # uint, by drum
P_HASREF = L.take(4)             # bit, by drum x (bar 0 of its phrase)
P_REFD = L.take(2 * USZ)         # uint, by drum
P_EXACT = L.take(2)
P_MASK = L.take(4)               # bit, by drum x the previous mask bit
P_RDT = L.take(2 * 16)           # tree 4, by drum
P_RDV = L.take(2 * 16)
P_NNEW = L.take(4 * USZ)         # uint, by drum x has ref
P_SAME = L.take(4)               # bit, by drum x previous same
P_STEP = L.take(2 * 4 * USZ)       # uint, by drum x the previous step (0, 1, 2, 3+)
P_RES = L.take(2 * USZ)
P_VEL = L.take(2 * 8 * 3 * 16)   # tree 4, by drum x key index (7 = 7 and up) x place in the beat
P_GSAME = L.take(1)
P_GATE = L.take(USZ)
P_NRUNS = L.take(USZ)
P_KIND = L.take(8)               # tree 3
P_CNT = L.take(USZ)
P_CDT = L.take(NKINDS * USZ)
P_CDTS = L.take(NKINDS)           # bit: the same time step as the last, by kind
P_CV0 = L.take(NKINDS * 128)
P_CDV = L.take(NKINDS * 3 * USZ)   # sint, by kind x the last change's sign
NPRIOR = L.n                     # these start from the bank's priors ...
P_KEY = L.take(2 * 4 * 128)      # ... the key trees from 1/2: tree 7, by drum x previous key index (3 = 3+)
NPROB = L.n


# ---------------------------------------------------------------- the range coder (LZMA's)

class Enc:
    def __init__(self, probs):
        self.p = list(probs)
        self.low, self.rng, self.cache, self.csize, self.out = 0, 0xFFFFFFFF, 0, 1, bytearray()

    def _shift(self):
        if self.low < 0xFF000000 or self.low > 0xFFFFFFFF:
            carry, temp = self.low >> 32, self.cache
            while True:
                self.out.append((temp + carry) & 0xFF)
                temp = 0xFF
                self.csize -= 1
                if not self.csize:
                    break
            self.cache = (self.low >> 24) & 0xFF
        self.csize += 1
        self.low = (self.low & 0x00FFFFFF) << 8

    def bit(self, i, b):
        p = self.p[i] if i >= 0 else 2048
        bound = (self.rng >> 12) * p
        if not b:
            self.rng = bound
            if i >= 0:
                self.p[i] = p + ((4096 - p) >> SHIFT)
        else:
            self.low += bound
            self.rng -= bound
            if i >= 0:
                self.p[i] = p - (p >> SHIFT)
        while self.rng < 1 << 24:
            self.rng = (self.rng << 8) & 0xFFFFFFFF
            self._shift()
        return b

    def finish(self):
        for _ in range(5):
            self._shift()
        return bytes(self.out[1:])           # the first byte is always 0: not stored


class Dec:
    def __init__(self, probs, data, pos=0):
        self.p = list(probs)
        self.d, self.i = data, pos
        self.rng, self.code = 0xFFFFFFFF, 0
        for _ in range(4):
            self.code = (self.code << 8) | self._byte()

    def _byte(self):
        b = self.d[self.i] if self.i < len(self.d) else 0
        self.i += 1
        return b

    def bit(self, i, _=None):
        p = self.p[i] if i >= 0 else 2048
        bound = (self.rng >> 12) * p
        if self.code < bound:
            self.rng = bound
            if i >= 0:
                self.p[i] = p + ((4096 - p) >> SHIFT)
            b = 0
        else:
            self.code -= bound
            self.rng -= bound
            if i >= 0:
                self.p[i] = p - (p >> SHIFT)
            b = 1
        while self.rng < 1 << 24:
            self.rng = (self.rng << 8) & 0xFFFFFFFF
            self.code = ((self.code << 8) | self._byte()) & 0xFFFFFFFF
        return b


class Count:
    """the priors' training pass: counts each context's bits"""
    def __init__(self):
        self.n0, self.n1 = [0] * NPROB, [0] * NPROB

    def bit(self, i, b):
        if i >= 0:
            if b:
                self.n1[i] += 1
            else:
                self.n0[i] += 1
        return b

    def priors(self):
        """each context's starting probability, as one of 16 levels (4 bits, two a byte)"""
        import math
        lv = []
        for i in range(NPRIOR):
            n0, n1 = self.n0[i], self.n1[i]
            if not n0 + n1:
                lv.append(8)
                continue
            x = math.log((n0 + 0.5) / (n1 + 0.5))
            lv.append(min(range(16), key=lambda j: abs(math.log(PLEVEL[j] / (4096 - PLEVEL[j])) - x)))
        if len(lv) & 1:
            lv.append(8)
        return bytes(lv[i] | lv[i + 1] << 4 for i in range(0, len(lv), 2))


# the 16 starting probabilities (of a 0, in 4096ths) a prior can name
PLEVEL = (24, 64, 140, 280, 520, 900, 1400, 1850, 2048, 2696, 3196, 3576, 3816, 3956, 4032, 4072)


def probs_from(priors):
    out = []
    for i in range(NPRIOR):
        out.append(PLEVEL[(priors[i >> 1] >> (4 * (i & 1))) & 15])
    return out + [2048] * (NPROB - NPRIOR)


# ---- the codes (the same function codes and decodes: c is Enc, Dec or Count; v is ignored when decoding)

def tree(c, base, nbits, v=0):
    m = 1
    for k in range(nbits - 1, -1, -1):
        b = c.bit(base + m, (v >> k) & 1)
        m = m << 1 | b
    return m - (1 << nbits)


def uint(c, base, v=0):
    """v >= 0: the length of v + 1 in unary (own probabilities), then its bits below the top (the first UM own,
    the rest at 1/2)"""
    v1 = v + 1
    k = v1.bit_length() - 1 if isinstance(c, (Enc, Count)) else 0
    n = 0
    while n < UK:
        if not c.bit(base + n, 1 if n < k else 0):
            break
        n += 1
    r = 1
    for j in range(n - 1, -1, -1):
        idx = base + UK + 1 + min(n, 15) * UM + (n - 1 - j) if n - 1 - j < UM else -1
        r = r << 1 | c.bit(idx, (v1 >> j) & 1)
    return r - 1


def sint(c, base, v=0):
    z = uint(c, base, (v << 1) ^ (v >> 31) if v >= 0 else ((-v) << 1) - 1)
    return z >> 1 if not z & 1 else -((z + 1) >> 1)


# ---------------------------------------------------------------- a phrase

class Bar:
    __slots__ = ("drum", "blen", "notes")

    def __init__(self, drum, blen, notes):
        self.drum, self.blen, self.notes = drum, blen, notes       # notes: (rel t, gate units, key, vel)


def vq(v, vmin, vr):                     # velocity to a 0..15 step of the phrase's range, and back
    return round((v - vmin) * 15 / vr) if vr else 0


def vdq(n, vmin, vr):
    return vmin + (n * vr * 2 + 15) // 30 if vr else vmin


def match(cur, ref, melodic):
    """the reference bar's notes this bar repeats: same key (and gate), within -7..+7 ticks, -16..+14
    velocity; returns kept flags, deltas (dt, dv in 2s), the other notes"""
    byk = {}
    for j, r in enumerate(ref):
        byk.setdefault(r[2], []).append(j)
    used, kept, deltas, rest = set(), [False] * len(ref), {}, []
    for n in cur:
        best = None
        for j in byk.get(n[2], ()):
            r = ref[j]
            if j in used or (melodic and r[1] != n[1]):
                continue
            dt = n[0] - r[0]
            dv = n[3] - r[3]
            dvq = (dv + 1) // 2 if dv >= 0 else -((1 - dv) // 2)       # nearest step of 2
            if -7 <= dt <= 7 and -8 <= dvq <= 7 and (best is None or abs(dt) < best[0]):
                best = (abs(dt), j, dt, dvq)
        if best:
            used.add(best[1])
            kept[best[1]] = True
            deltas[best[1]] = (best[2], best[3])
        else:
            rest.append(n)
    return kept, deltas, rest


def code_phrase(c, ph, bars, enc_plan=None):
    """code (or decode, ph = None) one phrase; bars: the style's decoded bars so far (appended to). Returns the
    phrase: dict(typ, drum, lo, hi, hikey, length, notes [(t, gate, key, vel)], ctl [(t, kind, value)])"""
    E = ph is not None
    g = ph if E else {}
    typ = ["bypass", "parallel", "chord1", "chord2", "bass"][tree(c, P_TYP, 3, q.XT[g["typ"]] if E else 0)]
    drum = c.bit(P_DRUM, g.get("drum", 0))
    melodic = not drum
    lim = c.bit(P_LIM, int((g.get("lo"), g.get("hi"), g.get("hikey")) != (0, 127, 12)) if E else 0)
    nbars = tree(c, P_NBARS, 3, g["length"] // g["blen"] - 1 if E else 0) + 1
    beats = tree(c, P_BEATS, 3, g["blen"] // PPQ - 1 if E else 0) + 1
    blen = beats * PPQ
    unit = UNITS[tree(c, P_UNIT, 5, UNITS.index(g["unit"]) if E else 0)]
    vmin = tree(c, P_VMIN, 7, g.get("vmin", 0))
    vr = tree(c, P_VRNG, 7, g.get("vr", 0))
    lo, hi, hikey = 0, 127, 12
    if lim:
        lo = tree(c, P_LO, 7, g.get("lo", 0))
        hi = tree(c, P_HI, 7, g.get("hi", 0))
        hikey = tree(c, P_HIKEY, 4, g.get("hikey", 0))
    keys = g.get("keys", [])
    nk = uint(c, P_NKEYS + drum * USZ, len(keys) - 1 if E else 0) + 1
    if not E:
        keys = []
    k0 = tree(c, P_KEY0 + drum * 128, 7, keys[0] if E else 0)
    if not E:
        keys.append(k0)
    for i in range(1, nk):
        d = uint(c, P_KEYD + drum * USZ, keys[i] - keys[i - 1] - 1 if E else 0) + 1
        if not E:
            keys.append(keys[-1] + d)
    kb = max(1, (nk - 1).bit_length())
    out = []
    for b in range(nbars):
        start = b * blen
        cur = g["bars"][b] if E else None
        elig = [x for x in reversed(bars) if x.drum == drum and x.blen == blen and x.notes]
        plan = enc_plan(cur, elig, melodic) if E else None      # (distance, kept, deltas, exact, rest) or None
        has = c.bit(P_HASREF + drum * 2 + (b == 0), int(plan is not None) if E else 0) if elig else 0
        notes = []
        if has:
            d = uint(c, P_REFD + drum * USZ, plan[0] if E else 0)
            ref = elig[d].notes
            exact = c.bit(P_EXACT + drum, plan[3] if E else 0)
            prev = 1
            for j, r in enumerate(ref):
                k = c.bit(P_MASK + drum * 2 + prev, int(plan[1][j]) if E else 0)
                prev = k
                if not k:
                    continue
                dt = dv = 0
                if not exact:
                    dt = tree(c, P_RDT + drum * 16, 4, plan[2][j][0] + 8 if E else 0) - 8
                    dv = tree(c, P_RDV + drum * 16, 4, plan[2][j][1] + 8 if E else 0) - 8
                notes.append((r[0] + dt, r[1], r[2], max(1, min(127, r[3] + 2 * dv))))
        new = plan[4] if (E and has) else cur
        nn = uint(c, P_NNEW + drum * 2 * USZ + has * USZ, len(new) if E else 0)
        gi, tp, same, gprev, kprev, sprev = start // unit, None, 0, -1, 0, 0
        for i in range(nn):
            n = new[i] if E else None
            t = start + n[0] if E else 0
            s = c.bit(P_SAME + drum * 2 + same, int(t == tp) if E and tp is not None else 0) if tp is not None else 0
            same = s
            if not s:
                grid = (t + unit // 2) // unit if E else 0
                st = uint(c, P_STEP + (drum * 4 + min(sprev, 3)) * USZ, grid - gi if E else 0)
                sprev = st
                gi += st
                r = sint(c, P_RES + drum * USZ, t - gi * unit if E else 0)
                t = gi * unit + r
            else:
                t = tp
            tp = t
            ki = tree(c, P_KEY + (drum * 4 + min(kprev, 3)) * 128, kb, keys.index(n[2]) if E else 0)
            kprev = ki
            ph_ = (t - start) % PPQ
            place = 0 if ph_ <= 12 or ph_ >= PPQ - 12 else 1 if abs(ph_ - PPQ // 2) <= 12 else 2
            vn = tree(c, P_VEL + ((drum * 8 + min(ki, 7)) * 3 + place) * 16, 4, vq(n[3], vmin, vr) if E else 0)
            gu = 0
            if melodic:
                gs = c.bit(P_GSAME, int(n[1] == gprev) if E else 0) if gprev >= 0 else 0
                gu = gprev if gs else uint(c, P_GATE, n[1] if E else 0)
                gprev = gu
            notes.append((t - start, gu, keys[ki] if ki < len(keys) else 0, vdq(vn, vmin, vr)))
        notes.sort(key=lambda x: (x[0], x[2]))
        bars.append(Bar(drum, blen, notes))
        out += [(start + x[0], x[1], x[2], x[3]) for x in notes]
    # controllers: runs of a kind, each event its ticks since the last and its value (the first, then changes)
    runs = g.get("runs", [])
    nr = uint(c, P_NRUNS, len(runs) if E else 0)
    ctl, tp = [], 0
    for ri in range(nr):
        run = runs[ri] if E else None
        kind = tree(c, P_KIND, 3, run[0][1] if E else 0)
        cnt = uint(c, P_CNT, len(run) - 1 if E else 0) + 1
        vp, dtp, dvs = None, -1, 1
        for j in range(cnt):
            ev = run[j] if E else None
            if dtp >= 0 and c.bit(P_CDTS + kind, int(ev[0] - tp == dtp) if E else 0):
                dt = dtp
            else:
                dt = uint(c, P_CDT + kind * USZ, ev[0] - tp if E else 0)
            dtp = dt
            tp += dt
            if vp is None:
                v = tree(c, P_CV0 + kind * 128, 7, ev[2] if E else 0)
            else:
                dv = sint(c, P_CDV + (kind * 3 + dvs) * USZ, ev[2] - vp if E else 0)
                dvs = 0 if dv < 0 else 1 if dv == 0 else 2
                v = vp + dv
            vp = v
            ctl.append((tp, kind, v))
    gates = [(t, gu * GATE_UNIT if melodic else DRUM_GATE, k, v) for (t, gu, k, v) in out]
    return dict(typ=typ, drum=drum, lo=lo, hi=hi, hikey=hikey, length=nbars * blen, notes=gates, ctl=ctl)


def plan_bar(cost_new):
    """the encoder's choice for a bar: the earlier bar (if any) whose repeat costs the fewest bits, by estimate"""
    def choose(cur, elig, melodic):
        base = sum(cost_new(melodic) for _ in cur) + 1
        best = None
        for d, ref in enumerate(elig[:96]):
            kept, deltas, rest = match(cur, ref.notes, melodic)
            nk = sum(kept)
            if not nk:
                continue
            exact = all(deltas[j] == (0, 0) for j in deltas)
            bits = 2 + 2 * (d + 1).bit_length() + len(ref.notes) * 0.6 + (0 if exact else nk * 5) + \
                sum(cost_new(melodic) for _ in rest)
            if bits < base and (best is None or bits < best[0]):
                best = (bits, d, kept, deltas, exact, rest)
        return None if best is None else best[1:]
    return choose


def prep(notes, ctl, length, typ, drum, lo, hi, hikey, blen):
    """an importer phrase (qy70_bank.phrase_events) as the coder wants it"""
    melodic = not drum
    vels = [n[3] for n in notes] or [64]
    vmin, vmax = min(vels), max(vels)
    keys = sorted(set(n[2] for n in notes)) or [0]
    bars = []
    for b in range(length // blen):
        bars.append([(t - b * blen, (gt + GATE_UNIT // 2) // GATE_UNIT if melodic else 0, k, v)
                     for (t, gt, k, v) in notes if b * blen <= t < (b + 1) * blen])
    runs, i = [], 0
    while i < len(ctl):
        j = i
        while j < len(ctl) and ctl[j][1] == ctl[i][1]:
            j += 1
        runs.append(ctl[i:j])
        i = j
    return dict(typ=typ, drum=int(drum), lo=lo, hi=hi, hikey=hikey, length=length, blen=blen,
                unit=qb.best_unit([n[0] for n in notes]), vmin=vmin, vr=vmax - vmin, keys=keys, bars=bars,
                runs=runs)


# ---------------------------------------------------------------- the bank

def style_phrases(rom, st):
    """a style's plan (qy70_import.plan) and its phrases in first-use order: [(use key, prepped phrase)]"""
    tracks, uses, ph = q.plan(rom, st)
    order, seen = [], {}
    for s in [1, 2, 3, 4, 0, 5]:
        for t in range(8):
            u = uses[s][t]
            if not u:
                continue
            k = (u["i"], u["typ"], u["drum"])
            if k in seen:
                continue
            p = ph[u["i"]]
            notes, ctl, length = qb.phrase_events(rom, u["i"], u["typ"], u["drum"])
            lo, hi = q.limits(p, u["typ"], [n[2] for n in notes])
            hk = p["hikey"] if u["typ"] in ("chord1", "bass") and p["hikey"] < 12 else 12
            if u["drum"] or u["typ"] == "bypass":
                lo, hi, hk = 0, 127, 12
            seen[k] = len(order)
            order.append((k, prep(notes, ctl, length, u["typ"], u["drum"], lo, hi, hk, p["num"] * PPQ)))
    return tracks, uses, order, seen


def head(st, tracks, uses, seen):
    """the style's plain header: name, tempo, beats, bars, tracks, mixer; then per section the phrase each track
    plays (an index into the style's phrases, 0xFF none) and a program byte when it differs from the track's"""
    out = bytearray(st["name"].ljust(8).encode("ascii")[:8])
    out += struct.pack("<H", round(st["tempo"] * 10)) + bytes([st["num"]])
    bars = [min(8, b) - 1 for b in st["bars"]]
    out += bytes([bars[0] | bars[1] << 4, bars[2] | bars[3] << 4, bars[4] | bars[5] << 4])
    out.append(sum(1 << t for t in tracks))
    for t in sorted(tracks):
        tr = tracks[t]
        out += bytes([tr["chan"] - 1 | tr["drum"] << 7, tr["prog"], tr["vol"], tr["pan"], tr["rev"], tr["cho"],
                      min(255, round(tr["vscale"] * 128))])
    out.append(len(seen))
    for s in range(6):
        for t in range(8):
            u = uses[s][t]
            if not u:
                out.append(0xFF)
                continue
            i = seen[(u["i"], u["typ"], u["drum"])]
            other = u["prog"] != tracks[t]["prog"] and not u["drum"]
            out.append(i | other << 7)
            if other:
                out.append(u["prog"])
    return bytes(out)


def cost_new(melodic):
    return 18 if melodic else 11


def build(rom, styles=None):
    """the bank of a ROM's styles (build_from)"""
    styles = styles or rom.styles()
    return build_from([(st,) + style_phrases(rom, st) for st in styles])


def build_from(sp):
    """the bank: MAGIC, style count, priors size, the priors, then per style: its header's and stream's sizes,
    the header, the range-coded stream. sp: per style (st, tracks, uses, order, seen) as style_phrases makes them
    (st: name, tempo, num (beats a bar), bars (six sections)). Returns (bytes, per-style sizes, stats)"""
    cnt = Count()
    for st, tracks, uses, order, seen in sp:           # the training pass
        bars = []
        for k, p in order:
            code_phrase(cnt, p, bars, plan_bar(cost_new))
    priors = cnt.priors()
    probs = probs_from(priors)
    blob = bytearray(MAGIC + struct.pack("<HH", len(sp), len(priors)) + priors)
    per, ref = {}, {}
    for st, tracks, uses, order, seen in sp:
        e, bars = Enc(probs), []
        for k, p in order:
            code_phrase(e, p, bars, plan_bar(cost_new))
        data = e.finish()
        h = head(st, tracks, uses, seen)
        blob += struct.pack("<HH", len(h), len(data)) + h + data
        per[st["name"]] = 4 + len(h) + len(data)
    stats = dict(priors=len(priors), probs=NPROB, notes=sum(len(sum(p["bars"], [])) for s in sp for k, p in s[3]),
                 ctrl=sum(sum(len(r) for r in p["runs"]) for s in sp for k, p in s[3]))
    return bytes(blob), per, stats


def decode_bank(blob):
    """the reference decoder: [(name, header bytes, [phrase dicts])]"""
    assert blob[:4] == MAGIC
    ns, np_ = struct.unpack_from("<HH", blob, 4)
    priors = blob[8:8 + np_]
    probs = probs_from(priors)
    p, out = 8 + np_, []
    for _ in range(ns):
        hl, dl = struct.unpack_from("<HH", blob, p)
        h = blob[p + 4:p + 4 + hl]
        data = blob[p + 4 + hl:p + 4 + hl + dl]
        p += 4 + hl + dl
        tm = h[14]
        nph = h[15 + 7 * bin(tm).count("1")]
        d, bars, phs = Dec(probs, data), [], []
        for _ in range(nph):
            phs.append(code_phrase(d, None, bars))
        out.append((h[:8].decode().rstrip(), h, phs))
    return out


def expected(rom, styles=None):
    """what the decoder must give: the importer's events, quantised as the codec stores them"""
    res = []
    for st in styles or rom.styles():
        tracks, uses, order, seen = style_phrases(rom, st)
        phs = []
        for k, p in order:
            notes = []
            for b, bar in enumerate(p["bars"]):
                for (t, gu, k2, v) in bar:
                    notes.append((b * p["blen"] + t, gu * GATE_UNIT if not p["drum"] else DRUM_GATE, k2, v))
            phs.append((p, sorted(notes, key=lambda n: (n[0], n[2])), [e for r in p["runs"] for e in r]))
        res.append((st["name"], phs))
    return res


def test(rom):
    blob, per, stats = build(rom)
    dec = decode_bank(blob)
    exp = expected(rom)
    bad, n, worst_v = 0, 0, 0
    for (name, h, phs), (name2, eph) in zip(dec, exp):
        for d, (p, notes, ctl) in zip(phs, eph):
            n += 1
            ok = len(d["notes"]) == len(notes) and d["ctl"] == ctl and d["length"] == p["length"] and \
                (d["typ"], d["drum"], d["lo"], d["hi"], d["hikey"]) == (p["typ"], p["drum"], p["lo"], p["hi"], p["hikey"])
            for a, b in zip(d["notes"], notes):
                ok &= a[0] == b[0] and a[1] == b[1] and a[2] == b[2]
                worst_v = max(worst_v, abs(a[3] - b[3]))
            bad += not ok
    print("codec round trip: %d phrases of %d styles, %d differ; velocity within %d; %d bytes" % (
        n, len(dec), bad, worst_v, len(blob)))
    return 1 if bad else 0


def dump(dec, f):
    """the decoded bank as text (the C decoder's test prints the same)"""
    for name, h, phs in dec:
        f.write("style %s %d\n" % (name, len(phs)))
        for d in phs:
            f.write("phrase %d %d %d %d %d %d %d %d\n" % (["bypass", "parallel", "chord1", "chord2", "bass"].index(
                d["typ"]), d["drum"], d["lo"], d["hi"], d["hikey"], d["length"], len(d["notes"]), len(d["ctl"])))
            for t, g, k, v in d["notes"]:
                f.write("n %d %d %d %d\n" % (t, g, k, v))
            for t, k, v in d["ctl"]:
                f.write("c %d %d %d\n" % (t, k, v))


def ctest(blob):
    """compile the device's decoder (firmware/src/dsp/qydec.c) for the host, decode the bank, compare with this
    decoder"""
    here = os.path.dirname(os.path.abspath(__file__))
    dsp = os.path.join(here, "..", "firmware", "src", "dsp")
    with tempfile.TemporaryDirectory() as tmp:
        bank, exe = os.path.join(tmp, "bank.qyb"), os.path.join(tmp, "qydec_test")
        with open(bank, "wb") as f:
            f.write(blob)
        with open(os.path.join(tmp, "ref.txt"), "w") as f:
            dump(decode_bank(blob), f)
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I" + dsp, "-o", exe, os.path.join(dsp, "qydec.c"),
                        os.path.join(here, "qydec", "qydec_test.c")], check=True)
        r = subprocess.run([exe, bank, os.path.join(tmp, "c.txt")], capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode:
            print(r.stderr)
            return 1
        same = open(os.path.join(tmp, "ref.txt")).read() == open(os.path.join(tmp, "c.txt")).read()
        print("C decoder %s the Python decoder" % ("matches" if same else "DIFFERS from"))
        return 0 if same else 1


# ---------------------------------------------------------------- the bank on the FM-1 (firmware/src/dsp/qybank.h)

# the data's logical bytes (qybank.h QB_CAPACITY: 63 sectors of 4088, the guard bytes stepped over; the FM-1 maps
# them to flash as it writes them)
QB_SECTOR, QB_GUARD = 4096, 0xF00
QB_CAPACITY = 63 * (QB_SECTOR - 8)
QB_MAGIC, QB_VERSION, QB_DIR_HEAD, QB_DIR_ENTRY, QB_MAXSTYLES = b"PQB1", 1, 32, 24, 128


def device_image(blob):
    """the bank as the FM-1 stores it: (image, info). image: the directory sector (4096 bytes) then the logical
    data (the priors, then each style's record: header, stream); qy70_install.py writes it, the simulator loads it"""
    assert blob[:4] == MAGIC
    ns, np_ = struct.unpack_from("<HH", blob, 4)
    if ns > QB_MAXSTYLES:
        raise ValueError("%d styles: the directory holds %d" % (ns, QB_MAXSTYLES))
    priors = blob[8:8 + np_]
    data = bytearray(priors)
    ents, p = [], 8 + np_
    for _ in range(ns):
        hl, dl = struct.unpack_from("<HH", blob, p)
        h, st = blob[p + 4:p + 4 + hl], blob[p + 4 + hl:p + 4 + hl + dl]
        p += 4 + hl + dl
        ents.append(h[:8] + struct.pack("<IHHIHBB", len(data), hl, dl, zlib.crc32(h + st), struct.unpack_from("<H", h, 8)[0],
                                        h[10], 0))
        data += h + st
    if len(data) > QB_CAPACITY:
        raise ValueError("the bank is %d bytes: the FM-1 has room for %d" % (len(data), QB_CAPACITY))
    d = bytearray(QB_MAGIC + struct.pack("<HH", QB_VERSION, ns) + MAGIC + struct.pack("<IIHHII", len(data), 0, np_, 0,
                                                                                         zlib.crc32(priors), 0))
    for e in ents:
        d += e
    d += struct.pack("<I", zlib.crc32(d))
    assert len(d) <= QB_GUARD
    sector = bytes(d) + b"\xFF" * (QB_SECTOR - len(d))
    return sector + bytes(data), dict(styles=ns, bytes=len(data), capacity=QB_CAPACITY)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom", nargs="?")
    ap.add_argument("--synthetic", action="store_true", help="a test bank from Polly's styles/*.sty, no ROM")
    ap.add_argument("--out")
    ap.add_argument("--image", help="the bank as the FM-1 stores it (for the simulator's bank command)")
    ap.add_argument("--sizes", action="store_true")
    ap.add_argument("--test", action="store_true")
    ap.add_argument("--ctest", action="store_true")
    a = ap.parse_args(argv)
    if a.synthetic == bool(a.rom):
        ap.error("give a ROM or --synthetic")
    if a.synthetic:
        import qybank_synth
        rom = None
        blob, per, stats = build_from(qybank_synth.styles())
    else:
        with open(a.rom, "rb") as f:
            rom = q.Rom(f.read())
        if a.test:
            raise SystemExit(test(rom))
        blob, per, stats = build(rom)
    if a.image:
        img, info = device_image(blob)
        with open(a.image, "wb") as f:
            f.write(img)
        print("%s: %d styles, %d of %d bytes" % (a.image, info["styles"], info["bytes"], info["capacity"]))
    if a.ctest:
        raise SystemExit(ctest(blob))
    if a.out:
        with open(a.out, "wb") as f:
            f.write(blob)
        print("%s: %d bytes" % (a.out, len(blob)))
    if a.sizes:
        for name, n in per.items():
            print("%-8s %6d" % (name, n))
        print("%d styles: %d bytes (priors %d bytes; %d probabilities); %d notes, %d controller events" % (
            len(per), len(blob), stats["priors"], stats["probs"], stats["notes"], stats["ctrl"]))


if __name__ == "__main__":
    main()
