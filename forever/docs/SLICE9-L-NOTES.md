# Slice 9, package L9: item loot, group-loot rolls, the item usage value

Branch `s9/l` (from `port/forever`), worktree `~/wt-s9-l`, build slot 2. Spec: `~/TortoiseBots-audit/FOREVER-SLICE9-SPEC.md` section 6 row L9 and the lead decisions of 2026-11-20:
**group loot, greed only, no auto-equip, at least 12 free bag slots**. Core pin 6517af0, `~/ForeverCore` untouched (zero core changes). No server was started.

## 1. What loot and rolls do on ForeverCore now

| Part | What happens | Where |
|---|---|---|
| Open a corpse | unchanged (slice 2): `HandleLootOpcode` through `LootOpenCompat`; the engine event `loot response` is raised by the open | `TbCompat_I2.h` `LootOpenCompat` |
| Read the window | the event's packet is rebuilt: money from `Loot::gold`, one row per item the core shows this bot (`LootItem::GetUiTypeForPlayer`, Loot.cpp:212) in the 1.12 row layout `StoreLootAction` reads; slot type 0 = takeable (ALLOW_LOOT, OWNER), 1 roll running, 2 locked, 3 master: the loop skips everything but 0 | `TbCompat_I2.h:142` `LootWindowReadCompat` |
| Slot rights | `BotMayTakeLootSlot` is fed from the same UI type: blocked = roll ongoing, shared allowed = ALLOW_LOOT/OWNER, quest copy = `needs_quest && !follow_loot_rules`, FFA copy = `freeforall` | `TbCompat_I2.h:212` `LootSlotFactsCompat` |
| Take an item | `CMSG_LOOT_ITEM` into `HandleAutostoreLootItemOpcode` (LootHandler.cpp:77); the request names the Loot's own guid; the core runs `Player::StoreLootItem` (Player.cpp:28834) with every check | `TbCompat_I2.h:235` `LootAutostoreCompat` |
| Allowed to take | `IsLootAllowed` runs (T's rules: always/skip lists, unique, quest items, loot strategy); quest items from F's item objectives | `TbCompat_I2.h:248` `ItemLootingUnportedCompat` (false), `:252` `QuestRequiredItemCompat` |
| What the corpse offers | `LootContentForCompat` (items takeable now), `LootStatusForCompat` (gold, items, rolling items count as unfinished), `CorpseHasVisiblePersonalItemForCompat` (`Loot::hasItemFor`) | `TbCompat_V2.h:74`, `:209`, `:229` |
| Bag floor | an item that needs a NEW slot is left when two or fewer bag slots are free (stacking is always allowed; an ordered item is exempt). The start check of 12 free slots is package C9's | `TbCompat_S9.h:269` `LootBagFloorCompat`, `LootAction.cpp:693` |
| A group-loot roll starts | the roll is in `Player::GetLootRolls()` (Player.h:2358, the core's read-only list of rolls waiting for this player). `WorldPacketTrigger::Check` polls it for the two triggers `loot start roll` and `loot response`, builds the legacy event packet `[owner guid][slot][item id][0][0][timeout]` once per roll and hands it to the existing `LootStartRollAction` | `WorldPacketTrigger.h:24`, `TbCompat_S9.h:119` `PollPacketEventCompat` |
| The vote | `RollOnItemInSlot` is the ONE sink: vote normalised, then `GreedOnlyVoteCompat(vote)` (F: always GREED), then `RollVoteCompat` = typed `CMSG_LOOT_ROLL` packet into `WorldSession::HandleLootRoll` (LootHandler.cpp:507) | `LootRollAction.cpp:597-598`, `TbCompat_S9.h:184`, `:220` |
| Roll winner | the core stores the item itself (`LootRoll::Finish`); nothing in the bot equips it: the trigger `loot roll won` (whose second action is `equip upgrades`) and `item push result` are NOT registered | `slice_gen_context.py` SLICE9_KEYS comment |
| A chest | `LootGatherUnportedCompat(object)` is false for a game object chest, so `DoLoot` casts the opening spell as on T; the chest window is announced by the same poll (game object owners only) and read like a corpse. Herb/ore nodes, skinning, pickpocket, fishing stay unported | `TbCompat_I2.h:191`, `TbCompat_V2.h:137` |
| Loot tables | `creature_`, `gameobject_`, `reference_`, `item_loot_template` are copied once (first use, or `LootTablePreloadCompat()`), through a guarded reader (information_schema first: a missing table or column reads empty, never aborts the server), into the layout classes the readers use. Used by the chest check (`IsCommonWorldProp`); the shared drop maps stay unported | `TbCompat_V2.h:298-390` |

Greed-only means: never need, never pass on its own (no roll times out), also for an ordered "roll need". `ShouldWaitForPlayers` is not reached for a bots-only party (no real player in the group); with `GroupLootTimerCompat` = 0 the bot votes at once in any case.

## 2. The compat pair

`forever/src/compat/TbCompat_S9.h` and `ai/cmangos-compat-s9.h` (same names, T bodies are the original expressions): `BotRollVote` (alias of T `RollVote`, F `RollType`), `ROLL_NOT_VALID`, `MAX_ROLL_FROM_CLIENT`, `BIND_WHEN_PICKED_UP`, `BIND_WHEN_EQUIPPED`, `ITEM_SPELLTRIGGER_CHANCE_ON_HIT`, `ITEM_CLASS_JUNK`, two aura names; `PollPacketEventCompat`, `NoteLootWindowAnnouncedCompat`, `RollVoteCompat`, `GreedOnlyVoteCompat`, `LootItemOfSlotCompat`, `LootItemAtIndexCompat`, `LootItemAllowedForCompat`, `MemberInLootRangeCompat`, `GroupLootTimerCompat`, `RandomRollCompat`, `AddRandomPropertyStatsCompat`, `LootBagFloorCompat`, `AuctionItemCountCompat`, `AhDepositMinCompat`, `AhDepositRateCompat`, `QuestIncompleteCompat`, `QuestSourceSlotsCompat`/`QuestSourceItemCompat`, `SkillLineIdCompat`/`SkillLineCategoryCompat`/`SkillLineAbilitySkillCompat`/`SkillLineAbilitySpellCompat`, `ItemRequiredHonorRankCompat`, `ItemDisenchantIdCompat`, `PlayerAmmoIdCompat`, `EquipSlotOfProtoCompat`. Edited in place (slice-2 files, same names): `TbCompat_I2.h` (`LootWindowReadCompat`, `LootSlotFactsCompat`, `LootAutostoreCompat` now takes the `Loot*`, `LootGatherUnportedCompat` now takes the object, `ItemLootingUnportedCompat`, `QuestRequiredItemCompat`), `TbCompat_V2.h` (see table), and the Turtle twin `cmangos-compat-s2i.h` for the two changed signatures. `AuctionEntry` (F, `TbCompat_Unported.h`) gained the four field names the price code reads; nothing creates one.

Shared files changed on the Turtle side, each with the T behaviour word for word: `LootRollAction.cpp/.h` (`RollVote` -> `BotRollVote`, the helpers above), `ItemUsageValue.cpp/.h` (helpers; `IsItemUsedBySkill` takes `uint32`), `LootAction.cpp` (two helper calls, the bag floor, T false), `WorldPacketTrigger.h` (T no-op poll). Test pin updated: `tools/test_quest_source_item_wiring.py` (the quest source fields moved behind `QuestSourceItemCompat`; the twin is pinned instead).

## 3. Registry and slice list

- `forever/tools/slice_gen_context.py`: `SLICE9_KEYS` = strategy `roll`; actions `loot start roll`, `loot vote`, `loot roll`, `auto loot roll`, `roll`, `release loot`; triggers `loot start roll`, `loot vote`, `loot roll`; values `active rolls`, `seen need rolls`, `item usage`, `skip go loot list`. Generated file: 18 strategies / 54 actions / 78 triggers / 174 values (was 17/48/75/170). Left out on purpose: `loot roll won`, `log roll won`, `equip upgrades`, `delayed roll`, `item push result` (nothing feeds them; `loot roll won` would wire auto-equip). `SliceMissingKeyGuard.h`: inert stand-ins for `std::vector<uint32>` (`craft spells`) and `std::vector<MountValue>` (`mount list`), which `ItemUsageValue.cpp` reads.
- `SliceAiFactory.cpp`: every non-combat kit gets `roll`; the tank (leader, carrier) gets `loot` and `roll` too (it had neither). One token each: the merge worker may drop the tank's `loot` if the run's own loot steps clash.
- `forever/SLICE9.txt` = the slice-4 list plus `LootRollAction.cpp` and `ItemUsageValue.cpp` (the "# L9" trailer). CMake and `slice_registry_check.py` read it. List the L9 files for a census: `FOREVER_SLOT=2 FOREVER_CENSUS_LIST=forever/SLICE9.txt forever/tools/vm-census.sh "" 14` (the whole list; the two L9 files alone are the last two lines). The merge worker combines the L9 trailer with SLICE8.
- The four stand-ins of `ItemUsageValue` in `SliceLinkStubs.cpp` are deleted. `UnportedFeatures.h`: `ItemUsage` row removed; `LootItems` -> `LootBoost`; `LootTables`, `GatheringLoot`, `IncomingLoot` re-worded (`UNPORTED.md`, "slice 9" section).

## 4. Things the merge worker must know

- `WorldQueryCompat` (slice 5, branch run/*) is not in this branch: the loot table reader has its own guard with the same rule (information_schema before a query that names columns). At the merge it may call `WorldQueryCompat` instead; behaviour is the same.
- `LootTablePreloadCompat()` is optional: a boot hook may call it (`OnWorldInitialized`); without it the tables are copied on first use (a few hundred ms on the world thread, once per table).
- `loot response` poll covers game object windows only. A chest opened by an order (package O9) arrives as that event; O9's result check reads the item in the bags.
- The roll poll runs inside the trigger check, which runs in the engine tick: no host file changed, no packet parsing (the core's `GetLootRolls()` is the source). Rolls of a bot that logs out with a window open leave one small memo entry until the bot's next poll prunes it.
- Not done on purpose: auto-equip, need/pass votes, master loot, `PERSONAL_LOOT` (reads as unsupported by the policy), gathering/skinning, the shared drop maps, `loot roll won` / vote echo.

## 5. U4: a roll vote from a socketless session (answered at merge time, one RFC boot after L9)

Needs a worldserver built option ON from the merged tree, a free test server (`docker ps --format '{{.Names}}' | grep -c forever-world` = 0), and a party of at least two bots on a group-loot party (a new group gets `GROUP_LOOT`, threshold uncommon, spec fact 5). Pattern: `~/forever-build/s6m-sess.sh` (`start`, `ready`, `c "<cmd>" [s]`, `log`, `stop`); copy it to a new session script, point it at the new binary.
1. `start`; `ready` (World initialized). Boot lines to read: `slice: registered 18/54/78/174`, `slice: N features not ported` (75), no `slice: missing ...` line naming a loot key.
2. `c "tb login <tank> <commander>"` and the same for a second bot (a priest or arms warrior), then the group exists (`tb where <bot>`); the commander is a GM outside the group, as slice 4 C4.
3. Put the party into Ragefire Chasm with the slice-4 sequence (`TB-COMMANDS.md`: `.tb tele ... to ...`, `.tb enter`; the entry rule is level 10), the group loot method is the default of a new group (spec fact 5: `Loot.SharedEncounterLoot` 1, threshold uncommon).
4. Let the party fight (the slice-3 pull/fight orders) and kill mobs until the first green (uncommon) item drops: the owner bot opens the corpse (`LootOpenCompat`), which starts the roll (`Loot::OnLootOpened`).
5. Expected in `world.out`, in this order: `LootRoll` start for the owner corpse (the core's `LogTable`/`LogGiven` lines of `playerbot.v2` at INFO), a `[BOT LOOT]`/`LootRoll` event row per bot (`bot_events.csv`, event `LootRoll`, vote `greed`), then the core's `roll winner takes it` line (`playerbot.v2` `LogGiven`) and the item in the winner's bags (a bag listing of the character). PASS = every bot voted greed once, one winner, no roll timed out (no `all passed`/60 s gap), no crash.
6. FAIL signatures and what each means: no `LootRoll` row at all -> the poll did not see the roll (check `Player::GetLootRolls()` non-empty with `.tb where`, the `roll` strategy on the bot, `slice: missing trigger loot start roll`); a vote row but the roll times out -> `HandleLootRoll` did not reach the roll (the session has no Player, or the loot guid differs: compare `Loot::GetGUID` with the owner); a crash in `LootRoll::PlayerVote` -> the session lacks something `SendRoll` needs (note the stack and stop). `stop` afterwards.

## 6. Gates

(filled at the end of the work: see the final report and the commit list)
