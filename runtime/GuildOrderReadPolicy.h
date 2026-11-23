#pragma once

#include "GuildOrderCategoryPolicy.h"
#include "GuildOrderContract.h"

#include <cstddef>
#include <cstdint>

// Pure rules behind the read-only guild order verbs (search, who, preview,
// steps): which items an order may hand over, whether one bot can serve an
// item, and when a bot counts as busy. Header-only and free of core types so
// the rules stay unit tested on their own (forever/tools/test_guild_order_read.cpp).
namespace TortoiseBots
{
namespace GuildOrders
{

// Core values kept literal so this header includes no game headers.
inline constexpr uint32_t kItemClassQuest = 12;   // ITEM_CLASS_QUEST
inline constexpr uint32_t kBindOnAcquire = 1;     // BIND_ON_ACQUIRE (vanilla BIND_WHEN_PICKED_UP)
inline constexpr uint32_t kBindQuest = 4;         // BIND_QUEST (vanilla BIND_QUEST_ITEM)
inline constexpr uint32_t kItemFlagConjured = 0x00000002; // ITEM_FLAG_CONJURED

inline constexpr std::size_t kSearchMinChars = 3;
inline constexpr std::size_t kSearchMaxRows = 30;
inline constexpr std::size_t kWhoMaxRows = 30;

// Why an item cannot be handed over, as the preview reports it. `named`
// false means the template has no usable name (never a search row).
struct OrderableFacts
{
    bool named = true;
    bool bound = false;    // bind on pickup / quest bind
    bool quest = false;    // quest item class
    bool timed = false;    // expires (duration)
    bool conjured = false;
};

inline OrderableFacts CheckOrderable(uint32_t itemClass, uint32_t bonding, uint32_t duration, bool conjured)
{
    OrderableFacts f;
    f.bound = bonding == kBindOnAcquire || bonding == kBindQuest;
    f.quest = itemClass == kItemClassQuest;
    f.timed = duration != 0;
    f.conjured = conjured;
    return f;
}

// The hand-over rule of TB's order store (runtime/GuildOrderStore.cpp
// IsOrderableItem): a named item that is not bound on pickup, not a quest
// item and not timed. Conjured items are not part of this rule; search skips
// them separately like TB's item index did.
inline bool IsOrderableItem(OrderableFacts const& f)
{
    return f.named && !f.bound && !f.quest && !f.timed;
}

// Whether a search may browse the whole kind without text. Drop and other
// hold thousands of rows; the 30-row cut would show the same A slice.
inline bool IsBrowsableCategory(OrderItemCategory category)
{
    return category == OrderItemCategory::Herb || category == OrderItemCategory::Ore ||
           category == OrderItemCategory::Leather || category == OrderItemCategory::Fish ||
           category == OrderItemCategory::Cloth;
}

// One gathering profession of a bot.
struct FarmSkill
{
    bool has = false;
    uint32_t value = 0;
};

// A farm item is servable when one of its node kinds matches a profession the
// bot has at the node's lock skill. herbReq/oreReq are the smallest lock skill
// seen across that kind's nodes (0 = no requirement). A source kind the bot
// has no profession for is no skill; a kind it has below the lock is low
// skill; only when every kind fails is the item refused.
inline OrderReason FarmVerdict(bool herbSource, uint16_t herbReq, bool oreSource, uint16_t oreReq,
    FarmSkill herb, FarmSkill mining)
{
    if (!herbSource && !oreSource)
        return OrderReason::NoSource;

    bool anyProfession = false;
    auto serves = [&anyProfession](bool source, uint16_t req, FarmSkill const& skill) -> bool
    {
        if (!source || !skill.has)
            return false;
        anyProfession = true;
        return skill.value >= req;
    };

    if (serves(herbSource, herbReq, herb))
        return OrderReason::None;
    if (serves(oreSource, oreReq, mining))
        return OrderReason::None;
    return anyProfession ? OrderReason::LowSkill : OrderReason::NoSkill;
}

// Craft: the bot must know one recipe that creates the item and have the
// recipe's profession at the required skill. Knowledge is the bot's spell
// book, not a moment's reagents (the preview lists reagents separately).
inline OrderReason CraftVerdict(bool craftable, bool knowsRecipe, uint32_t botSkill, uint32_t reqSkill)
{
    if (!craftable || !knowsRecipe)
        return OrderReason::NoRecipe;
    if (botSkill < reqSkill)
        return OrderReason::LowSkill;
    return OrderReason::None;
}

// TB's who-search busy rule: a cancelled row is free, a finished row is free
// except a stocked one, which keeps watching the bank.
inline bool OrderBotBusy(bool hasOrder, bool cancelled, OrderState state)
{
    return hasOrder && !cancelled && (!IsFinished(state) || state == OrderState::Stocked);
}

// Preview is available for the order types TBF can place. Enchant orders are
// not in this build; a Buy preview is allowed because a TBF Buy order is the
// auction-house leg and its cost can be read before sending.
inline bool PreviewTypeSupported(OrderType type)
{
    return type == OrderType::Farm || type == OrderType::Craft || type == OrderType::Buy;
}

} // namespace GuildOrders
} // namespace TortoiseBots
