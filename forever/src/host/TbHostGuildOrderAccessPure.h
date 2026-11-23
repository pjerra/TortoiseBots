#pragma once

// Core-free rules of the guild-order access control: who counts as a bot for the
// rank rule, which rank the bots hold, which ranks may place or move orders, and
// how the rank's bank tabs are split for the access report. The TB policy lives in
// runtime/GuildOrderAccessPolicy.h; this is its ForeverCore shape, where bank tab
// access is a per-rank right mask (`guild_bank_right.gbright`) instead of the
// Turtle table's per-tab minimum rank.
//
// Header-only and free of core types on purpose: forever/tools/test_guild_order_access_policy.cpp
// exercises the matrix standalone, the host code feeds plain values it already read.
#include <cstdint>
#include <string>
#include <vector>

namespace tbhost
{
namespace guild_order_access
{

// ForeverCore's GuildBankRights bits (Guild.h:186-193).
inline constexpr uint8_t kBankRightViewTab = 0x01;
inline constexpr uint8_t kBankRightPutItem = 0x02;
inline constexpr uint8_t kBankRightDepositItem = kBankRightViewTab | kBankRightPutItem;

// A member counts as an order bot for the rank rule when the module owns it: a
// pool character, online or offline. A player's own character running as a bot
// never counts, so the rank rule cannot move it.
inline bool IsGuildOrderBot(bool poolCharacter, bool playerOwnedAlt)
{
    return poolCharacter && !playerOwnedAlt;
}

// The rank newly joined bots get, and the fallback when the guild never picked
// one: the guild's lowest rank. Rank 0 is the master; a guild with only a master
// row keeps its bots at rank 0.
inline uint32_t DefaultBotRank(uint32_t rankCount)
{
    if (rankCount < 2)
        return 0;
    return rankCount - 1;
}

// Clamp a player-chosen bot rank to the guild's ranks. Rank 0 stays the
// master's own: choosing it would mix bots into the leader rank.
inline uint32_t ClampBotRank(uint32_t wanted, uint32_t rankCount)
{
    if (rankCount < 2)
        return DefaultBotRank(rankCount);
    if (wanted < 1)
        return 1;
    if (wanted >= rankCount)
        return rankCount - 1;
    return wanted;
}

// The guild master is never moved: in bot-led guilds the leader is a bot at
// rank 0, and moving it would leave the guild without a leader.
inline bool BotRankMayMoveMember(uint32_t currentRank)
{
    return currentRank != 0;
}

// The bot rank applies only in guilds with at least one real player member:
// all-bot pool guilds keep their ranks untouched (nobody gives orders there, and
// the bots' own guild management runs on those ranks). A disabled feature
// (rank 0) applies nowhere.
inline bool BotRankAppliesInGuild(bool hasRealPlayerMember, uint32_t botRank)
{
    return hasRealPlayerMember && botRank != 0;
}

// One bank tab's reachability for the ORDER_ACCESS report: tab numbers are
// 1-based on the wire. A tab is reachable when its rights for the bot rank carry
// the mask the caller asks for (deposit for orders).
inline void SplitBankTabs(std::vector<uint8_t> const& tabRights, uint8_t requiredMask,
    std::vector<uint32_t>& tabsOk, std::vector<uint32_t>& tabsDenied)
{
    tabsOk.clear();
    tabsDenied.clear();
    uint32_t tab = 0;
    for (uint8_t rights : tabRights)
    {
        ++tab;
        if ((rights & requiredMask) == requiredMask)
            tabsOk.push_back(tab);
        else
            tabsDenied.push_back(tab);
    }
}

// Comma-separated 1-based tab list, or "-" when empty.
inline std::string FormatTabList(std::vector<uint32_t> const& tabs)
{
    if (tabs.empty())
        return "-";
    std::string out;
    for (uint32_t tab : tabs)
    {
        if (!out.empty())
            out += ",";
        out += std::to_string(tab);
    }
    return out;
}

// The rank id at a 0-based rank order, from the guild's ranks ordered by
// RankOrder. False when the guild has no rank there.
inline bool RankIdAtOrder(std::vector<uint32_t> const& rankIdsByOrder, uint32_t order, uint32_t& outRankId)
{
    if (order >= rankIdsByOrder.size())
        return false;
    outRankId = rankIdsByOrder[order];
    return true;
}

// Who may place, cancel or move an order: a real player (never a bot session or
// the console) in the bot's guild whose rank may edit officer notes. `sameGuild`
// compares the asker's guild with the order's.
inline bool MayGiveOrders(bool realPlayer, bool sameGuild, bool mayEditOfficerNotes)
{
    return realPlayer && sameGuild && mayEditOfficerNotes;
}

// Who may change the bots' rank: a real player of the guild whose rank may
// promote and demote members.
inline bool MayChangeBotRank(bool realPlayer, bool mayPromote, bool mayDemote)
{
    return realPlayer && mayPromote && mayDemote;
}

// Why a reassign is refused, in the order the checks run.
enum class ReassignVerdict : uint8_t
{
    Allowed = 0,
    NoOrder,
    SameBot,
    NoGuild,
    TargetElsewhere,
    TargetBusy,
    TargetNotOrderBot,
};

// Move an order row from one bot to another. The target must be online in the
// order's guild, carry no running or watched (`stocked`) order of its own, and be
// a bot the module owns.
inline ReassignVerdict CheckReassign(bool sourceHasOrder, bool targetIsSource, bool orderHasGuild,
    bool targetOnlineInOrderGuild, bool targetBusy, bool targetIsOrderBot)
{
    if (!sourceHasOrder)
        return ReassignVerdict::NoOrder;
    if (targetIsSource)
        return ReassignVerdict::SameBot;
    if (!orderHasGuild)
        return ReassignVerdict::NoGuild;
    if (!targetOnlineInOrderGuild)
        return ReassignVerdict::TargetElsewhere;
    if (targetBusy)
        return ReassignVerdict::TargetBusy;
    if (!targetIsOrderBot)
        return ReassignVerdict::TargetNotOrderBot;
    return ReassignVerdict::Allowed;
}

// A target's own row blocks only while it runs or watches the bank: finished
// rows and parked refund stubs do not.
inline bool OrderKeepsBotBusy(bool hasOrder, bool cancelled, bool finished, bool stocked)
{
    return hasOrder && !cancelled && (!finished || stocked);
}

} // namespace guild_order_access
} // namespace tbhost
