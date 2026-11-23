#pragma once

// Skinning sources of guild Farm orders: the boot-time index from skinning_loot_template and the
// verdict the loot stack and the loot window ask (ai::GuildOrderSkinMobFor on ForeverCore).
//
// A skin order's mob must first lose its body loot: the Skinning cast is refused while the corpse
// still holds any (Spell::CheckCast, SPELL_EFFECT_SKINNING). This gate is what lets the bot queue
// and take the grey loot of a Working skin order's mobs, and nothing else.
//
// World thread only: the index is built once at order-store load, and the verdict is read on a bot
// tick (memory only, no database access there).
#include <cstdint>

class Player;

namespace tbhost
{
namespace guild_order_skin
{

// One pass over creature_template_difficulty (DifficultyID 0, SkinLootID != 0) and
// skinning_loot_template, references expanded through reference_loot_template. Called from
// guild_orders::LoadOrders at boot; an unavailable world database or table leaves the index empty,
// which makes every query answer "not a skin source".
void LoadSkinSources();

// True when the creature entry can yield the bot's Working Farm order item by skinning: the item
// is in the entry's skin loot and the bot's Skinning skill covers the mob's level. Also the narrow
// gate the loot stack and the loot window use (ai::GuildOrderSkinMobFor).
bool SkinMobFor(Player* bot, std::uint32_t creatureEntry);

} // namespace guild_order_skin
} // namespace tbhost
