"use strict";
// Run by commandersjs_test.go under node: the Raids, Raid coverage and Commanders tabs show the "not ported" notice when GET /api/v1/core
// says the core does not feed them (ForeverCore), and show nothing on the Turtle core or when the route does not answer.
//   node core_notice_ui_check.js <app.js> <commanders.js>
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const src = fs.readFileSync(path.resolve(process.argv[2]), "utf8");
const CmdView = require(path.resolve(process.argv[3]));
function grab(name) {
  const m = new RegExp(`^  function ${name}\\(`, "m").exec(src);
  assert.ok(m, `${name} not found`);
  return src.slice(m.index, src.indexOf("\n  }\n", m.index) + 4);
}

function el() {
  const p = { textContent: "x" };
  return { style: { display: "none" }, querySelector: (sel) => (sel === "p" ? p : null), p };
}
function page(core, ok = true) {
  const boxes = { "core-note-raids": el(), "core-note-coverage": el(), "core-note-commanders": el(), "cmd-empty-how": { textContent: "bind a bot" } };
  const ctx = {
    CmdView, console, Promise,
    state: { core: null },
    document: { getElementById: (id) => boxes[id] || null },
    fetch: (url) => {
      assert.strictEqual(url, "/api/v1/core");
      return Promise.resolve({ ok, json: () => Promise.resolve(core) });
    },
  };
  vm.createContext(ctx);
  vm.runInContext([grab("applyCoreNotices"), grab("loadCore")].join("\n"), ctx);
  return { ctx, boxes };
}
const tick = () => new Promise((r) => setTimeout(r, 5));

(async () => {
  // ForeverCore: all three notices are shown with their text, and the empty Commanders page names the run command
  let { ctx, boxes } = page({ core: "forever", ported: { raids: false, coverage: false, commander_orders: false, combat: false } });
  vm.runInContext("loadCore()", ctx);
  await tick();
  assert.strictEqual(boxes["core-note-raids"].style.display, "");
  assert.ok(/Raids are not ported to ForeverCore/.test(boxes["core-note-raids"].p.textContent));
  assert.strictEqual(boxes["core-note-coverage"].style.display, "");
  assert.ok(/every boss shows as a gap/.test(boxes["core-note-coverage"].p.textContent));
  assert.strictEqual(boxes["core-note-commanders"].style.display, "");
  assert.ok(/\.tb run start/.test(boxes["cmd-empty-how"].textContent));
  assert.strictEqual(ctx.state.core.core, "forever");

  // Turtle: nothing shown, the how-to line is left as it was
  ({ ctx, boxes } = page({ core: "turtle", ported: { raids: true, coverage: true, commander_orders: true, combat: true } }));
  vm.runInContext("loadCore()", ctx);
  await tick();
  ["core-note-raids", "core-note-coverage", "core-note-commanders"].forEach((id) => assert.strictEqual(boxes[id].style.display, "none", id));
  assert.strictEqual(boxes["cmd-empty-how"].textContent, "bind a bot");

  // an older daemon (404): no state, no notice, no error
  ({ ctx, boxes } = page(null, false));
  vm.runInContext("loadCore()", ctx);
  await tick();
  assert.strictEqual(ctx.state.core, null);
  assert.strictEqual(boxes["core-note-raids"].style.display, "none");

  // the source wiring: loaded once at start, and the Commanders menu item is always on on ForeverCore
  assert.ok(/^  loadCore\(\);$/m.test(src), "loadCore is called at start");
  assert.ok(/const on = isForever \|\| /.test(src), "the Commanders tab is always shown on ForeverCore");
  console.log("core notice ui ok");
})().catch((e) => { console.error(e); process.exit(1); });
