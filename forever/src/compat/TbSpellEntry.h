// The bot brain reads spell data as public fields of cmangos's SpellEntry (`spellInfo->Effect[i]`, `pSpellInfo->Reagent[k]`,
// `spellInfo->EffectBasePoints[i]`, ...). ForeverCore's SpellInfo keeps the effects in a vector of SpellEffectInfo rows, base
// points as floats, and has no Effect[]/Category/manaCost field at all, so a macro cannot turn those reads into calls.
//
// So this is our SpellEntry: the old field names and units, filled from the core's SpellInfo on first use (TbSpellEntry.cpp)
// and kept for the life of the process. Only the fields the bot actually reads are here, each with what it means on the Turtle
// core and where the ForeverCore value comes from. A used field with no true ForeverCore counterpart is left out on purpose
// (its uses stay census errors): SHIM-R3-NOTES.md lists those with their uses.
//
// Effect values and dice: the core's `BasePoints` is the *average* of the vanilla roll (Fireball 133: 18, the mean of 14..22),
// while the old field is the client's base points (13) that the bot code raises by one. The core keeps the vanilla roll itself
// in `classic_spell_value` (base points, die sides, points per level, misc value, points per combo, bonus coefficient - the same
// columns of the same 1.12 spell_template the Turtle core reads), and `SpellMgr::GetClassicSpellValue` hands it out; that row is
// used whenever the core matched it to the effect, so the dice are the vanilla dice. See SHIM-R3-NOTES.md for the fallback.
#pragma once

#include "SpellInfo.h"      // SpellInfo, SpellEffectInfo, SpellPowerEntry, flag128
#include "SpellMgr.h"       // sSpellMgr, SpellMgr::GetClassicSpellValue / GetClassicSpellCost
#include "ClassicSpellValue.h"
#include <array>
#include <atomic>
#include <mutex>
#include <string>

// The old core's effect slot limit (DBCEnums.h:159-163 on the Turtle core: EFFECT_INDEX_0..2). ForeverCore spells may carry
// more effects; the mirror holds the first three by EffectIndex and `MoreEffects` counts the rest (`.tb spellscan` logs it).
#define MAX_EFFECT_INDEX 3

// The old core's locale count (shared/Common.h:237): the eight 1.12 client locales. LOCALE_enUS is 0 in both cores.
constexpr uint32 MAX_DBC_LOCALE = 8;

// The client's spell-effect slot enum under its cmangos name; ForeverCore calls it SpellEffIndex (EFFECT_0..).
using SpellEffectIndex = SpellEffIndex;

// The old core's effect slot names (Turtle: DBCEnums.h:153-163, EFFECT_INDEX_0..2 and MAX_EFFECT_INDEX 3). ForeverCore calls
// the slots EFFECT_0.. and allows more of them; the bot's loops and masks use the first three.
#define EFFECT_INDEX_0 EFFECT_0
#define EFFECT_INDEX_1 EFFECT_1
#define EFFECT_INDEX_2 EFFECT_2

