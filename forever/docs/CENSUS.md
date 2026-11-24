# Port census

## State today (2026-11-24)

Measured on a local PC (12 threads) against the newest ForeverCore branch `forever` (25be61f58f, 0.1.39.1) with this
repository at `dev` 77614ed00. The core of `forever-bots` at the pin is the same code (it only adds the PlayerbotV2
module, which stays off), so the numbers hold for the pin too.

| What | Files | Compile clean | Error places | Time |
|---|---|---|---|---|
| The build list, `forever/SLICE-W4.txt` (what the module compiles with `TORTOISEBOTS_SLICE1=ON`) | 375 | 375 | 0 | 3.3 min |
| The whole tree (every `.cpp` outside `forever/` and `tools/`) | 651 | 470 | 4,041 (in 206 files); 36 includes of a header ForeverCore does not have | 6.1 min |

Everything the ForeverCore build compiles is clean. The 181 files with errors are outside the build: Turtle-only host
and service code that ForeverCore replaces with its own in `forever/src/host` (`runtime/BotManager.cpp`,
`host/HireRecruiterScript.cpp`, the hire, auction-bot, LFT and battleground queue services), and features that are not
ported (`commands/Commander.cpp` 785 places, `BossTacticsActions.cpp` 306, `BattleGroundTactics.cpp` 215 and the AV file
160, `PlayerbotFactory.cpp` 175, `RandomItemMgr.cpp` 119). The features still answered "not ported yet" are in
`UNPORTED.md`.

Local run (Docker image `forever-builder-clang:local`, the core configured with `-G Ninja -DBUILD_TORTOISEBOTS=1
-DTORTOISEBOTS_SLICE1=ON` into `<build>`; no compile needed):

```bash
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e FOREVER_CENSUS_LIST=forever/SLICE-W4.txt \
  -v <core>:/src:ro -v <build>:/build -v <out>:/census forever-builder-clang:local \
  python3 /src/src/modules/TortoiseBots/forever/tools/port_census.py /build /census 12
```

Leave `FOREVER_CENSUS_LIST` empty for the whole tree. Everything below is the history of the port: the numbers of each
round at its own core pin, kept as they were measured.

## History

`forever/tools/port_census.py` compiles every `.cpp` of the bot brain syntax-only against ForeverCore, with
`forever/src/compat/botpch_forever.h` in front (as `ai/botpch.h` is on the Turtle core), and counts distinct error
places. A place is one file, line and message; a mistake in a header counts once however many files include it.
The compiler stops looking in a function after some errors, so the number is a floor, not the whole distance.

Run: `forever/tools/vm-census.sh "" 14` (all, about 45 minutes) or with a path part for a subset.

Order inside `vm-census.sh` (pinned by `forever/tools/test_vm_census_order.py`): the slot lock on the VM first
(`census[-N].lock`, plus `build[-N].lock` so no build starts on a source being replaced), then the upload into `src[-N]`, then
the census and the summary read (`forever/tools/census.sh`, the runner that comes with the uploaded tree, started by the same VM-side session that holds the
lock, so a dropped connection does not free the slot under a running census), then the lock is released. Each run writes its tables to its own folder `yulon-ubuntu2:~/forever-build/census-runs/<name>/`
(`FOREVER_CENSUS_NAME`, default `<worktree>-s<slot>-<list or all>-<utc stamp>-<pid>`), so two runs never share an output
folder; the old `census-N/` folders are not used any more. A summary that says `files 0` fails the script, and so does a census that exits nonzero (docker or `port_census.py`) or writes no summary or tables: the script prints no summary then and exits with the census's status (1 when the artifacts are missing).

