// The spell mirror (TbSpellEntry.h): one SpellEntry per spell id the core has, built on first use.
//
// Storage, in the shape round 2 gave the item store a lock-free read path for: an array of atomics indexed by spell id,
// sized once from the highest id the core has (InitSpellTable, from the module's WorldScript::OnWorldInitialized). The first
// reader of an id builds its mirror and publishes it with a compare-exchange; a racing builder frees its own copy. A lookup
// before the table exists, or for an id above it, takes a mutex-protected fallback map, so no caller can see a half-built
// mirror and no path is left without an answer.
//
// The core has no spell reload at the pinned commit: LoadSpellInfoStore and every pass that edits the SpellInfo objects
// (LoadSpellInfoCorrections, LoadClassicSpellInfo, LoadSpellInfoCustomAttributes) are called once from
// World::SetInitialWorldSettings (World.cpp:1471-1491) and no `.reload` handler calls them again (scripts/Commands/cs_reload.cpp
// reloads spell_required/spell_area/spell_proc and the other side tables only). So a mirror is only as fresh as the core's
// object, which never changes once built; PORT-PLAN.md carries the same rule for the pin.
#include "TbSpellEntry.h"
#include "Log.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace
{
std::atomic<std::atomic<SpellEntry*>*> g_table{nullptr};
std::atomic<uint32> g_tableSize{0};
std::atomic<uint32> g_maxSpellId{0};

std::mutex g_fallbackMutex;
std::unordered_map<uint32, SpellEntry const*> g_fallback;   // only used before the table exists or beyond its end
std::mutex g_buildMutex;                                    // one builder at a time: a fill reads a few core stores

// The single bit of a school mask as the old field's school id (0 normal, 1 holy, ... 6 arcane). A spell with several
// schools keeps the first; `.tb spellscan` counts those.
uint32 SchoolId(uint32 schoolMask)
{
    if (!schoolMask)
        return 0;
    for (uint32 i = 0; i < 8; ++i)
        if (schoolMask & (1u << i))
            return i;
    return 0;
}

void SetIncomplete(SpellEntry& entry, char const* reason)
{
    entry.Incomplete = true;
    if (!entry.IncompleteReason.empty())
        entry.IncompleteReason += "; ";
    entry.IncompleteReason += reason;
}
}

