#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The emulator as a private claude.ai Artifact (the repo stays private, there is no public site):
app/build/emu (web/emu/build.sh) -> app/build/artifact/{index.html, worklet.js, the modules beside them}. The page loses its
document wrapper (the Artifact host adds one) and its site links; app.wasm goes into it as base64,
so nothing is fetched."""
import base64
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EMU, OUT = ROOT / "app/build/emu", ROOT / "app/build/artifact"
OUT.mkdir(parents=True, exist_ok=True)
s = (EMU / "index.html").read_text()
s = re.sub(r"<!doctype html>\s*<html[^>]*>\s*<head>\s*", "", s)
s = re.sub(r'<meta charset="utf-8">\s*<meta name="viewport"[^>]*>\s*', "", s)
s = s.replace("</head>\n<body>\n", "").replace("</body>\n</html>\n", "")
s = re.sub(r"\s*<nav>.*?</nav>", "", s)
wasm = base64.b64encode((EMU / "app.wasm").read_bytes()).decode()
old = 'fetch("app.wasm").then((r) => { if (!r.ok) throw new Error(`app.wasm: ${r.status}`); return r.arrayBuffer(); })'
assert old in s
s = s.replace(old, "Promise.resolve(Uint8Array.from(atob(POLLY_WASM), (c) => c.charCodeAt(0)).buffer)")
s = s.replace('<script>\n"use strict";', f'<script>\n"use strict";\nconst POLLY_WASM = "{wasm}";', 1)
s = s.replace("<title>Polly in the browser</title>", "<title>Polly Emulator</title>", 1)
(OUT / "index.html").write_text(s)
# the modules the page and the worklet load beside it: fm1-base's worklet and emulator parts, Polly's own parts, the QY70 box
mods = ["worklet.js", "app.js", "app_own.js", "app_worklet.js", "app_own_worklet.js", "qy.js", "qybank_worker.js",
        *(str(p.relative_to(EMU)) for p in sorted((EMU / "parts").glob("*.js")))]
for m in mods:
    if (EMU / m).exists():
        (OUT / m).parent.mkdir(parents=True, exist_ok=True)
        (OUT / m).write_text((EMU / m).read_text())
print(f"artifact: {OUT / 'index.html'} ({len(s)} B), {', '.join(m for m in mods if (EMU / m).exists())}")
