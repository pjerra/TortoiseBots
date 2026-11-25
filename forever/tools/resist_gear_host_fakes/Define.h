// The core's integer aliases and the slot/result constants the resistance-gear host code names, as the standalone
// host check (forever/tools/resist_gear_host_check.cpp) needs them.
#pragma once

#include <cstdint>

using uint8 = uint8_t;
using int8 = int8_t;
using uint16 = uint16_t;
using int16 = int16_t;
using uint32 = uint32_t;
using int32 = int32_t;
using uint64 = uint64_t;
using int64 = int64_t;

// Player.h: the equipment slots 0-18, the backpack rows 23-38 and the four worn bags 19-22 (bag 0 = worn equipment).
inline uint8 const INVENTORY_SLOT_BAG_0 = 255;
inline uint8 const INVENTORY_SLOT_BAG_START = 19;
inline uint8 const INVENTORY_SLOT_BAG_END = 23;
inline uint8 const INVENTORY_SLOT_ITEM_START = 23;
inline uint8 const INVENTORY_SLOT_ITEM_END = 39;
inline uint8 const EQUIPMENT_SLOT_START = 0;
inline uint8 const EQUIPMENT_SLOT_END = 19;
inline uint32 const NULL_SLOT = 255;

// Item.h: the inventory type of a ring (it fits either finger) and the core's "the move is allowed" answer.
inline uint32 const INVTYPE_FINGER = 11;
inline uint8 const EQUIP_ERR_OK = 0;
inline uint8 const EQUIP_ERR_CANT_EQUIP_LEVEL_I = 42;
