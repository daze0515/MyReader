const PORT = process.env.PROBE_PORT || "9336";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const list = await (await fetch("http://127.0.0.1:" + PORT + "/json/list")).json();
const page = list.find((t) => t.type === "page");
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
const send = (m, p = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method: m, params: p })); });
ws.onmessage = (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { const q = pending.get(m.id); pending.delete(m.id); m.error ? q.rej(new Error(JSON.stringify(m.error))) : q.res(m.result); return; }
  if (m.method === "Page.frameNavigated") console.log("   [nav]", m.params.frame.url, "|", m.params.frame.unreachableUrl || "");
  if (m.method === "Page.loadEventFired") console.log("   [load]");
  if (m.method === "Network.loadingFailed") console.log("   [netfail]", m.params.errorText, m.params.type);
};
await new Promise((r) => (ws.onopen = r));
await send("Page.enable");
await send("Runtime.enable");
await send("Network.enable");
const ev = async (x) => { const r = await send("Runtime.evaluate", { expression: x, returnByValue: true }); return r.result ? r.result.value : undefined; };
console.log("start url=", await ev("location.href"), "title=", await ev("document.title"));
console.log("toast=", await ev("(document.getElementById('toast')||{}).textContent"));
console.log("click:", await ev("(function(){var c=document.querySelector('.book-card');if(!c)return 'no card';c.click();return 'clicked';})()"));
for (let i = 0; i < 20; i++) {
  await sleep(1500);
  console.log(i, "url=", await ev("location.href"), "title=", await ev("document.title"), "toast=", await ev("(document.getElementById('toast')||{}).textContent"));
}
ws.close();
