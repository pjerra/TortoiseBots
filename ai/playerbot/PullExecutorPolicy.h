#pragma once

// Pull executor selection policy (`.bot action pull|pullback`, `.bot cmd pull|pullback`).
//
// A pull needs a tank that is not fighting something: a tank mid-fight cannot
// walk up to the next mob. The core combat flag alone is not that test - it
// can linger with nothing attacking and nothing attacked (a stale flag; the
// same reason PlayerbotAI::IsGroupMemberNearInCombat demands a victim or an
// attacker beside the flag before it calls a member's fight real). A tank
// counts as busy only while the flag is backed by a live opponent: a victim
// or a non-empty attacker set. A flag with neither is stale, and the tank
// stays a pull candidate. That is the whole Naxx-vs-elsewhere shape of issue
// #908: nothing about the selection depends on the map, the target geometry
// or the tank's gear, so a refusal with live tanks on the same map is a tank
// the flag rules out - really fighting (answer `tank-busy`) or stale-flagged.
//
// Core-free, so tools/test_pull_executor_policy.cpp includes it directly.

namespace ai
{
    // One bot's pull-candidate state, translated by the caller so the policy
    // never touches game objects.
    struct PullCandidateFacts
    {
        bool alive = false;
        bool inWorld = false;
        bool sameMap = false;      // bot and requester on the same map
        bool hasAi = false;        // a usable PlayerbotAI is registered
        bool tankCapable = false;  // forced tank role or PlayerbotAI::IsTank
        bool pullReady = false;    // the combat engine holds the "pull" strategy
        bool combatFlag = false;   // the core combat flag (may be stale)
        bool hasVictim = false;    // the bot has a victim
        bool hasAttackers = false; // something attacks the bot
    };

    // Why a bot is not the pull executor. Order mirrors the selection gates:
    // world state, then the fight, then capability.
    enum class PullExcludeReason
    {
        None,           // free: this bot can pull
        NotInWorld,     // offline, teleporting or loading
        Dead,
        Busy,           // in a real fight (flag backed by victim or attackers)
        WrongMap,       // not on the requester's map
        NoAi,           // no usable AI registered
        NotTank,        // not tank-capable
        NoPullStrategy, // the "pull" strategy is missing and did not attach
    };

    // True while the bot fights something, as opposed to carrying a stale
    // combat flag with no opponent on either side.
    inline bool PullCandidateInRealFight(PullCandidateFacts const& facts)
    {
        return facts.combatFlag && (facts.hasVictim || facts.hasAttackers);
    }

    inline PullExcludeReason ClassifyPullCandidate(PullCandidateFacts const& facts)
    {
        if (!facts.inWorld)
            return PullExcludeReason::NotInWorld;
        if (!facts.alive)
            return PullExcludeReason::Dead;
        if (PullCandidateInRealFight(facts))
            return PullExcludeReason::Busy;
        if (!facts.sameMap)
            return PullExcludeReason::WrongMap;
        if (!facts.hasAi)
            return PullExcludeReason::NoAi;
        if (!facts.tankCapable)
            return PullExcludeReason::NotTank;
        if (!facts.pullReady)
            return PullExcludeReason::NoPullStrategy;
        return PullExcludeReason::None;
    }

    // True while the bot cannot pull no matter what the strategy attach does.
    // The selection tries to attach "pull" on demand, so a missing strategy
    // alone does not block; the refusal detail counts it instead.
    inline bool PullCandidateBlocked(PullCandidateFacts const& facts)
    {
        PullExcludeReason reason = ClassifyPullCandidate(facts);
        return reason != PullExcludeReason::None && reason != PullExcludeReason::NoPullStrategy;
    }

    // Short reason word for the refusal detail. Plain words only: the text
    // travels inside TBM:ACTION_ERR, so no '|' characters.
    inline char const* PullExcludeWord(PullExcludeReason reason)
    {
        switch (reason)
        {
            case PullExcludeReason::None: return "ready";
            case PullExcludeReason::NotInWorld: return "away";
            case PullExcludeReason::Dead: return "dead";
            case PullExcludeReason::Busy: return "in combat";
            case PullExcludeReason::WrongMap: return "on another map";
            case PullExcludeReason::NoAi: return "without AI";
            case PullExcludeReason::NotTank: return "non-tank";
            case PullExcludeReason::NoPullStrategy: return "without pull";
        }
        return "";
    }
}
