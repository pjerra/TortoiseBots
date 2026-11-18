// The spell mirror (TbSpellEntry.h): one SpellEntry per DIFFICULTY_NONE spell id the core has, built on first use.
//
// Storage is proportional to the number of spells, not to the highest id: InitSpellTable (from the module's
// WorldScript::OnWorldInitialized, or from the first lookup of the id bound after that hook) collects the DIFFICULTY_NONE
// ids into a sorted, immutable array and allocates one atomic mirror slot per id. The first reader of an id
// binary-searches the array, builds the mirror and publishes it with a compare-exchange; a racing builder frees its own
// copy. The index is built once, by one thread at a time (the store scan is inside the builder's lock), so a lookup that
// arrives during the build waits for it and every later reader binary-searches immutable data without a lock.
//
// Nothing here touches the spell store before the readiness flag is set, and only the module's WorldScript::
// OnWorldInitialized sets it (after World::SetInitialWorldSettings has filled the store and returned): until then the
// bound lookup answers 0 and every spell lookup nullptr, each logged once. A lazy build after the flag uses the same
// code as the hook's. Nothing is allocated while the module is disabled.
//
// The spell slot holds only the DIFFICULTY_NONE record: SpellMgr's map is keyed on (Id, Difficulty) (SpellMgr.cpp:53-67)
// and ForEachSpellInfo walks every variant, so publishing whichever variant reached the slot first would let a raid
// creature's spell variant answer for the id (after `.tb spellscan`, for example). Bot code written for 1.12 knows no
// difficulties, so another difficulty resolves to the same id's NONE record, or to nullptr when there is none.
//
// The core has no spell reload at the pinned commit: LoadSpellInfoStore and every pass that edits the SpellInfo objects
// (LoadSpellInfoCorrections, LoadClassicSpellInfo, LoadSpellInfoCustomAttributes) are called once from
// World::SetInitialWorldSettings (World.cpp:1471-1491) and no `.reload` handler calls them again (scripts/Commands/cs_reload.cpp
// reloads spell_required/spell_area/spell_proc and the other side tables only). So a mirror is only as fresh as the core's
// object, which never changes once built; PORT-PLAN.md carries the same rule for the pin.
#include "TbSpellEntry.h"
#include "Log.h"
#include "TbBotMgr.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
// The immutable id index: sorted DIFFICULTY_NONE spell ids and one atomic mirror slot per id. Published once, read by
// every map thread without a lock; never freed (process lifetime, like the mirrors).
struct SpellIndex
{
    uint32 count = 0;
    uint32* ids = nullptr;                       // sorted, no duplicates
    std::atomic<SpellEntry*>* slots = nullptr;   // slots[i] is the mirror of ids[i]
};

std::atomic<SpellIndex*> g_index{nullptr};
std::atomic<uint32> g_bound{0};                  // one past the highest spell id the core has
std::mutex g_initMutex;                          // one index builder at a time
// Set only by the module's WorldScript::OnWorldInitialized, after World::SetInitialWorldSettings has returned (no spell
// reload exists at the pin). Nothing reads SpellMgr or publishes a mirror while it is false: the store is being filled
// then, and an index built from it would keep the partial result for the process lifetime.
std::atomic<bool> g_storeReady{false};
std::atomic<bool> g_notReadyBoundLogged{false};
std::atomic<bool> g_notReadyLookupLogged{false};
std::atomic<uint32> g_nonNoneOnly{0};
std::atomic<bool> g_nonNoneLogged{false};
std::atomic<bool> g_disabledLogged{false};

