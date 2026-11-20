# W3: Quests (`feat/w3-quests`)

## What Was Done

1. **Pure Vanilla Quest Filter & Bounded Drop Policy (`ai/playerbot/policy/quest/QuestFilterPolicy.h`)**
   - `IsForeverCustomQuestId`: matches Forever's custom re-ID bands (`91xxx`, `99xxx`).
   - `IsVanillaOrCustomQuest`: accepts only `Expansion == 0` or custom re-IDs (`91xxx`/`99xxx`) that have both a spawned giver and a spawned ender (refuses retail-era leaks like `13504` and unspawned quests like `7961`, `91752`, `91758`).
   - `PreferQuestByTitle`: deduplicates by title, preferring custom re-IDs over retail copies.
   - `ShouldDropUnresolvableQuest`: bounded drop rule for item objectives with no known source.

2. **Both-Core Quest Compat Pair (`forever/src/compat/TbCompat_Quest.h` + `ai/cmangos-compat-quest.h`)**
   - Removed `QuestInternals` and `IncomingQuest` from `UnportedFeatures.h`, `TbCompat_Unported.h/.cpp`, `ai/cmangos-compat-unported.h`, and `forever/docs/UNPORTED.md`.
   - Ported quest templates & progress to ForeverCore's `Quest::GetObjectives()`, `Player::GetQuestObjectiveData()`, `Quest::GetLogTitle()`, `Quest::GetClassicQuestLevel()`, `Quest::GetClassicRequiredMoney()`, `Quest::IsTurnIn()`, and `Player::AbandonQuest()`.
   - Direct core accept/reward/share calls: `AcceptQuestDirectCompat` (`AddQuestAndCheckCompletion`), `CanRewardQuestCompat` / `CanRewardQuestChoiceCompat`, `RewardQuestDirectCompat` (`RewardQuest(quest, LootItemType::Item, rewardItemId, giver, announce)`), `PushQuestToPartyCompat` (`WorldPackets::Quest::PushQuestToParty`).
   - Party quest share polling in `TbCompat_S9.h` (`PollQuestShareCompat` reading `Player::GetPlayerSharingQuest()` + `GetSharedQuestID()`), answered by `AcceptQuestShareDirectCompat` (`SendPushToPartyResponse(bot, QuestPushReason::Accepted, qInfo)` + `ClearQuestSharingInfo()` + `AddQuestAndCheckCompletion`).
   - Vanilla quest eligibility gate (`QuestIsVanillaEligibleCompat`) caching spawned creature/GO entries once and checking `ObjectMgr::GetCreatureQuestRelationMapHACK()`, `GetGOQuestRelationMapHACK()`, `GetCreatureQuestInvolvedRelationReverseBounds()`, and `GetGOQuestInvolvedRelationReverseBounds()`.

3. **Quest Actions, Strategies & Reward Choice (`forever/SLICE9.txt` + `W3_KEYS` in `slice_gen_context.py`)**
   - Added 7 legacy files to `forever/SLICE9.txt`: `QuestStrategies.cpp`, `QuestAction.cpp`, `AcceptQuestAction.cpp`, `TalkToQuestGiverAction.cpp`, `DropQuestAction.cpp`, `ShareQuestAction.cpp`, `QuestRewardActions.cpp`.
   - Added `W3_KEYS` to `forever/tools/slice_gen_context.py` (`SliceQuestStrategyContext` for `quest`, `accept all quests`; actions `talk to quest giver`, `accept quest`, `accept all quests`, `accept quest share`, `drop`, `clean quest log`, `share`, `auto share quest`, `quest reward`; trigger `quest share`; value `quest reward`).
   - Reward choice in `TalkToQuestGiverAction` uses W6a's `ItemUsageValue` (`BestRewards`) and `sRandomItemMgr.ItemStatWeight` (`RewardGain`) directly (not the sell-price fallback).

