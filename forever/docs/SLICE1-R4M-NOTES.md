# ForeverCore port, slice 1, package R4M: integration of the four R4 packages (V, A, P, C)

Branch `slice1/integ` (worktree `~/wt-slice-integ`). The four branches were merged one at a time (`git merge --no-ff`) with no textual
conflict (the shared wiring tests `test_magma_guard_wiring`, `test_move_fail_note_wiring`, `test_swim_path_wiring` were edited by A and
C in different lines and merged cleanly; every pin is re-checked in the gate). T = Turtle core, F = pinned ForeverCore.

Census before the packages: 1,570 error places. The merged tree before any dedupe edit does not compile at all (the four compat headers
redefine each other); the number after the dedupe is under "Numbers" at the end.

## 1. The dedupe table (written BEFORE the code was changed)

Common pair = `forever/src/compat/TbCompat_Unit.h` (section "R4M") and `ai/cmangos-compat-r3.h` (T side); names and plain macros go to
`TbCompat_Names.h`. "same" = the two definitions give the same answer on F (and on T the bodies are the same call).

### 1a. Same name defined by two or more packages

| helper | defined by | the definitions | kept (reason) |
|---|---|---|---|
| `AuraIsPositiveCompat(Aura const*)` | P, V | same on F (`GetSpellInfo()->IsPositive()`) and T (`IsPositive()`) | one copy, common |
| `DuelOpponentGuidCompat(Player const*)` | P, V | F: V `duel ? duel->Opponent->GetGUID() : Empty`; P adds `&& duel->Opponent` (null check). T same | P's (V's dereferences a null `Opponent`); common |
| `GuidKeyCompat(ObjectGuid const&)` | V, C | F: V `empty ? 0 : hash`, C `hash` (the hash of an empty guid is not 0). T `GetRawValue()` (empty = 0) | V's (empty guid is 0 on T, so 0 here too); common; THE guid key (section 3) |
| `GuidRawCompat(ObjectGuid const&)` | A, P | F: A `GetRawValue(0) ^ (GetRawValue(1) * 0x9E37...)` (mixes both halves), P `GetRawValue(0)` (low word only). T `GetRawValue()` | BOTH deleted: every use becomes `GuidKeyCompat` (finding 15). Behaviour change on F: the number printed in the action log and used as the ritual-hold / claim / flee-failure key is the hash instead of the mix or the low word; it is the same number everywhere now |
| `HasPlayerFlagCompat` | A (`WorldObject const*`, false for a non-player), P (`Player const*`) | F: A `ToPlayer() && HasPlayerFlag`, P `HasPlayerFlag`. T: A `obj->HasFlag(PLAYER_FLAGS, f)`, P the same on a Player | A's signature (a Player converts to WorldObject; same answer for a player) |
| `HasUnitFlagCompat` | A, P | same | one copy |
| `UnitHasFlagCompat` (V) | V | same as `HasUnitFlagCompat` under another name | deleted; V's 9 call sites use `HasUnitFlagCompat` |
| `IsUnderwaterCompat` | A, C | same (`IsUnderWater()` / `IsUnderwater()`) | one copy |
| `GroupMainTankGuidCompat` | A, V | same loop over the member slots (V through `GroupFlagHolderCompat`) | V's (with `GroupMainAssist...`, `GroupFlagHolderCompat`) |
| `ItemDurabilityCompat`, `ItemMaxDurabilityCompat` | V, P | same | one copy |
| `AreaZoneIdCompat` | P, C | same | one copy |
| `AreaNameOrEmptyCompat` | P (null area), C (null name) | F: P `area ? string(AreaName[enUS]) : ""`, C `AreaName[enUS] ? it : ""`. T: P `area && Name`, C `Name` | both null checks (area and name): the union, never returns a null deref |
| `PathCalculateCompat` | V (xyz, no default), A (xyz and two-point, default `forceDest=false`), C (two-point) | same call | A's two overloads |
| `PathPointsCompat` | V, A | same | one copy |
| `SpellPowerCostCompat(SpellEntry const*, Unit*)` | V (`Unit*`), P (`Unit const*`), A (a different function: `(Spell const*)`, the mana entry of a cast) | V = P on both cores | P's `Unit const*`; A's is renamed `SpellManaCostCompat` (it answers another question; the same name with another argument list hid that) |
| `TerrainWaterOrGroundLevelCompat` | A, C | same | one copy |
| `MAP_LIQUID_TYPE_*`, `MAP_ALL_LIQUIDS` | A, C | same numbers (T's: magma 1, ocean 2, slime 4, water 8) | one copy (A's has `NO_WATER`, `DARK_WATER` too) |
| `#define CONFIG_UINT32_MAX_PLAYER_LEVEL`, `CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GROUP` | C, and already in `forever-compat-shim.h` | identical | C's two lines deleted (the shim has them) |
| `#define GetAuraMaxDuration`, `GetCastedTime`, `UNIT_STAT_MELEE_ATTACKING`, `MOVEFLAG_BACKWARD/WALK_MODE/FALLINGFAR/JUMPING` | V+A, A+P, V+A, A+P | identical expansions | one copy each in `TbCompat_Names.h`; `MOVEFLAG_FORWARD/SWIMMING` (A) and `MOVEFLAG_NONE` (P) move with them |

