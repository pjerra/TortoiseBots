// Standalone check of host/TbEngineMapPure.h: the engine rule for a bot that stands on another map than its master. No game types, no server.
//   cd <worktree> && clang++ -std=c++20 -I forever/src forever/tools/test_engine_map.cpp -o /tmp/test_engine_map && /tmp/test_engine_map
#include "host/TbEngineMapPure.h"

#include <cstdio>

using TortoiseBots::EngineKeptOnOtherMap;

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
    // A party that landed in a dungeon while its commander stayed on the open-world map: the engine stays (the party's leader is on the bot's map, so the
    // engine follows that leader; the run needs it after the worldport).
    Check(EngineKeptOnOtherMap(true, true, true), "alive, in an instance, its party is there: the engine stays");
    // Alone in the instance (no party on its map): nothing to follow, the engine goes as before.
    Check(!EngineKeptOnOtherMap(true, true, false), "alive, in an instance, no party there: the engine goes");
    // Open world, another map than the master: the host's catch-up teleport reunites them, the engine waits (no follow across maps).
    Check(!EngineKeptOnOtherMap(true, false, true), "alive, open world, party on the map: the engine goes");
    Check(!EngineKeptOnOtherMap(true, false, false), "alive, open world, no party: the engine goes");
    // A body has its own dead-engine rule; this one never keeps an engine for a dead bot.
    Check(!EngineKeptOnOtherMap(false, true, true), "dead, in an instance with its party: not this rule");

    std::printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "RESULT: PASS", checks, failures);
    return failures ? 1 : 0;
}
