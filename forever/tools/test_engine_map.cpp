// Standalone check of host/TbEngineMapPure.h: the engine rule for a bot that stands on another map than its master. No game types, no server.
//   cd <worktree> && clang++ -std=c++20 -I forever/src forever/tools/test_engine_map.cpp -o /tmp/test_engine_map && /tmp/test_engine_map
#include "host/TbEngineMapPure.h"

#include <cstdio>

using TortoiseBots::EngineKeptOnOtherMap;
using TortoiseBots::PartyMemberOnMap;

namespace
{
int failures = 0;
int checks = 0;

void Check(bool ok, char const* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
}

int main()
{
    // A party that landed in a dungeon while its commander stayed on the open-world map: the engine stays (a party member is on the bot's map, so the
    // engine has its party to follow and fight with; the run needs it after the worldport).
    Check(EngineKeptOnOtherMap(true, true, true), "alive, in an instance, its party is there: the engine stays");
    // Alone in the instance (no party on its map): nothing to follow, the engine goes as before.
    Check(!EngineKeptOnOtherMap(true, true, false), "alive, in an instance, no party there: the engine goes");
    // Open world, another map than the master: the host's catch-up teleport reunites them, the engine waits (no follow across maps).
    Check(!EngineKeptOnOtherMap(true, false, true), "alive, open world, party on the map: the engine goes");
    Check(!EngineKeptOnOtherMap(true, false, false), "alive, open world, no party: the engine goes");
    // A body has its own dead-engine rule; this one never keeps an engine for a dead bot.
    Check(!EngineKeptOnOtherMap(false, true, true), "dead, in an instance with its party: not this rule");

    // "Its party is here" is a question about the group's MEMBERS, not about who leads: one member that is another character, in the world, not between
    // maps, on the bot's exact map (the same Map, so the same instance) is enough. The bot's own row never counts, so a lone leader has no party.
    Check(!PartyMemberOnMap(true, true, false, true), "the bot itself in its own group is no party beside it");
    Check(PartyMemberOnMap(false, true, false, true), "another member on the bot's map: its party is here");
    Check(!PartyMemberOnMap(false, false, false, true), "the member is not in the world: not here");
    Check(!PartyMemberOnMap(false, true, true, true), "the member is between maps: not here");
    Check(!PartyMemberOnMap(false, true, false, false), "the member is on another map (or another instance of the bot's map id): not here");
    Check(!PartyMemberOnMap(false, false, true, false), "not in the world, between maps, another map: not here");
    // The review's two cases: a group LEADER is not a lone member. Leading the group says nothing about the members standing beside it, and the
    // instance is split from the commander, not from the party.
    Check(!EngineKeptOnOtherMap(true, true, PartyMemberOnMap(true, true, false, true)), "a truly lone self-leader's engine goes, as for no party");
    Check(EngineKeptOnOtherMap(true, true, PartyMemberOnMap(false, true, false, true)),
        "a self-leader with another member in its instance keeps its engine");

    std::printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "RESULT: PASS", checks, failures);
    return failures ? 1 : 0;
}
