"use strict";
// Run by commandersjs_test.go under node: the Dungeons tab and the glance panel (dungeons.js). The inputs are the daemon's real answers for
// the Go fixtures (testdata/dungeons_list.json, dungeons_runs_209.json; regenerate with DG_DUMP, see dungeons_test.go in internal/dungeons).
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const Dg = require(path.resolve(process.argv[2]));
global.CmdView = require(path.join(path.dirname(path.resolve(process.argv[2])), "commanders.js"));
const read = (f) => JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", f), "utf8"));
const list = read("dungeons_list.json"), runs209 = read("dungeons_runs_209.json");
const EVIL = '<img src=x onerror=alert(1)>"&';

// ---- formatting
assert.strictEqual(Dg.fmtTime(4260), "71m 00s");
assert.strictEqual(Dg.fmtTime(45), "45s");
assert.strictEqual(Dg.fmtTime(3425.4), "57m 05s");
assert.strictEqual(Dg.fmtTime(null), "–");
assert.strictEqual(Dg.fmtTime(-1), "–");
assert.strictEqual(Dg.fmtDelta(0), "best");
assert.strictEqual(Dg.fmtDelta(500), "+8m 20s");
assert.strictEqual(Dg.fmtDelta(-65), "-1m 05s");
assert.strictEqual(Dg.fmtDelta(null), "");
assert.strictEqual(Dg.fmtPct(0.8), "80 %");
assert.strictEqual(Dg.fmtPct(null), "–");
assert.strictEqual(Dg.ago(1000, 1000 - 30), "just now");
assert.strictEqual(Dg.ago(10000, 10000 - 600), "10 min ago");
assert.strictEqual(Dg.outcomeLabel("wiped_out"), "Wiped out");
assert.strictEqual(Dg.outcomeLabel("nonsense"), "Unknown");
assert.strictEqual(Dg.runColor(0), Dg.runColor(10), "colours start over after ten runs");
assert.notStrictEqual(Dg.runColor(0), Dg.runColor(1));

// ---- empty states: say why, and how to turn tracing on
const noDir = Dg.emptyState({ meta: { dir_found: false, dir: "/srv/logs/traces" }, dungeons: [] }, false);
assert.strictEqual(noDir.title, "No runs recorded yet");
assert.ok(noDir.lines.join(" ").includes(".bot trace start"), "tracing hint with the command");
assert.ok(noDir.lines.join(" ").includes("/srv/logs/traces") && noDir.lines.join(" ").includes("TBDASH_TRACE_DIR"));
const emptyDir = Dg.emptyState({ meta: { dir_found: true, dir: "/t", files: 0 } }, false);
assert.strictEqual(emptyDir.title, "No runs recorded yet");
assert.ok(emptyDir.lines.join(" ").includes("empty") && emptyDir.lines.join(" ").includes(".bot trace"));
assert.ok(Dg.emptyState({ meta: { dir_found: true, files: 3, corrupt: 1 } }, false).lines.join(" ").includes("unreadable"));
assert.strictEqual(Dg.emptyState(list, true).title, "No runs match these filters");
assert.ok(Dg.emptyState(null, false).lines.join(" ").includes(".bot trace"), "a missing answer still explains");
const emptyTab = Dg.tabHtml({ list: { meta: { dir_found: false, dir: EVIL }, dungeons: [], facets: {} }, filter: {} });
assert.ok(emptyTab.includes("No runs recorded yet") && !emptyTab.includes(EVIL), "the folder name is escaped");
assert.ok(Dg.tabHtml({ list: null }).includes("Loading"));
assert.ok(Dg.tabHtml({ error: "HTTP 500" }).includes("Could not read"));
assert.ok(Dg.glanceHtml({ meta: { dir_found: false }, dungeons: [] }, 0).includes("No runs recorded yet"), "the glance strip says it too");

// ---- glance: the last run per dungeon
const rows = Dg.glanceRows(list, Date.parse("2026-10-17T12:00:00Z") / 1000);
assert.strictEqual(rows.length, 2);
const zf = rows.find((r) => r.map === 209), uld = rows.find((r) => r.map === 70);
assert.strictEqual(zf.outcome, "running");
assert.strictEqual(zf.timeText, "running");
assert.strictEqual(zf.deltaText, "", "a run that has not ended has no delta");
assert.strictEqual(uld.outcome, "cleared");
assert.strictEqual(uld.timeText, "78m 20s");
assert.strictEqual(uld.deltaText, "best");
assert.strictEqual(uld.deltaClass, "good");
const slower = JSON.parse(JSON.stringify(list));
Object.assign(slower.dungeons.find((d) => d.map === 70), { delta_s: 125 });
assert.strictEqual(Dg.glanceRows(slower, 0).find((r) => r.map === 70).deltaText, "+2m 05s");
assert.strictEqual(Dg.glanceRows(slower, 0).find((r) => r.map === 70).deltaClass, "bad");
const gh = Dg.glanceHtml(list, 0);
assert.ok(gh.includes("Zul&#39;Farrak") && gh.includes('href="#/dungeons?map=209"') && gh.includes("dg-cleared"));
const evilList = JSON.parse(JSON.stringify(list));
evilList.dungeons[0].name = EVIL; evilList.dungeons[0].last.run = EVIL;
assert.ok(!Dg.glanceHtml(evilList, 0).includes(EVIL) && !/<img/.test(Dg.glanceHtml(evilList, 0)), "names from the summaries are escaped");

