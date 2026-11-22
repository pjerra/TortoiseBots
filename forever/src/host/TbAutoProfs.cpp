#include "host/TbAutoProfs.h"

#include "host/TbAutoProfsPure.h"
#include "TbBotMgr.h"

#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <atomic>

namespace TortoiseBots
{
namespace autoprofs
{
namespace
{

std::atomic<bool> g_enabled{ true };

bool HasAnyPrimaryProfession(Player const* bot)
{
    static constexpr uint32_t kPrimaries[] = {
        prof::SkillBlacksmithing,
        prof::SkillLeatherworking,
        prof::SkillAlchemy,
        prof::SkillHerbalism,
        prof::SkillMining,
        prof::SkillTailoring,
        prof::SkillEngineering,
        prof::SkillEnchanting,
        prof::SkillSkinning,
    };
    for (uint32_t skillId : kPrimaries)
    {
        if (bot->HasSkill(skillId))
            return true;
    }
    return false;
}

bool LearnApprenticeSkill(Player* bot, uint32_t skillId, char const* why)
{
    if (!bot || bot->HasSkill(skillId))
        return false;
    if (prof::IsPrimaryProfession(skillId) && bot->GetFreePrimaryProfessionPoints() == 0)
        return false;

    uint32_t const spellId = ApprenticeSpellForSkill(skillId);
    if (!spellId)
        return false;

    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info || !SpellMgr::IsSpellValid(info, bot, false))
        return false;

    if (!bot->HasSpell(spellId))
        bot->LearnSpell(spellId, false);

    if (!bot->HasSkill(skillId))
        return false;

    TC_LOG_INFO("tortoisebots", "auto-profs: {} learned {} (skill {}, spell {}, val {}/{}) on {}",
        bot->GetName(), prof::SkillNameFromId(skillId), skillId, spellId,
        bot->GetSkillValue(skillId), bot->GetMaxSkillValue(skillId), why ? why : "sync");
    return true;
}

} // namespace

void LoadConfig()
{
    g_enabled = sConfigMgr->GetBoolDefault("TortoiseBots.AutoProfessions", true);
}

bool Enabled()
{
    return g_enabled;
}

uint32_t Sync(Player* bot, char const* why)
{
    if (!Enabled() || !BotMgr::Instance().Enabled() || !bot || !bot->IsInWorld())
        return 0;
    if (!BotMgr::Instance().IsPoolBot(bot->GetGUID()))
        return 0;
    if (bot->GetLevel() < PRIMARY_PROFESSION_MIN_LEVEL)
        return 0;

    AutoProfInputs inputs;
    inputs.level = bot->GetLevel();
    inputs.classId = bot->GetClass();
    inputs.guidCounter = bot->GetGUID().GetCounter();
    inputs.isPoolBot = true;
    inputs.hasPrimaryProfession = HasAnyPrimaryProfession(bot);
    inputs.hasFirstAid = bot->HasSkill(prof::SkillFirstAid);
    inputs.hasCooking = bot->HasSkill(prof::SkillCooking);

    AutoProfPlan const plan = PlanAutoProfessions(inputs);
    uint32_t taught = 0;
    for (uint32_t skillId : plan.skillsToLearn)
    {
        if (LearnApprenticeSkill(bot, skillId, why))
            ++taught;
    }
    return taught;
}

} // namespace autoprofs
} // namespace TortoiseBots
