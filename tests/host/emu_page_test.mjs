// SPDX-License-Identifier: GPL-3.0-only
// The browser build as the page runs it (web/emu/build.sh): fm1-base's page with Polly's bits (tools/emu_page.py: the
// help with the controls list, the QY70 box), its worklet with the emulator's parts, among them "resample": in a
// browser whose audio runs at 48 kHz (many Macs) the FM-1's clock keeps the page's time (without it Polly plays 48/44.1
// fast and high). And the QY70 bank's way in (app_worklet.js): with the load message, before Polly boots, answered
// with what it mounted; a bad one while running is refused. fm1-base's worklet under node (base/tests/emu_harness.mjs).
// No ROM needed (tests/host/emu_bank_test.mjs takes yours).
//   node tests/host/emu_page_test.mjs app/build/emu
import { existsSync, readFileSync } from "node:fs";
import { startEmu, check, done } from "../../base/tests/emu_harness.mjs";

const dir = process.argv[2] || "app/build/emu";
const page = readFileSync(`${dir}/index.html`, "utf8");
const read = (f) => existsSync(`${dir}/${f}`) ? readFileSync(`${dir}/${f}`, "utf8") : "";
check(page.includes("<title>Polly in the browser</title>") && page.includes('<section class="help">'), "fm1-base's page with Polly's name and help");
check(page.includes('<pre class="controls"') && !page.includes("<!--CONTROLS-->"), "the controls list from app/ui.c");
check(page.includes('id="qyFile"') && page.includes('src="qy.js"') && read("qy.js").includes("qy70-bank") && existsSync(`${dir}/qybank_worker.js`),
      "the QY70 box, its script and its worker");
check(!page.includes("{{"), "no placeholder left in the page");
check(read("app_own.js").includes("Polly") && read("app.js").includes("app_own.js"), "Polly's app.js on the page");
check(read("app_worklet.js").includes("app_own_worklet.js") && read("app_worklet.js").includes("resample_worklet.js")
      && existsSync(`${dir}/parts/resample_worklet.js`), "the worklet has Polly's part and the part resample");
const emu = await startEmu(dir, { sampleRate: 48000 });
const atLoad = emu.take("app");
check(atLoad.length === 1 && atLoad[0].atLoad && atLoad[0].bank === 0, `no bank at load: answered ${JSON.stringify(atLoad)}`);
emu.run(500);
const t0 = emu.ex.web_now();
emu.run(1000);
const ms = emu.ex.web_now() - t0;
check(Math.abs(ms - 1000) <= 12, `at 48 kHz the FM-1 ran ${ms} ms in the page's 1000`);
await emu.send({ type: "app", bank: new Uint8Array(64).fill(0x55).buffer });
const bad = emu.take("app");
check(bad.length === 1 && bad[0].bank <= 0, `a damaged bank while running: refused (${bad.length && bad[0].bank})`);
await emu.send({ type: "app", bank: null });
const none = emu.take("app");
check(none.length === 1 && none[0].bank === 0 && emu.run(300) >= 0, "forgotten: Polly's own styles");
done();
