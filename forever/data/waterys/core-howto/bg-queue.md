# Battleground queue — client flow vs core

Client build **1.60.1.70938** (Interface **16001**), 2026-11-17. Core: TrinityCore `6517af05`
(`forever/spike/TrinityCore`); paths are relative to that clone root.

## Client side, step by step

1. **Click the battlemaster**. `CMSG_BATTLEMASTER_HELLO` (`Opcodes.cpp:213`) → `HandleBattlemasterHelloOpcode`
   (`Handlers/BattleGroundHandler.cpp:46`): resolves the BG type for the NPC entry, level-gates with
   `Player::GetBGAccessByLevel`, then `BattlegroundMgr::SendBattlegroundList` (`Battlegrounds/BattlegroundMgr.cpp:504`)
   → `SMSG_BATTLEFIELD_LIST` (`Server/Packets/BattlegroundPackets.h:302`, opcode `Opcodes.cpp:1282`).
   The same list is refreshed by `CMSG_BATTLEFIELD_LIST` (`Opcodes.cpp:211` → handler `:328`).
2. **Press "Join Battle"**. `CMSG_BATTLEMASTER_JOIN` (`Opcodes.cpp:214`) → `HandleBattlemasterJoinOpcode`
   (`BattleGroundHandler.cpp:69`) → `WorldSession::JoinBattlegroundQueue` (`BattleGroundHandler.cpp:80`):
   queue id validated by `BattlegroundMgr::IsValidQueueId` (`BattlegroundMgr.cpp:581`), template looked up,
   **bracket** resolved with `DB2Manager::GetBattlegroundBracketByLevel` (`DataStores/DB2Stores.cpp:2975`), a
   group is checked by `Group::CanJoinBattlegroundQueue` (`Groups/Group.cpp:1190`), then
   `BattlegroundQueue::AddGroup` (`Battlegrounds/BattlegroundQueue.cpp:167`) stores the `GroupQueueInfo`
   and `BattlegroundMgr::ScheduleQueueUpdate` (`BattlegroundMgr.cpp:622`) marks the queue dirty.
   The client gets `SMSG_BATTLEFIELD_STATUS_QUEUED` built by `BuildBattlegroundStatusQueued`
   (`BattlegroundMgr.cpp:215`, opcode `Opcodes.cpp:1289`).
3. **Queue pops**. `BattlegroundMgr::Update` (`BattlegroundMgr.cpp:106`) flushes the scheduler into
   `BattlegroundQueue::BattlegroundQueueUpdate` (`BattlegroundQueue.cpp:850`), which finds a match with
   `CheckNormalMatch` (`:739`) (or `CheckPremadeMatch` `:681`), fills both sides in `FillPlayersToBG`
   (`:548`), creates or reuses the instance (`BattlegroundMgr::CreateNewBattleground`,
   `BattlegroundMgr.cpp:366`) and invites each group with `InviteGroupToBG` (`BattlegroundQueue.cpp:485`),
   which sets `IsInvitedToBGInstanceGUID` and its accept deadline (`:494`) and sends
   `SMSG_BATTLEFIELD_STATUS_NEED_CONFIRMATION` (`BattlegroundMgr.cpp:197`, opcode `Opcodes.cpp:1287`).
4. **Accept the invite**. `CMSG_BATTLEFIELD_PORT` (`Opcodes.cpp:212`) → `HandleBattleFieldPortOpcode`
   (`BattleGroundHandler.cpp:340`): it re-resolves which queue the player was really invited from, requires
   `ginfo.IsInvitedToBGInstanceGUID` (`:419`), then `Battleground::AddPlayer`
   (`Battlegrounds/Battleground.cpp:1127`) (creates the score entry, sends `SMSG_PVP_MATCH_INITIALIZE`,
   `Opcodes.cpp:2243`); `Battleground::StartBattleground` (`Battleground.cpp:1106`) starts the match once
   both sides have enough players. A declined/expired invite is removed by
   `BattlegroundQueue::RemovePlayer` (`BattlegroundQueue.cpp:315`).
5. **Status refresh**. `CMSG_REQUEST_BATTLEFIELD_STATUS` (`Opcodes.cpp:963`) →
   `HandleRequestBattlefieldStatusOpcode` (`BattleGroundHandler.cpp:588`), which rebuilds
   `SMSG_BATTLEFIELD_STATUS_QUEUED` / `_ACTIVE` / `_NONE` from the player's queue slots.
6. **Leave / desert**. `CMSG_BATTLEFIELD_LEAVE` (`Opcodes.cpp:210`) → `HandleBattlefieldLeaveOpcode`
   (`BattleGroundHandler.cpp:556`) → `Player::LeaveBattleground`; the match side is cleaned up in
   `Battleground::RemovePlayerAtLeave` (`Battleground.cpp:950`).
7. **Finish and rewards**. `Battleground::EndBattleground` (`Battleground.cpp:725`): sets
   `STATUS_WAIT_LEAVE`, sends `SMSG_PVP_MATCH_COMPLETE` (`Battleground.cpp:801`, opcode `Opcodes.cpp:2242`),
   then per player: resurrect/cleanup (`Battleground.cpp:821`), statistics row (`:837`), the classic mark-of-honor spell
   (win 3 / lose 1, table at `Battleground.cpp:779`), the classic PvP-rank points (`Battleground.cpp:796`) and the
   honor configs (`Battleground.cpp:832`). Team-wide variants are `RewardHonorToTeam` (`Battleground.cpp:691`)
   and `RewardReputationToTeam` (`:698`).

## Notes / limits

- One queue object per `BattlegroundQueueTypeId`; `BattlegroundMgr::GetBattlegroundQueue`
  (`Battlegrounds/BattlegroundMgr.h:127`) lazily creates them, so every bracket of a BG type is a separate
  map keyed by queue id + bracket id.
- Bracket selection is client-data driven (`PVPDifficultyEntry`); a level with no bracket entry cannot
  queue at all (`BattleGroundHandler.cpp:127` is the "no level bracket" refusal).
- There is no reward mail in this flow: rewards are spells/currency applied at `EndBattleground`
  (`Battleground.cpp:725`), so a bot only needs to be in `m_Players` when the match ends.
- Handler-level bot special cases exist (a human leader drops an out-of-bracket bot group, and a human
  leaving takes his group's bots out with him, `BattleGroundHandler.cpp:150` / `:569`); they are core code,
  not part of the wire protocol.
