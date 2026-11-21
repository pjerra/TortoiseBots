# B. Commander P6: task orders (objects, loot windows, gossip)

**Start from `main` after Commander P2 merges (last fix round now), and after A (P3)**: the task verbs are `.tb cmd` verbs
on A's party path.

## Done looks like

A commander tells a bound bot to loot a corpse, open a chest, use an object or pick a gossip option, and the bot walks
there, does it through the core's own handlers, and the answer says what really happened.

**Live acceptance** (Commander plan, section 8): in Ragefire Chasm with a bound party: loot a corpse, open a chest, use the
door object, gossip an NPC -> ACK and the change seen in `.tb state`; `ritual` -> `unsupported`. Add: loot a corpse that was
already looted -> ERR, not ACK.

## What it is

From the Commander plan: "loot, open, use, gossip, state, lootmethod: task tick and walks (T :4135-6498, :17764-18007);
`.tb cmd loot|open|use|gossip|state|lootmethod`", file `forever/src/host/cmd/TbCmdTasks.cpp`, size 5 days.

**Reuse slice 9's run tasks** (`forever/src/host/run/TbRunTasks.{h,cpp}`, `TbRunTasksPure.h`, test `forever/tools/test_run_tasks.cpp`):
they already carry the run's `open`, `loot` and `lootmethod` on public core API (TB-COMMANDS.md section 12):
- `open entry:<id>@x,y,z`: the nearest spawned object of that entry **within 5 yd** of the point (none = `invalid-target`);
  walk to a stand 1.5 yd short; door/lever by the typed `GameObjUse` packet into `HandleGameObjectUseOpcode`, chest by the
  engine's `open loot`. Lock judged first (`no-item`, `locked`). **A door not at rest is never used again** (`Use` toggles
  it). Success = state change within 4 s (chest: window closed within 8 s).
- `loot <handle|entry:..> [item]`: the engine's loot window with the ledger; `nothing this bot may loot`, `Nothing is left to loot`.
- `lootmethod ffa|group|rr [threshold 2-5]` via `HandleSetLootMethodOpcode`; `master` and `nbg` refused `unsupported`
  (bots vote greed only); threshold below uncommon refused.
- One task per bot (`busy`), refused `in-combat`.

P6 adds the commander addressing, `gossip`, `state`, and `use` of objects. Legacy pure helpers: `runtime/RemoteCommanderTasks.h`
(1,119 lines), `runtime/GossipOrderPolicy.h` (354); behaviour source `commands/Commander.cpp` 4135-6498, 17764-18007.

## ForeverCore APIs (Commander plan, section 6)

`GameObject.h:314 Use`; `Player.h:2700 SendLoot`; `Player.h:1744 PrepareGossipMenu`; `:1746 OnGossipSelect`;
`:1300 GetNPCIfCanInteractWith`; `Group.h:266 SetLootMethod`. Gossip: prepare the menu for the bot on the NPC, then select
the option by id (the trainer flow in `forever/data/waterys/core-howto/trainers.md` shows the same two calls).

## UNPORTED rows it removes or shrinks (decide per row, write it in UNPORTED.md)

- `incoming: gossip, game object and area trigger` (gossip hello, game object use rows).
- `game object use` (existing row): O9's note says the ENGINE-driven uses (lightwells, party portals, ritual objects) still
  count "a bot's own strategy never uses a game object, only the run's order does". P6 makes commander orders real; the
  engine row stays unless you port that path too.
- `item use except food and drink`, only if `use` reaches items. O9 left `use <item>` refused; keep it refused unless you
  port `Player::CastItemUseSpell` per item (the row's "what porting needs").

## Decisions already made

- D5 (Commander plan): ritual and scout answer `unsupported`; the addon view, trace, LFG/hire links are cut.
- D1/D2: read the binding from h3's table, guids only, live group through the lead.
- Loot: group-loot greed only; personal loot reads as `none` (O9).

## Rules that bite here

- The task keeps guids and a state, never a `GameObject*`/`Creature*`; re-find by guid (or entry + point within 5 yd) each tick.
- The answer to the order is "accepted"; the RESULT is reported later (`done: ...` / `failed: <code>: <message>`), and only
  what the core showed (state change, window closed, item in bag). Never ACK a loot that found nothing.
- Gossip and object handlers are world-thread calls; run them from the engine tick or the command, never another thread.
- `FindMap()` on a bot that may be teleporting.

## Tests (failing case first)

- **Failing case:** loot on an already-looted corpse answers ACK; must be ERR (Commander plan section 7).
- Task state machine walk -> act -> done / timeout, pure, from `RemoteCommanderTasks.h` (extend `test_run_tasks.cpp` or a new
  `forever/tools/cmd_tasks_check.cpp`).
- Gossip: an option id that is not in the prepared menu is refused, not selected.
- Unknown U4 of the Commander plan first: are the legacy actions behind loot/use/gossip in the slice registry?
  `forever/tools/slice_registry_check.py` query for the action keys; an unregistered key answers `action-not-registered`.
- Wiring: the commander path calls the same task functions as the run (one implementation).

## Gates

Census 0 on the slice list (plus any legacy `.cpp` you add), ON and OFF `worldserver`, checks above, registry/context
checks, the live acceptance.
