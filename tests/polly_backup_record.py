#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Record what tools/polly_backup.py says to a simulated Polly, and what Polly answers, for the web backup page's test
(tests/host/backup_page_test.mjs replays Polly's side and checks that the page sends the same bytes). The simulated
Polly is tests/polly_backup_test.py's (backup.c's frames, restore_rx()'s reads), on the device's own image (made by
tests/backup_test.c). Recorded: a backup, a restore answered SEL, a restore while playing.

    python3 tests/polly_backup_record.py IMAGE OUT.json
"""
import io
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
src = (ROOT / "tests" / "polly_backup_test.py").read_text()
cut = src.find("\nimg = open(sys.argv[1]")                 # its definitions (FakePolly), not its checks
if cut < 0:
    sys.exit("tests/polly_backup_test.py changed: no 'img = open(sys.argv[1]' line to stop at")
ns = {"__name__": "polly_backup_sim", "__file__": str(ROOT / "tests" / "polly_backup_test.py")}
exec(compile(src[:cut], "polly_backup_test.py", "exec"), ns)
B, FakePolly, Link = ns["B"], ns["FakePolly"], ns["Link"]


class Recorder(FakePolly):
    """FakePolly whose links note every message each way, in order"""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.log = []

    def open(self, i, o):
        link = Link(self)
        put, send, log = link.q.put, link.send, self.log
        link.q.put = lambda pkt: (log.append(["dev", bytes(pkt).hex()]), put(pkt))[1]

        def rec_send(pkt):
            log.append(["host", bytes(pkt).hex()])
            return send(pkt)
        link.send = rec_send
        return link


def main(image_path, out_path):
    img = open(image_path, "rb").read()
    flags, objs = B.parse(img)
    other = B.build([(o, d[::-1] if d else b"x" * 10) for o, d in objs])
    tmp = Path(out_path).with_suffix(".plb")
    runs = {}

    dev = Recorder(img)
    assert B.main(["backup", str(tmp)], backend=dev, out=io.StringIO()) == 0 and tmp.read_bytes() == img
    runs["backup"] = dev.log

    dev = Recorder(other)
    assert B.main(["restore", image_path, "--yes"], backend=dev, out=io.StringIO()) == 0 and dev.image == img
    runs["restore"] = dev.log

    dev = Recorder(other, playing=True)
    assert B.main(["restore", image_path, "--yes"], backend=dev, out=io.StringIO()) == B.EXIT["playing"]
    runs["restore_playing"] = dev.log

    tmp.unlink()
    json.dump({"image": img.hex(), "other": other.hex(), "runs": runs}, open(out_path, "w"))
    print(f"{out_path}: " + ", ".join(f"{k} {len(v)} messages" for k, v in runs.items()))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
