#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/polly_backup.py against a simulated Polly (no hardware, no mido). Its stored set is the image the device's
code made in tests/backup_test.c (firmware/src/backup.c's format, read by the tool's parser); it streams a backup in
backup.c's frames (128 bytes, then the end frame) and pulls a restore as restore_rx() does (the head, each record,
its payload in 512s, the tail, then the question and the result). Checked: the backup written byte for byte; a
restore sent whole and taken only on SEL; playing (STOP FIRST), HOME, a damaged file (nothing sent), not Polly, a
Polly that stops asking. Run from the repo root:  python3 tests/polly_backup_test.py IMAGE"""
import io
import os
import queue
import shutil
import struct
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402
import polly_backup as B  # noqa: E402

I.DELAY.update(open=0, hs=0.1, retry=0.02)
B.DELAY.update(poll=0.02, idle=0.3, ask=0.5, first=0.3)
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<66} {'ok' if cond else 'FAIL'}")
    failed += not cond


class Link:
    def __init__(self, dev):
        self.dev, self.q, self.lost = dev, queue.Queue(), False

    def send(self, pkt):
        self.dev.rx(self, bytes(pkt))
        return True

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        pass


def dev_frame(cmd, body):
    """backup.c's bu_frame"""
    n = len(body)
    return b"\xF0" + I.pack7(bytes([0, 0x59, cmd, n & 0xFF, n >> 8, 0]) + body + bytes([~sum(body) & 0xFF])) + b"\xF7"


class FakePolly:
    def __init__(self, image, identity="FM-1_300", playing=False, answer=True, stall_after=None, flags=0):
        self.image, self.identity, self.playing, self.answer = image, identity, playing, answer
        self.stall_after, self.flags = stall_after, flags
        self.req, self.asked, self.pull, self.wanted, self.reads = None, 0, None, None, 0

    def input_names(self):
        return ["Polly FM-1"]

    output_names = input_names

    def open(self, i, o):
        return Link(self)

    # ---- backup: the stored image in 128-byte frames, then the end frame (bu_step)
    def stream(self, link):
        img = bytearray(self.image)
        if self.flags:                            # (playing: flagged in the head)
            img[8] = self.flags
            img[12:16] = struct.pack("<I", zlib.crc32(img[:12]))
            img[-4:] = struct.pack("<I", zlib.crc32(img[:-4]))
        for off in range(0, len(img), 128):
            link.q.put(dev_frame(B.BU_REQ, struct.pack("<I", off) + bytes(img[off:off + 128])))
        link.q.put(dev_frame(B.BU_END, struct.pack("<IIBB", len(img), zlib.crc32(img), 0, self.flags)))

    # ---- restore: restore_rx()'s reads
    def puller(self, n, crc):
        if self.playing:
            yield (B.FAIL_ADDR | B.PLAYING, 8)
            return
        got = bytearray((yield (0, 16)))
        pos = 16
        for _ in range(18):
            r = yield (pos, 8)
            got += r
            ln = struct.unpack_from("<H", r, 2)[0]
            pos += 8
            for k in range(0, ln, 512):
                got += (yield (pos + k, min(512, ln - k)))
            pos += ln
        got += (yield (pos, 4))
        if len(got) != n or zlib.crc32(got) != crc:
            yield (B.FAIL_ADDR | 6, 8)
            return
        try:
            B.parse(bytes(got))
        except B.BackupError:
            yield (B.FAIL_ADDR | 5, 8)
            return
        yield (B.ASK_ADDR, 8)
        self.asked += 1
        if not self.answer:
            yield (B.FAIL_ADDR | B.DECLINED, 8)
            return
        self.image = bytes(got)
        yield (B.OK_ADDR, 8)

    def ask(self, link, addr, n):
        q = bytes([0]) + addr.to_bytes(4, "little") + bytes([n & 0xFF, n >> 8, 0])
        link.q.put(b"\xF0" + I.pack7(bytes([0, 0x59, 0x30, 8, 0, 0]) + q + bytes([~sum(q) & 0xFF])) + b"\xF7")
        self.wanted = (addr, n)

    def next(self, link, data):
        try:
            addr, n = self.pull.send(data)
        except StopIteration:
            self.pull = None
            return
        self.ask(link, addr, n)

    def rx(self, link, d):
        if d == I.HS_QUERY:
            t = self.identity.encode()
            link.q.put(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + t + bytes(28 - len(t))) + b"\xF7")
            return
        f = B.parse_frame(d)
        if f and f[0] == B.BU_REQ and f[1] == b"PLYB":
            self.stream(link)
            return
        if f and f[0] == B.RS_REQ and len(f[1]) == 8:   # ota_take()'s checks passed; the request
            self.req = struct.unpack("<II", f[1])
            self.pull = self.puller(*self.req)
            self.next(link, None)
            return
        u = I.unpack7(d[1:-1])
        if self.pull and self.wanted and len(u) >= 15 and u[2] == 0x30:
            addr, n = self.wanted
            if int.from_bytes(u[7:11], "little") != addr or u[11] | u[12] << 8 != n:
                return
            self.reads += 1
            if self.stall_after is not None and self.reads > self.stall_after:
                return                            # (stops asking)
            self.next(link, bytes(u[14:14 + n]))