namespace tbcompat
{
void InitSpellTable()
{
    if (g_table.load(std::memory_order_acquire))
        return;

    uint32 maxId = 0;
    sSpellMgr->ForEachSpellInfo([&maxId](SpellInfo const* info)
    {
        if (info)
            maxId = std::max(maxId, info->Id);
    });

    std::unique_lock lock(g_buildMutex);
    if (g_table.load(std::memory_order_relaxed))
        return;

    // Not one atomic per possible id: the ids are dense enough (the store holds about 300k entries) that 8 bytes each is
    // a few megabytes, and the array is what makes every later lookup a single acquire load.
    std::size_t const count = std::size_t(maxId) + 1;
    std::atomic<SpellEntry*>* const table = new std::atomic<SpellEntry*>[count]();
    table[0].store(nullptr);

    // Publish the table, then move any mirror the lazy path built before this call into it.
    std::lock_guard fallbackLock(g_fallbackMutex);
    for (auto const& [id, entry] : g_fallback)
        if (id <= maxId)
            table[id].store(const_cast<SpellEntry*>(entry), std::memory_order_relaxed);
    g_tableSize.store(maxId + 1, std::memory_order_relaxed);
    g_maxSpellId.store(maxId, std::memory_order_relaxed);
    g_table.store(table, std::memory_order_release);
}

uint32 MaxSpellId()
{
    return g_maxSpellId.load(std::memory_order_acquire);
}

SpellEntry* BuildSpellEntry(SpellInfo const* info)
{
    if (!info)
        return nullptr;

    SpellEntry* const entry = new SpellEntry();
    entry->Core = info;
    entry->Id = info->Id;
    entry->School = SchoolId(info->SchoolMask);
    entry->Category = info->GetCategory();
    entry->Dispel = info->Dispel;
    entry->Mechanic = info->Mechanic;
    entry->Attributes = info->Attributes;
    entry->AttributesEx = info->AttributesEx;
    entry->AttributesEx2 = info->AttributesEx2;
    entry->AttributesEx3 = info->AttributesEx3;
    entry->AttributesEx4 = info->AttributesEx4;
    entry->Targets = info->Targets;
    entry->TargetCreatureType = info->TargetCreatureType;
    entry->RequiresSpellFocus = info->RequiresSpellFocus;
    entry->RecoveryTime = info->RecoveryTime;
    entry->CategoryRecoveryTime = info->CategoryRecoveryTime;
    entry->InterruptFlags = uint32(info->InterruptFlags.AsUnderlyingType());
    entry->AuraInterruptFlags = uint32(info->AuraInterruptFlags.AsUnderlyingType());
    entry->ChannelInterruptFlags = uint32(info->ChannelInterruptFlags.AsUnderlyingType());
    entry->procChance = info->ProcChance;
    entry->procCharges = info->ProcCharges;
    entry->maxLevel = info->MaxLevel;
    entry->baseLevel = info->BaseLevel;
    entry->spellLevel = info->SpellLevel;
    entry->speed = info->Speed;
    entry->StackAmount = info->StackAmount;
    entry->EquippedItemClass = info->EquippedItemClass;
    entry->EquippedItemSubClassMask = info->EquippedItemSubClassMask;
    entry->EquippedItemInventoryTypeMask = info->EquippedItemInventoryTypeMask;
    entry->StartRecoveryCategory = info->StartRecoveryCategory;
    entry->StartRecoveryTime = info->StartRecoveryTime;
    entry->MaxTargetLevel = info->MaxTargetLevel;
    entry->MaxAffectedTargets = info->MaxAffectedTargets;
    entry->DmgClass = info->DmgClass;
    entry->PreventionType = info->PreventionType;
    entry->SpellFamilyName = info->SpellFamilyName;
    entry->SpellFamilyFlags = (uint64(info->SpellFamilyFlags[1]) << 32) | info->SpellFamilyFlags[0];

    for (uint32 i = 0; i < MAX_SPELL_TOTEMS; ++i)
        entry->Totem[i] = info->Totem[i] < 0 ? 0 : uint32(info->Totem[i]);
    for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        entry->Reagent[i] = info->Reagent[i];
        entry->ReagentCount[i] = info->ReagentCount[i] < 0 ? 0 : uint32(info->ReagentCount[i]);
    }

    // Name: the core's LocalizedString has one slot per locale, in the same order as both cores' LocaleConstant, and a
    // slot may be absent (nullptr). Rank: 1.12 stores the rank text in spell_template, which the core does not carry; the
    // chain node's own rank is the number that text held, and the client shows it as "Rank N".
    if (info->SpellName)
    {
        for (uint32 i = 0; i < MAX_DBC_LOCALE; ++i)
            if (char const* name = (*info->SpellName)[LocaleConstant(i)])
                entry->SpellName[i] = name;
    }
    else
        SetIncomplete(*entry, "no SpellName row");

    if (info->ChainEntry && info->ChainEntry->rank)
    {
        std::string const rank = "Rank " + std::to_string(uint32(info->ChainEntry->rank));
        entry->Rank.fill(rank);
    }

    // Range: the old field is the SpellRange id the bot code looks up in the store; the core keeps the row itself, whose
    // ID is that id. The member default (1) stays when the core has no row, exactly the old struct's default.
    if (info->RangeEntry)
        entry->rangeIndex = info->RangeEntry->ID;

    // Power cost: the first row is the spell's power type; for mana the core charges the vanilla table's flat + percent
    // figure when it has the row (SpellInfo.cpp:4386-4395), so the same row is what the old fields held. Without it the
    // first row's flat cost and percent are the core's own base numbers.
    SpellPowerEntry const* const power = info->PowerCosts[0];
    if (power)
    {
        entry->powerType = power->PowerType < 0 ? 0 : uint32(power->PowerType);
        entry->manaCost = power->ManaCost < 0 ? 0 : uint32(power->ManaCost);
        entry->ManaCostPercentage = power->PowerCostPct <= 0.0f ? 0 : uint32(std::lround(power->PowerCostPct));

        if (power->PowerType == POWER_MANA && ClassicSpellValue::AppliesToSpell(info->Id))
        {
            if (ClassicSpellValue::Cost const* vanilla = sSpellMgr->GetClassicSpellCost(info->Id))
            {
                entry->manaCost = vanilla->ManaCost < 0 ? 0 : uint32(vanilla->ManaCost);
                entry->ManaCostPercentage = vanilla->ManaCostPct < 0 ? 0 : uint32(vanilla->ManaCostPct);
            }
        }
    }

