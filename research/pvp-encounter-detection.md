# PvP encounter detection: signals, existing checks, costs, end signals

Issue: Sagiroth/TortoiseBots#1446. Facts only. Module paths are relative to
`ai/playerbot/` at upstream/dev. Core = `~/tortoise-wow` (branch soap-optional, 5d79486),
paths relative to `src/game/`. Costs are read from the code, not measured.

## 1. Signals that a fight involves a player-controlled unit

| Signal | Where | Notes |
|---|---|---|
| Duel opponent | `bot->m_duel` (`DuelInfo`: opponent GUID, startTime, finished; core Objects/Player.h:332-341). Module: `strategy/values/DuelTargetValue.cpp:7-11` ("duel target"); `AttackersValue.cpp:311-317` adds the opponent to attackers (owner bot only); `AttackersValue.cpp:454-457` skips distance and combat checks for it. | Core "in a live duel" test: `Player::IsInDuelWith` Objects/Player.h:2808 (needs startTime != 0). `m_duel` exists from the request, before the countdown ends (startTime 0 then). |
| BG map | `Player::InBattleGround()` = `m_bgData.bgInstanceID != 0` (Objects/Player.h:2845); arena `InArena()` :2844. Module: trigger "in battleground" `PlayerIsInBattleground::IsActive` `strategy/triggers/PvpTriggers.cpp:~50` (calls `InBattleGround()`), registered `TriggerContext.h:221`. | Says nothing about whether any enemy player is near. BG state triggers: `BgWaitingTrigger` (STATUS_WAIT_JOIN), `BgActiveTrigger` (STATUS_IN_PROGRESS), `BgEndedTrigger` (STATUS_WAIT_LEAVE) in the same file. |
| Enemy player as attacker | `Unit::GetAttackers()` (Objects/Unit.h:1242), `dynamic_cast<Player*>` as in `WorldPvpSeekChecks.cpp:121-125`. | Attackers of the bot's pet are also read: `AttackersValue.cpp:319-327`. |
| Player target | `current target` cast to Player: `PvpTriggers.cpp` EnemyPlayerNear (first branch); `PossibleAttackTargetsValue.cpp:319` (`target->getObjectGuid().IsPlayer()` makes any player a combat candidate). | |
| Player pets/guardians/totems/charm of the enemy | Not a player GUID. Module expands an attacker's controlled units only when adding attackers: `AttackersValue.cpp:347` `CallForAllControlledUnits(... PET|GUARDIANS|CHARM|MINIPET|TOTEMS)`. Core owner lookup: `Unit::GetCharmerOrOwnerPlayerOrPlayerItself` (Objects/Unit.cpp:5384-5391; `GetCharmerOrOwnerGuid` Unit.h:1407, `IsCharmed` Unit.h:1405). | The module has no helper that maps an attacking pet/totem to its owning player; `EnemyPlayersValue::IsValid` (EnemyPlayerValue.cpp:73-134) dynamic_casts to Player and so rejects non-Player units. Core's own PvP rule uses the "player affecting" owner: Objects/Object.cpp:5935-5965 (group check, duel, `IsPvP()`, FFA flag). |
| Mind control / charm | Aura checks `SPELL_AURA_MOD_CHARM` / `MOD_POSSESS` at `EnemyPlayerValue.cpp:88` (only to exclude a charmed group member); `PossibleAttackTargetsValue.cpp:192-193` (charmed and friendly = not a target). A mind-controlled player is still a `Player` GUID. | `Unit::IsCharmed()` is a single field read (UNIT_FIELD_CHARMEDBY). |
| Attack legality | `player->IsValidAttackTarget(target)` called in `PossibleAttackTargetsValue.cpp:~347` (IsPossibleTarget); core `WorldObject::IsValidAttackTarget` Objects/Object.cpp:5894, PvP branch :5935-5965 and duel hostility Objects/Unit.cpp:3480-3481. | Same rule the spell system uses; excludes same-group players, unflagged players, finished duels (:3481 checks `!finished`; :5955 does not). |

## 2. Existing module checks

