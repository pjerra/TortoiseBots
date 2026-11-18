# ForeverCore port: shim round 3 (spell data)

The bot brain reads spell data as public fields of cmangos's `SpellEntry` (`spellInfo->Effect[i]`,
`spellInfo->EffectBasePoints[i]`, `pSpellInfo->Reagent[k]`, ...). ForeverCore's `SpellInfo` is a different object: the
effects are `SpellEffectInfo` rows in a vector, the base points are floats that hold the vanilla *average* (the die sides
live in the `classic_spell_value` table), and there is no `Effect[]`, `Category` or `manaCost` field at all. A macro cannot
turn a field into a call, so on ForeverCore `SpellEntry` is our own struct with the old field names, filled from the core's
`SpellInfo` on first use (`forever/src/compat/TbSpellEntry.h` / `.cpp`). The field reads compile unchanged; only the places
that FETCH the pointer change, to helpers that exist on both cores.

## Numbers

| Run | What | Error places |
|---|---|---|
| BEFORE | full census, this branch at bd0a076ae (round 2 fix 2) | 11,938 in 531 files, 11 clean files |
| AFTER | full census, this branch at the round-3 commit (6cbc923cd) | 11,065 in 520 files, 11 clean files |

The 873 places that went away are the spell-data families: `Effect` 173 -> 0, `EffectApplyAuraName` 102 -> 0,
`MAX_EFFECT_INDEX` 64 -> 0, `EffectTriggerSpell` 58 -> 0, `SpellEffectIndex` 28 -> 0, `EffectMiscValue` 42 -> 0,
`EffectItemType` 37 -> 0, `EffectBasePoints` 35 -> 0, `EFFECT_INDEX_0/1/2` 21/12/15 -> 0, `IsPassiveSpell` 21 -> 0,
`GetSpellCastTime` 18 -> 0, `EffectImplicitTargetA` 18 -> 0, `manaCost` 11 -> 0, `IsPositiveSpell` 10 -> 0,
`rangeIndex` 9 -> 0, `EffectRadiusIndex` 9 -> 0, `spellLevel` 12 -> 0, `GetSpellSchoolMask` 2 -> 0, `School` 5 -> 0,
`procCharges`/`procChance` 2/1 -> 0, `SpellFamilyName` 1 -> 0, `SPELL_RANGE_IDX_*` 11 -> 0,
`SPELL_INTERRUPT_FLAG_INTERRUPT` 1 -> 0, `AURA_INTERRUPT_FLAG_NOT_SEATED` 3 -> 0, `SPELL_ATTR_*` 16 -> 0,
`SPELL_CUSTOM_MOUNT_*` 2 -> 0 (the `Custom` field behind them stays, see the left-out list).
Two names remain on purpose: `SpellIconID` 3 and `Custom` 2. Four places name `SpellRangeMax`/`SpellRangeMin`; every
file that calls them compiles cleanly in a direct compile, so those four come from a census stub round (a file that needs
a stand-in for a Turtle-only header is compiled once against it), not from the code.

## Storage: lazy, lock-free read path

The spell store holds 35,889 spells (this server, ids up to 9,970,801), so nothing is filled eagerly (D2). `tbcompat::InitSpellTable()` runs once from the
module's `WorldScript::OnWorldInitialized`, after `World::SetInitialWorldSettings` has loaded the spell store
(`World.cpp:1471-1491`), and sizes a `std::atomic<SpellEntry*>` array from the highest id `sSpellMgr->ForEachSpellInfo`
reports. `tbcompat::SpellProto(id)` is one acquire load in that array; the first reader of an id builds its mirror and
publishes it with a compare-exchange, and a racing builder deletes its own copy (both copies come from the same immutable
`SpellInfo`, so either is correct). A lookup before the table exists, or for an id above it, takes a mutex-guarded
fallback map, which the same fill uses, so there is one fill function and one set of pre-checks.

**No spell reload at the pin.** `LoadSpellInfoStore` and every pass that edits the `SpellInfo` objects
(`LoadSpellInfoCorrections`, `LoadClassicSpellInfo`, `LoadSpellInfoCustomAttributes`) are called once from
`World::SetInitialWorldSettings` (`World.cpp:1471`, `:1477`, `:1480`, `:1491`). The `.reload` handlers
(`scripts/Commands/cs_reload.cpp:301-317`) reload `spell_required`, `spell_area`, `spell_group`, `spell_learn_spell`,
`spell_linked_spell`, `spell_proc`, `spell_target_position`, `spell_threats`, `spell_group_stack_rules`,
`spell_pet_auras` and `spell_script_names` — side tables, never the store and never a `SpellInfo`. So a mirror is only as
fresh as the core's object, which does not change after startup. Recorded in `PORT-PLAN.md` rule 3 next to the item note.

