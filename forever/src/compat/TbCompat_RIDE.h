#pragma once
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

// ForeverCore mount model: there is no collection_mount table and no sMountMgr. A mount item carries
// its own mount spell, and the speed of a mount is the amount of its SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED
// aura (Unit::UpdateSpeed, Unit.cpp:9312), not a rider-skill ladder. Riding skill gates the retail mount
// capability (Unit::GetMountCapability, Unit.cpp:8892), not the classic mount aura.
inline std::optional<uint32> MountSpellForCollectionItemCompat(uint32)
{
    return std::nullopt;
}

inline std::vector<std::pair<uint32, uint32>> LoadCollectionMountRowsCompat()
{
    return {};
}

inline std::unordered_set<uint32> LoadLootMountItemsCompat()
{
    return {};
}

// No rider ladder on this core: MountValue::GetSpeedFor reads the mount aura itself.
inline uint32 RiderMountSpeedCompat(Player*)
{
    return 0;
}

// One riding skill on this core (SharedDefines.h:5966); the per-race skill lines are Turtle data.
inline uint32 RidingSkillForRaceCompat(uint8)
{
    return SKILL_RIDING;
}

inline bool MountSpeedAlways100Compat(SpellEntry const*)
{
    return false;
}

// Every mount aura on ForeverCore keeps its own amount; none scales with SKILL_RIDING.
inline bool MountSpeedIgnoresRidingSkillCompat(SpellEntry const*)
{
    return true;
}

// Classic indoor dismount rule (Player::IsMountRestrictedHere, Player.cpp:7095) plus the battleground
// WMO check it calls (Battleground::IsMountRefused, Battleground.cpp:611).
inline bool MapAllowsMountsCompat(Player* bot)
{
    return !bot->IsMountRestrictedHere(false);
}

// The unit's own outdoor flag, updated from the vmaps in ProcessPositionDataChanged
// (Object.cpp:359, TerrainMgr.cpp:634-656).
inline bool IsOutdoorsCompat(Player* bot)
{
    return bot->IsOutdoors();
}

// ForeverCore has no BattleGroundWS class and the Turtle flag-stand guids are not its objects.
// Refusing the battleground branch makes the bot do less, never more.
inline bool CanMountInBattlegroundCompat(Player*)
{
    return false;
}

inline bool BattlegroundMountStartBlockedCompat(Player* bot)
{
    Battleground* bg = bot->GetBattleground();
    return bg && bg->GetStatus() == STATUS_WAIT_JOIN && bg->GetStartDelayTime() > BG_START_DELAY_30S;
}
