const PORT = process.env.PROBE_PORT || "9340";
const URL_ = process.env.PROBE_URL || "https://twkan.com/book/67581/index.html";
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
await send("Network.enable");
const ev = async (x) => (await send("Runtime.evaluate", { expression: x, returnByValue: true, awaitPromise: true })).result.value;
console.log("UA =", await ev("navigator.userAgent"));
await send("Page.navigate", { url: URL_ });
for (let i = 0; i < 40; i++) {
  await sleep(1000);
  const t = await ev("document.title");
  console.log(i + "s title=" + JSON.stringify(t));
  if (t && !/just a moment|请稍候|attention required/i.test(t)) { console.log("PASSED challenge"); break; }
}
const ck = await send("Network.getAllCookies");
const mine = ck.cookies.filter((c) => /twkan/.test(c.domain));
console.log("cookies:", mine.map((c) => c.name + "(" + c.value.length + ")").join(", "));
const cf = mine.find((c) => c.name === "cf_clearance");
if (cf) fs.writeFileSync(process.env.PROBE_OUT || "cf_fresh.txt", cf.value);
const r = await send("Page.captureScreenshot", { format: "png" });
fs.writeFileSync(process.env.PROBE_SHOT || "cf_probe.png", Buffer.from(r.data, "base64"));
console.log("shot saved");
ws.close();
