// SPDX-License-Identifier: GPL-3.0-only
// The browser build with a QY70 bank, as the page makes it: the tester's ROM -> web/emu/qy_build.py (the page runs
// the same function in Pyodide) -> the image into the wasm's flash. Needs your ROM: QY70_ROM=qy70main.bin;
// skipped without it. Checks Polly's 16 styles plus the bank's 128, that each decodes, that the bank also mounts
// after boot (the page's Load), that Forget leaves Polly's own, and sound.
//   QY70_ROM=... node tests/host/emu_bank_test.mjs build/emu/polly.wasm
import fs from "fs";
import os from "os";
import path from "path";
import { execFileSync } from "child_process";
import { fileURLToPath } from "url";

const rom = process.env.QY70_ROM;
if (!rom) { console.log("emu bank: skipped (set QY70_ROM to your qy70main.bin)"); process.exit(0); }
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "polly-bank-"));
const imgPath = path.join(tmp, "bank.img");
let t0 = Date.now();
try {
  console.log(execFileSync("python3", [path.join(root, "web/emu/qy_build.py"), rom, imgPath]).toString().trim().replace(imgPath, "bank"));
} catch (e) { console.log("emu bank: the builder refused the ROM:", (e.stdout || "").toString().trim()); process.exit(1); }
const img = fs.readFileSync(imgPath);
fs.rmSync(tmp, { recursive: true });
console.log(`image ${img.length} B in ${Date.now() - t0} ms`);

const wasm = fs.readFileSync(process.argv[2]);
let fail = 0;
const check = (ok, what) => { console.log((ok ? "ok   " : "FAIL ") + what); if (!ok) fail++; };
async function device() {
  let mem;
  const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
    fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
  const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
  const ex = { ...instance.exports }; mem = ex.memory;
  if (ex._initialize) ex._initialize();
  ex.web_master(2800);
  ex.put = (b) => { new Uint8Array(mem.buffer, ex.web_bank_buf(), b.length).set(b); return ex.web_bank_load(b.length); };
  ex.mem = mem;
  return ex;
}

// at startup (the page: from IndexedDB, before boot)
let ex = await device();
check(img.length <= ex.web_bank_max(), `image fits the page's buffer (${img.length} <= ${ex.web_bank_max()})`);
check(ex.put(img) === 128, "bank mounted before boot: 128 styles");
ex.web_boot();
for (let i = 0; i < 50; i++) ex.web_render(128);
check(ex.web_bank_status() === 128, "after boot the UI's mount found 128");
check(ex.web_styles_own() === 16 && ex.web_styles_bank() === 128, `styles: ${ex.web_styles_own()} + ${ex.web_styles_bank()}`);
t0 = Date.now();
check(ex.web_bank_check() === 0, `every bank style decodes (${Date.now() - t0} ms)`);
let peak = 0;
for (let i = 0; i < 400; i++) {
  if (i % 40 === 0 && i < 340) ex.web_keys(1 << [0, 2, 4, 6, 7, 9][(i / 40) % 6]);
  ex.web_render(128);
  for (const v of new Float32Array(ex.mem.buffer, ex.web_out_l(), 128)) peak = Math.max(peak, Math.abs(v));
}
check(peak > 0.05 && peak < 1, `sound with the bank (peak ${peak.toFixed(3)})`);

// built while running (the page's Load), then forgotten
ex = await device();
ex.web_boot();
for (let i = 0; i < 50; i++) ex.web_render(128);
check(ex.web_styles_bank() === 0, "no bank: Polly's 16 only");
check(ex.put(img) === 128 && ex.web_styles_bank() === 128, "bank loaded after boot: 128");
check(ex.web_bank_load(0) === 0 && ex.web_styles_bank() === 0 && ex.web_styles_own() === 16, "forgotten: Polly's 16 again");
const bad = Uint8Array.from(img); bad[40] ^= 1;
check(ex.put(bad) < 0, "a damaged image is refused");
process.exit(fail ? 1 : 0);
