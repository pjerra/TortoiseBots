// `.bot maintenance <bot>` (ticket #137, piece E): TB's HandleMaintenance
// (commands/Inventory.cpp:247) on the `.bot` surface. TB's seven factory
// top-ups are not compiled on ForeverCore (only InitAmmo has a host body,
// TbHostPetFactory.cpp): reagents and bandages are TB's fixed tables re-run
// through the core's own item grant, potions/food/consumes need the unported
// RandomItemMgr caches and thrown never served owned bots, so those four do
// LESS (TbUpkeepPure.h pins the skipped set). Repair and save are the core's
// own calls; the repair takes the Trinity third argument (no guild-bank
// repair) TB's two-argument call has no room for.
#include "host/player/TbUpkeep.h"

#include "ObjectMgr.h"
#include "Player.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "compat/botpch_forever.h"
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#endif

#include <cstdint>
#include <string>

namespace TortoiseBots
{
namespace upkeep
{
namespace
{

// First Aid, the skill line 129 on both cores (the ForeverCore SkillType
// enum has no name for it).
inline constexpr std::uint32_t kSkillFirstAid = 129;

// TB's per-class reagent table (PlayerbotFactory::InitReagents): the fixed
// ids and level gates, every id verified against the core's ItemSparse.db2.
// The spell-scan totem tail is not ported (starter totems ride the login
// seed); counts are TB's regCount per class, shards capped at the keep band.
struct ReagentRow
{
    std::uint8_t classId;
    std::uint8_t minLevel;
    std::uint32_t itemId;
    std::uint32_t regCount;
    std::uint32_t cap;
};

inline constexpr ReagentRow kReagents[] = {
    { CLASS_MAGE, 12, 17056, 2, 0 },    // Light Feather
    { CLASS_MAGE, 20, 17031, 2, 0 },    // Rune of Teleportation
    { CLASS_MAGE, 36, 17032, 2, 0 },    // Rune of Portals
    { CLASS_MAGE, 56, 17020, 2, 0 },    // Arcane Powder
    { CLASS_DRUID, 20, 17034, 2, 0 },   // Maple Seed
    { CLASS_DRUID, 30, 17035, 2, 0 },   // Stranglethorn Seed
    { CLASS_DRUID, 40, 17036, 2, 0 },   // Ashwood Seed
    { CLASS_DRUID, 50, 17037, 2, 0 },   // Hornbeam Seed
    { CLASS_DRUID, 50, 17021, 2, 0 },   // Wild Berries
    { CLASS_DRUID, 60, 17038, 2, 0 },   // Ironwood Seed
    { CLASS_DRUID, 60, 17026, 2, 0 },   // Wild Thornroot
    { CLASS_PALADIN, 30, 17033, 3, 0 }, // Symbol of Divinity
    { CLASS_PALADIN, 52, 21177, 3, 0 }, // Symbol of Kings
    { CLASS_SHAMAN, 23, 17057, 1, 0 },  // Shiny Fish Scales
    { CLASS_SHAMAN, 29, 17058, 1, 0 },  // Fish Oil
    { CLASS_SHAMAN, 30, 17030, 1, 0 },  // Ankh
    { CLASS_WARLOCK, 10, 6265, 2, 5 },  // Soul Shard (organic only: top up to the keep band, never bulk-seed)
    { CLASS_WARLOCK, 50, 5565, 2, 0 },  // Infernal Stone
    { CLASS_PRIEST, 48, 17028, 3, 0 },  // Holy Candle (below 60 only: 60 casts the higher rank)
    { CLASS_PRIEST, 60, 17029, 3, 0 },  // Sacred Candle
    { CLASS_ROGUE, 22, 5140, 1, 0 },    // Flash Powder
    { CLASS_ROGUE, 34, 5530, 1, 0 },    // Blinding Powder
};

// TB's First Aid ladder (PlayerbotFactory::InitBandages): the skill
// thresholds and the vendored cloth bandage of each tier.
inline std::uint32_t BandageForSkill(std::uint16_t firstAid)
{
    if (firstAid >= 225) return 14530;
    if (firstAid >= 200) return 14529;
    if (firstAid >= 175) return 8545;
    if (firstAid >= 150) return 8544;
    if (firstAid >= 125) return 6451;
    if (firstAid >= 100) return 6450;
    if (firstAid >= 75) return 3531;
    if (firstAid >= 50) return 3530;
    if (firstAid >= 20) return 2581;
    return 1251;
}

// Bounded top-up toward `want`: grant only what is missing, never destroy,
// never exceed the want. A template the core does not know is skipped.
std::uint32_t TopUp(Player* bot, std::uint32_t itemId, std::uint32_t want)
{
    if (!bot || !itemId || !want)
        return 0;
    ItemTemplate const* tpl = sObjectMgr.GetItemTemplate(itemId);
    if (!tpl || tpl->GetMaxStackSize() == 0)
        return 0;
    std::uint32_t const have = bot->GetItemCount(itemId);
    if (have >= want)
        return 0;
    std::uint32_t const add = want - have;
    if (!bot->StoreNewItemInBestSlots(itemId, add, ItemContext::NONE))
        return 0;
    return add;
}

std::uint32_t TopUpReagents(Player* bot)
{
    std::uint32_t granted = 0;
    std::uint8_t const classId = bot->GetClass();
    std::uint8_t const level = bot->GetLevel();
    for (ReagentRow const& row : kReagents)
    {
        if (row.classId != classId || level < row.minLevel)
            continue;
        if (row.itemId == 17028 && level >= 60)
            continue;
        ItemTemplate const* tpl = sObjectMgr.GetItemTemplate(row.itemId);
        if (!tpl || tpl->GetMaxStackSize() == 0)
            continue;
        std::uint32_t want = tpl->GetMaxStackSize() * row.regCount;
        if (row.cap != 0 && want > row.cap)
            want = row.cap;
        granted += TopUp(bot, row.itemId, want);
    }
    return granted;
}

std::uint32_t TopUpBandages(Player* bot)
{
    std::uint32_t const itemId = BandageForSkill(bot->GetSkillValue(kSkillFirstAid));
    ItemTemplate const* tpl = sObjectMgr.GetItemTemplate(itemId);
    if (!tpl || tpl->GetMaxStackSize() == 0)
        return 0;
    return TopUp(bot, itemId, tpl->GetMaxStackSize() / 2);
}

} // namespace

bool Run(Player* bot, Summary& summary)
{
    summary = Summary{};
    if (!bot || !bot->IsInWorld())
        return false;
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    summary.grantedItems += TopUpReagents(bot);
    summary.grantedItems += TopUpBandages(bot);
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitAmmo();
    bot->DurabilityRepairAll(false, 0.0f, false);
    summary.repaired = true;
    bot->SaveToDB();
    return true;
#else
    return false;
#endif
}

std::string Describe(Summary const& summary)
{
    std::string out = std::to_string(summary.grantedItems) + " item(s) granted";
    out += summary.repaired ? ", repaired, saved" : ", repair skipped";
    return out;
}

bool IsEnabled()
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    return sPlayerbotAIConfig.ownedBotMaintenanceEnabled;
#else
    return false;
#endif
}

} // namespace upkeep
} // namespace TortoiseBots
