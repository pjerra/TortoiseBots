# ForeverCore port: plan

Read `SPIKE.md` first. This file says how the rest of the module gets onto ForeverCore and in what order.

## Rules

1. No change to the core. A need for one is written down and asked, never patched.
2. One repository, two cores. The Turtle build (`TortoiseBots.cmake`) and the ForeverCore build (`CMakeLists.txt`)
   both stay green on this branch. A change that breaks the Turtle build is not finished.
3. The core pin (`forever/PINNED_CORE`) moves on purpose, in its own commit, with the gates re-run. The item mirror
   (`forever/src/compat/TbItemPrototype.*`) is filled once, from `WorldScript::OnWorldInitialized`: the core has no item
   template reload at the pin (`LoadItemTemplates` is called once, `World.cpp:1625`), so a moved pin must re-check that
   the store is still loaded there and still not reloaded.
   The spell mirror (`forever/src/compat/TbSpellEntry.*`) is filled per spell on first use, after
   `tbcompat::InitSpellTable()` builds its sorted `DIFFICULTY_NONE` id index and slot array from the same hook. The hook
   first calls `tbcompat::MarkSpellStoreReady()`; only that sets the readiness flag, and until it is set the id bound
   answers 0 and every spell lookup nullptr without reading the store, so a lookup from an earlier hook cannot publish
   an index built from a half-loaded store. After the flag the id bound's lookup builds the index too, and nothing is
   built while `TortoiseBots.Enable = 0`. The core has no spell reload
   either at the pin:
   `LoadSpellInfoStore` and every pass that edits the `SpellInfo` objects (`LoadSpellInfoCorrections`,
   `LoadClassicSpellInfo`, `LoadSpellInfoCustomAttributes`) are called once from `World::SetInitialWorldSettings`
   (`World.cpp:1487-1517`), and no `.reload` handler calls them again (`scripts/Commands/cs_reload.cpp` reloads
   `spell_required`, `spell_area`, `spell_proc` and the other side tables, never the store). A moved pin must re-check
   both halves of that: the store loaded before the hook, and no runtime path that rebuilds a `SpellInfo`.
   Re-checked at 25fc7ca (the move from 6517af05, 2026-11-19; none of the core files this rule cites changed:
   `Main.cpp`, `World.cpp`, `ObjectMgr.cpp`, `SpellMgr.cpp`, `SpellInfo.*` and `cs_reload.cpp` are not in the diff
   6517af0..25fc7ca): `worldserver/Main.cpp:369` `SetInitialWorldSettings()`, then `:372` `ScriptMgr::OnWorldInitialized()`
   (the hook, `ScriptMgr.cpp:1549`); inside it `World.cpp:1487` `LoadSpellInfoStore`, `:1474` serverside spells, `:1477`
   `LoadSpellInfoCorrections`, `:1480-1485` `LoadClassicSpellInfo` / `TwinChannels` / `SpellEffects` / `SpellValues` /
   `SpellCosts` / `ForeverVanillaAoe` (all edit `SpellInfo`, once), `:1491` `LoadSpellInfoCustomAttributes`, `:1494-1500`
   diminishing, immunities, target caps; `:1608` `LoadItemTemplates` (definition `ObjectMgr.cpp:3649`, the only writer of
   `_itemTemplateStore` besides `LoadClassicItemDurability`, `World.cpp:1631`, which only edits entries). A grep of `src/server` for every one of those loaders finds no caller outside
   `World.cpp`; `cs_reload.cpp` (`:812-884`) reloads only the side tables, and `.reload all item` (`:328`) reloads page
   texts and random bonus lists. `Main.cpp` calls `OnWorldInitialized` once, only after the world settings succeed.
   Re-checked at d02645dd21 (2026-11-20; `World.cpp` is the only file of the set in the diff 25fc7ca..d02645dd21, which
   adds `WorldThreadId` at the top and its store at the start of `SetInitialWorldSettings` and `World::Update`: the
   loader lines move by 14, nothing else changes): `World.cpp:1502` `LoadSpellInfoStore`, `:1488` serverside spells,
   `:1491` `LoadSpellInfoCorrections`, `:1494-1499` `LoadClassicSpellInfo` / `TwinChannels` / `SpellEffects` /
   `SpellValues` / `SpellCosts` / `ForeverVanillaAoe`, `:1505` `LoadSpellInfoCustomAttributes`, `:1508-1514`
   diminishing, immunities, target caps; `:1622` `LoadItemTemplates`, `:1628` `LoadClassicItemDurability`.
   `Main.cpp:369/372`, `ObjectMgr.cpp:3649`, `SpellMgr.cpp` and `cs_reload.cpp` are unchanged. The new
   `ScriptMgr::OnAfterReload` (`ChatCommand.cpp:326`) lets a module react to a successful `.reload`; it calls no loader
   and no `.reload` handler gained one, so neither store is rebuilt at runtime.
   Re-checked at d3358c21dc (0.1.39, 2026-11-21; `World.cpp`, `ObjectMgr.cpp` and `SpellMgr.cpp` are in the diff, `Main.cpp`
   and `cs_reload.cpp` are not): `World.cpp:1487` `LoadSpellInfoStore`, `:1490` serverside spells, `:1493`
   `LoadSpellInfoCorrections`, `:1496` the new `LoadClassicCreatureSpellTwins` (edits `SpellInfo`, once) before
   `LoadClassicSpellInfo` .. `:1502` `ForeverVanillaAoe`, `:1508` `LoadSpellInfoCustomAttributes`, `:1517` target caps;
   `:1625` `LoadItemTemplates`, `:1631` `LoadClassicItemDurability`, `:1634` the new `LoadClassicItemDamage` (fills the
   classic damage rows `ItemTemplate::GetDPS`/`GetDamage` now read first, `ObjectMgr.cpp:3822`), `:1649` the new
   `LoadClassicVersusBonus`. Each new loader has `World.cpp` as its only caller, all run before `OnWorldInitialized`.
   The pin move added nothing that reloads either store. `forever/tools/check_core_cites.py` lists the `File.ext:LINE`
   cites in `forever/` that point into a core file a pin move changed and where the cited line went: run it with
   `--old <old pin> --new <new pin>` on every pin move.
