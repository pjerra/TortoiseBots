#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
#include "DruidActions.h"

using namespace ai;

bool CastCasterFormAction::Execute(Event& event)
{
    ai->RemoveShapeshift();
    return true;
}

namespace
{
Unit* FindRejuvenationSpreadTarget(PlayerbotAI* ai, Player* bot)
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return nullptr;
    Unit* best = nullptr;
    float bestHealth = (float)sPlayerbotAIConfig.almostFullHealth;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !sServerFacade.IsAlive(member))
            continue;
        if (member->GetMapId() != bot->GetMapId() || member->GetVisibility() == VISIBILITY_OFF)
            continue;
        if (member->GetHealthPercent() >= sPlayerbotAIConfig.almostFullHealth)
            continue;
        if (ai->HasMyAura("rejuvenation", member))
            continue;
        if (!best || member->GetHealthPercent() < bestHealth)
        {
            best = member;
            bestHealth = (float)member->GetHealthPercent();
        }
    }
    return best;
}
}

Unit* CastRejuvenationSpreadAction::GetTarget()
{
    return FindRejuvenationSpreadTarget(ai, bot);
}

bool CastRejuvenationSpreadAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target)
        return false;
    // The almost-full tier is a mana-gated top-up tier (HealthTriggers): no
    // healer HoTs below medium mana, and never double-blanket one target.
    if (AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana)
        return false;
    return CastHealingSpellAction::isUseful();
}
