#pragma once

#include <cstdint>

namespace TortoiseBots
{
namespace world
{

enum class CapitalErrandKind : uint8_t
{
    None = 0,
    Mailbox = 1,
    Trainer = 2,
    Bank = 3,
    AuctionHouse = 4,
    Mount = 5
};

inline constexpr uint64_t kErrandGlobalIntervalMs = 30000;
inline constexpr uint64_t kErrandFailBackoffMs = 10000;
inline constexpr uint64_t kMailboxCooldownMs = 60000;
inline constexpr uint64_t kTrainerCooldownMs = 120000;
inline constexpr uint64_t kBankCooldownMs = 180000;
inline constexpr uint64_t kAuctionCooldownMs = 180000;
inline constexpr uint64_t kMountCooldownMs = 120000;
inline constexpr uint32_t kBankFreeSlotsThreshold = 6;

inline constexpr uint64_t kDuelCooldownMs = 120000;
inline constexpr uint64_t kDuelScanIntervalMs = 5000;

inline bool IsCapitalZoneId(uint32_t zoneId)
{
    switch (zoneId)
    {
        case 1497: // Undercity
        case 1519: // Stormwind City
        case 1537: // Ironforge
        case 1637: // Orgrimmar
        case 1638: // Thunder Bluff
        case 1657: // Darnassus
            return true;
        default:
            return false;
    }
}

inline bool IsCapitalAreaOrZone(bool areaHasLinkedChat, bool zoneHasLinkedChat, uint32_t zoneId)
{
    return areaHasLinkedChat || zoneHasLinkedChat || IsCapitalZoneId(zoneId);
}

inline uint64_t CooldownMsForErrand(CapitalErrandKind kind)
{
    switch (kind)
    {
        case CapitalErrandKind::Mailbox: return kMailboxCooldownMs;
        case CapitalErrandKind::Trainer: return kTrainerCooldownMs;
        case CapitalErrandKind::Bank: return kBankCooldownMs;
        case CapitalErrandKind::AuctionHouse: return kAuctionCooldownMs;
        case CapitalErrandKind::Mount: return kMountCooldownMs;
        default: return 0;
    }
}

struct CapitalErrandInput
{
    bool isFreeBot = false;
    bool hasActiveOrder = false;
    bool isAlive = false;
    bool inCombat = false;
    bool isMoving = false;
    bool isCasting = false;
    bool inDuel = false;
    bool inCapital = false;
    bool auctionEnabled = false;
    uint32_t unreadMailCount = 0;
    bool hasTrainableSpell = false;
    bool needsMountPurchase = false;
    uint32_t freeBagSlots = 0;
    uint32_t bankableItemCount = 0;
    uint32_t ahSellableItemCount = 0;
    uint64_t nowMs = 0;
    uint64_t nextGlobalTryMs = 0;
    uint64_t mailboxCooldownUntilMs = 0;
    uint64_t trainerCooldownUntilMs = 0;
    uint64_t bankCooldownUntilMs = 0;
    uint64_t ahCooldownUntilMs = 0;
    uint64_t mountCooldownUntilMs = 0;
};

inline CapitalErrandKind PickCapitalErrand(CapitalErrandInput const& in)
{
    if (!in.isFreeBot || in.hasActiveOrder || !in.isAlive || in.inCombat ||
        in.isMoving || in.isCasting || in.inDuel)
        return CapitalErrandKind::None;

    if (in.nowMs < in.nextGlobalTryMs)
        return CapitalErrandKind::None;

    // A mount trainer or vendor sits outside the capitals too (the racial
    // pairs live in Goldshire, Sen'jin Village, ...), so this errand is not
    // gated on inCapital.
    if (in.inCapital && in.unreadMailCount > 0 && in.nowMs >= in.mailboxCooldownUntilMs)
        return CapitalErrandKind::Mailbox;

    if (in.needsMountPurchase && in.nowMs >= in.mountCooldownUntilMs)
        return CapitalErrandKind::Mount;

    if (!in.inCapital)
        return CapitalErrandKind::None;

    if (in.hasTrainableSpell && in.nowMs >= in.trainerCooldownUntilMs)
        return CapitalErrandKind::Trainer;

    if (in.freeBagSlots <= kBankFreeSlotsThreshold && in.bankableItemCount > 0 && in.nowMs >= in.bankCooldownUntilMs)
        return CapitalErrandKind::Bank;

    if (in.auctionEnabled && in.ahSellableItemCount > 0 && in.nowMs >= in.ahCooldownUntilMs)
        return CapitalErrandKind::AuctionHouse;

    return CapitalErrandKind::None;
}

inline bool ShouldBankItemCapital(
    uint32_t itemClass,
    uint32_t quality,
    bool isEquipped,
    bool isSoulbound,
    bool isFoodOrWaterOrAmmo,
    bool isProfessionTool)
{
    constexpr uint32_t kQualityPoor = 0;
    constexpr uint32_t kQualityUncommon = 2;
    constexpr uint32_t kClassTradeGoods = 7;
    constexpr uint32_t kClassRecipe = 9;
    constexpr uint32_t kClassQuest = 12;

    if (isEquipped || isFoodOrWaterOrAmmo || isProfessionTool || itemClass == kClassQuest || quality == kQualityPoor)
        return false;

    if (itemClass == kClassTradeGoods || itemClass == kClassRecipe)
        return true;

    return !isSoulbound && quality >= kQualityUncommon;
}

struct DuelCandidateInput
{
    bool challengerIsFreeBot = false;
    bool challengerAlive = false;
    bool challengerInCombat = false;
    bool challengerInDuel = false;
    bool challengerIsMoving = false;
    uint8_t challengerLevel = 1;
    uint8_t challengerHealthPct = 100;
    bool targetIsBot = false;
    bool targetIsFreeBot = false;
    bool targetAlive = false;
    bool targetInCombat = false;
    bool targetInDuel = false;
    uint8_t targetLevel = 1;
    uint8_t targetHealthPct = 100;
    bool allowDuelingArea = false;
    uint8_t minDuelLevel = 10;
    float distanceYd = 0.0f;
    float maxRangeYd = 30.0f;
    uint64_t nowMs = 0;
    uint64_t challengerCooldownUntilMs = 0;
    uint64_t targetCooldownUntilMs = 0;
};

inline bool CanInitiateBotDuel(DuelCandidateInput const& in)
{
    if (!in.challengerIsFreeBot || !in.challengerAlive || in.challengerInCombat ||
        in.challengerInDuel || in.challengerIsMoving)
        return false;

    if (!in.targetIsBot || !in.targetIsFreeBot || !in.targetAlive ||
        in.targetInCombat || in.targetInDuel)
        return false;

    if (!in.allowDuelingArea)
        return false;

    if (in.challengerLevel < in.minDuelLevel || in.targetLevel < in.minDuelLevel)
        return false;

    if (in.challengerHealthPct < 90 || in.targetHealthPct < 90)
        return false;

    if (in.targetLevel > in.challengerLevel + 3 || in.challengerLevel > in.targetLevel + 10)
        return false;

    if (in.distanceYd > in.maxRangeYd)
        return false;

    if (in.nowMs < in.challengerCooldownUntilMs || in.nowMs < in.targetCooldownUntilMs)
        return false;

    return true;
}

inline bool ShouldAcceptBotDuel(
    bool botIsFreeBot,
    bool botAlive,
    bool botInCombat,
    bool challengerIsBot,
    uint8_t botLevel,
    uint8_t minDuelLevel,
    uint8_t botHealthPct)
{
    return botIsFreeBot && botAlive && !botInCombat && challengerIsBot &&
           botLevel >= minDuelLevel && botHealthPct >= 90;
}

} // namespace world
} // namespace TortoiseBots
