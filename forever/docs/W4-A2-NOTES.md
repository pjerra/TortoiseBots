# W4-A2: travel destinations on ForeverCore

Port of the travel destination layer (W4-A2 of `FOREVER-W0-W4-SPEC.md`, section 2.3) onto the core pin `25fc7ca`: what a free bot
may travel to, how a destination's level is rated, and which data on this core answers the questions the Turtle loader answered
from its own stores. Round A1 (the node graph) and B (generation) are other rounds; this one keeps the boot path, the destination
build and the two pure rules.

## 1. What runs

| Piece | Where |
|---|---|
| Both-core pair (field names, the three travel data sets, the reduced value answers, the purpose gate) | `forever/src/compat/TbCompat_WL4.h`, `ai/cmangos-compat-wl4.h`, wrapper `ai/playerbot/TbCompatWL4.h` |
| Out-of-line ForeverCore side: quest relations, spawn index, purpose map, registry gate, the reduced values | `forever/src/compat/TbCompat_WL4.cpp` (slice-only, filtered like `TbCompat_C.cpp`) |
| Destination manager (area levels, quest table, map transfers, pick rules) | `ai/playerbot/TravelMgr.cpp` (in the slice now) |
| Its values | `ai/playerbot/strategy/values/TravelValues.cpp` (bodies moved to the pair, see 3) |
| Zone level rule | `ai/playerbot/policy/travel/ZoneLevelPolicy.h` + `forever/tools/test_zone_level_policy.cpp` |
| Purpose gate (D5) | `ai/playerbot/policy/travel/TravelPurposeGatePolicy.h` + `forever/tools/test_travel_purpose_gate_policy.cpp` |
| Pins | `forever/tools/test_wl4_wiring.py` (round A1 appends its own classes to the same file) |

## 2. ForeverCore data, instead of the Turtle stores

