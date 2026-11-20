# ForeverCore port: five items for Sagiroth (context pack)

Repository `TortoiseBots/TortoiseBotsForever`, base branch `main`. The module lives under `forever/` and builds inside a
ForeverCore checkout (Karrz1/TrinityCore, branch `forever`) at the commit in `forever/PINNED_CORE` (6517af05). **Zero core
changes**: if an item seems to need one, write it down in the PR and stop that part.

| File | Item | Size (worker-days, from the plans) |
|---|---|---|
| `A-commander-p3-orders.md` | Commander P3: one order path (`.tb cmd` move/attack/hold/follow..., pathing and liquid checks, target icons) | 1.5 |
| `B-commander-p6-interact.md` | Commander P6: task orders (objects, loot windows, gossip) | 5 |
| `C-w6a-item-usage.md` | W6a: item usage and equip upgrades | 8.5-11 incl. live and reviews |
| `D-w2-vendor-repair-trainer.md` | W2: sell, buy, repair, train, read mail | 3 (code) |
| `E-w3-quests.md` | W3: accept, objectives, turn-in, drop, share | 5 (code) |
| `F-ci-pr-10.md` | CI PR #10 retarget | small |

## Order and what each waits for

1. **C (W6a) can start now** from `main` (item loot L9 is merged). Its one seam with our W1 work is the "slice 9" block in
   `forever/src/slice/SliceLinkStubs.cpp` and the keys in `forever/tools/slice_gen_context.py`: W1 owns them first, so
   rebase on W1 when it lands, or tell us which lines you need.
2. **A (P3) and B (P6) start from `main` after our Commander P2 merges** (branch `cmd/p2`, in its last fix round now). P2
   edits `forever/src/host/run/TbRunOrders.cpp`, `TbRun.cpp`, `forever/src/host/cmd/TbCmdBinding.*`, `forever/src/TbScripts.cpp`
   and `forever/src/dash/TbDashRuns.cpp`; starting earlier means a conflict in exactly the files P3 lifts. Do A before B:
   B's `.tb cmd` verbs ride on A's party path.
3. **D (W2) and E (W3) wait for W1 "grind and live"**, which we are doing now on branch `world/w1` (masterless `free` bots,
   rest, loot, open-world death and corpse run). D and E can run in parallel after that. **E also wants C** (reward choice
   uses the item value).
4. F is independent.

Shared seams touched by several items (`UnportedFeatures.h` rows, `forever/src/slice/SliceLinkStubs.cpp`,
`slice_gen_context.py` keys, `TbScripts.cpp` command rows): one owner per file per wave. Say in the PR which rows you took.

## How to build and test

```
git clone -b forever https://github.com/Karrz1/TrinityCore.git ForeverCore
git -C ForeverCore checkout $(cat TortoiseBotsForever/forever/PINNED_CORE)
ln -s /path/to/TortoiseBotsForever ForeverCore/src/modules/TortoiseBots
cmake -S ForeverCore -B build -DSERVERS=1 -DSCRIPTS=static -DBUILD_TORTOISEBOTS=1 -DTORTOISEBOTS_SLICE1=ON
cmake --build build --target worldserver        # `ninja tortoisebots` = module-only compile gate (~2 min)
```

(`forever/docs/SPIKE.md` "Build"; the engine path only exists with `-DTORTOISEBOTS_SLICE1=ON`.)

**Gates every item passes before its PR** (quote the result lines in the PR):

