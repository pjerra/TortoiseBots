// Out-of-line parts of the R4C compat pair (TbCompat_C.h). Compiled with the module, WITHOUT the cmangos shim in front: this
// file only sees the core's own headers, so the core's logging macros and the movement generator headers are usable here.
#include "ChaseMovementGenerator.h"
#include "FollowMovementGenerator.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectGuid.h"
#include "Unit.h"

#include <atomic>

// TbCompat_C.h: GuidFromRawCompat. Only 0 (no guid) has a 128-bit form; a legacy 64-bit number is refused once, loudly.
ObjectGuid GuidFromRawCompat(uint64 raw)
{
    if (raw)
    {
        static std::atomic<bool> reported{ false };
        if (!reported.exchange(true))
            TC_LOG_ERROR("server.loading", "TortoiseBots: a legacy 64-bit guid ({}) has no ForeverCore form, treated as no guid", raw);
    }
    return ObjectGuid::Empty;
}

// TbCompat_C.h: TargetedMovementTargetCompat. The unit a chase or follow generator is going after.
Unit* TargetedMovementTargetCompat(Unit* unit)
{
    if (!unit || !unit->GetMotionMaster())
        return nullptr;

    MovementGenerator const* generator = unit->GetMotionMaster()->GetCurrentMovementGenerator();
    if (!generator)
        return nullptr;

    switch (generator->GetMovementGeneratorType())
    {
        case CHASE_MOTION_TYPE:
            return static_cast<ChaseMovementGenerator const*>(generator)->GetTarget();
        case FOLLOW_MOTION_TYPE:
            return static_cast<FollowMovementGenerator const*>(generator)->GetTarget();
        default:
            return nullptr;
    }
}