### 1b. Different names, same job

| helpers | the definitions | kept (reason) |
|---|---|---|
| `GetPlayerCompat(guid)` (V), `PlayerByGuidCompat(guid)` (P) | F both `ObjectAccessor::FindPlayer`; T both `sObjectMgr.GetPlayer` | `GetPlayerCompat` (126 uses of the call in the tree); P's 2 sites rewritten |
| `PlayerAccountIdCompat` (P), `AccountIdOfCharacterCompat` (C) | F both `sCharacterCache->GetCharacterAccountIdByGuid` | C's name; P's 4 sites rewritten |
| `CurrentThreatVictimCompat(Unit*)` (V), `ThreatCurrentVictimCompat(ThreatManager&)` (A) | F: **V `GetLastVictim()`, A `GetCurrentVictim()`** (differ in behaviour); T both `getCurrentVictim()->getTarget()` | V's: `GetCurrentVictim()` re-selects and WRITES the creature's list outside the map update (and asserts); `GetLastVictim` is a plain read. A's one site (`MovementActions.cpp:3800`) now asks `CurrentThreatVictimCompat(target)`. What A's caller now gets on F: the stored victim (null when the stored reference should be offline) instead of a fresh pick |
| `IsWithinLOSPointCompat` (V), `IsWithinLOSCompat` (A) | same on both cores | A's name; V's 1 site rewritten |
| `ApplyRangeSpellModCompat(Player*, SpellEntry const*, float&)` (P), `ApplySpellModRangeCompat(Player*, uint32 spellId, float&)` (A) | F: P through the mirror's `SpellInfo` link, A `sSpellMgr.GetSpellInfo(id, DIFFICULTY_NONE)` with a null guard. T both `ApplySpellMod(id, SPELLMOD_RANGE, r)` | A's (keyed by the id exactly as T is, null-safe); P's site passes `spellInfo->Id` |
| `SpellEffectAmountCompat` (V), `SpellCalculateDamageCompat` (A) | F: V `GetSpellInfo()->GetEffect(i)` unchecked, A bounds- and null-checked. T the same call | A's (an out-of-range effect index asserts in V's) |
| `CreatureRankCompat` (V), `CreatureIsWorldBossRankCompat` (P), `CreatureIsAboveNormalCompat(template)` (C) | F: V and P both `isWorldBoss()` (boss type flag of the difficulty, never a pet); C is on the TEMPLATE: classification Elite, RareElite, Obsolete, Rare. T rank 0..4 | V's `CreatureRankCompat` + the `CreatureEliteType` enum move to common; P's helper is gone (`CreatureRankCompat(c) == CREATURE_ELITE_WORLDBOSS`, same answer on both cores); C's stays (a template, no creature, no difficulty) and ALSO counts the template's boss type flag so "above normal" and "world boss" agree on F (finding below) |
| `PathTypeCompat` (A, `uint32`), `PathTypeOfCompat` (C, `PathType`) | same call | A's (`uint32`); C's 2 sites cast to `PathType` |
| `PathPointsOfCompat` (C) | same as `PathPointsCompat` | deleted |
| `PathIsCompleteCompat` (V) | `type == PATHFIND_NORMAL` | kept (V only, moves with the path set) |
| `LiquidStatusCompat` + `GridMapLiquidData` (A), `LiquidAtCompat` (C, out of line) | F translate the legacy type mask to F's. **A**: a mask `0` becomes flags 0 and is passed as an engaged filter, which matches nothing; the four-type mask is a filter. **C**: `0` and the four-type mask both become "no filter". T: `0` = no filter, a mask is ANDed with the liquid type (`GridMap.cpp:550`, `VMapManager2.cpp:281`), and `MAP_ALL_LIQUIDS` is the four types (no dark water) | ONE helper `LiquidStatusCompat` (A's T shape: status + `GridMapLiquidData`, so the six A sites and the `FishAction` shape need no rewrite) with the translation of A and the `0` = no filter rule of C (that is T's). The four-type mask stays a filter (T's, exact), so dark water is not matched, as on T. What C's callers now get: `MAP_ALL_LIQUIDS` no longer includes dark water (T never did). `LiquidAtCompat` and its .cpp part are deleted |
| `AuraListCompat`/`AurasByTypeCompat`/`GetAuraCompat`/`AuraHolderCompat` (P) vs `ForEachAuraOfTypeCompat` (V) | both walk `GetAuraEffectsByType`: P hands back the effect's base `Aura*` (the type the bot's own `Aura*` API returns), V a view `{spell, effIndex, caster}` | both kept: P's is the shape of `PlayerbotAI::GetAura/GetAuras` (an `Aura*`, the effect index is lost), V's the shape the callers that need the effect index have. A's `MovementActions.cpp` periodic-trigger loop (needed `GetEffIndex`) now uses V's, so `ForEachAuraOfTypeCompat` + `AuraOfTypeCompat` move to common |
| `SPELL_STATE_CASTING` (P macro to `SPELL_STATE_CHANNELING`) vs `SpellIsInFlightOrChannelingCompat` (V: `LAUNCHED` or `CHANNELING`) | see 2. | P's reading (T's state 2 IS the channel); V's helper renamed `SpellIsChannelingCompat` |
| `GuidTextCompat` (V) and `GuidWriteCompat/GuidReadCompat` (C) | text for display vs a lossless `high/low` stream form for `GuidPosition` strings and `|Hfound` links | both kept: one function turns a guid into text for people (`GuidTextCompat`), the other is the round-trip form of a stored string (it has to parse back). `GuidTextCompat` moves to common |
| `CStrCompat` (R3, used by A, P, V) vs `CStrOrCompat` (C) | not a duplicate: R3's is the one `CStrCompat`; C's takes a fallback for the places that tested the pointer | both kept, nothing to merge |
| `HasMovementFlagCompat` (A) and the `MOVEFLAG_*` names (A, P) | see 1a | names to `TbCompat_Names.h`, `HasMovementFlagCompat` stays in A (only A uses it) |

