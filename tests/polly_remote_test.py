#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/polly_remote.py against the simulator on a simulated cable (build/host/polly_host --serve: the firmware's
mirror.c and UI, the requests taken as main_fm1.c takes them). Checked: the screen as mirrored equals the simulator's;
a tap, a held button, keys, turns (exact and accelerated), MASTER go through the plat_* calls into the UI; the panel
reports' LEDs; let go when the tool stops and when it goes silent (6 s); refused while not mirroring; an older Polly
(no remote) refused by the tool; a whole scenario (tests/scenarios/style_pick.polly) run through the remote, its
expects read from the simulator; the command line. Run from the repo root:  python3 tests/polly_remote_test.py"""
import io
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import polly_backup as B  # noqa: E402
import polly_remote as R  # noqa: E402

failed = 0
OUT = tempfile.mkdtemp(prefix="polly_remote_")


def ok(cond, what):
    global failed
    print(f"{what:<66} {'ok' if cond else 'FAIL'}")
    failed += not cond


def sim_is(r, what, val):
    ln = r.link.expect(what, str(val))
    if not ln.startswith("  ok"):
        print("   ", ln)
    return ln.startswith("  ok")


# ---- the screen, the panel
r = R.connect(sim=True, outdir=OUT)
ok(r.version == 2 and r.on, "mirroring on, the remote's version (2)")
r.shot(os.path.join(OUT, "start.png"))
ok(r.link.sim_screen(os.path.join(OUT, "start.fb")) == r.scr.px, "the screen mirrored = the simulator's")
ok(sim_is(r, "page", 0), "Home at the start")
r.spin("SELECT", 1)
ok(sim_is(r, "page", 1), "spin SELECT 1: the Song page")
r.tap("HOME")
ok(sim_is(r, "page", 0), "tap HOME: Home again")
r.spin("K2", 5)
ok(sim_is(r, "tempo", 110), "spin K2 5: tempo 105 -> 110 (exact)")
r.turn("K2", 6, 10)
ok(r.link.expect("tempo", ">116").startswith("  ok"), "turn K2 6 10 ms apart: accelerated (past 116)")
r.press("HOME")
r.wait(600)
ok(sim_is(r, "home_shown", 1), "HOME held 600 ms: its shortcuts on screen")
r.tapkey("w2")
r.release("HOME")
r.wait(50)
ok(sim_is(r, "mode", 1) and sim_is(r, "app_loop", 3), "HOME + white key 3: progression P3")
r.key(7, True)
r.wait(50)
ok(sim_is(r, "home_chord_root", 0), "key 7 (C4) held: the chord C")
r.key(7, False)
r.tap("PLAY")
r.wait(300)
ok(sim_is(r, "playing", 1), "tap PLAY: playing")
ok(r.lit("PLAY"), "PLAY lit (the panel report)")
r.master(0)
r.wait(300)
ok(r.input and r.input["master"] == 0, "MASTER 0 reported")
r.master(None)
r.wait(50)
ok(r.input and r.input["master"] == 2048, "MASTER the pot's again (2048)")
r.tap("PLAY")
r.wait(100)
ok(sim_is(r, "playing", 0) and not r.lit("PLAY"), "tap PLAY: stopped, PLAY not lit")
r.shot(os.path.join(OUT, "after.png"))
ok(r.link.sim_screen(os.path.join(OUT, "after.fb")) == r.scr.px, "the screen mirrored = the simulator's, after")

# ---- let go: the tool stops (HOME held: its release seen)
r.press("HOME")
r.wait(100)
ok(r.input and r.input["buttons"] >> 6 & 1, "HOME held reported")
r.stop()
r.link.read(0.1)
ok(sim_is(r, "mirror_on", 0), "the tool stops: mirroring off")
r2 = R.Remote(r.link).start()
r2.wait(50)
ok(r2.input and not r2.input["buttons"], "on again: nothing held")
# ---- silence: off after 6 s, let go
r2.press("SEL")
r2.on = False                                   # (the keep-alive stops: the tool hung)
r2._pump(7.0)
ok(sim_is(r2, "mirror_on", 0), "silent 7 s: mirroring off")
# ---- refused while not mirroring
try:
    r2._req(1, 6, 1)
    ok(False, "a request while off: refused")
except R.RemoteError as e:
    ok(e.code == "refused", "a request while off: refused")
r.link.close()

# ---- an older Polly: mirrors (version 1), no remote


class OldLink:
    def __init__(self):
        self.q = []

    def send(self, pkt):
        f = B.parse_frame(pkt)
        if f and f[0] == R.MR_REQ:
            body = bytes([1, 1, 239, 239])
            n = len(body)
            self.q.append(b"\xF0" + R.I.pack7(bytes([0, 0x59, R.MR_REQ, n, 0, 0]) + body +
                                                bytes([~sum(body) & 0xFF])) + b"\xF7")
        return True

    def read(self, t):
        return self.q.pop(0) if self.q else None

    def drain(self):
        self.q.clear()

    def close(self):
        pass


try:
    R.Remote(OldLink()).start()
    ok(False, "an older Polly: refused by the tool")
except R.RemoteError as e:
    ok(e.code == "refused", "an older Polly: refused by the tool")

# ---- a whole scenario through the remote, its expects read from the simulator
log = io.StringIO()
rc = R.main(["--sim", "--out", os.path.join(OUT, "style_pick"), "run", str(ROOT / "tests/scenarios/style_pick.polly")],
            out=log)
ok(rc == 0 and "FAIL" not in log.getvalue(), "style_pick.polly through the remote: every expect")
if rc:
    print(log.getvalue()[-2000:])
ok(os.path.exists(os.path.join(OUT, "style_pick", "yours_ask.png")), "... its shots written")

# ---- the command line
log = io.StringIO()
rc = R.main(["--sim", "--out", OUT, "tap", "HOME", "turn", "K1", "2", "80", "wait", "100", "shot", "cli.png", "leds"],
            out=log)
ok(rc == 0 and os.path.exists(os.path.join(OUT, "cli.png")) and "leds: buttons" in log.getvalue(),
   "command line: tap, turn with a gap, wait, shot, leds")
ok(R.main(["--sim", "jump", "HOME"], out=io.StringIO()) == 2, "command line: no such action (2)")
ok(R.main(["--sim", "tap", "BOGUS"], out=io.StringIO()) == 2, "command line: no such button (2)")
ok(R.actions(["turn", "K1", "3", "tap", "SEL"]) == [["turn", "K1", "3"], ["tap", "SEL"]], "command line: turn without MS")

print("all polly_remote tests passed" if not failed else f"{failed} FAILED")
sys.exit(1 if failed else 0)
