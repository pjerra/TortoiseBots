# REVFIX-PORT-1: review-fix round on the merged run glue (branch fix/run-review-1)

Input: the Gemini review of `55dff9543..tbf/main` (`~/TortoiseBots-audit/reviews/gemini-port-merged.md`). Every finding was read against the code first.
Tests: `forever/tools/test_run_revfix1_wiring.py` (8 cases; 7 of them fail on the commit before the fixes) and a new block in `forever/tools/test_run_orders.cpp`.

| # | Verdict | Where | Fix |
|---|---|---|---|
| 1 | REAL | `TbRunStands.cpp` `PathInLiquid` | `PathPrefixRefused` (the R6 round 2 move rule) instead of `PathRefused` |
| 2 | REAL | `TbRun.cpp` `CancelRunWork` | `ForgetRunOrders(rec.leader)` |
| 3 | dashboard: see below; `TbScripts.cpp:472` REAL | `TbScripts.cpp` `.tb where` | `FindMap()` and skip when null |
| 4 | REAL (map mismatch; the assert needs a rarer case) | `TbOrders.cpp` `move-direct` | `FindMap`, `fail:wrong-map` |
| 5 | REAL | `TbRunFacts.cpp` `ReadRezRoster` | `SameMapObject` (Map object compare) |
| 6 | REAL (cost only, same verdict) | `TbRunOrders.cpp` `CheckLeadMove` | TooFar returns before path and liquid reads |
| 7a | REAL | `TbHostBotManager.cpp` `BindBotMaster` | always `ai->SetMaster(master)` |
| 7b | NOT REAL | `TbRun.cpp:516` | none, see below |

## 1 (HIGH) wet-origin escape: REAL, but the premise needs the round 2 rule stated right
RUN-S6-M-NOTES R6 round 2 (item 7 of the R6 table and the "R6 live re-check" table) set ONE rule for a MOVE of a bot that stands in liquid: wet samples are allowed only as the
contiguous prefix from the origin, every wet sample after the first dry one refuses, a wet end refuses ("a mover that already stands in liquid may walk out of it"). `CheckLeadMove`
(`TbRunOrders.cpp`) applies it to `come`/`walk`, `move-direct` applies it at execution. The stand searches stayed on the round 1 rule: `PathInLiquid` called
`liquid::PathRefused`, which refuses a wet sample ANYWHERE, including `points[0]`, the lead's own spot. `SearchStallStand` exists for the case "the stalled walk's own move had failed in
the water" (comment at the dry-ground test), so with the lead wet every candidate was refused and the search answered "none" although the `come` to that stand would have been accepted by
the executor. So the review is right about the inconsistency, and the intended rule ("walk out of the liquid you stand in, any later liquid refused") is exactly what `PathPrefixRefused`
gives; no valid-escape is blocked by the intended rule, the stand search simply never applied it. The candidate point itself is still refused when wet (`InLiquid` before the path), and a wet
destination, wet -> dry -> wet and wet -> wet still refuse. Fix and test: wiring `test_1_*`; the prefix rule itself is covered by `test_run_liquid`.

## 2 (HIGH) pause leaves pending orders: REAL
`CancelRunOrders` (TbOrders.cpp:897) removes the queued run orders and writes `fail:cancelled` as the bot's result. `PendingByRun()[leader]` is cleared only by `ForgetRunOrders` in `Finish`
(TbRun.cpp:490) and by `TakeLateFailures` settling; a paused tick returns before `TakeLateFailures` (TbRun.cpp:575-594). After `.tb run resume` the first running tick settles the entry:
`fail:cancelled` (or `fail:expired` after 20 s) -> `DungeonRun::OrderRefused` -> `Pause(... "cmd <order> refused: failed: ...")` (DungeonRunPolicy.h, last line of OrderRefused) for a move,
walk, pull or pullback. `CancelRunWork` is the one place every pause, stop-pending and finish goes through, so the pending list is forgotten there. Test: wiring `test_2_*`.

