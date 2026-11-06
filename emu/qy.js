// SPDX-License-Identifier: GPL-3.0-only
// The QY70 styles box on the emulator's page (app/web/emu/page.html puts in its HTML and loads this at once, so the
// box says what this browser keeps and takes a ROM before the emulator is on). The bank is built from the tester's
// ROM in a worker (qybank_worker.js: Pyodide running Polly's own tools), kept in this browser's IndexedDB (the bank
// image only, never the ROM), and given to the emulator by app.js: at switch-on (in the load message), or at once
// when built or forgotten while it runs (connect).

const IDB = { db: "polly-emu", store: "files", key: "qy70-bank" };
function idb(mode, fn) {
  return new Promise((res, rej) => {
    let rq;
    try { rq = indexedDB.open(IDB.db, 1); } catch (e) { rej(e); return; }
    rq.onupgradeneeded = () => rq.result.createObjectStore(IDB.store);
    rq.onerror = () => rej(rq.error);
    rq.onsuccess = () => {
      const tx = rq.result.transaction(IDB.store, mode), r = fn(tx.objectStore(IDB.store));
      tx.oncomplete = () => { rq.result.close(); res(r && r.result); };
      tx.onerror = tx.onabort = () => { rq.result.close(); rej(tx.error); };
    };
  });
}
const bankGet = () => idb("readonly", (s) => s.get(IDB.key)).catch(() => null);
const bankPut = (v) => idb("readwrite", (s) => s.put(v, IDB.key));
const bankDel = () => idb("readwrite", (s) => s.delete(IDB.key)).catch(() => {});

const $ = (id) => document.getElementById(id);
const qy = $("qy"), qyFile = $("qyFile"), qyForget = $("qyForget"), qyState = $("qyState");
export let bank = null;                               // { image: ArrayBuffer, styles, built }
let building = false, worker = null, toEmu = null;    // toEmu(image or null): the emulator's mount, once it is on
function qySay(text, err) { qyState.textContent = text; qyState.classList.toggle("err", !!err); }
function bankShown(n) {                               // n: the emulator's mount (styles, 0 none, -1 damaged, -2 old)
  qyForget.hidden = !bank;
  if (!bank) qySay("None loaded. Polly plays its own 16 styles.");
  else if (n === -2) qySay("The kept QY70 styles are from an older Polly. Load your ROM again to rebuild them.", true);
  else if (n !== undefined && n <= 0) qySay("The kept QY70 styles didn't load. Load your ROM again.", true);
  else qySay(`This browser keeps ${bank.styles} QY70 styles.${toEmu ? "" : " They play once Polly is on."}`);
}
/** the emulator is on: put(image or null) mounts a bank in it and resolves to its mount */
export function connect(put) { toEmu = put; }
/** what the emulator mounted at switch-on */
export function mounted(n) { if (!building) bankShown(bank ? n : undefined); }

async function buildFrom(file) {
  if (building || !file) return;
  building = true;
  qyFile.disabled = true;
  const t0 = performance.now();
  let msg = "Starting...";
  const show = () => qySay(`${msg} ${Math.round((performance.now() - t0) / 1000)} s`);
  const tick = setInterval(show, 500);
  try {
    if (file.size < 65536 || file.size > 16 << 20) throw new Error(`${file.name} is ${file.size} bytes, but the QY70's main ROM (qy70main.bin) is 2 MB.`);
    const rom = await file.arrayBuffer();
    worker ??= new Worker("qybank_worker.js", { type: "module" });
    const r = await new Promise((res) => {
      worker.onmessage = (e) => { if (e.data.type === "progress") { msg = e.data.text; show(); } else res(e.data); };
      worker.onerror = (e) => res({ type: "error", text: "The bank builder could not start: " + (e.message || "worker error") });
      worker.postMessage({ rom }, [rom]);
    });
    if (r.type !== "done") throw new Error(r.text);
    const styles = +(/(\d+) styles/.exec(r.text) || [0, 0])[1];
    bank = { image: r.image, styles, built: Date.now(), ms: r.ms };
    try { await bankPut(bank); } catch { /* private window: this visit only */ }
    clearInterval(tick);
    const n = toEmu ? await toEmu(bank.image) : undefined;
    bankShown(n);
    if (n === undefined || n > 0) qySay(qyState.textContent + ` Built in ${(r.ms / 1000).toFixed(1)} s.`);
  } catch (err) {
    clearInterval(tick);
    qySay(err && err.message || String(err), true);
    if (bank) qyForget.hidden = false;
  } finally {
    building = false;
    qyFile.disabled = false;
    qyFile.value = "";
  }
}
qyFile.addEventListener("change", () => buildFrom(qyFile.files[0]));
for (const t of ["dragenter", "dragover"]) qy.addEventListener(t, (e) => { e.preventDefault(); qy.classList.add("drag"); });
for (const t of ["dragleave", "drop"]) qy.addEventListener(t, () => qy.classList.remove("drag"));
qy.addEventListener("drop", (e) => { e.preventDefault(); buildFrom(e.dataTransfer.files[0]); });
qyForget.addEventListener("click", async () => {
  bank = null;
  await bankDel();
  bankShown(toEmu ? await toEmu(null) : undefined);
});
/** the kept bank, read once at page load (null: none) */
export const loaded = bankGet().then((b) => { bank = b && b.image ? b : null; bankShown(); return bank; });
