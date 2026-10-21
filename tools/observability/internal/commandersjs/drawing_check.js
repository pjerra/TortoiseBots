"use strict";
// Run by commandersjs_test.go under node: the SVG builders of the Commanders map drawing layers (in app.js) with the real helpers.
// Every string that comes from the game server (names, reasons) must leave them escaped; a missing field must not throw.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}
function grabConst(name) {
  const re = new RegExp(`^  const ${name} = `, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  };\n", m.index), line = src.indexOf(";\n", m.index);
  return src.slice(m.index, (end > 0 && end < line + 400 && src.slice(m.index, line).includes("{\n") ? end + 5 : line + 1));
}
const ctx = { console };
vm.createContext(ctx);
["commanders.js", "maplayers.js", "unitinfo.js", "paths.js", "demo.js"].forEach((f) => vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f }));
vm.runInContext(
  [grab("esc"), grabConst("CMD_MAP_ICONS"), grabConst("CMD_OBJECT_ICON"), grab("cmdMapIcon"), grab("cmdLineStyle"), grabConst("cmdPt"), grabConst("CMD_TRAIL_MARKS"), grab("cmdTrailSvg"),
    grab("cmdMemberTrailsSvg"), grab("cmdRunStepsSvg"), grab("cmdStallsSvg"), grab("cmdObjectsSvg"), grab("cmdBossRosterSvg"), grab("cmdLineSvg"), grab("cmdCombatLinesSvg"), grab("cmdMapFacts")].join("\n") +
  "\nvar cmdLayers = MapLayers.defaults(); var cmdTrails = CmdPaths.newTrails();" +
  "\nvar cmdTracker = CmdView.newTracker(); var cmdEvents = UnitInfo.newEvents(); var cmdStalls = CmdPaths.newStalls();" +
  "\nvar state = { dungeons: { runs: [] } }; var ZONE_BOUNDS = null;" +
  "\nfunction ensureCmdPics() { return undefined; } function cmdZoneRect() { return null; } function cmdNowFor() { return 1000; }", ctx);

const f = (n) => n.toFixed(2);
const mk = () => ({ f, k: 0.5, units: {} });
const EVIL = '<img src=x onerror=alert(1)>"&';

// ---- line style: thickness and opacity from the panel
vm.runInContext('cmdLayers = MapLayers.setStyle(MapLayers.setStyle(cmdLayers, "leadpath", "th", 200), "leadpath", "op", 50);', ctx);
assert.strictEqual(vm.runInContext('cmdLineStyle("leadpath", 3, 0.8)', ctx), "stroke-width:6.00px;opacity:0.40");
assert.strictEqual(vm.runInContext('cmdLineStyle("grid", 3)', ctx), "stroke-width:3.00px;opacity:1.00", "a layer without a style is drawn as normal");

// ---- trails
ctx.pts = []; for (let i = 0; i < 30; i++) ctx.pts.push([i * 5, 0, 100 + i]);
const lead = vm.runInContext('(function(){ var c = ' + JSON.stringify({}) + '; return cmdTrailSvg(' + 'ctx0' + ', "leadpath", "trail", pts, 1, 3); })', Object.assign(ctx, { ctx0: mk() }));
const svg1 = lead();
assert.ok(/<polyline class="trail" style="stroke-width:6\.00px;opacity:[0-9.]+"/.test(svg1), svg1.slice(0, 200));
assert.ok((svg1.match(/<polyline/g) || []).length >= 2, "fading chunks");
// member trails, cut by the length setting
const bind = { commander: "C", bots: [{ guid: 1, lead: true, map: 1, x: 0, y: 0 }, { guid: 2, role: "healer", name: "H", map: 1, x: 0, y: 0 }] };
ctx.bind = bind;
vm.runInContext('for (var i = 0; i < 50; i++) { bind.bots[1].x = i * 4; CmdPaths.noteTrails(cmdTrails, bind, 100 + i); }', ctx);
vm.runInContext('cmdLayers = MapLayers.setTrailLen(cmdLayers, 10);', ctx);
ctx.dc = mk();
const mtShort = vm.runInContext('cmdMemberTrailsSvg(dc, bind, 1)', ctx);
assert.ok(/class="mtrail healer"/.test(mtShort) && !/class="mtrail lead"/.test(mtShort));
const pointsOf = (s) => (s.match(/points="([^"]+)"/g) || []).join(" ").split(/[ ,]+/).filter((x) => /\d/.test(x)).length / 2;
vm.runInContext('cmdLayers = MapLayers.setTrailLen(cmdLayers, 100);', ctx);
const mtLong = vm.runInContext('cmdMemberTrailsSvg(dc, bind, 1)', ctx);
assert.ok(pointsOf(mtShort) < pointsOf(mtLong), "the length slider shortens the drawn path");

