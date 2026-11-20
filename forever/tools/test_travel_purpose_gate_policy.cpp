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

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
