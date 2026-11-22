// Standalone test of the `behavior` and `service` addon verbs' core-free half
// (forever/src/host/TbAddonBehaviorServicePure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_addon_behsvc.cpp -o /tmp/test_addon_behsvc && /tmp/test_addon_behsvc
#include "forever/src/host/TbAddonBehaviorServicePure.h"

#include <cstdio>
#include <string>

namespace bs = TortoiseBots::behsvc;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void TestBehaviorAllowlistAndOffSpec()
{
    std::size_t count = 0;
    bs::BehaviorToggle const* toggles = bs::BehaviorToggles(count);
    CHECK(count == 13);
    CHECK(toggles != nullptr);

    char const* const expectedKeys[] = {
        "loot", "aoe", "autocc", "savemana", "boost", "threat",
        "potions", "mimic", "spread", "hold", "noblink", "offheal", "offdps"
    };
    for (char const* key : expectedKeys)
        CHECK(bs::FindBehaviorToggle(key) != nullptr);
    CHECK(bs::FindBehaviorToggle("unknown") == nullptr);

    bs::BehaviorToggle const* aoe = bs::FindBehaviorToggle("aoe");
    CHECK(aoe != nullptr && aoe->inverted && std::string(aoe->strategy) == "no aoe" &&
        aoe->state == bs::BehaviorState::Combat);

    bs::BehaviorToggle const* offheal = bs::FindBehaviorToggle("offheal");
    bs::BehaviorToggle const* offdps = bs::FindBehaviorToggle("offdps");
    CHECK(offheal != nullptr && offheal->offSpec);
    CHECK(offdps != nullptr && offdps->offSpec);

    // Off-spec classes: Paladin (2), Priest (5), Shaman (7), Druid (11).
    CHECK(!bs::ClassAllowsToggle(1, *offheal)); // Warrior
    CHECK(bs::ClassAllowsToggle(2, *offheal));  // Paladin
    CHECK(!bs::ClassAllowsToggle(3, *offheal)); // Hunter
    CHECK(!bs::ClassAllowsToggle(4, *offheal)); // Rogue
    CHECK(bs::ClassAllowsToggle(5, *offheal));  // Priest
    CHECK(bs::ClassAllowsToggle(7, *offdps));   // Shaman
    CHECK(!bs::ClassAllowsToggle(8, *offheal)); // Mage
    CHECK(!bs::ClassAllowsToggle(9, *offdps));  // Warlock
    CHECK(bs::ClassAllowsToggle(11, *offdps));  // Druid

    // Non-offspec toggles allowed on all classes.
    bs::BehaviorToggle const* loot = bs::FindBehaviorToggle("loot");
    CHECK(bs::ClassAllowsToggle(1, *loot) && bs::ClassAllowsToggle(8, *loot));
}

static void TestParseBehavior()
{
    bs::ParsedBehavior const p1 = bs::ParseBehaviorArgs("Alpha loot on");
    CHECK(p1.ok && !p1.isStatus && !p1.isPartyScope && p1.botName == "Alpha" && p1.key == "loot" && p1.enable &&
        p1.intent == "behavior loot");

    bs::ParsedBehavior const p2 = bs::ParseBehaviorArgs("party AOE OFF");
    CHECK(p2.ok && !p2.isStatus && p2.isPartyScope && p2.key == "aoe" && !p2.enable &&
        p2.intent == "behavior aoe");

    bs::ParsedBehavior const p3 = bs::ParseBehaviorArgs("Raid offheal on");
    CHECK(p3.ok && p3.isPartyScope && p3.key == "offheal" && p3.enable);

    bs::ParsedBehavior const st1 = bs::ParseBehaviorArgs("Alpha");
    CHECK(st1.ok && st1.isStatus && !st1.isPartyScope && st1.botName == "Alpha" && st1.intent == "behavior");

    bs::ParsedBehavior const st2 = bs::ParseBehaviorArgs("party status");
    CHECK(st2.ok && st2.isStatus && st2.isPartyScope && st2.intent == "behavior");

    CHECK(!bs::ParseBehaviorArgs("").ok);
    CHECK(!bs::ParseBehaviorArgs("Alpha bogus on").ok);
    CHECK(!bs::ParseBehaviorArgs("Alpha loot maybe").ok);
    CHECK(!bs::ParseBehaviorArgs("Alpha loot on extra").ok);
}

static void TestParseServiceAndFormat()
{
    bs::ParsedService const q = bs::ParseServiceArgs("Magi");
    CHECK(q.ok && q.isQuery && q.botName == "Magi" && q.intent == "service");

    bs::ParsedService const pId = bs::ParseServiceArgs("Magi portal 10059");
    CHECK(pId.ok && !pId.isQuery && pId.botName == "Magi" && pId.spellId == 10059 && pId.intent == "service portal");

    bs::ParsedService const pCity = bs::ParseServiceArgs("Magi portal Ironforge");
    CHECK(pCity.ok && !pCity.isQuery && pCity.spellId == 11416);

    bs::ParsedService const pPrefix = bs::ParseServiceArgs("Magi portal storm");
    CHECK(pPrefix.ok && pPrefix.spellId == 10059);

    bs::ParsedService const badEmpty = bs::ParseServiceArgs("");
    CHECK(!badEmpty.ok && badEmpty.errCode == "invalid");

    bs::ParsedService const badVerb = bs::ParseServiceArgs("Magi teleport 10059");
    CHECK(!badVerb.ok && badVerb.errCode == "invalid");

    bs::ParsedService const badCity = bs::ParseServiceArgs("Magi portal Atlantis");
    CHECK(!badCity.ok && badCity.errCode == "unknown");

    CHECK(bs::FormatBotStateLine("Alpha", "move=follow,loot=on") == "TBM:BOTSTATE|Alpha|move=follow,loot=on");
    CHECK(bs::FormatServiceLine("Magi", true, true, false, "10059:Stormwind") ==
        "TBM:SERVICE|Magi|1|1|0|10059:Stormwind");
    CHECK(bs::FormatServiceLine("Locky", false, false, true, "") ==
        "TBM:SERVICE|Locky|0|0|1|-");
}

int main()
{
    TestBehaviorAllowlistAndOffSpec();
    TestParseBehavior();
    TestParseServiceAndFormat();
    if (g_fail)
    {
        std::printf("FAILED %d/%d checks\n", g_fail, g_checks);
        return 1;
    }
    std::printf("PASS %d checks\n", g_checks);
    return 0;
}
