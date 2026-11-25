// The enchanting upkeep's one legacy call: the enchant weight table lives in
// the legacy item manager, and its header (RandomItemMgr -> GuidPosition ->
// WorldPosition) only compiles behind the compat shim. The shim also rewrites
// the core's singleton and logging macros, so it must not reach the tick file:
// this TU is the boundary, and it holds no logging of its own.
#include "host/TbHostProfessionUpkeep.h"

#include "compat/botpch_forever.h"
#include "playerbot/RandomItemMgr.h"

namespace tbhost
{
namespace upkeep
{

std::uint32_t EnchantWeightFor(Player* bot, std::uint32_t enchantId)
{
    if (!bot || !enchantId)
        return 0;
    std::uint8_t const spec = std::uint8_t(sRandomItemMgr.GetPlayerSpecId(bot));
    return sRandomItemMgr.CalculateEnchantWeight(bot->GetClass(), spec, enchantId);
}

bool SpecEnchantGoverns(Player* bot, Item* item)
{
    return sRandomItemMgr.SpecEnchantGoverns(bot, item);
}

} // namespace upkeep
} // namespace tbhost
