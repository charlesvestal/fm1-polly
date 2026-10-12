#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Polly in the browser: the host simulator compiled to WebAssembly (Emscripten), plus its page.
#   [GM_VERSION=0.1] web/emu/build.sh  ->  build/emu/{index.html, worklet.js, polly.wasm, qybank_worker.js, py/*.py}
# Same sources and flags as host/build_host.sh (-ffp-contract=off, like the device).
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/gen build/emu
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
    -DGM_HOST -DGM_WEB "-DGM_VERSION=\"$(printf %s "${GM_VERSION:-DEV}" | tr a-z A-Z)\"" -Ifirmware/src -Ifirmware/src/dsp -Ifirmware/src/dsp/eas -Ibuild/gen \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/polly.wasm web/emu/polly_web.c firmware/src/dsp/gm.c firmware/src/dsp/chords.c firmware/src/dsp/xpose.c firmware/src/dsp/arr.c firmware/src/dsp/loops.c firmware/src/dsp/leads.c firmware/src/dsp/notes.c firmware/src/dsp/qybank.c firmware/src/dsp/qydec.c firmware/src/dsp/templates.c $(ls firmware/src/dsp/eas/*.c | grep -v "wt_22khz\|wt_200k_samples")
# the page, with the controls list taken from the firmware's own (ui.c's header comment), so it stays current;
# the QY70 bank builder: the worker, its entry point and Polly's tools (GPL code, no Yamaha data: the tester's
# own ROM is read in their browser)
python3 - <<'PY'
import html, re
ui = open("firmware/src/app/ui.c").read()
rest = ui[ui.index("*/") + 2:]                       # the comment after the licence line
head = rest[rest.index("/*") + 2:rest.index("*/")] if "/*" in rest and "*/" in rest else ""
lines = [re.sub(r"^ \* ?", "", l) for l in head.splitlines()]
table = lines[lines.index("") + 1:] if "" in lines else []
block = ('<pre class="controls">' + html.escape("\n".join(l.rstrip() for l in table)) + "</pre>") if table else \
        "<p>The firmware's controls list is in firmware/src/app/ui.c.</p>"
page = open("web/emu/index.html").read()
assert "<!--CONTROLS-->" in page
open("build/emu/index.html", "w").write(page.replace("<!--CONTROLS-->", block))
PY
mkdir -p build/emu/py
cp web/emu/worklet.js web/emu/qybank_worker.js build/emu/
cp web/emu/qy_build.py tools/qy70_import.py tools/qy70_bank.py tools/qy70_codec.py build/emu/py/
echo "emu: build/emu ($(wc -c < build/emu/polly.wasm) B wasm)"
