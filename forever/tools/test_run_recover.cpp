// Standalone check of proof-1 Part B (forever/docs/PROOF-1-NOTES.md): the run's self-recover after the policy stopped regroups by itself at a step
// (TbRunRecoverPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_recover.cpp -o /tmp/test_run_recover && /tmp/test_run_recover
#include "forever/src/host/run/TbRunRecoverPure.h"
#include "runtime/DungeonRunPolicy.h"

#include <cstdio>
#include <string>

namespace DR = TortoiseBots::DungeonRun;
namespace rc = TortoiseBots::run::recover;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// The policy's own pause texts, built as the policy builds them (runtime/DungeonRunPolicy.h: under `LeadRevivedAway` "the lead bot died and came back
// X yd from where it fell (N regroups by itself at this step did not hold): .bot run resume", and at the wipe "N dead: regroup (cmd revive party),
// then .bot run resume (N regroups by itself at this step did not hold)"). The count is the policy's own kAutoRegroupsPerStep; the wording is the
// policy's own (test_recover_wiring.py pins the same words in the policy source, so an edit there fails a test).
static std::string const kWording = " regroups by itself at this step did not hold";

static std::string LeadDiedAwayText()
{
    return "the lead bot died and came back 51.2 yd from where it fell (" + std::to_string(DR::kAutoRegroupsPerStep) + kWording + "): .bot run resume";
}

static std::string WipeText()
{
    return "3 dead: regroup (cmd revive party), then .bot run resume (" + std::to_string(DR::kAutoRegroupsPerStep) + kWording + ")";
}

static void TestRecoverMatchesThePolicyText()
{
    CHECK(rc::IsRegroupsDidNotHoldPause(LeadDiedAwayText()));
    CHECK(rc::IsRegroupsDidNotHoldPause(WipeText()));
    CHECK(!rc::IsRegroupsDidNotHoldPause(""));
    // the other pauses the policy writes are left alone, including the gather pause and a "regroups by itself" note that did hold
    CHECK(!rc::IsRegroupsDidNotHoldPause("regroup: the party did not gather within 60 s (a bot is 72.5 yd from the lead bot)"));
    CHECK(!rc::IsRegroupsDidNotHoldPause("the lead bot died and came back 51.2 yd from where it fell (2 regroups by itself at this step): .bot run resume"));
    CHECK(!rc::IsRegroupsDidNotHoldPause("2 still dead after 240 s (cmd revive <bot>, then .bot run resume)"));

    // the two real pause notes are recovered, with a reason that names nothing but the decision
    rc::State s;
    rc::Decision d = rc::Decide(s, 12, LeadDiedAwayText(), 30.0f, true);
    CHECK(d.recover && !d.why.empty());
    rc::State t;
    d = rc::Decide(t, 12, WipeText(), 30.0f, true);
    CHECK(d.recover && !d.why.empty());
}

static void TestRecoverOncePerStep()
{
    rc::State s;
    rc::Decision d = rc::Decide(s, 56, WipeText(), 20.0f, true);
    CHECK(d.recover && s.stepCount == 1 && s.runCount == 1 && !d.why.empty());
    d = rc::Decide(s, 56, WipeText(), 20.0f, true);
    CHECK(!d.recover && !d.why.empty() && s.stepCount == rc::kMaxRecoversPerStep && s.runCount == 1);
    // a third pause of the same step answers the same, still with its reason
    d = rc::Decide(s, 56, WipeText(), 20.0f, true);
    CHECK(!d.recover && !d.why.empty());
}

static void TestRecoverNewStepResets()
{
    rc::State s;
    CHECK(rc::Decide(s, 56, WipeText(), 20.0f, true).recover);
    CHECK(!rc::Decide(s, 56, WipeText(), 20.0f, true).recover);
    rc::Decision d = rc::Decide(s, 57, WipeText(), 20.0f, true);
    CHECK(d.recover && s.step == 57 && s.stepCount == 1 && s.runCount == 2);
    CHECK(!rc::Decide(s, 57, WipeText(), 20.0f, true).recover);
}

static void TestRecoverRunCap()
{
    rc::State s;
    uint32_t recovered = 0;
    for (uint64_t step = 1; step <= 5; ++step)
    {
        rc::Decision const d = rc::Decide(s, step, WipeText(), 20.0f, true);
        if (d.recover)
            ++recovered;
        else
            CHECK(!d.why.empty());
    }
    CHECK(recovered == rc::kMaxRecoversPerRun);
    CHECK(s.runCount == rc::kMaxRecoversPerRun);
    // every later step is refused as well, with the run cap as the reason
    rc::Decision d = rc::Decide(s, 6, WipeText(), 20.0f, true);
    CHECK(!d.recover && !d.why.empty() && s.runCount == rc::kMaxRecoversPerRun);
    // the same step counter starts over per step, but the run's does not
    CHECK(s.step == 6 && s.stepCount == 0);
}

static void TestRecoverOtherPauseSilent()
{
    rc::State s;
    rc::Decision d = rc::Decide(s, 1, "regroup: the party did not gather within 60 s (a bot is 72.5 yd from the lead bot)", 20.0f, true);
    CHECK(!d.recover && d.why.empty() && s.runCount == 0 && s.stepCount == 0);
    d = rc::Decide(s, 1, "paused by .tb run pause", 20.0f, true);
    CHECK(!d.recover && d.why.empty());
    d = rc::Decide(s, 1, "", 20.0f, true);
    CHECK(!d.recover && d.why.empty());
    d = rc::Decide(s, 1, "3 dead: regroup (cmd revive party), then .bot run resume", 20.0f, true);
    CHECK(!d.recover && d.why.empty());
    CHECK(s.runCount == 0);
}

static void TestRecoverFarMember()
{
    rc::State s;
    rc::Decision d = rc::Decide(s, 1, WipeText(), rc::kRecoverFarYd, true);
    CHECK(!d.recover && !d.why.empty() && s.runCount == 0);
    d = rc::Decide(s, 1, WipeText(), 1000.0f, true);
    CHECK(!d.recover && !d.why.empty());
    d = rc::Decide(s, 1, WipeText(), rc::kRecoverFarYd - 1.0f, true);
    CHECK(d.recover && !d.why.empty());
}

static void TestRecoverOff()
{
    rc::State s;
    rc::Decision d = rc::Decide(s, 1, WipeText(), 20.0f, false);
    CHECK(!d.recover && !d.why.empty() && s.runCount == 0 && s.stepCount == 0);
    // and it stays off on the next step
    d = rc::Decide(s, 2, WipeText(), 20.0f, false);
    CHECK(!d.recover && !d.why.empty() && s.runCount == 0);
}

int main()
{
    TestRecoverMatchesThePolicyText();
    TestRecoverOncePerStep();
    TestRecoverNewStepResets();
    TestRecoverRunCap();
    TestRecoverOtherPauseSilent();
    TestRecoverFarMember();
    TestRecoverOff();
    std::printf("test_run_recover: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
