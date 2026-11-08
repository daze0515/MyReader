const PORT = process.env.PROBE_PORT || "9336";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const list = await (await fetch("http://127.0.0.1:" + PORT + "/json/list")).json();
const page = list.find((t) => t.type === "page" && /reader[.]local/.test(t.url));
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0; const pending = new Map();
const send = (m, p = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method: m, params: p })); });
ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { const q = pending.get(m.id); pending.delete(m.id); m.error ? q.rej(new Error(JSON.stringify(m.error))) : q.res(m.result); } };
await new Promise((r) => (ws.onopen = r));
await send("Page.enable"); await send("Runtime.enable");
const ev = async (x) => { const r = await send("Runtime.evaluate", { expression: x, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) return "EXC:" + (r.exceptionDetails.exception && r.exceptionDetails.exception.description); return r.result ? r.result.value : undefined; };
const img = process.env.IMG;
await send("Page.navigate", { url: img });
for (let i = 0; i < 20; i++) { await sleep(700); const t = await ev("document.readyState + '|' + document.contentType + '|imgs=' + document.images.length + '|cw=' + (document.images[0]?document.images[0].naturalWidth:-1)"); console.log(i, t); if (String(t).indexOf("complete") === 0 && String(t).indexOf("cw=-1") < 0) break; }
const snippet = "(function(){var i=document.images[0];if(!i)return 'noimg';try{var c=document.createElement('canvas');c.width=i.naturalWidth;c.height=i.naturalHeight;var g=c.getContext('2d');g.drawImage(i,0,0);var u=c.toDataURL('image/jpeg',0.9);return 'ok len='+u.length+' head='+u.slice(0,30);}catch(e){return 'ERR:'+e.message;}})()";
console.log("canvas:", await ev(snippet));
await send("Page.navigate", { url: "https://reader.local/index.html" });
ws.close();
