"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's wording, warnings and map math.
const assert = require("assert");
const C = require(require("path").resolve(process.argv[2]));

const bot = (o) => Object.assign({ name: "B", guid: 1, class: "warrior", role: "dps", level: 60, hp: 100, max_hp: 100, power: 50, max_power: 100, power_type: "mana", alive: true, lead: false, map: 70, zone: 1337, x: 0, y: 0, z: 0 }, o);
const party = () => ({
  commander: "Gbmage", online: true, view: true, group: 7, lead: "Tankie",
  bots: [
    bot({ name: "Tankie", guid: 1, role: "tank", class: "warrior", lead: true, x: 100, y: 100, power_type: "rage" }),
    bot({ name: "Heals", guid: 2, role: "healer", class: "priest", x: 105, y: 100 }),
    bot({ name: "Dmg1", guid: 3, role: "dps", class: "mage", x: 110, y: 105 }),
  ],
  orders: [],
});

// ---- roles, rows, distance
assert.strictEqual(C.roleKey(bot({ role: "tank" })), "tank");
assert.strictEqual(C.roleKey(bot({ role: "tank", alive: false })), "dead");
assert.strictEqual(C.roleKey(bot({ role: "" })), "other");
assert.strictEqual(C.roleLabel(bot({ role: "dps" })), "DPS");
assert.strictEqual(C.distance(bot({ x: 0, y: 0 }), bot({ x: 3, y: 4 })), 5);
assert.strictEqual(C.distance(bot({ map: 1 }), bot({ map: 70 })), null);
let rows = C.partyRows(party());
assert.deepStrictEqual(rows.map((r) => r.name), ["Tankie", "Heals", "Dmg1"]);
assert.strictEqual(rows[0].lead, true);
assert.strictEqual(rows[1].dist, 5);
assert.strictEqual(rows[1].far, false);
assert.strictEqual(rows[2].cls, "Mage");
let p = party(); p.bots.push(bot({ name: "Far", guid: 4, x: 200, y: 100, hp: 25, max_hp: 100 }), bot({ name: "Away", guid: 5, map: 1 }));
rows = C.partyRows(p);
assert.strictEqual(rows.find((r) => r.name === "Far").far, true);
assert.strictEqual(rows.find((r) => r.name === "Far").hp, 25);
assert.strictEqual(rows.find((r) => r.name === "Away").otherMap, true);
assert.strictEqual(rows.find((r) => r.name === "Away").dist, null);
rows = C.partyRows({ commander: "x", lead: "", bots: [bot({ max_hp: 0, hp: 0, max_power: 0 })] });
assert.strictEqual(rows[0].hp, 0); assert.strictEqual(rows[0].hasPower, false);

// ---- orders in plain words
const t = (o) => C.orderText(Object.assign({ at: 1, verb: "move", detail: "", ok: true, result: "party 5" }, o));
assert.strictEqual(t({ verb: "attack", detail: "0xF130001B5300AC91" }).text, "Attack a target → OK (5 bots)");
assert.strictEqual(t({ verb: "move", detail: "", ok: false, result: "no-path: No walkable path." }).text, "Move → refused: no walkable path");
assert.strictEqual(t({ verb: "move", detail: "-6.7 401.9 -52.1" }).text, "Move to -7, 402 → OK (5 bots)");
assert.strictEqual(t({ verb: "follow", result: "party 1" }).text, "Follow → OK (1 bot)");
assert.strictEqual(t({ verb: "loot", detail: "Heals 0x1", result: "bot:Heals 1" }).text, "Loot a target → OK (Heals)");
assert.strictEqual(t({ verb: "state", detail: "entry:7011@1,2,3", result: "guid:1,entry:7011" }).text, "Check creature 7011 → OK (guid:1,entry:7011)");
assert.strictEqual(t({ verb: "stay", ok: false, result: "denied" }).text, "Stay → refused: denied");
assert.strictEqual(t({ verb: "cc", detail: "a very long argument that goes on and on and on", ok: true, result: "" }).text.indexOf("…") > 0, true);
assert.strictEqual(t({ verb: "wibble" }).label, "Wibble");
assert.ok(/^ERR cmd move -> no-path/.test(t({ verb: "move", ok: false, result: "no-path: x" }).raw));

// ---- state of the party
const ord = (verb, at, ok) => ({ at, verb, detail: "", ok: ok !== false, result: "party 3" });
let b = party();
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "idle");
assert.strictEqual(C.stateWord({ bots: [], orders: [] }, { nowSec: 1 }), "no bots reporting");
b.bots.forEach((x) => (x.alive = false));
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "wiped");
b = party();
assert.strictEqual(C.stateWord(b, { nowSec: 1000, hpDropAt: 995 }), "fighting");
assert.strictEqual(C.stateWord(b, { nowSec: 1000, hpDropAt: 900 }), "idle");
// the run leader's run: paused shows as the state and as a warning with the reason
const paused = { bots: [{ name: "A", alive: true }], orders: [], run: { state: "paused", note: "a unit survived 3 pulls" } };
assert.strictEqual(C.stateWord(paused, { nowSec: 1000 }), "run paused");
assert.deepStrictEqual(C.warnings(paused, {}).filter((w) => w.kind === "runpaused").map((w) => w.text), ["Run paused: a unit survived 3 pulls"]);
assert.deepStrictEqual(C.warnings({ bots: [], orders: [], run: { state: "paused", note: "-" } }, {}).filter((w) => w.kind === "runpaused").map((w) => w.text), ["Run paused: no reason given"]);
assert.strictEqual(C.warnings({ bots: [], orders: [], run: { state: "running" } }, {}).filter((w) => w.kind === "runpaused").length, 0);
// a module that reports the AI state (st: c = combat) says "fighting" itself, with no HP drop needed
assert.strictEqual(C.stateWord({ bots: [{ name: "A", alive: true, st: "c" }], orders: [] }, { nowSec: 1000 }), "fighting");
assert.strictEqual(C.stateWord({ bots: [{ name: "A", alive: false, st: "c" }, { name: "B", alive: true, st: "n" }], orders: [] }, { nowSec: 1000 }), "idle");
b.orders = [ord("attack", 990)];
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "attacking");
assert.strictEqual(C.stateWord(b, { nowSec: 1100 }), "idle");
b.orders = [ord("pull", 990)];
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "pulling");
b.orders = [ord("follow", 900)];
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "following");
assert.strictEqual(C.stateWord(b, { nowSec: 1000, leadMovedAt: 997 }), "moving");
b.orders = [ord("rest", 900), ord("state", 950), ord("move", 960, false)];
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "resting"); // a refused order and a question change nothing
assert.strictEqual(C.headerText(party(), "Uldaman", "fighting"), "Gbmage → 3 bots · Uldaman · fighting");
assert.strictEqual(C.headerText({ commander: "G", bots: [bot({})] }, "", ""), "G → 1 bot");

