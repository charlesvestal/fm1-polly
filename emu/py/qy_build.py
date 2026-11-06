#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The page's bank builder: the tester's QY70 ROM -> the bank image the FM-1 stores (qybank.h), by the codec's own
--image path (tools/qy70_codec.py, unchanged). The page runs this in Pyodide (web/emu/qybank_worker.js) with the
tools beside it; tests/host/emu_bank_test.mjs runs it in the host's Python. No Yamaha data in this file.

    python3 web/emu/qy_build.py qy70main.bin bank.img
"""
import io
import os
import sys
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../tools"))


def build(rom_path, image_path):
    """(ok, message): the codec's report ("... 128 styles, 254406 of 257544 bytes"), or why the ROM was refused"""
    import qy70_codec as c
    import qy70_import as q
    out = io.StringIO()
    try:
        with redirect_stdout(out):
            c.main([rom_path, "--image", image_path])
    except q.RomError as e:
        m = str(e)
        return False, m[:1].upper() + m[1:]
    except Exception as e:  # a damaged or different ROM the checks let through
        return False, "This ROM could not be read (%s: %s). Is it the QY70's main ROM, qy70main.bin?" % (
            type(e).__name__, e)
    except SystemExit as e:
        return False, "The bank builder stopped (%s)." % e.code
    return True, out.getvalue().strip()


if __name__ == "__main__":
    ok, msg = build(sys.argv[1], sys.argv[2])
    print(msg)
    sys.exit(0 if ok else 1)
