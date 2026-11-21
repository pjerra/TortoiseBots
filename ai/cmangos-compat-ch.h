// The Turtle side of the both-core helpers round S1-R2. Same names and signatures as forever/src/compat/TbCompat_CH.h; each body is the code
// the bot ran before the move, not changed: 1.12's SetTargetIcon takes the slot and the unit, its durability numbers and the repair estimate
// read the Turtle tables, and `help` lists the whole chat action context (which compiles on this core).
#pragma once

#include "ChatActionContext.h"

#include <set>
#include <string>

class Group;
class Item;

inline void SetTargetIconCompat(Group* group, int index, ObjectGuid unit, ObjectGuid /*changedBy*/)
{
    group->SetTargetIcon(index, unit);
}

inline uint32 RestXPCompat(Player const* player) { return player->GetUInt32Value(PLAYER_REST_STATE_EXPERIENCE); }

// StatsAction::EstRepair, moved here as it was: lost durability, the subclass multiplier and the quality modifier.
inline uint32 ItemRepairCostCompat(Item const* item)
{
    uint32 maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
    if (!maxDurability)
        return 0;

    uint32 curDurability = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
    uint32 LostDurability = maxDurability - curDurability;
    if (LostDurability == 0)
        return 0;

    ItemPrototype const* ditemProto = GetItemProtoCompat(item);
    DurabilityCostsEntry const* dcost = sDurabilityCostsStore.LookupEntry(ditemProto->ItemLevel);
    if (!dcost)
    {
        sLog.outError("RepairDurability: Wrong item lvl %u", ditemProto->ItemLevel);
        return 0;
    }

    uint32 dQualitymodEntryId = (ditemProto->Quality + 1) * 2;
    DurabilityQualityEntry const* dQualitymodEntry = sDurabilityQualityStore.LookupEntry(dQualitymodEntryId);
    if (!dQualitymodEntry)
    {
        sLog.outError("RepairDurability: Wrong dQualityModEntry %u", dQualitymodEntryId);
        return 0;
    }

    uint32 dmultiplier = dcost->multiplier[ItemSubClassToDurabilityMultiplierId(ditemProto->Class, ditemProto->SubClass)];
    uint32 costs = uint32(LostDurability * dmultiplier * double(dQualitymodEntry->quality_mod));
    if (costs == 0)                                     // fix for ITEM_QUALITY_ARTIFACT
        costs = 1;
    return costs;
}

inline uint32 SkillLineAbilityMinCompat(SkillLineAbilityEntry const* ability) { return ability->min_value; }
inline uint32 SkillLineAbilityMaxCompat(SkillLineAbilityEntry const* ability) { return ability->max_value; }

inline std::set<std::string> ChatCommandKeys(PlayerbotAI*)
{
    std::set<std::string> keys;
    ai::ChatActionContext context;
    context.GetSupportedKeys(keys);
    return keys;
}