4. Game data is not ported blindly. Spell ids, talents, races, zones, travel nodes and boss data are re-derived from
   ForeverCore's DB2 and world database, feature by feature.

## How the port actually proceeds: vertical slices

A slice is a set of legacy files that must compile, link and pass a live test together on ForeverCore, while the
Turtle build and its unit tests stay green. Slice 1 is the bot engine and the warrior; its files are listed in
`forever/SLICE1.txt` (`SLICE1-R9-NOTES.md`).
The cut is a slice registry in `forever/src/slice`, switched on by one CMake option, `TORTOISEBOTS_SLICE1`
(`SLICE1-R2-NOTES.md`).
Features that live in slice files but are not ported yet go through a closed table: `ai/playerbot/UnportedFeatures.h`,
with the to-do list in `forever/docs/UNPORTED.md` (`SLICE1-R6A-NOTES.md`). On ForeverCore their helpers count the call
and return a failure value; on Turtle each one is the original call.
Link stand-ins for what the slice calls but does not port are in `forever/src/slice/SliceLinkStubs.cpp`. Their rule:
a fallback must make the bot do less, never more (`SLICE1-R9-NOTES.md`, section 1).
Progress is measured by the slice census in `CENSUS.md`.

## How a file gets ported

Three kinds of code, three treatments:

| Kind | Size | Treatment |
|---|---|---|
| Core-free (`*Policy.h`, most of `runtime/policy`) | 564 files, 114.5k lines | Shared as it is. Included by repository path from `forever/src`. |
| Host side (`host/`, hooks, session, packets, commands) | ~5k lines + parts of `runtime/` | Written new for ForeverCore in `forever/src`. Same behaviour, different seams. |
| Bot brain on the MaNGOS API (`ai/`, most of `runtime/`, `commands/`) | ~1,250 files | Made to compile on both cores, in place, a feature at a time. |

For the third kind, the differences between the cores are closed in this order, cheapest first:

1. **Include paths:** `forever/compat/` (generated by `forever/tools/gen_compat_headers.py`). No source change.
2. **Pure renames** (a type, function or macro that means the same in both cores): aliases and wrappers in the compat
   headers. The source line changes once and then reads the same for both cores. The files that exist now:
   - `forever/src/compat/TbCompat_A.h`, `TbCompat_C.h/.cpp`, `TbCompat_P.h`, `TbCompat_V.h` (R4 packages): both-core
     helpers for actions and triggers, the engine core and config, PlayerbotAI, and values. Turtle twins:
     `ai/cmangos-compat-r4a.h`, `-r4c.h`, `-r4p.h`, `-r4v.h`.
   - `TbCompat_Unit.h`, `TbCompat_Position.h`, `TbCompat_Config.h/.cpp`, `TbCompat_Names.h` (R3): unit, map and session
     calls, the position type, the `Config` class, and name-level entries. Turtle twin: `ai/cmangos-compat-r3.h`.
   - `TbCompat_Unported.h/.cpp` (R6A): the "not ported yet" helpers. Turtle twin: `ai/cmangos-compat-unported.h`.
   - `TbItemPrototype.h/.cpp` and `TbSpellEntry.h/.cpp`: the item and spell mirrors, filled from the core's data.
   - `forever-compat-shim.h` (the names, Turtle twin `ai/cmangos-compat-shim.h`) and `botpch_forever.h` (the
     pre-include, the counterpart of `ai/botpch.h`).
