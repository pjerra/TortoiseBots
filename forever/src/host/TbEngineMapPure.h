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

// "Its party is here" for EngineKeptOnOtherMap, asked of ONE group member: a party stands beside the bot when a member that is ANOTHER character is in
// the world, not between maps, on the bot's exact map (the same Map object, so the same instance of it). The bot's own member row (a bot can lead its own
// group: ObjectAccessor::FindPlayer answers the bot itself) never counts, so a lone leader has no party; who leads the group says nothing about it, and a
// bot that leads its own group keeps its engine beside its members.
inline bool PartyMemberOnMap(bool isTheBot, bool inWorld, bool teleporting, bool sameMap)
{
    return !isTheBot && inWorld && !teleporting && sameMap;
}
}
