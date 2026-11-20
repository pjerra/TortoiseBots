# E. W3: quests

**Waits for W1 "grind and live"** (our branch `world/w1`, in progress) and **wants C (W6a)** for the reward choice. Can run
in parallel with D (W2).

## Done looks like

A free bot finds quest givers (! and ?), accepts quests it can do, works kill and collect objectives (and the few
use-object / explore ones), turns them in with a sensible reward choice, drops what it cannot finish, and shares quests in
a party, through the core's quest calls.

**Live acceptance** (world-life plan, W3 row): Northshire and Valley of Trials chains to level 6 for one Alliance and one
Horde bot: at least 8 quests turned in, one collect-item quest, one quest shared in a party of 2. Log accept, counts, turn-in, reward.

## Scope and size

"accept, objectives (kill, collect via loot, use object, explore), turn-in with reward choice, drop, share; quest givers
with ! and ?", 5 worker-days (code). Biggest risk: "vanilla quest data on F: objective-to-spawn mapping".

Legacy files (lines / census places): `QuestValues.cpp` 721/61, `QuestAction.cpp` 486/27, `AcceptQuestAction.cpp` 417/26,
`DropQuestAction.cpp` 268/22, `TalkToQuestGiverAction.cpp` 352/7; around them `QuestRewardActions.cpp`,
`ShareQuestAction.cpp`, `AutoCompleteQuestAction.cpp`, `QuestConfirmAcceptAction.cpp`, `generic/QuestStrategies.cpp`
(under `ai/playerbot/strategy/`). Area total 33 files, 4,494 lines, 178 places.

## ForeverCore differences and APIs

From the world-life plan:
- `Quest::Objectives` (`QuestObjective`: Type, ObjectID, Amount, StorageIndex, `QuestDef.h:477`) replaces
  `ReqItemId/ReqCreatureOrGOId[4]` (62 places); `Quest::GetObjectives()` `QuestDef.h:688`.
- Counts via `Player::GetQuestObjectiveData` (`Player.h:1837`, `:1871`); `QuestStatusData` reshaped (32 places).
- Modern quest packets (`WorldPackets::Quest`), so call `CanTakeQuest` / `CanAddQuest` / `AddQuestAndCheckCompletion` /
  `CanRewardQuest` / `AbandonQuest` / `RewardQuest(quest, LootItemType, rewardId, giver)` directly (`Player.h:1765-1780`).
- Share: `WorldSession::HandlePushQuestToParty` (`WorldSession.h:1693`) with the typed packet.
- Givers/enders: `ObjectMgr::GetCreatureQuestRelations`, `GetCreatureQuestInvolvedRelations`, `GetGOQuestRelations`,
  `GetGOQuestInvolvedRelations` (`ObjectMgr.h:1261-1266`, in the public section at the pin). **This corrects
  `forever/docs/UNPORTED.md`**, which says the "involved" maps are private with no accessor (it cites the members at
  `ObjectMgr.h:1817/1820`): the accessors exist. Check it when you start.
- Gossip with the giver: `PrepareGossipMenu` / `OnGossipSelect` (`Player.h:1744/1746`), `GetNPCIfCanInteractWith` (`:1300`).

## Data facts (DB sweep of the world DB at the pin, plus the team's real-client checks)

- **`quest_template` is the full modern table** (49,237 rows, Expansion 0 through 11), not a vanilla table. Vanilla is the
  `Expansion=0` / low-id subset plus Forever's custom re-ids (91xxx/99xxx). Retail-era quests leak into vanilla zone sorts
  (Darkshore: 132 of 211 zone quests have no giver or ender, e.g. 13504-13518, the Cataclysm chain). 112 duplicate titles
  (retail copy + custom re-id) across 11 zones.
- POI: 94 % of classic ids have `quest_poi`, custom ids >= 90000 only 60 %.
- Objectives in 11 low zones (distinct quests): ITEM 624, MONSTER 335, GAMEOBJECT 20, AREATRIGGER 13, PROGRESS_BAR 7,
  TALKTO 3. A "use object" test has about 20 candidates. Item objectives resolve to a creature loot / quest-item source
  for only 286 of 624 (46 %).
- 605 of 1,254 zone quests have neither giver nor ender relation; no giver/ender creature lacks a spawn. Escort has no
  flag (17 scripted starters only). 147 quests have `RewardChoiceItemID1 > 0`.
- `forever/data/waterys/starting-zone-quests.csv` (206 rows): Valley of Trials 13 of 13 work; Northshire 15 work, 7 do not
  (e.g. 14032 "[Programming Test]", 7961/7962 holiday quests with no giver, 91752/91758 no giver, 91772 objective object
  not spawned). Use its `works=yes` rows as the live set.
- `forever/data/waterys/quest-spells.csv` (real client): shaman quest 1518 (Earth totem) worked with accept + complete +
  turn-in; quest 96 did not complete with `.quest complete` at its Durotar spot (giver 5895 has no static spawn); druid
  quest 94638 on Zephras Isle is not offered at level 10 (probable `PlayerCondition` gate). Conditions refuse silently: log them.
- Character creation: 67 of 76 vanilla `playercreateinfo` rows carry `npe_map` 2175. Check that a `.tb create` bot starts
  in its race's start zone, not the modern intro, before the live test.

## UNPORTED rows it removes

- `incoming: quest` (accept, complete, confirm, share, details, update packets): replaced by direct calls and a polled
  quest-log state, no packet tables.
- `quest internals` (both rows): the 1.12 template fields and `RemoveQuest` (-> objectives, `GetLogTitle`, classic level
  range, `AbandonQuest`), and the four relation maps of `QuestObjectMgr` in `QuestValues.h` (-> the accessors above).
- Inside `ItemUsageValue`, the QUEST usage stays a stand-in until this item (W6a decision D5): W3 turns it on.

## Decisions already made

- Blizzlike: no `.quest add`/`complete` shortcuts in the bot path; bots never teleport to a giver; raids out.
- Direct server calls (`AddQuestAndCheckCompletion`, `CanRewardQuest`) over building quest packets.
- Ideas to read (never copy, GPL-2.0-only) from the ForeverCore team's bot: `QuestObjectiveLocations` (objectives without
  POI resolved from spawns and loot tables; use as a cross-check of the data), `QuestHubDatabase` (quest-giver clusters,
  built once per map).

## Rules that bite here

- Every quest/spawn/loot lookup the core does not cache goes through `WorldQueryCompat`; prefer the core's loaded data.
- A quest whose objective has no known source is skipped or dropped (less), never "completed".

## Tests (failing case first)

- **Failing case:** the quest filter accepts a retail-era quest under a vanilla zone sort (e.g. 13504) or a no-giver quest;
  must be refused. Second: an item objective with no creature source is "worked" forever; must be dropped after a bound.
- Pure: quest filter, objective progress from `GetQuestObjectiveData`, reward choice, drop rule.
- Wiring: quest calls only through the host seam; no `.quest` GM command in the bot path.

## Open for you to settle

1. The vanilla quest filter (suggest: `Expansion=0` or custom band, giver AND ender spawned, every objective has a known
   source, not on the CSV's `works=no` list).
2. Reward choice before C merges: wait for W6a, or choose by vendor price and log `reward=price` (less, but turns in).
