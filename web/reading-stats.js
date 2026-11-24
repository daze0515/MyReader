(function (root) {
  "use strict";

  const KEY_PREFIX = "myreader.stats.v1/";
  const IDLE_MS = 120000;
  const MAX_SAMPLE_GAP_MS = 4000;
  const FLUSH_MS = 10000;
  const MIN_SPEED_MS = 60000;

  function localDay(date) {
    const pad = (value) => String(value).padStart(2, "0");
    return date.getFullYear() + "-" + pad(date.getMonth() + 1) + "-" + pad(date.getDate());
  }

  function hourParts(start, end) {
    const parts = [];
    while (start < end) {
      const date = new Date(start);
      const hour = date.getHours();
      const remaining = ((60 - date.getMinutes()) * 60 - date.getSeconds()) * 1000 - date.getMilliseconds();
      const until = Math.min(end, start + remaining);
      parts.push({ day: localDay(date), hour, ms: until - start });
      start = until;
    }
    return parts;
  }

  function addCoverage(ranges, start, end) {
    if (end <= start) return 0;
    let first = 0;
    while (first < ranges.length && ranges[first][1] < start) first++;
    let last = first;
    let added = end - start;
    while (last < ranges.length && ranges[last][0] <= end) {
      const [left, right] = ranges[last++];
      added -= Math.max(0, Math.min(end, right) - Math.max(start, left));
      start = Math.min(start, left);
      end = Math.max(end, right);
    }
    ranges.splice(first, last - first, [start, end]);
    return added;
  }

  function readEntry(storage, key) {
    try {
      const value = JSON.parse(storage.getItem(key));
      if (value && typeof value.id === "string" && /^\d{4}-\d{2}-\d{2}$/.test(value.day) &&
          typeof value.title === "string" && Array.isArray(value.hours)) return value;
    } catch {}
    return null;
  }

  class Tracker {
    constructor(storage, writer) {
      this.storage = storage;
      this.writer = writer;
      this.pending = new Map();
      this.coverage = new Map();
      this.previous = null;
      this.book = null;
      this.lastActivity = -Infinity;
      this.lastFlush = 0;
      this.writeFailed = false;
    }

    setBook(book, now) {
      this.book = book && book.id ? book : null;
      this.previous = null;
      this.coverage.clear();
      this.activity(now);
    }

    activity(now) {
      if (this.isIdle(now)) this.previous = null;
      this.lastActivity = now;
    }

    isIdle(now) {
      return now >= this.lastActivity + IDLE_MS;
    }

    sample(now, wallTime, ranges) {
      const previous = this.previous;
      const activeUntil = this.lastActivity + IDLE_MS;
      this.previous = this.book && ranges && now < activeUntil ? { now, wallTime, ranges } : null;
      if (this.book && previous) {
        const elapsed = Math.min(now, activeUntil) - previous.now;
        const wallElapsed = wallTime - previous.wallTime;
        if (elapsed > 0 && now - previous.now <= MAX_SAMPLE_GAP_MS &&
            Math.abs(wallElapsed - (now - previous.now)) < MAX_SAMPLE_GAP_MS) {
          let chars = 0;
          if (ranges && elapsed >= 750) {
            for (const [chapter, [start, end]] of ranges) {
              const prior = previous.ranges.get(chapter);
              if (!prior) continue;
              if (!this.coverage.has(chapter)) this.coverage.set(chapter, []);
              chars += addCoverage(this.coverage.get(chapter), Math.max(start, prior[0]), Math.min(end, prior[1]));
            }
          }
          const parts = hourParts(previous.wallTime, previous.wallTime + elapsed);
          let credited = 0;
          let accumulatedMs = 0;
          for (const part of parts) {
            accumulatedMs += part.ms;
            const totalChars = Math.round(chars * accumulatedMs / elapsed);
            this.record(part.day, part.hour, part.ms, totalChars - credited);
            credited = totalChars;
          }
        }
      }
      if (now - this.lastFlush >= FLUSH_MS) this.flush(now);
    }

    record(day, hour, ms, chars) {
      const { id, title } = this.book;
      const key = KEY_PREFIX + day + "/" + encodeURIComponent(id) + "/" + this.writer;
      if (!this.pending.has(key)) this.pending.set(key, { id, title, day, hours: [] });
      const entry = this.pending.get(key);
      entry.title = title;
      if (!entry.hours[hour]) entry.hours[hour] = [0, 0];
      entry.hours[hour][0] += ms;
      entry.hours[hour][1] += chars;
    }

    flush(now) {
      this.lastFlush = now;
      for (const [key, delta] of this.pending) {
        try {
          const entry = readEntry(this.storage, key) || { ...delta, hours: [] };
          entry.title = delta.title;
          delta.hours.forEach((values, hour) => {
            const [oldMs, oldChars] = entry.hours[hour] || [0, 0];
            entry.hours[hour] = [oldMs + values[0], oldChars + values[1]];
          });
          this.storage.setItem(key, JSON.stringify(entry));
          this.pending.delete(key);
        } catch {}
      }
      this.writeFailed = this.pending.size > 0;
    }

    entries() {
      const entries = [];
      try {
        for (let index = 0; index < this.storage.length; index++) {
          const key = this.storage.key(index);
          if (!key || !key.startsWith(KEY_PREFIX)) continue;
          const entry = readEntry(this.storage, key);
          if (entry) entries.push(entry);
        }
        this.writeFailed = this.pending.size > 0;
      } catch {
        this.writeFailed = true;
      }
      return entries.concat(Array.from(this.pending.values()));
    }
  }

  function periodPrefix(mode, date) {
    return localDay(date).slice(0, { day: 10, month: 7, year: 4 }[mode]);
  }

  function movePeriod(mode, date, offset) {
    const year = date.getFullYear();
    const month = date.getMonth();
    if (mode === "day") return new Date(year, month, date.getDate() + offset);
    if (mode === "month") return new Date(year, month + offset, 1);
    return new Date(year + offset, 0, 1);
  }

  function summarize(entries, mode, date, bookId) {
    const prefix = mode ? periodPrefix(mode, date) : "";
    const bucketCount = mode === "day" ? 24 : mode === "month"
      ? new Date(date.getFullYear(), date.getMonth() + 1, 0).getDate() : mode === "year" ? 12 : 0;
    const buckets = Array(bucketCount).fill(0);
    const books = new Map();
    const days = new Set();
    let ms = 0;
    let chars = 0;
    for (const entry of entries) {
      if (!entry.day.startsWith(prefix) || (bookId && entry.id !== bookId)) continue;
      entry.hours.forEach((values, hour) => {
        if (hour >= 24 || !Array.isArray(values)) return;
        const [duration, count] = values;
        if (!Number.isFinite(duration) || duration <= 0 || !Number.isFinite(count) || count < 0) return;
        const bucket = mode === "day" ? hour : mode === "month"
          ? Number(entry.day.slice(8, 10)) - 1 : Number(entry.day.slice(5, 7)) - 1;
        if (bucket >= 0 && bucket < buckets.length) buckets[bucket] += duration;
        days.add(entry.day);
        ms += duration;
        chars += count;
        if (!books.has(entry.id)) books.set(entry.id, { id: entry.id, title: entry.title, day: entry.day, ms: 0 });
        const item = books.get(entry.id);
        item.ms += duration;
        if (entry.day >= item.day) {
          item.title = entry.title;
          item.day = entry.day;
        }
      });
    }
    const ranking = Array.from(books.values()).sort((left, right) =>
      right.ms - left.ms || left.title.localeCompare(right.title, "zh-CN") || left.id.localeCompare(right.id));
    return { ms, chars, days: days.size, ranking, buckets, speed: ms >= MIN_SPEED_MS && chars > 0
      ? Math.round(chars * 60000 / ms) : null };
  }

  function visibleRange(body, top, bottom, metricsCache) {
    let metrics = metricsCache.get(body);
    if (!metrics) {
      let offset = 0;
      metrics = Array.from(body.children).filter((element) => element.tagName === "P").map((element) => {
        const length = (element.textContent.match(/[\p{L}\p{N}]/gu) || []).length;
        const result = { element, length, offset };
        offset += length;
        return result;
      });
      metricsCache.set(body, metrics);
    }
    function positionAt(edge) {
      let low = 0;
      let high = metrics.length;
      while (low < high) {
        const middle = Math.floor((low + high) / 2);
        if (metrics[middle].element.getBoundingClientRect().bottom <= edge) low = middle + 1;
        else high = middle;
      }
      const paragraph = metrics[low];
      if (!paragraph) {
        const last = metrics[metrics.length - 1];
        return last ? last.offset + last.length : 0;
      }
      const bounds = paragraph.element.getBoundingClientRect();
      const ratio = Math.max(0, Math.min(1, (edge - bounds.top) / Math.max(1, bounds.height)));
      return paragraph.offset + Math.round(ratio * paragraph.length);
    }
    return [positionAt(top), positionAt(bottom)];
  }

  const api = { Tracker, hourParts, addCoverage, periodPrefix, movePeriod, summarize, visibleRange };
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.ReadingStats = api;
})(globalThis);
