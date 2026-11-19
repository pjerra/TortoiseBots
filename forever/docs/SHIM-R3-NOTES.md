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
| FIX1 | full census, this branch at the fix-round commit (0a7f82604) | 11,063 in 520 files, 11 clean files (slice 1 unchanged at 2,504 in 140 files) |

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

The spell store holds 35,889 `SpellInfo` records (35,732 of them `DIFFICULTY_NONE`, this server, ids up to 9,970,801), so
nothing is filled eagerly (D2).
`tbcompat::InitSpellTable()` runs from the module's `WorldScript::OnWorldInitialized`, after
`World::SetInitialWorldSettings` has loaded the spell store (`World.cpp:1471-1491`), and again on demand from the id
bound's first lookup (or a first spell lookup) once the hook has set the readiness flag. It collects the ids of the
`DIFFICULTY_NONE` records into a sorted, immutable array and allocates one `std::atomic<SpellEntry*>` slot per id
(`forever/src/compat/TbSpellEntry.cpp`): storage is proportional to the number of spells, not to the highest id.
`tbcompat::SpellProto(id)` binary-searches the array and makes one acquire load of the slot; the first reader of an id
builds its mirror and publishes it with a compare-exchange, and a racing builder deletes its own copy (both copies come
from the same immutable `SpellInfo`, so either is correct). The id index itself is built once: the store scan and the
publication are both inside the builder's mutex, so the hook and a lazy caller cannot walk the store at the same time, and
a caller that arrives during a build waits for the mutex and then uses the published index. There is no second table.

**Only the `DIFFICULTY_NONE` record is mirrored.** SpellMgr's map is keyed on `(Id, Difficulty)` (`SpellMgr.cpp:53-67`)
and `ForEachSpellInfo` walks every variant, so publishing whichever variant reached the slot first would let a raid
creature's spell variant answer for the id. `SpellProto(SpellInfo const*)` with another difficulty resolves to the same
id's NONE record when one exists and returns nullptr (counted, logged once) when it does not; `.tb spellscan` prints how
many records have a non-NONE difficulty and how many of their ids have no NONE record.

**Readiness: nothing touches the store before the hook says so.** An index built from a partly loaded store would keep
its partial contents for the process lifetime (the core has no spell reload, below), so publication now depends on an
explicit flag. `tbcompat::MarkSpellStoreReady()` is called by `WorldScript::OnWorldInitialized` and by nothing else,
immediately before its `InitSpellTable()` (`TbScripts.cpp:60-61`). It is a release store; every reader is an acquire load.
While it is unset, `InitSpellTable` returns before the store pass, both `SpellProto` overloads return nullptr before they
read `sSpellMgr` at all, and `MaxSpellId` returns 0 — each case logged once (`g_notReadyLookupLogged`,
`g_notReadyBoundLogged`), and nothing is published. After the flag, a lazy build is the hook's own code under the same
mutex; a lookup that arrives during the build waits on that mutex, then sees the index (it never falls back, and it cannot
see a dangling pointer: the index and every mirror are allocated once and never freed, the index is published with a
release store and read with an acquire load, and the only `delete` is a losing builder's own copy, which was never
published).

The module registers three scripts on this build (the root `CMakeLists.txt` compiles `forever/src/*.cpp`;
`TbScripts.cpp:429-434`): `tortoisebots_world` (a `WorldScript`), `tortoisebots_player` (a `PlayerScript`) and
`tortoisebots_commands` (a `CommandScript`). When each fires relative to `World::SetInitialWorldSettings` (core at
6517af05):

| hook | fires | relative to the flag |
|---|---|---|
| `WorldScript::OnConfigLoad` (`TbScripts.cpp:46`) | `World.cpp:2065`, inside `SetInitialWorldSettings` after `sScriptMgr->Initialize()` (and `World.cpp:1334` on `.reload config`) | before |
| `WorldScript::OnWorldInitialized` (`TbScripts.cpp:51`) | `ScriptMgr.cpp:1549-1551`, called from `Main.cpp:372` after `SetInitialWorldSettings` returns | sets it |
| `WorldScript::OnBeforeMapsUpdate` (`TbScripts.cpp:65`) | `World.cpp:2465`, from `World::Update`, which runs in `WorldUpdateLoop` (`Main.cpp:465`) after `Main.cpp:372` | after |
| `PlayerScript::OnLogin` (`TbScripts.cpp:83`) | needs an accepted session; the listener starts at `Main.cpp:414` (`StartNetwork`), after `Main.cpp:372` | after |
| `CommandScript` handlers (`TbScripts.cpp:96`) | CLI thread starts at `Main.cpp:454`, after `Main.cpp:372`; socket commands need the same listener | after |

