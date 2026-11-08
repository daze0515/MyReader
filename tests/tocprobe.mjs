const PORT = process.env.PROBE_PORT || "9336";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const list = await (await fetch("http://127.0.0.1:" + PORT + "/json/list")).json();
const page = list.find((t) => t.type === "page" && /reader[.]local/.test(t.url));
if (!page) { console.log("no reader page"); process.exit(1); }
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0; const pending = new Map();
const send = (m, p = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method: m, params: p })); });
ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { const q = pending.get(m.id); pending.delete(m.id); m.error ? q.rej(new Error(JSON.stringify(m.error))) : q.res(m.result); } };
await new Promise((r) => (ws.onopen = r));
await send("Runtime.enable");
const ev = async (x) => { const r = await send("Runtime.evaluate", { expression: x, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) return "EXC:" + JSON.stringify(r.exceptionDetails); return r.result ? r.result.value : undefined; };
const clickCard = "(function(){var im=document.querySelectorAll('#shelf img.book-cover');for(var i=0;i<im.length;i++){if((im[i].getAttribute('src')||'').indexOf('67581')>=0){im[i].closest('.book-card').click();return i;}}return -1;})()";
console.log("clickCard:", await ev(clickCard));
let blocks = 0;
for (let i = 0; i < 30; i++) { await sleep(1000); blocks = await ev("document.querySelectorAll('#content .chapter-block').length"); const eh = await ev("(document.getElementById('error-bar')||{}).hidden"); if (blocks > 0) break; if (eh === false) { console.log("err:", await ev("document.getElementById('error-bar').textContent")); break; } }
console.log("blocks:", blocks);
await ev("(document.querySelector('#rail .rail-btn[data-act=toc]')||{}).click()");
await sleep(600);
const m = "JSON.stringify({rowH:getComputedStyle(document.documentElement).getPropertyValue('--toc-row-h'), inner:(document.querySelector('.toc-inner')||{}).offsetHeight, items:document.querySelectorAll('#toc-list .toc-item').length, scrollH:(document.getElementById('toc-list')||{}).scrollHeight, clientH:(document.getElementById('toc-list')||{}).clientHeight, total:(document.getElementById('toc-sub')||{}).textContent})";
console.log("toc:", await ev(m));
process.exit(0);
