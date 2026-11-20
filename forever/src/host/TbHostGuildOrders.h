#pragma once

#include "runtime/GuildOrderContract.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Player;

namespace tbhost
{
namespace guild_orders
{

// Guarded characters-DB store for tortoise_bots_guild_order.
void LoadOrders();
bool IsOrderStoreAvailable();
std::optional<TortoiseBots::GuildOrders::OrderRecord> FindOrder(uint32_t botGuidLow);
std::vector<TortoiseBots::GuildOrders::OrderRecord> ListOrdersForGuild(uint32_t guildId);
bool GiveOrder(TortoiseBots::GuildOrders::OrderRecord record);
bool CancelOrder(uint32_t botGuidLow);

// Fulfilment seams (Step 5): default implementations answer "cannot" (false)
// until W7 (gather/craft), W8 (auction buy), and W4 (travel/delivery) are wired.
class IGatherCraftProvider
{
public:
    virtual ~IGatherCraftProvider() = default;
    virtual bool CanGather(uint32_t botGuidLow, uint32_t itemId) const = 0;
    virtual bool CanCraft(uint32_t botGuidLow, uint32_t itemId) const = 0;
    virtual bool StepGatherOrCraft(uint32_t botGuidLow, uint32_t itemId, uint32_t wantCount, std::string& outReason) = 0;
};

class IAuctionBuyProvider
{
public:
    virtual ~IAuctionBuyProvider() = default;
    virtual bool CanBuyOnAuction(uint32_t botGuidLow, uint32_t itemId, uint32_t maxCopper) const = 0;
    virtual bool StepBuyOnAuction(uint32_t botGuidLow, uint32_t itemId, uint32_t wantCount, uint32_t maxCopper, std::string& outReason) = 0;
};

class IDeliveryProvider
{
public:
    virtual ~IDeliveryProvider() = default;
    virtual bool CanDeliverToBank(uint32_t botGuidLow, uint64_t guildId) const = 0;
    virtual bool CanDeliverByMail(uint32_t botGuidLow, uint32_t recipientGuidLow) const = 0;
    virtual bool Deliver(uint32_t botGuidLow, uint64_t guildId, uint32_t recipientGuidLow, uint32_t itemId, uint32_t count, std::string& outReason) = 0;
};

IGatherCraftProvider& GatherCraft();
IAuctionBuyProvider& AuctionBuy();
IDeliveryProvider& Delivery();

void SetGatherCraftProvider(IGatherCraftProvider* provider);
void SetAuctionBuyProvider(IAuctionBuyProvider* provider);
void SetDeliveryProvider(IDeliveryProvider* provider);

// Per-tick order loop for `.tb guildorder work <bot> [id]`.
void SetOrderWorking(uint32_t botGuidLow, bool working);
bool IsOrderWorking(uint32_t botGuidLow);
void TickBotOrder(Player* bot);

// W8 trade-good reserve helper: true if any open order in `guildId` needs `itemId` (directly or as a craft reagent).
bool IsItemNeededByGuildOrder(uint64_t guildId, uint32_t itemId);

} // namespace guild_orders
} // namespace tbhost
