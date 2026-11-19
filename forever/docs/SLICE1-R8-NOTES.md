# Slice 1, package R8: the live test (a warrior on the real engine follows, assists and fights)

Branch `slice1/integ` (worktree `~/wt-slice-integ`), build slot: the default one. T = Turtle core, F = pinned ForeverCore.
Server: the build VM's `~/forever-run`. Boots used: see section 10. Evidence lines are from `~/forever-run/logs/world.out`
(console lines of `.tb ...` commands), the engine's action log (`~/forever-run/logs/bots/<bot>_*.log`, switched to
`AiPlayerbot.ActionLogScope = all` in the VM's copy of `aiplayerbot.conf` for the runs; the committed `.dist` keeps `commanded`) and the
engine's detail log (`AiPlayerbot.BotLogFile = bot.log`, `BotLogDebug = 1`, VM copy only).

## 1. The master (findings item 21)

`forever/src/TbBotMgr.cpp`: `BotMgr::JoinParty(bot, master)` does what `HandlePartyInviteOpcode` and `HandlePartyInviteResponseOpcode`
(Handlers/GroupHandler.cpp:65, :208) do, without packets: `new Group` + `AddLeaderInvite(master)` + `AddInvite(bot)` (or `AddInvite` into the master's
existing group), then `RemoveInvite(bot)`, `Create(master)` + `sGroupMgr->AddGroup` (first member only) and `AddMember(bot)` + `BroadcastGroupUpdate`.
The master is the leader. Refusals (with a message): the bot is in another party, the master has a pending invite, the master is not the leader of
its party, the party is full (MAX_GROUP_SIZE 5, the master among them; raids are not in this slice).

* `.tb login <bot> <master>`: `Login` remembers `wantParty`; the bot joins when it is in the world and the master is (first `Think`). `Login` itself
  refuses a login that would make a sixth seat (counting the bots still on their way in). A refused join logs the bot out again with a log line.