// R4 (slice 2): the rank number of a spell the core's own rank chain does not cover. The chain (SpellMgr::LoadSpellRanks) is built from the
// SkillLineAbility rows' SupercedesSpell, and this server's data has none for the 1.12 class spells (Smite 585/591, Lesser Heal
// 2050/2052/2053, Shadow Word: Pain 589/594, ...: `.tb spell 591` shows an empty rank). The engine picks the rank of a cast by reading that text
// (SpellIdValue.cpp: the highest "Rank N" of the spells the bot knows); with every rank empty it kept the LAST id of its loop, the LOWEST rank, so a
// level 10 priest cast rank 1 of everything (seen live in R4: Smite 585 with 591 known). The rank derived here is the position of the spell among
// the spells of the same name and spell family that have no chain, ordered by spell level then id: the same order the ranks have in the data.
// Built once with the index (published before it), immutable afterwards; a spell that is alone in its group keeps no rank text (as on Turtle).
std::atomic<std::unordered_map<uint32, uint32> const*> g_derivedRanks{nullptr};

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

bool MirrorEnabled()
{
    if (TortoiseBots::BotMgr::Instance().Enabled())
        return true;
    if (!g_disabledLogged.exchange(true, std::memory_order_relaxed))
        TC_LOG_INFO("server.loading", "TortoiseBots: spell mirrors are off (TortoiseBots.Enable = 0); spell lookups return null.");
    return false;
}

void NoteNonNoneOnly(SpellInfo const* info)
{
    g_nonNoneOnly.fetch_add(1, std::memory_order_relaxed);
    if (!g_nonNoneLogged.exchange(true, std::memory_order_relaxed))
        TC_LOG_INFO("server.loading", "TortoiseBots: spell {} exists only with difficulty {}, no DIFFICULTY_NONE record; its mirror is null (counted from here on).",
            info->Id, uint32(info->Difficulty));
}

bool SpellLookupReady()
{
    if (g_storeReady.load(std::memory_order_acquire))
        return true;
    if (!g_notReadyLookupLogged.exchange(true, std::memory_order_relaxed))
        TC_LOG_INFO("server.loading", "TortoiseBots: a spell lookup ran before the spell store was ready (module WorldScript::OnWorldInitialized); returning null until it is (logged once).");
    return false;
}

uint32 IndexPosition(SpellIndex const* index, uint32 id)
{
    uint32 low = 0, high = index->count;
    while (low < high)
    {
        uint32 const mid = low + (high - low) / 2;
        if (index->ids[mid] < id)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}
}

