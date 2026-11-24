// #48 round 1: the runtime of the spell-school switch; see TbSchool.h. Core and compat only, so both builds (slice ON and OFF)
// compile it: nothing here needs the bot engine.
#include "host/TbSchool.h"

#include "host/data/TbSchoolData.gen.h"

#include "compat/TbCompat_School.h"

#include "TbBotMgr.h"

#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace school
{
namespace
{
struct SpecEntry
{
    SpecSchools schools;
    std::uint32_t spellId[ai::kSchoolCount] = {};   // the resolved id of the spec's spell of each school, 0 = none
};

struct Index
{
    std::map<std::pair<std::uint8_t, std::string>, SpecEntry> specs;
    std::map<std::uint32_t, std::pair<std::uint8_t, std::uint8_t>> creatures;   // entry -> (immuneMask, vulnMask)
    std::uint32_t resolved = 0;
    std::uint32_t skipped = 0;
};

std::atomic<bool> g_enabled{ false };
std::atomic<int> g_minGainPct{ 10 };

std::mutex g_indexMutex;
std::shared_ptr<Index const> g_index;

std::shared_ptr<Index const> CurrentIndex()
{
    std::lock_guard<std::mutex> lock(g_indexMutex);
    return g_index;
}

void Publish(std::shared_ptr<Index const> index)
{
    std::lock_guard<std::mutex> lock(g_indexMutex);
    g_index = std::move(index);
}

bool SchoolBit(int school, std::uint8_t& mask)
{
    if (school < 0 || school >= ai::kSchoolCount)
        return false;
    mask = std::uint8_t(mask | (1u << school));
    return true;
}
}

void LoadConfig()
{
    g_enabled.store(sConfigMgr->GetBoolDefault("TortoiseBots.SpellSchool.Enable", false), std::memory_order_release);
    // A negative gain would switch for a school that takes less: 0 is the honest floor, and 1000 the ceiling (a percent of a
    // percent is meaningless, and the value is added to a taken percent kept in an int16).
    g_minGainPct.store(std::clamp<int>(int(sConfigMgr->GetIntDefault("TortoiseBots.SpellSchool.VulnerableMinPct", 10)), 0, 1000),
        std::memory_order_release);
}

bool Enabled()
{
    return g_enabled.load(std::memory_order_acquire);
}

int MinGainPct()
{
    return g_minGainPct.load(std::memory_order_acquire);
}

void BuildIndex()
{
    auto index = std::make_shared<Index>();
    // The spell mirror answers nothing while the module itself is off, so a resolve would only log 22 skips at boot.
    bool const resolve = Enabled() && tbcompat::SchoolSwitchComputableCompat() && BotMgr::Instance().Enabled();

    for (data::NukeRow const& row : data::kNukes)
    {
        std::uint32_t spellId = 0;
        if (resolve)
        {
            std::uint32_t school = 0;
            if (!tbcompat::ResolveSpellByNameCompat(row.spell, spellId, school))
            {
                ++index->skipped;
                TC_LOG_WARN("tortoisebots", "TortoiseBots: spell-school switch: no spell named '{}' in the spell store; the row is skipped", row.spell);
                continue;
            }
            if (school != row.school)
            {
                ++index->skipped;
                TC_LOG_WARN("tortoisebots", "TortoiseBots: spell-school switch: '{}' is {}.csv's school {} but the store says {}; the row is skipped",
                    row.spell, row.spec, uint32(row.school), school);
                continue;
            }
            ++index->resolved;
        }

        SpecEntry& spec = index->specs[{ row.classId, row.spec }];
        spec.schools.spec = row.spec;
        if (!SchoolBit(row.school, spec.schools.knownMask))
            continue;
        if (row.order == 0)
            spec.schools.mainSchool = row.school;
        spec.spellId[row.school] = spellId;
    }

    // A spec whose main row was skipped has no baseline for the gain test: no pick, as if the spec had no rows.
    for (auto& entry : index->specs)
    {
        SpecSchools& schools = entry.second.schools;
        if (schools.mainSchool < 0 || (schools.knownMask & (1u << schools.mainSchool)) == 0)
            schools.knownMask = 0;
    }

    for (data::CreatureRow const& row : data::kCreatures)
        index->creatures[row.entry] = { row.immuneMask, row.vulnMask };

    std::uint32_t const specs = std::uint32_t(index->specs.size());
    std::uint32_t const creatures = std::uint32_t(index->creatures.size());
    std::uint32_t const resolved = index->resolved;
    std::uint32_t const skipped = index->skipped;
    Publish(std::move(index));

    if (resolve)
        TC_LOG_INFO("tortoisebots", "TortoiseBots: spell-school switch: {} specs, {} creature rows, {} rows resolved, {} skipped (gain {} %, switch {})",
            specs, creatures, resolved, skipped, MinGainPct(), Enabled() ? "on" : "off");
}

SpecSchools SchoolsOf(Player const* bot, int specTab)
{
    if (!bot)
        return {};
    char const* const spec = ai::SpecNameOf(bot->GetClass(), specTab);
    if (!spec || !*spec)
        return {};
    std::shared_ptr<Index const> const index = CurrentIndex();
    if (!index)
        return {};
    auto const itr = index->specs.find({ bot->GetClass(), spec });
    if (itr == index->specs.end())
        return {};
    return itr->second.schools;
}

Reading Read(Unit const* target)
{
    Reading reading;
    for (int school = 0; school < ai::kSchoolCount; ++school)
        reading.state.takenPct[school] = 100;
    if (!target)
        return reading;

    std::uint32_t const templateMask = tbcompat::TemplateSchoolImmuneMaskCompat(target);
    for (int school = 0; school < ai::kSchoolCount; ++school)
    {
        std::uint32_t const bit = 1u << school;
        bool const templateImmune = (templateMask & bit) != 0;
        bool const auraImmune = tbcompat::AuraSchoolImmuneCompat(target, school);
        if (templateImmune || auraImmune)
        {
            reading.state.immuneMask = std::uint8_t(reading.state.immuneMask | bit);
            reading.coreReading = true;
        }
        int const taken = tbcompat::SchoolTakenPctCompat(target, school);
        if (taken != 100)
            reading.coreReading = true;
        reading.state.takenPct[school] = std::int16_t(std::clamp(taken, 0, 32767));
    }

    if (!target->IsCreature())
        return reading;
    std::shared_ptr<Index const> const index = CurrentIndex();
    if (!index)
        return reading;
    auto const itr = index->creatures.find(target->GetEntry());
    if (itr == index->creatures.end())
        return reading;
    reading.dataRow = true;
    reading.state.immuneMask = std::uint8_t(reading.state.immuneMask | itr->second.first);
    for (int school = 0; school < ai::kSchoolCount; ++school)
        // +50 for a school the core shows nothing for: an aura's own number always wins over the table's.
        if ((itr->second.second & (1u << school)) && reading.state.takenPct[school] == 100)
            reading.state.takenPct[school] = 150;
    return reading;
}

ai::SchoolState StateOf(Unit const* target)
{
    return Read(target).state;
}

int PickFor(SpecSchools const& spec, Unit const* target)
{
    if (!Enabled() || !target || spec.mainSchool < 0 || spec.knownMask == 0)
        return ai::kSchoolStay;
    return ai::PickSchool(StateOf(target), spec.knownMask, spec.mainSchool, MinGainPct());
}
}
}
