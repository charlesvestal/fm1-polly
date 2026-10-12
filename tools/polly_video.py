#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A video of Polly from the host simulator: a storyboard (the scenario language plus captions and bar-timed
chords, tools/video/*.story) expanded into a plain scenario with a screen dump every 1/FPS s of simulated time,
run on build/host/polly_host (the audio into a WAV), then each frame drawn (the screen 3x, the FM-1's panel with
the buttons and keys held and the knobs turned, a caption) and muxed with ffmpeg. Nothing is played.

  python3 tools/polly_video.py tools/video/song.story OUT.mp4 [--work DIR] [--fps 10] [--frames-only]

Storyboard, one command a line (# comments); the scenario's own commands pass through (their time is counted as
the host counts it: tap 80 ms, press / release / key 2, tapkey 90, spin 1, turn N x GAP), plus:
  cap TITLE | DETAIL          the caption from here on (DETAIL optional)
  head TEXT                   the header line after "Polly" (default: a song from scratch, on the FM-1)
  still NAME                  a still of the frame a moment from here (the screen and the panel), with --stills DIR:
                              DIR/NAME.jpg, for a lesson's step
  poster                      the thumbnail's frame (else the middle), with --poster FILE: a title card over it

  python3 tools/polly_video.py STORY OUT.mp4 [--stills DIR] [--poster FILE.jpg]
  barms MS                    a bar's length (the tempo), for `at`
  anchor NAME                 NAME = the time now (the next command's moment)
  at NAME BARS [MS]           wait until NAME + BARS bars + MS (a float of bars is fine)
  chord HOLD K K K...         the keys down together, held HOLD ms, up
  note HOLD K                 one key, held HOLD ms
  mel NAME BAR BEAT DUR K     key K at NAME + BAR bars + BEAT beats (+10 ms), held 85 % of DUR beats (4 beats a bar)

Setup that isn't filmed (a lesson that starts where the one before ended):
  hide                        from here nothing is recorded: no frames, no events (captions, stills, the panel), no
                              audio; the commands still run on the host (expects too). Only before the video starts
  show                        the video starts here: frame 0, the clock at 0:00, the WAV (song.wav) from here
  include FILE                FILE's commands (a storyboard; relative to this one's folder, else to the repository),
                              run hidden: its captions, stills, poster, head, hide and show are left out, its barms
                              and anchors are its own. Includes nest (104 includes 103, which includes 102, ...)
A storyboard with neither starts its video at its first command.
"""
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HOST = os.path.join(ROOT, "build", "host", "polly_host")
FFMPEG = shutil.which("ffmpeg") or "/opt/homebrew/bin/ffmpeg"


# ------------------------------------------------------------------------------------------- the expansion ---
class Expander:
    def __init__(self, step):
        self.t = 0                  # ms simulated
        self.step = step            # ms between frames
        self.next_shot = 0
        self.frame = 0
        self.out = []
        self.events = []            # [t, kind, name, value]
        self.anchors = {}
        self.barms = 2000.0
        self.hidden = False         # hide ... show: run, not recorded
        self.inc = 0                # include depth (> 0: hidden, hide / show ignored)
        self.started = False        # the video has begun (its frame 0)
        self.t0 = 0                 # the simulated time of frame 0
        self.wavname = "song.wav"

    def begin(self):
        """the video's frame 0, here: the clock and the frames count from now, the WAV starts"""
        self.started = True
        self.t0 = self.t
        self.next_shot = self.t
        self.out.append(f"wav {self.wavname}")

    def shots_due(self):
        if self.hidden:
            return
        if not self.started:
            self.begin()
        while self.next_shot <= self.t:
            self.out.append(f"echo F {self.frame} {self.t - self.t0}\n")
            self.out.append("leds")
            self.out.append(f"fbdump f{self.frame:05d}.raw")
            self.frame += 1
            self.next_shot = self.t0 + self.frame * self.step

    def prim(self, line, cost):
        self.shots_due()
        self.out.append(line)
        self.t += cost

    def wait(self, ms):
        ms = int(round(ms))
        while ms > 0:
            self.shots_due()
            n = ms if self.hidden else min(ms, self.next_shot - self.t)
            self.out.append(f"wait {n}")
            self.t += n
            ms -= n
        self.shots_due()

    def ev(self, kind, name, value=0):
        if self.hidden:
            return
        if not self.started:
            self.begin()
        self.events.append([self.t - self.t0, kind, name, value])

    def press(self, b):
        self.ev("btn", b, 1)
        self.prim(f"press {b}", 2)

    def release(self, b):
        self.ev("btn", b, 0)
        self.prim(f"release {b}", 2)

    def key(self, k, down):
        self.ev("key", str(k), 1 if down else 0)
        self.prim(f"key {k} {'down' if down else 'up'}", 2)

    def spin(self, e, n):
        self.ev("enc", e, n)
        self.prim(f"spin {e} {n}", 1)

    def include(self, path, base):
        for p in (os.path.join(base, path), os.path.join(ROOT, path)):
            if os.path.exists(p):
                break
        else:
            raise SystemExit(f"include {path}: no such storyboard")
        keep = (self.hidden, self.barms, dict(self.anchors))
        self.hidden = True
        self.inc += 1
        self.anchors = {}
        for raw in open(p):
            self.line(raw, os.path.dirname(os.path.abspath(p)))
        self.inc -= 1
        self.hidden, self.barms, self.anchors = keep

    def line(self, raw, base="."):
        s = raw.strip()
        if not s or s.startswith("#"):
            return
        w = s.split()
        c = w[0]
        if c == "include":
            self.include(s[8:].strip(), base)
        elif c == "hide":
            if self.inc:
                return
            if self.started:
                raise SystemExit("hide: only before the video starts (the first frame, caption or press)")
            self.hidden = True
        elif c == "show":
            if self.inc:
                return
            self.hidden = False
            if not self.started:
                self.begin()
        elif c == "cap":
            title, _, detail = s[4:].partition("|")
            self.ev("cap", title.strip(), detail.strip())
        elif c == "head":                   # (the whole video's: kept while hidden, but not from an include)
            if not self.inc:
                self.events.append([0, "head", s[5:].strip(), 0])
        elif c == "still":
            self.ev("still", w[1])
        elif c == "poster":
            self.ev("poster", "")
        elif c == "barms":
            self.barms = float(w[1])
        elif c == "anchor":
            self.anchors[w[1]] = self.t
        elif c == "at":
            due = self.anchors[w[1]] + float(w[2]) * self.barms + (float(w[3]) if len(w) > 3 else 0.0)
            if due < self.t - 0.5:
                raise SystemExit(f"at {' '.join(w[1:])}: {due:.0f} ms is past (now {self.t})")
            self.wait(due - self.t)
        elif c == "mel":
            beat = self.barms / 4
            due = self.anchors[w[1]] + float(w[2]) * self.barms + float(w[3]) * beat + 10
            if due < self.t - 0.5:
                raise SystemExit(f"mel {' '.join(w[1:])}: {due:.0f} ms is past (now {self.t})")
            self.wait(due - self.t)
            self.key(w[5], True)
            self.wait(float(w[4]) * beat * 0.85 - 2)
            self.key(w[5], False)
        elif c == "wait":
            self.wait(int(w[1]))
        elif c in ("press", "release"):
            (self.press if c == "press" else self.release)(w[1])
        elif c == "tap":
            self.press(w[1])
            self.wait(38)
            self.release(w[1])
            self.wait(38)
        elif c == "key":
            self.key(w[1], w[2] == "down")
        elif c == "tapkey":
            self.key(w[1], True)
            self.wait(58)
            self.key(w[1], False)
            self.wait(28)
        elif c == "chord" or c == "note":
            hold, ks = int(w[1]), w[2:]
            for k in ks:
                self.key(k, True)
            self.wait(hold - 2 * len(ks))
            for k in ks:
                self.key(k, False)
        elif c == "spin":
            self.spin(w[1], int(w[2]))
        elif c == "turn":
            n, gap = int(w[2]), int(w[3]) if len(w) > 3 else 80
            for _ in range(abs(n)):
                self.spin(w[1], 1 if n > 0 else -1)
                self.wait(gap - 1)
        elif c in ("expect", "wav", "wavstop", "echo", "leds", "style", "master", "hitreset"):
            self.prim(s, 0)
        else:
            raise SystemExit(f"storyboard: unknown command {c}")


def expand(story, step, wav):
    x = Expander(step)
    x.wavname = wav
    base = os.path.dirname(os.path.abspath(story))
    for raw in open(story):
        x.line(raw, base)
    if not x.started:
        raise SystemExit("storyboard: nothing shown (a hide with no show)")
    x.shots_due()
    x.prim("wavstop", 0)
    return x


# ------------------------------------------------------------------------------------------------ drawing ---
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

W, H = 1920, 1080
CREAM, CARD, INK, MUTED, RULE = (250, 244, 232), (255, 250, 241), (72, 57, 47), (138, 119, 100), (230, 220, 203)
CORAL, CORAL_L, TEAL, TEAL_L = (224, 104, 58), (246, 180, 142), (38, 166, 154), (143, 214, 206)
BODY, TRAY, KEY_W, KEY_B, KNOB, BTNC, LABEL = ((58, 61, 66), (38, 40, 44), (217, 218, 220), (201, 202, 205),
                                               (31, 33, 36), (46, 49, 53), (232, 232, 234))
BTN_N = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"]
WHITE_K = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK_K = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]
ENC_LABEL = {"SELECT": "SELECT", "ALGO": "ALGORITHM", "PRESET": "PRESETS", "K1": "KNOB1", "K2": "KNOB2",
             "K3": "KNOB3", "K4": "KNOB4"}

AV = "/System/Library/Fonts/Avenir Next.ttc"
AVC = "/System/Library/Fonts/Avenir Next Condensed.ttc"


def font(path, size, index):
    try:
        return ImageFont.truetype(path, size, index=index)
    except OSError:
        return ImageFont.load_default()


def key_of(name):
    if name[0] == "w":
        return WHITE_K[int(name[1:]) & 15]
    if name[0] == "b":
        return BLACK_K[int(name[1:]) % 11]
    return int(name)


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def glow(d, box, rad, base, col, s, a=1.0, oval=False):
    """a soft glow round box (held, turned): rings fading from col out into base, then a firm col outline; a: 0..1"""
    n, st = 6, max(2, int(3.5 * s))
    for i in range(n, 0, -1):
        e = i * st
        c = mix(base, col, a * (1 - i / (n + 1)) * 0.85)
        bb = [box[0] - e, box[1] - e, box[2] + e, box[3] + e]
        if oval:
            d.ellipse(bb, outline=c, width=st + 1)
        else:
            d.rounded_rectangle(bb, radius=rad + e, outline=c, width=st + 1)
    c = mix(base, col, a)
    if oval:
        d.ellipse(box, outline=c, width=max(3, int(4 * s)))
    else:
        d.rounded_rectangle(box, radius=rad, outline=c, width=max(3, int(4 * s)))


class Panel:
    """web/mirror's (web/emu's) panel drawing, in its 1000 x 640 units, scaled by S at (OX, OY)"""

    def __init__(self, ox, oy, s):
        self.ox, self.oy, self.s = ox, oy, s
        self.f_btn = font(AVC, int(15 * s * 1.05), 2)
        self.f_btn2 = font(AVC, int(12 * s * 1.05), 2)
        self.f_knob = font(AVC, int(17 * s), 2)
        self.f_delta = font(AVC, int(30 * s), 2)
        self.knobs = {}
        for x, y, r, lab in [(105, 100, 24, "MASTER"), (215, 100, 24, "SELECT"), (105, 205, 24, "PRESETS"),
                             (215, 205, 24, "ALGORITHM")] + [(610 + i * 104, 100, 22, f"KNOB{i + 1}") for i in range(4)]:
            self.knobs[lab] = (x, y, r)
        self.buttons = {"OCT-": (72, 268, 82, 38), "OCT+": (170, 268, 82, 38)}
        row1, row2 = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"], ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
        for i in range(6):
            self.buttons[row1[i]] = (582 + i * 63, 174, 54, 48)
            self.buttons[row2[i]] = (582 + i * 63, 236, 54, 48)
        x0, y0, w, h = 36, 340, 928, 270
        pitch = (w - 24) / 16
        kw = pitch * 0.78
        self.keys = {}
        for i in range(16):
            self.keys[WHITE_K[i]] = (x0 + 12 + i * pitch + (pitch - kw) / 2, y0 + h * 0.43, kw, h * 0.52, "w")
        for k, a in enumerate(BLACK_AFTER):
            self.keys[BLACK_K[k]] = (x0 + 12 + (a + 1) * pitch - kw / 2, y0 + h * 0.06, kw, h * 0.33, "b")

    def P(self, x, y):
        return (self.ox + x * self.s, self.oy + y * self.s)

    def R(self, x, y, w, h):
        a, b = self.P(x, y)
        return [a, b, a + w * self.s, b + h * self.s]

    def draw(self, d, held_b, lit_b, held_k, lit_k, enc, screen_small, img):
        s = self.s
        d.rounded_rectangle(self.R(4, 4, 992, 632), radius=44 * s, fill=BODY)
        # the knobs
        for lab, (x, y, r) in self.knobs.items():
            cx, cy = self.P(x, y)
            d.ellipse([cx - (r + 5) * s, cy - (r + 5) * s, cx + (r + 5) * s, cy + (r + 5) * s], fill=TRAY)
            e = enc.get(lab)
            if e and e["alpha"] > 0:                 # turned: a teal glow, as a held button
                rr = (r + 6) * s
                glow(d, [cx - rr, cy - rr, cx + rr, cy + rr], 0, BODY, TEAL_L, s, e["alpha"], oval=True)
            d.ellipse([cx - r * s, cy - r * s, cx + r * s, cy + r * s], fill=KNOB, outline=(68, 71, 76), width=2)
            ang = (e["angle"] if e else 0) if lab != "MASTER" else 0
            import math
            th = math.radians(ang)
            def rot(px, py):
                return (cx + px * math.cos(th) - py * math.sin(th), cy + px * math.sin(th) + py * math.cos(th))
            d.line([rot(0, -(r - 5) * s), rot(0, -(r - 15) * s)], fill=LABEL, width=max(2, int(3 * s)))
            d.text((cx, cy - (r + 12) * s), lab, font=self.f_knob, fill=LABEL, anchor="ms")
            if e and e["text_a"] > 0:
                ta = e["text_a"]
                col = tuple(int(BODY[i] + (CORAL_L[i] - BODY[i]) * ta) for i in range(3))
                d.text((cx + (r + 14) * s, cy + 8 * s - e["rise"] * s), e["text"], font=self.f_delta, fill=col,
                       anchor="ls")
        # the OCT tray, the screen, the buttons' tray
        d.rounded_rectangle(self.R(58, 258, 210, 58), radius=14 * s, fill=TRAY)
        d.rounded_rectangle(self.R(300, 44, 236, 236), radius=26 * s, fill=(17, 18, 20))
        d.rounded_rectangle(self.R(568, 160, 400, 140), radius=18 * s, fill=TRAY)
        if screen_small is not None:
            px, py = self.P(318, 62)
            img.paste(screen_small, (int(px), int(py)))
        for b, (x, y, w, h) in self.buttons.items():
            lit, held = b in lit_b, b in held_b
            face = CORAL if lit else (60, 95, 91) if held else BTNC
            if held:                                 # held: a teal glow round it
                glow(d, self.R(x, y, w, h), 9 * s, TRAY, TEAL_L, s)
            d.rounded_rectangle(self.R(x, y, w, h), radius=9 * s, fill=face,
                                outline=TEAL_L if held else (74, 77, 82), width=max(2, int((4 if held else 1.5) * s)))
            tc = (42, 20, 10) if lit else LABEL
            cx, cy = self.P(x + w / 2, y + h / 2)
            if b == "PLAY":
                d.text((cx, cy - 1 * s), "PLAY", font=self.f_btn2, fill=tc, anchor="ms")
                d.text((cx, cy + 12 * s), "STOP", font=self.f_btn2, fill=tc, anchor="ms")
            else:
                d.text((cx, cy + 5 * s), b, font=self.f_btn, fill=tc, anchor="ms")
        # the keys
        d.rounded_rectangle(self.R(36, 340, 928, 270), radius=18 * s, fill=TRAY)
        for k, (x, y, w, h, kind) in self.keys.items():
            lit, held = k in lit_k, k in held_k
            if kind == "w":
                face = TEAL_L if held else CORAL_L if lit else KEY_W
            else:
                face = TEAL_L if held else TEAL if lit else KEY_B
            if held:                                 # held: a teal glow round it
                glow(d, self.R(x, y, w, h), w * s / 2, TRAY, TEAL_L, s)
            d.rounded_rectangle(self.R(x, y, w, h), radius=w * s / 2, fill=face,
                                outline=TEAL if held else None, width=max(2, int(4 * s)) if held else 0)


def load_screen(path):
    raw = open(path, "rb").read()
    im = Image.new("RGB", (240, 240))
    px = bytearray(240 * 240 * 3)
    for i in range(240 * 240):
        v = raw[2 * i] << 8 | raw[2 * i + 1]
        px[3 * i] = ((v >> 11) & 31) * 255 // 31
        px[3 * i + 1] = ((v >> 5) & 63) * 255 // 63
        px[3 * i + 2] = (v & 31) * 255 // 31
    im.frombytes(bytes(px))
    return im


def wrap(d, text, f, width):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if d.textlength(t, font=f) <= width or not cur:
            cur = t
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def render(work, events, leds, step, nframes):
    panel = Panel(820, 150, 1.06)
    f_title = font(AV, 46, 2)        # Avenir Next Demi Bold
    f_detail = font(AV, 30, 5)
    f_brand = font(AV, 30, 2)
    f_small = font(AV, 22, 5)
    f_step = font(AV, 26, 2)
    caps = [e for e in events if e[1] == "cap"]
    head = next((e[2] for e in events if e[1] == "head"), "a song from scratch, on the FM-1")
    steps = [c for c in caps if re.match(r"^\d+\.", c[2])]
    nsteps = len({re.match(r"^(\d+)\.", c[2]).group(1) for c in steps})
    btn_ev = [e for e in events if e[1] == "btn"]
    key_ev = [e for e in events if e[1] == "key"]
    enc_ev = [e for e in events if e[1] == "enc"]
    angles = {}
    ei = bi = ki = 0
    held_b, held_k = set(), set()
    for n in range(nframes):
        T = n * step
        while bi < len(btn_ev) and btn_ev[bi][0] <= T:
            (held_b.add if btn_ev[bi][3] else held_b.discard)(btn_ev[bi][2])
            bi += 1
        while ki < len(key_ev) and key_ev[ki][0] <= T:
            (held_k.add if key_ev[ki][3] else held_k.discard)(key_of(key_ev[ki][2]))
            ki += 1
        # buttons held at least a frame long: show the ones pressed between frames too (a tap is 40 ms)
        shown_b = set(held_b) | {e[2] for e in btn_ev if T - step < e[0] <= T and e[3]}
        shown_k = set(held_k) | {key_of(e[2]) for e in key_ev if T - step < e[0] <= T and e[3]}
        while ei < len(enc_ev) and enc_ev[ei][0] <= T:
            lab = ENC_LABEL[enc_ev[ei][2]]
            angles[lab] = angles.get(lab, 0) + 18 * enc_ev[ei][3]
            ei += 1
        enc = {}
        for lab in angles:
            name = [k for k, v in ENC_LABEL.items() if v == lab][0]
            past = [e for e in enc_ev[:ei] if e[2] == name]
            last = past[-1][0]
            # the run of turns ending at the last one: gaps under 700 ms
            total, prev = 0, None
            for e in reversed(past):       # (a turn back the other way starts a new run)
                if prev is not None and (prev - e[0] >= 700 or (e[3] > 0) != (past[-1][3] > 0)):
                    break
                total += e[3]
                prev = e[0]
            age = T - last
            enc[lab] = {"angle": angles[lab], "alpha": 1.0 if age < 700 else max(0.0, 1 - (age - 700) / 500),
                        "text": ("+" if total > 0 else "−") + str(abs(total)),
                        "text_a": 1.0 if age < 700 else max(0.0, 1 - (age - 700) / 400), "rise": min(14, age / 60)}
        lb, lw, lbk = leds[n] if n < len(leds) else (set(), "", "")
        lit_k = {WHITE_K[i] for i, c in enumerate(lw) if c == "#"} | {BLACK_K[i] for i, c in enumerate(lbk) if c == "#"}
        scr = load_screen(os.path.join(work, f"f{n:05d}.raw"))
        img = Image.new("RGB", (W, H), CREAM)
        d = ImageDraw.Draw(img)
        # header
        d.text((60, 62), "Polly", font=f_brand, fill=CORAL, anchor="ls")
        d.text((60 + d.textlength("Polly", font=f_brand) + 14, 62), head,
               font=f_detail, fill=MUTED, anchor="ls")
        mm, ss = divmod(int(T / 1000), 60)
        d.text((W - 60, 62), f"{mm}:{ss:02d}", font=f_detail, fill=MUTED, anchor="rs")
        # the screen 3x, crisp, in a bezel
        d.rounded_rectangle([44, 134, 44 + 752, 134 + 752], radius=22, fill=(17, 18, 20))
        img.paste(scr.resize((720, 720), Image.NEAREST), (60, 150))
        # the panel
        small = scr.resize((int(200 * panel.s), int(200 * panel.s)), Image.LANCZOS)
        panel.draw(d, shown_b, lb, shown_k, lit_k, enc, small, img)
        # the caption card
        cap = [c for c in caps if c[0] <= T]
        cy = 150 + 640 * panel.s + 30
        d.rounded_rectangle([820, cy, W - 60, 1040], radius=18, fill=CARD, outline=RULE, width=2)
        if cap:
            title, detail = cap[-1][2], cap[-1][3]
            m = re.match(r"^(\d+)\.\s*(.*)$", title)
            x = 852
            if m:
                lab = f"{m.group(1)} / {nsteps}"
                tw = d.textlength(lab, font=f_step)
                d.rounded_rectangle([x, cy + 30, x + tw + 28, cy + 70], radius=20, fill=CORAL)
                d.text((x + 14, cy + 59), lab, font=f_step, fill=(255, 250, 241), anchor="ls")
                x += tw + 46
                title = m.group(2)
            d.text((x, cy + 66), title, font=f_title, fill=INK, anchor="ls")
            yy = cy + 112
            for ln in wrap(d, detail, f_detail, W - 60 - 852 - 30)[:2]:
                d.text((852, yy), ln, font=f_detail, fill=MUTED, anchor="ls")
                yy += 40
        img.save(os.path.join(work, f"v{n:05d}.png"), compress_level=1)
        if n % 100 == 0:
            print(f"  frame {n}/{nframes}", flush=True)


def frame_at(work, t, step, nframes):
    n = min(max(int(t // step), 0), nframes - 1)
    return Image.open(os.path.join(work, f"v{n:05d}.png")).convert("RGB")


def stills(work, events, step, nframes, out):
    """each `still`: its frame 0.6 s on (the press shown, the screen answered), the screen and the panel, 960 wide"""
    os.makedirs(out, exist_ok=True)
    for e in events:
        if e[1] == "still":
            im = frame_at(work, e[0] + 600, step, nframes)
            ImageDraw.Draw(im).rectangle([810, 850, W, H], fill=CREAM)   # (the caption card's top: gone)
            im = im.crop((30, 112, W - 40, 896))
            im = im.resize((960, im.height * 960 // im.width), Image.LANCZOS)
            im.save(os.path.join(out, e[2] + ".jpg"), quality=86)
            print(f"  still {e[2]}")


def poster(work, events, step, nframes, out):
    """the thumbnail: the `poster` frame (else the middle), its caption card a title card (the header's text, the
    length, a play button), its clock gone; 1280 wide"""
    t = next((e[0] for e in events if e[1] == "poster"), nframes * step // 2)
    im = frame_at(work, t, step, nframes)
    d = ImageDraw.Draw(im)
    head = next((e[2] for e in events if e[1] == "head"), "Polly")
    mm, ss = divmod(int(nframes * step / 1000), 60)
    d.rectangle([W - 300, 20, W, 90], fill=CREAM)
    cy = int(150 + 640 * 1.06 + 30)
    d.rounded_rectangle([820, cy, W - 60, 1040], radius=18, fill=CARD, outline=RULE, width=2)
    r, cx, ccy = 52, 820 + 40 + 52, (cy + 1040) // 2
    d.ellipse([cx - r, ccy - r, cx + r, ccy + r], fill=CORAL)
    d.polygon([(cx - 16, ccy - 26), (cx - 16, ccy + 26), (cx + 28, ccy)], fill=(255, 250, 241))
    d.text((cx + r + 30, ccy + 6), head, font=font(AV, 56, 2), fill=INK, anchor="ls")
    d.text((cx + r + 30, ccy + 52), f"{mm}:{ss:02d}", font=font(AV, 32, 5), fill=MUTED, anchor="ls")
    im.resize((1280, 720), Image.LANCZOS).save(out, quality=88)
    print(f"  poster {out}")


def parse_leds(stdout):
    out, cur = [], None
    for ln in stdout.splitlines():
        if ln.startswith("F "):
            cur = int(ln.split()[1])
        elif ln.strip().startswith("leds:") and cur is not None:
            m = re.match(r"\s*leds: buttons(.*)\| white (\S+) black (\S+)", ln)
            while len(out) < cur:
                out.append((set(), "", ""))
            out.append((set(m.group(1).split()), m.group(2), m.group(3)))
            cur = None
    return out


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        return 2
    story, mp4 = a[0], a[1]
    fps = int(a[a.index("--fps") + 1]) if "--fps" in a else 10
    work = a[a.index("--work") + 1] if "--work" in a else os.path.join(ROOT, "build", "video")
    os.makedirs(work, exist_ok=True)
    for f in os.listdir(work):
        if re.match(r"^[fv]\d{5}\.(raw|png)$", f):
            os.remove(os.path.join(work, f))
    step = 1000 // fps
    x = expand(story, step, "song.wav")
    scen = os.path.join(work, "video.polly")
    open(scen, "w").write("\n".join(x.out) + "\n")
    json.dump(x.events, open(os.path.join(work, "events.json"), "w"))
    print(f"scenario: {len(x.out)} lines, {x.t} ms ({x.t - x.t0} shown), {x.frame} frames")
    r = subprocess.run([HOST, scen, work], capture_output=True, text=True)
    open(os.path.join(work, "host.log"), "w").write(r.stdout + r.stderr)
    fails = [ln for ln in r.stdout.splitlines() if ln.startswith("FAIL") or ln.startswith("line ")]
    for ln in fails:
        print("  " + ln)
    print(r.stdout.strip().splitlines()[-1])
    if "--host-only" in a:
        return 1 if fails else 0
    leds = parse_leds(r.stdout)
    render(work, x.events, leds, step, x.frame)
    if "--stills" in a:
        stills(work, x.events, step, x.frame, a[a.index("--stills") + 1])
    if "--poster" in a:
        poster(work, x.events, step, x.frame, a[a.index("--poster") + 1])
    if "--frames-only" in a:
        return 0
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-framerate", str(fps), "-i", os.path.join(work, "v%05d.png"),
                    "-i", os.path.join(work, "song.wav"), "-c:v", "libx264", "-preset", "slow", "-crf", "18",
                    "-tune", "animation", "-pix_fmt", "yuv420p", "-r", "30", "-af", "volume=5dB", "-c:a", "aac", "-b:a", "192k",
                    "-shortest", "-movflags", "+faststart", mp4], check=True)
    print(f"wrote {mp4}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
