// The `lootlog` addon verb (TBF #134 piece i-loot): the last day of loot and
// roll rows for group bots, over the TBM addon channel (`TbAddonChannel.h`).
// Ports TortoiseBots/commands/Inventory.cpp HandleLootLog: same scope (group
// members' bots, else the requester's own bots), same bot_events.csv pieces,
// same 40-row / 24 h / 250 B rules, same TBM:LOOT_BEGIN/LOOT/LOOT_END lines
// and the same `loot-log` cap. Poll only: no live push (the shared live push
// is an unported no-op on this core). Plus `.tb lootlog` for GM testing.
// Parsing and line building live in `TbAddonLootLogPure.h`.
#pragma once

#include "ChatCommand.h"

class ChatHandler;

namespace TortoiseBots
{
namespace lootlog
{
void RegisterAddonVerbsLootLog();
bool HandleLootLogChat(ChatHandler* handler, Trinity::ChatCommands::Tail args);
} // namespace lootlog
} // namespace TortoiseBots
