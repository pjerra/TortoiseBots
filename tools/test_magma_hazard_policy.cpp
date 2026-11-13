// Standalone check for ai/playerbot/MagmaHazardPolicy.h (the magma guard of
// MovementAction::MoveTo2 and the formation station trigger):
//   g++ -std=c++17 -Wall -Wextra -Werror tools/test_magma_hazard_policy.cpp -o /tmp/test_magma_hazard_policy && /tmp/test_magma_hazard_policy
#include "../ai/playerbot/MagmaHazardPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ClassifyLiquidHazard;
using ai::CutPathBeforeMagma;
using ai::FollowSlotHolds;
using ai::IsMagmaHazard;
using ai::IsSlimeHazard;
using ai::KeepNavmeshZOverMagma;
using ai::KeepStairNavmeshZ;
using ai::LiquidHazard;
using ai::MagmaGuardApplies;
using ai::MagmaInteriorSamples;
using ai::MagmaPathCut;
using ai::MagmaPoint;
using ai::MagmaRefusal;
using ai::MagmaRefusalActive;
using ai::MagmaRefusalFor;
using ai::MagmaRouteRefusalFor;
using ai::MagmaNudgeBlocked;
using ai::kMagmaMaxInteriorSamples;
using ai::kMagmaRefusalSeconds;

// GridMapDefines.h, mirrored for the test (the static_asserts in
// MovementActions.cpp pin the header's copies to the core's).
constexpr std::uint32_t kMagma = 0x01, kOcean = 0x02, kSlime = 0x04, kWater = 0x08;
constexpr std::uint32_t kNoWater = 0x00, kAbove = 0x01, kWalk = 0x02, kIn = 0x04, kUnder = 0x08;

