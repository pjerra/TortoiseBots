// Host half of bot talent spending: maps Forever Talent.db2 IDs to TraitNode/TraitNodeEntry
// via TraitDefinition SpellID, validates each step with TraitMgr::ValidateConfigForCommit,
// and commits via Player::UpdateTraitConfig so the 1.60 client tab and #122 spec choice stay in sync.
#include "host/TbTalents.h"

#include "host/TbTalentsPure.h"
#include "slice/SliceSpecPolicy.h"
#include "TbBotMgr.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "DB2Stores.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "TraitMgr.h"
#include "TraitPacketsCommon.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "PlayerbotAIStorage.h"
#endif

namespace TortoiseBots
{
namespace talents
{
namespace
{

std::atomic<bool> g_enabled{ true };
std::atomic<bool> g_censusSpec{ true };

struct ResolvedNode
{
    int32 traitNodeId = 0;
    int32 traitNodeEntryId = 0;
    int32 maxRanks = 0;
};

struct ClassTalentCache
{
    std::unordered_map<int32, int> nodeTab; // TraitNodeID -> 0..2
    std::unordered_map<uint32, ResolvedNode> talentToNode; // Talent.db2 ID -> TraitNode/Entry
    std::unordered_map<int32, uint32> nodeToTalent; // TraitNodeID -> Talent.db2 ID
    std::unordered_map<uint32, ResolvedNode> nodeById; // TraitNodeID -> node, for build steps that name a node
    std::vector<FallbackNode> fallbackNodes;
};

ClassTalentCache const& CacheForClass(uint8 classId)
{
    static std::mutex mutex;
    static std::map<uint8, ClassTalentCache> cache;

    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(classId);
    if (it != cache.end())
        return it->second;

    ClassTalentCache built;
    if (ChrSpecializationEntry const* spec = sDB2Manager.GetChrSpecializationByIndex(classId, 0))
    {
        WorldPackets::Traits::TraitConfig dummy;
        dummy.Type = TraitConfigType::Combat;
        dummy.ChrSpecializationID = spec->ID;

        std::vector<TraitMgr::NodeEntryInfo> const entries = TraitMgr::GetNodeEntriesForConfig(dummy);
        std::vector<TortoiseBots::slice::spec::TraitNodeLayout> layout;
        layout.reserve(entries.size());

        std::unordered_map<int32, ResolvedNode> spellToNode;
        for (TraitMgr::NodeEntryInfo const& e : entries)
        {
            if (TraitNodeEntry const* node = sTraitNodeStore.LookupEntry(e.TraitNodeID))
                layout.push_back({ e.TraitNodeID, node->PosX, node->PosY });

            if (TraitDefinitionEntry const* def = sTraitDefinitionStore.LookupEntry(e.TraitDefinitionID))
            {
                ResolvedNode rn{ e.TraitNodeID, e.TraitNodeEntryID, e.MaxRanks };
                if (def->SpellID)
                    spellToNode.emplace(def->SpellID, rn);
                if (def->VisibleSpellID)
                    spellToNode.emplace(def->VisibleSpellID, rn);
                if (def->OverridesSpellID)
                    spellToNode.emplace(def->OverridesSpellID, rn);
            }
        }

        built.nodeTab = TortoiseBots::slice::spec::BuildNodeTabMap(layout);

        for (TraitMgr::NodeEntryInfo const& e : entries)
            built.nodeById.emplace(uint32(e.TraitNodeID), ResolvedNode{ e.TraitNodeID, e.TraitNodeEntryID, e.MaxRanks });

        std::unordered_set<int32> seenFallback;
        for (TraitMgr::NodeEntryInfo const& e : entries)
        {
            if (!seenFallback.insert(e.TraitNodeID).second)
                continue;
            auto tit = built.nodeTab.find(e.TraitNodeID);
            if (tit == built.nodeTab.end())
                continue;
            if (TraitNodeEntry const* node = sTraitNodeStore.LookupEntry(e.TraitNodeID))
                built.fallbackNodes.push_back({ e.TraitNodeID, e.TraitNodeEntryID, e.MaxRanks, node->PosX, node->PosY, tit->second });
        }

        for (TalentEntry const* tal : sTalentStore)
        {
            if (!tal || !TalentMatchesClass(tal->ClassID, classId))
                continue;
            ResolvedNode found;
            bool ok = false;
            if (tal->SpellID)
            {
                auto sit = spellToNode.find(int32(tal->SpellID));
                if (sit != spellToNode.end())
                {
                    found = sit->second;
                    ok = true;
                }
            }
            if (!ok)
            {
                for (uint32 rankSpell : tal->SpellRank)
                {
                    if (!rankSpell)
                        continue;
                    auto sit = spellToNode.find(int32(rankSpell));
                    if (sit != spellToNode.end())
                    {
                        found = sit->second;
                        ok = true;
                        break;
                    }
                }
            }
            if (ok)
            {
                built.talentToNode[tal->ID] = found;
                built.nodeToTalent.emplace(found.traitNodeId, tal->ID);
            }
        }
    }

    return cache.emplace(classId, std::move(built)).first->second;
}

ResolvedNode const* FindStepNode(ClassTalentCache const& cache, uint32 stepId)
{
    auto tit = cache.talentToNode.find(stepId);
    if (tit != cache.talentToNode.end())
        return &tit->second;
    if (IsTraitNodeStepId(stepId))
    {
        auto nit = cache.nodeById.find(stepId);
        if (nit != cache.nodeById.end())
            return &nit->second;
    }
    return nullptr;
}

int8 ForcedSpecTab(ObjectGuid guid)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    return PlayerbotAIStorage::Instance().GetPlayerForcedSpecTab(guid);
#else
    (void)guid;
    return -1;
#endif
}

struct SyncReport
{
    uint32 applied = 0;
    int32 spent = 0;
    int32 free = 0;
    int tab = -1;
    int tabCounts[3] = { 0, 0, 0 };
};

SyncReport ApplyTalents(Player* bot, char const* why, bool ignoreConfigFlag)
{
    SyncReport report;
    if (!bot || !BotMgr::Instance().IsBot(bot->GetGUID()))
        return report;
    if (!ignoreConfigFlag && !Enabled())
        return report;
    if (bot->GetLevel() < 10)
        return report;

    int32 const activeConfigId = *bot->m_activePlayerData->ActiveCombatTraitConfigID;
    UF::TraitConfig const* currentConfig = activeConfigId ? bot->GetTraitConfig(activeConfigId) : nullptr;
    if (!currentConfig)
        return report;

    ClassTalentCache const& cache = CacheForClass(bot->GetClass());
    WorldPackets::Traits::TraitConfig working(*currentConfig);

    auto countTabs = [&](WorldPackets::Traits::TraitConfig const& cfg)
    {
        report.tabCounts[0] = report.tabCounts[1] = report.tabCounts[2] = 0;
        report.spent = 0;
        for (WorldPackets::Traits::TraitEntry const& e : cfg.Entries)
        {
            if (e.Rank <= 0)
                continue;
            report.spent += e.Rank;
            auto tit = cache.nodeTab.find(e.TraitNodeID);
            if (tit != cache.nodeTab.end() && tit->second >= 0 && tit->second < 3)
                report.tabCounts[tit->second] += e.Rank;
        }
    };

    countTabs(working);
    int32 const expectedTotal = bot->GetLevel() >= 10 ? int32(bot->GetLevel()) - 9 : 0;
    report.free = std::max<int32>(0, expectedTotal - report.spent);

    int8 const forced = ForcedSpecTab(bot->GetGUID());
    bool const censusSpec = g_censusSpec.load(std::memory_order_relaxed) && BotMgr::Instance().IsPoolBot(bot->GetGUID());
    report.tab = PickSpecTabForBot(
        bot->GetClass(),
        bot->GetGUID().GetCounter(),
        forced,
        report.tabCounts[0],
        report.tabCounts[1],
        report.tabCounts[2],
        censusSpec);

    if (report.free <= 0)
        return report;

    std::unordered_map<uint32, uint8> currentRanks;
    for (WorldPackets::Traits::TraitEntry const& e : working.Entries)
    {
        if (e.Rank <= 0)
            continue;
        uint8 const rank = uint8(std::min<int32>(255, e.Rank));
        auto nit = cache.nodeToTalent.find(e.TraitNodeID);
        if (nit != cache.nodeToTalent.end())
            currentRanks[nit->second] = rank;
        currentRanks[uint32(e.TraitNodeID)] = rank;
    }

    std::vector<uint32> const plan = PlanNextSteps(bot->GetClass(), report.tab, currentRanks);
    uint32 firstUnmapped = 0;
    int32 lastFailResult = 0;
    uint32 lastFailTalent = 0;

    auto tryAddPoint = [&](int32 traitNodeId, int32 traitNodeEntryId, int32 maxRanks, uint32 talentIdForDiag, bool& outOfPoints) -> bool
    {
        WorldPackets::Traits::TraitConfig candidate = working;
        auto eit = std::find_if(candidate.Entries.begin(), candidate.Entries.end(), [&](WorldPackets::Traits::TraitEntry const& e)
        {
            return e.TraitNodeID == traitNodeId && e.TraitNodeEntryID == traitNodeEntryId;
        });
        if (eit != candidate.Entries.end())
        {
            if (eit->Rank + eit->GrantedRanks >= maxRanks)
                return false;
            ++eit->Rank;
        }
        else
        {
            WorldPackets::Traits::TraitEntry& added = candidate.Entries.emplace_back();
            added.TraitNodeID = traitNodeId;
            added.TraitNodeEntryID = traitNodeEntryId;
            added.Rank = 1;
        }

        TraitMgr::LearnResult const res = TraitMgr::ValidateConfigForCommit(candidate, bot);
        if (res != TraitMgr::LearnResult::Ok)
        {
            if (res == TraitMgr::LearnResult::NotEnoughTalentsInPrimaryTree && report.spent >= expectedTotal)
                outOfPoints = true;
            if (talentIdForDiag)
            {
                lastFailResult = int32(res);
                lastFailTalent = talentIdForDiag;
            }
            return false;
        }

        working = std::move(candidate);
        ++report.applied;
        ++report.spent;
        return true;
    };

    for (uint32 talentId : plan)
    {
        if (report.spent >= expectedTotal)
            break;
        ResolvedNode const* found = FindStepNode(cache, talentId);
        if (!found)
        {
            if (!firstUnmapped)
                firstUnmapped = talentId;
            continue;
        }
        ResolvedNode const& rn = *found;
        bool outOfPoints = false;
        tryAddPoint(rn.traitNodeId, rn.traitNodeEntryId, rn.maxRanks, talentId, outOfPoints);
        if (outOfPoints)
            break;
    }

    if (report.spent < expectedTotal)
    {
        std::vector<FallbackNode> const ordered = OrderFallbackNodes(cache.fallbackNodes, report.tab);
        auto runFallbackTabPass = [&](bool mainTabOnly)
        {
            while (report.spent < expectedTotal)
            {
                uint32 passApplied = 0;
                for (FallbackNode const& fn : ordered)
                {
                    if ((fn.tab == report.tab) != mainTabOnly)
                        continue;
                    while (report.spent < expectedTotal)
                    {
                        bool outOfPoints = false;
                        if (!tryAddPoint(fn.traitNodeId, fn.traitNodeEntryId, fn.maxRanks, 0, outOfPoints))
                            break;
                        ++passApplied;
                    }
                    if (report.spent >= expectedTotal)
                        break;
                }
                if (passApplied == 0)
                    break;
            }
        };
        runFallbackTabPass(true);
        runFallbackTabPass(false);
    }

    if (report.applied > 0)
    {
        countTabs(working);
        report.free = std::max<int32>(0, expectedTotal - report.spent);
        report.tab = PickSpecTabForBot(
            bot->GetClass(),
            bot->GetGUID().GetCounter(),
            forced,
            report.tabCounts[0],
            report.tabCounts[1],
            report.tabCounts[2],
            censusSpec);
        bot->UpdateTraitConfig(std::move(working), 0, false);
        TC_LOG_INFO("tortoisebots", "talents: {} spent {} point(s) in {} ({}/{}/{}, total {}/{}) on {}",
            bot->GetName(), report.applied, SpecName(bot->GetClass(), report.tab),
            report.tabCounts[0], report.tabCounts[1], report.tabCounts[2],
            report.spent, expectedTotal, why ? why : "sync");
    }
    else if (report.free > 0)
    {
        TC_LOG_WARN("tortoisebots", "talents: {} ({}) spent 0/{} free (spec {}, unmapped={}, failTalent={}, failRes={})",
            bot->GetName(), why ? why : "sync", report.free, SpecName(bot->GetClass(), report.tab),
            firstUnmapped, lastFailTalent, lastFailResult);
    }

    return report;
}

} // namespace

void LoadConfig()
{
    g_enabled.store(sConfigMgr->GetBoolDefault("TortoiseBots.AutoTalentsOnLevel", true), std::memory_order_relaxed);
    g_censusSpec.store(sConfigMgr->GetBoolDefault("TortoiseBots.Pool.CensusSpec", true), std::memory_order_relaxed);
}

void LogStartupSelfCheck()
{
    static uint8 const kClasses[] = { 1, 2, 3, 4, 5, 7, 8, 9, 11 };
    std::ostringstream ss;
    for (size_t i = 0; i < sizeof(kClasses) / sizeof(kClasses[0]); ++i)
    {
        uint8 const cls = kClasses[i];
        ClassTalentCache const& cache = CacheForClass(cls);
        size_t mapped = 0;
        size_t total = 0;
        for (TalentStep const& step : kBuildSteps)
        {
            if (step.classId != cls)
                continue;
            total += step.ranks;
            if (FindStepNode(cache, step.talentId))
                mapped += step.ranks;
        }
        if (i)
            ss << ' ';
        ss << "cls" << unsigned(cls) << '=' << mapped << '/' << total;
    }
    TC_LOG_INFO("server.loading", "TortoiseBots: talents self-check: {}", ss.str());
}

bool Enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

bool PoolCensusSpecEnabled()
{
    return g_censusSpec.load(std::memory_order_relaxed);
}

uint32_t Sync(Player* bot, char const* why)
{
    return ApplyTalents(bot, why, false).applied;
}

bool HandleTalentsCommand(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    if (!BotMgr::Instance().Enabled())
    {
        handler->PSendSysMessage("TortoiseBots: module is disabled.");
        handler->SetSentErrorMessage(true);
        return false;
    }

    std::istringstream iss{ std::string(args) };
    std::string name;
    if (!(iss >> name))
    {
        handler->PSendSysMessage("TortoiseBots: usage: .tb talents <bot>");
        handler->SetSentErrorMessage(true);
        return false;
    }

    if (!normalizePlayerName(name))
    {
        handler->PSendSysMessage("TortoiseBots: invalid character name.");
        handler->SetSentErrorMessage(true);
        return false;
    }

    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(name);
    Player* bot = guid.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(guid);
    if (!bot || !BotMgr::Instance().IsBot(guid))
    {
        handler->PSendSysMessage("TortoiseBots: bot '%s' is not online.", name.c_str());
        handler->SetSentErrorMessage(true);
        return false;
    }

    SyncReport const r = ApplyTalents(bot, "command", true);
    if (r.tab < 0)
    {
        handler->PSendSysMessage("TortoiseBots: %s (level %u): spec=none spent=%d (%d/%d/%d) free=%d applied=%u.",
            bot->GetName().c_str(), uint32(bot->GetLevel()),
            r.spent, r.tabCounts[0], r.tabCounts[1], r.tabCounts[2],
            r.free, r.applied);
    }
    else
    {
        handler->PSendSysMessage("TortoiseBots: %s (level %u): spec=%s (tab %d) spent=%d (%d/%d/%d) free=%d applied=%u.",
            bot->GetName().c_str(), uint32(bot->GetLevel()),
            SpecName(bot->GetClass(), r.tab), r.tab,
            r.spent, r.tabCounts[0], r.tabCounts[1], r.tabCounts[2],
            r.free, r.applied);
    }
    return true;
}

} // namespace talents
} // namespace TortoiseBots