3. **Different shape, same idea** (spell data, auras, update fields, grid searches, packets, database calls): a small
   function in the host API (`tbhost::`), implemented once per core. Call sites move to it.
4. **Different design** (LFG, loot, auction house, battlegrounds, taxi and transports, trainers, talents): the host
   side is rewritten for ForeverCore; the decisions stay in the shared policies.

`#if TORTOISEBOTS_CORE_FOREVER` is allowed only inside the compat header and the host API, not in the bot brain.

## Gates

| Gate | Command | Time |
|---|---|---|
| Module compiles on ForeverCore | `forever-build/build.sh 8 tortoisebots` on the build VM | ~2 min |
| Server links | `forever-build/build.sh 12 "worldserver bnetserver"` | ~8 min warm |
| Core-free tests on the ForeverCore toolchain | `forever-build/policy-tests.sh` | ~3 min |
| Turtle build still compiles | `vm-build <worktree> --wait` | as today |
| Live boot | `rsync -aL --delete --exclude .git --exclude 'src/modules/TortoiseBots/.git' ~/ForeverCore/ <build VM>:forever-build/src/`, `forever-build/build.sh 12 worldserver`, `forever-run/setup.sh && world.sh start`, then poll `logs/world.out` (15 s, 3 min) for `ready...`, no `ASSERTION FAILED` and the item mirrors line, and drive `world.sh cmd "tb item <entry>"`; stop with `world.sh stop`. A build gate does not prove the module starts. | ~5 min |
| Live boot with the engine | the same boot, with the worldserver built as `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON` (`SLICE1-R8-NOTES.md`, section 13). Console: `world.sh cmd "tb login <bot> <master>"`, `"tb engine <bot> on"`, `"tb where <bot>"`, `"tb unported"`; then `world.sh stop` | not recorded |
| Slice census | `FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 14` (slice 1: 133 files, 133 compile clean, 0 error places, `SLICE1-R11-NOTES.md`) | not recorded |
| What is left | `forever/tools/port_census.py` | ~1 h |

The console commands are listed in `SPIKE.md`, Status.

A ported file is added to `forever/PORTED.txt`; the root `CMakeLists.txt` compiles the files in that list. The census
number going down and the list going up is the progress measure.

## Order

1. Spike live run: **done** 2026-11-17, one bot pair (login, follow, assist, cast, logout; `SPIKE.md`). Five bots in a
   party: open.
2. Host API for ForeverCore (session, movement, cast, auras, loot, group): **started**. The host side is services under
   `forever/src/host` (`TbEngineBrain`, `TbHostBotManager`, `TbHostOwnership`, `TbHostTracing`); the spike's `BotMgr`
   and session stay in `forever/src`.
3. The engine: **runs for slice 1** (`PlayerbotAI`, `Engine`, `AiObjectContext`, and the values and triggers the warrior
   needs). Slice 1: 133 files in `forever/SLICE1.txt`, census 133 clean, 0 error places (`SLICE1-R11-NOTES.md`).
   Live test (`SLICE1-R8-NOTES.md`, 12 boots, bot master with no game client): a warrior on the engine follows
   at 2.4 to 4.9 yd, assists and melee-fights a Timber Wolf, returns to non-combat, and logs out with its master;
   22 minutes idle with no crash. A crash on a master's map change was fixed in R11 (`SLICE1-R11-NOTES.md`).
4. Combat and the nine class strategies, with a spell data pass against ForeverCore's DB2.
5. Follow, hire, party. First usable companion bot.
6. Loot, vendor, repair. 7. Quests. 8. Travel and the world pool. 9. Guild, orders, auction house.
10. Dungeons and raids. 11. Battlegrounds.
12. The TBM addon for the modern Classic client API (its own port).

Switched off on ForeverCore: Turtle LFT, Turtle-only items, spells, zones and races, Lua (Eluna) glue.

## Census

See `CENSUS.md` (numbers from `port_census.py` at the pinned commit).
