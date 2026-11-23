"use strict";
// Dashboard UI v2 switch (v1/v2 per viewer, server default) and optional 3D model viewer.
// Runs in <head> so the right skin is applied before the first paint.
// Order: ?ui=v1|v2 in the URL (remembered), then the viewer's saved choice, else v1.
(function () {
  var KEY = 'tbdash.ui';
  var ui = null;
  try {
    var q = new URLSearchParams(window.location.search).get('ui');
    if (q === 'v1' || q === 'v2') { ui = q; localStorage.setItem(KEY, q); }
    if (!ui) ui = localStorage.getItem(KEY);
  } catch (e) { /* storage blocked: fall back to v1 */ }
  document.documentElement.setAttribute('data-ui', ui === 'v2' ? 'v2' : 'v1');
})();