## What the shim does now

- No `#define SpellEntry SpellInfo`: `SpellEntry` is the mirror (`TbSpellEntry.h`).
- No `#define GetSpellEntry(...) GetSpellInfo(...)`. Every fetch is `GetSpellEntryCompat(...)` on both cores:
  - `sSpellMgr.GetSpellEntry(id)` (32 sites),
  - `sSpellTemplate.LookupEntry<SpellEntry>(id)` (54 sites; the proxy returns the mirror and its `GetMaxEntry()` is the
    highest id the core has, which the table computes at startup),
  - `X->m_spellInfo` (125 sites) and `X->GetSpellProto()` (32 sites): the core stores the `SpellInfo` there, the mirror is
    `GetSpellEntryCompat(...)` of it. On the Turtle core both helpers are identity / the old call.
- `GetSpellInfoCompat(entry)` is the way back for a core function that wants the core's record. No bot call site needs it
  this round (the core calls the bot code makes with a spell take an id, a `SpellCastResult` or a mirror-sized struct);
  it exists because the field mirror may not be handed to the core as if it were a `SpellInfo`.
- The old free functions over a spell are all over the mirror and each asks the core through `Core` (never re-derived from
  mirrored fields): `GetSpellCastTime` (2 and 3 argument forms), `GetSpellDuration`, `GetSpellRecoveryTime`,
  `GetSpellSchoolMask`, `IsChanneledSpell`, `IsPassiveSpell`, `IsPositiveSpell` (pointer and id), `IsPositiveEffect`,
  `IsNonCombatSpell`, `IsSpellHaveEffect`, `IsSpellAppliesAura`, `IsAreaAuraEffect`, `IsNextMeleeSwingSpell`,
  `IsAutoRepeatRangedSpell`, `IsAutocastable` (pointer and id).
- The old struct's own helpers, on the mirror: `CalculateSimpleValue` (the old formula over the mirrored dice),
  `IsPositiveSpell`, `IsPositiveEffect`, `IsPassiveSpell`, `IsChanneledSpell`, `IsNonCombatSpell`,
  `IsAreaOfEffectSpell`, `IsNextMeleeSwingSpell`, `IsFromBehindOnlySpell`, `HasEffect`, `IsSpellAppliesAura` (both
  forms), `GetAllSpellMechanicMask`, `GetDuration`, `CalculateDuration`, `GetCastTime`, `GetRecoveryTime`,
  `GetSpellSchoolMask`, `GetErrorAtShapeshiftedCast`.
- Constants the bot compares against, added by name (`forever/src/compat/forever-compat-shim.h`,
  `TbSpellEntry.h`): `EFFECT_INDEX_0..2`, `MAX_EFFECT_INDEX`, `SpellEffectIndex`, the twelve `SPELL_ATTR_*` names the bot
  tests, the five `SPELL_INTERRUPT_FLAG_*` bits, `AURA_INTERRUPT_FLAG_NOT_SEATED`,
  `SPELL_RANGE_IDX_SELF_ONLY/COMBAT/ANYWHERE`, `SPELLCATEGORY_GLOBAL`, `TARGET_FLAG_SELF`.
