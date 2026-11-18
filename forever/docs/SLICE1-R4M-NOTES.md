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

(filled in after the code change, see "GUID-key audit")

## 4. Requests to the host, CMake or the lead

(filled in at the end)

## 5. PQuery / PExecute / DirectPExecute with printf placeholders (finding 18)

(filled in at the end)

## 6. The remaining error places by feature (input for the linking round, finding 8)

(filled in at the end)

## 7. Numbers and gates

(filled in at the end)
