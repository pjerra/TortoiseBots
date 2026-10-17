"use strict";
// Dungeons tab and glance panel: formatting, wording and the HTML / SVG they are drawn from, free of the page so a test can run it under
// node (internal/commandersjs). app.js fetches /api/v1/dungeons, keeps the state and sets innerHTML; nothing here touches the DOM.
// The wipe map uses the Commanders tab's plane (CmdView.project: north up, a game position x north / y west lands at (-y, -x)) and its
// picture layers, so a map picture the operator set up for the Commanders tab shows here too.
(function (root) {
  const cmd = () => (typeof CmdView !== "undefined" ? CmdView : require("./commanders.js"));

  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
  const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);

  const OUTCOMES = {
    cleared: "Cleared", partial: "Partial", wiped_out: "Wiped out", stopped: "Stopped", abandoned: "Abandoned", running: "Running", unknown: "Unknown",
  };
  const outcomeLabel = (o) => OUTCOMES[o] || OUTCOMES.unknown;
  // Run colours for the wipe map: ten that stay apart on a dark map; the 11th run starts over.
  const PALETTE = ["#58a6ff", "#f0883e", "#3fb950", "#d2a8ff", "#ff7b72", "#e3b341", "#39c5cf", "#f778ba", "#a5d6ff", "#8b949e"];
  const runColor = (i) => PALETTE[((i % PALETTE.length) + PALETTE.length) % PALETTE.length];
  const MAP_W = 640, MAP_H = 420;
  const MAP_RUN_CHOICES = [5, 10, 25, 0]; // 0 = every run in the table

  // 4260 -> "71m 00s", 45 -> "45s", 3 -> "3s"; no time -> an en dash.
  function fmtTime(sec) {
    const s = num(sec);
    if (s === null || s < 0) return "–";
    const t = Math.round(s), m = Math.floor(t / 60), r = t % 60;
    return m > 0 ? m + "m " + String(r).padStart(2, "0") + "s" : r + "s";
  }
  // The difference to the best: "best" at 0, "+8m 20s" slower, "-1m 05s" faster.
  function fmtDelta(sec) {
    const s = num(sec);
    if (s === null) return "";
    if (Math.round(s) === 0) return "best";
    return (s > 0 ? "+" : "-") + fmtTime(Math.abs(s));
  }
  function fmtPct(rate) {
    const r = num(rate);
    return r === null ? "–" : Math.round(r * 100) + " %";
  }
  function fmtDate(unix) {
    const u = num(unix);
    if (!u) return "–";
    const d = new Date(u * 1000), p = (n) => String(n).padStart(2, "0");
    return d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate()) + " " + p(d.getHours()) + ":" + p(d.getMinutes());
  }
  function ago(now, unix) {
    const u = num(unix);
    if (!u) return "";
    const d = Math.max(0, now - u);
    if (d < 90) return "just now";
    if (d < 3600) return Math.round(d / 60) + " min ago";
    if (d < 172800) return Math.round(d / 3600) + " h ago";
    return Math.round(d / 86400) + " d ago";
  }

  // ---- empty states -----------------------------------------------------------------------------------------------------------

  // What the tab says when there is nothing to list. filtered: a build / driver / mode filter is set.
  function emptyState(list, filtered) {
    const meta = (list && list.meta) || {};
    const hint = (list && list.hint) || "Run tracing is switched on in the server config (AiPlayerbot.RunTrace.Enabled = 1); a GM then starts a run with .bot trace start <run_id> and ends it with .bot trace stop.";
    const lines = [];
    let title;
    if (filtered) {
      title = "No runs match these filters";
      lines.push("Clear a filter to see the other runs.");
    } else if (!meta.dir_found) {
      title = "No runs recorded yet";
      lines.push("The dashboard found no run folder" + (meta.dir ? " (it looked at " + meta.dir + ")" : "") + ". Tracing is probably off.");
      lines.push(hint);
      lines.push("If the server already writes runs, point the dashboard at its folder with TBDASH_TRACE_DIR (or -trace-dir): the server's AiPlayerbot.RunTrace.Dir, under its LogsDir.");
    } else if (!meta.files) {
      title = "No runs recorded yet";
      lines.push("The run folder " + (meta.dir || "") + " is empty.");
      lines.push(hint);
    } else {
      title = "No dungeon runs in the run folder yet";
      lines.push(meta.files + " summar" + (meta.files === 1 ? "y was" : "ies were") + " found in " + (meta.dir || "the folder") + ", but none is a dungeon run.");
      lines.push(hint);
    }
    if (meta.corrupt) lines.push(meta.corrupt + " file" + (meta.corrupt === 1 ? " was" : "s were") + " unreadable and skipped.");
    return { title, lines };
  }

  function emptyHtml(list, filtered) {
    const e = emptyState(list, filtered);
    return '<div class="card dg-empty"><h2 class="dg-empty-title">' + esc(e.title) + "</h2>"
      + e.lines.map((l) => '<p class="empty-hint">' + esc(l) + "</p>").join("") + "</div>";
  }

  // ---- glance panel -----------------------------------------------------------------------------------------------------------

  // One row per dungeon (the list is newest run first): the last run's result, time and the difference to the best.
  function glanceRows(list, now) {
    return ((list && list.dungeons) || []).map((d) => {
      const l = d.last || {};
      let timeText = "", deltaText = "", deltaClass = "";
      if (l.outcome === "cleared" && num(l.clear_s) !== null) {
        timeText = fmtTime(l.clear_s);
        deltaText = fmtDelta(d.delta_s);
        deltaClass = deltaText === "best" ? "good" : deltaText ? "bad" : "";
      } else if (l.outcome === "running") {
        timeText = "running";
      } else {
        timeText = l.furthest || (num(l.duration_s) ? fmtTime(l.duration_s) : "");
      }
      return {
        map: d.map, name: d.name, outcome: l.outcome || "unknown", label: outcomeLabel(l.outcome), timeText, deltaText, deltaClass,
        driver: l.driver || "unknown", ago: ago(now, l.started), best: d.best_s, run: l.run,
      };
    });
  }

  function glanceHtml(list, now) {
    const rows = glanceRows(list, now);
    if (!rows.length) {
      const e = emptyState(list, false);
      return '<span class="dg-g-empty">' + esc(e.title) + " &middot; " + esc(e.lines[e.lines.length > 1 ? 1 : 0] || "") + "</span>";
    }
    return rows.slice(0, 6).map((r) =>
      '<a class="dg-g-item" href="#/dungeons?map=' + esc(r.map) + '" title="' + esc(r.run + " · " + r.driver + " · " + r.ago) + '">'
      + '<span class="dg-g-name">' + esc(r.name) + "</span>"
      + '<span class="dg-pill dg-' + esc(r.outcome) + '">' + esc(r.label) + "</span>"
      + '<span class="dg-g-time">' + esc(r.timeText) + "</span>"
      + (r.deltaText ? '<span class="dg-g-delta ' + esc(r.deltaClass) + '">' + esc(r.deltaText) + "</span>" : "")
      + "</a>").join("");
  }

  // ---- the wipe map -----------------------------------------------------------------------------------------------------------

  // The runs shown on the map (newest n; 0 = all) with a colour each, and their wipe and death spots as points in the drawing plane.
  // Colours follow the position in the table, so a run keeps its colour in the table and on the map. Spots without a position are skipped.
  function mapModel(runs, n) {
    const C = cmd();
    const all = runs || [];
    const shown = n > 0 ? all.slice(0, n) : all;
    const points = [];
    const key = [];
    shown.forEach((r, i) => {
      const color = runColor(i);
      let wipes = 0, deaths = 0;
      (r.wipes || []).forEach((s) => {
        if (num(s.x) === null || num(s.y) === null) return;
        const p = C.project(s.x, s.y);
        points.push({ kind: "wipe", run: r.run, color, x: p.x, y: p.y, t: s.t_s, leg: s.leg, group: s.pull_group, role: s.role });
        wipes++;
      });
      (r.deaths || []).forEach((s) => {
        if (num(s.x) === null || num(s.y) === null) return;
        const p = C.project(s.x, s.y);
        points.push({ kind: "death", run: r.run, color, x: p.x, y: p.y, t: s.t_s, leg: s.leg, group: s.pull_group, role: s.role });
        deaths++;
      });
      key.push({ run: r.run, color, wipes, deaths });
    });
    return { points, key };
  }

  const spotTitle = (p) => p.run + ": " + (p.kind === "wipe" ? "wipe" : "death") + (p.role ? " (" + p.role + ")" : "")
    + (num(p.t) !== null ? " at " + fmtTime(p.t) : "") + (p.leg ? ", leg " + p.leg : "") + (p.group ? ", group " + p.group : "");

  // SVG of the model. pic: {url, x, y, w, h} (a CmdView.pictureLayers entry) or null.
  function mapSvg(model, pic) {
    const C = cmd();
    const pts = model.points.map((p) => ({ x: p.x, y: p.y }));
    const v = C.fitView(pts, MAP_W, MAP_H, 40, 120), box = C.viewBox(v, MAP_W, MAP_H), k = 1 / v.scale;
    const f = (x) => x.toFixed(1), step = C.gridStep(v.scale), grid = C.gridLines(box, step);
    let g = "";
    if (pic) g += '<image class="bg" href="' + esc(pic.url) + '" x="' + pic.x + '" y="' + pic.y + '" width="' + pic.w + '" height="' + pic.h + '" preserveAspectRatio="none"/>';
    grid.xs.forEach((x) => { g += '<line class="grid" x1="' + f(x) + '" y1="' + f(box[1]) + '" x2="' + f(x) + '" y2="' + f(box[1] + box[3]) + '"/>'; });
    grid.ys.forEach((y) => { g += '<line class="grid" x1="' + f(box[0]) + '" y1="' + f(y) + '" x2="' + f(box[0] + box[2]) + '" y2="' + f(y) + '"/>'; });
    // deaths first, wipes (bigger crosses) on top
    model.points.filter((p) => p.kind === "death").forEach((p) => {
      g += '<circle class="dg-death" cx="' + f(p.x) + '" cy="' + f(p.y) + '" r="' + f(4.5 * k) + '" style="fill:' + esc(p.color) + '"><title>' + esc(spotTitle(p)) + "</title></circle>";
    });
    model.points.filter((p) => p.kind === "wipe").forEach((p) => {
      const s = 8 * k;
      g += '<g class="dg-wipe" style="stroke:' + esc(p.color) + '"><title>' + esc(spotTitle(p)) + "</title>"
        + '<circle class="hit" cx="' + f(p.x) + '" cy="' + f(p.y) + '" r="' + f(s * 1.3) + '"/>'
        + '<line x1="' + f(p.x - s) + '" y1="' + f(p.y - s) + '" x2="' + f(p.x + s) + '" y2="' + f(p.y + s) + '"/>'
        + '<line x1="' + f(p.x - s) + '" y1="' + f(p.y + s) + '" x2="' + f(p.x + s) + '" y2="' + f(p.y - s) + '"/></g>';
    });
    const bar = C.scaleBar(v.scale, 110);
    g += '<g class="dg-scale"><line x1="' + f(box[0] + 12 * k) + '" y1="' + f(box[1] + box[3] - 12 * k) + '" x2="' + f(box[0] + 12 * k + bar.yd) + '" y2="' + f(box[1] + box[3] - 12 * k)
      + '"/><text x="' + f(box[0] + 12 * k) + '" y="' + f(box[1] + box[3] - 17 * k) + '" style="font-size:' + f(11 * k) + 'px">' + bar.yd + " yd</text></g>";
    return '<svg class="dg-map" viewBox="' + box.map(f).join(" ") + '" preserveAspectRatio="xMidYMid meet" role="img" aria-label="Wipe and death spots">' + g + "</svg>";
  }

  // ---- the tab ----------------------------------------------------------------------------------------------------------------

  const kpi = (label, value, sub, cls) =>
    '<div class="kpi"><div class="kpi-label">' + esc(label) + '</div><div class="kpi-value ' + (cls || "") + '">' + value + '</div><div class="kpi-sub">' + sub + "</div></div>";

  function groupTable(title, groups, note) {
    if (!groups || !groups.length) return "";
    return '<div class="card dg-card"><div class="card-head"><h2 class="card-title">' + esc(title) + "</h2></div>"
      + (note ? '<p class="empty-hint dg-note">' + esc(note) + "</p>" : "")
      + '<div class="table-container"><table class="data-table"><thead><tr><th>' + esc(title.replace("By ", "")) + "</th><th>Runs</th><th>Cleared</th><th>Best</th><th>Median</th><th>Wipes</th></tr></thead><tbody>"
      + groups.map((g) => "<tr><td>" + esc(g.label) + "</td><td>" + g.runs + "</td><td>" + fmtPct(g.success_rate) + " <span class=\"dg-dim\">(" + g.cleared + "/" + g.runs + ")</span></td><td>"
        + fmtTime(g.best_s) + "</td><td>" + fmtTime(g.median_s) + "</td><td>" + g.wipes + "</td></tr>").join("")
      + "</tbody></table></div></div>";
  }

  // The run table: one row per run with the kill time of each boss; the best of each column is highlighted.
  function runTable(rv, shownKey) {
    const bosses = rv.bosses || [], runs = rv.runs || [];
    const color = {};
    (shownKey || []).forEach((s) => { color[s.run] = s.color; });
    let h = '<div class="table-container dg-scroll"><table class="data-table dg-runs"><thead><tr><th>Run</th><th>Build</th><th>Driver</th><th>Result</th><th>Clear</th>'
      + bosses.map((b) => "<th>" + esc(b.name) + "</th>").join("") + "<th>Wipes</th><th>Deaths</th></tr></thead><tbody>";
    h += '<tr class="dg-bestrow"><td colspan="4">Best</td><td></td>'
      + bosses.map((b) => '<td title="' + esc(b.best_run || "") + '">' + fmtTime(b.best_s) + "</td>").join("") + "<td></td><td></td></tr>";
    const bestClear = runs.reduce((m, r) => (num(r.clear_s) !== null && (m === null || r.clear_s < m) ? r.clear_s : m), null);
    runs.forEach((r) => {
      const byKey = {};
      (r.bosses || []).forEach((b) => { byKey[b.entry ? String(b.entry) : String(b.name || "").toLowerCase()] = b; });
      const sw = color[r.run] ? '<i class="dg-sw" style="background:' + esc(color[r.run]) + '"></i>' : '<i class="dg-sw dg-sw-off"></i>';
      h += "<tr><td title=\"" + esc(r.run) + '">' + sw + esc(fmtDate(r.started)) + ' <span class="dg-dim">' + esc(r.run) + "</span></td><td>" + esc(r.build || "–") + "</td><td>" + esc((r.driver && r.driver.text) || "unknown")
        + '</td><td><span class="dg-pill dg-' + esc(r.outcome) + '" title="' + esc(r.stop_reason || "") + '">' + esc(outcomeLabel(r.outcome)) + "</span>"
        + (r.outcome !== "cleared" && r.furthest ? ' <span class="dg-dim">' + esc(r.furthest) + "</span>" : "") + "</td>"
        + '<td class="' + (num(r.clear_s) !== null && r.clear_s === bestClear ? "dg-best" : "") + '">' + fmtTime(r.clear_s) + "</td>";
      bosses.forEach((b) => {
        const kb = byKey[b.key];
        const t = kb ? num(kb.killed_at_s) : null;
        const cls = t === null ? "dg-miss" : num(b.best_s) !== null && t === b.best_s ? "dg-best" : "";
        h += '<td class="' + cls + '">' + (t === null ? "–" : fmtTime(t)) + "</td>";
      });
      h += "<td>" + (r.wipe_count || 0) + "</td><td>" + (r.death_count || 0) + "</td></tr>";
    });
    return h + "</tbody></table></div>";
  }

  function selectHtml(name, label, values, cur, anyLabel) {
    return '<label class="dg-filter">' + esc(label) + ' <select data-dg-f="' + esc(name) + '"><option value="">' + esc(anyLabel) + "</option>"
      + values.map((v) => '<option value="' + esc(v) + '"' + (v === cur ? " selected" : "") + ">" + esc(v) + "</option>").join("") + "</select></label>";
  }

  // ctx: {list, error, sel (map id), runs (RunsView for sel), filter {build, driver, mode}, mapN, pic}
  function tabHtml(ctx) {
    const list = ctx.list, filter = ctx.filter || {};
    const filtered = !!(filter.build || filter.driver || filter.mode);
    if (ctx.error) return '<div class="card dg-empty"><h2 class="dg-empty-title">Could not read the run data</h2><p class="empty-hint">' + esc(ctx.error) + "</p></div>";
    if (!list) return '<div class="card dg-empty"><p class="empty-hint">Loading the runs…</p></div>';
    const fc = list.facets || {};
    const filters = '<div class="card dg-filters">' + selectHtml("build", "Build", fc.builds || [], filter.build, "all builds")
      + selectHtml("driver", "Driver", fc.drivers || [], filter.driver, "all drivers") + selectHtml("mode", "Mode", fc.modes || [], filter.mode, "all modes")
      + (filtered ? '<button type="button" class="btn" data-dg-clear="1">Clear filters</button>' : "")
      + '<span class="empty-hint dg-note" title="The server cannot tell who drove a run. The value is reported by whoever started it.">Driver is self-reported.</span></div>';
    const ds = list.dungeons || [];
    if (!ds.length) return (filtered || (list.meta && list.meta.runs) ? filters : "") + emptyHtml(list, filtered);
    const d = ds.find((x) => x.map === ctx.sel) || ds[0];

    const picker = '<div class="dg-picker" role="tablist">' + ds.map((x) =>
      '<button type="button" role="tab" class="dg-pick' + (x.map === d.map ? " active" : "") + '" data-dg-sel="' + esc(x.map) + '"><span class="dg-pick-name">' + esc(x.name) + "</span>"
      + '<span class="dg-pick-sub">' + x.runs + " run" + (x.runs === 1 ? "" : "s") + " · " + fmtPct(x.success_rate) + " · best " + fmtTime(x.best_s) + "</span></button>").join("") + "</div>";

    const l = d.last || {};
    const kpis = '<div class="kpi-row">'
      + kpi("Runs", String(d.runs), d.cleared + " cleared, " + (d.partial + d.wiped_out + d.stopped + d.abandoned) + " not" + (d.running ? ", " + d.running + " running" : "") + (d.unknown ? ", " + d.unknown + " unknown" : ""))
      + kpi("Success", fmtPct(d.success_rate), "full clears of finished runs")
      + kpi("Best clear", fmtTime(d.best_s), "entry to the last boss", d.best_s != null ? "kpi-green" : "")
      + kpi("Median clear", fmtTime(d.median_s), "of the full clears")
      + kpi("Last run", esc(outcomeLabel(l.outcome)), (l.outcome === "cleared" ? fmtTime(l.clear_s) + (d.delta_s != null ? " (" + esc(fmtDelta(d.delta_s)) + " vs best)" : "") : esc(l.furthest || "")))
      + kpi("Wipes / deaths", d.wipes + " / " + d.deaths, "over these runs", d.wipes ? "kpi-red" : "")
      + "</div>";

    let body = "";
    const rv = ctx.runs && ctx.runs.map === d.map ? ctx.runs : null;
    if (!rv) {
      body = '<div class="card dg-card"><p class="empty-hint">Loading the runs of ' + esc(d.name) + "…</p></div>";
    } else {
      const model = mapModel(rv.runs, ctx.mapN == null ? 10 : ctx.mapN);
      const choices = MAP_RUN_CHOICES.map((n) => '<option value="' + n + '"' + (n === (ctx.mapN == null ? 10 : ctx.mapN) ? " selected" : "") + ">" + (n ? "last " + n + " runs" : "all runs") + "</option>").join("");
      const mapCard = '<div class="card dg-card"><div class="card-head"><h2 class="card-title">Wipe and death map</h2>'
        + '<label class="dg-filter">Show <select data-dg-mapn="1">' + choices + "</select></label></div>"
        + (model.points.length
          ? '<div class="dg-mapwrap">' + mapSvg(model, ctx.pic || null) + "</div>"
            + '<div class="dg-legend"><span class="lic"><svg viewBox="-6 -6 12 12" class="dg-ic"><path d="M-4-4L4 4M-4 4L4-4" class="dg-x"/></svg> wipe</span><span class="lic"><svg viewBox="-6 -6 12 12" class="dg-ic"><circle r="3.5" class="dg-dot"/></svg> death</span>'
            + model.key.map((s) => '<span class="lic"><i class="dg-sw" style="background:' + esc(s.color) + '"></i>' + esc(s.run) + " (" + s.wipes + "/" + s.deaths + ")</span>").join("") + "</div>"
            + (ctx.pic ? "" : '<p class="empty-hint dg-note">Plain grid: no map picture for this dungeon (the dashboard shows the same pictures as the Commanders tab; they need TBDASH_MAP_DIR).</p>')
          : '<p class="empty-hint">No wipe or death with a position in these runs' + ((rv.runs || []).some((r) => r.wipe_count || r.death_count) ? " (the server did not record where)." : ".") + "</p>")
        + "</div>";
      const shown = (rv.runs || []).slice(0, 200);
      body = '<div class="card dg-card"><div class="card-head"><h2 class="card-title">Boss kill times and runs</h2><span class="empty-hint">Minutes into the run, best of each boss highlighted'
        + (rv.total > shown.length ? "; newest " + shown.length + " of " + rv.total + " runs" : "") + "</span></div>" + runTable({ bosses: rv.bosses, runs: shown }, model.key) + "</div>"
        + '<div class="dg-grid">' + mapCard + "<div>" + groupTable("By build", d.builds, "Shows the effect of each fix.") + groupTable("By driver", d.drivers, "Model and effort, self-reported by whoever started the run.") + "</div></div>";
    }
    const meta = list.meta || {};
    const foot = '<p class="empty-hint dg-foot">' + (meta.runs || 0) + " dungeon runs read from " + esc(meta.dir || "?") + (meta.corrupt ? "; " + meta.corrupt + " unreadable file(s) skipped" : "") + ".</p>";
    return filters + picker + kpis + body + foot;
  }

  const api = { esc, fmtTime, fmtDelta, fmtPct, fmtDate, ago, outcomeLabel, runColor, emptyState, emptyHtml, glanceRows, glanceHtml, mapModel, mapSvg, runTable, tabHtml, MAP_RUN_CHOICES, OUTCOMES };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.DgView = api;
})(typeof self !== "undefined" ? self : this);
