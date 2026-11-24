(function () {
  "use strict";

  const periods = { day: "日", month: "月", year: "年" };
  const number = new Intl.NumberFormat("zh-CN");

  function duration(ms) {
    const seconds = Math.round(ms / 1000);
    if (seconds < 60) return number.format(seconds) + " 秒";
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor(seconds / 60) % 60;
    if (!hours) return number.format(minutes) + " 分 " + seconds % 60 + " 秒";
    return number.format(hours) + " 小时 " + minutes + " 分";
  }

  function element(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }

  function metric(label, value) {
    const item = element("div", "stats-metric");
    item.append(element("dt", "", label), element("dd", "", value));
    return item;
  }

  function speedText(summary) {
    return summary.speed === null ? "—" : number.format(summary.speed) + " 字/分钟";
  }

  function chartScale(ms) {
    const steps = [60000, 120000, 300000, 600000, 900000, 1800000, 3600000, 7200000];
    return (steps.find((step) => step * 4 >= ms) || Math.ceil(ms / 14400000) * 3600000) * 4;
  }

  function renderChart(dialog, mode, buckets) {
    const scale = chartScale(Math.max(...buckets));
    const axis = dialog.querySelector("#stats-axis");
    const bars = dialog.querySelector("#stats-bars");
    const rows = dialog.querySelector("#stats-chart-values");
    axis.replaceChildren();
    bars.replaceChildren();
    rows.replaceChildren();
    bars.style.setProperty("--stats-columns", buckets.length);
    for (let tick = 4; tick >= 0; tick--) {
      const minutes = scale * tick / 240000;
      axis.append(element("span", "", minutes >= 60 && minutes % 60 === 0
        ? minutes / 60 + " 小时" : minutes + " 分"));
    }
    buckets.forEach((ms, index) => {
      const label = mode === "day" ? String(index).padStart(2, "0") + ":00" : index + 1 + periods[mode];
      const showLabel = mode === "year" || (mode === "day" ? index % 4 === 0 : index === 0 || (index + 1) % 5 === 0);
      const column = element("div", "stats-column");
      column.title = label + " · " + duration(ms);
      const track = element("div", "stats-track");
      const bar = element("span", "stats-bar");
      bar.style.height = (ms / scale * 100) + "%";
      bar.hidden = !ms;
      track.append(bar);
      column.append(track, element("span", "stats-tick", showLabel ? (mode === "day" ? label.slice(0, 2) : String(index + 1)) : ""));
      bars.append(column);
      const row = element("tr");
      const heading = element("th", "", label);
      heading.scope = "row";
      row.append(heading, element("td", "", duration(ms)));
      rows.append(row);
    });
  }

  function createPanel(dialog, tracker, translateTitle) {
    let book;
    let mode;
    let date;
    let entries;
    const selectPeriod = dialog.querySelector("#stats-periods");
    const earlier = dialog.querySelector("#stats-earlier");
    const later = dialog.querySelector("#stats-later");

    function render() {
      const stats = ReadingStats.summarize(entries, mode, date, book?.id);
      const top = stats.ranking[0];
      const dateParts = ReadingStats.periodPrefix(mode, date).split("-");
      dialog.querySelector("#stats-date").textContent = dateParts.map((value, index) =>
        Number(value) + ["年", "月", "日"][index]).join("");
      later.disabled = ReadingStats.periodPrefix(mode, date) >= ReadingStats.periodPrefix(mode, new Date());
      selectPeriod.querySelectorAll("button").forEach((button) => {
        button.setAttribute("aria-pressed", String(button.dataset.period === mode));
      });
      dialog.querySelector("#stats-summary").replaceChildren(
        metric("阅读时长", duration(stats.ms)),
        metric("估算阅读字数", number.format(stats.chars) + " 字"),
        metric("平均阅读速度", speedText(stats)),
        book ? metric("阅读天数", stats.days + " 天") : metric("读过的书", stats.ranking.length + " 本")
      );
      dialog.querySelector("#stats-empty").hidden = stats.ms > 0;
      dialog.querySelector("#stats-chart").hidden = !stats.ms;
      dialog.querySelector("#stats-axis-unit").textContent = { day: "按小时", month: "按日", year: "按月" }[mode];
      if (stats.ms) renderChart(dialog, mode, stats.buckets);
      dialog.querySelector("#stats-ranking").hidden = !!book || !top;
      const ranking = dialog.querySelector("#stats-books");
      ranking.replaceChildren();
      if (!book) for (const item of stats.ranking) {
        const row = element("li", "stats-rank");
        const name = element("span", "stats-rank-name", translateTitle(item.title));
        name.title = name.textContent;
        row.append(name, element("span", "stats-rank-time", duration(item.ms)));
        ranking.append(row);
      }
    }

    function open(selectedBook) {
      entries = tracker.entries();
      book = selectedBook || null;
      mode = "month";
      date = new Date();
      dialog.querySelector("#stats-title").textContent = book ? "本书统计" : "个人阅读统计";
      const lifetime = dialog.querySelector("#stats-lifetime");
      lifetime.hidden = !book;
      if (book) {
        const total = ReadingStats.summarize(entries, null, date, book.id);
        dialog.querySelector("#stats-book-title").textContent = translateTitle(book.title);
        dialog.querySelector("#stats-total").replaceChildren(
          metric("累计阅读时长", duration(total.ms)),
          metric("累计估算字数", number.format(total.chars) + " 字"),
          metric("平均阅读速度", speedText(total))
        );
      }
      dialog.querySelector("#stats-storage-warning").hidden = !tracker.writeFailed;
      dialog.querySelector("#stats-chart-details").open = false;
      dialog.scrollTop = 0;
      render();
      if (!dialog.open) dialog.showModal();
    }

    function move(offset) {
      date = ReadingStats.movePeriod(mode, date, offset);
      render();
    }

    selectPeriod.addEventListener("click", (event) => {
      const period = event.target.closest("button")?.dataset.period;
      if (!period || period === mode) return;
      mode = period;
      render();
    });
    earlier.addEventListener("click", () => move(-1));
    later.addEventListener("click", () => move(1));
    return { open };
  }

  window.createReadingStatsPanel = createPanel;
})();
