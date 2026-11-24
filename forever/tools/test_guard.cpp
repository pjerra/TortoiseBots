// Standalone test of the commander/party ownership guard (forever/src/host/TbGuardPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_guard.cpp -o /tmp/test_guard && /tmp/test_guard
#include "forever/src/host/TbGuardPure.h"

#include <cstdio>
#include <string_view>

namespace g = TortoiseBots::h3::guard;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
constexpr g::Guid Caller{10, 1}, OtherCmd{20, 1}, BotLead{30, 1}, None{};
}

// proof-1 A: the narrow GM-observer exemption of `.tb run`. A GM whose commander chain ends at a bot (the run's bot commander) may control the run.
static void TestObserverGmBotCommander()
{
    CHECK(g::ObserverMayControlRun(true, true, false));
}

// A GM behind a chain that ends at a real player is refused: that player owns the party.
static void TestObserverGmPlayerOwned()
{
    CHECK(!g::ObserverMayControlRun(true, true, true));
}

// A caller that is not a GM is refused whatever the chain does.
static void TestObserverNonGm()
{
    CHECK(!g::ObserverMayControlRun(true, false, false));
    CHECK(!g::ObserverMayControlRun(true, false, true));
}

// The option off (TortoiseBots.Run.GmObserver = 0) refuses the GM too.
static void TestObserverOptionOff()
{
    CHECK(!g::ObserverMayControlRun(false, true, false));
    CHECK(!g::ObserverMayControlRun(false, false, false));
}

int main()
{
    // 1. Foreign bot: bound to another commander via .tb master.
    {
        g::Subject s;
        s.caller = Caller;
        s.binding = OtherCmd;
        s.master = OtherCmd;
        CHECK(g::CheckControl(s) == g::Verdict::ForeignBot);
        CHECK(std::string_view(g::CodeOf(g::CheckControl(s))) == "foreign-bot");
    }

    // 2. Foreign bot: BotMgr master is another real player (no .tb master binding).
    {
        g::Subject s;
        s.caller = Caller;
        s.master = OtherCmd;
        s.masterIsBot = false;
        CHECK(g::CheckControl(s) == g::Verdict::ForeignBot);
        CHECK(std::string_view(g::CodeOf(g::CheckControl(s))) == "foreign-bot");
    }

    // 3. Foreign lead: bot in a party led by another real (non-bot) player.
    {
        g::Subject s;
        s.caller = Caller;
        s.partyLeader = OtherCmd;
        s.partyLeaderIsBot = false;
        CHECK(g::CheckControl(s) == g::Verdict::ForeignLead);
        CHECK(std::string_view(g::CodeOf(g::CheckControl(s))) == "foreign-lead");
    }

    // 4. Bot bound to self (allowed), even in a bot-led party.
    {
        g::Subject s;
        s.caller = Caller;
        s.binding = Caller;
        s.master = Caller;
        s.partyLeader = BotLead;
        s.partyLeaderIsBot = true;
        CHECK(g::CheckControl(s) == g::Verdict::Allowed);
    }

    // 5. Bot in caller's own party (allowed).
    {
        g::Subject s;
        s.caller = Caller;
        s.master = Caller;
        s.partyLeader = Caller;
        s.partyLeaderIsBot = false;
        CHECK(g::CheckControl(s) == g::Verdict::Allowed);
    }

    // 6. Bot in a bot-led party with BotMgr master = that bot lead (allowed).
    {
        g::Subject s;
        s.caller = Caller;
        s.master = BotLead;
        s.masterIsBot = true;
        s.partyLeader = BotLead;
        s.partyLeaderIsBot = true;
        CHECK(g::CheckControl(s) == g::Verdict::Allowed);
    }

    // 7. Free / ungrouped bot (allowed).
    {
        g::Subject s;
        s.caller = Caller;
        CHECK(g::CheckControl(s) == g::Verdict::Allowed);
    }

    // 8. GM console (no player session, caller empty) keeps today's behaviour (allowed).
    {
        g::Subject s;
        s.caller = None;
        s.binding = OtherCmd;
        s.master = OtherCmd;
        s.partyLeader = OtherCmd;
        s.partyLeaderIsBot = false;
        CHECK(g::CheckControl(s) == g::Verdict::Allowed);
    }

    TestObserverGmBotCommander();
    TestObserverGmPlayerOwned();
    TestObserverNonGm();
    TestObserverOptionOff();

    std::printf("test_guard: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