- Two more both-core helpers for the same family: `SpellRangeMin`/`SpellRangeMax` (the `SpellRangeEntry` row's two numbers:
  `minRange`/`maxRange` here, `RangeMin[0]`/`RangeMax[0]` there) and `GetMaxSpellIdCompat()` (the highest spell id, the
  Turtle core's `sSpellMgr.GetMaxSpellId()`).
- `GetSpellPowerCostCompat(caster, entry)` is **not** added: no bot site reads a power cost for a caster. The three sites
  that price a cast do it from the mirrored flat cost and percent of the caster's base mana themselves
  (`GenericSpellActions.cpp:719-721`, `Commander.cpp:10539-10540`, `BossTacticsActions.cpp:13380-13384`), and for mana
  that is caster-independent; the flat `manaCost` field is the same number the core charges for those spells (see the
  mana row below).

## Field table

T = `~/playerbots-references/penqle-tortoise-wow-modsys/src/game/Spells/SpellEntry.h` (Tortoise core);
F = `~/ForeverCore/src/server/game/Spells/SpellInfo.h` / `.cpp` at the pinned core.

| old field (T line) | ForeverCore source | same meaning because |
|---|---|---|
| `Id` (:529) | `SpellInfo::Id` (F.h:335) | the spell id |
| `School` (:530) | lowest set bit of `SchoolMask` (F.h:427) | old `School` is one `SpellSchools` id (0 normal .. 6 arcane); the core keeps the bit mask. `.tb spellscan` counts the spells with more than one bit, which the old field can only show as the first school |
| `Category` (:531) | `GetCategory()` (F.cpp:2136), `CategoryId` | the same SpellCategories row column the old `Category` read |
| `Dispel` (:533) | `Dispel` (F.h:336) | `DispelType` 0..10 in both, from the same column |
| `Mechanic` (:534) | `Mechanic` (F.h:337) | `Mechanics` ids in both |
| `Attributes`..`AttributesEx4` (:535-539) | `Attributes`..`AttributesEx4` (F.h:338-342) | both cores number the Attr0..Attr4 words by the client's bits; `LoadSpellInfoStore` copies them straight from `SpellMisc` |
| `Targets` (:542) | `Targets` (F.h:436), `SpellTargetRestrictions::Targets` | same `TARGET_FLAG_*` values (ITEM 0x10, SOURCE/DEST_LOCATION 0x20/0x40 in both) |
| `TargetCreatureType` (:543) | `TargetCreatureType` (F.h:437) | creature type mask |
| `RequiresSpellFocus` (:544) | `RequiresSpellFocus` (F.h:438) | game object id |
| `RecoveryTime`, `CategoryRecoveryTime` (:548-549) | `RecoveryTime`, `CategoryRecoveryTime` (F.h:414-415) | milliseconds, same columns |
| `InterruptFlags`, `AuraInterruptFlags`, `ChannelInterruptFlags` (:550-552) | the `EnumFlag<>` fields, `AsUnderlyingType()` (F.h:422-425) | the raw client bits. The core labels some of the same bits differently (0x4 is `Stun` there, 0x8 `Combat`), so the mirror carries the number and the shim's old names are those numbers (see the constants section) |
| `procChance`, `procCharges` (:554-555) | `ProcChance`, `ProcCharges` (F.h:430-431) | percent / count |
| `maxLevel`, `baseLevel`, `spellLevel` (:556-558) | `MaxLevel`, `BaseLevel`, `SpellLevel` (F.h:435-437) | the SpellLevels row |
| `powerType` (:560) | `PowerCosts[0]->PowerType` (F.h:401) | the first SpellPower row's power type, which is what the old single `powerType` column held |
| `manaCost` (:561) | `GetClassicSpellCost(Id)->ManaCost` for a mana spell the core treats as legacy (`SpellInfo.cpp:4386-4395`); else `PowerCosts[0]->ManaCost` | for a legacy mana spell the core charges the vanilla table's flat number (plus its percent), which is exactly the old `manaCost` column of the same 1.12 spell_template; for anything else the DB2 row's flat cost. The core's `CalcPowerCost` adds percent and caster modifiers on top — the old field never carried those either |
| `ManaCostPercentage` (:666) | the same classic cost row's `ManaCostPct`; else `round(PowerCosts[0]->PowerCostPct)` | percent of the caster's base mana, the quantity the old field held |
| `rangeIndex` (:565) | `RangeEntry->ID` (F.h:402) | the client's SpellRange id: the core keeps the row, and the row's ID is the index the old column held and the bot looks up in `sSpellRangeStore`. The old struct's default (1) stays when the core has no row |
| `speed` (:566) | `Speed` (F.h:403) | missile speed |
| `StackAmount` (:568) | `StackAmount` (F.h:406), `SpellAuraOptions::CumulativeAura` | max stacks |
| `Totem[2]` (:569) | `Totem` (F.h:407) | the two totem item ids |
| `Reagent[8]`, `ReagentCount[8]` (:570-571) | `Reagent`, `ReagentCount` (F.h:409-410) | the SpellReagents row |
| `EquippedItemClass`, `EquippedItemSubClassMask`, `EquippedItemInventoryTypeMask` (:572-574) | the three fields (F.h:413-415) | SpellEquippedItems row |
| `Effect[i]` (:575) | `GetEffects()[i].Effect` (F.h:236) | `SpellEffects` ids; filled for the first three slots by EffectIndex, the rest counted in `MoreEffects` |
| `EffectDieSides[i]` (:576) | `classic_spell_value.die_sides` (F `ClassicSpellValue.h:38-46`, `SpellMgr.cpp:3678-3720`) | the vanilla dice of the same effect, matched by the core's own effect/aura/misc check; without a row a DB2 effect is a single result (die sides 1, below) |
| `EffectBaseDice[i]` (:577) | 1 | the vanilla column's value for a fixed or single-die effect (the number of dice); the DB2 has no dice at all |
| `EffectBasePoints[i]` (:579) | `classic_spell_value.base_points`; else `round(SpellEffectInfo::BasePoints) - 1` (F.h:241) | the old field is the client's base points, one below the smallest roll (the bot code adds one everywhere). For every legacy effect the core has a vanilla row, that row holds the same base-1 number. Without a row the core's `BasePoints` is the value the spell uses (the mean of the vanilla roll where there is one); minus one is the same shape |
| `EffectRealPointsPerLevel[i]` (:578) | classic row `points_per_level`; else `RealPointsPerLevel` (F.h:239) | value added per level above the spell level |
| `EffectPointsPerComboPoint[i]` (:614) | classic row `points_per_combo`; else `PointsPerResource` (F.h:240) | value per combo point |
| `EffectBonusCoefficient[i]` (:580) | classic row `bonus_coeff` when >= 0; else `BonusCoefficient` (F.h:244) | spell power factor |
| `EffectImplicitTargetA/B[i]` (:586-587) | `TargetA/TargetB.GetTarget()` (F.h:252-253) | client `Targets` ids |
| `EffectRadiusIndex[i]` (:592) | `TargetARadiusEntry->ID` (F.h:254) | the client's SpellRadius id; the core keeps the row, whose ID is the index. Turtle/DB2 carry one radius index per effect; the core splits it into A and B, A is the old column |
| `EffectApplyAuraName[i]` (:595) | `ApplyAuraName` (F.h:238) - the core has already mapped the two Forever-only aura ids (673/674) to their retail auras (`SpellAuraDefines.h:759-766`) | the bot compares against the core's `AuraType`, so the core's own value is the consistent one on both cores |
| `EffectChainTarget[i]` (:604) | `ChainTargets` (F.h:257) | chain jumps |
| `EffectItemType[i]` (:607) | `ItemType` (F.h:258) | created item id |
| `EffectMiscValue[i]` (:610) | `MiscValue` (F.h:245) | the effect's misc value |
| `EffectTriggerSpell[i]` (:611) | `TriggerSpell` (F.h:260) | triggered spell id |
| `SpellName[locale]` (:630) | `SpellName->Str[locale]` (F.h:420), absent slot = empty | `LocalizedString` has the same nine slots in the same `LocaleConstant` order; the bot reads slot 0 (enUS) and `LOCALE_enUS` |
| `Rank[locale]` (:633) | derived: chain member -> `"Rank N"` from `ChainEntry->rank`; otherwise empty | 1.12 keeps the rank text in `spell_template`, which the core does not carry; the chain node's rank is the number that text holds and the client renders it as "Rank N". The two consumers (`SpellIdValue.cpp:138` picks the highest rank by the digits, `AutoLearnSpellAction.cpp:419` prints it) read exactly that |
| `StartRecoveryCategory`, `StartRecoveryTime` (:667-668) | the two fields (F.h:418-419) | the GCD category and its milliseconds |
| `MaxTargetLevel` (:670) | `MaxTargetLevel` (F.h:447) | level cap |
| `SpellFamilyName` (:671) | `SpellFamilyName` (F.h:449) | `SpellClassSet` |
| `SpellFamilyFlags` (:672) | low 64 bits of `SpellFamilyFlags` (`flag128`, F.h:450) | the old field is one 64-bit word; the core's is 128 bits, and every classic spell the bot tests fits in the first two words. The single reader (`PlayerbotAISpells.cpp:2091`) tests family flags of a 1.12 spell |
| `MaxAffectedTargets` (:673) | `MaxAffectedTargets` (F.h:448) | target cap |
| `DmgClass`, `PreventionType` (:674-675) | the two fields (F.h:451-452) | SpellCategories row |

`CalculateSimpleValue(eff)` keeps the old formula `EffectBasePoints[eff] + EffectBaseDice[eff]`, i.e. the smallest roll,
from the mirrored fields; the two readers (`PlayerbotAI.cpp:5905`, `DebugSpellCommands.cpp:159`) apply aura points with it.

### Traps that were checked, not assumed

- **Base points and dice.** The core's `BasePoints` is the *mean* of the vanilla roll (Fireball 133: the comment in
  `SpellInfo.cpp:783-786` says 18 = mean of 14..22), while the old column is 13 with 9 die sides. Deriving the old pair
  from the mean is lossy, which is why the fill uses the core's `classic_spell_value` row for the effect: it holds the
  vanilla base points, die sides, points per level, points per combo and bonus coefficient — the same columns the Turtle
  core's `spell_template` has. The row is only used when the core itself matched it to the effect
  (`SpellMgr.cpp:3707-3711`: same effect, same aura, and same misc value for the auras whose value lives in it), so an
  effect the client reworked is never given the old dice. `.tb spell 133` shows the result.
