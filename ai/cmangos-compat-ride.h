#pragma once
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "MountManager.hpp"
#include "Battlegrounds/BattleGroundWS.h"

// TurtleCore keeps the item-to-mount spell mapping of its collection mounts in the core-owned
// `collection_mount` table and answers it through sMountMgr (MountValues.cpp:158-178 in the
// pre-port tree). ForeverCore has no collection mounts: a mount item there carries its own mount
// spell, so the fallback in MountValue::GetMountSpell covers it.
inline std::optional<uint32> MountSpellForCollectionItemCompat(uint32 itemId)
{
    return sMountMgr.GetMountSpellId(itemId);
}

inline std::vector<std::pair<uint32, uint32>> LoadCollectionMountRowsCompat()
{
    std::vector<std::pair<uint32, uint32>> rows;
    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT itemId, spellId FROM collection_mount"));
    if (!result)
        return rows;
    do
    {
        Field* fields = result->Fetch();
        rows.emplace_back(fields[0].GetUInt32(), fields[1].GetUInt32());
    } while (result->NextRow());
    return rows;
}

inline std::unordered_set<uint32> LoadLootMountItemsCompat()
{
    std::unordered_set<uint32> items;
    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT DISTINCT c.itemId FROM collection_mount c WHERE "
        "EXISTS (SELECT 1 FROM creature_loot_template l WHERE l.item = c.itemId) OR "
        "EXISTS (SELECT 1 FROM gameobject_loot_template l WHERE l.item = c.itemId) OR "
        "EXISTS (SELECT 1 FROM reference_loot_template l WHERE l.item = c.itemId)"));
    if (result)
    {
        do
            items.insert(result->Fetch()[0].GetUInt32());
        while (result->NextRow());
    }
    return items;
}

// Turtle's rider ladder: a mount's speed scales with SKILL_RIDING (0 -> level/2, 75 -> 60,
// 150 -> 100; any other value unmounts in that core).
inline uint32 RiderMountSpeedCompat(Player* bot)
{
    if (!bot)
        return 0;
    switch (bot->GetSkillValue(SKILL_RIDING))
    {
    case 75:
        return 60;
    case 150:
        return 100;
    case 0:
        return bot->GetLevel() / 2;
    default:
        return 0;
    }
}

// The per-race riding skill lines of Turtle. ForeverCore has one SKILL_RIDING (762).
inline uint32 RidingSkillForRaceCompat(uint8 race)
{
    switch (race)
    {
    case RACE_HUMAN:
        return SKILL_RIDING_HORSE;
    case RACE_ORC:
        return SKILL_RIDING_WOLF;
    case RACE_NIGHTELF:
        return SKILL_RIDING_TIGER;
    case RACE_DWARF:
        return SKILL_RIDING_RAM;
    case RACE_TROLL:
        return SKILL_RIDING_RAPTOR;
    case RACE_GNOME:
        return SKILL_RIDING_MECHANOSTRIDER;
    case RACE_UNDEAD:
        return SKILL_RIDING_UNDEAD_HORSE;
    case RACE_TAUREN:
        return SKILL_RIDING_KODO;
    default:
        return SKILL_RIDING;
    }
}

inline bool MountSpeedAlways100Compat(SpellEntry const* spellInfo)
{
    return spellInfo && (spellInfo->Custom & SPELL_CUSTOM_MOUNT_SPEED_100);
}

inline bool MountSpeedIgnoresRidingSkillCompat(SpellEntry const* spellInfo)
{
    return spellInfo && (spellInfo->Custom & SPELL_CUSTOM_IGNORE_RIDING_SKILL_MOUNT_SPEED);
}

inline bool MapAllowsMountsCompat(Player* bot)
{
    MapEntry const* mapEntry = sMapStorage.LookupEntry<MapEntry>(bot->GetMapId());
    return !mapEntry || mapEntry->IsMountAllowed();
}

inline bool IsOutdoorsCompat(Player* bot)
{
    return bot->GetMap()->GetTerrain()->IsOutdoors(bot->getPositionX(), bot->getPositionY(), bot->getPositionZ());
}

// The original battleground gate: no mounting next to a WS flag stand.
inline bool CanMountInBattlegroundCompat(Player* bot)
{
    if (bot->GetBattleGroundTypeId() == BattleGroundTypeId::BATTLEGROUND_WS)
    {
        BattleGroundWS* bg = (BattleGroundWS*)bot->GetBattleGround();
        if (!bg)
            return false;
        if (bot->HasAura(23333) || bot->HasAura(23335))
            return false;
        GameObject* allianceFlagStand = bg->GetBgMap()->GetGameObject(ObjectGuid(HIGHGUID_GAMEOBJECT, 179830, 90000));
        if (bot->IsWithinDistInMap(allianceFlagStand, 3.0f))
            return false;
        GameObject* hordeFlagStand = bg->GetBgMap()->GetGameObject(ObjectGuid(HIGHGUID_GAMEOBJECT, 179831, 90001));
        if (bot->IsWithinDistInMap(hordeFlagStand, 3.0f))
            return false;
    }
    return true;
}

inline bool BattlegroundMountStartBlockedCompat(Player* bot)
{
    BattleGround* bg = bot->GetBattleGround();
    return bg && bg->GetStatus() == STATUS_WAIT_JOIN && bg->GetStartDelayTime() > BG_START_DELAY_30S;
}