img = open(sys.argv[1], "rb").read()
flags, objs = B.parse(img)
ok(len(objs) == 18 and objs[0][0] == 0 and objs[1][0] == 2, "the device's image (backup_test) parses: 18 parts")
tmp = tempfile.mkdtemp()
path = os.path.join(tmp, "polly.plb")

dev = FakePolly(img)
out = io.StringIO()
rc = B.main(["backup", path], backend=dev, out=out)
ok(rc == 0 and open(path, "rb").read() == img and "done" in out.getvalue(), "backup: exit 0, the file byte for byte")
out = io.StringIO()
ok(B.main(["info", path], out=out) == 0 and "slots used" in out.getvalue(), "info: the file's parts")
out = io.StringIO()
dev = FakePolly(img, flags=1)
rc = B.main(["backup", path + "2"], backend=dev, out=out)
ok(rc == 0 and "playing" in out.getvalue(), "backup while playing: written, the note said")

other = B.build([(o, d[::-1] if d else b"x" * 10) for o, d in objs])   # another set on the device
dev = FakePolly(other)
out = io.StringIO()
rc = B.main(["restore", path, "--yes"], backend=dev, out=out)
ok(rc == 0 and dev.image == img and "done" in out.getvalue(), "restore: exit 0, the device holds the backup")
ok(dev.req == (len(img), zlib.crc32(img)) and dev.asked == 1, "... the request: its size and CRC-32; asked once")
ok("SEL yes" in out.getvalue(), "... the host says to answer on the FM-1")

dev = FakePolly(other, playing=True)
ok(B.main(["restore", path, "--yes"], backend=dev, out=io.StringIO()) == B.EXIT["playing"] and dev.image == other,
   "playing: STOP FIRST (exit 5), nothing changed")
dev = FakePolly(other, answer=False)
ok(B.main(["restore", path, "--yes"], backend=dev, out=io.StringIO()) == 1 and dev.image == other and dev.asked == 1,
   "HOME on the FM-1: exit 1, nothing changed")
bad = bytearray(img)
bad[len(bad) // 2] ^= 1
open(path + ".bad", "wb").write(bad)
dev = FakePolly(other)
ok(B.main(["restore", path + ".bad", "--yes"], backend=dev, out=io.StringIO()) == 2 and dev.req is None,
   "a damaged file: refused here (exit 2), nothing sent")
dev = FakePolly(other, identity="FM-1_906")
ok(B.main(["restore", path, "--yes"], backend=dev, out=io.StringIO()) == I.EXIT["model"] and dev.image == other,
   "not Polly (X0X's identity): refused")
dev = FakePolly(other, stall_after=5)
ok(B.main(["restore", path, "--yes"], backend=dev, out=io.StringIO()) == I.EXIT["stopped"] and dev.image == other,
   "the FM-1 stops asking: exit 4, nothing changed")
dev = FakePolly(other)
ok(B.main(["restore", path], backend=dev, out=io.StringIO(), ask=lambda p: False) == 1 and dev.req is None,
   "not confirmed here: cancelled, nothing sent")
shutil.rmtree(tmp)
print("polly backup test %s" % ("FAILED" if failed else "passed"))
sys.exit(1 if failed else 0)
