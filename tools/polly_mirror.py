#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Save Polly's screen as a PNG, over USB-MIDI (the screen mirror: fm1-base's device/mirror.c and web/mirror).

    python3 tools/polly_mirror.py screen.png [--port NAME] [--scale N]
    python3 tools/polly_mirror.py screen.png --from STREAM.syx     (a recorded stream: the simulator's `mirror FILE`)

Asks Polly for the whole screen, waits until it has arrived, turns the mirror off again and writes the PNG (240 x 240,
or N times that). fm1-base's tools/fm1_mirror.py, for Polly (its identity). Needs mido with python-rtmidi (as
tools/fm1_install.py). Exit codes: 0 done, 1 failed, 2 bad arguments, 3 FM-1 not found, 4 no screen came, 6 not Polly.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "base", "tools"))
sys.path.insert(0, HERE)
import polly_backup as B  # noqa: E402
import fm1_mirror as M  # noqa: E402

MR_REQ, MR_RECT, MR_FILL, MR_INPUT = M.MR_REQ, M.MR_RECT, M.MR_FILL, M.MR_INPUT
W = H = M.W
mr_hash, Screen, split = M.mr_hash, M.Screen, M.split


def grab(link, quiet=0.5, limit=10.0):
    """mirror on with a refresh, until the screen stops coming (quiet s with no screen frame), then off"""
    return M.grab(link, quiet, limit, "Polly")


def main(argv=None, backend=None, out=sys.stdout):
    return M.main(argv, backend, out, B.KIND, __doc__)


if __name__ == "__main__":
    sys.exit(main())
