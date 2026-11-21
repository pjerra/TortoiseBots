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

    // ---- wading: a run-issued move may cross water up to kSwimDepthYd deep, never swimming water ----
    // The point rule, in the order the core's liquid read gives it (status, level, depth_level = the ground under the liquid).
    CHECK(L::kSwimDepthYd == 2.03128f);                                           // ForeverCore's UNDER_WATER band (collision height)
    std::uint32_t const water = L::kWadeTypeWater;
    CHECK(L::WadePointOf(0, 99.0f, 0.0f, 0.0f, water) == L::WadeWay::Dry);          // no liquid
    CHECK(L::WadePointOf(0x04, 10.4f, 9.5f, 10.0f, water) == L::WadeWay::Shallow);  // 0.9 yd deep: a walk
    CHECK(L::WadePointOf(0x04, 10.4f, 9.1f, 10.0f, water) == L::WadeWay::Shallow);  // 1.3 yd deep: still a walk
    CHECK(L::WadePointOf(0x04, 10.4f, 8.1f, 10.0f, water) == L::WadeWay::Refused);  // 2.3 yd deep: past the swim band
    CHECK(L::WadePointOf(0x04, 10.4f, 8.37f, 10.0f, water) == L::WadeWay::Shallow); // 2.03 yd: just inside the band
    CHECK(L::WadePointOf(0x04, 10.4f, 8.36f, 10.0f, water) == L::WadeWay::Refused); // a hair past it: swim water
    CHECK(L::WadePointOf(0x04, 10.4f, 7.0f, 10.0f, water) == L::WadeWay::Refused);  // 3.4 yd deep
    CHECK(L::WadePointOf(0x08 | 0x10, 5.0f, 4.0f, 0.0f, water) == L::WadeWay::Refused); // under water on the floor: swimming
    CHECK(L::WadePointOf(0x01, 9.3f, 8.0f, 10.0f, water) == L::WadeWay::Dry);       // surface 0.7 below the walk: dry enough
    CHECK(L::WadePointOf(0x01, 9.7f, 8.2f, 10.0f, water) == L::WadeWay::Shallow);   // within the 0.6 slack, 1.5 deep
    CHECK(L::WadePointOf(0x02, 10.2f, 9.1f, 10.0f, L::kWadeTypeOcean) == L::WadeWay::Shallow); // ocean water wades
    // Lava, slime and dark water never wade, however shallow (the lava refusal RFC relies on stays); liquid far below is dry.
    CHECK(L::WadePointOf(0x01, 9.7f, 8.2f, 10.0f, 0x01) == L::WadeWay::Refused);    // magma, 1.5 deep
    CHECK(L::WadePointOf(0x04, 10.4f, 10.2f, 10.0f, 0x04) == L::WadeWay::Refused);  // slime, 0.2 deep
    CHECK(L::WadePointOf(0x02, 10.2f, 10.0f, 10.0f, 0x10) == L::WadeWay::Refused);  // dark water, 0.2 deep
    CHECK(L::WadePointOf(0x01, 2.0f, 1.0f, 10.0f, 0x01) == L::WadeWay::Dry);        // lava far below the walk
    // The RFC rule on the same shallow point refuses: that is what a player's own `cmd move` still gets.
    CHECK(L::PointRefused(0x04, 10.4f, 10.0f));
    // A hop across a shallow strip: a run-issued move passes the wade rule, the same hop from a player pauses (the exact
    // pair the executor chooses between: PathWadeRefused for the run's own move, PathPrefixRefused for a human's).
    std::vector<P> crossing = { {0, 0, 0}, {20, 0, 0} };
    auto shallowStrip = [](float x, float, float) { return x >= 8.0f && x <= 12.0f ? L::WadeWay::Shallow : L::WadeWay::Dry; };
    auto deepStrip = [](float x, float, float) { return x >= 8.0f && x <= 12.0f ? L::WadeWay::Refused : L::WadeWay::Dry; };
    CHECK(!L::PathWadeRefused(crossing, crossing.size(), shallowStrip));
    CHECK(L::PathWadeRefused(crossing, crossing.size(), deepStrip));
    auto wetStrip = [](float x, float, float) { return x >= 8.0f && x <= 12.0f; };
    CHECK(L::PathPrefixRefused(crossing, crossing.size(), wetStrip));              // a human move: the RFC pause
    CHECK(!L::PathPrefixRefused(crossing, crossing.size(), [](float, float, float) { return false; }));
    // The executor's own choice, both ways on the same shallow hop: a player's move pauses, the run's move crosses.
    auto hopRefused = [&](bool fromRun) {
        return fromRun ? L::PathWadeRefused(crossing, crossing.size(), shallowStrip)
                       : L::PathPrefixRefused(crossing, crossing.size(), wetStrip);
    };
    CHECK(hopRefused(false));                                                     // a human move: "crosses or ends in liquid", the pause
    CHECK(!hopRefused(true));                                                     // a run-issued move and a shallow hop: walked
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
    // passes for a run-issued move, deep refuses, and shallow from a player still pauses with the RFC rule.
    std::vector<P> middles = { {0, 0, 0}, {10, 0, 0}, {20, 0, 0} };
    auto midShallow = [](float x, float, float) { return x > 9.0f && x < 11.0f ? L::WadeWay::Shallow : L::WadeWay::Dry; };
    auto midDeep = [](float x, float, float) { return x > 9.0f && x < 11.0f ? L::WadeWay::Refused : L::WadeWay::Dry; };
    CHECK(!L::PathWadeRefused(middles, middles.size(), midShallow));             // run-issued, shallow middle: walked
    CHECK(L::PathWadeRefused(middles, middles.size(), midDeep));                 // run-issued, deep middle: pauses
    CHECK(L::PathPrefixRefused(middles, middles.size(), wetStrip));              // a human move over the same shallow water: pauses
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

    // ---- the run's own move: the permission (DR::MoveMayWade: the origin flag and a live plan step), then the wade rule ----
    // Live run 4, step 60 of leg 6: the run's move to -30.3 133.5 was refused over a 0.2 yd cell at -28.5 135.4, on a
    // leg whose rows carry no wet bit, with the lead dry at -8.7 157.1. Live run 3: the run's cross-leg regroup at
    // -37.2 291.3 crossed the leg 4 river while the current step was leg 5. Both are run-issued moves: they wade.
    CHECK(DR::MoveMayWade(true, true));
    CHECK(!DR::MoveMayWade(false, true));                                      // a player's `.tb cmd move`: the RFC rule
    std::vector<P> step60 = { {-8.7f, 157.1f, -106.2f}, {-30.3f, 133.5f, -105.1f} };
    auto oneShallow = [](float x, float, float) { return x > -30.0f && x < -27.0f ? L::WadeWay::Shallow : L::WadeWay::Dry; };
    auto oneDeep = [](float x, float, float) { return x > -30.0f && x < -27.0f ? L::WadeWay::Refused : L::WadeWay::Dry; };
    CHECK(!L::PathWadeRefused(step60, step60.size(), oneShallow));             // the step-60 0.2 yd cell: the run walks it
    CHECK(L::PathWadeRefused(step60, step60.size(), oneDeep));                 // deep water on any leg pauses the same move
    // The same 0.2 yd cell as the log read it (status 0x01, level -105.9, depth 0.2, walk z -105.4): the RFC point rule
    // refuses it for a human commander (surface within 0.6 yd of the walk), and the wade rule reads it as shallow.
    CHECK(L::PointRefused(0x01, -105.9f, -105.4f));
    CHECK(L::WadePointOf(0x01, -105.9f, -106.1f, -105.4f, L::kWadeTypeWater) == L::WadeWay::Shallow);
    CHECK(L::PathPrefixRefused(step60, step60.size(), [](float, float, float) { return true; }));
    // The run's regroup across shallow water passes; a player's identical move pauses with the RFC rule.
    std::vector<P> regroup = { {-21.6f, 251.4f, -104.2f}, {-37.2f, 291.3f, -104.8f} };
    auto shallow = [](float, float, float) { return L::WadeWay::Shallow; };
    auto deep = [](float, float, float) { return L::WadeWay::Refused; };
    CHECK(!L::PathWadeRefused(regroup, regroup.size(), shallow));              // the run's regroup across shallow water passes
    CHECK(L::PathPrefixRefused(regroup, regroup.size(), [](float, float, float) { return true; }));   // the human `cmd move` pauses
    CHECK(L::PathWadeRefused(regroup, regroup.size(), deep));                  // deep water always pauses, permission or not
    // Deep water under the permission: the run's move is judged by the real sample rule (WadePointOf over a synthetic bed),
    // so a bed 3 yd under the surface refuses the hop whatever the origin, and a bed 1 yd under walks it. A human's move
    // over the same shallow hop pauses with the RFC rule (wetStrip, above). The wet bit is no input of the permission.
    auto bedAt = [](float bed) {
        return [bed](float x, float, float z) {
            return x >= 8.0f && x <= 12.0f ? L::WadePointOf(0x04, 10.0f, bed, z, L::kWadeTypeWater) : L::WadeWay::Dry;
        };
    };
    bool const runMay = DR::MoveMayWade(true, true);
    CHECK(runMay && !L::PathWadeRefused(crossing, crossing.size(), bedAt(9.0f)));  // 1 yd of water: the run walks it
    CHECK(runMay && L::PathWadeRefused(crossing, crossing.size(), bedAt(7.0f)));   // 3 yd of water: the run pauses (depth)
    CHECK(!DR::MoveMayWade(false, true) && L::PathPrefixRefused(crossing, crossing.size(), wetStrip));  // a human pauses

    if (failures)
        return 1;
    std::printf("RESULT: PASS (test_run_liquid)\n");
    return 0;
}