// ---- run steps
const steps = ctx.CmdPaths.runSteps({
  step: 3, plan_steps: [
    { i: 1, k: "move", x: 0, y: 0 }, { i: 2, k: "pull", x: 5, y: 5, r: 15, sk: true }, { i: 3, k: "pull", x: 9, y: 9, r: 15 },
    { i: 4, k: "boss", x: 12, y: 12, n: EVIL, r: 30 }, { i: 5, k: "rest", x: 15, y: 15 }, { i: 6, k: "open", x: 18, y: 18 }, { i: 120, k: "pull", x: 20, y: 20 },
  ],
});
ctx.steps = steps; ctx.dc = mk(); ctx.leadBot = { x: 1, y: 1 };
const rs = vm.runInContext("cmdRunStepsSvg(dc, steps, leadBot)", ctx);
assert.ok(!rs.includes(EVIL) && !/<img/.test(rs), "a boss name from the server is escaped");
assert.ok(rs.includes("&lt;img"), "...and still shown");
assert.ok(/class="route done"/.test(rs) && /class="route ahead"/.test(rs) && /class="route toward"/.test(rs), "route lines");
assert.ok(/class="rstep next k-pull"/.test(rs) && /class="rstep skipped k-pull"/.test(rs) && /class="rstep todo k-boss"/.test(rs), "state classes");
assert.ok(/class="steparea"/.test(rs), "the next pull shows its radius");
assert.strictEqual((rs.match(/class="stepskip"/g) || []).length, 1, "a skipped pull is struck out");
assert.ok(/class="ic ic-crown crownmark"/.test(rs) && /ic-stop/.test(rs) && /ic-door/.test(rs), "boss crown, stop mark, door");
assert.ok(/class="stepmove"/.test(rs));
assert.deepStrictEqual(Object.keys(ctx.dc.units).sort(), ["s:1", "s:120", "s:2", "s:3", "s:4", "s:5", "s:6"], "every step can be hovered");
assert.strictEqual(vm.runInContext("cmdRunStepsSvg(dc, [], null)", ctx), "", "no steps, nothing drawn");
ctx.steps = steps.slice(2, 3); ctx.dc = mk();
assert.doesNotThrow(() => vm.runInContext("cmdRunStepsSvg(dc, steps, null)", ctx), "no lead is fine");

// ---- stalls
ctx.dc = mk();
ctx.stalls = [
  { id: "p:1:2", kind: "pause", map: 1, x: 3, y: 4, reason: EVIL, at: 1, live: true },
  { id: "o:2:move", kind: "refused", map: 1, x: 5, y: 6, verb: "move", reason: "R".repeat(100), at: 2, live: false },
];
const ss = vm.runInContext("cmdStallsSvg(dc, stalls)", ctx);
assert.ok(!/<img/.test(ss) && ss.includes("&lt;img"), "a reason from the server is escaped");
assert.ok(/class="stall live"/.test(ss) && /class="stall"/.test(ss));
assert.ok(!ss.includes("R".repeat(50)) && ss.includes("R".repeat(43) + "…"), "long reasons are clipped on the map");
assert.deepStrictEqual(Object.keys(ctx.dc.units).sort(), ["x:o:2:move", "x:p:1:2"]);
assert.strictEqual((ss.match(/class="stalltext"/g) || []).length, 2);

