// The startup pool reset's decisions. Header-only and free of core/database
// types: the rules that must not regress live here, where tools/test_pool_reset_safety_policy.cpp exercises them,
// and the reset (forever/src/host/pool/TbPoolReset.cpp) only gathers the facts they take.
//
// Two rules matter most:
//  - a character is deleted only when it is a `ready` pool row, its account is still the pool's, the characters
//    database still puts it on that account, no human session holds the account and it owns no auction listing;
//  - a guild led by a target is not touched while one member is outside the managed pool (no character row, an
//    account outside the pool, or a character on a pool account without a ready pool row).
//
// The setting parser and the one-shot generation comparison are runtime/policy/infra/PoolResetPolicy.h's; this file
// never duplicates them.
#pragma once

#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace world
{

// What the reset does with one target on its own world tick.
enum class PoolResetTargetAction : std::uint8_t
{
    Delete,   // every fact holds: the core deletion may be issued now
    Wait,     // the character is still a live bot or a Player in the world: drain it first, bounded
    Gone,     // the characters table no longer has it (an interrupted earlier reset): the row goes, nothing is deleted
    Refuse,   // a fact that must never be overridden: stop the reset, delete nothing more
};

// One target, re-read immediately before its deletion (nothing is cached from planning).
struct PoolResetTargetFacts
{
    bool rowReady = false;         // the tb_pool_character row is still `ready`
    bool accountTrusted = false;   // the account row still passes the pool's name/id/stamp proof
    bool sameAccount = false;      // the characters table still puts the character on the row's account
    bool guidExists = false;       // the characters table answered a row for the guid
    bool humanSession = false;     // a session that is not headless holds the target's account
    bool ownsAuctions = false;     // the character owns an `auctionhouse` listing
    bool inWorld = false;          // a Player object for the guid is in the world
    bool botRegistered = false;    // BotMgr still holds the guid
};

inline PoolResetTargetAction DecidePoolResetTarget(PoolResetTargetFacts const& facts)
{
    // Ownership first: a row that stopped being the pool's is never deleted, whatever else is true.
    if (!facts.rowReady || !facts.accountTrusted || !facts.sameAccount)
        return PoolResetTargetAction::Refuse;
    // A human on the account is final. It is checked before the presence test so a failed read cannot hide it.
    if (facts.humanSession)
        return PoolResetTargetAction::Refuse;
    if (!facts.guidExists)
        return PoolResetTargetAction::Gone;
    // The core's DeleteFromDB leaves `auctionhouse` rows behind; deleting the character would orphan them unrefunded.
    if (facts.ownsAuctions)
        return PoolResetTargetAction::Refuse;
    if (facts.inWorld || facts.botRegistered)
        return PoolResetTargetAction::Wait;
    return PoolResetTargetAction::Delete;
}

inline char const* PoolResetTargetActionText(PoolResetTargetAction action)
{
    switch (action)
    {
        case PoolResetTargetAction::Delete: return "delete";
        case PoolResetTargetAction::Wait:   return "wait";
        case PoolResetTargetAction::Gone:   return "gone";
        case PoolResetTargetAction::Refuse: return "refuse";
    }
    return "refuse";
}

// One member of a guild led by a target, as the reset reads it.
struct PoolResetMemberFacts
{
    std::uint64_t guid = 0;
    bool characterKnown = false;  // the characters table answered a row for the guid
    bool poolAccount = false;     // the member's account is a ready, trusted pool account
    bool readyPoolRow = false;    // a ready tb_pool_character row names the guid
};

// A guild only ever holds the managed pool. A dangling guild_member row (no character) is outside the pool:
// deleting the target would let the core promote or disband around somebody the module does not own.
inline bool PoolResetMemberOutsidePool(PoolResetMemberFacts const& member)
{
    return !member.characterKnown || !member.poolAccount || !member.readyPoolRow;
}

// The first member that blocks the guild (nullptr when every member is a pool character).
inline PoolResetMemberFacts const* FirstPoolResetOutsider(std::vector<PoolResetMemberFacts> const& members)
{
    for (PoolResetMemberFacts const& member : members)
        if (PoolResetMemberOutsidePool(member))
            return &member;
    return nullptr;
}

// Planning order. Skip = nothing to do (off, already applied, the pool is off); Refuse = a configured reset that must
// not run, and no character may be deleted. `reason` is a short diagnostic for the caller's detailed log line.
enum class PoolResetPlanAction : std::uint8_t
{
    Schedule,
    Skip,
    Refuse,
};

struct PoolResetPlanFacts
{
    bool settingOff = false;
    bool settingInvalid = false;
    bool poolEnabled = false;
    bool refillPossible = false;      // TortoiseBots.Pool.CreatePerMinute > 0
    bool stateAnswered = false;       // tb_pool_state could be read
    bool generationPending = false;   // ShouldResetForGeneration
    bool targetsAnswered = false;     // the characters database answered the target scan
    bool accountsTrusted = false;     // every target account still is the pool's
    bool noHumanSession = true;
    bool noAuctions = true;
    bool guildsClear = true;
};

inline PoolResetPlanAction DecidePoolResetPlan(PoolResetPlanFacts const& facts, char const** reason = nullptr)
{
    if (reason)
        *reason = nullptr;
    if (facts.settingOff)
        return PoolResetPlanAction::Skip;
    if (facts.settingInvalid)
    {
        if (reason) *reason = "the setting is not 'off', 'always' or 'once:<token>'";
        return PoolResetPlanAction::Refuse;
    }
    if (!facts.poolEnabled)
        return PoolResetPlanAction::Skip;
    if (!facts.refillPossible)
    {
        if (reason) *reason = "TortoiseBots.Pool.CreatePerMinute is 0, so a reset could not refill";
        return PoolResetPlanAction::Refuse;
    }
    // Without the recorded generation a one-shot token cannot be compared, so nothing is scheduled.
    if (!facts.stateAnswered)
    {
        if (reason) *reason = "the applied generation could not be read";
        return PoolResetPlanAction::Refuse;
    }
    if (!facts.generationPending)
        return PoolResetPlanAction::Skip;
    if (!facts.targetsAnswered)
    {
        if (reason) *reason = "the characters database did not answer which characters the pool holds";
        return PoolResetPlanAction::Refuse;
    }
    if (!facts.accountsTrusted)
    {
        if (reason) *reason = "a target account is no longer the pool's";
        return PoolResetPlanAction::Refuse;
    }
    if (!facts.noHumanSession)
    {
        if (reason) *reason = "a target account carries a session that is not a headless bot";
        return PoolResetPlanAction::Refuse;
    }
    if (!facts.noAuctions)
    {
        if (reason) *reason = "a target owns an auction listing the core deletion does not remove";
        return PoolResetPlanAction::Refuse;
    }
    if (!facts.guildsClear)
    {
        if (reason) *reason = "a guild led by a target holds a member outside the managed pool";
        return PoolResetPlanAction::Refuse;
    }
    return PoolResetPlanAction::Schedule;
}

// Characters on the pool's accounts that the reset keeps: every guid the target list does not name. The live realm
// holds characters on pool accounts without a ready pool row; they are somebody's, and a reset keeps them.
inline std::uint32_t CountPoolResetKeptCharacters(std::vector<std::uint64_t> const& accountCharacters,
    std::vector<std::uint64_t> const& targets)
{
    std::uint32_t kept = 0;
    for (std::uint64_t const guid : accountCharacters)
    {
        bool targeted = false;
        for (std::uint64_t const target : targets)
        {
            if (target == guid)
            {
                targeted = true;
                break;
            }
        }
        if (!targeted)
            ++kept;
    }
    return kept;
}

} // namespace world
} // namespace TortoiseBots
