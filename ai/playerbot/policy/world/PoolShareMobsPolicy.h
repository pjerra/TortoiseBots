#pragma once

// #24: pool bots leave camps and quest mobs to the real players working them. A pool bot (`.tb pool` login, no master, not an
// owner's alt) does not START a fight on a mob a real player stands next to, nor on a mob that player's open quests need while
// he is near, does not walk to a grind spot he is working, and at one quest objective site only the first `QuestSpotCap` workers
// keep going (real players first, then companions, then pool bots, each by guid); a pool bot past the cap gives that objective
// up for `GiveUpSeconds`. Self-defence is not this rule: a mob already fighting the bot or its group is answered by the attackers
// loop of FindTargetForGrinding before any of this is asked.
//
// Pure (no core types): forever/tools/test_pool_share_mobs_policy.cpp checks every decision; the host (forever/src/host/
// TbHostShareMobs.cpp) rebuilds the real-player table once a second from the world thread and fills the facts from the core.
// The mob rule lives here so it can be read and tested on its own; the host only supplies distance-independent facts
// (the player row, the mob's map/position/entry, and whether the mob's loot has a quest drop for that player).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace world
{

// TortoiseBots.World.PoolShareMobs.*: switch, the two radii in yards, the workers one quest spot feeds, and how long a pool bot
// past the cap leaves the objective alone. The defaults are the spec's; the switch stays OFF until the live rows prove it.
struct ShareMobsConfig
{
    bool enabled = false;
    float nearYd = 30.0f;
    float farYd = 60.0f;
    std::uint32_t questSpotCap = 3;
    std::uint32_t giveUpSeconds = 600;
};

constexpr float ShareMobsNearMinYd = 5.0f;
constexpr float ShareMobsNearMaxYd = 100.0f;
constexpr float ShareMobsFarMaxYd = 150.0f;
constexpr std::uint32_t ShareMobsCapMin = 1;
constexpr std::uint32_t ShareMobsCapMax = 10;
constexpr std::uint32_t ShareMobsGiveUpMinSeconds = 60;
constexpr std::uint32_t ShareMobsGiveUpMaxSeconds = 3600;

// The config the host runs with: every key clamped to its range. The far ring can never be tighter than the near one (a config
// that asks for that reads as far = near), and the near radius is clamped first so that bound holds.
inline ShareMobsConfig ClampShareMobsConfig(ShareMobsConfig config)
{
    config.nearYd = std::clamp(config.nearYd, ShareMobsNearMinYd, ShareMobsNearMaxYd);
    config.farYd = std::clamp(config.farYd, config.nearYd, ShareMobsFarMaxYd);
    config.questSpotCap = std::clamp(config.questSpotCap, ShareMobsCapMin, ShareMobsCapMax);
    config.giveUpSeconds = std::clamp(config.giveUpSeconds, ShareMobsGiveUpMinSeconds, ShareMobsGiveUpMaxSeconds);
    return config;
}

// What the host knows about one bot when it asks whether #24 applies to it at all. Every field is one host fact:
// `poolBot`/`freeBot`/`owned` are BotMgr's own flags (`Login(..., pool=true)`, `.tb free`, `.bot add`), `hasRealPlayerMaster` is
// PlayerbotAI::HasRealPlayerMaster, `openGuildOrder` is tbhost::guild_orders::HasOpenOrder, `groupHasRealPlayer` is a party
// member with a network transport, and `mapInstanceable` is Map::Instanceable (dungeons, raids, battlegrounds and arenas never).
struct BotFacts
{
    bool switchOn = false;
    bool poolBot = false;
    bool freeBot = false;
    bool owned = false;
    bool hasRealPlayerMaster = false;
    bool openGuildOrder = false;
    bool groupHasRealPlayer = false;
    bool mapInstanceable = false;
};

// The one gate every part of #24 asks before it looks at a player or a mob. A bot-only quest group (TickQuestGroups) is not a
// real player, so a pool bot in one still applies.
constexpr bool ShareMobsApplies(BotFacts const& facts)
{
    return facts.switchOn && facts.poolBot && facts.freeBot && !facts.owned && !facts.hasRealPlayerMaster &&
        !facts.openGuildOrder && !facts.groupHasRealPlayer && !facts.mapInstanceable;
}

// One real player of the table: the position and instance the mob rule measures from, the party (a bot of his own party is
// never told to step aside from him), and what his open quests still need - the creature entries of his incomplete MONSTER
// objectives and the item ids of his incomplete ITEM objectives, both sorted and unique for a binary search.
struct PlayerRow
{
    std::uint64_t guid = 0;      // the player's guid low (ObjectGuid::GetCounter): identity, and the cap's tie-break
    std::uint32_t mapId = 0;
    std::uint32_t instanceId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint64_t groupGuid = 0; // group guid low, 0 = not in a party
    std::vector<std::uint32_t> monsterEntries;
    std::vector<std::uint32_t> itemIds;

    bool needsMonster(std::uint32_t entry) const
    {
        return std::binary_search(monsterEntries.begin(), monsterEntries.end(), entry);
    }

    bool needsItem(std::uint32_t itemId) const
    {
        return std::binary_search(itemIds.begin(), itemIds.end(), itemId);
    }
};

// What one of the player's quest objectives asks the world for: a kill (`Monster`), a dropped item (`Item`), or something
// neither a fight nor a loot roll can serve.
enum class QuestNeedKind : std::uint8_t
{
    None = 0,
    Monster = 1,
    Item = 2,
};

// One objective of one open quest into the row's lists. Only an incomplete objective that a kill or a drop can serve is kept:
// a finished objective keeps nothing - bots would otherwise step aside for a quest the player already handed in. `amount` and
// `objectId` are the objective's own fields (a zero of either asks for nothing). The caller sorts the lists once it is done;
// duplicates are allowed here and removed there.
inline void AddQuestObjective(PlayerRow& row, QuestNeedKind kind, std::uint32_t objectId, std::int32_t amount, bool complete)
{
    if (kind == QuestNeedKind::None || complete || amount <= 0 || objectId == 0)
        return;
    if (kind == QuestNeedKind::Monster)
        row.monsterEntries.push_back(objectId);
    else
        row.itemIds.push_back(objectId);
}

// A candidate mob as the rule sees it. `entry` is the creature entry (`Creature::GetEntry`), the key of the player's MONSTER list.
struct MobPoint
{
    std::uint32_t mapId = 0;
    std::uint32_t instanceId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t entry = 0;
};

enum class MobVerdict : std::uint8_t
{
    None,           // the bot may start this fight
    NearPlayer,     // within NearYd of a real player it is not grouped with: any mob, any faction
    QuestNearPlayer, // within FarYd of such a player whose open quests need its kill or its quest drop
};

inline float ShareMobsDistanceYd(PlayerRow const& player, MobPoint const& mob)
{
    float const dx = mob.x - player.x;
    float const dy = mob.y - player.y;
    float const dz = mob.z - player.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// One candidate mob against one real player. `lootNeeded` is the core's answer to "does this mob's loot hold a quest drop for
// that player" (LootTemplate::HasQuestDropForPlayer), asked by the host only inside the far ring and memoised per row per
// rebuild. A mob inside the near ring is skipped whatever its entry; between the two rings only a mob the player's open quests
// name (or whose loot they need) is. A same-party real player never makes the bot step aside, and another map or instance is a
// different world.
inline MobVerdict JudgeMob(ShareMobsConfig const& config, PlayerRow const& player, std::uint64_t botGroupGuid,
    MobPoint const& mob, bool lootNeeded)
{
    if (!config.enabled)
        return MobVerdict::None;
    if (player.mapId != mob.mapId || player.instanceId != mob.instanceId)
        return MobVerdict::None;
    if (player.groupGuid != 0 && player.groupGuid == botGroupGuid)
        return MobVerdict::None;

    float const distance = ShareMobsDistanceYd(player, mob);
    if (distance <= config.nearYd)
        return MobVerdict::NearPlayer;
    if (distance > config.farYd)
        return MobVerdict::None;
    if (player.needsMonster(mob.entry) || lootNeeded)
        return MobVerdict::QuestNearPlayer;
    return MobVerdict::None;
}

// Is this Grind destination point a spot a real player is working? The host asks it for every Grind point after
// TravelMgr::DropCrowdedGrindPoints, and for a held Grind target (TRAVEL or WORK) the tick expires.
inline bool SpotWorkedByPlayer(ShareMobsConfig const& config, std::vector<PlayerRow> const& players,
    std::uint32_t mapId, std::uint32_t instanceId, float x, float y, float z)
{
    if (!config.enabled)
        return false;
    for (PlayerRow const& player : players)
    {
        if (player.mapId != mapId || player.instanceId != instanceId)
            continue;
        float const dx = x - player.x;
        float const dy = y - player.y;
        float const dz = z - player.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) <= config.nearYd)
            return true;
    }
    return false;
}

