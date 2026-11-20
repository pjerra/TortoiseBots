#pragma once

// W1 (review fix): which units a free (masterless) host bot may START a fight with. A free bot plays the world unsupervised, so it only
// opens fights the world can accept: an ordinary hostile or neutral NPC. A player, anything a player controls (pet, guardian, totem),
// a quest giver, vendor, trainer, flight master, innkeeper, guard or civilian, a kill that lowers a standing, a friendly unit and a mob
// another player has tapped are all refused. Self-defence is NOT this decision: the attackers loop of FindTargetForGrinding returns
// anything already fighting the bot or its group before the possible targets are read.
//
// Pure (no core types): forever/tools/test_free_grind_policy.cpp checks the rule; GrindTargetValue.cpp fills the facts from the unit.

#include <cstdint>

namespace TortoiseBots
{
namespace world
{

enum class FreeGrindRefusal : std::uint8_t
{
    None,
    Player,
    Friendly,
    PlayerControlled,
    ServiceNpc,
    Guard,
    Civilian,
    ReputationLowering,
    TappedByAnotherPlayer,
};

// What a candidate unit is, as far as the choice to start a fight goes. `serviceNpc` is a quest giver, vendor, trainer, flight master
// or innkeeper (one fact: all of them are refused for the same reason).
struct FreeGrindFacts
{
    bool player = false;
    bool friendly = false;
    bool playerControlled = false;
    bool serviceNpc = false;
    bool guard = false;
    bool civilian = false;
    bool killLowersReputation = false;
    bool tappedByAnotherPlayer = false;
};

// The refusal, in the order the reasons are checked (a player who is also a quest giver is a player).
constexpr FreeGrindRefusal FreeGrindRefusalOf(FreeGrindFacts const& facts)
{
    if (facts.player)
        return FreeGrindRefusal::Player;
    if (facts.friendly)
        return FreeGrindRefusal::Friendly;
    if (facts.playerControlled)
        return FreeGrindRefusal::PlayerControlled;
    if (facts.serviceNpc)
        return FreeGrindRefusal::ServiceNpc;
    if (facts.guard)
        return FreeGrindRefusal::Guard;
    if (facts.civilian)
        return FreeGrindRefusal::Civilian;
    if (facts.killLowersReputation)
        return FreeGrindRefusal::ReputationLowering;
    if (facts.tappedByAnotherPlayer)
        return FreeGrindRefusal::TappedByAnotherPlayer;
    return FreeGrindRefusal::None;
}

constexpr bool FreeBotMayStartFight(FreeGrindFacts const& facts)
{
    return FreeGrindRefusalOf(facts) == FreeGrindRefusal::None;
}

// The `.tb state`/grind-log wording of a refusal, empty when there is none.
inline char const* FreeGrindRefusalText(FreeGrindRefusal refusal)
{
    switch (refusal)
    {
        case FreeGrindRefusal::Player: return "player";
        case FreeGrindRefusal::Friendly: return "friendly";
        case FreeGrindRefusal::PlayerControlled: return "player-controlled";
        case FreeGrindRefusal::ServiceNpc: return "quest giver, vendor, trainer, flight master or innkeeper";
        case FreeGrindRefusal::Guard: return "guard";
        case FreeGrindRefusal::Civilian: return "civilian";
        case FreeGrindRefusal::ReputationLowering: return "kill lowers reputation";
        case FreeGrindRefusal::TappedByAnotherPlayer: return "tapped by another player";
        default: return "";
    }
}

} // namespace world
} // namespace TortoiseBots
