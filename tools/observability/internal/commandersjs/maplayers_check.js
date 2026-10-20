"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's map layer state: the 19 layers and their defaults, the migration from
// the 32-switch v2 (and the v1 booleans), effective() (the old keys the drawing code reads), the per-layer greying rule, the mob
// union of a commander card with its dungeon run, and the "N of M" note.
const assert = require("assert");
const L = require(require("path").resolve(process.argv[2]));

// a Storage stand-in
const mem = () => { const d = {}; return { d, getItem: (k) => (k in d ? d[k] : null), setItem: (k, v) => { d[k] = String(v); } }; };
const oldAll = (v) => { const o = {}; L.OLD_KEYS.forEach((k) => { o[k] = v; }); return o; };
const oldDefaults = () => { const o = {}; L.OLD_KEYS.forEach((k) => { o[k] = L.OLD_LAYERS[k].def; }); return o; };

// ---- the 19 layers, 5 groups, and the defaults the brief asks for
const def = L.defaults();
assert.deepStrictEqual(Object.keys(def).sort(), L.keys().concat(["dim", "trailLen", "style"]).sort());
assert.strictEqual(L.keys().length, 19);
assert.deepStrictEqual(L.groups(), ["Map", "Party", "Enemies", "Lines and run", "Objects"]);
assert.deepStrictEqual(L.keys(), ["bg", "grid", "zone", "bots", "hpbars", "leadpath", "memberpaths", "partyextras", "enemies", "critters", "enemynames", "threat", "aggro",
  "actionlines", "runsteps", "events", "stalls", "runinfo", "objects"]);
["bg", "grid", "zone", "bots", "hpbars", "leadpath", "enemies", "enemynames", "threat", "runsteps", "events", "runinfo"].forEach((k) => assert.strictEqual(def[k], true, k));
["memberpaths", "partyextras", "critters", "aggro", "actionlines", "stalls", "objects"].forEach((k) => assert.strictEqual(def[k], false, k));
assert.strictEqual(L.DEFS.filter((d) => d.def).length, 12);
assert.strictEqual(new Set(L.keys()).size, L.keys().length);
L.DEFS.forEach((d) => {
  assert.ok(d.label && d.group && d.tip, d.key + " has a label, a group and a tooltip");
  assert.ok(d.label.length <= 48, d.key + " has a short label");
  assert.ok(Array.isArray(d.parts) && d.parts.length && d.parts.every((p) => L.OLD_LAYERS[p]), d.key + " names its old parts");
  assert.ok(d.line ? L.OLD_LAYERS[d.styleFrom] && L.OLD_LAYERS[d.styleFrom].line : !d.styleFrom, d.key + " takes a line style from an old line key");
});
// every old key belongs to exactly one new layer
const owned = L.DEFS.reduce((a, d) => a.concat(d.parts), []);
assert.deepStrictEqual(owned.slice().sort(), L.OLD_KEYS.slice().sort());
assert.strictEqual(new Set(owned).size, owned.length);

// ---- effective(): the old keys the drawing code reads, computed from the new ones
const eff = L.effective(def);
assert.deepStrictEqual(Object.keys(eff).sort(), L.OLD_KEYS.slice().sort());
["bg", "grid", "scale", "zone", "zonelabels", "bots", "botnames", "lead", "trail", "trailtime", "heading", "hpbars", "statusicons", "mobhostile", "mobneutral",
  "hostilenames", "bossmark", "threatcolors", "threat", "runsteps", "events", "feed", "runinfo"].forEach((k) => assert.strictEqual(eff[k], true, k));
["membertrails", "healring", "commander", "mobcritter", "aggro", "bottarget", "healline", "ccline", "stalls", "objects"].forEach((k) => assert.strictEqual(eff[k], false, k));
const e2 = L.effective(L.set(L.set(L.set(def, "enemies", false), "critters", true), "bots", false));
assert.strictEqual(e2.mobhostile, false); assert.strictEqual(e2.mobneutral, false); assert.strictEqual(e2.mobcritter, true);
assert.strictEqual(e2.bots, false); assert.strictEqual(e2.botnames, false); assert.strictEqual(e2.lead, false);