static MagmaPoint P(float x, float y = 0.0f, float z = 0.0f)
{
    MagmaPoint p;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

// A basin between x = 10 and x = 20 (the RFC trogg-cave floor, flattened).
static bool Basin(MagmaPoint const& p)
{
    return p.x >= 10.0f && p.x <= 20.0f;
}

int main()
{
    std::cout << "Starting TortoiseBots magma hazard policy tests...\n";

    // The core's in-magma rule: magma and under, in, or on its surface.
    {
        CHECK(IsMagmaHazard(kMagma, kIn));
        CHECK(IsMagmaHazard(kMagma, kUnder));
        CHECK(IsMagmaHazard(kMagma, kWalk));
        CHECK(!IsMagmaHazard(kMagma, kAbove));
        CHECK(!IsMagmaHazard(kMagma, kNoWater));
        std::cout << "  [PASS] magma under, in or on the surface is a hazard, above it is not\n";
    }

    // Other liquids are no hazard: slime does no damage in this core
    // (Undercity), water and ocean are swum.
    {
        for (std::uint32_t status : { kWalk, kIn, kUnder })
        {
            CHECK(!IsMagmaHazard(kSlime, status));
            CHECK(!IsMagmaHazard(kWater, status));
            CHECK(!IsMagmaHazard(kOcean, status));
            CHECK(!IsMagmaHazard(0, status));
        }
        std::cout << "  [PASS] slime, water, ocean and no liquid are no hazard\n";
    }

    // Round 2: slime by the same rule, for the swim hand-off routes only.
    // Ordinary moves stay magma-only, so IsMagmaHazard still says no to slime
    // (the block above); ClassifyLiquidHazard adds slime only with `withSlime`.
    {
        CHECK(IsSlimeHazard(kSlime, kIn));
        CHECK(IsSlimeHazard(kSlime, kUnder));
        CHECK(IsSlimeHazard(kSlime, kWalk));
        CHECK(!IsSlimeHazard(kSlime, kAbove));
        CHECK(!IsSlimeHazard(kSlime, kNoWater));
        CHECK(!IsSlimeHazard(kMagma, kIn));
        CHECK(!IsSlimeHazard(kWater, kIn));
        CHECK(!IsSlimeHazard(0, kIn));
        CHECK(ClassifyLiquidHazard(kSlime, kIn, true) == LiquidHazard::Slime);
        CHECK(ClassifyLiquidHazard(kSlime, kIn, false) == LiquidHazard::None);
        CHECK(ClassifyLiquidHazard(kMagma, kIn, true) == LiquidHazard::Magma);
        CHECK(ClassifyLiquidHazard(kMagma, kIn, false) == LiquidHazard::Magma);
        std::cout << "  [PASS] slime is a hazard for the hand-off only, magma for every move\n";
    }

    // The guard runs on instance maps only; the open world keeps its old
    // movement.
    {
        CHECK(MagmaGuardApplies(true));
        CHECK(!MagmaGuardApplies(false));
        std::cout << "  [PASS] the guard applies on instance maps only\n";
    }

    // Sample spacing between two path points.
    {
        CHECK(MagmaInteriorSamples(0.0f) == 0);
        CHECK(MagmaInteriorSamples(2.0f) == 0);
        CHECK(MagmaInteriorSamples(3.0f) == 1);
        CHECK(MagmaInteriorSamples(10.0f) == 4);
        CHECK(MagmaInteriorSamples(std::nanf("")) == 0);
        std::cout << "  [PASS] at most 2 yd between samples\n";
    }

    // A dry route is left alone.
    {
        MagmaPathCut c = CutPathBeforeMagma(P(0), { P(3), P(6), P(9) }, Basin);
        CHECK(!c.cut);
        std::cout << "  [PASS] a dry route is unchanged\n";
    }

    // A path point in the basin: the route ends at the point before it.
    {
        MagmaPathCut c = CutPathBeforeMagma(P(0), { P(2), P(4), P(6), P(8), P(10), P(12) }, Basin);
        CHECK(c.cut);
        CHECK(c.keepPoints == 4);
        CHECK(!c.addPoint);
        std::cout << "  [PASS] a route ends before its first magma point\n";
    }

    // The first path point is already in magma: nothing is kept (MoveTo2
    // then refuses the move).
    {
        MagmaPathCut c = CutPathBeforeMagma(P(9), { P(15), P(30) }, Basin);
        CHECK(c.cut);
        CHECK(c.keepPoints == 0);
        CHECK(!c.addPoint);  // 9 -> 15: the first sample (11) is magma, the last dry one is the bot's own spot
        std::cout << "  [PASS] magma right ahead leaves the bot where it stands\n";
    }

    // Two dry corners either side of the basin: the straight leg between
    // them crosses it, the samples find it and the route ends at the last dry
    // sample on that leg.
    {
        MagmaPathCut c = CutPathBeforeMagma(P(0), { P(4), P(30) }, Basin);
        CHECK(c.cut);
        CHECK(c.keepPoints == 1);
        CHECK(c.addPoint);
        CHECK(c.point.x < 10.0f && c.point.x >= 8.0f);
        std::cout << "  [PASS] a leg crossing magma between two dry points ends before it\n";
    }

    // Magma only at the last point (a destination the caller did not check).
    {
        MagmaPathCut c = CutPathBeforeMagma(P(0), { P(2), P(4), P(6), P(8), P(10) }, Basin);
        CHECK(c.cut);
        CHECK(c.keepPoints == 4);
        std::cout << "  [PASS] magma at the last point cuts the last point\n";
    }

    // A bot already standing in magma walks out: the leading magma run is no
    // cut, magma after dry ground is.
    {
        MagmaPathCut out = CutPathBeforeMagma(P(15), { P(18), P(22), P(26) }, Basin);
        CHECK(!out.cut);
        MagmaPathCut back = CutPathBeforeMagma(P(15), { P(22), P(24), P(16) }, Basin);
        CHECK(back.cut);
        CHECK(back.keepPoints == 2);
        CHECK(back.addPoint);
        CHECK(back.point.x > 20.0f);
        std::cout << "  [PASS] a bot in magma walks out, and stops before magma again\n";
    }

    // A route that never leaves magma is left alone (no dry spot to stop at).
    {
        MagmaPathCut c = CutPathBeforeMagma(P(12), { P(14), P(18) }, Basin);
        CHECK(!c.cut);
        std::cout << "  [PASS] a route that never leaves magma is unchanged\n";
    }

    // Outside a basin (no magma anywhere) nothing changes, however long.
    {
        std::vector<MagmaPoint> path;
        for (int i = 1; i <= 40; ++i)
            path.push_back(P(-5.0f * i, 1.0f, 2.0f));
        MagmaPathCut c = CutPathBeforeMagma(P(0), path, Basin);
        CHECK(!c.cut);
        std::cout << "  [PASS] a route away from magma is unchanged\n";
    }

    // The sample cap: a long dry route makes at most 1 (start) + 48
    // (between points) + 1 per path point lookups.
    {
        std::vector<MagmaPoint> path;
        for (int i = 1; i <= 10; ++i)
            path.push_back(P(-30.0f * i));
        std::size_t calls = 0;
        MagmaPathCut c = CutPathBeforeMagma(P(0), path, [&calls](MagmaPoint const&) { ++calls; return false; });
        CHECK(!c.cut);
        CHECK(kMagmaMaxInteriorSamples == 48);
        CHECK(calls == 1 + 48 + path.size());
        std::cout << "  [PASS] at most 48 samples between path points per route\n";
    }

    // Past the cap only path points are looked at: the samples nearest the
    // bot keep their 2 yd spacing and still find a crossing there; a crossing
    // between two dry points beyond the cap is not seen (a magma path point
    // there still is).
    {
        // 0 -> 120 is 59 samples; the basin at 10..20 is inside the first 48.
        MagmaPathCut near = CutPathBeforeMagma(P(0), { P(120) }, Basin);
        CHECK(near.cut && near.addPoint && near.point.x >= 8.0f && near.point.x < 10.0f);
        // A basin far out: 0 -> 100 uses 48 samples (to 96 yd)... then 100 -> 140 has none.
        auto farBasin = [](MagmaPoint const& p) { return p.x >= 110.0f && p.x <= 120.0f; };
        MagmaPathCut far = CutPathBeforeMagma(P(0), { P(100), P(140) }, farBasin);
        CHECK(!far.cut);
        MagmaPathCut farPoint = CutPathBeforeMagma(P(0), { P(100), P(115), P(140) }, farBasin);
        CHECK(farPoint.cut && farPoint.keepPoints == 1 && !farPoint.addPoint);
        std::cout << "  [PASS] past the cap only path points are checked\n";
    }

    // The refusal memory: the same destination (same map, same 2 yd cell) is
    // refused again from memory until the time runs out; another cell,
    // another map or a later time looks again.
    {
        MagmaRefusal none;
        CHECK(!MagmaRefusalActive(none, 389, -205.5f, -16.4f, -65.7f, 0.0f, 0.0f, 0.0f, 100));
        MagmaRefusal r = MagmaRefusalFor(389, -205.5f, -16.4f, -65.7f, 100);
        CHECK(MagmaRefusalActive(r, 389, -205.5f, -16.4f, -65.7f, 0.0f, 0.0f, 0.0f, 100));
        CHECK(MagmaRefusalActive(r, 389, -205.1f, -16.1f, -65.2f, 0.0f, 0.0f, 0.0f, 100 + kMagmaRefusalSeconds - 1));
        CHECK(!MagmaRefusalActive(r, 389, -205.5f, -16.4f, -65.7f, 0.0f, 0.0f, 0.0f, 100 + kMagmaRefusalSeconds));
        CHECK(!MagmaRefusalActive(r, 389, -201.0f, -16.4f, -65.7f, 0.0f, 0.0f, 0.0f, 101));
        CHECK(!MagmaRefusalActive(r, 389, -205.5f, -12.0f, -65.7f, 0.0f, 0.0f, 0.0f, 101));
        CHECK(!MagmaRefusalActive(r, 389, -205.5f, -16.4f, -61.7f, 0.0f, 0.0f, 0.0f, 101));
        CHECK(!MagmaRefusalActive(r, 1, -205.5f, -16.4f, -65.7f, 0.0f, 0.0f, 0.0f, 101));
        MagmaRefusal cleared = r;
        cleared.set = false;
        CHECK(!MagmaRefusalActive(cleared, 389, -205.5f, -16.4f, -65.7f, 0.0f, 0.0f, 0.0f, 101));
        std::cout << "  [PASS] a refused destination is refused from memory for 10 s\n";
    }

    // An end point in magma is refused from anywhere; a route refusal only
    // from the same 4 yd origin cell (a knockback to the safe side, or any
    // step to a new cell, looks again at once).
    {
        MagmaRefusal end = MagmaRefusalFor(389, -205.5f, -16.4f, -65.7f, 100);
        CHECK(MagmaRefusalActive(end, 389, -205.5f, -16.4f, -65.7f, -209.7f, -4.4f, -61.7f, 101));
        CHECK(MagmaRefusalActive(end, 389, -205.5f, -16.4f, -65.7f, -180.0f, 0.0f, -60.0f, 101));
        MagmaRefusal route = MagmaRouteRefusalFor(389, -205.5f, -16.4f, -65.7f, -209.7f, -4.4f, -61.7f, 100);
        CHECK(MagmaRefusalActive(route, 389, -205.5f, -16.4f, -65.7f, -209.7f, -4.4f, -61.7f, 101));
        CHECK(MagmaRefusalActive(route, 389, -205.5f, -16.4f, -65.7f, -210.5f, -5.5f, -61.0f, 101)); // same cell
        CHECK(!MagmaRefusalActive(route, 389, -205.5f, -16.4f, -65.7f, -213.0f, -4.4f, -61.7f, 101)); // x cell
        CHECK(!MagmaRefusalActive(route, 389, -205.5f, -16.4f, -65.7f, -209.7f, -0.5f, -61.7f, 101)); // y cell
        CHECK(!MagmaRefusalActive(route, 389, -205.5f, -16.4f, -65.7f, -209.7f, -4.4f, -66.5f, 101)); // z cell
        CHECK(!MagmaRefusalActive(route, 389, -205.5f, -16.4f, -65.7f, -209.7f, -4.4f, -61.7f, 110)); // expired
        CHECK(!MagmaRefusalActive(route, 389, -195.0f, -16.4f, -65.7f, -209.7f, -4.4f, -61.7f, 101)); // other dest
        std::cout << "  [PASS] a route refusal holds only from the same origin cell\n";
    }

    // The task walk nudge: on an instance map it waits after a magma refusal
    // still in its window, or when its standing point is in magma.
    {
        MagmaRefusal r = MagmaRefusalFor(389, -205.5f, -16.4f, -65.7f, 100);
        CHECK(MagmaNudgeBlocked(true, r, 389, 101, false));
        CHECK(!MagmaNudgeBlocked(true, r, 389, 110, false));         // window over
        // Codex review 2026-10-23: a magma refusal, then a later try that failed for another
        // reason (MoveTo2 resets the last-try reason), then the nudge: still blocked by the
        // memory alone (the call takes no last-try reason any more).
        CHECK(MagmaNudgeBlocked(true, r, 389, 105, false));
        CHECK(!MagmaNudgeBlocked(true, r, 1, 101, false));           // the memory is of another map
        CHECK(!MagmaNudgeBlocked(true, MagmaRefusal(), 389, 101, false)); // nothing refused
        MagmaRefusal cleared = r;
        cleared.set = false;
        CHECK(!MagmaNudgeBlocked(true, cleared, 389, 101, false));   // a cleared memory
        CHECK(MagmaNudgeBlocked(true, MagmaRefusal(), 389, 101, true));  // the stand is in magma
        CHECK(!MagmaNudgeBlocked(false, r, 389, 101, true));         // open world: unchanged
        std::cout << "  [PASS] the task walk nudge waits after a magma refusal\n";
    }
    // the core's UpdateAllowedPositionZ can lower a navmesh path point into WMO
    // magma while the navmesh floor is dry (Dragonmaw Retreat r39s2/r41s2:
    // navmesh 72.6, clamp 70.25 in lava). Keep the navmesh z only when the
    // core's collision (vmap-only, no .map terrain, no dynamic tree) height
    // query confirms a real floor at the navmesh point; a bridged/eroded
    // navmesh point over an open hole (or a polygon a little above open lava)
    // reads dry but has no floor, so the bot would float or walk on the lava
    // surface. Round-1 lead decision: restore only on `floorConfirmed`.
    {
        int kept = 0;
        for (int guard = 0; guard <= 1; ++guard)
            for (int lowered = 0; lowered <= 1; ++lowered)
                for (int clamped = 0; clamped <= 1; ++clamped)
                    for (int navmesh = 0; navmesh <= 1; ++navmesh)
                        for (int floorOk = 0; floorOk <= 1; ++floorOk)
                        {
                            bool const want = guard && lowered && clamped && !navmesh && floorOk;
                            bool const got = KeepNavmeshZOverMagma(guard, lowered, clamped, navmesh, floorOk);
                            if (got)
                                ++kept;
                            if (want != got)
                            {
                                std::cerr << "KeepNavmeshZOverMagma(" << guard << "," << lowered << "," << clamped << ","
                                    << navmesh << "," << floorOk << ") = " << got << ", want " << want << "\n";
                            }
                            CHECK(got == want);
                        }
        CHECK(kept == 1);
        CHECK(KeepNavmeshZOverMagma(true, true, true, false, true)); // lowered into magma, navmesh floor dry, floor confirmed: keep the floor
        CHECK(!KeepNavmeshZOverMagma(true, true, true, false, false)); // same point, no collision floor (hole above lava): keep the clamp
        std::cout << "  [PASS] the clamp keeps the navmesh floor over magma only on a confirmed floor\n";
    }

    // Slime reads by the same hazard rule (type flag 0x04, same status bits):
    // a clamp sunk in slime is no safer a floor signal than one sunk in magma.
    {
        for (std::uint32_t status : { kWalk, kIn, kUnder })
        {
            CHECK(IsSlimeHazard(kSlime, status));
            CHECK(!IsSlimeHazard(kMagma, status));
            CHECK(!IsSlimeHazard(kWater, status));
            CHECK(!IsSlimeHazard(kOcean, status));
            CHECK(!IsSlimeHazard(0, status));
        }
        CHECK(!IsSlimeHazard(kSlime, kAbove));
        CHECK(!IsSlimeHazard(kSlime, kNoWater));
        std::cout << "  [PASS] slime under, in or on its surface counts, other liquids do not\n";
    }

    // The stair-workaround gate (round 2): the big-drop restore keeps the
    // navmesh z only when the ORIGINAL clamp is neither in magma nor in slime.
    {
        int kept = 0;
        for (int lowered = 0; lowered <= 1; ++lowered)
            for (int bigDrop = 0; bigDrop <= 1; ++bigDrop)
                for (int wet = 0; wet <= 1; ++wet)
                {
                    bool const want = lowered && bigDrop && !wet;
                    bool const got = KeepStairNavmeshZ(lowered, bigDrop, wet);
                    if (got)
                        ++kept;
                    if (want != got)
                    {
                        std::cerr << "KeepStairNavmeshZ(" << lowered << "," << bigDrop << ","
                            << wet << ") = " << got << ", want " << want << "\n";
                    }
                    CHECK(got == want);
                }
        CHECK(kept == 1);
        CHECK(KeepStairNavmeshZ(true, true, false)); // dry crypt-stair fallback: unchanged behaviour
        CHECK(!KeepStairNavmeshZ(true, true, true)); // big drop into magma/slime: the confirmed-floor path decides
        std::cout << "  [PASS] the stair workaround restores a big drop only when the clamp is dry\n";
    }

    // Round-2 lead cases: restore needs the ORIGINAL clamp and a confirmed
    // floor, at both drop sizes. The big-drop cases pin the round-2 bypass:
    // the stair workaround must not restore a wet big drop blindly.
    {
        // Unconfirmed hole, small (2.35 yd) drop into magma, no vmap floor:
        // NOT restored.
        CHECK(!KeepNavmeshZOverMagma(true, true, true, false, false));
        // Unconfirmed hole, big (6 yd) drop into magma, no vmap floor: the
        // stair gate refuses and the magma guard has no floor — NOT restored.
        CHECK(!KeepStairNavmeshZ(true, true, true));
        CHECK(!KeepNavmeshZOverMagma(true, true, true, false, false));
        // Confirmed thin floor (vmap hit within 1.0 yd), small drop: restored.
        CHECK(KeepNavmeshZOverMagma(true, true, true, false, true));
        // Confirmed floor under a big drop into magma (deep lava bed under a
        // high deck the clamp fell through to): the magma guard restores on
        // the confirmed floor even though the stair gate refused.
        CHECK(!KeepStairNavmeshZ(true, true, true));
        CHECK(KeepNavmeshZOverMagma(true, true, true, false, true));
        std::cout << "  [PASS] unconfirmed holes stay clamped at any drop size, confirmed floors restore\n";
    }


    // The follow slot / chase goal hold (RFC first-bridge deaths 2026-11-13):
    // the follow or chase generator is never handed a goal in magma, and a
    // route into magma holds it too; off an instance map (guard off) nothing
    // is held, and a magma slot with no magma in the route still holds.
    {
        int held = 0;
        for (int guard = 0; guard <= 1; ++guard)
            for (int slot = 0; slot <= 1; ++slot)
                for (int route = 0; route <= 1; ++route)
                {
                    bool const want = guard && (slot || route);
                    bool const got = FollowSlotHolds(guard, slot, route);
                    if (got)
                        ++held;
                    if (want != got)
                    {
                        std::cerr << "FollowSlotHolds(" << guard << "," << slot << ","
                            << route << ") = " << got << ", want " << want << "\n";
                    }
                    CHECK(got == want);
                }
        CHECK(held == 3);
        CHECK(FollowSlotHolds(true, true, false));   // the Ragefire shelf: the slot itself is under the lava
        CHECK(FollowSlotHolds(true, false, true));   // a dry slot across a lava strip: the route walks in
        CHECK(!FollowSlotHolds(false, true, true));  // the guard does not apply on this map
        CHECK(!FollowSlotHolds(true, false, false)); // a dry slot with a dry route: follow normally
        std::cout << "  [PASS] a follow or chase goal in magma holds, or its route into magma\n";
    }

    std::cout << "All magma hazard policy tests passed.\n";
    return 0;
}
