#pragma once

#include <cstdint>
#include <unordered_map>

namespace TortoiseBots
{
namespace professions
{
void RegisterAddonVerbsProfessions();
// Recipe spell -> the lowest-entry recipe item that teaches it, and -> the lowest tradeskill-trainer skill rank that teaches it.
std::unordered_map<uint32_t, uint32_t> const& RecipeItemBySpell();
std::unordered_map<uint32_t, uint32_t> const& TrainerMinSkillBySpell();
} // namespace professions
} // namespace TortoiseBots
