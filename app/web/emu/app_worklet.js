// SPDX-License-Identifier: GPL-3.0-only
// Polly's own part of fm1-base's emulator worklet (base/web/emu/worklet.js; fm1-base's build loads it first, as
// app_own_worklet.js): a QY70 style bank the page built from the tester's ROM into the emulated flash (app/web.c),
// from the load message, before Polly boots, or when built (or forgotten: null) while it runs. Each answers the
// page with the styles mounted, or the mount's error (<= 0): { type: "app", bank, atLoad } (at load always, 0 with none).

// the load message's bank, which fm1App.load is not given: kept by the processor as it comes
const register = globalThis.registerProcessor;
globalThis.registerProcessor = (name, Proc) => register(name, name !== "fm1" ? Proc : class extends Proc {
  onMessage(m) {
    if (m && m.type === "load") this.bank = m.bank || null;
    return super.onMessage(m);
  }
});

function putBank(fm1, buf) {
  const ex = fm1.ex, b = buf ? new Uint8Array(buf) : new Uint8Array(0);
  if (b.length > ex.web_bank_max()) return -1;
  new Uint8Array(ex.memory.buffer, ex.web_bank_buf(), b.length).set(b);
  return ex.web_bank_load(b.length);
}

globalThis.fm1App = {
  load(fm1) {
    fm1.port.postMessage({ type: "app", bank: fm1.bank ? putBank(fm1, fm1.bank) : 0, atLoad: true });
    fm1.bank = null;
  },
  message(fm1, m) {
    if ("bank" in m) fm1.port.postMessage({ type: "app", bank: putBank(fm1, m.bank) });
  },
};
