#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Save Polly's screen as a PNG, over USB-MIDI (the screen mirror: firmware/src/mirror.c, web/mirror/index.html).

    python3 tools/polly_mirror.py screen.png [--port NAME] [--scale N]
    python3 tools/polly_mirror.py screen.png --from STREAM.syx     (a recorded stream: the simulator's `mirror FILE`)

Asks Polly for the whole screen, waits until it has arrived, turns the mirror off again and writes the PNG (240 x 240,
or N times that). Needs mido with python-rtmidi (as tools/fm1_install.py). Exit codes: 0 done, 1 failed, 2 bad
arguments, 3 FM-1 not found, 4 no screen came, 6 not Polly.
"""
import argparse
import os
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fm1_install as I  # noqa: E402
import polly_backup as B  # noqa: E402

MR_REQ, MR_RECT, MR_FILL, MR_INPUT = 0x50, 0x51, 0x52, 0x53
W = H = 240


def mr_hash(c):
    return (c * 40503 & 0xFFFFFFFF) >> 10 & 63


class Screen:
    """the viewer's decoder (web/mirror/index.html createMirror), RGB565 pixels"""

    def __init__(self):
        self.px = [0] * (W * H)
        self.frames = self.bad = 0

    def rect(self, b):
        x, y, w, h = b[0], b[1], b[2], b[3]
        total, p, i, cur, cache = w * h, 0, 5, 0, [0] * 64
        if not w or not h or x + w > W or y + h > H:
            return False

        def put(c, n):
            nonlocal p
            if p + n > total:
                return False
            for _ in range(n):
                self.px[(y + p // w) * W + x + p % w] = c
                p += 1
            return True

        while i < len(b):
            op = b[i]
            i += 1
            if op < 0x80:
                ok = put(cur, op + 1)
            elif op < 0xC0:
                cur = cache[op & 63]
                ok = put(cur, 1)
            else:
                if i + 2 > len(b):
                    return False
                cur = b[i] << 8 | b[i + 1]
                i += 2
                cache[mr_hash(cur)] = cur
                ok = put(cur, (op & 63) + 1)
            if not ok:
                return False
        return p == total

    def frame(self, pkt):
        """one F0..F7 message; its command, or None (not Polly's mirror)"""
        f = B.parse_frame(pkt)
        if not f:
            return None
        cmd, b = f
        if cmd == MR_RECT:
            self.frames += 1
            if not self.rect(b):
                self.bad += 1
        elif cmd == MR_FILL and len(b) >= 7:
            self.frames += 1
            c = b[5] << 8 | b[6]
            for j in range(b[1], min(H, b[1] + b[3])):
                self.px[j * W + b[0]:j * W + min(W, b[0] + b[2])] = [c] * (min(W, b[0] + b[2]) - b[0])
        return cmd

    def png(self, path, scale=1):
        rows = []
        for y in range(H):
            line = bytearray()
            for x in range(W):
                v = self.px[y * W + x]
                rgb = bytes(((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31))
                line += rgb * scale
            rows += [b"\0" + bytes(line)] * scale

        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
        data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W * scale, H * scale, 8, 2, 0, 0, 0))
        data += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
        with open(path, "wb") as f:
            f.write(data)


def split(stream):
    """a recorded byte stream -> its F0..F7 messages"""
    out, start = [], None
    for i, v in enumerate(stream):
        if v == 0xF0:
            start = i
        elif v == 0xF7 and start is not None:
            out.append(bytes(stream[start:i + 1]))
            start = None
    return out


def grab(link, quiet=0.5, limit=10.0):
    """mirror on with a refresh, until the screen stops coming (quiet s with no screen frame), then off"""
    scr, got = Screen(), False
    link.drain()
    link.send(B.frame(MR_REQ, b"\x02"))
    t0 = last = time.monotonic()
    try:
        while time.monotonic() - t0 < limit:
            pkt = link.read(0.1)
            now = time.monotonic()
            if pkt is not None and scr.frame(pkt) in (MR_RECT, MR_FILL):
                got, last = True, now
            if got and now - last > quiet:
                break
            if not got and now - t0 > 3.0:
                raise I.InstallError("stopped", "no screen came (an older Polly, without the mirror?)")
            if link.lost:
                raise I.InstallError("lost", "the FM-1 was disconnected")
    finally:
        link.send(B.frame(MR_REQ, b"\x00"))
    return scr


def main(argv=None, backend=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png", help="the PNG to write")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name; default: Polly's)")
    ap.add_argument("--scale", type=int, default=1, choices=range(1, 9), metavar="N", help="pixels per pixel (1..8)")
    ap.add_argument("--from", dest="src", metavar="STREAM", help="decode a recorded SysEx stream instead of the FM-1")
    a = ap.parse_args(argv)
    try:
        if a.src:
            scr = Screen()
            with open(a.src, "rb") as f:
                for pkt in split(f.read()):
                    scr.frame(pkt)
        else:
            if backend is None:
                try:
                    backend = I.MidoBackend()
                except ImportError:
                    raise I.InstallError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
            dev = B.find(backend, a.port)
            try:
                scr = grab(dev.link)
            finally:
                dev.link.close()
        if scr.bad or not scr.frames:
            print(f"error: {scr.bad} damaged frames of {scr.frames}", file=sys.stderr)
            return 4
        scr.png(a.png, a.scale)
        print(f"{a.png}: {W * a.scale} x {H * a.scale}, from {scr.frames} frames", file=out)
        return 0
    except OSError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except I.InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        return B.EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
