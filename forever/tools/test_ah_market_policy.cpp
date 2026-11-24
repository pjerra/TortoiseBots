// Pure unit tests for the automatic AH market sweep policy (TbHostAhMarketPure.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_ah_market_policy.cpp -o /tmp/test_ah_market_policy && /tmp/test_ah_market_policy

#include "forever/src/host/TbHostAhMarketPure.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main()
{
    int passed = 0;
    using namespace tbhost::ah_market;

    // 1. Failing case first: a bot with nothing sellable is never picked, whatever else is true.
    {
        SweepCandidateInput in;
        in.freeBot = true;
        in.inWorld = true;
        in.alive = true;
        in.openWorldMap = true;
        in.hasSellableStock = false;
        assert(!IsSweepCandidate(in));
        in.hasSellableStock = true;
        assert(IsSweepCandidate(in));
        ++passed;
    }

    // 2. Every disqualifier alone blocks the bot.
    {
        SweepCandidateInput base;
        base.freeBot = true;
        base.inWorld = true;
        base.alive = true;
        base.openWorldMap = true;
        base.hasSellableStock = true;

        struct Case { bool SweepCandidateInput::*field; char const* name; };
        Case cases[] = {
            { &SweepCandidateInput::freeBot, "freeBot" },
            { &SweepCandidateInput::inWorld, "inWorld" },
            { &SweepCandidateInput::alive, "alive" },
            { &SweepCandidateInput::openWorldMap, "openWorldMap" },
            { &SweepCandidateInput::hasSellableStock, "hasSellableStock" },
        };
        for (Case const& c : cases)
        {
            SweepCandidateInput in = base;
            in.*(c.field) = false;
            if (IsSweepCandidate(in))
            {
                std::cerr << "candidate accepted without " << c.name << "\n";
                return 1;
            }
        }

        struct Block { bool SweepCandidateInput::*field; char const* name; };
        Block blocks[] = {
            { &SweepCandidateInput::inCombat, "inCombat" },
            { &SweepCandidateInput::casting, "casting" },
            { &SweepCandidateInput::inDuel, "inDuel" },
            { &SweepCandidateInput::grouped, "grouped" },
            { &SweepCandidateInput::busyElsewhere, "busyElsewhere" },
        };
        for (Block const& b : blocks)
        {
            SweepCandidateInput in = base;
            in.*(b.field) = true;
            if (IsSweepCandidate(in))
            {
                std::cerr << "candidate accepted while " << b.name << "\n";
                return 1;
            }
        }
        ++passed;
    }

    // 3. Interval clamps to 5..3600 s, batch to 1..5 (PlayerbotAIConfig applies the same bounds).
    {
        assert(ClampIntervalSeconds(0) == 5);
        assert(ClampIntervalSeconds(4) == 5);
        assert(ClampIntervalSeconds(5) == 5);
        assert(ClampIntervalSeconds(120) == 120);
        assert(ClampIntervalSeconds(3600) == 3600);
        assert(ClampIntervalSeconds(1000000) == 3600);
        assert(ClampBatchSize(0) == 1);
        assert(ClampBatchSize(1) == 1);
        assert(ClampBatchSize(5) == 5);
        assert(ClampBatchSize(9) == 5);
        ++passed;
    }

    // 4. One pass per interval; the first call runs at once; a sub-5 s interval is raised to 5 s.
    {
        assert(SweepCycleDue(1000, 0, 120));
        assert(!SweepCycleDue(120999, 1000, 120));
        assert(SweepCycleDue(121000, 1000, 120));
        assert(!SweepCycleDue(1000 + 4999, 1000, 1));
        assert(SweepCycleDue(1000 + 5000, 1000, 1));
        assert(!SweepCycleDue(1000 + 119999, 1000, 3600));
        assert(SweepCycleDue(1000 + 3600000, 1000, 3600));
        ++passed;
    }

    // 5. The walk deadline is 45 s.
    {
        assert(!WalkTimedOut(44999, 0));
        assert(WalkTimedOut(45000, 0));
        assert(WalkTimedOut(46000, 1000));
        ++passed;
    }

    // 6. Rotation: past the block just considered, one step when nothing was considered, empty list stays at 0.
    {
        assert(AdvanceRotation(10, 2, 10) == 2);
        assert(AdvanceRotation(9, 3, 10) == 2);
        assert(AdvanceRotation(3, 5, 4) == 0);
        assert(AdvanceRotation(0, 0, 4) == 1);
        assert(AdvanceRotation(0, 0, 0) == 0);
        assert(AdvanceRotation(2, 1, 3) == 0);
        ++passed;
    }

    std::cout << "PASS: test_ah_market_policy (" << passed << " checks, 0 failed)\n";
    return 0;
}
