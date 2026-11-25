// The enchanting upkeep's legacy calls: the enchant weight table and the
// bot's spec name live in the legacy item manager, and its header
// (RandomItemMgr -> GuidPosition -> WorldPosition) only compiles behind the
// compat shim. The shim also rewrites the core's singleton and logging
// macros, so it must not reach the tick file: this TU is the boundary, and it
// holds no logging of its own.
#include "host/TbHostProfessionUpkeep.h"

#include "compat/botpch_forever.h"
#include "host/TbSpecEnchant.h"
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
    if (!bot || !item)
        return false;
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return false;

    std::uint32_t specId = sRandomItemMgr.GetPlayerSpecId(bot);
    if (!specId)
        specId = sRandomItemMgr.GetFallbackSpecId(bot->GetClass());
    std::string const specName = sRandomItemMgr.GetSpecName(specId);
    if (specName.empty())
        return false;

    TortoiseBots::enchant::SpecEnchantDecision const choice = TortoiseBots::enchant::SpecEnchantFor(
        bot->GetClass(), specName, std::uint8_t(item->GetSlot()), bot->GetLevel(), std::uint8_t(proto->GetQuality()));
    return choice.verdict != TortoiseBots::enchant::SpecEnchantVerdict::NoRule;
}

} // namespace upkeep
} // namespace tbhost
