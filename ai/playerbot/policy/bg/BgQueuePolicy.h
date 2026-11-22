#pragma once

// BG1 (BATTLEGROUNDS-SPEC.md, decision D4/D5): who may be queued for a battleground, and who must be taken out of a queue again.
// Pure and core-free: the host (forever/src/host/bg/TbBgQueue.cpp) turns the bot manager, the Player and the battleground
// manager into BgQueueFacts, and this header answers with one refusal code or None. The refusal names are what the log and the
// `bg queue` event carry (`why=`), so a refused bot says why instead of silently not queueing.
//
// The eligibility list is D4's, in the spec's order: online, alive, not in a battleground or queue already, not in LFG or a
// `.tb run`, no Deserter aura, in a bracket of that map, a free pool bot (a bound bot joins only through its human's group), not
// in combat, and the D9 caps not reached.
//
// The second half answers the bucket questions the host asks every pass: how many bots a side still needs (queued and invited
// both count), whether the core may create an instance yet (BgVetoCreate), which refusals still hold when an invitation comes due
// (BgAcceptEligible), and when a module-queued bot gives up its queue entry (BgQueuedDrop).

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace bg
{

enum class BgRefusal : std::uint8_t
{
    None = 0,
    NotPool,        // not logged in by the pool scheduler
    Bound,          // has a master: only its human's group join takes it in
    Dead,
    InBattleground,
    InQueue,
    InLfg,
    InRun,
    Deserter,
    OutOfBracket,
    InCombat,
    MaxInstances,   // Bg.MaxInstancesWithBots reached by the instances already holding bots
    MaxBots,        // Bg.MaxBots reached by the bots already inside
};

inline char const* BgRefusalName(BgRefusal refusal)
{
    switch (refusal)
    {
        case BgRefusal::None: return "ok";
        case BgRefusal::NotPool: return "not-pool";
        case BgRefusal::Bound: return "bound";
        case BgRefusal::Dead: return "dead";
        case BgRefusal::InBattleground: return "in-battleground";
        case BgRefusal::InQueue: return "in-queue";
        case BgRefusal::InLfg: return "in-lfg";
        case BgRefusal::InRun: return "in-run";
        case BgRefusal::Deserter: return "deserter";
        case BgRefusal::OutOfBracket: return "out-of-bracket";
        case BgRefusal::InCombat: return "in-combat";
        case BgRefusal::MaxInstances: return "max-instances";
        case BgRefusal::MaxBots: return "max-bots";
    }
    return "unknown";
}

// The two D9 caps (config: Bg.MaxInstancesWithBots, Bg.MaxBots).
struct BgQueueCaps
{
    std::uint32_t instancesWithBots = 0;
    std::uint32_t maxInstances = 2;
    std::uint32_t botsInBattlegrounds = 0;
    std::uint32_t maxBots = 50;
    // The candidate's own (list, bracket) holds a queued reservation: the fill in progress paid for this bucket, and the
    // instance cap must not stop it half-filled. A match already running in the bracket is not that reservation.
    bool ownBucketCounted = false;
};

// One bot the module holds in a queue or a match, as the caps see it. A queued bot has no instance yet (0): it reserves its
// (list, bracket). An invited or inside bot names the instance it went to, so every state of one match counts once.
enum class BgTrackState : std::uint8_t
{
    Queued = 0,
    Invited = 1,
    Inside = 2,
};

struct BgTrackFact
{
    std::uint32_t listId = 0;
    std::uint32_t bracket = 0;
    std::uint32_t instance = 0;
    BgTrackState state = BgTrackState::Queued;
    // A queued bot whose humans are all inside (or invited to) an instance waits for the core's fill into that match; its queue
    // entry reserves nothing.
    bool demandPlaced = false;
};

// D9: every bot the module holds (queued, invited, inside) counts, and every match counts once. A running match is identified
// by its instance; a bucket the module queues into but no match covers yet reserves one instance of its own -- unless the humans
// it serves are already inside one, where the queue entry is only a leftover the core fills that match from. The two are kept
// apart: a match running in the same bracket does not pay for a queue that will create the next one, and a queued reservation
// is not hidden inside a running match's count. `candidateList`/`candidateBracket` say which bucket the bot being judged would
// join; only that bucket's own queued reservation is exempt from the instance cap.
inline BgQueueCaps BgCapsOfTracks(std::vector<BgTrackFact> const& tracks, std::uint32_t maxInstances, std::uint32_t maxBots,
                                  std::uint32_t candidateList = 0, std::uint32_t candidateBracket = 0)
{
    BgQueueCaps caps;
    caps.maxInstances = maxInstances;
    caps.maxBots = maxBots;
    caps.botsInBattlegrounds = std::uint32_t(tracks.size());
    std::set<std::pair<std::uint32_t, std::uint32_t>> instances;   // listId, instance id: a running match
    std::set<std::pair<std::uint32_t, std::uint32_t>> queued;      // a (listId, bracket) the fill reserves, no instance yet
    for (BgTrackFact const& track : tracks)
    {
        if (track.state == BgTrackState::Queued || !track.instance)
        {
            if (!track.demandPlaced)
                queued.emplace(track.listId, track.bracket);
            continue;
        }
        instances.emplace(track.listId, track.instance);
    }
    caps.instancesWithBots = std::uint32_t(instances.size() + queued.size());
    caps.ownBucketCounted = queued.count({ candidateList, candidateBracket }) != 0;
    return caps;
}

struct BgQueueFacts
{
    bool pool = false;          // BotMgr::IsPoolBot
    bool free = false;          // BotMgr::IsFreeBot (no master)
    bool alive = true;
    bool inBattleground = false;
    bool inQueue = false;
    bool inLfg = false;
    bool inRun = false;
    bool deserter = false;
    bool inBracket = false;     // the map has a bracket row for the bot's level
    bool inCombat = false;
    BgQueueCaps caps;
};

// The first fact that refuses, in the spec's order. A free pool bot in the world with no other claim is None.
inline BgRefusal BgQueueRefusalOf(BgQueueFacts const& facts)
{
    if (!facts.pool)
        return BgRefusal::NotPool;
    if (!facts.free)
        return BgRefusal::Bound;
    if (!facts.alive)
        return BgRefusal::Dead;
    if (facts.inBattleground)
        return BgRefusal::InBattleground;
    if (facts.inQueue)
        return BgRefusal::InQueue;
    if (facts.inLfg)
        return BgRefusal::InLfg;
    if (facts.inRun)
        return BgRefusal::InRun;
    if (facts.deserter)
        return BgRefusal::Deserter;
    if (!facts.inBracket)
        return BgRefusal::OutOfBracket;
    if (facts.inCombat)
        return BgRefusal::InCombat;
    // A candidate joining a bucket the module already counts (its human's own match) adds no instance: only Bg.MaxBots decides.
    if (!facts.caps.ownBucketCounted && facts.caps.instancesWithBots >= facts.caps.maxInstances)
        return BgRefusal::MaxInstances;
    if (facts.caps.botsInBattlegrounds >= facts.caps.maxBots)
        return BgRefusal::MaxBots;
    return BgRefusal::None;
}

inline bool BgQueueEligible(BgQueueFacts const& facts)
{
    return BgQueueRefusalOf(facts) == BgRefusal::None;
}

// The human has entered THIS track's match: the same battleground, and once the bot has an instance, that instance. A human who
// went into another battleground has left this queue (the core removed him from it), so he does not hold the bot's entry:
// `humanListId` 0 means the human is in no battleground, and the bot's `instance` 0 means its own invitation names none yet.
inline bool BgHumanInTrackMatch(std::uint32_t humanListId, std::uint32_t humanInstanceId, std::uint32_t listId,
                                std::uint32_t instance)
{
    if (!humanListId || humanListId != listId)
        return false;
    return !instance || humanInstanceId == instance;
}

// D5: a bound bot's queue entry follows its human. It stays only while that human is still connected and still holds the same
// queue (or has entered a match); a human who leaves the queue, cancels, logs out or ungroups leaves nothing for the core to
// take the bot out with, so the bot must leave by itself. `hasHuman` is false for a track with no human to follow, and
// `humanInBattleground` is `BgHumanInTrackMatch`'s answer, not any battleground.
inline bool BgBoundQueueHeld(bool hasHuman, bool humanConnected, bool humanInQueue, bool humanInBattleground)
{
    return hasHuman && humanConnected && (humanInQueue || humanInBattleground);
}

// D5: a bot that became ineligible while it waited (it got a master, or a run took it) leaves the queue at the next check.
// Being dead outside is not a reason to leave a queue the core accepted: the bot is revived before the match starts.
inline bool BgMustLeaveQueue(BgQueueFacts const& facts)
{
    return !facts.free || facts.inRun || facts.inBattleground || !facts.pool;
}

// ---- one (list, bracket) bucket: the fill, the orphan rule and the core's creation veto read the same view ----
//
// The core starts a battleground as soon as each side holds its own minimum (1 for WSG/AB on the pinned data), so a batch of bots
// queued in one pass would start a 1v1 and leave the rest waiting. The fill therefore keeps a bucket's queue at the target, and
// WorldScript::CanCreateBattleground vetoes a new instance until both sides are at the target (or the deficit window passed); the
// core's free-slot fill then moves the queued bots into the instance the human is in.
struct BgBucketFacts
{
    std::uint32_t target = 0;          // HumanBgSideTarget for the map
    std::uint32_t waiting[2] = {};     // players in the core's queue, uninvited, per side (0 = alliance, 1 = horde)
    std::uint32_t invited[2] = {};     // invited, not yet inside: the humans of the bucket and the module's bots
    bool humanWaiting = false;         // a non-bot session is queued or invited in this bucket
    bool moduleBots = false;           // the module queued at least one bot in this bucket
    bool deficitDone = false;          // a side stayed short for the whole deficit window
    bool autonomous = false;           // Bg.Autonomous: bot-only matches are allowed (D4.3)
};

// Everyone the bucket already holds on one side: the uninvited queue and the invitations in flight.
inline std::uint32_t BgSideHeld(BgBucketFacts const& facts, std::uint32_t side)
{
    return facts.waiting[side] + facts.invited[side];
}

// More bots wanted on one side.
inline std::uint32_t BgSideNeed(BgBucketFacts const& facts, std::uint32_t side)
{
    std::uint32_t const held = BgSideHeld(facts, side);
    return held >= facts.target ? 0 : facts.target - held;
}

// True while the core must not create an instance for this bucket: a human waits and the fill can still reach the target, or
// nobody but the module's bots is queued (D4.3 bot-only matches are off). A bucket whose pool cannot fill it is released after
// the deficit window, so an empty pool never blocks a real queue forever.
inline bool BgVetoCreate(BgBucketFacts const& facts)
{
    if (!facts.humanWaiting)
        return facts.moduleBots && !facts.autonomous;
    if (facts.deficitDone)
        return false;
    return BgSideNeed(facts, 0) != 0 || BgSideNeed(facts, 1) != 0;
}

// Refusals that still matter when an invitation comes due. A dead accepter is revived by the core and combat does not block
// entry (BattleGroundHandler.cpp:506), so declining for either only costs the slot and another queue round.
inline bool BgAcceptEligible(BgQueueFacts const& facts)
{
    return facts.pool && facts.free && !facts.inRun && !facts.inBattleground && !facts.deserter;
}

// An invitation only ever enters an instance that already exists, so the instance cap is not asked again: it was paid when the
// bots were queued, and the leftover entries of the same match would otherwise outnumber it and decline every accept. Only a
// Bg.MaxBots that dropped below what is already held refuses, since the two caps are inclusive.
inline bool BgAcceptCapsOver(BgQueueCaps const& caps)
{
    return caps.botsInBattlegrounds > caps.maxBots;
}

// A module-queued bot waits for a human: when the bucket lost its human it leaves after a short grace, and a queue entry that got
// no invitation either way is dropped so a pool session cannot sit in a queue forever.
inline constexpr std::uint32_t kOrphanQueueGraceMs = 10000;
inline constexpr std::uint32_t kQueuedTimeoutMs = 3 * 180000;

enum class BgQueueDrop : std::uint8_t
{
    None = 0,
    NoHuman,    // the bucket has no human session left
    Timeout,    // queued too long to be waiting for anything
};

inline char const* BgQueueDropWhy(BgQueueDrop drop)
{
    switch (drop)
    {
        case BgQueueDrop::None: return "";
        case BgQueueDrop::NoHuman: return "no-human";
        case BgQueueDrop::Timeout: return "queue-timeout";
    }
    return "unknown";
}

// `orphanMs` is how long the bucket has had no human session, `queuedMs` how long the bot has waited.
inline BgQueueDrop BgQueuedDrop(bool humanWaiting, std::uint32_t orphanMs, std::uint32_t queuedMs)
{
    if (queuedMs >= kQueuedTimeoutMs)
        return BgQueueDrop::Timeout;
    if (!humanWaiting && orphanMs >= kOrphanQueueGraceMs)
        return BgQueueDrop::NoHuman;
    return BgQueueDrop::None;
}

// The deficit window of D4.2: a side still short after it gets its one system line and stops holding the match back.
inline constexpr std::uint32_t kBgDeficitMs = 180000;

} // namespace bg
} // namespace TortoiseBots
