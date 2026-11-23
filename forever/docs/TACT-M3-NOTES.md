# Tactical orders, round M3: the toggles and the corpse run

Issue TortoiseBots/TortoiseBotsForever#131, round M3 of `TACTICAL-ORDERS-SPEC.md`, stacked on M2. Two pieces: the `aoe`,
`autocc`, `sap` and `loot` toggles (D7) and the `corpserun` order. No core change, no legacy source edit, no new config key.

## Toggles

`aoe|autocc|sap|loot [on|off|status]` in `.tb order` and `.tb cmd` (`.tb cmd`: `[on|off|status] [bot]`; the two-word `auto cc` is
accepted and normalised to `autocc`). A bare verb flips each bot's own state; `status` reads without queuing.

The choice is a host setting per bot guid (memory, server uptime; no DB, `PlayerbotDbStore` is a stub): `host/TbTactics.cpp` keeps a
`std::map<ObjectGuid, ToggleSettings>` and `h3::BeforeEngineTick` calls `tactics::BeforeTick` every tick, so a `co`/`nc` change of
the same strategy does not win and a `ResetStrategies` is covered. `free` does not clear a toggle. Defaults when never set: aoe on,
autocc off, sap on, loot = the kit.

| Toggle | Strategy | Present means |
|---|---|---|
| `aoe` | `no aoe` (combat) | off |
| `autocc` | `auto cc` (combat) | on |
| `sap` | `no sap` (non-combat) | off (rogues only; a non-rogue is refused `no-rogue`) |
| `loot` | `loot` (non-combat) | on (only once the table has an entry; the kit default stands otherwise) |

The queued Run writes the table entry and changes the strategy at once; `BeforeTick` re-enforces it. `.tb order` answers
`status=queued` (or `status=read state=<on|off>` for a `status`). `.tb cmd` answers `state=<on|off|mixed>`: the wanted state for a
change, the bots' current state aggregated for a `status` read. `AggregateState` (`on`/`off`/`mixed`) is the same for both.

A `status` read goes through `tactics::ToggleStatusFault` first (Codex review): `sap` on a non-rogue is refused `no-rogue` and a bot
with no running engine `no-engine`; such a bot is counted in `refused=` and left out of the `.tb cmd` on/off/mixed aggregate (all
bots refused: the command is refused with that code).

## Corpse run

`corpserun` (also `corpse run`) in `.tb order`, and `corpserun [bot]` in `.tb cmd` (no bot: every dead bound bot). An alive bot is
refused `not-dead`. A body runs the existing `ReleaseSpiritAction` with its gates (`no-entrance` from
`BotMgr::Instance().ReleaseHasEntrance(bot)`), then the W1 dead engine walks; a ghost answers `status=queued` with Run `ok` (the walk
is already the engine's). `.tb state` `corpserun=` shows the leg (already written by `TbPartyState.cpp`).

## Files

* `forever/src/host/TbTacticsPure.h`: `ToggleOp`, `ParseToggle`, `ToggleWanted`, `AggregateState`, `ToggleStrategy`,
  `NormaliseTacticalVerb`; `IsTacticalVerb` gains `aoe`/`autocc`/`sap`/`loot`/`corpserun`.
* `forever/src/host/TbTactics.{h,cpp}`: `ToggleSettings` table, `ReadToggle`, `BeforeTick`, the `Preflight`/`Run` toggle and
  corpserun branches, engine-not-built twin, selftest rows.
* `forever/src/host/cmd/TbCmdTactics.cpp`: the `.tb cmd` toggle (change and `status` read) and `corpserun` branches.
* `forever/src/host/TbOrders.cpp`: `IsVerb`, the two-word verb normalisation, the `.tb order` toggle `status` read, the
  `corpserun` `unexpected-argument` rule, the missing-verb list, `BeforeEngineTick` calling `tactics::BeforeTick`.
* `forever/src/host/cmd/TbCmdOrders.cpp`: the two-word normalisation before `TryHandleTacticalVerb`, the missing-verb list.
* `forever/tools/slice_gen_context.py` `SLICE_KEYS`: strategy `no aoe`, `auto cc`; the regenerated
  `forever/src/slice/SliceAiObjectContext.cpp`.
* `forever/SLICE-W4.txt`: `ai/playerbot/strategy/generic/NoAoeStrategy.cpp`.
* `forever/docs/TB-COMMANDS.md`: section 4 rows, section 23 title/M3 subsection/table/state wording, `.tb cmd` table.
* Tests: `forever/tools/test_tactics_pure.cpp`, `forever/tools/test_tactics_wiring.py`.

## Tests

Failing first, before the code: `test_tactics_pure.cpp` did not compile (`ParseToggle`/`ToggleOp` missing) and the wiring test failed
8 checks (`SLICE_KEYS` lacked `no aoe`, `SLICE-W4.txt` lacked `NoAoeStrategy.cpp`, `BeforeEngineTick` did not call
`tactics::BeforeTick`, no `sap`/`corpserun` branches in `Run`, no `not-dead` in `Preflight`, no body release path). After: pure test
58 checks pass; wiring 30 tests pass; `TACTICS_MUTATE=1` catches all 22 mutations, the M3 ones being an inverted aoe sense (pure and
wiring), a dropped `BeforeTick` enforce, a sap applied to a warrior, a dropped `BeforeTick` call, a dropped `NoAoeStrategy.cpp` and a
dropped `no aoe` SLICE key.
