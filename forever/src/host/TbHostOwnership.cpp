// Host service R6B-2: the pool, claim, lease, commander and guild-order questions the legacy engine asks, answered for a
// ForeverCore host bot. Built only with TORTOISEBOTS_SLICE1 (CMakeLists.txt). The hired-companion lifecycle has its own
// owner now (host/hire/TbHireLifecycle.cpp).
//
// A host bot (forever/src/TbBotMgr.h) is no pool bot, no hire, no claimed guild bot, holds no activity lease, belongs to no
// commanded group and has no guild order. Each function below keeps the legacy name and signature and answers that; the
// "not on this host" ones also count under a feature of the unported list, so `.tb unported` shows how often the engine asked.
// Every answer is listed in forever/docs/SLICE1-R6B-NOTES.md (section 3, choice c). The real owners of these names
// (ClaimLifecycle.cpp, BotActivityLease.cpp, AhBotSellerAccount.cpp, RandomBotAccountRegistry.cpp,
// GuildOrderStore.cpp, commands/Commander.cpp, behavior/PlayerConvenience.cpp) are not part of the slice: if one is ported, its
// function must leave this file (a duplicate symbol at link says so).
#include "compat/botpch_forever.h"

#include "behavior/PlayerConvenience.h"
#include "runtime/AhBotSellerAccount.h"
#include "runtime/BotActivityLease.h"
#include "runtime/BotGearService.h"
#include "runtime/ClaimLifecycle.h"
#include "runtime/CommandedGroupQuery.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/RandomBotAccountRegistry.h"
#include "runtime/RunReleaseHold.h"
#include "host/TbH3.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/run/TbRunHooks.h"
#include "TbBotMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"

namespace TortoiseBots
{
// ---- ClaimLifecycle (runtime/ClaimLifecycle.cpp): no guild bot is claimed. ----
ClaimLifecycle& ClaimLifecycle::Instance()
{
    static ClaimLifecycle instance;
    return instance;
}

bool ClaimLifecycle::IsClaimed(uint32_t /*botGuidLow*/) const
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return false;
}

bool ClaimLifecycle::IsClaimedByPlayer(uint32_t /*botGuidLow*/, uint32_t /*playerGuidLow*/) const
{
    return false;
}

// ---- BotGearService (runtime/BotGearService.cpp): no pinned gear cap. ----
BotGearService& BotGearService::Instance()
{
    static BotGearService instance;
    return instance;
}

uint32_t BotGearService::PinnedCap(uint32_t /*guidLow*/) const
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return 0;
}

// ---- BotActivityLeaseManager (runtime/BotActivityLease.cpp): no bot holds a lease, every bot is Idle, except a COMMANDED bot (H4). ----
BotActivityLeaseManager& BotActivityLeaseManager::Instance()
{
    static BotActivityLeaseManager instance;
    return instance;
}

namespace
{
// H4 (spec section 6, "Lease answer", decision D5): the "dungeon crew" rule of the legacy release logic applies to a bot that `.tb master` bound to a
// commander. RunReleaseHold::Decide (runtime/RunReleaseHold.h:57-64): a Dungeon lease INSIDE an instance map waits at its body while a living
// priest/paladin/shaman/druid is on the map or a living member still fights, and releases only on a wipe. Every other reader of the lease treats
// Dungeon as "leave the bot alone" (R0 list, all in the slice's sources): PlayerbotAI.cpp:3367 (no master strip: a commanded bot has that already,
// BotCommands::IsInCommandedGroup), LeaveGroupAction.cpp:88 (never leaves its group), InviteToGroupAction.cpp:14 (no random invites), the guild gates
// (GuildManagementActions.cpp:156, GuildCreateActions.cpp/.h: idle/grinding/player-master only: a Dungeon bot does no guild errand), ReleaseSpiritAction.h:232
// (`RepopLeavesGroup`: a repop leaves the group for a Dungeon bot: `repop` is not registered in the slice). Each of them makes the bot do LESS.
//
// No entrance on the instance (no script, no area trigger into the map: BotMgr::EnsureEntrance): a release would revive the bot at a graveyard OUTSIDE, so it
// is HELD instead: the lease answer is PlayerMaster and the run-hold query below says "held", which Decide turns into Wait (relhold=no-entrance). The only
// other readers of PlayerMaster are the guild gates, whose actions the slice does not register.
bool HeldNoEntrance(Player* bot)
{
    return bot && !bot->IsAlive() && TortoiseBots::h3::IsCommandedBot(bot) && !BotMgr::Instance().ReleaseHasEntrance(bot);
}

struct RegisterHold
{
    RegisterHold() { RunReleaseHold::Register(&HeldNoEntrance); }
} registerHold;
}

BotActivity BotActivityLeaseManager::GetActivity(uint32_t guidLow) const
{
    ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(guidLow);
    if (TortoiseBots::h3::CommanderGuid(guid).IsEmpty())
        return BotActivity::Idle;
    Player* bot = ObjectAccessor::FindPlayer(guid);
    if (bot && !BotMgr::Instance().ReleaseHasEntrance(bot))
        return BotActivity::PlayerMaster;
    return BotActivity::Dungeon;
}

// ---- PlayerConvenience (behavior/PlayerConvenience.cpp) ----
PlayerConvenience& PlayerConvenience::Instance()
{
    static PlayerConvenience instance;
    return instance;
}

