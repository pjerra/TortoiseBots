# Loot and rolls — client flow vs core

Client build **1.60.1.70938** (Interface **16001**), 2026-11-17. Core: TrinityCore `6517af05`
(`forever/spike/TrinityCore`); paths are relative to that clone root.

## Client side, step by step

1. **Corpse is created on death**. `Unit::Kill` (`Entities/Unit/Unit.cpp:11807`) builds the corpse loot:
   a grouped kill makes one shared `Loot` under the group's loot method (`Unit.cpp:11923`,
   `FillLoot` at `Unit.cpp:11928`), gold is rolled in `generateMoneyLoot` (`Unit.cpp:11931`) and the
   round-robin rotation advances in `Group::UpdateLooterGuid` (`Groups/Group.cpp:1148`).
   Loot table expansion is `Loot::FillLoot` (`Loot/Loot.cpp:924`) → `Loot::AddItem` (`Loot.cpp:1003`);
   items at or above the group's threshold in `MASTER_LOOT` / `GROUP_LOOT` / `NEED_BEFORE_GREED` are
   marked `is_blocked` (`Loot.cpp:981`) so they cannot be taken directly.
2. **Right-click the corpse → loot window**. `CMSG_LOOT_UNIT` (`Opcodes.cpp:693`) →
   `HandleLootOpcode` (`Handlers/LootHandler.cpp:213`) → `Player::SendLoot` (`Entities/Player/Player.cpp:10019`),
   which sends `SMSG_LOOT_RESPONSE` built by `Loot::BuildLootResponse` (`Loot.cpp:1223`, packet
   `LootResponse` `Server/Packets/LootPackets.h:61`). Per-slot usability/owner/greed-only flags come from
   `LootItem::GetUiTypeForPlayer` (`Loot.cpp:212`). Automatic (AOE) looting of nearby corpses is the
   `AELootTargets` / `AELootTargetsAck` pair in the same handler (`LootHandler.cpp:244`).
3. **Rolls start when the first player opens the loot**. `Loot::OnLootOpened` (`Loot.cpp:857`) walks the
   blocked items and calls `LootRoll::TryToStart` (`Loot.cpp:468`), which fills the voter map, narrows the
   vote mask (no need if `ITEM_FLAG2_CAN_ONLY_ROLL_GREED` at
   `Loot.cpp:503`, no disenchant below the enchanter's skill at `Loot.cpp:505`) and pushes `SMSG_START_LOOT_ROLL` via `LootRoll::SendStartRoll` (`Loot.cpp:283`, packet
   `StartLootRoll` `LootPackets.h:221`). A single eligible looter skips the roll and the item drops under
   threshold (`Loot.cpp:513`).
4. **Need / greed / pass**. `CMSG_LOOT_ROLL` (`Opcodes.cpp:692`) → `HandleLootRoll`
   (`LootHandler.cpp:507`) → `Player::GetLootRoll` (`Player.cpp:9936`) → `LootRoll::PlayerVote`
   (`Loot.cpp:524`), which rebroadcasts `SMSG_LOOT_ROLL` (`LootRoll::SendRoll`, `Loot.cpp:346`).
5. **Resolve**. `LootRoll::UpdateRoll` (`Loot.cpp:568`) → `AllPlayerVoted` (`Loot.cpp:590`) →
   `Finish` (`Loot.cpp:684`): winner gets `SMSG_LOOT_ROLL_WON` (`Loot.cpp:386`) and everyone gets
   `SMSG_LOOT_ROLLS_COMPLETE`; nobody-voted ends in `SendAllPassed` (`Loot.cpp:323`). The 60 s
   timeout is driven from `Loot::Update` (`Loot.cpp:1264`), called per creature in
   `Creature::Update` (`Entities/Creature/Creature.cpp:848`).
6. **Take the item**. `CMSG_LOOT_ITEM` (`Opcodes.cpp:689`) → `HandleAutostoreLootItemOpcode`
   (`LootHandler.cpp:77`) → `Player::StoreLootItem` (`Player.cpp:28844`), which re-checks allowed looter,
   `is_blocked` and `rollWinnerGUID` before `StoreNewItem`. For an item-owned loot container the residue
   is persisted by `LootItemStorage::AddNewStoredLoot` (`Loot/LootItemStorage.cpp:243`) and reloaded by
   `LoadStoredLoot` (`LootItemStorage.cpp:122`).
7. **Take the money**. `CMSG_LOOT_MONEY` (`Opcodes.cpp:690`) → `HandleLootMoneyOpcode`
   (`LootHandler.cpp:142`): for `LOOT_CORPSE` with a group the pot is split between members within
   `IsAtGroupRewardDistance`, each gets `SMSG_LOOT_MONEY_NOTIFY`, then `Loot::LootMoney` (`Loot.cpp:1135`)
   zeroes it.
8. **Master loot**. Opening the corpse as master looter pushes the candidate list
   (`SMSG_MASTER_LOOT_CANDIDATE_LIST`, `Loot.cpp:891`). `CMSG_MASTER_LOOT_ITEM` (`Opcodes.cpp:704`) →
   `HandleLootMasterGiveOpcode` (`LootHandler.cpp:413`), which requires `GetMasterLooterGuid() == player`,
   `IsInRaidWith(target)` and `HasAllowedLooter(target)`, then hands the item over (and records it in the
   master-loot log via `Loot::LogGiven`, `Loot.cpp:852`).
9. **Close the window**. `CMSG_LOOT_RELEASE` (`Opcodes.cpp:691`) → `HandleLootReleaseOpcode`
   (`LootHandler.cpp:261`) → `WorldSession::DoLootRelease` (`LootHandler.cpp:269`) →
   `Player::SendLootRelease` (`Player.cpp:10006`).

## Notes / limits

- Loot methods are the enum in `Loot/Loot.h:88` (`FREE_FOR_ALL`, `ROUND_ROBIN`, `MASTER_LOOT`,
  `GROUP_LOOT`, `NEED_BEFORE_GREED`, `PERSONAL_LOOT`). Round-robin only rotates the looter
  (`Group::UpdateLooterGuid`); it does not create a separate item distribution path.
- Roll state is per-`Loot` (`_rolls`, `Loot.h:366`), i.e. in memory for the corpse's lifetime, not in a
  table; only unlooted container contents are persisted (`LootItemStorage`).
- Personal loot exists (`PERSONAL_LOOT`, `m_personalLoot`) but the 1.60 classic servers use the shared
  corpse path (`Unit.cpp:11923`), which is the one bots see.
