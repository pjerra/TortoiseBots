# Slice 4, package H4 (host): entering a dungeon, the entrance point, life state, instance commands

Branch `slice4/h` (worktree `~/wt-slice4-h`), build slot 2 (`FOREVER_SLOT=2`), branched from the port branch (slice 3 merged). Core read only at the pin 6517af05 (zero core changes). No server was started or touched, nothing pushed, no SQL applied.
Briefs: `SLICE4-COMMON.md` (C1-C5, Q1-Q5), `SLICE4-H-BRIEF.md`; spec `FOREVER-SLICE4-SPEC.md` with its "Lead decisions". The other package (P4, `~/wt-slice4-p`) owns `ai/`, `forever/src/compat`, `forever/src/slice`.

## 1. Files

| File | New | What |
|---|---|---|
| `forever/src/host/TbH4.h`, `TbH4.cpp` | yes | `.tb trigger`, `.tb enter` (new form), `.tb instance`, `.tb tele`, `.tb revive`, `.tb kill`, `.tb gm`, and `SelfTestRecords` (one example of every record kind and key). Core headers only; compiled in both builds (the option-OFF build gets the `engine-not-built` answers from the same file) |
| `forever/src/TbBotMgr.h/.cpp` | | the entry job (`StartEntry`, `UpdateEntry`), `CheckEntry`, the entrance rule (`EnsureEntrance`), life tracking (`TrackLife`), landing notes (`NoteTeleportStarted`), D4 in `Think` |
| `forever/src/host/TbHostOwnership.cpp` | | the lease answer (`GetActivity`) and the run-hold query |
| `forever/src/host/TbOrders.cpp` | | the order verb `release`; the resurrection answer (`ResurrectOffererOf`, `BeforeEngineTick`) |
| `forever/src/host/TbPartyState.cpp` | | state keys (Q1), end-line counts, threat keys (Q2) |
| `forever/src/host/TbH3.cpp`, `TbH3.h` | | `.tb grammar selftest` calls `h4::SelfTestRecords`; `ResurrectOffererOf` declared |
| `forever/src/TbScripts.cpp` | | six new rows; `enter` dispatches the old two-word form (character + map NUMBER) to the old code, everything else to `h4::HandleEnter` |
| `forever/sql/world/2026_11_19_00_warrior_trainer_spells.sql`, `forever/sql/README.md` | yes | the data collaborator's trainer fix (108 `INSERT IGNORE` rows, 27 trainers) and how a test server applies it BY HAND (the module has no world SQL, the core's updater does not read a module's folder). Not applied anywhere |
| `forever/docs/TB-COMMANDS.md` | | section 8, the state/threat/order tables, the new error codes |

Nothing outside `forever/` changed: the Turtle gates (`vm-build`, `vm-test tb`) are not needed. `forever/src/slice`, `forever/src/compat` untouched.

## 2. The items

