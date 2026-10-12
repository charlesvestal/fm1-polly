#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/polly_remote.py against the simulator on a simulated cable (build/host/polly_host --serve: the firmware's
mirror.c and UI, the requests taken as main_fm1.c takes them). Checked: the screen as mirrored equals the simulator's;
a tap, a held button, keys, turns (exact and accelerated), MASTER go through the plat_* calls into the UI; the panel
reports' LEDs; let go when the tool stops and when it goes silent (6 s); refused while not mirroring; an older Polly
(no remote) refused by the tool; a whole scenario (tests/scenarios/style_pick.polly) run through the remote, its
expects read from the simulator; a test session (fresh): refused while playing, the unsaved saved first, a used simulator
blank (its screen below the header a fresh one's, the TEST pill), its saves kept in RAM (the flash untouched), blank
again, walkthrough.polly's every expect in one, the user's own back at its end and when mirroring stops; the command
line. Run from the repo root:  python3 tests/polly_remote_test.py"""
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


def sim_val(r, what):
    """the simulator's own value of what (its expect line: "  ok what == N ...")"""
    ln = r.link.expect(what, ">-100000")
    return int(ln.split("==")[1].split()[0])


TEAL = 4 << 11 | 41 << 5 | 19                   # K_TEAL, RGB(38, 166, 154) in RGB565: the header's TEST pill


def header_teal(r):
    return any(p == TEAL for p in r.scr.px[:24 * 240])


def sim_is(r, what, val):
    ln = r.link.expect(what, str(val))
    if not ln.startswith("  ok"):
        print("   ", ln)
    return ln.startswith("  ok")


# ---- the screen, the panel
r = R.connect(sim=True, outdir=OUT)
ok(r.version == 3 and r.on, "mirroring on, the remote's version (3: the test session)")
r.shot(os.path.join(OUT, "start.png"))
start_px = list(r.scr.px)
ok(r.link.sim_screen(os.path.join(OUT, "start.fb")) == r.scr.px, "the screen mirrored = the simulator's")
ok(sim_is(r, "page", 0), "Home at the start")
r.tap("HOME")
ok(sim_is(r, "page", 1), "tap HOME: the Song page")
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

# ---- a test session (fresh): a used simulator as a blank one, nothing written, the user's own back after
r = R.connect(sim=True, outdir=OUT)
r.spin("K1", 2)
r.spin("K2", 5)
r.wait(100)
r.tap("SAVE")
r.wait(300)
user_tempo, user_style, writes = sim_val(r, "tempo"), sim_val(r, "song_style"), sim_val(r, "store_writes")
ok(user_tempo != 105 and writes > 0 and sim_is(r, "dirty", 0), "the user's song: another style and tempo, saved")
r.shot(os.path.join(OUT, "user.png"))
ok(not header_teal(r), "no TEST pill outside a session")
r.tap("PLAY")
r.wait(200)
try:
    r.fresh()
    ok(False, "fresh while playing: refused")
except R.RemoteError as e:
    ok(e.code == "refused" and sim_is(r, "test_session", 0), "fresh while playing: refused")
r.tap("PLAY")
r.wait(200)
r.spin("K2", 1)                                 # (an unsaved change: saved first, to the flash)
r.fresh()
user_tempo += 1
ok(sim_is(r, "test_session", 1) and sim_is(r, "tempo", 105) and sim_is(r, "page", 0) and sim_is(r, "dirty", 0),
   "fresh: a blank FM-1 (tempo 105, Home, nothing unsaved)")
ok(sim_val(r, "store_writes") > writes, "fresh: the user's unsaved change saved first")
writes = sim_val(r, "store_writes")
r.shot(os.path.join(OUT, "fresh.png"))
ok(header_teal(r), "fresh: the TEST pill in the header")
ok(r.scr.px[24 * 240:] == start_px[24 * 240:], "fresh: the screen below the header = a fresh simulator's")
r.spin("K2", 3)
r.wait(100)
r.tap("SAVE")
r.wait(300)
ok(sim_is(r, "tempo", 108) and sim_is(r, "dirty", 0) and sim_val(r, "test_bytes") > 0,
   "in the session: SAVE saved (into RAM)")
ok(sim_val(r, "store_writes") == writes, "in the session: nothing written to the flash")
r.fresh()
ok(sim_is(r, "tempo", 105) and sim_is(r, "test_bytes", 0), "fresh again: blank again")
log = io.StringIO()
os.makedirs(os.path.join(OUT, "walk"))
skipped = r.run(str(ROOT / "tests/scenarios/walkthrough.polly"), os.path.join(OUT, "walk"), lambda t: print(t, file=log),
                fresh=True)
ok("FAIL" not in log.getvalue() and "test session ended" in log.getvalue(),
   "walkthrough.polly in a session (fresh first): every expect")
if "FAIL" in log.getvalue():
    print(log.getvalue()[-2000:])
ok(sim_val(r, "store_writes") == writes, "... nothing written to the flash (its SAVE, its autosaves: RAM)")
ok(sim_is(r, "test_session", 0) and sim_is(r, "tempo", user_tempo) and sim_is(r, "song_style", user_style) and
   sim_is(r, "rows", 0), "fresh_end: the user's own again (style, tempo, no walkthrough rows)")
ok(not header_teal(r), "fresh_end: no TEST pill")
r.fresh()
r.stop()
r.link.read(0.1)
ok(sim_is(r, "test_session", 0) and sim_is(r, "tempo", user_tempo), "mirroring off: the session ended, the user's own")
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
ok(R.main(["--sim", "--fresh", "tap", "HOME"], out=io.StringIO()) == 2, "command line: --fresh without run (2)")
log = io.StringIO()
rc = R.main(["--sim", "--out", OUT, "fresh", "shot", "cli_fresh.png", "fresh_end"], out=log)
ok(rc == 0 and "test session ended" in log.getvalue(), "command line: fresh, fresh_end")
ok(R.actions(["turn", "K1", "3", "tap", "SEL"]) == [["turn", "K1", "3"], ["tap", "SEL"]], "command line: turn without MS")

print("all polly_remote tests passed" if not failed else f"{failed} FAILED")
sys.exit(1 if failed else 0)
