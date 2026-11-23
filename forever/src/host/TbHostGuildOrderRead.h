#pragma once

#include "runtime/GuildOrderContract.h"

#include <cstdint>
#include <string>
#include <vector>

class ChatHandler;
class Player;

namespace TortoiseBots
{
namespace h3
{

struct Words;

namespace guild_read
{

// The read-only `.tb guildorder` verbs. They answer from the store and live
// bot state and never write an order.
bool IsReadVerb(std::string const& sub);
bool Handle(ChatHandler* handler, Words const& words, Player* sender, std::string const& sub);

// One entry of the memory-only step timeline a `steps` reply prints. Time is
// the store's wall clock at the change; age is clamped at 0 on the reply.
struct OrderStepView
{
    uint32_t ageSec = 0;
    GuildOrders::OrderState state = GuildOrders::OrderState::Working;
    GuildOrders::OrderReason reason = GuildOrders::OrderReason::None;
    GuildOrders::SupplyStep step = GuildOrders::SupplyStep::None;
    uint32_t legItemId = 0;   // the supply leg's item when the row names one
    uint32_t orderItemId = 0; // the order's own item, so a row reads on its own
};

// Called by the order store at the one point every row mutation passes
// (GiveOrder). `fresh` starts a new timeline (no row before, or a different
// order took the bot's slot); otherwise only a changed tuple is appended.
void NoteOrderHistory(GuildOrders::OrderRecord const& record, bool fresh);
std::vector<OrderStepView> OrderHistory(uint32_t botGuidLow);
uint32_t DroppedOrderSteps(uint32_t botGuidLow);
void ForgetOrderHistory(uint32_t botGuidLow);
void ForgetAllOrderHistory();

} // namespace guild_read
} // namespace h3
} // namespace TortoiseBots