- **More than three effects.** `MAX_EFFECT_INDEX` is 3 on Turtle; ForeverCore spells can carry more. The mirror holds the
  first three by `EffectIndex` and counts the rest (`MoreEffects`); `.tb spellscan` reports how many spells have any.
- **Range, duration, cast time.** Only `rangeIndex` is read by the bot (and then looked up in `sSpellRangeStore`), so only
  it is mirrored, from the row's ID. Duration and cast time are the core's answers (`GetDuration`, `CalcCastTime`) and are
  not mirrored as indices; nothing reads `DurationIndex`/`CastingTimeIndex`.
- **Mana cost.** The old flat/percent pair is filled from the same source the core charges from (see the table), not from a
  guess; the caster-dependent part (talents, auras, `PowerCostPct` of base mana) was never in those two columns.
- **Interrupt flags.** The bits are the client's, but the core's *names* for them differ (`0x4` is `Stun` there, `0x8`
  `Combat`); the mirror carries the number and the shim restores the old names with the same numbers, so the bot's tests
  keep answering the same question.
- **Attributes.** Mapped by the bit the bot has always tested. Two names are misnomers on both cores and stay the same
  test: `SPELL_ATTR_USES_RANGED_SLOT` has been bit `0x10` (Is Ability) on the Turtle build since the round-1 shim
  (`ai/cmangos-compat-shim.h:479-482`), and `SPELL_ATTR_EX2_TAME_BEAST` is bit `0x10000`, which the core's retail-derived
  enum labels No Active Pets while the classic client data uses it for Tame Beast.