// ---- availability: greyed only when NONE of a layer's parts has data
let av = L.availability({});
assert.deepStrictEqual(Object.keys(av).sort(), L.keys().slice().sort());
["grid", "hpbars", "runinfo"].forEach((k) => assert.strictEqual(av[k].ok, true, k));
let grey = ["bg", "zone", "bots", "leadpath", "memberpaths", "partyextras", "enemies", "critters", "enemynames", "threat", "aggro", "actionlines", "runsteps", "events", "stalls", "objects"];
grey.forEach((k) => { assert.strictEqual(av[k].ok, false, k); assert.ok(av[k].why.length > 5, k + " says why"); });
// the map folder's own reason (the missing minimap's) stands in for the built-in one of the map picture
assert.strictEqual(L.availability({ pictureWhy: "No minimap picture: indoor dungeon" }).bg.why, "No minimap picture: indoor dungeon");
assert.strictEqual(L.availability({ picture: true, pictureWhy: "ignored" }).bg.ok, true);
assert.strictEqual(L.availability({ pictureWhy: "" }).bg.why, L.OLD_LAYERS.bg.why);
// bots: the dots and names need bots, the lead mark needs the lead; any one is enough
av = L.availability({ lead: true });
assert.strictEqual(av.bots.ok, true, "the lead mark alone makes the party layer available");
av = L.availability({ mobs: true });
["enemies", "critters", "enemynames", "aggro"].forEach((k) => assert.strictEqual(av[k].ok, true, k));
assert.strictEqual(av.threat.ok, false, "threat colours need the targets, not just the mobs");
assert.strictEqual(av.actionlines.ok, false);
av = L.availability({ mobs: true, mobTargets: true, activity: true, events: true });
["threat", "actionlines", "events"].forEach((k) => assert.strictEqual(av[k].ok, true, k));
av = L.availability({ zone: true });
assert.strictEqual(av.zone.ok, true);
assert.strictEqual(L.availability({ zonesAround: true }).zone.ok, true, "either zone source is enough");
av = L.availability({ healer: true });
assert.strictEqual(av.partyextras.ok, true);
assert.strictEqual(L.availability({ commanderPos: true }).partyextras.ok, true);
// several maps: available when any can draw it
const m = L.mergeAvailability([L.availability({}), L.availability({ mobs: true })]);
assert.strictEqual(m.enemies.ok, true); assert.strictEqual(m.grid.ok, true); assert.strictEqual(m.threat.ok, false);
assert.strictEqual(L.mergeAvailability([]).grid.ok, false);
// drawn = chosen by the new key, its merged layer available, and its OWN data there
const act = L.active(L.set(def, "enemies", false), L.availability({ mobs: true }), { mobs: true });
assert.strictEqual(act.mobhostile, false); assert.strictEqual(act.hostilenames, true); assert.strictEqual(act.mobcritter, false);
assert.strictEqual(act.grid, true); assert.strictEqual(act.runsteps, false, "no plan steps, no route");
assert.strictEqual(L.active(def, L.availability({ runSteps: true }), { runSteps: true }).runsteps, true, "the run route is drawn once the run leader sends plan steps");
const act2 = L.active(def, L.availability({}));
assert.strictEqual(act2.mobhostile, false, "chosen but no data");
assert.strictEqual(act2.grid, true); assert.strictEqual(act2.hpbars, true);
// one part's data does not switch another part of the same layer on
let part = L.active(def, L.availability({ mobs: true }), { mobs: true });
assert.strictEqual(part.hpbars, true); assert.strictEqual(part.statusicons, false, "no activity data: no status icons");
assert.strictEqual(part.hostilenames, true); assert.strictEqual(part.bossmark, true);
part = L.active(def, L.availability({ lead: true }), { lead: true });
assert.strictEqual(part.lead, true); assert.strictEqual(part.bots, false); assert.strictEqual(part.botnames, false, "no bot on this map");
part = L.active(L.set(def, "partyextras", true), L.availability({ healer: true }), { healer: true });
assert.strictEqual(part.healring, true); assert.strictEqual(part.commander, false, "only a healer: no commander mark");
part = L.active(def, L.availability({ trail: true }), { trail: true });
assert.strictEqual(part.trail, true); assert.strictEqual(part.trailtime, true); assert.strictEqual(part.heading, false, "no heading");
part = L.active(L.set(def, "enemies", true), L.availability({ mobs: true }), {});
assert.strictEqual(part.mobhostile, false, "the merged layer is available but this part's data is missing");
assert.strictEqual(part.grid, true, "a part that needs nothing is drawn");

