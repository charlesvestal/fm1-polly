#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Publish the emulator, an internal test build, at charles.pizza/qy/ (unlisted: noindex in the page, Disallow in
# the site's robots.txt, no links to it): build it, test it, check that nothing of Yamaha's is in it, copy app/build/emu
# into the site's repo (a sibling checkout, ../charles.pizza from fm1-parent, or $SITE), commit and push (it
# deploys on push). The tester loads their own QY70 ROM in the page; no ROM, bank or ROM-derived file goes up.
#   [QY70_ROM=qy70main.bin] web/emu/publish.sh ["message"]
# With QY70_ROM the bank test runs and the published files are searched for the ROM's content too.
set -e
cd "$(dirname "$0")/../.."
SITE="${SITE:-$(cd .. && pwd)/../charles.pizza}"
DEST=qy
[ -d "$SITE/.git" ] || { echo "no site checkout at $SITE (set SITE)"; exit 1; }
PYTHON="${PYTHON:-python3}" sh web/emu/build.sh
node tests/host/emu_test.mjs app/build/emu/app.wasm
node tests/host/emu_bank_test.mjs app/build/emu/app.wasm
python3 web/emu/check_published.py app/build/emu ${QY70_ROM:+"$QY70_ROM"}
git -C "$SITE" pull -q --ff-only
rm -rf "${SITE:?}/$DEST"
mkdir -p "$SITE/$DEST/py"
cp app/build/emu/index.html app/build/emu/worklet.js app/build/emu/app.wasm app/build/emu/qybank_worker.js "$SITE/$DEST/"
cp app/build/emu/py/*.py "$SITE/$DEST/py/"
python3 web/emu/check_published.py "$SITE/$DEST" ${QY70_ROM:+"$QY70_ROM"}
# robots.txt: keep its entries, add the Disallow under "User-agent: *" once
grep -qx "Disallow: /$DEST/" "$SITE/robots.txt" 2>/dev/null || python3 - "$SITE/robots.txt" "$DEST" <<'PY'
import os, sys
p, d = sys.argv[1], sys.argv[2]
lines = open(p).read().splitlines() if os.path.exists(p) else ["User-agent: *", "Allow: /"]
i = lines.index("User-agent: *") + 1 if "User-agent: *" in lines else None
if i is None:
    lines = ["User-agent: *", "Disallow: /%s/" % d, ""] + lines
else:
    lines.insert(i, "Disallow: /%s/" % d)
open(p, "w").write("\n".join(lines) + "\n")
PY
SHA=$(git rev-parse --short HEAD 2>/dev/null || echo dev)
git -C "$SITE" add -A "$DEST" robots.txt
if git -C "$SITE" diff --cached --quiet; then echo "site: $DEST unchanged"; exit 0; fi
git -C "$SITE" commit -q -m "${1:-Polly test build, in the browser} (fm1-polly $SHA)"
git -C "$SITE" push -q
echo "published: https://charles.pizza/$DEST/ (fm1-polly $SHA)"
