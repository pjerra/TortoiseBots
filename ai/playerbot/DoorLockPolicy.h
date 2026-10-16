#pragma once

// Whether a bot opens a locked door with a plain use (CMSG_GAMEOBJ_USE, a
// player's right-click), as the core lets a player do: GameObject::PlayerCanUse
// (tortoise-wow src/game/Objects/GameObject.cpp:2247-2273) checks only a key
// item for a door, and GameObject::Use opens it (UseDoorOrButton, :1453).
//
// Uldaman's Temple Doors 124368 and 141869 carry lock 86 (Lock.dbc: a skill
// entry of lock type 10, quick open, no skill): the bot's own door path looked
// for a spell of its own that opens the lock, found none and refused ("could
// not use it"), while the door opens on a plain use (2026-10-16 live run: the
// agent's use_object opened both).
//
// A plain use is an allowlist: every slot of the lock is empty, or a skill
// entry of a lock type a player opens with no tool, skill or item: OPEN (5),
// QUICK_OPEN (10, lock 86) or SLOW_OPEN (17). Anything else is refused: a key
// (the key path opens it), a profession skill (lockpicking, herbalism, mining,
// fishing, survival), and the lock types the core's plain use would let
// through though a player cannot open them so (the server checks only a key
// item and GO_FLAG_NO_INTERACT: SpellHandler.cpp:311-318, GameObject.cpp:
// 2247-2268): CALCIFIED_ELVEN_GEMS (7), DISARM_TRAP (4), ARM_TRAP (9),
// OPEN_TINKERING (12), OPEN_KNEELING (13, not confirmed to need nothing),
// OPEN_ATTACKING (14), GAHZRIDIAN (15), BLASTING (16) and the rest.
//
// Standalone (no core types): types[i] is the lock's Type[i] (0 none, 1 key
// item, 2 skill), index[i] its Index[i] (the item, or the lock type).

#include <cstdint>

namespace TortoiseBots
{
namespace DoorLock
{

constexpr std::uint32_t kLockKeyNone = 0;
constexpr std::uint32_t kLockKeyItem = 1;
constexpr std::uint32_t kLockKeySkill = 2;
constexpr int kLockSlots = 8;

constexpr std::uint32_t kLockTypeOpen = 5;
constexpr std::uint32_t kLockTypeQuickOpen = 10;
constexpr std::uint32_t kLockTypeSlowOpen = 17;

inline bool PlainUseLockType(std::uint32_t lockType)
{
    return lockType == kLockTypeOpen || lockType == kLockTypeQuickOpen || lockType == kLockTypeSlowOpen;
}

inline bool OpensOnPlainUse(const std::uint32_t (&types)[kLockSlots], const std::uint32_t (&index)[kLockSlots])
{
    for (int i = 0; i < kLockSlots; ++i)
    {
        if (types[i] == kLockKeyNone)
            continue;
        if (types[i] == kLockKeySkill && PlainUseLockType(index[i]))
            continue;
        return false; // a key, a skill or tool lock, or a kind of lock this rule does not know
    }
    return true;
}

} // namespace DoorLock
} // namespace TortoiseBots
