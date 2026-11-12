// RegroupReleaseProbePolicy: the release window, the per-commander book and
// the row layout of the RegroupReleaseProbe diagnostic (bot_events.csv,
// AiPlayerbot.RaidBossDiagnostics only).
//
// Named failing cases before the policy existed: one global window another
// commander's ticks spent, a window left behind after its run ended, a
// hand-issued release outside a run arming a window for the next one, and a
// row with a victim, a selection or a holder list that broke the key=value
// layout.
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

void Book_keeps_two_commanders_windows()
{
    Book book;
    book.Arm(7, {11, 12});
    book.Arm(9, {21});
    CHECK(book.Find(7)->bots.size() == 2);
    // Interleaved run ticks: each commander's own window advances only on its own ticks.
    CHECK(book.Find(7)->window.Next() == 1);
    CHECK(book.Find(9)->window.Next() == 1);
    CHECK(book.Find(7)->window.Next() == 2);
    // A second release for 7 starts it over (and takes its new bots); 9's window is untouched.
    book.Arm(7, {13});
    CHECK(book.Find(7)->window.Next() == 1);
    CHECK(book.Find(9)->window.Next() == 2);
    // Both windows complete their own ten ticks.
    for (unsigned i = 3; i <= kTickRows; ++i)
        CHECK(book.Find(9)->window.Next() == i);
    CHECK(book.Find(9)->window.Next() == 0);
    for (unsigned i = 2; i <= kTickRows; ++i)
        CHECK(book.Find(7)->window.Next() == i);
    CHECK(book.Find(7)->window.Next() == 0);
    // A run's end (and the module's clear) drops its window.
    book.End(7);
    CHECK(book.Find(7) == nullptr);
    CHECK(book.Find(9) != nullptr);
    book.Clear();
    CHECK(book.Find(9) == nullptr);
    CHECK(book.Find(42) == nullptr);
}

void Book_release_outside_a_run_leaves_no_state()
{
    Book book;
    // A hand-issued release outside a run arms nothing for the commander's next ticks.
    book.Release(7, {11}, false);
    CHECK(book.Find(7) == nullptr);
    // ... and it drops a window an earlier in-run release left standing.
    book.Release(7, {11}, true);
    CHECK(book.Find(7) != nullptr);
    book.Release(7, {12}, false);
    CHECK(book.Find(7) == nullptr);
    // The commander's next run starts clean: its first tick is 1, and only its own bots are read.
    book.Release(7, {13}, true);
    CHECK(book.Find(7)->window.Next() == 1);
    CHECK(book.Find(7)->bots.size() == 1);
    CHECK(book.Find(7)->bots[0] == 13);
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
    std::string const detail = BotDetail("combat", "Hakkar", "Hakkar", "", "Hakkar", "123456", true, 2,
        {"Hakkar", "Shade"});
    CHECK(detail == "engine=combat|cur=Hakkar|atk=Hakkar|pull=-|vic=Hakkar|sel=123456|combat=1|holders=2:Hakkar,Shade");
    std::string const idle = BotDetail("non-combat", "", "", "", "", "", false, 0, {});
    CHECK(idle == "engine=non-combat|cur=-|atk=-|pull=-|vic=-|sel=-|combat=0|holders=0:-");
}

void BotDetail_caps_and_escapes_the_holders()
{
    // Six holders: the count is all of them, the names stop at kHolderNames, and a name's
    // separators are escaped so the row stays parseable.
    std::vector<std::string> names = {"a", "x,y", "c", "d", "e", "f"};
    std::string const detail = BotDetail("combat", "", "", "", "", "", true, 6, names);
    CHECK(detail.find("|holders=6:a,x%2Cy,c,d,e") != std::string::npos);
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
    Book_keeps_two_commanders_windows();
    Book_release_outside_a_run_leaves_no_state();
    BossDetail_keeps_the_layout();
    BossDetail_caps_names_and_marks_an_empty_list();
    BossDetail_escapes_a_names_separators();
    BotDetail_keeps_the_layout();
    BotDetail_caps_and_escapes_the_holders();
    Subjects_carry_the_tick_and_the_name();
    std::cout << "regroup release probe policy: all cases passed\n";
    return 0;
}
