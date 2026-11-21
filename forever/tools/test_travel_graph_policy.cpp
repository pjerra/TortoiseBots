// Standalone test of the W4 review fixes for the travel graph and the creature level (decisions 1, 2 and 4):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_travel_graph_policy.cpp -o /tmp/test_travel_graph_policy && /tmp/test_travel_graph_policy
//
// The graph below is the two-continent one the review asked for: Azeroth (map 0) and Kalimdor (map 1), joined ONLY by a
// zeppelin link (transport) and a flight path. On ForeverCore neither is a route (W5), so a Kalimdor bot must not see Stormwind as
// reachable, and neither link may become a map transfer. The node walk is the production one (CollectReachable is what
// TravelNode::getNodeMap runs); the link rules are the ones TravelLinkUsableCompat and TravelMgr::LoadMapTransfers call.
#include "ai/playerbot/policy/travel/CreatureLevelPolicy.h"
#include "ai/playerbot/policy/travel/TravelGraphPolicy.h"
#include "ai/playerbot/GrindSpotPolicy.h"
#include "ai/playerbot/policy/pull/PullRegenPolicy.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace tg = ai::travel_graph;
namespace tl = ai::travel_level;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
struct Node
{
    std::string name;
    std::uint32_t map;
    bool important;
    std::vector<std::pair<Node*, int>> links;
};

void Link(Node& a, Node& b, int type)
{
    a.links.push_back({ &b, type });
    b.links.push_back({ &a, type });
}

std::vector<Node*> Reach(Node& start, bool (*usable)(std::uint8_t), bool importantOnly = false, bool mapOnly = false,
    std::vector<Node*> const& ignore = {})
{
    return tg::CollectReachable<Node*>(&start, importantOnly, ignore, mapOnly,
        [](Node* n) -> std::vector<std::pair<Node*, int>> const& { return n->links; },
        [](Node* n) { return n->map; }, [](Node* n) { return n->important; }, usable);
}

bool Has(std::vector<Node*> const& v, Node const& n)
{
    for (Node* x : v)
        if (x == &n)
            return true;
    return false;
}

bool UsableAll(std::uint8_t) { return true; } // the legacy (Turtle) rule: every link type is usable
}

