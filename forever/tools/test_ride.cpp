// Pure unit tests for W5 ride review fixes (deck Z check, approach no-path give-up,
// missing transport timeout & throttled rescan, pending-until-arrival order status).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_ride.cpp -o /tmp/test_ride && /tmp/test_ride

#include "ai/playerbot/policy/travel/TransportWaitPolicy.h"

#include <iostream>
#include <string>
#include <vector>

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

    // 4. #215 boarding point: the deck sample nearest the bot at its height band; none qualifies -> false.
    {
        std::vector<ai::DeckPoint> samples = {
            { 10.0f, 0.0f, 53.5f },   // deck, near the bot
            { 30.0f, 0.0f, 53.8f },   // deck, far end
            { 4.0f, 0.0f, 40.0f },    // hull bottom: nearest in 2D, 14 yd too low
            { 5.0f, 0.0f, 70.0f } };  // balloon
        ai::DeckPoint p;
        Check(ai::PickBoardingPoint(samples, 0.0f, 0.0f, 53.9f, 2.5f, p) && p.x == 10.0f,
            "i215_boarding_point_nearest_deck_sample");
        Check(!ai::PickBoardingPoint({ { 4.0f, 0.0f, 40.0f } }, 0.0f, 0.0f, 53.9f, 2.5f, p),
            "i215_no_deck_at_bot_height");
        Check(!ai::PickBoardingPoint({}, 0.0f, 0.0f, 53.9f, 2.5f, p),
            "i215_no_samples");
    }

    // 5. #215 approach: far = navmesh to the dock edge, near = one straight step onto the deck.
    {
        Check(ai::ChooseDeckApproach(25.0f, 10.0f) == ai::DeckApproachMode::Navmesh, "i215_far_walks_navmesh");
        Check(ai::ChooseDeckApproach(10.0f, 10.0f) == ai::DeckApproachMode::Direct, "i215_near_steps_direct");
        Check(ai::ChooseDeckApproach(3.0f, 10.0f) == ai::DeckApproachMode::Direct, "i215_close_steps_direct");
    }

    // 6. #215 stale state: a boarded bot that is no longer a passenger fails after the grace; aboard or arrived never does.
    {
        Check(!ai::HasLostTransport(true, false, false, 4999, 5000), "i215_lost_inside_grace_waits");
        Check(ai::HasLostTransport(true, false, false, 5000, 5000), "i215_lost_after_grace_fails");
        Check(!ai::HasLostTransport(true, false, true, 9000, 5000), "i215_aboard_is_not_lost");
        Check(!ai::HasLostTransport(true, true, false, 9000, 5000), "i215_arrived_is_not_lost");
        Check(!ai::HasLostTransport(false, false, false, 9000, 5000), "i215_never_boarded_is_not_lost");
        Check(ai::RideOrderResult(false, false, false, false, "lost-transport") == "fail:lost-transport",
            "i215_lost_transport_fail_code");
    }

    // 7. #215 follow vs ride: a running ride holds the bot's movement until it arrived or ended.
    {
        Check(ai::RideHoldsMovement(true, false), "i215_waiting_or_aboard_holds");
        Check(!ai::RideHoldsMovement(false, true), "i215_arrived_releases");
        Check(!ai::RideHoldsMovement(false, false), "i215_ended_releases");
    }

    if (g_failed > 0)
    {
        std::cout << "FAIL: test_ride (" << g_failed << " failed)\n";
        return 1;
    }

    std::cout << "PASS: test_ride (0 failed)\n";
    return 0;
}
