#include "host/TbSpecEnchant.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h" // EQUIPMENT_SLOT_END

#include "compat/TbCompat_S5.h" // the schema guard: a missing table is "no rule", never a core abort

#include <array>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace TortoiseBots
{
namespace enchant
{
namespace
{

struct SpecRows
{
    // (class, canonical spec name) -> one rule per equipment slot.
    std::map<std::pair<std::uint8_t, std::string>, std::array<SpecEnchantRule, EQUIPMENT_SLOT_END>> rows;
};

SpecRows& Rows()
{
    static SpecRows state;
    return state;
}

std::mutex& RowsMutex()
{
    static std::mutex mutex;
    return mutex;
}

} // namespace

void LoadSpecEnchants()
{
    std::map<std::pair<std::uint8_t, std::string>, std::array<SpecEnchantRule, EQUIPMENT_SLOT_END>> loaded;
    std::size_t count = 0;
    std::size_t skipped = 0;

    bool missing = false;
    QueryResult result = WorldQueryCompat(
        "SELECT `class_id`, `spec_name`, `slotid`, `spellid`, `min_level` FROM `ai_playerbot_spec_enchant`", &missing);
    if (missing)
        return; // the schema guard logged the table it could not read
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            std::uint8_t const classId = fields[0].GetUInt8();
            std::string specName = fields[1].GetString();
            std::uint8_t const slot = fields[2].GetUInt8();
            std::uint32_t const spellId = fields[3].GetUInt32();
            std::uint8_t const minLevel = fields[4].GetUInt8();
            if (!classId || specName.empty() || slot >= EQUIPMENT_SLOT_END)
            {
                ++skipped;
                continue;
            }
            SpecEnchantRule& rule = loaded[std::make_pair(classId, std::move(specName))][slot];
            rule.present = true;
            rule.spellId = spellId;
            rule.minLevel = minLevel;
            ++count;
        } while (result->NextRow());
    }

    {
        std::lock_guard<std::mutex> lock(RowsMutex());
        Rows().rows = std::move(loaded);
    }

    if (skipped)
        TC_LOG_ERROR("server.loading", "TortoiseBots: {} bot spec enchant rows name no class, no spec or no equipment slot", skipped);
    TC_LOG_INFO("server.loading", ">> Loaded {} bot spec enchant rows", count);
}

SpecEnchantDecision SpecEnchantFor(std::uint8_t classId, std::string_view specName,
    std::uint8_t equipSlot, std::uint32_t level, std::uint8_t itemQuality)
{
    if (classId == 0 || specName.empty() || equipSlot >= EQUIPMENT_SLOT_END)
        return SpecEnchantDecision{};

    SpecEnchantRule rule;
    {
        std::lock_guard<std::mutex> lock(RowsMutex());
        auto const& rows = Rows().rows;
        auto spec = rows.find(std::make_pair(classId, std::string(specName)));
        if (spec != rows.end())
            rule = spec->second[equipSlot];
    }
    return DecideSpecEnchant(rule, level, itemQuality);
}

} // namespace enchant
} // namespace TortoiseBots

#endif // TORTOISEBOTS_SLICE1
