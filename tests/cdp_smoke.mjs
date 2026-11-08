const PORT = process.env.SMOKE_PORT || "9334";
const PAGE = process.env.SMOKE_PAGE || "file:///D:/codexproject/reader/tests/web_smoke.html";
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
  } else if (m.method === "Runtime.exceptionThrown") {
    console.log("EXCEPTION " + m.params.exceptionDetails.text);
  }
};
await new Promise((r) => (ws.onopen = r));
await send("Page.enable");
await send("Runtime.enable");
const ev = async (x) => (await send("Runtime.evaluate", { expression: x, returnByValue: true, awaitPromise: true })).result.value;
await send("Page.navigate", { url: PAGE });
let out = null;
for (let i = 0; i < 80; i++) {
  await sleep(500);
  out = await ev('(function(){var e=document.getElementById("smoke-out");return e?e.textContent:null;})()');
  if (out) break;
}
console.log(out || "NO_RESULT");
ws.close();