struct SpellEntry
{
    // -- what the bot reads -------------------------------------------------------------------------------------------------
    uint32    Id = 0;                                     // T :529  DBC spell id                  F Id                          SpellInfo.h:335
    uint32    School = 0;                                 // T :530  single school id 0..6         F lowest set bit of SchoolMask SpellInfo.h:427
    uint32    Category = 0;                               // T :531  SpellCategory id              F GetCategory()               SpellInfo.cpp:2136
    uint32    Dispel = 0;                                 // T :533  DispelType                  F Dispel                      SpellInfo.h:336
    uint32    Mechanic = 0;                               // T :534  Mechanics                   F Mechanic                    SpellInfo.h:337
    uint32    Attributes = 0;                             // T :535  Attr0 bits                  F Attributes                  SpellInfo.h:338
    uint32    AttributesEx = 0;                           // T :536  Attr1 bits                  F AttributesEx                SpellInfo.h:339
    uint32    AttributesEx2 = 0;                          // T :537  Attr2 bits                  F AttributesEx2               SpellInfo.h:340
    uint32    AttributesEx3 = 0;                          // T :538  Attr3 bits                  F AttributesEx3               SpellInfo.h:341
    uint32    AttributesEx4 = 0;                          // T :539  Attr4 bits                  F AttributesEx4               SpellInfo.h:342
    uint32    Targets = 0;                                // T :542  TARGET_FLAG_* mask          F Targets                     SpellInfo.h:436
    uint32    TargetCreatureType = 0;                     // T :543  creature type mask          F TargetCreatureType          SpellInfo.h:437
    uint32    RequiresSpellFocus = 0;                     // T :544  spell focus object id       F RequiresSpellFocus          SpellInfo.h:438
    uint32    RecoveryTime = 0;                           // T :548  ms, own cooldown            F RecoveryTime                SpellInfo.h:414
    uint32    CategoryRecoveryTime = 0;                   // T :549  ms, category cooldown       F CategoryRecoveryTime       SpellInfo.h:415
    uint32    InterruptFlags = 0;                         // T :550  SpellInterruptFlags bits    F InterruptFlags raw          SpellInfo.h:422
    uint32    AuraInterruptFlags = 0;                     // T :551  SpellAuraInterruptFlags     F AuraInterruptFlags raw      SpellInfo.h:423
    uint32    ChannelInterruptFlags = 0;                  // T :552  SpellChannelInterruptFlags  F ChannelInterruptFlags raw   SpellInfo.h:425
    uint32    procChance = 0;                             // T :554  percent                     F ProcChance                  SpellInfo.h:430
    uint32    procCharges = 0;                            // T :555  0 = unlimited               F ProcCharges                 SpellInfo.h:431
    uint32    maxLevel = 0;                               // T :556  last level the spell scales to  F MaxLevel                SpellInfo.h:435
    uint32    baseLevel = 0;                              // T :557  level the spell starts at   F BaseLevel                   SpellInfo.h:436
    uint32    spellLevel = 0;                             // T :558  level it is learned at      F SpellLevel                  SpellInfo.h:437
    uint32    powerType = 0;                              // T :560  Powers of the first cost row  F PowerCosts[0]->PowerType  SpellInfo.h:401
    uint32    manaCost = 0;                               // T :561  flat mana, 0 = none         F see Fill (classic cost row, else PowerCosts[0]->ManaCost)
    uint32    rangeIndex = 1;                             // T :565  SpellRange id, looked up in the store  F RangeEntry->ID     SpellInfo.h:402
    float     speed = 0.f;                                // T :566  missile speed, y/s          F Speed                       SpellInfo.h:403
    uint32    StackAmount = 0;                            // T :568  max stacks                  F StackAmount                 SpellInfo.h:406
    uint32    Totem[MAX_SPELL_TOTEMS] = {};               // T :569  totem item ids              F Totem                       SpellInfo.h:407
    int32     Reagent[MAX_SPELL_REAGENTS] = {};           // T :570  item ids                    F Reagent                     SpellInfo.h:409
    uint32    ReagentCount[MAX_SPELL_REAGENTS] = {};      // T :571  counts                      F ReagentCount                SpellInfo.h:410
    int32     EquippedItemClass = -1;                     // T :572  required item class         F EquippedItemClass           SpellInfo.h:413
    int32     EquippedItemSubClassMask = 0;               // T :573                          F EquippedItemSubClassMask    SpellInfo.h:414
    int32     EquippedItemInventoryTypeMask = 0;          // T :574                          F EquippedItemInventoryTypeMask SpellInfo.h:415
    uint32    Effect[MAX_EFFECT_INDEX] = {};              // T :575  SPELL_EFFECT_*              F GetEffects()[i].Effect      SpellInfo.h:236
    int32     EffectDieSides[MAX_EFFECT_INDEX] = {};      // T :576  die sides of the roll       F classic_spell_value die_sides (else 1)
    uint32    EffectBaseDice[MAX_EFFECT_INDEX] = {};      // T :577  dice count (1 in vanilla)   F 1: no core column (Known differences)
    float     EffectRealPointsPerLevel[MAX_EFFECT_INDEX] = {};  // T :578 added per level  F classic row points_per_level / RealPointsPerLevel
    int32     EffectBasePoints[MAX_EFFECT_INDEX] = {};    // T :579  base - 1 of the roll        F classic row base_points (else round(BasePoints) - 1)
    float     EffectBonusCoefficient[MAX_EFFECT_INDEX] = {};  // T :580  spell power factor    F classic row bonus_coeff (else BonusCoefficient)
    uint32    EffectImplicitTargetA[MAX_EFFECT_INDEX] = {}; // T :586 TARGET_* (client ids)     F TargetA.GetTarget()         SpellInfo.h:252
    uint32    EffectImplicitTargetB[MAX_EFFECT_INDEX] = {}; // T :587                         F TargetB.GetTarget()         SpellInfo.h:253
    uint32    EffectRadiusIndex[MAX_EFFECT_INDEX] = {};   // T :592  SpellRadius id              F TargetARadiusEntry->ID      SpellInfo.h:254
    uint32    EffectApplyAuraName[MAX_EFFECT_INDEX] = {}; // T :595  SPELL_AURA_*                F ApplyAuraName               SpellInfo.h:238
    uint32    EffectChainTarget[MAX_EFFECT_INDEX] = {};   // T :604  chain jumps                 F ChainTargets                SpellInfo.h:257
    uint32    EffectItemType[MAX_EFFECT_INDEX] = {};      // T :607  created item id             F ItemType                    SpellInfo.h:258
    int32     EffectMiscValue[MAX_EFFECT_INDEX] = {};     // T :610  effect-specific value       F MiscValue                   SpellInfo.h:245
    uint32    EffectTriggerSpell[MAX_EFFECT_INDEX] = {};  // T :611  triggered spell id          F TriggerSpell                SpellInfo.h:260
    float     EffectPointsPerComboPoint[MAX_EFFECT_INDEX] = {}; // T :614  T :614 F classic row points_per_combo (else PointsPerResource)
    std::array<std::string, MAX_DBC_LOCALE> SpellName{};  // T :630  the name, per locale        F SpellName->Str[locale]      SpellInfo.h:420
    std::array<std::string, MAX_DBC_LOCALE> Rank{};       // T :633  "Rank N"                    F derived from ChainEntry->rank (see Fill)
    uint32    ManaCostPercentage = 0;                     // T :666  percent of the caster's base mana  F see Fill (classic cost row, else PowerCosts[0]->PowerCostPct)
    uint32    StartRecoveryCategory = 0;                  // T :667  GCD category                F StartRecoveryCategory       SpellInfo.h:418
    uint32    StartRecoveryTime = 0;                      // T :668  GCD in ms                  F StartRecoveryTime           SpellInfo.h:419
    uint32    MaxTargetLevel = 0;                         // T :670  0 = no cap                  F MaxTargetLevel              SpellInfo.h:447
    uint32    SpellFamilyName = 0;                        // T :671  SpellFamily                 F SpellFamilyName             SpellInfo.h:449
    uint64    SpellFamilyFlags = 0;                       // T :672  low 64 bits of the class mask  F SpellFamilyFlags[0..1]    SpellInfo.h:450
    uint32    MaxAffectedTargets = 0;                     // T :673  0 = no cap                  F MaxAffectedTargets          SpellInfo.h:448
    uint32    DmgClass = 0;                               // T :674  SpellDmgClass               F DmgClass                    SpellInfo.h:451
    uint32    PreventionType = 0;                         // T :675  SpellPreventionType         F PreventionType              SpellInfo.h:452