// ---- objects
ctx.dc = mk();
ctx.objs = [
  { id: "1", name: EVIL, type: "c", state: "u", x: 1, y: 2 }, { id: "2", name: "Door", type: "d", state: "a", x: 3, y: 4 },
  { id: "3", name: "Gong", type: "g", state: "r", x: 5, y: 6 }, { id: "4", name: "Lever", type: "b", state: "r", x: 7, y: 8 }, { id: "5", name: "?", type: "r", state: "r", x: 9, y: 9 },
];
const os = vm.runInContext("cmdObjectsSvg(dc, objs)", ctx);
assert.ok(!/<img/.test(os), "object names never reach the markup");
assert.ok(/ic-chest/.test(os) && /ic-door/.test(os) && /ic-gong/.test(os) && /ic-lever/.test(os) && /ic-spark/.test(os), "an icon per type");
assert.ok(/class="obj st-u"/.test(os) && /class="obj st-a"/.test(os) && /class="obj st-r"/.test(os), "state classes");
assert.strictEqual(Object.keys(ctx.dc.units).length, 5);

// ---- combat lines: the three action-line drawings share one style key, actionlines
ctx.dc = mk();
vm.runInContext('cmdLayers = MapLayers.setStyle(MapLayers.setStyle(MapLayers.defaults(), "actionlines", "th", 200), "actionlines", "op", 50);', ctx);
const ln = vm.runInContext('cmdLineSvg(dc, "actionlines", "healline", {x: 1, y: 2}, {x: 3, y: 4}, 0.9)', ctx);
assert.ok(/<line class="healline" style="stroke-width:3\.20px;opacity:0\.45" x1="-2\.00" y1="-1\.00" x2="-4\.00" y2="-3\.00"\/>/.test(ln), ln);
ctx.clBots = [
  { name: "T", id: "1", alive: true, role: "tank", x: 0, y: 0, target: "9" },
  { name: "H", id: "2", alive: true, role: "healer", x: 1, y: 1, cast: "Heal", ctgt: "1" },
  { name: "M", id: "3", alive: true, x: 2, y: 2, cast: "Polymorph", ctgt: "9" },
];
ctx.clMobs = [{ id: "9", name: "Mob", x: 3, y: 3, reaction: "h", cc: "c" }];
ctx.dc = mk();
const cl = vm.runInContext("cmdCombatLinesSvg(dc, { bottarget: true, healline: true, ccline: true }, clBots, clMobs)", ctx);
assert.ok(/class="bottarget"/.test(cl) && /class="healline"/.test(cl) && /class="ccline"/.test(cl), cl);
assert.strictEqual((cl.match(/stroke-width:3\.00px;opacity:0\.45/g) || []).length, 1, "the bot-target line uses the actionlines style");
assert.strictEqual((cl.match(/stroke-width:3\.20px;opacity:0\.45/g) || []).length, 2, "heal and crowd-control lines too");
assert.strictEqual(vm.runInContext("cmdCombatLinesSvg(dc, {}, clBots, clMobs)", ctx), "", "no layer on, no lines");

// ---- cmdMapFacts: a Commanders card unions its run's DUNGEON_MOBS list (map + shared bot) with the feed
ctx.umRuns = [{ key: "70:1", map: 70, instance: 1, members: [{ name: "A", guid: 1 }], mobs_map: 70, mobs_total: 30,
  mobs: [{ id: "1", name: "Run Name", reaction: "h", x: 5, y: 5 }, { id: "2", name: "Extra", reaction: "h", x: 6, y: 6 },
    { id: "3", name: "Neutral", reaction: "n", x: 7, y: 7 }] }];
