#pragma once

// When a pool bot is "spawned": its first login, decided like TB's start-level and gear-seed block
// (runtime/BotManager.cpp:1417-1470 in TortoiseBots). A spawn is
//   - a pool bot (BotMgr::IsPoolBot), never a bot with a master and never a player;
//   - fresh: played time 0 (characters.totaltime) and no persisted spawn row;
//   - finished once: an in-process mark and the `tortoise_bots_player_flag` row keep a relog from spawning it twice,
//     even when the first session ended before the core saved a non-zero played time.
//
// The start level and the kit are both applied on that one login; a bot that spawns below level 5 keeps the starter
// outfit for good (it is not seeded later when it levels up).
//
// Pure and core-free, so tools/test_pool_spawn_policy.cpp includes it directly.

#include <cstdint>

namespace TortoiseBots
{
inline constexpr std::uint32_t kSpawnLevelLimit = 60;

inline bool PoolSpawnSubject(bool isPoolBot, bool hasMaster)
{
    return isPoolBot && !hasMaster;
}

// `spawnFlag` is the persisted "this bot already spawned" row.
inline bool NeedsPoolSpawn(std::uint32_t playedTimeSeconds, bool spawnFlag)
{
    return playedTimeSeconds == 0 && !spawnFlag;
}

struct SpawnLevelRange
{
    std::uint32_t minLevel = 1;
    std::uint32_t maxLevel = 1;
};

// AiPlayerbot.RandomBotStartLevelMin / .Max, clamped to the levels this build's gear tables cover. A maximum below
// the minimum is raised to the minimum, as TB's urand call does.
inline SpawnLevelRange NormalizeSpawnLevelRange(std::uint32_t minLevel, std::uint32_t maxLevel)
{
    std::uint32_t const lo = minLevel < 1 ? 1 : (minLevel > kSpawnLevelLimit ? kSpawnLevelLimit : minLevel);
    std::uint32_t hi = maxLevel < 1 ? 1 : (maxLevel > kSpawnLevelLimit ? kSpawnLevelLimit : maxLevel);
    if (hi < lo)
        hi = lo;
    return { lo, hi };
}
}