    // -- diagnostics ---------------------------------------------------------------------------------------------------------
    SpellInfo const* Core = nullptr;                      // the core record this mirror was filled from
    uint32 MoreEffects = 0;                               // effects at index 3 and above (not mirrored, counted)
    bool        Incomplete = false;                        // a core row the fill needed was missing; the field feeds 0
    std::string IncompleteReason;

    // The old struct's helpers, answered by the core wherever the core asks the same question (SHIM-R3-NOTES.md).
    int32 CalculateSimpleValue(SpellEffectIndex eff) const { return EffectBasePoints[eff] + int32(EffectBaseDice[eff]); }
    bool IsPositiveSpell() const;
    bool IsPositiveEffect(SpellEffectIndex effIndex) const;
    bool IsPassiveSpell() const;
    bool IsChanneledSpell() const;
    bool IsNonCombatSpell() const;
    bool IsAreaOfEffectSpell() const;
    bool IsNextMeleeSwingSpell() const;
    bool IsFromBehindOnlySpell() const;
    bool HasEffect(SpellEffects effect) const;
    SpellSchoolMask GetSpellSchoolMask() const;
    bool IsSpellAppliesAura() const;
    bool IsSpellAppliesAura(uint32 effectMask) const;
    uint32 GetAllSpellMechanicMask() const;
    int32 GetDuration() const;
    int32 CalculateDuration(WorldObject const* caster = nullptr) const;
    uint32 GetCastTime(WorldObject* caster, Spell* spell = nullptr) const;
    uint32 GetRecoveryTime() const;
    SpellCastResult GetErrorAtShapeshiftedCast(uint32 form) const;
};

namespace tbcompat
{
    // Builds the index of DIFFICULTY_NONE spell ids and the dense slot array for their mirrors. Called once from the
    // module's WorldScript::OnWorldInitialized (the spell store is loaded by then: World.cpp:1471); a lookup of the id
    // bound calls it too, so a static cache of the bound cannot keep a zero. Nothing is allocated when
    // TortoiseBots.Enable = 0, and a call while the spell store is still empty builds nothing.
    void InitSpellTable();

    // One fill for both paths: the lazy path and `.tb spellscan` use the same function, so the same pre-checks apply.
    SpellEntry* BuildSpellEntry(SpellInfo const* info);

    // The mirror of a DIFFICULTY_NONE spell. A SpellInfo of another difficulty resolves to the same id's
    // DIFFICULTY_NONE mirror when one exists, and to nullptr (counted, logged once) when it does not. nullptr too when
    // the core has no such spell or the module is off. Safe from any thread (the bot brain runs on map threads).
    SpellEntry const* SpellProto(uint32 id);
    SpellEntry const* SpellProto(SpellInfo const* info);   // nullptr for nullptr
    SpellInfo const* SpellCore(SpellEntry const* entry);   // back to the core's object

    // The old core's GetMaxSpellId(): one past the highest spell id the core has, so a `id < bound` loop covers the
    // last id. Builds the index itself when the spell store is loaded and the index is not; 0 while the store is empty
    // or the module is off.
    uint32 MaxSpellId();

    // Bytes held by the immutable id index and by the mirror slot array; 0 when not built.
    uint64 SpellIndexBytes();
    uint64 SpellSlotBytes();

    // Lookups that asked for an id the core has only with a non-NONE difficulty (answered nullptr).
    uint32 NonNoneOnlyLookups();
}
