# LFG / Dungeon Finder — client flow vs core

Client build **1.60.1.70938** (Interface **16001**), 2026-11-17. Core: TrinityCore `6517af05`
(`forever/spike/TrinityCore`); every path below is relative to that clone root.

## Client side, step by step

1. **Open the Dungeon Finder (I)**. Client asks for the lock info it draws the dungeon list with:
   `CMSG_DF_GET_SYSTEM_INFO` (`Opcodes.cpp:436`) → `HandleDFGetSystemInfo` (`Handlers/LFGHandler.cpp:110`),
   which answers with `SendLfgPlayerLockInfo` (`LFGHandler.cpp:144`, per-dungeon lock reasons for one player)
   or `SendLfgPartyLockInfo` (`LFGHandler.cpp:214`) depending on the request flag.
2. **Tick dungeons + roles, hit "Find Group"**. `CMSG_DF_JOIN` (`Opcodes.cpp:437`) → `HandleLfgJoinOpcode`
   (`LFGHandler.cpp:31`): slots are masked with `0x00FFFFFF` to dungeon ids, then `LFGMgr::JoinLfg`
   (`DungeonFinding/LFGMgr.cpp:401`). `JoinLfg` runs the group/level/lock checks, opens a role check
   (`RoleChecksStore[gguid]`, `LFGMgr.cpp:561`) and pushes `SMSG_LFG_UPDATE_STATUS` via
   `SendLfgUpdateStatus` (`LFGHandler.cpp:249`, packet `LFGUpdateStatus`, `Server/Packets/LFGPackets.h:218`)
   plus `SMSG_LFG_ROLE_CHECK_UPDATE` via `SendLfgRoleCheckUpdate` (`LFGHandler.cpp:312`, packet
   `LFGRoleCheckUpdate`, `LFGPackets.h:267`).
3. **Role check window**. Every group member answers with `CMSG_DF_SET_ROLES` (`Opcodes.cpp:441`) →
   `HandleLfgSetRolesOpcode` (`LFGHandler.cpp:79`) → `LFGMgr::UpdateRoleCheck` (`LFGMgr.cpp:722`). A role
   check that nobody completes in time is expired and reported as `LFG_ROLECHECK_MISSING_ROLE` in
   `LFGMgr::Update` (`LFGMgr.cpp:296`).
4. **Queued**. Queue membership lives in `LFGQueue::AddToQueue` (`DungeonFinding/LFGQueue.cpp:128`);
   every server tick `LFGMgr::Update` calls `LFGQueue::FindGroups` (`LFGMgr.cpp:347` →
   `LFGQueue.cpp:293`). Average waits are pushed to the client from `LFGQueue::UpdateQueueTimers`
   (`LFGQueue.cpp:590`) through `SendLfgQueueStatus` (`LFGHandler.cpp:380`, packet `LFGQueueStatus`,
   `LFGPackets.h:298`, `SMSG_LFG_QUEUE_STATUS`).
5. **Proposal ("a group is ready")**. `LFGMgr::AddProposal` (`LFGMgr.cpp:1060`) stores it and
   `LFGMgr::Update` pushes `SMSG_LFG_PROPOSAL_UPDATE` through `SendLfgUpdateProposal`
   (`LFGHandler.cpp:466`, packet `LFGProposalUpdate`, `LFGPackets.h:385`). The client answers with
   `CMSG_DF_PROPOSAL_RESPONSE` (`Opcodes.cpp:439`) → `HandleLfgProposalResultOpcode`
   (`LFGHandler.cpp:72`) → `LFGMgr::UpdateProposal` (`LFGMgr.cpp:1104`); unanswered proposals are
   cancelled in `LFGMgr::Update` (`LFGMgr.cpp:318`).
6. **Group formed**. `LFGMgr::MakeNewGroup` (`LFGMgr.cpp:948`) builds the real group, sets
   `LFG_STATE_DUNGEON` and pushes another `SMSG_LFG_UPDATE_STATUS`.
7. **Teleport in / out**. `CMSG_DF_TELEPORT` (`LFGHandler.cpp:103` handler) → `LFGMgr::TeleportPlayer`
   (`LFGMgr.cpp:1409`); the manager also calls it itself when the group is formed.
8. **Dungeon completion**. The instance script reports the final encounter through
   `InstanceScript.cpp:977` → `LFGMgr::OnDungeonEncounterDone` (`LFGMgr.cpp:1501`) →
   `LFGMgr::FinishDungeon` (`LFGMgr.cpp:1524`), which sets `LFG_STATE_FINISHED_DUNGEON` and gives the
   completion reward **only** when the chosen dungeon is `LFG_TYPE_RANDOM` or seasonal.
9. **Leave**. `CMSG_DF_LEAVE` (`LFGHandler.cpp:60` handler) → `LFGMgr::LeaveLfg` (`LFGMgr.cpp:630`).
10. **Vote kick**. `CMSG_DF_BOOT_PLAYER_VOTE` (`Opcodes.cpp:433`) → handler (`LFGHandler.cpp:95`) → `LFGMgr::UpdateBoot`
    (`LFGMgr.cpp:1345`); the vote itself is opened by `LFGMgr::InitBoot` (`LFGMgr.cpp:1312`).

## Notes / limits

- Everything is gated on `LFGMgr::isOptionEnabled` (`LFGMgr.cpp:2161`) with
  `LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER`; with the options off the join
  handler returns before doing anything (`LFGHandler.cpp:33`).
- The client's "raid browser" is the same code path — there is no separate raid-browser manager.
- There is no `src/server/game/LFG` directory at this commit; the system is `DungeonFinding/` (this doc's
  `LFGHandler.cpp` is the only "LFG"-named file).
