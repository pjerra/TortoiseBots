#pragma once

#include "ObjectGuid.h"

#include <cstdint>
#include <string>
#include <vector>

class Creature;
class Item;
class Player;
struct ItemTemplate;

namespace tbhost
{
namespace ah
{

struct AhListingView
{
    uint32_t auctionId = 0;
    uint32_t itemId = 0;
    uint32_t count = 0;
    uint64_t unitPrice = 0;
    uint64_t buyoutPrice = 0;
    uint64_t minBid = 0;
    bool isCommodity = false;
    bool isOwn = false;
};

struct AhOwnedView
{
    uint32_t auctionId = 0;
    uint32_t itemId = 0;
    uint32_t count = 0;
    uint64_t buyoutOrUnitPrice = 0;
    bool isCommodity = false;
    bool hasBidder = false;
};

// NPC interaction & faction-house check (faction houses only; neutral houseId == 7 is excluded).
bool IsWorldAuctionEnabled();
Creature* NpcIfCanInteract(Player* bot, ObjectGuid const& guid);
bool IsFactionAuctionHouse(Creature const* auctioneer);
Creature* FindNearestFactionAuctioneer(Player* bot, float maxDistYd = 300.0f);

// Market value & deposit helpers.
uint64_t UnitValue(ItemTemplate const* proto);
uint64_t GetDeposit(Player const* bot, Creature const* auctioneer, Item const* item, uint32_t count,
    uint32_t runTimeMinutes, bool isCommodity);

// Read market without sending packets (uses BuildListAuctionItems / BuildListBuckets / BuildListOwnedItems).
std::vector<AhListingView> QueryMarketByItemId(Player* bot, Creature const* auctioneer, uint32_t itemId);
std::vector<AhListingView> QueryMarketByName(Player* bot, Creature const* auctioneer, std::string const& nameSubstr);
std::vector<AhOwnedView> Owned(Player* bot, Creature const* auctioneer, uint32_t* outSoldCount = nullptr);

// Core-handler actions (WorldSession::HandleAuction*).
bool PostItem(Player* bot, Creature* auctioneer, Item* item, uint64_t minBid, uint64_t buyoutPrice,
    uint32_t runTimeMinutes, std::string* failReason = nullptr);
bool PostCommodity(Player* bot, Creature* auctioneer, Item* item, uint32_t count, uint64_t unitPrice,
    uint32_t runTimeMinutes, std::string* failReason = nullptr);
bool BuyItem(Player* bot, Creature* auctioneer, uint32_t auctionId, uint64_t buyoutPrice,
    std::string* failReason = nullptr);
bool BuyCommodity(Player* bot, Creature* auctioneer, uint32_t itemId, uint32_t quantity,
    std::string* failReason = nullptr);
bool Cancel(Player* bot, Creature* auctioneer, uint32_t auctionId, uint32_t itemId,
    std::string* failReason = nullptr);

// Async-commit confirmation next tick via BuildListOwnedItems, and state counters for `.tb state`.
void ConfirmPendingPosts(Player* bot);
void GetBotStateCounts(Player* bot, uint32_t& outPosted, uint32_t& outSold);

} // namespace ah
} // namespace tbhost