## Fields left out (D3: a used field with no true ForeverCore counterpart)

| old field | uses | why not filled |
|---|---|---|
| `SpellIconID` (:631) | `PlayerbotAISpells.cpp:2093-2094` (equals 70 or 242), `GenericSpellActions.cpp:962` (same icon as another spell) | the core has `IconFileDataId`, a FileDataID, not the SpellIcon.dbc index; filling one with the other would make the 70/242 comparisons silently never match |
| `Custom` (:699) | `MountValues.cpp:103`, `:105` (`SPELL_CUSTOM_MOUNT_SPEED_100`, `SPELL_CUSTOM_IGNORE_RIDING_SKILL_MOUNT_SPEED`) | Turtle's spell_template carries a custom column; the core has no counterpart and no such bits |
| `Internal` (:700) | none read directly (the old struct's own helpers used it; those now ask the core) | computed Turtle flags |
| `MinTargetLevel` (:696), `ScriptId`, `ParsedTooltip` (:702-703) | none | no core counterpart / unused by the bot |
| `Stances`, `StancesNot`, `Targets`' sibling `CasterAuraState`/`TargetAuraState`, `procFlags`, `manaCostPerlevel`, `manaPerSecond`, `manaPerSecondPerLevel`, `EffectAmplitude`, `EffectMultipleValue`, `DmgMultiplier`, `EffectMechanic` | none (the scan of every `X->Field` read found no site) | not read by the bot; kept out rather than filled and forgotten |

## Call sites

Script: `forever/tools/rewrite_spell_entry.py` (`--check` prints instead of writing), applied to `ai/`, `runtime/`,
`commands/`. The diff is text-preserving: every changed line differs only by the call spelling (228 lines changed in 64
files; the 65th changed file is the Turtle shim, which gains the helpers).

| category | sites | rewrite |
|---|---|---|
| `sSpellMgr.GetSpellEntry(id)` | 32 | `GetSpellEntryCompat(id)` |
| `sSpellTemplate.LookupEntry<SpellEntry>(id)` | 54 | `GetSpellEntryCompat(id)` |
| `X->m_spellInfo` (core `Spell`/`Unit::GetCurrentSpell` results) | 125 | `GetSpellEntryCompat(X->m_spellInfo)` |
| `X->GetSpellProto()` (core aura holders) | 32 | `GetSpellEntryCompat(X->GetSpellProto())` |
| inherited `m_spellInfo` inside `BotUseItemSpell` (a `Spell` subclass) | 4 | `GetSpellEntryCompat(m_spellInfo)`, by hand (no `->` for the script to anchor on) |
| `X->minRange` / `X->maxRange` on a `SpellRangeEntry` | 4 | `SpellRangeMin(X)` / `SpellRangeMax(X)` |
| `sSpellMgr.GetMaxSpellId()` (the id bound of two scans) | 2 | `GetMaxSpellIdCompat()` |

253 rewrites in all. `TARGET_FLAG_SELF` (0, and ForeverCore names 0 as `TARGET_FLAG_NONE`) is a constant, not a site.

Deliberately not rewritten: `m_triggeredByAuraSpell = triggeredByAura->GetSpellProto()` in `UseItemAction.cpp:60` — that
member is the core `Spell`'s own `SpellInfo const*` and the core reads it, so the core pointer stays.

## Live check (G5)

`.tb spellscan` (console) builds the mirror for every spell the core has and prints the counts and the time.
`.tb spell <id>` prints one spell's filled fields (cast time, range from `rangeIndex`, duration, levels, cooldowns, GCD,
mana, then every non-empty effect and a summary line). Both are read-only, `Console::Yes`.

