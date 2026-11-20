#include "host/TbHostTaxi.h"

#include "Creature.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "TaxiPackets.h"
#include "TaxiPathGraph.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "playerbot/policy/travel/TaxiChoicePolicy.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace tbhost
{
namespace taxi
{
namespace
{

struct FlightWatchState
{
    bool wasInFlight = false;
    uint32_t destNode = 0;
    uint32_t startMap = 0;
};

std::unordered_map<ObjectGuid, FlightWatchState>& WatchStates()
{
    static std::unordered_map<ObjectGuid, FlightWatchState> states;
    return states;
}

std::string ToLowerAscii(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool HasTaxiMaskBit(TaxiMask const& mask, uint32_t nodeId)
{
    if (!nodeId)
        return false;
    uint32_t const field = (nodeId - 1) / (sizeof(TaxiMask::value_type) * 8);
    uint32_t const bit = (nodeId - 1) % (sizeof(TaxiMask::value_type) * 8);
    if (field >= mask.size())
        return false;
    return (mask[field] & (TaxiMask::value_type(1) << bit)) != 0;
}

} // namespace

Creature* NearestFlightMaster(Player const* bot, float range)
{
    if (!bot || !bot->FindMap() || !bot->IsInWorld())
        return nullptr;

    std::vector<Creature*> creatures;
    bot->GetCreatureListWithEntryInGrid(creatures, 0, range);

    Creature* best = nullptr;
    float bestDist = range + 1.0f;
    for (Creature* c : creatures)
    {
        if (!c || !c->IsAlive() || !c->HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER))
            continue;
        if (c->IsHostileTo(bot) || c->GetReactionTo(bot) < REP_NEUTRAL)
            continue;
        float const d = bot->GetDistance(c);
        if (d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

uint32_t NodeOfFlightMaster(Player const* bot, Creature const* npc)
{
    if (!bot || !npc)
        return 0;
    return sObjectMgr->GetNearestTaxiNode(npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), npc->GetMapId(), bot->GetTeam());
}

bool Learn(Player* bot, Creature* npc, uint32_t* outNodeId)
{
    if (outNodeId)
        *outNodeId = 0;
    if (!bot || !npc || !bot->GetSession())
        return false;

    Creature* interactNpc = bot->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_FLIGHTMASTER, UNIT_NPC_FLAG_2_NONE);
    if (!interactNpc)
    {
        TC_LOG_INFO("tortoisebots", "[W5] taxi-learn-refused bot={} npc={} reason=cannot-interact",
            bot->GetName(), npc->GetEntry());
        return false;
    }

    uint32_t const nodeId = NodeOfFlightMaster(bot, interactNpc);
    if (outNodeId)
        *outNodeId = nodeId;
    if (!nodeId)
    {
        TC_LOG_INFO("tortoisebots", "[W5] taxi-learn-refused bot={} npc={} reason=no-node",
            bot->GetName(), interactNpc->GetEntry());
        return false;
    }

    bool const knownBefore = bot->m_taxi.IsTaximaskNodeKnown(nodeId);
    WorldPacket raw(CMSG_TAXI_QUERY_AVAILABLE_NODES, 16);
    WorldPackets::Taxi::TaxiQueryAvailableNodes pkt(std::move(raw));
    pkt.Unit = interactNpc->GetGUID();
    bot->GetSession()->HandleTaxiQueryAvailableNodesOpcode(pkt);

    bool const knownAfter = bot->m_taxi.IsTaximaskNodeKnown(nodeId);
    TC_LOG_INFO("tortoisebots", "[W5] taxi-learn bot={} npc={} node={} before={} after={} total_known={} map={}",
        bot->GetName(), interactNpc->GetEntry(), nodeId, knownBefore ? 1 : 0, knownAfter ? 1 : 0,
        KnownNodeCount(bot), bot->GetMapId());
    return knownAfter;
}

std::vector<uint32_t> KnownNodes(Player const* bot)
{
    std::vector<uint32_t> out;
    if (!bot)
        return out;
    for (TaxiNodesEntry const* node : sTaxiNodesStore)
    {
        if (node && bot->m_taxi.IsTaximaskNodeKnown(node->ID))
            out.push_back(node->ID);
    }
    return out;
}

uint32_t KnownNodeCount(Player const* bot)
{
    if (!bot)
        return 0;
    uint32_t count = 0;
    for (TaxiNodesEntry const* node : sTaxiNodesStore)
    {
        if (node && bot->m_taxi.IsTaximaskNodeKnown(node->ID))
            ++count;
    }
    return count;
}

uint32_t ResolveNodeArg(Player const* bot, std::string const& arg)
{
    if (arg.empty())
        return 0;

    bool allDigits = true;
    for (char c : arg)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            allDigits = false;
            break;
        }
    }
    if (allDigits)
    {
        uint32_t const id = static_cast<uint32_t>(std::strtoul(arg.c_str(), nullptr, 10));
        return sTaxiNodesStore.LookupEntry(id) ? id : 0;
    }

    std::string const needle = ToLowerAscii(arg);
    uint32_t firstMatch = 0;
    for (TaxiNodesEntry const* node : sTaxiNodesStore)
    {
        if (!node || !ai::IsClassicContinentMap(node->ContinentID))
            continue;
        char const* rawName = node->Name[LOCALE_enUS];
        if (!rawName || !*rawName)
            continue;
        std::string const lower = ToLowerAscii(rawName);
        if (lower.find(needle) == std::string::npos)
            continue;
        if (bot && bot->m_taxi.IsTaximaskNodeKnown(node->ID))
            return node->ID;
        if (!firstMatch)
            firstMatch = node->ID;
    }
    return firstMatch;
}

TaxiQuote Quote(Player* bot, Creature* npc, uint32_t toNode)
{
    TaxiQuote q;
    q.toNode = toNode;
    if (!bot || !npc || !toNode)
    {
        q.refuseCode = "bad-args";
        return q;
    }

    q.fromNode = NodeOfFlightMaster(bot, npc);
    TaxiNodesEntry const* fromEntry = sTaxiNodesStore.LookupEntry(q.fromNode);
    TaxiNodesEntry const* toEntry = sTaxiNodesStore.LookupEntry(toNode);
    if (!fromEntry || !toEntry)
    {
        q.refuseCode = "no-node";
        return q;
    }

    bool const fromKnown = bot->isTaxiCheater() || bot->m_taxi.IsTaximaskNodeKnown(q.fromNode);
    bool const toKnown = bot->isTaxiCheater() || bot->m_taxi.IsTaximaskNodeKnown(toNode);

    if (fromKnown && toKnown)
        TaxiPathGraph::GetCompleteNodeRoute(fromEntry, toEntry, bot, q.nodes);

    uint64_t totalCost = 0;
    if (q.nodes.size() >= 2)
    {
        for (size_t i = 1; i < q.nodes.size(); ++i)
        {
            uint32_t path = 0;
            uint32_t cost = 0;
            sObjectMgr->GetTaxiPath(q.nodes[i - 1], q.nodes[i], path, cost);
            if (!path)
            {
                q.nodes.clear();
                break;
            }
            if (i == 1)
                q.pathId = path;
            totalCost += cost;
        }
        float const discount = bot->GetReputationPriceDiscount(npc);
        totalCost = static_cast<uint64_t>(std::ceil(totalCost * discount));
        totalCost = static_cast<uint64_t>(bot->ApplyClassicLegacyPriceMod(static_cast<uint32_t>(totalCost), 4));
    }
    q.costCopper = static_cast<uint32_t>(totalCost);

    bool const busy = bot->IsInCombat() || bot->HasUnitState(UNIT_STATE_STUNNED) || bot->HasUnitState(UNIT_STATE_ROOT);
    ai::TaxiChoiceDecision const decision = ai::DecideTaxiChoice(
        fromKnown, toKnown, q.nodes.size(), totalCost, bot->GetMoney(), bot->IsInCombat(), busy);
    if (decision.action == ai::TaxiChoiceResult::Walk)
    {
        q.refuseCode = decision.refuseCode;
        TC_LOG_INFO("tortoisebots", "[W5] fly-refused bot={} code={} from={} to={} fare={} money={} map={}",
            bot->GetName(), q.refuseCode, q.fromNode, q.toNode, q.costCopper, bot->GetMoney(), bot->GetMapId());
        return q;
    }

    q.ok = true;
    return q;
}

bool Fly(Player* bot, Creature* npc, uint32_t toNode, std::string* failCode)
{
    if (failCode)
        failCode->clear();
    if (!bot || !npc || !bot->GetSession())
    {
        if (failCode)
            *failCode = "bad-args";
        return false;
    }

    Creature* interactNpc = bot->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_FLIGHTMASTER, UNIT_NPC_FLAG_2_NONE);
    if (!interactNpc)
    {
        if (failCode)
            *failCode = "cannot-interact";
        return false;
    }

    TaxiQuote const q = Quote(bot, interactNpc, toNode);
    if (!q.ok)
    {
        if (failCode)
            *failCode = q.refuseCode;
        return false;
    }

    bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

    uint64_t const moneyBefore = bot->GetMoney();
    WorldPacket raw(CMSG_ACTIVATE_TAXI, 28);
    WorldPackets::Taxi::ActivateTaxi pkt(std::move(raw));
    pkt.Vendor = interactNpc->GetGUID();
    pkt.Node = toNode;
    bot->GetSession()->HandleActivateTaxiOpcode(pkt);

    if (!bot->IsInFlight())
    {
        if (failCode)
            *failCode = "activate-failed";
        TC_LOG_INFO("tortoisebots", "[W5] fly-refused bot={} code=activate-failed from={} to={} fare={} money={} map={}",
            bot->GetName(), q.fromNode, toNode, q.costCopper, bot->GetMoney(), bot->GetMapId());
        return false;
    }

    FlightWatchState& ws = WatchStates()[bot->GetGUID()];
    ws.wasInFlight = true;
    ws.destNode = toNode;
    ws.startMap = bot->GetMapId();

    TC_LOG_INFO("tortoisebots", "[W5] fly-start bot={} from={} to={} fare={} money_before={} money_after={} map={}",
        bot->GetName(), q.fromNode, toNode, q.costCopper, moneyBefore, bot->GetMoney(), bot->GetMapId());
    return true;
}

