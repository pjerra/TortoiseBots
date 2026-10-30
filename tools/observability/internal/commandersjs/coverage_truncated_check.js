"use strict";
// Run by commandersjs_test.go under node: a truncated tactics snapshot (the
// daemon hit a query's row cap) marks every database-answered need unknown and
// warns on the page; the rows before the cut are never used as proof.
const assert = require("assert");
const path = require("path");
const C = require(path.resolve(process.argv[2]));

function ability(sid, name, flags) {
  return Object.assign({
    sid, name, castMs: 0, a: 0, b: 0, c: 0, phase: "", target: "",
    aura: false, self: false, mark: false, ground: false, cone: false, tail: false, int: false, summon: 0, hp: 0,
    ok: "", okNeeds: {},
  }, flags || {});
}

const TABLE = {
  version: 1,
  maps: [{ key: "testraid", mapId: 900, name: "Test Raid", bosses: [90001] }],
  bosses: {
    "90001": {
      entry: 90001, name: "Boss One", map: "testraid", order: 1,
      abilities: [ability(91001, "Spell A", { int: true })],
      phases: [], adds: [], notes: [],
    },
  },
  codeAnswers: [],
};

const ROWS = {
  tactics: [
    { map: 900, boss: 90001, kind: "interrupt", target: 0, spell: 91001, priority: 0, param: 0, comment: "<img onerror=\"alert(1)\">" },
  ],
  stands: [],
  planKillFirst: [],
  source: "world",
  loadedAt: "2026-10-30T12:34:00Z",
};

// ---- complete rows answer the interrupt need -----------------------------------------------------------
const full = C.compute(TABLE, ROWS);
assert.strictEqual(full.raids[0].bosses[0].lines[0].status, "answered", "complete rows keep the interrupt answered");

// ---- a cut snapshot: unknown, no DB-funded answer, and a banner ----------------------------------------
const cut = C.compute(TABLE, Object.assign({}, ROWS, { truncated: true, truncatedAt: 5000 }));
const line = cut.raids[0].bosses[0].lines[0];
assert.strictEqual(line.needs.interrupt, "unknown", "a DB-dependent need is unknown when the rows were cut");
assert.strictEqual(line.status, "unknown", "the line is unknown, not answered or missing: " + line.status);
assert.ok(!line.answers.some((answer) => /interrupt row/.test(answer.text)),
  "a row before the cut does not answer the need: " + JSON.stringify(line.answers));
assert.ok(cut.raids[0].bosses[0].counts.unknown >= 1, "the counts carry the unknown line");
assert.ok(cut.raids[0].bosses[0].counts.answered === 0, "no line reads answered from the cut rows");
assert.ok(cut.raids[0].bosses[0].counts.missing === 0, "no line reads missing from the cut rows");

const page = C.renderRaid(cut, { raid: "testraid", missing: false, boss: null }, {});
assert.ok(page.includes(C._esc("the server's tactic rows were cut off at 5000 rows: coverage from the database is incomplete")),
  "the page names the cap in the incomplete-coverage banner");
assert.ok(!/<img/.test(page), "the cut banner and the row comments stay escaped");

// ---- the banner names whichever table hit its cap ------------------------------------------------------
const plans = C.compute(TABLE, Object.assign({}, ROWS, { truncated: true, truncatedAt: 1000 }));
const plansPage = C.renderRaid(plans, { raid: "testraid", missing: false, boss: null }, {});
assert.ok(plansPage.includes("cut off at 1000 rows"), "a plan-table cap is named too");

// ---- a complete snapshot keeps the old page -------------------------------------------------------------
const plain = C.renderRaid(full, { raid: "testraid", missing: false, boss: null }, {});
assert.ok(!plain.includes("cut off"), "a complete snapshot has no incomplete-coverage banner");
assert.ok(plain.includes('<span class="cov-need cov-need-full">interrupt ✓</span>'), "the complete page still ticks the need");

console.log("coverage truncated ok");
