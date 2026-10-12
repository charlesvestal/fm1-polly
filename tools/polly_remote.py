#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Play Polly's panel from the computer, for testing: buttons, keys, knobs and MASTER sent over USB-MIDI while the
screen is mirrored (firmware/src/mirror.c, cmd 0x54), the screen saved as PNGs. The FM-1 takes them as if pressed on
its panel, only while this tool mirrors it; quit (or 6 s of silence) and everything is let go.

    python3 tools/polly_remote.py tap HOME                 one action (names as in the scenarios)
    python3 tools/polly_remote.py turn K1 3 shot s.png     several, in order
    python3 tools/polly_remote.py run tests/scenarios/home.polly [--out DIR]
                                                           a scenario against the FM-1: its taps, keys, turns, waits
                                                           and shots (PNGs into DIR); what reads the simulator's insides
                                                           (expect, reboot, midi, wav ...) is skipped and counted, but
                                                           "expect lit_BTN 0|1" (the LEDs, from the panel reports)
    ... --sim                                              the simulator (build/host/polly_host --serve) instead

Actions: tap BTN | press BTN | release BTN | key K down|up | tapkey K | turn ENC N [MS] | spin ENC N | master N |
wait MS | shot FILE.png | leds.   BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+;
K: 0..26 (F3 = 0), w0..w15 (white keys), b0..b10 (black); ENC: SELECT ALGO PRESET K1 K2 K3 K4.

As a library:
    from polly_remote import Remote, connect
    with connect() as r:            # (connect(sim=True): the simulator)
        r.tap("HOME"); r.turn("K1", 2); r.wait(300); r.shot("home.png"); print(r.lit("PLAY"))

Needs mido with python-rtmidi for the FM-1 (as tools/fm1_install.py). Never writes the flash itself (a SAVE you send
saves, as on the panel); the update mode (OCT- + OCT+ held) is the hand's only. Exit codes: 0 done, 1 failed, 2 bad
arguments, 3 FM-1 not found, 4 refused (an older Polly, without the remote), 6 not Polly.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fm1_install as I  # noqa: E402
import polly_backup as B  # noqa: E402
import polly_mirror as M  # noqa: E402

MR_REQ, MR_RECT, MR_FILL, MR_INPUT, MR_REMOTE = 0x50, 0x51, 0x52, 0x53, 0x54
BTN = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"]
ENC = ["SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"]
ENC_ALIAS = {"ALGORITHM": "ALGO", "PRESETS": "PRESET", "KNOB1": "K1", "KNOB2": "K2", "KNOB3": "K3", "KNOB4": "K4"}
WHITE_K = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK_K = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
KEEP_S = 2.0              # the keep-alive (Polly stops mirroring after 6 s without one)
STEP_S = 0.065            # a detent's gap: Polly's knobs accelerate under 60 ms (spin: exact steps)


class RemoteError(I.InstallError):
    pass


def btn_id(name):
    n = name.upper()
    if n not in BTN:
        raise RemoteError("usage", f"no button {name} (one of {' '.join(BTN)})")
    return BTN.index(n)


def enc_id(name):
    n = ENC_ALIAS.get(name.upper(), name.upper())
    if n not in ENC:
        raise RemoteError("usage", f"no encoder {name} (one of {' '.join(ENC)})")
    return ENC.index(n)


def key_id(s):
    s = str(s).lower()
    try:
        k = WHITE_K[int(s[1:])] if s[0] == "w" else BLACK_K[int(s[1:])] if s[0] == "b" else int(s)
    except (ValueError, IndexError):
        k = -1
    if not 0 <= k <= 26:
        raise RemoteError("usage", f"no key {s} (0..26, w0..w15, b0..b10)")
    return k