ctx.umBind = { commander: "C", lead: "A", bots: [{ name: "A", guid: 1, map: 70, x: 0, y: 0, role: "tank", id: "1", alive: true }],
  mobs_map: 70, mobs: [{ id: "1", name: "Feed Name", reaction: "h", x: 1, y: 1, victim: "1" }] };
vm.runInContext("state.dungeons.runs = umRuns;", ctx);
const um = vm.runInContext("cmdMapFacts(umBind)", ctx);
assert.strictEqual(um.mobs.map((m) => m.id).join(","), "1,2,3", "the union by guid");
assert.strictEqual(um.mobs[0].name, "Feed Name", "the commander feed wins the conflict");
assert.strictEqual(um.mobsTotal, 30);
assert.strictEqual(vm.runInContext("MapLayers.enemiesText(cmdMapFacts(umBind).mobs.length, cmdMapFacts(umBind).mobsTotal)", ctx), "3 of 30 enemies shown");
// no matching run: the feed only, no cap note
ctx.umOther = { commander: "C2", lead: "Z", bots: [{ name: "Z", guid: 50, map: 70, x: 0, y: 0, role: "tank", id: "50", alive: true }],
  mobs_map: 70, mobs: [{ id: "9", name: "Feed only", reaction: "h", x: 1, y: 1 }] };
const noRun = vm.runInContext("cmdMapFacts(umOther)", ctx);
assert.strictEqual(noRun.mobs.map((m) => m.id).join(","), "9", "no shared bot, no union");
assert.strictEqual(noRun.mobsTotal, 0);
assert.strictEqual(vm.runInContext("MapLayers.enemiesText(cmdMapFacts(umOther).mobs.length, cmdMapFacts(umOther).mobsTotal)", ctx), "",
  "without a run there is no total to compare: no N of M note");
// ---- the picture slots: the Map picture layer is about a PICTURE, not a list (an all-missing answer must grey it)
ctx.noPic = [{ kind: "worldmap", floor: "", label: "World map", available: false, reason: "no world map" },
  { kind: "minimap", floor: "", label: "Minimap", available: false, reason: "no minimap" },
  { kind: "navmesh", floor: "", label: "Navmesh", available: false, reason: "no navmesh" }];
ctx.onePic = [Object.assign({}, ctx.noPic[0], { available: true, url: "/api/v1/mapfiles/70.png", x: 0, y: 0, w: 10, h: 10 }),
  ctx.noPic[1], ctx.noPic[2]];
vm.runInContext("state.commanders = { realMaps: true }; ensureCmdPics = () => CmdView.pictureSlots(noPic);", ctx);
const noPic = vm.runInContext("cmdMapFacts(umOther)", ctx);
assert.strictEqual(noPic.ctx.picture, false, "three missing slots are no picture (a slots.length check would be wrong)");
assert.strictEqual(noPic.ctx.pictureWhy, "No picture of this map in the map folder");
vm.runInContext("ensureCmdPics = () => CmdView.pictureSlots(onePic);", ctx);
assert.strictEqual(vm.runInContext("cmdMapFacts(umOther).ctx.picture", ctx), true, "one available slot is a picture");
assert.strictEqual(vm.runInContext("cmdMapFacts(umOther).ctx.pictureWhy", ctx), "", "and needs no reason");
// the demo card gets its made-up slots only while the daemon has a map folder (its URLs are the daemon's files)
ctx.demoBind = { commander: "DEMO", demo: true, lead: "Simtank", bots: [{ name: "Simtank", guid: 9001, map: 70, x: 0, y: 0, role: "tank", alive: true }], orders: [] };
vm.runInContext("state.commanders = { realMaps: false };", ctx);
assert.strictEqual(vm.runInContext("cmdMapFacts(demoBind).ctx.picture", ctx), false, "no map folder, no demo picture");
vm.runInContext("state.commanders = { realMaps: true };", ctx);
assert.strictEqual(vm.runInContext("cmdMapFacts(demoBind).ctx.picture", ctx), true, "with a map folder the demo draws its own slots");
// ---- asking again: a stale list is refreshed behind what is on screen (no loading state over an existing list) and only an empty
// answer replaces it; a failed refresh never throws away a list
const picsFn = (() => { const i = src.indexOf("  function ensureCmdPics("); assert.ok(i > 0, "ensureCmdPics"); return src.slice(i, src.indexOf("\n  }\n", i)); })();
assert.ok(/CmdView\.pictureRefreshAfter\(have\)/.test(picsFn), "the retry interval comes from the slots (30 s none, 5 min partial, never complete)");
assert.ok(/if \(!cmdPics\.has\(mapId\)\) cmdPics\.set\(mapId, 'loading'\);/.test(picsFn), "only the first ask sets the loading state");
assert.ok(/if \(have === 'loading'\) return have;/.test(picsFn), "an answer in flight is not asked for again");
assert.ok(/if \(!Array\.isArray\(cmdPics\.get\(mapId\)\)\) cmdPics\.set\(mapId, null\);/.test(picsFn), "a failed refresh keeps a list on screen");

