#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Polly in the browser: fm1-base's emulator build (base/tools/build.py app --web: the simulator in WebAssembly,
# app.wasm, fm1-base's page and worklet with the emulator's parts, Polly's app.js and app_worklet.js from app/web/emu/),
# Polly's bits of the page (tools/emu_page.py: app/web/emu/page.html), then Polly's additions: the controls list taken
# from the firmware's own (app/ui.c's header comment), so it stays current, and the QY70 bank builder (qy.js, its
# worker and Polly's tools, run by Pyodide: no Yamaha data, the tester's own ROM is read in their browser).
#   [PYTHON=python3] web/emu/build.sh [--release X.Y]  ->  app/build/emu/{index.html, worklet.js, app.js, app_worklet.js,
#                                                           parts/, app.wasm, qy.js, qybank_worker.js, py/*.py, reader/}
#   (./build.sh --web runs this)
set -e
cd "$(dirname "$0")/../.."
PY="${PYTHON:-python3}"
case " $* " in *" --web "*) ;; *) set -- --web "$@";; esac      # (./build.sh --web comes here)
: "${JIELI_TOOLCHAIN:=$HOME/.jieli/toolchain}" "${AC79_SDK:=$HOME/fw-AC79_AIoT_SDK}"
export JIELI_TOOLCHAIN AC79_SDK
"$PY" base/tools/build.py app "$@"
"$PY" tools/emu_page.py
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
page = open("app/build/emu/index.html").read()
assert "<!--CONTROLS-->" in page
open("app/build/emu/index.html", "w").write(page.replace("<!--CONTROLS-->", block))
PY
mkdir -p $E/py
cp app/web/emu/qybank_worker.js app/web/emu/qy.js $E/
cp app/web/emu/qy_build.py tools/qy70_import.py tools/qy70_bank.py tools/qy70_codec.py $E/py/
echo "emu: $E ($(wc -c < $E/app.wasm) B wasm)"
