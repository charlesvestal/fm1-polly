// SPDX-License-Identifier: GPL-3.0-only
// The browser build in Node: boot it, press a few keys, expect sound and screen frames.
import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]);
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(2800); ex.web_boot();
// SEQ: the keys play the selected track there (on HOME they play chords, heard with the band)
const B_SEQ = 9;
ex.web_buttons(1 << B_SEQ); for (let i = 0; i < 8; i++) ex.web_render(128);
ex.web_buttons(0); for (let i = 0; i < 8; i++) ex.web_render(128);
let peak = 0;
const WHITE = [0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26];
for (let i = 0; i < 400; i++) {
  if (i % 20 === 0 && i < 340) ex.web_keys(1 << WHITE[(i / 20) % 16]);
  ex.web_render(128);
  const l = new Float32Array(mem.buffer, ex.web_out_l(), 128);
  for (const v of l) peak = Math.max(peak, Math.abs(v));
}
console.log("peak", peak.toFixed(3), "blits", ex.web_blits());
// the page's screen reader (web_reader, web_said_buf): HELLO answered with the words file's link, then the screen in
// words; a WHERE (web_sysex) says it again; off, nothing more
const said = () => { const b = new Uint8Array(mem.buffer, ex.web_said_buf(), ex.web_said_len()).slice(); ex.web_said_clear(); return b; };
const frames = (b) => { const out = []; for (let i = 0; i < b.length; i++) if (b[i] === 0xF0) { let j = i; while (b[j] !== 0xF7) j++; out.push(b.subarray(i, j + 1)); i = j; } return out; };
const text = (f) => String.fromCharCode(...f.subarray(6, f.length - 1));
ex.web_reader(1);
for (let i = 0; i < 400; i++) ex.web_render(128);                       // ~1.2 s
let fs1 = frames(said());
const hello = fs1.find((f) => f[4] === 1), says = fs1.filter((f) => f[4] === 16).map(text);
const link = hello ? text(hello).split("\0")[1] : "";
new Uint8Array(mem.buffer, ex.web_sysex_buf(), 4).set([0x7D, 0x53, 0x52, 3]); ex.web_sysex(4);
for (let i = 0; i < 400; i++) ex.web_render(128);
const again = frames(said()).filter((f) => f[4] === 16).length;
ex.web_reader(0);
for (let i = 0; i < 400; i++) ex.web_render(128);
const after = frames(said()).length;
console.log("reader: link", link, "|", says.length, "said, first:", says[0], "| WHERE:", again, "| off:", after);
const readerOk = /^https:\/\/\S+\/words\/\w+\.json$/.test(link) && says.length > 0 && /page/.test(says.join(" ")) && again > 0 && after === 0;
process.exit(peak > 0.05 && peak < 1 && readerOk ? 0 : 1);