| Gate | Pass line |
|---|---|
| Slice census on your item's list (`forever/SLICE<n>.txt` style list naming the legacy `.cpp` you add): `FOREVER_CENSUS_LIST=<list> python3 forever/tools/port_census.py <build dir> <out dir> <jobs>` | `files N compile clean N with errors 0 error places 0` |
| `worldserver` with `-DTORTOISEBOTS_SLICE1=ON` | links |
| `worldserver` with the option OFF (default) | links; your new commands answer `error code=engine-not-built` |
| your pure checks (`forever/tools/*_check.cpp` / `test_*.cpp`, standalone `g++ -std=c++17 -O1 -Wall -Wextra -I. <file>`) | `PASS` / `0 failed` |
| wiring pins (`forever/tools/test_*_wiring.py`) incl. a mutation mode that must FAIL | OK, mutations fail |
| `forever/tools/slice_registry_check.py`, `forever/tools/slice_gen_context.py --check` | `summary: (a)=0`, context up to date |
| Turtle build + Turtle unit tests (the module's own CI) | only when a file OUTSIDE `forever/` changed (C does: `RandomItemMgr.cpp` split) |
| live boot with the engine (console `tb ...`), the item's acceptance check | as written in the item file |

## Rules that bite (all items)

- **Every module DB query goes through the guarded readers**: `WorldQueryCompat` (`forever/src/compat/TbCompat_S5.h:61`) and,
  after P2 merges, `CharacterQueryCompat` (`forever/src/host/cmd/TbCmdBindingStore.cpp`). ForeverCore's connection **aborts
  the server** on a query naming a missing table or column. Guarded = single-table SELECT, schema checked first, a missing
  table reads as empty. `CharacterQueryCompat` accepts only its own store's table: a new characters table needs its own
  guarded entry. No printf-style `PQuery`: it compiles and drops its arguments; use prepared statements or `{}` formats.
- **A fallback, stand-in or unported path makes the bot do LESS, never more** and never reports success it did not see.
  Every stand-in is a row in `ai/playerbot/UnportedFeatures.h` (F helpers in `forever/src/compat/TbCompat_Unported.h`)
  documented in `forever/docs/UNPORTED.md`. Porting a feature deletes its row, enum entry and helpers; a new stand-in adds one.
- **No raw object pointers across ticks.** Keep guids (and order seqs); re-find the `Player`/`Unit`/`GameObject` each tick.
- **`FindMap()`, not `GetMap()`**, on a player that may be teleporting or loading: `GetMap()` asserts on a player with no map
  (see `forever/src/host/run/TbRunOrders.cpp:72`).
- **World thread only.** Commands, the engine tick (`WorldScript::OnBeforeMapsUpdate`) and the THREADUNSAFE handlers you call
  directly all run there.
- **Compat pairs.** Core-shape differences go into `forever/src/compat/TbCompat_<X>.h` plus the Turtle twin
  `ai/cmangos-compat-<x>.h` (Turtle body = the original expression). `#if TORTOISEBOTS_CORE_FOREVER` only in `forever/` and in
  compat headers, never in the bot brain. No behaviour change on the Turtle side.
- **Replies**: grammar v2, one encoder (`Enc`/`Rec`/`Reply` in `forever/src/host/TbH3.cpp`), token last, a bad token refused
  first, reply TRUE (`forever/docs/TB-COMMANDS.md` section 3).
- **Census 0** on the item's list; **ON and OFF builds** both link.
- **Blizzlike only; bots never teleport** except W1's hard-stuck ghost rescue (one sink). Raids are out.
- Direct server calls (e.g. `AddQuestAndCheckCompletion`, `BuyItemFromVendorSlot`) are preferred over building packets; when
  a handler is the only door, send the typed `WorldPackets::...` struct into the `WorldSession::Handle...` function.
- The ForeverCore team's own bot module is GPL-2.0-only: read it for ideas, never copy code or its `playerbot_*` SQL rows.
- The port is private: no mention in any public issue, PR or channel.

## Data in the repo you will want

`forever/data/waterys/` (real-client and DB findings by the team): `service-points/*.csv` (vendors, repairers, class and
profession trainers, mailboxes, innkeepers), `core-howto/trainers.md` (client flow vs core, with lines),
`starting-zone-quests.csv`, `quest-spells.csv`, `supplies-by-level.csv`, `gear-rule.md`, `sql-inventory.csv`.
Command reference: `forever/docs/TB-COMMANDS.md`. Unported list: `forever/docs/UNPORTED.md`. Commander P1:
`forever/docs/CMD-P1-NOTES.md`.

## Decisions settled after this pack was written

1. **CI PR #10:** it is already based on main, so retargeting is not needed. Its build checks out the Turtle core, so the ForeverCore side is never built in CI. Turn it into a ForeverCore job: the module ON and OFF builds plus the census at forever/PINNED_CORE. If that is too heavy for the org's CI limit, close #10 and keep the gates local.
2. **W3 vanilla quest filter:** a quest is in scope when `Expansion = 0`, or its ID is one of Forever's custom re-IDs (91xxx/99xxx), AND it has a spawned giver and a spawned ender. Retail-era leaks (for example the Cataclysm Darkshore chain) and duplicates by title are skipped, the custom re-ID winning over the retail copy.
3. **W3 reward choice before W6a merges:** pick the reward with the highest vendor sell price; switch to the W6a item value once W6a is in main.
4. **W2 walking distance before travel (W4) exists:** a bot only visits an NPC in its current zone whose navmesh path is at most 300 yards; otherwise it skips the visit (no teleport).
5. **Quest relation maps:** at the pinned core they are public (ObjectMgr.h, around lines 1261-1266), so the UNPORTED row "quest internals" is out of date on that point: use them directly.
