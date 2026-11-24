#pragma once

// Enchant orders on the ForeverCore build: the giver's item is enchanted by
// a guild bot enchanter after a Meet in its capital. The decisions come from
// runtime/GuildOrderEnchantLifecyclePolicy.h (the meet/walk/trade legs),
// runtime/GuildOrderMeetPolicy.h (who may press Meet and when) and
// ai/playerbot/policy/guild/EnchantTradePolicy.h (the two trade windows);
// this pair only reads the core and runs the effects.
//
// World thread only: the `.tb guildorder` command and BotMgr's per-bot pass.
#include "runtime/GuildOrderContract.h"

#include <cstdint>

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace h3
{
struct Words;
}
}

namespace tbhost
{
namespace guild_orders
{

// `.tb guildorder create <bot> enchant <spell link or id>`.
bool HandleEnchantCreate(ChatHandler* handler, TortoiseBots::h3::Words const& words, Player* sender);
// `.tb guildorder meet <bot>`.
bool HandleEnchantMeet(ChatHandler* handler, TortoiseBots::h3::Words const& words, Player* sender);

// One pass over the bot's live enchant order: the ready gate, the Meet walk
// and the two trade windows. Runs for every bot that has a row; a bot with
// no enchant order costs one store lookup.
void TickEnchantOrder(Player* bot);

// True when the open window belongs to a live enchant order of this trader
// (the giver): the addon trade driver must leave that window to this service.
bool IsEnchantOrderTrader(Player* bot, Player* trader);

// True while the Meet walk or a trade window runs: BotMgr pauses the engine
// brain for this bot so its own movement does not fight the walk.
bool HasActiveEnchantErrand(Player* bot);

// True when `itemId` is a reagent of the enchant spell `spellId`: the order
// reserves it while the row runs (IsItemNeededByGuildOrder).
bool IsEnchantReagent(uint32_t spellId, uint32_t itemId);

// Dropped on logout: the Meet request and the trade session are memory-only.
void ForgetEnchantOrder(uint32_t botGuidLow);

} // namespace guild_orders
} // namespace tbhost
