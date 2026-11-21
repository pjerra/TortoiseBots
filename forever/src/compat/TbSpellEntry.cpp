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
// World::SetInitialWorldSettings (World.cpp:1485-1505) and no `.reload` handler calls them again (scripts/Commands/cs_reload.cpp
// reloads spell_required/spell_area/spell_proc and the other side tables only). So a mirror is only as fresh as the core's
// object, which never changes once built; PORT-PLAN.md carries the same rule for the pin.
#include "TbSpellEntry.h"
#include "Log.h"
#include "TbBotMgr.h"
#include "DB2Stores.h"
#include "ObjectMgr.h"
#include "Trainer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <functional>
#include <set>
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

// R5 (slice 2): the rank chains. ONE table answers every rank question the legacy bot code asks (the mirror's Rank text, "is higher rank of", "same chain",
// first/next/previous rank, `.tb ranks`, `.tb train`). It holds only spells that belong to a chain proved by the data; a spell with no chain has no rank
// (Rank text empty, rank 0: the engine treats it as a single spell), and nothing is ever chained by name, family or level (R4's by-name derivation chained
// a retail-era Renew 425268 above the classic ranks and was removed).
// Sources (every accepted link of every source goes into ONE graph, then the chains are materialised from it; R5 round 2):
//  (a) the core's own chain, SpellMgr::LoadSpellRanks (SpellMgr.cpp:837-905: SkillLineAbility rows with a SupercedesSpell, both spells present at
//      DIFFICULTY_NONE), read through SpellInfo::ChainEntry (SpellMgr.h:518-525: prev/next/first/last/rank);
//  (b) the same client table read here for the links the core did not turn into a chain entry: SkillLineAbilityEntry::SupercedesSpell (DB2Structure.h:3724);
//  (c) the trainers' required-ability links (trainer_spell.ReqAbility, Trainer.h:59), accepted as rank links only with same-line evidence: see TrainerLinks.
// A component of the graph is a chain when every spell has at most one successor and one predecessor and there is no cycle; a link that agrees with the
// others extends the chain whichever source it comes from. Any other component (a fork, a join, a cycle, an order that two sources disagree on) is
// QUARANTINED whole: its spells have no rank, the component is logged once and counted. Every link endpoint is therefore in a chain or in a quarantined component.
// Built once with the index (published before it), immutable afterwards.
struct TrainerLinkStats
{
    uint32 seen = 0, accepted = 0;
    uint32 notClassTrainer = 0, missingSpell = 0, noFamilyFlags = 0, notSameClassLine = 0, familyDiffers = 0, flagsDiffer = 0, shapeDiffers = 0, levelNotLower = 0;
};
struct ChainTable
{
    std::unordered_map<uint32, tbcompat::SpellChainNode> nodes;
    std::vector<tbcompat::SpellChainList> chains;  // every chain as ids in rank order, ordered by its first id
    std::vector<std::vector<uint32>> quarantined;  // components that are no chain: spell ids, ordered
    std::vector<std::vector<uint32>> lost;         // chains the loose (R5 round 1) trainer rule made that the same-line rule does not
    struct Rejected { uint32 required, taught; char const* why; };
    std::vector<Rejected> rejected;                // trainer links kept out of the chains, with the first failing test
    TrainerLinkStats trainer;
    uint32 coreLinks = 0, skillLinks = 0, trainerLinks = 0, quarantinedSpells = 0, disagree = 0;
    std::string summary;
};
std::atomic<ChainTable const*> g_chains{nullptr};

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

