# W3 Quests — Live Verification Notes (`feat/world-life`)

Verified live on the local ForeverCore realm (`Qbot`, level 1→2 Human Warrior, account 90, master `Cmdr`, Northshire Valley) using `forever-agent` and `.tb quest`.

## Root Causes Found & Fixed Live

1. **Numeric quest IDs blocked by `ChatLinks` stand-in (`forever/src/compat/TbCompat_Unported.h`, `ai/cmangos-compat-unported.h`, `ai/playerbot/PlayerbotAI.cpp`)**
   - `PlayerbotChatHandler::extractQuestId` routed all input through `UnportedBlockResultCompat(tbcompat::Feature::ChatLinks, ...)`, returning `0` for plain numeric IDs such as `"783"` or `"7"`.
   - Added the `ExtractQuestIdCompat` compat pair: Turtle keeps `ExtractKeyFromLink(&source, "Hquest")` unchanged; ForeverCore parses plain positive numeric IDs directly without depending on chat links, falling back to the unported `ChatLinks` block only for link strings.

2. **`accept *` always returned `false` (`ai/playerbot/strategy/actions/AcceptQuestAction.cpp`)**
   - When `text == "*"`, `quest` is `0` and `guid` is empty; `AcceptQuestAction::Execute` fell through after the nearby NPC/GO loop to `if (!quest || guid.IsEmpty() || quest == 80388) return false;`, discarding `hasAccept`.
   - Returned `hasAccept` directly for `text == "*"`.
   - Also added `QuestMenuItemIsAvailableCompat` (`forever/src/compat/TbCompat_Quest.h`, `ai/cmangos-compat-quest.h`) because ForeverCore's `Player::PrepareQuestMenu` sets `QuestIcon = 2` for available quests while `DIALOG_STATUS_AVAILABLE` in `TbCompat_V2.h` is `5`.

3. **`RemoveQuestFromLogCompat` did not remove quests from the log (`forever/src/compat/TbCompat_Quest.h`)**
   - TrinityCore's `Player::AbandonQuest(questId)` only destroys quest items; `WorldSession::HandleQuestLogRemoveQuest` performs the actual quest log removal (`RemoveActiveQuest`, timers, source items, `OnQuestStatusChange`).
   - Updated `RemoveQuestFromLogCompat` to invoke `HandleQuestLogRemoveQuest` for the matching log slot (with `RemoveActiveQuest` + `AbandonQuest` fallback when no session is present).

4. **`turnin` failed when master had a non-giver targeted (`ai/playerbot/strategy/actions/QuestAction.cpp`)**
   - `QuestAction::Execute` returned `ProcessQuests(guid)` unconditionally when `master->GetSelectionGuid()` was non-empty (e.g. a dead mob from combat), never falling through to `"nearest npcs"`.
   - Changed to return `true` only when `ProcessQuests(guid)` succeeds, otherwise falling through to nearby NPCs/GOs within `INTERACTION_DISTANCE`.

## Live Acceptance Transcript (`Qbot`, Northshire Valley)

### (1) Accept by ID and with `accept *` (plus negative filter check)

Near Deputy Willem (`823`), accepting `783` (A Threat Within):
```text
quest-begin #a1
quest bot=Qbot verb=accept id=783 status=accepted #a1
quest-end count=1 records=1 bot=Qbot verb=accept #a1
```

After turning in `783` at Marshal McBride (`197`), accepting `7` (Kobold Camp Cleanup) via `accept *`:
```text
quest-begin #a2
quest bot=Qbot verb=accept status=accepted #a2
quest-end count=1 records=1 bot=Qbot verb=accept #a2
```

Negative filter check (retail quest `13504` refused):
```text
quest-begin #f1
quest bot=Qbot verb=accept id=13504 status=refused code=filtered #f1
quest-end count=1 records=1 bot=Qbot verb=accept #f1
```

### (2) `status` lists quests with objectives

```text
quest-begin #s2
quest bot=Qbot id=7 title=Kobold%20Camp%20Cleanup status=incomplete objectives=0%2F10 #s2
quest-end count=1 records=1 bot=Qbot verb=status quests=1 #s2
```

### (3) Complete objectives (kill quest `7` and collect-item quest `33`)

