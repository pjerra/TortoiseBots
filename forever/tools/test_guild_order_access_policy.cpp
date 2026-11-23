// Pure tests for the guild-order access control (TbHostGuildOrderAccessPure.h).
// Build & run:
//   g++ -std=c++17 -I. -Iforever/src forever/tools/test_guild_order_access_policy.cpp -o /tmp/test_guild_order_access_policy && /tmp/test_guild_order_access_policy
#include "host/TbHostGuildOrderAccessPure.h"

#include <cassert>
#include <cstdint>
#include <vector>

using namespace tbhost::guild_order_access;

int main()
{
    // A member counts as an order bot only when the module owns it; a player's
    // own character never does.
    assert(IsGuildOrderBot(true, false));
    assert(!IsGuildOrderBot(false, false));
    assert(!IsGuildOrderBot(true, true));
    assert(!IsGuildOrderBot(false, true));

    // Default is the lowest rank; a master-only guild keeps rank 0.
    assert(DefaultBotRank(5) == 4);
    assert(DefaultBotRank(2) == 1);
    assert(DefaultBotRank(1) == 0);
    assert(DefaultBotRank(0) == 0);

    // The chosen rank is clamped into the guild's ranks, never the master.
    assert(ClampBotRank(1, 5) == 1);
    assert(ClampBotRank(3, 5) == 3);
    assert(ClampBotRank(0, 5) == 1);
    assert(ClampBotRank(4, 5) == 4);
    assert(ClampBotRank(9, 5) == 4);
    assert(ClampBotRank(0, 1) == 0);

    // The guild master is never moved.
    assert(!BotRankMayMoveMember(0));
    assert(BotRankMayMoveMember(1));
    assert(BotRankMayMoveMember(4));

    // The bot rank applies only in guilds with a real player member, and never
    // when the feature is off.
    assert(BotRankAppliesInGuild(true, 1));
    assert(!BotRankAppliesInGuild(false, 1));
    assert(!BotRankAppliesInGuild(true, 0));
    assert(!BotRankAppliesInGuild(false, 0));

    // Deposit needs both the view and the put-item bit; 0xff is FULL.
    std::vector<uint32_t> ok, denied;
    SplitBankTabs({ kBankRightDepositItem, kBankRightViewTab, 0x00, 0xff, kBankRightPutItem }, kBankRightDepositItem, ok, denied);
    assert(ok.size() == 2 && ok[0] == 1 && ok[1] == 4);
    assert(denied.size() == 3 && denied[0] == 2 && denied[1] == 3 && denied[2] == 5);
    SplitBankTabs({}, kBankRightDepositItem, ok, denied);
    assert(ok.empty() && denied.empty());

    assert(FormatTabList(ok) == "-");
    ok = { 1, 3, 4 };
    assert(FormatTabList(ok) == "1,3,4");

    // Rank ids come from the guild's ranks in order.
    uint32_t rankId = 0;
    assert(RankIdAtOrder({ 0, 2, 7 }, 1, rankId) && rankId == 2);
    assert(!RankIdAtOrder({ 0, 2, 7 }, 3, rankId));

    // Orders: a real player of the bot's guild who may edit officer notes.
    assert(MayGiveOrders(true, true, true));
    assert(!MayGiveOrders(false, true, true));   // a bot session
    assert(!MayGiveOrders(true, false, true));   // another guild
    assert(!MayGiveOrders(true, true, false));   // rank without the note right

    // Rank changes: a real player whose rank may promote and demote.
    assert(MayChangeBotRank(true, true, true));
    assert(!MayChangeBotRank(false, true, true));
    assert(!MayChangeBotRank(true, false, true));
    assert(!MayChangeBotRank(true, true, false));

    // Reassign: the checks run in a fixed order.
    assert(CheckReassign(true, false, true, true, false, true) == ReassignVerdict::Allowed);
    assert(CheckReassign(false, false, true, true, false, true) == ReassignVerdict::NoOrder);
    assert(CheckReassign(true, true, true, true, false, true) == ReassignVerdict::SameBot);
    assert(CheckReassign(true, false, false, true, false, true) == ReassignVerdict::NoGuild);
    assert(CheckReassign(true, false, true, false, false, true) == ReassignVerdict::TargetElsewhere);
    assert(CheckReassign(true, false, true, true, true, true) == ReassignVerdict::TargetBusy);
    assert(CheckReassign(true, false, true, true, false, false) == ReassignVerdict::TargetNotOrderBot);

    // Only running rows and stocked keep orders block a target.
    assert(OrderKeepsBotBusy(true, false, false, false));   // working
    assert(OrderKeepsBotBusy(true, false, false, true));    // stocked
    assert(!OrderKeepsBotBusy(true, false, true, false));   // done/cant
    assert(!OrderKeepsBotBusy(true, true, false, false));   // parked refund stub
    assert(!OrderKeepsBotBusy(false, false, false, false)); // no row

    return 0;
}
