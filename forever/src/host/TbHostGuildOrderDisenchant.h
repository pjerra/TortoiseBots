// The disenchant service (TBF #155): a guild member mails an item to a bot
// with the subject `disenchant`; the bot casts Disenchant on it if its
// Enchanting skill allows, collects the loot and mails it back.
//
// Two halves share this header: the guarded characters store for
// `tortoise_bots_guild_disenchant` (TbHostGuildOrderDisenchantStore.cpp) and
// the mailbox/cast service on the core's own handlers
// (TbHostGuildOrderDisenchant.cpp). The rules are in
// runtime/GuildOrderDisenchantPolicy.h; the row type is
// runtime/GuildOrderContract.h's DisenchantQueueEntry.
//
// Core-free declarations on purpose: the generic mail take (a compiled
// legacy file) asks IsDisenchantServiceMail so it leaves service mail to the
// host tick.
#pragma once

#include "runtime/GuildOrderContract.h"

#include <cstdint>
#include <vector>

class ObjectGuid;
class Player;
struct Mail;

namespace tbhost
{
namespace guild_orders
{

// Startup read of the whole table, once, from TbScripts's load hook.
void LoadDisenchantQueue();
// False when the table is missing (the service is off for this session).
bool IsDisenchantStoreAvailable();

// The bot's queued items, oldest first. Safe on any thread.
std::vector<TortoiseBots::GuildOrders::DisenchantQueueEntry> DisenchantQueued(uint32_t botGuidLow);
// Writes one row synchronously and reads it back: false means the table did
// not take it, so the caller must not treat the item as queued. Memory
// changes only when the write landed.
bool NoteDisenchantQueued(TortoiseBots::GuildOrders::DisenchantQueueEntry const& entry);
bool SaveDisenchantRow(TortoiseBots::GuildOrders::DisenchantQueueEntry const& entry);
bool DeleteDisenchantRow(uint32_t botGuidLow, uint64_t itemGuidLow);

// A guild member's normal mail with the `disenchant` subject, at least one
// item and no cash on delivery: the service owns it, the generic mail take
// must not pull it into the bags.
bool IsDisenchantServiceMail(Player* bot, Mail* mail);

// Every unlooted piece of the item's disenchant window, through the core's
// own autostore handler; the window is released only when nothing is left.
// Shared with the maintenance upkeep (TbHostProfessionUpkeep.cpp): both cast
// the client's Disenchant on an item the bot owns.
void TakeItemLoot(Player* bot, ObjectGuid const& itemGuid);

// The per-bot service tick (world thread, beside TickBotOrder and
// TickGuildMail): mail intake, the cast, loot collection, result mail.
void TickDisenchantService(Player* bot, uint32_t diffMs);
// True while the bot walks to a mailbox for the service (the engine pauses).
bool HasActiveDisenchantErrand(Player* bot);
// The bot is leaving the world: its in-memory walk/cast state goes.
void DisenchantForgetBot(uint32_t botGuidLow);

} // namespace guild_orders
} // namespace tbhost