### 2. SPELL_STATE_CASTING (this one changes behaviour)

Read both cores. T `Spell.h:206-214`: `NULL 0, PREPARING 1, CASTING 2, FINISHED 3, IDLE 4, DELAYED 5`; there is no separate channeling
state: `SPELL_STATE_CHANNELING` is `#define`d to `SPELL_STATE_CASTING` in the Turtle shim (`cmangos-compat-shim.h:590`) and `m_spellState = SPELL_STATE_CASTING` is set in one
place, `Spell::handle_immediate` for a channeled spell (`Spell.cpp:3999`); a missile in flight is `SPELL_STATE_DELAYED`. F `Spell.h:254-262`:
`NULL 0, PREPARING 1, LAUNCHED 2, CHANNELING 3, FINISHED 4, IDLE 5`; `LAUNCHED` is set at `Spell.cpp:3945` for every cast that went out (missile in flight).
So T's state 2 is "channeling", and P read it right (the macro maps it to F `CHANNELING`). V's helper described T's state as "cast time over,
waiting for the delay" and made F answer `LAUNCHED` or `CHANNELING`, which is the T answer plus a state T answers differently (T's in-flight state is DELAYED,
which V's Turtle body does not include). Decision: F answers `SPELL_STATE_CHANNELING` only. What the callers now get on F:
`SpellCastUsefulValue.cpp:40` ("is the spell I just sent still going") no longer counts a launched missile as "still going" (T did not either);
`PlayerbotAI.cpp:1054,1131` (cast-bar delay and the reaction-cast total) are unchanged (they already used the macro).

## 3. GUID keys (finding 15)

One function turns a guid into a 64-bit key on ForeverCore: `GuidKeyCompat` (common pair; F: `std::hash<ObjectGuid>` of the 128 bits, 0 for the empty guid; T:
`GetRawValue()`). One function turns it into text for people: `GuidTextCompat` (F `ToString()`, T the stream form). `GuidRawCompat` (A: both halves mixed, P: the low
word) is gone: its 40 call sites (A 22, P 18) are `GuidKeyCompat`. `GuidWriteCompat/GuidReadCompat` (C) is not a second key: it is the round-trip text of a stored
`GuidPosition` string and has to parse back (a hash cannot).

