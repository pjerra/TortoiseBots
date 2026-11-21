#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace world
{

inline constexpr uint64_t kGuildMailCheckIntervalMs = 30000;
inline constexpr uint64_t kGuildMailGoldCooldownMs = 300000;
inline constexpr uint64_t kGuildMailMatsCooldownMs = 300000;
inline constexpr uint64_t kGuildMailFailBackoffMs = 10000;

inline constexpr uint64_t kMinSenderReserveCopper = 20000;  // 2g
inline constexpr uint64_t kMinGoldGiftCopper = 1000;        // 10s
inline constexpr uint64_t kMaxGoldGiftCopper = 50000;       // 5g
inline constexpr uint64_t kPoorestCeilingCopper = 50000;    // 5g
inline constexpr uint64_t kGuildMailPostageCopper = 30;
inline constexpr uint32_t kMaxGuildMailAttachments = 4;

struct GuildMemberMailView
{
    uint32_t guidLow = 0;
    std::string name;
    bool isBot = false;
    bool isOnline = false;
    uint64_t moneyCopper = 0;
};

inline bool HasRealGuildPlayers(std::vector<GuildMemberMailView> const& members)
{
    for (GuildMemberMailView const& m : members)
    {
        if (!m.isBot)
            return true;
    }
    return false;
}

struct GuildGoldGiftPlan
{
    bool shouldSend = false;
    uint32_t recipientGuidLow = 0;
    std::string recipientName;
    uint64_t copper = 0;
};

inline GuildGoldGiftPlan PlanGuildGoldGift(
    uint32_t senderGuidLow,
    bool senderIsFreeBot,
    uint64_t senderCopper,
    bool guildHasRealPlayers,
    bool allowMixedGuild,
    std::vector<GuildMemberMailView> const& members)
{
    if (!senderIsFreeBot || senderGuidLow == 0)
        return {};
    if (guildHasRealPlayers && !allowMixedGuild)
        return {};
    if (senderCopper <= kMinSenderReserveCopper + kMinGoldGiftCopper + kGuildMailPostageCopper)
        return {};

    GuildMemberMailView const* best = nullptr;
    for (GuildMemberMailView const& m : members)
    {
        if (m.guidLow == 0 || m.guidLow == senderGuidLow || !m.isBot || !m.isOnline)
            continue;
        if (m.moneyCopper >= kPoorestCeilingCopper)
            continue;
        if (senderCopper < m.moneyCopper * 2 + kMinSenderReserveCopper)
            continue;

        if (!best || m.moneyCopper < best->moneyCopper ||
            (m.moneyCopper == best->moneyCopper && m.guidLow < best->guidLow))
        {
            best = &m;
        }
    }

    if (!best)
        return {};

    uint64_t const surplus = senderCopper - kMinSenderReserveCopper - kGuildMailPostageCopper;
    uint64_t const gift = std::min<uint64_t>(kMaxGoldGiftCopper, surplus / 4);
    if (gift < kMinGoldGiftCopper)
        return {};

    return { true, best->guidLow, best->name, gift };
}

struct GuildMatItemCandidate
{
    uint32_t itemId = 0;
    uint32_t itemClass = 0;
    bool isSoulbound = false;
    bool isEquipped = false;
    bool isProfessionTool = false;
    bool neededByOrder = false;
    bool usedBySender = false;
};

inline bool CanMailGuildMaterial(GuildMatItemCandidate const& item)
{
    constexpr uint32_t kClassTradeGoods = 7;
    if (item.itemId == 0 || item.itemClass != kClassTradeGoods)
        return false;
    if (item.isSoulbound || item.isEquipped || item.isProfessionTool || item.neededByOrder || item.usedBySender)
        return false;
    return true;
}

inline GuildMemberMailView const* PickMaterialRecipient(
    uint32_t senderGuidLow,
    bool senderIsFreeBot,
    bool guildHasRealPlayers,
    bool allowMixedGuild,
    std::vector<GuildMemberMailView> const& craftersWhoUseItem)
{
    if (!senderIsFreeBot || senderGuidLow == 0)
        return nullptr;
    if (guildHasRealPlayers && !allowMixedGuild)
        return nullptr;

    GuildMemberMailView const* best = nullptr;
    for (GuildMemberMailView const& m : craftersWhoUseItem)
    {
        if (m.guidLow == 0 || m.guidLow == senderGuidLow || !m.isBot || !m.isOnline)
            continue;
        if (!best || m.guidLow < best->guidLow)
            best = &m;
    }
    return best;
}

} // namespace world
} // namespace TortoiseBots
