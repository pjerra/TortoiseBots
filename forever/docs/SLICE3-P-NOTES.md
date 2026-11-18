# Slice 3, porting worker (threat/tank, pull, orders): notes

Branch `slice3/p` (worktree `~/wt-slice3-p`), build slot 2 (`FOREVER_SLOT=2`). Spec: `FOREVER-SLICE3-SPEC.md` (sections 3, 4, 5 and the lead's decisions D1-D6). One worker did the three
porting packages (lead's decision D5) in the order threat/tank, pull, orders. Core read only at the pin 6517af05; Turtle source at
`~/playerbots-references/penqle-tortoise-wow-modsys/src`. T = Turtle, F = ForeverCore. Not pushed. No live boot (the test server belongs to slice 2's live test).

## 1. Census

`FOREVER_SLOT=2 FOREVER_CENSUS_LIST=forever/SLICE3.txt forever/tools/vm-census.sh "" 10`

| State | Summary line |
|---|---|
| Baseline N0 (c. list file only) | `files 170  compile clean 165  with errors 5  error places 17 (in 5 files)  includes of a header ForeverCore does not have 0` |
| After the 17 places were closed | `files 170  compile clean 170  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` |

Baseline `where.tsv`: ThreatStrategy.cpp 7, WaitForAttackAction.cpp 5, PullActions.cpp 2, PullTriggers.cpp 2, ValueActions.cpp 1. (The spec estimated 20-60; slices 1 and 2 had already ported
`PlayerConvenience`, `Formations`, the position values and the movement actions that the order files lean on.) The list holds 170 files, not the 173 the spec says (the spec counted
the lines of SLICE2.txt, 163 with its comment lines, plus 10).

## 2. The 17 places, by family

| # | File:line (before) | Place | Class | Fix |
|---|---|---|---|---|
| 1-2 | PullActions.cpp:343, :440 | `event.GetSource()` (T: the shim's `#define GetSource getSource`, `ai::Event` only has `getSource`) | (a) | `event.getSource()`: the same member on both cores; on T the macro made the old text exactly this |
| 3 | ValueActions.cpp:38 | `sObjectMgr.GetPlayer(guid)` | (b) existing helper | `GetPlayerCompat(guid)` (T: the same call, TbCompat_Unit.h:389 F: `ObjectAccessor::FindPlayer`) |
| 4-5 | PullTriggers.cpp:189 (two places: the cast `ObjectGuid(uint64)` and `GetPlayer`) | the engage window stores the requester as a `uint64` and the trigger rebuilds a guid from it | (b) new | `PlayerOfGuidKeyCompat(key)` (section 4) |
| 6-7 | WaitForAttackAction.cpp:18, :108 | `Unit::GetCombatReach(victim, ability, flat)` (3 args; F's takes none) | (b) existing helper, (c) | `GetCombatReachCompat(self, victim, ability, flat)` (TbCompat_Unit.h:72), "Different answer" 1 |
| 8-10 | WaitForAttackAction.cpp:49, :66, :83 | `bot->SummonCreature(entry, x, y, z, o, type, despawnMs)` (debug markers of `debug move`) | (b) existing helper | `SummonCreatureCompat(bot, ...)` (TbCompat_A.h:179) |
| 11-13 | ThreatStrategy.cpp:48, :51, :75 | `std::unordered_map<uint64,...>` indexed by `guid.GetRawValue()` (F: 128-bit guid) | (b) existing helper | `GuidKeyCompat(guid)` (the one key, TbCompat_Unit.h:396) |
| 14 | ThreatStrategy.cpp:111 | `SeekCycleSnapshot(instance, guid.GetRawValue(), view)` | (b) existing helper | `GuidKeyCompat(target->GetObjectGuid())` |
| 15 | ThreatStrategy.cpp:217 | `SpellThreatEntry::threat` | (b) new | `SpellFlatThreatCompat(entry)` (section 4) |
| 16-17 | ThreatStrategy.cpp:243, :247 | `Unit::SendPlaySpellVisual(kitId)` (the `debug threat` strategy) | (d) | `DebugSpellVisualCompat` counted under the new unported row `debug spell visuals` (section 7) |

Counts: (a) 2, (b) 12 (of them 4 through a new helper, 8 through helpers that already existed), (c) 2 (one number difference, listed), (d) 2 (one new row, nothing is shown).
The 5 other files (TankAssistStrategy, StayStrategy, PassiveStrategy, StayActions, ChatShortcutActions) compiled as they were.

## 3. Every threat call of the ten files and their headers is a pure READ

Rule (findings 16, spec section 5): the engine runs on the world thread before the maps update (World.cpp:2465-2475). It may read the core's `ThreatManager` and must never call a
function that writes or re-selects (`GetCurrentVictim` ThreatManager.cpp:245 calls `UpdateVictim`; `AddThreat`, `ScaleThreat`, `MatchUnitThreatToHighestThreat`, `TauntUpdate`,
`ResetAllThreat`, `ClearThreat`, `FixateTarget`, `SetGeneratedThreatMultiplier`, `SetTauntNeverMisses`).

R0 gate (done): a grep of exactly those names (plus the legacy spellings `getCurrentVictim`, `addThreat`, `modifyThreat`, `tauntApply`, `clearReferences`, `UpdateVictim`, `SelectVictim`) over
the 170 `.cpp` of `forever/SLICE3.txt` and the headers of the same base name: **no hit.** Calls that exist in the ten files:

| Place | Call | Core function | Why it is a pure read |
|---|---|---|---|
| ThreatStrategy.cpp:125 (`ThreatResetHolds`) | `sServerFacade.GetThreatManager(target).getThreat(bot)` (shim: `getThreat` is `GetThreat`) | `ThreatManager::GetThreat(Unit const*, bool) const`, ThreatManager.cpp:287 | `const` member: one `find` in `_myThreatListEntries`, returns the amount or 0; no list change |
| ThreatStrategy.cpp:214 | `ThreatValue::GetThreat(bot, target)` (ThreatValues.cpp:229, slice 1) | the same `GetThreat` (ThreatValues.cpp:243) | as above |
| PullActions.cpp:31, :53 (`PullTargetEngaged`) | `target->GetThreatManager().getThreat(bot or member)` | `ThreatManager::GetThreat` ThreatManager.cpp:287 | as above |
| PullActions.cpp, PullTriggers.cpp, ThreatStrategy.cpp:246 | `target->GetVictim()`, `unit->GetVictim()`, `bot->GetAttackers()` | `Unit::GetVictim() const` Unit.h:743 (`m_attacking`), `Unit::getAttackers() const` Unit.h:741 | plain members of the unit, not the threat manager (the threat victim is `ThreatManager::GetLastVictim`, a different call) |
| PullActions.cpp:~463 | `bot->Attack(target, true)` | `Unit::Attack` Unit.cpp:6150 | the call a client's attack packet ends in; sets the bot's own `m_attacking` and the target's attacker set. For a **player** attacker it makes no threat call (the `EngageWithTarget`/`CallAssistance` branch at :6229 is for creatures that are not player controlled). It is an order, not a threat read, and the original code made it too |

Threat-list reads reached through the files' headers and the slice's values (already in slice 1, unchanged): `ThreatValues.cpp:201, :213, :243, :271, :312` and `TankTargetValue.cpp:228-229`
(`GetThreat`, same line), `TargetValue.cpp:30` (hands `ThreatManager&` to `CheckAttacker`, whose four implementations ignore the parameter), `CurrentThreatVictimCompat`
(`GetLastVictim`, ThreatManager.cpp:253, const) and `ForEachThreatener*Compat` (`GetThreatenedByMeList`, `IsThreateningAnyone`, const).

## 4. Helpers: reused versus new

Reused (grep first; no second definition): `GuidKeyCompat`, `GetPlayerCompat`, `GetCombatReachCompat`, `SummonCreatureCompat`, `CurrentThreatVictimCompat`, `ForEachThreatener*Compat`,
`CreatureTauntImmuneCompat` (through `TargetTauntImmune`), the shim's `getThreat`/`TEMPSPAWN_*` spellings. **8 reused, 3 new**, all in the pair
`forever/src/compat/TbCompat_S3.h` / `ai/cmangos-compat-s3.h` (one include line each, before `TbCompat_Position.h`):

| New helper | F | T | Core lines |
|---|---|---|---|
| `SpellFlatThreatCompat(SpellThreatEntry const&)` | `flatMod` as a float | `threat` | F SpellMgr.h:362 (int32 flat threat of the `spell_threat` row), T SpellMgr.h:108 (uint16, same row) |
| `DebugSpellVisualCompat(Unit*, uint32)` | counted under `Feature::DebugVisuals`, nothing shown | `unit->SendPlaySpellVisual(id)` | T Unit.cpp:11476 (SMSG_PLAY_SPELL_VISUAL: guid + a SpellVisualKit.dbc index); F Unit.cpp:12952/12989 take a target and the new client's ids |
| `PlayerOfGuidKeyCompat(uint64)` | scans `ObjectAccessor::GetPlayers()` under `HashMapHolder<Player>::GetLock()` (ObjectAccessor.h:84-91), returns the in-world player whose `GuidKeyCompat` equals the key | `sObjectMgr.GetPlayer(ObjectGuid(key))` (the original expression) | the key is a hash on F and cannot be turned back into a guid (TbCompat_Unit.h:391-395) |

## 5. Different answers (the live test knows where F and T can differ)

1. `WaitForAttackAction.cpp:18, :108`: how far a mob is from "being able to hit" the bot reads `GetCombatReachCompat`: T `GetCombatReach(victim, ability, flat)` = both combat reaches + leeway; F
   `GetMeleeRange(victim)` + flat (TbCompat_Unit.h:72). The safe-distance ring a waiting bot keeps from the pack is therefore slightly different. Existing rule, not new.
2. `SpellFlatThreatCompat`: F's value is signed (a spell can carry a negative flat threat), T's is unsigned; the ThreatCap interrupt rule adds it to the bot's threat, so a negative row lowers the line.
3. `PlayerOfGuidKeyCompat` on F finds only a player that is in the world (T `GetPlayer` also finds a player that is logging in); the caller needs the requester in the world.
4. `debug threat` shows no colour cue on F (section 7).

## 6. Factory (forever/src/slice/SliceAiFactory.cpp) and registry

Roles are read from the forced role (`.tb role` stores it player-wide and on the bot, then resets the strategies, which runs this file again). tank: combat `protection`, `tank assist`, `pull`,
`close`, `tank face`; non-combat `protection`, `tank assist`, `follow`, `food`, `default`; reaction `protection` (AiFactory.cpp:2085). dps warrior: the old kit plus `threat`
(where `KeepsPartyThreatBackoff` says so) and non-combat `food`. priest: the old kit plus `threat`. `GetPlayerRoles` answers the stored forced role first. `KeepsPartyThreatBackoff` is
the full factory's text (AiFactory.cpp:633-642) compiled in the slice factory; the full one also calls `BotCommands::NoteCommanderThreat` (the commander's unbind mark), left out because the slice
has no unbind: the strategy is still given. With the host's constant `IsInCommandedGroup == false` today a bot with a console master gets NO `threat`; it needs the host answer (NEEDS FROM host 1).

45 keys: `forever/docs/SLICE3-KEYS.md` (creator file:line for each), applied through `forever/tools/slice_gen_context.py` (`SLICE3_KEYS`; `--check` current; `slice: registered 15/44/72/168`).
`slice_registry_check.py`: (a) = 0, now reads `forever/SLICE3.txt`. Two small tool changes: `DONOR_ONLY_HEADERS` in the generator (the unused donor header
`strategy/triggers/WaitForAttackTriggers.h` defines `WaitForAttackSafeDistanceTrigger` a second time and stopped the class finder).

## 7. Unported rows

New: **`debug spell visuals`** (`Feature::DebugVisuals`, UnportedFeatures.h, last row; UNPORTED.md): the `debug threat` strategy's two colour cues. Failure value: nothing is shown. "Less" value: the
bot does the same fight, only without a debug marker (it never feeds a decision). One call site (ThreatStrategy.cpp, two lines). No other row.

## 8. Link stand-ins (SliceLinkStubs.cpp)

(filled in with the link analysis, section below)

## 9. NEEDS FROM host

1. `TbHostOwnership.cpp:159` `IsInCommandedGroup(bot)` must answer true for a bot bound by `.tb master` (spec section 2). The factory reads it for `threat`.
2. `NoteCommanderThreat` is not called (see section 6); nothing needed.
3. Order queue (host): the actions run through `DoSpecificAction(key, Event("tb order", ...), true)`: keys `follow chat shortcut`, `stay chat shortcut`, `follow target`, `attack rti target`, `attack my target`,
   `pull my target`, `end pull`, `tank assist`: all registered now (SLICE3-KEYS.md). Of the order surface the keys `come` (position["stay"] write + `SetMovementStrategy("stay")`), `hold`, `free`, `assist`
   stay host code as the spec says.

## 10. Gates

(filled at the end)
