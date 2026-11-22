# Tactical orders, round M2: interrupt and the pull options

Issue TortoiseBots/TortoiseBotsForever#131, round M2 of `TACTICAL-ORDERS-SPEC.md`, stacked on M1. Two pieces: the `interrupt`
order (D6) and the Turtle pull options `[seconds 0-60] [ranged|body]` (D14), including the `body` fix for plan rows. No core change,
no legacy source edit, no config key.

## Interrupt

`interrupt <target>` in `.tb order` (the `attack` target grammar) and `interrupt <guid> [bot]` in `.tb cmd`.

* Command checks: the unit exists, is alive and `IsNonMeleeSpellCast(true)`; refusals `no-target`, `dead-target`, `not-casting`.
* One bot is picked (TB `ResolveInterruptExecutor`): the named bot only, else the first bot in party order with a castable action.
  The probe is TB `FindInterruptAction`: registered action, `ai->HasSpell`, `ai->IsInterruptableSpellCasting(target, action, true)`,
  `ai->CanCastSpell(action, target, 0, nullptr, true, true, true)` (range ignored: the queued action walks). Review fix: a bot that can cast its action from where it stands wins
  over an earlier one that would walk; the walk-and-queue path is used only when no probed bot is in range. The action list is
  `counterspell`, `silence`, `kick`, `pummel`, `shield bash`, `bash`, `hammer of justice`, `earth shock`, `death coil` - without
  `spell lock` (pet context) and `repentance` (no Forever kit).
* No bot qualifies: `no-interrupt`, `msg` = `name:reason` per probed bot (`dead`, `no-engine`, `other-map`, `far`, `no-spell`).
* Only the picked bot is queued (`unit` = target, `arg` = action). On its tick: still casting (else `fail:not-casting`),
  `SetSelection(target)`, the `current target` value, then `DoSpecificAction(action, Event("tb order", "", owner), true)`. A refusal
  while the target still casts queues the same action on the current engine at `ACTION_PASSTROUGH` (after `OnCombatStarted()` when
  the bot is in combat); a refusal there is `fail:interrupt`.
* `.tb cmd` answers `cmd verb=interrupt status=accepted bots=1 result=bot:<name> 1 action=<action>`; `.tb order` answers the usual
  `order` lines, one for the picked bot.

## Pull options

`pull|pullback <guid> [seconds 0-60] [ranged|body]` (`.tb cmd`, the judge) and `pull <target> [seconds] [ranged|body]` (`.tb order`).
`ai::ParsePullOrderOption` owns the words: any order, each once, `ranged` and `body` exclude each other, a sign or another word is
`invalid` with the usage. The canonical text (TB `PullOptionText`) travels as `Parsed::pullOption` into `batch.Back().arg`.

* The seconds replace the party hold's join delay (`kPullJoinDelaySeconds`, 3, for both verbs without seconds) and, for `pullback`,
  the strategy's DPS join delay (`AiPlayerbot.PullBackDpsJoinDelay` without seconds). Review fix: a default `pullback` keeps its 3 s hold.
* `ranged`/`body` record the per-command mode with `PullStrategy::BeginCommand(pullback, hadPullBack, delay, ranged, body)` before
  `pull my target`, and the `SaveCommand`/`RestoreCommand` rollback runs for `pull` as well as `pullback` when the request refuses.
* `body` is no longer parsed and dropped: plan rows that ask for it walk in. `pull` with only seconds does not touch the strategy
  mode (the hold carries the delay).

## Files

* `forever/src/host/TbTacticsPure.h`: `IsTacticalVerb` gains `interrupt`; `kInterruptActions`, `InterruptProbe`, `InterruptPick`,
  `PickInterrupt` (named-bot rule, party order, action order).
* `forever/src/host/TbTactics.{h,cpp}`: `Preflight`/`Run` interrupt branches, `PickInterruptBot`, engine-not-built twin, selftest rows.
* `forever/src/host/cmd/TbCmdTactics.cpp`: the `.tb cmd interrupt` branch (target, candidates, pick, queue, reply).
* `forever/src/host/TbOrders.cpp`: `IsVerb`, the `.tb order` interrupt branch (target + pick before the per-bot loop; one line), the
  `pull` target/option split, h3 `pull`/`pullback` parse `o.arg` and pass the delay and flags to `BeginCommand`/the hold;
  usage lists gain `interrupt`.
* `forever/src/host/run/TbRunOrdersPure.h`: `Parsed::pullOption`, `PullOptionText`, the pull grammar and usage text.
* `forever/src/host/run/TbRunOrders.cpp`: `DoPull` sets `batch.Back().arg = p.pullOption`.
* `forever/docs/TB-COMMANDS.md`: section 4 rows, section 14 verbs, section 23 interrupt/pull option sections, kill-switch wording.
* Tests: `forever/tools/test_tactics_pure.cpp`, `forever/tools/test_run_orders.cpp`, `forever/tools/test_tactics_wiring.py`.

## Tests

Failing first, before the fix: `test_tactics_pure.cpp` did not compile (`InterruptProbe`/`PickInterrupt` missing);
`test_run_orders.cpp` did not compile (`Parsed::pullOption` missing) and its old cases asserted `pull <guid> ranged` and
`pullback <guid> 5` were `unsupported`. After: pure test 37 checks pass; run orders 287 checks pass; wiring 18 tests pass;
`TACTICS_MUTATE=1` catches all 15 mutations, the three M2 ones being a dropped `DoPull` arg pass, a reversed interrupt list and an
ignored named bot.

## Not in M2

Toggles (`aoe`, `autocc`, `sap`, `loot`), corpse run, whole-party `cc`, ready check, run verbs and `cmd enter` are the later rounds.
The class kits (#123) are Sagiroth's and are untouched.