| Date | Core pin | State | Files | Compile clean | Error places |
|---|---|---|---|---|---|
| 2026-11-17 | 6517af05 | no pre-include, include paths only | 648 | 8 | 30,167 |
| 2026-11-17 | 6517af05 | pre-include + first shim, `ai/playerbot/strategy/values` only | 104 | 0 | 2,717 |
| 2026-11-17 | 6517af05 | pre-include + first shim (7808b02ae): the real baseline | 648 | 0 | 17,530 |
| 2026-11-17 | 6517af05 | shim round 1 (a7a640591), no bot code changed | 648 | 11 | 13,496 |
| 2026-11-17 | 6517af05 | pre-include + first shim, all files (BEFORE shim round 1) | 648 | 0 | 17,530 |
| 2026-11-17 | 6517af05 | shim round 1 (AFTER) | 648 | 11 | 13,496 |
| 2026-11-17 | 6517af05 | shim round 2 (BEFORE, = round 1 AFTER at 233e35a0c) | 648 | 11 | 13,496 |
| 2026-11-17 | 6517af05 | shim round 2 (AFTER, item prototype mirror, 570d22dd4) | 648 | 11 | 12,182 |
| 2026-11-18 | 6517af05 | shim round 2 fix round 1 (review: block/holy/bag family/one fill; entry + IsMoving helpers, b7d20355a) | 648 | 11 | 11,938 |
| 2026-11-18 | 6517af05 | shim round 3 (BEFORE, = round 2 fix 2) | 648 | 11 | 11,938 |
| 2026-11-18 | 6517af05 | shim round 3 (AFTER, spell mirror + fetch helpers, 6cbc923cd) | 648 | 11 | 11,065 |
| 2026-11-18 | 6517af05 | shim round 3 fix round 1 (difficulty-NONE slot, sparse index, faithful casts, usable-item gate, 0a7f82604) | 648 | 11 | 11,063 |

| 2026-11-18 | 6517af05 | whole tree at c88084785 (slice 1 after R8), measured by the lead | 649 | 322 | 7,650 (in 350 files) |

The first row is not comparable with later ones: without the pre-include most files stopped early.

The fix-round row is two places below the round-3 AFTER (11,065); the slice — whose 127 files this round did not touch —
is exactly 2,504 again. No error family appeared or vanished from the top of the name table.

What the first run named most (distinct places): the cmangos singleton spelling (`MaNGOS::Singleton<>::Instance()`,
`sObjectMgr.`), `Player` / `Item` / `Unit` members with other names (`GetObjectGuid`, `GetGUIDLow`, `GetProto`,
`getPositionX`), `ItemPrototype` fields (`Class`, `SubClass`, `Quality`, `ItemId`), `SpellEntry` fields (`Effect[i]`),
`WorldPosition`'s `x` / `y` / `z` / `mapId` (it derives from the core's `WorldLocation`, whose fields are named
differently), update fields (`GetUInt32Value`), `WorldTimer`, `sLog.outString`.

Shim round 1 notes, the table of mapped names and the "cannot shim" groups: `SHIM-R1-NOTES.md`. Largest groups left:
item template fields (1,168 places), then spell data, position members, database calls, update fields, `GetProto`.

Shim round 3 (the spell mirror) notes: `SHIM-R3-NOTES.md`. `SpellEntry` is our own struct on ForeverCore, filled per spell
from `SpellInfo` on first use; every spell fetch reads `GetSpellEntryCompat` (32 `sSpellMgr.GetSpellEntry` and 54
`sSpellTemplate.LookupEntry` sites, 125 `X->m_spellInfo` and 32 `X->GetSpellProto()` wraps, 4 by hand). The spell-data
families are gone from the name table: `Effect` 173 -> 0, `EffectApplyAuraName` 102 -> 0, `MAX_EFFECT_INDEX` 64 -> 0,
`EffectTriggerSpell` 58 -> 0, `EffectMiscValue` 42 -> 0, `EffectItemType` 37 -> 0, `EffectBasePoints` 35 -> 0,
`SpellEffectIndex` 28 -> 0, `IsPassiveSpell` 21 -> 0, `GetSpellCastTime` 18 -> 0, `manaCost` 11 -> 0, `rangeIndex` 9 -> 0;
what stays is `SpellIconID` (3) and `Custom` (2), both left out for want of a counterpart. Census 11,938 -> 11,065.

Shim round 2 (the item prototype mirror) notes: `SHIM-R2-NOTES.md`. The lead's round-1 finding on
`IsMoving -> isMoving` is resolved there: the rename is removed (D6), and the bot's 59 `IsMoving()` calls now go through
`IsMovingCompat` (fix round B2). The review fix round 1 also corrected `Block` (the classic column, not the item stat),
kept holy out of "+all resistances", converted `BagFamily` from the core mask to the old number, filled the mirror once at
`OnWorldInitialized` (lock-free lookups afterwards) and moved the 180 entry lookups to `GetItemProtoByIdCompat`: census
12,182 -> 11,938 places, with `GetItemPrototype` and `IsMoving` gone from the name table.

## Slice 1 (bot engine + warrior)

Measured on the files of `forever/SLICE1.txt` only
(`FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 14`): 127 files at the baseline,
133 at the end of R9 (`SLICE1-R9-NOTES.md`).
The whole-tree number above is no longer the target; this one is.

