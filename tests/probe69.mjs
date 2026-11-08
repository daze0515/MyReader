const PORT = process.env.PROBE_PORT || "9336";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const list = await (await fetch("http://127.0.0.1:" + PORT + "/json/list")).json();
console.log("targets:");
for (const t of list) console.log("  ", t.type, t.url.slice(0, 120));
const page = list.find((t) => t.type === "page" && /reader[.]local/.test(t.url));
if (!page) { console.log("no reader page"); process.exit(1); }
const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0; const pending = new Map();
const send = (m, p = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method: m, params: p })); });
ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { const q = pending.get(m.id); pending.delete(m.id); m.error ? q.rej(new Error(JSON.stringify(m.error))) : q.res(m.result); } };
await new Promise((r) => (ws.onopen = r));
await send("Runtime.enable");
const ev = async (x) => { const r = await send("Runtime.evaluate", { expression: x, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) return "EXC:" + JSON.stringify(r.exceptionDetails.exception && r.exceptionDetails.exception.description); return r.result ? r.result.value : undefined; };
const q = (s) => JSON.stringify(s);
const url = process.env.URL || "https://www.69shuba.com/book/55199.htm";
console.log("open:", url);
const click = "(function(){var i=document.getElementById(\"url-input\");i.value=" + q(url) + ";i.dispatchEvent(new Event(\"input\",{bubbles:true}));document.getElementById(\"open-btn\").click();return 1;})()";
await ev(click);
for (let i = 0; i < 30; i++) {
  await sleep(2000);
  const st = await ev("(document.getElementById(\"home-status\")||{}).textContent");
  const eh = await ev("(document.getElementById(\"error-bar\")||{}).hidden");
  const et = await ev("(document.getElementById(\"error-bar\")||{}).textContent");
  const blocks = await ev("document.querySelectorAll(\"#content .chapter-block\").length");
  const t = await ev("(document.getElementById(\"tb-title\")||{}).textContent");
  console.log(i, "status=", st, "| err=", eh, et, "| blocks=", blocks, "| title=", t);
  if (blocks > 0) break;
  if (eh === false) break;
}
console.log("--- state ---");
console.log("tb-title=", await ev("(document.getElementById(\"tb-title\")||{}).textContent"));
console.log("tb-meta=", await ev("(document.getElementById(\"tb-meta\")||{}).textContent"));
console.log("tb-progress=", await ev("(document.getElementById(\"tb-progress\")||{}).textContent"));
console.log("blocks=", await ev("document.querySelectorAll(\"#content .chapter-block\").length"));
console.log("titles=", await ev("Array.from(document.querySelectorAll(\"#content .ch-title\")).map(function(e){return e.textContent;}).join(\" / \")"));
console.log("firstParas=", await ev("(document.querySelector(\"#content .chapter-block\")||{innerText:\"\"}).innerText.slice(0,300)"));
console.log("tocSub=", await ev("(document.getElementById(\"toc-sub\")||{}).textContent"));
ws.close();