What is NOT a guid key but is a `uint64`/`uint32` made from a guid, in the slice's files: the low part (`GetGUIDLow()` = `GetCounter()`) of a PLAYER guid used as a bot id
(`BotActionLog` file map, `PlayerbotAI::worldPvpIgnoreGuidLow_`, `ChooseTargetActions` pursuit low guid, `WarriorTriggers.cpp:78` ordering): the counter of a player guid is
unique on both cores, both sides of every compare use the same call, so these agree. They are not used for creatures (a creature counter is not unique across entries on F).

Every place in the 127 files and the `ai/` headers that stores or compares a guid as `uint64`, and what feeds it (grep `GuidKeyCompat`, 62 sites, all in the slice):

| consumer (policy or store) | file:line in the slice that feeds it | helper |
|---|---|---|
| `ActionFailureBackoff::Key(targetGuid, destGuid)` | `Engine.cpp:141,143` (target, rpg target) | `GuidKeyCompat` |
| `WhyTraceService::NoteTickStart(botKey, ...)` | `Engine.cpp:327` | `GuidKeyCompat`; the reader `runtime/WhyTrace.cpp` (outside the slice) keys by `GetRawValue()`: see "outside the slice" |
| action log lines `0x%llx` and the damage throttle map (`BotActionLog.cpp:766` `g`) | `BotActionLog.cpp:480,524,551,561,766,773,780`, `PlayerbotAISpells.cpp` CAST_GATE x10, `PlayerbotAIPackets.cpp:568`, `AttackAction.cpp:101`, `ChooseTargetActions.cpp:250`, `MovementActions.cpp:3318,5084,5091,5131,5139` | `GuidKeyCompat` |
| `BotDiagnostics` sampled target (`BotDiagnostics.h:68`) | `BotDiagnostics.cpp:152` | `GuidKeyCompat` |
| `PlayerbotSecurity` per-sender map | `PlayerbotSecurity.cpp:249` | `GuidKeyCompat` |
| `RitualHolds().Holds(key)` (`runtime/RemoteCommanderTasks.h:415`) | `PlayerbotAI.cpp:2634`, `PlayerbotAISpells.cpp:603,1175` | `GuidKeyCompat`; the inserting side `commands/Commander.cpp:4720,5653` uses `task.botRaw` (outside the slice) |
| `ClaimTable` (`caster`, `std::pair(scope, spell)`) | `GenericSpellActions.cpp:137,139,149,161` | `GuidKeyCompat` for caster and scope (scope = `GroupScopeGuidCompat`, a guid, then keyed) |
| flee / ledge / behind-streak policies (`fleeFailures.Observe/BeginAttempt`, `ledgeStepDown.Due`, `SetBehindStreakPoll`) | `MovementActions.cpp:3784,4084,4243,4818,4959,5015,5249` | `GuidKeyCompat` |
| `LinkedDeathHoldsMember(..., guid)`, `OneAtATimeHolds(solePick, guid)` (`BossTacticsPolicy.h:1783,2198`) | `AoeValues.cpp:33`, `DpsTargetValue.cpp:102,156,567,604,638`, `PossibleTargetsValue.cpp:292-306` | `GuidKeyCompat`; `solePick` comes from `ai::SoleKillGuid` (`BossTacticsActions.cpp:778`, outside the slice) |
| `TankPostUnitPick(keyOn, latched, newGuid, oldGuid)` (`BossTacticsPolicy.h:4360`) | `TankTargetValue.cpp:195,200` | `GuidKeyCompat`; `latched` comes from `ai::TankPostLatchedUnit` (`BossTacticsActions.cpp:12268`, outside the slice: it looks the record up by `bot->GetObjectGuid().GetRawValue()` and returns `arrivedSubjectGuid` stored the same way) |
| `SeekMeetHealWalker(guid, meetPuller)` (`SeekAuraPolicy.h:360`), `HealTargetPolicy` assigned/focus lists | `PartyMemberToHeal.cpp:202,206,258,293` | `GuidKeyCompat`; `meetPuller` comes from `SeekCycleSnapshot` (`BossTacticsActions.cpp`, outside the slice) |
| `ThreatValues` maps (`g_threatCapLog`, `g_hexScan` pair with the instance id, `member.key`) | `ThreatValues.cpp:88,143,167` | `GuidKeyCompat` |
| `ObjectGuidCalculatedValue::Format()` (a guid as the text of a value) | `Value.cpp:12` | `std::to_string(GuidKeyCompat(...))`; the same text is built by `LootObjectStack.cpp:474` (`std::to_string(guid.GetRawValue())`, outside the slice) for the qualifier of "should loot object" |
| `PlayerbotAIPackets.cpp:426` `SpeedChangeAck::WriteAck(ack, guid.GetRawValue(), ...)` | a packet field (the mover guid), not a key | still `GetRawValue()`: in the packet table that P left open (does not compile on F, listed in the feature table) |
| `GuidPosition(uint64 guid, pos)` (`GuidPosition.h:13`) | callers `TravelMgr` (outside the slice) | `GuidFromRawCompat` (only 0 has a form); a key must never go in here |