namespace tbcompat
{
void MarkSpellStoreReady()
{
    g_storeReady.store(true, std::memory_order_release);
}

void InitSpellTable()
{
    // Only the hook that runs after World::SetInitialWorldSettings (Main.cpp:372) may start this: before it the store is
    // still being filled, and an index built from it would keep its partial contents for the process lifetime.
    if (!g_storeReady.load(std::memory_order_acquire))
        return;

    if (g_index.load(std::memory_order_acquire))
        return;
    if (!MirrorEnabled())
        return;

    // The scan is inside the lock: the hook and a lazy caller (the id bound, a first spell lookup) cannot walk the whole
    // store at the same time, and a caller that arrives during a build waits here and then finds the published index.
    std::lock_guard lock(g_initMutex);
    if (g_index.load(std::memory_order_relaxed))
        return;

    std::vector<uint32> ids;
    uint32 maxId = 0;
    // (name + family, spell level, id) of every spell without a core rank chain that could be a rank of a class spell.
    std::vector<std::tuple<std::string, uint32, uint32>> unchained;
    sSpellMgr->ForEachSpellInfo([&](SpellInfo const* info)
    {
        if (!info)
            return;
        maxId = std::max(maxId, info->Id);
        if (info->Difficulty == DIFFICULTY_NONE)
        {
            ids.push_back(info->Id);
            if (!info->ChainEntry && info->SpellFamilyName != 0 && info->SpellLevel > 0 && info->SpellName)
                if (char const* name = (*info->SpellName)[LOCALE_enUS])
                    if (*name)
                        unchained.emplace_back(std::string(name) + '\x1f' + std::to_string(info->SpellFamilyName), uint32(info->SpellLevel), info->Id);
        }
    });

    // The store is loaded by the time the flag is set, so this pass is never empty on a live server. With no spells at
    // all, build nothing (a zero-sized index would stick) and let the next lookup try again.
    if (ids.empty())
        return;

    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    SpellIndex* const index = new SpellIndex();
    index->count = uint32(ids.size());
    index->ids = new uint32[index->count];
    std::copy(ids.begin(), ids.end(), index->ids);
    index->slots = new std::atomic<SpellEntry*>[index->count]();

    {
        std::sort(unchained.begin(), unchained.end());
        auto* ranks = new std::unordered_map<uint32, uint32>();
        for (size_t begin = 0; begin < unchained.size();)
        {
            size_t end = begin;
            while (end < unchained.size() && std::get<0>(unchained[end]) == std::get<0>(unchained[begin]))
                ++end;
            if (end - begin >= 2)
                for (size_t i = begin; i < end; ++i)
                    (*ranks)[std::get<2>(unchained[i])] = uint32(i - begin + 1);
            begin = end;
        }
        g_derivedRanks.store(ranks, std::memory_order_release);
    }

    g_bound.store(maxId + 1, std::memory_order_release);
    g_index.store(index, std::memory_order_release);
}

uint32 MaxSpellId()
{
    if (!g_storeReady.load(std::memory_order_acquire))
    {
        if (!g_notReadyBoundLogged.exchange(true, std::memory_order_relaxed))
            TC_LOG_INFO("server.loading", "TortoiseBots: the spell id bound was asked for before the spell store was ready (module WorldScript::OnWorldInitialized); returning 0 until it is (logged once).");
        return 0;
    }
    if (!g_index.load(std::memory_order_acquire))
        InitSpellTable();
    return g_bound.load(std::memory_order_acquire);
}

uint64 SpellIndexBytes()
{
    SpellIndex const* const index = g_index.load(std::memory_order_acquire);
    return index ? sizeof(SpellIndex) + uint64(index->count) * sizeof(uint32) : 0;
}

uint64 SpellSlotBytes()
{
    SpellIndex const* const index = g_index.load(std::memory_order_acquire);
    return index ? uint64(index->count) * sizeof(std::atomic<SpellEntry*>) : 0;
}

uint32 NonNoneOnlyLookups()
{
    return g_nonNoneOnly.load(std::memory_order_relaxed);
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

    // Totem: the core's -1 is its "no totem" sentinel (SpellTotems row absent -> the array's 0; a row with -1 means
    // none), while the old uint32 column's "none" is 0. Keeping the bit pattern (0xFFFFFFFF) would make the bot's
    // `Totem[0] > 0` test (CastCustomSpellAction.cpp:142) think a totem is required.
    for (uint32 i = 0; i < MAX_SPELL_TOTEMS; ++i)
        entry->Totem[i] = info->Totem[i] < 0 ? 0 : uint32(info->Totem[i]);
    // Same for the reagent count: it is an int16 count in the core (DB2Structure.h:4169) and a uint32 count in the old
    // column; a negative value is not a count, and the old field's "no reagent" is 0.
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
    else if (!info->ChainEntry)
    {
        // No chain in this server's data: the derived position (g_derivedRanks above).
        if (auto const* derived = g_derivedRanks.load(std::memory_order_acquire))
        {
            auto const found = derived->find(info->Id);
            if (found != derived->end())
                entry->Rank.fill("Rank " + std::to_string(found->second));
        }
    }

    // Range: the old field is the SpellRange id the bot code looks up in the store; the core keeps the row itself, whose
    // ID is that id. The member default (1) stays when the core has no row, exactly the old struct's default.
    if (info->RangeEntry)
        entry->rangeIndex = info->RangeEntry->ID;

    // Power cost: the first row is the spell's power type; for mana the core charges the vanilla table's flat + percent
    // figure when it has the row (SpellInfo.cpp:4386-4395), so the same row is what the old fields held. Without it the
    // first row's flat cost and percent are the core's own base numbers.
    //
    // powerType keeps the signed value's uint32 image, which is what the old core's uint32 column held: POWER_HEALTH is
    // -2 in both cores (SharedDefines.h:294 there, :137 on Turtle), so the old field read 0xFFFFFFFE and the bot's
    // health-cost branch (PlayerbotAISpells.cpp:52-59) tests that. manaCost is the same cast of the raw column. The
    // percent is a float in the DB2 and an unsigned integer percentage in the old column, so it truncates (the old
    // loader's GetUInt32 parsed the same column) and a negative percentage cannot be held there.
    SpellPowerEntry const* const power = info->PowerCosts[0];
    if (power)
    {
        entry->powerType = uint32(power->PowerType);
        entry->manaCost = uint32(power->ManaCost);
        entry->ManaCostPercentage = power->PowerCostPct > 0.0f ? uint32(power->PowerCostPct) : 0;

        if (power->PowerType == POWER_MANA && ClassicSpellValue::AppliesToSpell(info->Id))
        {
            if (ClassicSpellValue::Cost const* vanilla = sSpellMgr->GetClassicSpellCost(info->Id))
            {
                entry->manaCost = uint32(vanilla->ManaCost);
                entry->ManaCostPercentage = vanilla->ManaCostPct > 0 ? uint32(vanilla->ManaCostPct) : 0;
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
        // The core's ChainTargets is a signed count (Spell.cpp:1838-1851 treats it as the chain's target count); the old
        // column is an unsigned count of chain jumps. A negative core value is not a count, and 0 is the old column's
        // "not a chain"; the classic data has no negative chain target.
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
            // The old field is int32 and the vanilla dice are signed: a negative count is a range
            // (Object.cpp:4435-4454 rolls irand(randomPoints, baseDice)), so no clamp.
            entry->EffectDieSides[i] = vanilla->DieSides;
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
        // The old column's die count. ForeverCore's classic_spell_value has no base-dice column (and neither does the
        // DB2 effect), so the value is not available from the core; 1 is the 1.12 value for 7,420 of the 7,611 matched
        // effects (SHIM-R3-NOTES.md, Known differences). The two readers are CalculateSimpleValue (a debug aura apply)
        // and the craft log lines.
        entry->EffectBaseDice[i] = 1;
    }

    return entry;
}

SpellEntry const* SpellProto(SpellInfo const* info)
{
    if (!info || !MirrorEnabled() || !SpellLookupReady())
        return nullptr;

    // A slot holds only the DIFFICULTY_NONE record. Another variant answers for the same id through the NONE sibling
    // (1.12 bot code knows no difficulties); without one there is no old-core record to hand out.
    if (info->Difficulty != DIFFICULTY_NONE)
    {
        SpellInfo const* const none = sSpellMgr->GetSpellInfo(info->Id, DIFFICULTY_NONE);
        if (!none || none->Difficulty != DIFFICULTY_NONE)
        {
            NoteNonNoneOnly(info);
            return nullptr;
        }
        return SpellProto(none);
    }

    uint32 const id = info->Id;

    SpellIndex* index = g_index.load(std::memory_order_acquire);
    if (!index)
    {
        InitSpellTable();
        index = g_index.load(std::memory_order_acquire);
    }

    // Fast path: one binary search over immutable data, then one acquire load of the slot.
    if (index)
    {
        uint32 const position = IndexPosition(index, id);
        if (position < index->count && index->ids[position] == id)
        {
            std::atomic<SpellEntry*>& slot = index->slots[position];
            if (SpellEntry* entry = slot.load(std::memory_order_acquire))
                return entry;

            SpellEntry* expected = nullptr;
            SpellEntry* built = BuildSpellEntry(info);
            if (slot.compare_exchange_strong(expected, built, std::memory_order_acq_rel))
                return built;
            delete built;                 // another thread published first; both built from the same immutable object
            return expected;
        }
    }

    // The index holds every DIFFICULTY_NONE id of the store it was built from, every SpellInfo the bot can hold comes
    // from that store, and the core has no reload, so a miss is an id with no NONE record: nullptr, no second table.
    return nullptr;
}

SpellEntry const* SpellProto(uint32 id)
{
    // The readiness check comes before the store lookup: before the hook the store's map is still being filled.
    if (!MirrorEnabled() || !SpellLookupReady())
        return nullptr;
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
