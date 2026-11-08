const PORT = process.env.SHOT_PORT || "9335";
const fs = await import("node:fs");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const list = await (await fetch("http://127.0.0.1:" + PORT + "/json/list")).json();
const page = list.find((t) => t.type === "page");
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
const send = (m, p = {}) => new Promise((res, rej) => {
  const i = ++id;
  pending.set(i, { res, rej });
  ws.send(JSON.stringify({ id: i, method: m, params: p }));
});
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) {
    const q = pending.get(m.id);
    pending.delete(m.id);
    m.error ? q.rej(new Error(JSON.stringify(m.error))) : q.res(m.result);
  }
};
await new Promise((r) => (ws.onopen = r));
await send("Page.enable");
await send("Runtime.enable");
const ev = async (x) => (await send("Runtime.evaluate", { expression: x, returnByValue: true, awaitPromise: true })).result.value;
const shot = async (file) => {
  const r = await send("Page.captureScreenshot", { format: "png" });
  fs.writeFileSync(file, Buffer.from(r.data, "base64"));
  console.log("saved " + file);
};

// A. 设置面板（真实 web/index.html，无宿主）
await send("Page.navigate", { url: "file:///D:/codexproject/reader/web/index.html" });
await sleep(1200);
await ev('(function(){document.getElementById("home").hidden=true;document.getElementById("reader").hidden=false;document.getElementById("settings-panel").hidden=false;document.getElementById("dim").hidden=true;return 1;})()');
await send("Emulation.setDeviceMetricsOverride", { width: 1180, height: 1400, deviceScaleFactor: 1, mobile: false });
await sleep(400);
await shot(process.env.SHOT_A || "C:/Windows/Temp/ui_settings.png");

// B. 阅读页 + 缓存标记
await send("Page.navigate", { url: "file:///D:/codexproject/reader/tests/web_smoke.html" });
for (let i = 0; i < 60; i++) {
  await sleep(200);
  const has = await ev('(function(){return !!document.querySelector(\'#content .chapter-block[data-index="5"] .ch-chip\');})()');
  if (has) break;
}
await ev('(function(){var e=document.querySelector(\'#content .chapter-block[data-index="5"]\');if(e)e.scrollIntoView({block:"start"});return 1;})()');
await sleep(250);
await shot(process.env.SHOT_B || "C:/Windows/Temp/ui_chip.png");
ws.close();
