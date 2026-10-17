"use strict";
// Run by commandersjs_test.go under node: the refresh-rate picker (refresh.js): its options, the choice saved per browser (also when the
// storage throws), and the gate that lets live updates through no faster than the choice, or not at all while paused.
const assert = require("assert");
const R = require(require("path").resolve(process.argv[2]));

// ---- the options
assert.deepStrictEqual(R.OPTIONS, [1, 2, 5, 10, 30, 0], "1 / 2 / 5 / 10 / 30 s / paused");
assert.strictEqual(R.DEFAULT_SEC, 2, "the default is the emit interval: nothing is held back until the viewer picks");
assert.strictEqual(R.label(0), "paused"); assert.strictEqual(R.label(5), "5 s");
[["5", 5], [5, 5], ["0", 0], [0, 0], ["30", 30], ["7", 2], [7, 2], ["abc", 2], [null, 2], [undefined, 2], [NaN, 2], ["", 2], ["-1", 2], ["1e1", 2], [{}, 2]]
  .forEach(([v, want]) => assert.strictEqual(R.normalize(v), want, String(v)));

// ---- saved per browser, every read and write in try/catch
const mem = () => { const m = {}; return { getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = String(v); }, m }; };
let st = mem();
assert.strictEqual(R.load(st), 2, "nothing saved: the default");
assert.strictEqual(R.save(st, 10), true);
assert.strictEqual(st.m[R.STORAGE_KEY], "10");
assert.strictEqual(R.load(st), 10, "the choice survives a reload");
R.save(st, 0); assert.strictEqual(R.load(st), 0, "paused is saved too");
st.m[R.STORAGE_KEY] = "junk"; assert.strictEqual(R.load(st), 2, "a damaged value falls back");
R.save(st, 7); assert.strictEqual(st.m[R.STORAGE_KEY], "2", "a value that is no option is never stored");
const throwing = { getItem() { throw new Error("SecurityError"); }, setItem() { throw new Error("QuotaExceededError"); } };
assert.strictEqual(R.load(throwing), 2, "storage that throws on read: the default, no exception");
assert.strictEqual(R.save(throwing, 5), false, "storage that throws on write: reported, no exception");
assert.strictEqual(R.load(null), 2, "no storage at all (even asking for it threw)");
assert.strictEqual(R.save(null, 5), false);
assert.strictEqual(R.load({}), 2, "an object that is no storage");

// ---- the gate, on a fake clock
function env() {
  const e = { t: 1000000, timers: [], nextId: 1 };
  e.now = () => e.t;
  e.setTimer = (fn, ms) => { const h = e.nextId++; e.timers.push({ h, at: e.t + ms, fn }); return h; };
  e.clearTimer = (h) => { e.timers = e.timers.filter((x) => x.h !== h); };
  e.advance = (ms) => { // run due timers in order
    const end = e.t + ms;
    for (;;) {
      e.timers.sort((a, b) => a.at - b.at);
      const nx = e.timers[0];
      if (!nx || nx.at > end) break;
      e.timers.shift(); e.t = nx.at; nx.fn();
    }
    e.t = end;
  };
  return e;
}
const feed = (g, e, key, log, n, gapMs) => { for (let i = 0; i < n; i++) { const v = key + i; g.submit(key, () => log.push(v)); e.advance(gapMs); } };

// default 2 s with a 2 s stream: everything shows
let e = env(), g = R.createGate(2, e), log = [];
feed(g, e, "a", log, 5, 2000);
assert.strictEqual(log.length, 5, "a 2 s stream at a 2 s rate loses nothing");
// 1 s: every update shows
e = env(); g = R.createGate(1, e); log = [];
feed(g, e, "a", log, 5, 2000); assert.strictEqual(log.length, 5);

// 10 s over a 2 s stream: about one in five, always the newest, never early
e = env(); g = R.createGate(10, e); log = [];
const shown = [];
for (let i = 0; i < 30; i++) { g.submit("a", () => shown.push([i, e.t])); e.advance(2000); }
assert.ok(shown.length >= 5 && shown.length <= 7, "30 updates over 60 s at 10 s: " + shown.length);
shown.slice(1).forEach((s, i) => assert.ok(s[1] - shown[i][1] >= 10000 - 400, "never faster than the rate"));
assert.strictEqual(shown[0][0], 0, "the first update shows at once");
assert.ok(shown.slice(1).every((s) => (s[0] * 2000) % 2000 === 0), "each shown update is a whole one");
// a late update is the newest of those that arrived, not the first
e = env(); g = R.createGate(5, e); log = [];
g.submit("a", () => log.push("one"));   // shows
e.advance(1000); g.submit("a", () => log.push("two"));
e.advance(1000); g.submit("a", () => log.push("three"));
assert.deepStrictEqual(log, ["one"], "held back");
e.advance(3100);
assert.deepStrictEqual(log, ["one", "three"], "the timer shows the newest at the interval");
e.advance(60000); assert.deepStrictEqual(log, ["one", "three"], "and nothing more");

// keys are independent
e = env(); g = R.createGate(30, e); log = [];
g.submit("snapshot", () => log.push("s1")); g.submit("commanders", () => log.push("c1"));
g.submit("snapshot", () => log.push("s2")); g.submit("commanders", () => log.push("c2"));
assert.deepStrictEqual(log, ["s1", "c1"], "each stream has its own first update and its own wait");
assert.strictEqual(g.waiting(), 2);