```
TortoiseBots: spell mirrors: 35889 spells in 88 ms, highest id 9970801, 0 incomplete
TortoiseBots: effects: 45607 effect slots in all, 456 spells with more than 3 effects (first: 1223037 (5), 1226997 (1), 436097 (3), 1310285 (1), 1247430 (4)), 136 spells with more than one school.
```

No assertion: the scan runs every fill path once (all 35,889 spells) and the boot log has `ASSERTION FAILED` 0.

```
TC> tb spell 133
TortoiseBots: spell 133 "Fireball ": school 2 (mask 0x4), category 0, cast time 1500 ms, range 0.0-35.0 (idx 35), duration 4000 ms, levels 1/5/1 (base/max/spell), recovery 0/0 ms, GCD 1500 ms (category 133), mana 30 (0%), power 0.
TortoiseBots: effect 0: type 2, aura 0, base points 17 + 1, die sides 1, misc 0, target A/B 6/0, radius 0, trigger 0, item 0, chain 0, amp 0, points/level 0.20.
TortoiseBots: effect 1: type 6, aura 3, base points 0 + 1, die sides 1, misc 0, target A/B 6/0, radius 0, trigger 0, item 0, chain 0, amp 0, points/level 0.00.
TortoiseBots: targets 0x0, family 3 flags 0x40000001, attributes 0x10000/0x0/0x0, dispel 0, mechanic 0, stack 0, proc 101 x0, reagents 0/0 totem 0/0, max targets 0, more effects 0.

TC> tb spell 2050
TortoiseBots: spell 2050 "Lesser Heal ": school 1 (mask 0x2), category 0, cast time 1500 ms, range 0.0-40.0 (idx 5), duration 0 ms, levels 1/3/1 (base/max/spell), recovery 0/0 ms, GCD 1500 ms (category 133), mana 30 (0%), power 0.
TortoiseBots: effect 0: type 10, aura 0, base points 50 + 1, die sides 1, misc 0, target A/B 21/0, radius 0, trigger 0, item 0, chain 0, amp 1, points/level 0.30.
TortoiseBots: targets 0x0, family 6 flags 0x40000, attributes 0x10000/0x0/0x80000, dispel 0, mechanic 0, stack 0, proc 0 x0, reagents 0/0 totem 0/0, max targets 0, more effects 0.

TC> tb spell 20154
TortoiseBots: spell 20154 "Seal of Righteousness Rank 1": school 1 (mask 0x2), category 0, cast time 0 ms, range 0.0-0.0 (idx 1), duration 30000 ms, levels 1/7/1 (base/max/spell), recovery 0/0 ms, GCD 1500 ms (category 133), mana 20 (0%), power 0.
TortoiseBots: effect 0: type 6, aura 4, base points 107 + 1, die sides 1, misc 0, target A/B 1/0, radius 0, trigger 0, item 0, chain 0, amp 0, points/level 18.00.
TortoiseBots: targets 0x0, family 10 flags 0x8000000, attributes 0x50000/0x0/0x0, dispel 1, mechanic 0, stack 0, proc 100 x0, reagents 0/0 totem 0/0, max targets 0, more effects 0.

TC> tb spell 1223037
TortoiseBots: spell 1223037 "Concord of the Challenger ": school 0 (mask 0x1), category 0, cast time 0 ms, range 0.0-0.0 (idx 1), duration 8000 ms, levels 0/0/0 (base/max/spell), recovery 0/0 ms, GCD 0 ms (category 0), mana 0 (0%), power 0.
TortoiseBots: effect 0: type 6, aura 311, base points -1 + 1, die sides 1, misc 0, target A/B 1/0, radius 0, trigger 0, item 0, chain 0, amp 0, points/level 0.00.
TortoiseBots: effect 1: type 6, aura 244, base points -1 + 1, die sides 1, misc 0, target A/B 1/0, radius 0, trigger 0, item 0, chain 0, amp 0, points/level 0.00.
TortoiseBots: effect 2: type 6, aura 244, base points -1 + 1, die sides 1, misc 1, target A/B 1/0, radius 0, trigger 0, item 0, chain 0, amp 0, points/level 0.00.
TortoiseBots: targets 0x0, family 0 flags 0x0, attributes 0x89000100/0x20/0x84000, dispel 0, mechanic 0, stack 0, proc 0 x0, reagents 0/0 totem 0/0, max targets 0, more effects 5.
```

