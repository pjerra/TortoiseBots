// The engine rule for a bot that stands on another map than its master, with no game type (a pure function: forever/tools/test_engine_map.cpp).
// TbBotMgr::Think asks it before it drops the brain of an alive bot.
#pragma once

namespace TortoiseBots
{
// An ALIVE bot outside its master's map keeps its engine only inside an instance its own party stands in. There the engine follows the group's leader
// (MasterTargetCompat) and fights, and a party that entered while its commander stayed outside must not stand with its engine off after the worldport.
// Elsewhere the engine goes: its follow cannot path across maps, and the host's own catch-up teleport (open world only) is what reunites the bot with its
// master. A dead bot is not this rule's (it has its own, dead-engine one).
inline bool EngineKeptOnOtherMap(bool alive, bool inInstance, bool partyHere)
{
    return alive && inInstance && partyHere;
}

// "Its party is here" for EngineKeptOnOtherMap: the group's leader is SOMEBODY ELSE (a bot that leads its own group is not a party standing beside it:
// ObjectAccessor::FindPlayer(leaderGuid) answers the bot itself), in the world, not between maps, on the bot's map.
inline bool PartyLeaderOnMap(bool leaderIsOther, bool inWorld, bool teleporting, bool sameMap)
{
    return leaderIsOther && inWorld && !teleporting && sameMap;
}
}