4. **Host `.tb quest` Command (`forever/src/host/TbHostQuest.cpp`, `TbH3.h`, `TbH3Off.cpp`, `TbScripts.cpp`)**
   - `.tb quest <bot> status|accept <questId|*>|turnin [questId]|share <questId>|drop <questId|all>|reward <auto|list|ask|reset|?> [#token]`
   - Grammar v2 replies (`quest-begin` / `quest` / `quest-end`), `error code=engine-not-built` when `TORTOISEBOTS_SLICE1=OFF`.

## What Waits for W1 / W4

- **W1 (`world/w1`)**: Masterless free-bot lifecycle (`sRandomBotFacade.IsFreeBot`), autonomous questing loop, open-world rest/loot/corpse-run. Currently, autonomous questing triggers (`quest`, `accept all quests`) and `quest share` are registered, and a commanded/test bot near a giver/taker executes all quest actions via `.tb quest` or party share.
- **W4 (`TravelMgr`)**: Cross-zone travel to quest givers/objectives (`QuestValues.cpp`, `TravelMgr.cpp`). Currently `TravelMgr::LoadQuestTravelTable()` remains a no-op stub in `SliceLinkStubs.cpp`.

## Live Test Steps & Expected Output

Run on worldserver console (`tb ...`) or GM chat (`.tb ...`) with a character standing next to a starting-zone quest giver (e.g. Deputy Willem in Northshire or Gornek in Valley of Trials):

1. Setup two bots and enable engine:
   - `tb create testacct QbotA 1 1`
   - `tb create testacct QbotB 1 1`
   - `tb login QbotA` / `tb login QbotB`
   - `tb engine QbotA on` / `tb engine QbotB on`
   - `tb strategy QbotA nc +quest` / `tb strategy QbotB nc +quest`
2. Negative filter check (retail quest `13504` must be refused):
   - `tb quest QbotA accept 13504 #q1`
   - Expected: `quest bot=QbotA verb=accept id=13504 status=refused code=filtered #q1`
3. Accept a vanilla quest near giver (e.g. `783` A Threat Within in Northshire):
   - `tb quest QbotA accept 783 #q2`
   - Expected: `quest bot=QbotA verb=accept id=783 status=accepted #q2`
   - `tb quest QbotA status #q3`
   - Expected: `quest bot=QbotA id=783 title=A%20Threat%20Within status=complete objectives= #q3` (or `incomplete` for kill/collect quests)
4. Party share check:
   - `tb party QbotA QbotB #p1`
   - `tb quest QbotA share <shareableQuestId> #q4`
   - Expected: `quest bot=QbotA verb=share id=<shareableQuestId> status=shared #q4`, and `QbotB` accepts automatically on next tick (`tb quest QbotB status #q5` shows the quest).
5. Turn-in with reward choice (multi-choice quest):
   - `tb quest QbotA turnin <questId> #q6`
   - Expected: `quest bot=QbotA verb=turnin id=<questId> status=ok #q6` and dashboard/bot log records `QuestRewarded`.
6. Drop quest:
   - `tb quest QbotA drop <questId> #q7`
   - Expected: `quest bot=QbotA verb=drop id=<questId> status=dropped #q7`

## Gates

- **Census (`forever/SLICE9.txt`)**: `files 275  compile clean 275  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- **Build (`TORTOISEBOTS_SLICE1=ON`, `tortoisebots`)**: `[317/317] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a`
- **Build (`TORTOISEBOTS_SLICE1=OFF`, `tortoisebots`)**: `[3/3] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a`
- **Registry check**: `summary: (a)=0  (b)=231 keys to register or stub later`
- **Context check**: `SliceAiObjectContext.cpp is up to date` (`23 strategies, 78 actions, 109 triggers, 187 values`)
- **Pure test (`forever/tools/test_quest_filter.cpp`)**: `test_quest_filter: PASS`
- **Wiring test (`forever/tools/test_w3_quests_wiring.py`)**: `Ran 7 tests in 0.002s OK`; `--mutate` failed as expected (`AssertionError: TbHostQuest.cpp must not use GM .quest shortcuts`, exit 1)
- **Existing pure tests**: `test_run_orders` `RESULT: PASS (209 checks)`, `test_run_tasks: 148 checks, 0 failed`, `test_run_c6` `RESULT: PASS: 28 checks, 0 failed`
