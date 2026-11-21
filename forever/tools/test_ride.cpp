// Pure unit tests for W5 ride review fixes (deck Z check, approach no-path give-up,
// missing transport timeout & throttled rescan, pending-until-arrival order status).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_ride.cpp -o /tmp/test_ride && /tmp/test_ride

#include "ai/playerbot/policy/travel/TransportWaitPolicy.h"

#include <iostream>
#include <string>

namespace
{

int g_failed = 0;

void Check(bool cond, char const* name)
{
    if (!cond)
    {
        std::cout << "FAIL: " << name << "\n";
        ++g_failed;
    }
}

} // namespace

int main()
{
    // 1. Finding 5: bot within 2D board reach but below or above deck Z must NOT board.
    {
        ai::TransportWaitInput in;
        in.mapReady = true;
        in.onBoard = false;
        in.transportFound = true;
        in.transportStopped = true;
        in.distanceToDeck = 5.0f;
        in.maxBoardRange = 12.0f;
        in.approachRange = 60.0f;
        in.hasDeckZ = true;
        in.botZ = 0.0f;
        in.deckZ = 8.0f;
        in.maxDeckZDiff = 2.5f;
        Check(ai::DecideTransportWait(in) != ai::TransportWaitStep::Board,
            "finding5_below_deck_must_not_board");

        in.botZ = 14.0f;
        Check(ai::DecideTransportWait(in) != ai::TransportWaitStep::Board,
            "finding5_above_deck_must_not_board");

        in.hasDeckZ = false;
        in.botZ = 8.0f;
        Check(ai::DecideTransportWait(in) != ai::TransportWaitStep::Board,
            "finding5_no_deck_surface_must_not_board");

        in.hasDeckZ = true;
        in.botZ = 8.5f;
        Check(ai::DecideTransportWait(in) == ai::TransportWaitStep::Board,
            "finding5_on_deck_boards");
    }

    // 2. Finding 7: approach with no navmesh path gives up; usable path check rejects NOPATH.
    {
        Check(!ai::IsApproachPathUsable(0x08u /*PATHFIND_NOPATH*/, 0),
            "finding7_nopath_unusable");
        Check(ai::IsApproachPathUsable(0x01u /*PATHFIND_NORMAL*/, 3),
            "finding7_normal_path_usable");

        ai::TransportWaitInput in;
        in.mapReady = true;
        in.onBoard = false;
        in.transportFound = true;
        in.transportStopped = true;
        in.distanceToDeck = 30.0f;
        in.maxBoardRange = 12.0f;
        in.approachRange = 60.0f;
        in.approachPathFailed = true;
        Check(ai::DecideTransportWait(in) == ai::TransportWaitStep::GiveUp,
            "finding7_approach_nopath_gives_up");
    }

    // 3. Finding 8: never-found transport (totalPathTimeMs == 0) gives up after missingTimeoutMs;
    //    rescan is throttled; ride order answers pending until arrival.
    {
        ai::TransportWaitInput in;
        in.mapReady = true;
        in.onBoard = false;
        in.transportFound = false;
        in.totalPathTimeMs = 0;
        in.missingTimeoutMs = 30000;
        in.waitedMs = 29000;
        Check(ai::DecideTransportWait(in) == ai::TransportWaitStep::Wait,
            "finding8_waits_before_missing_timeout");

        in.waitedMs = 30000;
        Check(ai::DecideTransportWait(in) == ai::TransportWaitStep::GiveUp,
            "finding8_never_found_gives_up");

        Check(!ai::ShouldRescanTransport(/*transportFound=*/false, /*boarded=*/false, 500, 3000),
            "finding8_rescan_throttled");
        Check(ai::ShouldRescanTransport(/*transportFound=*/false, /*boarded=*/false, 3000, 3000),
            "finding8_rescan_due");
        Check(!ai::ShouldRescanTransport(/*transportFound=*/true, /*boarded=*/false, 5000, 3000),
            "finding8_no_rescan_when_found");

        Check(ai::RideOrderResult(true, false, false, false, "") == "pending:waiting",
            "finding8_pending_while_waiting");
        Check(ai::RideOrderResult(true, false, false, true, "") == "pending:off-deck",
            "finding8_pending_off_deck");
        Check(ai::RideOrderResult(true, true, false, false, "") == "pending:aboard",
            "finding8_pending_aboard");
        Check(ai::RideOrderResult(false, true, true, false, "") == "ok",
            "finding8_ok_on_arrival");
        Check(ai::RideOrderResult(false, false, false, false, "not-found") == "fail:not-found",
            "finding8_fail_code");
    }

    if (g_failed > 0)
    {
        std::cout << "FAIL: test_ride (" << g_failed << " failed)\n";
        return 1;
    }

    std::cout << "PASS: test_ride (0 failed)\n";
    return 0;
}