// Who is working a quest objective site. The rank is the spec's precedence: a real player with that objective still open, a
// companion (a bot with a real master, or an owner's alt), then a pool bot. Ties are broken by guid, so the order is the same
// on every pass and in every process - no shared counter.
enum class WorkerRank : std::uint8_t
{
    RealPlayer = 0,
    Companion = 1,
    PoolBot = 2,
};

struct Worker
{
    WorkerRank rank = WorkerRank::PoolBot;
    std::uint64_t guid = 0;   // guid low, the tie-break inside a rank
};

inline void SortWorkers(std::vector<Worker>& workers)
{
    std::stable_sort(workers.begin(), workers.end(), [](Worker const& first, Worker const& second)
    {
        if (first.rank != second.rank)
            return static_cast<std::uint8_t>(first.rank) < static_cast<std::uint8_t>(second.rank);
        return first.guid < second.guid;
    });
}

// How many real players / companions / pool bots are in front of `selfGuid` at this site. The caller expires the target and
// books the objective when this answers true, and passes the same `cap` the host runs with.
inline bool PastQuestSpotCap(std::vector<Worker> workers, std::uint32_t cap, std::uint64_t selfGuid)
{
    SortWorkers(workers);
    for (std::size_t index = 0; index < workers.size(); ++index)
        if (workers[index].guid == selfGuid)
            return index >= cap;
    return false;
}

