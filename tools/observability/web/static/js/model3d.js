"use strict";
// UI v2 optional 3D character preview. It fills the .v2-model-stage the profile
// reserves, but only when the daemon runs with -ui-3d (window.tbUI.model3d)
// and the v2 skin is on. The viewer is loaded lazily on the first click and
// never touched when the feature is off, so v1 stays unchanged.
(function () {
  var ASSET = '/api/v1/model3d/asset/';
  var VIEWER = '/api/v1/model3d/viewer.js';
  var JQUERY = '/static/vendor/jquery-3.7.1.min.js';
  var MAX_WAIT_MS = 45000;

  var stage = null;
  var host = null;
  var guid = null;
  var model = null;
  var generation = 0;
  var loading = false;
  var viewerPromise = null;

  function v2() {
    return document.documentElement.getAttribute('data-ui') === 'v2';
  }

  function featureOn() {
    // ?ui3d=1 is the local override; otherwise the daemon's meta tag decides.
    try {
      if (new URLSearchParams(window.location.search).get('ui3d') === '1') return true;
    } catch (e) { /* old browser: fall through to the global */ }
    return !!(window.tbUI && window.tbUI.model3d === true);
  }

  function profileGuid() {
    var q = window.location.hash.split('?')[1] || '';
    var m = /(?:^|&)bot=(\d+)(?:&|$)/.exec(q);
    return m ? m[1] : null;
  }

  function ensureStage() {
    if (stage && document.body.contains(stage)) return stage;
    stage = document.querySelector('.v2-model-stage');
    if (stage || !v2()) return stage;
    var gear = document.querySelector('.armory-gear');
    if (!gear) return null;
    stage = document.createElement('div');
    stage.className = 'v2-model-stage';
    var left = gear.querySelector('#armory-gear-left');
    if (left && left.nextSibling) gear.insertBefore(stage, left.nextSibling);
    else gear.appendChild(stage);
    return stage;
  }

  function stageVisible() {
    return !!(v2() && featureOn() && guid && stage && !stage.hidden);
  }

  function clearViewer() {
    generation++;
    if (model) {
      try { model.destroy(); } catch (e) { /* already gone */ }
      model = null;
    }
    loading = false;
    if (stage && stage._m3dRO) { stage._m3dRO.disconnect(); stage._m3dRO = null; }
    if (host) host.innerHTML = '';
  }

  function renderIdle() {
    clearViewer();
    if (!stage) return;
    stage.innerHTML =
      '<div class="m3d-view"></div>' +
      '<button type="button" class="m3d-show">Show 3D</button>' +
      '<div class="m3d-bar"><span class="m3d-note">Interactive character preview</span></div>';
    host = stage.querySelector('.m3d-view');
    var btn = stage.querySelector('.m3d-show');
    if (btn) btn.addEventListener('click', start);
  }

  function renderRetry() {
    if (!stage) return;
    var bar = stage.querySelector('.m3d-bar');
    if (!bar) return;
    bar.innerHTML =
      '<span class="m3d-note">3D preview</span>' +
      '<span class="m3d-actions"><button type="button" class="m3d-retry">Retry 3D</button></span>';
    bar.querySelector('.m3d-retry').addEventListener('click', start);
  }

  function setStatus(text, isError, bottom) {
    if (!stage) return;
    var el = stage.querySelector('.m3d-status');
    if (!el) {
      el = document.createElement('div');
      el.className = 'm3d-status';
      stage.appendChild(el);
    }
    el.textContent = text;
    el.classList.toggle('is-error', !!isError);
    el.classList.toggle('m3d-status-bottom', !!bottom);
    el.hidden = false;
  }

  function hideStatus() {
    if (!stage) return;
    var el = stage.querySelector('.m3d-status');
    if (el) el.hidden = true;
  }

  function loadScript(src, check) {
    return new Promise(function (resolve, reject) {
      if (check()) { resolve(); return; }
      var s = document.createElement('script');
      s.src = src;
      s.async = true;
      s.onload = function () { check() ? resolve() : reject(new Error('script did not define its global')); };
      s.onerror = function () { reject(new Error('script failed to load')); };
      document.head.appendChild(s);
    });
  }

  function loadViewer() {
    if (viewerPromise) return viewerPromise;
    // The viewer expects a small WH shim; only debug and the image extension
    // are read by the classicplus bundle.
    if (!window.WH) window.WH = {};
    if (!window.WH.debug) window.WH.debug = function () {};
    if (!window.WH.WebP) window.WH.WebP = { getImageExtension: function () { return '.webp'; } };
    if (!window.WH.defaultAnimation) window.WH.defaultAnimation = 'Stand';
    viewerPromise = loadScript(JQUERY, function () { return !!window.jQuery; })
      .then(function () { return loadScript(VIEWER, function () { return !!window.ZamModelViewer; }); });
    viewerPromise.catch(function () { viewerPromise = null; });
    return viewerPromise;
  }

  function customizationOptions(meta, saved) {
    var options = [];
    var opts = (meta && meta.Options) || [];
    for (var i = 0; i < opts.length; i++) {
      var opt = opts[i];
      var choices = opt.Choices || [];
      var pick = null;
      for (var s = 0; s < saved.length; s++) {
        if (saved[s].optionId !== opt.Id) continue;
        for (var c = 0; c < choices.length; c++) {
          if (choices[c].Id === saved[s].choiceId) { pick = saved[s]; break; }
        }
        if (pick) break;
      }
      if (pick) { options.push(pick); continue; }
      var first = choices.slice().sort(function (a, b) { return (a.OrderIndex || 0) - (b.OrderIndex || 0); })[0];
      if (first) options.push({ optionId: opt.Id, choiceId: first.Id });
    }
    return options;
  }

  function start() {
    if (!stageVisible() || loading) return;
    var myGuid = guid;
    var myGen = ++generation;
    loading = true;
    var btn = stage.querySelector('.m3d-show');
    if (btn) btn.remove();
    setStatus('Loading 3D model…', false);

    fetch('/api/v1/model3d/character/' + encodeURIComponent(myGuid))
      .then(function (r) {
        if (!r.ok) {
          var e = new Error('character HTTP ' + r.status);
          e.character = r.status;
          throw e;
        }
        return r.json();
      })
      .then(function (data) {
        if (myGen !== generation || !stageVisible()) throw new Error('cancelled');
        return loadViewer().then(function () {
          if (myGen !== generation || !stageVisible()) throw new Error('cancelled');
          return fetch(ASSET + 'classicplus/meta/charactercustomization/' + data.model + '.json');
        }).then(function (r) {
          if (!r.ok) return null;
          return r.json();
        }).then(function (meta) {
          if (myGen !== generation || !stageVisible()) throw new Error('cancelled');
          return mount(data, customizationOptions(meta, data.customizations || []), myGen);
        });
      })
      .catch(function (err) {
        if (myGen !== generation) return;
        loading = false;
        if (String(err && err.message) === 'cancelled') return;
        // One short line: the profile and paperdoll stay usable, and a dead CDN
        // never produces a retry loop.
        setStatus(err && err.character
          ? '3D unavailable for this character.'
          : '3D unavailable (no connection to the model CDN).', true);
        renderRetry();
      });
  }

  function mount(data, options, myGen) {
    var container = window.jQuery(host);
    var w = Math.max(1, host.clientWidth || stage.clientWidth || 320);
    var h = Math.max(1, host.clientHeight || stage.clientHeight || 440);
    var aspect = w / h;
    var race = data.approximate ? 10 : data.race;
    var opts = {
      type: window.ZamModelViewer.WOW,
      contentPath: ASSET + 'classicplus/',
      container: container,
      hd: true,
      aspect: aspect,
      cls: data.class,
      charCustomization: {
        race: race,
        gender: data.gender,
        options: options,
        sheathMain: -1,
        sheathOff: -1
      },
      items: data.items || [],
      models: [{ type: window.ZamModelViewer.Wow.Types.CHARACTER, id: data.model }]
    };
    model = new window.ZamModelViewer(opts);
    if (!model) throw new Error('viewer did not start');
    var defaultZoom = -2.2;

    var bar = stage.querySelector('.m3d-bar');
    if (bar) {
      bar.innerHTML =
        '<span class="m3d-hint">Drag to rotate · Scroll to zoom</span>' +
        '<span class="m3d-actions">' +
        '<button type="button" class="m3d-zoom" data-d="-1.6" title="Zoom out">−</button>' +
        '<button type="button" class="m3d-zoom" data-d="1.6" title="Zoom in">+</button>' +
        '<button type="button" class="m3d-reset">Reset view</button>' +
        '</span>';
      var actions = bar.querySelector('.m3d-actions');
      actions.addEventListener('click', function (ev) {
        var z = ev.target.closest ? ev.target.closest('.m3d-zoom') : null;
        if (z && model && model.renderer && model.renderer.zoom) {
          var zoom = model.renderer.zoom;
          var range = zoom.range || [0.3, 4];
          var next = zoom.target + Number(z.getAttribute('data-d'));
          zoom.target = Math.max(-range[1], Math.min(-range[0], next));
          return;
        }
        if (ev.target.className === 'm3d-reset' && model && model.renderer) {
          model.setZoom(defaultZoom);
          model.setOffset(0, 0);
          if (model.cam) {
            model.renderer.azimuth = model.cam.azimuth;
            model.renderer.zenith = model.cam.zenith;
          }
        }
      });
    }

    // The renderer is created a moment after the constructor; wait for its
    // onResize hook, then poll isLoaded like Wowhead's own page glue does.
    var started = Date.now();
    var timer = setInterval(function () {
      if (myGen !== generation) { clearInterval(timer); return; }
      if (model && model.renderer) {
        try { model.aspect = aspect; model.renderer.onResize(w, h, aspect); } catch (e) { /* not ready */ }
        // The stage is narrower than Wowhead's player-housing view; its stock
        // camera crops the feet, so start fitted and reset to the same view.
        try { model.setZoom(defaultZoom); } catch (e) { /* not ready */ }
        if (model.method && model.method('isLoaded')) {
          clearInterval(timer);
          hideStatus();
          loading = false;
          model.cam = { azimuth: model.renderer.azimuth, zenith: model.renderer.zenith };
          if (data.approximate) setStatus('Approximate model: this custom race has no native preview.', false, true);
          else hideStatus();
          return;
        }
      }
      if (Date.now() - started > MAX_WAIT_MS) {
        clearInterval(timer);
        clearViewer();
        setStatus('3D preview timed out. The model service may be slow; try again.', true);
        renderRetry();
      }
    }, 250);

    if (window.ResizeObserver) {
      var ro = new ResizeObserver(function () {
        if (!model || !model.renderer) return;
        var nw = Math.max(1, host.clientWidth);
        var nh = Math.max(1, host.clientHeight);
        model.aspect = nw / nh;
        try { model.renderer.onResize(nw, nh, model.aspect); } catch (e) { /* not ready */ }
      });
      ro.observe(host);
      stage._m3dRO = ro;
    }
  }

  function sync() {
    var st = ensureStage();
    if (!st) return;
    var on = v2() && featureOn();
    var g = on ? profileGuid() : null;
    if (!g) {
      if (!st.hidden) {
        clearViewer();
        stage.innerHTML = '';
        st.hidden = true;
        st.setAttribute('aria-hidden', 'true');
      }
      guid = null;
      return;
    }
    st.hidden = false;
    st.setAttribute('aria-hidden', 'false');
    if (g !== guid) {
      guid = g;
      changeState();
    } else if (!model && !loading && !st.querySelector('.m3d-show') &&
               !st.querySelector('.m3d-retry') && !st.querySelector('canvas')) {
      renderIdle();
    }
  }

  function changeState() {
    clearViewer();
    renderIdle();
  }

  function boot() {
    sync();
    window.addEventListener('hashchange', sync);
    window.addEventListener('tbui:change', sync);
    // The drawer opens/closes without always changing which bot is open (map
    // clicks, Esc); observe just its class attribute.
    var drawer = document.getElementById('profile-drawer');
    if (drawer && window.MutationObserver) {
      new MutationObserver(sync).observe(drawer, { attributes: true, attributeFilter: ['class'] });
    }
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
