const assert = require("node:assert/strict");
const { test } = require("node:test");
const { Tracker, addCoverage, hourParts, movePeriod, summarize, visibleRange } = require("../web/reading-stats.js");

function memoryStorage() {
  const values = new Map();
  return {
    get length() { return values.size; },
    key: (index) => Array.from(values.keys())[index],
    getItem: (key) => values.get(key),
    setItem: (key, value) => values.set(key, value)
  };
}

const sampleBook = { id: "book-a", title: "甲书" };
const nextBook = { id: "book-b", title: "乙书" };
const noon = new Date(2024, 1, 29, 12).getTime();
const view = (start = 0, end = 100) => new Map([[0, [start, end]]]);

test("merges viewed ranges without counting earlier text twice", () => {
  const coverage = [];
  assert.equal(addCoverage(coverage, 0, 100), 100);
  assert.equal(addCoverage(coverage, 180, 200), 20);
  assert.equal(addCoverage(coverage, 80, 190), 80);
  assert.equal(addCoverage(coverage, 60, 110), 0);
  assert.deepEqual(coverage, [[0, 200]]);
});

test("counts displayed letters and numbers, excluding whitespace, punctuation and images", () => {
  function item(tagName, textContent, top, bottom) {
    return { tagName, textContent, getBoundingClientRect: () => ({ top, bottom, height: bottom - top }) };
  }
  const body = { children: [
    item("P", "𠮷 字，123！", 0, 20),
    item("FIGURE", "图", 20, 110),
    item("P", "Hi", 110, 130)
  ] };
  const cache = new WeakMap();
  assert.deepEqual(visibleRange(body, 0, 50, cache), [0, 5]);
  assert.deepEqual(visibleRange(body, 25, 100, cache), [5, 5]);
  assert.deepEqual(visibleRange(body, 20, 130, cache), [5, 7]);
});

test("counts stable display samples and skips text between jumps and background intervals", () => {
  const tracker = new Tracker(memoryStorage(), "window-a");
  tracker.setBook(sampleBook, 0);
  tracker.sample(0, noon, view());
  tracker.sample(1000, noon + 1000, view());
  tracker.sample(2000, noon + 2000, view(2000, 2100));
  tracker.sample(3000, noon + 3000, view(2000, 2100));
  tracker.sample(3500, noon + 3500, null);
  tracker.sample(10000, noon + 10000, null);
  tracker.sample(11000, noon + 11000, view());
  tracker.sample(12000, noon + 12000, view());
  tracker.sample(50000, noon + 50000, view());
  tracker.flush(50000);
  const summary = summarize(tracker.entries(), "day", new Date(noon));
  assert.equal(summary.ms, 4500);
  assert.equal(summary.chars, 200);
  assert.equal(summary.speed, null);
});

test("pauses at the idle boundary and resumes after activity", () => {
  const tracker = new Tracker(memoryStorage(), "window-a");
  tracker.setBook(sampleBook, 0);
  for (let now = 0; now <= 125000; now += 1000) tracker.sample(now, noon + now, view());
  tracker.activity(130000);
  tracker.sample(130000, noon + 130000, view(50, 150));
  tracker.sample(131000, noon + 131000, view(50, 150));
  const summary = summarize(tracker.entries(), null, null, sampleBook.id);
  assert.equal(summary.ms, 121000);
  assert.equal(summary.chars, 150);
  assert.equal(summary.speed, 74);
});

test("splits elapsed time across local midnight and groups each calendar period", () => {
  const midnight = new Date(2024, 2, 1).getTime();
  assert.deepEqual(hourParts(midnight - 500, midnight + 600), [
    { day: "2024-02-29", hour: 23, ms: 500 },
    { day: "2024-03-01", hour: 0, ms: 600 }
  ]);
  const tracker = new Tracker(memoryStorage(), "window-a");
  tracker.setBook(sampleBook, 0);
  tracker.sample(0, midnight - 500, view());
  tracker.sample(1100, midnight + 600, view());
  const entries = tracker.entries();
  const day = summarize(entries, "day", new Date(midnight));
  const month = summarize(entries, "month", movePeriod("month", new Date(midnight), -1));
  const year = summarize(entries, "year", new Date(midnight));
  assert.equal(day.buckets[0], 600);
  assert.equal(month.buckets.length, 29);
  assert.equal(month.buckets[28], 500);
  assert.deepEqual(year.buckets.slice(0, 3), [0, 500, 600]);
  assert.equal(year.days, 2);
  assert.equal(year.chars, 100);
});

test("persists per-book time, combines other windows, and ranks by time", () => {
  const storage = memoryStorage();
  const left = new Tracker(storage, "window-a");
  const right = new Tracker(storage, "window-b");
  left.setBook(sampleBook, 0);
  right.setBook(sampleBook, 0);
  left.record("2024-02-29", 12, 60000, 1200);
  right.record("2024-02-29", 12, 30000, 150);
  right.setBook(nextBook, 10);
  right.record("2024-02-29", 12, 120000, 1500);
  left.flush(500);
  right.flush(500);
  const reopened = new Tracker(storage, "window-c");
  const month = summarize(reopened.entries(), "month", new Date(noon));
  assert.deepEqual(month.ranking.map((book) => book.id), [nextBook.id, sampleBook.id]);
  assert.equal(month.speed, 814);
  assert.equal(summarize(reopened.entries(), null, null, sampleBook.id).speed, 900);
});

test("retains unsaved records and retries without duplication", () => {
  const storage = memoryStorage();
  const save = storage.setItem;
  storage.setItem = () => { throw new Error("quota"); };
  const tracker = new Tracker(storage, "window-a");
  tracker.setBook(sampleBook, 0);
  tracker.record("2024-02-29", 12, 1500, 34);
  tracker.flush(1500);
  assert.equal(tracker.writeFailed, true);
  assert.equal(tracker.entries()[0].hours[12][0], 1500);
  storage.setItem = save;
  tracker.flush(2000);
  assert.equal(tracker.writeFailed, false);
  assert.equal(tracker.entries().length, 1);
  assert.deepEqual(tracker.entries()[0].hours[12], [1500, 34]);
});
