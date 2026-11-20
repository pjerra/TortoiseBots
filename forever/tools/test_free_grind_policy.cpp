// Standalone test of W1's free-bot target choice (ai/playerbot/policy/world/FreeGrindPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_free_grind_policy.cpp -o /tmp/test_free_grind_policy && /tmp/test_free_grind_policy
//
// The scene below is a free bot standing in a camp: an ordinary mob, an enemy player in range, a quest giver in range, and the rest of
// the protected kinds. Only the ordinary mob may be picked; the player and the quest giver give their own refusal. Self-defence is a
// different decision (the attackers loop), so nothing here speaks for a unit that is already fighting the bot.
#include "ai/playerbot/policy/world/FreeGrindPolicy.h"

#include <cstdio>
#include <cstring>

namespace w = TortoiseBots::world;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
struct Candidate
{
    char const* name;
    w::FreeGrindFacts facts;
    bool picked;                 // may a free bot start this fight?
    w::FreeGrindRefusal refusal; // the refusal when it may not
};
}

int main()
{
    // The ordinary world: a hostile or neutral NPC that is none of the protected kinds.
    w::FreeGrindFacts wolf;
    CHECK(w::FreeBotMayStartFight(wolf));
    CHECK(w::FreeGrindRefusalOf(wolf) == w::FreeGrindRefusal::None);

    // The scene, one candidate per rule. A player and a quest giver are both IN RANGE here (the caller sees no distance; range is its
    // own earlier gate) and both are refused.
    Candidate const scene[] = {
        { "Ragged Young Wolf",  wolf,                                                                        true,  w::FreeGrindRefusal::None },
        { "enemy player",       [] { w::FreeGrindFacts f; f.player = true; return f; }(),                     false, w::FreeGrindRefusal::Player },
        { "quest giver",        [] { w::FreeGrindFacts f; f.serviceNpc = true; return f; }(),                 false, w::FreeGrindRefusal::ServiceNpc },
        { "vendor",             [] { w::FreeGrindFacts f; f.serviceNpc = true; return f; }(),                 false, w::FreeGrindRefusal::ServiceNpc },
        { "trainer",            [] { w::FreeGrindFacts f; f.serviceNpc = true; return f; }(),                 false, w::FreeGrindRefusal::ServiceNpc },
        { "flight master",      [] { w::FreeGrindFacts f; f.serviceNpc = true; return f; }(),                 false, w::FreeGrindRefusal::ServiceNpc },
        { "innkeeper",          [] { w::FreeGrindFacts f; f.serviceNpc = true; return f; }(),                 false, w::FreeGrindRefusal::ServiceNpc },
        { "guard",              [] { w::FreeGrindFacts f; f.guard = true; return f; }(),                      false, w::FreeGrindRefusal::Guard },
        { "civilian",           [] { w::FreeGrindFacts f; f.civilian = true; return f; }(),                   false, w::FreeGrindRefusal::Civilian },
        { "player pet",         [] { w::FreeGrindFacts f; f.playerControlled = true; return f; }(),           false, w::FreeGrindRefusal::PlayerControlled },
        { "player guardian",    [] { w::FreeGrindFacts f; f.playerControlled = true; return f; }(),           false, w::FreeGrindRefusal::PlayerControlled },
        { "player totem",       [] { w::FreeGrindFacts f; f.playerControlled = true; return f; }(),           false, w::FreeGrindRefusal::PlayerControlled },
        { "friendly NPC",       [] { w::FreeGrindFacts f; f.friendly = true; return f; }(),                   false, w::FreeGrindRefusal::Friendly },
        { "reputation mob",     [] { w::FreeGrindFacts f; f.killLowersReputation = true; return f; }(),        false, w::FreeGrindRefusal::ReputationLowering },
        { "tapped mob",         [] { w::FreeGrindFacts f; f.tappedByAnotherPlayer = true; return f; }(),       false, w::FreeGrindRefusal::TappedByAnotherPlayer },
    };

    for (Candidate const& candidate : scene)
    {
        CHECK(w::FreeBotMayStartFight(candidate.facts) == candidate.picked);
        CHECK(w::FreeGrindRefusalOf(candidate.facts) == candidate.refusal);
        if (!candidate.picked)
            CHECK(std::strlen(w::FreeGrindRefusalText(candidate.refusal)) != 0);
    }

    // What the picker may return from the scene: the wolf alone. A neutral quest giver standing next to it changes nothing, and neither
    // does the player.
    char const* picked = nullptr;
    for (Candidate const& candidate : scene)
        if (w::FreeBotMayStartFight(candidate.facts))
            picked = candidate.name;
    CHECK(picked != nullptr);
    CHECK(picked && std::strcmp(picked, "Ragged Young Wolf") == 0);

    // The words the grind log uses for the two the report names.
    CHECK(std::strcmp(w::FreeGrindRefusalText(w::FreeGrindRefusalOf(w::FreeGrindFacts{ true })), "player") == 0);
    w::FreeGrindFacts questGiver;
    questGiver.serviceNpc = true;
    CHECK(std::strstr(w::FreeGrindRefusalText(w::FreeGrindRefusalOf(questGiver)), "quest giver") != nullptr);

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
