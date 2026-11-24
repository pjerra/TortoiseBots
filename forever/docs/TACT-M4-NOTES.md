# Tactical orders, round M4: `cmd cc` for the whole party

Issue TortoiseBots/TortoiseBotsForever#131, round M4 of `TACTICAL-ORDERS-SPEC.md` (D12, D13), stacked on M3. No core change, no
legacy source edit, no new config key, no kit change (the crowd-control kits are #123's: this round calls their strategy and actions).

## What it does

`.tb cmd cc <mark> <guid>` with no bot named picks the bot and the family from the run's own crowd-control table, then runs the same
`h8::ApplyCcOrder` as `.tb cc` and the run's `cc` verb. `.tb cmd cc clear` with no bot clears every bound bot. Both work without a
dungeon run: before this round `IssueOrder` answered `unsupported` for `cc` when no run stood behind the order.

Grammar (D13, `TbRunOrdersPure.h` `ParseOrder`):

| Text | Parsed |
|---|---|
| `cc moon <guid>` | pick, any family |
| `cc moon <guid> polymorph` | pick, family `polymorph` |
| `cc moon <guid> Mmage` | the bot `Mmage` (not a family word) |
| `cc moon <guid> Mmage polymorph` | unchanged (the run's form) |
| `cc moon <guid> none` | the bot named `none`; a bot named like a family uses `cc moon <guid> <bot> none` |
| `cc clear` | clear every bound bot; the answer is `party <n>` and, when a bot could not be cleared, `failed=<names>` (Codex review) |
| `cc clear <bot> [<mark> <guid>]` | unchanged |

The unit word of both verbs is read like pull and attack: a run handle or the 32-hex guid text (`targetToken`), so a commander can type
what `.tb state` prints. Before this round only a numeric handle parsed.

## The pick (`TbTacticsPure.h` `PickCcBot`, `TbTactics.cpp` `PickCcPlayer`)

Over every (bot, family row) pair of `DungeonRun::kRunCc` the best pair wins: tier A before S before R, then the table order, then the
party order. A pair qualifies when the row is an `autoPick` row, `CcFits(family, creatureType)` holds (an unread type fits polymorph
only), the bot carries the `cc` strategy in its combat kit and knows a rank of the spell (the run's spell tables, `Player::HasSpell`),
and `ai::CcPermissions` of the owner (`h8::CcDeniedForOwner`) does not deny the family's kind. A `prePull` row (Sap) needs the target
and the bot out of combat. No pick: `no-cc-bot` with `name:reason;...` (`dead`, `no-cc-kit`, `no-fitting-spell`, `denied`,
`in-combat`). Answer on success: `result=bot:<name> 1 family=<token>`.

## Run bookkeeping

`DoCc` and `DoCcClear` take the run pointer: `DropPendingCc` and `CcOwnedRecord`/`CcOwnedForget` run only with a run behind the order.
`test_g8_wiring.py` pins follow (`rec->ccOwned`).

## Files

* `forever/src/host/TbTacticsPure.h`: `CcCandidate`, `CcPick`, `CcFamilyFromToken`, `PickCcBot` (it now includes the policy header).
* `forever/src/host/TbTactics.{h,cpp}`: `PickCcPlayer` (live facts: alive, combat, `cc` strategy, known families, deny mask) and its
  engine-not-built twin.
* `forever/src/host/run/TbRunOrdersPure.h`: `ParseOrder` cc forms, the usage text.
* `forever/src/host/run/TbRunOrders.cpp`: `DoCc`, `DoCcClear`, the two `IssueOrder` cases.
* `forever/src/host/cmd/TbCmdOrders.cpp`: the missing-verb list names `cc`.
* `forever/docs/TB-COMMANDS.md`: section 23 "Whole-party cc (M4)".
* Tests: `forever/tools/test_run_orders.cpp`, `test_tactics_pure.cpp`, `test_tactics_wiring.py`, `test_g8_wiring.py` (pins).

## Tests

Failing first: `test_run_orders.cpp` (`cc moon 77` was `invalid`, `cc clear` was `invalid`, a 32-hex guid did not parse) and
`test_tactics_pure.cpp` (no `PickCcBot`). Mutations (`TACTICS_MUTATE=1`): the prePull rule dropped, the deny filter dropped, `none`
treated as a family (pure and parse), cc refused again without a run, the run bookkeeping unguarded, the pick call dropped, `cc clear`
limited to one bot, the host deny read dropped.

## Live fix (#208): the order starts its cast

The first live row (`notes/milestone-runb-recheck-2026-11-23.md` row 2) showed three accepted `cmd cc moon <guid>` orders and no
Polymorph: `result=bot:Pmag 1 family=polymorph` and `rticc=moon ccfam=polymorph` took, and the mage kept following while the
marked trogg died with its pack. Two gaps, both on the order-to-cast path:

* `h8::ApplyCcOrder` only wrote the values and the icon; the cast was left to the mage's fight-start trigger. TB's `.bot action cc`
  starts the cast at the assignment itself (`RunCcOrder` in `commands/Action.cpp`: selection, current target, facing, a direct run of
  the class action, and the mature action queued behind its own `reach cc` when the target is out of reach). Without it an order
  given outside combat asked no bot to do anything.
* `CcTargetValue::Calculate` only ever offered units from the engaged-attacker tournament, so an order given before its pack opened
  had no `cc target` at all: the trigger never fired and the queued reach chain had nothing to cast. The chooser now takes the bot's
  own `rti cc target` first, at any range (`CcChooserTakesAssignedMark` in `CcMarkHoldPolicy.h`), through the same assigned-mark
  rules (`CheckAttacker`: only-marks-left, party DoTs, castability); `reach cc` closes the distance and `CcCastRangeMargin`/LOS gate
  the cast.

`StartCcCast` (`TbCc.cpp`) now runs after the order's read-back: it drops the cached mark picks (`rti cc target`, `cc target::<spell>`,
`possible attack targets`), selects and faces the unit, finds the crowd-control action for the picked family through the action graph
(`IsCrowdControlAction`/`GetCrowdControlSpellName`, as TB's `FindCcAction`), runs it once (`DoSpecificAction`), and when that refuses
queues it (`Engine::QueueAction`, `ACTION_PASSTROUGH`) so its `reach cc` prerequisite moves the bot in and the cast follows. A
hand-written order with no family (`none`) starts no probe; the fight-start trigger remains the fallback. The family-to-spell table is
pure (`TbCcPure.h` `CcFamilySpellName`).

Tests: `tools/test_cc_assigned_mark_first_wiring.py` (the chooser pick order and the probe's shape; two checks fail on the old code),
`tools/test_cc_mark_hold_policy.cpp` (the chooser rule) and `forever/tools/test_cc_order.cpp` (the family table).

## Live fix 2 (#208): the sheep holds

Recheck 2 (bot logs on the box, `Pmag`): the order and the trigger DID cast. 13 Polymorph casts started on the marked unit
(06:10:10, 06:14:44 from the order's own probe, 06:14:55-06:15:20, 06:19:09-06:19:27), none held. The party fought the mark: the
priest's Shadow Word: Pain on it 0.9 s after the order's sheep (06:14:47), the tank's Taunt, Thunder Clap, Sunder Armor and Heroic
Strike on it (06:19:12-16) while another trogg was still up. Two causes:

* `CombatTimeCompat` (`forever/src/compat/TbCompat_Unit.h`) answered a constant -1 (findings item 14), so `CcMarkHeld`'s
  fight-start window, which leaves a ready cc bot's mark alone for `CcMarkHoldSeconds`, never opened on ForeverCore. The module
  now measures the engage time (`TbCombatClockPure.h` `CombatClock`: the first question while the creature is in combat starts its
  clock, leaving combat drops it). Different answer: a mark first looked at mid-fight gets its window from then on.
* `FindNonCcTargetStrategy::OnlyCcMarksLeft` read a fresh sheep that pulled its mark alone (the order casts at once) as "only cc
  marks left", so every bot's `FindLastCcTarget` took it. A crowd-controlled mark inside its window is an opening hold
  (`CcOpeningHold`, `CcMarkHoldPolicy.h`), not the end of the fight; past the window a lone mark is killed as before.

Tests: `tools/test_cc_hold_window_forever_wiring.py` (fails at 696f4828a: the constant -1, no opening hold),
`tools/test_cc_mark_hold_policy.cpp` (opening hold cases), `forever/tools/test_combat_clock.cpp` (the clock and the window).
