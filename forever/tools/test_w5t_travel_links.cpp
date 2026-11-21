// W5T: the W4 travel graph uses W5 flight and transport links (forever/docs/REVIEW-W5T-NOTES.md).
// Step 1 (fails before the wiring): the static link gate must let flight and transport links count as routes.
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iai forever/tools/test_w5t_travel_links.cpp -o /tmp/test_w5t_travel_links && /tmp/test_w5t_travel_links
#include "ai/playerbot/policy/travel/TaxiChoicePolicy.h"
#include "ai/playerbot/policy/travel/TravelGraphPolicy.h"

#include <cassert>
#include <iostream>

int main()
{
    int passed = 0;

    // Flight links are routes on ForeverCore (the runtime flight links become travel links).
    assert(ai::travel_graph::LinkUsableOnForever(ai::travel_graph::kFlightPath));
    ++passed;

    // Transport links (boats, zeppelins, tram) are routes on ForeverCore (board via tbhost::ride).
    assert(ai::travel_graph::LinkUsableOnForever(ai::travel_graph::kTransport));
    ++passed;

    // Walks and area triggers stay usable.
    assert(ai::travel_graph::LinkUsableOnForever(ai::travel_graph::kWalk));
    assert(ai::travel_graph::LinkUsableOnForever(ai::travel_graph::kAreaTrigger));
    ++passed;

    // Teleports stay refused (decision D4: the host's logged rescue is the only travel teleport).
    assert(!ai::travel_graph::LinkUsableOnForever(ai::travel_graph::kTeleportSpell));
    assert(!ai::travel_graph::LinkUsableOnForever(ai::travel_graph::kStaticPortal));
    ++passed;

    // One stored flight link: unknown from or to node refuses, even with money and faction.
    {
        ai::FlightLinkAccess ok;
        ok.fromKnown = ok.toKnown = true;
        ok.fareCopper = 50;
        ok.botMoneyCopper = 1000;
        ok.fromFactionOk = ok.toFactionOk = true;
        ok.fromClassic = ok.toClassic = true;
        assert(ai::DecideFlightLinkUsable(ok).action == ai::TaxiChoiceResult::Fly);

        ai::FlightLinkAccess unknownFrom = ok;
        unknownFrom.fromKnown = false;
        assert(ai::DecideFlightLinkUsable(unknownFrom).action == ai::TaxiChoiceResult::Walk);
        assert(ai::DecideFlightLinkUsable(unknownFrom).refuseCode == "unknown-node");

        ai::FlightLinkAccess unknownTo = ok;
        unknownTo.toKnown = false;
        assert(ai::DecideFlightLinkUsable(unknownTo).refuseCode == "unknown-node");
        ++passed;
    }

    // Faction and classic maps refuse before the fare is even asked.
    {
        ai::FlightLinkAccess base;
        base.fromKnown = base.toKnown = true;
        base.fareCopper = 0;
        base.botMoneyCopper = 0;
        base.fromFactionOk = base.toFactionOk = true;
        base.fromClassic = base.toClassic = true;

        ai::FlightLinkAccess wrongFaction = base;
        wrongFaction.toFactionOk = false;
        assert(ai::DecideFlightLinkUsable(wrongFaction).action == ai::TaxiChoiceResult::Walk);
        assert(ai::DecideFlightLinkUsable(wrongFaction).refuseCode == "faction");

        ai::FlightLinkAccess offMap = base;
        offMap.fromClassic = false;
        assert(ai::DecideFlightLinkUsable(offMap).refuseCode == "no-route");
        ++passed;
    }

    // A fare above the bot's copper refuses with money; an exact fare still flies.
    {
        ai::FlightLinkAccess base;
        base.fromKnown = base.toKnown = true;
        base.fromFactionOk = base.toFactionOk = true;
        base.fromClassic = base.toClassic = true;

        ai::FlightLinkAccess broke = base;
        broke.fareCopper = 500;
        broke.botMoneyCopper = 499;
        assert(ai::DecideFlightLinkUsable(broke).action == ai::TaxiChoiceResult::Walk);
        assert(ai::DecideFlightLinkUsable(broke).refuseCode == "money");

        ai::FlightLinkAccess exact = base;
        exact.fareCopper = 500;
        exact.botMoneyCopper = 500;
        assert(ai::DecideFlightLinkUsable(exact).action == ai::TaxiChoiceResult::Fly);
        assert(ai::DecideFlightLinkUsable(exact).refuseCode.empty());
        ++passed;
    }

    std::cout << "PASS: test_w5t_travel_links (" << passed << " checks, 0 failed)\n";
    return 0;
}
