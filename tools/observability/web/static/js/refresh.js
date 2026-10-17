"use strict";
// Refresh rate of the live views: the choice "Refresh every 1 / 2 / 5 / 10 / 30 s / paused", its saved value, and the gate that lets
// the page apply a live update no more often than that. Free of the page so a test can run it under node (internal/commandersjs).
// The daemon still pushes (and the game server still emits) every 2 s; only what the browser shows is slowed down or frozen.
(function (root) {
  const OPTIONS = [1, 2, 5, 10, 30, 0]; // seconds; 0 = paused
  const DEFAULT_SEC = 2;                // the emit interval: nothing is held back until the viewer picks something slower
  const STORAGE_KEY = "tbdash.refresh.v1";
  const HOLD_MAX = 1000;                // reports kept for the catch-up after a pause (about 30 min at 2 s); older ones are dropped
  const EARLY_MS = 400;                 // an update this much short of the interval still counts (the 2 s stream jitters)

  function normalize(v) {
    const n = typeof v === "string" && /^[0-9]{1,3}$/.test(v) ? Number(v) : v;
    return typeof n === "number" && OPTIONS.indexOf(n) >= 0 ? n : DEFAULT_SEC;
  }

  function label(sec) { return sec === 0 ? "paused" : sec + " s"; }

  // The saved choice of this browser, or the default when there is none, it is junk, or the storage throws (a private window, blocked
  // site data). `storage` is whatever the page could get from window.localStorage (an object, or null where even asking threw).
  function load(storage) {
    try {
      if (!storage) return DEFAULT_SEC;
      const raw = storage.getItem(STORAGE_KEY);
      return raw == null ? DEFAULT_SEC : normalize(raw);
    } catch (e) {
      return DEFAULT_SEC;
    }
  }

  // Remember the choice. Returns whether it was stored; a storage that throws only means the choice lasts until the page is closed.
  function save(storage, sec) {
    try {
      if (!storage) return false;
      storage.setItem(STORAGE_KEY, String(normalize(sec)));
      return true;
    } catch (e) {
      return false;
    }
  }

  // A gate for streams of live updates. submit(key, fn) runs fn (the code that applies one update) right away when the stream is due,
  // else keeps only the newest fn for that key and runs it when the interval has passed. While paused nothing runs: the newest fn waits,
  // and runs the moment the viewer resumes. The first update of a key always runs (a page opened while paused still fills once), and
  // flush() runs everything waiting (the Refresh button).
  //   env: { now(): ms, setTimer(fn, ms): handle, clearTimer(handle) }
  function createGate(sec, env) {
    let interval = normalize(sec);
    const slot = {}; // key -> { last: ms of the last run or null, fn: waiting update or null, timer: handle or null }
    const listeners = [];
    let held = []; // memory notes of reports that arrived while paused, replayed on resume
    let overflowed = false; // the oldest held reports were dropped: the replay starts after a gap
    const gapListeners = [];
    function replay() {
      const q = held, gap = overflowed;
      held = []; overflowed = false;
      if (gap && q.length) gapListeners.forEach((f) => { try { f(); } catch (e) { /* ignore */ } });
      q.forEach((f) => f());
    }
    const get = (key) => slot[key] || (slot[key] = { last: null, fn: null, timer: null });

    function run(key) {
      const s = get(key);
      if (s.timer != null) { env.clearTimer(s.timer); s.timer = null; }
      const fn = s.fn;
      s.fn = null;
      if (!fn) return;
      s.last = env.now();
      fn();
    }

    function arm(key) {
      const s = get(key);
      if (s.timer != null) { env.clearTimer(s.timer); s.timer = null; }
      if (!s.fn || interval === 0) return;
      const wait = s.last == null ? 0 : Math.max(0, s.last + interval * 1000 - env.now());
      if (wait <= EARLY_MS) { run(key); return; }
      s.timer = env.setTimer(() => { s.timer = null; run(key); }, wait);
    }

    return {
      submit(key, fn) {
        const s = get(key);
        s.fn = fn;
        if (s.last == null) { run(key); return; }          // the first update always shows
        if (interval === 0) return;                         // paused: keep the newest
        arm(key);
      },
      // note(fn): fn updates what the page remembers of a live report (paths, events). Runs at once, except while paused: it is then held,
      // so nothing the page can draw moves, and replayed in order on resume (or flush), so the paths catch up in full.
      note(fn) {
        if (interval !== 0) { fn(); return; }
        held.push(fn);
        if (held.length > HOLD_MAX) { held.splice(0, held.length - HOLD_MAX); overflowed = true; }
      },
      heldCount() { return held.length; },
      interval() { return interval; },
      paused() { return interval === 0; },
      // Waiting updates (what a viewer would see change on resume).
      waiting() { return Object.keys(slot).filter((k) => slot[k].fn).length; },
      set(next) {
        const was = interval;
        interval = normalize(next);
        if (interval === was) return interval;
        if (interval !== 0) replay(); // catch up before the newest update is shown
        Object.keys(slot).forEach((k) => arm(k)); // resuming runs what waited; a new interval re-times it; pausing stops the timers
        listeners.forEach((f) => { try { f(interval, was); } catch (e) { /* a broken listener must not stop the others */ } });
        return interval;
      },
      flush() { replay(); Object.keys(slot).forEach((k) => run(k)); },
      // A timer callback that does nothing while paused: setInterval(gate.guard(poll), ms) stops polling when the viewer pauses.
      guard(fn) { return function () { if (interval !== 0) return fn.apply(this, arguments); }; },
      onChange(f) { listeners.push(f); },
      // f() runs just before a replay whose start was lost (more than HOLD_MAX reports were held): the page marks "a gap here" on its paths.
      onGap(f) { gapListeners.push(f); },
    };
  }

  const api = { HOLD_MAX, OPTIONS, DEFAULT_SEC, STORAGE_KEY, normalize, label, load, save, createGate };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.RefreshRate = api;
})(typeof self !== "undefined" ? self : this);
