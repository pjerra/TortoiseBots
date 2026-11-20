// Turtle side of the both-core helpers for W6a (item weights, stat policy, and auto-equip upgrades).
// Same names and signatures as forever/src/compat/TbCompat_W6.h; each is the original Turtle expression.
#pragma once

// weights

// equip
inline void AutoEquipCompat(Player* bot, uint8 bagIndex, uint8 slot)
{
    WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
    packet << bagIndex << slot;
    bot->GetSession()->HandleAutoEquipItemOpcode(packet);
}

inline InventoryResult CanUseItemForEquipCompat(Player const* bot, ItemPrototype const* proto)
{
    return bot->CanUseItem(proto);
}

inline bool MayAutoEquipUpgradesCompat(Player* /*bot*/)
{
    return true;
}
