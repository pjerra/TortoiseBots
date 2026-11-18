# Slice 1, package R9: the review fix round

Branch `slice1/integ` (worktree `~/wt-slice-integ`), default build slot. T = Turtle core, F = pinned ForeverCore. Input: the review gate on d22bcaec4
(`FOREVER-SLICE1-FINDINGS.md`, rows R1', R2 to R12 and the rule). R7 and R8 were applied by the live-test package: verified in
(`TbCompat_P.h:120` bounds the effect index before `GetEffect`, `TbCompat_Unit.h:470` skips aura effects with index >= 3), not redone.

## 1. The polarity audit (item 1, R2 and the rule)

Rule: every Unported helper and link stand-in that feeds a PERMIT / DENY decision fails toward the bot doing LESS. Method: for each stand-in the
real definition was read in the file it is tagged with (the Turtle original of an Unported helper is in `ai/cmangos-compat-unported.h`), then its callers
in the slice (`.cpp` files of `forever/SLICE1.txt` plus every header) and the effect of the value at each caller. What "feature absent" means per group:
boss tactics = the table has no row for the map and entry (every tactic reader is row-gated); travel = no node map and no transfer table; texts = no
`ai_playerbot_texts` table (it is not in ForeverCore's world DB); loot, flee, guild orders = nothing stored.

Numbers: **161 stand-in definitions** (the 158 symbols of the R6B count; the constructors and the two table getters are listed too) and **109
Unported / both-core helpers** in `TbCompat_Unported.h`.

* Stand-ins: same 70, less 20, fixed 15, no-op 56. Unported helpers: same 57, less 18, fixed 6, no-op 28 (kinds are defined above each table).
* **Rows that PERMITTED what the original blocks, now block** (the dangerous kind): `HealTopUpAllowedFor` (top-up heals at any mana; now the real
  `mana >= mediumMana` rule), `TauntTargetHeldByOtherTank` (taunted a target another tank holds), `CompositeChatFilter::Filter` (an `@role` or `@group` command
  reached every bot), `CreatureSchoolImmuneMaskCompat` and `CreatureMechanicImmuneMaskCompat` (the school gate and the stun pick permitted immune targets),
  `PlayerHiddenGmCompat` (a hidden GM counted as missing a buff), `GameObjectNoInteractCompat` and `CanEnterAreaCompat` (unknown object / area read as "may"; both
  unreachable today), `FankrissHealReachable` (unreachable behind `FankrissHealingActive`).
* **Rows whose value was wrong in the other direction or not the real answer, now the real one**: `ClampMoveToBossAnchor` (false -> true, no anchor = move
  allowed), `StayWalkDueNow` (stand-in gone: `PositionAction.cpp` joined the slice), `GrindTargetValue::IsAllowedInstanceTarget` (false -> true outside a grouped
  dungeon), `TravelMgr::MapTransDistance` (0.0 -> FLT_MAX across maps, real same-map distance), `TravelMgr::CollectMapTransferPortals`,
  `TravelNodeMap::getMapOffset`, `EnemyPlayerValue::GetMaxAttackDistance` (0 -> 60 outside a battleground), `ChatFilter::Filter` (real strip), `PlayerbotTextMgr::GetBotText`
  (key -> empty), `GetLocalePriority` (0 -> -1).
* **One permit that cannot be fixed from the module: `ExcludeSteepSlopesCompat`.** T excludes steep ground from a plan; F's `PathGenerator` has no exclude call
  (its filter is private and only penalises slope), so the bot may plan over steeper ground than on T. `NavExcludeFlagsCompat` still excludes steep ground
  for the corrected-point query. Next slice: a core-side exclude or a slope check on the returned path.
* **Limit that is not a polarity question: raid-encounter rules.** The Fankriss (map 531), Patchwerk (533), Viscidus and Solnius stand-ins answer exactly what the
  real functions answer outside their encounter, and nothing inside it, because the boss rules are not ported (raids are not in slice 1). A bot sent into those
  fights would play them without the holds; not a permit the audit can turn into "less".
* Not audited as stand-ins (they answer from the host, not the legacy tree): the 59 host services of `forever/src/host/TbHost*.cpp` (R6B notes 3a). Two of them are
  not "false": `BotCommands::NoteHumanChatOrder` and `RaidTankHealerSeat` answer true / not seated by design; they gate no combat decision.

### The immunity port (R2)

F keeps a creature's template immunities in a `CreatureImmunities` row: `CreatureTemplate::CreatureImmunitiesId` (`CreatureData.h:533`) names it,
`SpellMgr::GetCreatureImmunities(id)` returns it (`SpellMgr.h:750`; the struct is `SpellMgr.h:566`: `School`, `Mechanic`, ... bitsets), and
`Creature::LoadTemplateImmunities` (`Creature.cpp:2540`) applies the row as the unit's school (`1 << school`) and mechanic (index = the Mechanics value)
immunities. T keeps two masks on the template (`school_immune_mask`, bit `1 << school`; `mechanic_immune_mask`, bit `1 << (mechanic - 1)`, `Creature.cpp:2172` of the
Turtle tree). `CreatureSchoolImmuneMaskCompat` / `CreatureMechanicImmuneMaskCompat` (`TbCompat_Unported.h:319`, `:329`) rebuild T's two masks from the row, so
`SpellSchoolImmune` (`GenericSpellActions.cpp:430`) and `StunMechanicImmune` (`PlayerbotAISpells.cpp:1867`, bit MECHANIC_STUN - 1) read what they read on T. A template with no
row gives 0, as on T. The feature row "creature immunity data" and the "visibility (GM)" row left `UnportedFeatures.h` and `UNPORTED.md` (56 rows now).

### 1a. The link stand-ins (forever/src/slice/SliceLinkStubs.cpp), 161 rows

Kind: `same` = the stand-in equals the real function's "feature absent" answer; `less` = it differs and the bot does less; `fixed` = changed in R9 (the stand-in permitted something the original blocks, or answered something the real function does not); `noop` = a void function, a constructor or a loader with nothing to answer.

| line | stand-in | real answer when the feature is absent | value now (before -> after) | callers in the slice | effect at the callers | kind |
|---|---|---|---|---|---|---|
| 97 | `GetBossTactics` | the table the DB read filled; with no rows it is empty | empty table | PartyMemberToHeal.cpp:244, PartyMemberToHeal.cpp:249, TankTargetValue.cpp:92 +14 | neutral: every reader asks the table for a row of its map/entry and gets none | same |
| 104 | `GetDungeonPlans` | the table the DB read filled; with no rows it is empty | empty table | PlayerbotAISpells.cpp:1870, PartyMemberToHeal.cpp:57, WarlockSpreadTargeting.h:29 | neutral: every reader asks the table for a row of its map/entry and gets none | same |
| 111 | `LoadBossTactics` | reads ai_boss_tactic / ai_boss_stand / the plan tables into the table (none exist in this DB) | no-op | PlayerbotAIConfig.cpp:1600 | neutral: the tables stay empty | same |
| 112 | `LoadBossStands` | reads ai_boss_tactic / ai_boss_stand / the plan tables into the table (none exist in this DB) | no-op | PlayerbotAIConfig.cpp:1604 | neutral: the tables stay empty | same |
| 113 | `LoadDungeonPlans` | reads ai_boss_tactic / ai_boss_stand / the plan tables into the table (none exist in this DB) | no-op | PlayerbotAIConfig.cpp:1606 | neutral: the tables stay empty | same |
| 118 | `BossTacticsScanScope::BossTacticsScanScope` | opens a per-update scan slot | no-op | PlayerbotAI.cpp:383, BossTacticsActions.h:42, BossTacticsActions.h:45 +3 | neutral: no scan exists | noop |
| 119 | `BossTacticsScanScope::~BossTacticsScanScope` | opens a per-update scan slot | no-op | none in the slice | neutral: no scan exists | noop |
| 120 | `AoeTauntStillNeeded` | counts the group's loose adds (not boss-gated): true when the group has enough | false | WarriorActions.h:104, GenericTriggers.cpp:396, DruidActions.h:701 | blocks the AoE taunt actions (isUseful false); the real answer could permit one | less |
| 121 | `AreaTauntRefusedByPost` | false unless a live tank post (table-driven) reserves a unit near the bot | false | WarriorActions.h:106, DruidActions.h:703 | neutral: no post | same |
| 122 | `ClampMoveToBossAnchor` | TRUE when there is no live anchor row (the move is allowed, point untouched); false only when the clamped point has no floor | false -> TRUE (R9) | PositionAction.cpp:231, MovementActions.cpp:3497, MovementActions.cpp:3520 +1 | was: refused every stay walk and anchored move (and made StayWalkPoint fail); now allows them | fixed |
| 123 | `DispelCastBlocked` | false unless a dispel row (boss table) excludes the cast | false | PlayerbotAISpells.cpp:1149 | neutral: no row | same |
| 124 | `FindBossStand` | invalid stand target on a map without stand rows | invalid target | MovementActions.cpp:4558, ReachTargetActions.h:948, ReachTargetActions.h:1128 +1 | neutral: no stand seat is sought | same |
| 125 | `FindTankPostHealDuty` | empty duty with no live tank-post record | empty duty | PartyMemberToHeal.cpp:181 | neutral | same |
| 126 | `GateHoldFighters` | count of fighters of an entry (AI value); asked only for a hold/gate row | 0 | DpsTargetValue.cpp:162, BossTacticsActions.h:84 | neutral: called only under a hold_kill / gate_hold row, and there is none | same |
| 129 | `HealTopUpAllowedFor` | with no live heal-reserve row: today's mana rule, mana >= mediumMana (BossTacticsActions.cpp:9550) | true -> mana rule (R9) | HealthTriggers.cpp:67 | was: top-up heals at any mana (permitted what the original blocks at low mana); now blocks below mediumMana | fixed |
| 130 | `HealReserveRefusesTopUp` | false when there is no live reserve row | false | PartyMemberToHeal.cpp:303 | neutral | same |
| 131 | `HoldKillBlockers` | count of fighters of an entry (AI value); asked only for a hold/gate row | 0 | DpsTargetValue.cpp:160, BossTacticsActions.h:80 | neutral: called only under a hold_kill / gate_hold row, and there is none | same |
| 132 | `IsDpsHeld` | false when the map has no matching tactic row / no walk-out task is running | false | WarriorGapCloserPolicy.h:49, DpsTargetValue.cpp:123, WarriorGapCloser.cpp:120 | neutral: the hold that would block is row-gated and there are no rows | same |
| 133 | `IsLiveWalkoutPuller` | false when the map has no matching tactic row / no walk-out task is running | false | PlayerbotAI.cpp:1184, BotStateTriggers.cpp:94 | neutral: the hold that would block is row-gated and there are no rows | same |
| 134 | `IsRangedOnlyHeld` | false when the map has no matching tactic row / no walk-out task is running | false | TankTargetValue.cpp:172, TankTargetValue.cpp:328, DpsTargetValue.cpp:119 | neutral: the hold that would block is row-gated and there are no rows | same |
| 135 | `IsSeekSourceForTankPickup` | false when the map has no matching tactic row / no walk-out task is running | false | TankTargetValue.cpp:190 | neutral: the hold that would block is row-gated and there are no rows | same |
| 136 | `IsSoughtCloudFor` | false when the map has no matching tactic row / no walk-out task is running | false | NearestGameObjects.cpp:226, AoeValues.cpp:116 | neutral: the hold that would block is row-gated and there are no rows | same |
| 137 | `LiveBossAnchor` | false with no anchor row for the map | false | MovementActions.cpp:3463 | neutral: no anchor (the caller's "no anchor" branch) | same |
| 138 | `MeetSonChaseExempt` | false when the map has no matching tactic row / no walk-out task is running | false | MovementActions.cpp:3463 | neutral: the hold that would block is row-gated and there are no rows | same |
| 139 | `ReleaseWalkoutPet` | returns at once when no pet hold exists | no-op | PlayerbotAI.cpp:384 | neutral | noop |
| 140 | `ScanBossLinkedDeath` | empty scan when the table has no linked-death group | empty scan | DpsTargetValue.cpp:550, AoeValues.cpp:25 | neutral | same |
| 141 | `SeekMeetHealPuller` | 0 with no meet / sole-kill candidate / post record | 0 | PartyMemberToHeal.cpp:255, BossTacticsActions.h:614 | neutral | same |
| 142 | `SeekMeetPrePlaceHoldsChase` | false when the map has no matching tactic row / no walk-out task is running | false | WarriorGapCloser.cpp:106 | neutral: the hold that would block is row-gated and there are no rows | same |
| 143 | `SoleKillGuid` | 0 with no meet / sole-kill candidate / post record | 0 | DpsTargetValue.cpp:102, DpsTargetValue.cpp:156, BossTacticsActions.h:99 | neutral | same |
| 144 | `TankIgnoresTarget` | false when the map has no matching tactic row / no walk-out task is running | false | PossibleAttackTargetsValue.cpp:323 | neutral: the hold that would block is row-gated and there are no rows | same |
| 145 | `TankPostLatchedUnit` | 0 with no meet / sole-kill candidate / post record | 0 | TankTargetValue.cpp:198, BossTacticsActions.h:209 | neutral | same |
| 146 | `TauntRefusedByGate` | false unless a tank post reserves the subject or a needs_aura row refuses | false | ShamanActions.h:527, WarriorActions.h:105, WarriorActions.h:199 +6 | neutral: no rows | same |
| 147 | `TauntTargetHeldByOtherTank` | NOT boss logic: true when the target's victim is another tank (the taunt stands down), TauntStandsDownForTank | false -> real body (R9) | ShamanActions.h:528, WarriorActions.h:200, WarriorActions.h:468 +4 | was: permitted a taunt the original blocks when another tank holds the target; now blocks it | fixed |
| 152 | `CharmedAllyHoldMultiplier::GetValue` | 1.0 with no boss row (no change) | 1.0 | PlayerbotAI.cpp:569, PlayerbotAI.cpp:573, PlayerbotAI.cpp:574 +409 | neutral | same |
| 153 | `TrapHoldMultiplier::GetValue` | 1.0 with no boss row (no change) | 1.0 | PlayerbotAI.cpp:569, PlayerbotAI.cpp:573, PlayerbotAI.cpp:574 +409 | neutral | same |
| 156 | `FankrissBlocksAoe` | false/null outside map 531 or when the Fankriss strategies are not on | false/null | PlayerbotAISpells.cpp:498, PlayerbotAISpells.cpp:842, PlayerbotAISpells.cpp:978 +5 | neutral outside Ahn'Qiraj (531); inside it no Fankriss rule runs (raids are not in slice 1) | same |
| 157 | `FankrissBurnSpawn` | false/null outside map 531 or when the Fankriss strategies are not on | false/null | GenericTriggers.cpp:315, GenericTriggers.cpp:322, GenericTriggers.cpp:406 +3 | neutral outside Ahn'Qiraj (531); inside it no Fankriss rule runs (raids are not in slice 1) | same |
| 158 | `FankrissDpsMayAttack` | true unless a Fankriss single-target hold is active | true | AttackAction.cpp:263 | neutral: equals the real "not active" answer | same |
| 159 | `FankrissHealReachable` | a plain reach check (alive, in map, hurt, friendly, a heal in range); its only caller here is behind FankrissHealingActive (false) | true -> false (R9) | PartyMemberToHeal.cpp:350 | unreachable today; "not reachable" is the answer that does less | fixed |
| 160 | `FankrissHealingActive` | false/null outside map 531 or when the Fankriss strategies are not on | false/null | PartyMemberToHeal.cpp:347, PartyMemberToHeal.cpp:385 | neutral outside Ahn'Qiraj (531); inside it no Fankriss rule runs (raids are not in slice 1) | same |
| 161 | `GetAssignedFankrissSpawnTankTarget` | false/null outside map 531 or when the Fankriss strategies are not on | false/null | TankTargetValue.cpp:303 | neutral outside Ahn'Qiraj (531); inside it no Fankriss rule runs (raids are not in slice 1) | same |
| 162 | `GetAssignedFankrissTankTarget` | false/null outside map 531 or when the Fankriss strategies are not on | false/null | TankTargetValue.cpp:301 | neutral outside Ahn'Qiraj (531); inside it no Fankriss rule runs (raids are not in slice 1) | same |
| 163 | `GetFankrissDpsTarget` | false/null outside map 531 or when the Fankriss strategies are not on | false/null | DpsTargetValue.cpp:518, DpsTargetValue.cpp:671 | neutral outside Ahn'Qiraj (531); inside it no Fankriss rule runs (raids are not in slice 1) | same |
| 164 | `GetFankrissSpawnTarget` | false/null outside map 531 or when the Fankriss strategies are not on | false/null | DpsTargetValue.cpp:520, DpsTargetValue.cpp:570, DpsTargetValue.cpp:673 | neutral outside Ahn'Qiraj (531); inside it no Fankriss rule runs (raids are not in slice 1) | same |
| 165 | `IgnoreFankrissGauntlet` | false/null outside map 531 or when the Fankriss strategies are not on | false/null | TargetValue.cpp:27, InvalidTargetValue.cpp:23, PossibleAttackTargetsValue.cpp:369 +1 | neutral outside Ahn'Qiraj (531); inside it no Fankriss rule runs (raids are not in slice 1) | same |
| 166 | `RetainFankrissFocusTarget` | false/null outside map 531 or when the Fankriss strategies are not on | false/null | CurrentTargetValue.cpp:23 | neutral outside Ahn'Qiraj (531); inside it no Fankriss rule runs (raids are not in slice 1) | same |
| 167 | `UpdateFankrissFocus` | returns at once outside the encounter (map/boss gated) | no-op / false | PlayerbotAI.cpp:910 | neutral outside the encounter; inside it no rule runs (raids are not in slice 1) | noop |
| 168 | `UpdateFankrissHealing` | returns at once outside the encounter (map/boss gated) | no-op / false | PlayerbotAI.cpp:909 | neutral outside the encounter; inside it no rule runs (raids are not in slice 1) | noop |
| 171 | `GetAssignedPatchwerkTankTarget` | null/false outside map 533 (Naxxramas) and without the patchwerk strategies | null/false | TankTargetValue.cpp:307 | neutral outside 533; inside it the opening hold and soak rules do not run (raids are not in slice 1) | same |
| 172 | `GetPatchwerkSoakBoss` | null/false outside map 533 (Naxxramas) and without the patchwerk strategies | null/false | PlayerbotAI.cpp:988, PlayerbotAI.cpp:1460, CombatStrategy.cpp:172 +1 | neutral outside 533; inside it the opening hold and soak rules do not run (raids are not in slice 1) | same |
| 173 | `GetPatchwerkSoakPuller` | null/false outside map 533 (Naxxramas) and without the patchwerk strategies | null/false | PlayerbotAI.cpp:956 | neutral outside 533; inside it the opening hold and soak rules do not run (raids are not in slice 1) | same |
| 174 | `PatchwerkHealingTarget` | null/false outside map 533 (Naxxramas) and without the patchwerk strategies | null/false | PartyMemberToHeal.cpp:175, RaidTankHealing.cpp:37, RaidTankHealing.cpp:58 | neutral outside 533; inside it the opening hold and soak rules do not run (raids are not in slice 1) | same |
| 175 | `PatchwerkDpsOpeningHold` | null/false outside map 533 (Naxxramas) and without the patchwerk strategies | null/false | PlayerbotAI.cpp:927, AttackAction.cpp:266, ReachTargetActions.h:113 +1 | neutral outside 533; inside it the opening hold and soak rules do not run (raids are not in slice 1) | same |
| 176 | `PatchwerkOpeningBlocksSpell` | null/false outside map 533 (Naxxramas) and without the patchwerk strategies | null/false | PlayerbotAI.cpp:933, PlayerbotAISpells.cpp:498, PlayerbotAISpells.cpp:842 +5 | neutral outside 533; inside it the opening hold and soak rules do not run (raids are not in slice 1) | same |
| 177 | `PatchwerkThreatTank` | null/false outside map 533 (Naxxramas) and without the patchwerk strategies | null/false | WarriorActions.h:552, WarriorTriggers.cpp:100, WarriorTriggers.cpp:125 | neutral outside 533; inside it the opening hold and soak rules do not run (raids are not in slice 1) | same |
| 178 | `UpdatePatchwerkSafety` | returns at once outside the encounter (map/boss gated) | no-op / false | PlayerbotAI.cpp:916 | neutral outside the encounter; inside it no rule runs (raids are not in slice 1) | noop |
| 179 | `WakePatchwerkPullMain` | returns at once outside the encounter (map/boss gated) | no-op / false | PlayerbotAI.cpp:995 | neutral outside the encounter; inside it no rule runs (raids are not in slice 1) | noop |
| 181 | `PatchwerkMainEngageAction::Execute` | built only when GetPatchwerkSoakBoss is non-null (never here) | false | PlayerbotAI.cpp:951, PlayerbotAI.cpp:978, PlayerbotAI.cpp:1008 +481 | unreachable | same |
| 182 | `PatchwerkSoakStageAction::Execute` | built only when GetPatchwerkSoakBoss is non-null (never here) | false | PlayerbotAI.cpp:951, PlayerbotAI.cpp:978, PlayerbotAI.cpp:1008 +481 | unreachable | same |
| 183 | `PatchwerkSoakEngageAction::isUseful` | built only when GetPatchwerkSoakBoss is non-null (never here) | false | PlayerbotAI.cpp:1003, PlayerbotAI.cpp:4065, PlayerbotAI.cpp:4072 +527 | unreachable | same |
| 184 | `PatchwerkSoakEngageAction::Execute` | built only when GetPatchwerkSoakBoss is non-null (never here) | false | PlayerbotAI.cpp:951, PlayerbotAI.cpp:978, PlayerbotAI.cpp:1008 +481 | unreachable | same |
| 187 | `GetViscidusCombatTarget` | null/empty/false unless the Viscidus strategy is on and the target is entry 15299 | null/empty/false | InvalidTargetValue.cpp:68, DpsTargetValue.cpp:526 | neutral outside the Viscidus fight | same |
| 188 | `GetViscidusToxinHazards` | null/empty/false unless the Viscidus strategy is on and the target is entry 15299 | null/empty/false | HazardsValue.cpp:167, ViscidusActions.h:12 | neutral outside the Viscidus fight | same |
| 189 | `IsViscidusShatterTarget` | null/empty/false unless the Viscidus strategy is on and the target is entry 15299 | null/empty/false | PossibleAttackTargetsValue.cpp:133, PossibleAttackTargetsValue.cpp:177 | neutral outside the Viscidus fight | same |
| 190 | `UpdateViscidus` | returns at once outside the encounter (map/boss gated) | no-op / false | PlayerbotAI.cpp:900 | neutral outside the encounter; inside it no rule runs (raids are not in slice 1) | noop |
| 191 | `ViscidusBlocksSpell` | null/empty/false unless the Viscidus strategy is on and the target is entry 15299 | null/empty/false | PlayerbotAISpells.cpp:498, PlayerbotAISpells.cpp:1122, PlayerbotAISpells.cpp:1732 | neutral outside the Viscidus fight | same |
| 194 | `FindSolniusAdd` | null/false (Emerald Sanctum add entries only) | null/false | DpsTargetValue.cpp:181 | neutral outside that dungeon | same |
| 195 | `IsSolniusAdd` | null/false (Emerald Sanctum add entries only) | null/false | DpsTargetValue.cpp:179 | neutral outside that dungeon | same |
| 198 | `ThreatResetHolds` | false unless dungeonRunThreatResetWindow is on and a row exists | false | GenericTriggers.cpp:865 | neutral | same |
| 199 | `StayWalkDueNow` | the shared stay-walk rule (distance to the stay point, back-off, movement state), not boss logic | false -> the real function (R9: PositionAction.cpp joined the slice) | PositionAction.cpp:258, GenericTriggers.cpp:1765 | was: no stay walk ever fired (less, but not the real rule); the real rule runs now | fixed |
| 204 | `BuildGuildOrderEnchantIndex` | builds the index from DBC/creature/loot stores | no-op | PlayerbotAIConfig.cpp:1592 | neutral: the index stays empty (no order is ever sourced) | noop |
| 205 | `BuildGuildOrderSourceIndex` | builds the index from DBC/creature/loot stores | no-op | PlayerbotAIConfig.cpp:1591 | neutral: the index stays empty (no order is ever sourced) | noop |
| 206 | `GuildOrderSourcesFor` | empty list for an item with no source | empty | PlayerbotAI.cpp:1752, GuildOrderSources.h:67 | neutral | same |
| 212 | `LogCastCancel` | a log line when AiPlayerbot cast-cancel logging is on | no-op | PlayerbotAI.cpp:522, PlayerbotAI.cpp:936, PlayerbotAISpells.cpp:1787 +10 | neutral: diagnostics only | noop |
| 213 | `LogCastCancelNonMelee` | a log line when AiPlayerbot cast-cancel logging is on | no-op | PlayerbotAI.cpp:968, PlayerbotAI.cpp:996 | neutral: diagnostics only | noop |
| 214 | `LogCastCancelStop` | a log line when AiPlayerbot cast-cancel logging is on | no-op | MovementActions.cpp:2751 | neutral: diagnostics only | noop |
| 216 | `EndFishingSession` | writes the fish-session values | no-op | PlayerbotAI.cpp:532 | neutral: no fishing | noop |
| 218 | `UpdateBotAreaTriggers` | walks a grouped bot in a dungeon through area triggers; clears state otherwise | no-op | PlayerbotAI.cpp:425 | bots never take an area trigger by themselves (less) | less |
| 221 | `CheatAction::GetCheatMask` | the mask of the configured cheat names (default repair, breath; rnd also item) | 0 = none | PlayerbotAIConfig.cpp:1335, PlayerbotAIConfig.cpp:1337 | no cheat is enabled (less than the real default) | less |
| 223 | `EmoteActionBase::GetNumberOfEmoteVariants` | per race and gender table; at least 1 | 1 | PlayerbotAI.cpp:4193 | neutral: only the plain variant (urand(0,0)) | same |
| 225 | `MountValue::IsMountSpell` | true for mount auras, travel form, ghost wolf | false | PlayerbotAISpells.cpp:585, GenericSpellActions.cpp:385 | a mounted bot refuses non-ride-safe casts (blocks more, never fewer); bots do not mount in slice 1 | less |
| 227 | `GrindTargetValue::IsAllowedInstanceTarget` | TRUE outside a grouped dungeon with a real player master; inside one only ordered/marked/engaged targets | false -> real prefix (R9) | ChooseTargetActions.cpp:129 | was: no target allowed anywhere (a bot would grind nothing); now true outside a grouped dungeon, inside one only the explicit attack target | fixed |
| 229 | `SummonAction::FindSpotBeside` | false when no safe ground beside the summoner | false | UseMeetingStoneAction.h:18, MovementActions.cpp:2440 | neutral | same |
| 231 | `ItemUsageValue::PopulateProfessionReagentIds` | fills the reagent / vendor id sets from DBC and npc_vendor | no-op | PlayerbotAIConfig.cpp:1574 | neutral: the sets are empty ("not a reagent", "not sold"); no item-usage action is registered | noop |
| 232 | `ItemUsageValue::PopulateReagentItemIdsForCraftableItemIds` | fills the reagent / vendor id sets from DBC and npc_vendor | no-op | PlayerbotAIConfig.cpp:1576 | neutral: the sets are empty ("not a reagent", "not sold"); no item-usage action is registered | noop |
| 233 | `ItemUsageValue::PopulateSoldByVendorItemIds` | fills the reagent / vendor id sets from DBC and npc_vendor | no-op | PlayerbotAIConfig.cpp:1575 | neutral: the sets are empty ("not a reagent", "not sold"); no item-usage action is registered | noop |
| 235 | `EnemyPlayerValue::GetMaxAttackDistance` | 60 yd outside a battleground (40, 10 late in AV, inside) | 0 -> 60 / 10 in a BG (R9) | AttackersValue.cpp:479 | was 0 = no enemy player in range ever; now the real 60 outside a BG; the value has no reader in the slice | fixed |
| 236 | `WorldPvpSeekChecks::IsSeeking` | false unless AiPlayerbot.WorldPvpSeek is on and the strategy runs | false | AttackersValue.cpp:440, ChooseTargetActions.cpp:401, ChooseTargetActions.cpp:445 | neutral/less: no open-world PvP seek | less |
| 237 | `WorldPvpSeekChecks::IsSeekTarget` | false unless AiPlayerbot.WorldPvpSeek is on and the strategy runs | false | ChooseTargetActions.cpp:425 | neutral/less: no open-world PvP seek | less |
| 240 | `ChatFilter::Filter` | strips the "@..." prefix (base body) | message as it came -> real body (R9) | PlayerbotAI.cpp:2999, ChatFilter.h:9, ChatFilter.h:25 | was: "@tank follow" went on unstripped | fixed |
| 241 | `CompositeChatFilter::CompositeChatFilter` | builds the filter chain (strategy, role, class, race, rti, combat type, level, group) | empty chain | PlayerbotAI.h:1157, ChatFilter.h:20, ChatFilter.h:23 +2 | neutral | noop |
| 242 | `CompositeChatFilter::~CompositeChatFilter` | builds the filter chain (strategy, role, class, race, rti, combat type, level, group) | empty chain | none in the slice | neutral | noop |
| 243 | `CompositeChatFilter::Filter` | each filter strips its "@..." or returns "" when the line is not for this bot | message as it came -> "" for a line that opens with "@" (R9) | PlayerbotAI.cpp:2999, ChatFilter.h:9, ChatFilter.h:25 | was: a role/group command reached every bot; now refused explicitly | fixed |
| 246 | `IdleMobVeto::IdleMobVeto` | collects sleeping mobs near the flee landing | empty | MovementActions.cpp:3843, MovementActions.cpp:4007, MovementActions.cpp:4042 +8 | neutral | noop |
| 247 | `IdleMobVeto::Wakes` | false with no sleeper | false | MovementActions.cpp:4061, MovementActions.cpp:4148 | neutral | same |
| 248 | `FleeManager::CalculateDestination` | false when no safe point exists / no threat | false | MovementActions.cpp:4021, FleeManager.h:62 | the bot never flees by itself (less) | less |
| 249 | `FleeManager::isUseful` | false when no safe point exists / no threat | false | PlayerbotAI.cpp:1003, PlayerbotAI.cpp:4065, PlayerbotAI.cpp:4072 +527 | the bot never flees by itself (less) | less |
| 252 | `LootObject::LootObject` | builds from a guid | copy | LootObjectStack.h:10, LootObjectStack.h:13, LootObjectStack.h:18 +20 | neutral | noop |
| 253 | `LootObject::IsLootPossible` | false when empty / refused | false | PlayerbotAISpells.cpp:1371, PlayerbotAISpells.cpp:1695, AvailableLootValue.h:42 +3 | no loot (less) | less |
| 254 | `LootObject::GetWorldObject` | null for an empty object | null | PlayerbotAI.cpp:493, AvailableLootValue.h:41, AttackersValue.cpp:889 +12 | neutral | same |
| 255 | `LootObjectStack::Add` | false when empty / refused | false | WorldSquare.cpp:28, PlayerbotAISpells.cpp:1491, BossStandPolicy.h:143 +47 | no loot (less) | less |
| 256 | `LootObjectStack::Remove` | erases a map entry | no-op | TravelNode.h:540, TravelNode.h:541, MovementActions.cpp:877 +10 | neutral: the stack is empty | noop |
| 257 | `LootObjectStack::NoteApproachFailure` | erases a map entry | no-op | MovementActions.cpp:5129 | neutral: the stack is empty | noop |
| 258 | `LootObjectStack::IsAbandoned` | false when empty / refused | false | MovementActions.cpp:5130 | no loot (less) | less |
| 266 | `TravelMgr::Clear` | clear / read the travel tables | no-op | PlayerbotAI.cpp:1548, PlayerbotAI.cpp:1549, PlayerbotAI.cpp:2696 +62 | neutral: tables stay empty | noop |
| 267 | `TravelMgr::LoadAreaLevels` | clear / read the travel tables | no-op | PlayerbotAIConfig.cpp:1571 | neutral: tables stay empty | noop |
| 268 | `TravelMgr::LoadQuestTravelTable` | clear / read the travel tables | no-op | PlayerbotAIConfig.cpp:1587 | neutral: tables stay empty | noop |
| 269 | `TravelMgr::LoadTaxiNodeZones` | clear / read the travel tables | no-op | PlayerbotAIConfig.cpp:1595 | neutral: tables stay empty | noop |
| 270 | `TravelMgr::GetAreaLevel` | the area's level from the creature census; 0 for an unknown area | 0 | WorldPosition.h:337, WorldPosition.cpp:888 | neutral: read only by travel destinations and WorldPosition::GetAreaLevel (no caller in the slice) | same |
| 271 | `TravelMgr::SetNullTravelTarget` | sets the null destination and status NONE | no-op | PlayerbotAI.cpp:2142, PlayerbotAI.cpp:2231, PlayerbotAI.cpp:2714 +1 | neutral: a new target is already empty and NONE | noop |
| 272 | `TravelMgr::CollectMapTransferPortals` | clears the list; same map -> (start, 0); else the transfer table (empty here) | no-op -> real (R9) | WorldSquare.cpp:25, TravelMgr.h:540 | was: the out list was left as the caller had it; now cleared, same-map case answered | fixed |
| 274 | `TravelMgr::MapTransDistance` | same map: flat distance; across maps: through the nearest transfer, FLT_MAX with none | 0.0 -> real (R9) | WorldPosition.cpp:191, WorldPosition.cpp:200 | was: every cross-map distance read "right here" (53 call sites); now FLT_MAX, same-map distance real | fixed |
| 276 | `TravelTarget::TravelTarget` | null destination, status EXPIRED | null destination, status NONE | PlayerbotAI.cpp:2121, PlayerbotAI.cpp:2183, PlayerbotAI.cpp:2223 +32 | neutral: IsActive() is false for both | noop |
| 277 | `TravelTarget::~TravelTarget` | releases the grind spot | no-op | none in the slice | neutral | noop |
| 278 | `TravelTarget::IsActive` | false for NONE/EXPIRED/PREPARE | false | EngineValues.cpp:35, HunterTriggers.h:25, HunterTriggers.h:33 +692 | neutral | same |
| 279 | `TravelTarget::SetStatus` | stores the status and its timers | no-op | PlayerbotAI.cpp:2143, PlayerbotAI.cpp:2232, PlayerbotAI.cpp:2715 +1 | neutral: only read for the status text | noop |
| 281 | `TravelNodeMap::getMapOffset` | the map with a zero position when no offset is stored | WorldPosition() -> (mapId,0,0,0) (R9) | TravelNode.h:561, WorldPosition.cpp:462 | display position only | fixed |
| 282 | `TravelNodeMap::getFullPath` | the route over nodes; empty with none | empty path | TravelNode.h:500 | neutral: no route (the bot does not travel) | same |
| 283 | `TravelPath::ClipPath` | edit the path; nothing on an empty path | no-op | LastMovementValue.h:29, MovementActions.cpp:1620, MovementActions.cpp:1737 +1 | neutral | noop |
| 284 | `TravelPath::UpcommingSpecialMovement` | false on an empty path | false | MovementActions.cpp:1003, MovementActions.cpp:2041 | neutral | same |
| 285 | `TravelPath::cutTo` | false on an empty path | false | MovementActions.cpp:877, MovementActions.cpp:913, MovementActions.cpp:919 +1 | neutral | same |
| 286 | `TravelPath::makeShortCut` | edit the path; nothing on an empty path | no-op | LastMovementValue.h:26, MovementActions.cpp:2017 | neutral | noop |
| 290 | `EntryTravelDestination::GetShortName` | a name text | text | PlayerbotAI.cpp:5596, TravelMgr.h:172, TravelMgr.h:201 +2 | neutral | noop |
| 291 | `GrindTravelDestination::IsPossible` | conditions of the destination (strategy, level, config) | false | EngineValues.cpp:15, Action.h:87, RogueTriggers.h:224 +8 | no grind / gather trip is planned (less) | less |
| 292 | `GrindTravelDestination::IsActive` | conditions of the destination (strategy, level, config) | false | EngineValues.cpp:35, HunterTriggers.h:25, HunterTriggers.h:33 +692 | no grind / gather trip is planned (less) | less |
| 293 | `GrindTravelDestination::GetTitle` | a name text | text | PlayerbotAI.cpp:2140, PlayerbotAI.cpp:2141, PlayerbotAI.cpp:2380 +13 | neutral | noop |
| 294 | `GatherTravelDestination::IsPossible` | conditions of the destination (strategy, level, config) | false | EngineValues.cpp:15, Action.h:87, RogueTriggers.h:224 +8 | no grind / gather trip is planned (less) | less |
| 295 | `GatherTravelDestination::IsActive` | conditions of the destination (strategy, level, config) | false | EngineValues.cpp:35, HunterTriggers.h:25, HunterTriggers.h:33 +692 | no grind / gather trip is planned (less) | less |
| 296 | `GatherTravelDestination::GetTitle` | a name text | text | PlayerbotAI.cpp:2140, PlayerbotAI.cpp:2141, PlayerbotAI.cpp:2380 +13 | neutral | noop |
| 303 | `RandomItemMgr::RandomItemMgr` | builds the item caches | no-op | RandomItemMgr.h:165, RandomItemMgr.h:168, RandomItemMgr.h:169 +9 | neutral | noop |
| 304 | `RandomItemMgr::~RandomItemMgr` | builds the item caches | no-op | none in the slice | neutral | noop |
| 305 | `RandomItemMgr::Init` | builds the item caches | no-op | PlayerbotAI.cpp:245, PlayerbotAI.cpp:2754, PlayerbotAI.cpp:3966 +7 | neutral | noop |
| 306 | `RandomItemMgr::GetStatWeight` | 0 for an unknown item / spec | 0 | PlayerbotAI.cpp:4914 | neutral: no caller in the slice (PlayerbotFactory only) | same |
| 307 | `RandomItemMgr::GetStatWeight` | 0 for an unknown item / spec | 0 | PlayerbotAI.cpp:4914 | neutral: no caller in the slice (PlayerbotFactory only) | same |
| 308 | `RandomItemMgr::GetPlayerSpecId` | 0 for an unknown item / spec | 0 | PlayerbotAI.cpp:4909, CastCustomSpellAction.h:121, CastCustomSpellAction.h:123 | neutral: no caller in the slice (PlayerbotFactory only) | same |
| 309 | `RandomItemMgr::GetFood` | an item id of the tier | 0 (no item) | PlayerbotAI.cpp:1620 | neutral: no caller in the slice | same |
| 313 | `PlayerbotTextMgr::PlayerbotTextMgr` | construction | no-op | PlayerbotAI.cpp:108, PlayerbotAI.h:8, PlayerbotTextMgr.h:18 +6 | neutral | noop |
| 314 | `PlayerbotTextMgr::~PlayerbotTextMgr` | construction | no-op | none in the slice | neutral | noop |
| 315 | `PlayerbotTextMgr::LoadBotTexts` | reads ai_playerbot_texts / help texts (the tables are not in ForeverCore's DB) | no-op | PlayerbotAIConfig.cpp:1579 | neutral: no texts | noop |
| 316 | `PlayerbotTextMgr::GetBotText` | "" (with an error log) when no texts are loaded; the placeholder form falls back to the name | the key -> "" (R9); the placeholder form: name with placeholders replaced | PlayerbotHelpMgr.h:7, PlayerbotHelpMgr.h:84, PlayerbotTextMgr.h:7 +4 | was: the bot whispered raw keys ("hello"); now nothing, and every caller skips an empty text | fixed |
| 317 | `PlayerbotTextMgr::GetBotText` | "" (with an error log) when no texts are loaded; the placeholder form falls back to the name | the key -> "" (R9); the placeholder form: name with placeholders replaced | PlayerbotHelpMgr.h:7, PlayerbotHelpMgr.h:84, PlayerbotTextMgr.h:7 +4 | was: the bot whispered raw keys ("hello"); now nothing, and every caller skips an empty text | fixed |
| 318 | `PlayerbotTextMgr::GetLocalePriority` | -1 with players online and no locale counted; 0 only with no session online | 0 -> -1 (R9) | ChatHelper.cpp:428, ChatHelper.cpp:437, ChatHelper.cpp:457 +6 | was: index 0 = a real client locale, not "none"; read for names in ChatHelper | fixed |
| 320 | `PlayerbotHelpMgr::PlayerbotHelpMgr` | construction | no-op | PlayerbotHelpMgr.h:20, PlayerbotHelpMgr.h:23, PlayerbotHelpMgr.h:24 +5 | neutral | noop |
| 321 | `PlayerbotHelpMgr::~PlayerbotHelpMgr` | construction | no-op | none in the slice | neutral | noop |
| 322 | `PlayerbotHelpMgr::LoadBotHelpTexts` | reads ai_playerbot_texts / help texts (the tables are not in ForeverCore's DB) | no-op | PlayerbotAIConfig.cpp:1580 | neutral: no texts | noop |
| 325 | `TalentSpec::GetTalents` | fill the talent list from the stores / the bot | no-op | Talentspec.h:35, Talentspec.h:36, Talentspec.h:38 | neutral: the spec stays empty | noop |
| 326 | `TalentSpec::ReadTalents` | fill the talent list from the stores / the bot | no-op | Talentspec.h:36, Talentspec.h:37, Talentspec.h:38 | neutral: the spec stays empty | noop |
| 327 | `TalentSpec::ReadTalents` | fill the talent list from the stores / the bot | no-op | Talentspec.h:36, Talentspec.h:37, Talentspec.h:38 | neutral: the spec stays empty | noop |
| 328 | `TalentSpec::CheckTalents` | true for a valid spec / link | false | PlayerbotAIConfig.cpp:2162, Talentspec.h:42 | no spec is accepted (less); the config loader no longer asks (R9, PremadeSpecsPortedCompat) | less |
| 329 | `TalentSpec::CheckTalentLink` | true for a valid spec / link | false | PlayerbotAIConfig.cpp:2153 | no spec is accepted (less); the config loader no longer asks (R9, PremadeSpecsPortedCompat) | less |
| 330 | `TalentSpec::LeveltoPoints` | (level-9) * talent rate | 0 | PlayerbotAIConfig.cpp:2162 | no talent points are spent (less) | less |
| 331 | `TalentSpec::SetPublicNote` | fill the talent list from the stores / the bot | no-op | PlayerbotAI.cpp:1903 | neutral: the spec stays empty | noop |
| 334 | `PlayerbotFactory::InitAmmo` | equips ammo / makes a pet | no-op | PlayerbotAI.cpp:781, PlayerbotAI.cpp:800 | neutral: factory is not used for host bots | noop |
| 335 | `PlayerbotFactory::InitPet` | equips ammo / makes a pet | no-op | GenericActions.cpp:179 | neutral: factory is not used for host bots | noop |
| 336 | `PlayerbotFactory::InitPetSpells` | equips ammo / makes a pet | no-op | GenericActions.cpp:180 | neutral: factory is not used for host bots | noop |
| 337 | `PlayerbotFactory::PetAutoTrainAllowed` | a config flag / trains the pet | false | GenericActions.cpp:142, GenericActions.cpp:199 | no pet training (less) | less |
| 338 | `PlayerbotFactory::TrainHunterPet` | a config flag / trains the pet | false | GenericActions.cpp:158, GenericActions.cpp:202 | no pet training (less) | less |
| 341 | `PlayerbotDbStore::Load` | replays the saved strategy rows (table absent here) | no-op | PlayerbotAI.cpp:4250, PlayerbotAIConfig.cpp:1608, SkipSpellsListValue.h:14 +25 | neutral: nothing saved is applied | noop |
| 344 | `RandomBotFacade::LoadNamedLocationCache` | reads named locations / battlemasters / auction prices | no-op | PlayerbotAIConfig.cpp:1598 | neutral: caches stay empty | noop |
| 345 | `RandomBotFacade::LoadBattleMastersCache` | reads named locations / battlemasters / auction prices | no-op | PlayerbotAIConfig.cpp:1609 | neutral: caches stay empty | noop |
| 346 | `RandomBotFacade::LoadAuctionPrices` | reads named locations / battlemasters / auction prices | no-op | PlayerbotAIConfig.cpp:1610 | neutral: caches stay empty | noop |

Counts: 161 rows: same 70, less 20, fixed 15, noop 56.

### 1b. The Unported helpers and both-core helpers of forever/src/compat/TbCompat_Unported.h, 109 rows

| line | helper | Turtle original | ForeverCore value now (before -> after) | callers in the slice | effect | kind |
|---|---|---|---|---|---|---|
| 84 | `KeyringSlotsCompat` | the slot ranges | empty range | PlayerbotAIItems.cpp:82, PlayerbotAIItems.cpp:127, PlayerbotAIItems.cpp:170 +2 | finds no item in those slots (no bank/keyring scan) | same |
| 85 | `BankItemSlotsCompat` | the slot ranges | empty range | PlayerbotAI.cpp:4863, PlayerbotAIItems.cpp:342 | finds no item in those slots (no bank/keyring scan) | same |
| 89 | `LootOfCreatureCompat` | &creature->loot | nullptr | PlayerbotAIItems.cpp:273 | callers test the pointer first: no loot object | same |
| 90 | `LootOfGameObjectCompat` | &creature->loot | nullptr | PlayerbotAIItems.cpp:274 | callers test the pointer first: no loot object | same |
| 94 | `ItemProtoHasDisplayIdCompat` | proto->DisplayInfoID == id | false | PlayerbotAIItems.cpp:825, PlayerbotAIItems.cpp:844 | no item "looks like" the wanted one: the bot finds no substitute (less) | less |
| 99 | `QueueUseItemPacketCompat` | queues CMSG_USE_ITEM | nothing queued | PlayerbotAIItems.cpp:1045 | the item is not used (less) | noop |
| 106 | `ItemProtoFitsSlotCompat` | FindEquipSlot == slot | false | ItemVisitors.h:294 | an item is never "for that slot": no equip by this path (less) | less |
| 110 | `ItemRandomPropertySuffixCompat` | suffix text of the property | false | ChatHelper.cpp:540 | plain names | same |
| 113 | `SpellSummonsCritterCompat` | spell has the critter effect | false | ItemVisitors.h:454 | no companion item is recognised | same |
| 120 | `ItemRandomPropertyIdCompat` | the property id (0 for a plain item) | 0 | ItemUsageValue.h:14, ItemUsageValue.h:15 | same as a plain item on T | same |
| 121 | `NewItemRandomPropertyIdCompat` | the property id (0 for a plain item) | 0 | PlayerbotAI.cpp:1647, PlayerbotAI.cpp:1672, PlayerbotAI.cpp:1775 | same as a plain item on T | same |
| 196 | `OpcodesAgreeCompat` | policy value == opcode | true (reserved numbers, no wire value) | PlayerbotAIPackets.cpp:21, PlayerbotAIPackets.cpp:22, PlayerbotAIPackets.cpp:23 +9 | compile-time pin only | noop |
| 200 | `PackedGuidReadCompat` | reads a packed guid | false (guid stays empty) | PlayerbotAIPackets.cpp:192, PlayerbotAIPackets.cpp:207, PlayerbotAIPackets.cpp:417 +1 | "not for this bot": the packet is ignored (less) | less |
| 206 | `RegisterPacketHandlersCompat` | registers the handler rows | nothing registered, counted | PlayerbotAIPackets.cpp:164 | no legacy packet handler runs | noop |
| 219 | `UnportedBlockCompat` | runs the block | counts, runs nothing | PlayerbotAI.cpp:631, PlayerbotAI.cpp:641, PlayerbotAI.cpp:842 +4 | the block's action does not happen (less) | noop |
| 221 | `UnportedBlockResultCompat` | runs the block, returns its value | counts, returns the caller's `failure` argument | ServerFacade.h:270, PlayerbotAI.cpp:140, PlayerbotAI.cpp:4433 +10 | the failure value is chosen at each of the 14 call sites; see the callers (ServerFacade.h:274 BG queue -> none; PlayerbotAI.cpp chat-link extractors -> nullptr/0) | less |
| 225 | `RegisterDirectPacketCasesCompat` | registers the handler rows | nothing registered, counted | PlayerbotAI.cpp:254 | no legacy packet handler runs | noop |
| 236 | `WorldAverageDiffCompat` | sWorld.GetAverageDiff() | 0 | PlayerbotAI.cpp:1035, MovementActions.cpp:6265, PlayerbotAIPackets.cpp:538 | no lag allowance added to a jump landing time | same |
| 240 | `SetFallInformationCompat` | records the fall start | nothing | PlayerbotAI.cpp:658, PlayerbotAIPackets.cpp:494 | no fall-damage bookkeeping from a bot jump (bots do not jump here) | noop |
| 248 | `TotalTalentPointsCompat` | talent points | 0 | Talentspec.h:5 | no points to spend: nothing is learned (less) | less |
| 249 | `FreeTalentPointsCompat` | talent points | 0 | PlayerbotAI.cpp:259 | no points to spend: nothing is learned (less) | less |
| 253 | `PremadeSpecsPortedCompat` | true (R9, new) | false + one summary line | PlayerbotAIConfig.cpp:2125 | the premade spec loop is skipped; no error per spec | fixed |
| 261 | `TalentTabPageCompat` | tab->tabpage | 0 | Talentspec.h:28 | page 0 | same |
| 266 | `GuidOfCreatureDataCompat` | guid from a spawn record | empty guid | GuidPosition.h:17 | "no such object" | same |
| 268 | `GuidOfGameObjectDataCompat` | guid from a spawn record | empty guid | GuidPosition.h:18 | "no such object" | same |
| 272 | `GuidPositionWriteCompat` | writes guid and position to a packet | nothing | GuidPosition.h:85 | no packet written | noop |
| 284 | `AmmoItemIdCompat` | PLAYER_AMMO_ID | Player::GetAmmoId() (proper) | PlayerbotAI.cpp:771, PlayerbotAI.cpp:785, GenericSpellActions.cpp:1169 +1 | same question, F spelling | same |
| 289 | `UnitSilencedCompat` | unit flags / IsSpellProhibited | silence aura / pacify flag / school lockout (proper) | GenericTriggers.cpp:282 | same question; the wand trigger gates on them | same |
| 290 | `UnitPacifiedCompat` | unit flags / IsSpellProhibited | silence aura / pacify flag / school lockout (proper) | GenericTriggers.cpp:286 | same question; the wand trigger gates on them | same |
| 291 | `SpellProhibitedCompat` | unit flags / IsSpellProhibited | silence aura / pacify flag / school lockout (proper) | GenericTriggers.cpp:283 | same question; the wand trigger gates on them | same |
| 300 | `PetLearnSpellCompat` | pet learn / autocast / controlled-guardian walk | nothing | GenericActions.cpp:168 | no pet action happens (less) | noop |
| 301 | `PetToggleAutocastCompat` | pet learn / autocast / controlled-guardian walk | nothing | GenericActions.cpp:170, GenericActions.cpp:301, GenericActions.cpp:603 | no pet action happens (less) | noop |
| 302 | `PetDismissCommandCompat` | dismisses the pet | false | GenericActions.cpp:443 | not dismissed | same |
| 304 | `ForAllGuardiansCompat` | pet learn / autocast / controlled-guardian walk | nothing | PlayerbotAI.cpp:5363, PlayerbotAI.cpp:5399, GenericActions.cpp:651 | no pet action happens (less) | noop |
| 307 | `UseGameObjectCompat` | use / interact / portal spell of an object | false / nothing | GenericActions.h:102 | "could not use / cannot interact": the bot touches no object (less) | less |
| 311 | `HasThrownSkillCompat` | HasSkill(thrown) | false | PullStrategy.cpp:134 | no thrown-weapon pull (less) | less |
| 319 | `CreatureSchoolImmuneMaskCompat` | template immunity masks | 0 -> the masks rebuilt from CreatureImmunities (R9, proper) | GenericSpellActions.cpp:430 | was: "vulnerable to everything", so the school gate and the stun pick PERMITTED what the original blocks; now they block it | fixed |
| 329 | `CreatureMechanicImmuneMaskCompat` | template immunity masks | 0 -> the masks rebuilt from CreatureImmunities (R9, proper) | PlayerbotAISpells.cpp:1867 | was: "vulnerable to everything", so the school gate and the stun pick PERMITTED what the original blocks; now they block it | fixed |
| 342 | `PlayerHiddenGmCompat` | GetVisibility()==VISIBILITY_OFF | false -> !isGMVisible() (R9, proper) | GenericSpellActions.cpp:767 | was: a hidden GM counted as missing a buff (more casts); now skipped like the original | fixed |
| 346 | `EstimateHealOfEffectCompat` | the heal of the effect | 0 | GenericSpellActions.cpp:973 | "heals nothing": the caller skips the spell (less) | less |
| 354 | `CastItemOnTargetCompat` | the item cast loop | false | GenericSpellActions.cpp:1403 | the cast does not start (less) | less |
| 360 | `AddSpellAndCategoryCooldownsCompat` | item cooldowns | nothing | UseItemAction.h:153, UseItemAction.h:264 | neutral (no item cast starts) | noop |
| 374 | `DbStoreBeginCompat` | begins a transaction, true | false | ChangeStrategyAction.cpp:21, ChangeStrategyAction.cpp:61, ChangeStrategyAction.cpp:133 | nothing is stored (callers skip the write) | less |
| 375 | `DbStoreCommitCompat` | commits | nothing | ChangeStrategyAction.cpp:35, ChangeStrategyAction.cpp:75, ChangeStrategyAction.cpp:149 | neutral | noop |
| 377 | `LoadCustomStrategyLinesCompat` | SELECT custom strategy lines | no lines | CustomStrategy.cpp:118 | no custom strategy loads | same |
| 390 | `CharacterPQueryCompat` | printf query / unique_ptr / escape | proper (printf formatter, shared result) | PlayerbotAIConfig.cpp:1778, GenericActions.cpp:237 | same answers | same |
| 396 | `QueryGuardCompat` | printf query / unique_ptr / escape | proper (printf formatter, shared result) | GenericActions.cpp:237 | same answers | same |
| 398 | `EscapeStringCompat` | printf query / unique_ptr / escape | proper (printf formatter, shared result) | none in the slice | same answers | same |
| 420 | `QuestTriageViewOfCompat` | the quest's level, items, money | empty view (level 0 = scaling, not inactive) | GenericTriggers.h:1750 | the only reader (QuestLogNearlyFull trigger) is not registered | same |
| 427 | `QuestTitleCompat` | quest title | "" | PlayerbotAIQuests.cpp:26, ChatHelper.cpp:429 | no title | same |
| 428 | `RemoveQuestFromLogCompat` | abandons the quest | nothing | PlayerbotAIQuests.cpp:28 | no quest dropped (less) | noop |
| 429 | `QuestRequiredItemsCompat` | the quest's required items | empty | PlayerbotAIQuests.cpp:168 | "no quest needs the item": only matters to sell/destroy decisions, whose actions are not registered | same |
| 441 | `TaxiInstantFinishCompat` | taxi path data | false | TaxiRouteGuard.h:40 | no flight handling (the bot never takes a flight) | same |
| 452 | `SplineSetTransportCompat` | sets the transport on a spline | nothing (F transforms the points itself, proper) | MovementActions.cpp:695, MovementActions.cpp:1559, MovementActions.cpp:3618 | same walk | same |
| 453 | `SplinePointToTransportCompat` | sets the transport on a spline | nothing (F transforms the points itself, proper) | MovementActions.cpp:1543 | same walk | same |
| 457 | `PortalSpellOfGameObjectCompat` | use / interact / portal spell of an object | false / nothing | MovementActions.cpp:1127 | "could not use / cannot interact": the bot touches no object (less) | less |
| 459 | `UsePortalObjectCompat` | use / interact / portal spell of an object | false / nothing | MovementActions.cpp:1153 | "could not use / cannot interact": the bot touches no object (less) | less |
| 463 | `TaxiDestinationCompat` | taxi path data | false | MovementActions.cpp:2685 | no flight handling (the bot never takes a flight) | same |
| 475 | `NavMeshProbeQueryCompat` | the navmesh query of the map | nullptr | MovementActions.cpp:2328 | the island probe answers "not isolated": no unstick teleport (less) | less |
| 477 | `NavMeshFindWalkPolyCompat` | PathInfo::FindWalkPoly | 0 (invalid poly) | MovementActions.cpp:2339, MovementActions.cpp:2342 | probe finds nothing | same |
| 488 | `EndCinematicIfWatchingCompat` | ends a watched cinematic | proper (CinematicMgr) | PlayerbotAI.cpp:410 | same | same |
| 501 | `LootGuidCompat` | the open loot window | proper (GetLootGUID); counts only a real window | PlayerbotAI.cpp:486 | same | same |
| 503 | `ReleaseLootIfFarCompat` | the open loot window | proper (GetLootGUID); counts only a real window | PlayerbotAI.cpp:498 | same | same |
| 513 | `BroadcastFacingCompat` | extra facing broadcast | nothing (F's SetFacingTo does it) | PlayerbotAI.cpp:1202 | same | same |
| 519 | `CanEnterAreaCompat` | instance full / locked / bound check | true -> FALSE (R9) | PlayerbotAI.cpp:1391 | was: "no restriction"; now the bot does not take the trigger. Only random bots ask (PlayerbotAI::CanEnterArea), a host bot is never one | fixed |
| 523 | `DismountCompat` | dismounts | proper | PlayerbotAI.cpp:1401 | same | same |
| 531 | `EnsureLockpickSkillCompat` | skill / rest bonus / breath / speed writes | nothing | PlayerbotAI.cpp:1711 | no cheat applied (less) | noop |
| 532 | `EnsureCampSurvivalSkillCompat` | skill / rest bonus / breath / speed writes | nothing | PlayerbotAI.cpp:1726 | no cheat applied (less) | noop |
| 533 | `SetRestBonusCompat` | skill / rest bonus / breath / speed writes | nothing | PlayerbotAI.cpp:1830 | no cheat applied (less) | noop |
| 534 | `ClearWaterBreathingCompat` | skill / rest bonus / breath / speed writes | nothing | PlayerbotAI.cpp:745 | no cheat applied (less) | noop |
| 536 | `FishingZoneOfAreaCompat` | the zone of an area | 0 | PlayerbotAI.cpp:1758 | the caller keeps the area id | same |
| 538 | `SpeedCheatCompat` | skill / rest bonus / breath / speed writes | nothing | PlayerbotAI.cpp:741 | no cheat applied (less) | noop |
| 542 | `SessionHasInstantLogoutCompat` | security >= InstantLogout | RBAC_PERM_INSTANT_LOGOUT (proper) | PlayerbotAI.cpp:2530, PlayerbotAI.cpp:2536 | same | same |
| 547 | `RemoveOnTransportFlagCompat` | clears the transport flag | proper / nothing (no flag on F) | PlayerbotAI.cpp:863, PlayerbotAI.cpp:873, PlayerbotAI.cpp:893 +1 | same | same |
| 550 | `InterruptCurrentMovementCompat` | generator interrupt, heartbeat | nothing | PlayerbotAI.cpp:2586 | movement not interrupted by this path | noop |
| 551 | `SendHeartBeatCompat` | generator interrupt, heartbeat | nothing | PlayerbotAI.cpp:2648 | movement not interrupted by this path | noop |
| 555 | `ClearStaleTransportStateCompat` | clears the transport flag | proper / nothing (no flag on F) | PlayerbotAI.cpp:2665 | same | same |
| 563 | `StopMovingCompat` | stops the bot | proper | PlayerbotAI.cpp:5935 | same | same |
| 575 | `SendAddonMessageCompat` | addon / guild / channel / chat packets | not sent | PlayerbotAI.cpp:3020, PlayerbotAI.cpp:3046 | the bot says less | noop |
| 577 | `SendAddonChatCompat` | addon / guild / channel / chat packets | not sent | PlayerbotAI.cpp:3025, PlayerbotAI.cpp:3031 | the bot says less | noop |
| 580 | `PartyPortalSpellOfObjectCompat` | use / interact / portal spell of an object | false / nothing | PlayerbotAI.cpp:3609 | "could not use / cannot interact": the bot touches no object (less) | less |
| 582 | `GameObjectNoInteractCompat` | the NO_INTERACT flag | false -> TRUE (R9) | PlayerbotAI.cpp:3615, PlayerbotAI.cpp:3681 | was: an unknown object read as "may interact" (the original answers true to keep the bot off); now blocked. Both callers are behind PartyPortalSpellOfObjectCompat (false), so unreachable today | fixed |
| 583 | `GameObjectCanInteractCompat` | use / interact / portal spell of an object | false / nothing | PlayerbotAI.cpp:3716 | "could not use / cannot interact": the bot touches no object (less) | less |
| 584 | `SendGameObjectUseCompat` | use / interact / portal spell of an object | false / nothing | PlayerbotAI.cpp:3721 | "could not use / cannot interact": the bot touches no object (less) | less |
| 587 | `TaxiSourceDestinationCompat` | taxi path data | false | PlayerbotAI.cpp:3804 | no flight handling (the bot never takes a flight) | same |
| 588 | `TaxiNodePositionCompat` | taxi path data | false | PlayerbotAI.cpp:3866 | no flight handling (the bot never takes a flight) | same |
| 590 | `FlightMasterIfCanInteractCompat` | the NPC if interactable | proper (flag word 2 added) | PlayerbotAI.cpp:3875, PlayerbotAI.cpp:3881 | same | same |
| 594 | `PlayTextEmoteCompat` | sends a text emote | proper (typed packet, sound -1) | PlayerbotAI.cpp:4200 | same visible emote | same |
| 605 | `UnitInMapCompat` | map lookups by guid | proper | none in the slice | same | same |
| 613 | `AnyTypeCreatureInMapCompat` | map lookups by guid | proper | PlayerbotAI.cpp:4471 | same | same |
| 614 | `WorldObjectInMapCompat` | map lookups by guid | proper | PlayerbotAI.cpp:4516 | same | same |
| 630 | `GuildRanksOfBotCompat` | rank data | false | PlayerbotAI.cpp:4686 | "no rank data": unscaled size | same |
| 634 | `PlayerViewObjectCompat` | camera body | GetViewpoint() (proper) | PlayerbotAI.cpp:4743 | same | same |
| 646 | `GameObjectDataPairOfCounterCompat` | the spawn record behind a guid counter | nullptr | WorldPosition.cpp:106 | WorldPosition(guid) answers the empty position | same |
| 647 | `CreatureDataPairOfCounterCompat` | the spawn record behind a guid counter | nullptr | WorldPosition.cpp:128 | WorldPosition(guid) answers the empty position | same |
| 650 | `CreatureMaxLevelCompat` | level_max | 0 | WorldPosition.cpp:675 | read only by the point-danger index, built by TravelMgr (not in the slice): unreachable | same |
| 655 | `ExcludeSteepSlopesCompat` | PathFinder::ExcludeSteepSlopes() | nothing (F has no exclude call; F's filter penalises slope) | WorldPosition.cpp:1362 | PERMITS steeper paths than T. Not fixable from the module (the core's filter is private); NavExcludeFlagsCompat still excludes steep ground for the corrected-point query. Open item for the next slice | less |
| 659 | `PathTypeBitsMatchCoreCompat` | compile-time pins | true | WorldPosition.cpp:28 | pin only | noop |
| 660 | `NavFlagsMatchCoreCompat` | compile-time pins | true | WorldPosition.cpp:39 | pin only | noop |
| 663 | `NavExcludeFlagsCompat` | NAV_MAGMA|SLIME|WATER|STEEP | F bits (proper) | WorldPosition.cpp:1657 | same | same |
| 667 | `PoolOfSpawnCompat` | pool / event of a spawn | 0 | GuidPosition.cpp:163, GuidPosition.cpp:165 | "not in a pool / no event": read only by travel value code (IsEventUnspawned has no caller in the slice) | same |
| 668 | `GameEventOfSpawnCompat` | pool / event of a spawn | 0 | GuidPosition.cpp:172 | "not in a pool / no event": read only by travel value code (IsEventUnspawned has no caller in the slice) | same |
| 670 | `GameEventActiveCompat` | event running | proper | GuidPosition.cpp:178 | same | same |
| 677 | `GuildSayCompat` | addon / guild / channel / chat packets | not sent | PlayerbotAIChat.cpp:153 | the bot says less | noop |
| 680 | `SayToChannelCompat` | addon / guild / channel / chat packets | not sent | PlayerbotAIChat.cpp:163 | the bot says less | noop |
| 683 | `SendChatPacketCompat` | addon / guild / channel / chat packets | not sent | PlayerbotAIChat.cpp:393 | the bot says less | noop |
| 691 | `ReleaseForeignSummonObjectsCompat` | hands objects back | nothing | PlayerbotAISpells.cpp:1356 | neutral | noop |
| 693 | `SpellIconIdIsCompat` | SpellIconID == icon | false | PlayerbotAISpells.cpp:2057, PlayerbotAISpells.cpp:2058 | IsHealSpell loses the two paladin icon ids; the heal-effect loop below still classifies the spell | same |
| 695 | `ItemSpellChargesCompat` | Item::GetSpellCharges | 0 | PlayerbotAISpells.cpp:2177 | reagent counting treats the cast item as used up (counts it, never more reagents than there are) | same |

Counts: 109 rows: same 57, less 18, fixed 6, noop 28.

## 2. The other items

| Item | What changed | Where |
|---|---|---|
| 2 R10 texts | `GetBotText(name)` answers "" (the real answer with no text table: ForeverCore's world DB has no `ai_playerbot_texts` table, so there is nothing to load); the placeholder form falls back to the name with the placeholders replaced, as the real one does; `GetLocalePriority` -1. Every caller that sends it skips an empty text: `PlayerbotAI.cpp:469, :2737, :3105, :3116, :3511` (the `BOT_TEXT("logout_cancel" / "logout_start" / "hello")` whispers) and `FollowActions.cpp:604, :610, :615` (`wait_travel_*`). Turtle: identical for a non-empty text; an unloaded table no longer whispers an empty line | `SliceLinkStubs.cpp:385-403` |
| 2 R10 filter | base `ChatFilter::Filter` is the real body (strips the `@...` prefix); the filter chain is not ported, so `CompositeChatFilter::Filter` refuses a line that opens with `@` (empty = not for this bot) and passes any other line | `SliceLinkStubs.cpp:287, :298` |
| 2 R10 talents | `PremadeSpecsPortedCompat()` (T: true; F: one line "premade talent specs are not ported (no spec is read, no talent is chosen)") skips the spec loop and the "No premade specs found" error: 1 line at boot, no error per configured spec (`Error with premade spec` count in `world.out`: 0) | `PlayerbotAIConfig.cpp:2125-2130, :2187`, `TbCompat_Unported.h:253` |
| 3 R11 | `WorldPosition::ClosestCorrectPoint` returns false when the query is null or has no attached mesh. On T today (and on F before) the `MANGOS_ASSERT` aborted the server in that case; T with the query present behaves as before | `WorldPosition.cpp:1643` |
| 4 R1' logout | `BotMgr::OnPlayerLogout` takes down the brain, clears the host master and requests the logout of every bot whose master is the player logging out; `Think` drops the brain when the master is not in the world; an engine is not ticked in the update in which its logout was requested or its master was lost | `TbBotMgr.cpp:266-290, :488-492, :464` |
| 5 R3 config | `EnsureConfig` caches only a success (a failure is retried at the next brain, `.tb engine on`, and `OnConfigLoad(reload)`); the config is loaded at `OnWorldInitialized` when the engine is built in and `TortoiseBots.Enable = 1`, with a "loaded in N ms" line | `TbEngineBrain.cpp:77-112`, `TbScripts.cpp:66, :88, :111` |
| 6 R4 | `RootedCompat` (T `IsInRoots()`, F `HasRootAura()`: Unit.h:1108, also ROOT_2 and the gravity-less root) replaces the copied body in `ServerFacade::IsInRoots`. DIFFERENT ANSWER on F: the two extra root kinds read as rooted | `ServerFacade.h:99`, `cmangos-compat-r3.h:215`, `TbCompat_Unit.h:570` |
| 7 R5 | the two `static_assert`s (price table sizes equal `MAX_ITEM_CLASS`, `MAX_ITEM_QUALITY`) are back, in the Turtle compat header | `cmangos-compat-r4c.h:103-104` |
| 8 R6 | the verify script also pins `TargetedMovementTargetCompat(target)` in `ServerFacade.cpp` (`verify_tortoise_surface.sh`: OK) | `tools/verify_tortoise_surface.sh:156-158` |
| 9 R12 | CMake leaves `TbCompat_C.cpp`, `TbCompat_Config.cpp`, `TbCompat_Unported.cpp` out of the option-OFF build; `TbScripts.cpp` names the unported counters only with `TORTOISEBOTS_SLICE1`; `.tb engine` and `.tb unported` answer "the engine is not built in" | `CMakeLists.txt:38-42`, `TbScripts.cpp:4-7, :100-116, :368-373`, `TbBotMgr.cpp:212` |

Shared-file edits besides the stand-ins (all through compat helpers, Turtle behaviour unchanged except where said): `PositionAction.cpp` joined the slice
(`forever/SLICE1.txt:45`) with two compat calls (`ZoneIdCompat`, `IsWithinLOSCompat`, and `Map2ZoneCoordinatesCompat` for the position text: F answers the UI map position, display only).

## 3. Live evidence (build VM `~/forever-run`, three boots; the server is stopped after each; `forever-test1` untouched)

Characters as in the R8 notes: Tbbeta (priest, master), Tbalpha and Tbgamma (warriors, engine on). Lines are from `~/forever-run/logs/world.out`.

**Boot line (item 2 and 5).** `TortoiseBots: premade talent specs are not ported (no spec is read, no talent is chosen)` (one line; `Error with premade spec` count: 0) and
`TortoiseBots: engine configuration loaded in 957 ms` (at world init; 996, 1003, 1220 ms in the other boots).

**First engine start (item 5, `.tb perf` window reset right before it).**
Before (R8, config loaded lazily on the first brain): worst module tick 1,091 ms. Now:
`tb perf` -> `perf: 5469 ticks; world update time avg 1.12 ms, worst 7 ms; module (sessions + engines) avg 8 us, worst 1249 us; 3 bot session(s).` after
`TortoiseBots engine: brain of Tbalpha built in 1 ms (PlayerbotAI constructor 0, strategies 1, movement 0)` (the stall moved to boot: 957 ms once, before the world is open;
the 700 ms worst of the window before it is the three character loads, not the engine).

**Follow** (`tb go Tbbeta 15 0`, `tb where`): `Tbalpha ... at -8676.3 -66.4`, `Tbgamma ... at -8672.2 -66.5`, master `-8671.0 -66.5`: 5.3 yd and 1.2 yd,
`master: Tbbeta (no network session)`, `follow target: Tbbeta ... motion type: 14` for both (an earlier boot of the same code: 3.6 and 3.2 yd).

**Assist**: `tb attack Tbbeta` -> `TortoiseBots: Tbbeta: attacking Kobold Worker.`; 3 s later `Tbalpha ... in combat, victim Kobold Worker (health 57)`,
`state: Combat`, `target reach: distance 0.491524 yd, melee range 5.000000 yd, in melee range: yes`; the same for Tbgamma (`distance 0.490770 yd`).

**After combat**: 12 s later `state: Non Combat`, `follow target: Tbbeta, safe: yes, distance to master target: 0.600000, motion type: 14` (Tbalpha) and
`distance to master target: 0.000000, motion type: 14` (Tbgamma).

**Logout 1, master logs out while two engine bots fight** (`tb logout Tbbeta`; Tbalpha `in combat, victim Kobold Worker (health 31)`, Tbgamma in combat):
```
BotSession: logout asked for Player-70-00000002 (command)
bot Tbbeta left the world
BotSession: logout asked for Player-70-00000003 (the master left the game)
BotSession: logout asked for Player-70-00000001 (the master left the game)
BotMgr: session of Player-70-00000002 ended
bot Tbgamma left the world
bot Tbalpha left the world
BotMgr: session of Player-70-00000003 ended / BotMgr: session of Player-70-00000001 ended
tb status: enabled, 0 bot session(s).      (no assert, no crash)
```
**Logout 2, `kick Tbbeta` on the console**: `Player Tbbeta kicked.` and nothing else happened: a bot session has no socket, so the core's kick does not reach it (`tb status`: still 3
bot sessions, both engine bots fighting). The master here is a bot session, so a kick cannot exercise the hook; the path a kick takes in the core is the same
`WorldSession::LogoutPlayer` -> `PlayerScript::OnLogout` -> `BotMgr::OnPlayerLogout` that Logout 1 drives. A real client kick was not possible (no network client on the VM).
**Logout 3, server shutdown with engine bots online** (`world.sh stop`, Tbalpha and Tbgamma on the engine, a fight started):
```
BotSession: logout asked for Player-70-00000003 / -00000001 / -00000002 (server shutdown)
bot Tbgamma left the world, bot Tbalpha left the world, bot Tbbeta left the world
BotMgr: session of Player-70-00000003 / -00000001 / -00000002 ended
Halting process...
```
`docker ps` shows no `forever-world` afterwards; `ASSERT|Segmentation|Crash` count in `world.out`: 0. (The first version of the hook asked the two bots a second time with
"the master left the game" while they were already leaving; fixed with a `LogoutRequested()` check, the second shutdown above has no such line.)

**Item 10, boss-tactics counter live, `tb unported` 5 min 28 s after the engines started** (two engine warriors, three fights): `slice: 70 features not ported (calls since start)`, then
`boss and raid tactics: 156` (R8, before the once-per-second counter: 1,271,584 in 22 minutes), `chat link parsing: 50`, `talent spec tables: 16`, `travel and exploration: 15`,
`navmesh and path internals: 9`, `transports: 7`, `other legacy services: 6`, `pool and hire services: 6`, `loot object stack: 4`, `random item tables: 4`, `bot texts and help: 2`,
`trace and observability services: 1`, `incoming:` rows 2 to 22 (handler registrations), every other row 0. So the counter reads "seconds in which a check reached the feature" now: 156 seconds of 328.

## 4. The option-OFF build (item 9)

`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=OFF ~/forever-build/build.sh 12 tortoisebots` -> `CMAKE_RC=0 MAKE_RC=0` (a `worldserver` link of the same configuration was green too). Object list of the module
library, from `build.ninja`:

| | before the slice (commit before `6fcf7ee57`) | before R9 (`c88084785`) | now |
|---|---|---|---|
| `TbBotMgr`, `TbBotSession`, `TbScripts` | yes | yes | yes |
| `compat/TbItemPrototype`, `compat/TbSpellEntry` | yes | yes | yes |
| `host/TbEngineBrain` (the "not built in" stubs `.tb engine` needs) | - | yes | yes |
| `compat/TbCompat_C`, `compat/TbCompat_Config`, `compat/TbCompat_Unported` | - | yes | **no** |
| objects | 5 | 9 | 6 |

So the option-off build is the spike plus the two mirrors plus one stub object. What is still there on purpose (lead decision): `.tb engine` and `.tb unported` (they answer "the engine is not
built in"), the read of `TortoiseBots.Slice.EngineByDefault` (a warning when it is 1), and the `tortoisebots` logger line. `build.ninja` has 0 entries for `forever/src/slice`, `host/TbHost` and `ai/playerbot`.

## 5. Gates (final commit)

| Gate | Result |
|---|---|
| Option ON `worldserver` build (`-DTORTOISEBOTS_SLICE1=ON`) | `CMAKE_RC=0 MAKE_RC=0` |
| Option OFF module build | `CMAKE_RC=0 MAKE_RC=0`, 6 objects (section 4) |
| Slice census (`FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 14`) | `files 133  compile clean 133  with errors 0  error places 0` (133 = 132 + `PositionAction.cpp`) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | file current; `(a)=0  (b)=164` (unchanged) |
| `tools/verify_tortoise_surface.sh` | `Tortoise 1.18.1 Core module surface: OK` |
| `forever-build/policy-tests.sh` (tree synced to `forever-build/tb`) | 472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL: the baseline (SHIM-R3 notes G4: `test_combat_monitor_service`, `_race`, `test_decision_capture_files`, `test_run_trace_writer`; RUN_FAIL `test_log_file_rotation`, `test_decision_capture_codec`) |
| Turtle `vm-build ~/wt-slice-integ --wait` | `COMPILE_SECONDS=2267 CMAKE_RC=0 MAKE_RC=0` (sha 84ca1ef89: the commit with all shared-file changes; the later commits touch only `forever/`) |
| Turtle `vm-test tb ~/wt-slice-integ` | `RESULT: PASS  kind=tb wt=wt-slice-integ sha=7d775e61c dirty=0 files=3660`, `rc=0 vm_duration=867s` (once, at the final code) |
| The server on the build VM | stopped after every boot; `docker ps` shows `forever-bnet`, `forever-db` and no `forever-world` |

## 6. What I am unsure about

* **The audit judges polarity from the callers' code, not from a Turtle run**: the "same" rows say the stand-in equals what the real function answers for an empty table or an absent feature. For
  the 40-odd boss rows that rests on every reader being row- or map-gated (checked for the ones named in the table's callers column, not by running the raid rules).
* `AoeTauntStillNeeded` stays false: the real function counts the group's loose adds without any boss row, so it can say true where the stand-in blocks the AoE taunt (less, not the real answer).
* `IsAllowedInstanceTarget` in a grouped dungeon with a real master answers only "the explicit attack target": the real rule also lets marked and engaged targets through (not ported, less).
* `EnemyPlayerValue::GetMaxAttackDistance` inside a battleground answers 10 yd (the shortest real value); no reader exists in the slice.
* `ExcludeSteepSlopesCompat` stays a permit (section 1); `RootedCompat` on F also counts ROOT_2 and the gravity-less root (the core's own `HasRootAura`).
* The kick test cannot be made with a bot master (Logout 2): the same hook ran in Logout 1 and 3, but a real client kick was not exercised.
* `PositionAction.cpp` is now compiled into the slice and registers nothing new (its actions are not in the generated context); `.tb where` / `ReturnToStayPosition` were not driven live (no stay position is set in the tests).
