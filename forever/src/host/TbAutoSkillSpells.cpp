#include "host/TbAutoSkillSpells.h"

#include "host/TbAutoSkillSpellsPure.h"

#include "DB2Stores.h"
#include "DB2Structure.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <vector>

namespace tbhost
{
namespace skillspells
{
namespace
{

// True for the spells this pass owns: the equip/combat flags the core reads,
// and the weapon "Shoot" abilities (a bow without Shoot Bow never pulls).
bool RewardedAbility(SkillLineAbilityEntry const* ability, SpellInfo const* spell)
{
    if (spell->HasEffect(SPELL_EFFECT_DUAL_WIELD) || spell->HasEffect(SPELL_EFFECT_BLOCK) ||
        spell->HasEffect(SPELL_EFFECT_PARRY))
        return true;
    if (spell->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE))
        return true;
    if (!spell->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL))
        return false;
    // Effect 17 is shared with melee abilities (Heroic Strike, pet Cleave): a
    // row only counts for a weapon skill line, never for a class line.
    SkillLineEntry const* skillLine = sSkillLineStore.LookupEntry(ability->SkillLine);
    return skillLine && skillLine->CategoryID == SKILL_CATEGORY_WEAPON;
}

// A spell the core's own load pass learned (Player::LearnSkillRewardedSpells) sits in
// m_spells as dependent, and Player::_SaveSpells never writes a dependent spell: it is
// in the spell book now and gone after the next character load. Promote what this pass
// owns so it is saved, and learn a spell the bot does not have as a normal one.
void Grant(Player* bot, SpellInfo const* spell, std::uint32_t& taught, std::uint32_t& kept)
{
    PlayerSpellMap& spells = bot->GetSpellMap();
    PlayerSpellMap::iterator itr = spells.find(spell->Id);
    if (itr == spells.end())
    {
        bot->LearnSpell(spell->Id, false);
        ++taught;
        return;
    }

    if (!itr->second.dependent || itr->second.state == PLAYERSPELL_REMOVED ||
        itr->second.state == PLAYERSPELL_TEMPORARY)
        return;

    itr->second.dependent = false;
    itr->second.state = PLAYERSPELL_CHANGED;
    ++kept;
}

// Every skill-line ability this pass could ever teach, built once: the table
// is static for the run and the per-bot pass only filters it.
std::vector<SkillLineAbilityEntry const*> const& RewardedRows()
{
    static std::vector<SkillLineAbilityEntry const*> const rows = []()
    {
        std::vector<SkillLineAbilityEntry const*> out;
        for (std::uint32_t id = 0; id < sSkillLineAbilityStore.GetNumRows(); ++id)
        {
            SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(id);
            if (!ability || ability->Spell <= 0)
                continue;
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(std::uint32_t(ability->Spell), DIFFICULTY_NONE);
            if (!spell || !RewardedAbility(ability, spell))
                continue;
            out.push_back(ability);
        }
        return out;
    }();
    return rows;
}

// A hunter that knows no bow, gun or crossbow skill gets the one its ranged weapon
// needs, or Bows for the Worn Shortbow the starter path and the spawn kit equip.
// The core's own create pass seeds a weapon skill at 1 (Player::LearnDefaultSkill,
// SKILL_RANGE_LEVEL); combat raises it from there.
void EnsureHunterRangedSkill(Player* bot)
{
    if (!HunterRangedSkillOwed(bot->GetClass(), bot->GetPureSkillValue(SKILL_BOWS),
            bot->GetPureSkillValue(SKILL_GUNS), bot->GetPureSkillValue(SKILL_CROSSBOWS)))
        return;

    std::uint32_t rangedWeaponSkill = 0;
    if (Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
        if (ItemTemplate const* proto = ranged->GetTemplate())
            rangedWeaponSkill = proto->GetSkill();

    std::uint32_t const skill = HunterRangedSkill(rangedWeaponSkill);
    std::uint16_t const maxValue = bot->GetMaxSkillValueForLevel();
    bot->SetSkill(skill, 0, 1, maxValue);
    TC_LOG_INFO("tortoisebots", "skill spells: {} learned ranged weapon skill {} at level {}", bot->GetName(), skill, bot->GetLevel());
}

} // namespace

std::uint32_t Sync(Player* bot, char const* why)
{
    if (!bot)
        return 0;

    std::uint32_t const classMask = bot->GetClassMask();
    Races const race = Races(bot->GetRace());
    std::uint32_t const level = bot->GetLevel();
    std::uint32_t taught = 0;
    std::uint32_t kept = 0;

    // Before the ability loop: a seeded skill line is what makes the world's own
    // skill-rewarded rows (the weapon's shots) reachable for this bot.
    EnsureHunterRangedSkill(bot);

    for (SkillLineAbilityEntry const* ability : RewardedRows())
    {
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(std::uint32_t(ability->Spell), DIFFICULTY_NONE);
        if (!spell)
            continue;
        bool const dualWield = spell->HasEffect(SPELL_EFFECT_DUAL_WIELD);

        // Every other row belongs to a skill the bot already owns: the seed decides
        // which weapon skills a bot has, exactly as the legacy pass leaves it. Dual
        // Wield is the exception: a bot may own the skill without the spell, or reach
        // the class level before the skill is seeded, and the threshold is the rule.
        if (!dualWield)
        {
            std::uint32_t const skillValue = bot->GetPureSkillValue(ability->SkillLine);
            if (!skillValue || skillValue < std::uint32_t(ability->MinSkillLineRank))
                continue;
        }
        if (ability->ClassMask && !(std::uint32_t(ability->ClassMask) & classMask))
            continue;
        if (!ability->RaceMask.IsEmpty() && !ability->RaceMask.HasRace(race))
            continue;
        if (spell->SpellLevel && level < std::uint32_t(spell->SpellLevel))
            continue;
        if (dualWield && !DualWieldOwed(bot->GetClass(), level))
            continue;

        Grant(bot, spell, taught, kept);

        // The flags are per session (Player::m_canDualWield and friends are
        // never saved), so a spell already in the book still needs them set.
        if (dualWield)
            bot->SetCanDualWield(true);
        if (spell->HasEffect(SPELL_EFFECT_BLOCK))
            bot->SetCanBlock(true);
        if (spell->HasEffect(SPELL_EFFECT_PARRY))
            bot->SetCanParry(true);
    }

    if (taught || kept)
        TC_LOG_INFO("tortoisebots", "skill spells: {} learned {}, kept {} skill-rewarded abilit{} at level {} on {}",
            bot->GetName(), taught, kept, (taught + kept) == 1 ? "y" : "ies", level, why ? why : "sync");
    return taught + kept;
}

} // namespace skillspells
} // namespace tbhost
