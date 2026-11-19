# Slice 4, package P4 (engine port): notes

Branch `slice4/p` (worktree `~/wt-slice4-p`), build slot 1 (`FOREVER_SLOT=1`). Spec `FOREVER-SLICE4-SPEC.md` sections 2, 3, 4 and the D4 round of 7, brief `SLICE4-P-BRIEF.md`.
Core `~/ForeverCore` read only at the pin 6517af0 (no change). T = Turtle, F = ForeverCore. Not pushed. No live boot (the test server is not used in this package).
The package owns `ai/`, `forever/src/compat`, `forever/src/slice`, `forever/SLICE4.txt`, `forever/tools`, this file and `UNPORTED.md`; nothing of H4's (`forever/src/host`, commands) was touched.

## 1. Census

`FOREVER_SLOT=1 FOREVER_CENSUS_LIST=forever/SLICE4.txt forever/tools/vm-census.sh "" 10`

| State | Summary line |
|---|---|
| Baseline N0 (list = SLICE3 + 4 files, nothing ported yet) | `files 174  compile clean 171  with errors 3  error places 43 (in 3 files)  includes of a header ForeverCore does not have 0` |
| After the 43 places were closed | `files 174  compile clean 174  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` |

`SLICE4.txt` holds 174 files, not the 177 of the spec (the spec counted the 173 LINES of SLICE3.txt, two of them comments, plus four). Baseline `where.tsv`: `ReleaseSpiritAction.h` 39, `PartyMemberToResurrect.cpp` 3, `AcceptResurrectAction.h` 1 (the spec estimated 16-36; the header is compiled whole, `RepopAction`, `CorpseRunAction` and `SelfResurrectAction` included, and its 24 `std::string` printf places are most of the count). `DeadStrategy.cpp` and `ReleaseSpiritAction.cpp` (two lines) had no place. `ReviveFromCorpseAction.cpp` is NOT in the list (1,015 lines); only its header is read.

## 2. The 43 places

| Places | What | Class | Fix |
|---|---|---|---|
| 24 (ReleaseSpiritAction.h) | `sLog.outDetail("... %s", bot->GetName())` (F: `std::string`, a non-POD vararg) | (b) existing helper | `CStrCompat(bot->GetName())`, the helper the slice already uses (TbCompat_Unit.h:352) |
| 5 (ReleaseSpiritAction.h:33, :139, :141, PartyMemberToResurrect.cpp:36, one more flag read) | `HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST)` | (b) existing helper | `HasPlayerFlagCompat(obj, PLAYER_FLAGS_GHOST)` (TbCompat_Unit.h:434; T has it in the R3 header) |
| 4 (ReleaseSpiritAction.h:47-49, :113-115, AcceptResurrectAction.h:25 and the typed packets) | `HandleRepopRequestOpcode(packet)`, `HandleResurrectResponseOpcode(packet)` | (b) new | `RepopRequestCompat(bot)`, `AcceptResurrectCompat(bot, offerer)` (section 3) |
| 2 (ReleaseSpiritAction.h:55, :121) | `bot->CastSpell(bot, 2584, TRIGGERED_OLD_TRIGGERED)` (Waiting to Resurrect, battleground only) | (d) not ported | `WaitingForResurrectAuraCompat(bot)`; F counts the new feature `battleground release aura` and casts nothing |
| 1 (PartyMemberToResurrect.cpp:43) | `IsRessurectRequested()` (T spelling) | (b) new | `IsResurrectRequestedCompat(player)` |
| 1 (ReleaseSpiritAction.h:238) | `group->RemoveMember(guid, 0)` (F: `RemoveMethod`) | (b) new | `GroupRemoveMemberCompat(group, guid)` |
| 5 (:272, :280, :283 x4, :291) | `GetClosestGraveYard(x,y,z,map,team)` and `grave->map_id/x/y/z` | (b) new + existing | `ClosestGraveyardCompat(bot)` and `WorldPosition(grave)`'s accessors (the constructor already existed) |
| 6 (:295, :301 x5) | `RACE_HIGH_ELF`, `PlayerInfo::mapId/positionX/Y/Z/orientation` | (b) new + existing | `RepopUsesHomebindCompat(bot)`; `WorldPosition(playerInfo)` (the constructor existed: `LocationOfCompat(PlayerInfo const*)`) |
| 3 (:303, :309, :310) | `SendHeartBeat()`, `TeleportToHomebind()` | (b) existing + new | `SendHeartBeatCompat` (existed, counts `movement interrupt and heartbeat`), `TeleportToHomebindCompat(bot)` |
| 2 (:339, :345) | `HandleSelfResOpcode`, `PLAYER_SELF_RES_SPELL` | (d) not ported | `SelfResurrectCompat`, `SelfResurrectPossibleCompat`; F counts the new feature `self resurrection` and answers: not possible, nothing sent |

