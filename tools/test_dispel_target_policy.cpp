#include "../ai/playerbot/DispelTargetPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DispelClaimKey;
using ai::DispelReachCap;
using ai::DispelTargetTier;
using ai::IsWithinDispelReach;
using ai::kDispelTargetRejected;

int main()
{
    std::cout << "Starting TortoiseBots dispel-target policy tests...\n";

    float const spell = 26.0f;

    // Reach cap: 1.5 x spell distance, never the 60yd sight distance.
    CHECK(DispelReachCap(spell) == 39.0f);
    CHECK(IsWithinDispelReach(39.0f, spell));
    CHECK(!IsWithinDispelReach(39.1f, spell));
    CHECK(!IsWithinDispelReach(60.0f, spell));
    std::cout << "  [PASS] reach cap is 1.5 x spell distance\n";

    // Out of sight or past the cap is never a target.
    CHECK(DispelTargetTier(10.0f, spell, false, false) == kDispelTargetRejected);
    CHECK(DispelTargetTier(45.0f, spell, true, false) == kDispelTargetRejected);
    std::cout << "  [PASS] no line of sight / too far is rejected\n";

    // In range beats a walk; unclaimed beats claimed at the same reach.
    int const inRangeFree = DispelTargetTier(20.0f, spell, true, false);
    int const inRangeClaimed = DispelTargetTier(20.0f, spell, true, true);
    int const walkFree = DispelTargetTier(30.0f, spell, true, false);
    int const walkClaimed = DispelTargetTier(30.0f, spell, true, true);
    CHECK(inRangeFree == 0);
    CHECK(inRangeFree < inRangeClaimed);
    CHECK(inRangeClaimed < walkFree);
    CHECK(walkFree < walkClaimed);
    CHECK(DispelTargetTier(spell, spell, true, false) == 0);
    std::cout << "  [PASS] in range, then unclaimed, decide the tier\n";

    // A claim lowers the rank but never rejects: someone still cures it.
    CHECK(walkClaimed != kDispelTargetRejected);
    std::cout << "  [PASS] a claimed member is still a fallback\n";

    // One key per dispel type: decursers share it, other types do not.
    CHECK(DispelClaimKey("2") == DispelClaimKey("2"));
    CHECK(DispelClaimKey("1") != DispelClaimKey("2"));
    CHECK(DispelClaimKey("2") != "2");
    std::cout << "  [PASS] claim key is per dispel type\n";

    std::cout << "All dispel-target policy tests passed.\n";
    return 0;
}