// ---- one global choice, kept once
const st = mem();
assert.deepStrictEqual(L.load(st), def);                    // nothing stored: the defaults
let s = L.set(L.load(st), "grid", false);
s = L.set(s, "enemies", false);
assert.strictEqual(L.save(st, s), true);
assert.strictEqual(Object.keys(st.d).length, 1);             // one key, not one per map
assert.strictEqual(Object.keys(st.d)[0], L.STORAGE_KEY);
assert.strictEqual(L.STORAGE_KEY, "tbdash.mapLayers.v3");
const again = L.load(st);
assert.strictEqual(again.grid, false); assert.strictEqual(again.enemies, false); assert.strictEqual(again.bots, true);
assert.strictEqual(L.set(def, "mobhostile", true).mobhostile, undefined, "an old key is not a switch any more");
assert.strictEqual(L.set(def, "nope", true).nope, undefined);

// ---- bad storage never breaks the page
assert.deepStrictEqual(L.load(null), def);
assert.deepStrictEqual(L.load({ getItem() { throw new Error("blocked"); } }), def);
assert.strictEqual(L.save({ setItem() { throw new Error("full"); } }, def), false);
assert.strictEqual(L.save(null, def), false);
const junk = mem(); junk.d[L.STORAGE_KEY] = "{not json";
assert.deepStrictEqual(L.load(junk), def);
junk.d[L.STORAGE_KEY] = JSON.stringify({ grid: "yes", bots: false, zzz: true, trail: null, mobhostile: false });
const jl = L.load(junk);
assert.strictEqual(jl.grid, true);  // a non-boolean falls back to the default
assert.strictEqual(jl.bots, false); // a boolean is kept
assert.strictEqual(jl.zzz, undefined);
assert.strictEqual(jl.trail, undefined, "old v2 keys stored in v3 are ignored");
junk.d[L.STORAGE_KEY] = "[1,2]";
assert.deepStrictEqual(L.load(junk), def);

// ---- v2 -> v3: a merged layer is on when ANY of its old layers was; dim, path length and line styles carry over
const v2 = mem();
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldDefaults());
let mig = L.load(v2);
L.keys().forEach((k) => assert.strictEqual(mig[k], def[k], "untouched v2 defaults: the new default of " + k));
assert.strictEqual(mig.enemies, true, "a v2 that never touched the mob switches gets the new enemies default");
assert.strictEqual(mig.threat, true); assert.strictEqual(mig.events, true); assert.strictEqual(mig.runinfo, true);
assert.strictEqual(mig.dim, 30); assert.strictEqual(mig.trailLen, 120);
assert.deepStrictEqual(mig.style.leadpath, { op: 100, th: 100 }, "no old style: the normal look");

v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldAll(false));
mig = L.load(v2);
// the layers whose old switches were all off by default cannot tell "untouched" from "all off": they take the new default
["enemies", "threat", "events", "runinfo"].forEach((k) => assert.strictEqual(mig[k], true, k + " takes the new default"));
L.keys().filter((k) => ["enemies", "threat", "events", "runinfo"].indexOf(k) < 0).forEach((k) => assert.strictEqual(mig[k], false, k + " stays off"));

v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldAll(true));
mig = L.load(v2);
L.keys().forEach((k) => assert.strictEqual(mig[k], true, k + " on: an old switch was on or its source default was on"));

const mixed = oldDefaults();
Object.assign(mixed, { grid: false, botnames: false, lead: false, mobhostile: true, hostilenames: true, bossmark: false,
  threatcolors: true, events: true, runinfo: true, membertrails: true });
mixed.dim = 55; mixed.trailLen = 60; mixed.style = { trail: { op: 40, th: 250 }, membertrails: { op: 70, th: 150 }, threat: { op: 60, th: 300 }, bottarget: { op: 20, th: 90 } };
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(mixed);
mig = L.load(v2);
assert.deepStrictEqual(["bg", "zone", "bots", "leadpath", "hpbars", "memberpaths", "enemies", "enemynames", "threat", "events", "runinfo"].map((k) => mig[k]),
  [true, true, true, true, true, true, true, true, true, true, true], "the mixed fixture's on bits carry");
["grid", "partyextras", "critters", "aggro", "actionlines", "stalls", "objects"].forEach((k) => assert.strictEqual(mig[k], false, k));
assert.strictEqual(mig.dim, 55); assert.strictEqual(mig.trailLen, 60);
assert.deepStrictEqual(mig.style.leadpath, { op: 40, th: 250 }, "leadpath takes the old trail style (its first old line key)");
assert.deepStrictEqual(mig.style.memberpaths, { op: 70, th: 150 });
assert.deepStrictEqual(mig.style.threat, { op: 60, th: 300 }, "threat colours have no style; the threat-line style carries");
assert.deepStrictEqual(mig.style.actionlines, { op: 20, th: 90 }, "actionlines takes the bot-target style");
assert.deepStrictEqual(mig.style.aggro, { op: 100, th: 100 });

