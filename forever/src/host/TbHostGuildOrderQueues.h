#pragma once

// The guild order queues that pay value back to the guild bank (TBF #155):
// the relist ledger (an auction surplus the order posted, settled by mail
// later) and the refund of unspent guild gold (including the stub a reassign
// leaves behind). The store is the guarded characters table
// `tortoise_bots_guild_relist` plus the in-memory rows; the tick is the
// world-thread banker pass that pays them.
//
// The ledger rules themselves are the pure policy in
// runtime/GuildOrderRelistPolicy.h and runtime/GuildOrderRefundPolicy.h.
#include "runtime/GuildOrderRelistPolicy.h"
#include "runtime/GuildOrderStore.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

class Player;

namespace tbhost
{
namespace guild_orders
{

// Startup load of the relist ledger. Creates the table when the characters
// database user may, then reads every row through the schema guard; a missing
// table or column leaves the ledger off with one warning.
void LoadGuildOrderQueues();
bool IsRelistStoreAvailable();

// The bot's ledger rows, oldest first. Any thread (the mail read is map
// thread, the drains are world thread); rows are copied under the lock.
std::vector<TortoiseBots::GuildOrders::RelistLedgerEntry> RelistLedger(uint32_t botGuidLow);
// A matched mail is about to be taken: the row names it and the value it
// promises. False when the table did not take the change, so the mail must
// not be taken.
bool RelistClaimed(uint32_t botGuidLow, uint32_t seq, uint32_t mailId, uint32_t gold, uint32_t items);
// The claimed mail was collected: the value is in the bot's hands.
bool RelistReceived(uint32_t botGuidLow, uint32_t seq, uint32_t gold, uint32_t items);
// The received rows a bank trip has to pay out, oldest first.
std::vector<TortoiseBots::GuildOrders::RelistLedgerEntry> RelistDeposits(uint32_t botGuidLow);
// Checked ledger row writes (insert/replace and delete): the table lands
// before memory changes.
bool SaveRelistRow(TortoiseBots::GuildOrders::RelistLedgerEntry const& row);
bool DeleteRelistRow(uint32_t botGuidLow, uint32_t seq);

// The giver-attribution stash of the mail read: the take-mail action records
// the per-mail counts, and the buying action drains them for the item. Memory
// only (a restart drops the stash with the mail still in the mailbox).
void StashTakenMailSender(uint32_t senderLow, uint32_t itemId, uint32_t count);
std::vector<TortoiseBots::GuildOrders::TakenMailSender> TakeTakenMailSenders(uint32_t itemId);

// One stale sweep a minute, never per bot tick.
bool RelistSweepDue(uint64_t nowSec);
// The bots whose rows are old enough to expire (see RelistEntryAged).
std::vector<uint32_t> AgedRelistBots(uint64_t nowSec);
// Drops rows the policy names (RelistEntryExpiry) and deletes them from the
// table. `presence` holds one entry per aged bot, online or not.
void ExpireRelistRows(uint64_t nowSec,
    std::unordered_map<uint32_t, TortoiseBots::GuildOrders::RelistBotPresence> const& presence);

// World thread, from the bot manager's idle pass beside TickBotOrder: repay
// unspent guild gold and pay collected relist value at a guild banker in
// reach (walking to one within the order visit distance). A bot with nothing
// owed costs one store lookup.
void TickGuildOrderQueues(Player* bot);
// A bot left the world: its session start is forgotten, so the expiry sweep
// never counts a previous session.
void ForgetGuildOrderQueueSession(uint32_t botGuidLow);

} // namespace guild_orders
} // namespace tbhost
