#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Polly's resource region (fm1-base's device/res.c; fm1.toml resources): what the firmware keeps out of its image,
written by the installer after an update and read in place. Laid out as LOOM-8's (tools/gen_res.py there): sections by
tag, so another can join later. Its sections:
  STY1  the factory styles' patterns and notes (tools/make_styles.py's tables from styles/*.sty: app/dsp/arr.c reads
        them): u16 styles, u16 sections a style, u32 notes, then STY_PAT (sty_pat_t, 116 bytes each), STY_EV (u32
        each), STY_VEL (u8 each), as styles.h has them
The payload (little-endian): u32 magic "PRS1", u16 count, u16 0; count entries of u32 tag, u32 offset (from the
payload's start), u32 length; the sections, each 4-aligned. Deterministic: the same bytes every run.

  gen_res.py OUT.bin            (from the repository's styles/*.sty)
  gen_res.py --check OUT.bin    exit 1 unless OUT.bin is what this would write
"""
import glob
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import make_styles as M  # noqa: E402

MAGIC = 0x31535250                                # "PRS1"


def tag(s): return struct.unpack("<I", s.encode())[0]


def styles_section():
    files = M.ordered(glob.glob(str(ROOT / "styles" / "*.sty")))
    styles = [M.parse(f) for f in files]
    errs = [e for s in styles for e in M.check(s)]
    if errs:
        raise SystemExit("gen_res: " + "; ".join(errs))
    pats, ev, vel = M.tables(styles)
    out = struct.pack("<HHI", len(styles), M.NSECT, len(ev))
    for bars, beats, first, trks in pats:          # sty_pat_t: bars, beats, first; 8 x sty_trk_t (12 bytes, count)
        out += struct.pack("<BBH", bars, beats, first)
        for t in trks:
            out += struct.pack("<12BH", *t[:11], 0, t[11])
    out += struct.pack(f"<{len(ev)}I", *ev) + bytes(vel)
    return out


def payload():
    sections = [("STY1", styles_section())]
    head = 8 + 12 * len(sections)
    out, at, table = b"", head, b""
    for t, b in sections:
        n = len(b)
        b += bytes(-len(b) % 4)
        table += struct.pack("<III", tag(t), at, n)
        out += b
        at += len(b)
    return struct.pack("<IHH", MAGIC, len(sections), 0) + table + out


def main(argv):
    check = argv[1:2] == ["--check"]
    args = argv[2:] if check else argv[1:]
    if len(args) != 1:
        raise SystemExit(__doc__)
    p = payload()
    if check:
        same = Path(args[0]).is_file() and Path(args[0]).read_bytes() == p
        print(f"gen_res: {args[0]} {'up to date' if same else 'differs'}")
        return 0 if same else 1
    Path(args[0]).parent.mkdir(parents=True, exist_ok=True)
    Path(args[0]).write_bytes(p)
    print(f"  ok    the resource region: {len(p)} B (the factory styles)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
