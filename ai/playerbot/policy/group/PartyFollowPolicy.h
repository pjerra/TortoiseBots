#pragma once

// Whether a bot may answer for its own party: the "following party" value the
// quest travel gates AND into every member's {can accept/turn in quest npc}
// check (TravelMgr.cpp, QuestRelationTravelDestination), and the group sell
// rows read. A bot nobody leads decides for itself. TB's pool bots answered
// through the wander/follow strategy their factory always gave them; the free
// bot kit of the Forever slice carries neither, so the real distinction is
// mastery, not movement. Kept pure (no core includes) so the truth table has a
// standalone test.

namespace ai
{
    inline bool FollowingPartyCounts(bool hasRealPlayerMaster, bool isGroupMaster, bool followsOrWanders)
    {
        return !hasRealPlayerMaster || isGroupMaster || followsOrWanders;
    }
}