* `.tb group <bot> <master>` for a logged-in pair (also sets the bot's host master), `.tb ungroup <bot>` (leaves the party, no master afterwards).
* A bot that leaves the world leaves its party (`OnPlayerLogout`): the core keeps a logged-out member in the saved group, which would count against the five
  seats and re-form a party at the next login (seen: "3 member(s)" with only two bots in the world).
* The legacy rule is unchanged. Evidence: `BotMgr: Tbalpha joined the party of Tbbeta (2 member(s))`, then `.tb where`: `master: Tbbeta (no network session)`
  stays for the whole run (the old run lost it in the first tick).
* **A second decision the party did not cover.** `PlayerbotAI::GetDefaultMovementStrategy` answers "follow" only for a master with a network session and
  "wander" for any other (PlayerbotAI.cpp:5025), so the first run after the party fix had `non-combat strategies: arms, dps assist` and no `follow`
  (and the 7th missing key, strategy `wander`, in the log). The host's brain creation (`TbEngineBrain.cpp` Create) now calls
  `SetMovementStrategy("follow")` instead of `EnsureDefaultMovementStrategy()`: the master was bound on purpose, so the bot follows it (the same change the chat
  order "follow" makes). Turtle is untouched.

## 2. The seven missing keys (findings item 22)

Generator `forever/tools/slice_gen_context.py` (key lists), `forever/tools/slice_registry_check.py`: `(a)=0`, `(b)` 166 -> 164 (the seven are not all in (b)).

| Key | Creator | Decision |
|---|---|---|
| trigger `master target active` | `MasterTargetActiveTrigger` (GenericTriggers.h:546, .cpp in the slice) | registered (assist: the action log shows `PUSH:dps assist - 90 src=master target active`) |
| trigger `out of free move range` | `OutOfFreeMoveRangeTrigger` (RangeTriggers.h:612) | registered (follow at ACTION_HIGH) |
| trigger `formation station` + action `formation station` | `FormationStationTrigger` (RangeTriggers.h:461), `FormationStationAction` (FollowActions.cpp:619, in the slice) | both registered |
| strategy `wander` | `WanderStrategy` (WanderStrategy.cpp, not in the slice) | not registered. It is `FollowMasterStrategy` with three triggers (`wander far/medium/near`, none registered) whose actions start with `check mount state`. The slice's factory and the host ask for "follow"; the only asker was the default-movement rule above, which the host now bypasses (section 1). |
| action `check mount state` | `CheckMountStateAction` (CheckMountStateAction.cpp, not in the slice) | not registered: it pulls in mounts, battlegrounds (`BattleGroundWS.h`) and travel (`TravelMgr.h`); a level-1 warrior has no mount. Asked by `NonCombatStrategy`/`ClassStrategy` every `very often` tick (Engine skips a null action) and by `FollowAction` only with `boostFollow`. |
| action `check values` | `CheckValuesAction` (CheckValuesAction.cpp, not in the slice) | not registered: its body is debug output plus `sTravelNodeMap.manageNodes` (travel) and list pre-warming; nothing a warrior's follow/fight reads. |
| value `has enemy player targets` | `HasEnemyPlayersValue` (EnemyPlayerValue.cpp, not in the slice) | not registered: needs `EnemyPlayersValue` and the world-PvP seek checks (battleground and open-world PvP). The inert stand-in answers false (no enemy player), which is the PvE answer; read by `PlayerbotAI.cpp:1280`, `GroupValues.cpp:159`, `ChooseTargetActions.cpp:548`. |

Two more keys were needed by the fights and are registered or covered: value `move style` (registered; a missing std::string value crashed
`ReachTargetAction`, section 8 crash 2; `SubStrategyValue.cpp` joined `forever/SLICE1.txt` as its base class) and the stand-in now also answers `std::string`.

Other `slice: missing` lines seen in the fights (all explained, none registered): triggers `mounted`, `feared`, `combat stuck`, `combat long stuck`
(mount/fear/stuck-in-combat handling, actions not in the slice), values `unreachable targets`, `unreachable entries`, `keepout set aside`, `runners let go`
(`ManualSetValue` maps the engine only reads and writes: the inert stand-in holds the map like the real value), `available loot`, `has available loot`,
`drink unfinished`, `rest to full`, `enemy player target`, action `use lightwell`, action `reach spell::{charge::current target}` (charge is not
learned at level 1), `check mount state`, `check values`.

## 3. Live observations a to f (characters: Tbalpha and Tbgamma, level 1 human warriors, Tbbeta the master (a human priest, level 2 from the second boot on))

All three are console-driven bot sessions on map 0 (Northshire). `tb login Tbbeta`, `tb login Tbalpha Tbbeta`, `tb engine Tbalpha on`; the console line
and the `.tb where` fields (`state`, `master`, `target reach`, `chase gates`, `rage`, `ground`, added in this package) are the evidence.

### a. FOLLOW: PASS
* `tb go Tbbeta 15 0`, `tb where` 5 s later: warrior `-8771.5 -57.2`, master `-8768.3 -57.4` = **3.2 yd**, `motion type: 14` (follow), `master: Tbbeta (no network session)`.
* Second walk `tb go Tbbeta -10 -15`: warrior `-8774.9 -68.8`, master `-8778.3 -72.4` = **4.9 yd**.
* With two warriors (`tb go Tbbeta 14 8`): Tbalpha 2.4 yd, Tbgamma 4.2 yd from the master.
* Different answers named: R4A 1 (chase angle tolerance, follow slot not exact), R4A 2 (follow clears the generators first).
* "follow - USELESS" every second in the action log while idle is the engine's own re-trigger ("out of free move range" fires at ACTION_HIGH, the action is useless
  while the bot is already following); not a failure.

### b. ASSIST: PASS
* `tb attack Tbbeta` -> `TortoiseBots: Tbbeta: attacking Timber Wolf.`; 2 s later `Tbalpha ... in combat, victim Timber Wolf (health 31)`, `state: Combat`,
  `target reach: distance 0.85 yd, melee range 5.0 yd, in melee range: yes`. The wolf died within 8 s (health 31, 14, 1, none). The same in the later runs with
  one and with two warriors; the action log shows the assist chain `PUSH:dps assist - 90 (trigger) src=master target active` then `A:dps assist - OK`.
