#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Publish the emulator at charles.pizza/fm1-gm: build it, test it, copy build/emu into the site's repo
# (a sibling checkout, ../charles.pizza from fm1-parent, or $SITE), commit and push (it deploys on push).
#   web/emu/publish.sh ["message"]
set -e
cd "$(dirname "$0")/../.."
SITE="${SITE:-$(cd .. && pwd)/../charles.pizza}"
[ -d "$SITE/.git" ] || { echo "no site checkout at $SITE (set SITE)"; exit 1; }
sh web/emu/build.sh
node tests/host/emu_test.mjs build/emu/gm.wasm
git -C "$SITE" pull -q --ff-only
mkdir -p "$SITE/fm1-gm"
cp build/emu/index.html build/emu/worklet.js build/emu/gm.wasm "$SITE/fm1-gm/"
SHA=$(git rev-parse --short HEAD 2>/dev/null || echo dev)
git -C "$SITE" add fm1-gm
if git -C "$SITE" diff --cached --quiet; then echo "site: fm1-gm unchanged"; exit 0; fi
git -C "$SITE" commit -q -m "${1:-Life for the FM-1, in the browser} (fm1-gm $SHA)"
git -C "$SITE" push -q
echo "published: https://charles.pizza/fm1-gm/ (fm1-gm $SHA)"