// The class skill lines of a class and the spells each holds (R5 round 2). How the core marks a class skill line: SkillLineEntry::CategoryID ==
// SKILL_CATEGORY_CLASS (SharedDefines.h:6385, DB2Structure.h:3702) and a SkillRaceClassInfo row of the line whose ClassMask names the class
// (DB2Structure.h:3748-3757, read through DB2Manager::GetSkillRaceClassInfo, DB2Stores.h:555-556); its abilities are the SkillLineAbility rows
// (DB2Manager::GetSkillLineAbilitiesBySkill) whose ClassMask is 0 or names the class (DB2Structure.h:3723). cs_learn.cpp's `.learn my spells` and the core's
// trainer class mask (Trainer::GetClassMask) are built on the same rows.
std::vector<std::pair<uint32, std::set<uint32>>> ClassSkillLines(uint32 classId)
{
    std::vector<std::pair<uint32, std::set<uint32>>> lines;
    if (classId < 1 || classId > 32)
        return lines;
    uint32 const classBit = 1u << (classId - 1);
    for (SkillLineEntry const* line : sSkillLineStore)
    {
        if (!line || line->CategoryID != SKILL_CATEGORY_CLASS)
            continue;
        bool classLine = false;
        for (SkillRaceClassInfoEntry const* info : sDB2Manager.GetSkillRaceClassInfo(line->ID))
            if (info->ClassMask && (uint32(info->ClassMask) & classBit))
                classLine = true;
        if (!classLine)
            continue;
        std::set<uint32> spells;
        if (std::vector<SkillLineAbilityEntry const*> const* abilities = sDB2Manager.GetSkillLineAbilitiesBySkill(line->ID))
            for (SkillLineAbilityEntry const* row : *abilities)
                if (row->Spell > 0 && (!row->ClassMask || (uint32(row->ClassMask) & classBit)))
                    spells.insert(uint32(row->Spell));
        lines.emplace_back(line->ID, std::move(spells));
    }
    return lines;
}

