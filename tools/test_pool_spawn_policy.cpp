// Standalone policy check for when a pool bot spawns (runtime/PoolSpawnPolicy.h) and the start-level range.
// Build: g++ -std=c++17 -Wall -Wextra -I. tools/test_pool_spawn_policy.cpp -o /tmp/t
#include "../runtime/PoolSpawnPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

int main()
{
    // Only a masterless pool bot is a spawn subject; a player is never passed here.
    CHECK(PoolSpawnSubject(true, false));
    CHECK(!PoolSpawnSubject(true, true));
    CHECK(!PoolSpawnSubject(false, false));
    CHECK(!PoolSpawnSubject(false, true));

    // Seed-once truth table: fresh played time and no spawn row.
    CHECK(NeedsPoolSpawn(0, false));
    CHECK(!NeedsPoolSpawn(0, true));
    CHECK(!NeedsPoolSpawn(1, false));
    CHECK(!NeedsPoolSpawn(1, true));
    CHECK(!NeedsPoolSpawn(3600, false));
    CHECK(!NeedsPoolSpawn(0xffffffffu, false));

    // A spawn is both facts: the bot is a masterless pool bot AND it is fresh.
    CHECK(PoolSpawnSubject(true, false) && NeedsPoolSpawn(0, false));
    CHECK(!(PoolSpawnSubject(true, true) && NeedsPoolSpawn(0, false)));
    CHECK(!(PoolSpawnSubject(false, false) && NeedsPoolSpawn(0, false)));
    CHECK(!(PoolSpawnSubject(true, false) && NeedsPoolSpawn(0, true)));

    // Start-level range: 1..60, minimum wins over a lower maximum, a maximum above the cap clamps.
    {
        SpawnLevelRange r = NormalizeSpawnLevelRange(1, 60);
        CHECK(r.minLevel == 1 && r.maxLevel == 60);
        r = NormalizeSpawnLevelRange(5, 60);
        CHECK(r.minLevel == 5 && r.maxLevel == 60);
        r = NormalizeSpawnLevelRange(0, 0);
        CHECK(r.minLevel == 1 && r.maxLevel == 1);
        r = NormalizeSpawnLevelRange(1, 0);
        CHECK(r.minLevel == 1 && r.maxLevel == 1);
        r = NormalizeSpawnLevelRange(50, 10);
        CHECK(r.minLevel == 50 && r.maxLevel == 50);
        r = NormalizeSpawnLevelRange(70, 80);
        CHECK(r.minLevel == 60 && r.maxLevel == 60);
        r = NormalizeSpawnLevelRange(10, 80);
        CHECK(r.minLevel == 10 && r.maxLevel == 60);
    }

    std::printf("PASS: test_pool_spawn_policy (%d checks, 0 failed)\n", checks);
    return 0;
}
