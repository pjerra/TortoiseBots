#include "playerbot/playerbot.h"
#include "TravelValues.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/TbCompatWL4.h"

// The value bodies live in the both-core pair (TbCompatWL4.h + ai/cmangos-compat-wl4.h): their Turtle answers read the
// guild-order, alt/hire and trainer services of the full build, their ForeverCore answers the reduced data this core
// has. Keeping one call here means the two answers cannot drift apart.
using namespace ai;

EntryGuidps EntryGuidpsValue::Calculate()
{
    return TravelEntryGuidpsLegacyCompat();
}

EntryTravelPurposeMap EntryTravelPurposeMapValue::Calculate()
{
    return TravelEntryPurposeMapLegacyCompat();
}

bool NeedTravelPurposeValue::Calculate()
{
    return TravelPurposeNeedCompat(ai, getQualifier(), activeNodeProbe);
}

bool ShouldTravelNamedValue::Calculate()
{
    return TravelNamedPurposeCompat(ai, bot, chat, getQualifier(), activeTrainerProbe);
}

bool ShouldLeaveOutgrownZoneValue::Calculate()
{
    return TravelLeaveOutgrownCompat(ai);
}


bool TravelTargetActiveValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "travel target")->IsActive();
};

bool TravelTargetReadyValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "leader travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_READY;
};

bool TravelTargetTravelingValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "leader travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL;
};

bool TravelTargetWorkingValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "leader travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK;
};

bool QuestStageActiveValue::Calculate()
{
    return TravelQuestStageActiveCompat(bot, getQualifier());
}
