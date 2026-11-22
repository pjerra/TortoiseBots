// Standalone test of BG1's queue eligibility (ai/playerbot/policy/bg/BgQueuePolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_queue_policy.cpp -o /tmp/test_bg_queue_policy && /tmp/test_bg_queue_policy
//
// The spec's case (BATTLEGROUNDS-SPEC.md section 4, test_bg_queue_policy): a free bot inside a `.tb run` is not eligible; Deserter,
// dead, LFG, in BG, out of bracket, bound bot queued solo, instance cap reached all refused; a free in-bracket bot accepted.
#include "ai/playerbot/policy/bg/BgQueuePolicy.h"

#include <cstdio>

namespace bg = TortoiseBots::bg;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bg::BgQueueFacts Eligible()
{
    bg::BgQueueFacts facts;
    facts.pool = true;
    facts.free = true;
    facts.alive = true;
    facts.inBracket = true;
    facts.caps.instancesWithBots = 0;
    facts.caps.maxInstances = 2;
    facts.caps.botsInBattlegrounds = 0;
    facts.caps.maxBots = 50;
    return facts;
}

int main()
{
    // The plain case: a free pool bot in the world, in a bracket, under both caps.
    CHECK(bg::BgQueueEligible(Eligible()));
    CHECK(bg::BgQueueRefusalOf(Eligible()) == bg::BgRefusal::None);

    // Each refusal on its own, from the otherwise eligible bot.
    struct Case { char const* what; void (*spoil)(bg::BgQueueFacts&); bg::BgRefusal expected; };
    Case const cases[] =
    {
        { "not a pool bot", [](bg::BgQueueFacts& f) { f.pool = false; }, bg::BgRefusal::NotPool },
        { "bound bot queued solo", [](bg::BgQueueFacts& f) { f.free = false; }, bg::BgRefusal::Bound },
        { "dead", [](bg::BgQueueFacts& f) { f.alive = false; }, bg::BgRefusal::Dead },
        { "inside a battleground", [](bg::BgQueueFacts& f) { f.inBattleground = true; }, bg::BgRefusal::InBattleground },
        { "already queued", [](bg::BgQueueFacts& f) { f.inQueue = true; }, bg::BgRefusal::InQueue },
        { "in LFG", [](bg::BgQueueFacts& f) { f.inLfg = true; }, bg::BgRefusal::InLfg },
        { "inside a .tb run", [](bg::BgQueueFacts& f) { f.inRun = true; }, bg::BgRefusal::InRun },
        { "Deserter", [](bg::BgQueueFacts& f) { f.deserter = true; }, bg::BgRefusal::Deserter },
        { "out of bracket", [](bg::BgQueueFacts& f) { f.inBracket = false; }, bg::BgRefusal::OutOfBracket },
        { "in combat", [](bg::BgQueueFacts& f) { f.inCombat = true; }, bg::BgRefusal::InCombat },
        { "instance cap", [](bg::BgQueueFacts& f) { f.caps.instancesWithBots = 2; }, bg::BgRefusal::MaxInstances },
        { "bot cap", [](bg::BgQueueFacts& f) { f.caps.botsInBattlegrounds = 50; }, bg::BgRefusal::MaxBots },
    };
    for (Case const& c : cases)
    {
        bg::BgQueueFacts facts = Eligible();
        c.spoil(facts);
        bg::BgRefusal const got = bg::BgQueueRefusalOf(facts);
        if (got != c.expected)
            std::printf("FAIL %s: got %s\n", c.what, bg::BgRefusalName(got));
        ++g_checks;
        g_fail += got != c.expected;
        CHECK(!bg::BgQueueEligible(facts));
    }

    // The refusals read in the spec's order: a bound bot that is also dead and out of bracket says bound, and a dead one inside a
    // run says dead (the run is a reason to take it out of a queue, not to refuse it twice).
    bg::BgQueueFacts bound = Eligible();
    bound.free = false; bound.alive = false; bound.inBracket = false;
    CHECK(bg::BgQueueRefusalOf(bound) == bg::BgRefusal::Bound);
    bg::BgQueueFacts deadRun = Eligible();
    deadRun.alive = false; deadRun.inRun = true;
    CHECK(bg::BgQueueRefusalOf(deadRun) == bg::BgRefusal::Dead);

    // Caps are inclusive limits: one below is fine, at the limit is not.
    bg::BgQueueFacts underInstance = Eligible();
    underInstance.caps.instancesWithBots = 1;
    CHECK(bg::BgQueueEligible(underInstance));
    bg::BgQueueFacts underBots = Eligible();
    underBots.caps.botsInBattlegrounds = 49;
    CHECK(bg::BgQueueEligible(underBots));
    bg::BgQueueFacts zeroCaps = Eligible();
    zeroCaps.caps.maxInstances = 0;
    CHECK(bg::BgQueueRefusalOf(zeroCaps) == bg::BgRefusal::MaxInstances);

    // D5's cleanup: a queued bot that got a master or was taken by a run leaves the queue; a dead queued bot stays (the core
    // revives it when it accepts).
    bg::BgQueueFacts queued = Eligible();
    queued.inQueue = true;
    CHECK(!bg::BgMustLeaveQueue(queued));
    bg::BgQueueFacts boundQueue = queued;
    boundQueue.free = false;
    CHECK(bg::BgMustLeaveQueue(boundQueue));
    bg::BgQueueFacts runQueue = queued;
    runQueue.inRun = true;
    CHECK(bg::BgMustLeaveQueue(runQueue));
    bg::BgQueueFacts deadQueue = queued;
    deadQueue.alive = false;
    CHECK(!bg::BgMustLeaveQueue(deadQueue));

    // D5: a bound bot's queue entry follows its human: it stays only while that human is connected and still holds the same
    // queue, or has entered a match. A cancel, a logout or an ungroup must take the bot out with him.
    CHECK(bg::BgBoundQueueHeld(true, true, true, false));
    CHECK(bg::BgBoundQueueHeld(true, true, false, true));
    CHECK(!bg::BgBoundQueueHeld(true, false, true, false));   // the human logged out
    CHECK(!bg::BgBoundQueueHeld(true, true, false, false));   // the human left or cancelled the queue
    CHECK(!bg::BgBoundQueueHeld(false, true, true, true));    // no human to follow at all

    // Every name is a stable token (the event's `why=`), never empty.
    for (int i = 0; i <= int(bg::BgRefusal::MaxBots); ++i)
        CHECK(bg::BgRefusalName(bg::BgRefusal(i))[0] != '\0');

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
