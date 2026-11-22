// Standalone test of the `.bot action` addon's core-free half (forever/src/host/TbAddonActionsPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_addon_actions.cpp -o /tmp/test_addon_actions && /tmp/test_addon_actions
// Intent parsing ports TB's ParseAction (TortoiseBots/commands/Action.cpp:251-488); the scope rule and the
// support table are the f-actions contract (TB behaviour, TBF implementation).
#include "forever/src/host/TbAddonActionsPure.h"

#include <cstdio>
#include <string>

namespace act = TortoiseBots::action;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static act::ParsedAction Parse(std::string const& text)
{
    return act::ParseAction(text);
}

static void TestParseBasics()
{
    CHECK(Parse("follow").ok && Parse("follow").intent == "follow" && Parse("follow").option.empty());
    CHECK(Parse("  Stay  ").ok && Parse("  Stay  ").intent == "stay");
    CHECK(Parse("ATTACK").ok && Parse("ATTACK").intent == "attack");
    CHECK(Parse("flee").ok);
    CHECK(Parse("come").ok && Parse("hold").ok && Parse("comestay").ok);
    CHECK(Parse("guard").ok && Parse("free").ok && Parse("rebuff").ok);
    CHECK(Parse("ready").ok && Parse("trade").ok && Parse("learn").ok);
    CHECK(Parse("rest").ok && Parse("drink").ok && Parse("eat").ok);
    CHECK(Parse("repair").ok && Parse("sell").ok && Parse("release").ok);
    CHECK(Parse("stop").ok);
    CHECK(!Parse("").ok && !Parse("   ").ok);
    CHECK(!Parse("frobnicate").ok);
    CHECK(!Parse("follow now").ok);   // TB refuses trailing words on plain verbs
    CHECK(!Parse("guard here").ok);
}

static void TestInvalidName()
{
    CHECK(act::InvalidIntentName("frobnicate now") == "frobnicate");
    CHECK(act::InvalidIntentName("") == "unknown");
    CHECK(act::InvalidIntentName("   ") == "unknown");
    CHECK(act::InvalidIntentName("|||") == "unknown");
    CHECK(act::InvalidIntentName("  pull 5 ") == "pull");
    CHECK(act::InvalidIntentName(std::string(40, 'x')) == std::string(32, 'x'));
}

static void TestParseCompound()
{
    act::ParsedAction end = Parse("end pull");
    CHECK(end.ok && end.intent == "end pull");
    CHECK(!Parse("end").ok && !Parse("end game").ok);
    act::ParsedAction corpse = Parse("corpse run");
    CHECK(corpse.ok && corpse.intent == "corpse run");
    CHECK(Parse("corpserun").ok && Parse("corpserun").intent == "corpse run");
    CHECK(Parse("CORPSE RUN").ok);
    CHECK(!Parse("corpse").ok);
    act::ParsedAction focus = Parse("focus skull");
    CHECK(focus.ok && focus.intent == "focus skull");
    CHECK(!Parse("focus moon").ok && !Parse("focus").ok && !Parse("focus skull extra").ok);
}

static void TestParseCc()
{
    CHECK(Parse("cc moon").ok && Parse("cc moon").intent == "cc moon");
    CHECK(Parse("cc skull").ok);
    act::ParsedAction named = Parse("cc moon Bob");
    CHECK(named.ok && named.intent == "cc moon Bob");
    CHECK(!Parse("cc foo").ok && !Parse("cc").ok);
    CHECK(!Parse("cc moon Bob extra").ok);
    act::ParsedAction clear = Parse("cc clear");
    CHECK(clear.ok && clear.intent == "cc clear");
    act::ParsedAction clearNamed = Parse("cc clear Bob");
    CHECK(clearNamed.ok && clearNamed.intent == "cc clear Bob");
    CHECK(Parse("CC CLEAR").ok);
}

static void TestParseToggles()
{
    act::ParsedAction aoe = Parse("aoe on");
    CHECK(aoe.ok && aoe.intent == "aoe" && aoe.option == "on");
    CHECK(Parse("aoe").ok && Parse("aoe").option.empty());
    CHECK(Parse("aoe status").ok && Parse("aoe status").option == "status");
    CHECK(!Parse("aoe now").ok);
    CHECK(Parse("loot off").ok && Parse("loot off").option == "off");
    act::ParsedAction autoCc = Parse("auto cc on");
    CHECK(autoCc.ok && autoCc.intent == "auto cc" && autoCc.option == "on");
    CHECK(Parse("autocc").ok && Parse("autocc").intent == "auto cc");
    CHECK(Parse("autocc status").ok && Parse("autocc status").option == "status");
    act::ParsedAction sap = Parse("sap off Bob");
    CHECK(sap.ok && sap.intent == "sap" && sap.option == "off Bob");
    CHECK(Parse("sap").ok && Parse("sap").option == "toggle");
    CHECK(Parse("sap Bob").ok && Parse("sap Bob").option == "toggle Bob");
}

