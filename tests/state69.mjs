const PORT = process.env.PROBE_PORT || "9336";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const list = await (await fetch("http://127.0.0.1:" + PORT + "/json/list")).json();
const page = list.find((t) => t.type === "page" && /reader[.]local/.test(t.url));
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0; const pending = new Map();
const send = (m, p = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method: m, params: p })); });
ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { const q = pending.get(m.id); pending.delete(m.id); m.error ? q.rej(new Error(JSON.stringify(m.error))) : q.res(m.result); } };
await new Promise((r) => (ws.onopen = r));
await send("Runtime.enable");
const ev = async (x) => { const r = await send("Runtime.evaluate", { expression: x, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) return "EXC:" + JSON.stringify(r.exceptionDetails.exception && r.exceptionDetails.exception.description); return r.result ? r.result.value : undefined; };
if (process.env.KEYS) {
  for (const k of process.env.KEYS.split(",")) {
    const code = k === "Space" ? " " : k;
    await ev("window.dispatchEvent(new KeyboardEvent(\"keydown\",{key:" + JSON.stringify(code) + ",code:" + JSON.stringify(k) + ",bubbles:true}))");
    await sleep(Number(process.env.KEY_GAP || 700));
  }
}
await sleep(Number(process.env.WAIT || 2500));
console.log("tb-title=", await ev("(document.getElementById(\"tb-title\")||{}).textContent"));
console.log("tb-meta=", await ev("(document.getElementById(\"tb-meta\")||{}).textContent"));
console.log("tb-progress=", await ev("(document.getElementById(\"tb-progress\")||{}).textContent"));
console.log("blocks=", await ev("document.querySelectorAll(\"#content .chapter-block\").length"));
console.log("indices=", await ev("Array.from(document.querySelectorAll(\"#content .chapter-block\")).map(function(e){return e.dataset.index;}).join(\",\")"));
console.log("titles=", await ev("Array.from(document.querySelectorAll(\"#content .ch-title\")).map(function(e){return e.textContent;}).join(\" / \")"));
console.log("paraCounts=", await ev("Array.from(document.querySelectorAll(\"#content .chapter-block\")).map(function(e){return e.querySelectorAll(\"p\").length;}).join(\",\")"));
console.log("firstParas=", await ev("(document.querySelector(\"#content .chapter-block\")||{innerText:\"\"}).innerText.slice(0,260)"));
console.log("scrollTop=", await ev("document.getElementById(\"viewport\").scrollTop"));
console.log("errBar=", await ev("(document.getElementById(\"error-bar\")||{}).hidden + \"|\" + (document.getElementById(\"error-bar\")||{}).textContent"));
ws.close();
