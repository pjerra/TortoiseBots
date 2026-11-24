// The permanent-enchant catalogue for the ForeverCore build.
//
// TB built this table from its own spell and skill-line stores
// (ai/playerbot/GuildOrderEnchants.cpp). ForeverCore keeps the same rows in
// DB2, so the scan reads SkillLineAbility rows whose classic profession
// (Player::GetClassicProfessionSkill) is Enchanting and each spell through
// sSpellMgr (SpellMgr.h). The matching rules themselves live in
// runtime/GuildOrderEnchantPolicy.h and are unit tested on their own.
//
// The catalogue is built once, on the first caller, so a build that never
// asks for it pays nothing. World thread only.
#include "playerbot/GuildOrderEnchants.h"

#include "host/TbAddonProfessions.h"
#include "runtime/GuildOrderEnchantPolicy.h"

#include "DB2Stores.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace ai
{
namespace
{
using namespace TortoiseBots::GuildOrders;

constexpr uint32_t kSkillEnchanting = 333;

bool& Built()
{
    static bool built = false;
    return built;
}

std::vector<EnchantEntry>& Index()
{
    static std::vector<EnchantEntry> index;
    return index;
}

std::string Lowered(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = char(c + ('a' - 'A'));
    return text;
}

// The lowest trainer requirement that teaches the spell: the skill line
// ability row's own min rank (DB2Structure.h:3722), the same value the
// craft index reads (TbHostGuildOrderRead.cpp BuildRecipeIndex).
bool HasEffect(SpellInfo const* spell, SpellEffects effect)
{
    for (SpellEffectInfo const& row : spell->GetEffects())
        if (row.Effect == effect)
            return true;
    return false;
}
} // namespace

void BuildGuildOrderEnchantIndex()
{
    if (Built())
        return;
    Built() = true;

    // Forever's recipes sit on child skill lines ("Classic Enchanting"), not on 333 itself.
    std::unordered_map<uint32_t, std::vector<SkillLineAbilityEntry const*>> bySpell;
    for (SkillLineAbilityEntry const* ability : sSkillLineAbilityStore)
    {
        if (!ability || ability->Spell <= 0 || !ability->SkillLine)
            continue;
        if (Player::GetClassicProfessionSkill(ability->SkillLine) != kSkillEnchanting)
            continue;
        bySpell[uint32_t(ability->Spell)].push_back(ability);
    }

    std::vector<EnchantEntry>& index = Index();
    for (auto const& [spellId, rows] : bySpell)
    {
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!spell)
            continue;

        bool const hasEnchant = HasEffect(spell, SPELL_EFFECT_ENCHANT_ITEM);
        bool const createsItem = HasEffect(spell, SPELL_EFFECT_CREATE_ITEM);
        bool const isSkillStep = HasEffect(spell, SPELL_EFFECT_SKILL_STEP);
        uint32_t const effect0 = spell->GetEffects().empty() ? 0u
            : uint32_t(spell->GetEffects().front().Effect);
        bool rowOk = false;
        for (SkillLineAbilityEntry const* ability : rows)
            if (IsCatalogueEnchantRow(kSkillEnchanting, hasEnchant, createsItem, isSkillStep, effect0))
            {
                rowOk = true;
                break;
            }
        if (!rowOk)
            continue;

        EnchantSlot const slot = EnchantSlotForMasks(spell->EquippedItemClass,
            spell->EquippedItemSubClassMask, spell->EquippedItemInventoryTypeMask);
        if (slot == EnchantSlot::Unknown)
            continue;

        EnchantEntry entry;
        entry.spellId = spellId;
        char const* name = spell->SpellName ? (*spell->SpellName)[LOCALE_enUS] : nullptr;
        entry.name = name ? name : "";
        entry.lowerName = Lowered(entry.name);
        entry.slot = slot;
        // ForeverCore's skill-line rows carry rank 1 for every recipe; the real gate is the trainer's rank or the recipe
        // item's. An enchant with neither (a reputation or raid reward) has no rank here, so the upkeep never grants it.
        auto const trainerIt = TortoiseBots::professions::TrainerMinSkillBySpell().find(spellId);
        auto const itemIt = TortoiseBots::professions::RecipeItemBySpell().find(spellId);
        ItemTemplate const* recipe = itemIt != TortoiseBots::professions::RecipeItemBySpell().end()
            ? sObjectMgr->GetItemTemplate(itemIt->second) : nullptr;
        bool const fromTrainer = trainerIt != TortoiseBots::professions::TrainerMinSkillBySpell().end();
        entry.reqSkill = TortoiseBots::GuildOrders::EnchantReqSkillCombine(fromTrainer, fromTrainer ? trainerIt->second : 0,
            recipe != nullptr, recipe ? recipe->GetRequiredSkillRank() : 0);
        for (uint8_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
            if (spell->ReagentCount[i] > 0 && spell->Reagent[i] > 0)
                entry.reagents.emplace_back(uint32_t(spell->Reagent[i]), uint32_t(spell->ReagentCount[i]));
        for (int32_t totem : spell->Totem)
            if (totem > 0)
                entry.tools.push_back(uint32_t(totem));
        index.push_back(std::move(entry));
    }

    std::sort(index.begin(), index.end(), EnchantSortLess);
    TC_LOG_INFO("tortoisebots", "Loading guild order enchants: {} permanent enchants.", uint32_t(index.size()));
}

bool GuildOrderEnchantsReady()
{
    return Built();
}

std::vector<TortoiseBots::GuildOrders::EnchantEntry> GuildOrderEnchants()
{
    BuildGuildOrderEnchantIndex();
    return Index();
}

std::vector<GuildOrderEnchantMaterial> GuildOrderEnchantReagents(uint32_t spellId)
{
    BuildGuildOrderEnchantIndex();
    for (EnchantEntry const& entry : Index())
    {
        if (entry.spellId != spellId)
            continue;
        std::vector<GuildOrderEnchantMaterial> out;
        for (auto const& [itemId, count] : entry.reagents)
            out.push_back(GuildOrderEnchantMaterial{ itemId, count });
        return out;
    }
    return {};
}

std::vector<uint32_t> GuildOrderEnchantTools(uint32_t spellId)
{
    BuildGuildOrderEnchantIndex();
    for (EnchantEntry const& entry : Index())
        if (entry.spellId == spellId)
            return entry.tools;
    return {};
}

} // namespace ai
