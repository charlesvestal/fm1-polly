#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Put the QY70's 128 preset styles on Polly, from your own QY70 ROM (bring your own ROM).

    python3 tools/qy70_install.py qy70main.bin [--yes] [--port NAME]
    python3 tools/qy70_install.py qy70main.bin --image-out FILE    # the bank image only (the simulator's `bank FILE`)
    python3 tools/qy70_install.py --image FILE [--yes]               # an image made before

The bank is built here (tools/qy70_codec.py: the styles' phrases, range-coded, with their bends and controllers;
about 235 KB) and written to the FM-1 over USB-MIDI, the way an update is: the FM-1 asks for the image piece by
piece (SysEx read requests, each checksummed), writes it into the flash Polly keeps for it, reads it back and
checks it (CRC-32), and only then writes its directory, so a cut-off write leaves no bank, never half of one
(app/bank_rx.c). It takes some 30 seconds; the FM-1 stops playing meanwhile. Nothing of Yamaha's is
in this file or in Polly: what it writes is Yamaha's music, for your own use.

Needs mido with python-rtmidi (as tools/fm1_install.py). Exit codes: 0 written, 1 cancelled or failed, 2 bad
arguments or ROM, 3 FM-1 not found, 4 connection lost or the FM-1 stopped, 6 not Polly.
"""
import argparse
import os
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(1, os.path.join(HERE, "..", "base", "tools"))   # fm1-base's (fm1_install)
try:
    import fm1_install as I  # noqa: E402
except ImportError:              # base/ is empty: a ZIP download, or a clone without its submodule
    sys.exit("base/ (fm1-base) is missing. Clone Polly with it:\n"
             "    git clone --recursive https://github.com/charlesvestal/fm1-polly\n"
             "or, in a clone made before:  git submodule update --init --recursive")

BK_REQ = 0x40
OK_ADDR, FAIL_ADDR = 0xB0000000, 0xBF000000
ERRORS = {1: "the image's size", 2: "the transfer (a read request unanswered)", 3: "the bank's directory",
          4: "a flash erase", 5: "a flash write", 6: "the image's CRC (a byte changed on the way)",
          7: "the read-back (the flash does not hold what was written)", 8: "mounting it"}
POLLY = "FM-1_3"                  # Polly's package identity (app/fm1.toml id_prefix)
DELAY = {"start": 0.0, "poll": 1.0, "idle": 30.0, "reply": 0.0}   # (the FM-1 asks for the directory at once)


def request(n, crc):
    """the host's bank request (ota.c framing): cmd 0x40, body: the image's bytes and CRC-32"""
    body = struct.pack("<II", n, crc)
    msg = bytes([0x00, 0x59, BK_REQ, len(body), 0, 0]) + body + bytes([~sum(body) & 0xFF])
    return b"\xF0" + I.pack7(msg) + b"\xF7"


def build_image(rom_path):
    import qy70_codec as c
    import qy70_import as q
    try:
        with open(rom_path, "rb") as f:
            rom = q.Rom(f.read())
    except OSError as e:
        raise I.InstallError("badpkg", f"cannot read {rom_path}: {e.strerror}")
    except q.RomError as e:
        raise I.InstallError("badpkg", f"{rom_path}: {e}")
    blob, per, stats = c.build(rom)
    img, info = c.device_image(blob)
    return img, info


def serve(link, image, progress=lambda done, total: None):
    """answer the FM-1's read requests until it reports; -> 0 written, else its error code"""
    last, served = time.monotonic(), 0
    while True:
        pkt = link.read(DELAY["poll"])
        if pkt is None:
            if link.lost:
                raise I.InstallError("lost", f"the FM-1 was disconnected after {served} requests: run this again "
                                             "(it has no QY70 styles until then)")
            if time.monotonic() - last > DELAY["idle"]:
                raise I.InstallError("stopped", f"the FM-1 stopped asking after {served} requests")
            continue
        r = I.parse_request(pkt)
        if not r:
            continue
        fl, addr, n = r
        last = time.monotonic()
        if addr == OK_ADDR or addr & 0xFF000000 == FAIL_ADDR:
            link.send(I.response(addr, b"success\0"))
            return 0 if addr == OK_ADDR else (addr & 0xFFFF) or 99
        if n > I.MAXDATA or addr > len(image):
            raise I.InstallError("badreq", f"the FM-1 asked for {addr:#x}+{n}, outside the bank")
        data = image[addr:addr + n]
        data += b"\0" * (n - len(data))
        time.sleep(DELAY["reply"])
        if not link.send(I.response(addr, data, fl)):
            raise I.InstallError("lost", "the FM-1 was disconnected")
        served += 1
        progress(min(addr + n, len(image)), len(image))


def install(backend, image, port=None, yes=False, ask=I.ask_tty, out=sys.stdout):
    up = I.Updater(backend, port)
    dev = up.find()
    if not dev:
        raise I.not_found(up)
    if not dev.id.text.startswith(POLLY):
        dev.link.close()
        raise I.InstallError("model", f"the FM-1 runs {dev.id.text}, not Polly ({POLLY}xx): install Polly first")
    print(f"device: {dev.id.text}  ({dev.name})", file=out)
    if not yes and not ask("Write the QY70 styles to it? It stops playing meanwhile. [y/N] "):
        dev.link.close()
        print("cancelled", file=out)
        return 1
    dev.link.drain()
    dev.link.send(request(len(image), zlib.crc32(image)))
    time.sleep(DELAY["start"])

    def progress(done, total):
        print(f"\rwriting {done * 100 // total:3d}%\033[K", end="", file=out, flush=True)
    try:
        code = serve(dev.link, image, progress)
    finally:
        print(file=out)
        dev.link.close()
    if code:
        print(f"error: not written: {ERRORS.get(code, 'code %d' % code)}. Run this again.", file=sys.stderr)
        return 1
    print("done: the QY70 styles are on Polly (Setup shows them; the Style knob goes on past Polly's own)", file=out)
    return 0


def main(argv=None, backend=None, out=sys.stdout, ask=I.ask_tty):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", nargs="?", help="your QY70 main ROM (MAME's qy70main.bin)")
    ap.add_argument("--image", help="a bank image made before (--image-out), instead of a ROM")
    ap.add_argument("--image-out", help="write the bank image to this file; nothing is sent")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name)")
    ap.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    a = ap.parse_args(argv)
    if bool(a.rom) == bool(a.image):
        ap.error("give a ROM or --image")
    try:
        if a.image:
            with open(a.image, "rb") as f:
                image = f.read()
        else:
            image, info = build_image(a.rom)
            print(f"bank: {info['styles']} styles, {info['bytes']} of {info['capacity']} bytes", file=out)
        if a.image_out:
            with open(a.image_out, "wb") as f:
                f.write(image)
            print(f"{a.image_out}: {len(image)} bytes (for your own use: Yamaha's music)", file=out)
            return 0
        if backend is None:
            try:
                backend = I.MidoBackend()
            except ImportError:
                raise I.InstallError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
        return install(backend, image, a.port, a.yes, ask, out)
    except I.InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        return I.EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        print("\ninterrupted: run this again (the FM-1 has no QY70 styles until it is done)", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