    // Effects: the first three slots by EffectIndex. A spell with more is counted, not mirrored (MAX_EFFECT_INDEX is the
    // old core's three).
    std::vector<SpellEffectInfo> const& effects = info->GetEffects();
    for (size_t i = 0; i < effects.size(); ++i)
    {
        SpellEffectInfo const& effect = effects[i];
        if (i >= MAX_EFFECT_INDEX)
        {
            if (effect.Effect != SPELL_EFFECT_NONE || effect.ApplyAuraName != SPELL_AURA_NONE)
                ++entry->MoreEffects;
            continue;
        }

        entry->Effect[i] = uint32(effect.Effect);
        entry->EffectApplyAuraName[i] = uint32(effect.ApplyAuraName);
        entry->EffectImplicitTargetA[i] = uint32(effect.TargetA.GetTarget());
        entry->EffectImplicitTargetB[i] = uint32(effect.TargetB.GetTarget());
        entry->EffectMiscValue[i] = effect.MiscValue;
        entry->EffectItemType[i] = effect.ItemType;
        entry->EffectTriggerSpell[i] = effect.TriggerSpell;
        entry->EffectChainTarget[i] = effect.ChainTargets < 0 ? 0 : uint32(effect.ChainTargets);
        if (effect.TargetARadiusEntry)
            entry->EffectRadiusIndex[i] = effect.TargetARadiusEntry->ID;

        // The dice, from the vanilla row of this effect when the core matched one (SpellMgr.cpp:3696-3720: the effect
        // type, the aura and, for the auras whose value lives in it, the misc value all agree). Without a row the core's
        // BasePoints is the value the spell uses (the mean of the vanilla roll where there is one); the old field keeps
        // that number minus one, and a single-result effect has die sides 1 and one die - the shape the vanilla column
        // has for a fixed amount, which is what MinYieldPerCast and CalculateSimpleValue read.
        if (ClassicSpellValue::Value const* vanilla = sSpellMgr->GetClassicSpellValue(info->Id, uint32(i)))
        {
            entry->EffectBasePoints[i] = vanilla->BasePoints;
            entry->EffectDieSides[i] = vanilla->DieSides < 0 ? 0 : vanilla->DieSides;
            entry->EffectRealPointsPerLevel[i] = vanilla->PointsPerLevel;
            entry->EffectPointsPerComboPoint[i] = vanilla->PointsPerCombo;
            entry->EffectBonusCoefficient[i] = vanilla->BonusCoefficient >= 0.0f ? vanilla->BonusCoefficient : effect.BonusCoefficient;
        }
        else
        {
            entry->EffectBasePoints[i] = int32(std::lround(effect.BasePoints)) - 1;
            entry->EffectDieSides[i] = 1;
            entry->EffectRealPointsPerLevel[i] = effect.RealPointsPerLevel;
            entry->EffectPointsPerComboPoint[i] = effect.PointsPerResource;
            entry->EffectBonusCoefficient[i] = effect.BonusCoefficient;
        }
        entry->EffectBaseDice[i] = 1;
    }

    return entry;
}

SpellEntry const* SpellProto(SpellInfo const* info)
{
    if (!info)
        return nullptr;

    uint32 const id = info->Id;

    // Fast path: the published table, one acquire load, no lock.
    uint32 const size = g_tableSize.load(std::memory_order_acquire);
    std::atomic<SpellEntry*>* const table = g_table.load(std::memory_order_acquire);
    if (table && id < size)
    {
        std::atomic<SpellEntry*>& slot = table[id];
        if (SpellEntry* entry = slot.load(std::memory_order_acquire))
            return entry;

        SpellEntry* expected = nullptr;
        SpellEntry* built = BuildSpellEntry(info);
        if (slot.compare_exchange_strong(expected, built, std::memory_order_acq_rel))
            return built;
        delete built;                 // another thread published first; both built from the same immutable object
        return expected;
    }

    // Before InitSpellTable, or an id above the table: one lock, one map.
    std::lock_guard lock(g_fallbackMutex);
    auto [it, inserted] = g_fallback.try_emplace(id, nullptr);
    if (inserted)
        it->second = BuildSpellEntry(info);
    return it->second;
}

