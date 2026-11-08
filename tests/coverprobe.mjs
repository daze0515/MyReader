const PORT = process.env.PROBE_PORT || "9336";
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
const expr = "JSON.stringify([].slice.call(document.querySelectorAll('.book-cover')).map(function(n){ if(n.tagName==='IMG') return {tag:'img', src:n.getAttribute('src'), w:n.naturalWidth, complete:n.complete}; return {tag:n.tagName, cls:n.className, text:(n.textContent||'').slice(0,12)}; }))";
console.log("covers:", await ev(expr));
process.exit(0);
