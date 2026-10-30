// Standalone tests for the bot LFG group lifecycle policy (bot LFG part 2):
// the life of one group (Forming -> Waiting -> Ready -> Ended) and the GM
// command grammar.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_bot_lfg_lifecycle_policy.cpp -o /tmp/lfg2_life
//   /tmp/lfg2_life

#include "../runtime/policy/lfg/BotLfgLifecyclePolicy.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace TortoiseBots::BotLfgLifecycle;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static Facts At(State s, uint32_t elapsed = 0)
{
    Facts f;
    f.state = s; f.elapsedMs = elapsed; f.fillAfterMs = 600000;
    return f;
}

static void TestFormingAndWaiting()
{
    CHECK(DecideStep(At(State::Forming)).step == Step::ToWaiting);
    CHECK(DecideStep(At(State::Waiting, 599999)).step == Step::None);
    Facts due = At(State::Waiting, 600000);
    CHECK(DecideStep(due).step == Step::None);              // due, but no bot of the open role yet
    due.fillCandidateFound = true;
    Decision d = DecideStep(due);
    CHECK(d.step == Step::FillWithBot && d.fill == FillKind::Bot);
    Facts forced = At(State::Waiting, 1000);
    forced.forceFill = true; forced.fillCandidateFound = true;
    CHECK(DecideStep(forced).step == Step::FillWithBot);    // GM fill skips the timer
    Facts late = At(State::Waiting, 600000 + kFillRetryMs);
    d = DecideStep(late);
    CHECK(d.step == Step::End && d.reason == EndReason::NoFill && !d.release);
    Facts forceLate = At(State::Waiting, 600000 + kFillRetryMs);
    forceLate.forceFill = true;
    CHECK(DecideStep(forceLate).step == Step::End && DecideStep(forceLate).reason == EndReason::NoFill);
    CHECK(DecideStep(At(State::Waiting, 600000 + kFillRetryMs - 1)).step == Step::None);
    CHECK(FillDue(State::Waiting, 600000, 600000, false) && !FillDue(State::Ready, 600000, 600000, true));
    // a missing slot ends or replaces only after the grace, never on one tick
    CHECK(!MissingGraceExpired(20000, 20000 + kMissingGraceMs - 1));
    CHECK(MissingGraceExpired(20000, 20000 + kMissingGraceMs));
}

static void TestReady()
{
    CHECK(DecideStep(At(State::Ready, kReadyHoldMs - 1)).step == Step::None);
    Decision d = DecideStep(At(State::Ready, kReadyHoldMs));
    CHECK(d.step == Step::End && d.reason == EndReason::ReadyNoTravel && !d.release);
    CHECK(DecideStep(At(State::Ended, 999999)).step == Step::None);
}

// A real player in the group fills it while it waits; he is never disbanded.
static void TestEndReleasesWithPlayer()
{
    Facts w = At(State::Waiting, 10);
    w.realPlayerInGroup = true;
    Decision d = DecideStep(w);
    CHECK(d.step == Step::ToReady && d.fill == FillKind::Player);
    Facts f = At(State::Forming);
    f.realPlayerInGroup = true;
    d = DecideStep(f);
    CHECK(d.step == Step::End && d.reason == EndReason::PlayerJoined && d.release);
    Facts r = At(State::Ready, kReadyHoldMs);
    r.realPlayerInGroup = true;
    d = DecideStep(r);
    CHECK(d.step == Step::End && d.reason == EndReason::ReadyNoTravel && d.release);
    Facts off = At(State::Waiting);
    off.enabled = false; off.realPlayerInGroup = true;
    d = DecideStep(off);
    CHECK(d.reason == EndReason::SwitchedOff && d.release);
    Facts lead = At(State::Ready, 10);
    lead.realPlayerInGroup = true; lead.leaderLost = true;
    CHECK(DecideStep(lead).release);
    CHECK(ReleaseOnEnd(false, EndReason::TakenOver) && ReleaseOnEnd(true, EndReason::NoFill) &&
        !ReleaseOnEnd(false, EndReason::GmDisband));
}

static void TestDecideStepOrder()
{
    Facts f = At(State::Waiting, 600000);
    f.enabled = false; f.boundByOther = true; f.leaderLost = true; f.lostMembers = 2; f.fillCandidateFound = true;
    CHECK(DecideStep(f).reason == EndReason::SwitchedOff);
    f.enabled = true;
    Decision d = DecideStep(f);
    CHECK(d.reason == EndReason::TakenOver && d.release);   // a binding owns the group now: hands off
    f.boundByOther = false;
    CHECK(DecideStep(f).reason == EndReason::LeaderLost && !DecideStep(f).release);
    f.leaderLost = false;
    CHECK(DecideStep(f).reason == EndReason::MemberLost);
    f.replacementFound = true;
    CHECK(DecideStep(f).step == Step::Replace);             // replace before the fill
    f.lostMembers = 0;
    CHECK(DecideStep(f).step == Step::FillWithBot);
}

