#pragma once

// Free-bot profession upkeep on the ForeverCore build: an enchanter breaks
// down disenchantable junk in its bags and applies a better permanent enchant
// to the gear it wears, both through the core's own cast path (the client's
// CMSG_CAST_SPELL with the item as target) so the skill check, reagents,
// skill-ups and the item enchantment are the core's.
//
// The decisions are in ai/playerbot/policy/profession/EnchantUpkeepPolicy.h;
// this pair only resolves the core facts and runs the casts. World thread
// only: BotMgr's per-bot pass after the engine tick.
#include <cstdint>

class Player;

namespace tbhost
{
namespace upkeep
{

// One pass over the bot. `disenchant` and `enchantGear` are
// TortoiseBots.World.Disenchant and TortoiseBots.World.EnchantGear.
void TickProfessionUpkeep(Player* bot, std::uint32_t diffMs, bool disenchant, bool enchantGear);

// True while a cast this service started is in flight: BotMgr pauses the
// engine brain so the bot's own movement does not interrupt it.
bool HasActiveUpkeepCast(Player* bot);

// The bot leaves the world: its cooldowns and failure marks go with it.
void ForgetProfessionUpkeep(std::uint32_t botGuidLow);

} // namespace upkeep
} // namespace tbhost
