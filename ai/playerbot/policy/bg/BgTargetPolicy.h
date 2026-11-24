#pragma once

// Who a bot hits first inside a battleground (BATTLEGROUNDS-SPEC.md D9, round BG2). Core-free: the host lists the enemy players the
// engine already found ("enemy player targets": hostile, valid, in sight) as candidates with the facts below and takes the handle
// of the pick back; nothing here knows a Unit.
//
// Order, first rule that finds anyone wins, ties go to the nearer enemy:
//   0 a bound bot assists its human's target (inside kBgFlagCarrierYd), before everything else
//   1 the enemy flag carrier inside kBgFlagCarrierYd
//   2 an enemy hitting our flag carrier or one of our healers, inside kBgDefendYd
//   3 an enemy healer inside kBgNearYd
//   4 the enemy with the least health inside kBgNearYd
//   5 whoever is attacking me, at any range
// The leash is applied first: with an objective, an enemy more than kBgLeashYd from it is not chased, except the carrier and an
// enemy that is already attacking the bot (that is no chase, the bot is being hit). A bound bot has no objective.

#include <cstdint>
#include <vector>

#include "BgLimits.h"

namespace TortoiseBots
{
namespace bg
{

enum class BgTargetRule : std::uint8_t
{
    None = 0,
    MasterTarget,
    FlagCarrier,
    Defend,
    Healer,
    Weakest,
    MyAttacker,
};

struct BgTargetCandidate
{
    std::uint32_t handle = 0;               // the host's index of this enemy; returned in the pick
    float distanceYd = 0;                // from the bot
    float objectiveDistanceYd = 0;       // from the bot's objective; read only when BgTargetContext::hasObjective
    float healthPct = 100;
    bool isFlagCarrier = false;             // holds the flag of the bot's side (the carrier aura)
    bool hitsOurCarrier = false;            // its victim is our flag carrier
    bool hitsOurHealer = false;             // its victim is one of our healers
    bool isHealer = false;                  // a healing spec (the engine's "enemy healer target" answer)
    bool isMyAttacker = false;
    bool isMasterTarget = false;            // the unit the bot's human has selected
};

struct BgTargetContext
{
    bool bound = false;                     // has a human master (a bound bot escorts him, D7)
    bool hasObjective = false;              // an objective position is set on this map (BG3 sets it); the leash needs it
};

struct BgTargetPick
{
    bool found = false;
    std::uint32_t handle = 0;
    BgTargetRule rule = BgTargetRule::None;
};

namespace detail
{

// Index of the best candidate among those `accept` takes, by `better`; -1 when none.
template <typename Accept, typename Better>
inline int BgBest(std::vector<BgTargetCandidate> const& list, std::vector<bool> const& alive, Accept accept, Better better)
{
    int best = -1;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (!alive[i] || !accept(list[i]))
            continue;
        if (best < 0 || better(list[i], list[std::size_t(best)]))
            best = int(i);
    }
    return best;
}

inline bool BgNearer(BgTargetCandidate const& a, BgTargetCandidate const& b) { return a.distanceYd < b.distanceYd; }

} // namespace detail

inline BgTargetPick BgPickTarget(std::vector<BgTargetCandidate> const& list, BgTargetContext const& context)
{
    // The leash first: a candidate that is out stays out of every rule.
    std::vector<bool> alive(list.size(), true);
    if (context.hasObjective && !context.bound)
    {
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].objectiveDistanceYd > kBgLeashYd && !list[i].isFlagCarrier && !list[i].isMyAttacker)
                alive[i] = false;
    }

    auto const take = [&](int index, BgTargetRule rule) -> BgTargetPick
    {
        BgTargetPick pick;
        pick.found = true;
        pick.handle = list[std::size_t(index)].handle;
        pick.rule = rule;
        return pick;
    };

    int index = -1;
    if (context.bound)
    {
        index = detail::BgBest(list, alive,
            [](BgTargetCandidate const& c) { return c.isMasterTarget && c.distanceYd <= kBgFlagCarrierYd; },
            detail::BgNearer);
        if (index >= 0)
            return take(index, BgTargetRule::MasterTarget);
    }

    index = detail::BgBest(list, alive,
        [](BgTargetCandidate const& c) { return c.isFlagCarrier && c.distanceYd <= kBgFlagCarrierYd; }, detail::BgNearer);
    if (index >= 0)
        return take(index, BgTargetRule::FlagCarrier);

    index = detail::BgBest(list, alive,
        [](BgTargetCandidate const& c) { return (c.hitsOurCarrier || c.hitsOurHealer) && c.distanceYd <= kBgDefendYd; },
        detail::BgNearer);
    if (index >= 0)
        return take(index, BgTargetRule::Defend);

    index = detail::BgBest(list, alive,
        [](BgTargetCandidate const& c) { return c.isHealer && c.distanceYd <= kBgNearYd; }, detail::BgNearer);
    if (index >= 0)
        return take(index, BgTargetRule::Healer);

    index = detail::BgBest(list, alive,
        [](BgTargetCandidate const& c) { return c.distanceYd <= kBgNearYd; },
        [](BgTargetCandidate const& a, BgTargetCandidate const& b)
        {
            if (a.healthPct != b.healthPct)
                return a.healthPct < b.healthPct;
            return a.distanceYd < b.distanceYd;
        });
    if (index >= 0)
        return take(index, BgTargetRule::Weakest);

    index = detail::BgBest(list, alive, [](BgTargetCandidate const& c) { return c.isMyAttacker; }, detail::BgNearer);
    if (index >= 0)
        return take(index, BgTargetRule::MyAttacker);

    return BgTargetPick();
}

} // namespace bg
} // namespace TortoiseBots
