// R6 (review items 2, 3, 7): the run glue's liquid rule (forever/src/host/run/TbRunLiquid.h), core-free.
//   g++ -std=c++17 -Wall -Wextra -I. forever/tools/test_run_liquid.cpp -o /tmp/t && /tmp/t
// Each case names what it would have been on the code before the fix.
#include "forever/src/host/run/TbRunLiquid.h"
#include "runtime/DungeonRunPolicy.h"

#include <cstdio>
#include <vector>

namespace L = TortoiseBots::run::liquid;
namespace DR = TortoiseBots::DungeonRun;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

// The rule the facts and the stand search used before the fix: the status compared for equality, the level never read.
static bool OldRule(std::uint32_t status) { return status == 0x04 || status == 0x08; }

struct P { float x, y, z; };

int main()
{
    // item 2: ForeverCore ORs OCEAN_FLOOR (0x10) into the status near a floor: a bot standing on the floor of a lava pool reads 0x04|0x10.
    CHECK(L::PointRefused(0x04 | 0x10, 5.0f, 0.0f));          // new: refused
    CHECK(!OldRule(0x04 | 0x10));                             // old: passed (the bug)
    CHECK(L::PointRefused(0x08 | 0x10, 5.0f, 0.0f));          // under water on the floor
    CHECK(L::PointRefused(0x04, 5.0f, 0.0f) && L::PointRefused(0x08, 5.0f, 0.0f));
    CHECK(!L::PointRefused(0, 99.0f, 0.0f));                  // no liquid: the level is not read

    // item 3: a path laid ON the lava surface reads ABOVE_WATER (0x01); the level rule refuses it at the point's own z (the old read was 0.5 yd up).
    CHECK(L::PointRefused(0x01, 10.0f, 10.0f));               // surface exactly at z
    CHECK(L::PointRefused(0x01, 9.5f, 10.0f));                // 0.5 yd under z: within the 0.6 slack
    CHECK(!L::PointRefused(0x01, 9.3f, 10.0f));               // 0.7 yd under: dry enough
    CHECK(L::PointRefused(0x02, 10.2f, 10.0f));               // walking on it
    CHECK(!L::PointRefused(0x01, 2.0f, 10.0f));               // lava far below the walk: fine
    CHECK(!OldRule(0x01));                                    // old: ABOVE_WATER never refused

    // item 3 and 7: a lava strip BETWEEN two dry ends. The strip is 2 yd wide at x 10..12; the line runs x 0 -> 20 at z 0.
    auto strip = [](float x, float, float) { return x >= 10.0f && x <= 12.0f; };
    CHECK(L::SegmentRefused(0, 0, 0, 20, 0, 0, strip));       // sampled every <= 1.5 yd: caught
    CHECK(!strip(0, 0, 0) && !strip(20, 0, 0));               // an end-point-only test (the old one) sees nothing
    CHECK(!L::SegmentRefused(0, 0, 0, 9.5f, 0, 0, strip));    // a line that stops before the strip is clear
    CHECK(L::SegmentRefused(0, 0, 0, 10.5f, 0, 0, strip));    // ... and one that ends inside it is not
    // a long line stays within the sample bound: 100 yd = 67 samples + 1
    int calls = 0;
    L::SegmentRefused(0, 0, 0, 100, 0, 0, [&calls](float, float, float) { ++calls; return false; });
    CHECK(calls == 68);
    int zero = 0;                                             // a zero-length line still asks its point twice at most
    L::SegmentRefused(5, 5, 5, 5, 5, 5, [&zero](float, float, float) { ++zero; return false; });
    CHECK(zero >= 1 && zero <= 2);

    // a path: vertices and legs
    std::vector<P> path = { {0, 0, 0}, {8, 0, 0}, {8, 10, 0}, {20, 10, 0} };
    auto lavaAt = [](float x, float y, float) { return x >= 13.0f && x <= 14.0f && y > 9.0f && y < 11.0f; };
    CHECK(L::PathRefused(path, path.size(), lavaAt));         // the last leg crosses it though no vertex is in it
    auto none = [](float, float, float) { return false; };
    CHECK(!L::PathRefused(path, path.size(), none));
    std::vector<P> single = { {1, 2, 3} };
    CHECK(L::PathRefused(single, 1, [](float, float, float) { return true; }));

    // round 2: the move rule. A mover that starts in liquid may leave it, only as one contiguous wet prefix; the destination is always dry.
    // Lines along x at z 0; "wet" is a predicate on x.
    auto wetUntil = [](float edge) { return [edge](float x, float, float) { return x < edge; }; };          // wet for x < edge
    // wet -> dry: starts in a pool 0..6, walks out to 20: accepted
    CHECK(!L::SegmentPrefixRefused(0, 0, 0, 20, 0, 0, wetUntil(6.0f)));
    // wet -> wet: the whole line submerged (pool 0..30), the end in liquid: refused
    CHECK(L::SegmentPrefixRefused(0, 0, 0, 20, 0, 0, wetUntil(30.0f)));
    // wet -> dry -> wet: out of the pool, then across another lava area 12..14: refused
    auto twoPools = [](float x, float, float) { return x < 6.0f || (x >= 12.0f && x <= 14.0f); };
    CHECK(L::SegmentPrefixRefused(0, 0, 0, 20, 0, 0, twoPools));
    // wet destination from a dry origin: refused (the end sample is wet)
    auto endWet = [](float x, float, float) { return x >= 18.0f; };
    CHECK(L::SegmentPrefixRefused(0, 0, 0, 20, 0, 0, endWet));
    // dry -> dry stays accepted; dry origin crossing a strip refused (empty prefix)
    CHECK(!L::SegmentPrefixRefused(0, 0, 0, 20, 0, 0, [](float, float, float) { return false; }));
    CHECK(L::SegmentPrefixRefused(0, 0, 0, 20, 0, 0, strip));
    // the same rule over a path (vertices and legs in order)
    std::vector<P> out = { {0, 0, 0}, {8, 0, 0}, {20, 0, 0} };
    CHECK(!L::PathPrefixRefused(out, out.size(), wetUntil(6.0f)));
    CHECK(L::PathPrefixRefused(out, out.size(), wetUntil(30.0f)));
    CHECK(L::PathPrefixRefused(out, out.size(), twoPools));
    CHECK(L::PathPrefixRefused(out, out.size(), endWet));
    // the OLD exemption (clear everything when the origin is wet) would have accepted the three refused cases
    auto oldExempt = [](bool originWet, bool refusedOnWay) { return originWet ? false : refusedOnWay; };
    CHECK(!oldExempt(true, true));

    // ---- wading (ai::kPlanFlagWadeOk): a row's own hop may cross water up to kWadeDepthYd deep, never swimming water ----
    // The point rule, in the order the core's liquid read gives it (status, level, depth_level = the ground under the liquid).
    CHECK(L::kWadeDepthYd == 1.5f);
    CHECK(L::WadePointOf(0, 99.0f, 0.0f, 0.0f) == L::WadeWay::Dry);                 // no liquid
    CHECK(L::WadePointOf(0x04, 10.4f, 9.5f, 10.0f) == L::WadeWay::Shallow);         // 0.4 yd deep: a wade
    CHECK(L::WadePointOf(0x04, 10.4f, 8.8f, 10.0f) == L::WadeWay::Refused);         // 1.6 yd deep: past the ceiling, swimming
    CHECK(L::WadePointOf(0x04, 10.4f, 8.9f, 10.0f) == L::WadeWay::Shallow);         // exactly 1.5 yd: the ceiling itself wades
    CHECK(L::WadePointOf(0x04, 10.4f, 7.0f, 10.0f) == L::WadeWay::Refused);         // 3.4 yd deep
    CHECK(L::WadePointOf(0x08 | 0x10, 5.0f, 4.0f, 0.0f) == L::WadeWay::Refused);    // under water on the floor: swimming
    CHECK(L::WadePointOf(0x01, 9.3f, 8.0f, 10.0f) == L::WadeWay::Dry);              // surface 0.7 below the walk: dry enough
    CHECK(L::WadePointOf(0x01, 9.7f, 8.2f, 10.0f) == L::WadeWay::Shallow);          // within the 0.6 slack, 1.5 deep
    CHECK(L::WadePointOf(0x02, 10.2f, 9.1f, 10.0f) == L::WadeWay::Shallow);         // walking on it, shallow
    // The old rule on the same shallow point refuses: that is what a row WITHOUT the flag still gets (the RFC pause).
    CHECK(L::PointRefused(0x04, 10.4f, 10.0f));
    // A hop across a shallow strip: allowed with the flag, and the same hop without the flag pauses (the exact pair the executor
    // chooses between: PathWadeRefused for a wade row, PathPrefixRefused for every other row).
    std::vector<P> crossing = { {0, 0, 0}, {20, 0, 0} };
    auto shallowStrip = [](float x, float, float) { return x >= 8.0f && x <= 12.0f ? L::WadeWay::Shallow : L::WadeWay::Dry; };
    auto deepStrip = [](float x, float, float) { return x >= 8.0f && x <= 12.0f ? L::WadeWay::Refused : L::WadeWay::Dry; };
    CHECK(!L::PathWadeRefused(crossing, crossing.size(), shallowStrip));
    CHECK(L::PathWadeRefused(crossing, crossing.size(), deepStrip));
    auto wetStrip = [](float x, float, float) { return x >= 8.0f && x <= 12.0f; };
    CHECK(L::PathPrefixRefused(crossing, crossing.size(), wetStrip));              // flagless: the pause every other plan keeps
    CHECK(!L::PathPrefixRefused(crossing, crossing.size(), [](float, float, float) { return false; }));
    // The executor's own choice, both ways on the same shallow hop: a row without the wade bit pauses, the row with it crosses.
    auto hopRefused = [&](bool wadeRow) {
        return wadeRow ? L::PathWadeRefused(crossing, crossing.size(), shallowStrip)
                       : L::PathPrefixRefused(crossing, crossing.size(), wetStrip);
    };
    CHECK(hopRefused(false));                                                     // no wade bit: "crosses or ends in liquid", the pause
    CHECK(!hopRefused(true));                                                     // wade bit and a shallow hop: walked
    // A wet destination (the shallows themselves): the wade rule's destination sample is read the same way.
    auto wetEnd = [](float x, float, float) { return x >= 18.0f ? L::WadeWay::Shallow : L::WadeWay::Dry; };
    CHECK(!L::PathWadeRefused(crossing, crossing.size(), wetEnd));
    auto swimmingEnd = [](float x, float, float) { return x >= 18.0f ? L::WadeWay::Refused : L::WadeWay::Dry; };
    CHECK(L::PathWadeRefused(crossing, crossing.size(), swimmingEnd));
    // The samples are the move rule's: a short strip between two dry ends is caught (every <= 1.5 yd), as is a one-point path.
    std::vector<P> narrow = { {0, 0, 0}, {20, 0, 0} };
    auto narrowDeep = [](float x, float, float) { return x > 10.5f && x < 11.5f ? L::WadeWay::Refused : L::WadeWay::Dry; };
    CHECK(L::PathWadeRefused(narrow, narrow.size(), narrowDeep));
    std::vector<P> one = { {1, 2, 3} };
    CHECK(!L::PathWadeRefused(one, one.size(), [](float, float, float) { return L::WadeWay::Shallow; }));
    CHECK(L::PathWadeRefused(one, one.size(), [](float, float, float) { return L::WadeWay::Refused; }));
    // The hop's MIDDLE point (a path vertex between dry ends), the point the live river hop is walked in parts through: shallow
    // passes with the wade bit, deep refuses, and shallow without the bit still pauses with the RFC rule.
    std::vector<P> middles = { {0, 0, 0}, {10, 0, 0}, {20, 0, 0} };
    auto midShallow = [](float x, float, float) { return x > 9.0f && x < 11.0f ? L::WadeWay::Shallow : L::WadeWay::Dry; };
    auto midDeep = [](float x, float, float) { return x > 9.0f && x < 11.0f ? L::WadeWay::Refused : L::WadeWay::Dry; };
    CHECK(!L::PathWadeRefused(middles, middles.size(), midShallow));             // flagged hop, shallow middle: walked
    CHECK(L::PathWadeRefused(middles, middles.size(), midDeep));                 // flagged hop, deep middle: pauses
    CHECK(L::PathPrefixRefused(middles, middles.size(), wetStrip));              // flagless hop over the same shallow water: pauses
    // The log's kept sample is the deepest wet one (a refused sample wins over a wading one, deeper within the same kind).
    L::WadeSample trace;
    trace.Keep(L::WadeWay::Dry, 0, 0.0f, 1.0f, 1.0f, 1.0f);
    trace.Keep(L::WadeWay::Shallow, 0x01, 0.3f, 10.0f, 0.0f, 0.0f);
    trace.Keep(L::WadeWay::Shallow, 0x04, 1.1f, 11.0f, 0.0f, 0.0f);
    CHECK(trace.samples == 3 && trace.wetSamples == 2 && !trace.refused && trace.depth == 1.1f && trace.status == 0x04);
    trace.Keep(L::WadeWay::Refused, 0x08, 2.0f, 12.0f, 0.0f, 0.0f);
    CHECK(trace.refused && trace.depth == 2.0f && trace.x == 12.0f);
    trace.Keep(L::WadeWay::Refused, 0x04, 1.9f, 13.0f, 0.0f, 0.0f);
    CHECK(trace.refused && trace.depth == 2.0f && trace.x == 12.0f);            // a shallower refusal does not replace it

    // ---- a move the run makes on its own inside a leg (a regroup's gather, an approach stand, the return to the plan
    // point, the follow-up after a pull): it is no point of a row's own walk, so DungeonRun::MoveMayWade adds the plan's
    // wet areas (kWadeAreaYd of a flagged row's own hop) and the executor then asks the wade rule above. The order's
    // origin flag (Party::fromRun) is the judge: only a move the run issued for itself may wade. Live case: the run-3
    // pause "cmd move -37.2 291.3 refused: crosses or ends in liquid" after the Druid fight, the run's return from the
    // graveyard across the shallow river hop that a walked leg (4) flags, on step 52 of leg 5.
    ai::DungeonPlan plan;
    auto row = [&plan](std::uint8_t leg, std::uint16_t seq, ai::PlanStepKind kind, float x, float y, float z, std::uint32_t flags)
    {
        ai::PlanStep s;
        s.leg = leg;
        s.seq = seq;
        s.kind = kind;
        s.x = x;
        s.y = y;
        s.z = z;
        s.flags = flags;
        plan.steps.push_back(s);
    };
    row(0, 0, ai::PlanStepKind::Move, -3.641f, 229.268f, -106.189f, 0);        // the row before the river hop
    row(0, 1, ai::PlanStepKind::Pull, -61.367f, 323.721f, -106.561f, ai::kPlanFlagWadeOk);   // the first river hop (flagged)
    row(0, 2, ai::PlanStepKind::Pull, -63.800f, 417.300f, -106.700f, 0);       // a dry row of the same leg
    row(1, 0, ai::PlanStepKind::Move, -21.600f, 251.400f, -104.200f, 0);       // the next leg's own (dry) move: the regroup's leg
    DR::Run run;
    run.plan = &plan;
    run.step = 2;                                                              // after the fight on the dry row: a regroup
    CHECK(run.phase != DR::Phase::Walk);
    // The point the live regroup move was refused on: ~6 yd off the flagged hop, inside the wet area.
    CHECK(DR::PlanWetAreaAt(run, -37.2f, 291.3f));
    // A run-issued move wades; the same move typed by a player keeps the RFC rule (the origin flag, not the text).
    CHECK(DR::MoveMayWade(true, run, plan.steps[run.step], -37.2f, 291.3f));
    CHECK(!DR::MoveMayWade(false, run, plan.steps[run.step], -37.2f, 291.3f));
    std::vector<P> regroup = { {-21.6f, 251.4f, -104.2f}, {-37.2f, 291.3f, -104.8f} };
    auto shallow = [](float, float, float) { return L::WadeWay::Shallow; };
    auto deep = [](float, float, float) { return L::WadeWay::Refused; };
    CHECK(!L::PathWadeRefused(regroup, regroup.size(), shallow));              // the regroup move across shallow water passes
    CHECK(L::PathPrefixRefused(regroup, regroup.size(), [](float, float, float) { return true; }));   // the human `cmd move` pauses
    // The live case's cross-leg hop: step 52 is leg 5, the river hop flagged in leg 4. The plan's wet area covers it (the
    // 12 yd area of any flagged row), so the run's own return move wades; a player's still keeps the RFC rule.
    run.step = 3;
    CHECK(plan.steps[run.step].leg == 1);
    CHECK(DR::PlanWetAreaAt(run, -37.2f, 291.3f));
    CHECK(DR::MoveMayWade(true, run, plan.steps[run.step], -37.2f, 291.3f));
    CHECK(!DR::MoveMayWade(false, run, plan.steps[run.step], -37.2f, 291.3f));
    // The same move on a dry plan (the bit gone): no wet area, the run's own move keeps the RFC rule.
    CHECK(!DR::PlanWetAreaAt(run, -63.8f, 417.3f));                             // the dry end: never in a wet area
    plan.steps[1].flags = 0;
    CHECK(!DR::MoveMayWade(true, run, plan.steps[run.step], -37.2f, 291.3f));
    CHECK(L::PathPrefixRefused(regroup, regroup.size(), [](float, float, float) { return true; }));   // the RFC pause
    // Deep water always pauses, permission or not (the core's measured depth is the judge).
    plan.steps[1].flags = ai::kPlanFlagWadeOk;
    CHECK(DR::MoveMayWade(true, run, plan.steps[run.step], -37.2f, 291.3f));
    CHECK(L::PathWadeRefused(regroup, regroup.size(), deep));
    // A point outside every wet area keeps the rule even with the bit on a row.
    CHECK(!DR::PlanWetAreaAt(run, -120.0f, 40.0f));

    if (failures)
        return 1;
    std::printf("RESULT: PASS (test_run_liquid)\n");
    return 0;
}
