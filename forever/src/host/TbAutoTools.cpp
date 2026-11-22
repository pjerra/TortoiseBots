#include "host/TbAutoTools.h"

#include "TbBotMgr.h"
#include "host/TbAutoToolsPure.h"

#include "Config.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"

#include <atomic>
#include <vector>

namespace TortoiseBots
{
namespace autotools
{

namespace
{

std::atomic<bool> g_enabled{ true };

bool GrantToolIntoBag(Player* bot, uint32_t itemId)
{
    if (!bot || !itemId)
        return false;
    if (!sObjectMgr->GetItemTemplate(itemId))
        return false;
    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, dest, itemId, 1) != EQUIP_ERR_OK)
        return false;
    return bot->StoreNewItem(dest, itemId, true, 0, GuidSet(), ItemContext::NONE) != nullptr;
}

} // namespace

void LoadConfig()
{
    g_enabled.store(sConfigMgr->GetBoolDefault("TortoiseBots.AutoTools", true), std::memory_order_relaxed);
}

bool Enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

uint32_t Sync(Player* bot, char const* why)
{
    if (!Enabled() || !bot || !bot->IsInWorld())
        return 0;

    ObjectGuid const guid = bot->GetGUID();
    BotMgr const& mgr = BotMgr::Instance();
    if (!mgr.IsBot(guid))
        return 0;
    if (!mgr.IsPoolBot(guid) && !mgr.IsFreeBot(guid))
        return 0;

    std::vector<uint32_t> const missing = MissingToolsForSkills(
        [bot](uint32_t skillId) { return bot->HasSkill(skillId); },
        [bot](uint32_t itemId) { return bot->HasItemCount(itemId, 1, false); });

    uint32_t granted = 0;
    for (uint32_t itemId : missing)
    {
        if (GrantToolIntoBag(bot, itemId))
        {
            ++granted;
            TC_LOG_INFO("tortoisebots", "[autotools] bot={} why={} granted item={}",
                bot->GetName(), why ? why : "?", itemId);
        }
    }
    return granted;
}

} // namespace autotools
} // namespace TortoiseBots