### 1. Enter as a group through the real entrance (`.tb enter`)
`BotMgr::StartEntry` (TbBotMgr.cpp:731) validates (trigger row, its `areatrigger_teleport` target, bot on the trigger's map, alive, not in combat, `CheckEntry` for the target map) and starts a per-bot job; nothing is started or changed on a refusal. `UpdateEntry` (:780) runs every tick:
the engine brain is dropped and not rebuilt while the job runs (`Think` returns early); the bot walks into the box (`MovePoint`, 20 s without progress = `block=stuck`, 120 s = `timeout`); a follower waits until the party leader stands on the target map (leader = the group leader when it is one of the addressed host bots: C5, the tank bot);
then the guarded entry question, then **the core's own handler**: `WorldSession::HandleAreaTriggerOpcode` (MiscHandler.cpp:538-753) with the typed packet `{AreaTriggerID, Entered=true}` (a public member; the bot session is a WorldSession). The handler checks the box (`IsInAreaTrigger`, Player.cpp:2031), the corpse rule, calls `Map::PlayerCannotEnter` (:694) and
teleports to `GetInstanceEntrance` or the trigger's target (:748-753). All five land in the SAME instance because a group's bot goes to the group's recent instance (`MapManager::CreateMap` :300-319), which the leader opened. The outcome is read from the world: landed on the target map = `entered=trigger` (+ the trigger is remembered for the instance);
a teleport that ended elsewhere = `block=teleport-refused`; a handler that did not teleport = `block=other`. `direct` is the D1 fallback (no walk, no handler, the same checks, then `Player::TeleportTo`).
Why the entry question is asked first: the entry check's refusal (level, full, lock...) is known with its words before the handler runs, so the state record says why (`block=level`, `blockmsg=level too low ... needs 10`); the handler's own `SMSG_TRANSFER_ABORTED` is therefore not captured (the same answer, one packet fewer).
`outside=yes` in the state record = the leader is in an instance and the bot is not.
**C1 kept**: `grep PlayerCannotEnter forever/src` finds ONE call (TbBotMgr.cpp:141, inside `CheckEntry`; `CanEnterMap` wraps it). `CheckEntry` answers `already-on-map` for a bot on the target map id, `teleporting`, not in world, so the core's abort (`InstanceMap::CannotEnter`, Map.cpp:2965-2971) cannot be reached from the host. The two core calls that ask the entry question themselves (`HandleAreaTriggerOpcode` MiscHandler.cpp:694, `Player::TeleportTo` far path Player.cpp:1432) are only reached after that guard has said no objection for a bot that is not on that map.

### 2. Recovery: the entrance point (D2)
**Finding: it CAN be done with module code alone.** `InstanceScript::SetEntranceLocation(worldSafeLocId)` is public (InstanceScript.h:289, :819); `InstanceMap::GetInstanceScript()` is public (Map.h:908). `Player::RepopAtGraveyard` (Player.cpp:5211-5262) revives a released player at once in every dungeon (:5220), at `GetEntranceLocation()` of the instance script when it names one (:5227-5228),
else at the closest graveyard (for Ragefire Chasm and an Alliance bot: Razor Hill, outside). `BotMgr::EnsureEntrance` (TbBotMgr.cpp:953), run every 500 ms for every host bot on an instance map, gives the instance the safe loc of the trigger the first entry used (`_entryTriggerByInstance`), else of `ObjectMgr::GetMapEntranceTrigger(map)` (1102230 for map 389: 0.80, -8.23, -15.53), once per instance, logged.
Limits: (a) an instance with NO script (`InstanceMap::CreateInstanceData` makes one only for a map with an `instance_template` row, Map.cpp:3134-3146) or with no trigger leading into the map gets no entrance: the release is HELD instead (below), never a graveyard revive; (b) a dungeon with several entrances uses the trigger the entry used, else the first one `GetMapEntranceTrigger` returns (an unordered map): fine for Ragefire Chasm (one entrance).
**The release rule** (D5, the legacy dungeon crew): `BotActivityLeaseManager::GetActivity` (TbHostOwnership.cpp) answers `Dungeon` for a commanded bot, so `RunReleaseHold::Decide` (runtime/RunReleaseHold.h:57-64) holds a dead bot while a living priest/paladin/shaman/druid is on its map or a member fights, and releases on a wipe.
With no entrance on the instance it answers `PlayerMaster` and the run-hold query (`RunReleaseHold::Register`) says "held" for the dead bot: `Decide` returns Wait (`relhold=no-entrance`); `.tb order <bot> release` is refused with `no-entrance`.
**R0 list of every `GetActivity` reader** (all in the slice's sources), polarity of a `Dungeon` answer: PlayerbotAI.cpp:3367 (a Dungeon bot keeps its master state: no strip, no adoption: a commanded bot already does), LeaveGroupAction.cpp:88 (`FarAwayLeaveAllowed`: never leaves), InviteToGroupAction.cpp:14 (no random invites), GuildManagementActions.cpp:156 and GuildCreateActions.cpp/.h (guild errands only for Idle/Grinding/PlayerMaster: a Dungeon bot does none), ReleaseSpiritAction.h:110/151/232 (the crew rule above; `RepopLeavesGroup`: `repop` is not registered). Every reader makes the bot do LESS under Dungeon. `PlayerMaster` (the no-entrance case) is read by the guild gates only (actions the slice does not register).
**Wipe**: every commanded bot's dead engine releases by `auto release` (engine side, P4), the core revives it at the entrance inside the instance, `life=running-back` until within 20 yd of the commander (leader), and `state-end` reports `alive= dead= ghost= wipe= whole=` (whole = everyone alive, on the leader's map, within 40 yd).
**Resurrection**: a dead bot whose party member (or commander) offered a resurrection answers it at once: `BeforeEngineTick` calls the action `accept resurrect` with the offerer's guid as the event's object (`Event(source, ObjectGuid, owner)`, the shape the legacy action reads) at most once per second; the pending offer is found with the public `Player::IsResurrectRequestedBy` (Player.h:2158) on the world thread (the spec's capture of `SMSG_RESURRECT_REQUEST` in the session would give the same fact through a packet parse). A stranger's offer is not answered (does less than the legacy action, which accepts any). `.tb state` shows `resreq=`.
`.tb revive` marks `practice=yes` until the next combat ends (`TrackLife`), and every reply line and a log line carry PRACTICE.

### 3. Commands
`.tb instance <leader> info|reset|leave` (Q3), `.tb tele` (Q4), `.tb trigger`, `.tb revive`, `.tb kill`, `.tb gm`, order verb `release`: TB-COMMANDS.md section 8 has each reply. All tokened (a bad token is refused first by `RequireBuilt`), all through `Enc/Rec/Reply`.
* **Reset rules** (read in the core, refused with a reason before the core is called): `HandleResetInstancesOpcode` (MiscHandler.cpp:985-1003): returns silently when the leader is on an instance map (`leader-inside`), only the group's leader (`not-leader`), not an LFG group (`lfg-group`), then `Group::ResetInstances(Manual)` (Group.cpp:1348-1372) over the instances the group OWNS: `InstanceMap::Reset` (Map.cpp:3182-3249) answers `CannotReset` when a lock exists and is not new (a boss was killed: `locked`), `NotEmpty` while any player, a GM included, is on the map (`not-empty`; the core notifies the players), else `Success` (unload timer; the group forgets the instance, Group.cpp:1357). The host calls `Group::ResetInstances` itself (public) after the same checks and reads the outcome from `Group::GetRecentInstanceId` (forgotten = ok), so a record never says `ok` unread.
* **`.tb tele` into an instance** (Q4, the instance bind rules): `Player::TeleportTo(WorldLocation, options, Optional<uint32> instanceId)` is public (Player.h:1267); a given instance id makes the worldport ack use `sMapMgr->FindMap(map, id)` (MovementHandler.cpp:215-217) instead of `CreateMap`, then `newMap->CannotEnter` (:231): a GM passes it before the cap and the owning-group test (Map.cpp:2972-2973) and its instance is always valid (`CheckInstanceValidity`, Player.cpp:21493); a non-GM passes only as a member of the group that owns the instance, which the host checks first (`not-in-group`). NO core change was needed. Refused first: a source that already stands on the target's map in ANOTHER instance (`already-on-map`): the core's own far-teleport path would call the entry check for a player standing in that map and abort.
* **Q1/Q2**: new state keys `life relhold resreq corpse corpsemap outside entered entry block blockmsg catchup practice` (appended after `rest_why`, older keys unchanged); end line `alive dead ghost wipe whole`; threat `pos flee`. `inst`/`stuck=blocked:` exist. The grammar never writes `none`: `entered`, `relhold`, `block` are EMPTY when there is no value (the spec's `none` would break addendum A6).
* **Q5**: every new command takes the token.

### 4. The leash inside an instance (D4)
`Think`: for a bot on its commander's instance the 100-yd distance catch-up is OFF (it walks, the engine's own follow; no `stuck` entry for it, `dl`/`dm` and `ready=no why=far` show it); never a teleport of a dead bot (existing N2 rule), never into or between instances (C2). The only host teleports left in `Think` are the open-world ones, counted in `catchup=`. The follow distance and the "do not chase a fleeing mob into the next pack" rule are engine questions: the host exposes what it can read from the core,
`.tb threat` `pos=` and `flee=` (the fear state of a 15 % flee, `UNIT_STATE_FLEEING`, or the walk to the nearest helper, `ASSISTANCE_MOTION_TYPE`: `DoFleeToGetAssistance`, Creature.cpp:1050-1072). The pack facts for an in-engine rule (see "Needs from P4").

### 5. The trainer SQL
Shipped as `forever/sql/world/...sql` + `forever/sql/README.md` (how the test server applies it: by hand with `docker exec -i forever-db mysql ... world < file`, then restart; `INSERT IGNORE`, safe twice). NOT applied.

### 6. Selftest
`.tb grammar selftest` now also prints `selftest case=state-h4`, `state-h4-refused`, `state-end-h4`, `threat-h4` and one real-kind record of each of `trigger`, `enter` (started and refused), `instance`, `leave`, `reset`, `tele`, `revive`, `kill`, `gm` (fixed example values).
The samples in TB-COMMANDS.md section 8 were produced from the encoder's rules (a literal re-implementation, not the server), NOT from a boot: the first boot's selftest replaces them.

## 3. Needs from P4 (engine side)
1. `dead` strategy added to the dead-state kit in the slice AiFactory, the actions `auto release` (WorldPacketActionContext), `release` (order verb), `accept resurrect`, and the trigger `resurrect` (+ `party member dead` for the priest) registered: the host calls `DoSpecificAction("release", ...)` and `("accept resurrect", Event("resurrect request", offererGuid, bot), true)` BY KEY and checks `GetAction(key)` first (`action-not-registered`, or no answer).
2. `AcceptResurrectCompat(bot, offerer)` writes the core's accept (`Response 0`, MiscHandler.cpp:507-534), `ResurrectOffererCompat(event)` reads the FIRST guid of the event's packet (the host's `Event(source, ObjectGuid, owner)` writes exactly that); `RepopRequestCompat` -> `HandleRepopRequest` (MiscHandler.cpp:63-93).
3. The lease answer is the host's (TbHostOwnership.cpp). If P4 changes `RunReleaseHold::Decide` or the `GetActivity` readers, say so: the no-entrance hold relies on `PlayerMaster` + `RunReleaseHold::Register`.
4. For the leash rules (engine): the facts the host can already read are public core calls: `Creature::HasSearchedAssistance()` (Creature.h:349), `Unit::HasUnitState(UNIT_STATE_FLEEING)`, `MotionMaster::GetCurrentMovementGeneratorType() == ASSISTANCE_MOTION_TYPE`, a mob's `GetThreatManager()`; the core has no distance leash inside a dungeon (`Creature::CanCreatureAttack`, Creature.cpp:2864). If the engine wants one of them as a value (a pack = creatures within aggro radius of the pulled mob), it needs a both-core helper in P4's compat files; the host will not add engine headers for it.
5. Walk-back: the engine's follow must walk a revived bot 100+ yd (U5); the host does no teleport for it (`stuck=runback` after 120 s is NOT implemented: it would need the engine's walk state; the state record shows `life=running-back` and `dm=` instead).

## 4. Not done / limits (said, not hidden)
* `stuck=runback:<s>` after 120 s: not implemented (above). `relhold`/`life` are computed from core facts on each `.tb state`, not from the engine's own decision: if the engine's release rule differs, the key says what the CREW RULE says.
* The entrance rule runs only while a host bot is on the instance; an instance first entered by a non-host player gets none.
* `mobs_alive` for a spawn whose creature is not loaded is "no future respawn time on the map" (an unloaded, never-killed creature counts alive).
* The entry's `SMSG_TRANSFER_ABORTED` is not captured (section 2.1). `.tb enter` needs the bots in a party for the leader-first rule; a lone bot enters on its own.
* The SQL is not applied; the first live test must apply it (README) before a level 18 tank can learn Taunt/Defensive Stance/Sunder Armor.

## 5. What the live-test package must check first
1. Boot: new command rows, no ASSERT; `.tb grammar selftest #g` prints the new records and replaces the hand samples; `.tb state` has the new keys at the END of every record (older keys unchanged).
2. U1 (Orgrimmar guards at the box): one bot in the 2230 box for 60 s; if attacked, use `direct`.
3. `.tb gm Mcmd on`; `.tb enter party Mtank` (or `trigger 2230`): all five on 389 with one `inst`, the commander in by `.tb tele Mcmd to Mtank`; `.tb instance Mtank info` (players=5 gms=1); log line `got entrance world_safe_locs 1102230`.
4. U2/U3: `.tb kill` + `.tb order release` inside the dungeon: revived at (0.80, -8.23) in the SAME instance; the priest's resurrection accepted at once (`resreq=`, log `answers the resurrection`) once P4's action exists.
5. Reset after leave (`.tb instance Mtank leave` then `reset`), and the refusals with a bot / the leader inside.

## 6. Gates
See the end of this file (filled in last).
