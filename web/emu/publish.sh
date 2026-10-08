#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Publish the emulator at charles.pizza/fm1-polly: build it, test it, copy build/emu into the site's repo
# (a sibling checkout, ../charles.pizza from fm1-parent, or $SITE), commit and push (it deploys on push).
#   web/emu/publish.sh ["message"]
set -e
cd "$(dirname "$0")/../.."
SITE="${SITE:-$(cd .. && pwd)/../charles.pizza}"
[ -d "$SITE/.git" ] || { echo "no site checkout at $SITE (set SITE)"; exit 1; }
sh web/emu/build.sh
node tests/host/emu_test.mjs build/emu/polly.wasm
git -C "$SITE" pull -q --ff-only
mkdir -p "$SITE/fm1-polly"
cp build/emu/index.html build/emu/worklet.js build/emu/polly.wasm "$SITE/fm1-polly/"
SHA=$(git rev-parse --short HEAD 2>/dev/null || echo dev)
git -C "$SITE" add fm1-polly
if git -C "$SITE" diff --cached --quiet; then echo "site: fm1-polly unchanged"; exit 0; fi
git -C "$SITE" commit -q -m "${1:-Polly for the FM-1, in the browser} (fm1-polly $SHA)"
git -C "$SITE" push -q
echo "published: https://charles.pizza/fm1-polly/ (fm1-polly $SHA)"