// ---- warnings
assert.deepStrictEqual(C.warnings(party(), { ageSec: 3 }), []);
p = party(); p.bots[1].alive = false; p.bots[1].hp = 0;
let w = C.warnings(p, {});
assert.strictEqual(w.length, 1); assert.strictEqual(w[0].kind, "dead"); assert.strictEqual(w[0].level, "bad"); assert.strictEqual(w[0].text, "1 down: Heals");
p = party(); p.bots[2].x = 160; // 60 yd from the lead
w = C.warnings(p, {});
assert.strictEqual(w[0].kind, "far"); assert.ok(/Dmg1 \d+ yd/.test(w[0].text));
p = party(); p.bots[2].map = 1;
assert.strictEqual(C.warnings(p, {})[0].kind, "othermap");
p = party(); p.bots[2].hp = 20;
assert.strictEqual(C.warnings(p, {})[0].kind, "lowhp");
p = party(); p.orders = [ord("move", 1, true), { at: 2, verb: "move", detail: "", ok: false, result: "no-path: No walkable path." }];
w = C.warnings(p, {});
assert.strictEqual(w[0].kind, "refused"); assert.strictEqual(w[0].text, "Last order refused: Move (no walkable path)");
p.orders.push(ord("follow", 3));
assert.deepStrictEqual(C.warnings(p, {}), []); // a newer accepted order clears it
p = party(); p.online = false;
assert.strictEqual(C.warnings(p, {})[0].kind, "offline");
p = party(); p.lead = "Gone"; p.bots.forEach((x) => (x.lead = false));
assert.strictEqual(C.warnings(p, {})[0].kind, "nolead");
assert.deepStrictEqual(C.warnings(party(), { ageSec: 30 }), []);
w = C.warnings(party(), { ageSec: 45 });
assert.strictEqual(w[0].kind, "stale"); assert.ok(/45 s/.test(w[0].text));
assert.strictEqual(C.ageText(5), "5 s"); assert.strictEqual(C.ageText(125), "2 min 05 s"); assert.strictEqual(C.ageText(3700), "1 h 01 min");
p = party(); for (let i = 0; i < 4; i++) p.bots.push(bot({ name: "F" + i, guid: 10 + i, x: 300 + i * 10 }));
assert.ok(/ and 2 more$/.test(C.warnings(p, {})[0].text));

// ---- tracker: fights and the lead's trail from changes between reports
let tr = C.newTracker();
b = party();
C.note(tr, b, 100);
assert.strictEqual(C.context(tr, b, 100).hpDropAt, 0);
b = party(); b.bots[1].hp = 80;
C.note(tr, b, 102);
assert.strictEqual(C.context(tr, b, 102).hpDropAt, 102);
b = party(); b.bots[1].hp = 90; // healed: no new drop
C.note(tr, b, 104);
assert.strictEqual(C.context(tr, b, 104).hpDropAt, 102);
assert.strictEqual(C.context(tr, b, 104).leadMovedAt, 0);
b = party(); b.bots[0].x = 110; // the lead walked 10 yd
C.note(tr, b, 106);
assert.strictEqual(C.context(tr, b, 106).leadMovedAt, 106);
assert.strictEqual(tr.trail.Gbmage.pts.length, 2);
b = party(); b.bots[0].x = 110.5; C.note(tr, b, 108); // under 2 yd: no new point
assert.strictEqual(tr.trail.Gbmage.pts.length, 2);
b = party(); b.bots[0].map = 1; C.note(tr, b, 110); // a new map restarts the trail
assert.strictEqual(tr.trail.Gbmage.pts.length, 1); assert.strictEqual(tr.trail.Gbmage.map, 1);
for (let i = 0; i < 300; i++) { b = party(); b.bots[0].x = i * 5; C.note(tr, b, 200 + i); }
assert.strictEqual(tr.trail.Gbmage.pts.length, 240, "the lead's trail is a ring of 240 positions (the path-length slider's maximum)");
// the daemon's stored history goes in front: only older points, only on the trail's map
{
  const t2 = C.newTracker(); let q = party(); q.bots[0].x = 500; C.note(t2, q, 1000);
  assert.strictEqual(C.prependTrail(t2, "Gbmage", q.bots[0].map, [[1, 2, 10], [3, 4, 20], [500, 0, 1000], [9, 9, 1500], ["x", 1, 2], null]), 2);
  assert.deepStrictEqual(t2.trail.Gbmage.pts.map((p) => p[2]), [10, 20, 1000]);
  assert.strictEqual(C.prependTrail(t2, "Gbmage", q.bots[0].map, [[1, 2, 10]]), 0, "twice adds nothing");
  assert.strictEqual(C.prependTrail(t2, "Gbmage", 999, [[1, 2, 1]]), 0, "another map");
  assert.strictEqual(C.prependTrail(t2, "Nobody", 1, [[1, 2, 1]]), 0);
  assert.strictEqual(C.prependTrail(t2, "Gbmage", q.bots[0].map, "junk"), 0);
}
// a dead bot that is raised is not a "drop"
tr = C.newTracker(); b = party(); b.bots[1].alive = false; b.bots[1].hp = 0; C.note(tr, b, 1);
b = party(); b.bots[1].hp = 50; C.note(tr, b, 3);
assert.strictEqual(C.context(tr, b, 3).hpDropAt, 0);
C.prune(tr, []);
assert.deepStrictEqual(Object.keys(tr.trail), []); assert.deepStrictEqual(Object.keys(tr.hp), []);