(The counts add up to the 43 of the census with the split above by line; two lines hold two places each.) `RepopAction`, `CorpseRunAction`, `SelfResurrectAction` are compiled but NOT registered: nothing builds them.

## 3. Helpers, one by one: F side, T side (the original expression), proof

All in `forever/src/compat/TbCompat_D4.h` (F) and `ai/cmangos-compat-d4.h` (T, included from `ai/cmangos-compat-shim.h` after `-s3.h`; F from `forever-compat-shim.h` after `TbCompat_S3.h`). The proof that every T body is the original text is `forever/tools/test_slice4_d4_wiring.py` (reads both headers and the three files; 10 tests, run on the VM tree) plus the Turtle `vm-build` and `vm-test tb` gates.

| Helper | T side = original | F side (core line at the pin) |
|---|---|---|
| `RepopRequestCompat(bot)` -> bool | `WorldPacket packet(CMSG_REPOP_REQUEST); packet << uint8(0); GetSession()->HandleRepopRequestOpcode(packet);` then `true` (the original never looked) | typed `WorldPackets::Misc::RepopRequest` -> `WorldSession::HandleRepopRequest` (MiscHandler.cpp:63-93). The handler returns silently when alive or already a ghost (:65), when being teleported (:71) and with SPELL_AURA_PREVENT_RESURRECTION (:74): the answer is what the player is afterwards (ghost flag, from the Ghost aura 8326 cast in `BuildPlayerRepop`, Player.cpp:4688, or alive again), so a release the core ignored is not reported (Execute returns false) |
| `AcceptResurrectCompat(bot, offerer)` | `guid + uint8(1)`, `HandleResurrectResponseOpcode`, `true` | typed `ResurrectResponse{Resurrecter, Response=0}` -> `HandleResurrectResponse` (MiscHandler.cpp:507-534). **POLARITY: F Response 0 = accept, T 1 = accept.** Returns false (nothing done) when the bot is alive or has no pending request FROM that offerer (`IsResurrectRequestedBy`, Player.h:2158), so an acceptance is never reported that the core did not take |
| `DeclineResurrectCompat` | the same packet with `uint8(0)` | `Response = 1` (the handler clears the request, :511-515). Only F reaches it |
| `ResurrectOffererCompat(event)` | `WorldPacket p(event.GetPacket()); p.rpos(0); ObjectGuid guid; p >> guid;` | `event.getObject()` (Event.cpp:9: the first guid of the event's packet, empty with no packet). The host raises `Event("resurrect request", offererGuid)` (Event.h writes the guid as the packet) |
| `ResurrectOffererAcceptableCompat(ai, offerer)` | `true` (T accepts every request) | true only for a member of the bot's group or the bot's master (the commander); declined otherwise (spec fallback; less than T) |
| `IsResurrectRequestedCompat(player)` | `IsRessurectRequested()` (Player.h:2543, const) | `IsResurrectRequested()` (Player.h:2166) |
| `InstanceEntranceKnownCompat(bot)` | `true` | dungeon/raid map: the instance script's `GetEntranceLocation()` names a real `world_safe_locs` row (`GetWorldSafeLoc`, ObjectMgr.h:1199), else false: `auto release` waits (section 4). Other maps: true. A read |
| `WaitingForResurrectAuraCompat(bot)` | `CastSpell(bot, 2584, TRIGGERED_OLD_TRIGGERED)` | counts `battleground release aura`, casts nothing (the core puts the aura on a player when its spirit guide is chosen, Player.cpp:33468) |
| `SelfResurrectPossibleCompat`, `SelfResurrectCompat` | `GetUInt32Value(PLAYER_SELF_RES_SPELL) != 0`; `CMSG_SELF_RES` + `HandleSelfResOpcode` | counts `self resurrection`; false |
| `GroupRemoveMemberCompat` | `RemoveMember(guid, 0)` | `RemoveMember(guid, GROUP_REMOVEMETHOD_DEFAULT)` (Group.h:264) |
| `ClosestGraveyardCompat(bot)` | `sObjectMgr.GetClosestGraveYard(x, y, z, map, team)` | `ObjectMgr::GetClosestGraveyard(*bot, team, bot)` (ObjectMgr.cpp:7282, conditions and team honoured). A read |
| `RepopUsesHomebindCompat(bot)` | `race == RACE_GOBLIN \|\| race == RACE_HIGH_ELF` | false (the two custom starts do not exist on F) |
| `TeleportToHomebindCompat(bot)` | `TeleportToHomebind()` | `TeleportTo(m_homebind)` (the core's own target, Player.cpp:5264) |

Reused, no second definition: `HasPlayerFlagCompat`, `CStrCompat`, `SendHeartBeatCompat`, `WorldPosition(WorldSafeLocsEntry*/PlayerInfo*)`.

`ReleaseSpiritAction::Execute` / `AutoReleaseSpiritAction::Execute` now `return false` when `RepopRequestCompat` says the core did nothing (T: the helper is always true, so the original always-true path is unchanged), `AcceptResurrectAction::Execute` returns what `AcceptResurrectCompat` says. `AutoReleaseSpiritAction::ReleaseHoldReason()` (new, `none|rezzer|combat|wipe|no-entrance`) reads the same predicates as `isUseful()` for the host's `relhold` key.

## 4. Registry keys and the dead engine

`slice_gen_context.py` (`SLICE4_KEYS`) now registers `slice: registered 17/48/75/170` (slice 3: 16/44/73/169): +1 strategy, +4 actions, +2 triggers, +1 value. `--check` current; `slice_registry_check.py`: `(a)=0  (b)=215` (slice 3: 207; its list is now `SLICE4.txt`). Creators are the full build's text byte for byte (the check proves it):

| Kind | Key | Context class | Creator at |
|---|---|---|---|
| strategy | `dead` | StrategyContext | `ai/playerbot/strategy/StrategyContext.h:103` (`new DeadStrategy(ai)`) |
| action | `auto release` | WorldPacketActionContext | `strategy/actions/WorldPacketActionContext.h:58` (`AutoReleaseSpiritAction`) |
| action | `accept resurrect` | WorldPacketActionContext | `WorldPacketActionContext.h:59` (`AcceptResurrectAction`) |
| action | `release` | ChatActionContext | `strategy/actions/ChatActionContext.h:116` (`ReleaseSpiritAction`: the order verb) |
| action | `reach party member to resurrect` | ActionContext | `strategy/actions/ActionContext.h:152` (`ReachPartyMemberToResurrectAction`, ReachTargetActions.h:1150) |
| trigger | `party member dead` | TriggerContext | `strategy/triggers/TriggerContext.h:175` (`PartyMemberDeadTrigger`, HealthTriggers.h:120, target `party member to resurrect`) |
| trigger | `resurrect` | TriggerContext | `TriggerContext.h:122` (`ResurrectTrigger`, BotStateTriggers.h:27: the reaction engine's `resurrect` -> `set non combat state`, ClassStrategy.cpp:53-55) |
| value | `party member to resurrect` | ValueContext | `strategy/values/ValueContext.h:171` |

Already registered (checked): values `dead`, `death count`; trigger `death`, `very often`; actions `set dead state`, `set non combat state`. Come with the priest's own context, compiled as it is: `resurrection` (`PriestAiObjectContext.cpp:279`), `remove shadowform` (:252). Their strategies: `PriestStrategy.cpp:181-182` / `PriestNonCombatStrategy.cpp:26-28` fire `resurrection` on `party member dead` in the NON-combat engine only (the priest raises after the fight).

`AiFactory::AddDefaultDeadStrategies` (SliceAiFactory.cpp) adds `dead` to the dead engine (the full factory adds `dead stay default follow group`, AiFactory.cpp:1776; the rest is the corpse run).

**Deliberately unregistered (each does less; the missing-key guard logs each once as `slice: missing ...`, these are the lines the live test explains):** actions `repop` (DeadStrategy.cpp:19, :53, :57), `self resurrect` (:23), `spirit healer` (:27), `find corpse` (:31), `revive from corpse` (:45), `bg reset objective force` (:41); triggers `dead` (:30, :40), `corpse near` (:44), `resurrect request` (:48: the host calls the action), `falling far` (:52), `move long stuck` (:56), `val` (the `val::gt32::{death count,N}` and `val::should spirit healer` nodes of DeadStrategy.cpp:18, :26: the `val` trigger is not registered, so those two nodes never fire); from WorldPacketHandlerStrategy.cpp (already in slice 3): `area trigger`, `reach area trigger`, `within area trigger`, `use meeting stone`, `summon`. The new `(b)` entries are these (8 more than slice 3).

## 5. Resurrection and release on ForeverCore (the spec's FIRST risk), read from the core

**How a session with no client accepts a resurrection.** A caster's Resurrection spell lands in `Spell::EffectResurrect` (SpellEffects.cpp:3807-3833): for a player that is dead and has no pending request it stores the request (`Player::SetResurrectRequestData`, Player.cpp:25470: it ASSERTs that none is pending; the effect checks first, :3825) and sends `SMSG_RESURRECT_REQUEST` through the player's session (`Spell::SendResurrectRequest`, Spell.cpp:5554). A headless session drops that packet, so **nothing ever answers it**: the request stays stored (there is no server timer; the real client times out and sends Response 2), and while it is stored the target is excluded by `PartyMemberToResurrect` (`IsResurrectRequestedCompat`) and `EffectResurrect` returns for a second caster. Hence the host must capture the packet in `BotSession::SendPacket` and either accept at once or decline (a request must never be left to sit): see "Needs from H4".
The accept is `WorldSession::HandleResurrectResponse` (MiscHandler.cpp:507-534). Response 0 = accept (the legacy action writes 1: the polarity table in section 3). It requires `IsResurrectRequestedBy(offerer)` (:517), looks at the combat-resurrection charges only when `Instance.CombatResurrectionLimit` is on (default false, World.cpp:643) and an encounter is running, then `ResurrectUsingRequestData` (Player.cpp:28079): `TeleportTo(caster's position at cast time)`; if the teleport leaves the player "being teleported" the resurrect is delayed (`DELAYED_RESURRECT_PLAYER`, :28086) and run by `ProcessDelayedOperations` (:1566) when the teleport ack arrives (MovementHandler.cpp:479; the host session acks near teleports itself, TbBotSession.cpp AcknowledgeTeleports); otherwise it runs at once (:28090): `ResurrectPlayer(0.0f)`, health and mana from the request, rage 0, `SpawnCorpseBones`. So the bot is alive at the OFFERER's position, with the spell's health and mana. All of it runs on the world thread (the handler is THREADUNSAFE, Opcodes.cpp:1002).
**Release.** `HandleRepopRequest` (MiscHandler.cpp:63-93; THREADUNSAFE, Opcodes.cpp:957): `BuildPlayerRepop` (Player.cpp:4679-4736: Ghost aura, corpse created and added, `setDeathState(DEAD)`, health 1) then `RepopAtGraveyard` (:5211-5262). In a non-raid dungeon or raid `RepopAtGraveyard` revives the player: bones at once (:5223), the teleport to the instance script's entrance location (:5230) or, with none, the nearest graveyard of the zone, with `TELE_REVIVE_AT_TELEPORT` (:5254); on the same map the revive happens inside `TeleportTo` (Player.cpp:1396, 50 % health), on another map when the worldport lands (MovementHandler.cpp:333). No release timer inside an instance (Player.cpp:1149). So a release in RFC revives the bot at once, either at the entrance (host set it) or in Durotar (not set): this is why `InstanceEntranceKnownCompat` holds `auto release` until the entrance exists.
**Corpse reclaim (open world, the case the slice does not drive).** `HandleReclaimCorpse` (MiscHandler.cpp:472-505): needs the ghost flag, a corpse, `corpse->GetGhostTime() + reclaim delay (30/60/120 s)` passed, `CORPSE_RECLAIM_RADIUS` 39 yd (Corpse.h:38), then `ResurrectPlayer(0.5)` and bones. Nothing in the slice calls it (`find corpse` and `revive from corpse` are unregistered): an open-world release leaves a ghost at the graveyard until `.tb revive`.
**What the legacy dead strategy now drives on F:** `auto release` (`AutoReleaseSpiritAction`) -> `RepopRequestCompat` -> the core's handler; `accept resurrect` -> `AcceptResurrectCompat` -> the core's handler. No call of the core's own entry checks is made (C1).

## 6. Stand-ins and unported rows

Made real: the dead engine's strategy (`dead`); release and acceptance; `party member to resurrect` and `party member dead` for the priest (the priest's raise now fires). The unported row `incoming: release spirit and resurrect` is renamed `incoming: reclaim corpse` (`UnportedFeatures.h`, `TbCompat_Unported.cpp` keeps its handler names; `UNPORTED.md` row rewritten). New rows (counts: 76 features in the boot line): `battleground release aura`, `self resurrection`. Kept doing less (unchanged): `instance bind`, `incoming: gossip, game object and area trigger`, `SummonAction::FindSpotBeside` (SliceLinkStubs.cpp:275), `PlayerConvenience::NoteHoldOrder` (:505). `RepopAction` must not be built (it would instantiate `SpiritHealerAction`'s members, defined in the uncompiled `ReviveFromCorpseAction.cpp`): it is unregistered. The guild-order store's `NoteDeath` (host stand-in, TbHostOwnership.cpp:140) is a no-op that `OnDeath` calls for a bot with no active player master: nothing to do.

`GetActivity` readers in the SLICE4 set (R0 item, polarity when the lease answers `Dungeon`): `ReleaseSpiritAction.h` `Decide` (:151: the intended crew rule), the `[AUTO-RELEASE]` log (:110, read only), `RepopAction` (:232, unregistered); `PlayerbotAI.cpp:3367` `dungeonCrew`: skips the master strip (:3393), the hired restore (:3383) and the "test BG master set" block (:3418: leadership hand-over and master adoption): all three are SKIPPED, i.e. LESS (the first and third were already skipped for a commanded bot by `commanded` / `CommandedBotKeepsMasterCompat`). No other reader is in the set (`InviteToGroupAction`, `LeaveGroupAction`, the guild actions are not compiled).

## 7. Needs from H4

1. `BotActivityLeaseManager::GetActivity` (TbHostOwnership.cpp:61) answers `Dungeon` for a commanded bot (spec section 6), so `Decide` applies the crew rule on instance maps.
2. Capture `SMSG_RESURRECT_REQUEST` in `BotSession::SendPacket`; on the world thread call `ai->DoSpecificAction("accept resurrect", Event("resurrect request", offererGuid), true)` (the offerer's guid is the event's packet). A request must be answered in the same tick or the next (never left pending, section 5): when the action returns false, decline through `DeclineResurrectCompat(bot, offerer)`.
3. Set the dungeon's entrance (`InstanceScript::SetEntranceLocation(GetMapEntranceTrigger(map)->PortLocID)`) before any commanded bot can release; until it is set `auto release` waits and `ReleaseHoldReason()` says `no-entrance`.
4. For `relhold`: `dynamic_cast<ai::AutoReleaseSpiritAction*>(ai->GetAiObjectContext()->GetAction("auto release"))->ReleaseHoldReason()` (`none`, `rezzer`, `combat`, `wipe`, `no-entrance`; the key's `rezzer`/`combat`/`wipe` are exactly the predicates the release rule reads).
5. The engine tick must not run for a bot in the middle of the accept's teleport (`UpdateAIInternal` already returns while `IsBeingTeleported`), and the S3 `HandleTeleportAck` call is still the host's.

## 8. Option-ON link
CMakeLists.txt (not mine) still reads `forever/SLICE3.txt` (line 73): the merge package must switch it to `SLICE4.txt`; until then an option-ON build that has this branch's `SliceAiObjectContext.cpp` fails to link (`DeadStrategy` and friends are not compiled). Measured with the list switched on the VM copy only (slot 1, `-DTORTOISEBOTS_SLICE1=1`, `-k 0`): `CMAKE_RC=0 MAKE_RC=0`, `worldserver` links, 0 `undefined`. So **nothing is expected to be undefined at the merge** from this package: the only needed edit is that one CMake line (and the wide-pass merge below changed nothing in it).

## 8b. Fit with H4 (read from `~/wt-slice4-h/forever/docs/SLICE4-H-NOTES.md` section 3 and its sources, read only)

* The host calls `release` and `accept resurrect` BY KEY (`GetAction` first): both registered (section 4). H4 does not capture the packet: it polls `IsResurrectRequested()` and builds `Event("resurrect request", offerer->GetGUID(), bot)` (TbOrders.cpp:435); `ResurrectOffererCompat` reads the event's FIRST guid, so it matches. Its pre-filter (party member or commander) is the same rule as `ResurrectOffererAcceptableCompat`; a request from anyone else is declined by the action, which clears it.
* `AcceptResurrectCompat` writes the CORE's accept (Response 0); `RepopRequestCompat` -> `HandleRepopRequest`.
* Lease: H4 answers `PlayerMaster` while no entrance is known and registers a run-hold query that makes `Decide` answer Wait, otherwise `Dungeon`. P4 did not change `RunReleaseHold::Decide` or any `GetActivity` reader. DIFFERENCE (harmless, stricter): P4's own `InstanceEntranceKnownCompat` also requires the entrance id to name a real `world_safe_locs` row (`GetWorldSafeLoc`), H4's `ReleaseHasEntrance` only `!= 0`; the host sets a trigger's real `PortLocID`, so they agree; if they ever differ the bot waits (less). `relhold` is computed by the host from core facts; `AutoReleaseSpiritAction::ReleaseHoldReason()` (this package) says the same from the engine's side and is available if the host wants to read the engine's own answer.
* NOT done (not in the brief): an engine-side leash rule (do not chase a fleeing mob into the next pack). The host exposes `pos=` and `flee=`; a rule would need a both-core helper under `forever/src/compat` for the pack facts (`Creature::HasSearchedAssistance`, `UNIT_STATE_FLEEING`, `ASSISTANCE_MOTION_TYPE`) and a change to the legacy runner/leash policy: left for the live test (observation l) to show whether it is needed.

## 9. Merge of `tbf/port/forever` (the wide pass) and gates
Merged `tbf/port/forever` (3b9138052) into `slice4/p` before the final gates. Two conflicted files, both resolved to THIS package's version (the slice-4 port of the same lines; where the wide pass had made the same helper substitution the auto-merge kept one: `HasPlayerFlagCompat` and `CStrCompat` in `ReleaseSpiritAction.h` and `PartyMemberToResurrect.cpp`): `ai/playerbot/strategy/actions/ReleaseSpiritAction.h` (kept `GroupRemoveMemberCompat`, `graveMapId` / `gravePos` for the graveyard teleport, the entrance gate) and `tools/test_run_release_hold_wiring.py` (a comment only). No helper name of the wide pass collides with a D4 name (checked by grep); `slice_gen_context.py --check` and the registry check unchanged by the merge.

| Gate (final merged tree, sha in the report) | Result |
|---|---|
| census SLICE4 | `files 174  compile clean 174  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (baseline: 171 / 3 / 43) |
| census SLICE3 / SLICE2 / SLICE1 | `files 170 / 160 / 133  compile clean = files  error places 0` |
| generator `--check`, registry check | `SliceAiObjectContext.cpp is up to date`; `(a)=0  (b)=215`; `slice: registered 17/48/75/170` |
| module build, option OFF (slot 1) | `CMAKE_RC=0 MAKE_RC=0`; 7 objects `TbBotMgr TbBotSession TbScripts TbItemPrototype TbSpellEntry TbEngineBrain TbH3Off` (this branch has no H4 files) |
| option ON `worldserver` (slot 1, list switched on the VM copy) | `CMAKE_RC=0 MAKE_RC=0`, links, 0 undefined |
| `forever/tools/test_slice4_d4_wiring.py`, the two re-pinned tests | 10 tests OK; `test_run_release_hold_wiring.py` and `test_dungeon_crew_wiring.py` OK (two pins re-spelled to the helper names, no assertion removed) |
| Turtle `vm-build` | `COMPILE_SECONDS=1186 CMAKE_RC=0 MAKE_RC=0` |
| Turtle `vm-test tb` (sha 356b99309, the merged tree; later commits are notes only) | `RESULT: PASS ... rc=0` (951 s) |