// ---- a path with a teleport is drawn in pieces, with a swirl where it broke and a skull where the bot died
{
  vm.runInContext('cmdLayers = MapLayers.defaults();', ctx);
  ctx.tpts = [[0, 0, 100], [10, 0, 102], [20, 0, 104], [800, 0, 106, "t"], [810, 0, 108], [820, 0, 110, "d"], [830, 0, 112]];
  ctx.dc = mk();
  const svg = vm.runInContext('cmdTrailSvg(dc, "leadpath", "trail", tpts, 1, 3)', ctx);
  const lines = (svg.match(/points="[^"]+"/g) || []).map((m) => m.slice(8, -1).split(" ").map((p) => Number(p.split(",")[1]) * -1)); // game x of each vertex
  assert.ok(lines.length >= 2, "pieces");
  lines.forEach((xs) => assert.ok(!(Math.min(...xs) <= 20 && Math.max(...xs) >= 800), "one polyline runs from before the teleport to after it: " + xs));
  assert.ok(lines.some((xs) => Math.max(...xs) >= 830) && lines.some((xs) => Math.min(...xs) === 0), "both sides are drawn");
  assert.strictEqual((svg.match(/ic-swirl/g) || []).length, 1, "one swirl where it broke");
  assert.strictEqual((svg.match(/ic-skull/g) || []).length, 1, "one skull where it died");
  assert.ok(/Teleported or summoned here/.test(svg) && /Died here/.test(svg), "with a hover text");
  ctx.tpts = [[0, 0, 1], [10, 0, 2], [20, 0, 3]];
  const plain = vm.runInContext('cmdTrailSvg(dc, "leadpath", "trail", tpts, 1, 3)', ctx);
  assert.ok(!/ic-swirl|ic-skull/.test(plain), "no marks on a plain walk");
}
// ---- a neutral mob must not look dead: WoW draws neutral units yellow, and the map's dead tone is grey (owner 2026-10-21: Kresh, a
// neutral turtle in Wailing Caverns, read as a dead unit while it walked). The legend shows the same colour as the map.
{
  const css = fs.readFileSync(path.join(dir, "..", "css", "app.css"), "utf8");
  const colour = (sel, prop) => {
    const i = css.indexOf(sel + " {");
    assert.ok(i >= 0, sel + " not in app.css");
    const m = new RegExp(prop + ":\\s*(#[0-9a-fA-F]{6})").exec(css.slice(i, css.indexOf("}", i)));
    assert.ok(m, sel + " has no " + prop);
    return m[1].toLowerCase();
  };
  const rgb = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16));
  const chroma = (h) => { const c = rgb(h); return Math.max(...c) - Math.min(...c); };
  const neutral = colour(".cmd-map .mob.neutral circle.body", "fill");
  assert.ok(chroma(neutral) >= 80, "neutral mobs are a colour, not a grey: " + neutral);
  assert.strictEqual(colour(".cmd-legend i.mobn", "background"), neutral, "the legend's neutral swatch is the map's");
  assert.notStrictEqual(neutral, colour(".cmd-legend i.dead", "background"));
  assert.ok(chroma(colour(".cmd-legend i.dead", "background")) < 40, "dead stays grey");
  // a neutral boss (Kresh) keeps a visible outline: the boss ring is yellow, so on a yellow body it gets its own, far from both
  const dist = (a, b) => { const x = rgb(a), y = rgb(b); return Math.hypot(x[0] - y[0], x[1] - y[1], x[2] - y[2]); };
  const nb = colour(".cmd-map .mob.neutral.boss circle.body", "stroke");
  assert.ok(dist(nb, neutral) >= 150, "neutral boss outline stands out from its body: " + nb);
  assert.ok(dist(nb, colour(".cmd-map .mob.boss circle.body", "stroke")) >= 150, "and is not the yellow boss ring");
}
// ---- the map crowns a mob the run plan names as a boss, not only one the module ranks "b"
assert.ok(/boss = on\.bossmark && CmdView\.isBoss\(m, planBosses\)/.test(src) && /const planBosses = CmdView\.bossEntries\(bind, bind\.dungeon \? state\.commanders\.list : null\)/.test(src),
  "drawCmdMap marks bosses through CmdView.isBoss with the plan's boss entries");
