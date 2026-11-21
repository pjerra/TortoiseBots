// Standalone test of combat-item pure policies:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_combat_items.cpp -o /tmp/test_combat_items && /tmp/test_combat_items
#include "forever/src/compat/ItemUseCastPolicy.h"
#include "ai/playerbot/policy/gear/TrinketUsePolicy.h"
#include "runtime/policy/class/WarlockSoulstonePolicy.h"

#include <cstdio>
#include <vector>

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    using namespace TortoiseBots;

    // Target kind resolution: item > unit > self.
    CHECK(DecideItemCastTargetKind(true, false) == ItemCastTargetKind::Item);
    CHECK(DecideItemCastTargetKind(false, true) == ItemCastTargetKind::Unit);
    CHECK(DecideItemCastTargetKind(false, false) == ItemCastTargetKind::Self);

    // Movement: instant potions/healthstones/trinkets do not stop movement; food/drink, cast-time, and channeled spells do.
    CHECK(!ShouldStopMovingForItemCast(false, 0, false));
    CHECK(ShouldStopMovingForItemCast(true, 0, false));
    CHECK(ShouldStopMovingForItemCast(false, 3000, false));
    CHECK(ShouldStopMovingForItemCast(false, 0, true));

    // Duration calculation.
    CHECK(ItemCastDurationMs(true, 0, false, 0) == 24000u);
    CHECK(ItemCastDurationMs(false, 0, true, 8000) == 8000u);
    CHECK(ItemCastDurationMs(false, 0, true, -1) == 8000u);
    CHECK(ItemCastDurationMs(false, 3000, false, 0) == 3000u);
    CHECK(ItemCastDurationMs(false, 0, false, 0) == 0u);

    // Trinket policy sanity.
    CHECK(ai::TrinketEffectAllowed(ai::TrinketEffectClass::Aura));
    CHECK(!ai::TrinketEffectAllowed(ai::TrinketEffectClass::None));
    CHECK(ai::TrinketHealthAllowed(ai::TrinketEffectClass::Defensive, 25, 45));
    CHECK(!ai::TrinketHealthAllowed(ai::TrinketEffectClass::Defensive, 80, 45));

    // Soulstone policy sanity: prefers healer over tank/leader.
    SoulstoneFacts facts;
    facts.inGroup = true;
    SoulstoneCandidate tank;
    tank.isTank = true;
    tank.alive = true;
    tank.inRange = true;
    tank.inLineOfSight = true;
    SoulstoneCandidate healer;
    healer.isHealer = true;
    healer.alive = true;
    healer.inRange = true;
    healer.inLineOfSight = true;
    SoulstoneChoice choice = DecideSoulstoneTarget(facts, std::vector<SoulstoneCandidate>{tank, healer});
    CHECK(choice.kind == SoulstoneChoiceKind::Member && choice.index == 1);

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
