// Standalone check for ai/playerbot/TankFirstPolicy.h (tank_first, r13):
//   g++ -std=c++17 -Wall -Wextra tools/test_tank_first_policy.cpp -o /tmp/test_tank_first_policy && /tmp/test_tank_first_policy
//
// Every case names the old-code answer it replaces: today the seek releases a Son 5 s on a non-tank (SeekHoldWhyOf is
// NonTank), holds it at most 20 s and burns an engaged source at once, which is the opposite rule on both ends.
#include "../ai/playerbot/TankFirstPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

namespace
{
TankFirstRules Rules(std::uint32_t dwellMs, std::uint32_t capMs, std::uint8_t losses)
{
    TankFirstRules rules;
    rules.dwellMs = dwellMs;
    rules.capMs = capMs;
    rules.maxLosses = losses;
    return rules;
}

TankFirstIn In(TankFirstVictim victim, std::uint64_t guid, float hp, std::uint32_t now)
{
    TankFirstIn in;
    in.victim = victim;
    in.victimGuid = guid;
    in.hpPct = hp;
    in.nowMs = now;
    return in;
}

// The Hakkar row (D15): a 3 s dwell, a 15 s cap, 2 losses.
const TankFirstRules kRow = Rules(3000, 15000, 2);

// The walk's row after round 2 (R6): a 2 s dwell, the same 15 s cap and 2 losses.
const TankFirstRules kRow2 = Rules(2000, 15000, 2);

// A record that is already in `burn`: a non-tank had it, a tank held it for the dwell at t = 1000.
void ToBurn(TankFirstRecord& r, std::uint32_t dwellStart)
{
    TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, dwellStart - 1000));
    const TankFirstOut onTank = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, dwellStart));
    CHECK(onTank.event == TankFirstEvent::OnTank || onTank.event == TankFirstEvent::Arm);
    const TankFirstOut settled = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, dwellStart + 3000));
    CHECK(settled.event == TankFirstEvent::Settled);
    CHECK(r.phase == TankFirstPhase::Burn);
}
}

