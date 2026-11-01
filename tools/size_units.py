#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Polly's main-loop o2 units built for size (app/fm1.toml ir_pass; fm1-base's tools/build.py runs it on each unit's
LLVM IR before its code is made): every function SIZE_UNITS define gets LLVM's minsize + optsize (about -Oz) instead of
-O2. After LOOM-8's tools/size_fns.py (Melodee's idea). First the units none of whose code runs in the render: the QY70
bank's decoder and directory (a style read when it is picked), the note editor and the chord loops (-5.7 KB, the
btaudio study). Since Bluetooth (2026-11-01) the sequencer too (arr, leads, chords, xpose: -7.4 KB), for the room
Bluetooth audio's UI needs: its arr_block runs in the audio interrupt, once a block (5.8 ms), so this costs some of the
render's time, not yet measured on the FM-1 (Status's CPU, a dense style playing, Bluetooth audio streaming). Not EAS
(EAS_Render: the voices, per sample). Functions placed in a section are left alone. The unity build (no UNIT) is
copied unchanged (unity_opt = "z" is its level).

    size_units.py IN.ll OUT.ll [UNIT]        (UNIT: an o2 unit's path relative to app/)"""
import re
import sys
from pathlib import Path

SIZE_UNITS = ["dsp/qydec.c", "dsp/qybank.c", "dsp/notes.c", "dsp/loops.c",   # (dsp/templates.c: data only)
              "dsp/arr.c", "dsp/leads.c", "dsp/chords.c", "dsp/xpose.c"]     # (the sequencer: see above)


def main(src, dst, unit=None):
    ir = Path(src).read_text()
    if unit not in SIZE_UNITS:
        Path(dst).write_text(ir)
        return 0
    groups = dict(re.findall(r"^attributes #(\d+) = \{ (.*) \}$", ir, re.M))
    nxt = max(map(int, groups)) + 1 if groups else 0
    made = {}
    n = 0

    def mark(m):
        nonlocal nxt, n
        g = m.group(1)
        if " section " in m.group(0):
            return m.group(0)
        if g not in made:
            made[g] = nxt
            nxt += 1
        n += 1
        return m.group(0)[:m.start(1) - m.start(0)] + str(made[g]) + m.group(0)[m.end(1) - m.start(0):]
    ir = re.sub(r"^define [^\n]* #(\d+) [^\n]*\{$", mark, ir, flags=re.M)
    for g, new in made.items():
        a = groups[g] if "optsize" in groups[g].split() else "optsize " + groups[g]
        ir += f"\nattributes #{new} = {{ minsize {a} }}\n"
    Path(dst).write_text(ir)
    print(f"size_units: {unit}: {n} functions built for size")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
