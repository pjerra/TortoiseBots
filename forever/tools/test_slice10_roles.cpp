// Standalone test of Slice 10's role policy (forever/src/slice/SliceHybridPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_slice10_roles.cpp -o /tmp/test_slice10_roles && /tmp/test_slice10_roles
//
// A tank order is honoured only where the slice has a tank kit (H10): warrior protection, the bear druid and the protection paladin
// (recorded only). A shaman has the restoration kit alone (D2) and must be refused, and only the warrior and the bear druid may lead a
// run (D1: the protection paladin is recorded, not a run tank). Before this policy both questions were answered by the forced role bit
// (PlayerbotAI::IsTank), so a shaman ordered tank was admitted as the run's lead tank while running its healing kit.
#include "forever/src/slice/SliceHybridPolicy.h"

#include <cstdio>

namespace hy = TortoiseBots::slice::hybrid;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    // The class ids are the core's (SharedDefines.h); SliceAiFactory.cpp static_asserts them.
    CHECK(hy::ClassWarrior == 1 && hy::ClassPaladin == 2 && hy::ClassShaman == 7 && hy::ClassDruid == 11);

    // A tank order: the three classes whose kit can tank.
    CHECK(hy::CanTakeTankOrder(hy::ClassWarrior));
    CHECK(hy::CanTakeTankOrder(hy::ClassDruid));
    CHECK(hy::CanTakeTankOrder(hy::ClassPaladin));      // the protection paladin is the recorded kit
    CHECK(!hy::CanTakeTankOrder(hy::ClassShaman));      // restoration only (D2): the live test never gives it the tank order
    CHECK(!hy::CanTakeTankOrder(3));                    // rogue
    CHECK(!hy::CanTakeTankOrder(8));                    // mage
    CHECK(!hy::CanTakeTankOrder(5));                    // priest
    CHECK(!hy::CanTakeTankOrder(0));

    // A run's lead tank: only the warrior tank and the bear druid.
    CHECK(hy::CanLeadRun(hy::ClassWarrior));
    CHECK(hy::CanLeadRun(hy::ClassDruid));
    CHECK(!hy::CanLeadRun(hy::ClassPaladin));           // recorded only (spec row i: no taunt at 18)
    CHECK(!hy::CanLeadRun(hy::ClassShaman));            // no tank kit: the old `IsTank`-only check admitted it
    CHECK(!hy::CanLeadRun(5));

    // A tank bit is a tank only where the kit can tank: `PlayerbotAI::IsTank` answers from the forced role first, so the run's member
    // reads and the party orders must not trust the bit alone (a bit left on a shaman by an older build, `.bot role` or LFT).
    CHECK(!hy::CountsAsTank(hy::ClassShaman, true));
    CHECK(!hy::CountsAsTank(3, true));                  // rogue
    CHECK(!hy::CountsAsTank(5, true));                  // priest
    CHECK(hy::CountsAsTank(hy::ClassWarrior, true));
    CHECK(hy::CountsAsTank(hy::ClassDruid, true));
    CHECK(hy::CountsAsTank(hy::ClassPaladin, true));
    CHECK(!hy::CountsAsTank(hy::ClassWarrior, false));  // no role is no tank
    CHECK(!hy::CountsAsTank(hy::ClassShaman, false));

    // `.tb role ... tank` on a class with no tank kit: refused, and a tank bit already on the bot is cleared (both reads
    // PlayerbotAI::IsTank makes before its kit: the player-wide stored role, then the bot's own forced role).
    hy::TankOrderStep const stale = hy::PlanTankOrder(hy::ClassShaman, true, true, true);
    CHECK(stale.refuse && stale.clearStored && stale.clearAi);
    hy::TankOrderStep const storedOnly = hy::PlanTankOrder(hy::ClassShaman, true, true, false);
    CHECK(storedOnly.refuse && storedOnly.clearStored && !storedOnly.clearAi);
    hy::TankOrderStep const aiOnly = hy::PlanTankOrder(hy::ClassShaman, true, false, true);
    CHECK(aiOnly.refuse && !aiOnly.clearStored && aiOnly.clearAi);
    hy::TankOrderStep const clean = hy::PlanTankOrder(hy::ClassShaman, true, false, false);
    CHECK(clean.refuse && !clean.clearStored && !clean.clearAi);
    hy::TankOrderStep const bear = hy::PlanTankOrder(hy::ClassDruid, true, true, true);
    CHECK(!bear.refuse && !bear.clearStored && !bear.clearAi);
    hy::TankOrderStep const healerOrder = hy::PlanTankOrder(hy::ClassShaman, false, false, false);
    CHECK(!healerOrder.refuse);                         // not a tank order: nothing to refuse or clear

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
