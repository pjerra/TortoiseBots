// The module's guarded access to the core's start-outfit data: whether an item entry is one of the items the core creates a character with.
//
// The data: `PlayerInfo::item` (ObjectMgr.h:716), filled at load from the DB2 character loadouts for new characters (`CharacterLoadoutItem` by
// `CharacterLoadoutID`, `CharacterLoadout::IsForNewCharacter`, ObjectMgr.cpp:4400-4439) and then from the world-table overrides
// `playercreateinfo_item` (ObjectMgr.cpp:4441-4499); Player::Create stores each of them with `StoreNewItemInBestSlots` (Player.cpp:545-547). This
// core keys the record by race and class only (ObjectMgr::GetPlayerInfo, ObjectMgr.cpp:11044-11046, the `_playerInfo` map is {race, class}); the
// gender of a character does not change its starting items, so it is not part of the data or of this signature.
//
// The guard: GetPlayerInfo answers nullptr for a race/class the core has no row for, and the miss is answered false here, never dereferenced. The
// list is in memory (nothing is queried), so this is safe on the world thread, but it is not a per-tick call.
#pragma once

#include "ObjectMgr.h"
#include "Player.h"

#include <cstdint>

namespace TortoiseBots
{
namespace h3
{
namespace replace
{
inline bool StartOutfitItem(Player const* character, uint32 itemEntry)
{
    if (!character || !itemEntry)
        return false;
    PlayerInfo const* info = sObjectMgr->GetPlayerInfo(character->GetRace(), character->GetClass());
    if (!info)
        return false;
    for (PlayerCreateInfoItem const& initial : info->item)
        if (initial.item_id == itemEntry)
            return true;
    return false;
}
}
}
}