// The trainer links, with the same-line evidence (R5 round 2). `strict` false is the first R5 round's rule (mechanics only, any trainer), kept only to list
// what the tighter rule costs. Tests in this order, the first failing one is counted:
//  1. the row comes from a class trainer of ONE class (Trainer::Type::None and a one-class Trainer::GetClassMask);
//  2. both spells exist at DIFFICULTY_NONE;
//  3. the spell has family flags (a spell with none never chains by this source);
//  4. BOTH spells have a SkillLineAbility row in the SAME class skill line of THAT class (ClassSkillLines);
//  5. same SpellFamilyName; 6. same 128-bit SpellFamilyFlags; 7. same effect shape (per effect: effect type, aura, both implicit targets);
//  8. the required spell has the lower spell level. The icon is not compared (Heal changes icon between rank 2 and 3).
struct TrainerLinkResult
{
    std::vector<tbcompat::SpellChainLink> links;
    TrainerLinkStats stats;
    std::vector<ChainTable::Rejected> rejected;
};
TrainerLinkResult TrainerLinks(bool strict)
{
    TrainerLinkResult result;
    std::map<uint32, std::vector<std::pair<uint32, std::set<uint32>>>> classLines;   // class id -> lines
    std::set<std::tuple<uint32, uint32, uint32>> seen;
    for (auto const& [trainerId, trainer] : sObjectMgr->GetTrainers())
    {
        uint32 const mask = trainer.GetClassMask();
        bool const classTrainer = trainer.GetType() == Trainer::Type::None && mask && (mask & (mask - 1)) == 0;
        uint32 classId = 0;
        if (classTrainer)
            while (!((mask >> classId) & 1u))
                ++classId;
        ++classId;
        for (Trainer::Spell const& spell : trainer.GetSpells())
            for (uint32 required : spell.ReqAbility)
            {
                if (!required || !seen.insert({ classTrainer ? mask : 0u, required, spell.SpellId }).second)
                    continue;
                TrainerLinkStats& st = result.stats;
                ++st.seen;
                auto reject = [&](uint32& counter, char const* why)
                {
                    ++counter;
                    result.rejected.push_back({ required, spell.SpellId, why });
                };
                if (strict && !classTrainer)
                {
                    reject(st.notClassTrainer, "not a class trainer");
                    continue;
                }
                SpellInfo const* const lower = sSpellMgr->GetSpellInfo(required, DIFFICULTY_NONE);
                SpellInfo const* const higher = sSpellMgr->GetSpellInfo(spell.SpellId, DIFFICULTY_NONE);
                if (!lower || !higher)
                {
                    ++st.missingSpell;
                    continue;
                }
                bool const flagsZero = !lower->SpellFamilyFlags[0] && !lower->SpellFamilyFlags[1] && !lower->SpellFamilyFlags[2] && !lower->SpellFamilyFlags[3];
                if (strict && flagsZero)
                {
                    reject(st.noFamilyFlags, "no family flags");
                    continue;
                }
                if (strict)
                {
                    auto found = classLines.find(classId);
                    if (found == classLines.end())
                        found = classLines.emplace(classId, ClassSkillLines(classId)).first;
                    bool sameLine = false;
                    for (auto const& [lineId, spells] : found->second)
                        if (spells.count(required) && spells.count(spell.SpellId))
                            sameLine = true;
                    if (!sameLine)
                    {
                        reject(st.notSameClassLine, "not in one class skill line");
                        continue;
                    }
                }
                if (lower->SpellFamilyName == 0 || lower->SpellFamilyName != higher->SpellFamilyName)
                {
                    reject(st.familyDiffers, "family differs");
                    continue;
                }
                if (lower->SpellFamilyFlags[0] != higher->SpellFamilyFlags[0] || lower->SpellFamilyFlags[1] != higher->SpellFamilyFlags[1] ||
                    lower->SpellFamilyFlags[2] != higher->SpellFamilyFlags[2] || lower->SpellFamilyFlags[3] != higher->SpellFamilyFlags[3])
                {
                    reject(st.flagsDiffer, "family flags differ");
                    continue;
                }
                bool sameShape = lower->GetEffects().size() == higher->GetEffects().size();
                for (size_t i = 0; sameShape && i < lower->GetEffects().size(); ++i)
                {
                    SpellEffectInfo const& a = lower->GetEffects()[i];
                    SpellEffectInfo const& b = higher->GetEffects()[i];
                    sameShape = a.Effect == b.Effect && a.ApplyAuraName == b.ApplyAuraName && a.TargetA.GetTarget() == b.TargetA.GetTarget() &&
                        a.TargetB.GetTarget() == b.TargetB.GetTarget();
                }
                if (!sameShape)
                {
                    reject(st.shapeDiffers, "effect shape differs");
                    continue;
                }
                if (lower->SpellLevel >= higher->SpellLevel)
                {
                    reject(st.levelNotLower, "required spell level not lower");
                    continue;
                }
                ++st.accepted;
                result.links.push_back({ required, spell.SpellId, tbcompat::SpellChainSource::Trainer });
            }
    }
    return result;
}

std::string SpellNameOf(uint32 id)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
    return info && info->SpellName && (*info->SpellName)[LOCALE_enUS] ? (*info->SpellName)[LOCALE_enUS] : "?";
}

