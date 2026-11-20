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

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