// ---- map math
assert.deepStrictEqual(C.project(10, 20), { x: -20, y: -10 }); // north (x) is up, west (y) is left
let v = C.fitView([{ x: 0, y: 0 }, { x: 100, y: 50 }], 400, 300, 20, 60);
assert.strictEqual(v.cx, 50); assert.strictEqual(v.cy, 25);
assert.ok(Math.abs(v.scale - 360 / 100) < 1e-9);
v = C.fitView([{ x: 5, y: 5 }], 400, 300, 20, 60); // one point: at least 60 yd across
assert.ok(Math.abs(v.scale - Math.min(360 / 60, 260 / 60)) < 1e-9);
const bx = C.viewBox({ cx: 10, cy: 20, scale: 2 }, 400, 200);
assert.deepStrictEqual(bx, [-90, -30, 200, 100]);
// zoom keeps the point under the cursor
v = { cx: 0, cy: 0, scale: 2 };
const z = C.zoomAt(v, 2, 400, 300, 300, 100);
const at = (vv) => ({ x: vv.cx + (300 - 200) / vv.scale, y: vv.cy + (100 - 150) / vv.scale });
assert.ok(Math.abs(at(v).x - at(z).x) < 1e-9 && Math.abs(at(v).y - at(z).y) < 1e-9);
assert.strictEqual(C.zoomAt({ cx: 0, cy: 0, scale: 13 }, 4, 400, 300, 200, 150).scale, 14);
assert.deepStrictEqual(C.panBy({ cx: 0, cy: 0, scale: 2 }, 20, -10), { cx: -10, cy: 5, scale: 2 });
const pz = C.pinch({ cx: 0, cy: 0, scale: 1 }, { x: 150, y: 150 }, { x: 250, y: 150 }, { x: 100, y: 150 }, { x: 300, y: 150 }, 400, 300);
assert.ok(Math.abs(pz.scale - 2) < 1e-9);
assert.strictEqual(C.gridStep(1), 100); assert.strictEqual(C.gridStep(4), 20); assert.strictEqual(C.gridStep(0.04), 2000);
const gl = C.gridLines([-95, -30, 200, 100], 50);
assert.deepStrictEqual(gl.xs, [-50, 0, 50, 100]); assert.deepStrictEqual(gl.ys.map((n) => n + 0), [0, 50]); // -30..70 -> 0, 50
const segs = C.trailSegments(Array.from({ length: 50 }, (_, i) => ({ x: i, y: 0 })), 10, 0.2);
assert.ok(segs.length <= 10 && segs.length >= 2);
assert.ok(segs[0].alpha < segs[segs.length - 1].alpha && segs[segs.length - 1].alpha === 1);
assert.deepStrictEqual(C.trailSegments([{ x: 0, y: 0 }], 5), []);

// ---- links to the detail view of a character (the route app.js parses)
assert.strictEqual(C.botHref(1234), "#/commanders?bot=1234");
assert.strictEqual(C.botHref("77"), "#/commanders?bot=77");
["", 0, -5, 1.5, "abc", null, undefined, NaN, 4294967296, "1;x", "0x10"].forEach((g) => assert.strictEqual(C.botHref(g), "", String(g)));
assert.strictEqual(C.partyRows(party())[1].guid, 2);
assert.deepStrictEqual(C.headerParts(party(), "Uldaman", "fighting"), ["Gbmage", " → 3 bots · Uldaman · fighting"]);
assert.deepStrictEqual(C.headerParts({ commander: "", bots: [] }, "", ""), ["", " → 0 bots"]);

// ---- map layers helpers
// heading: positions [x, y, t] in game coordinates; the unit vector comes back in the plane (x -> -y, y -> -x)
let hd = C.heading([[0, 0, 1], [10, 0, 2]], 3, 20, 4); // walking north (game x grows): up the screen, plane y shrinks
assert.ok(Math.abs(hd.x) < 1e-9 && Math.abs(hd.y + 1) < 1e-9);
hd = C.heading([[0, 0, 1], [0, 10, 2]], 3, 20, 4); // walking west (game y grows): plane x shrinks, to the left of the screen
assert.ok(Math.abs(hd.x + 1) < 1e-9 && Math.abs(hd.y) < 1e-9);
assert.strictEqual(C.heading([[0, 0, 1], [1, 0, 2]], 3, 20, 4), null);   // moved less than 4 yd
assert.strictEqual(C.heading([[0, 0, 1], [10, 0, 2]], 100, 20, 4), null); // newest point is stale
assert.strictEqual(C.heading([[0, 0, 1]], 3), null);
assert.strictEqual(C.heading(null, 3), null);
// a step of 1 yd at a time still finds the point far enough back
hd = C.heading(Array.from({ length: 12 }, (_, i) => [i, 0, i + 1]), 12, 20, 4);
assert.ok(hd && Math.abs(hd.y + 1) < 1e-9);
// time labels keep their distance and the newest one
const pts = Array.from({ length: 101 }, (_, i) => [i, 0, 1000 + i]);
let st = C.trailStamps(pts, 2, 70, 8); // 70 px at 2 px/yd is 35 yd
assert.strictEqual(st[st.length - 1].t, 1100);
for (let i = 1; i < st.length; i++) assert.ok(Math.hypot(st[i].x - st[i - 1].x, st[i].y - st[i - 1].y) * 2 >= 70 - 1e-9);
assert.ok(st.length <= 8 && st.length >= 3);
assert.strictEqual(C.trailStamps(pts, 2, 70, 2).length, 2);
assert.deepStrictEqual(C.trailStamps([[0, 0, 0], [5, 5, 0]], 2, 70, 8), []); // no times, no labels
assert.deepStrictEqual(C.trailStamps(null, 1), []);
// scale bar: the longest round number of yards within maxPx
assert.deepStrictEqual(C.scaleBar(1, 110), { yd: 100, px: 100 });
assert.deepStrictEqual(C.scaleBar(4, 110), { yd: 20, px: 80 });
assert.deepStrictEqual(C.scaleBar(0.04, 110), { yd: 2000, px: 80 });
assert.strictEqual(C.scaleBar(500, 110).yd, 1); // even a yard is wide: still a yard
// zones around: those overlapping the box, not the skipped one
const zb = {
  "1_14": { map_id: 1, area_id: 14, name: "Durotar", loc_left: 100, loc_right: -100, loc_top: 100, loc_bottom: -100 },
  "1_17": { map_id: 1, area_id: 17, name: "Barrens", loc_left: 1000, loc_right: 500, loc_top: 1000, loc_bottom: 500 },
  "0_9": { map_id: 0, area_id: 9, name: "Elsewhere", loc_left: 100, loc_right: -100, loc_top: 100, loc_bottom: -100 },
};
let zs = C.zonesIn(zb, 1, [-150, -150, 300, 300], 0);
assert.deepStrictEqual(zs.map((z) => z.name), ["Durotar"]);
assert.deepStrictEqual([zs[0].x, zs[0].y, zs[0].w, zs[0].h], [-100, -100, 200, 200]);
assert.deepStrictEqual(C.zonesIn(zb, 1, [-150, -150, 300, 300], 14), []);
assert.deepStrictEqual(C.zonesIn(null, 1, [0, 0, 1, 1], 0), []);
// a picture lies where the game puts it: layer rect from the daemon (Zul'Farrak 209), a game point inside it
const L = { x: -1625, y: -2052.080078125, w: 1625 - 241.6699981689453, h: 2052.080078125 - 1129.1700439453125 };
const gp = C.project(2052.080078125 - 100, 1625 - 200); // 100 yd south and 200 yd east of the north-west corner
assert.ok(Math.abs(gp.x - (L.x + 200)) < 1e-9 && Math.abs(gp.y - (L.y + 100)) < 1e-9);
assert.ok(gp.x > L.x && gp.x < L.x + L.w && gp.y > L.y && gp.y < L.y + L.h);
// picture slots from the daemon: an available one needs a daemon file url and a finite rectangle, a missing one keeps its label and reason
const okLayer = { kind: "worldmap", floor: "", label: "World map", name: "worldmap", source: "worldmap", available: true,
  url: "/api/v1/mapfiles/209.png", x: -1625, y: -2052, w: 1383, h: 923 };