int main()
{
    // ---- Decision 1: the two-continent graph --------------------------------------------------------------------------
    Node stormwind{ "Stormwind", 0, true, {} }, goldshire{ "Goldshire", 0, false, {} };
    Node orgrimmar{ "Orgrimmar", 1, true, {} }, crossroads{ "Crossroads", 1, false, {} }, razorHill{ "Razor Hill", 1, false, {} };
    Link(stormwind, goldshire, tg::kWalk);
    Link(orgrimmar, crossroads, tg::kWalk);
    Link(orgrimmar, razorHill, tg::kWalk);
    Link(razorHill, stormwind, tg::kTransport);   // the zeppelin/boat leg
    Link(crossroads, goldshire, tg::kFlightPath); // a taxi leg

    // The legacy rule (every link usable) reaches across the zeppelin: this is the old "reachable" answer for a goal A* refuses.
    std::vector<Node*> legacy = Reach(razorHill, UsableAll);
    CHECK(Has(legacy, stormwind));
    CHECK(legacy.size() == 5);

    // ForeverCore: the Kalimdor bot reaches only Kalimdor.
    std::vector<Node*> fromKalimdor = Reach(razorHill, tg::LinkUsableOnForever);
    CHECK(fromKalimdor.size() == 3);
    CHECK(Has(fromKalimdor, orgrimmar) && Has(fromKalimdor, crossroads) && Has(fromKalimdor, razorHill));
    CHECK(!Has(fromKalimdor, stormwind));
    CHECK(!Has(fromKalimdor, goldshire));

    // And the Azeroth side the other way round.
    std::vector<Node*> fromAzeroth = Reach(stormwind, tg::LinkUsableOnForever);
    CHECK(fromAzeroth.size() == 2);
    CHECK(!Has(fromAzeroth, orgrimmar));

    // The walk keeps its discovery order and its filters exactly: the nodes below are the same set the old loop closed on.
    CHECK(fromKalimdor.front() == &razorHill);
    std::vector<Node*> important = Reach(razorHill, tg::LinkUsableOnForever, true);
    CHECK(important.size() == 1 && important.front() == &orgrimmar);
    std::vector<Node*> ignoring = Reach(razorHill, tg::LinkUsableOnForever, false, false, { &orgrimmar });
    CHECK(ignoring.size() == 1); // orgrimmar is the only way on, and it is ignored
    // An area trigger across the maps is a usable link (the core teleports on the trigger): the continents join and mapOnly cuts it.
    Node dungeonDoor{ "Door", 0, false, {} }, dungeonInside{ "Inside", 34, false, {} };
    Link(dungeonDoor, dungeonInside, tg::kAreaTrigger);
    CHECK(Reach(dungeonDoor, tg::LinkUsableOnForever).size() == 2);
    CHECK(Reach(dungeonDoor, tg::LinkUsableOnForever, false, true).size() == 1);

    // The link rule itself: only walks and area triggers are routes on ForeverCore (D4 and W5).
    CHECK(!tg::LinkUsableOnForever(tg::kNone)); // review medium: an invalid type is no route
    CHECK(!tg::LinkUsableOnForever(7));
    CHECK(!tg::LinkUsableOnForever(255));
    CHECK(!tg::LinkTypeKnown(tg::kNone));
    CHECK(!tg::LinkTypeKnown(7));
    CHECK(tg::LinkTypeKnown(tg::kWalk) && tg::LinkTypeKnown(tg::kStaticPortal));
    CHECK(tg::LinkUsableOnForever(tg::kWalk));
    CHECK(tg::LinkUsableOnForever(tg::kAreaTrigger));
    CHECK(!tg::LinkUsableOnForever(tg::kTransport));
    CHECK(!tg::LinkUsableOnForever(tg::kFlightPath));
    CHECK(!tg::LinkUsableOnForever(tg::kTeleportSpell)); // D4: no teleport outside the logged rescue
    CHECK(!tg::LinkUsableOnForever(tg::kStaticPortal));  // D4

    // A cross-map link of an invalid type gives no route and no map transfer.
    Node inv1{ "InvA", 0, true, {} }, inv2{ "InvB", 1, true, {} };
    Link(inv1, inv2, tg::kNone);
    CHECK(Reach(inv1, tg::LinkUsableOnForever).size() == 1);
    CHECK(!tg::MapTransferWanted(tg::LinkUsableOnForever(tg::kNone), 0, 1));
    CHECK(!tg::MapTransferWanted(tg::LinkUsableOnForever(9), 0, 1));

    // Map transfers (LoadMapTransfers): the zeppelin and the flight path add none, the area trigger does, a same-map link never does.
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> transfers;
    struct Row { std::uint8_t type; std::uint32_t from, to; };
    for (Row r : { Row{ tg::kTransport, 1, 0 }, Row{ tg::kFlightPath, 1, 0 }, Row{ tg::kTeleportSpell, 0, 1 },
        Row{ tg::kStaticPortal, 0, 1 }, Row{ tg::kAreaTrigger, 0, 34 }, Row{ tg::kWalk, 0, 0 } })
        if (tg::MapTransferWanted(tg::LinkUsableOnForever(r.type), r.from, r.to))
            ++transfers[{ r.from, r.to }];
    CHECK(transfers.size() == 1);
    CHECK((transfers[{ 0, 34 }] == 1));
    CHECK((transfers.find({ 1, 0 }) == transfers.end())); // no Kalimdor -> Azeroth shortcut: the cross-continent goal stays unreachable

    // The loader's walk rule (review low): a "walk" between two maps, or through a point on another map, is refused.
    CHECK(tg::WalkLinkMapsConsistent(tg::kWalk, 0, 0));
    CHECK(!tg::WalkLinkMapsConsistent(tg::kWalk, 0, 1));
    CHECK(tg::WalkLinkMapsConsistent(tg::kAreaTrigger, 0, 34)); // a cross-map trigger is not a walk
    CHECK(tg::WalkLinkMapsConsistent(tg::kTransport, 1, 0));
    CHECK(tg::WalkPathPointOnLinkMap(tg::kWalk, 0, 0));
    CHECK(!tg::WalkPathPointOnLinkMap(tg::kWalk, 0, 1));
    CHECK(tg::WalkPathPointOnLinkMap(tg::kTransport, 1, 0));

    // ---- Decision 2: max_creature on a link comes from the classic level ------------------------------------------------
    // A level-60 hostile camp beside a path raises slot [0]; an Alliance-friendly one slot [1]; a Horde-friendly one slot [2]. The old
    // stub answered 0 for every creature, so every link of the generated graph stored 0,0,0.
    std::vector<std::uint8_t> slots = { 0, 0, 0 };
    tg::RecordCreatureLevel(slots, 60, false, false);
    tg::RecordCreatureLevel(slots, 45, true, false);
    tg::RecordCreatureLevel(slots, 52, false, true);
    tg::RecordCreatureLevel(slots, 12, false, false); // a lower one never lowers the slot
    tg::RecordCreatureLevel(slots, 0, false, false);  // an unrated one changes nothing
    CHECK(slots[0] == 60 && slots[1] == 45 && slots[2] == 52);
    std::vector<std::uint8_t> nothing = { 0, 0, 0 };
    tg::RecordCreatureLevel(nothing, 0, false, false);
    CHECK(nothing[0] == 0 && nothing[1] == 0 && nothing[2] == 0);
    // The level feeding it is the classic one; unknown is not rated (no threat), never a giant level.
    CHECK(tl::ThreatLevelOf(tl::FromClassicRow(true, 60)) == 60);
    CHECK(tl::ThreatLevelOf(tl::FromClassicRow(false, 0)) == 0);
    CHECK(tl::ThreatLevelOf(tl::FromClassicRow(true, 0)) == 0);

    // ---- Decision 4: an unknown creature level is never a travel target -------------------------------------------------
    std::int32_t const unknown = tl::FromClassicRow(false, 0);
    CHECK(!tl::IsKnown(unknown));
    CHECK(tl::IsKnown(tl::FromClassicRow(true, 1)));
    CHECK(!tl::IsKnown(tl::FromClassicRow(true, 0))); // a row that says level 0 is corrupt, not level 0
    for (std::uint32_t botLevel = 1; botLevel <= 60; ++botLevel)
        for (bool levelAppropriate : { false, true })
            for (std::uint8_t power : { 0, 100, 250 })
            {
                ai::GrindLevelBand const band = ai::GetGrindLevelBand(botLevel, power, levelAppropriate);
                CHECK(!ai::GrindLevelFits(band, unknown)); // levels 1 and 2 have band minimum 0: reading unknown as 0 fitted
                CHECK(!ai::QuestObjectiveLevelFits(unknown, botLevel, true, false));
            }
    // A known level still fits where it always did.
    CHECK(ai::GrindLevelFits(ai::GetGrindLevelBand(1, 0, true), tl::FromClassicRow(true, 1)));
    CHECK(ai::GrindLevelFits(ai::GetGrindLevelBand(30, 0, true), tl::FromClassicRow(true, 30)));
    CHECK(!ai::GrindLevelFits(ai::GetGrindLevelBand(1, 0, true), tl::FromClassicRow(true, 60)));
    // The boss cap (`level > botLevel + 3` refuses) and the gather skill read refuse it too.
    CHECK(unknown > 60 + 3);

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