class Remote:
    """the panel from the computer, over a link (fm1_install's: send, read, drain, close; now() if it keeps its own
    time, as the simulator's)"""

    def __init__(self, link):
        self.link, self.scr = link, M.Screen()
        self.seq, self.on, self.version = 0, False, 0
        self.input = None                       # the last panel report: dict (buttons, keys, master, lit, lit_keys)
        self.last_screen = self.last_keep = 0.0
        self.mseq = -1                          # the next screen frame's seq (a gap: a refresh)
        self.screen_frames = 0

    def now(self):
        return self.link.now() if hasattr(self.link, "now") else time.monotonic()

    # ---------------------------------------------------------------- the link
    def _handle(self, pkt):
        f = B.parse_frame(pkt)
        if not f:
            return None
        cmd, b = f
        if cmd in (MR_RECT, MR_FILL) and len(b) >= 5:
            if self.mseq >= 0 and b[4] != self.mseq:
                self._send(MR_REQ, b"\x02")    # a frame missed: everything again
            self.mseq = (b[4] + 1) & 255
            self.scr.frame(pkt)
            self.screen_frames += 1
            self.last_screen = self.now()
        elif cmd == MR_INPUT and len(b) >= 21:
            u32 = lambda o: b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24  # noqa: E731
            lit = b[15] | b[16] << 8
            self.input = dict(buttons=b[0] | b[1] << 8, keys=u32(2), master=b[13] | b[14] << 8,
                              lit=lit & 0x7FFF, play_red=bool(lit & 0x8000), lit_keys=u32(17))
        elif cmd == MR_REQ and len(b) >= 4:
            self.version, self.on = b[0], bool(b[1])
        return cmd, b

    def _send(self, cmd, body):
        if not self.link.send(B.frame(cmd, bytes(body))):
            raise RemoteError("lost", "the FM-1 was disconnected")

    def _pump(self, secs):
        """read and keep alive for secs; the first frame's (cmd, body) list"""
        got, end = [], self.now() + secs
        while True:
            left = end - self.now()
            if self.on and self.now() - self.last_keep >= KEEP_S:
                self.last_keep = self.now()
                self._send(MR_REQ, b"\x01")
            if left <= 0:
                return got
            pkt = self.link.read(min(left, 0.05))
            if pkt is not None:
                h = self._handle(pkt)
                if h:
                    got.append(h)
            if getattr(self.link, "lost", False):
                raise RemoteError("lost", "the FM-1 was disconnected")

    def start(self):
        """mirroring on (the whole screen sent), the remote's version checked"""
        self.link.drain()
        for _ in range(3):
            self._send(MR_REQ, b"\x02")
            self.last_keep = self.now()
            end = self.now() + 1.0
            while self.now() < end and not self.on:
                self._pump(0.05)
            if self.on:
                break
        if not self.on:
            raise RemoteError("stopped", "no answer to the mirror (is Polly running?)")
        if self.version < 2:
            raise RemoteError("refused", f"this Polly mirrors (version {self.version}) but has no remote: update it")
        self.wait_screen()
        return self

    def stop(self):
        try:
            if self.on:
                self._req(0)
                self._send(MR_REQ, b"\x00")
                self._pump(0.05)
        finally:
            self.on = False

    def close(self):
        try:
            self.stop()
        finally:
            self.link.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _req(self, op, a=0, b=0):
        """one request (cmd 0x54), its answer waited for (the device keeps one frame from the host at a time); asked
        again with the same seq when the answer is lost (done once)"""
        self.seq = (self.seq + 1) & 255
        body = bytes([self.seq, op, a & 255, b & 255, 0])
        for _ in range(5):
            self._send(MR_REMOTE, body)
            end = self.now() + 0.3
            while self.now() < end:
                for cmd, r in self._pump(0.01):
                    if cmd == MR_REMOTE and len(r) >= 3 and r[0] == self.seq:
                        if not r[2]:
                            raise RemoteError("refused", "the FM-1 refused (mirroring off?)")
                        return
        raise RemoteError("stopped", "the FM-1 does not answer the remote")

    # ---------------------------------------------------------------- the panel
    def press(self, name):
        self._req(1, btn_id(name), 1)

    def release(self, name):
        self._req(1, btn_id(name), 0)

    def tap(self, name, hold_ms=40):
        self.press(name)
        self.wait(hold_ms)
        self.release(name)
        self.wait(40)

    def key(self, k, down=True):
        self._req(2, key_id(k), 1 if down else 0)

    def tapkey(self, k, hold_ms=60):
        self.key(k, True)
        self.wait(hold_ms)
        self.key(k, False)
        self.wait(30)

    def turn(self, enc, n, gap_ms=80):
        """n detents, gap_ms apart (as a hand turns it: Polly's acceleration applies)"""
        e, d = enc_id(enc), 1 if n > 0 else -1
        for i in range(abs(int(n))):
            self._req(3, e, d & 255)
            self.wait(gap_ms)

    def spin(self, enc, n):
        """exactly n detents (one at a time, slower than the acceleration)"""
        self.turn(enc, n, STEP_S * 1000)

    def master(self, gain):
        """MASTER's gain 0..4096; None: the pot's again"""
        v = 0xFFFF if gain is None else max(0, min(4096, int(gain)))
        self._req(4, v & 255, v >> 8)

    def letgo(self):
        self._req(0)

    def wait(self, ms):
        self._pump(ms / 1000.0)

    def wait_screen(self, quiet=0.25, limit=5.0):
        """until the screen stops changing (quiet s with no frame)"""
        t0 = self.now()
        while self.now() - t0 < limit:
            self._pump(0.05)
            if self.now() - max(self.last_screen, t0) >= quiet and self.screen_frames:
                return True
        return False

    def shot(self, path, scale=1):
        self.wait_screen()
        if self.scr.bad:
            self._send(MR_REQ, b"\x02")       # (damaged frames: everything again)
            self.scr.bad = 0
            self.wait_screen()
        self.scr.png(path, scale)

    def lit(self, name):
        """a button's LED (the last panel report)"""
        self._pump(0.03)
        return bool(self.input and self.input["lit"] >> btn_id(name) & 1)

    def leds(self):
        self._pump(0.03)
        s = self.input or dict(lit=0, lit_keys=0)
        return [b for i, b in enumerate(BTN) if s["lit"] >> i & 1], [k for k in range(27) if s["lit_keys"] >> k & 1]

    # ---------------------------------------------------------------- scenarios
    def do(self, words, outdir=".", log=print):
        """one scenario line (split); False when it is one the device cannot run (skipped)"""
        c, a = words[0], words[1:]
        if c == "wait":
            self.wait(int(a[0]))
        elif c in ("tap", "press", "release"):
            getattr(self, c)(a[0])
        elif c == "key":
            self.key(a[0], a[1] == "down")
        elif c == "tapkey":
            self.tapkey(a[0])
        elif c == "strum":
            x, y, gap = int(a[0]), int(a[1]), int(a[2]) if len(a) > 2 else 25
            s = 1 if y >= x else -1
            for w in range(x, y + s, s):
                self.key(WHITE_K[w & 15], True)
                self.wait(gap)
                self.key(WHITE_K[w & 15], False)
        elif c == "turn":
            self.turn(a[0], int(a[1]), int(a[2]) if len(a) > 2 else 80)
        elif c == "spin":
            self.spin(a[0], int(a[1]))
        elif c == "master":
            self.master(int(a[0]))
        elif c == "shot":
            path = os.path.join(outdir, a[0])
            self.shot(path)
            log(f"  shot {path}")
        elif c == "leds":
            b, k = self.leds()
            log(f"  leds: buttons {' '.join(b) or '-'} keys {' '.join(map(str, k)) or '-'}")
        elif c == "expect" and hasattr(self.link, "expect"):   # the simulator: its own state, read
            ln = self.link.expect(a[0], a[1])
            if not ln.startswith("  ok"):
                raise RemoteError("failed", ln.strip() or f"expect {a[0]}: no answer")
            log(ln)
        elif c == "expect" and a[0].startswith("lit_"):
            got = int(self.lit(a[0][4:]))
            if got != int(a[1]):
                raise RemoteError("failed", f"expect {a[0]} {a[1]}: got {got}")
            log(f"  ok {a[0]} == {got}")
        else:
            return False
        return True

    def run(self, path, outdir=".", log=print):
        """a .polly scenario: what it can run, in order; the skipped lines' count"""
        skipped = {}
        with open(path) as f:
            for n, line in enumerate(f, 1):
                words = line.split() if not line.lstrip().startswith("#") else []
                if not words:
                    continue
                try:
                    if not self.do(words, outdir, log):
                        skipped[words[0]] = skipped.get(words[0], 0) + 1
                except RemoteError as e:
                    raise RemoteError(e.code, f"{path}:{n}: {e}")
        return skipped


