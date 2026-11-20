// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "HireGearCache.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DatabaseEnv.h"
#include "Log.h"

#include <cstring>
#include "../ai/playerbot/TbCompatW3.h"

namespace TortoiseBots
{

HireGearCache& HireGearCache::Instance()
{
    static HireGearCache instance;
    return instance;
}

void HireGearCache::Load()
{
    std::vector<HireGearRow> rows;
    // The generator fills this table later; until then it is legitimately
    // empty (missing table on an unmigrated install reads the same way).
    auto result = WorldDatabase.Query(
        "SELECT `class`, `spec`, `race`, `tier`, `slot`, `item_id`, `enchant_spell` "
        "FROM `tortoise_bots_hire_gear`");
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            HireGearRow row;
            row.classId = fields[0].GetUInt8();
            std::string spec = FieldTextW3(fields[1]);
            std::strncpy(row.spec, spec.c_str(), sizeof(row.spec) - 1);
            row.spec[sizeof(row.spec) - 1] = '\0';
            row.race = fields[2].GetUInt8();
            row.tier = fields[3].GetUInt8();
            row.slot = fields[4].GetUInt8();
            row.itemId = fields[5].GetUInt32();
            row.enchantSpell = fields[6].GetUInt32();
            rows.push_back(row);
        } while (result->NextRow());
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rows.swap(rows);
    }
    sLog.outString(">> Loaded %u hire gear rows", uint32(m_rows.size()));
    sLog.outString();
}

void HireGearCache::SetRowsForTest(std::vector<HireGearRow> const& rows)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rows = rows;
}

bool HireGearCache::HasSetFor(uint8_t classId, char const* spec, uint8_t tier) const
{
    if (!spec)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (HireGearRow const& row : m_rows)
    {
        if (row.classId == classId && row.tier == tier && std::strcmp(row.spec, spec) == 0)
            return true;
    }
    return false;
}

bool HireGearCache::FindRow(uint8_t classId, char const* spec, uint8_t race,
    uint8_t tier, uint8_t slot, HireGearRow const*& out) const
{
    out = nullptr;
    if (!spec)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return LookupHireGearRow(m_rows.data(), m_rows.size(), classId, spec, race, tier, slot, out);
}

size_t HireGearCache::RowCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rows.size();
}

} // namespace TortoiseBots