ChainTable const* BuildChainTable()
{
    auto* table = new ChainTable();
    std::vector<tbcompat::SpellChainLink> links;

    // (a) the core's own chains: one link per ChainEntry that has a predecessor.
    sSpellMgr->ForEachSpellInfo([&](SpellInfo const* info)
    {
        if (info && info->Difficulty == DIFFICULTY_NONE && info->ChainEntry && info->ChainEntry->prev)
        {
            links.push_back({ info->ChainEntry->prev->Id, info->Id, tbcompat::SpellChainSource::Core });
            ++table->coreLinks;
        }
    });

    // (b) the SkillLineAbility supersedes links (the core's own source: the same rows again, so the graph merges them with (a)).
    for (SkillLineAbilityEntry const* row : sSkillLineAbilityStore)
    {
        if (!row || row->SupercedesSpell <= 0 || row->Spell <= 0)
            continue;
        if (!sSpellMgr->GetSpellInfo(uint32(row->SupercedesSpell), DIFFICULTY_NONE) || !sSpellMgr->GetSpellInfo(uint32(row->Spell), DIFFICULTY_NONE))
            continue;
        links.push_back({ uint32(row->SupercedesSpell), uint32(row->Spell), tbcompat::SpellChainSource::SkillLine });
        ++table->skillLinks;
    }

    // (c) the trainers' required-ability links with same-line evidence. The client's SkillLineAbility table has NO supercedes link for the 1.12 priest spells
    // (`.tb ranks priest`: Smite 585/591/598 were singles), while the world database's trainer rows name the previous rank (2052 requires 2050, 591 requires
    // 585; Trainer::Spell::ReqAbility, Trainer.h:59, enforced by Trainer::GetSpellState, Trainer.cpp:431-433). A prerequisite is not always a rank
    // (Whirlwind requires Berserker Stance), hence TrainerLinks' tests.
    TrainerLinkResult strict = TrainerLinks(true);
    table->trainer = strict.stats;
    table->rejected = strict.rejected;
    table->trainerLinks = strict.stats.accepted;
    std::vector<tbcompat::SpellChainLink> strictLinks = links;
    strictLinks.insert(strictLinks.end(), strict.links.begin(), strict.links.end());
    tbcompat::SpellChainResult built = tbcompat::MaterializeSpellChains(strictLinks);

    // What the same-line rule costs: the chains the first round's loose trainer rule made that this graph does not have.
    {
        TrainerLinkResult loose = TrainerLinks(false);
        std::vector<tbcompat::SpellChainLink> looseLinks = links;
        looseLinks.insert(looseLinks.end(), loose.links.begin(), loose.links.end());
        tbcompat::SpellChainResult const looseBuilt = tbcompat::MaterializeSpellChains(looseLinks);
        std::set<std::vector<uint32>> have;
        for (tbcompat::SpellChainList const& chain : built.chains)
            have.insert(chain.ids);
        for (tbcompat::SpellChainList const& chain : looseBuilt.chains)
            if (!have.count(chain.ids))
                table->lost.push_back(chain.ids);
    }

    for (tbcompat::SpellChainList& chain : built.chains)
    {
        for (uint32 i = 0; i < chain.ids.size(); ++i)
        {
            tbcompat::SpellChainNode node;
            node.rank = i + 1;
            node.first = chain.ids.front();
            node.last = chain.ids.back();
            node.prev = i ? chain.ids[i - 1] : 0;
            node.next = i + 1 < chain.ids.size() ? chain.ids[i + 1] : 0;
            // The core's own rank number for the same spell: a difference (a chain the other sources extended below its first rank) is counted; the position is kept.
            if (SpellInfo const* cur = sSpellMgr->GetSpellInfo(chain.ids[i], DIFFICULTY_NONE))
                if (cur->ChainEntry && cur->ChainEntry->rank != node.rank)
                    ++table->disagree;
            table->nodes[chain.ids[i]] = node;
        }
    }
    table->chains = std::move(built.chains);
    table->quarantined = std::move(built.quarantined);
    for (std::vector<uint32> const& component : table->quarantined)
    {
        table->quarantinedSpells += uint32(component.size());
        std::string ids;
        for (uint32 id : component)
            ids += std::to_string(id) + ":" + SpellNameOf(id) + " ";
        TC_LOG_INFO("server.loading", "TortoiseBots: spell chains: quarantined component (no rank for any of its spells): {}", ids);
    }

    TrainerLinkStats const& t = table->trainer;
    std::ostringstream out;
    out << table->chains.size() << " chains, " << table->nodes.size() << " ranked spells; links " << table->coreLinks << " core + " << table->skillLinks
        << " skill-line + " << table->trainerLinks << " trainer; trainer links seen " << t.seen << ", accepted " << t.accepted << ", rejected: not a class trainer "
        << t.notClassTrainer << ", no family flags " << t.noFamilyFlags << ", not in one class skill line " << t.notSameClassLine << ", family differs " << t.familyDiffers
        << ", flags differ " << t.flagsDiffer << ", effect shape differs " << t.shapeDiffers << ", level not lower " << t.levelNotLower << " (spell missing " << t.missingSpell
        << "); quarantined components " << table->quarantined.size() << " (" << table->quarantinedSpells << " spells); rank differences " << table->disagree
        << "; chains lost by the same-line rule " << table->lost.size();
    table->summary = out.str();
    TC_LOG_INFO("server.loading", "TortoiseBots: spell chains: {}", table->summary);
    return table;
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
    sSpellMgr->ForEachSpellInfo([&](SpellInfo const* info)
    {
        if (!info)
            return;
        maxId = std::max(maxId, info->Id);
        if (info->Difficulty == DIFFICULTY_NONE)
        {
            ids.push_back(info->Id);
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

    g_chains.store(BuildChainTable(), std::memory_order_release);

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

SpellChainNode const* SpellChainNodeOf(uint32 spellId)
{
    ChainTable const* const table = g_chains.load(std::memory_order_acquire);
    if (!table)
        return nullptr;
    auto const found = table->nodes.find(spellId);
    return found == table->nodes.end() ? nullptr : &found->second;
}

uint32 SpellRankOf(uint32 spellId)
{
    SpellChainNode const* const node = SpellChainNodeOf(spellId);
    return node ? node->rank : 0;
}

uint32 SpellFirstRank(uint32 spellId)
{
    SpellChainNode const* const node = SpellChainNodeOf(spellId);
    return node ? node->first : 0;
}

uint32 SpellNextRank(uint32 spellId)
{
    SpellChainNode const* const node = SpellChainNodeOf(spellId);
    return node ? node->next : 0;
}

uint32 SpellPrevRank(uint32 spellId)
{
    SpellChainNode const* const node = SpellChainNodeOf(spellId);
    return node ? node->prev : 0;
}

uint32 SpellLastRank(uint32 spellId)
{
    SpellChainNode const* const node = SpellChainNodeOf(spellId);
    return node ? node->last : 0;
}

bool SpellsShareChain(uint32 a, uint32 b)
{
    SpellChainNode const* const na = SpellChainNodeOf(a);
    SpellChainNode const* const nb = SpellChainNodeOf(b);
    return na && nb && na->first == nb->first;
}

bool SpellIsHigherRank(uint32 spellId, uint32 otherId)
{
    SpellChainNode const* const na = SpellChainNodeOf(spellId);
    SpellChainNode const* const nb = SpellChainNodeOf(otherId);
    return na && nb && na->first == nb->first && na->rank > nb->rank;
}

void ForEachSpellChain(std::function<void(SpellChainList const&)> const& fn)
{
    ChainTable const* const table = g_chains.load(std::memory_order_acquire);
    if (!table)
        return;
    for (SpellChainList const& chain : table->chains)
        fn(chain);
}

void ForEachRejectedTrainerLink(std::function<void(uint32, uint32, char const*)> const& fn)
{
    ChainTable const* const table = g_chains.load(std::memory_order_acquire);
    if (!table)
        return;
    for (ChainTable::Rejected const& r : table->rejected)
        fn(r.required, r.taught, r.why);
}

void ForEachQuarantinedComponent(std::function<void(std::vector<uint32> const&)> const& fn)
{
    ChainTable const* const table = g_chains.load(std::memory_order_acquire);
    if (table)
        for (std::vector<uint32> const& component : table->quarantined)
            fn(component);
}

void ForEachLostChain(std::function<void(std::vector<uint32> const&)> const& fn)
{
    ChainTable const* const table = g_chains.load(std::memory_order_acquire);
    if (table)
        for (std::vector<uint32> const& chain : table->lost)
            fn(chain);
}

std::string SpellChainSummary()
{
    ChainTable const* const table = g_chains.load(std::memory_order_acquire);
    return table ? table->summary : std::string("no chain table");
}

std::set<uint32> ClassLineSpells(uint32 classId)
{
    std::set<uint32> spells;
    for (auto const& [lineId, lineSpells] : ClassSkillLines(classId))
        spells.insert(lineSpells.begin(), lineSpells.end());
    return spells;
}

SpellChainResult MaterializeSpellChains(std::vector<SpellChainLink> const& links)
{
    SpellChainResult result;
    std::map<uint32, std::set<uint32>> succ, pred;
    std::map<std::pair<uint32, uint32>, uint8> edgeSources;
    std::set<uint32> nodes;
    for (SpellChainLink const& link : links)
    {
        succ[link.lower].insert(link.higher);
        pred[link.higher].insert(link.lower);
        edgeSources[{ link.lower, link.higher }] |= link.source;
        nodes.insert(link.lower);
        nodes.insert(link.higher);
    }
    // Components (undirected reachability over the links).
    std::map<uint32, uint32> component;   // node -> representative
    std::vector<std::vector<uint32>> members;
    for (uint32 start : nodes)
    {
        if (component.count(start))
            continue;
        uint32 const index = uint32(members.size());
        members.emplace_back();
        std::vector<uint32> stack{ start };
        component[start] = index;
        while (!stack.empty())
        {
            uint32 const id = stack.back();
            stack.pop_back();
            members[index].push_back(id);
            for (auto const* map : { &succ, &pred })
            {
                auto const found = map->find(id);
                if (found == map->end())
                    continue;
                for (uint32 other : found->second)
                    if (component.emplace(other, index).second)
                        stack.push_back(other);
            }
        }
    }
    for (std::vector<uint32>& ids : members)
    {
        std::sort(ids.begin(), ids.end());
        bool straight = true;
        std::vector<uint32> starts;
        for (uint32 id : ids)
        {
            auto const s = succ.find(id);
            auto const p = pred.find(id);
            if ((s != succ.end() && s->second.size() > 1) || (p != pred.end() && p->second.size() > 1))
                straight = false;
            if (p == pred.end())
                starts.push_back(id);
        }
        std::vector<uint32> order;
        if (straight && starts.size() == 1)
        {
            for (uint32 cur = starts[0]; order.size() <= ids.size();)
            {
                order.push_back(cur);
                auto const s = succ.find(cur);
                if (s == succ.end())
                    break;
                cur = *s->second.begin();
            }
        }
        if (!straight || starts.size() != 1 || order.size() != ids.size())
        {
            result.quarantined.push_back(std::move(ids));   // a fork, a join, a cycle (no start), or a path that does not cover the component
            continue;
        }
        uint8 sources = 0;
        for (size_t i = 0; i + 1 < order.size(); ++i)
            sources |= edgeSources[{ order[i], order[i + 1] }];
        result.chains.push_back({ std::move(order), sources });
    }
    std::sort(result.chains.begin(), result.chains.end(), [](SpellChainList const& a, SpellChainList const& b) { return a.ids.front() < b.ids.front(); });
    std::sort(result.quarantined.begin(), result.quarantined.end());
    return result;
}

std::vector<std::string> SpellChainSelfTest()
{
    using L = SpellChainLink;
    std::vector<std::string> lines;
    struct Case
    {
        char const* name;
        std::vector<L> links;
        std::vector<std::vector<uint32>> chains;
        std::vector<std::vector<uint32>> quarantined;
    };
    uint8 const C = SpellChainSource::Core, K = SpellChainSource::SkillLine, T = SpellChainSource::Trainer;
    std::vector<Case> cases = {
        { "straight chain from two sources (core 1>2, trainer 2>3)", { { 1, 2, C }, { 2, 3, T } }, { { 1, 2, 3 } }, {} },
        { "trainer link extends a partial core chain below its first rank (skill 10>11, core 11>12)", { { 11, 12, C }, { 10, 11, K } }, { { 10, 11, 12 } }, {} },
        { "same link from two sources (core and trainer 21>22)", { { 21, 22, C }, { 21, 22, T } }, { { 21, 22 } }, {} },
        { "fork (31>32, 31>33)", { { 31, 32, C }, { 31, 33, T } }, {}, { { 31, 32, 33 } } },
        { "join (41>43, 42>43)", { { 41, 43, C }, { 42, 43, T } }, {}, { { 41, 42, 43 } } },
        { "cycle of three (51>52>53>51)", { { 51, 52, T }, { 52, 53, T }, { 53, 51, T } }, {}, { { 51, 52, 53 } } },
        { "order conflict between two sources (61>62 core, 62>61 trainer)", { { 61, 62, C }, { 62, 61, T } }, {}, { { 61, 62 } } },
        { "self link (71>71)", { { 71, 71, T } }, {}, { { 71 } } },
        { "a good chain next to a cycle: only the cycle is quarantined", { { 81, 82, C }, { 82, 83, T }, { 91, 92, T }, { 92, 91, T } }, { { 81, 82, 83 } }, { { 91, 92 } } },
        { "a conflict inside a longer chain quarantines the whole component (101>102>103, 102>104)", { { 101, 102, C }, { 102, 103, C }, { 102, 104, T } }, {}, { { 101, 102, 103, 104 } } },
    };
    for (Case const& c : cases)
    {
        SpellChainResult const r = MaterializeSpellChains(c.links);
        std::vector<std::vector<uint32>> chains;
        for (SpellChainList const& chain : r.chains)
            chains.push_back(chain.ids);
        // every link endpoint is in a chain or in a quarantined component
        std::set<uint32> covered;
        for (auto const& ids : chains)
            covered.insert(ids.begin(), ids.end());
        for (auto const& ids : r.quarantined)
            covered.insert(ids.begin(), ids.end());
        bool allCovered = true;
        for (L const& link : c.links)
            allCovered = allCovered && covered.count(link.lower) && covered.count(link.higher);
        bool const pass = chains == c.chains && r.quarantined == c.quarantined && allCovered;
        lines.push_back(std::string(pass ? "PASS " : "FAIL ") + c.name + " -> " + std::to_string(chains.size()) + " chain(s), " + std::to_string(r.quarantined.size()) + " quarantined");
    }
    // The sources of a merged chain are the union of its links' sources.
    {
        SpellChainResult const r = MaterializeSpellChains({ { 1, 2, C }, { 2, 3, T } });
        lines.push_back(std::string(r.chains.size() == 1 && r.chains[0].sources == (C | T) ? "PASS " : "FAIL ") + "merged chain carries both source bits");
    }
    return lines;
}

uint32 SpellChainCount()
{
    ChainTable const* const table = g_chains.load(std::memory_order_acquire);
    return table ? uint32(table->chains.size()) : 0;
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
    // Slice 11 (U3): `hotfixes.spell_name` renames Curse of Agony 980 and 1014 "Bane of Agony" (forever/docs/SLICE11-NOTES.md, hotfix_data rows valid), and the bot
    // finds a spell by the Classic name its action carries ("curse of agony"). The mirror gives those two ids the name the legacy code asks for, only while the core's
    // own name is the renamed one (so a data fix on the core's side makes this a no-op). Other locales keep what the core says.
    if (info->Id == 980 || info->Id == 1014)
        if (entry->SpellName[LOCALE_enUS] == "Bane of Agony")
            entry->SpellName[LOCALE_enUS] = "Curse of Agony";

    // The rank text is the spell's place in a proved chain (ChainTable above); a spell with no chain keeps an empty text.
    if (uint32 const rank = tbcompat::SpellRankOf(info->Id))
        entry->Rank.fill("Rank " + std::to_string(rank));

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
    // The core tests a 32-bit set (SpellInfo.cpp:2041-2044); an index beyond the spell's effects is no positive effect (R5 item 11).
    return Core && uint32(effIndex) < Core->GetEffects().size() && Core->IsPositiveEffect(uint8(effIndex));
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