## 4 (MEDIUM) move-direct map check: REAL for the map mismatch
`BeforeEngineTick` runs from the bot's own update, so `GetMap()` is normally valid there; the queued order can be up to 15 s old though, and the bot may have been teleported to
another map (the run ends on a lead in another INSTANCE, a `.tb tele` is one way) before it ran: the old coordinates were then walked and the liquid line read on the new map. The verb now
refuses with `fail:wrong-map` when there is no map, the bot is being teleported, or `o.map` differs (`o.map` is the lead's map when the executor queued it). The run hears it as a late failure.

## 5 (MEDIUM) rez roster: REAL
`GetMapId()` equals for two copies of a dungeon; `GetDistance` then measures coordinates of different worlds and a body in the other copy could read < `kCorpseMaxYd` and hold the run for a
rez that cannot reach it. `SameMapObject(a, b)` compares `FindMap()` (null is never the same). Used for the body distance, the rezzer's sight (`seesBody`) and `blocksDruid`
(same defect, one line). No `IsInWorld()` condition was added (the review proposed it): a corpse placed in a map has that Map; one without a map reads 1000 yd as before for other maps.

## 6 (MEDIUM) CheckLeadMove cost: REAL (cost), verdict unchanged
`RemoteCommander::CheckMove` tests `distanceFromLead > maxDistance` first and returns TooFar whatever the path facts are; `CheckLeadMove` still ran the PathFinder and, with a path of
fewer than two points, a straight-line liquid sample every 1.5 yd to the point (a point at the map's edge: ~13,000 terrain reads in one world tick). Now it returns at once when
`CheckMove == TooFar` (the early return is only taken for what the judge refuses anyway; `pathRead` stays false: only an accepted order's path type is noted, TbRun.cpp). `test_run_orders`
checks the verdict and the text do not depend on any path fact when too far.

## 7a (LOW) BindBotMaster stale pointer: REAL
`ReleaseMember` gives a bot back to its previous master through `BindBotMaster`; an offline master left `ai->SetMaster` at the run leader. It now always replaces the pointer
(null for an offline master, as `ClearBotMaster`). The other callers bind to an online master, unchanged.

## 7b (LOW) time-limit "underflow": NOT REAL, no change
`nowMs` and `startedMs` are both `GameTime::GetGameTimeMS()` (TbRun.cpp:746, TbRunCommands.cpp:115), monotonic and read in the same update, so `nowMs < startedMs` does not happen. The one
case where it would is the 32-bit wrap (49.7 days), and there unsigned subtraction is exactly right; the proposed `nowMs >= startedMs ? ... : 0` would then read 0 for up to 49 days and
disable the limit. (The guards at TbRun.cpp:119 and :161 clamp the same way and are only cosmetic at the wrap.)

## For the dashboard owner (TbDashboard.cpp: NOT edited; finding 3, verified, suggested patch)
Verification against the code and the core (`~/ForeverCore`, read only):
* `GetMap()` asserts: `Object.h:411` `Map* GetMap() const { ASSERT(m_currMap); return m_currMap; }`. `FindMap()` (`Object.h:412`) returns the pointer without it. Real, but only reachable
  while `m_currMap` is null, which for a Player is between a far teleport's `ResetMap()` and `AddToMap`; the review's claim "even while `IsInWorld()` is true" does not hold for the far
  teleport (the player leaves the world first). Sites that check `IsInWorld()` first (:190, :694 `SendCycle` list, :750) are therefore safe in practice; the unguarded ones are `FightOf` :577
  (a hook's `Unit const*`: `GetMap()` is called before any null test, so the `!unit->GetMap()` test cannot work) and the raw `GetMap()` at :461/:481/:664 for a bot in a list built earlier in
  the same call (fine while the list is built in the same tick). `FightOf` is the one worth fixing; the others are cheap hardening.
* `SpellName` null: NOT real. `SpellInfo::SpellName` is set from a `SpellNameEntry` in both constructors (`SpellInfo.cpp:1498`, `:1683`), so the pointer is never null for a built SpellInfo.
  `LocalizedString::operator[]` returning a null `char const*` for a locale with no text is possible (the file already guards it in `LocName`, :850-853), and `std::string(char const*)` of null is UB:
  :501, :1166, :1197 (map key) and `map->Name[...]` at :1085 do not use that guard. Worth routing through `LocName`.

Suggested patch (not applied):
```diff
--- a/forever/src/TbDashboard.cpp
+++ b/forever/src/TbDashboard.cpp
@@ std::string LocName(LocalizedString const& str)   (move above its first use, ~line 120)
+std::string SpellNameOf(SpellInfo const* spell)
+{
+    return spell && spell->SpellName ? LocName(*spell->SpellName) : std::string();
+}
@@ SendDungeonMobs (:501)
-                cast = (*spell->GetSpellInfo()->SpellName)[sWorld->GetDefaultDbcLocale()];
+                cast = SpellNameOf(spell->GetSpellInfo());
@@ WriteUiMapsFile (:1085)
-        out << ... << Escape(map->Name[sWorld->GetDefaultDbcLocale()]) << ...
+        out << ... << LocName(map->Name) << ...
@@ OnDamage (:1166)
-                name += std::string(" (") + (*spell->SpellName)[sWorld->GetDefaultDbcLocale()] + ")";
+                name += " (" + SpellNameOf(spell) + ")";
@@ OnSpellCast (:1197)
-        ++acc.spells[(*spell->SpellName)[sWorld->GetDefaultDbcLocale()]];
+        ++acc.spells[SpellNameOf(spell)];
@@ FightOf (:577)
-    if (!unit || !unit->GetMap() || !unit->GetMap()->IsDungeon())
+    Map const* map = unit ? unit->FindMap() : nullptr;
+    if (!map || !map->IsDungeon())
         return nullptr;
@@ :461, :664 (bots), :481 (viewer), :750
-        if (Map* map = bot->GetMap(); map && map->IsDungeon())
+        if (Map* map = bot->FindMap(); map && map->IsDungeon())
```
(`LocName` escapes already, so the `Escape(...)` around the map name goes. `acc.spells` keys were raw names before; they pass through `Escape` later or not: check where they are printed.)

## Gates (build slot 1; source of 45af74ed6, test-only 1deb3e92a after the builds)
| Gate | Result |
|---|---|
| census SLICE5 / SLICE4 / SLICE3 / SLICE2 / SLICE1 (`FOREVER_SLOT=1`, `FOREVER_CENSUS_LIST=...`) | `files 176 / 174 / 170 / 160 / 133  compile clean = files  with errors 0  error places 0  includes of a header ForeverCore does not have 0` |
| option ON `worldserver` (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON`, slot 1) | `CMAKE_RC=0 MAKE_RC=0`; `run::Issue`, `run::StartCheck`, `run::ReadFacts`, `run::Update` strong in the binary, `fail:wrong-map` present, 0 `undefined` |
| option OFF `tortoisebots` (`-DTORTOISEBOTS_SLICE1=OFF`) | `CMAKE_RC=0 MAKE_RC=0`; `libtortoisebots.a` 18 objects, `TbRunStands.cpp.o` / `TbRunOrders.cpp.o` empty objects, no `wrong-map` string |
| `slice_gen_context.py --check`, `slice_registry_check.py` | up to date; `(a)=0  (b)=215` |
| run tests (VM, g++ -std=c++17 -O1 -Wall -Wextra) | `test_run_orders` 159 checks PASS, `test_run_c6` 28 PASS, `test_run_cancel` PASS, `test_run_liquid` PASS, `test_run_facts` PASS (`facts_rfc_rows.py ragefire_chasm` + `RUN-S6-F-FIELDS.tsv`) |
| S5 policy tests (VM) | `test_dungeon_run_policy` ok, `test_system_run_policy` 109 checks, `test_forever_plan_match` 38 checks, `test_dungeon_plan_reload_policy` all passed, `test_rez_hold_policy` ok |
| wiring pins (VM) | `test_run_revfix1_wiring.py` 8 OK, `test_run_review_wiring.py` 9 OK, `test_reload_plans_wiring`, `test_plan_step_label_wiring`, `test_aura_tactics_wiring`, `test_run_leader_wiring`, `test_forever_no_direct_queries`, `test_dungeon_plan_sql` OK |
| Turtle `vm-build` / `vm-test tb` | not needed: every changed file is under `forever/` (`git diff --stat tbf/main..HEAD`) |

## Review fix round 2 (two reviews of this branch)
| Review finding | Verdict | Where / reason |
|---|---|---|
| Codex (medium) `move-direct` checks the map id but not the dungeon instance | FIX | `TbOrdersApi.h` / `TbOrders.cpp` `OrderSpec`+`Order` carry `instance` (0 = not recorded); `TbRunOrders.cpp` `DoMove` queues `lead->GetInstanceId()`; `TbOrders.cpp` `move-direct` refuses with `fail:wrong-map` unless `PointOnBotMap(o.map, o.instance, bot->GetMapId(), bot->GetInstanceId())` (`TbOrderQueuePure.h`). Tests: `test_run_cancel.cpp` (equal map id, different instance refuses; 0 = map id only) and `test_run_revfix2_wiring.py` (3 cases, all fail on the previous commit). |
| Gemini 1 pause cancels queued orders | REJECT (no defect) | The review itself confirms `ForgetRunOrders` in `CancelRunWork` covers pause, stop-pending and finish; no change. |
| Gemini 2 stand-path liquid rule | REJECT (no defect) | It confirms `PathPrefixRefused` matches the executor's move rule; no change. |
| Gemini 3 `move-direct` / `.tb where` map reads | REJECT (no defect; superseded) | It confirms `FindMap`/teleport guards; round 2 replaces the map-id comparison with the map+instance rule (Codex). |
| Gemini 4 rez roster map compare | REJECT (no defect) | `SameMapObject` is used for `corpseYd`, `blocksDruid` and `seesBody`; no change. |
| Gemini 5 too-far before path work | REJECT (no defect) | Early `TooFar` return is present; verdict and text unchanged. |
| Gemini 6 `BindBotMaster` | REJECT (no defect) | The pointer is set unconditionally (null for an offline master); no change. |
| Gemini 7 time-limit subtraction | REJECT (no defect) | Both stamps are `GameTime::GetGameTimeMS()`; unsigned subtraction is correct at the wrap; no change. |

## Gates (round 2, slot 1, commit 4f9aa7537)
| Gate | Result |
|---|---|
| census SLICE5 / SLICE4 / SLICE3 / SLICE2 / SLICE1 (`FOREVER_SLOT=1`) | `files 176 / 174 / 170 / 160 / 133  compile clean = files  with errors 0  error places 0  includes of a header ForeverCore does not have 0` |
| option ON `worldserver` (`-DTORTOISEBOTS_SLICE1=ON`) | `CMAKE_RC=0 MAKE_RC=0`; 4 `run::` symbols strong, 0 `undefined` |
| option OFF `tortoisebots` (`-DTORTOISEBOTS_SLICE1=OFF`) | `CMAKE_RC=0 MAKE_RC=0`; `libtortoisebots.a` 18 objects (run files empty) |
| `slice_gen_context.py --check`, `slice_registry_check.py`, `verify_okf.py` | up to date; `(a)=0  (b)=215`; OKF PASSED |
| run tests (VM, g++ -std=c++17 -O1 -Wall -Wextra) | `test_run_orders` 159 checks, `test_run_c6` 28, `test_run_cancel` PASS (incl. equal-map/different-instance), `test_run_liquid` PASS, `test_run_facts` PASS |
| S5 policy tests (VM) | `test_dungeon_run_policy` ok, `test_system_run_policy` 109, `test_forever_plan_match` 38, `test_dungeon_plan_reload_policy` all passed, `test_rez_hold_policy` ok |
| wiring pins | `test_run_revfix2_wiring.py` 3 OK, `test_run_revfix1_wiring.py` 8 OK, `test_run_review_wiring.py` 9 OK, `test_reload_plans_wiring`, `test_plan_step_label_wiring`, `test_aura_tactics_wiring`, `test_run_leader_wiring`, `test_forever_no_direct_queries`, `test_dungeon_plan_sql` OK |
| Turtle `vm-build` / `vm-test tb` | not needed: every changed file is under `forever/` |
