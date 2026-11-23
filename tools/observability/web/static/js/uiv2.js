"use strict";
// Dashboard UI v2 switch (v1/v2 per viewer, server default) and optional 3D model viewer.
// Runs in <head> so the right skin is applied before the first paint.
// Order: ?ui=v1|v2 in the URL (remembered), then the viewer's saved choice, else the server default from
// the <meta name="tbdash-ui"> the daemon injects, else v1.
(function () {
  var KEY = 'tbdash.ui';

  function meta(name) {
    var el = document.querySelector('meta[name="' + name + '"]');
    return el ? (el.getAttribute('content') || '') : '';
  }

  function valid(v) {
    return v === 'v1' || v === 'v2';
  }

  function stored() {
    try { return localStorage.getItem(KEY); } catch (e) { return null; }
  }

  function remember(v) {
    try { localStorage.setItem(KEY, v); } catch (e) { /* storage blocked: the switch still works for this page */ }
  }

  function current() {
    return document.documentElement.getAttribute('data-ui') === 'v2' ? 'v2' : 'v1';
  }

  // The button names what it switches to; aria-pressed reports the skin in use.
  function syncToggle(v) {
    var btn = document.getElementById('ui-toggle');
    if (!btn) return;
    var next = v === 'v2' ? 'v1' : 'v2';
    btn.textContent = 'UI ' + next;
    btn.setAttribute('aria-pressed', v === 'v2' ? 'true' : 'false');
    btn.setAttribute('aria-label', 'Switch to UI ' + next + ' (currently UI ' + v + ')');
    btn.title = 'Switch to the ' + next + ' look. Kept in this browser.';
  }

  function apply(v) {
    document.documentElement.setAttribute('data-ui', v);
    syncToggle(v);
  }

  function set(v) {
    if (!valid(v)) return current();
    remember(v);
    apply(v);
    try {
      window.dispatchEvent(new CustomEvent('tbui:change', { detail: { version: v } }));
    } catch (e) { /* no CustomEvent: the skin has still changed */ }
    return v;
  }

  window.tbUI = {
    version: current,
    set: set,
    model3d: meta('tbdash-ui-3d') === '1'
  };

  var chosen = meta('tbdash-ui') === 'v2' ? 'v2' : 'v1';
  try {
    var q = new URLSearchParams(window.location.search).get('ui');
    if (valid(q)) {
      chosen = q;
      remember(q);
    } else {
      var saved = stored();
      if (valid(saved)) chosen = saved;
    }
  } catch (e) { /* URL or storage blocked: keep the server default */ }
  apply(chosen);

  function wire() {
    var btn = document.getElementById('ui-toggle');
    if (!btn) return;
    syncToggle(current());
    btn.addEventListener('click', function () { set(current() === 'v2' ? 'v1' : 'v2'); });
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', wire);
  else wire();
})();