// a user's real choices survive: a default-on old switch turned OFF keeps its merged layer off, a default-off one turned ON turns it on
const chosen = oldDefaults();
chosen.trail = false; chosen.bots = false; chosen.botnames = false; chosen.lead = false; chosen.mobcritter = true;
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(chosen);
mig = L.load(v2);
assert.strictEqual(mig.leadpath, false, "trail off in v2 survives (trailtime/heading were default off)");
assert.strictEqual(mig.bots, false, "all three bots switches off in v2 survive (the merged layer stays off)");
assert.strictEqual(mig.critters, true, "a critter switch the user turned on survives");
assert.strictEqual(mig.enemies, true, "the untouched mob switches still get the new default");
// the next save writes v3 and v3 then wins
assert.strictEqual(L.save(v2, L.setDim(mig, 10)), true);
assert.ok(L.STORAGE_KEY in v2.d);
assert.strictEqual(L.load(v2).dim, 10);
v2.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldAll(true));
assert.strictEqual(L.load(v2).bots, false, "v3 wins once it exists");
// a v2 without a layer added later uses that old layer's default
const older = mem(); older.d[L.PREV_STORAGE_KEY] = JSON.stringify({ grid: false });
mig = L.load(older);
assert.strictEqual(mig.grid, false); assert.strictEqual(mig.zone, true); assert.strictEqual(mig.bossmark, undefined);
assert.strictEqual(mig.enemynames, true, "bossmark was on by default in v2, so the merged layer keeps its new default");
assert.strictEqual(mig.hpbars, true); assert.strictEqual(mig.threat, true, "the untouched threat switches get the new default");

// ---- v1 still migrates, v2 wins over v1, and a broken one gives the defaults
const v1 = mem();
v1.d[L.OLD_STORAGE_KEY] = JSON.stringify({ grid: false, trail: false, threat: true });
mig = L.load(v1);
assert.strictEqual(mig.grid, false); assert.strictEqual(mig.leadpath, false); assert.strictEqual(mig.threat, true);
assert.strictEqual(mig.bots, true); assert.strictEqual(mig.dim, 30, "v1 had no dim: the default");
v1.d[L.PREV_STORAGE_KEY] = JSON.stringify({ grid: true });
assert.strictEqual(L.load(v1).grid, true, "v2 wins over v1");
const bad1 = mem(); bad1.d[L.OLD_STORAGE_KEY] = "{broken";
assert.deepStrictEqual(L.load(bad1), def);
const bad3 = mem(); bad3.d[L.STORAGE_KEY] = "{broken"; bad3.d[L.PREV_STORAGE_KEY] = JSON.stringify(oldAll(true));
assert.deepStrictEqual(L.load(bad3), def, "a broken v3 does not fall back to a stale v2");

// ---- the dim and path-length sliders
assert.strictEqual(L.DIM_MAX, 80);
assert.strictEqual(L.setDim(def, 0).dim, 0);
assert.strictEqual(L.setDim(def, 55).dim, 55);
assert.strictEqual(L.setDim(def, 100).dim, 80, "capped at 80");
assert.strictEqual(L.setDim(def, -5).dim, 0, "not below 0");
assert.strictEqual(L.setDim(def, "40").dim, 40, "a slider value arrives as a string");
assert.strictEqual(L.setDim(def, "abc").dim, 30, "junk falls back");
assert.strictEqual(L.setDim(def, NaN).dim, 30);
assert.strictEqual(L.setDim(def, 12.6).dim, 13);
assert.strictEqual(L.set(def, "dim", true).dim, 30, "dim is not a toggle");
assert.strictEqual(L.setTrailLen(def, 60).trailLen, 60);
assert.strictEqual(L.setTrailLen(def, 5).trailLen, 10);
assert.strictEqual(L.setTrailLen(def, 5000).trailLen, 240);
assert.strictEqual(L.setTrailLen(def, "abc").trailLen, 120);
const dst = mem();
L.save(dst, L.setTrailLen(L.setDim(L.set(def, "grid", false), 65), 50));
assert.strictEqual(Object.keys(dst.d).length, 1);
assert.strictEqual(L.load(dst).dim, 65); assert.strictEqual(L.load(dst).grid, false); assert.strictEqual(L.load(dst).trailLen, 50);