A key produced by anything else, found in the slice's files: none (the only `GetRawValue()` left in the 127 files is the packet field above; the `ai/` headers
`strategy/hunter/HunterTriggers.h:280` and `strategy/warlock/WarlockActions.h:236` also still call it, they are not reached by the slice and are listed for their class
slice). Producers OUTSIDE the slice that feed the same policies with `GetRawValue()` (this is the list the porting rounds must change to `GuidKeyCompat` in the same
change that ports the file, or the policies disagree about which unit a key means): `BossTacticsActions.cpp` (179 places: tank post records, sole kill, seek cycle,
latches), `commands/Commander.cpp` (237), `LootAction.cpp` (25), `runtime/RunTrace.cpp` (12), `AddLootAction.cpp` (11), `runtime/BotManager.cpp` (10, `BotRegistryKey`),
`ViscidusActions.cpp` (9), `runtime/WhyTrace.cpp` (8), `PatchwerkSafety.cpp` (8), `FankrissFocusActions.cpp` (8), `runtime/ObservabilityEmitter.cpp` (6),
`HoldKillBlockersValue.cpp` (6), `TravelMgr.cpp` (6), `host/ViscidusDiagnostics.cpp` (5), `MaintenanceValues.cpp` (5), `PatchwerkSoakerActions.cpp` (5), and 25 smaller files.
The host's `BotRegistryKey` has the same trap (`GetRawValue()` as the key of the bot table).


## 4. Requests to the host, CMake or the lead

(filled in at the end)

## 5. PQuery / PExecute / DirectPExecute with printf placeholders (finding 18)

Three calls with printf placeholders, all in the slice's `.cpp` files (no `.h` of the slice has one; `runtime/HireUnlockKillStore.h:12` only names `PExecute` in a comment).
On ForeverCore each COMPILES (the core's `PQuery` takes a `{}` format), silently drops its arguments and raises no error: the query text is the format string as written.

| file:line | call | format string (and arguments) |
|---|---|---|
| `ai/playerbot/strategy/CustomStrategy.cpp:120` | `CharacterDatabase.PQuery` | `"SELECT action_line FROM ai_playerbot_custom_strategy WHERE name = '%s' and owner = '%u' order by idx"`, `safeQualifier.c_str(), owner` (and `CharacterDatabase.escape_string` at :119, which F lacks) |
| `ai/playerbot/PlayerbotAIConfig.cpp:1776` | `CharacterDatabase.PQuery` | `"SELECT name, guid FROM characters WHERE account = '%u'"`, `accountId` |
| `ai/playerbot/strategy/actions/GenericActions.cpp:237` | `CharacterDatabase.PQuery` | `"SELECT id, entry, owner FROM character_pet WHERE owner = '%u' AND (slot = '%u' OR slot > '%u') "`, `bot->GetGUIDLow(), PET_SAVE_AS_CURRENT, PET_SAVE_LAST_STABLE_SLOT` |

For completeness (no placeholder, safe as written): `PlayerbotAIConfig.cpp:1752` `LoginDatabase.PQuery("SELECT username, id FROM account")`. No `PExecute`, no `DirectPExecute`
in the 127 files or the `ai/` headers. (Outside the slice the same pattern is everywhere: `commands/`, `runtime/`, `PlayerbotDbStore.cpp`: the porting rounds grep them.)


## 6. The remaining error places by feature (input for the linking round, finding 8)

(filled in at the end)

## 7. Numbers and gates

(filled in at the end)