The last one is one of the 456 spells with more than three effects: three slots mirrored, `more effects 5` counted (a Forever spell,
so no classic row and the DB2 values are what the core uses).

Target flags spot-checked through the mirror, because the bot branches on them: enchant `7418` prints `targets 0x10`
(`TARGET_FLAG_ITEM`, the item in the trade window) and Blizzard `10` prints `targets 0x40` (`TARGET_FLAG_DEST_LOCATION`).

The other branch of the fill - a spell the core has a classic row for - is visible too:

```
TC> tb spell 49
TortoiseBots: spell 49 "Healing Touch (TEST) ": school 4 (mask 0x10), category 0, cast time 0 ms, range 0.0-0.0 (idx 1), duration 0 ms, levels 0/0/0 (base/max/spell), recovery 0/0 ms, GCD 0 ms (category 0), mana 10 (0%), power 0.
TortoiseBots: effect 0: type 10, aura 0, base points 9 + 1, die sides 11, misc 0, target A/B 0/0, radius 0, trigger 0, item 0, chain 0, amp 1, points/level 0.00.
TortoiseBots: targets 0x100, family 0 flags 0x0, attributes 0x0/0x0/0x0, dispel 0, mechanic 0, stack 0, proc 101 x0, reagents 0/0 totem 0/0, max targets 0, more effects 0.
```

`die sides 11` is the vanilla row (`classic_spell_value`: 9 + 1d11 = 10..20); the DB2 alone has no dice and would have
printed 1.

### D7: the numbers against vanilla rank 1

| spell | mirror | vanilla 1.12 rank 1 | difference |
|---|---|---|---|
| Fireball 133 | 1500 ms cast, 0-35 yd, 30 mana, effect 0 type 2 (school damage), base points 17 (+1 = 18), die sides 1, school 2 (fire), 4000 ms duration (the DoT) | 1.5 s, 35 yd, 30 mana, 13 + 1d9 = 14..22 fire plus a 4 s DoT | cast, range, mana, effect, school and duration are the vanilla ones. The base points are 18, the **mean** of the vanilla roll, and the die sides are 1, because this server's `classic_spell_value` has **no row for 133** (the table loaded 5,984 rows, 163 of which did not match the client spell). Without the row the mirror takes the core's `BasePoints`, which is what the core casts; the vanilla pair (13, 9) is not recoverable from it. No factor-of-two anywhere |
| Lesser Heal 2050 | 1500 ms, 0-40 yd, 30 mana, effect 0 type 10 (heal), base points 50 (+1 = 51), die sides 1, school 1 (holy) | 1.5 s, 40 yd, 30 mana, 46..56 | same: cast, range and mana match; 51 is exactly the mean of 46..56, and the die sides are 1 for the same missing-row reason |

So the mirror agrees with the core's own numbers on every field the bot reads; where it differs from the vanilla
*minimum* it is the vanilla *mean*, which is the number the core uses. The one thing that would make it exact is the
`classic_spell_value` row, which the core's data supplies for 5,984 spells on this server and not for these two.

