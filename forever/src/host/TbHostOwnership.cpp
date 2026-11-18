// Host service R6B-2: the pool, hire, claim, lease, commander and guild-order questions the legacy engine asks, answered for a
// ForeverCore host bot. Built only with TORTOISEBOTS_SLICE1 (CMakeLists.txt).
//
// A host bot (forever/src/TbBotMgr.h) is no pool bot, no hire, no claimed guild bot, holds no activity lease, belongs to no
// commanded group and has no guild order. Each function below keeps the legacy name and signature and answers that; the
// "not on this host" ones also count under a feature of the unported list, so `.tb unported` shows how often the engine asked.
// Every answer is listed in forever/docs/SLICE1-R6B-NOTES.md (section 3, choice c). The real owners of these names
// (runtime/HireLifecycle.cpp, ClaimLifecycle.cpp, BotActivityLease.cpp, AhBotSellerAccount.cpp, RandomBotAccountRegistry.cpp,
// GuildOrderStore.cpp, commands/Commander.cpp, behavior/PlayerConvenience.cpp) are not part of the slice: if one is ported, its
// function must leave this file (a duplicate symbol at link says so).
#include "compat/botpch_forever.h"

#include "behavior/PlayerConvenience.h"
#include "runtime/AhBotSellerAccount.h"
#include "runtime/BotActivityLease.h"
#include "runtime/ClaimLifecycle.h"
#include "runtime/CommandedGroupQuery.h"
#include "runtime/GuildOrderStore.h"
#include "runtime/HireLifecycle.h"
#include "runtime/RandomBotAccountRegistry.h"

namespace TortoiseBots
{
// ---- HireLifecycle (runtime/HireLifecycle.cpp): nobody is hired. ----
HireLifecycle& HireLifecycle::Instance()
{
    static HireLifecycle instance;
    return instance;
}

bool HireLifecycle::IsHired(ObjectGuid /*botGuid*/) const
{
    return false;
}

ObjectGuid HireLifecycle::GetMaster(ObjectGuid /*botGuid*/) const
{
    return ObjectGuid::Empty;
}

// ---- ClaimLifecycle (runtime/ClaimLifecycle.cpp): no guild bot is claimed. ----
ClaimLifecycle& ClaimLifecycle::Instance()
{
    static ClaimLifecycle instance;
    return instance;
}

bool ClaimLifecycle::IsClaimedByPlayer(uint32_t /*botGuidLow*/, uint32_t /*playerGuidLow*/) const
{
    return false;
}

// ---- BotActivityLeaseManager (runtime/BotActivityLease.cpp): no bot holds a lease, every bot is Idle. ----
BotActivityLeaseManager& BotActivityLeaseManager::Instance()
{
    static BotActivityLeaseManager instance;
    return instance;
}

BotActivity BotActivityLeaseManager::GetActivity(uint32_t /*guidLow*/) const
{
    return BotActivity::Idle;
}

// ---- PlayerConvenience (behavior/PlayerConvenience.cpp) ----
PlayerConvenience& PlayerConvenience::Instance()
{
    static PlayerConvenience instance;
    return instance;
}

// The summon of a bot to its master: not ported. False = "not accepted", the callers then do nothing (they test the result). The
// host's own BotMgr::Think already teleports a bot that is on another map or more than 100 yd from its master.
bool PlayerConvenience::RequestSummon(Player* /*requester*/, Player* /*bot*/, Player* /*anchor*/)
{
    tbcompat::Unported(tbcompat::Feature::PlayerSummon);
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

std::vector<DisenchantQueueEntry> DisenchantQueued(uint32_t /*botGuidLow*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
    return {};
}

void NoteDeath(uint32_t /*botGuidLow*/)
{
    tbcompat::Unported(tbcompat::Feature::PoolServices);
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
// The bot is in no commander-bound group. (Findings: the host's masters are plain players.)
bool IsInCommandedGroup(Player* /*bot*/)
{
    return false;
}

bool IsInRunningDungeonRun(Player* /*bot*/)
{
    return false;
}

bool IsRunKillFirst(Player* /*bot*/, uint32_t /*entry*/)
{
    return false;
}

// Null = "no armed area triggers".
std::vector<uint32_t> const* RunArmedAreaTriggers(Player* /*bot*/)
{
    return nullptr;
}

// 0 = "no bound run plan".
uint32_t BoundRunPlanMap(Player* /*bot*/)
{
    return 0;
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
