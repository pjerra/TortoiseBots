// Standalone test: an explicit craft (a guild order) of a bag is allowed with every bag slot worn; a self craft is not.
//   g++ -std=c++17 -I. -Iai forever/tools/test_craft_bag_order.cpp -o /tmp/t && /tmp/t
#include "playerbot/policy/profession/CraftForSelfPolicy.h"
#include <cstdio>
#include <cstring>

using namespace TortoiseBots::prof;
static int failed = 0, checks = 0;
static void Check(bool ok, char const* what) { ++checks; if (!ok) { ++failed; std::printf("FAIL: %s\n", what); } }

static CraftFacts LinenBag(CraftPurpose purpose)
{
    CraftFacts f;
    f.spellId = 3755; f.known = true; f.skillId = 197; f.reqSkill = 45; f.botSkill = 60;
    f.freeBagSlots = 10; f.createsBag = true; f.hasFreeEquipBagSlot = false; f.purpose = purpose;
    f.reagents = { { 2996, 3, 3 }, { 2320, 3, 3 } };
    return f;
}

int main()
{
    Check(EvaluateCraftForSelf(LinenBag(CraftPurpose::Explicit)).allow, "an ordered Linen Bag with all bag slots worn is crafted");
    CraftDecision self = EvaluateCraftForSelf(LinenBag(CraftPurpose::Bag));
    Check(!self.allow && std::strcmp(self.code, "bags") == 0, "a self-craft bag still needs a free bag slot to be worn");
    CraftFacts full = LinenBag(CraftPurpose::Explicit);
    full.freeBagSlots = 0;
    Check(!EvaluateCraftForSelf(full).allow, "no free slot in the bags still refuses");
    std::printf("%s: test_craft_bag_order (%d checks, %d failed)\n", failed ? "FAIL" : "PASS", checks, failed);
    return failed ? 1 : 0;
}