Table memory: the mirror table is `(highest id + 1) x sizeof(std::atomic<SpellEntry*>)` = 9,970,802 x 8 =
**79,766,416 bytes (about 76 MiB)**, allocated once at startup; only the ~36k slots that hold a spell are ever written
(the rest stay zero). That is the one cost of the D2 shape on this id distribution (the ids reach 9.97M for 35,889
spells) and is written down for the lead: a two-level table or a lock-free hash would keep it under a megabyte if the
80 MB ever matter.

## Open points

- **The dice fallback is the mean, not the minimum.** For a spell the core has no `classic_spell_value` row for, the mirror
  carries `round(BasePoints) - 1` (the value the core uses) and die sides 1, where the old column would have the vanilla
  base-1 and its dice. On this server that is Fireball (18 instead of 14, die sides 1 instead of 9) and Lesser Heal (51
  instead of 46..56). The bot's single-value readers (RandomItemMgr stat weights, the craft yield) get the core's number,
  which is the one the spell actually uses; only a `classic_spell_value` row with the spell would make the pair exact.
- **`Rank[]` is derived.** The core has no rank text, so a chain member gets `"Rank N"` from `SpellChainNode::rank` and
  everything else empty. The digits are right (both consumers parse them); a rank-1 chain member prints "Rank 1" where
  vanilla may have had an empty string.
- **`School` is one bit.** A multi-school spell (136 of them here) shows the lowest bit; the old field could not hold
  more. Nothing in the bot code tests a school it needs the second bit for.
- **`EffectRadiusIndex` is the A entry** (the core splits the effect's radius into TargetA/TargetB; the old column is one).
- **`IsAreaOfEffectSpell`** asks the core's `IsAffectingArea()` (area targets plus area auras), a little wider than the
  old computed flag.
- **`GetAllSpellMechanicMask`** is the core's `GetAllEffectsMechanicMask() >> 1`, because the bot builds its own masks as
  `1 << (MECHANIC_X - 1)` (TargetValue.cpp:214-217) while the core sets `1 << Mechanic`.
- **Table memory.** 76 MiB, see the D7 section. A two-level or hashed table would cut it if the lead wants it.
- **Tests.** Eight wiring tests pin the exact expression at the call site they guard, and three Viscidus harnesses compile
  production method bodies against fake types; they were updated to the new spelling and to define the helper (the same
  treatment round 2's B1/B2 gave their pinned tests). No test was deleted or weakened in what it checks.

## Gates

All on the final tree; the only commit after them is this notes file.

| Gate | Command | Result |
|---|---|---|
| G1 module compiles on ForeverCore | `forever-build/build.sh 12 worldserver` | PASS: `CMAKE_RC=0`, `MAKE_RC=0`, 9 s on the final commit (24 s and 29 s on the earlier round-3 commits); the worldserver binary was written at 06:48 |
| G5 live boot | rsync core + module to `forever-build/src`, build, `forever-run/setup.sh`, `world.sh start`, poll `world.out` for `ready...` / `ASSERTION FAILED` / the mirrors line, `tb spellscan`, `tb spell 133/2050/20154/1223037`, `world.sh stop` | PASS: `ready...` 1, `ASSERTION FAILED` 0, item mirrors line 1; the spellscan and the four spell outputs above; server stopped, `forever-world` removed |
| G2 Turtle build | `vm-build ~/wt-forever-port --wait` | PASS: `COMPILE_SECONDS=2248 CMAKE_RC=0 MAKE_RC=0`, `QUEUE_RC=0`, mangosd `1bcf69c1...c095b8` and realmd written (a cold slot, hence 2248 s) |
| G3 Turtle unit tests | `vm-test tb ~/wt-forever-port` | PASS twice: `RESULT: PASS` rc=0, 890 s at decae2b34 and `RESULT: PASS` rc=0, 1137 s at 6cbc923cd (the whole `tools/verify_all.sh`: OKF, surface, wiring, 2,656 unit tests, decision trail) |
| G4 core-free tests on the ForeverCore toolchain | `policy-tests.sh` on the tree under test | 472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL in 120 s - the same set as the round-2 baseline: `test_combat_monitor_service`, `test_combat_monitor_service_race`, `test_decision_capture_files`, `test_run_trace_writer` (COMPILE_FAIL), `test_decision_capture_codec`, `test_log_file_rotation` (RUN_FAIL). Nothing got worse |
