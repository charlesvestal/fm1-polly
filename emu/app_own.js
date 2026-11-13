// SPDX-License-Identifier: GPL-3.0-only
// Polly's own part of fm1-base's emulator page (base/web/emu/index.html; fm1-base's build loads it as app_own.js
// beside the emulator's parts, when the emulator is switched on). It works through the page's own names (KEYMAP,
// holdDown, turn, knobs, buttons, shortcuts, controls, node): the QY70 bank into the emulator (qy.js keeps it; it
// goes in with the load message, before Polly boots, and at once when built or forgotten while on), what each
// control does on hover, and Polly's computer keys: Space PLAY, the arrows PRESETS and ALGORITHM, on the panel and
// the page. A control reached with Tab keeps Space and Enter for itself (it presses that control); one clicked with
// the mouse or a finger does not, so Space stays PLAY after playing the panel with the mouse.
import * as qy from "./qy.js";

// ---------------------------------------------------------------- the QY70 bank (app_worklet.js puts it in the flash)
let port = null, answer = null;
function bankTo(image) {                              // a bank (or null: none) into the running emulator: its mount
  return new Promise((res) => {
    answer = res;
    const copy = image ? image.slice(0) : null;
    port.postMessage({ type: "app", bank: copy }, copy ? [copy] : []);
  });
}
function bankAtLoad(node) {                           // the kept bank in the load message (the worklet's load takes it)
  port = node.port;
  const post = port.postMessage.bind(port);
  port.postMessage = (m, transfer) => {
    if (!m || m.type !== "load") return post(m, transfer);
    qy.loaded.then(() => {
      const image = qy.bank ? qy.bank.image.slice(0) : null;
      post({ ...m, bank: image }, image ? [...(transfer || []), image] : transfer);
    });
  };
}
export function message(m) {
  if (!("bank" in m)) return;
  if (m.atLoad) { qy.connect(bankTo); qy.mounted(m.bank); }
  else if (answer) { const a = answer; answer = null; a(m.bank); }
}

// ---------------------------------------------------------------- what each control does, on hover and for a screen reader
// (from the firmware's controls list; the full list is under the device)
const TIP = { PLAY: "Starts and stops. On the Song page, it plays the song.", REC: "Records what the keys play here",
  SEL: "Shows this page as a list with its actions. In a question, it means yes.", HOME: "Home, then the Song pages. Hold it for the shortcuts.",
  SEQ: "The Band page, with the groove's notes", EDIT: "The Sound page for the instrument", FX: "Effects", LFO: "Mixer pages",
  ENV: "Style pages", ARP: "Progression pages", GLO: "Setup pages", SAVE: "Saves. Hold it and press a white key to copy here.",
  "OCT-": "Octave down", "OCT+": "Octave up", SELECT: "Turns the page. Hold HOME to set the tempo.", ALGORITHM: "Picks the instrument",
  PRESETS: "Depends on the page. Hold HOME to pick the song.", MASTER: "Volume",
  KNOB1: "Sets what the page shows", KNOB2: "Sets what the page shows", KNOB3: "Sets what the page shows", KNOB4: "Sets what the page shows" };
function tips() {
  for (const [n, b] of Object.entries(buttons)) if (TIP[n]) { b.hit.title = TIP[n]; b.hit.setAttribute("aria-description", TIP[n]); }
  for (const [n, k] of Object.entries(knobs)) {
    if (!TIP[n] || n === "MASTER") continue;
    const how = k.hit.getAttribute("aria-description");
    k.hit.title = TIP[n];
    k.hit.setAttribute("aria-description", how ? `${TIP[n]}. ${how}` : TIP[n]);
  }
}

// ---------------------------------------------------------------- the computer's keys
// fm1-base's map (white keys Q-I, A-K; black keys 1-0, -; Esc HOME; Z / X octave) and Polly's: Space PLAY; up and
// down turn PRESETS (up: back), left and right ALGORITHM, unless on a knob (it turns with them)
const KNOB_KEYS = new Set(["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "PageUp", "PageDown", "Home", "End"]);
const ARROWS = { ArrowUp: ["PRESETS", -1], ArrowDown: ["PRESETS", 1], ArrowLeft: ["ALGORITHM", -1], ArrowRight: ["ALGORITHM", 1] };
function keyboard() {
  const map = { ...KEYMAP, Space: { btn: BTN.PLAY } };
  let clicked = null;                    // the control the mouse or a finger last gave the focus to
  controls.addEventListener("pointerdown", (e) => { clicked = e.target.closest(".hit"); }, true);
  addEventListener("keydown", (e) => { if (e.key === "Tab") clicked = null; }, true);
  addEventListener("keydown", (e) => {
    if (!node || !shortcuts.checked || e.metaKey || e.ctrlKey || e.altKey) return;
    const t = e.target;
    if (t !== document.body && t !== document.documentElement && !controls.contains(t)) return;   // the page's fields
    const hit = t.closest ? t.closest(".hit") : null;
    if (hit && hit.classList.contains("knob") && KNOB_KEYS.has(e.key)) return;   // a knob turns with its keys
    if (hit && hit !== clicked && (e.key === " " || e.key === "Enter")) return;   // reached with Tab: presses it
    if (ARROWS[e.code]) {
      const [k, n] = ARROWS[e.code];
      turn(knobs[k], n);
      e.preventDefault(); e.stopPropagation();
      return;
    }
    const m = map[e.code];
    if (!m) return;
    e.preventDefault(); e.stopPropagation();
    if (!e.repeat) holdDown("k" + e.code, m.key !== undefined ? "key" : "btn", m.key !== undefined ? m.key : m.btn);
  }, true);
}

export function attach(ctx) {
  bankAtLoad(ctx.node);
  tips();
  keyboard();
}
