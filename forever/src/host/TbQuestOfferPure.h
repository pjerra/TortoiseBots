#pragma once

// Core-free policy for the quest-target group offer (SPEC #42, round 1): which real player a pool bot may
// offer to, which bot offers, the two cooldowns, the 20 s target memory, the invite expiry and the release
// rules. Every fact is passed in, so forever/tools/test_quest_offer_pure.cpp proves the decisions without the
// server; the host service in TbHostQuestOffer.cpp (round 2) only gathers the facts and calls these.
//
// The player sees: a pool bot of his faction, within +/-5 levels and 30 yd, that fights the same quest mob,
// invites him and whispers "We're both after <creature>. Want to group up?". New behaviour is off by default.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tbhost
{
namespace quest_offer
{

// Config defaults and the ranges the host loader clamps them to (tortoisebots.conf.dist, sConfigMgr).
inline constexpr uint32_t kDefaultRangeYd = 30;
inline constexpr uint32_t kMinRangeYd = 10;
inline constexpr uint32_t kMaxRangeYd = 60;
inline constexpr uint32_t kDefaultPlayerCooldownMin = 30;
inline constexpr uint32_t kMinPlayerCooldownMin = 5;
inline constexpr uint32_t kMaxPlayerCooldownMin = 240;
inline constexpr uint32_t kDefaultBotCooldownMin = 10;
inline constexpr uint32_t kMinBotCooldownMin = 1;
inline constexpr uint32_t kMaxBotCooldownMin = 120;
inline constexpr uint32_t kDefaultInviteExpireSec = 60;
inline constexpr uint32_t kMinInviteExpireSec = 20;
inline constexpr uint32_t kMaxInviteExpireSec = 120;
inline constexpr uint32_t kDefaultMaxMinutes = 30;
inline constexpr uint32_t kMinMaxMinutes = 5;
inline constexpr uint32_t kMaxMaxMinutes = 120;

// The spec's fixed limits: scan every 5 s and at most 2 offers per scan.
inline constexpr uint32_t kScanIntervalMs = 5000;
inline constexpr uint32_t kMaxOffersPerScan = 2;

// The player's GetVictim() entry is remembered this long after the last sighting.
inline constexpr uint32_t kVictimMemorySec = 20;
// The bot must be within this many levels of the player.
inline constexpr int32_t kLevelWindow = 5;
// Release when the player has been this far away for this long.
inline constexpr float kFarDistanceYd = 200.0f;
inline constexpr uint32_t kFarReleaseSec = 120;

// The whisper and the line the bot says when it releases (SPEC player-visible text).
inline constexpr char const* kReleaseLine = "Got what I needed, thanks!";

inline std::string OfferLine(std::string_view creatureName)
{
    std::string out = "We're both after ";
    out.append(creatureName);
    out += ". Want to group up?";
    return out;
}

// What the host knows about the real player this tick. `faction` is the player's team side (the bot's must
// match); `level` feeds the +/-5 window in BotEligible.
struct PlayerFacts
{
    bool isReal = false;          // !IsKnownBot
    bool alive = false;
    bool inGroup = false;
    bool hasPendingInvite = false;
    bool dnd = false;
    bool onTaxi = false;
    bool ignoresBot = false;
    bool instanceableMap = false; // dungeons and battlegrounds never offer
    bool needsTarget = false;     // an incomplete MONSTER objective or quest loot for the remembered entry
    uint32_t faction = 0;
    uint8_t level = 0;
};

// The player is offered to only when he is a real, living, ungrouped, free player on a non-instance map,
// needs the target himself, and does not shut the bot out (DND, taxi, ignore).
inline bool PlayerEligible(PlayerFacts const& player, uint32_t botFaction)
{
    if (!player.isReal || !player.alive)
        return false;
    if (player.inGroup || player.hasPendingInvite)
        return false;
    if (player.dnd || player.onTaxi || player.ignoresBot)
        return false;
    if (player.instanceableMap)
        return false;
    if (!player.needsTarget)
        return false;
    return player.faction == botFaction;
}

// What the host knows about one pool bot this tick. `victimEntry` is its current combat victim (0 = none);
// `needsTarget` says the bot has an incomplete MONSTER objective or quest loot for the target entry;
// `distanceYd` is the bot-to-player distance on the same map.
struct BotFacts
{
    uint32_t guidLow = 0;
    uint32_t faction = 0;
    uint8_t level = 0;
    bool pool = false;
    bool free = false;
    bool owned = false;
    bool hasMaster = false;
    bool hasBrain = false;
    bool alive = false;
    bool inGroup = false;
    bool hasInvite = false;
    bool inCombat = false;
    uint32_t victimEntry = 0;
    bool needsTarget = false;
    double distanceYd = 0.0;
    double rangeYd = double(kDefaultRangeYd);
};

// The bot offers only while it is a free, unowned pool bot with a brain and no group or invite; it is alive,
// of the player's faction, level-close, needs the same entry, is out of combat or fighting that entry, and is
// within range.
inline bool BotEligible(BotFacts const& bot, PlayerFacts const& player, uint32_t targetEntry)
{
    if (!targetEntry)
        return false;
    if (!bot.pool || !bot.free || bot.owned || bot.hasMaster || !bot.hasBrain)
        return false;
    if (!bot.alive || bot.inGroup || bot.hasInvite)
        return false;
    if (bot.faction != player.faction)
        return false;
    int32_t const levelDiff = int32_t(bot.level) - int32_t(player.level);
    if (levelDiff > kLevelWindow || levelDiff < -kLevelWindow)
        return false;
    if (!bot.needsTarget)
        return false;
    if (bot.inCombat && bot.victimEntry != targetEntry)
        return false;
    if (bot.distanceYd < 0.0 || bot.distanceYd > bot.rangeYd)
        return false;
    return true;
}

// When several bots qualify, the nearest offers; the lowest guid breaks ties. 0 = nobody offers.
inline uint32_t PickBot(std::vector<BotFacts> const& eligible)
{
    uint32_t best = 0;
    double bestDistance = 0.0;
    for (BotFacts const& bot : eligible)
    {
        if (!bot.guidLow)
            continue;
        if (!best || bot.distanceYd < bestDistance ||
            (bot.distanceYd == bestDistance && bot.guidLow < best))
        {
            best = bot.guidLow;
            bestDistance = bot.distanceYd;
        }
    }
    return best;
}

// One offer per cooldown, whatever the outcome (accepted, declined, expired, ignored). A first offer is
// never blocked; a clock that went backwards never opens the gate.
inline bool CooldownOk(uint64_t nowSec, uint64_t lastSec, bool hasOffered, uint32_t cooldownMin)
{
    if (!hasOffered)
        return true;
    if (nowSec < lastSec)
        return false;
    return nowSec - lastSec >= uint64_t(cooldownMin) * 60u;
}

// The player's offer memory: the last offer time (the player cooldown runs from it) and the invite still
// standing. Kept in RAM by the host service.
struct PendingInvite
{
    uint32_t botGuidLow = 0;
    uint64_t startedSec = 0;
    bool active = false;
};

struct OfferRecord
{
    bool hasOffered = false;
    uint64_t lastOfferSec = 0;
    PendingInvite invite;
};

// The bot's own offer memory: one offer per BotCooldownMin, independent of the player's.
struct BotOfferRecord
{
    bool hasOffered = false;
    uint64_t lastOfferSec = 0;
};

inline OfferRecord RecordOffer(OfferRecord record, uint32_t botGuidLow, uint64_t nowSec)
{
    record.hasOffered = true;
    record.lastOfferSec = nowSec;
    record.invite.botGuidLow = botGuidLow;
    record.invite.startedSec = nowSec;
    record.invite.active = true;
    return record;
}

inline BotOfferRecord RecordBotOffer(BotOfferRecord record, uint64_t nowSec)
{
    record.hasOffered = true;
    record.lastOfferSec = nowSec;
    return record;
}

// The invite ran past InviteExpireSec: the host calls UninviteFromGroup on the player while the invite is
// still the bot's group, then EndInvite records the outcome.
inline bool InviteExpired(PendingInvite const& invite, uint64_t nowSec, uint32_t expireSec)
{
    if (!invite.active)
        return false;
    if (nowSec < invite.startedSec)
        return false;
    return nowSec - invite.startedSec >= uint64_t(expireSec);
}

// Ends the invite (expiry, refusal or accept); the offer time stays so the player cooldown runs from it.
inline OfferRecord EndInvite(OfferRecord record, uint64_t /*nowSec*/)
{
    record.invite = PendingInvite{};
    return record;
}

// The player's GetVictim() entry, remembered for 20 s after the last sighting so a target that flickers
// between hits still counts. Seeing the same entry again refreshes the window; another entry replaces it.
struct VictimMemory
{
    bool has = false;
    uint32_t entry = 0;
    uint64_t seenSec = 0;
};

inline VictimMemory RememberVictim(VictimMemory memory, uint32_t entry, uint64_t nowSec)
{
    if (!entry)
        return VictimMemory{};
    if (memory.has && memory.entry == entry)
    {
        memory.seenSec = nowSec;
        return memory;
    }
    VictimMemory out;
    out.has = true;
    out.entry = entry;
    out.seenSec = nowSec;
    return out;
}

inline bool VictimMemoryValid(VictimMemory const& memory, uint32_t entry, uint64_t nowSec)
{
    if (!memory.has || !entry || memory.entry != entry)
        return false;
    if (nowSec < memory.seenSec)
        return false;
    return nowSec - memory.seenSec <= uint64_t(kVictimMemorySec);
}

// Why a quest-buddy bot leaves the player's group and goes back to the pool.
enum class ReleaseReason : uint8_t
{
    None = 0,
    PlayerLeft,   // the player left the group, or the group is gone
    MapChange,    // the player is on another map
    InstanceMap,  // the player is in a dungeon or battleground
    TooFar,       // the player has been over 200 yd away for 120 s
    NeedEnded,    // the bot no longer needs the mob
    MaxMinutes,   // MaxMinutes have passed since the accept
};

inline char const* ReleaseReasonName(ReleaseReason reason)
{
    switch (reason)
    {
        case ReleaseReason::None: return "none";
        case ReleaseReason::PlayerLeft: return "player-left";
        case ReleaseReason::MapChange: return "map-change";
        case ReleaseReason::InstanceMap: return "instance";
        case ReleaseReason::TooFar: return "too-far";
        case ReleaseReason::NeedEnded: return "need-ended";
        case ReleaseReason::MaxMinutes: return "max-minutes";
    }
    return "unknown";
}

// What the host knows about the group before deciding to release. `inGroup` is the player still being a
// member of the bot's group; `groupedSec` counts from the accept.
struct ReleaseFacts
{
    bool inGroup = false;
    bool sameMap = false;
    bool playerInInstance = false;
    uint64_t farSec = 0;
    bool botNeedsTarget = false;
    uint64_t groupedSec = 0;
    uint32_t maxMinutes = kDefaultMaxMinutes;
};

inline ReleaseReason ShouldRelease(ReleaseFacts const& facts)
{
    if (!facts.inGroup)
        return ReleaseReason::PlayerLeft;
    if (!facts.sameMap)
        return ReleaseReason::MapChange;
    if (facts.playerInInstance)
        return ReleaseReason::InstanceMap;
    if (facts.farSec >= uint64_t(kFarReleaseSec))
        return ReleaseReason::TooFar;
    if (!facts.botNeedsTarget)
        return ReleaseReason::NeedEnded;
    if (facts.groupedSec >= uint64_t(facts.maxMinutes) * 60u)
        return ReleaseReason::MaxMinutes;
    return ReleaseReason::None;
}

} // namespace quest_offer
} // namespace tbhost