- `EnemyPlayerNear` (trigger "enemy player near", `PvpTriggers.cpp:10-27`, class `PvpTriggers.h:6-13`): default `checkInterval=1` (Trigger.h:20). Calls value "has enemy player targets", then "current target" and "enemy player target". Used by strategies `WorldPvpStrategy.cpp:12`, `AttackEnemyPlayersStrategy.cpp:17,24`, `AiFactory.cpp:657`.
- `HasEnemyPlayersValue` ("has enemy player targets", `EnemyPlayerValue.cpp:203-206`, ValueContext.h:167): BoolCalculatedValue, interval 3 (EnemyPlayerValue.h:28). Reads "enemy player targets" with qualifier 1.
- `EnemyPlayersValue` ("enemy player targets", `EnemyPlayerValue.cpp:9-71`, ValueContext.h:166): interval 1 (EnemyPlayerValue.h:14). Pipeline: (a) "possible attack targets" (qualifier 1, then full) filtered by `IsValid`; (b) BG only: falls back to "possible targets" (proximity scan) filtered by `IsValid` plus `CanEngageFromHere` (LOS, 30 yd Z, aggro range; :136-154); (c) outside BG only when `WorldPvpSeekChecks::IsSeeking` (config worldPvpSeek + "world pvp" strategy, WorldPvpSeekChecks.cpp:22-32): proximity list filtered by `IsSeekTarget` (target must be `IsPvP()`, :34-44) with `FindAssistTarget` (:95-134) fallback. Returns empty unless `AllowActivity(ALL_ACTIVITY)` and in world.
- `EnemyPlayerValue` ("enemy player target", :208-273): duel opponent first (:211-216), then best of the list (BG flag carrier aura 23333/23335, nearest for melee, lowest HP for ranged).
- `DuelTargetValue` ("duel target", `DuelTargetValue.cpp:7-11`, ValueContext.h:175): two field reads plus `ObjectAccessor::GetUnit`.
- `PlayerbotAI::IsInPvp()` (`PlayerbotAI.cpp:1023-1046`): true if `m_duel && m_duel->opponent`, or `InBattleGround()`, else `has enemy player targets`. Consumers: `GenericTriggers.cpp:404,413,1667,1672`, `IsInPve`/`IsInRaid` (PlayerbotAI.cpp:1018-1020, 1058). This is the nearest thing to an existing "am I in a PvP encounter" predicate. Note: it is true for the whole BG and for the whole duel request, not only while a fight is on.
- `AttackersValue` ("attackers", interval 2, AttackersValue.h:24; build :300-360; `IsValid` :390-): gathers `player->GetAttackers()`, duel opponent, pet attackers, then validates each. `InCombat` (:364-388) = threat > 0 or victim is bot/pet.

## 3. Cost per tick (~500 bots)

- BG / duel / charm tests: field reads only (`m_bgData.bgInstanceID`, `m_duel`, aura flag). Effectively free. `IsInPvp` in a BG or duel returns before any list is touched.
- `GetAttackers()` loop with `dynamic_cast<Player*>`: linear in the attacker set (usually under 10), no grid access; `attackers` is cached 2 ticks.
- `IsValid`/`IsValidAttackTarget`: per candidate, faction/group/aura checks, a hash lookup of "unreachable targets" and `ShouldAvoidPlayerKiller`, plus for the proximity branch an LOS test (`IsWithinLOSInMap`, EnemyPlayerValue.cpp:153), the one real per-target cost.
- Expensive part: "possible targets" = Cell grid visit (`PossibleTargetsValue.cpp:35-59`). The code comment calls it "~60% of tick" (:21). Throttled by `ShouldReuseSpatialScan` (`PlayerbotAIActivity.cpp:~320-362`): full rate in combat, when dead, below 80% HP, in revive/teleport grace, or with a human nearby; otherwise cached, rescanned every 1 s (staggered by GUID) and never staler than 1.5 s (`kGrindScanMaxStaleMs`). `IsSpatialScanIdle` (:307-318) returns empty for taxi and rested bots. So a bot in a PvP fight (in combat) pays the full-rate scan whenever the BG fallback (b) or seek branch (c) is reached.
- Outside a BG with no seek strategy, `EnemyPlayersValue` costs only the two "possible attack targets" reads, which are built from `attackers`, not a grid scan.
- No measurement was taken; the per-tick figures above are structural.

## 4. Signals that the encounter has ended

- Duel over: `Player::DuelComplete` (Objects/Player.cpp:8277-) sets `m_duel->finished = true` on both players (:8383, :8386); `m_duel` is deleted and nulled on the next `Player::Update` (Objects/Player.cpp:1594-1597). Between the two, `m_duel` is non-null with `finished == true`; the module checks (`DuelTargetValue.cpp:9`, `IsInPvp`:1027, `AttackersValue.cpp:312`) test only non-null/opponent, not `finished`, so they stay true for that window. After deletion `bot->m_duel == nullptr`. DuelComplete also strips the other side's duel auras (:8305-8330) and clears combo points.
- Combat dropped: `Unit::IsInCombat()` reads UNIT_FLAG_IN_COMBAT (Objects/Unit.h:1290); cleared by `Unit::ClearInCombat` (Unit.cpp:6741) / `Unit::CombatStop` (Unit.cpp:5206, calls `AttackStop`, `RemoveAllAttackers`). Module reaction: `PlayerbotAI::OnCombatEnded` (PlayerbotAI.cpp:1327, called from `BotStateActions.cpp:26`) resets explicit attack target and movement failure counters.
- Target dead / no attackers: `GetAttackers().empty()` and "attackers" list empty; "enemy player targets" empty (it is rebuilt every tick, interval 1) and "has enemy player targets" false (interval 3, so up to 3 ticks stale).
- Left the BG: `InBattleGround()` false (m_bgData.bgInstanceID 0); match end earlier via `BgEndedTrigger` (STATUS_WAIT_LEAVE, PvpTriggers.cpp).
- Gaps (facts): no module value records a per-encounter start/stop; `IsInPvp()` has no memory, so it flips back to false the tick the enemy list empties (outside BG/duel).

## 5. Options (list only)

1. Reuse `PlayerbotAI::IsInPvp()` as is (BG, duel request, or enemy player list non-empty).
2. Narrow variant: `IsInDuelWith`-style test (startTime != 0 and !finished) plus `InBattleGround()` with BgActive.
3. Add attacker/pet/totem owner resolution via `GetCharmerOrOwnerPlayerOrPlayerItself` over `GetAttackers()` (no existing helper).
4. Use `IsInCombat()` plus the above as the end signal; add a short debounce if flapping is a concern (none exists today).