const missingMinimap = { kind: "minimap", floor: "", label: "Minimap", name: "minimap", available: false, reason: "No minimap picture: indoor dungeon" };
const navLayer = Object.assign({}, okLayer, { kind: "navmesh", label: "Navmesh", name: "navmesh", url: "/api/v1/mapfiles/209.navmesh.png" });
let slots = C.pictureSlots([okLayer, missingMinimap, navLayer]);
assert.deepStrictEqual(slots.map((s) => s.key), ["worldmap", "minimap", "navmesh"]);
assert.deepStrictEqual(slots.map((s) => s.available), [true, false, true]);
assert.strictEqual(slots[1].reason, "No minimap picture: indoor dungeon");
assert.strictEqual(slots[1].url, undefined);
// the order is the ZF one whatever order the answer came in, and a floor sits right after its main picture
const floor3f = Object.assign({}, okLayer, { floor: "3f", label: "World map 3f", name: "worldmap 3f", url: "/api/v1/mapfiles/189.worldmap-3f.png" });
const floor2f = Object.assign({}, okLayer, { floor: "2f", label: "World map 2f", name: "worldmap 2f", url: "/api/v1/mapfiles/189.worldmap-2f.png" });
const floor10f = Object.assign({}, okLayer, { floor: "10f", label: "World map 10f", name: "worldmap 10f", url: "/api/v1/mapfiles/189.worldmap-10f.png" });
// floors sort by number: 10f comes after 3f, not between 1f and 2f
assert.deepStrictEqual(C.pictureSlots([navLayer, floor10f, floor3f, missingMinimap, okLayer, floor2f]).map((s) => s.key),
  ["worldmap", "worldmap 2f", "worldmap 3f", "worldmap 10f", "minimap", "navmesh"]);
// duplicates keep the first; a kind that is not one of the three is dropped
assert.strictEqual(C.pictureSlots([okLayer, Object.assign({}, okLayer, { label: "second" })]).length, 1);
assert.deepStrictEqual(C.pictureSlots([Object.assign({}, okLayer, { kind: "zone" })]), []);
// a bad url or rectangle on an available entry drops it
[ "http://evil.example/x.png", "/api/v1/mapfiles/../../etc/passwd.png", "/api/v1/mapfiles/209.json", "javascript:alert(1)", "/api/v1/mapfiles/209.png\"onload=\"x", "", 5 ]
  .forEach((u) => assert.deepStrictEqual(C.pictureSlots([Object.assign({}, okLayer, { url: u })]), [], String(u)));
assert.deepStrictEqual(C.pictureSlots([Object.assign({}, okLayer, { w: NaN })]), []);
assert.deepStrictEqual(C.pictureSlots([Object.assign({}, okLayer, { w: 0 })]), []);
assert.deepStrictEqual(C.pictureSlots([Object.assign({}, okLayer, { x: "1" })]), []);
assert.deepStrictEqual(C.pictureSlots(null), []); assert.deepStrictEqual(C.pictureSlots([null, 3]), []);
assert.strictEqual(C.pictureSlots([Object.assign({}, okLayer, { label: "x".repeat(100) })])[0].label.length, 40);
// a stored choice of a missing kind falls back to the first available slot and says why; a stored available one is kept
assert.strictEqual(C.pictureChoice(slots, "minimap").slot.key, "worldmap");
assert.strictEqual(C.pictureChoice(slots, "minimap").missing.reason, "No minimap picture: indoor dungeon");
assert.strictEqual(C.pictureChoice(slots, "navmesh").slot.key, "navmesh");
assert.strictEqual(C.pictureChoice(slots, "gone").slot.key, "worldmap");
assert.strictEqual(C.pictureChoice(slots, "gone").missing, null);
assert.strictEqual(C.pictureChoice([missingMinimap], "minimap").slot, null);
// the picker: hidden with nothing available; options for all slots, a missing one disabled with "(none)" and its reason as title
assert.deepStrictEqual(C.pictureSelect(C.pictureSlots([missingMinimap]), "minimap"), { hidden: true, value: "", reason: "",
  options: [{ value: "minimap", label: "Minimap (none)", disabled: true, title: "No minimap picture: indoor dungeon" }] });
let oneAvail = C.pictureSelect(C.pictureSlots([missingMinimap, okLayer]), "");
assert.strictEqual(oneAvail.hidden, false); assert.strictEqual(oneAvail.value, "worldmap");
assert.deepStrictEqual(oneAvail.options.map((o) => [o.value, o.disabled]), [["worldmap", false], ["minimap", true]]);
let threeAvail = C.pictureSelect(slots, "");
assert.strictEqual(threeAvail.hidden, false); assert.strictEqual(threeAvail.value, "worldmap");
assert.deepStrictEqual(threeAvail.options.map((o) => o.label), ["World map", "Minimap (none)", "Navmesh"]);
assert.strictEqual(threeAvail.options[1].title, "No minimap picture: indoor dungeon");
assert.strictEqual(threeAvail.reason, "", "no stored choice, so no reason line");
assert.strictEqual(C.pictureSelect(slots, "minimap").reason, "No minimap picture: indoor dungeon");
assert.strictEqual(C.pictureSelect(slots, "minimap").value, "worldmap");
// how long a picture list may be kept: no picture at all 30 s (the extractor may have run since), some missing 5 min, complete never
assert.strictEqual(C.pictureRefreshAfter([]), C.PIC_RETRY_MS);
assert.strictEqual(C.pictureRefreshAfter(null), C.PIC_RETRY_MS);
assert.strictEqual(C.pictureRefreshAfter([missingMinimap]), C.PIC_RETRY_MS, "no available picture: look again soon");
assert.strictEqual(C.pictureRefreshAfter(slots), C.PIC_MISSING_MS, "one missing picture: look again now and then");
assert.strictEqual(C.pictureRefreshAfter([okLayer]), null, "a complete list is never asked for again");