| Date | State | Files | Compile clean | Error places |
|---|---|---|---|---|
| 2026-11-18 | baseline, at round 3 (spell mirror) | 127 | 3 | 2,504 (in 140 files incl. headers) |
| 2026-11-18 | round 3 fix round 1 (6f6bce297; no slice file changed at 0a7f82604 either) | 127 | 3 | 2,504 (in 140 files incl. headers) |
| 2026-11-18 | round 3, registry move applied in a VM copy (`SLICE1-R3-NOTES.md`) | 127 | not stated | 1,570 (in 73 files) |
| 2026-11-18 | four R4 packages merged, final (`SLICE1-R4M-NOTES.md`) | 127 | 4 | 460 (in 27 files) |
| 2026-11-18 | R6A: the 460 places answered by the not-ported table (`SLICE1-R6A-NOTES.md`) | 127 | 127 | 0 |
| 2026-11-18 | R6B: linked and booted (`SLICE1-R6B-NOTES.md`) | 131 | 131 | 0 |
| 2026-11-18 | R9 to R11: review fix rounds and the engine live test (`SLICE1-R11-NOTES.md`) | 133 | 133 | 0 |

Slice 1 error places in order: 2,504 -> 1,570 -> 460 -> 0 (the rows above).

Where they are: `PlayerbotAI.cpp` 348, `MovementActions.cpp` 317, `WorldPosition.cpp` 258 and `WorldPosition.h` 225,
`PlayerbotAISpells.cpp` 128, `PlayerbotAIPackets.cpp` 74, `GenericTriggers.cpp` 51.

## Slice 2 (priest, cast results, money looting)

Measured on the 159 files of `forever/SLICE2.txt` (SLICE1.txt's 132 plus the 27 files of FOREVER-SLICE2-SPEC section 3; the spec
text says "158", the sum is 159) with `FOREVER_SLOT=1 FOREVER_CENSUS_LIST=forever/SLICE2.txt forever/tools/vm-census.sh "" 10`, at c88084785
plus the list file only (no source file changed).

| Date | State | Files | Compile clean | Error places |
|---|---|---|---|---|
| 2026-11-18 | baseline N0, slice 2 list at c88084785 | 159 | 148 | 260 (in 14 files); 1 include of a header ForeverCore does not have |

Summary line: `files 159  compile clean 148  with errors 11  error places 260 (in 14 files)  includes of a header ForeverCore does not have 1`.
The 132 slice-1 files are clean (slice-1 census, same build: `files 132 ... error places 0`); all 260 places are in the new files and
the headers they pull in.

Top 15 of `where.tsv` (error places per file; 14 files have any):

| Places | File |
|---|---|
| 76 | `ai/playerbot/strategy/actions/LootAction.cpp` |
| 63 | `ai/playerbot/strategy/actions/UseItemAction.cpp` |
| 48 | `ai/playerbot/strategy/values/LootValues.cpp` |
| 22 | `ai/playerbot/LootObjectStack.cpp` |
| 16 | `ai/playerbot/strategy/values/QuestValues.h` |
| 13 | `ai/playerbot/policy/loot/CorpseLootPolicy.h` |
| 12 | `ai/playerbot/strategy/actions/AddLootAction.cpp` |
| 3 | `ai/playerbot/strategy/priest/PriestActions.cpp` |
| 2 | `ai/playerbot/strategy/triggers/LootTriggers.cpp` |
| 1 | `ai/playerbot/strategy/priest/GenericPriestStrategy.h` |
| 1 | `ai/playerbot/strategy/priest/GenericPriestStrategy.cpp` |
| 1 | `ai/playerbot/strategy/priest/PriestMultipliers.cpp` |
| 1 | `ai/playerbot/strategy/values/NearestCorpsesValue.cpp` |
| 1 | `ai/playerbot/strategy/values/PartyMemberToDispel.cpp` |

(The list has only 14 files with errors.) Against the spec's 231 places in the 27 files: the 27 `.cpp` files hold 231 as predicted
(76+63+48+22+12+3+2+1+1+1+1+1 = 231) and the other 29 are in two headers they pull in, `QuestValues.h` (16, quest-item types
`LootAction.cpp` / `LootValues.cpp` read) and `CorpseLootPolicy.h` (13, loot-table internals: `LootStoreItem`, `mincountOrRef`,
`QuestItem`). The one missing include is `GenericPriestStrategy.h:12` `#include "RangedCombatStrategy.h"`, a bare name that only
resolves on Turtle (its include path holds `strategy/generic`); it costs the 2 `GenericPriestStrategy` places (fix: the path form
`generic/RangedCombatStrategy.h`, as `StrategyContext.h:41` writes it, or the include directory). Full tables on the VM:
`yulon-ubuntu2:~/forever-build/census-1/`.

