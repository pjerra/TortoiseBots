// Pure parsing and wire-line formatting for the `professions` addon verb (TBF #134 piece P134-D).
// No core headers, so forever/tools/test_addon_professions.cpp tests it standalone.
#pragma once

#include "host/TbAddonPure.h"
#include "runtime/policy/provision/ProfessionSnapshotPolicy.h"

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace professions
{

inline constexpr char const* kProfessionsUsage = "Usage: .bot professions <online bot you control>";

struct ParsedProfessions
{
    bool ok = false;
    std::string botNameLower;
};

inline ParsedProfessions ParseProfessionsArgs(std::string_view args)
{
    ParsedProfessions out;
    std::istringstream input{ std::string(addon::pure::Trim(args)) };
    std::string word;
    std::string extra;
    input >> word >> extra;
    if (word.empty() || !extra.empty())
        return out;
    out.botNameLower = addon::pure::Lower(word);
    out.ok = true;
    return out;
}

struct RecipeSnapshot
{
    ProfessionSnapshotPolicy::AbilityRow row;
    std::string orderToken;
};

inline std::vector<std::string> BuildSnapshotLines(
    std::string_view botName,
    std::vector<ProfessionSnapshotPolicy::ProfessionRow> const& profs,
    std::vector<RecipeSnapshot> const& recipes,
    std::vector<ProfessionSnapshotPolicy::MissingRow> const& missing)
{
    using namespace ProfessionSnapshotPolicy;
    std::string const safeName = addon::pure::Safe(botName);
    std::vector<std::string> lines;
    lines.reserve(2 + profs.size() + recipes.size() + missing.size());
    lines.push_back(FormatBegin(safeName));
    for (ProfessionRow const& prof : profs)
        lines.push_back(FormatProfession(prof));
    for (RecipeSnapshot const& recipe : recipes)
        lines.push_back(FormatRecipe(recipe.row, recipe.orderToken));
    for (MissingRow const& row : missing)
        lines.push_back(FormatMissing(row));
    lines.push_back(FormatEnd(safeName, recipes.size(), missing.size()));
    return lines;
}

} // namespace professions
} // namespace TortoiseBots
