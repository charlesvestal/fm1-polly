#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Back up Polly's songs to a file, and put them back, over USB-MIDI.

    python3 tools/polly_backup.py backup FILE [--port NAME]
    python3 tools/polly_backup.py restore FILE [--yes] [--port NAME]
    python3 tools/polly_backup.py info FILE

A backup holds everything Polly keeps: the project (Setup, the channels' sounds), the four songs (with their
progressions), and the 16 places of memory (grooves, melodies; a song's progressions only when they did not fit with
the song); not the QY70 styles (tools/qy70_install.py puts them back) and
not the panel's calibration. Polly can play on while it is backed up (what is not saved yet is left out then: stop
first to have it).

A restore replaces all of it. Polly must be stopped ("STOP FIRST" otherwise). It receives the whole backup and
checks it (every part's CRC-32, the whole file's) before anything changes, then asks on its screen: RESTORE FROM
USB? SEL yes, HOME no. A cut cable, a damaged file or HOME changes nothing (fm1-base's device/backup.c).

fm1-base's tools/fm1_backup.py with Polly's backups (magic PLYB, version 2, its 18 parts) and Polly's identity.
Needs mido with python-rtmidi (as tools/fm1_install.py). Exit codes: 0 done, 1 cancelled or failed, 2 bad arguments
or file, 3 FM-1 not found, 4 connection lost or the FM-1 stopped answering, 5 playing (stop it first), 6 not Polly.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "base", "tools"))
sys.path.insert(0, HERE)
import fm1_install as I  # noqa: E402
import fm1_backup as F  # noqa: E402

RS_REQ, BU_REQ, BU_END = F.RS_REQ, F.BU_REQ, F.BU_END
OK_ADDR, ASK_ADDR, FAIL_ADDR = F.OK_ADDR, F.ASK_ADDR, F.FAIL_ADDR
MAGIC, VERSION, NOBJ, HEAD, REC, PAYLOAD_MAX = b"PLYB", 2, 18, F.HEAD, F.REC, F.PAYLOAD_MAX   # 2: songs hold progressions
OBJ_IDS = [0, 2] + list(range(3, 19))             # app/app.h: OBJ_PROJ, OBJ_SONGS, OBJ_PAT0 + 0..15
PLAYING, DECLINED = F.PLAYING, F.DECLINED
POLLY = "FM-1_3"                  # Polly's package identity (app/fm1.toml id_prefix)
DELAY = F.DELAY                   # (the same dict: a test's changes reach fm1_backup)
EXIT = F.EXIT
BackupError = F.BackupError
frame, parse_frame = F.frame, F.parse_frame


def name_of(obj):
    return {0: "project", 2: "songs"}.get(obj, f"slot {obj - 2:2d}")


class PollyKind(F.Kind):
    def older(self, ver):
        if ver < VERSION:
            return (f"a backup from an older Polly (version {ver}: its songs kept their progressions elsewhere); "
                    f"this Polly cannot restore it")
        return f"a backup of another version ({ver})"

    def summary(self, objs):
        parts = [name_of(o) for o, d in objs if d and o < 3]
        slots = sum(1 for o, d in objs if d and o >= 3)
        return f"{sum(len(d) for _, d in objs)} bytes: " + ", ".join(parts + [f"{slots} of 16 places of memory used"])


KIND = PollyKind(MAGIC, VERSION, OBJ_IDS, name_of, POLLY, "Polly")
ERRORS = F.errors(KIND)


def parse(image):
    """check a backup file whole -> (flags, [(obj, payload)]); BackupError if it is not one or is damaged"""
    return F.parse(image, KIND)


def build(objs, flags=0):
    """a backup file from [(obj, payload)] in OBJ_IDS order (the tests' simulated Polly)"""
    return F.build(objs, flags, kind=KIND)


def summary(objs):
    return KIND.summary(objs)


def find(backend, port):
    return F.find(backend, port, KIND)


def backup(backend, path, port=None, out=sys.stdout):
    return F.backup(backend, path, port, out, KIND)


def restore(backend, path, port=None, yes=False, ask=I.ask_tty, out=sys.stdout):
    return F.restore(backend, path, port, yes, ask, out, KIND)


def main(argv=None, backend=None, out=sys.stdout, ask=I.ask_tty):
    return F.main(argv, backend, out, ask, KIND, __doc__)


if __name__ == "__main__":
    sys.exit(main())