struct GiveUpEntry
{
    std::uint32_t questId = 0;
    std::uint32_t objectiveId = 0;   // QuestObjective::ID (the core's global objective row id)
    std::uint64_t untilMs = 0;
};

// One bot's given-up objectives: while a (quest, objective) is booked, QuestObjectiveTravelDestination::IsActive answers false
// for it, so the bot picks something else. A second booking of the same pair restarts the time; the book is dropped at logout.
class GiveUpBook
{
public:
    void GiveUp(std::uint32_t questId, std::uint32_t objectiveId, std::uint64_t nowMs, std::uint32_t seconds)
    {
        std::uint64_t const until = nowMs + static_cast<std::uint64_t>(seconds) * 1000u;
        for (GiveUpEntry& entry : _entries)
        {
            if (entry.questId == questId && entry.objectiveId == objectiveId)
            {
                entry.untilMs = until;
                return;
            }
        }
        _entries.push_back(GiveUpEntry{ questId, objectiveId, until });
    }

    bool IsGivenUp(std::uint32_t questId, std::uint32_t objectiveId, std::uint64_t nowMs) const
    {
        for (GiveUpEntry const& entry : _entries)
            if (entry.questId == questId && entry.objectiveId == objectiveId)
                return nowMs < entry.untilMs;
        return false;
    }

    void Clear() { _entries.clear(); }
    std::size_t Size() const { return _entries.size(); }

private:
    std::vector<GiveUpEntry> _entries;
};

} // namespace world
} // namespace TortoiseBots