// ---- line styles: 5 line layers, opacity 10-100 %, thickness 50-300 %
const lines = L.DEFS.filter((d) => d.line).map((d) => d.key);
assert.deepStrictEqual(lines, ["leadpath", "memberpaths", "threat", "aggro", "actionlines", "runsteps"]);
assert.deepStrictEqual(Object.keys(def.style).sort(), lines.slice().sort());
lines.forEach((k) => assert.deepStrictEqual(L.styleOf(def, k), { op: 1, th: 1 }, k));
assert.deepStrictEqual(L.styleOf(def, "grid"), { op: 1, th: 1 }, "a layer without a style reads as normal");
let sty = L.setStyle(def, "leadpath", "op", 40);
sty = L.setStyle(sty, "leadpath", "th", "250");
assert.deepStrictEqual(L.styleOf(sty, "leadpath"), { op: 0.4, th: 2.5 });
assert.strictEqual(L.styleOf(sty, "aggro").op, 1, "one layer's style does not touch another's");
assert.strictEqual(L.styleOf(L.setStyle(def, "leadpath", "op", 0), "leadpath").op, 0.1, "opacity is kept visible (10 % at least)");
assert.strictEqual(L.styleOf(L.setStyle(def, "leadpath", "th", 9999), "leadpath").th, 3);
assert.strictEqual(L.styleOf(L.setStyle(def, "leadpath", "th", 1), "leadpath").th, 0.5);
assert.strictEqual(L.styleOf(L.setStyle(def, "leadpath", "op", "junk"), "leadpath").op, 1, "junk falls back");
assert.deepStrictEqual(L.setStyle(def, "grid", "op", 20).style, def.style, "grid is no line layer");
assert.deepStrictEqual(L.setStyle(def, "trail", "op", 20).style, def.style, "the old key is not a line layer any more");
assert.deepStrictEqual(L.setStyle(def, "leadpath", "bad", 20).style, def.style);
const sst = mem();
L.save(sst, sty);
assert.deepStrictEqual(L.styleOf(L.load(sst), "leadpath"), { op: 0.4, th: 2.5 });
assert.strictEqual(L.normalize({ style: { leadpath: { op: "x", th: 99999 }, grid: { op: 5 } } }).style.leadpath.op, 100);
assert.strictEqual(L.normalize({ style: { leadpath: { op: "x", th: 99999 } } }).style.leadpath.th, 300);
assert.ok(!("grid" in L.normalize({ style: { grid: { op: 5 } } }).style), "styles of other keys are dropped");
assert.strictEqual(L.normalize({ style: [1, 2] }).style.leadpath.op, 100);
assert.strictEqual(L.normalize({ trailLen: "x" }).trailLen, 120);

// ---- the union by guid: the feed first, it wins conflicts and fills what the run fills in; no id, no union entry
const feed = [
  { id: "1", name: "A", hp: 50, x: 1, y: 2, victim: "9001", reaction: "h" },
  { id: "2", name: "B" },
  { id: "1", name: "A again", hp: 1 },
];
const run = [
  { id: "1", name: "A-run", hp: 80, level: 40, rank: "e" },
  { id: "3", name: "C", hp: 10 },
  { id: "", name: "no guid" },
  { name: "also no guid" },
  null,
];
const u = L.mobUnion(feed, run);
assert.deepStrictEqual(u.map((m) => m.id), ["1", "2", "3"]);
assert.strictEqual(u[0].name, "A", "the feed wins a conflicting field");
assert.strictEqual(u[0].hp, 50, "the feed's health wins");
assert.strictEqual(u[0].victim, "9001");
assert.strictEqual(u[0].level, 40, "a field the feed lacks is filled from the run");
assert.strictEqual(u[0].rank, "e");
assert.strictEqual(u[1].name, "B");
assert.strictEqual(u[2].name, "C");
assert.strictEqual(u[2].hp, 10);
assert.deepStrictEqual(L.mobUnion([], run).map((m) => m.id), ["1", "3"], "guid-less run mobs are dropped");
assert.deepStrictEqual(L.mobUnion(feed, []).map((m) => m.id), ["1", "2"]);
assert.deepStrictEqual(L.mobUnion(null, null), []);
assert.deepStrictEqual(L.mobUnion([{ id: 5, hp: 20 }], [{ id: "5", name: "N", x: 3 }]), [{ id: "5", hp: 20, name: "N", x: 3 }],
  "a numeric and a string id are the same unit, and the empty name is filled");
