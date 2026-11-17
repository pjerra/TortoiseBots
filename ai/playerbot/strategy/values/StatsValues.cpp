
#include "playerbot/playerbot.h"
#include <memory>
#include "StatsValues.h"
#include "../../../runtime/HunterPetPolicy.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/actions/CheckMountStateAction.h"

using namespace ai;

uint8 HealthValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 100;
    return (static_cast<float> (target->GetHealth()) / target->GetMaxHealth()) * 100;
}

bool IsDeadValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;
    return sServerFacade.GetDeathState(target) != ALIVE;
}

bool PetIsDeadValue::Calculate()
{
    // Hunters have no pet system before level 10 (Revive Pet auto-learns at
    // 10+, AutoLearnSpellAction.cpp:62): without this, a pet-less lowbie with
    // a stale character_pet row reads "pet dead" and loops a failing revive.
    // Rule ported from mod-playerbots StatsValues.cpp (mature behavior donor).
    if ((bot->GetLevel() < TortoiseBots::HUNTER_PET_MIN_LEVEL && bot->GetClass() == CLASS_HUNTER) || bot->IsMounted())
        return false;

    if (bot->GetPet())
        return sServerFacade.GetDeathState(bot->GetPet()) != ALIVE;

    if (bot->GetPetGuid())
        return true;

    return false;
}

bool PetIsHappyValue::Calculate()
{
    if (!bot->GetPet())
        return true;

    return bot->GetPet()->GetHappinessState() == HAPPY;
}


uint8 RageValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 0;
    return (target->GetPower(POWER_RAGE) / 10.0f);
}

uint8 EnergyValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 0;
    return (static_cast<float> (target->GetPower(POWER_ENERGY)));
}

uint8 ManaValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 100;
    if (!target->GetMaxPower(POWER_MANA))
        return 0;
    return (static_cast<float> (target->GetPower(POWER_MANA)) / target->GetMaxPower(POWER_MANA)) * 100;
}

bool HasManaValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;
    return target->GetPower(POWER_MANA);
}


uint8 ComboPointsValue::Calculate()
{
    Unit *target = GetTarget();
	if (!target || target->getObjectGuid() != bot->GetComboTargetGuid())
		return 0;

    return bot->GetComboPoints();
}

bool IsMountedValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (target->IsMounted())
        return true;

    if (AI_VALUE2(uint32, "current mount speed", getQualifier()))
        return true;

    return false;
}


bool IsInCombatValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (sServerFacade.IsInCombat(target)) return true;

    if (target == bot)
    {
        Group* group = bot->GetGroup();
        if (group)
        {
            Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
            for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
            {
                Player *member = sObjectMgr.GetPlayer(itr->guid);
                if (!member || member == bot) continue;

                if (sServerFacade.IsInCombat(member) &&
                    sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.getDistance2d(member, bot), sPlayerbotAIConfig.reactDistance)) return true;
            }
        }
    }

    return false;
}

uint8 BagSpaceValue::Calculate()
{
    uint32 totalused = 0, total = 16;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
    {
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            totalused++;
    }

    uint32 totalfree = 16 - totalused;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        const Bag* const pBag = (Bag*) bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
        {
            ItemPrototype const* pBagProto = GetItemProtoCompat(pBag);
            if (pBagProto->Class == ITEM_CLASS_CONTAINER && pBagProto->SubClass == ITEM_SUBCLASS_CONTAINER)
            {
                total += pBag->GetBagSize();
                totalfree += pBag->GetFreeSlots();
                totalused += pBag->GetBagSize() - pBag->GetFreeSlots();
            }
        }

    }

    return (static_cast<float> (totalused) / total) * 100;
}

uint8 DurabilityValue::Calculate()
{
    uint32 totalMax = 0, total = 0;

    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        uint16 pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        Item* item = bot->GetItemByPos(pos);

        if (!item)
            continue;

        uint32 maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
        if (!maxDurability)
            continue;

        totalMax += maxDurability;

        uint32 curDurability = item->GetUInt32Value(ITEM_FIELD_DURABILITY);

        total += curDurability;
    }

    if (total == 0)
        return 0;

    return (static_cast<float> (total) / totalMax) * 100;
}

uint8 DurabilityInventoryValue::Calculate()
{
    uint32 totalMax = 0, total = 0;

    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        uint16 pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        Item* item = bot->GetItemByPos(pos);

        if (!item)
            continue;

        uint32 maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
        if (!maxDurability)
            continue;

        totalMax += maxDurability;

        uint32 curDurability = item->GetUInt32Value(ITEM_FIELD_DURABILITY);

        total += curDurability;
    }

    if (total == 0)
        return 0;

    return (static_cast<float> (total) / totalMax) * 100;
}

uint8 LowestDurabilityValue::Calculate()
{
    uint32 durabilityPercent, minDurabilityPercent = 100;

    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        uint16 pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        Item* item = bot->GetItemByPos(pos);

        if (!item)
            continue;

        uint32 maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
        if (!maxDurability)
            continue;

        uint32 curDurability = item->GetUInt32Value(ITEM_FIELD_DURABILITY);

        durabilityPercent = (static_cast<float> (curDurability) / maxDurability) * 100;

        if (durabilityPercent < minDurabilityPercent)
            minDurabilityPercent = durabilityPercent;
    }

    return minDurabilityPercent;
}

uint8 SpeedValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 100;

    return (uint8) (100.0f * target->GetSpeedRate(MOVE_RUN));
}