## Slice 3 (group fight on orders: threat/tank, pull, orders)

Measured on the 170 files of `forever/SLICE3.txt` (SLICE2.txt's 160 plus the 10 files of FOREVER-SLICE3-SPEC section 3; the spec text says
"173", that is the line count of the list file with its two comment lines and the sum of SLICE2.txt's 163 lines: the list holds 170 paths)
with `FOREVER_SLOT=2 FOREVER_CENSUS_LIST=forever/SLICE3.txt forever/tools/vm-census.sh "" 10`, at the commit that adds the list file
(no source file changed). The spec's estimate was 20-60 places.

| Date | State | Files | Compile clean | Error places |
|---|---|---|---|---|
| 2026-11-18 | baseline N0, slice 3 list | 170 | 165 | 17 (in 5 files); 0 includes of a header ForeverCore does not have |
| 2026-11-18 | after the porting worker (slice3/p) | 170 | 170 | 0 (SLICE2.txt 160 files: 0; SLICE1.txt 133 files: 0) |

Summary line: `files 170  compile clean 165  with errors 5  error places 17 (in 5 files)  includes of a header ForeverCore does not have 0`.
All 160 slice-2 files and 5 of the 10 new ones are clean at once; the 17 places are all in new files (no header holds any).

`where.tsv` (error places per file):

| Places | File |
|---|---|
| 7 | `ai/playerbot/strategy/generic/ThreatStrategy.cpp` |
| 5 | `ai/playerbot/strategy/actions/WaitForAttackAction.cpp` |
| 2 | `ai/playerbot/strategy/actions/PullActions.cpp` |
| 2 | `ai/playerbot/strategy/triggers/PullTriggers.cpp` |
| 1 | `ai/playerbot/strategy/actions/ValueActions.cpp` |

The ten files with 0 places: `TankAssistStrategy.cpp`, `StayStrategy.cpp`, `PassiveStrategy.cpp`, `StayActions.cpp`, `ChatShortcutActions.cpp` (the
spec expected 3-8 in the last two: `PlayerConvenience.h`, `Formations.h` and the position values were already ported by slice 1 and 2).
Names (`names.tsv`): `SummonCreature` 3, `GetSource` 2, `GetPlayer` 2, `SendPlaySpellVisual` 2, and one each of `find`, two `unordered_map`
operator[] on a raw guid, `SeekCycleSnapshot`, `threat` (the field of `SpellThreatEntry`), `uint64_t`. Full tables:
`yulon-ubuntu2:~/forever-build/census-2/`.

## W1 (world life, grind and live), 2026-11-20

List `forever/SLICE-W1.txt` = the slice-9 list plus `strategy/generic/GrindingStrategy.cpp`, `strategy/values/GrindTargetValue.cpp`,
`strategy/actions/ReviveFromCorpseAction.cpp`, `strategy/values/DeadValues.cpp` and `FleeManager.cpp` (270 files; the list CMake and
`slice_registry_check.py` now read). Summary: `files 270  compile clean 270  with errors 0  error places 0  includes of a header
ForeverCore does not have 0` (run folder `yulon-ubuntu2:~/forever-build/census-runs/wt-w1-s8-SLICE-W1-20261120T125238Z-2669463/`).
The first pass had 13 places in 3 files (the `%s`/`GetName()` format, the `ObjectMgr` pointer, the duplicated `MapEntryCompat` and
`TerrainIsInWaterCompat`); all closed in `forever/src/compat/TbCompat_W1.h`. `FleeManager` is no longer an unported feature (it is
compiled now), so the boot line counts 74, one less than before.

Integration batch 6 (the merge of `tbf/main` 6e8e0494a into W1) re-read the same list on the core pin 25fc7ca (slot 32, module tree
`integ/batch-6`): `files 270  compile clean 270  with errors 0  error places 0  includes of a header ForeverCore does not have 0`
(run folder `yulon-ubuntu2:~/forever-build/census-runs/integ6-32-SLICE-W1`). `SLICE11.txt` on the same tree: `files 263  compile
clean 263  with errors 0  error places 0` (`census-runs/integ6-32-SLICE11`). Nothing in the module needed a change for the pin move.

## W4-A1 (travel on foot: the node graph), 2026-11-20

List `forever/SLICE-W4.txt` = every path of `SLICE-W1.txt` plus `ai/playerbot/TravelNode.cpp` (271 files; the list `CMakeLists.txt` and
`slice_registry_check.py` now read; later W4 rounds append their files). Summary on the core pin 25fc7ca (slot 7): `files 271
compile clean 271  with errors 0  error places 0  includes of a header ForeverCore does not have 0` (run folder
`yulon-ubuntu2:~/forever-build/census-runs/wt-w4a1-s7-SLICE-W4-20261120T164613Z-4145388/`). The first read of `TravelNode.cpp`
alone had 106 places: the area-trigger fields and the teleport condition, the creature template and spawn fields, the NPC service
flags, the game-object spellcaster/transport fields, the taxi row fields, `PlayerInfo`'s start position, `RACE_HIGH_ELF`,
`SIZEFMTD`, the printf `PExecute` of the saver and the three `WorldDatabase.Query` reads; all closed in
`forever/src/compat/TbCompat_WL4n.h` (with the area-trigger entry helpers of the W1 pair). `TravelNode.cpp` alone after the last
edits: `files 1  compile clean 1  with errors 0  error places 0`.

## W4-A1 with round A2 merged (the travel destinations and zone levels), 2026-11-20 (re-review round)

List `forever/SLICE-W4.txt` = `SLICE-W1.txt` (which now ends with `ai/playerbot/TravelMgr.cpp` and
`ai/playerbot/strategy/values/TravelValues.cpp`, round A2) plus `ai/playerbot/TravelNode.cpp`: 273 files. Summary on the core pin
25fc7ca (slot 8): `files 273  compile clean 273  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does
not have 0` (run folder `yulon-ubuntu2:~/forever-build/census-runs/w4a1-fix-s8-SLICE-W4-20261120T190128Z/`). The A2 files needed
no new compat helper in this round; the node graph and the loader were exercised at boot by
`forever/tools/travel_graph_boot_check.sh` (W4-A1-NOTES.md section 7).

## Integration batch 8 (W4 merged into main's #93/#95), 2026-11-20

List `forever/SLICE-W4.txt` = the W4 list (273 files on 25fc7ca) plus the `W6a/W2/W3` block main's #93 added to `SLICE-W1.txt`
(16 legacy files: `RandomItemWeights.cpp`, `EquipAction.cpp`, `MaintenanceStrategy.cpp`, `SellAction.cpp`, `BuyAction.cpp`,
`RepairAllAction.cpp`, `TrainerAction.cpp`, `MailAction.cpp`, `BudgetValues.cpp`, `QuestStrategies.cpp`, `QuestAction.cpp`,
`AcceptQuestAction.cpp`, `TalkToQuestGiverAction.cpp`, `DropQuestAction.cpp`, `ShareQuestAction.cpp`,
`QuestRewardActions.cpp`): 289 files, the same set as `SLICE-W1.txt` plus `ai/playerbot/TravelNode.cpp`. W4 branched before #93,
so its list did not hold that block; without it the branch would have lost every W2/W3/W6a file from the build. Summary on the
core pin d02645dd21 (slot 1, `~/ForeverCore-d02645dd`): `files 289  compile clean 289  with errors 0  error places 0 (in 0
files)  includes of a header ForeverCore does not have 0` (run folder
`yulon-ubuntu2:~/forever-build/census-runs/integ8-s1-SLICE-W4/`). `slice_gen_context.py --check` up to date;
`slice_registry_check.py` `(a)=0  (b)=232` (223 on the W4 branch: the sixteen files above name more keys).

*Closed since:* `MaintenanceValues.cpp` is in the build list now and `vendor trip needed` is registered
(`SliceAiObjectContext.cpp`), so the Vendor and Repair purposes resolve. The text of that day follows.

The vendor travel need does not resolve on this branch: `TravelPurposeGateRegisteredCompat` needs W2's `vendor trip needed`
value, #93 registers the `sell`/`repair` actions, the `maintenance` strategy and its budget values but not that value
(`W2_KEYS["value"]` has no vendor need), and its calculator `ai::VendorTripNeeded` lives in
`ai/playerbot/strategy/values/MaintenanceValues.cpp`, which no slice list compiles. So `values.count("vendor trip needed")` is
false, the Vendor and Repair purposes are refused (`travel_purpose::RefusalOf` = `VendorKeysMissing`) and the bot keeps
grinding - the gate's do-less answer, not a crash (`SliceMissingKeyGuard.h` answers an unregistered value with an inert
stand-in). Closing it is a W2 follow-up: compile `MaintenanceValues.cpp` into the slice, or add a ForeverCore
`VendorTripNeeded` and the creator, and the gate opens by itself.