SpellEntry const* SpellProto(uint32 id)
{
    return SpellProto(sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE));
}

SpellInfo const* SpellCore(SpellEntry const* entry)
{
    return entry ? entry->Core : nullptr;
}
}

// ---------------------------------------------------------------------------------------------------------------
// The old struct's own helpers: each asks the core the same question through the mirror's core pointer. Where the core
// only has a wider or narrower form, the note says which.
// ---------------------------------------------------------------------------------------------------------------
bool SpellEntry::IsPositiveSpell() const
{
    return Core && Core->IsPositive();
}

bool SpellEntry::IsPositiveEffect(SpellEffectIndex effIndex) const
{
    return Core && Core->IsPositiveEffect(uint8(effIndex));
}

bool SpellEntry::IsPassiveSpell() const
{
    return Core && Core->IsPassive();
}

bool SpellEntry::IsChanneledSpell() const
{
    return Core && Core->IsChanneled();
}

bool SpellEntry::IsNonCombatSpell() const
{
    // The old core tested the attr0 bit that carries the same value on both cores (SPELL_ATTR_CANT_USED_IN_COMBAT /
    // SPELL_ATTR0_NOT_IN_COMBAT_ONLY_PEACEFUL, 0x10000000); the core's CanBeUsedInCombat also answers for a caster.
    return Core && Core->HasAttribute(SPELL_ATTR0_NOT_IN_COMBAT_ONLY_PEACEFUL);
}

bool SpellEntry::IsAreaOfEffectSpell() const
{
    // Old: the computed "has an area effect" flag. Core: any effect whose target is an area (plus area auras).
    return Core && Core->IsAffectingArea();
}

bool SpellEntry::IsNextMeleeSwingSpell() const
{
    return Core && Core->IsNextMeleeSwingSpell();
}

bool SpellEntry::IsFromBehindOnlySpell() const
{
    // Old: the two attribute bits of the client data, or the Turtle custom flag. The core computes the same set in
    // LoadSpellInfoCustomAttributes (SpellMgr.cpp:3368, :3374) into its own custom attribute.
    return Core && Core->HasAttribute(SPELL_ATTR0_CU_REQ_CASTER_BEHIND_TARGET);
}

bool SpellEntry::HasEffect(SpellEffects effect) const
{
    return Core && Core->HasEffect(effect);
}

SpellSchoolMask SpellEntry::GetSpellSchoolMask() const
{
    return Core ? Core->GetSchoolMask() : SPELL_SCHOOL_MASK_NONE;
}

bool SpellEntry::IsSpellAppliesAura() const
{
    return IsSpellAppliesAura(0xFFFFFFFF);
}

bool SpellEntry::IsSpellAppliesAura(uint32 effectMask) const
{
    if (!Core)
        return false;
    for (size_t i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (effectMask & (1u << i))
            if (i < Core->GetEffects().size() && Core->GetEffects()[i].IsAura())
                return true;
    return false;
}

uint32 SpellEntry::GetAllSpellMechanicMask() const
{
    // Old: one bit per mechanic, at mechanic - 1. The core's mask is the same shape one position up, so shift it down;
    // the bot builds its own masks as 1 << (MECHANIC_X - 1) (TargetValue.cpp:214-217).
    return Core ? uint32(Core->GetAllEffectsMechanicMask() >> 1) : 0;
}

int32 SpellEntry::GetDuration() const
{
    return Core ? Core->GetDuration() : 0;
}

int32 SpellEntry::CalculateDuration(WorldObject const* caster) const
{
    return Core ? Core->CalcDuration(caster) : 0;
}

uint32 SpellEntry::GetCastTime(WorldObject* /*caster*/, Spell* spell) const
{
    // The core's cast time takes the spell (haste, cast-time mods live there) and not the caster.
    return Core ? Core->CalcCastTime(spell) : 0;
}

uint32 SpellEntry::GetRecoveryTime() const
{
    return Core ? Core->GetRecoveryTime() : 0;
}

SpellCastResult SpellEntry::GetErrorAtShapeshiftedCast(uint32 form) const
{
    return Core ? Core->CheckShapeshift(form) : SPELL_FAILED_ERROR;
}