bool InFlight(Player const* bot)
{
    return bot && bot->IsInFlight();
}

void TickLandingWatch(Player* bot)
{
    if (!bot || !bot->FindMap())
        return;

    auto it = WatchStates().find(bot->GetGUID());
    if (it == WatchStates().end())
    {
        if (bot->IsInFlight())
        {
            FlightWatchState& ws = WatchStates()[bot->GetGUID()];
            ws.wasInFlight = true;
            ws.destNode = bot->m_taxi.GetTaxiDestination();
            ws.startMap = bot->GetMapId();
        }
        return;
    }

    if (it->second.wasInFlight && !bot->IsInFlight())
    {
        uint32_t const destNode = it->second.destNode;
        it->second.wasInFlight = false;
        it->second.destNode = 0;
        TC_LOG_INFO("tortoisebots", "[W5] fly-landed bot={} dest={} money={} known={} map={}",
            bot->GetName(), destNode, bot->GetMoney(), KnownNodeCount(bot), bot->GetMapId());
    }
}

std::vector<FlightLinkInfo> BuildRuntimeFlightLinks(bool forAlliance)
{
    std::vector<ai::PureFlightNode> nodes;
    for (TaxiNodesEntry const* n : sTaxiNodesStore)
    {
        if (!n || !ai::IsClassicContinentMap(n->ContinentID))
            continue;
        bool const alliance = HasTaxiMaskBit(sAllianceTaxiNodesMask, n->ID);
        bool const horde = HasTaxiMaskBit(sHordeTaxiNodesMask, n->ID);
        if (!alliance && !horde)
            continue;
        nodes.push_back({ n->ID, n->ContinentID, n->Pos.X, n->Pos.Y, n->Pos.Z, alliance, horde });
    }

    std::vector<ai::PureFlightEdge> edges;
    for (TaxiPathEntry const* p : sTaxiPathStore)
    {
        if (!p || !p->ID)
            continue;
        edges.push_back({ p->FromTaxiNode, p->ToTaxiNode, p->ID, p->Cost });
    }

    std::vector<ai::PureFlightLink> const pureLinks = ai::BuildClassicFlightLinks(nodes, edges, forAlliance);
    std::vector<FlightLinkInfo> out;
    out.reserve(pureLinks.size());
    for (ai::PureFlightLink const& l : pureLinks)
        out.push_back({ l.fromNode, l.toNode, l.pathId, l.costCopper, l.distance });
    return out;
}

} // namespace taxi
} // namespace tbhost