* The hidden condition from the first run: assist needs the trigger `master target active` (item 22), now registered.

### c. ABILITIES: PARTIAL (auto-attack and Heroic Strike work; the rest is character data)
Source: the engine's action log (`AiPlayerbot.ActionLogScope = all`, VM copy of the conf), 3 fights per boot, plus the `.tb spells` list.
* A fresh level 1 warrior of this core knows **Heroic Strike (78)** and Battle Stance (2457), no Battle Shout and no Execute (`tb spells Tbalpha`: 46 spells, none of them those).
  The bot asks for both every tick and is refused by its own pre-check: `[CAST_REFUSED] spell=battle shout result=87` (SPELL_FAILED_NOT_KNOWN), `spell=execute result=87`
  (~5,000 and ~3,900 per 90 s); the action returns IMPOSSIBLE and the engine falls to the next row (`melee`). Same code path as Turtle; the Turtle level 1 spell list
  was not compared (the "known" set is game data: findings item 20 and the PORT-PLAN rule 4).
* **Heroic Strike never fired in the short level 1 fights** because `HeroicStrikeTrigger` asks for 40 rage (WarriorTriggers.h:271, "rage dump, not a builder"), and a
  level 1 warrior reaches about 10 to 20 in a 6 s fight. Not a port bug (same rule on Turtle). Proof with `.tb rage Tbalpha 90` (new, sets the bot's rage, the
  console's stand-in for `.modify rage`): `PUSH:heroic strike - 11 (trigger)`, `[CAST_START] spell=Heroic Strike(78)`, `[CAST_OK] spell=Heroic Strike(78) result=0 phase=PREPARE`,
  `A:heroic strike - OK` three times in one 6 s fight, rage 90 -> 49 (`.tb where`: `rage: engine value 49, core power 499/1000`).
* Auto attack: `A:melee - PREREQ` starts it, `A:melee - USELESS` (14,000 per 90 s) is "already attacking".
* Charge (`reach spell::{charge::current target}`, level 4) is missing as an action and not known: `slice: missing action reach spell::{charge::current target}`.

### d. AFTER COMBAT: PASS (after one fix)
* First run: after the wolf died `state: Combat` stayed, `motion type: 5` (chase) and the warrior did not follow again (distance to the master grew to 6.4 yd).
  Cause: `AiFactory::AddDefaultReactionStrategies` of the slice was empty; the warrior class strategy is what carries `combat end -> set non combat state`
  (ClassStrategy::InitReactionTriggers; AttackAction calls `OnCombatStarted` itself, so the bot entered the combat engine but nothing sent it back).
  `SliceAiFactory.cpp` now adds the "arms" strategy to the reaction engine, as the full factory does (AiFactory.cpp:2085).
* After the fix: `t+8 s state: Non Combat`, `t+10 s motion type: 14`, `distance to master target: 0.1`.

### e. SECOND BOT AND LOGOUT ORDERS: PASS
* `tb login Tbgamma Tbbeta` -> `BotMgr: Tbgamma joined the party of Tbbeta (3 member(s))`; both follow and both assist (`tb attack Tbbeta`: Tbalpha `distance 0.0 yd` victim Timber Wolf, Tbgamma 2.4 yd).
* Bot first while in combat: `tb logout Tbgamma` mid-fight: `bot Tbgamma left the world`, `BotMgr: session ... ended`, server up.
* Master first while both bots fight: `tb logout Tbbeta` -> `BotSession: logout asked for ... (the master left the game)` for each bot, all three `left the world`, `tb status: 0 bot session(s)`.
* Engine off while in combat: `tb engine Tbalpha off` two seconds into a fight, then back on: no crash, the spike's follow/assist took over and the next fight ran on the engine.

### f. 20 MINUTES IDLE: PASS (22 minutes, section 9)

## 4. Item 9: "incoming: cast failed"
* `tb unported` shows `incoming: cast failed: 1..4` and the other `incoming:` rows at 1 to 44: those numbers are the **registration** of the packet-handler rows
  (one per engine built; `UnportedPacketHandler`), not packets received. No `incoming:` feature was hit by a fight.
* No asynchronous cast failure happened in the fights: every cast the bot made succeeded (`CAST_OK result=0`). By construction the bot never hears one: `BotSession::SendPacket`
  drops every packet (SMSG_CAST_FAILED, SMSG_SPELL_FAILURE, SMSG_FORCE_*_SPEED_CHANGE); only the synchronous pre-checks answer. What the bot does after a refusal it does hear
  (`CAST_REFUSED result=87`): the action is IMPOSSIBLE, the trigger fires again next tick (the trigger has no back-off), and the lower rows run (melee). That is the same engine
  code as Turtle. An asynchronous failure (out of range at the moment of the cast, interrupted) would be a cast the bot believes went through; not exercised.
  Next package: a hook on the core's spell failure for a bot (a `Spell::SendCastResult` equivalent through a script) before any caster class.

## 5. Items 16 and 12: melee reach and chase
Two port bugs found and fixed (both Different answers, entries below), then 5+ samples with the final code (`tb where`, `target reach`):

| sample | bot to target (centre to centre) | melee range of the core | in melee range |
|---|---|---|---|
| fight 1 (Tbalpha) | 0.499 yd | 5.0 yd | yes |
| fight 1 (Tbgamma) | 0.499 yd | 5.0 yd | yes |
| fight 2 (Tbalpha) | 0.499 yd | 5.0 yd | yes |
| fight 2 (Tbgamma) | 0.500 yd | 5.0 yd | yes |
| fight 3 (Tbalpha, Tbgamma) | 0.4997 / 0.4998 yd | 5.0 yd | yes |

Before the fixes: 0.005 to 0.3 yd (inside the target), and a **chase that never started**: the warrior stood 5.6 to 20 yd from a target it held as victim, `motion 0`,
`A:reach melee - FAILED why=no-move-try`, for the whole fight.
* **Bug 1, no height under a roof.** `ChaseTo` builds its end point with `WorldPosition::GetHeight`, which is `GetHeightCompat` = F's `Map::GetHeight(x, y, z)`. F's `TerrainInfo::GetStaticHeight`
  (TerrainMgr.cpp:924) searches the vmap floor from z itself and takes the raw terrain only when z is above it; T's `GetHeightStatic` (GridMap.cpp:776) looks from z + 2 and, on a
  miss, again with an infinite range. A bot standing at a mine mouth got -200000 (VMAP_INVALID_HEIGHT_VALUE), `endPosition.isValid()` was false and `ChaseTo` returned false
  before any move (detail log: `[BOT CHASE] ... endPos invalid, falling back`, one line per second; a temporary debug line showed `end=(-8714.38,-88.90,-200000.00) endvalid=0`).
  `GetHeightCompat` now asks the way T does. Different answer, new: **R8-1 height search** (T: from z + 2 and an infinite retry; F raw: from z).
* **Bug 2, chase to the centre.** The bot asks `MoveChase(target, 0, angle)` for "melee contact"; T's generator stops at the attack reach, F's float overload makes `ChaseRange(0)`,
  which is "to the centre". `MoveChaseCompat` now calls F's default `MoveChase(target)` for distance <= 0 (melee/combat reach, no angle wedge). Different answer, new: **R8-2 chase distance 0**.
  What F's default ends at is **0.5 yd** (CONTACT_DISTANCE) centre to centre: the creature does the same to the bot, so both stand on one point (no unit collision on either core).
  The bot is in melee range and does not run through it. R4A 10 (melee reach, near point) stays the entry for the numbers.

## 6. Item 24: core-sized tables in the slice's files
Method: grep of the 131 slice sources and every header under `ai/playerbot` for arrays declared with MAX_*/EQUIPMENT_SLOT_END/k* sizes and for every `[... getClass()/getRace()/Quality/Class/school ...]` index, then each declaration read.

| Table | Size on T / F | Indexed by | Out-of-range possible? |
|---|---|---|---|
| `PlayerbotAIConfig::ahBotPriceMultiplierCategoryQuality[kAhBotMaxItemClass][MAX_ITEM_QUALITY]`, `...MountQuality`, `...PetQuality` | 16 x 7 on T; 16 x 8 (F MAX_ITEM_QUALITY 8) | loops bounded by `ai::kAhBotPriceMax*` (R6B fix) | no (the array is the larger one) |
| `classRaceProbability[MAX_CLASSES][MAX_RACES]` | MAX_CLASSES 12 on T / 16 on F, MAX_RACES 11 (our `#define`, TbCompat_Names.h) on both | loops `cls < MAX_CLASSES`, `race < MAX_RACES` only (PlayerbotAIConfig.cpp:1238-1330); no reader in the slice | no index by a bot's race; F race ids go past 11 (Pandaren 24 ...), so a future reader that indexes by `getRace()` would overrun: **the define must grow to F's race count before the random-bot tables are ported** |
| `classSpecs[MAX_CLASSES]`, `gearProgressionSystemItems[..][MAX_CLASSES][4][SLOT_EMPTY]` | 16 on F (larger than T), SLOT_EMPTY = EQUIPMENT_SLOT_END = 19 on both | `cls` loops 1..MAX_CLASSES-1 (config load) | no |
| `ChatHelper` `classes`, `races`, `specs` | `std::map` | class, race, spec | no (maps) |
| `ChatHelper::ReputationRankStrIndex[MAX_REPUTATION_RANK]` | 8 on both | local | no |
| `ItemQualityColors[proto->Quality]` (ChatHelper.cpp:531) | F's own table of MAX_ITEM_QUALITY | the mirror's quality, 0..7 | no |
| `TravelMgr.h` `currentSkill[MAX_SKILL_TYPE]`, `skillMax[...]` (763, our define) | F skill ids reach well past 763 | `GetCurrentSkill(SkillType)` | **yes if called**; no caller in the slice (grep), `TravelMgr.cpp` is a link stand-in |
| mirrors: `TbSpellEntry` `Effect[...]` x 3 (MAX_EFFECT_INDEX), `Reagent`, `Totem`, locale arrays; `TbItemPrototype` `ItemStat`, `Spells`, `Damage` | 3 on T; F spells carry more effects | fill loops are bounded (TbSpellEntry.cpp:309/315, TbItemPrototype.cpp:37/320); **readers indexing with an F effect index** | fixed this round (the lead's review): `IsImmuneToSpellEffectCompat` now returns false for an index past the effect count (the core's `GetEffect` ASSERTs), `ForEachAuraOfTypeCompat` skips effects with index >= 3; grep over `forever/src` for other `GetEffect(`: only `SpellCalculateDamageCompat` (already guarded) |
| `PlayerbotAI::allowActive[MAX_ACTIVITY_TYPE]`, `botTextLocalePriority[MAX_LOCALE]` | own / locale counts | own enum | no |

## 7. Tick cost (`.tb perf`, new: the world's own update time and the module's share, average and worst over a window)
Two engine warriors and a bot master following idle, one minute each (about 52,000 world ticks per minute, the core ticks about every 1 ms):

| | module (sessions + engines) per tick | world update time per tick |
|---|---|---|
| engine ON (2 bots) | avg 24 us, worst 4,139 us | avg 1.13 ms, worst 10 ms |
| engine OFF (spike behaviour) | avg 3 us, worst 152 us | avg 1.14 ms, worst 9 ms |

So the two engines cost about 21 us per tick on average (about 2% of a 1.1 ms tick), at most 4 ms in one tick. The worst single tick of a session is the first engine start,
**1,091 ms**: `PlayerbotAIConfig::Initialize` (the 720-key config read and the spell and talent tables) runs lazily on the first brain (`EnsureConfig`). It happens once per boot and stalls
the world for that second; moving it to the world-initialised hook would keep it out of the first bot's login.

## 8. Crashes and their fixes (gdb, `forever-runner-gdb`)
1. **Boot 2 and 3, first fight, `ai::MoveStyleValue::HasValue` in `ReachTargetAction::Execute`** (backtrace via `world-gdb.sh`, a copy of `world.sh start` that runs `gdb -batch -ex run -ex bt`): `GetValue<std::string>("move style")`
   was a missing key and the missing-key stand-in had no `std::string` base, so the cast gave null and `->Get()` crashed (findings item 4's "value read as a type the stand-in lacks";
   the registry check had listed it: `move style types: std::string [stand-in lacks: std::string]`). Fix: register the value (generator list) and add `SubStrategyValue.cpp` to
   `forever/SLICE1.txt` (the base class of `MoveStyleValue`; a link error said so), and `std::string` joins the stand-in's types.
2. No other abort or crash in the remaining 9 boots. The two findings of the lead's review (`IsImmuneToSpellEffectCompat`'s unchecked `GetEffect`, `ForEachAuraOfTypeCompat`'s effect index) did not
   fire (the warrior's spell set is small, and the `move style` crash came first); both are fixed anyway (section 6).
3. Not a crash but the same class: the first fight run left `Combat` for good (section 3d) and the chase stalled (section 5).

## 9. 20 minutes idle, unported and missing keys
Boot 12, 16:14 to 16:37 UTC (22 minutes): Tbalpha and Tbgamma on the engine, following Tbbeta, no fight. `.tb perf` every 4 minutes, `.tb where`, `world.out` line count:
* no crash, no assert, no error flood: `world.out` grew by 126 lines in the 22 minutes (12,926 -> 13,052 at 16:33), 0 `ERROR` / `ASSERT`; the party and the masters held (`master: Tbbeta (no network session)` at every check).
* tick cost in the idle windows (about 215,000 to 265,000 world ticks each): world update time avg 1.12 to 1.13 ms (worst 48 to 182 ms: other load on the shared VM, the module share
  stayed at avg 24 to 25 us, worst 1.9 to 5.3 ms), VM memory steady (12.2 to 12.4 GB used). The engine's action log grows by about 2 MB a minute per bot with `ActionLogScope = all` (the VM copy; the shipped `.dist` keeps `commanded`).
* `slice: missing` lines at the end: **18**, each logged once per name (the guard logs a name once per process), all explained in section 2: actions `check mount state`, `check values`, `use lightwell`; triggers `combat long stuck`,
  `combat stuck`, `feared`, `mounted`, `resurrect`; values `available loot`, `drink unfinished`, `enemy player target`, `has available loot`, `has enemy player targets`, `keepout set aside`, `rest to full`, `runners let go`,
  `unreachable entries`, `unreachable targets`. (`use lightwell` is a priest action the warrior context's follow/assist rows ask for by name; `resurrect` belongs to the reaction rows of the class strategy: death and
  resurrection are out of the slice.) None is registered; the four triggers and the loot values need actions or stacks outside the slice (`mounted`/`feared`/stuck: mount, fear and unstuck handling; loot: the loot stack), the
  four map/bool values are plain holders the stand-in reproduces.
* `tb unported` at the end (calls since the start of the boot, 22 minutes, 2 to 3 engines): top: boss and raid tactics 1,271,584 (measured before the counter below was coalesced; see below), chat link parsing 28, incoming: quest 22, incoming: group and party 18,
  incoming: trade and item errors 16, travel and exploration 12, incoming: loot 10, incoming: taxi 8, talent spec tables 15, navmesh and path internals 8, then single digits. Everything under `incoming:` is handler-row registration (section 4).
  The three hot counters of R6B: **boss tactics** 607k/4 min then (now 1.27M/22 min with the engine doing more), **loot objects** 204k -> **0** (`LootGuidCompat` answers from `Player::GetLootGUID()` and `ReleaseLootIfFarCompat` counts only a real loot window),
  **cinematic and camera** 102k -> **0** (`EndCinematicIfWatchingCompat` asks `CinematicMgr::IsOnCinematic()` and counts only a bot that is watching one). Boss tactics: the stand-ins are reached through per-tick checks in about 40 shared files, so no
  guard at the slice's side keeps the call out of the stand-in; they were made the hottest by the two multipliers (`CharmedAllyHoldMultiplier`, `TrapHoldMultiplier`, slice-side, now uncounted) and the 60+ boolean gates; the counter now counts once per second of world time
  (changed after the last boot; compile gate only, not run live).


## 10. Different answers, new entries (the lists of R3, R4A, R4V, R4P, R4C, R4M stay as they are)
* **R8-1 height search.** `GetHeightCompat` (TbCompat_Unit.h): T looks for the floor from z + 2 and retries with an infinite range; F searches from z. The helper now does T's search. Callers that rely on the
  F behaviour: none in the slice (it is the helper of the legacy `Map::GetHeight`).
* **R8-2 chase distance 0.** `MoveChaseCompat` (TbCompat_A.h): "melee contact" is `MoveChase(target)` on F (stops at 0.5 yd centre to centre, the core's contact distance), not `ChaseRange(0)` (the centre). R4A 1 and 10 stay valid for
  the other distances.
* **R8-3 default movement mode of a host master.** The legacy default for a master without a network session is "wander" (PlayerbotAI.cpp:5025); the host sets "follow" explicitly (section 1).
* **R8-4 party seats.** A bot leaves the party when it leaves the world; the host forms the party with the master as leader (the hire flow's shape, no packets); five seats, raids not in this slice.
* **R8-5 starting spells.** A level 1 warrior of this core knows Heroic Strike and Battle Stance only; Battle Shout and Execute answer NOT_KNOWN (result 87). Whether Turtle's level 1 set differs was not compared.
* **R8-6 incoming packets.** Every packet to a bot session is dropped (`BotSession::SendPacket`): asynchronous cast failure, forced speed change acks and knock back are not seen (findings item 9); the `incoming:` rows of `tb unported` count handler registrations.

## 11. What is still wrong, what to do next
1. The warrior stands exactly on its victim (0.5 yd centre to centre): fine for a melee swing, wrong for a bot that should respect the target's back/side (an angle is not passed any more for melee).
2. Refused casts repeat every tick (`battle shout`, `execute`: 5,000 per 90 s each): no back-off after a NOT_KNOWN. The same trigger rule as Turtle, but the full build teaches the spells at level 1 through a trainer path the slice does not have. Cheapest fix: register the spell-known pre-check in the trigger, or a one-line "known" guard in `SpellCanBeCastedTrigger` (shared code, a Turtle proof needed).
3. Asynchronous cast results are not heard (R8-6): needed before any caster.
4. First brain costs 1.09 s (config load) on the world thread: move `EnsureConfig` to a world-initialised hook.
5. The `MAX_RACES 11` define and the `MAX_SKILL_TYPE 763` define (section 6) must grow to F's counts before the random-bot / travel tables are ported.
6. The boss-tactics stand-ins are reached by per-tick checks in about 40 shared files (about 1.9 million calls in 5 minutes with two bots before the counter was coalesced to one per world tick).

## 12. Boots
12 of 12: 1 (party and master, found the `wander` default), 2 (follow ok, first fight died: `move style`), 3 (gdb backtrace of it), 4 (fixed: assist, found `Combat` stuck), 5 (reaction fix, action log, second bot), 6 (spells, chase stall found, logout orders),
7 (detail log: `endPos invalid`), 8 (ground height probe), 9 (temporary chase debug: -200000 height), 10 (height fix verified, ghost master), 11 (melee contact fix, tick cost on/off), 12 (heroic strike with rage, 22 minutes idle, final `tb unported`).
The shared VM server was started only through `~/forever-run/world.sh` / `world-gdb.sh` and stopped with `world.sh stop` after each boot, also the last (`docker ps` shows no `forever-world`; `forever-test1` was not touched).

## 13. Gates (final commit)
| Gate | Result |
|---|---|
| Option ON `worldserver` build | `CMAKE_RC=0 MAKE_RC=0` (final code) |
| Option OFF module build (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=OFF ... build.sh 12 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`, `build.ninja` has 0 entries for `forever/src/slice`, `forever/src/host/TbHost` or `/ai/playerbot` |
| Slice census (`FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 14`) | `files 132  compile clean 132  with errors 0  error places 0` |
| `slice_gen_context.py --check`, `slice_registry_check.py` | file current; `(a)=0  (b)=164` |
| Turtle `vm-build` / `vm-test tb` | not needed: no file outside `forever/` changed (`git diff --stat d22bcaec4 HEAD`: 14 files, all under `forever/`) |
| The server on the build VM | stopped; `docker ps` shows `forever-bnet`, `forever-db` (the shared infrastructure) and no `forever-world` |