// The summon of a bot to its master: false = "not accepted", the callers then do nothing (they test the result). The host's own
// BotMgr::Think already teleports a bot that is on another map or more than 100 yd from its master.
bool PlayerConvenience::RequestSummon(Player* /*requester*/, Player* /*bot*/, Player* /*anchor*/)
{
    return false;
}

// ---- AhBotSellerAccount (runtime/AhBotSellerAccount.cpp): no auction bot sellers. ----
AhBotSellerAccount& AhBotSellerAccount::Instance()
{
    static AhBotSellerAccount instance;
    return instance;
}

bool AhBotSellerAccount::IsSellerGuid(uint32_t /*guidLow*/) const
{
    return false;
}

bool AhBotSellerAccount::IsSellerAccount(uint32_t /*accountId*/) const
{
    return false;
}

// ---- RandomBotAccountRegistry (runtime/RandomBotAccountRegistry.cpp): no pool accounts. ----
RandomBotAccountRegistry& RandomBotAccountRegistry::Instance()
{
    static RandomBotAccountRegistry instance;
    return instance;
}

// Startup load of the managed pool accounts (PlayerbotAIConfig::Initialize): nothing to load, the registry stays unvalidated
// (IsValidated() false), which is what the legacy code treats as "destructive pool operations refuse to run".
RegistryLoadResult RandomBotAccountRegistry::LoadValidatedAccounts()
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return RegistryLoadResult{};
}

// ---- GuildOrders (runtime/GuildOrderStore.cpp): the guild order store is not ported: there is never an order. ----
namespace GuildOrders
{
std::optional<OrderRecord> Find(uint32_t /*botGuidLow*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return std::nullopt;
}

EnchantTradeSession GetEnchantTrade(uint32_t /*botGuidLow*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return EnchantTradeSession{};
}

bool IsOrderProtectedItem(Player* /*bot*/, uint32_t /*itemId*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return false;
}

std::vector<DisenchantQueueEntry> DisenchantQueued(uint32_t botGuidLow)
{
    return tbhost::guild_orders::DisenchantQueued(botGuidLow);
}

void NoteDeath(uint32_t /*botGuidLow*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
}

// The mail read: no relist ledger, so no mail is matched to one and none is claimed.
void NoteTakenMailSender(uint32_t /*senderLow*/, uint32_t /*itemId*/, uint32_t /*count*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
}

std::vector<RelistLedgerEntry> RelistEntries(uint32_t /*botGuidLow*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return {};
}

bool NoteRelistClaimed(uint32_t /*botGuidLow*/, uint32_t /*seq*/, uint32_t /*mailId*/, uint32_t /*gold*/, uint32_t /*items*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return false;
}

bool NoteRelistReceived(uint32_t /*botGuidLow*/, uint32_t /*seq*/, uint32_t /*gold*/, uint32_t /*items*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return false;
}

void NoteDisenchantDropOff(uint32_t /*botGuidLow*/, uint32_t /*memberGuidLow*/, uint32_t /*nowMs*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
}

void NoteEnchantReplaceConfirmed(uint32_t /*botGuidLow*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
}
}

// ---- BotCommands (commands/Commander.cpp, commands/BotCommands.cpp): the remote commander and `.bot run` are not ported. ----
namespace BotCommands
{
// The bot stands in a group a commander bound. (Findings: the host's masters are plain players.)
// H3 (slice 3): a bot that `.tb master` bound to a commander (host/TbOrders.cpp) answers yes.
// P2: the binding record's lead's live group, like the two questions below (which read the same record).
bool IsInCommandedGroup(Player* bot)
{
    return TortoiseBots::h3::IsCommandedBot(bot);
}

// P2: the commander's binding holds this bot in its group (the binding record beside h3's table; commands/BotCommands.h's two questions). Both are
// one guid lookup and one group membership check; a released binding answers no at once.
bool IsCommandedBy(Player* commander, Player* bot)
{
    return TortoiseBots::cmd::IsCommandedBy(commander, bot);
}

bool IsInOwnBindingGroup(Player* commander, Player* bot)
{
    return TortoiseBots::cmd::IsInOwnBindingGroup(commander, bot);
}

// S6: these four answer from the run of the bot's party (host/run/TbRunHooks.h); "no run" (false / null / 0) when it has none or it is not running.
bool IsInRunningDungeonRun(Player* bot)
{
    return run::BotInRunningRun(bot);
}

bool IsRunKillFirst(Player* bot, uint32_t entry)
{
    return run::BotRunKillFirst(bot, entry);
}

// Null = "no armed area triggers".
std::vector<uint32_t> const* RunArmedAreaTriggers(Player* bot)
{
    return run::BotRunArmedTriggers(bot);
}

// 0 = "no bound run plan".
uint32_t BoundRunPlanMap(Player* bot)
{
    return run::BotRunPlanMap(bot);
}

// "Always true while no run exists" (runtime/CommandedGroupQuery.h): a chat order is never held back.
bool NoteHumanChatOrder(Player* /*bot*/, Player& /*from*/, std::string const& /*text*/, bool /*whisper*/)
{
    return true;
}

// seated false = "outside a bound raid's layout, no seat rule applies".
RaidTankSeat RaidTankHealerSeat(Player* /*bot*/)
{
    return RaidTankSeat{};
}
}
}