// ---- zooming while following: the party stays in the middle, only the scale changes
{
  const pts = [{ x: 0, y: 0 }, { x: 100, y: 60 }];
  const fit = C.followView(pts, 400, 300, 20, 60, null);
  assert.deepStrictEqual(fit, C.fitView(pts, 400, 300, 20, 60), "no zoom of the viewer's own: the fitting view");
  const in1 = C.zoomFollow(fit, 1.5);
  assert.strictEqual(in1.cx, fit.cx); assert.strictEqual(in1.cy, fit.cy); // the party's centre did not move
  assert.ok(Math.abs(in1.scale - fit.scale * 1.5) < 1e-9);
  // the next refresh (the party walked): centred on the new middle, at the viewer's zoom
  const walked = [{ x: 500, y: 500 }, { x: 600, y: 560 }];
  const kept = C.followView(walked, 400, 300, 20, 60, in1.scale);
  assert.strictEqual(kept.scale, in1.scale);
  assert.strictEqual(kept.cx, 550); assert.strictEqual(kept.cy, 530);
  // zoom out and in, clamped
  assert.strictEqual(C.zoomFollow({ cx: 1, cy: 2, scale: 13 }, 4).scale, 14);
  assert.strictEqual(C.zoomFollow({ cx: 1, cy: 2, scale: 0.05 }, 0.1).scale, 0.04);
  assert.strictEqual(C.followView(pts, 400, 300, 20, 60, 99).scale, 14, "a stored zoom is clamped too");
  assert.deepStrictEqual(C.followView([], 400, 300, 20, 60, 2), { cx: 0, cy: 0, scale: 1 }, "no points: nothing to follow");
  assert.deepStrictEqual(C.followView(pts, 400, 300, 20, 60, 0), fit, "0 means not chosen");
  // pinch while following: only the distance counts, the middle stays
  const a0 = { x: 100, y: 100 }, b0 = { x: 200, y: 100 };
  const pz = C.pinchFollow(fit, a0, b0, { x: 50, y: 300 }, { x: 250, y: 300 }); // spread 100 -> 200, and dragged down
  assert.strictEqual(pz.cx, fit.cx); assert.strictEqual(pz.cy, fit.cy);
  assert.ok(Math.abs(pz.scale - fit.scale * 2) < 1e-9);
  assert.strictEqual(C.pinchFollow(fit, a0, a0, a0, b0), fit, "fingers on one point: no change");
  // while not following, zoom around the cursor still keeps the point under it (unchanged)
  const free = C.zoomAt(fit, 2, 400, 300, 300, 100);
  assert.notStrictEqual(free.cx, fit.cx);
}

// ---- mobs, lines, rings, the commander
{
  const mobs = [
    { id: "11", entry: 1, name: "Wolf", hp: 100, x: 10, y: 20, reaction: "h", victim: "700" },
    { id: "12", entry: 2, name: "Rat", hp: 100, x: 11, y: 21, reaction: "n", kind: "k" },
    { id: "13", entry: 3, name: "Boar", hp: 50, x: 12, y: 22, reaction: "n" },
    { id: "14", entry: 4, name: "Guard", hp: 100, x: 13, y: 23, reaction: "f", kind: "g" },
    { id: "15", entry: 5, name: "Spider", hp: 90, x: 14, y: 24, reaction: "h" },
    { id: "16", entry: 6, name: "Cultist", hp: 0, x: 15, y: 25, reaction: "h" },
    { id: "17", entry: 7, name: "Civilian", hp: 100, x: 1, y: 1, reaction: "h", kind: "v" },
  ];
  const bind = { mobs_map: 209, mobs: mobs.concat([null, { id: 5, x: 1, y: 1, reaction: "h" }, { id: "18", x: NaN, y: 1, reaction: "h" }, { id: "19", x: 1, y: 1, reaction: "z" }]) };
  assert.strictEqual(C.mobsOf(bind, 209).length, 7, "malformed mobs are dropped");
  assert.deepStrictEqual(C.mobsOf(bind, 1), [], "another map: none");
  assert.deepStrictEqual(C.mobsOf({}, 209), []); assert.deepStrictEqual(C.mobsOf(null, 209), []);
  assert.deepStrictEqual(C.mobsOf({ mobs_map: 209, mobs: "x" }, 209), []);
  assert.deepStrictEqual(mobs.map(C.mobClass), ["hostile", "critter", "neutral", "", "hostile", "hostile", ""]);
  const bots = [{ id: "700", name: "Tank", x: 0, y: 0, alive: true, target: "11" }, { id: "701", name: "Heal", x: 1, y: 1, alive: true },
    { id: "702", name: "Dead", x: 2, y: 2, alive: false, target: "15" }];
  const tl = C.threatLines(mobs, bots);
  assert.strictEqual(tl.length, 1); assert.strictEqual(tl[0].mob.id, "11"); assert.strictEqual(tl[0].bot.name, "Tank");
  assert.deepStrictEqual(C.threatLines(mobs, []), []);
  assert.deepStrictEqual(C.threatLines([{ id: "1", x: 0, y: 0, reaction: "h", victim: "999" }], bots), [], "a victim that is not a listed bot");
  const bl = C.botTargetLines(bots, mobs);
  assert.strictEqual(bl.length, 1, "a dead bot draws no line"); assert.strictEqual(bl[0].mob.name, "Wolf");
  assert.deepStrictEqual(C.botTargetLines([{ id: "1", target: "nope", alive: true }], mobs), []);
  // aggro rings: hostile mobs nobody fights yet (the wolf has a victim, the dead cultist has no health)
  assert.deepStrictEqual(C.aggroMobs(mobs).map((m) => m.name), ["Spider"]);
  assert.strictEqual(C.AGGRO_RANGE_YD, 20);
  assert.deepStrictEqual(C.commanderPlace({ commander_pos: { map: 209, x: 5, y: 6 } }, 209), { x: 5, y: 6 });
  assert.strictEqual(C.commanderPlace({ commander_pos: { map: 1, x: 5, y: 6 } }, 209), null);
  assert.strictEqual(C.commanderPlace({ commander_pos: { map: 209, x: "5", y: 6 } }, 209), null);
  assert.strictEqual(C.commanderPlace({}, 209), null);
}