The only hook that can run first is `OnConfigLoad`, whose body is `BotMgr::LoadConfig()` (`TbScripts.cpp:46-49`); that
reads two config keys (`TbBotMgr.cpp:37-41`) and no spell or item store.

`ItemUsageValue.cpp:92` (`IsCraftingTool`'s function-local static) is the one cache in the bot brain that a spell lookup
could leave empty for the process lifetime. Its only caller is `ItemUsageValue::Calculate` (`ItemUsageValue.cpp:147`), a
strategy value the bot brain evaluates during an AI update; on this build the brain is reached only from `BotMgr::Update`
(`TbBotMgr.cpp:188`, `Think` at `:231`), which the module calls from `OnBeforeMapsUpdate` and `OnShutdown`
(`TbScripts.cpp:67`, `:74`). No hook of this module can reach it before the flag on ForeverCore, so it stays an open item
only for a future path that looks a spell up from `OnConfigLoad`. (`.tb spell`/`.tb spellscan`/`.tb item` do not use it:
they read the mirror directly, `TbScripts.cpp:268` (`spellscan`) and `:345` (`spell`).)

**Nothing is allocated when `TortoiseBots.Enable = 0`**: `InitSpellTable` returns before the pass and every lookup returns
nullptr, with one log line. That keeps a disabled module at zero cost, including the old 76 MiB table.

**No spell reload at the pin.** `LoadSpellInfoStore` and every pass that edits the `SpellInfo` objects
(`LoadSpellInfoCorrections`, `LoadClassicSpellInfo`, `LoadSpellInfoCustomAttributes`) are called once from
`World::SetInitialWorldSettings` (`World.cpp:1471`, `:1477`, `:1480`, `:1491`). The `.reload` handlers
(`scripts/Commands/cs_reload.cpp:301-317`) reload `spell_required`, `spell_area`, `spell_group`, `spell_learn_spell`,
`spell_linked_spell`, `spell_proc`, `spell_target_position`, `spell_threats`, `spell_group_stack_rules`,
`spell_pet_auras` and `spell_script_names` — side tables, never the store and never a `SpellInfo`. So a mirror is only as
fresh as the core's object, which does not change after startup. Recorded in `PORT-PLAN.md` rule 3 next to the item note.

**Memory.** 35,732 ids: the sorted id array and the slot array are `4 x n` and `8 x n` bytes plus the index header, about
419 KB in all; `.tb spellscan` prints both numbers. The old design was one slot per id up to 9,970,801, 76 MiB, allocated
even with the module off.

**Id bound.** `tbcompat::MaxSpellId()` returns the highest id + 1, the old core's `GetMaxSpellId()` semantics
(`SpellMgr.h:721-722` returns the map size; the cmangos shim's `GetMaxEntry` comment at `ai/cmangos-compat-shim.h:215`
says the same), so the six `id < bound` loops include the highest spell. It builds the index itself once the readiness
flag is set, so a static cache of the bound (for example `ItemUsageValue.cpp:92`, `IsCraftingTool`'s `tools` set) cannot
latch a zero: the first call after the flag gets the real bound. Before the flag it answers 0, unbuilt and logged once.

**Clamps and casts in the fill** (a clamp is kept only where the core's signed value is a sentinel the old field cannot
express as a value):

| field | now | clamp | why |
|---|---|---|---|
| `powerType` | `uint32(PowerType)`, no clamp | — | `POWER_HEALTH` is -2 on both cores; Turtle's uint32 column held `0xFFFFFFFE`, which the bot's health-cost branch tests |
| `manaCost` | `uint32` of the classic row / `PowerCosts[0]->ManaCost`, no clamp | — | the old column is the same raw number as a uint32 |
| `ManaCostPercentage` | truncating `uint32(...)` (not `lround`) | negative/zero percent -> 0 | the old column is an unsigned integer percentage; Turtle's loader parsed the same float column with `GetUInt32` |
| `EffectDieSides` | the signed vanilla value, no clamp | — | a negative count is a valid range on Turtle (`Object.cpp:4435-4454` rolls `irand(randomPoints, baseDice)`) |
| `EffectChainTarget` | `uint32(ChainTargets)` | negative -> 0 | the core's field is a signed chain-target count (`Spell.cpp:1838-1851`); a negative is not a count, and 0 is the old column's "not a chain" |
| `Totem[i]` | `uint32(Totem[i])` | negative -> 0 | the core's -1 is its "no totem" sentinel; the old column's "none" is 0, and the bit pattern would pass the bot's `Totem[0] > 0` test |
| `ReagentCount[i]` | `uint32(ReagentCount[i])` | negative -> 0 | an int16 count in the core (`DB2Structure.h:4169`); a negative is not a count and the old field's "no reagent" is 0 |

`EffectBaseDice` stays 1 for every effect the core has no column for; see Known differences below (62 matched effects have
0 in the 1.12 data, 6 have 2, 3 or 500, and the core's table carries none of them).

## What the shim does now

- No `#define SpellEntry SpellInfo`: `SpellEntry` is the mirror (`TbSpellEntry.h`).
- No `#define GetSpellEntry(...) GetSpellInfo(...)`. Every fetch is `GetSpellEntryCompat(...)` on both cores:
  - `sSpellMgr.GetSpellEntry(id)` (32 sites),
  - `sSpellTemplate.LookupEntry<SpellEntry>(id)` (54 sites; the proxy returns the mirror and its `GetMaxEntry()` is one
    past the highest id the core has, which the mirror computes on first use),
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
  `minRange`/`maxRange` here, `RangeMin[0]`/`RangeMax[0]` there) and `GetMaxSpellIdCompat()` (one past the highest spell
  id, the Turtle core's `sSpellMgr.GetMaxSpellId()`, so a `id < bound` loop covers the last id).
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
| `ManaCostPercentage` (:666) | the same classic cost row's `ManaCostPct`; else `uint32(PowerCosts[0]->PowerCostPct)` (truncating, as the old loader's `GetUInt32` did) | percent of the caster's base mana, the quantity the old field held |
| `rangeIndex` (:565) | `RangeEntry->ID` (F.h:402) | the client's SpellRange id: the core keeps the row, and the row's ID is the index the old column held and the bot looks up in `sSpellRangeStore`. The old struct's default (1) stays when the core has no row |
| `speed` (:566) | `Speed` (F.h:403) | missile speed |
| `StackAmount` (:568) | `StackAmount` (F.h:406), `SpellAuraOptions::CumulativeAura` | max stacks |
| `Totem[2]` (:569) | `Totem` (F.h:407) | the two totem item ids |
| `Reagent[8]`, `ReagentCount[8]` (:570-571) | `Reagent`, `ReagentCount` (F.h:409-410) | the SpellReagents row |
| `EquippedItemClass`, `EquippedItemSubClassMask`, `EquippedItemInventoryTypeMask` (:572-574) | the three fields (F.h:413-415) | SpellEquippedItems row |
| `Effect[i]` (:575) | `GetEffects()[i].Effect` (F.h:236) | `SpellEffects` ids; filled for the first three slots by EffectIndex, the rest counted in `MoreEffects` |
| `EffectDieSides[i]` (:576) | `classic_spell_value.die_sides` (F `ClassicSpellValue.h:38-46`, `SpellMgr.cpp:3678-3720`) | the vanilla dice of the same effect, matched by the core's own effect/aura/misc check; without a row a DB2 effect is a single result (die sides 1, below) |
| `EffectBaseDice[i]` (:577) | 1 (the core has no base-dice column; see Known differences) | the vanilla column's die count, which is 1 for the vast majority of the matched effects; the DB2 has no dice at all |
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

## Known differences (the core's answer where the bot used the old core's)

These are places where the mirror or a helper asks the core the same question and the core answers differently from the
Turtle core. No code change; each is a value the bot may see differ.

- **`IsAreaOfEffectSpell` counts area auras.** It asks `SpellInfo::IsAffectingArea()` (which includes area-aura effects),
  not the old core's computed "has an area effect" flag, so Devotion Aura `465` reports "area" on ForeverCore where the
  old core did not.
- **`IsPositiveSpell` has no `spell_mod` overrides or special cases.** ForeverCore is `NegativeEffects.none()`
  (`SpellInfo.cpp:2036-2044`), a set it builds while loading the store. Turtle's `SpellEntry::IsPositiveEffect`
  (`SpellEntry.cpp:891-925`) also honours the `SPELL_CUSTOM_POSITIVE`/`SPELL_CUSTOM_NEGATIVE` bits and a list of special
  cases (Hellfire, Awaken Vault Warder `10258`, Kodo Kombobulator `18153`, the suicide/sacrifice instakills); a spell
  whose 1.12 data carried one of those is classified by the core's own rules here.
- **`IsAutocastable` also refuses `SPELL_ATTR1_NO_AUTOCAST_AI`** (`SpellInfo.cpp:1915-1922`). The Turtle shim only refuses
  passive spells (`ai/cmangos-compat-shim.h:416-424`), because the Turtle core's Vanilla `SpellEntry` has no such bit, so
  a ForeverCore spell with that attribute is not autocastable where the old build might have allowed it.
- **`GetSpellCastTime` ignores the caster.** The core's `CalcCastTime(Spell*)` has no caster parameter and no
  `CastTimePerLevel` term; Turtle's `GetCastTime` raises the cast time with the caster's spell rank
  (`SpellEntry.cpp:582`: `CastTimePerLevel * (spellRank / 5 - baseLevel)`). The mirror's two-argument form takes the
  caster and passes it to nothing.
- **`EffectRadiusIndex` takes target A's radius only.** The core splits an effect's radius into `TargetARadiusEntry` and
  `TargetBRadiusEntry`; the old 1.12 column was one index, and the mirror keeps A.
- **`Rank` is `"Rank N"`.** 1.12 kept the rank text itself in `spell_template`; ForeverCore has only
  `SpellChainNode::rank`, so a rank-1 chain member can print "Rank 1" where vanilla may have had an empty string.
- **The 59 `< MAX_EFFECT_INDEX` loops miss an effect at index 3 or more.** `grep -rn '< MAX_EFFECT_INDEX' ai runtime
  commands` (compat headers excluded) finds 59 loops; a ForeverCore spell with an effect at a later index is only seen by
  the first three. The paths the review named: `ReactionCastIsHeal` (`PlayerbotAI.cpp:1092`), the heal-target guard in
  `HealthTriggers.cpp:125`, the craft-effect scan in `AhBotItemPool.cpp:449`, `FindCraftEffectFor` (`GuildOrderCraft.h:42`,
  with the `< 3` loops at `:26` and `:118`), and the rest of the 59. `.tb spellscan` counts the spells with more than three
  effects.
- **`EffectBaseDice` is 1 for every mirrored effect.** ForeverCore's `classic_spell_value` has no base-dice column
  (`SHOW COLUMNS`: spell_id, effect_index, effect, apply_aura, base_points, die_sides, points_per_level, misc_value,
  points_per_combo, bonus_coeff, spell_name), and neither does the DB2 effect, so the core holds no value to carry. In the
  1.12 data the column is 1 for 7,420 of the 7,611 effects the core matched and 0 for 62 (plus 6 with 2, 3 or 500), so a
  mirrored `CalculateSimpleValue` (the debug aura apply, `PlayerbotAI.cpp:5905`) can be one or two points high on those 68
  effects; the two production readers are log lines. Fixing it exactly needs a core data column, not a mirror change.

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

The boot's item line now answers the two round-2 open checks:

```
TortoiseBots: item mirrors filled: 26054 items in 38 ms, 0 with a Stackable column <= 0 or INT_MAX, 379 incomplete (0 at required level 60 or less), 0 incomplete ammo items whose ItemDamageAmmo row exists but the five-store damage pre-check rejects: 284846 (no row for item level 298 in ItemDamageOneHand, ItemDamageOneHandCaster, ItemDamageTwoHand, ItemDamageTwoHandCaster); ...
```

```
TortoiseBots: spell mirrors: 35732 spells in 88 ms, spell id bound 9970802 (index 142952 B + slots 285856 B), 0 incomplete
TortoiseBots: effects: 45345 effect slots in all, 456 spells with more than 3 effects (first: 1223037 (5), 1226997 (1), 436097 (3), 1310285 (1), 1247430 (4)), 136 spells with more than one school.
TortoiseBots: difficulty: 157 SpellInfo records with a difficulty other than NONE, 0 of their ids have no DIFFICULTY_NONE record (mirror null; 0 such lookups counted).
```

35,732 is the number of `DIFFICULTY_NONE` records; the 157 difficulty variants are not mirrored (their ids all have a NONE record, so
each variant resolves to it). The old scan reported 35,889 because it walked every variant. Index 142,952 B + slots 285,856 B
(about 419 KB) and the bound is 9,970,802, one past the highest id. No assertion: the scan runs every fill path once and the
boot log has `ASSERTION FAILED` 0.

With `TortoiseBots.Enable = 0`, the same scan prints `0 spells ... (index 0 B + slots 0 B)` and the first lookup logs
`TortoiseBots: spell mirrors are off (TortoiseBots.Enable = 0); spell lookups return null.` — nothing is allocated.

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

TC> tb spell 11687
TortoiseBots: spell 11687 "Life Tap ": school 5 (mask 0x20), category 0, cast time 0 ms, range 0.0-0.0 (idx 1), duration 0 ms, levels 36/46/36 (base/max/spell), recovery 0/0 ms, GCD 1500 ms (category 133), mana 0 (0%), power 4294967294.
TortoiseBots: effect 0: type 3, aura 0, base points 209 + 1, die sides 1, misc 0, target A/B 1/0, radius 28, trigger 0, item 0, chain 0, amp 0, points/level 1.00.

`power 4294967294` is `0xFFFFFFFE`, the uint32 image of `POWER_HEALTH` (-2): the old uint32 column held the same bits and the
bot's health-cost branch (`PlayerbotAISpells.cpp:52-59`) tests that value.

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

The five ends of the binary search, from the same boot (`tb spell <id>` on the console):

| id | what it is | result |
|---|---|---|
| `0` | below the lowest id | `spell 0 does not exist.` |
| `1` | the lowest id (position 0 of the sorted array) | `spell 1 "Word of Recall (OLD) "`: school 3 (mask 0x8), cast 10000 ms, mana 10, effect 0 type 252 |
| `9970801` | the highest id (last position) | `spell 9970801 "Command (pet) "`: school 0, cast 0 ms, duration -1 ms |
| `9970802` | max id + 1, the bound `MaxSpellId()` returns | `spell 9970802 does not exist.` |
| `9970800` | a gap below the highest id (absent while 9,970,801 is the last present id) | `spell 9970800 does not exist.` |

`5000000`, a second gap in the sparse upper range, also answered `does not exist.`, and `.tb spellscan` reported 35,732
spells with the bound 9,970,802: the low end, the high end and both sides of the bound answer as the search expects.

### D7: the numbers against vanilla rank 1

| spell | mirror | vanilla 1.12 rank 1 | difference |
|---|---|---|---|
| Fireball 133 | 1500 ms cast, 0-35 yd, 30 mana, effect 0 type 2 (school damage), base points 17 (+1 = 18), die sides 1, school 2 (fire), 4000 ms duration (the DoT) | 1.5 s, 35 yd, 30 mana, 13 + 1d9 = 14..22 fire plus a 4 s DoT | cast, range, mana, effect, school and duration are the vanilla ones. The base points are 18, the **mean** of the vanilla roll, and the die sides are 1, because this server's `classic_spell_value` has **no row for 133** (the table loaded 5,984 rows, 163 of which did not match the client spell). Without the row the mirror takes the core's `BasePoints`, which is what the core casts; the vanilla pair (13, 9) is not recoverable from it. No factor-of-two anywhere |
| Lesser Heal 2050 | 1500 ms, 0-40 yd, 30 mana, effect 0 type 10 (heal), base points 50 (+1 = 51), die sides 1, school 1 (holy) | 1.5 s, 40 yd, 30 mana, 46..56 | same: cast, range and mana match; 51 is exactly the mean of 46..56, and the die sides are 1 for the same missing-row reason |

So the mirror agrees with the core's own numbers on every field the bot reads; where it differs from the vanilla
*minimum* it is the vanilla *mean*, which is the number the core uses. The one thing that would make it exact is the
`classic_spell_value` row, which the core's data supplies for 5,984 spells on this server and not for these two.

Table memory: the sorted id index is `4 x 35,732` bytes and the mirror slot array `8 x 35,732` bytes, plus the small
index header: **142,952 + 285,856 = about 419 KB** on this server (`.tb spellscan` prints the exact two numbers). Nothing
is allocated when `TortoiseBots.Enable = 0`; the old design (one atomic per id up to 9,970,801) was 76 MiB at startup
even with the module off.

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
- **Table memory.** About 419 KB (see the D7 section), proportional to the spell count; nothing when the module is off.
- **Tests.** Eight wiring tests pin the exact expression at the call site they guard, and three Viscidus harnesses compile
  production method bodies against fake types; they were updated to the new spelling and to define the helper (the same
  treatment round 2's B1/B2 gave their pinned tests). No test was deleted or weakened in what it checks.

## Gates

All on the final tree; the only commit after them is this notes file.

| Gate | Command | Result |
|---|---|---|
| G1 module compiles on ForeverCore | `forever-build/build.sh 12 worldserver` | PASS: `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=13` at 0a7f82604 (warm ccache; TbSpellEntry.cpp and the module relinked, worldserver written) |
| G5 live boot | rsync core + module to `forever-build/src`, build, `forever-run/setup.sh`, `world.sh start`, poll `world.out` for `ready...` / `ASSERTION FAILED` / the item and spell lines, `tb spellscan`, `tb spell 133/2050/11687` (11687 = Life Tap, power 4294967294), `world.sh stop` | PASS at 0a7f82604: `ready...` 1, `ASSERTION FAILED` 0, the item mirrors line and the spellscan/difficulty lines above, all three `tb spell` outputs; a second boot with `TortoiseBots.Enable = 0` printed the "spell mirrors are off" line and `0 spells ... (index 0 B + slots 0 B)`; server stopped, `forever-world` removed |
| G2 Turtle build | `vm-build ~/wt-forever-port --wait` | PASS: `sha=0a7f82604 dirty-entries=0`, `COMPILE_SECONDS=1807 CMAKE_RC=0 MAKE_RC=0`, `QUEUE_RC=0`, slot 2, mangosd `8a540a0c...74e0fd` and realmd `85c40603...373868` written |
| G3 Turtle unit tests | `vm-test tb ~/wt-forever-port` | PASS: `RESULT: PASS` rc=0, `sha=0a7f82604 dirty=0 files=3601`, `vm_duration=1430s` (the whole `tools/verify_all.sh`: OKF, surface, wiring, 2,656 unit tests, decision trail; also PASS at 6f6bce297, 1024 s) |
| G4 core-free tests on the ForeverCore toolchain | `policy-tests.sh` on the tree under test | 472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL in 323 s at 0a7f82604 (run beside the build and census jobs) - the same set as the round-2 baseline and round 3: `test_combat_monitor_service`, `test_combat_monitor_service_race`, `test_decision_capture_files`, `test_run_trace_writer` (COMPILE_FAIL), `test_decision_capture_codec`, `test_log_file_rotation` (RUN_FAIL). Nothing got worse |

### Fix round 2 (the readiness gate)

That round changed only `forever/`: `forever/src/compat/TbSpellEntry.cpp`, `TbSpellEntry.h`, `TbScripts.cpp`,
`SHIM-R3-NOTES.md` and `PORT-PLAN.md`. Nothing outside `forever/` changed, so G2/G3/G4 keep their fix-round-1 results
above; G1 and G5 were re-run:

| Gate | Command | Result |
|---|---|---|
| G1 module compiles on ForeverCore | core + this worktree rsynced to `forever-build/src`, `forever-build/build.sh 12 worldserver` | PASS at 17dc70a9c: `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=23`; ninja rebuilt `TbSpellEntry.cpp` and `TbScripts.cpp` (`[2/5]`, `[3/5]`), then `libtortoisebots.a` and `bin/Release/bin/worldserver` |
| G5 live boot | `forever-run/setup.sh`, `world.sh start`, poll `world.out`, `tb spellscan`, `tb spell 0/1/9970801/9970802/9970800`, `tb spell 133`, `world.sh stop` | PASS at 17dc70a9c: `ready...` 1 (60 s poll), `ASSERTION FAILED` 0; `spell mirrors: 35732 spells in 82 ms, spell id bound 9970802 (index 142952 B + slots 285856 B), 0 incomplete`; the five lookups above; no `before the spell store was ready` line (nothing looked a spell up before the hook) and no `Error`/`Exception` line; server stopped, `forever-world` gone |