After fighting Kobold Vermin (`entry 6`):
```text
quest-begin #s8
quest bot=Qbot id=7 title=Kobold%20Camp%20Cleanup status=complete objectives=10%2F10 #s8
quest-end count=1 records=1 bot=Qbot verb=status quests=1 #s8
```

After killing Young Wolves (`299`) and Timber Wolves (`69`) and looting Tough Wolf Meat (`750`) for quest `33` (Wolves Across the Border):
```text
quest-begin #sw
quest bot=Qbot id=33 title=Wolves%20Across%20the%20Border status=complete objectives=8%2F8 #sw
quest-end count=1 records=1 bot=Qbot verb=status quests=1 #sw
```

### (4) Turn in (`turnin`), picking a reward by item value when there is a choice

Single/no-choice turn-ins (`783`, `7`, `5261` without targeting the NPC):
```text
quest-begin #t1
quest bot=Qbot verb=turnin id=783 status=ok #t1
quest-end count=1 records=1 bot=Qbot verb=turnin #t1

quest-begin #t7b
quest bot=Qbot verb=turnin id=7 status=ok #t7b
quest-end count=1 records=1 bot=Qbot verb=turnin #t7b

quest-begin #t5261
quest bot=Qbot verb=turnin id=5261 status=ok #t5261
quest-end count=1 records=1 bot=Qbot verb=turnin #t5261
```

Multi-choice reward turn-in (`33` Wolves Across the Border — choices: `80` Soft Fur-lined Boots vs `6070` Wolfskin Bracers):
```text
quest-begin #t33
quest bot=Qbot verb=turnin id=33 status=ok #t33
quest-end count=1 records=1 bot=Qbot verb=turnin #t33
```
`Qbot` selected and equipped `80` (Soft Fur-lined Boots, slot 7, replacing `40`):
```text
state bot=Qbot role=dps rolesrc=auto class=warrior level=2 hp=79 hpmax=79 hppct=100 power=rage pw=0 pwmax=100 st=non-combat incombat=no tgt= tgtentry= tgthp= victim= hold=no mv=follow stuck= engine=yes pos=-8872.74,-164.17,80.82 map=0 inst=0 dl=1.2 dm=1.2 ord= lastorder= co=arms;arms%20pve;buff;buff%20arms%20pve;close;dps%20assist;threat nc=arms;arms%20pve;default;dps%20assist;follow;food;loot;maintenance;roll cast_queued=0 cast_delivered=0 cast_dropped=0 cast_pending=0 eating=no drinking=no resting=no rest_why= life=alive relhold= resreq= outside=no entered= entry= block= catchup=0 practice=no rticc=moon ccfam= form=17 totems= imbue= seal= paura= bagfree=9 money=49 spec=arms gear=3:38;6:39;7:80;15:25;16:2362 dur=100 mail=0 #st_end
```

### (5) `share` and `drop`

Sharing quest `7` with party (`Cmdr` + `Qbot`):
```text
quest-begin #sh1
quest bot=Qbot verb=share id=7 status=shared #sh1
quest-end count=1 records=1 bot=Qbot verb=share #sh1
```

Accepting `15` (Investigate Echo Ridge) and dropping it:
```text
quest-begin #s12
quest bot=Qbot id=15 title=Investigate%20Echo%20Ridge status=incomplete objectives=0%2F10 #s12
quest-end count=1 records=1 bot=Qbot verb=status quests=1 #s12

quest-begin #d15
quest bot=Qbot verb=drop id=15 status=dropped #d15
quest-end count=1 records=1 bot=Qbot verb=drop #d15

quest-begin #s13
quest-end count=0 records=0 bot=Qbot verb=status quests=0 #s13
```

## What Still Waits for W1 / W4

- **W1 (`world/w1`)**: Autonomous questing loop (`grind` strategy is not yet registered in this slice — `grind is not a strategy this build registers for the bot`), masterless free-bot lifecycle (`sRandomBotFacade.IsFreeBot`), open-world rest/corpse-run.
- **W4 (`TravelMgr`)**: Cross-zone travel to quest givers and objectives (`TravelMgr::LoadQuestTravelTable()` stub).