// ---- paused: nothing runs, the newest waits, resume shows it
e = env(); g = R.createGate(2, e); log = [];
g.submit("a", () => log.push("a1"));
e.advance(2000);
g.set(0);
assert.strictEqual(g.paused(), true); assert.strictEqual(g.interval(), 0);
for (let i = 2; i < 12; i++) { g.submit("a", () => log.push("a" + i)); e.advance(2000); }
assert.deepStrictEqual(log, ["a1"], "paused freezes: no update shows");
assert.strictEqual(e.timers.length, 0, "...and no timer is left running");
assert.strictEqual(g.waiting(), 1);
g.set(2);
assert.deepStrictEqual(log, ["a1", "a11"], "resume shows the newest update at once");
assert.strictEqual(g.paused(), false);
// the first update of a stream shows even while paused (a page opened paused is not blank)
e = env(); g = R.createGate(0, e); log = [];
g.submit("a", () => log.push("first")); g.submit("a", () => log.push("second"));
assert.deepStrictEqual(log, ["first"]);
// flush (the Refresh button) shows what waits, still paused
g.flush(); assert.deepStrictEqual(log, ["first", "second"]); assert.strictEqual(g.paused(), true);
// changing the rate re-times what waits
e = env(); g = R.createGate(30, e); log = [];
g.submit("a", () => log.push("x1")); e.advance(1000); g.submit("a", () => log.push("x2")); e.advance(700);
g.set(2); assert.deepStrictEqual(log, ["x1", "x2"], "a faster rate shows a waiting update that is already due");
e = env(); g = R.createGate(2, e); log = [];
g.submit("a", () => log.push("y1")); e.advance(500); g.submit("a", () => log.push("y2"));
g.set(10); e.advance(3000); assert.deepStrictEqual(log, ["y1"], "a slower rate holds it"); e.advance(7000); assert.deepStrictEqual(log, ["y1", "y2"]);
// a pause also cancels a timer that was about to fire
e = env(); g = R.createGate(10, e); log = [];
g.submit("a", () => log.push("z1")); e.advance(1000); g.submit("a", () => log.push("z2"));
assert.strictEqual(e.timers.length, 1); g.set(0); assert.strictEqual(e.timers.length, 0); e.advance(60000); assert.deepStrictEqual(log, ["z1"]);
// listeners hear of a change; a broken one does not stop the others
e = env(); g = R.createGate(2, e); const heard = [];
g.onChange(() => { throw new Error("boom"); }); g.onChange((now, was) => heard.push([now, was]));
g.set(5); g.set(5); g.set("0");
assert.deepStrictEqual(heard, [[5, 2], [0, 5]], "only real changes");
assert.strictEqual(R.createGate("junk", env()).interval(), 2);

// ---- paused stops polling: a guarded timer callback does nothing while paused
e = env(); g = R.createGate(5, e);
let polls = 0; const poll = g.guard(() => { polls++; });
poll(); poll(); assert.strictEqual(polls, 2);
g.set(0); poll(); poll(); poll(); assert.strictEqual(polls, 2, "paused: the poll does not run");
g.set(30); poll(); assert.strictEqual(polls, 3, "resumed: it runs again");

// ---- paused freezes what the page remembers too (the paths), and resume catches up in order
e = env(); g = R.createGate(2, e);
const memory = [], shownLog = [];
const report = (n) => { g.note(() => memory.push(n)); g.submit("commanders", () => shownLog.push(n)); };
report(1); report(2);
assert.deepStrictEqual(memory, [1, 2], "running: the memory takes every report at once");
g.set(0);
for (let n = 3; n <= 8; n++) { report(n); e.advance(2000); }
assert.deepStrictEqual(memory, [1, 2], "paused: the paths do not grow, nothing the map draws moves");
assert.strictEqual(g.heldCount(), 6);
g.set(5);
assert.deepStrictEqual(memory, [1, 2, 3, 4, 5, 6, 7, 8], "resume: every held report is replayed in order, none lost");
assert.strictEqual(shownLog[shownLog.length - 1], 8, "...and the newest is shown, after the catch-up");
assert.strictEqual(g.heldCount(), 0);
// the Refresh button catches up while still paused
e = env(); g = R.createGate(0, e); const mem2 = [];
g.note(() => mem2.push("a")); g.note(() => mem2.push("b"));
assert.deepStrictEqual(mem2, []); g.flush(); assert.deepStrictEqual(mem2, ["a", "b"]); assert.strictEqual(g.paused(), true);
// bounded: a very long pause keeps the newest reports
e = env(); g = R.createGate(0, e); let cnt = 0, last = 0;
for (let i = 1; i <= R.HOLD_MAX + 50; i++) g.note(() => { cnt++; last = i; });
assert.strictEqual(g.heldCount(), R.HOLD_MAX); g.set(2);
assert.strictEqual(cnt, R.HOLD_MAX); assert.strictEqual(last, R.HOLD_MAX + 50);
// more than HOLD_MAX held: the replay starts after a gap, announced once before the replay
e = env(); g = R.createGate(0, e); const order = [];
g.onGap(() => order.push("gap"));
for (let i = 1; i <= R.HOLD_MAX; i++) g.note(() => {});
g.set(2); assert.deepStrictEqual(order, [], "up to the cap nothing is lost: no gap");
g.set(0); for (let i = 1; i <= R.HOLD_MAX + 1; i++) g.note(() => order.push("n"));
g.set(2); assert.strictEqual(order[0], "gap", "the gap is announced before the first replayed report"); assert.strictEqual(order.filter((x) => x === "gap").length, 1);
g.set(0); g.set(2); assert.strictEqual(order.filter((x) => x === "gap").length, 1, "and not again");
console.log("refresh ok");
