#pragma once
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

// ForeverCore mount model: there is no collection_mount table and no sMountMgr. A mount item teaches its
// mount spell when used (1132: 55884 learns 580). The mount spell holds only the mounted aura and a mount
// type; the speed comes from that type's capability, picked by SKILL_RIDING (Unit::GetMountCapability,
// Unit.cpp:8892).
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

// ForeverCore mount spells carry only the mounted aura and a mount type (e.g. 580: aura 78, misc 358, no speed
// aura); the core picks the speed from the type's capabilities by SKILL_RIDING (Unit.cpp:8892-8913). Classic
// capabilities: riding 75 -> +60 %, 150 -> +100 %. Values are base-1 like the mirrored aura amounts.
inline uint32 RiderMountSpeedCompat(Player* bot)
{
    uint32 const riding = bot ? bot->GetSkillValue(SKILL_RIDING) : 0;
    return riding >= 150 ? 99 : riding >= 75 ? 59 : 0;
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

// A mount spell with its own speed aura keeps it; the rest scale with SKILL_RIDING (RiderMountSpeedCompat).
inline bool MountSpeedIgnoresRidingSkillCompat(SpellEntry const* spellInfo)
{
    if (!spellInfo)
        return false;
    for (int i = 0; i < 3; ++i)
        if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
            return true;
    return false;
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
