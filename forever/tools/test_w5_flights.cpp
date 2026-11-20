// Pure unit tests for W5 (taxi choice, runtime flight links, transport dock/board timing).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_w5_flights.cpp -o /tmp/test_w5_flights && /tmp/test_w5_flights

#include "ai/playerbot/policy/travel/TaxiChoicePolicy.h"
#include "ai/playerbot/policy/travel/TransportWaitPolicy.h"

#include <cassert>
#include <iostream>

int main()
{
    int passed = 0;

    // 1. Failing case (required): fare above bot money -> MUST answer Walk with refuseCode == "money".
    {
        ai::TaxiChoiceDecision const d = ai::DecideTaxiChoice(
            /*fromKnown=*/true,
            /*toKnown=*/true,
            /*hopCount=*/2,
            /*fareCopper=*/500,
            /*botMoneyCopper=*/499,
            /*inCombat=*/false,
            /*rootedOrStunned=*/false);
        assert(d.action == ai::TaxiChoiceResult::Walk);
        assert(d.refuseCode == "money");
        ++passed;
    }

    // 2. Unknown endpoint -> MUST answer Walk with "unknown-node".
    {
        ai::TaxiChoiceDecision const d1 = ai::DecideTaxiChoice(false, true, 2, 100, 1000, false, false);
        assert(d1.action == ai::TaxiChoiceResult::Walk && d1.refuseCode == "unknown-node");

        ai::TaxiChoiceDecision const d2 = ai::DecideTaxiChoice(true, false, 2, 100, 1000, false, false);
        assert(d2.action == ai::TaxiChoiceResult::Walk && d2.refuseCode == "unknown-node");
        ++passed;
    }

    // 3. Valid known route with enough copper -> Fly.
    {
        ai::TaxiChoiceDecision const d = ai::DecideTaxiChoice(true, true, 3, 500, 500, false, false);
        assert(d.action == ai::TaxiChoiceResult::Fly);
        assert(d.refuseCode.empty());
        ++passed;
    }

    // 4. Runtime flight link builder filters by classic maps (0, 1) and faction.
    {
        std::vector<ai::PureFlightNode> const nodes = {
            { 2, 0, -8835.0f, 490.0f, 109.0f, true, false },   // Stormwind (Alliance)
            { 6, 0, -4821.0f, -1152.0f, 502.0f, true, false }, // Ironforge (Alliance)
            { 23, 1, 1676.0f, -4313.0f, 61.0f, false, true },  // Orgrimmar (Horde)
            { 999, 530, 0.0f, 0.0f, 0.0f, true, true },        // Non-classic map
        };
        std::vector<ai::PureFlightEdge> const edges = {
            { 2, 6, 10, 50 },
            { 2, 23, 11, 50 },
            { 2, 999, 12, 50 },
        };
        auto const links = ai::BuildClassicFlightLinks(nodes, edges, /*forAlliance=*/true);
        assert(links.size() == 1);
        assert(links[0].fromNode == 2 && links[0].toNode == 6 && links[0].pathId == 10);
        ++passed;
    }

    // 5. TransportWaitPolicy: one cycle minus 1 s keeps waiting; one cycle plus margin gives up.
    {
        ai::TransportWaitInput in;
        in.mapReady = true;
        in.onBoard = false;
        in.transportFound = false;
        in.totalPathTimeMs = 120000;
        in.marginMs = 15000;
        in.waitedMs = 119000;
        assert(ai::DecideTransportWait(in) == ai::TransportWaitStep::Wait);

        in.waitedMs = 135000;
        assert(ai::DecideTransportWait(in) == ai::TransportWaitStep::GiveUp);
        ++passed;
    }

    // 6. TransportWaitPolicy: stopped within reach boards; moving does not; aboard at destination leaves.
    {
        ai::TransportWaitInput in;
        in.mapReady = true;
        in.onBoard = false;
        in.transportFound = true;
        in.transportStopped = false;
        in.distanceToDeck = 5.0f;
        in.maxBoardRange = 15.0f;
        assert(ai::DecideTransportWait(in) == ai::TransportWaitStep::Wait);

        in.transportStopped = true;
        assert(ai::DecideTransportWait(in) == ai::TransportWaitStep::Board);

        in.onBoard = true;
        in.atDestinationDock = false;
        assert(ai::DecideTransportWait(in) == ai::TransportWaitStep::Wait);

        in.atDestinationDock = true;
        assert(ai::DecideTransportWait(in) == ai::TransportWaitStep::Leave);

        // Mid-teleport (FindMap() == nullptr): always Wait.
        in.mapReady = false;
        assert(ai::DecideTransportWait(in) == ai::TransportWaitStep::Wait);
        ++passed;
    }

    // 7. Classic transport route filter: classic maps allowed; non-classic requires allow-list.
    {
        assert(ai::IsAllowedClassicTransportRoute(176495, { 0, 1 }, {}, false));
        assert(!ai::IsAllowedClassicTransportRoute(999999, { 0, 530 }, {}, false));
        assert(ai::IsAllowedClassicTransportRoute(999999, { 0, 530 }, { 999999 }, false));
        ++passed;
    }

    std::cout << "PASS: test_w5_flights (" << passed << " checks, 0 failed)\n";
    return 0;
}
