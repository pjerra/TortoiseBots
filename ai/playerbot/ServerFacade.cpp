
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "Database/DBCStore.h"

#include "Database/DatabaseEnv.h"
#include "PlayerbotAI.h"

#include "Movement/TargetedMovementGenerator.h"

ServerFacade::ServerFacade() {}
ServerFacade::~ServerFacade() {}

float ServerFacade::GetDistance2d(Unit *unit, WorldObject* wo)
{
    if (!unit || !wo)
        return false;

    float dist =
#ifdef MANGOS
    unit->getDistance2d(wo);
#endif
#ifdef CMANGOS
    unit->GetDistance2d(wo);
#endif
    return round(dist * 10.0f) / 10.0f;
}

float ServerFacade::GetDistance2d(Unit *unit, float x, float y)
{
    float dist =
#ifdef MANGOS
    unit->getDistance2d(x, y);
#endif
#ifdef CMANGOS
    unit->GetDistance2d(x, y);
#endif
    return round(dist * 10.0f) / 10.0f;
}

bool ServerFacade::IsDistanceLessThan(float dist1, float dist2)
{
    return dist1 - dist2 < sPlayerbotAIConfig.targetPosRecalcDistance;
}

bool ServerFacade::IsDistanceGreaterThan(float dist1, float dist2)
{
    return dist1 - dist2 > sPlayerbotAIConfig.targetPosRecalcDistance;
}

bool ServerFacade::IsDistanceGreaterOrEqualThan(float dist1, float dist2)
{
    return !IsDistanceLessThan(dist1, dist2);
}

bool ServerFacade::IsDistanceLessOrEqualThan(float dist1, float dist2)
{
    return !IsDistanceGreaterThan(dist1, dist2);
}

void ServerFacade::SetFacingTo(Unit* unit, float angle, bool force)
{
    MotionMaster &mm = *unit->GetMotionMaster();
    if (!force && !unit->IsStopped()) unit->SetFacingTo(angle);
    else
    {
        SetOrientationAndSendCompat(unit, angle);
    }
    //unit->m_movementInfo.RemoveMovementFlag(MovementFlags(MOVEFLAG_SPLINE_ENABLED | MOVEFLAG_FORWARD));
}

bool ServerFacade::IsFriendlyTo(Unit* bot, Unit* to)
{
#ifdef MANGOS
    return bot->IsFriendlyTo(to);
#endif
#ifdef CMANGOS
    return bot->IsFriendlyTo(to);
#endif
}

bool ServerFacade::IsHostileTo(Unit* bot, Unit* to)
{
#ifdef MANGOS
    return bot->IsHostileTo(to);
#endif
#ifdef CMANGOS
    return bot->IsHostileTo(to);
#endif
}

bool ServerFacade::IsFriendlyTo(WorldObject* bot, Unit* to)
{
#ifdef MANGOS
    return bot->IsFriendlyTo(to);
#endif
#ifdef CMANGOS
    return bot->IsFriendlyTo(to);
#endif
}

bool ServerFacade::IsHostileTo(WorldObject* bot, Unit* to)
{
#ifdef MANGOS
    return bot->IsHostileTo(to);
#endif
#ifdef CMANGOS
    return bot->IsHostileTo(to);
#endif
}


bool ServerFacade::IsSpellReady(Player* bot, uint32 spell, uint32 itemId)
{
#ifdef MANGOS
    return !bot->HasSpellCooldown(spell);
#endif
#ifdef CMANGOS
    if (!bot || !spell)
        return false;
    if (!itemId)
        return !bot->HasSpellCooldown(spell);
    // Item casts are gated by the core on the item row's spell category
    // (Unit::AddSpellAndCategoryCooldowns prefers the item prototype's
    // SpellCategory/SpellCooldown over the DBC row), so a pure spell-id
    // check calls ready while the core still refuses with NOT_READY.
    // Live case: Mana Ruby (8008) carries spell cooldown 0 but category
    // 1153 cooldown 120 s; every in-combat retry inside that window was
    // selected (isPossible true) then refused at cast time. Mirror the
    // core lookup: item row matching this spell, else the DBC category.
    if (bot->HasSpellCooldown(spell))
        return false;
    uint32 category = 0;
    if (const ItemPrototype* proto = GetItemProtoByIdCompat(itemId))
    {
        for (const auto& spellData : proto->Spells)
        {
            if (spellData.SpellId == spell)
            {
                category = spellData.SpellCategory;
                break;
            }
        }
    }
    if (!category)
    {
        if (const SpellEntry* spellInfo = GetSpellEntryCompat(spell))
            category = spellInfo->Category;
    }
    return !category || !HasSpellCategoryCooldownCompat(bot, category);
#endif
}



bool ServerFacade::IsUnderwater(Unit *unit)
{
#ifdef MANGOS
    return unit->IsUnderWater();
#endif
#ifdef CMANGOS
    return IsUnderwaterCompat(unit);
#endif
}

FactionTemplateEntry const* ServerFacade::GetFactionTemplateEntry(Unit *unit)
{
#ifdef MANGOS
    return unit->GetFactionTemplateEntry();
#endif
#ifdef CMANGOS
    return unit->GetFactionTemplateEntry();
#endif
}

namespace
{
float RelativeTargetAngle(Unit* unit, Unit* target)
{
    if (!unit || !target)
        return 0.0f;

    float angle = GetAngleCompat(target, unit) - target->GetOrientation();
    while (angle > M_PI_F)
        angle -= 2.0f * M_PI_F;
    while (angle < -M_PI_F)
        angle += 2.0f * M_PI_F;
    return angle;
}
}

Unit* ServerFacade::GetChaseTarget(Unit* target)
{
    return TargetedMovementTargetCompat(target);
}

float ServerFacade::GetChaseAngle(Unit* target)
{
    return RelativeTargetAngle(target, GetChaseTarget(target));
}

float ServerFacade::GetChaseOffset(Unit* target)
{
    Unit* chaseTarget = GetChaseTarget(target);
    return target && chaseTarget ? target->GetDistance2d(chaseTarget) : 0.0f;
}

bool ServerFacade::isMoving(Unit *unit)
{
#ifdef MANGOS
    return unit->m_movementInfo.HasMovementFlag(MOVEFLAG_MASK_MOVING);
#endif
#ifdef CMANGOS
    return !unit->IsStopped();
#endif
}
