// SPDX-License-Identifier: GPL-3.0-only
// The mirror viewer's decoder (web/mirror/index.html, the part between its "decoder" markers) on the device's own SysEx
// stream, as the simulator mirrored it (tests/scenarios/mirror.polly): the screen it rebuilds against the simulator's
// screen, and the panel's reports (a button and keys held, knobs turned, MASTER, the lights).
//   node tests/host/mirror_test.mjs build/scenarios/mirror
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const dir = process.argv[2] || "build/scenarios/mirror";
const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const html = fs.readFileSync(path.join(root, "web/mirror/index.html"), "utf8");
const m = /\/\/ --- decoder: begin[^\n]*\n([\s\S]*?)\/\/ --- decoder: end/.exec(html);
if (!m) { console.log("FAIL no decoder markers in web/mirror/index.html"); process.exit(1); }
const { createMirror, mrRequest, mrUnpack7 } = new Function(`"use strict";\n${m[1]}\nreturn { createMirror, mrRequest, mrUnpack7 };`)();

let fails = 0;
const check = (ok, msg) => { if (!ok) { console.log("FAIL " + msg); fails++; } };

// the requests, as the device's ota_take reads them
for (const op of [0, 1, 2]) {
  const r = mrRequest(op), u = mrUnpack7(r.slice(1, -1));
  check(r[0] === 0xF0 && r[r.length - 1] === 0xF7 && u.join() === [0, 0x59, 0x50, 1, 0, 0, op, ~op & 0xFF].join(), `request ${op}: ${[...r]}`);
}

// the simulator's screen dump (as stored: high byte first) -> RGB565
function dump(name) {
  const b = fs.readFileSync(path.join(dir, name)), px = new Uint16Array(240 * 240);
  for (let i = 0; i < px.length; i++) px[i] = b[2 * i] << 8 | b[2 * i + 1];
  return px;
}
function rowsDiffering(a, b) {
  let n = 0;
  for (let r = 0; r < 240; r++)
    for (let i = 0; i < 240; i++) if (a[r * 240 + i] !== b[r * 240 + i]) { n++; break; }
  return n;
}

// fed in pieces of any size (Web MIDI gives whole messages; a pipe may not)
function feedAll(dec, bytes, seed) {
  let s = seed, i = 0;
  while (i < bytes.length) {
    s = (s * 1103515245 + 12345) >>> 0;
    const n = 1 + (s >>> 16) % 700;
    dec.feed(bytes.subarray(i, i + n));
    i += n;
  }
}

const states = [];
let hellos = 0, rects = 0;
const dec = createMirror({ input: (s) => states.push(s), hello: (h) => { if (h.on && h.w === 240 && h.h === 240) hellos++; },
                          rect: () => rects++ });
const stream = fs.readFileSync(path.join(dir, "stream.syx"));
feedAll(dec, stream, 1);
const st = dec.stats;
check(st.bad === 0, `${st.bad} bad frames`);
check(st.gaps === 0, `${st.gaps} frames missing (seq)`);
check(hellos >= 1, "no hello");
const diff = rowsDiffering(dec.screen, dump("final2.bin"));
check(diff === 0, `the screen: ${diff} rows differ from the simulator's (final2.bin)`);

// the panel: SEL held with keys 5 and 9, K1 +3, PRESETS -2, K2 +4, MASTER 3000, PLAY lit
const B_SEL = 1, B_PLAY = 10, EN_PRESET = 2, EN_K1 = 3, EN_K2 = 4;
const sum = [0, 0, 0, 0, 0, 0, 0];
for (const s of states) s.enc.forEach((n, i) => { sum[i] += n; });
check(states.some((s) => s.buttons & (1 << B_SEL) && (s.keys & 0x220) === 0x220), "no report of SEL held with keys 5 and 9");
check(sum[EN_K1] === 3 && sum[EN_PRESET] === -2 && sum[EN_K2] === 4, `the turns: ${sum} (K1 3, PRESETS -2, K2 4)`);
check(sum[0] === 0 && sum[1] === 0 && sum[5] === 0 && sum[6] === 0, `turns not made: ${sum}`);
check(states.length && states[states.length - 1].master === 3000, `MASTER ${states.length && states[states.length - 1].master}`);
check(states.some((s) => s.litButtons & (1 << B_PLAY) && !s.playRed), "PLAY never lit (green)");
check(states.length && !states[states.length - 1].buttons && !states[states.length - 1].keys, "a button or key left held");

// the second session: from scratch, through to the viewer gone quiet
const dec2 = createMirror();
feedAll(dec2, fs.readFileSync(path.join(dir, "stream2.syx")), 7);
check(dec2.stats.bad === 0 && dec2.stats.gaps === 0, `stream2: ${dec2.stats.bad} bad, ${dec2.stats.gaps} missing`);
check(rowsDiffering(dec2.screen, dump("second.bin")) === 0, "stream2: the screen differs from second.bin");

console.log(`${fails ? "FAILED" : "ok"}: ${stream.length} bytes, ${st.frames} frames (${st.rects} rects, ${st.fills} fills, ` +
            `${st.inputs} input reports), turns ${sum.join(" ")}`);
process.exit(fails ? 1 : 0);
