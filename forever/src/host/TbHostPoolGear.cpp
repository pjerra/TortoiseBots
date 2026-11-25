// Host answers for the pool gear top-up (forever/src/compat/TbCompat_PoolGear.h): the pool identity, the in-memory
// per-level marker and the incremental gear pass over the W6b gear index. Built only with TORTOISEBOTS_SLICE1
// (CMakeLists.txt compiles forever/src/host/TbHost*.cpp in that build only).
#include "compat/botpch_forever.h"

#include "TbBotMgr.h"
#include "host/gear/TbGearGen.h"

#include "Item.h"
#include "ItemTemplate.h"
#include "Player.h"

#include "runtime/PoolGearTopUpPolicy.h"
#include "runtime/HireLifecycle.h"

#include <map>

namespace tbs_poolgear
{
namespace
{
// The legacy facade kept the last topped-up level in a process-local map too (runtime/PlayerbotRuntimeFacade.cpp
// values): nothing here is saved, so a restart gives every bot one check at its current level.
std::map<uint32, uint32> s_lastTopUpLevel;
}

bool IsPoolBot(Player* bot)
{
    return bot && TortoiseBots::BotMgr::Instance().IsPoolBot(bot->GetObjectGuid());
}

bool IsHired(Player* bot)
{
    return bot && TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
}

uint32 LastLevel(Player* bot)
{
    if (!bot)
        return 0;
    auto const it = s_lastTopUpLevel.find(bot->GetGUIDLow());
    return it == s_lastTopUpLevel.end() ? 0 : it->second;
}

void MarkLevel(Player* bot, uint32 level)
{
    if (bot)
        s_lastTopUpLevel[bot->GetGUIDLow()] = level;
}

uint32 TopUp(Player* bot, uint32 level)
{
    if (!bot || level < TortoiseBots::kPoolGearTopUpMinLevel)
        return 0;

    uint32 mask = 0;
    for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            continue;
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (TortoiseBots::PoolGearTopUpSlot(item != nullptr,
                proto ? proto->GetQuality() : 0,
                proto ? proto->GetBaseItemLevel() : 0, level))
            mask |= 1u << slot;
    }

    if (!mask)
        return 0;

    return TortoiseBots::gear::TopUpGearForTarget(bot, level, mask).equipped;
}
}
