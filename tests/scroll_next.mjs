const PORT = process.env.PROBE_PORT || "9336";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const list = await (await fetch("http://127.0.0.1:" + PORT + "/json/list")).json();
const page = list.find((t) => t.type === "page" && /reader\.local/.test(t.url));
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
const send = (m, p = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method: m, params: p })); });
ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { const q = pending.get(m.id); pending.delete(m.id); m.error ? q.rej(new Error(JSON.stringify(m.error))) : q.res(m.result); } };
await new Promise((r) => (ws.onopen = r));
await send("Runtime.enable");
const ev = async (x) => { const r = await send("Runtime.evaluate", { expression: x, returnByValue: true }); return r.result ? r.result.value : undefined; };
const state = async () => await ev("Array.from(document.querySelectorAll('#content .chapter-block')).map(function(e){return e.dataset.index;}).join(',') + ' | last=' + (function(){var b=document.querySelector('#content .chapter-block:last-child');return b?b.innerText.slice(0,40).replace(/\s+/g,' '):'';})()");
console.log("before:", await state());
for (let i = 0; i < 12; i++) {
  await ev("(function(){var v=document.getElementById('viewport');v.scrollTop=v.scrollHeight;return 1;})()");
  await sleep(1300);
  console.log("step" + i + ":", await state());
}
ws.close();
