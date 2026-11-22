#include "../../ai/playerbot/BoostReadinessPolicy.h"
#include "../../ai/playerbot/AvoidAoePolicy.h"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    // Blood Fury / racial boost readiness:
    // Solo orc warrior (no real player master) vs level+1 mob (balance = 95), full HP or 60% HP.
    assert(ai::RacialBoostSituationReady(false, false, 95, 100));
    assert(ai::RacialBoostSituationReady(false, false, 95, 60));
    // Equal-level 1v1 (balance = 100) fires.
    assert(ai::RacialBoostSituationReady(false, false, 100, 100));
    // Trivial lower-level pull at healthy HP holds the 2-min cooldown.
    assert(!ai::RacialBoostSituationReady(false, false, 140, 100));
    // Taking real damage even on a green pull fires.
    assert(ai::RacialBoostSituationReady(false, false, 140, 75));
    // PvP or real-player master always fires.
    assert(ai::RacialBoostSituationReady(true, false, 200, 100));
    assert(ai::RacialBoostSituationReady(false, true, 200, 100));

    // Avoid AoE hazard hold for a stay spot covered by a damaging dynobj circle.
    std::vector<ai::AoeHazardCircle> hazards = { { 2810.0f, -430.0f, 10.0f, 5.0f } };
    assert(ai::HazardsHoldMove(false, false, true, 2810.0f, -430.0f, 10.0f, hazards));
    assert(!ai::HazardsHoldMove(false, false, true, 2820.0f, -430.0f, 10.0f, hazards));

    std::cout << "PASS: test_fx3_reactions\n";
    return 0;
}