// ---- the tab
const html = Dg.tabHtml({ list, sel: 209, runs: runs209, filter: {}, mapN: 10 });
assert.ok(html.includes("Zul&#39;Farrak") && html.includes("Uldaman"), "a button per dungeon");
assert.ok(/class="dg-pick active" data-dg-sel="209"/.test(html), "the selected dungeon is marked");
assert.ok(html.includes("80 %") && html.includes("57m 00s") && html.includes("64m 15s"), "success rate, best (3420 s) and median (3855 s)");
assert.ok(html.includes("By build") && html.includes("By driver") && html.includes("self-reported"));
assert.ok(html.includes('data-dg-f="build"') && html.includes('data-dg-f="driver"') && html.includes('data-dg-f="mode"'));
assert.ok(!html.includes("data-dg-clear"), "no clear button while no filter is set");
assert.ok(Dg.tabHtml({ list, sel: 209, runs: runs209, filter: { driver: "haiku · max" } }).includes("data-dg-clear"));
// boss table: best of each column highlighted, a boss not reached shown as missing
const table = Dg.runTable(runs209, []);
assert.ok(/<th>Antu&#39;sul<\/th><th>Theka the Martyr<\/th><th>Velratha<\/th>/.test(table), "boss columns in plan order");
const bestCells = (table.match(/class="dg-best"/g) || []).length;
// 3 boss columns + the clear column have one best each; the best row itself is not counted
assert.strictEqual(bestCells, 4, "one highlighted cell per boss and one for the clear");
assert.ok(/<td class="dg-miss">–<\/td>/.test(table), "a boss not reached");
assert.ok(table.includes("30m 00s") && table.includes("32m 00s") && table.includes("57m 00s"), "the best kill times");
assert.ok(table.includes("dg-bestrow"));
// the table lists runs newest first, with the swatch colour of the map key
const model = Dg.mapModel(runs209.runs, 10);
assert.strictEqual(model.key.length, 6);
assert.strictEqual(model.key[0].run, "zf-running");
const withSw = Dg.runTable(runs209, model.key);
assert.ok(withSw.includes("background:" + Dg.runColor(0)));

// ---- the wipe map
// ZF wipes (leg 8 group 9) at game (1636.1, 1105.3): the plane point is (-y, -x)
const m10 = Dg.mapModel(runs209.runs, 10);
const wipes = m10.points.filter((p) => p.kind === "wipe"), deaths = m10.points.filter((p) => p.kind === "death");
assert.strictEqual(wipes.length, 4);
assert.strictEqual(deaths.length, 4 + 1 + 1 + 3 + 1);
const w = wipes.find((p) => p.run === "zf-ab-1-s2" && p.group === "9");
assert.ok(Math.abs(w.x - -1105.3) < 1e-6 && Math.abs(w.y - -1636.1) < 1e-6, "projected like the Commanders tab");
assert.strictEqual(w.role, "heal");
assert.strictEqual(Dg.mapModel(runs209.runs, 2).key.length, 2, "last N runs");
assert.strictEqual(Dg.mapModel(runs209.runs, 0).key.length, 6, "0 = all");
assert.deepStrictEqual(Dg.mapModel([{ run: "a", wipes: [{ t_s: 1 }, { x: "1", y: 2 }], deaths: null }], 5).points, [], "spots without a position are skipped");
assert.deepStrictEqual(Dg.mapModel(null, 5), { points: [], key: [] });
const svg = Dg.mapSvg(m10, null);
assert.ok(/^<svg class="dg-map" viewBox="[-0-9. ]+"/.test(svg));
assert.ok(!svg.includes("NaN") && !svg.includes("Infinity"), "no broken numbers");
assert.strictEqual((svg.match(/class="dg-wipe"/g) || []).length, 4);
assert.strictEqual((svg.match(/class="dg-death"/g) || []).length, 10);
assert.ok(svg.includes("fill:" + Dg.runColor(0)) || svg.includes("stroke:" + Dg.runColor(0)) || svg.includes(Dg.runColor(1)), "dots coloured by run");
assert.ok(svg.includes("wipe (heal)") && svg.includes("leg 8, group 9"), "hover text names the spot");
assert.ok(!svg.includes("<image"), "no picture without a layer");
const pic = { url: "/api/v1/mapfiles/209.png", x: -2000, y: -2000, w: 4000, h: 4000 };
assert.ok(Dg.mapSvg(m10, pic).includes('<image class="bg" href="/api/v1/mapfiles/209.png"'), "the Commanders picture layer is used");
assert.ok(Dg.mapSvg({ points: [], key: [] }, null).includes("<svg"), "an empty model does not throw");
const evilModel = Dg.mapModel([{ run: EVIL, wipes: [{ x: 1, y: 2, role: EVIL, leg: EVIL }], deaths: [] }], 5);
const evilSvg = Dg.mapSvg(evilModel, null);
assert.ok(!evilSvg.includes(EVIL) && !/<img/.test(evilSvg), "run ids and roles are escaped in the map");
assert.ok(html.includes('data-dg-mapn="1"') && html.includes("last 10 runs"));
const noPos = Dg.tabHtml({ list, sel: 209, runs: { map: 209, name: "x", total: 1, bosses: [], runs: [{ run: "r", outcome: "stopped", wipes: [], deaths: [], wipe_count: 1, death_count: 0, bosses: [], driver: { text: "unknown" } }] }, filter: {}, mapN: 10 });
assert.ok(noPos.includes("did not record where"), "a wipe without a position says so");
// a different dungeon selected before its runs arrive
assert.ok(Dg.tabHtml({ list, sel: 70, runs: runs209, filter: {} }).includes("Loading the runs of Uldaman"));
console.log("dungeons_check ok");