// ---- the boss roster layer (owner 2026-10-21): every known boss at its spawn point with its name and state, one hover entry each
{
  const J = (v) => JSON.parse(JSON.stringify(v));
  ctx.dc = mk();
  ctx.roster = [
    { entry: 6910, name: "Revelosh", x: 1, y: 2, state: "alive", source: "module", live: true, plan: true },
    { entry: 6906, name: EVIL, x: 3, y: 4, state: "dead", source: "module", live: false, plan: true },
    { entry: 7228, name: "Ironaya", x: 5, y: 6, state: "unknown", source: "plan", live: false, plan: true },
    { entry: 0, name: "By name", x: 7, y: 8, state: "dead", source: "seen", live: false, plan: false },
    { entry: 9, name: "Odd", x: 7, y: 8, state: "weird", source: "seen", live: false, plan: false }];
  const bs = vm.runInContext("cmdBossRosterSvg(dc, roster)", ctx);
  assert.ok(!/<img/.test(bs), "boss names never reach the markup unescaped");
  assert.strictEqual((bs.match(/class="bossr alive"/g) || []).length, 1);
  assert.strictEqual((bs.match(/class="bossr dead"/g) || []).length, 2);
  assert.strictEqual((bs.match(/class="bossr unknown"/g) || []).length, 2, "an unknown state letter is drawn as not seen");
  assert.strictEqual((bs.match(/class="bstrike"/g) || []).length, 2, "a killed boss's name is struck through");
  assert.strictEqual((bs.match(/class="btick"/g) || []).length, 2, "and ticked");
  assert.strictEqual((bs.match(/ic-crown bosscrown/g) || []).length, 5, "a crown for every boss");
  assert.ok(/>Revelosh</.test(bs) && /&lt;img/.test(bs), "every boss is named");
  assert.deepStrictEqual(J(Object.keys(ctx.dc.units).sort()), ["k:6906", "k:6910", "k:7228", "k:9", "k:n3"], "one hover entry per boss");
  assert.strictEqual(ctx.dc.units["k:6906"].kind, "k");
  assert.strictEqual(vm.runInContext("cmdBossRosterSvg(dc, [])", ctx), "");
  // cmdMapFacts builds the roster of the map the card shows and tells the layer it has one
  ctx.rbBind = { commander: "RB", lead: "A", bots: [{ name: "A", guid: 1, map: 70, x: 0, y: 0, role: "tank", id: "1", alive: true, lead: true }],
    mobs_map: 70, mobs: [], bosses_map: 70, bosses: [{ e: 6910, n: "Revelosh", x: 1, y: 2, s: "d", p: 1 }, { e: 2748, n: "Archaedas", x: 3, y: 4, s: "u" }] };
  const rf = vm.runInContext("cmdMapFacts(rbBind)", ctx);
  assert.deepStrictEqual(J(rf.roster.map((b) => [b.entry, b.state])), [[6910, "dead"], [2748, "unknown"]]);
  assert.strictEqual(rf.ctx.bossRoster, true); assert.strictEqual(rf.dungeonMap, true, "map 70 is a dungeon");
  ctx.rbOpen = Object.assign({}, ctx.rbBind, { commander: "RO", bosses: undefined, bots: [Object.assign({}, ctx.rbBind.bots[0], { map: 1 })] });
  const ro = vm.runInContext("cmdMapFacts(rbOpen)", ctx);
  assert.deepStrictEqual(J(ro.roster), []); assert.strictEqual(ro.ctx.bossRoster, false); assert.strictEqual(ro.dungeonMap, false, "Kalimdor is the open world");
  // a Dungeons-tab card borrows its own commander's roster (and run) and is always a dungeon
  vm.runInContext("state.commanders = { list: [rbBind] };", ctx);
  ctx.rbDgn = { commander: "dgn:70:1", dungeon: true, bots: [{ name: "A", guid: 1, map: 70, x: 0, y: 0 }] };
  const rd = vm.runInContext("cmdMapFacts(rbDgn)", ctx);
  assert.deepStrictEqual(J(rd.roster.map((b) => b.entry)), [6910, 2748]); assert.strictEqual(rd.dungeonMap, true);
  ctx.rbDgn0 = { commander: "dgn:0:9", dungeon: true, bots: [{ name: "Q", guid: 77, map: 0, x: 0, y: 0 }] };
  assert.strictEqual(vm.runInContext("cmdMapFacts(rbDgn0)", ctx).dungeonMap, true, "a Dungeons-tab card always counts its bosses, whatever its map id");
  vm.runInContext("state.commanders = undefined;", ctx);
}
// ---- the wiring: the roster is drawn under the mobs when its layer is on, hovered as a boss, and the run line always takes the roster
// and the dungeon flag (owner 2026-10-21: "bosses 3 / 8 killed" wherever the run line is shown)
{
  const draw = grab("drawCmdMap");
  assert.ok(/if \(on\.bossroster\) g \+= cmdBossRosterSvg\(dc, facts\.roster\);/.test(draw), "drawCmdMap draws the roster");
  assert.ok(draw.indexOf("cmdBossRosterSvg(") < draw.indexOf("shown.forEach("), "under the live mobs");
  assert.ok(/UnitInfo\.runInfo\(bind, cmdEvents, now, facts\.roster, \{ dungeon: facts\.dungeonMap, run: facts\.bossRun \}\)/.test(draw), "the run line counts the roster");
  assert.ok(/run\.title = ri\.title/.test(draw), "and names the bosses outside the plan");
  assert.ok(/run\.hidden = !on\.runinfo;/.test(draw), "the run line hangs on its own layer only");
  assert.ok(/u\.kind === 'k' \? UnitInfo\.describeBoss\(u\.obj, ctx\)/.test(grab("paintCmdTip")), "a roster mark is hovered as a boss");
  assert.ok(/o\.bossroster \?/.test(grab("cmdLegendHtml")), "the legend shows the roster's crowns");
  assert.strictEqual((src.match(/UnitInfo\.noteEvents\(cmdEvents, bind, (now|updatedAt), CmdView\.bossEntries\(bind\)\)/g) || []).length, 2, "kills of plan bosses are events");
}
console.log("drawing ok");
