// Mount training and purchase for bots: a bot of level 40+ with gold pays the
// riding trainer for Apprentice Riding (level 40) / Journeyman Riding (level 60)
// and buys its own race's vendor mount (epic from 60), then uses the mount item.
// The money is the bot's own; nothing is granted.
//
// The pure level/tier decisions live in TbMountShopPure.h. The world work here
// reuses tbhost::town's trainer and vendor paths (TbHostTown.h), so a purchase
// takes the same checks and logs as a player-driven one.
#pragma once

#include "host/TbMountShopPure.h"

#include "ObjectGuid.h"

#include <cstdint>
#include <string>

class Creature;
class Player;

namespace tbhost
{
namespace mountshop
{

enum class Step : uint8_t
{
    None = 0,
    TrainRiding = 1,   // pay the riding trainer
    BuyMount = 2,      // pay the mount vendor
    UseItem = 3        // use a tier mount item the bot already carries
};

char const* StepToken(Step step);

struct Plan
{
    Step step = Step::None;
    ObjectGuid npc;              // trainer or vendor; empty for UseItem
    uint32_t vendorSlot = 0;
    uint32_t itemId = 0;
    uint32_t cost = 0;           // discounted copper for this step
    std::string reason;          // why this step, or why there is none
};

// What the bot still needs: none, riding, or a mount.
Need NeedOf(Player* bot);

// The next step and the nearest NPC that can serve it within rangeYd (same map,
// zone and phase). A bot already carrying an unused tier mount gets UseItem with
// no NPC. `reason` names a refusal token when step is None.
Plan FindPlan(Player* bot, float rangeYd);

// Carries out `plan`. Returns false and sets `refusal` (a token) when the core,
// the trainer or the vendor refused. `spentCopper` is what left the bot's purse.
bool ExecutePlan(Player* bot, Plan const& plan, uint32_t* spentCopper = nullptr, std::string* refusal = nullptr);

} // namespace mountshop
} // namespace tbhost