static void TestParseStanceEngagePull()
{
    CHECK(Parse("stance near").ok && Parse("stance near").option == "near");
    CHECK(Parse("stance turn").ok && Parse("stance turn").option == "turnback");
    CHECK(Parse("STANCE TANK").ok && Parse("STANCE TANK").option == "tank");
    CHECK(!Parse("stance").ok && !Parse("stance left").ok);
    CHECK(Parse("engage").ok && Parse("engage").option.empty());
    CHECK(Parse("engage 10").ok && Parse("engage 10").option == "10");
    CHECK(!Parse("engage 61").ok && !Parse("engage soon").ok);
    act::ParsedAction pull = Parse("pull 5 ranged");
    CHECK(pull.ok && pull.intent == "pull" && pull.option == "5 ranged");
    CHECK(Parse("pull").ok && Parse("pull").option.empty());
    CHECK(Parse("pullback 3 body").ok && Parse("pullback 3 body").option == "3 body");
    CHECK(!Parse("pull 5 ranged body").ok);   // ranged + body exclude each other
    CHECK(!Parse("pull 61").ok);
}

static void TestParseRaidUse()
{
    CHECK(Parse("raid status").ok && Parse("raid status").intent == "raid status");
    CHECK(Parse("raid custom on").ok);
    CHECK(!Parse("raid").ok && !Parse("raid foo").ok);
    CHECK(Parse("use Bob").ok && Parse("use Bob").option == "Bob");
    CHECK(Parse("use").ok && Parse("use").option.empty());
    // TBM buttons the server never accepted: TB answers these `invalid`, not `unsupported`.
    CHECK(!Parse("strategy +grind").ok);
    CHECK(!Parse("totem party drop").ok && !Parse("downtime").ok);
}

static void TestScope()
{
    CHECK(act::DecideScope(true) == act::ScopeKind::Single);
    CHECK(act::DecideScope(false) == act::ScopeKind::Party);
    CHECK(act::ScopeName(true, "Bob") == "bot:Bob");
    CHECK(act::ScopeName(false, "Bob") == "party");
}

static void TestClassify()
{
    using K = act::ExecKind;
    CHECK(act::Classify("follow") == K::Order);
    CHECK(act::Classify("attack") == K::Order);
    CHECK(act::Classify("stop") == K::Order);
    CHECK(act::Classify("stance") == K::Order);
    CHECK(act::Classify("engage") == K::Order);
    CHECK(act::Classify("interrupt") == K::Interrupt);
    CHECK(act::Classify("pull") == K::Order);
    CHECK(act::Classify("focus skull") == K::FocusSkull);
    CHECK(act::Classify("cc clear") == K::CcClear);
    CHECK(act::Classify("cc clear Bob") == K::CcClear);
    CHECK(act::Classify("hold") == K::ComeAt);
    CHECK(act::Classify("come") == K::ComeAt);
    CHECK(act::Classify("comestay") == K::ComeAt);
    CHECK(act::Classify("use") == K::Unsupported);
    CHECK(act::Classify("trade") == K::Unsupported);
    CHECK(act::Classify("ready") == K::Unsupported);
    CHECK(act::Classify("corpse run") == K::Unsupported);
    CHECK(act::Classify("cc moon") == K::Unsupported);
    CHECK(act::Classify("cc moon Bob") == K::Unsupported);
    CHECK(act::Classify("aoe") == K::Unsupported);
    CHECK(act::Classify("auto cc") == K::Unsupported);
    CHECK(act::Classify("sap") == K::Unsupported);
    CHECK(act::Classify("loot") == K::Unsupported);
    CHECK(act::Classify("raid status") == K::Unsupported);
    CHECK(!act::WhyUnsupported("cc moon").empty());
    CHECK(!act::WhyUnsupported("aoe").empty());
}

static void TestOrderMap()
{
    CHECK(act::MapToOrder("follow").verb == "follow");
    CHECK(act::MapToOrder("learn", "").verb == "train");
    CHECK(act::MapToOrder("drink", "").verb == "rest");
    CHECK(act::MapToOrder("eat", "").verb == "rest");
    CHECK(act::MapToOrder("hold", "").verb == "come");
    CHECK(act::MapToOrder("comestay", "").verb == "come");
    CHECK(act::MapToOrder("end pull", "").verb == "endpull");
    CHECK(act::MapToOrder("focus skull", "").verb == "focus");
    CHECK(act::MapToOrder("stance", "turnback").arg == "turnback");
    CHECK(act::MapToOrder("pull", "5 ranged").verb == "pull");
    CHECK(act::MapToOrder("pull", "5 ranged").arg == "5 ranged");
}

static void TestSupport()
{
    CHECK(act::IsSupported("follow"));
    CHECK(act::IsSupported("attack"));
    CHECK(act::IsSupported("interrupt"));
    CHECK(act::IsSupported("pull"));
    CHECK(act::IsSupported("stance"));
    CHECK(act::IsSupported("engage"));
    CHECK(act::IsSupported("cc clear"));
    CHECK(act::IsSupported("cc clear Bob"));
    CHECK(act::IsSupported("focus skull"));
    CHECK(!act::IsSupported("cc moon"));
    CHECK(!act::IsSupported("aoe"));
    CHECK(!act::IsSupported("raid status"));
    CHECK(!act::IsSupported("strategy +grind"));
    CHECK(act::AdvertiseToken("follow") == "action:follow");
    bool found = false;
    for (std::string const& intent : act::SupportedIntents())
        if (act::AdvertiseToken(intent) == "action:interrupt")
            found = true;
    CHECK(found);
    CHECK(!act::Usage().empty());
}

int main()
{
    TestParseBasics();
    TestInvalidName();
    TestParseCompound();
    TestParseCc();
    TestParseToggles();
    TestParseStanceEngagePull();
    TestParseRaidUse();
    TestScope();
    TestClassify();
    TestOrderMap();
    TestSupport();
    if (g_fail == 0)
        std::printf("test_addon_actions: %d checks passed\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
