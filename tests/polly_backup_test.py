#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/polly_backup.py (fm1-base's tools/fm1_backup.py with Polly's kind) against fm1-base's simulated FM-1
(base/tests/backup_tool_test.py FakeFM1) holding a Polly set: what is Polly's own. fm1_backup.py itself is tested in
fm1-base (base/tests/run_tests.sh: backup_tool_test.py on the device's own image). Checked: the backup asked with
Polly's magic (PLYB) and no --magic, written byte for byte; info in Polly's words; a restore taken only on SEL;
playing (STOP FIRST, exit 5); an older Polly's backup (version 1) and another firmware's refused here (nothing sent);
not Polly (X0X's identity) refused. Run from the repo root:  python3 tests/polly_backup_test.py"""
import io
import os
import shutil
import struct
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "base" / "tests"))
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(1, str(ROOT / "base" / "tools"))   # fm1-base's (fm1_install)
import fm1_install as I  # noqa: E402
import polly_backup as B  # noqa: E402
import backup_tool_test as T  # noqa: E402

T.fast()
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<66} {'ok' if cond else 'FAIL'}")
    failed += not cond


def polly(seed):
    """a Polly set: the project, the songs, 5 of the 16 slots"""
    objs = []
    for o in B.OBJ_IDS:
        n = 0 if o >= 3 and (o + seed) % 3 else 40 + (o * 97 + seed * 31) % 3000
        objs.append((o, bytes((i * 7 + o + seed) & 255 for i in range(n))))
    return B.build(objs)


img, other = polly(1), polly(2)
flags, objs = B.parse(img)
ok(img[:4] == b"PLYB" and struct.unpack_from("<HH", img, 4) == (2, 18) and len(objs) == 18,
   "Polly's backups: PLYB, version 2, 18 parts")
tmp = tempfile.mkdtemp()
path = os.path.join(tmp, "polly.plb")
dev = lambda image, **k: T.FakeFM1(image, identity=k.pop("identity", "FM-1_300"), port="Polly FM-1", **k)  # noqa

d = dev(img)
out = io.StringIO()
ok(B.main(["backup", path], backend=d, out=out) == 0 and open(path, "rb").read() == img and d.backups == 1,
   "backup: Polly's magic asked (no --magic), the file byte for byte")
out = io.StringIO()
ok(B.main(["info", path], out=out) == 0 and "a Polly backup" in out.getvalue() and "places of memory used" in
   out.getvalue() and "songs" in out.getvalue(), "info: in Polly's words")
d = dev(other)
out = io.StringIO()
ok(B.main(["restore", path, "--yes"], backend=d, out=out) == 0 and d.image == img and d.asked == 1,
   "restore: exit 0, Polly holds the backup")
d = dev(other, playing=True)
ok(B.main(["restore", path, "--yes"], backend=d, out=io.StringIO()) == B.EXIT["playing"] == 5 and d.image == other,
   "playing: STOP FIRST (exit 5), nothing changed")
old = bytearray(img)                              # version 1 (the songs' progressions in the slots then), CRCs right
struct.pack_into("<H", old, 4, 1)
struct.pack_into("<I", old, 12, zlib.crc32(old[:12]))
struct.pack_into("<I", old, len(old) - 4, zlib.crc32(old[:-4]))
open(path + ".v1", "wb").write(old)
d = dev(other)
ok(B.main(["restore", path + ".v1", "--yes"], backend=d, out=io.StringIO()) == 2 and d.req is None and
   d.image == other, "an older Polly's backup (version 1): refused here (exit 2), nothing sent")
try:
    B.parse(bytes(old))
    said = ""
except B.BackupError as e:
    said = str(e)
ok("older Polly" in said, "... said so: " + said[:40])
tst = bytearray(img)                              # another firmware's (its magic), CRCs right
tst[:4] = b"TSTB"
struct.pack_into("<I", tst, 12, zlib.crc32(tst[:12]))
struct.pack_into("<I", tst, len(tst) - 4, zlib.crc32(tst[:-4]))
open(path + ".tst", "wb").write(tst)
d = dev(other)
ok(B.main(["restore", path + ".tst", "--yes"], backend=d, out=io.StringIO()) == 2 and d.req is None,
   "another firmware's backup: not a Polly backup (exit 2), nothing sent")
d = dev(other, identity="FM-1_906")
ok(B.main(["restore", path, "--yes"], backend=d, out=io.StringIO()) == I.EXIT["model"] and d.image == other,
   "not Polly (X0X's identity): refused")
shutil.rmtree(tmp)
print("polly backup test %s" % ("FAILED" if failed else "passed"))
sys.exit(1 if failed else 0)