int main()
{
    // TestBurnWaitsForTankDwell: a Son on the puller holds; the tank's first tick does not release it, and the dwell
    // only settles at t + 3000.
    //   Old code: SeekMeetHoldsSon(true, true, true) is false with a tank victim, so the Son is released at 0 ms.
    //   Mutations caught: `>= dwellMs` -> `>= 0` (settles on the on_tank tick); Dwell dropped from
    //   TankFirstPhaseHolds (the dwell is not held).
    {
        TankFirstRecord r;
        const TankFirstOut arm = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 1000));
        CHECK(arm.event == TankFirstEvent::Arm);
        CHECK(r.phase == TankFirstPhase::Wait);
        CHECK(TankFirstHolds(r, nullptr));
        const TankFirstOut onTank = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 2000));
        CHECK(onTank.event == TankFirstEvent::OnTank);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(TankFirstHolds(r, nullptr));
        CHECK(onTank.ms == 1000);
        // t + 2999: still the dwell.
        TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 4999));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(TankFirstHolds(r, nullptr));
        // t + 3000: settled, and the add is free.
        out = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 5000));
        CHECK(out.event == TankFirstEvent::Settled);
        CHECK(r.phase == TankFirstPhase::Burn);
        CHECK(!TankFirstHolds(r, nullptr));
        CHECK(out.ms == 4000); // the episode ran from the first sighting at 1000
        CHECK(r.heldTotalMs == 4000);
        CHECK(r.settles == 1);
    }

    // TestLossReholds: the burn stops the moment a non-tank has the add, and a fresh dwell is needed.
    //   Old code: SeekHoldWhyOf(..., nonTankHeldMs = 5000) is NonTank, so the raid keeps burning.
    //   Mutations caught: step 4's Lost transition removed (the burn runs on); episodeMs not reset at the loss (the new
    //   episode's cap would count from the first sighting and end the hold early).
    {
        TankFirstRecord r;
        ToBurn(r, 1000);
        const TankFirstOut loss = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 60.0f, 6000));
        CHECK(loss.event == TankFirstEvent::Loss);
        CHECK(r.phase == TankFirstPhase::Lost);
        CHECK(TankFirstHolds(r, nullptr));
        CHECK(r.losses == 1);
        CHECK(r.episodeMs == 6000);
        CHECK(loss.ms == 2000); // the burn ran from the settle at 4000
        const TankFirstOut onTank = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 60.0f, 7000));
        CHECK(onTank.event == TankFirstEvent::OnTank);
        CHECK(r.phase == TankFirstPhase::Dwell);
        const TankFirstOut settled = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 60.0f, 10000));
        CHECK(settled.event == TankFirstEvent::Settled);
        CHECK(r.settles == 2);
        CHECK(!TankFirstHolds(r, nullptr));
    }

    // TestCapOpensForGood: 15 s without a tank ends the holds for the add's life, and neither a tank nor another
    // non-tank re-opens them.
    //   Old code: SeekHoldWhyOf(..., heldMs = 20000) is Cap20 (20 s, and the clock restarts with every read).
    //   Mutations caught: the cap compare removed (held forever); Open leaving on a tank read.
    {
        TankFirstRecord r;
        const TankFirstOut arm = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 0));
        CHECK(arm.event == TankFirstEvent::Arm);
        TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 14999));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(TankFirstHolds(r, nullptr));
        out = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 15000));
        CHECK(out.event == TankFirstEvent::Release);
        CHECK(std::string(out.why) == "cap");
        CHECK(r.phase == TankFirstPhase::Open);
        CHECK(!TankFirstHolds(r, nullptr));
        CHECK(out.ms == 15000);
        CHECK(r.heldTotalMs == 15000);
        // A tank later, and a non-tank later: nothing changes.
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 20000)).event == TankFirstEvent::None);
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 100.0f, 21000)).event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Open);
        CHECK(!TankFirstHolds(r, nullptr));
        CHECK(r.settles == 0);
    }

    // TestBrokenDwellKeepsEpisodeClock: a dwell that falls back to a non-tank does not restart the episode, so the cap
    // still ends 15 s after the first sighting. A fall back needs kTankFirstFlipMs of non-tank reads in a row (R6).
    //   Old code: none (no dwell exists).
    //   Mutation caught: `episodeMs = now` on the fall back (the hold could never end).
    {
        TankFirstRecord r;
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 10000)).event == TankFirstEvent::OnTank);
        CHECK(r.phase == TankFirstPhase::Dwell);
        // The tank loses it before the dwell: the first non-tank read still keeps the dwell (a blip between reads) ...
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 100.0f, 12000)).event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(TankFirstHolds(r, nullptr));
        // ... and 1000 ms of them in a row fall it back to where it came from, still held.
        const TankFirstOut back = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 100.0f, 13000));
        CHECK(back.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Wait);
        CHECK(TankFirstHolds(r, nullptr));
        CHECK(r.episodeMs == 0); // the first sighting's clock
        const TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 100.0f, 15000));
        CHECK(out.event == TankFirstEvent::Release);
        CHECK(std::string(out.why) == "cap");
        CHECK(r.phase == TankFirstPhase::Open);
        CHECK(out.ms == 15000);
    }

    // TestSettleBeatsCapOnTheSameTick: a dwell that completes on the cap's tick burns (the settle is checked first).
    //   Old code: none.
    //   Mutation caught: the cap checked before the settle (the add is released instead of settled).
    {
        TankFirstRecord r;
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 12000)).event == TankFirstEvent::OnTank);
        const TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 15000));
        CHECK(out.event == TankFirstEvent::Settled);
        CHECK(r.phase == TankFirstPhase::Burn);
        CHECK(!TankFirstHolds(r, nullptr));
        CHECK(r.settles == 1);
    }

    // TestLossBudget: two losses re-hold (Hakkar's row); the third is ignored once and the phase stays burn.
    //   Old code: none.
    //   Mutation caught: `>=` -> `>` in the budget test (a third hold).
    {
        TankFirstRecord r;
        ToBurn(r, 1000);
        std::uint32_t now = 5000;
        for (int loss = 1; loss <= 2; ++loss)
        {
            const TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 60.0f, now));
            CHECK(out.event == TankFirstEvent::Loss);
            CHECK(r.losses == static_cast<std::uint8_t>(loss));
            CHECK(TankFirstHolds(r, nullptr));
            now += 1000;
            CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 60.0f, now)).event == TankFirstEvent::OnTank);
            now += 3000;
            CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 60.0f, now)).event == TankFirstEvent::Settled);
            CHECK(!TankFirstHolds(r, nullptr));
            now += 1000;
        }
        // The third: ignored, once.
        TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 60.0f, now));
        CHECK(out.event == TankFirstEvent::LossIgnored);
        CHECK(std::string(out.why) == "loss_cap");
        CHECK(r.phase == TankFirstPhase::Burn);
        CHECK(!TankFirstHolds(r, nullptr));
        CHECK(r.losses == 2);
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 60.0f, now + 1000)).event ==
              TankFirstEvent::None);
        // A budget of zero ignores the first loss.
        TankFirstRecord zero;
        ToBurn(zero, 1000);
        const TankFirstOut first = TankFirstTick(zero, Rules(3000, 15000, 0),
                                                 In(TankFirstVictim::NonTank, 12, 60.0f, 5000));
        CHECK(first.event == TankFirstEvent::LossIgnored);
        CHECK(std::string(first.why) == "loss_cap");
        CHECK(zero.phase == TankFirstPhase::Burn);
        CHECK(zero.losses == 0);
    }

    // TestLowHealthLossIgnored: a loss at or under 20 % health opens no hold; at 20.1 % it does.
    //   Old code: none.
    //   Mutation caught: the health test removed (a hold at 20 %).
    {
        TankFirstRecord r;
        ToBurn(r, 1000);
        TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 20.0f, 5000));
        CHECK(out.event == TankFirstEvent::LossIgnored);
        CHECK(std::string(out.why) == "low_hp");
        CHECK(r.phase == TankFirstPhase::Burn);
        CHECK(r.losses == 0);
        out = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 20.1f, 5100));
        CHECK(out.event == TankFirstEvent::Loss);
        CHECK(r.phase == TankFirstPhase::Lost);
        CHECK(r.losses == 1);
    }

    // TestUnknownVictim: no victim neither completes nor breaks the dwell and opens no loss, and the dwell runs on
    // across a tank change.
    //   Old code: none.
    //   Mutation caught: None treated as NonTank (the dwell breaks and a burn loses).
    {
        TankFirstRecord r;
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 1000)).event == TankFirstEvent::OnTank);
        TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::None, 0, 100.0f, 2000));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(r.tankSinceMs == 1000); // not reset
        CHECK(TankFirstHolds(r, nullptr));
        // Tank A -> tank B keeps the dwell.
        out = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 22, 100.0f, 2500));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(r.tank == 22);
        CHECK(r.tankSinceMs == 1000);
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 22, 100.0f, 4000)).event == TankFirstEvent::Settled);
        // No victim in a burn opens no loss.
        out = TankFirstTick(r, kRow, In(TankFirstVictim::None, 0, 60.0f, 5000));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Burn);
        CHECK(r.losses == 0);
        // No victim does not complete the dwell either, even once its time is in: only a tank's own read does.
        TankFirstRecord n;
        CHECK(TankFirstTick(n, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        CHECK(TankFirstTick(n, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 1000)).event == TankFirstEvent::OnTank);
        out = TankFirstTick(n, kRow, In(TankFirstVictim::None, 0, 100.0f, 4000));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(n.phase == TankFirstPhase::Dwell);
        CHECK(n.tankMs == 3000);
        out = TankFirstTick(n, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 4100));
        CHECK(out.event == TankFirstEvent::Settled);
    }

    // TestOverrides: no usable tank frees the add, and so does a seek source inside the last 15 s before its cycle
    // spell; an unknown clock frees nobody.
    //   Old code: none.
    //   Mutations caught: `<=` -> `<` (nothing is released at exactly 15000 ms); the dueKnown test dropped (an unknown
    //   clock would free every Son).
    {
        CHECK(std::string(TankFirstOverride(0, false, false, 0)) == "no_tank");
        CHECK(TankFirstOverride(1, false, false, 0) == nullptr);
        CHECK(std::string(TankFirstOverride(1, true, true, 15000)) == "siphon");
        CHECK(TankFirstOverride(1, true, true, 15001) == nullptr);
        CHECK(TankFirstOverride(1, true, false, 1000) == nullptr);
        CHECK(TankFirstOverride(1, false, true, 100) == nullptr);
        TankFirstRecord r;
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        CHECK(TankFirstHolds(r, nullptr));
        CHECK(!TankFirstHolds(r, "no_tank"));
        CHECK(!TankFirstHolds(r, "siphon"));
        CHECK(TankFirstHolds(r, nullptr));
    }

    // TestHowTaunt: a taunt by that tank within 2 s makes the take a taunt; older, or by another tank, is threat.
    //   Old code: none.
    //   Mutation caught: the tank compare dropped (another tank's taunt would count).
    {
        TankFirstRecord r;
        TankFirstNoteTaunt(r, 21, 2000);
        CHECK(std::string(TankFirstHow(r, 21, 4000)) == "taunt");
        CHECK(std::string(TankFirstHow(r, 21, 4001)) == "threat");
        CHECK(std::string(TankFirstHow(r, 22, 2001)) == "threat");
        CHECK(std::string(TankFirstHow(r, 0, 2001)) == "threat");
    }

    // TestTickDue: a fresh record is due, then 250 ms apart.
    //   Old code: none.
    //   Mutation caught: the throttle removed (two readers in one tick both advance the record).
    {
        TankFirstRecord r;
        CHECK(TankFirstTickDue(r, 0));
        TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 500));
        CHECK(!TankFirstTickDue(r, 749));
        CHECK(TankFirstTickDue(r, 750));
        CHECK(TankFirstTickDue(r, 1000));
    }

    // TestFirstSightingOnATank: an add first seen on a tank is in the dwell from its arm, and the event stays Arm: the
    // log's first row is `arm` and no `on_tank` follows (a first-fight reading accepts arm -> settled).
    //   Old code: none.
    //   Mutation caught: step 1 returning before the victim read (the add waits 3 s more).
    {
        TankFirstRecord r;
        const TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 5000));
        CHECK(out.event == TankFirstEvent::Arm);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(r.tankSinceMs == 5000);
        CHECK(r.tank == 21);
        CHECK(r.firstMs == 5000);
        CHECK(TankFirstHolds(r, nullptr));
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 8000)).event == TankFirstEvent::Settled);
    }

    // TestTauntLandingSettles: a taunt by the very tank the add turned to settles without the dwell, from `wait` and
    // from a running `dwell` (R5).
    //   Old code: the take casts its taunt and the record still waits out the dwell (the take row said `taunt`, the
    //   record never heard it), and only a non-tank's read moved it.
    //   Mutation caught: the tauntBy compare dropped (any tank's taunt would settle).
    {
        TankFirstRecord r;
        CHECK(TankFirstTick(r, kRow2, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        TankFirstNoteTaunt(r, 21, 1000);
        TankFirstOut out = TankFirstTick(r, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 1250));
        CHECK(out.event == TankFirstEvent::Settled);
        CHECK(r.phase == TankFirstPhase::Burn);
        CHECK(!TankFirstHolds(r, nullptr));
        CHECK(std::string(TankFirstHow(r, 21, 1250)) == "taunt");
        // From a running dwell the same taunt settles at once.
        TankFirstRecord dwell;
        CHECK(TankFirstTick(dwell, kRow2, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        CHECK(TankFirstTick(dwell, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 1000)).event == TankFirstEvent::OnTank);
        TankFirstNoteTaunt(dwell, 21, 1100);
        out = TankFirstTick(dwell, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 1250));
        CHECK(out.event == TankFirstEvent::Settled);
        CHECK(dwell.phase == TankFirstPhase::Burn);
        // Another tank's taunt is no proof: its take still waits out the dwell.
        TankFirstRecord other;
        CHECK(TankFirstTick(other, kRow2, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        TankFirstNoteTaunt(other, 22, 1000);
        out = TankFirstTick(other, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 1250));
        CHECK(out.event == TankFirstEvent::OnTank);
        CHECK(other.phase == TankFirstPhase::Dwell);
    }

    // TestCumulativeDwell: the dwell sums tank time across a short non-tank blip instead of restarting (R6).
    //   Old code: the dwell is one continuous stamp; any non-tank read dropped it back to wait.
    //   Mutation caught: reset tankMs on any non-tank read (the last 0.8 s would fall short).
    {
        TankFirstRecord r;
        CHECK(TankFirstTick(r, kRow2, In(TankFirstVictim::NonTank, 11, 100.0f, 1000)).event == TankFirstEvent::Arm);
        CHECK(TankFirstTick(r, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 2000)).event == TankFirstEvent::OnTank);
        TankFirstOut out = TankFirstTick(r, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 3200));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(r.tankMs == 1200);
        // 500 ms on a healer: no tank time, and no fall back.
        out = TankFirstTick(r, kRow2, In(TankFirstVictim::NonTank, 12, 100.0f, 3700));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(r.tankMs == 1200);
        // 0.8 s back on the tank: 2.0 s in all, settled.
        out = TankFirstTick(r, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 4500));
        CHECK(out.event == TankFirstEvent::Settled);
        CHECK(r.phase == TankFirstPhase::Burn);
        CHECK(r.tankMs == 2000);
        CHECK(out.ms == 3500); // the episode from the first sighting at 1000
    }

    // TestFlipDebounce: a non-tank falls the dwell back at 1000 ms in a row, not at 999 (R6).
    //   Old code: the first non-tank read fell it back.
    //   Mutation caught: the flip compare `>=` -> `>`.
    {
        TankFirstRecord r;
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 1000)).event == TankFirstEvent::OnTank);
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 100.0f, 2000)).event == TankFirstEvent::None);
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 100.0f, 2999)).event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(TankFirstHolds(r, nullptr));
        CHECK(TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 100.0f, 3000)).event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Wait);
        CHECK(TankFirstHolds(r, nullptr));
    }

    // TestDwellCapExtends: in the dwell the cap is window_s plus the dwell, so a take that lands at 14 s settles at
    // 16 s instead of ending on the cap at 15 s (R7); the extension still ends the hold at 17 s.
    //   Old code: the cap released at 15 s from the first sighting, whenever the dwell started.
    //   Mutation caught: the extension dropped (a `cap` at 15000).
    {
        TankFirstRecord r;
        CHECK(TankFirstTick(r, kRow2, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        CHECK(TankFirstTick(r, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 14000)).event == TankFirstEvent::OnTank);
        TankFirstOut out = TankFirstTick(r, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 15000));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(TankFirstHolds(r, nullptr));
        out = TankFirstTick(r, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 16000));
        CHECK(out.event == TankFirstEvent::Settled);
        CHECK(r.phase == TankFirstPhase::Burn);
        CHECK(r.heldTotalMs == 16000);
        // The extension ends at 17 s: a dwell whose blips kept its tank time short releases on the cap.
        TankFirstRecord late;
        CHECK(TankFirstTick(late, kRow2, In(TankFirstVictim::NonTank, 11, 100.0f, 0)).event == TankFirstEvent::Arm);
        CHECK(TankFirstTick(late, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 14000)).event == TankFirstEvent::OnTank);
        CHECK(TankFirstTick(late, kRow2, In(TankFirstVictim::NonTank, 12, 100.0f, 14500)).event == TankFirstEvent::None);
        CHECK(TankFirstTick(late, kRow2, In(TankFirstVictim::NonTank, 12, 100.0f, 15499)).event == TankFirstEvent::None);
        CHECK(late.phase == TankFirstPhase::Dwell); // the 999 ms blip is no fall back
        CHECK(TankFirstTick(late, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 15500)).event == TankFirstEvent::None);
        CHECK(TankFirstTick(late, kRow2, In(TankFirstVictim::NonTank, 12, 100.0f, 15600)).event == TankFirstEvent::None);
        CHECK(TankFirstTick(late, kRow2, In(TankFirstVictim::NonTank, 12, 100.0f, 16599)).event == TankFirstEvent::None);
        CHECK(TankFirstTick(late, kRow2, In(TankFirstVictim::Tank, 21, 100.0f, 16600)).event == TankFirstEvent::None);
        CHECK(late.tankMs < kRow2.dwellMs);
        out = TankFirstTick(late, kRow2, In(TankFirstVictim::None, 0, 100.0f, 17000));
        CHECK(out.event == TankFirstEvent::Release);
        CHECK(std::string(out.why) == "cap");
        CHECK(late.phase == TankFirstPhase::Open);
    }

    // TestChaseLeaseBounds: a lease needs a holding record, an allowed take and a tank that is not the boss's victim,
    // and it lapses kTankFirstChaseLeaseMs after it was written (R1).
    //   Old code: none (the take never chased, the anchor held every walk).
    //   Mutation caught: the boss-victim test dropped (Hakkar's own tank would walk off its circle).
    {
        CHECK(!TankFirstChaseLeaseOk(false, true, false)); // the record is not holding
        CHECK(!TankFirstChaseLeaseOk(true, false, false)); // the take was refused
        CHECK(!TankFirstChaseLeaseOk(true, true, true));   // the boss's own victim
        CHECK(TankFirstChaseLeaseOk(true, true, false));
        CHECK(!TankFirstChaseLeaseLive(0, 5000));          // no lease
        CHECK(TankFirstChaseLeaseLive(1000, 2500));        // exactly the lease
        CHECK(!TankFirstChaseLeaseLive(1000, 2501));       // lapsed
        CHECK(TankFirstChaseLeaseLive(5000, 4000));        // a read sampled before the write never lapses it
    }

    // TestTauntWhyOfResult: each taunt result maps to its own word (R3).
    //   Old code: every CanCastSpell false was `range`, a catch-all.
    //   Mutation caught: LINE_OF_SIGHT mapped to the plain range word.
    {
        CHECK(std::string(TankFirstTauntWhyOf(kTankFirstCastOk, false, false)) == "ok");
        CHECK(std::string(TankFirstTauntWhyOf(kTankFirstCastOutOfRange, true, false)) == "walk");
        CHECK(std::string(TankFirstTauntWhyOf(kTankFirstCastOutOfRange, false, false)) == "range_refused");
        CHECK(std::string(TankFirstTauntWhyOf(kTankFirstCastLineOfSight, true, false)) == "walk");
        CHECK(std::string(TankFirstTauntWhyOf(kTankFirstCastLineOfSight, false, false)) == "los");
        CHECK(std::string(TankFirstTauntWhyOf(kTankFirstCastNotReady, false, false)) == "gcd");
        CHECK(std::string(TankFirstTauntWhyOf(kTankFirstCastNotReady, false, true)) == "cooldown");
        CHECK(std::string(TankFirstTauntWhyOf(kTankFirstCastInProgress, false, false)) == "busy");
        CHECK(TankFirstTauntWhyOf(55, false, false) == nullptr); // the caller writes res=55
    }

    // TestClockSkew: a stamp another thread wrote while this read waited can be newer than the read's own clock sample.
    //   Old code: none (the record did not exist).
    //   Mutations caught: the raw unsigned age back in any compare (a future tankSince settles, a future episodeMs
    //   releases on the cap, a future seenMs erases the record, a future tickMs advances early).
    {
        TankFirstRecord r;
        r.armed = true;
        r.firstMs = 1000;
        r.episodeMs = 1000;
        r.tickMs = 1000;
        r.seenMs = 5000;
        r.phase = TankFirstPhase::Dwell;
        r.dwellFrom = TankFirstPhase::Wait;
        r.tankSinceMs = 5000; // stamped after this read's sample
        r.tank = 21;
        TankFirstOut out = TankFirstTick(r, kRow, In(TankFirstVictim::Tank, 21, 100.0f, 3000));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Dwell);
        CHECK(TankFirstHolds(r, nullptr));
        CHECK(r.settles == 0);
        CHECK(TankFirstAgeMs(3000, 5000) == 0);
        // A future episode start wraps the cap: it releases nothing and adds no held time.
        r.phase = TankFirstPhase::Wait;
        r.episodeMs = 9000;
        r.tankSinceMs = 0;
        out = TankFirstTick(r, kRow, In(TankFirstVictim::NonTank, 12, 100.0f, 4000));
        CHECK(out.event == TankFirstEvent::None);
        CHECK(r.phase == TankFirstPhase::Wait);
        CHECK(r.heldTotalMs == 0);
        // The sweep's stale test: a read older than the stamp saw nothing stale, and the threshold itself is inclusive.
        CHECK(!TankFirstStale(4000, 9000));
        CHECK(!TankFirstStale(kTankFirstStaleMs, 0));
        CHECK(TankFirstStale(kTankFirstStaleMs + 1, 0));
        // A future tick stamp does not make the record due.
        r.tickMs = 9000;
        CHECK(!TankFirstTickDue(r, 4000));
        // The taunt age clamps too: a future note is still the taunt.
        TankFirstRecord taunt;
        TankFirstNoteTaunt(taunt, 21, 9000);
        CHECK(std::string(TankFirstHow(taunt, 21, 4000)) == "taunt");
    }

    // TestOverrideReleaseRows: one release row per add and reason, on the read that starts the override.
    //   Old code: none (the release row did not exist).
    //   Mutation caught: asking the hold (always false under an override) instead of the phase (both rows were dead).
    {
        const char* const noTank = TankFirstOverride(0, false, false, 0);
        const char* const siphon = TankFirstOverride(1, true, true, 15000);
        CHECK(std::string(noTank) == "no_tank");
        CHECK(std::string(siphon) == "siphon");
        CHECK(TankFirstOverrideStarted(noTank, nullptr, TankFirstPhase::Wait) == noTank);
        CHECK(TankFirstOverrideStarted(noTank, noTank, TankFirstPhase::Wait) == nullptr);
        CHECK(TankFirstOverrideStarted(siphon, noTank, TankFirstPhase::Dwell) == siphon);
        CHECK(TankFirstOverrideStarted(noTank, nullptr, TankFirstPhase::Burn) == nullptr);
        CHECK(TankFirstOverrideStarted(noTank, nullptr, TankFirstPhase::Open) == nullptr);
        CHECK(TankFirstOverrideStarted(nullptr, noTank, TankFirstPhase::Wait) == nullptr);
        // The override coming back after it ended is a new start.
        CHECK(TankFirstOverrideStarted(noTank, nullptr, TankFirstPhase::Lost) == noTank);
    }

    // The row's own numbers (D15) come out of TankFirstRulesOf.
    {
        const TankFirstRules row = TankFirstRulesOf(3.0f, 15.0f, 2);
        CHECK(row.dwellMs == 3000);
        CHECK(row.capMs == 15000);
        CHECK(row.maxLosses == 2);
        CHECK(std::string(TankFirstPhaseWord(TankFirstPhase::Wait)) == "wait");
        CHECK(std::string(TankFirstPhaseWord(TankFirstPhase::Dwell)) == "dwell");
        CHECK(std::string(TankFirstPhaseWord(TankFirstPhase::Burn)) == "burn");
        CHECK(std::string(TankFirstPhaseWord(TankFirstPhase::Lost)) == "lost");
        CHECK(std::string(TankFirstPhaseWord(TankFirstPhase::Open)) == "open");
    }

    std::cout << "All tank_first policy tests passed.\n";
    return 0;
}
