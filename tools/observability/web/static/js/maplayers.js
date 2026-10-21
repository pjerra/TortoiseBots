"use strict";
// Commanders tab map layers: which things a map draws. One choice for every card (and every map), kept in the browser once, not per
// map. Pure logic (no page, no markup) so a test can run it under node (internal/commandersjs). app.js draws and shows the panel.
//
// The panel offers 20 layers in 5 groups. Several of them are merges of the finer switches the maps used before: `effective(state)`
// returns those older keys (mobhostile, mobneutral, trail, threatcolors, ...) computed from the new ones, and app.js's drawing code
// reads them, so the draw code did not have to change shape.
//
// Besides the on/off choices the state holds `dim`: how much the real map picture is darkened, 0-80 %; `trailLen`: how many positions of
// a path are drawn (10-240); and for each line layer (`line: true`) `style[key] = {op, th}`: its opacity (10-100 %) and thickness (50-300 %
// of the normal width).
//
// A layer has a key, a short label, a group, a tooltip (`tip`, the long explanation), a default, the old keys it merges (`parts`) and
// `needs`: the data each part can only be drawn from. A layer is greyed out in the panel only when NONE of its parts has data; the
// saved choice is kept for when the data arrives.
(function (root) {
  const STORAGE_KEY = "tbdash.mapLayers.v3";
  const PREV_STORAGE_KEY = "tbdash.mapLayers.v2"; // the 32-switch set; read once when v3 is missing
  const OLD_STORAGE_KEY = "tbdash.mapLayers.v1"; // booleans only; read once when v2 and v3 are missing (the picture was darkened 30 % then)
  const DIM_MAX = 80, DIM_DEFAULT = 30;
  const TRAIL_LEN_MIN = 10, TRAIL_LEN_MAX = 240, TRAIL_LEN_DEFAULT = 120;
  const OP_MIN = 10, OP_MAX = 100, TH_MIN = 50, TH_MAX = 300, STYLE_DEFAULT = 100;

  // The keys the drawing code used before the merge, with what each was drawn from. Kept so effective() can hand app.js the old
  // shape, and so a v2 saved choice can be migrated (a merged layer is on if any of its old layers was).
  const OLD_LAYERS = {
    bg: { def: true, needs: "picture", why: "no map picture for this map (the daemon needs TBDASH_MAP_DIR)" },
    grid: { def: true },
    zone: { def: true, needs: "zone", why: "the zone of the party is not known" },
    zonelabels: { def: false, needs: "zonesAround", why: "no zone outlines known for this map" },
    scale: { def: false },
    bots: { def: true, needs: "bots", why: "no bot on this map" },
    botnames: { def: true, needs: "bots", why: "no bot on this map" },
    lead: { def: true, needs: "lead", why: "the lead is not reporting" },
    trail: { def: true, needs: "trail", why: "the lead has not moved yet", line: true },
    membertrails: { def: false, needs: "memberTrails", why: "no other bot has moved yet", line: true },
    trailtime: { def: false, needs: "trail", why: "the lead has not moved yet" },
    heading: { def: false, needs: "heading", why: "the lead has not moved enough to tell" },
    healring: { def: false, needs: "healer", why: "no living healer on this map" },
    commander: { def: false, needs: "commanderPos", why: "the commander is not online on the map of the party" },
    mobhostile: { def: false, needs: "mobs", why: "the game server does not report mobs yet (it needs the newer module)" },
    mobneutral: { def: false, needs: "mobs", why: "the game server does not report mobs yet (it needs the newer module)" },
    mobcritter: { def: false, needs: "mobs", why: "the game server does not report mobs yet (it needs the newer module)" },
    aggro: { def: false, needs: "mobs", why: "the game server does not report mobs yet (it needs the newer module)", line: true },
    hostilenames: { def: false, needs: "mobs", why: "the game server does not report mobs yet (it needs the newer module)" },
    bossmark: { def: true, needs: "mobs", why: "the game server does not report mobs yet (it needs the newer module)" },
    threatcolors: { def: false, needs: "mobTargets", why: "the game server does not report mobs yet (it needs the newer module)" },
    threat: { def: false, needs: "mobTargets", why: "the game server does not report mobs yet (it needs the newer module)", line: true },
    bottarget: { def: false, needs: "botTargets", why: "the game server does not report bot targets yet (it needs the newer module)", line: true },
    healline: { def: false, needs: "activity", why: "the game server does not report what units do yet (it needs the newer module)", line: true },
    ccline: { def: false, needs: "activity", why: "the game server does not report what units do yet (it needs the newer module)", line: true },
    hpbars: { def: true },
    statusicons: { def: false, needs: "activity", why: "the game server does not report what units do yet (it needs the newer module)" },
    stalls: { def: false, needs: "stalls", why: "nothing has stalled or been refused since this page started watching" },
    events: { def: false, needs: "events", why: "nothing has happened since this page started watching" },
    feed: { def: false, needs: "events", why: "nothing has happened since this page started watching" },
    runsteps: { def: true, needs: "runSteps", why: "no run is going, or the game server does not send the plan steps yet (it needs the newer module)", line: true },
    runinfo: { def: false },
    objects: { def: false, needs: "objects", why: "the game server does not report objects yet (it needs the newer module)" },
  };

  // The 20 layers the panel offers. `parts` are the old switches each one replaces; `styleFrom` is the old line key whose opacity and
  // thickness a merged line layer takes over (its first old key that had a style).
  const DEFS = [
    { key: "bg", label: "Map picture", group: "Map", def: true, parts: ["bg"],
      tip: "The real game-map picture under the party (the daemon needs TBDASH_MAP_DIR). The Dim slider below darkens it." },
    { key: "grid", label: "Grid and scale bar", group: "Map", def: true, parts: ["grid", "scale"],
      tip: "The distance grid over the map and the scale bar under it." },
    { key: "zone", label: "Zone names", group: "Map", def: true, parts: ["zone", "zonelabels"],
      tip: "The outline and name of the party's zone, and the names of the zones around." },
    { key: "bots", label: "Bots and lead mark", group: "Party", def: true, parts: ["bots", "lead"],
      tip: "A dot for every bot on the map; the lead is ringed." },
    { key: "botnames", label: "Bot names", group: "Party", def: true, parts: ["botnames"],
      tip: "The name next to every bot on the map (the lead's is starred). Turn it off when the names cover a fight." },
    { key: "hpbars", label: "Health bars and status icons", group: "Party", def: true, parts: ["hpbars", "statusicons"],
      tip: "A health bar over every unit, and icons for dead, casting, crowd controlled, drinking or eating, and fleeing." },
    { key: "leadpath", label: "Lead path", group: "Party", def: true, line: true, styleFrom: "trail", parts: ["trail", "trailtime", "heading"],
      tip: "Where the lead has walked, with the times on the path and its heading arrow (from its movement)." },
    { key: "memberpaths", label: "Other members paths", group: "Party", def: false, line: true, styleFrom: "membertrails", parts: ["membertrails"],
      tip: "Where the other group members have walked, coloured by role." },
    { key: "partyextras", label: "Healer range ring and commander line", group: "Party", def: false, parts: ["healring", "commander"],
      tip: "A 40 yd ring around each living healer, and the commander with a line to the lead." },
    { key: "enemies", label: "Enemies", group: "Enemies", def: true, parts: ["mobhostile", "mobneutral"],
      tip: "Hostile and neutral units the game server reports near the party." },
    { key: "critters", label: "Critters", group: "Enemies", def: false, parts: ["mobcritter"],
      tip: "Critters (small harmless units) the game server reports." },
    { key: "enemynames", label: "Enemy names and boss marker", group: "Enemies", def: true, parts: ["hostilenames", "bossmark"],
      tip: "The name of every hostile unit; a boss is bigger, crowned and always named." },
    { key: "threat", label: "Threat colours and lines", group: "Enemies", def: true, line: true, styleFrom: "threat", parts: ["threatcolors", "threat"],
      tip: "A ring in the colour of whom the unit attacks (tank blue, others red), the tank's and the pull target marked, and a line to the unit it attacks." },
    { key: "aggro", label: "Aggro range", group: "Enemies", def: false, line: true, styleFrom: "aggro", parts: ["aggro"],
      tip: "The area in which a pull target would notice the party (20 yd, one more per level above the lead)." },
    { key: "actionlines", label: "Target, heal and crowd-control lines", group: "Lines and run", def: false, line: true, styleFrom: "bottarget", parts: ["bottarget", "healline", "ccline"],
      tip: "A line from each bot to its target, from a healer to whom it heals, and from a caster to the unit it controls." },
    { key: "runsteps", label: "Run steps (route, pulls, bosses, stops)", group: "Lines and run", def: true, line: true, styleFrom: "runsteps", parts: ["runsteps"],
      tip: "The route of the current section of the run: dashed lines, numbered pulls and bosses, loot, use and rest stops. Needs a run leader that sends the plan steps." },
    { key: "events", label: "Run events (pins and feed)", group: "Lines and run", def: true, parts: ["events", "feed"],
      tip: "Pins where a unit died, a boss fell, a wipe happened or an object was opened or looted, and the feed under the map. Shown when there is an event." },
    { key: "stalls", label: "Stalls and refused orders", group: "Lines and run", def: false, parts: ["stalls"],
      tip: "A red cross where the run paused or an order was refused, with the reason." },
    { key: "runinfo", label: "Run timer and boss checklist", group: "Lines and run", def: true, parts: ["runinfo"],
      tip: "The run line under the map: how long the binding has been standing and the bosses down." },
    { key: "objects", label: "Doors, chests, levers, altars and gongs", group: "Objects", def: false, parts: ["objects"],
      tip: "Game objects the bots have seen near them, with what they are and whether they are used." },
  ];

  const OLD_KEYS = Object.keys(OLD_LAYERS);
  // old key -> the new layer that owns it
  const OWNER = {};
  DEFS.forEach((d) => d.parts.forEach((p) => { OWNER[p] = d.key; }));
  // No old key is dead any more: the run-route layer ("runsteps") is offered here because the run leader lives on this branch.
  const DEAD_OLD_KEYS = [];

  const keys = () => DEFS.map((d) => d.key);

  function defaults() {
    const o = {};
    DEFS.forEach((d) => { o[d.key] = d.def; });
    o.dim = DIM_DEFAULT;
    o.trailLen = TRAIL_LEN_DEFAULT;
    o.style = {};
    DEFS.forEach((d) => { if (d.line) o.style[d.key] = { op: STYLE_DEFAULT, th: STYLE_DEFAULT }; });
    return o;
  }

  const clampInt = (n, lo, hi, dflt) => (typeof n === "number" && isFinite(n) ? Math.min(hi, Math.max(lo, Math.round(n))) : dflt);
  const clampTrailLen = (n) => clampInt(n, TRAIL_LEN_MIN, TRAIL_LEN_MAX, TRAIL_LEN_DEFAULT);

  // A dim percent as a whole number in 0..DIM_MAX; anything else is the default.
  function clampDim(n) {
    return typeof n === "number" && isFinite(n) ? Math.min(DIM_MAX, Math.max(0, Math.round(n))) : DIM_DEFAULT;
  }

  // Whatever was stored, as a full set: unknown keys are dropped, non-booleans fall back to the default.
  function normalize(raw) {
    const out = defaults();
    if (raw && typeof raw === "object" && !Array.isArray(raw)) {
      DEFS.forEach((d) => { if (typeof raw[d.key] === "boolean") out[d.key] = raw[d.key]; });
      // a choice saved before the names had their own layer: they were part of "bots", so they follow it
      if (typeof raw.botnames !== "boolean" && typeof raw.bots === "boolean") out.botnames = raw.bots;
      if ("dim" in raw) out.dim = clampDim(raw.dim);
      if ("trailLen" in raw) out.trailLen = clampTrailLen(raw.trailLen);
      if (raw.style && typeof raw.style === "object" && !Array.isArray(raw.style)) {
        DEFS.forEach((d) => {
          const r = raw.style[d.key];
          if (d.line && r && typeof r === "object") out.style[d.key] = { op: clampInt(r.op, OP_MIN, OP_MAX, STYLE_DEFAULT), th: clampInt(r.th, TH_MIN, TH_MAX, STYLE_DEFAULT) };
        });
      }
    }
    return out;
  }

  // A stored v1/v2 choice in the old keys' shape: booleans for every old layer (missing ones get their old default), the dim, the path
  // length and the per-line styles.
  function normalizeOld(raw) {
    const out = { dim: DIM_DEFAULT, trailLen: TRAIL_LEN_DEFAULT, style: {} };
    OLD_KEYS.forEach((k) => { out[k] = OLD_LAYERS[k].def; });
    if (raw && typeof raw === "object" && !Array.isArray(raw)) {
      OLD_KEYS.forEach((k) => { if (typeof raw[k] === "boolean") out[k] = raw[k]; });
      if ("dim" in raw) out.dim = clampDim(raw.dim);
      if ("trailLen" in raw) out.trailLen = clampTrailLen(raw.trailLen);
      if (raw.style && typeof raw.style === "object" && !Array.isArray(raw.style)) {
        OLD_KEYS.forEach((k) => {
          const r = raw.style[k];
          if (OLD_LAYERS[k].line && r && typeof r === "object") out.style[k] = { op: clampInt(r.op, OP_MIN, OP_MAX, STYLE_DEFAULT), th: clampInt(r.th, TH_MIN, TH_MAX, STYLE_DEFAULT) };
        });
      }
    }
    return out;
  }

  // v1/v2 -> v3: a merged layer whose old switches are all still at their v2 defaults was never touched, so it takes the NEW default
  // (that is how existing users get enemies, threat, events and the run line on); otherwise it is on when any of its old switches was.
  // A merged line layer takes its first old line key's style.
  function fromOld(raw) {
    const old = normalizeOld(raw), out = defaults();
    DEFS.forEach((d) => {
      const untouched = d.parts.every((p) => old[p] === OLD_LAYERS[p].def);
      out[d.key] = untouched ? d.def : d.parts.some((p) => old[p] === true);
    });
    out.dim = old.dim;
    out.trailLen = old.trailLen;
    DEFS.forEach((d) => {
      if (!d.line) return;
      const st = old.style[d.styleFrom] || { op: STYLE_DEFAULT, th: STYLE_DEFAULT };
      out.style[d.key] = { op: st.op, th: st.th };
    });
    return out;
  }

  // storage is a Storage-like {getItem, setItem} (or null / one that throws: the defaults are then used and nothing is kept).
  // v3 is read first; without it a v2 choice is migrated once (and a v1 one when v2 is missing too). The next save writes v3.
  function load(storage) {
    try {
      if (!storage) return defaults();
      const s3 = storage.getItem(STORAGE_KEY);
      if (s3) return normalize(JSON.parse(s3));
      const s2 = storage.getItem(PREV_STORAGE_KEY);
      if (s2) return fromOld(JSON.parse(s2));
      const s1 = storage.getItem(OLD_STORAGE_KEY);
      return s1 ? fromOld(JSON.parse(s1)) : defaults();
    } catch (e) {
      return defaults();
    }
  }

  function save(storage, state) {
    try {
      if (!storage) return false;
      storage.setItem(STORAGE_KEY, JSON.stringify(normalize(state)));
      return true;
    } catch (e) {
      return false;
    }
  }

  function set(state, key, on) {
    const out = normalize(state);
    if (key !== "dim" && DEFS.some((d) => d.key === key)) out[key] = !!on;
    return out;
  }

  const lineKey = (key) => (DEFS.some((d) => d.key === key && d.line) ? key : null);

  // The opacity (0.1-1) and the thickness factor (0.5-3) of a line layer; the normal look for anything else.
  function styleOf(state, key) {
    const k = lineKey(key);
    const st = (k && normalize(state).style[k]) || { op: STYLE_DEFAULT, th: STYLE_DEFAULT };
    return { op: st.op / 100, th: st.th / 100 };
  }

  // field is "op" or "th"; value in percent. A key that is not a line layer is left alone.
  function setStyle(state, key, field, value) {
    const out = normalize(state), k = lineKey(key);
    if (!k || (field !== "op" && field !== "th")) return out;
    out.style[k][field] = field === "op" ? clampInt(Number(value), OP_MIN, OP_MAX, STYLE_DEFAULT) : clampInt(Number(value), TH_MIN, TH_MAX, STYLE_DEFAULT);
    return out;
  }

  function setTrailLen(state, n) {
    const out = normalize(state);
    out.trailLen = clampTrailLen(Number(n));
    return out;
  }

  function setDim(state, pct) {
    const out = normalize(state);
    out.dim = clampDim(Number(pct));
    return out;
  }

  // What each of the 20 layers can be drawn from, for one map (one card): {key: {ok, why}}. ctx has the booleans named in
  // OLD_LAYERS' `needs`; a missing one counts as false. ok when ANY of the layer's parts has its data (the rest is simply not drawn);
  // why lists the missing parts' reasons.
  function availability(ctx) {
    const c = ctx || {}, out = {};
    DEFS.forEach((d) => {
      const missing = d.parts.filter((p) => OLD_LAYERS[p].needs && !c[OLD_LAYERS[p].needs]);
      // a map with no picture at all says why: the map folder's own minimap reason (ctx.pictureWhy) when there is one
      const why = missing.map((p) => (p === "bg" && typeof c.pictureWhy === "string" && c.pictureWhy) || OLD_LAYERS[p].why || "no data")
        .filter((w, i, a) => a.indexOf(w) === i).join("; ");
      out[d.key] = missing.length === d.parts.length ? { ok: false, why: why || "no data" } : { ok: true, why: "" };
    });
    return out;
  }

  // Availability over several maps: a layer is available when any of them can draw it.
  function mergeAvailability(list) {
    const out = {};
    DEFS.forEach((d) => {
      const hit = (list || []).find((a) => a && a[d.key] && a[d.key].ok);
      out[d.key] = hit ? { ok: true, why: "" } : { ok: false, why: ((list || [])[0] && list[0][d.key] && list[0][d.key].why) || "no data" };
    });
    return out;
  }

  // The old keys' on/off choices, computed from the new ones alone: the reference mapping of the merge (tests read it). The page does
  // NOT call this: it calls active(), which also checks each old key's own data so a part of an on layer without data is not switched on.
  function effective(state) {
    const st = normalize(state), out = {};
    OLD_KEYS.forEach((k) => { out[k] = !!st[OWNER[k]]; });
    DEAD_OLD_KEYS.forEach((k) => { out[k] = false; });
    return out;
  }

  // Which old keys get drawn: chosen, their merged layer available, and their OWN data there (a layer can be available through one part
  // while another part's data is missing: no status icons without activity data, no commander mark when only a healer is there, no
  // heading arrow without a heading). ctx is what availability() got; without it only the merged layer is checked.
  function active(state, avail, ctx) {
    const st = normalize(state), out = {};
    OLD_KEYS.forEach((k) => {
      const need = OLD_LAYERS[k].needs;
      const has = !ctx || !need || !!ctx[need];
      out[k] = !!st[OWNER[k]] && has && !!(avail && avail[OWNER[k]] && avail[OWNER[k]].ok);
    });
    DEAD_OLD_KEYS.forEach((k) => { out[k] = false; });
    return out;
  }

  // The union of the commander feed's mobs and a dungeon run's mobs, by guid: the feed comes first and wins every field it carries
  // (it has the threat and targets of the bound party); the run's list adds the mobs the feed's cap left out, and fills fields the
  // feed left empty. Entries without an id cannot be matched and are dropped. Pure: no state, no drawing.
  function mobUnion(feed, run) {
    const out = [], at = Object.create(null);
    const add = (m) => {
      if (!m || typeof m !== "object" || m.id == null || m.id === "") return;
      const id = String(m.id), i = at[id];
      if (i === undefined) {
        at[id] = out.length;
        out.push(Object.assign({}, m, { id }));
        return;
      }
      const keep = out[i];
      Object.keys(m).forEach((k) => { if ((keep[k] == null || keep[k] === "") && m[k] != null && m[k] !== "") keep[k] = m[k]; });
    };
    (feed || []).forEach(add);
    (run || []).forEach(add);
    return out;
  }

  // The dungeon run a commander binding's card is showing, when there is exactly one copy of its map with a bot of the party in it:
  // the card has no instance id, so map + a shared bot is the match. A bot matches by guid whenever BOTH sides have one (different
  // guids never match, however the names read, so two copies of one dungeon stay apart); the name is only used for a member one side
  // genuinely reports without a guid. The demo card and a Dungeons-tab binding (it already carries its own run) never match.
  function findRun(bind, runs, mapId) {
    if (!bind || bind.dungeon || bind.demo || !Array.isArray(runs)) return null;
    const bots = Array.isArray(bind.bots) ? bind.bots : [];
    if (!bots.length) return null;
    const map = mapId == null ? bots[0].map : mapId;
    const matches = runs.filter((r) => {
      if (!r || r.map !== map) return false;
      return (Array.isArray(r.members) ? r.members : []).some((m) => m && bots.some((b) => b && (b.guid != null && m.guid != null
        ? b.guid === m.guid
        : !!b.name && !!m.name && b.name === m.name)));
    });
    return matches.length === 1 ? matches[0] : null;
  }

  // "N of M enemies shown" when the module's cap hid some of them; empty otherwise.
  function enemiesText(shown, total) {
    const n = typeof shown === "number" && isFinite(shown) ? shown : 0;
    const m = typeof total === "number" && isFinite(total) ? total : 0;
    return m > n ? `${n} of ${m} enemies shown` : "";
  }

  const groups = () => DEFS.reduce((g, d) => (g.indexOf(d.group) < 0 ? g.concat(d.group) : g), []);

  const api = { STORAGE_KEY, PREV_STORAGE_KEY, OLD_STORAGE_KEY, DIM_MAX, DIM_DEFAULT, clampDim, setDim, TRAIL_LEN_MIN, TRAIL_LEN_MAX, TRAIL_LEN_DEFAULT, OP_MIN, OP_MAX, TH_MIN, TH_MAX,
    styleOf, setStyle, setTrailLen, clampTrailLen, DEFS, OLD_LAYERS, OLD_KEYS, keys, defaults, normalize, normalizeOld, fromOld, load, save, set, availability, mergeAvailability,
    effective, active, groups, mobUnion, findRun, enemiesText };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.MapLayers = api;
})(typeof self !== "undefined" ? self : this);
