# Tactical orders, round M1: stop, flee, guard, stance, engage, rebuff

Issue TortoiseBots/TortoiseBotsForever#131, round M1 of `TACTICAL-ORDERS-SPEC.md`. The orders the commander (`.tb cmd`) or a GM
(`.tb order`) gives mid-fight. Both surfaces queue through h3 and the bot's next engine tick runs the verb through the legacy action or
strategy that is already compiled; the answer is in `.tb state` `ord=`. No core change, no legacy source edit.

## What runs

| Verb | Engine calls |
|---|---|
| `stop` | `Player::CombatStopWithPets(true)`, `PlayerbotAI::Reset(false)`, `MotionMaster::Clear()`, the `explicit attack target` value cleared. No `passive` (a bot hit again defends itself) |
| `flee` | `-wait for attack` in every engine, combat stop, reset, `MotionMaster::Clear()`, `flee chat shortcut` |
| `guard` | `guard chat shortcut` (strategy `guard`, added to the slice); the bot guards the spot it stands on, `.tb state` `mv=guard` |
| `stance` | the `stance` action with the canonical word (`turn` → `turnback`); read back through the `stance` value (name must match) |
| `engage` | `PlayerbotAI::StartEngageWindow(requester raw guid, seconds)`; default `ai::kEngageDefaultDelaySeconds` (4), 0-60 |
| `rebuff` | `manual int` "force rebuff begin ms" = `WorldTimer::getMSTime()`, `manual bool` "force rebuff buff pending"/"buff proposed" = false |

D4: every one of them but `engage` ends a live engage hold first (`HasEngagePullWindow()` → `EndPullWindow("order")`). D11: `rebuff` has
no `force rebuff` strategy gate (the strategy is not compiled); the window values are the gate, and a bot in combat refuses it.

## Files

* `forever/src/host/TbTacticsPure.h`: `IsTacticalVerb`, `ParseEngageSeconds`, `NormaliseStance` (M1 keys only; later rounds append).
* `forever/src/host/TbTactics.{h,cpp}`: `Enabled`, `Preflight`, `Run`, `AppendState`, `SelfTestRecords`, with the `#else` engine-not-built
  twin (TbCc.cpp pattern).
* `forever/src/host/cmd/TbCmdTactics.{h,cpp}`: `.tb cmd` verb resolution, arguments, `cmd` reply, `NoteRunOrder`.
* `forever/src/host/TbOrders.cpp`: verbs in `IsVerb`, dispatches in `Run`/`PreflightOrder`, the per-bot pre-flight in `HandleOrder`,
  `ApplyKit` keeps `guard`, `needAction[3]` (the `ah` verb wrote index 2 of a size-2 array: undefined behaviour on every `.tb order ... ah`).
* `forever/src/host/TbPartyState.cpp`: `mv=guard`, `tactics::AppendState` after the h8 keys (`stance=`, `rebuff=`).
* `forever/src/host/cmd/TbCmdOrders.cpp`: `TryHandleTacticalVerb` after the task verbs and before `run::Issue`.
* `forever/tools/slice_gen_context.py` SLICE_KEYS (strategy `guard`; actions `guard chat shortcut`, `guard`, `stance`), regenerated
  `forever/src/slice/SliceAiObjectContext.cpp` (26 strategies, 111 actions).
* `forever/SLICE-W4.txt`: `ai/playerbot/strategy/generic/GuardStrategy.cpp` (shared legacy file, compiled as it is).
* `forever/conf/tortoisebots.conf.dist`: `TortoiseBots.Orders.Tactical` (default 1, kill switch, read on each command).
* `forever/docs/TB-COMMANDS.md`: section 4 rows, `mv`/`stance`/`rebuff` state rows, section 7 key, section 14 pointer, section 23.
* Tests: `forever/tools/test_tactics_pure.cpp`, `forever/tools/test_tactics_wiring.py`.

## Replies and refusals

`.tb order` keeps the `order` records; a refused bot carries `code=` and `msg=`. `.tb cmd` answers `cmd verb=.. status=accepted bots=..
result=party%20<n>|bot:<name>%201 [stance=] [seconds=] [refused=]`, a row per refused bot, and `cmd-end`. When every bot refuses, the
answer is the one-line `error`. Codes: `not-alive`, `in-combat`, `action-not-registered` (with `name=`), `strategy-not-registered:guard`,
`value-not-registered`, `bad-argument`, `missing-argument`, `unexpected-argument`, `no-bot`, `disabled`.

Rebuffed at command time, on purpose: the argument parsers run once per command (not per bot), so a party order cannot half-queue on a
typo.

## Tests

Failing first, on the tree before the fix: `test_tactics_wiring.py` failed 10 of 10 (every verb, both dispatch points, the engage rule,
the rebuff anchor, the slice keys, the `ah` bound, the `ApplyKit` guard); `test_tactics_pure.cpp` did not compile (no header). After the
fix: pure test 29 checks pass; wiring 10 tests pass; `TACTICS_MUTATE=1` catches all 10 mutations (dropped verb, skipped dispatches,
dropped stop body, `run-end` instead of `order`, renamed rebuff anchor, dropped slice key, dropped W4 file, `needAction[2]`, dropped
`ApplyKit` guard). `slice_gen_context.py --check` and `slice_registry_check.py` pass.

## Not in M1

Interrupt, the pull seconds/`ranged`/`body` options, the toggles (`aoe`, `autocc`, `sap`, `loot`), the whole-party `cc`, the ready
check and the run verbs are the later rounds of the same ticket. The class kits (#123) are Sagiroth's and are untouched.