assert.strictEqual(feed.length, 3, "the inputs are not changed");
// caps: the commander feed's 24 and a raid run's 48 are drawn together, every guid once (a cap is not a reason to drop one side)
const capFeed = Array.from({ length: 24 }, (_, i) => ({ id: String(i + 1), name: "feed " + i, reaction: "h" }));
const capRun = Array.from({ length: 48 }, (_, i) => ({ id: String(i + 1), name: "run " + i, reaction: i % 2 ? "h" : "n" }));
const capped = L.mobUnion(capFeed, capRun);
assert.strictEqual(capped.length, 48, "the run's list covers the feed's ids and adds the rest");
assert.strictEqual(new Set(capped.map((m) => m.id)).size, 48);
assert.strictEqual(capped[0].name, "feed 0", "the shared ids keep the feed's fields");
assert.strictEqual(capped[30].name, "run 30");
assert.strictEqual(L.enemiesText(capped.length, 57), "48 of 57 enemies shown");
const filled = L.mobUnion([{ id: "7", name: "", hp: 5 }], [{ id: "7", name: "Seven", hp: 99 }])[0];
assert.strictEqual(filled.name, "Seven"); assert.strictEqual(filled.hp, 5);

// ---- the card's run: same map and one shared bot; guids decide whenever both sides have them, names only when one side does not
const bind = { commander: "C", bots: [{ name: "A", guid: 1, map: 70 }, { name: "B", guid: 2, map: 70 }] };
const r1 = { key: "70:1", map: 70, instance: 1, members: [{ name: "A", guid: 1 }, { name: "P", guid: 9 }] };
const r2 = { key: "70:2", map: 70, instance: 2, members: [{ name: "B", guid: 2 }] };
assert.strictEqual(L.findRun(bind, [r1], 70), r1);
assert.strictEqual(L.findRun(bind, [r1], 71), null, "another map is no match");
assert.strictEqual(L.findRun({ bots: [{ name: "B", guid: 2, map: 70 }] }, [r1], 70), null, "no shared bot");
assert.strictEqual(L.findRun({ bots: [{ name: "A", guid: 1, map: 70 }] }, [r1, r2], 70), r1, "one shared bot picks its copy");
assert.strictEqual(L.findRun({ bots: [{ name: "B", guid: 2, map: 70 }] }, [r1, r2], 70), r2, "the other copy stays isolated");
assert.strictEqual(L.findRun(bind, [r1, r2], 70), null, "a party cannot be in two copies: no clean match");
// guids win over names: same name, different guid is another unit
assert.strictEqual(L.findRun({ bots: [{ name: "A", guid: 1, map: 70 }] }, [{ key: "70:1", map: 70, members: [{ name: "A", guid: 99 }] }], 70), null,
  "equal names with different guids do not match");
assert.strictEqual(L.findRun({ bots: [{ name: "A", guid: 1, map: 70 }] }, [{ key: "70:1", map: 70, members: [{ name: "Other", guid: 1 }] }], 70).key, "70:1",
  "equal guids match however the names read");
assert.strictEqual(L.findRun({ bots: [{ name: "A", map: 70 }] }, [r1], 70), r1, "the name is used when the binding has no guid");
assert.strictEqual(L.findRun({ bots: [{ name: "A", guid: 1, map: 70 }] }, [{ key: "70:1", map: 70, members: [{ name: "A" }] }], 70).key, "70:1",
  "the name is used when the run has no guid");
assert.strictEqual(L.findRun({ ...bind, demo: true }, [r1], 70), null, "the demo never matches a real run (its guids can collide)");
assert.strictEqual(L.findRun({ ...bind, dungeon: true }, [r1], 70), null, "a Dungeons-tab binding already carries its run");
assert.strictEqual(L.findRun(bind, null, 70), null);
assert.strictEqual(L.findRun({ bots: [] }, [r1], 70), null);
assert.strictEqual(L.findRun(bind, [{ map: 70, members: [] }], 70), null);

// ---- "N of M enemies shown" only when the cap hid some
assert.strictEqual(L.enemiesText(24, 57), "24 of 57 enemies shown");
assert.strictEqual(L.enemiesText(57, 57), "");
assert.strictEqual(L.enemiesText(0, 0), "");
assert.strictEqual(L.enemiesText(10, 0), "");
assert.strictEqual(L.enemiesText(0, 3), "0 of 3 enemies shown");
assert.strictEqual(L.enemiesText("x", "y"), "");
console.log("maplayers.js ok");
