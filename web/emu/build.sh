#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Polly in the browser: fm1-base's emulator build (./build.sh --web: the simulator in WebAssembly, app.wasm, with
# Polly's page and worklet from app/web/emu/), then Polly's additions: the controls list taken from the firmware's own
# (app/ui.c's header comment), so it stays current, and the QY70 bank builder (its worker and Polly's tools, run by
# Pyodide: no Yamaha data, the tester's own ROM is read in their browser).
#   [PYTHON=python3] web/emu/build.sh [--release X.Y]  ->  app/build/emu/{index.html, worklet.js, app.wasm,
#                                                           qybank_worker.js, py/*.py, reader/}
set -e
cd "$(dirname "$0")/../.."
PY="${PYTHON:-python3}"
sh build.sh --web "$@"
E=app/build/emu
"$PY" - <<'PY'
import html, re
ui = open("app/ui.c").read()
rest = ui[ui.index("*/") + 2:]                       # the comment after the licence lines
head = rest[rest.index("/*") + 2:rest.index("*/")] if "/*" in rest and "*/" in rest else ""
lines = [re.sub(r"^ \* ?", "", l) for l in head.splitlines()]
table = lines[lines.index("") + 1:] if "" in lines else []
block = ('<pre class="controls" tabindex="0" aria-label="The controls, from the firmware">' + html.escape("\n".join(l.rstrip() for l in table)) + "</pre>") if table else \
        "<p>The firmware's controls list is in app/ui.c.</p>"
page = open("app/web/emu/index.html").read()
assert "<!--CONTROLS-->" in page
open("app/build/emu/index.html", "w").write(page.replace("<!--CONTROLS-->", block))
PY
mkdir -p $E/py
cp app/web/emu/qybank_worker.js $E/
cp app/web/emu/qy_build.py tools/qy70_import.py tools/qy70_bank.py tools/qy70_codec.py $E/py/
echo "emu: $E ($(wc -c < $E/app.wasm) B wasm)"
