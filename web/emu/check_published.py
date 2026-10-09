#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Nothing of Yamaha's goes on the site: the published emulator directory holds exactly the files listed below
(the page, the wasm, the worklet, the bank builder's worker and its Python: GPL code), and, given your ROM, none
of them contains the ROM's style names or any 48 bytes of the ROM or of the bank image built from it.

    python3 web/emu/check_published.py DIR [qy70main.bin]
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ALLOWED = {"index.html", "worklet.js", "polly.wasm", "qybank_worker.js",
           "py/qy_build.py", "py/qy70_import.py", "py/qy70_bank.py", "py/qy70_codec.py"}
CHUNK = 32


def files(d):
    out = {}
    for root, _, names in os.walk(d):
        for n in names:
            p = os.path.join(root, n)
            out[os.path.relpath(p, d).replace(os.sep, "/")] = p
    return out


def pieces(blob, step=16):
    """{CHUNK-byte piece: offset} of blob every step bytes, filler left out (few distinct bytes): any copy of
    CHUNK + step bytes or more of blob contains one of them"""
    out = {}
    for o in range(0, len(blob) - CHUNK, step):
        c = blob[o:o + CHUNK]
        if len(set(c)) >= 12:
            out.setdefault(c, o)
    return out


def scan(data, table):
    """the first piece of table in data (every offset), or None"""
    for i in range(len(data) - CHUNK + 1):
        o = table.get(data[i:i + CHUNK])
        if o is not None:
            return i, o
    return None


def main(argv):
    d = argv[0]
    fs = files(d)
    bad = sorted(set(fs) - ALLOWED)
    missing = sorted(ALLOWED - set(fs))
    if bad:
        print("not allowed on the site: " + ", ".join(bad))
    if missing:
        print("missing: " + ", ".join(missing))
    if bad or missing:
        return 1
    print("files: %d, all on the list (%s)" % (len(fs), ", ".join(sorted(fs))))
    if len(argv) < 2:
        print("(no ROM given: the content checks skipped)")
        return 0
    sys.path.insert(0, HERE)
    import qy_build
    import qy70_import as q
    rom_bytes = open(argv[1], "rb").read()
    rom = q.Rom(rom_bytes)
    names = sorted({st["name"].strip() for st in rom.styles() if len(st["name"].strip()) >= 4})
    # a genre word Polly's own code uses too ("Bossa", "House"; GM's "Tango Accordion") is not the ROM's
    own = b""
    for sub in ("firmware/src", "styles"):
        for root, _, ns in os.walk(os.path.join(HERE, "../..", sub)):
            for n in ns:
                if n.endswith((".c", ".h", ".sty")):
                    own += open(os.path.join(root, n), "rb").read()
    shared = [n for n in names if n.encode() in own]
    names = [n for n in names if n.encode() not in own]
    with tempfile.TemporaryDirectory() as t:
        ok, msg = qy_build.build(argv[1], os.path.join(t, "bank.img"))
        assert ok, msg
        img = open(os.path.join(t, "bank.img"), "rb").read()
    tables = (("the bank image", pieces(img)), ("the ROM", pieces(rom_bytes)))
    found = 0
    for rel, p in fs.items():
        data = open(p, "rb").read()
        for n in names:
            if n.encode() in data:
                print("%s: the ROM's style name %r" % (rel, n))
                found += 1
        for what, table in tables:
            hit = scan(data, table)
            if hit:
                print("%s: %d bytes of %s (its offset %#x) at %#x" % (rel, CHUNK, what, hit[1], hit[0]))
                found += 1
    print("ROM content: %s (%d style names searched, %d shared with Polly's own sources skipped: %s; every %d-byte piece of "
          "the %d B bank image and the %d B ROM)" % ("FOUND" if found else "none", len(names), len(shared), ", ".join(shared), CHUNK + 16, len(img),
                                    len(rom_bytes)))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