static void TestTexts()
{
    CHECK(std::string(EndCode(EndReason::ReadyNoTravel)) == "ready-no-travel");
    CHECK(std::string(EndCode(EndReason::GmDisband)) == "gm");
    CHECK(std::string(EndText(EndReason::ReadyNoTravel)) == "the group is full; getting it to the dungeon is not built yet");
    CHECK(std::string(StateName(State::Waiting)) == "waiting");
    CHECK(LogLine(3, "deadmines", "alliance", "formed") == "TortoiseBots: bot LFG group 3 (deadmines, alliance): formed");
}

static void TestFillNowAnswer()
{
    CHECK(FillNowAnswer(State::Ready, FillKind::Bot, "Jubuwaeg", "", "tank") == "filled by a bot: Jubuwaeg (tank)");
    CHECK(FillNowAnswer(State::Ready, FillKind::Player, "", "Mira", "tank") ==
        "a player already took the open slot: Mira");
    CHECK(FillNowAnswer(State::Waiting, FillKind::None, "", "", "tank").find("no free tank bot") == 0);
    CHECK(FillNowAnswer(State::Waiting, FillKind::None, "", "", "tank").find("next tick") == std::string::npos);
    CHECK(FillNowAnswer(State::Ended, FillKind::None, "", "", "tank").find("ended") != std::string::npos);
}

static void TestParseArgs()
{
    CHECK(ParseArgs("list").verb == Verb::List && ParseArgs("LIST").verb == Verb::List);
    CHECK(ParseArgs("list x").verb == Verb::Invalid);
    Args f = ParseArgs("form");
    CHECK(f.verb == Verb::Form && f.plan.empty() && f.team == 0);
    f = ParseArgs("form Deadmines");
    CHECK(f.verb == Verb::Form && f.plan == "deadmines" && f.team == 0);
    f = ParseArgs("form deadmines horde");
    CHECK(f.plan == "deadmines" && f.team == 2);
    f = ParseArgs("form alliance");
    CHECK(f.verb == Verb::Form && f.plan.empty() && f.team == 1);
    CHECK(ParseArgs("form deadmines horde x").verb == Verb::Invalid);
    CHECK(ParseArgs("form horde alliance").verb == Verb::Invalid);
    Args n = ParseArgs("fill 12");
    CHECK(n.verb == Verb::Fill && n.id == 12);
    CHECK(ParseArgs("disband 3").verb == Verb::Disband && ParseArgs("disband 3").id == 3);
    CHECK(ParseArgs("fill").verb == Verb::Invalid && ParseArgs("fill 0").verb == Verb::Invalid &&
        ParseArgs("fill x1").verb == Verb::Invalid && ParseArgs("disband 1 2").verb == Verb::Invalid);
    CHECK(ParseArgs("").verb == Verb::Invalid && ParseArgs("start").verb == Verb::Invalid);
}

// An outstanding invite to a player holds the open slot; a GM fill does not wait.
static void TestInviteHoldsTheFill()
{
    Facts f = At(State::Waiting, 600000);
    f.fillCandidateFound = true; f.invitePending = true;
    CHECK(DecideStep(f).step == Step::None);
    Facts late = At(State::Waiting, 600000 + kFillRetryMs);
    late.invitePending = true;
    CHECK(DecideStep(late).step == Step::None);          // no no-fill end while he may still accept
    f.forceFill = true;
    CHECK(DecideStep(f).step == Step::FillWithBot);
    Facts joined = At(State::Waiting, 10);
    joined.invitePending = true; joined.realPlayerInGroup = true;
    CHECK(DecideStep(joined).step == Step::ToReady);     // he accepted
    Facts off = At(State::Waiting, 10);
    off.invitePending = true; off.enabled = false;
    CHECK(DecideStep(off).reason == EndReason::SwitchedOff);
}

int main()
{
    TestFormingAndWaiting();
    TestReady();
    TestEndReleasesWithPlayer();
    TestDecideStepOrder();
    TestTexts();
    TestFillNowAnswer();
    TestParseArgs();
    TestInviteHoldsTheFill();
    std::printf("bot lfg lifecycle policy: %d checks passed\n", checks);
    return 0;
}
