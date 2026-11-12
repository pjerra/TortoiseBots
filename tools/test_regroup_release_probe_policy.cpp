// RegroupReleaseProbePolicy: the release window, the per-bot living-tick budget
// and the row layout of the RegroupReleaseProbe diagnostic (bot_events.csv,
// AiPlayerbot.RaidBossDiagnostics only).
//
// Named failing cases before the policy existed: no window at all (every tick
// after a release wrote a row), a bot counted on ticks it spent dead, and a
// row with a victim or a top list that broke the key=value layout.
//
// Run: g++ -std=c++17 -Wall -Wextra tools/test_regroup_release_probe_policy.cpp
#include "../runtime/RegroupReleaseProbePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots::RegroupProbe;

namespace
{

void Window_bounded_to_ten_ticks()
{
    Window window;
    CHECK(window.Spent());
    CHECK(window.Next() == 0);
    window.Arm();
    unsigned rows = 0;
    for (unsigned i = 0; i < kTickRows; ++i)
    {
        unsigned const tick = window.Next();
        CHECK(tick == i + 1);
        ++rows;
    }
    CHECK(rows == kTickRows);
    // The 11th run tick writes nothing, and the window stays spent.
    CHECK(window.Next() == 0);
    CHECK(window.Next() == 0);
}

void Window_rearmed_by_a_second_release()
{
    Window window;
    window.Arm();
    CHECK(window.Next() == 1);
    CHECK(window.Next() == 2);
    // A second `revive|party` release starts its own ten ticks.
    window.Arm();
    CHECK(window.Next() == 1);
}

void BotBudget_counts_living_ticks_only()
{
    BotBudget bot;
    bot.guid = 42;
    CHECK(!bot.Due(false)); // a corpse writes nothing and keeps its budget
    CHECK(bot.Due(true));
    CHECK(!bot.Due(false));
    CHECK(bot.Due(true));
    CHECK(bot.Due(true));
    CHECK(!bot.Due(true)); // three living ticks is all
    CHECK(bot.living == kBotLivingTicks);
}

void BossDetail_keeps_the_layout()
{
    std::string const detail = BossDetail(14834, true, "Zed", 7, {"Ilaith", "Nivvla", "Tazzle"}, false, 12.4f);
    CHECK(detail == "entry=14834|combat=1|victim=Zed|threat=7|top=Ilaith,Nivvla,Tazzle|evade=0|homed=12");
}

void BossDetail_caps_names_and_marks_an_empty_list()
{
    std::vector<std::string> names = {"a", "b", "c", "d", "e", "f"};
    std::string const detail = BossDetail(1, false, "", 6, names, true, 0.0f);
    CHECK(detail == "entry=1|combat=0|victim=-|threat=6|top=a,b,c,d,e|evade=1|homed=0");
    std::string const none = BossDetail(2, false, "", 0, {}, false, 0.0f);
    CHECK(none.find("|top=-|") != std::string::npos);
}

void BossDetail_escapes_a_names_separators()
{
    // A threat name can never carry these, but a converted row must stay parseable.
    std::string const detail = BossDetail(3, false, "a|b", 1, {"x,y"}, false, 3.0f);
    CHECK(detail.find("victim=a%7Cb") != std::string::npos);
    CHECK(detail.find("top=x%2Cy") != std::string::npos);
}

void BotDetail_keeps_the_layout()
{
    std::string const detail = BotDetail("combat", "Hakkar", "Hakkar", "", true, 2);
    CHECK(detail == "engine=combat|cur=Hakkar|atk=Hakkar|pull=-|combat=1|holders=2");
    std::string const idle = BotDetail("non-combat", "", "", "", false, 0);
    CHECK(idle == "engine=non-combat|cur=-|atk=-|pull=-|combat=0|holders=0");
}

void Subjects_carry_the_tick_and_the_name()
{
    CHECK(BossSubject(3, "Hakkar") == "tick=3|boss=Hakkar");
    CHECK(BotSubject(0, "Nivvla") == "tick=0|bot=Nivvla");
}

} // namespace

int main()
{
    Window_bounded_to_ten_ticks();
    Window_rearmed_by_a_second_release();
    BotBudget_counts_living_ticks_only();
    BossDetail_keeps_the_layout();
    BossDetail_caps_names_and_marks_an_empty_list();
    BossDetail_escapes_a_names_separators();
    BotDetail_keeps_the_layout();
    Subjects_carry_the_tick_and_the_name();
    std::cout << "regroup release probe policy: all cases passed\n";
    return 0;
}
