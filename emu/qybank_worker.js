// SPDX-License-Identifier: GPL-3.0-only
// The QY70 bank, built in the tester's browser: Pyodide (Python in WebAssembly) runs Polly's own tools unchanged
// (tools/qy70_import.py, qy70_bank.py, qy70_codec.py; web/emu/qy_build.py calls the codec's --image path) on the
// ROM the tester picked. The ROM and the bank stay in this browser: nothing is sent anywhere. A worker, so the
// page and its sound keep going for the ten seconds or so it takes. Started as a module worker.
//   in:  { rom: ArrayBuffer }
//   out: { type: "progress", text } ... then { type: "done", image: ArrayBuffer, text } or { type: "error", text }

const PYODIDE = "https://cdn.jsdelivr.net/pyodide/v314.0.7/full/";
const TOOLS = ["qy70_import.py", "qy70_bank.py", "qy70_codec.py", "qy_build.py"];
let ready = null;

function say(text) { postMessage({ type: "progress", text }); }

async function start() {
  say("Loading Python (Pyodide, about 10 MB the first time)...");
  const { loadPyodide } = await import(PYODIDE + "pyodide.mjs");    // (a module worker: importScripts of the
  const [py, files] = await Promise.all([                            // CDN fails in some browsers)
    loadPyodide({ indexURL: PYODIDE }),
    Promise.all(TOOLS.map((f) => fetch("py/" + f).then((r) => {
      if (!r.ok) throw new Error(`py/${f}: ${r.status}`);
      return r.text();
    }))),
  ]);
  py.FS.mkdirTree("/qy");
  TOOLS.forEach((f, i) => py.FS.writeFile("/qy/" + f, files[i]));
  py.runPython('import sys; sys.path.insert(0, "/qy")');
  return py;
}

onmessage = async (e) => {
  try {
    ready ??= start();
    const py = await ready;
    say("Reading your ROM and building the styles...");
    const t0 = performance.now();
    py.FS.writeFile("/rom.bin", new Uint8Array(e.data.rom));
    let r;
    try {
      r = py.runPython('import qy_build; qy_build.build("/rom.bin", "/bank.img")').toJs();
    } finally {
      try { py.FS.unlink("/rom.bin"); } catch {}
    }
    const [ok, text] = r;
    if (!ok) { postMessage({ type: "error", text }); return; }
    const image = py.FS.readFile("/bank.img").slice().buffer;
    py.FS.unlink("/bank.img");
    postMessage({ type: "done", image, text, ms: Math.round(performance.now() - t0) }, [image]);
  } catch (err) {
    ready = null;
    postMessage({ type: "error", text: "The bank builder could not run: " + (err && err.message || err) });
  }
};
