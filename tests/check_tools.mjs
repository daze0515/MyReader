const PORT = process.env.PROBE_PORT || "9336";
const list = await (await fetch("http://127.0.0.1:" + PORT + "/json/list")).json();
const page = list.find(function (t) { return t.type === "page" && /reader\.local/.test(t.url); });
if (!page) { console.log("NO_PAGE"); for (const t of list) console.log("  ", t.type, t.url.slice(0, 110)); process.exit(1); }
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0; const pending = new Map(); const errors = [];
const send = function (m, p) { p = p || {}; return new Promise(function (res, rej) { const i = ++id; pending.set(i, { res: res, rej: rej }); ws.send(JSON.stringify({ id: i, method: m, params: p })); }); };
ws.onmessage = function (e) {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { const q = pending.get(m.id); pending.delete(m.id); m.error ? q.rej(new Error(JSON.stringify(m.error))) : q.res(m.result); }
  else if (m.method === "Runtime.exceptionThrown") { const d = m.params.exceptionDetails; errors.push(d.text + " " + ((d.exception && d.exception.description) || "")); }
};
await new Promise(function (r) { ws.onopen = r; });
await send("Runtime.enable");
const ev = async function (x) { const r = await send("Runtime.evaluate", { expression: x, returnByValue: true, awaitPromise: true }); return r.result ? r.result.value : undefined; };
const sleep = function (ms) { return new Promise(function (r) { setTimeout(r, ms); }); };
const RAIL = "Array.from(document.querySelectorAll('#rail .rail-btn')).filter(function(b){return b.dataset.act==='tools'})";

console.log("railToolsBtn=", await ev("!!" + RAIL + ".length"));
console.log("importBtn=", await ev("!!document.getElementById('import-txt')"));
console.log("shelfCards=", await ev("document.querySelectorAll('#shelf .book-card').length"));
console.log("localBadge=", await ev("(document.querySelector('#shelf .book-card.local .book-badge')||{}).textContent"));
await ev("document.querySelectorAll('#shelf .book-card')[0].click()");
await sleep(1500);
console.log("readerHidden=", await ev("document.getElementById('reader').hidden"));
console.log("blocks=", await ev("document.querySelectorAll('#content .chapter-block').length"));
console.log("tocItems=", await ev("document.querySelectorAll('#toc-list .toc-item').length"));
console.log("topbarTitle=", await ev("document.getElementById('tb-title').textContent"));
console.log("firstParas=", await ev("((document.querySelector('#content .chapter-block')||{}).innerText||'').slice(0,150)"));
await ev(RAIL + "[0].click()");
await sleep(500);
console.log("toolsOpen=", await ev("!document.getElementById('tools-panel').hidden"));
console.log("toolsSub=", await ev("document.getElementById('tools-sub').textContent"));
console.log("txtGroupHidden=", await ev("document.getElementById('txt-group').hidden"));
console.log("dlGroupHidden=", await ev("document.getElementById('dl-group').hidden"));
console.log("txtPath=", await ev("document.getElementById('txt-path').textContent"));
console.log("txtCount=", await ev("document.getElementById('txt-count').textContent"));
console.log("txtEncInfo=", await ev("document.getElementById('txt-enc-info').textContent"));
console.log("txtSamples=", await ev("document.getElementById('txt-samples').innerText"));
await ev("document.getElementById('txt-regex').value='^第(.*)章[ ]'; document.getElementById('txt-rescan').click()");
await sleep(1200);
console.log("afterRescanCount=", await ev("document.getElementById('txt-count').textContent"));
console.log("afterRescanSamples=", await ev("document.getElementById('txt-samples').innerText"));
console.log("toast=", await ev("document.getElementById('toast').textContent"));
console.log("exGroupVisible=", await ev("!document.getElementById('ex-group').hidden"));
console.log("ERRORS=", JSON.stringify(errors));
ws.close();