# -------------------------------------------------------------------- the simulator
class SimLink:
    """build/host/polly_host --serve: the simulated FM-1 on a cable; its time runs only while read() waits"""

    def __init__(self, host=None, outdir=None):
        host = host or os.path.join(HERE, "..", "build", "host", "polly_host")
        outdir = outdir or tempfile.mkdtemp(prefix="polly_sim_")   # (its MIDI log)
        self.p = subprocess.Popen([host, "--serve", outdir], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1)
        self.q, self.t, self.lost = [], 0.0, False

    def _cmd(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()

    def send(self, pkt):
        self._cmd("sx " + bytes(pkt).hex().upper())
        return True

    def read(self, timeout):
        if not self.q:
            self._cmd(f"run {max(1, round(timeout * 1000))}")
            while True:
                ln = self.p.stdout.readline()
                if not ln:
                    self.lost = True
                    break
                if ln.startswith("sx "):
                    self.q.append(bytes.fromhex(ln[3:].strip()))
                elif ln.startswith("ok "):
                    self.t = int(ln[3:]) / 1000.0
                    break
        return self.q.pop(0) if self.q else None

    def now(self):
        return self.t

    def drain(self):
        self.q.clear()

    def expect(self, what, val):
        """the simulator's own state (tests): its expect line"""
        self._cmd(f"expect {what} {val}")
        while True:
            ln = self.p.stdout.readline()
            if not ln or ln.startswith("  ok") or ln.startswith("FAIL"):
                return ln.rstrip()

    def sim_screen(self, path):
        """the simulator's own screen: RGB565 pixels, as the mirror's Screen keeps them (to compare)"""
        self._cmd(f"fbdump {os.path.abspath(path)}")
        self.p.stdout.readline()
        with open(path, "rb") as f:
            raw = f.read()
        return [raw[i] << 8 | raw[i + 1] for i in range(0, len(raw), 2)]

    def close(self):
        if self.p.poll() is None:
            try:
                self._cmd("quit")
                self.p.wait(5)
            except Exception:
                self.p.kill()


def connect(port=None, sim=False, backend=None, outdir=None):
    """a started Remote on the FM-1 (or the simulator)"""
    if sim:
        link = SimLink(outdir=outdir)
    else:
        if backend is None:
            try:
                backend = I.MidoBackend()
            except ImportError:
                raise RemoteError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
        link = B.find(backend, port).link
    r = Remote(link)
    try:
        return r.start()
    except Exception:
        link.close()
        raise


def actions(words):
    """the command line's words -> actions (each a list of words)"""
    arity = dict(tap=1, press=1, release=1, key=2, tapkey=1, spin=2, master=1, wait=1, shot=1, leds=0)
    out, i = [], 0
    while i < len(words):
        c = words[i]
        if c == "turn":                        # turn ENC N [MS]
            n = 3 if i + 3 < len(words) and words[i + 3].isdigit() else 2
        elif c in arity:
            n = arity[c]
        else:
            raise RemoteError("usage", f"no action {c}")
        if i + n >= len(words):
            raise RemoteError("usage", f"{c}: needs {n} argument(s)")
        out.append(words[i:i + n + 1])
        i += n + 1
    return out


def main(argv=None, backend=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("words", nargs="+", help="actions, or: run SCENARIO.polly")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name; default: Polly's)")
    ap.add_argument("--out", metavar="DIR", default=".", help="where shots go (default: here)")
    ap.add_argument("--sim", action="store_true", help="the simulator (build/host/polly_host --serve), not the FM-1")
    a = ap.parse_args(argv)
    log = lambda s: print(s, file=out)  # noqa: E731
    try:
        scenario = a.words[0] == "run"
        if scenario and len(a.words) != 2:
            raise RemoteError("usage", "run SCENARIO.polly")
        acts = [] if scenario else actions(a.words)
        os.makedirs(a.out, exist_ok=True)
        with connect(a.port, a.sim, backend) as r:
            if scenario:
                skipped = r.run(a.words[1], a.out, log)
                if skipped:
                    log("skipped (the simulator's only): " + ", ".join(f"{k} x{v}" for k, v in sorted(skipped.items())))
            else:
                for w in acts:
                    r.do(w, a.out, log)
        log("done")
        return 0
    except OSError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except I.InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        return dict(B.EXIT, refused=4).get(e.code, 1)
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
