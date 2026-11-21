// Standalone test of the W4-A2 travel purpose gate (ai/playerbot/policy/travel/TravelPurposeGatePolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_travel_purpose_gate_policy.cpp -o /tmp/test_travel_purpose_gate_policy && /tmp/test_travel_purpose_gate_policy
//
// The gate exists so W4 can merge before or after W2 (vendors/repairs) and W3 (quests): a purpose whose slice is absent
// is refused, and the bot does less (keeps grinding), never a substitute errand.
#include "ai/playerbot/policy/travel/TravelPurposeGatePolicy.h"

#include <cstdio>
#include <initializer_list>

namespace tp = ai::travel_purpose;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    tp::Registered const nothing;
    tp::Registered const vendorOnly{ true, false };
    tp::Registered const questOnly{ false, true };
    tp::Registered const both{ true, true };

    // W4's own purposes are always allowed: the grind kit is W1's, the town visit W4-D's own.
    CHECK(tp::Allows(tp::Group::Grind, nothing));
    CHECK(tp::Allows(tp::Group::Town, nothing));

    // Quests: refused while W3's keys are not registered, whatever W2 did.
    CHECK(!tp::Allows(tp::Group::Quest, nothing));
    CHECK(!tp::Allows(tp::Group::Quest, vendorOnly));
    CHECK(tp::Allows(tp::Group::Quest, questOnly));
    CHECK(tp::Allows(tp::Group::Quest, both));
    CHECK(tp::RefusalOf(tp::Group::Quest, nothing) == tp::Refusal::QuestKeysMissing);

    // Vendor and repair: refused while W2's keys are not registered, whatever W3 did.
    for (tp::Group group : { tp::Group::Vendor, tp::Group::Repair })
    {
        CHECK(!tp::Allows(group, nothing));
        CHECK(!tp::Allows(group, questOnly));
        CHECK(tp::Allows(group, vendorOnly));
        CHECK(tp::Allows(group, both));
        CHECK(tp::RefusalOf(group, nothing) == tp::Refusal::VendorKeysMissing);
    }

    // The two gates are independent in both directions: W3 alone never opens a vendor walk (above), and W2 alone never
    // opens a quest walk (covered by questOnly/vendorOnly above). A purpose that is not in the enum cannot be asked;
    // the switch has no default that widens access.
    for (tp::Group group : { tp::Group::Grind, tp::Group::Town, tp::Group::Vendor, tp::Group::Repair, tp::Group::Quest })
        CHECK(tp::Allows(group, both));


    // ---- Decision 3: the grind clock keeps the beginner round-the-clock clause (issue #393) ----------------------------------
    // A free pool bot below level 5 with no real master grinds the whole hour; everyone else only in the first 45 minutes.
    for (std::uint32_t phase = 1; phase <= 60; ++phase)
    {
        for (std::uint32_t level = 1; level <= 4; ++level)
        {
            CHECK(tp::GrindPhaseOpen(level, true, false, phase));   // beginner: open at every phase, including 46..60
            CHECK(tp::GrindPhaseOpen(level, true, true, phase) == (phase <= 45));  // a real master's bot is no beginner
            CHECK(tp::GrindPhaseOpen(level, false, false, phase) == (phase <= 45)); // not a free pool bot
        }
        for (std::uint32_t level : { 5u, 10u, 60u })
            CHECK(tp::GrindPhaseOpen(level, true, false, phase) == (phase <= 45)); // level 5+ keeps the clock
    }
    CHECK(!tp::GrindPhaseOpen(5, true, false, 46));
    CHECK(tp::GrindPhaseOpen(4, true, false, 60));

    // ---- Decision 5: the vendor need is the real one, and stays gated on W2's keys (issue #379) -----------------------------
    // The old pair (`should sell && can sell`) was true for every pool bot holding food; the need is `vendor trip needed`.
    // 112 bots all holding rations: the loose pair says yes for each, the real need says no, so no errand starts.
    int errands = 0;
    for (int bot = 0; bot < 112; ++bot)
    {
        bool const shouldSell = true, canSell = true; // the retired pair, true for a bot with food
        bool const vendorTripNeeded = false;          // nothing worth the walk
        (void)shouldSell; (void)canSell;
        if (tp::VendorErrandNeeded(both, vendorTripNeeded))
            ++errands;
    }
    CHECK(errands == 0);
    CHECK(tp::VendorErrandNeeded(both, true));          // a real need starts one
    CHECK(tp::VendorErrandNeeded(vendorOnly, true));
    CHECK(!tp::VendorErrandNeeded(nothing, true));      // W2's keys absent: no vendor errand whatever the value says
    CHECK(!tp::VendorErrandNeeded(questOnly, true));

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
