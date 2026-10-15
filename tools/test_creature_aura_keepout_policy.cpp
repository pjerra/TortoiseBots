// Standalone check for ai/playerbot/CreatureAuraKeepoutPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_creature_aura_keepout_policy.cpp -o /tmp/test_cak && /tmp/test_cak
#include "../ai/playerbot/CreatureAuraKeepoutPolicy.h"
#include "../ai/playerbot/AvoidAoePolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots creature aura keep-out tests...\n";

    // Table: the Creeping Sludge row, Turtle ids, only on Maraudon.
    {
        CreatureAuraKeepout const* row = FindCreatureAuraKeepout(349, 12222);
        CHECK(row != nullptr);
        CHECK(row->auraSpellId == 22638);
        CHECK(row->pulseRadius == 5.0f);
        CHECK(row->keepoutRadius == 8.0f);
        CHECK(FindCreatureAuraKeepout(0, 12222) == nullptr);   // other map
        CHECK(FindCreatureAuraKeepout(349, 12221) == nullptr); // Noxious Slime: no aura, not listed
        CHECK(FindCreatureAuraKeepout(349, 0) == nullptr);
    }
    std::cout << "  [PASS] table lookup by map and entry\n";

    // Every row keeps out past its pulse and fits the avoid-aoe radius cap,
    // so the existing strafe-first step-out handles it.
    for (CreatureAuraKeepout const& row : kCreatureAuraKeepouts)
    {
        CHECK(row.keepoutRadius > row.pulseRadius);
        CHECK(IsAvoidableAoeRadius(row.keepoutRadius));
        CHECK(CreatureAuraHazardRadius(row) > row.keepoutRadius);
        CHECK(IsAvoidableAoeRadius(CreatureAuraHazardRadius(row)));
    }
    std::cout << "  [PASS] keep-out wider than the pulse, inside the 15 yd cap\n";

    // Applies: toggle, row, alive, aura, and no explicit order on it.
    {
        CreatureAuraKeepout const* row = FindCreatureAuraKeepout(349, 12222);
        CHECK(CreatureAuraKeepoutApplies(true, row, true, true, false));
        CHECK(!CreatureAuraKeepoutApplies(false, row, true, true, false)); // toggle off: today's behaviour
        CHECK(!CreatureAuraKeepoutApplies(true, nullptr, true, true, false));
        CHECK(!CreatureAuraKeepoutApplies(true, row, false, true, false)); // dead
        CHECK(!CreatureAuraKeepoutApplies(true, row, true, false, false)); // aura gone
        CHECK(!CreatureAuraKeepoutApplies(true, row, true, true, true));   // player ordered the attack
    }
    std::cout << "  [PASS] applies only with toggle, live aura and no explicit order\n";

    // Step-out band and hysteresis: step out inside 8, paths kept outside 9.
    {
        CreatureAuraKeepout const& row = *FindCreatureAuraKeepout(349, 12222);
        CHECK(InsideCreatureAuraKeepout(row, 4.0f));  // melee reach: inside the pulse
        CHECK(InsideCreatureAuraKeepout(row, 8.0f));
        CHECK(!InsideCreatureAuraKeepout(row, 8.1f));
        CHECK(CreatureAuraHazardRadius(row) == 9.0f);
        // The existing escape step clears the keep-out rim from inside the pulse.
        CHECK(AoeEscapeStep(row.keepoutRadius, 8.0f) == 8.0f);
        CHECK(AoeEscapeStep(row.keepoutRadius, 15.0f) == 9.0f);
        CHECK(kCreatureAuraHazardSeconds >= 2);
    }
    std::cout << "  [PASS] step out inside 8 yd, hazard keeps paths 9 yd away\n";

    // Player order: the ordered creature is never a keep-out for that bot,
    // whatever its state; any other listed creature still is.
    {
        CreatureAuraKeepout const* row = FindCreatureAuraKeepout(349, 12222);
        bool const ordered = true;
        CHECK(!CreatureAuraKeepoutApplies(true, row, true, true, ordered));
        CHECK(CreatureAuraKeepoutApplies(true, row, true, true, !ordered));
    }
    std::cout << "  [PASS] attack order exempts only the ordered creature\n";

    // Follow near a keep-out. Sludge at the origin; rings 8 / 9 / 9.5 / 10 / 10.5.
    CreatureAuraKeepout const& sludge = *FindCreatureAuraKeepout(349, 12222);
    using Step = CreatureAuraFollowStep;
    auto plan = [&](bool engaged, float bx, float by, float px, float py, float cx, float cy,
                    float& hx, float& hy) {
        return PlanCreatureAuraFollow(sludge, engaged, bx, by, px, py, cx, cy, hx, hy);
    };
    auto dist = [](float x, float y) { return std::sqrt(x * x + y * y); };
    {
        CreatureAuraFollowRings const r = CreatureAuraFollowRingsFor(sludge);
        CHECK(r.keepout == 8.0f && r.hazard == 9.0f && r.hold == 9.5f);
        CHECK(r.backoff == 10.0f && r.holdOuter == 10.5f && r.release == 10.0f);
        // Every move target sits inside the hold band with margin both ways.
        CHECK(r.hold - r.hazard >= 0.5f && r.holdOuter - r.hold >= 0.5f);
        CHECK(r.backoff - r.hazard >= 0.5f && r.holdOuter - r.backoff >= 0.5f);
        CHECK(r.keepout < r.hazard && r.release > r.hold);
    }
    std::cout << "  [PASS] follow rings nest with 0.5 yd margins\n";

    // Bot east of the sludge, master (goal) 3 yd west of it: every distance
    // from 8 to 10.5 either backs off outward or holds; none follows.
    {
        float hx = 0.0f, hy = 0.0f;
        CHECK(plan(false, 4.0f, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::StepOut);
        CHECK(plan(false, 8.0f, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::StepOut);
        float const backoff[] = { 8.01f, 8.25f, 8.5f, 8.75f, 8.99f };
        for (float d : backoff)
        {
            hx = hy = 0.0f;
            CHECK(plan(false, d, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::BackOff);
            // Outward along the sludge-bot line, to the back-off ring.
            CHECK(std::fabs(hx - 10.0f) < 0.01f && std::fabs(hy) < 0.01f);
        }
        float const hold[] = { 9.0f, 9.25f, 9.49f, 9.5f, 9.51f, 10.0f, 10.5f };
        for (float d : hold)
        {
            CHECK(plan(false, d, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
            CHECK(plan(true, d, 0.0f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
        }
        // Oblique bot positions hold by distance too, not by axis.
        CHECK(plan(false, 9.49f * 0.6f, 9.49f * 0.8f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
        CHECK(plan(false, 8.5f * 0.6f, -8.5f * 0.8f, -3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::BackOff);
        CHECK(std::fabs(dist(hx, hy) - 10.0f) < 0.01f && hx > 0.0f && hy < 0.0f);
    }
    std::cout << "  [PASS] 8-10.5 yd: back off below 9, hold from 9 (incl. 9.49), step-out at 8\n";

    // Approach from outside the band: stop on the hold ring on the way in,
    // and the arrival (even a hair short after the height snap) holds.
    {
        float hx = 0.0f, hy = 0.0f;
        CHECK(plan(false, 20.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(hx - 9.5f) < 0.01f && std::fabs(hy) < 0.01f);
        CHECK(plan(false, 10.6f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(hx - 9.5f) < 0.01f);
        // Arrived 0.4 yd short or long of the hold point: hold.
        CHECK(plan(true, 9.1f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
        CHECK(plan(true, 9.9f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);
        // Oblique approach: the entry point lies on the hold ring, bot side.
        CHECK(plan(false, -20.0f, 5.0f, 0.0f, 2.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(dist(hx, hy) - 9.5f) < 0.01f && hx < 0.0f);
        // The hold point is outside the hazard and the keep-out radius.
        CHECK(!InsideCreatureAuraKeepout(sludge, 9.5f) && 9.5f > CreatureAuraHazardRadius(sludge));
    }
    std::cout << "  [PASS] approach stops on the 9.5 yd ring; arrival +-0.4 yd holds\n";

    // A sludge creeping (2 yd/s, 0.25 yd per tick) toward a bot holding at
    // 9.5 yd, the master 3 yd behind it: hold, back off along the same line
    // before the step-out radius, hold again; never follow, never step out.
    {
        float bx = 9.5f;
        float const by = 0.0f;
        float hx = 0.0f, hy = 0.0f;
        int backOffs = 0;
        float closest = 100.0f;
        for (int i = 0; i <= 40; ++i)
        {
            float const cx = 0.25f * i; // sludge moves east, toward the bot
            float const d = bx - cx;
            closest = std::min(closest, d);
            Step const s = plan(true, bx, by, cx - 3.0f, 0.0f, cx, 0.0f, hx, hy);
            CHECK(s == Step::Hold || s == Step::BackOff);
            CHECK((s == Step::Hold) == (d >= 9.0f));
            if (s == Step::BackOff)
            {
                // On the sludge-bot line, 10 yd from the sludge.
                CHECK(std::fabs(hy) < 0.01f && std::fabs((hx - cx) - 10.0f) < 0.01f);
                bx = hx; // the bot lands there before the next tick
                ++backOffs;
            }
        }
        CHECK(backOffs >= 4);
        CHECK(closest > sludge.keepoutRadius + 0.5f); // never near the step-out
    }
    std::cout << "  [PASS] creeping sludge: hold, back off on the line, hold; no step-out\n";

    // Goal leaving the circle, with hysteresis: not engaged, the goal counts
    // inside up to 9.5 yd; engaged, it must clear 10 yd before follow resumes.
    {
        float hx = 0.0f, hy = 0.0f;
        CHECK(plan(false, 9.5f, 0.0f, 9.4f, 1.0f, 0.0f, 0.0f, hx, hy) != Step::Follow); // goal 9.45
        CHECK(plan(false, 9.5f, 0.0f, -9.8f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Follow); // goal 9.8, fresh
        CHECK(plan(true, 9.5f, 0.0f, -9.8f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);    // goal 9.8, engaged
        CHECK(plan(true, 9.5f, 0.0f, -10.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Hold);   // on the release ring
        CHECK(plan(true, 9.5f, 0.0f, -10.01f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Follow); // left the circle
        CHECK(plan(true, 4.0f, 0.0f, -12.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Follow); // goal gone: the step-out alone
        // Engaged bot outside the band, goal between 9.5 and 10 on its own
        // side: walk to the goal (the straight line never meets the hold ring).
        CHECK(plan(true, 20.0f, 0.0f, 9.8f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(hx - 9.8f) < 0.01f && std::fabs(hy) < 0.01f);
        // ...and on the far side: stop where the line meets the hold ring.
        CHECK(plan(true, 20.0f, 0.0f, -9.8f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(hx - 9.5f) < 0.01f);
    }
    std::cout << "  [PASS] goal hysteresis: follow resumes only once the goal clears 10 yd\n";

    // Segment entry helper.
    {
        float hx = 0.0f, hy = 0.0f;
        CHECK(SegmentEntersCircle(20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 9.5f, hx, hy));
        CHECK(std::fabs(hx - 9.5f) < 0.01f);
        CHECK(!SegmentEntersCircle(5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 9.5f, hx, hy));    // B inside
        CHECK(!SegmentEntersCircle(20.0f, 0.0f, 12.0f, 0.0f, 0.0f, 0.0f, 9.5f, hx, hy));  // stops short
        CHECK(!SegmentEntersCircle(20.0f, 12.0f, -20.0f, 12.0f, 0.0f, 0.0f, 9.5f, hx, hy)); // misses
        CHECK(!SegmentEntersCircle(20.0f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 9.5f, hx, hy));  // circle behind
    }
    std::cout << "  [PASS] segment entry point\n";

    // Chase near a keep-out (live test 2026-10-13: a warrior on "reach melee"
    // chased a sludge to 2-4 yd). Sludge at the origin.
    {
        CreatureAuraKeepout const& sludge = *FindCreatureAuraKeepout(349, 12222);
        using Step = CreatureAuraFollowStep;
        float gx = 0.0f, gy = 0.0f, hx = 0.0f, hy = 0.0f;
        auto dist = [](float x, float y) { return std::sqrt(x * x + y * y); };

        // Goal: `distance` from the target toward the bot; melee (0) = target.
        CreatureAuraChaseGoal(20.0f, 0.0f, 0.0f, 0.0f, 0.0f, gx, gy);
        CHECK(gx == 0.0f && gy == 0.0f);
        CreatureAuraChaseGoal(30.0f, 0.0f, 0.0f, 0.0f, 22.0f, gx, gy);
        CHECK(std::fabs(gx - 22.0f) < 0.01f && std::fabs(gy) < 0.01f);
        CreatureAuraChaseGoal(5.0f, 0.0f, 0.0f, 0.0f, 22.0f, gx, gy); // already closer: stay
        CHECK(std::fabs(gx - 5.0f) < 0.01f);

        // Melee chasing the sludge itself from 20 yd: stop on the 9.5 ring,
        // then hold there; from 8.5 back off to 10; inside 8 the step-out.
        CreatureAuraChaseGoal(20.0f, 0.0f, 0.0f, 0.0f, 0.0f, gx, gy);
        CHECK(PlanCreatureAuraFollow(sludge, false, 20.0f, 0.0f, gx, gy, 0.0f, 0.0f, hx, hy) == Step::Approach);
        CHECK(std::fabs(dist(hx, hy) - 9.5f) < 0.01f);
        float const ax = hx, ay = hy;
        CHECK(PlanCreatureAuraFollow(sludge, true, ax, ay, gx, gy, 0.0f, 0.0f, hx, hy) == Step::Hold);
        CHECK(PlanCreatureAuraFollow(sludge, true, 8.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::BackOff);
        CHECK(std::fabs(dist(hx, hy) - 10.0f) < 0.01f);
        CHECK(PlanCreatureAuraFollow(sludge, true, 2.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, hx, hy) == Step::StepOut);

        // The sludge creeps after the bot it has aggro on (2 yd/s, 0.25 yd
        // per tick) while the melee keeps chasing it: never closer than 9.
        float bx = 9.5f, cx = 0.0f;
        for (int tick = 0; tick < 60; ++tick)
        {
            cx += 0.25f;
            CreatureAuraChaseGoal(bx, 0.0f, cx, 0.0f, 0.0f, gx, gy);
            Step const s = PlanCreatureAuraFollow(sludge, true, bx, 0.0f, gx, gy, cx, 0.0f, hx, hy);
            CHECK(s == Step::Hold || s == Step::BackOff);
            if (s == Step::BackOff)
                bx = hx;
            CHECK(bx - cx >= 8.74f); // a hold is re-judged every tick: at most one creep step inside 9
        }

        // A ranged chase (goal 22 yd out) and a melee chase onto a mob 12 yd
        // from the sludge are left to the normal chase.
        CreatureAuraChaseGoal(30.0f, 0.0f, 0.0f, 0.0f, 22.0f, gx, gy);
        CHECK(PlanCreatureAuraFollow(sludge, false, 30.0f, 0.0f, gx, gy, 0.0f, 0.0f, hx, hy) == Step::Follow);
        CreatureAuraChaseGoal(25.0f, 0.0f, 12.0f, 0.0f, 0.0f, gx, gy);
        CHECK(PlanCreatureAuraFollow(sludge, false, 25.0f, 0.0f, gx, gy, 0.0f, 0.0f, hx, hy) == Step::Follow);
    }
    std::cout << "  [PASS] chase: melee on a sludge stops outside the ring and kites it\n";

    // Step-out landing near other keep-outs: keep-out first, pulse relaxed.
    {
        CreatureAuraKeepout const& sludge = *FindCreatureAuraKeepout(349, 12222);
        CHECK(CreatureAuraLandingClearance(sludge, false) == sludge.keepoutRadius);
        CHECK(CreatureAuraLandingClearance(sludge, true) == sludge.pulseRadius);
        CHECK(CreatureAuraLandingClearance(sludge, true) < CreatureAuraLandingClearance(sludge, false));
    }
    std::cout << "  [PASS] step-out landing clears other keep-outs (pulse when relaxed)\n";

    // Late detection: a chase onto the sludge itself is planned from any
    // distance (the target is checked before the 15 yd list), so a melee bot
    // starting 40 yd out is sent to the 9.5 yd ring, never into the pulse.
    {
        CreatureAuraKeepout const& sludge = *FindCreatureAuraKeepout(349, 12222);
        float gx = 0.0f, gy = 0.0f, hx = 0.0f, hy = 0.0f;
        CreatureAuraChaseGoal(40.0f, 0.0f, 0.0f, 0.0f, 0.0f, gx, gy);
        CHECK(PlanCreatureAuraFollow(sludge, false, 40.0f, 0.0f, gx, gy, 0.0f, 0.0f, hx, hy) ==
            CreatureAuraFollowStep::Approach);
        CHECK(std::fabs(hx - 9.5f) < 0.01f && std::fabs(hy) < 0.01f);
    }
    std::cout << "  [PASS] chase onto a sludge from 40 yd stops on the ring\n";

    // Roles: tanks are exempt from the chase hold always, from the step-out
    // and the path hazard only in combat.
    {
        CHECK(CreatureAuraChaseExempt(true));
        CHECK(!CreatureAuraChaseExempt(false));
        CHECK(CreatureAuraCombatExempt(true, true));
        CHECK(!CreatureAuraCombatExempt(true, false)); // a following tank keeps out
        CHECK(!CreatureAuraCombatExempt(false, true));
    }
    std::cout << "  [PASS] tank exemption\n";

    // Bounded hold: set the target aside after 3 s on the same hostile
    // current target, never under a player's order, never a friend.
    {
        CHECK(!CreatureAuraSetAsideTarget(true, false, 0));
        CHECK(!CreatureAuraSetAsideTarget(true, false, kCreatureAuraChaseHoldMs - 1));
        CHECK(CreatureAuraSetAsideTarget(true, false, kCreatureAuraChaseHoldMs));
        CHECK(!CreatureAuraSetAsideTarget(true, true, 60000));  // ordered: hold, no retarget
        CHECK(!CreatureAuraSetAsideTarget(false, false, 60000)); // a heal reach on a friend
        CHECK(kCreatureAuraSetAsideMs > kCreatureAuraChaseHoldMs);
        // The set-aside book (args: live, isKeepoutCreature, isTank,
        // hitsBotInSight, stillRinged): refused until it runs out; a keep-out
        // creature even while it hits the bot or is not ringed; another mob
        // only while it still stands in a ring and does not hit the bot in
        // sight; tanks never hold one.
        CHECK(CreatureAuraSetAsideHolds(true, true, false, false, true));
        CHECK(CreatureAuraSetAsideHolds(true, true, false, true, true));   // sludge hitting the bot: still refused
        CHECK(CreatureAuraSetAsideHolds(true, true, false, false, false)); // sludge itself stays refused
        CHECK(CreatureAuraSetAsideHolds(true, false, false, false, true)); // ringed mob on the tank: refused
        CHECK(!CreatureAuraSetAsideHolds(true, false, false, false, false)); // mob left the ring: picked again at once
        CHECK(!CreatureAuraSetAsideHolds(true, false, false, true, true));   // ringed mob came out to the bot
        CHECK(!CreatureAuraSetAsideHolds(true, false, false, true, false));
        CHECK(!CreatureAuraSetAsideHolds(true, true, true, false, true));  // tanks pick it back up
        CHECK(!CreatureAuraSetAsideHolds(false, true, false, false, true)); // expired
        CHECK(!CreatureAuraSetAsideHolds(false, false, false, false, true));
    }
    std::cout << "  [PASS] bounded hold then retarget\n";

    // "Still ringed": the release ring of a listed creature (10 yd for the
    // sludge, centre measure) - the ring the chase hold lets go of.
    {
        CreatureAuraKeepout const* sludge = FindCreatureAuraKeepout(349, 12222);
        CHECK(sludge != nullptr);
        CHECK(CreatureAuraSetAsideRinged(*sludge, 0.0f));
        CHECK(CreatureAuraSetAsideRinged(*sludge, 9.5f));
        CHECK(CreatureAuraSetAsideRinged(*sludge, 10.0f));
        CHECK(!CreatureAuraSetAsideRinged(*sludge, 10.1f));
        CHECK(!CreatureAuraSetAsideRinged(*sludge, 40.0f));
        // The test uses only the creature-to-mob distance: a mob dragged 25 yd
        // from the sludge is unringed wherever the bot stands (the wiring is
        // pinned in tools/test_keepout_set_aside_wiring.py).
        CHECK(!CreatureAuraSetAsideRinged(*sludge, 25.0f));
        // A mob the hold lets go of is not held again: past the release ring
        // the chase plans Follow, engaged or not.
        float hx = 0.0f, hy = 0.0f;
        CHECK(PlanCreatureAuraFollow(*sludge, true, 30.0f, 0.0f, 10.1f, 0.0f, 0.0f, 0.0f, hx, hy)
            == CreatureAuraFollowStep::Follow);
    }
    std::cout << "  [PASS] still-ringed test matches the hold's release ring\n";

    // The chase-hold timer starts over for another target and after a pause
    // (combat ended mid-hold), so a later hold never sets a mob aside on its
    // first tick; steady ticks keep the timer.
    {
        CHECK(CreatureAuraChaseHoldRestarts(false, 0));
        CHECK(CreatureAuraChaseHoldRestarts(true, kCreatureAuraChaseHoldGapMs + 1));
        CHECK(!CreatureAuraChaseHoldRestarts(true, 0));
        CHECK(!CreatureAuraChaseHoldRestarts(true, 100));
        CHECK(!CreatureAuraChaseHoldRestarts(true, kCreatureAuraChaseHoldGapMs));
        CHECK(kCreatureAuraChaseHoldGapMs < kCreatureAuraChaseHoldMs);
        // Hold 2 s on mob A, combat ends, 20 s later the hold starts on A
        // again: the timer is new, so the first tick does not set it aside.
        unsigned since = 0, last = 0, now = 0;
        bool sameTarget = false;
        auto tick = [&](unsigned at) -> bool
        {
            now = at;
            if (CreatureAuraChaseHoldRestarts(sameTarget, now - last))
                since = now;
            sameTarget = true;
            last = now;
            return CreatureAuraSetAsideTarget(true, false, now - since);
        };
        for (unsigned t = 1000; t <= 3000; t += 100)
            CHECK(!tick(t)); // 2 s of hold: not yet
        CHECK(!tick(23000));  // the old start (1000) would set it aside here
        CHECK(!tick(24000));
        CHECK(!tick(25900));
        CHECK(tick(26000));   // 3 s of steady hold since 23000
    }
    std::cout << "  [PASS] chase-hold timer resets after a pause\n";

    // Regression (review round 2, 2026-10-13): the tank holds a mob inside a
    // sludge ring; a melee DPS on "dps assist" holds outside, sets it aside
    // after 3 s, and its next pick comes from DpsTargetValue: raid mark, then
    // the group tank's victim, then the tournament over the attackers list.
    // The first two bypass the attackers list (AttackersValue::IgnoreTarget),
    // so before the fix they handed the set-aside mob straight back: hold 3 s,
    // set aside, re-pick, all fight. Now every path asks the book
    // (AttackersValue::IsKeepoutSetAside -> CreatureAuraSetAsideHolds).
    {
        enum { kNone = 0, kRinged = 1, kOther = 2 };
        struct Run { int setAsides; int picks; int otherKills; int idleMs; };
        // gateBypassPaths: the fix. otherExists: a second mob outside any ring.
        // ringedIsSludge: the held mob is the keep-out creature itself.
        // markRinged: the ringed mob carries this bot's raid mark.
        unsigned ringLeftAtMs = ~0u; // the ringed mob stands out of every ring from this time on
        auto simulate = [&ringLeftAtMs](bool gateBypassPaths, bool otherExists, bool ringedIsSludge, bool markRinged,
            unsigned durationMs) -> Run
        {
            std::map<int, unsigned> book; // target -> set aside until (ms)
            auto refused = [&](int id, unsigned now) -> bool
            {
                auto it = book.find(id);
                if (it == book.end())
                    return false;
                // The tank holds the mob: it never hits the DPS bot.
                if (CreatureAuraSetAsideHolds(now < it->second, id == kRinged && ringedIsSludge, false, false,
                        now < ringLeftAtMs))
                    return true;
                book.erase(it);
                return false;
            };
            bool otherAlive = otherExists;
            unsigned otherHitMs = 0;
            auto pick = [&](unsigned now) -> int
            {
                if (markRinged && (!gateBypassPaths || !refused(kRinged, now)))
                    return kRinged;                       // RtiTargetValue branch
                if (!gateBypassPaths || !refused(kRinged, now))
                    return kRinged;                       // GetGroupTankTarget: the tank's victim
                if (!refused(kRinged, now))               // tournament: attackers list, IgnoreTarget
                    return kRinged;
                return otherAlive ? kOther : kNone;
            };
            Run run = { 0, 0, 0, 0 };
            int target = kNone;
            unsigned holdSince = 0;
            for (unsigned now = 0; now < durationMs; now += 100)
            {
                if (target == kNone)
                {
                    target = pick(now);
                    if (target != kNone)
                    {
                        ++run.picks;
                        holdSince = now;
                    }
                }
                if (target == kRinged)
                {
                    // The chase goal is inside the ring: hold, then set aside.
                    // Out of the ring (the tank dragged it out) it is just fought.
                    if ((ringedIsSludge || now < ringLeftAtMs) &&
                        CreatureAuraSetAsideTarget(true, false, now - holdSince))
                    {
                        book[kRinged] = now + kCreatureAuraSetAsideMs;
                        ++run.setAsides;
                        target = kNone;
                    }
                }
                else if (target == kOther)
                {
                    otherHitMs += 100;
                    if (otherHitMs >= 20000) // killed it
                    {
                        otherAlive = false;
                        ++run.otherKills;
                        target = kNone;
                    }
                }
                else
                    run.idleMs += 100;
            }
            return run;
        };

        // Before the fix: the tank's victim comes back every 3 s, all fight.
        Run const loop = simulate(false, true, false, false, 30000);
        CHECK(loop.setAsides >= 9);
        CHECK(loop.otherKills == 0);
        // Fixed: one set-aside, then the bot fights the other mob.
        Run const fixed = simulate(true, true, false, false, 30000);
        CHECK(fixed.setAsides == 1);
        CHECK(fixed.otherKills == 1);
        // The same through a raid mark, and with the sludge itself held.
        CHECK(simulate(false, true, false, true, 30000).setAsides >= 9);
        CHECK(simulate(true, true, false, true, 30000).setAsides == 1);
        CHECK(simulate(true, true, true, false, 30000).setAsides == 1);
        // No other target: the bot holds outside with no target (no pick, no
        // log line) until the set-aside runs out, then holds 3 s and sets it
        // aside again - one cycle per 33 s instead of one every 3 s.
        Run const alone = simulate(true, false, false, false, 30000);
        CHECK(alone.setAsides == 1);
        CHECK(alone.picks == 1);
        CHECK(alone.idleMs >= 26000);
        CHECK(simulate(true, false, false, false, 66000).setAsides == 2);
        CHECK(simulate(false, false, false, false, 30000).setAsides >= 9);

        // "Lift when it leaves the ring" (owner decision 2026-10-13): the
        // tank drags the mob out 10 s in; the bot picks it again at once
        // instead of idling the rest of the 30 s, and sets it aside no more.
        ringLeftAtMs = 10000;
        Run const dragged = simulate(true, false, false, false, 30000);
        CHECK(dragged.setAsides == 1);
        CHECK(dragged.picks == 2);
        CHECK(dragged.idleMs >= 6800 && dragged.idleMs <= 7200); // 3 s hold, set aside at 3 s, lifted at 10 s
        // Still ringed at 10 s (never leaves): stays set aside, as before.
        ringLeftAtMs = ~0u;
        CHECK(simulate(true, false, false, false, 30000).picks == 1);
        // The sludge itself stays refused after the ring "ends" (it is the ring).
        ringLeftAtMs = 10000;
        Run const sludge = simulate(true, false, true, false, 30000);
        CHECK(sludge.setAsides == 1);
        CHECK(sludge.picks == 1);
        // With another mob around, the lift costs nothing: it keeps fighting.
        CHECK(simulate(true, true, false, false, 30000).otherKills == 1);
        ringLeftAtMs = ~0u;
    }
    std::cout << "  [PASS] tank holds a ringed mob: the set-aside is not re-picked through assist or mark\n";

    // Gap closers and end points (Maraudon live run 2, 2026-10-13): a warrior
    // DPS charged a sludge from 21 yd, past every hold, and died in its pulse.
    {
        CreatureAuraKeepout const& row = *FindCreatureAuraKeepout(349, 12222);
        float const hazard = CreatureAuraHazardRadius(row); // 9 yd
        // End points: inside the hazard ring refused, on or past it allowed.
        CHECK(CreatureAuraEndPointBlocked(row, 0.0f));
        CHECK(CreatureAuraEndPointBlocked(row, 2.0f));  // rear flank on the sludge
        CHECK(CreatureAuraEndPointBlocked(row, 8.9f));
        CHECK(!CreatureAuraEndPointBlocked(row, hazard));
        CHECK(!CreatureAuraEndPointBlocked(row, 12.0f));

        // Landing: melee reach from the target on the bot's side.
        float lx = 0.0f, ly = 0.0f;
        CreatureAuraGapCloserLanding(21.0f, 0.0f, 0.0f, 0.0f, 2.0f, lx, ly);
        CHECK(std::fabs(lx - 2.0f) < 0.01f && std::fabs(ly) < 0.01f);
        // A charge onto a mob 3 yd from a sludge (sludge at (-3, 0)) lands
        // 5 yd from it: in the ring. A mob 15 yd from the sludge: clear.
        CHECK(CreatureAuraEndPointBlocked(row, std::fabs(lx - (-3.0f))));
        CHECK(!CreatureAuraEndPointBlocked(row, std::fabs(lx - (-15.0f))));

        // Veto: charge onto the sludge, non-tank, no order -> vetoed.
        CHECK(CreatureAuraGapCloserVetoed(true, true, false, false));
        CHECK(CreatureAuraGapCloserVetoed(true, false, false, false));
        // Charge onto a mob whose landing is in a sludge's ring -> vetoed.
        CHECK(CreatureAuraGapCloserVetoed(false, true, false, false));
        // Tank: allowed (it picks up mobs in the ring).
        CHECK(!CreatureAuraGapCloserVetoed(true, true, true, false));
        CHECK(!CreatureAuraGapCloserVetoed(false, true, true, false));
        // Explicit attack order on that target: today's behaviour.
        CHECK(!CreatureAuraGapCloserVetoed(true, true, false, true));
        CHECK(!CreatureAuraGapCloserVetoed(false, true, false, true));
        // Nothing listed nearby: allowed.
        CHECK(!CreatureAuraGapCloserVetoed(false, false, false, false));

        // Review #882: Charge from 20 yd onto a mob with a sludge 5 yd beyond
        // it. The sludge is 25 yd from the bot, outside the cached 15 yd list
        // (kMaxAoeAvoidRadiusYd); the bounded lookup around the target finds
        // it, and the landing (2 yd short of the mob) is in its ring.
        {
            float const bx = 0.0f, by = 0.0f, tx = 20.0f, ty = 0.0f, sx = 25.0f, sy = 0.0f;
            CHECK(std::hypot(sx - bx, sy - by) > kMaxAoeAvoidRadiusYd);
            float fx = 0.0f, fy = 0.0f;
            CreatureAuraGapCloserLanding(bx, by, tx, ty, 2.0f, fx, fy);
            float const landingToTarget = std::hypot(fx - tx, fy - ty);
            CHECK(std::fabs(landingToTarget - 2.0f) < 0.01f);
            // The shared hazard snapshot of the bot's cell holds it.
            CreatureAuraSnapshotCache cache;
            auto one = [&](float hx, float hy)
            {
                return [=](std::vector<CreatureAuraHazardEntry>& out, float, float)
                    { out.push_back({ 0x55ull, hx, hy, CreatureAuraHazardRadius(row) }); };
            };
            CreatureAuraHazardSnapshot const& snap =
                *CreatureAuraSnapshotCacheGet(cache, CreatureAuraSnapshotKeyFor(349, 1, bx, by), 1000, one(sx, sy));
            CHECK(std::hypot(sx - snap.cx, sy - snap.cy) <= CreatureAuraSnapshotRadius(CreatureAuraHazardRadius(row)));
            CHECK(CreatureAuraSnapshotCovers(snap, fx, fy));
            bool const ringed = CreatureAuraSnapshotRingAt(snap, fx, fy) != 0; // 7 yd
            CHECK(ringed);
            CHECK(CreatureAuraEndPointBlocked(row, std::hypot(sx - fx, sy - fy)));
            CHECK(CreatureAuraGapCloserVetoed(false, ringed, false, false)); // vetoed
            // A sludge 15 yd beyond the mob: outside its widened ring, allowed.
            CHECK(!CreatureAuraEndPointBlocked(row, std::hypot(35.0f - fx, 0.0f - fy)));
            CreatureAuraSnapshotCache farCache;
            CHECK(CreatureAuraSnapshotRingAt(*CreatureAuraSnapshotCacheGet(farCache,
                CreatureAuraSnapshotKeyFor(349, 1, bx, by), 1000, one(35.0f, 0.0f)), fx, fy) == 0);
        }
    }
    std::cout << "  [PASS] gap closers and end points stay out of the ring (tank and order exempt)\n";

    // Review #882 round 2: isUseful() runs every tick the trigger fires, so
    // the landing lookup sits behind a readiness gate and a 1 s memo.
    {
        CreatureAuraGapCloserReadiness ready;
        ready.knowsSpell = true;
        ready.rangeKnown = true;
        ready.distance = 20.0f;
        ready.minRange = 8.0f;
        ready.maxRange = 25.0f;
        CHECK(CreatureAuraGapCloserCouldFire(ready)); // Charge from 20 yd, out of combat

        CreatureAuraGapCloserReadiness r = ready;
        r.onCooldown = true;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // on cooldown: no lookup
        r = ready;
        r.distance = 30.0f;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // past 25 yd: no lookup
        r.distance = 25.0f;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // max range is exclusive, as in isPossible
        r.distance = 7.9f;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // inside the 8 yd minimum
        r.distance = 8.0f;
        CHECK(CreatureAuraGapCloserCouldFire(r));
        r = ready;
        r.nonCombatSpell = true;
        CHECK(CreatureAuraGapCloserCouldFire(r)); // Charge out of combat
        r.inCombat = true;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // Charge in combat (Arms chase): no lookup
        r.nonCombatSpell = false;
        CHECK(CreatureAuraGapCloserCouldFire(r)); // Intercept in combat
        r.powerShort = true;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // Intercept without 10 rage
        r = ready;
        r.lostControl = true;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // stunned
        r = ready;
        r.knowsSpell = false;
        CHECK(!CreatureAuraGapCloserCouldFire(r)); // not learned
        r = ready;
        r.rangeKnown = false;
        r.distance = 40.0f;
        CHECK(CreatureAuraGapCloserCouldFire(r)); // no range data: never skip the veto on range
    }
    std::cout << "  [PASS] gap-closer veto looks only when the cast could fire\n";

    // Reviews of #995: hazard snapshots shared per (map, instance, 30 yd
    // cell), at most one grid visit per cell per kCreatureAuraSnapshotMs,
    // read by every end-point and landing check.
    {
        CreatureAuraKeepout const& row = kCreatureAuraKeepouts[0];
        float const hazard = CreatureAuraHazardRadius(row);
        CHECK(CreatureAuraMaxHazardRadiusOnMap(349) == hazard); // 9 yd in Maraudon
        CHECK(CreatureAuraMaxHazardRadiusOnMap(0) == 0.0f);     // no listed row: the caller never visits
        int visits = 0;
        std::vector<CreatureAuraHazardEntry> world;
        auto fill = [&](std::vector<CreatureAuraHazardEntry>& out, float, float) { ++visits; out = world; };

        // Coverage: a bot anywhere in its cell, a point up to 30 yd from it.
        for (float bx : { 0.01f, 15.0f, 29.99f })
            for (float by : { 0.01f, 15.0f, 29.99f })
            {
                CreatureAuraSnapshotCache c;
                CreatureAuraHazardSnapshot const& sn =
                    *CreatureAuraSnapshotCacheGet(c, CreatureAuraSnapshotKeyFor(349, 1, bx, by), 100, fill);
                CHECK(sn.cx == 15.0f && sn.cy == 15.0f);
                for (float ang = 0.0f; ang < 6.3f; ang += 0.5f)
                {
                    float const px = bx + std::cos(ang) * kCreatureAuraSnapshotReachYd;
                    float const py = by + std::sin(ang) * kCreatureAuraSnapshotReachYd;
                    CHECK(CreatureAuraSnapshotCovers(sn, px, py));
                    // Any creature whose widened ring holds that point is in the visit radius.
                    float const hx = px + std::cos(ang) * (hazard + kCreatureAuraSnapshotDriftYd - 0.01f);
                    float const hy = py + std::sin(ang) * (hazard + kCreatureAuraSnapshotDriftYd - 0.01f);
                    CHECK(std::hypot(hx - sn.cx, hy - sn.cy) <= CreatureAuraSnapshotRadius(hazard));
                }
            }

        // Count: five bots of a party in one cell, each running rear flank
        // (2 points), set behind (2 tries), move out of collision (9 tries)
        // and Charge + Intercept within 500 ms: one visit.
        CreatureAuraSnapshotCache cache;
        visits = 0;
        world = { { 0x77ull, 24.0f, 0.0f, hazard } };
        float const party[5][2] = { { 1.0f, 1.0f }, { 4.0f, 2.0f }, { 2.0f, 6.0f }, { 8.0f, 3.0f }, { 5.0f, 9.0f } };
        for (int bot = 0; bot < 5; ++bot)
            for (int check = 0; check < 2 + 2 + 9 + 2; ++check)
                CreatureAuraSnapshotCacheGet(cache, CreatureAuraSnapshotKeyFor(349, 7, party[bot][0], party[bot][1]),
                    10000 + uint32_t(bot) * 90, fill);
        CHECK(visits == 1);
        CHECK(cache.refreshes == 1);
        CHECK(cache.entries.size() == 1);
        // Another instance of the same map: its own entry, its own visit.
        CreatureAuraHazardSnapshot const& inst8 =
            *CreatureAuraSnapshotCacheGet(cache, CreatureAuraSnapshotKeyFor(349, 8, 1.0f, 1.0f), 10400, fill);
        CHECK(visits == 2);
        CHECK(cache.entries.size() == 2);
        CHECK(inst8.valid);
        // The same instance again inside the window: no visit.
        CreatureAuraSnapshotCacheGet(cache, CreatureAuraSnapshotKeyFor(349, 7, 3.0f, 3.0f), 10450, fill);
        CHECK(visits == 2);
        // Another cell: its own visit.
        CreatureAuraSnapshotCacheGet(cache, CreatureAuraSnapshotKeyFor(349, 7, 31.0f, 3.0f), 10450, fill);
        CHECK(visits == 3);
        // The next window refreshes once.
        CreatureAuraSnapshotCacheGet(cache, CreatureAuraSnapshotKeyFor(349, 7, 3.0f, 3.0f), 10000 + kCreatureAuraSnapshotMs, fill);
        CreatureAuraSnapshotCacheGet(cache, CreatureAuraSnapshotKeyFor(349, 7, 4.0f, 3.0f), 10000 + kCreatureAuraSnapshotMs, fill);
        CHECK(visits == 4);
        // A bot that left the instance (through an unlisted map or not) and
        // came back to another one never reads the old instance's entry.
        world = {};
        CreatureAuraHazardSnapshot const& back =
            *CreatureAuraSnapshotCacheGet(cache, CreatureAuraSnapshotKeyFor(349, 9, 1.0f, 1.0f), 10600, fill);
        CHECK(visits == 5);
        CHECK(back.entries.empty());
        // Unused entries are pruned.
        CreatureAuraSnapshotCacheGet(cache, CreatureAuraSnapshotKeyFor(349, 9, 1.0f, 1.0f),
            10600 + kCreatureAuraSnapshotPruneMs, fill);
        CHECK(cache.entries.size() == 1);
        // Clock wrap: the entry goes stale on time, it never sticks.
        CreatureAuraSnapshotCache wrap;
        CreatureAuraHazardSnapshot const& w =
            *CreatureAuraSnapshotCacheGet(wrap, CreatureAuraSnapshotKeyFor(349, 1, 0.0f, 0.0f), 0xFFFFFF00u, fill);
        CHECK(!CreatureAuraSnapshotStale(w, 0x00000010u)); // 272 ms later
        CHECK(CreatureAuraSnapshotStale(w, 0x00000200u));  // 768 ms later
        CHECK(!CreatureAuraSnapshotCovers(CreatureAuraHazardSnapshot(), 0.0f, 0.0f));

        // An end point 24 yd away: bot at the origin, target 15 yd east,
        // set-behind end point 2 yd past it, sludge 7 yd further (24 yd from
        // the bot, outside the 15 yd list). The snapshot sees it.
        CreatureAuraSnapshotCache endCache;
        world = { { 0x77ull, 24.0f, 0.0f, hazard } };
        CreatureAuraHazardSnapshot const& endSnap =
            *CreatureAuraSnapshotCacheGet(endCache, CreatureAuraSnapshotKeyFor(349, 1, 0.0f, 0.0f), 20000, fill);
        CHECK(std::hypot(24.0f, 0.0f) > kMaxAoeAvoidRadiusYd);
        CHECK(CreatureAuraSnapshotCovers(endSnap, 17.0f, 0.0f));
        CHECK(CreatureAuraSnapshotRingAt(endSnap, 17.0f, 0.0f) == 0x77ull);
        // The target and the explicit attack target are checked live, not here.
        CHECK(CreatureAuraSnapshotRingAt(endSnap, 17.0f, 0.0f, 0x77ull) == 0);
        CHECK(CreatureAuraSnapshotRingAt(endSnap, 17.0f, 0.0f, 0, 0x77ull) == 0);

        // A hazard that walks into the landing ring inside the window: the
        // snapshot put it 11 yd from the landing (outside the 9 yd ring);
        // 400 ms later it stands 8 yd away. The snapshot is still current,
        // and the drift margin catches it.
        float const lx = 50.0f, ly = 0.0f;
        CreatureAuraSnapshotCache driftCache;
        world = { { 0x88ull, lx + 11.0f, ly, hazard } };
        CreatureAuraHazardSnapshot const& drift =
            *CreatureAuraSnapshotCacheGet(driftCache, CreatureAuraSnapshotKeyFor(349, 1, 40.0f, 0.0f), 30000, fill);
        CHECK(!CreatureAuraEndPointBlocked(row, 11.0f));          // outside the ring at the visit
        float const walked = 11.0f - (kCreatureAuraSnapshotDriftYd - 0.5f); // 8 yd 400 ms later
        CHECK(CreatureAuraEndPointBlocked(row, walked));           // inside the ring now
        CHECK(!CreatureAuraSnapshotStale(drift, 30400));           // still the same snapshot
        CHECK(CreatureAuraSnapshotRingAt(drift, lx, ly) == 0x88ull); // the margin catches it
        // Past ring + drift the creature cannot reach the ring in the window.
        CreatureAuraSnapshotCache clearCache;
        world = { { 0x89ull, lx + hazard + kCreatureAuraSnapshotDriftYd, ly, hazard } };
        CHECK(CreatureAuraSnapshotRingAt(*CreatureAuraSnapshotCacheGet(clearCache,
            CreatureAuraSnapshotKeyFor(349, 1, 40.0f, 0.0f), 30000, fill), lx, ly) == 0);
    }
    std::cout << "  [PASS] shared hazard snapshot: one visit per cell and window, per instance, drift margin\n";


    std::cout << "All creature aura keep-out tests passed.\n";
    return 0;
}