// ---- trails are never joined across a teleport, a death-release or a map change
assert.strictEqual(C.jumpLimitYd(2), 52, "a 2 s report gap: 12 yd slack + 20 yd/s");
assert.strictEqual(C.jumpLimitYd(0), 32, "the gap counts at least 1 s");
assert.strictEqual(C.jumpLimitYd(500), 612, "...and at most 30 s, so a long silence cannot hide a jump");
{
  const at = (x, t, o) => Object.assign({ map: 1, x, y: 0, t, alive: true }, o);
  assert.strictEqual(C.trailEvent(null, at(0, 1)), "");
  assert.strictEqual(C.trailEvent(at(0, 100), at(28, 102)), "", "14 yd/s on an epic mount is not a jump");
  assert.strictEqual(C.trailEvent(at(0, 100), at(52, 102)), "", "the limit itself is a walk");
  assert.strictEqual(C.trailEvent(at(0, 100), at(53, 102)), "t", "past it is a teleport");
  assert.strictEqual(C.trailEvent(at(0, 100), at(300, 102)), "t");
  assert.strictEqual(C.trailEvent(at(0, 100), at(300, 110)), "t", "300 yd in 10 s (limit 212)");
  assert.strictEqual(C.trailEvent(at(0, 100), at(300, 120)), "", "a longer gap allows more: 300 yd in 20 s (limit 412) is a ride");
  assert.strictEqual(C.trailEvent(at(0, 100), at(700, 400)), "t", "a long silence is capped at 30 s");
  assert.strictEqual(C.trailEvent(at(0, 100), at(5, 102, { map: 2 })), "m", "another map, however close");
  assert.strictEqual(C.trailEvent(at(0, 100), at(1, 102, { alive: false })), "d", "a death");
  assert.strictEqual(C.trailEvent(at(0, 100, { alive: false }), at(1, 102, { alive: false })), "", "lying dead is not a new death");
  assert.strictEqual(C.trailEvent(at(0, 100, { alive: false }), at(400, 102, { alive: false })), "r", "a spirit release: a jump while dead");
  assert.strictEqual(C.trailEvent(at(0, 100, { alive: false }), at(400, 102)), "r", "a resurrection at the spirit healer");
  assert.strictEqual(C.trailEvent(at(0, 100, { alive: false }), at(1, 102)), "", "raised at the corpse: nothing to break");
}
{
  const t3 = C.newTracker(); let q;
  const step = (x, t, o) => { q = party(); q.bots[0].x = x; q.bots[0].y = 0; Object.assign(q.bots[0], o || {}); C.note(t3, q, t); };
  step(0, 100); step(20, 102); step(40, 104);
  assert.deepStrictEqual(t3.trail.Gbmage.pts, [[0, 0, 100], [20, 0, 102], [40, 0, 104]], "walking: plain points");
  step(500, 106); // summoned
  assert.deepStrictEqual(t3.trail.Gbmage.pts[3], [500, 0, 106, "t"], "the point after a teleport carries it");
  step(500.5, 108); assert.strictEqual(t3.trail.Gbmage.pts.length, 4, "standing still adds nothing");
  step(505, 110, { alive: false }); // died where it stood
  assert.deepStrictEqual(t3.trail.Gbmage.pts[4], [505, 0, 110, "d"]);
  const runs = C.trailRuns(t3.trail.Gbmage.pts);
  assert.strictEqual(runs.length, 2, "split in two at the teleport, not joined");
  assert.strictEqual(runs[0].length, 3); assert.strictEqual(runs[1].length, 2, "the death does not split");
  assert.deepStrictEqual(C.trailMarks(t3.trail.Gbmage.pts).map((m) => [m.kind, m.x]), [["t", 500], ["d", 505]]);
  // a map change: the old map's points are gone, the new trail starts with a marked point
  step(10, 112, { map: 1 });
  assert.strictEqual(t3.trail.Gbmage.map, 1); assert.deepStrictEqual(t3.trail.Gbmage.pts, [[10, 0, 112, "m"]]);
}
// drawing pieces: no polyline reaches over a break; the fade runs over the whole trail
{
  const P = [[0, 0, 1], [10, 0, 2], [20, 0, 3], [900, 0, 4, "t"], [910, 0, 5], [920, 0, 6]];
  const sg = C.trailSegmentsBroken(P, 6, 0.2);
  assert.ok(sg.length >= 2);
  sg.forEach((s) => {
    const xs = s.points.map((p) => -p.y); // plane y = -game x
    assert.ok(!(Math.min(...xs) <= 20 && Math.max(...xs) >= 900), "a polyline joins both sides of the teleport: " + JSON.stringify(s.points));
  });
  assert.strictEqual(sg[sg.length - 1].alpha, 1); assert.ok(sg[0].alpha < 1);
  assert.deepStrictEqual(C.trailSegmentsBroken([[0, 0, 1]], 5), []);
  assert.deepStrictEqual(C.trailSegmentsBroken(null, 5), []);
  const lone = C.trailSegmentsBroken([[0, 0, 1], [1, 0, 2], [800, 0, 3, "t"]], 5);
  assert.strictEqual(lone.length, 1, "a lone point after a break has no line"); assert.strictEqual(lone[0].points.length, 2);
  // no break: the same chunks as before
  const plain = Array.from({ length: 50 }, (_, i) => [i, 0, i]);
  assert.deepStrictEqual(C.trailSegmentsBroken(plain, 10, 0.2), C.trailSegments(plain.map((p) => ({ x: -p[1], y: -p[0] })), 10, 0.2));
  // the heading looks only at the stretch after the last break
  assert.strictEqual(C.heading([[0, 0, 1], [100, 0, 2], [900, 0, 3, "t"], [901, 0, 4]], 5, 20, 4), null, "not across the teleport");
  assert.ok(C.heading([[0, 0, 1], [900, 0, 3, "t"], [910, 0, 4]], 5, 20, 4), "a walk after it");
  // history from the daemon keeps the kinds and drops junk kinds
  const t4 = C.newTracker(); let q2 = party(); q2.bots[0].x = 500; C.note(t4, q2, 1000);
  C.prependTrail(t4, "Gbmage", q2.bots[0].map, [[1, 2, 10, "t"], [3, 4, 20, "zzz"], [5, 6, 30, "d"]]);
  assert.deepStrictEqual(t4.trail.Gbmage.pts.slice(0, 3), [[1, 2, 10, "t"], [3, 4, 20], [5, 6, 30, "d"]]);
}
// a gap (reports lost in a very long pause): the next point starts a new piece, with no mark
{
  const tg = C.newTracker(); let q;
  const stepg = (x, t) => { q = party(); q.bots[0].x = x; q.bots[0].y = 0; C.note(tg, q, t); };
  stepg(0, 100); stepg(20, 102);
  C.markGap(tg);
  stepg(40, 104);
  assert.deepStrictEqual(tg.trail.Gbmage.pts[2], [40, 0, 104, "g"], "the point after a gap is marked");
  assert.strictEqual(C.trailRuns(tg.trail.Gbmage.pts).length, 2, "not joined to the point before");
  assert.strictEqual(C.trailEvent({ gap: true }, { map: 1, x: 0, y: 0, t: 1 }), "g");
  stepg(60, 106); assert.strictEqual(tg.trail.Gbmage.pts[3].length, 3, "only that one point");
}
// ---- bosses: a dungeon boss is an elite (rank 1) to the game server, so the module's rank letter is "e"; the run plan's boss steps
// name the bosses (live 2026-10-21: Kresh 3653, Lord Cobrahn 3669 and Lady Anacondra 3671 all came as rank "e").
{
  const run = { plan_steps: [{ i: 1, k: "move", x: 0, y: 0, e: 3669 }, { i: 2, k: "boss", x: 5, y: 5, e: 3669 }, { i: 3, k: "boss", x: 9, y: 9 },
    { i: 4, k: "pull", x: 1, y: 1, e: 3840 }, { i: 5, k: "boss", x: 7, y: 7, e: 3671, sk: true }] };
  const be = C.bossEntries({ run });
  assert.deepStrictEqual(Object.keys(be).sort(), ["3669", "3671"], "only boss steps with an entry (a skipped one still names a boss)");
  assert.deepStrictEqual(C.bossEntries({}), {}, "no run, no plan bosses");
  assert.deepStrictEqual(C.bossEntries({ run: { plan_steps: null } }), {});
  assert.deepStrictEqual(C.bossEntries(null), {});
  const cobrahn = { id: "1", entry: 3669, rank: "e", reaction: "h" }, trash = { id: "2", entry: 3840, rank: "e", reaction: "h" };
  assert.strictEqual(C.isBoss(cobrahn, be), true, "an elite the plan calls a boss is a boss");
  assert.strictEqual(C.isBoss(trash, be), false, "an elite pull target is not");
  assert.strictEqual(C.isBoss(cobrahn, {}), false, "without the plan an elite is not a boss");
  assert.strictEqual(C.isBoss({ id: "3", entry: 1, rank: "b" }, {}), true, "the module's boss rank always counts");
  assert.strictEqual(C.isBoss({ id: "3", entry: 1, rank: "b" }), true, "no plan map given");
  assert.strictEqual(C.isBoss(null, be), false);
  // A Dungeons-tab card is a pseudo-binding with no run of its own (the live run report has no plan): it borrows the boss steps of
  // the commander binding whose party shares a bot with it, and only of that one.
  const dgn = { commander: "dgn:43:1", dungeon: true, bots: [{ name: "Tankie", guid: 11, map: 43 }, { name: "Heals", guid: 12, map: 43 }] };
  const mine = { commander: "Gb", bots: [{ name: "Heals", guid: 12, map: 43 }], run };
  const other = { commander: "Zz", bots: [{ name: "Else", guid: 99, map: 43 }], run: { plan_steps: [{ i: 1, k: "boss", x: 0, y: 0, e: 5775 }] } };
  assert.deepStrictEqual(Object.keys(C.bossEntries(dgn, [other, mine])).sort(), ["3669", "3671"], "the run's own commander's plan");
  assert.deepStrictEqual(C.bossEntries(dgn, [other]), {}, "no shared bot, no plan");
  assert.deepStrictEqual(C.bossEntries(dgn), {}, "no commanders given");
  assert.deepStrictEqual(Object.keys(C.bossEntries(Object.assign({}, dgn, { dungeon: false }), [mine])), [], "a commander card reads only its own run");
  assert.deepStrictEqual(Object.keys(C.bossEntries({ dungeon: true, bots: [{ name: "Heals", map: 43 }] }, [{ bots: [{ name: "Heals", map: 43 }], run }])).sort(),
    ["3669", "3671"], "matched by name when there is no guid");
  assert.strictEqual(C.isBoss(cobrahn, C.bossEntries(dgn, [mine])), true, "Cobrahn is crowned on the Dungeons card");
}
// ---- the boss roster: every known boss of the map, alive, dead or not seen (owner 2026-10-21). Sources in priority order: a live boss in
// the feed (alive), the module's list (bosses/bosses_map: a, d, u), a "boss down" event of this card (dead), then unknown; positions are
// the spawn points (module) or the plan's boss steps, and a boss only seen live or killed is placed where it was seen.
{
  const lead = (o) => Object.assign({ name: "Tankie", guid: 1, lead: true, map: 70, x: 0, y: 0 }, o);
  const mod = { commander: "Gb", bots: [lead()], bosses_map: 70, bosses: [
    { e: 6910, n: "Revelosh", x: 10, y: 20, s: "a" }, { e: 6906, n: "Baelog", x: 30, y: 40, s: "d" }, { e: 7228, n: "Ironaya", x: 50, y: 60, s: "u" },
    { e: 0, n: "bad", x: 1, y: 1, s: "a" }, { e: 7, n: "nopos", s: "a" }, null, { e: 6906, n: "Baelog again", x: 9, y: 9, s: "a" }] };
  let r = C.bossRoster(mod, 70);
  assert.deepStrictEqual(r.map((b) => [b.entry, b.name, b.x, b.y, b.state, b.source]), [
    [6910, "Revelosh", 10, 20, "alive", "module"], [6906, "Baelog", 30, 40, "dead", "module"], [7228, "Ironaya", 50, 60, "unknown", "module"]],
  "module rows in order, malformed and repeated entries dropped");
  assert.deepStrictEqual(C.bossRoster(mod, 43), [], "the module's list is for its own map only");
  assert.deepStrictEqual(C.bossRoster(Object.assign({}, mod, { bosses: [{ e: 1, n: "X", x: 0, y: 0, s: "?" }] }), 70)[0].state, "unknown", "an unknown letter is unknown");
  assert.deepStrictEqual(C.bossRoster(null, 70), []); assert.deepStrictEqual(C.bossRoster(mod, null), []);
  // a live boss in the feed is alive, whatever the module said; the mark stays at the spawn point
  r = C.bossRoster(mod, 70, { mobs: [{ id: "5", entry: 6906, name: "Baelog", hp: 80, x: 99, y: 99, rank: "b" }] });
  assert.strictEqual(r[1].state, "alive", "seen alive in the feed"); assert.strictEqual(r[1].live, true); assert.strictEqual(r[1].x, 30, "spawn point kept");
  assert.strictEqual(C.bossRoster(mod, 70, { mobs: [{ id: "5", entry: 6906, hp: 0, x: 1, y: 1, rank: "b" }] })[1].state, "dead", "a 0 hp unit is no live boss");
  assert.strictEqual(C.bossRoster(mod, 70, { mobs: [{ id: "5", entry: 6906, hp: 50, x: 1, y: 1, rank: "e" }] })[1].state, "alive", "a unit of a listed entry is that boss, whatever its rank letter");
  assert.strictEqual(C.bossRoster(mod, 70, { mobs: [{ id: "5", entry: 4851, hp: 50, x: 1, y: 1, rank: "e" }] }).length, 3, "an elite nobody calls a boss is not added");
  // a live boss nobody listed is added where it stands
  r = C.bossRoster(mod, 70, { mobs: [{ id: "6", entry: 2748, name: "Archaedas", hp: 100, x: 7, y: 8, rank: "b" }] });
  assert.deepStrictEqual(r.slice(-1).map((b) => [b.entry, b.name, b.x, b.y, b.state, b.source]), [[2748, "Archaedas", 7, 8, "alive", "seen"]]);
  // a "boss down" event: dead, matched by entry or by name, on this map only; the module's alive beats the browser's guess
  const ev = (o) => Object.assign({ kind: "boss", map: 70, x: 3, y: 3 }, o);
  r = C.bossRoster(mod, 70, { events: [ev({ entry: 7228, name: "Ironaya" })] });
  assert.strictEqual(r[2].state, "dead", "event by entry");
  assert.strictEqual(C.bossRoster(mod, 70, { events: [ev({ name: "Ironaya" })] })[2].state, "dead", "event by name");
  assert.strictEqual(C.bossRoster(mod, 70, { events: [ev({ entry: 7228, map: 43 })] })[2].state, "unknown", "event of another map");
  assert.strictEqual(C.bossRoster(mod, 70, { events: [ev({ kind: "death", entry: 7228 })] })[2].state, "unknown", "a bot's death is no boss kill");
  assert.strictEqual(C.bossRoster(mod, 70, { events: [ev({ entry: 6910 })] })[0].state, "alive", "the module says alive (a reset after a wipe)");
  assert.strictEqual(C.bossRoster(mod, 70, { events: [ev({ entry: 6910 })], mobs: [] })[0].state, "alive");
  r = C.bossRoster(mod, 70, { events: [ev({ entry: 999, name: "Lost Boss", x: 4, y: 5 })] });
  assert.deepStrictEqual(r.slice(-1).map((b) => [b.entry, b.name, b.x, b.y, b.state, b.source]), [[999, "Lost Boss", 4, 5, "dead", "seen"]], "a kill nobody listed is kept where it fell");
  assert.strictEqual(C.bossRoster(mod, 70, { events: [ev({ entry: 0, name: "", x: null })] }).length, 3, "an event with no name or place adds nothing");
  // fallback without the module: the plan's boss steps (unknown), the feed, the events
  const run = { step: 3, plan_steps: [{ i: 1, k: "boss", x: 5, y: 6, e: 3669, n: "Lord Cobrahn" }, { i: 2, k: "pull", x: 1, y: 1, e: 3840 },
    { i: 4, k: "boss", x: 7, y: 8, e: 3671 }, { i: 5, k: "boss", x: 7, y: 8 }] };
  const plain = { commander: "Gb", bots: [lead({ map: 43 })], run };
  r = C.bossRoster(plain, 43);
  assert.deepStrictEqual(r.map((b) => [b.entry, b.name, b.x, b.y, b.state, b.source]), [[3669, "Lord Cobrahn", 5, 6, "unknown", "plan"], [3671, "creature 3671", 7, 8, "unknown", "plan"]]);
  assert.deepStrictEqual(C.bossRoster(plain, 70), [], "the plan is the lead's map's plan");
  r = C.bossRoster(plain, 43, { mobs: [{ id: "1", entry: 3671, name: "Lady Anacondra", hp: 40, x: 0, y: 0, rank: "e" }], events: [ev({ map: 43, entry: 3669 })] });
  assert.deepStrictEqual(r.map((b) => b.state), ["dead", "alive"], "a plan boss elite in the feed is alive; the event kills Cobrahn");
  assert.strictEqual(r[1].name, "Lady Anacondra", "the live unit names a step that had no name");
  // the module wins over the plan for the same entry (it has the spawn point)
  const both = Object.assign({}, plain, { bosses_map: 43, bosses: [{ e: 3669, n: "Lord Cobrahn", x: 50, y: 60, s: "d" }] });
  r = C.bossRoster(both, 43);
  assert.deepStrictEqual(r.map((b) => [b.entry, b.x, b.state, b.source]), [[3669, 50, "dead", "module"], [3671, 7, "unknown", "plan"]]);
  // a Dungeons-tab card borrows the roster of the commander binding that shares a bot with it, on the card's map only
  const dgn = { commander: "dgn:43:1", dungeon: true, bots: [{ name: "Tankie", guid: 1, map: 43 }] };
  const other = { commander: "Zz", bots: [{ name: "Else", guid: 99, map: 43, lead: true }], bosses_map: 43, bosses: [{ e: 5775, n: "Verdan", x: 1, y: 1, s: "a" }] };
  assert.deepStrictEqual(C.bossRoster(dgn, 43, { others: [other, both] }).map((b) => b.entry), [3669, 3671], "its own commander's, not the other copy's");
  assert.deepStrictEqual(C.bossRoster(dgn, 43, { others: [other] }), [], "no shared bot, nothing borrowed");
  assert.deepStrictEqual(C.bossRoster(dgn, 70, { others: [both] }), [], "another map");
  assert.deepStrictEqual(C.bossRoster(Object.assign({}, dgn, { dungeon: false }), 43, { others: [both] }), [], "a commander card reads only its own");
  // the count for the run line: every boss when none is marked as the plan's
  const cnt = (r) => { const c = C.bossCount(r); return [c.down, c.total, c.outside.map((b) => b.entry)]; };
  assert.deepStrictEqual(cnt(C.bossRoster(mod, 70)), [1, 3, []]);
  assert.deepStrictEqual(cnt([]), [0, 0, []]); assert.deepStrictEqual(cnt(null), [0, 0, []]);
  // the run bosses: the module's p flag (a plan boss step names the entry); the others are outside the plan
  const flagged = Object.assign({}, mod, { bosses: [{ e: 6910, n: "Revelosh", x: 1, y: 1, s: "d", p: 1 }, { e: 6906, n: "Baelog", x: 1, y: 1, s: "u", p: 1 },
    { e: 2748, n: "Archaedas", x: 1, y: 1, s: "d" }] });
  r = C.bossRoster(flagged, 70);
  assert.deepStrictEqual(r.map((b) => b.plan), [true, true, false]);
  assert.deepStrictEqual(cnt(r), [1, 2, [2748]], "Archaedas is killed but outside the plan: not counted, named apart");
  // the plan steps mark their entries too (also an entry the module listed without p), and a boss seen live of a plan entry
  r = C.bossRoster(both, 43);
  assert.deepStrictEqual(r.map((b) => [b.entry, b.plan]), [[3669, true], [3671, true]]);
  assert.deepStrictEqual(cnt(r), [1, 2, []]);
  r = C.bossRoster({ bots: [lead({ map: 43 })], bosses_map: 43, bosses: [{ e: 3653, n: "Kresh", x: 0, y: 0, s: "u" }], run }, 43);
  assert.deepStrictEqual(r.map((b) => [b.entry, b.plan]), [[3653, false], [3669, true], [3671, true]]);
  assert.deepStrictEqual(cnt(r), [0, 2, [3653]], "Kresh is outside the plan");
  // a capped roster
  const many = { bots: [lead()], bosses_map: 70, bosses: Array.from({ length: 60 }, (_, i) => ({ e: i + 1, n: "B" + i, x: i, y: i, s: "u" })) };
  assert.strictEqual(C.bossRoster(many, 70).length, C.BOSSES_MAX);
}
{
  assert.strictEqual(C.isDungeonMap(70), true); assert.strictEqual(C.isDungeonMap(409), true);
  [0, 1, 530, 571, null, undefined, "70", -1].forEach((m) => assert.strictEqual(C.isDungeonMap(m), false, String(m)));
}
console.log("commanders.js ok");
