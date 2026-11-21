#pragma once

#include "runtime/GuildOrderContract.h"

#include <cstdint>

namespace TortoiseBots
{
namespace GuildOrders
{

enum class OrderStepAction : uint8_t
{
    Complete = 0,
    DeliverBank = 1,
    DeliverMail = 2,
    Craft = 3,
    Gather = 4,
    BuyAh = 5,
    CannotTravel = 6,
};

struct OrderStepInput
{
    OrderType type = OrderType::Farm;
    uint32_t delivered = 0;
    uint32_t want = 0;
    uint32_t inBags = 0;
    bool canCraft = false;
    bool canGather = false;
    bool canBuyAh = false;
    bool canDeliverBank = false;
    bool canDeliverMail = false;
};

struct OrderStepDecision
{
    OrderStepAction action = OrderStepAction::CannotTravel;
    uint32_t have = 0;
    uint32_t remaining = 0;
    char const* reason = "cannot: travel";
};

// Pure step-choice policy for an active guild order:
// 1. Count have = delivered + inBags against want.
// 2. When have >= want, deliver (guild bank in reach first, else mail).
// 3. Otherwise acquire by source order (craft -> gather -> AH buy),
//    honouring the order's type; without W4 travel, missing local sources
//    or delivery endpoints answer "cannot: travel" (less).
inline OrderStepDecision DecideOrderStep(OrderStepInput const& in)
{
    OrderStepDecision out;
    out.have = in.delivered + in.inBags;
    out.remaining = (out.have >= in.want) ? 0u : (in.want - out.have);

    if (in.want == 0 || (in.delivered >= in.want && in.inBags == 0))
    {
        out.action = OrderStepAction::Complete;
        out.reason = "done";
        return out;
    }

    if (out.have >= in.want)
    {
        if (in.canDeliverBank)
        {
            out.action = OrderStepAction::DeliverBank;
            out.reason = "deliver-bank";
            return out;
        }
        if (in.canDeliverMail)
        {
            out.action = OrderStepAction::DeliverMail;
            out.reason = "deliver-mail";
            return out;
        }
        out.action = OrderStepAction::CannotTravel;
        out.reason = "cannot: travel";
        return out;
    }

    if (in.type == OrderType::Craft)
    {
        if (in.canCraft)
        {
            out.action = OrderStepAction::Craft;
            out.reason = "craft";
            return out;
        }
        if (in.canGather)
        {
            out.action = OrderStepAction::Gather;
            out.reason = "gather";
            return out;
        }
        if (in.canBuyAh)
        {
            out.action = OrderStepAction::BuyAh;
            out.reason = "buy-ah";
            return out;
        }
    }
    else if (in.type == OrderType::Farm)
    {
        if (in.canGather)
        {
            out.action = OrderStepAction::Gather;
            out.reason = "gather";
            return out;
        }
    }
    else if (in.type == OrderType::Buy)
    {
        if (in.canBuyAh)
        {
            out.action = OrderStepAction::BuyAh;
            out.reason = "buy-ah";
            return out;
        }
    }

    out.action = OrderStepAction::CannotTravel;
    out.reason = "cannot: travel";
    return out;
}

} // namespace GuildOrders
} // namespace TortoiseBots
