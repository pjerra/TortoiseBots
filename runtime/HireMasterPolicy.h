#pragma once

// Issue #1014: a hired companion keeps its hirer as its master. A cross-map
// teleport takes the hirer out of the world for a moment; the bot AI drops the
// raw master pointer then (FindPlayer only sees in-world players), and the
// group-master re-adoption in PlayerbotAI::DoNextAction used to hand the hire
// to the bot group leader. In a commander party the hirer is outside the group,
// so the hire never got its hirer back. Pure rules, unit-tested on their own
// (tools/test_hire_master_policy.cpp); bots that are not hires are untouched.
//
// Facts:
//  - hired: the bot is in the active-hire registry (HireLifecycle::IsHired).
//  - hirerBetweenMaps: the hirer's Player still exists (the not-in-world
//    lookup finds it) but is not in the world: a map change, not a logout.
//  - candidateIsRealPlayer: the re-adoption's pick is a human, not a bot.
//  - boundToHirer: the durable binding (BotManager record and the AI's master
//    guid) still names the hirer.
//  - masterPointerSet: the AI holds a live master pointer.
//  - hirerInWorld: the hirer is back in the world.

namespace TortoiseBots
{

// May the group-master re-adoption change this bot's master to the candidate?
inline bool HireAllowsMasterAdoption(bool hired, bool hirerBetweenMaps, bool candidateIsRealPlayer)
{
    if (!hired)
        return true;
    if (hirerBetweenMaps)
        return false;
    return candidateIsRealPlayer;
}

// Does a hire whose hirer is back in the world take that hirer as its live
// master again (the binding never changed, only the pointer was dropped)?
inline bool HireRestoresHirer(bool hired, bool boundToHirer, bool masterPointerSet, bool hirerInWorld)
{
    return hired && boundToHirer && !masterPointerSet && hirerInWorld;
}

} // namespace TortoiseBots
