/* reader.js — 阅读器前端：首页 / 阅读页 / 目录 / 设置 / 键盘滚动 */
(function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const host = (window.chrome && window.chrome.webview) || null;
  const post = (o) => { if (host) host.postMessage(o); };

  /* ================= 设置 ================= */
  const DEFAULTS = {
    theme: "sepia",
    customBg: "#f5efe1",
    fontFamily: "",
    fontSize: 19,
    fontWeight: 400,
    lineHeight: 1.9,
    paraSpacing: 0.9,
    letterSpacing: 0.02,
    pageWidth: 40,
    indent: true,
    scrollStepUnit: "line",
    scrollStep: 3,
    simplified: true,
    requestInterval: 1.2,
    downloadGap: 2,
    proxyMode: "system",
    proxyManual: ""
  };
  const settings = Object.assign({}, DEFAULTS);
  let lastSimplified = settings.simplified;
  let lastTheme = settings.theme;

  const FONTS = [
    ["", "系统默认"],
    ['"Microsoft YaHei", "微软雅黑", "PingFang SC", sans-serif', "微软雅黑"],
    ['"Source Han Serif SC", "Noto Serif SC", "Songti SC", SimSun, serif', "思源宋体"],
    ['SimSun, "宋体", "Songti SC", serif', "宋体"],
    ['"KaiTi", "楷体", STKaiti, serif', "楷体"],
    ['"FangSong", "仿宋", STFangsong, serif', "仿宋"],
    ['"LXGW WenKai", "霞鹜文楷", "KaiTi", serif', "霞鹜文楷"],
    ['"HarmonyOS Sans SC", "Microsoft YaHei", sans-serif', "鸿蒙黑体"],
    ['Georgia, "Times New Roman", "Songti SC", serif', "衬线西文"]
  ];

  const OUT_MAP = {
    "font-size": "fontSize",
    "font-weight": "fontWeight",
    "letter-spacing": "letterSpacing",
    "line-height": "lineHeight",
    "para-spacing": "paraSpacing",
    "page-width": "pageWidth",
    "scroll-step": "scrollStep",
    "request-interval": "requestInterval"
  };
  const OUT_FMT = {
    "font-size": (v) => v + "px",
    "font-weight": (v) => String(v),
    "letter-spacing": (v) => Number(v).toFixed(2) + "em",
    "line-height": (v) => Number(v).toFixed(2),
    "para-spacing": (v) => Number(v).toFixed(2) + "em",
    "page-width": (v) => v + "em",
    "scroll-step": (v) => settings.scrollStepUnit === "px" ? (v + " px") : (v + " 行"),
    "request-interval": (v) => Number(v).toFixed(1) + " 秒"
  };

  function hexToRgb(h) {
    const m = /^#?([0-9a-f]{6})$/i.exec(String(h || "").trim());
    if (!m) return null;
    const n = parseInt(m[1], 16);
    return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
  }

  /* ================= 元素 ================= */
  const homeEl = $("home");
  const readerEl = $("reader");
  const viewport = $("viewport");
  const contentEl = $("content");
  const loadingEl = $("loading");
  const errorEl = $("error-bar");
  const toastEl = $("toast");
  const dimEl = $("dim");
  const tocDrawer = $("toc-drawer");
  const setDrawer = $("settings-panel");
  const toolsPanel = $("tools-panel");

  /* ================= 通用 UI ================= */
  let toastTimer = 0;
  function showToast(text, ms) {
    toastEl.textContent = text;
    toastEl.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { toastEl.hidden = true; }, ms || 2600);
  }

  let errorTimer = 0;
  function showError(text) {
    errorEl.innerHTML = "";
    errorEl.appendChild(document.createTextNode(text));
    const btn = document.createElement("button");
    btn.textContent = "重新验证";
    btn.style.cssText = "margin-left:12px;background:rgba(255,255,255,.2);border:none;color:#fff;border-radius:6px;padding:3px 10px;font:inherit;cursor:pointer";
    btn.addEventListener("click", () => { errorEl.hidden = true; post({ type: "retryVerify" }); });
    errorEl.appendChild(btn);
    errorEl.hidden = false;
    clearTimeout(errorTimer);
    errorTimer = setTimeout(() => { errorEl.hidden = true; }, 9000);
  }

  function showScreen(name) {
    homeEl.hidden = name !== "home";
    readerEl.hidden = name !== "reader";
    if (name === "home") document.body.classList.remove("immersive");
  }

  function drawerOpen() { return !tocDrawer.hidden || !setDrawer.hidden || !toolsPanel.hidden; }
  function closeDrawers() {
    tocDrawer.hidden = true;
    setDrawer.hidden = true;
    toolsPanel.hidden = true;
    dimEl.hidden = true;
  }
  function openDrawer(el) {
    closeDrawers();
    el.hidden = false;
    dimEl.hidden = false;
  }

  /* ================= 阅读状态 ================= */
  let book = null;
  let reading = false;
  let cur = 0;
  let curRatio = 0;
  const blocks = new Map();
  let lo = -1, hi = -1;
  const mem = new Map();
  const MEM_MAX = 60;
  const inflight = new Set();
  let pendingResume = -1;
  let pendingResumeRatio = 0;
  let pendingPrev = -1;
  let lastTop = 0;
  let endNote = null;
  let tocQuery = "";

  function memGet(i) {
    const v = mem.get(i);
    if (v) { mem.delete(i); mem.set(i, v); }
    return v || null;
  }
  function memSet(i, title, paras, cached) {
    mem.delete(i);
    mem.set(i, { title: title, paras: paras, cached: !!cached });
    while (mem.size > MEM_MAX) mem.delete(mem.keys().next().value);
  }

  function blockOffset(i) {
    const b = blocks.get(i);
    return b ? b.el.offsetTop - contentEl.offsetTop : 0;
  }
  function blockHeight(i) {
    const b = blocks.get(i);
    return b ? b.el.offsetHeight : 0;
  }

  function mkBlock(i) {
    const el = document.createElement("section");
    el.className = "chapter-block";
    el.dataset.index = String(i);
    el.style.overflowAnchor = "none";
    const b = { el: el, state: "idle", collapsed: false };
    blocks.set(i, b);
    return b;
  }

  // 写入内容：若改动发生在视口上方，补偿滚动位置
  function setBlockContent(b, i, build) {
    const above = i < cur;
    const st = viewport.scrollTop;
    const before = above ? viewport.scrollHeight : 0;
    b.el.innerHTML = "";
    build(b.el);
    if (above) {
      const d = viewport.scrollHeight - before;
      if (d) viewport.scrollTop = st + d;
    }
  }

  function conv(text) {
    if (!settings.simplified || typeof toSimplified !== "function") return text;
    return toSimplified(text);
  }

  function chapterHead(i, title, cached) {
    const head = document.createElement("div");
    head.className = "chapter-head";
    const h2 = document.createElement("h2");
    h2.textContent = conv(String(title || (book && book.chapters[i]) || ("第 " + (i + 1) + " 章")));
    head.appendChild(h2);
    const meta = document.createElement("div");
    meta.className = "ch-meta";
    meta.textContent = "第 " + (i + 1) + " / " + (book ? book.total : "?") + " 章";
    if (cached) {
      const chip = document.createElement("span");
      chip.className = "ch-chip";
      chip.textContent = "缓存";
      chip.title = "本章来自本地缓存，未发起网络请求";
      meta.appendChild(chip);
    }
    head.appendChild(meta);
    return head;
  }

  function chapterBody(paras) {
    const body = document.createElement("div");
    body.className = "chapter-body";
    const frag = document.createDocumentFragment();
    for (let k = 0; k < paras.length; k++) {
      const p = document.createElement("p");
      p.textContent = conv(paras[k]);
      frag.appendChild(p);
    }
    body.appendChild(frag);
    return body;
  }

  function paintBlock(b, i, title, paras, cached) {
    setBlockContent(b, i, (el) => {
      el.appendChild(chapterHead(i, title, cached));
      el.appendChild(chapterBody(paras));
    });
    b.state = "ok";
  }

  function paintPlaceholder(b, i) {
    setBlockContent(b, i, (el) => {
      const d = document.createElement("div");
      d.className = "ch-ph";
      d.textContent = "正在加载 第 " + (i + 1) + " 章 …";
      el.appendChild(d);
    });
  }

  function paintError(b, i, message) {
    setBlockContent(b, i, (el) => {
      const d = document.createElement("div");
      d.className = "ch-ph ch-err";
      d.textContent = message;
      const btn = document.createElement("button");
      btn.className = "retry-btn";
      btn.textContent = "重试";
      btn.addEventListener("click", () => { b.state = "idle"; ensureLoaded(i); });
      const re = document.createElement("button");
      re.className = "retry-btn";
      re.textContent = "重新验证";
      re.addEventListener("click", () => {
        showToast("正在重新验证，请在页面中完成人机验证", 3000);
        post({ type: "retryVerify" });
      });
      el.appendChild(d);
      el.appendChild(btn);
      el.appendChild(re);
    });
    b.state = "error";
  }

  function request(i) {
    if (inflight.has(i)) return;
    inflight.add(i);
    post({ type: "needChapter", index: i });
    updateLoading();
  }

  function ensureLoaded(i) {
    const b = blocks.get(i);
    if (!b || b.state === "ok") return;
    const c = memGet(i);
    if (c) { paintBlock(b, i, c.title, c.paras, c.cached); return; }
    b.state = "loading";
    paintPlaceholder(b, i);
    request(i);
  }

  function updateLoading() {
    loadingEl.hidden = !(reading && inflight.size > 0);
  }

  function updateEndNote() {
    const atEnd = !!(book && hi === book.total - 1 && blocks.get(hi) && blocks.get(hi).state === "ok");
    if (atEnd && !endNote) {
      endNote = document.createElement("div");
      endNote.className = "book-end";
      endNote.textContent = "— 全书完 —";
      contentEl.appendChild(endNote);
    } else if (!atEnd && endNote) {
      endNote.remove();
      endNote = null;
    }
  }

  /* ---------------- 章节装载 ---------------- */
  function resetReader() {
    blocks.forEach((b) => b.el.remove());
    blocks.clear();
    mem.clear();
    inflight.clear();
    lo = hi = -1;
    pendingResume = -1;
    pendingPrev = -1;
    endNote = null;
    contentEl.innerHTML = "";
    errorEl.hidden = true;
  }

  function startReading(index, ratio) {
    resetReader();
    reading = true;
    lo = hi = index;
    cur = index;
    curRatio = 0;
    pendingResume = index;
    pendingResumeRatio = ratio > 0 ? ratio : 0;
    const b = mkBlock(index);
    contentEl.appendChild(b.el);
    viewport.scrollTop = 0;
    lastTop = 0;
    ensureLoaded(index);
    updateTopbar();
    markActiveToc();
    updateLoading();
    updateEndNote();
    pokeUi();
  }

  function appendNext() {
    if (!book || hi >= book.total - 1) return;
    const tail = blocks.get(hi);
    if (tail && tail.state === "loading") return;
    const i = hi + 1;
    hi = i;
    const b = mkBlock(i);
    contentEl.appendChild(b.el);
    ensureLoaded(i);
  }

  function insertPrev(i, title, paras) {
    const st = viewport.scrollTop;
    const before = viewport.scrollHeight;
    const b = mkBlock(i);
    b.el.appendChild(chapterHead(i, title));
    b.el.appendChild(chapterBody(paras));
    b.state = "ok";
    contentEl.insertBefore(b.el, contentEl.firstChild);
    lo = i;
    const d = viewport.scrollHeight - before;
    if (d) viewport.scrollTop = st + d;
  }

  function prependPrev() {
    if (!book || lo <= 0 || pendingPrev >= 0) return;
    const i = lo - 1;
    const c = memGet(i);
    if (c) { insertPrev(i, c.title, c.paras); return; }
    if (inflight.has(i)) return;
    pendingPrev = i;
    showToast("正在加载上一章…", 1500);
    request(i);
  }

  /* ---------------- 折叠 / 展开 ---------------- */
  function collapseBlock(i) {
    const b = blocks.get(i);
    if (!b || b.collapsed || b.state !== "ok") return;
    const h = b.el.offsetHeight;
    b.el.style.height = h + "px";
    b.el.style.overflow = "hidden";
    b.el.innerHTML = "";
    b.collapsed = true;
    b.state = "collapsed";
  }

  function expandBlock(i) {
    const b = blocks.get(i);
    if (!b || !b.collapsed) return;
    b.collapsed = false;
    b.el.style.height = "";
    b.el.style.overflow = "";
    b.state = "idle";
    ensureLoaded(i);
  }

  /* ================= 进度 ================= */
  function overallPct() {
    if (!book || !book.total) return 0;
    return Math.min(100, Math.round(((cur + curRatio) / book.total) * 100));
  }

  function updateTopbar() {
    if (!book) return;
    const title = conv(String(book.chapters[cur] || "").trim() || ("第 " + (cur + 1) + " 章"));
    $("tb-title").textContent = title;
    const m = book.meta || {};
    const parts = [];
    if (m.title) parts.push(conv(m.title));
    if (m.author) parts.push(m.author);
    parts.push("第 " + (cur + 1) + " / " + book.total + " 章");
    $("tb-meta").textContent = parts.join(" · ");
    $("tb-progress").textContent = overallPct() + "%";
  }

  let progTimer = 0;
  function sendProgress() {
    if (!reading || !book) return;
    post({ type: "progress", index: cur, ratio: Math.round(curRatio * 1000) / 1000 });
  }
  function scheduleProgress(immediate) {
    clearTimeout(progTimer);
    if (immediate) sendProgress();
    progTimer = setTimeout(sendProgress, 500);
  }

  let titleTimer = 0;
  function updateTitle() {
    clearTimeout(titleTimer);
    titleTimer = setTimeout(() => {
      if (!reading || !book) { post({ type: "title", text: "阅读器" }); return; }
      const name = conv((book.meta && book.meta.title) || "阅读");
      post({ type: "title", text: name + " · 第" + (cur + 1) + "章 · " + overallPct() + "%" });
    }, 260);
  }

  /* ================= 滚动 ================= */
  let scrollRaf = 0;
  function onScroll() {
    if (scrollRaf) return;
    scrollRaf = requestAnimationFrame(() => { scrollRaf = 0; handleScroll(); });
  }

  function handleScroll() {
    if (!reading || !book) return;
    const top = viewport.scrollTop;
    const vh = viewport.clientHeight;
    const total = book.total;

    let ci = lo;
    for (let i = lo; i <= hi; i++) {
      if (!blocks.has(i)) continue;
      if (blockOffset(i) <= top + 2) ci = i; else break;
    }
    const h = Math.max(1, blockHeight(ci));
    let r = (top - blockOffset(ci)) / h;
    if (!(r >= 0)) r = 0;
    if (r > 1) r = 1;
    const changed = ci !== cur;
    cur = ci;
    curRatio = r;

    for (let i = lo; i <= hi; i++) {
      if (i < ci - 3) collapseBlock(i);
      else expandBlock(i);
    }

    if (hi < total - 1 && viewport.scrollHeight - (top + vh) < vh * 1.5) appendNext();
    if (lo > 0 && top < vh * 0.9 && top < lastTop) prependPrev();

    if (changed) {
      updateTopbar();
      markActiveToc();
      scheduleProgress(true);
    } else {
      $("tb-progress").textContent = overallPct() + "%";
      scheduleProgress(false);
    }
    updateTitle();
    lastTop = viewport.scrollTop;
  }

  // 已经贴到顶部时再按向上，直接取上一章
  function wantPrevIfTop() {
    if (lo > 0 && viewport.scrollTop <= 0.5) prependPrev();
  }

  let anim = null;
  function stopAnim() {
    if (anim) { cancelAnimationFrame(anim.raf); anim = null; }
  }
  function scrollToPos(target, dur) {
    stopAnim();
    const max = Math.max(0, viewport.scrollHeight - viewport.clientHeight);
    const start = viewport.scrollTop;
    target = Math.max(0, Math.min(max, target));
    if (Math.abs(target - start) < 0.5) return;
    if (!dur || dur <= 0) { viewport.scrollTop = target; return; }
    const t0 = performance.now();
    const a = { raf: 0 };
    anim = a;
    const tick = (t) => {
      if (anim !== a) return;
      const k = Math.min(1, (t - t0) / dur);
      const e = 1 - Math.pow(1 - k, 3);
      viewport.scrollTop = start + (target - start) * e;
      if (k < 1) a.raf = requestAnimationFrame(tick);
      else anim = null;
    };
    a.raf = requestAnimationFrame(tick);
  }

  function stepPx() {
    if (settings.scrollStepUnit === "px") return Math.max(1, settings.scrollStep);
    const p = contentEl.querySelector(".chapter-body p");
    let lh = 0;
    if (p) lh = parseFloat(getComputedStyle(p).lineHeight);
    if (!lh || !isFinite(lh)) lh = settings.fontSize * settings.lineHeight;
    return Math.max(1, Math.round(lh * settings.scrollStep));
  }
  function scrollByStep(dir) { scrollToPos(viewport.scrollTop + dir * stepPx(), 130); }
  function pageSize() { return Math.max(80, Math.round(viewport.clientHeight * 0.92)); }

  function gotoChapter(i, ratio) {
    if (!book) return;
    if (i < 0) i = 0;
    if (i > book.total - 1) i = book.total - 1;
    if (i === cur && blocks.has(i)) {
      scrollToPos(blockOffset(i) + (ratio || 0) * blockHeight(i), 160);
      return;
    }
    startReading(i, ratio || 0);
    scheduleProgress(true);
  }

  /* ================= 键盘 ================= */
  function onKey(e) {
    if (e.ctrlKey && e.key === ",") {
      e.preventDefault();
      openDrawer(setDrawer);
      return;
    }
    const t = e.target;
    if (t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA")) {
      if (e.key === "Escape") t.blur();
      return;
    }
    if (readerEl.hidden) return;
    if (e.altKey || e.ctrlKey || e.metaKey) return;

    const vh = pageSize();
    switch (e.key) {
      case "ArrowDown": e.preventDefault(); scrollByStep(1); break;
      case "ArrowUp": e.preventDefault(); scrollByStep(-1); wantPrevIfTop(); break;
      case " ":
      case "Spacebar":
        e.preventDefault();
        scrollToPos(viewport.scrollTop + (e.shiftKey ? -vh : vh), 170);
        if (e.shiftKey) wantPrevIfTop();
        break;
      case "PageDown": e.preventDefault(); scrollToPos(viewport.scrollTop + vh, 170); break;
      case "PageUp": e.preventDefault(); scrollToPos(viewport.scrollTop - vh, 170); wantPrevIfTop(); break;
      case "ArrowRight": e.preventDefault(); gotoChapter(cur + 1, 0); break;
      case "ArrowLeft": e.preventDefault(); gotoChapter(cur - 1, 0); break;
      case "Home": e.preventDefault(); scrollToPos(blockOffset(cur), 160); break;
      case "End":
        e.preventDefault();
        scrollToPos(blockOffset(cur) + blockHeight(cur) - viewport.clientHeight, 160);
        break;
      case "F11": e.preventDefault(); post({ type: "fullscreen" }); break;
      case "Escape":
        if (drawerOpen()) closeDrawers();
        else if (document.body.classList.contains("immersive")) pokeUi();
        break;
      default: return;
    }
    pokeUi();
  }

  let idleTimer = 0;
  function pokeUi() {
    document.body.classList.remove("immersive");
    clearTimeout(idleTimer);
    if (!reading) return;
    idleTimer = setTimeout(() => {
      if (!drawerOpen()) document.body.classList.add("immersive");
    }, 2600);
  }
  window.addEventListener("mousemove", pokeUi);

  /* ================= 目录（虚拟滚动） ================= */
  // 上千章一次性建 DOM 会卡，所以只渲染可视区那几十行：
  // .toc-inner 只负责撑出总高度，行按固定行高绝对定位。
  let tocRendered = false;
  let tocInner = null;
  let tocFiltered = [];      // 过滤后的章号（0-based）
  let tocTitles = [];        // 过滤用标题（已转简体 / 已 trim），按书缓存
  let tocTitlesKey = "";
  let tocWinLo = -1, tocWinHi = -1;
  let tocRowH = 0;
  const TOC_PAD = 6;         // 上下各多画几行，滚动时不露白

  function tocRow() {
    if (!tocRowH) {
      tocRowH = parseFloat(
        getComputedStyle(document.documentElement).getPropertyValue("--toc-row-h")) || 38;
    }
    return tocRowH;
  }

  // 标题只在换书 / 切换简繁时重算一次，搜索每敲一个字都重算会卡
  function ensureTocTitles() {
    const total = book ? book.total : 0;
    const key = total + "|" + (settings.simplified ? 1 : 0);
    if (key === tocTitlesKey && tocTitles.length === total) return;
    tocTitlesKey = key;
    tocTitles = [];
    for (let i = 0; i < total; i++) {
      tocTitles.push(conv(String(book.chapters[i] || "").trim() || ("第 " + (i + 1) + " 章")));
    }
  }

  function renderToc() {
    const list = $("toc-list");
    if (!book) { list.innerHTML = ""; tocRendered = false; tocInner = null; return; }
    ensureTocTitles();
    const q = tocQuery.trim().toLowerCase();
    tocFiltered = [];
    for (let i = 0; i < tocTitles.length; i++) {
      if (!q || tocTitles[i].toLowerCase().indexOf(q) >= 0) tocFiltered.push(i);
    }
    list.innerHTML = "";
    tocInner = document.createElement("div");
    tocInner.className = "toc-inner";
    tocInner.style.height = (tocFiltered.length * tocRow()) + "px";
    list.appendChild(tocInner);
    tocRendered = true;
    tocWinLo = tocWinHi = -1;
    paintTocWindow(true);
    markActiveToc();
  }

  function paintTocWindow(force) {
    if (!tocRendered || !tocInner) return;
    const list = $("toc-list");
    const rowH = tocRow();
    const top = list.scrollTop;
    // 抽屉还没显示出来时 clientHeight 是 0，给个兜底值，免得只画两三行
    const vh = list.clientHeight || 900;
    const lo = Math.max(0, Math.floor(top / rowH) - TOC_PAD);
    const hi = Math.min(tocFiltered.length - 1, Math.ceil((top + vh) / rowH) + TOC_PAD);
    if (!force && lo === tocWinLo && hi === tocWinHi) return;
    tocWinLo = lo;
    tocWinHi = hi;
    const frag = document.createDocumentFragment();
    for (let k = lo; k <= hi; k++) {
      const i = tocFiltered[k];
      const row = document.createElement("div");
      row.className = "toc-item";
      row.dataset.index = String(i);
      row.style.top = (k * rowH) + "px";
      if (i === cur) row.classList.add("active");
      if (i < cur) row.classList.add("read");
      const tx = document.createElement("span");
      tx.className = "ti-text";
      tx.textContent = tocTitles[i];
      const num = document.createElement("span");
      num.className = "ti-num";
      num.textContent = String(i + 1);
      row.appendChild(tx);
      row.appendChild(num);
      frag.appendChild(row);
    }
    tocInner.innerHTML = "";
    tocInner.appendChild(frag);
  }

  function markActiveToc() {
    if (!tocRendered) return;
    const list = $("toc-list");
    const rowH = tocRow();
    const k = tocFiltered.indexOf(cur);
    if (k >= 0) {
      const vh = list.clientHeight || 900;
      const rowTop = k * rowH;
      if (rowTop < list.scrollTop || rowTop + rowH > list.scrollTop + vh) {
        list.scrollTop = Math.max(0, rowTop - vh / 2 + rowH / 2);
      }
    }
    paintTocWindow(true);
  }

  /* ================= 设置面板 ================= */
  function configStepRange() {
    const el = $("scroll-step");
    if (settings.scrollStepUnit === "px") {
      el.min = "20"; el.max = "2000"; el.step = "20";
      if (settings.scrollStep < 20) settings.scrollStep = 20;
    } else {
      el.min = "1"; el.max = "20"; el.step = "1";
      if (settings.scrollStep > 20) settings.scrollStep = 20;
    }
    el.value = String(settings.scrollStep);
  }

  function syncPanel() {
    const swatches = document.querySelectorAll("#theme-list .swatch");
    for (let i = 0; i < swatches.length; i++) {
      swatches[i].classList.toggle("active", swatches[i].dataset.themeValue === settings.theme);
    }
    $("custom-bg-row").hidden = settings.theme !== "custom";
    $("custom-bg").value = settings.customBg;
    $("font-family").value = settings.fontFamily;
    $("font-size").value = settings.fontSize;
    $("font-weight").value = settings.fontWeight;
    $("letter-spacing").value = settings.letterSpacing;
    $("line-height").value = settings.lineHeight;
    $("para-spacing").value = settings.paraSpacing;
    $("page-width").value = settings.pageWidth;
    $("indent").checked = !!settings.indent;
    $("simplified").checked = !!settings.simplified;
    $("scroll-step-unit").value = settings.scrollStepUnit;
    $("request-interval").value = settings.requestInterval;
    configStepRange();
    Object.keys(OUT_MAP).forEach((id) => {
      const out = $(id + "-out");
      if (out) out.textContent = OUT_FMT[id](settings[OUT_MAP[id]]);
    });
    $("proxy-mode").value = settings.proxyMode;
    $("proxy-manual").value = settings.proxyManual;
  }

  function readAnchor() { return { index: cur, ratio: curRatio }; }
  function restoreAnchor(a) {
    if (!reading || !book || !blocks.has(a.index)) return;
    viewport.scrollTop = blockOffset(a.index) + a.ratio * blockHeight(a.index);
  }

  function repaint() {
    blocks.forEach((b, i) => {
      if (b.collapsed || b.state !== "ok") return;
      const c = memGet(i);
      if (c) paintBlock(b, i, c.title, c.paras, c.cached);
    });
  }

  function applySettings(opts) {
    const anchor = readAnchor();
    const root = document.documentElement.style;
    document.body.dataset.theme = settings.theme;
    if (settings.theme === "custom") {
      const c = hexToRgb(settings.customBg) || [245, 239, 225];
      const lum = (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 255;
      root.setProperty("--bg", settings.customBg);
      root.setProperty("--fg", lum > 0.55 ? "#22262b" : "#d3d6db");
    } else {
      root.removeProperty("--bg");
      root.removeProperty("--fg");
    }
    root.setProperty("--fs", settings.fontSize + "px");
    root.setProperty("--fw", String(settings.fontWeight));
    root.setProperty("--lh", String(settings.lineHeight));
    root.setProperty("--ps", settings.paraSpacing + "em");
    root.setProperty("--ls", settings.letterSpacing + "em");
    root.setProperty("--pw", String(settings.pageWidth));
    document.body.classList.toggle("indent", !!settings.indent);
    contentEl.style.fontFamily = settings.fontFamily || "";

    if (settings.simplified !== lastSimplified) {
      lastSimplified = settings.simplified;
      repaint();
    }
    syncPanel();
    if (!opts || opts.anchor !== false) requestAnimationFrame(() => restoreAnchor(anchor));
  }

  let saveTimer = 0;
  function pushSettings(now) {
    clearTimeout(saveTimer);
    if (now) {
      post({ type: "setSetting", value: Object.assign({}, settings) });
      return;
    }
    saveTimer = setTimeout(() => {
      post({ type: "setSetting", value: Object.assign({}, settings) });
    }, 220);
  }

  /* ================= 首页 ================= */
  function coverPh(it) {
    const d = document.createElement("div");
    d.className = "book-cover";
    d.textContent = String(it.title || "书").slice(0, 1);
    d.style.display = "flex";
    d.style.alignItems = "center";
    d.style.justifyContent = "center";
    d.style.fontSize = "16px";
    d.style.color = "var(--muted)";
    return d;
  }

  function renderShelf(items) {
    const shelf = $("shelf");
    shelf.innerHTML = "";
    if (!items || !items.length) {
      const d = document.createElement("div");
      d.className = "shelf-empty";
      d.textContent = "书架还是空的，粘贴目录页网址开始阅读。";
      shelf.appendChild(d);
      return;
    }
    items.forEach((it) => {
      const card = document.createElement("div");
      card.className = "book-card" + (it.kind === "local" ? " local" : "");
      if (it.cover) {
        const img = document.createElement("img");
        img.className = "book-cover";
        img.referrerPolicy = "no-referrer";
        img.loading = "lazy";
        img.src = it.cover;
        img.addEventListener("error", () => { img.replaceWith(coverPh(it)); });
        card.appendChild(img);
      } else {
        card.appendChild(coverPh(it));
      }
      const info = document.createElement("div");
      info.className = "book-info";
      const t = document.createElement("div");
      t.className = "book-title";
      t.textContent = conv(it.title || "未命名");
      if (it.kind === "local") {
        const badge = document.createElement("span");
        badge.className = "book-badge";
        badge.textContent = "本地";
        badge.title = it.localPath || "";
        t.appendChild(badge);
      }
      const s = document.createElement("div");
      s.className = "book-sub";
      const idx = it.chapterIndex || 0;
      const pct = it.total ? Math.min(100, Math.round(((idx + (it.ratio || 0)) / it.total) * 100)) : 0;
      s.textContent = [(it.author || "未知作者"), "读到第 " + (idx + 1) + " 章", pct + "%"].join(" · ");
      info.appendChild(t);
      info.appendChild(s);
      card.appendChild(info);
      const del = document.createElement("button");
      del.className = "book-del";
      del.textContent = "✕";
      del.title = "从书架移除";
      del.addEventListener("click", (e) => {
        e.stopPropagation();
        post({ type: "removeBook", bookId: it.bookId });
      });
      card.appendChild(del);
      card.addEventListener("click", () => post({ type: "openBook", bookId: it.bookId }));
      shelf.appendChild(card);
    });
  }

  function openFromInput() {
    const v = $("url-input").value.trim();
    if (!v) { showToast("请输入目录页网址"); return; }
    post({ type: "open", url: v });
  }

  function goHome() {
    sendProgress();
    reading = false;
    post({ type: "title", text: "阅读器" });
    post({ type: "listBooks" });
    showScreen("home");
    closeDrawers();
    loadingEl.hidden = true;
    stopAnim();
  }

  function bindHome() {
    $("open-btn").addEventListener("click", openFromInput);
    $("import-txt").addEventListener("click", () => post({ type: "importTxt" }));
    $("url-input").addEventListener("keydown", (e) => { if (e.key === "Enter") openFromInput(); });
    $("refresh-shelf").addEventListener("click", () => post({ type: "listBooks" }));
  }

  function bindReader() {
    $("rail").addEventListener("click", (e) => {
      const btn = e.target.closest(".rail-btn");
      if (!btn) return;
      const act = btn.dataset.act;
      if (act === "toc") {
        // 抽屉显示出来才有真实高度，所以开完再按当前章对齐一次
        if (tocDrawer.hidden) { renderToc(); openDrawer(tocDrawer); markActiveToc(); }
        else closeDrawers();
      } else if (act === "settings") {
        if (setDrawer.hidden) { syncPanel(); openDrawer(setDrawer); }
        else closeDrawers();
      } else if (act === "theme") {
        if (settings.theme === "dark") {
          settings.theme = (lastTheme && lastTheme !== "dark") ? lastTheme : "sepia";
        } else {
          lastTheme = settings.theme;
          settings.theme = "dark";
        }
        applySettings();
        pushSettings();
      } else if (act === "tools") {
        if (toolsPanel.hidden) { syncTools(); openDrawer(toolsPanel); }
        else closeDrawers();
      } else if (act === "fullscreen") {
        post({ type: "fullscreen" });
      } else if (act === "home") {
        goHome();
      }
    });

    dimEl.addEventListener("click", closeDrawers);
    document.querySelectorAll("[data-close]").forEach((b) => {
      b.addEventListener("click", () => { $(b.dataset.close).hidden = true; dimEl.hidden = true; });
    });

    let tocTimer = 0;
    $("toc-search").addEventListener("input", (e) => {
      clearTimeout(tocTimer);
      const v = e.target.value;
      tocTimer = setTimeout(() => { tocQuery = v; renderToc(); }, 120);
    });
    $("toc-list").addEventListener("click", (e) => {
      const row = e.target.closest(".toc-item");
      if (!row) return;
      gotoChapter(Number(row.dataset.index), 0);
      closeDrawers();
    });
    // 虚拟滚动：滚动时只换窗口里的那几十行
    $("toc-list").addEventListener("scroll", () => paintTocWindow(false));
    window.addEventListener("resize", () => paintTocWindow(true));
  }

  /* ================= 设置绑定 ================= */
  function bindPanel() {
    document.querySelectorAll("#theme-list .swatch").forEach((btn) => {
      btn.addEventListener("click", () => {
        settings.theme = btn.dataset.themeValue;
        if (settings.theme !== "dark") lastTheme = settings.theme;
        applySettings();
        pushSettings();
      });
    });

    $("custom-bg").addEventListener("input", (e) => {
      settings.customBg = e.target.value;
      settings.theme = "custom";
      lastTheme = "custom";
      applySettings();
      pushSettings();
    });

    const fontSel = $("font-family");
    FONTS.forEach((f) => {
      const o = document.createElement("option");
      o.value = f[0];
      o.textContent = f[1];
      fontSel.appendChild(o);
    });
    fontSel.addEventListener("change", (e) => {
      settings.fontFamily = e.target.value;
      applySettings();
      pushSettings();
    });

    Object.keys(OUT_MAP).forEach((id) => {
      $(id).addEventListener("input", (e) => {
        settings[OUT_MAP[id]] = Number(e.target.value);
        applySettings();
        pushSettings();
      });
    });

    $("indent").addEventListener("change", (e) => {
      settings.indent = e.target.checked;
      applySettings();
      pushSettings();
    });
    $("simplified").addEventListener("change", (e) => {
      settings.simplified = e.target.checked;
      applySettings();
      pushSettings();
    });
    $("scroll-step-unit").addEventListener("change", (e) => {
      settings.scrollStepUnit = e.target.value;
      settings.scrollStep = e.target.value === "px" ? 120 : 3;
      applySettings();
      pushSettings();
    });
    $("proxy-mode").addEventListener("change", (e) => {
      settings.proxyMode = e.target.value;
      pushSettings();
      showToast("代理设置已保存，重启程序后生效");
    });
    $("proxy-manual").addEventListener("change", (e) => {
      settings.proxyManual = e.target.value.trim();
      pushSettings();
      showToast("代理设置已保存，重启程序后生效");
    });
  }

  /* ================= 工具：本地 TXT / 离线下载 / 导出 ================= */
  /* 离线下载：提示「哪些章已经在本地了」 */
  let cacheRanges = [];   // [[from, to], ...]，1-based，由宿主推送

  function fmtRange(a, b) { return a === b ? String(a) : a + "–" + b; }

  function cachedIn(from, to) {
    let n = 0;
    for (const r of cacheRanges) {
      const a = Math.max(from, r[0]);
      const b = Math.min(to, r[1]);
      if (b >= a) n += b - a + 1;
    }
    return n;
  }

  function selectedSpan() {
    const total = book ? book.total : 0;
    if (!total) return null;
    const scope = $("dl-scope").value;
    if (scope === "all") return [1, total];
    if (scope === "next") {
      const from = Math.min(total, cur + 1);
      return [from, Math.min(total, from + (Number($("dl-next").value) || 50) - 1)];
    }
    const from = Math.max(1, Math.min(total, Number($("dl-from").value) || 1));
    const to = Math.max(from, Math.min(total, Number($("dl-to").value) || from));
    return [from, to];
  }

  function dlGapSec() {
    const el = $("dl-gap");
    const v = el ? Number(el.value) : NaN;
    if (!isFinite(v) || v <= 0) return 0.5;
    return Math.max(0.5, Math.min(60, v));
  }

  // 下载节奏预估：每章间隔由用户设定，但不会低于「请求间隔」这个全局下限
  function renderGap() {
    const el = $("dl-est");
    if (!el) return;
    const gap = Math.max(dlGapSec(), Number(settings.requestInterval) || 0);
    const span = selectedSpan();
    let text = "每章间隔 " + gap.toFixed(1) + " 秒";
    if (span) {
      const todo = (span[1] - span[0] + 1) - cachedIn(span[0], span[1]);
      if (todo > 0) {
        const secs = Math.round(todo * gap);
        text += " · 还要抓 " + todo + " 章，约 " + (secs >= 60 ? Math.round(secs / 60) + " 分钟" : secs + " 秒");
      } else {
        text += " · 这个区间都已缓存，不会联网";
      }
    }
    el.textContent = text;
  }

  function renderCache() {
    renderGap();
    const el = $("dl-cache");
    if (!el) return;
    const total = cacheRanges.reduce((s, r) => s + (r[1] - r[0] + 1), 0);
    if (!total) { el.textContent = "本地缓存：无（读过的章节会自动缓存）"; return; }
    const head = cacheRanges.slice(0, 8).map((r) => fmtRange(r[0], r[1])).join("、");
    let text = "本地缓存：" + head + (cacheRanges.length > 8 ? "…" : "") + " · 共 " + total + " 章";
    const span = selectedSpan();
    if (span) {
      text += "；当前区间 " + fmtRange(span[0], span[1]) + " 已缓存 " +
              cachedIn(span[0], span[1]) + " / " + (span[1] - span[0] + 1) + " 章";
    }
    el.textContent = text;
  }


  /* 清理缓存：两步确认，防误点 */
  let clrPending = 0;

  function bytesIn(from, to) {
    let b = 0;
    for (const r of cacheRanges) {
      const a = Math.max(from, r[0]);
      const z = Math.min(to, r[1]);
      if (z < a) continue;
      const n = z - a + 1;
      b += (r[0] === a && r[1] === z) ? r[2] : Math.round((r[2] / (r[1] - r[0] + 1)) * n);
    }
    return b;
  }

  function sizeText(bytes) {
    if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB";
    if (bytes >= 1024) return Math.round(bytes / 1024) + " KB";
    return bytes + " B";
  }

  function clrPlan() {
    const total = book ? book.total : 0;
    if ($("clr-scope").value !== "range") {
      let count = 0, bytes = 0;
      for (const r of cacheRanges) { count += r[1] - r[0] + 1; bytes += r[2]; }
      return { scope: "all", from: 1, to: total || 1, count: count, bytes: bytes };
    }
    const from = Math.max(1, Math.min(total || 1, Number($("clr-from").value) || 1));
    const to = Math.max(from, Math.min(total || 1, Number($("clr-to").value) || from));
    return { scope: "range", from: from, to: to, count: cachedIn(from, to), bytes: bytesIn(from, to) };
  }

  function renderClear() {
    const p = clrPlan();
    const btn = $("clr-run");
    if (!btn) return;
    btn.disabled = p.count === 0;
    btn.textContent = clrPending ? ("确认清理 " + p.count + " 章？") : "清理";
    btn.classList.toggle("confirm", !!clrPending);
    $("clr-status").textContent = p.count === 0
      ? "这个范围里没有已缓存的章节"
      : ("将删除 " + p.count + " 章（" + sizeText(p.bytes) + "）；目录会保留，清掉后仍能离线打开这本书");
  }

  function dropClearPending() {
    if (!clrPending) return;
    clearTimeout(clrPending);
    clrPending = 0;
  }

  function applyScopeRows() {
    const total = book ? book.total : 1;
    const dFrom = Math.max(1, Math.min(total, cur + 1));
    const dl = $("dl-scope").value;
    $("dl-next-row").hidden = dl !== "next";
    $("dl-range-row").hidden = dl !== "range";
    if (dl === "range" && !$("dl-from").dataset.touched) $("dl-from").value = String(dFrom);
    const ex = $("ex-scope").value;
    $("ex-range-row").hidden = ex !== "range";
    if (ex === "range" && !$("ex-from").dataset.touched) $("ex-from").value = String(dFrom);
    const clr = $("clr-scope").value;
    $("clr-range-row").hidden = clr !== "range";
    if (clr === "range" && !$("clr-from").dataset.touched) $("clr-from").value = "1";
    renderCache();
    renderClear();
  }

  function syncTools() {
    const local = !!(book && book.kind === "local");
    $("txt-group").hidden = !local;
    $("dl-group").hidden = !book || local;
    $("tools-sub").textContent = !book ? "未打开书籍" : (local ? "本地 TXT" : conv(book.meta.title || "网页小说"));
    const total = book ? book.total : 1;
    ["dl-from", "dl-to", "ex-from", "ex-to", "clr-from", "clr-to"].forEach((id) => {
      const el = $(id);
      if (!el) return;
      el.max = String(total);
      if (Number(el.value) > total) el.value = String(total);
    });
    const gapEl = $("dl-gap");
    if (gapEl && document.activeElement !== gapEl) gapEl.value = String(settings.downloadGap);
    if (book) {
      const dTo = $("dl-to");
      if (!dTo.dataset.touched) dTo.value = String(Math.min(total, cur + 50));
      const eTo = $("ex-to");
      if (!eTo.dataset.touched) eTo.value = String(total);
      const cTo = $("clr-to");
      if (!cTo.dataset.touched) cTo.value = String(Math.min(total, cur + 1));
    }
    applyScopeRows();
  }

  function onTxtInfo(m) {
    const req = m.requested || "auto";
    $("txt-path").textContent = m.path || "—";
    $("txt-path").title = m.path || "";
    $("txt-enc-info").textContent = (m.encoding || "?") + (req === "auto" ? "（自动识别）" : "（手动指定）");
    $("txt-count").textContent = (m.chapters | 0) + " 章";
    $("txt-enc").value = req;
    $("txt-regex").value = m.regex || "";
    const box = $("txt-samples");
    box.innerHTML = "";
    const s = m.samples || [];
    if (s.length) {
      const lab = document.createElement("div");
      lab.className = "samples-label";
      lab.textContent = "识别到的前几章";
      box.appendChild(lab);
      s.forEach((t) => {
        const d = document.createElement("div");
        d.className = "sample-item";
        d.textContent = t;
        box.appendChild(d);
      });
    }
    syncTools();
  }

  function onNeedRelocate(m) {
    if (!reading) showScreen("home");
    const el = $("home-status");
    el.innerHTML = "";
    el.classList.remove("busy");
    el.classList.add("err");
    el.appendChild(document.createTextNode("文件找不到了：" + (m.path || "")));
    const b = document.createElement("button");
    b.className = "ghost small";
    b.textContent = "重新定位…";
    b.addEventListener("click", () => post({ type: "relocateBook", bookId: m.bookId }));
    el.appendChild(b);
    showToast("原文件已被移动或删除，请重新定位", 4200);
  }

  function onJob(m) {
    const isExport = m.kind === "export";
    const prog = $(isExport ? "ex-progress" : "dl-progress");
    const st = $(isExport ? "ex-status" : "dl-status");
    const done = m.done | 0;
    const total = m.total | 0;
    prog.max = Math.max(1, total);
    prog.value = Math.min(done, prog.max);
    st.classList.remove("err");
    if (m.state === "running") st.textContent = (isExport ? "补齐中 " : "下载中 ") + done + " / " + total;
    else if (m.state === "paused") st.textContent = "已暂停 · " + done + " / " + total;
    else if (m.state === "done") st.textContent = "完成 · 共 " + total + " 章";
    else st.textContent = "未开始";
    const busy = m.state === "running" || m.state === "paused";
    const running = m.state === "running";
    // 「开始」只在真正跑着的时候禁用，否则暂停后就再也点不动了（暂停 → 继续靠它）
    if (isExport) {
      $("ex-start").disabled = running;
      $("ex-cancel").disabled = !busy;
    } else {
      $("dl-start").disabled = running;
      $("dl-pause").disabled = !running;
    }
  }

  function onExportDone(m) {
    const st = $("ex-status");
    st.classList.remove("err");
    st.textContent = "已导出 " + (m.chapters | 0) + " 章" +
      (m.skipped ? ("（跳过 " + m.skipped + " 章）") : "") + " → " + (m.path || "");
    showToast("导出完成：" + (m.path || ""), 4600);
  }

  function bindTools() {
    $("txt-relocate").addEventListener("click", () => {
      if (book) post({ type: "relocateBook", bookId: book.meta.bookId });
    });
    $("txt-rescan").addEventListener("click", () => {
      post({ type: "rescanTxt", encoding: $("txt-enc").value, regex: $("txt-regex").value.trim() });
    });
    $("txt-regex").addEventListener("keydown", (e) => {
      if (e.key === "Enter") { e.preventDefault(); $("txt-rescan").click(); }
    });

    $("dl-scope").addEventListener("change", applyScopeRows);
    $("ex-scope").addEventListener("change", applyScopeRows);
    ["dl-from", "dl-to", "ex-from", "ex-to"].forEach((id) => {
      $(id).addEventListener("input", (e) => {
        e.target.dataset.touched = "1";
        if (id === "dl-from" || id === "dl-to") renderCache();
      });
    });
    $("dl-next").addEventListener("input", renderCache);
    $("dl-gap").addEventListener("input", () => {
      settings.downloadGap = dlGapSec();
      pushSettings();
      renderGap();
    });
    $("dl-gap").addEventListener("change", () => {
      settings.downloadGap = dlGapSec();
      $("dl-gap").value = String(settings.downloadGap);
      pushSettings(true);
      renderGap();
    });

    $("dl-start").addEventListener("click", () => {
      post({
        type: "download",
        scope: $("dl-scope").value,
        next: Number($("dl-next").value) || 50,
        from: Number($("dl-from").value) || 1,
        to: Number($("dl-to").value) || 1
      });
    });
    $("dl-pause").addEventListener("click", () => post({ type: "downloadPause" }));

    $("clr-scope").addEventListener("change", () => { dropClearPending(); applyScopeRows(); });
    ["clr-from", "clr-to"].forEach((id) => {
      $(id).addEventListener("input", (e) => {
        e.target.dataset.touched = "1";
        dropClearPending();
        renderClear();
      });
    });
    $("clr-run").addEventListener("click", () => {
      const p = clrPlan();
      if (p.count === 0) { showToast("这个范围里没有已缓存的章节"); return; }
      if (!clrPending) {
        clrPending = setTimeout(() => { clrPending = 0; renderClear(); }, 5000);
        renderClear();
        return;
      }
      dropClearPending();
      post({ type: "clearCache", scope: p.scope, from: p.from, to: p.to });
      renderClear();
    });

    $("ex-start").addEventListener("click", () => {
      post({
        type: "exportTxt",
        bookId: book ? book.meta.bookId : "",
        scope: $("ex-scope").value,
        from: Number($("ex-from").value) || 1,
        to: Number($("ex-to").value) || 1,
        encoding: $("ex-enc").value,
        includeTitles: $("ex-titles").checked,
        simplified: $("ex-simplified").checked,
        fill: $("ex-fill").checked
      });
    });
    $("ex-cancel").addEventListener("click", () => post({ type: "exportCancel" }));
  }

  /* ================= 宿主消息 ================= */
  function setStatus(text, busy, isErr) {
    const el = $("home-status");
    el.innerHTML = "";
    if (text) el.appendChild(document.createTextNode(text));
    el.classList.toggle("busy", !!busy);
    el.classList.toggle("err", !!isErr);
    if (text && !readerEl.hidden) showToast(text, 2200);
  }

  function onBook(m) {
    book = {
      meta: m.meta || {},
      total: m.total | 0,
      chapters: m.chapters || [],
      kind: m.kind || "web",
      localPath: m.localPath || ""
    };
    tocQuery = "";
    $("toc-search").value = "";
    $("toc-book").textContent = conv(book.meta.title || "目录");
    $("toc-sub").textContent = book.meta.author
      ? (book.meta.author + " · 共 " + book.total + " 章")
      : ("共 " + book.total + " 章");
    showScreen("reader");
    startReading(m.resumeIndex | 0, m.resumeRatio || 0);
    renderToc();
    updateTitle();
    setStatus("", false);
    cacheRanges = [];
    // 换了本书，下载/导出的区间输入框回到这本书的默认值，别留着上一本的章号
    ["dl-from", "dl-to", "ex-from", "ex-to", "clr-from", "clr-to"].forEach((id) => {
      const el = $(id);
      if (el) delete el.dataset.touched;
    });
    dropClearPending();
    syncTools();
  }

  function onChapter(m) {
    const i = m.index | 0;
    inflight.delete(i);
    if (!book) return;
    const paras = m.paragraphs || [];
    memSet(i, m.title || "", paras, m.cached);
    const b = blocks.get(i);
    if (b) {
      paintBlock(b, i, m.title, paras, m.cached);
      if (pendingResume === i) {
        const r = pendingResumeRatio;
        pendingResume = -1;
        pendingResumeRatio = 0;
        if (r > 0) {
          requestAnimationFrame(() => {
            if (!reading || !blocks.has(i)) return;
            viewport.scrollTop = blockOffset(i) + r * blockHeight(i);
            handleScroll();
          });
        }
      }
    } else if (pendingPrev === i) {
      pendingPrev = -1;
      insertPrev(i, m.title, paras);
    }
    updateLoading();
    updateEndNote();
  }

  function onChapterError(m) {
    const i = m.index | 0;
    inflight.delete(i);
    if (pendingPrev === i) pendingPrev = -1;
    const b = blocks.get(i);
    if (b) paintError(b, i, m.message || "加载失败");
    updateLoading();
  }

  function onMessage(ev) {
    const m = ev.data;
    if (!m || typeof m !== "object") return;
    if (m.type === "settings") {
      if (m.value) {
        Object.assign(settings, DEFAULTS, m.value);
        lastSimplified = settings.simplified;
        if (settings.theme !== "dark") lastTheme = settings.theme;
        applySettings({ anchor: false });
      }
    } else if (m.type === "status") {
      setStatus(m.text, m.busy, false);
    } else if (m.type === "error") {
      if (!readerEl.hidden) showError(m.message || "出错了");
      else setStatus(m.message || "出错了", false, true);
    } else if (m.type === "books") {
      renderShelf(m.items || []);
    } else if (m.type === "meta") {
      if (book && m.meta) {
        book.meta = m.meta;
        updateTopbar();
        updateTitle();
      }
    } else if (m.type === "book") {
      onBook(m);
    } else if (m.type === "chapter") {
      onChapter(m);
    } else if (m.type === "chapterError") {
      onChapterError(m);
    } else if (m.type === "txtInfo") {
      onTxtInfo(m);
    } else if (m.type === "needRelocate") {
      onNeedRelocate(m);
    } else if (m.type === "cacheInfo") {
      cacheRanges = Array.isArray(m.ranges) ? m.ranges : [];
      dropClearPending();
      renderCache();
      renderClear();
    } else if (m.type === "job") {
      onJob(m);
    } else if (m.type === "exportDone") {
      onExportDone(m);
    } else if (m.type === "toast") {
      showToast(m.text || "", 3400);
    } else if (m.type === "dragging") {
      $("drop-overlay").hidden = !m.on;
    }
  }

  function init() {
    bindHome();
    bindReader();
    bindPanel();
    bindTools();
    applySettings({ anchor: false });
    viewport.addEventListener("scroll", onScroll, { passive: true });
    window.addEventListener("keydown", onKey, true);
    window.addEventListener("beforeunload", () => { if (reading) sendProgress(); });
    if (host) host.addEventListener("message", onMessage);
    post({ type: "ready" });
  }

  init();
})();