| Need | Turtle | ForeverCore |
|---|---|---|
| Quest givers/enders and objectives | `ObjectMgr` quest relation stores (`QuestObjectMgr`) and the flat `ReqCreatureOrGOId` | `creature_queststarter/_ender`, `gameobject_queststarter/_ender` and `quest_objectives` rows, through `WorldQueryCompat`; classic quests only (`Expansion = 0`, the retail leftovers 81030/62609/80388 skipped), one line when a table is missing |
| Spawn points per entry | world scan (`WorldPosition().GetCreaturesNear()`/`GetGameObjectsNear()`) | `sObjectMgr` spawn stores, `FetchArea()` once per spawn at boot (the area flag is cached; no rule loads terrain later, #684) |
| Purposes a template serves | creature/game-object templates with every purpose | the reduced set the plan keeps on this core: **grind, town (GenericRpg), vendor, repair**. A vendor needs a non-empty stock list; invisible marker NPCs (`CREATURE_FLAG_EXTRA_TRIGGER`) are skipped; no trainer, AH, mail, boss or gather destination until their slices exist |
| Area level | `ai_playerbot_zone_level`, then the DBC `AreaLevel`, then a sub-area average, then the parent zone | the same table row, then **the parent zone**, then `AreaTableEntry::ExplorationLevel`, else `-1`. No sub-area average, no terrain. `ZoneLevelPolicy` is the rule the generation boot (W4-B) feeds: median `creature_classic_level` of hostile, non-critter, non-elite spawns, five samples minimum, lower median |
| Creature level/rank/gold/xp, quest level/type/items, area id/team/explore bit, skill maxima, map id | the cmangos fields | helpers in the pair (`CreatureLevelMaxWL4` = `creature_classic_level`, `CreatureRankWL4` = the classification enum with the same numbers, `QuestMinLevelWL4` = `GetClassicMinLevel`, `QuestReqItemIdWL4` = the objective rows, `AreaExploredWL4` = `Player::HasExploredZone`, ...) |
| Taxi zones | node store + terrain zones | none: flights are W5, the cache is empty |
| Fish locations | `ai_playerbot_named_location` rows | none: fishing is W7; the loader and saver return before their bodies |
| Travel graph | node store, generated links and paths | none yet: `TravelGraphLoadCompat()` says so once; **W4-A1 wires its node store in there** (the call site is `TravelMgr::LoadQuestTravelTable`) |

## 3. The cut (each one makes the bot do LESS)

| Legacy (Turtle) | ForeverCore |
|---|---|
| Quest needs by name, trainer needs by type, city/pvp/guild meeting/petition/mount purposes | `ShouldTravelNamedValue` answers false: those slices (W2/W3/W8/W9) are not merged |
| Vendor/repair/quest purposes | refused by `TravelPurposeGatePolicy` until W2's `sell`/`repair` actions plus the `maintenance` strategy, or W3's `rpg quest` strategy, are in the generated registry |
| Grind/town purposes | allowed; the legacy phase clock (`rpgPhase`) and the travel-refresh guard are kept |
| Outgrown-zone trigger with quest hand-in yields, targetless latch, grind dry-streak | zone level against the bot's level only (level 10+, pool bots); the yields come with W3/W0 |
| Run logs, fish, taxi zones, terrain trim, the travel graph | not opened, not built, not loaded on this core |

The four value bodies (`entry guidps`, `entry travel purpose`, `need travel purpose`, `should travel named`, `should leave outgrown
zone`, `quest stage active`) moved verbatim into `ai/cmangos-compat-wl4.h`, because their Turtle answers read services this core
does not build (guild orders, alt/hire lifecycle, trainer values) and the shared file must never compile those references into the
ForeverCore build. `TravelValues.cpp` keeps one call per value; the ForeverCore answers are the reduced ones above.

## 4. Shared lines touched (other rounds work on the same tree)

| File | Change | Owner note |
|---|---|---|
| `forever/SLICE-W1.txt` | three appended lines (marker comment + the two files) | the build list; round A1 appends its node graph the same way |
| `forever/SLICE-W4.txt` | created: SLICE-W1 plus the two files | round A1 appends `ai/playerbot/TravelNode.cpp` |
| `CMakeLists.txt` | the OFF-build filter names `TbCompat_WL4` | the out-of-line pair includes the bot brain |
| `forever/src/slice/SliceLinkStubs.cpp` | the Travel block keeps only the **node graph** stand-ins; the `TravelMgr`/`TravelTarget`/destination stand-ins are gone | round A1 deletes the node-graph lines when `TravelNode.cpp` lands |
| `forever/tools/slice_gen_context.py` | untouched (`WL4_KEYS` belongs to W4-C/D) | the pair reads registry keys without registering any |
| `ai/playerbot/TbCompatWL4.h` | new wrapper | A1's pair is `TbCompat_WL4n.h` per the plan |
| `forever/tools/test_wl4_wiring.py` | created with this round's classes | round A1 appends its classes |

## 5. Gates

| Gate | Result |
|---|---|
| Census `forever/SLICE-W4.txt` (272 files), slot 8, core 25fc7ca | `files 272  compile clean 272  with errors 0  error places 0`, run folder `w4a2-c2` |
| Policy tests (standalone, g++ C++17, forever-builder) | `test_zone_level_policy` 13 checks PASS; `test_travel_purpose_gate_policy` 22 checks PASS. Failing first: both tests against the pre-round tree (`HEAD~1`) stop at `fatal error: ai/playerbot/policy/travel/ZoneLevelPolicy.h: No such file or directory` (and the purpose header) |
| Wiring | `python3 -m unittest forever/tools/test_wl4_wiring.py` 12 tests OK |
| Generator / registry | `slice_gen_context.py --check` up to date; `slice_registry_check.py` (a)=0, (b)=222 |
| Module OFF (`FOREVER_SLOT=8 build.sh 14 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`; the OFF library holds no `TravelMgr::`/`TbCompat_WL4` symbol (the pair's .cpp is filtered like `TbCompat_C.cpp`) |
| Module ON + worldserver link (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON`, target `worldserver`) | `CMAKE_RC=0 MAKE_RC=0`, `bin/Release/bin/worldserver` 72,311,608 bytes, 0 `error:`; `strings` carries "travel destination spawn entries" and "the travel node store is not ported" |
| Turtle `vm-build` | `COMPILE_SECONDS=55 CMAKE_RC=0 MAKE_RC=0`, `mangosd` and `realmd` written, job rc=0 |
| `tools/verify_all.sh` (laptop, `tbf/main` baseline) | `Ran 2673 tests ... OK`, `482 standalone policy tests verified`, decision trail OK; the only failure is step 7, `TortoiseBots host-contract check failed: required core file is missing: src/game/SessionTransport.h` — the `../tortoise-wow` checkout is on another branch, exactly as on `tbf/main` (W1 notes, section 9). No new failure |
| Turtle `vm-test tb` (first run) | unit discovery 2673 tests, one error: `test_guild_founding_wiring.test_petition_need_honors_flag` read the petition branch from `TravelValues.cpp`, where the moved body no longer lives. The pin now reads `ai/cmangos-compat-wl4.h` (the branch text is verbatim there; the 3 tests pass). Its `action_loop`/`Mage stay FAILED` block is the same on the sibling A1 and W0a runs of the shared base (timing under load, not this round) |
| Turtle `vm-test tb` (after the pin fix) | see section 6 |


## 6. Live test (run2, 25fc7ca, console only)

Server `~/forever-run2` (lock taken for the whole run; its databases carry the 25fc7ca updates). Slot-8 ON `worldserver` installed
over `bin/worldserver` (backup `bin/worldserver.before-w4a2`, restored and boot-checked afterwards), `world.sh start`, console
`tb status` / `tb perf`, `world.sh stop`.

Boot log (the engine config loads in `OnWorldInitialized`, so the destination build is part of startup, not the first bot):

```
Loading area levels.
TortoiseBots: world table `ai_playerbot_zone_level` is missing, or lacks a column this query names: not read   (the W4-B migration is not applied here: parent/exploration fallback, no abort)
Loading trainable spells.
Loading object locations.
>> travel destination index: 20000 spawns, 1490 ms
...
>> Loaded 15684 travel destination spawn entries (123558 spawns on maps 0/1, 24551 skipped on other maps) in 1914 ms.
Creating travel destinations.
>> Loaded 224997 travel destination purpose entries in 64 ms.
Loading travel nodes.
TortoiseBots: the travel node store is not ported yet; travel has no graph and picks no target.
TrinityCore rev. ... (worldserver-daemon) ready...      (60 s after start, destination build included)
```

| Check | Result |
|---|---|
| Boot | ready; `grep -cE 'ASSERT|Segmentation|Aborted'` = 0 |
| Destination build | 1.9 s for 123,558 spawns on maps 0/1 (15,684 entries); purpose map 64 ms, 224,997 entries (grind, town, vendor, repair and the quest relations) |
| Zone levels | table missing on this server: one guard line, no abort, area levels from the parent/exploration fallback |
| Graph | one line: not loaded (round A1), travel still boots |
| `.tb status` | `enabled, 0 bot session(s)` |
| `.tb perf` after the build | 5007 ticks, world update avg 1.12 ms / worst 2 ms, module avg 0 us / worst 204 us, 0 sessions |
| Restore | old binary back, clean boot (`ready`, 0 asserts), server stopped |

Measurements that changed the design (kept in the code and here):

- `AsyncGuidPosition`'s pair constructor had no ForeverCore form (empty guid); the index now builds the guid from the entry and
  the spawn id.
- `TerrainMgr` holds the continents' terrain through a **weak** reference; with no live map every area lookup reloaded the
  map's grid files (20 ms per spawn, the boot did not finish in 4 minutes). Holding map 0 and 1's terrain for the pass makes
  the whole index 1.9 s; everything is released afterwards.
- The guarded queries must backtick the table and the columns (`SELECT \`id\`, \`level\` FROM \`ai_playerbot_zone_level\``), or the
  guard refuses them (it reports the refusal; nothing aborts).
