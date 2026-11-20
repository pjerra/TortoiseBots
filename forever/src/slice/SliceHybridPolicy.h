// Slice 10 (H10): which classes may be given the tank role, and which of them may lead a dungeon run. Pure (no core type), so
// forever/tools/test_slice10_roles.cpp runs it standalone; SliceAiFactory.cpp static_asserts the class ids against the core's.
//
// The role bit alone is not a capability: `PlayerbotAI::IsTank` answers from the forced role first (PlayerbotAI.cpp:4337-4341), so a
// tank order on a class the slice gives no tank kit (a shaman: restoration only, D2) used to make `IsTank` true while the bot ran the
// healing kit. The run's start check reads `IsTank` only (run::StartCheck), so it admitted that shaman as the party tank.
#pragma once

namespace TortoiseBots
{
namespace slice
{
namespace hybrid
{
// The core's class ids for the classes this slice sends to a kit (SharedDefines.h: CLASS_WARRIOR 1, CLASS_PALADIN 2, CLASS_SHAMAN 7,
// CLASS_DRUID 11). Numbers here keep the header core-free; SliceAiFactory.cpp static_asserts them.
enum ClassId : unsigned
{
    ClassWarrior = 1,
    ClassPaladin = 2,
    ClassShaman = 7,
    ClassDruid = 11,
};

// A tank order is honoured by a tank kit: the warrior's protection kit, the bear (a druid ordered to tank) and the protection paladin
// (recorded only: it may hold the order and play its kit, it just may not lead a run).
inline bool CanTakeTankOrder(unsigned classId)
{
    return classId == ClassWarrior || classId == ClassDruid || classId == ClassPaladin;
}

// A tank role is a tank only where the class has a tank kit. `PlayerbotAI::IsTank` answers from the forced role bit first
// (PlayerbotAI.cpp:4337-4341), so a bit left on a class with no tank kit (a restoration shaman: D2) would otherwise make every run
// read of that member - its tank/damage split, its threat victim, its pull-hold anchor - answer "tank". The run facts, the party orders
// and the pull orders ask this, never the bit alone.
inline bool CountsAsTank(unsigned classId, bool roleSaysTank)
{
    return roleSaysTank && CanTakeTankOrder(classId);
}

// What `.tb role ... tank` must do for a class with no tank kit: refuse, and clear a tank bit already on the bot. `storedTank` and
// `aiTank` are the two reads PlayerbotAI::IsTank makes before its kit (the player-wide stored role, then the bot's own forced role), so
// a bit left by an older build, `.bot role` or the dungeon finder cannot survive the refusal.
struct TankOrderStep
{
    bool refuse;
    bool clearStored;
    bool clearAi;
};

inline TankOrderStep PlanTankOrder(unsigned classId, bool tankOrder, bool storedTank, bool aiTank)
{
    if (!tankOrder || CanTakeTankOrder(classId))
        return TankOrderStep{ false, false, false };
    return TankOrderStep{ true, storedTank, aiTank };
}

// A run's lead tank, live test decision D1: the warrior tank and the bear druid. The protection paladin is recorded only (no taunt at
// 18, spec row i) and every other class has no tank kit at all.
inline bool CanLeadRun(unsigned classId)
{
    return classId == ClassWarrior || classId == ClassDruid;
}
}
}
}
