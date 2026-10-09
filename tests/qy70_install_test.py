#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/qy70_install.py against a simulated Polly (no hardware, no mido, no ROM: a bank image given): the
request it sends (ota.c framing, checked as the firmware checks it), the image served piece by piece as the FM-1
asks (firmware/src/bank_rx.c's order), the FM-1's result (written / an error code), a device that is not Polly,
one that stops answering. Run from the repo root:  python3 tests/qy70_install_test.py IMAGE"""
import io
import queue
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402
import qy70_install as Q  # noqa: E402

I.DELAY.update(open=0, hs=0.1, retry=0.02)
Q.DELAY.update(start=0, poll=0.05, idle=0.3)
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<64} {'ok' if cond else 'FAIL'}")
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


class FakePolly:
    """answers the handshake; on the bank request, asks for the image as bank_rx.c does (the directory in 512s,
    then the data), checks every piece, then reports `result` (0: written) or stops after `stall_after` pieces"""

    def __init__(self, image, identity="FM-1_300", result=0, stall_after=None):
        self.image, self.identity, self.result, self.stall_after = image, identity, result, stall_after
        self.req, self.got, self.bad, self.pending = None, bytearray(), 0, []

    def input_names(self):
        return ["FM-1"]

    output_names = input_names

    def open(self, i, o):
        return Link(self)

    def ask(self, link):
        if not self.pending:
            return
        addr, n = self.pending[0]
        q = bytes([0]) + addr.to_bytes(4, "little") + bytes([n & 0xFF, n >> 8, 0])
        body = bytes([0, 0x59, 0x30, 8, 0, 0]) + q + bytes([~sum(q) & 0xFF])
        link.q.put(b"\xF0" + I.pack7(body) + b"\xF7")

    def rx(self, link, d):
        if d == I.HS_QUERY:
            t = self.identity.encode()
            link.q.put(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + t + bytes(28 - len(t))) + b"\xF7")
            return
        u = I.unpack7(d[1:-1])
        if len(u) == 15 and u[:3] == bytes([0, 0x59, 0x40]):      # ota_take()'s checks, then the request
            ok(u[3] | u[4] << 8 | u[5] << 16 == 8 and (~sum(u[6:14]) & 0xFF) == u[14], "the request is framed as ota.c takes it")
            self.req = struct.unpack_from("<II", u, 6)
            n = self.req[0]
            self.pending = [(a, 512) for a in range(0, 4096, 512)]
            data, l = n - 4096, 0
            while l < data:                       # bank_rx.c's pieces: up to the guard bytes or 512
                o = l % 4088
                k = min((0xF00 if o < 0xF00 else 4088) - o, 512, data - l)
                self.pending.append((4096 + l, k))
                l += k
            self.pending.append((Q.OK_ADDR if not self.result else Q.FAIL_ADDR | self.result, 8))
            self.ask(link)
            return
        r = I.unpack7(d[1:-1])
        if self.pending and len(r) >= 15 and r[2] == 0x30:
            addr, n = self.pending.pop(0)
            got = r[14:14 + n]
            if addr < 0xB0000000:
                self.got += got
                self.bad += got != self.image[addr:addr + n]
            if self.stall_after is not None and len(self.got) >= self.stall_after:
                self.pending = []
            self.ask(link)


img = open(sys.argv[1], "rb").read()
dev = FakePolly(img)
out = io.StringIO()
rc = Q.main(["--image", sys.argv[1], "--yes"], backend=dev, out=out)
ok(rc == 0 and "done" in out.getvalue(), "written: exit 0, done")
ok(dev.req == (len(img), zlib.crc32(img)), "the request: the image's size and CRC-32")
ok(dev.bad == 0 and bytes(dev.got) == img, "the image served piece by piece, every byte as asked")
dev = FakePolly(img, result=6)
ok(Q.main(["--image", sys.argv[1], "--yes"], backend=dev, out=io.StringIO()) == 1, "the FM-1 reports an error: exit 1")
dev = FakePolly(img, identity="FM-1_906")
ok(Q.main(["--image", sys.argv[1], "--yes"], backend=dev, out=io.StringIO()) == I.EXIT["model"],
   "not Polly (X0X's identity): refused")
dev = FakePolly(img, stall_after=8192)
ok(Q.main(["--image", sys.argv[1], "--yes"], backend=dev, out=io.StringIO()) == I.EXIT["stopped"],
   "the FM-1 stops asking: exit 4")
ok(Q.main(["--image", sys.argv[1]], backend=FakePolly(img), out=io.StringIO(), ask=lambda p: False) == 1,
   "not confirmed: cancelled")
print("qy70 install test %s" % ("FAILED" if failed else "passed"))
sys.exit(1 if failed else 0)
