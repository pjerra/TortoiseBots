// Issue #139: the opt-in player-led run, the world-facing half: see TbRunPlayerLed.h and forever/docs/RUN-KNOBS-139-NOTES.md. Built with TORTOISEBOTS_SLICE1 only.
//
// Every read is the core's public API on the world thread, plain data out: the anchor and the engaged list are what the Turtle glue reads
// (commands/Commander.cpp ReadPlayerLedAnchor / ReadPlayerLedEngaged / the pick's path check), spelled for ForeverCore.
#include "host/run/TbRunPlayerLed.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <unordered_set>

#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "PathGenerator.h"
#include "Unit.h"

#include "compat/TbCompat_Unit.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

namespace TortoiseBots
{
namespace run
{
namespace
{
bool Usable(Player* player)
{
    return player && player->IsInWorld() && !player->IsBeingTeleported() && player->FindMap();
}

// The bots of the run that can be read: the tank and every bot the run owns, in the world.
std::vector<Player*> RunBots(RunRec const& rec)
{
    std::vector<Player*> bots;
    if (Player* tank = ObjectAccessor::FindPlayer(rec.leader); Usable(tank))
        bots.push_back(tank);
    for (auto const& entry : rec.bound)
        if (Player* bot = ObjectAccessor::FindPlayer(entry.first); Usable(bot) && bot->GetGUID() != rec.leader)
            bots.push_back(bot);
    return bots;
}

// The leading player as the run sees him once a tick. Read only: nothing is ever ordered to him. `moving`: he moved since the last read
// (PlayerLed::MovedSince) or his client says so. The bots' farthest distance is from him (the gather's measure in a player-led run).
PlayerLed::Anchor ReadAnchor(Player* commander, std::vector<Player*> const& bots, uint32 planMap, PlayerLed::State& pl)
{
    PlayerLed::Anchor a;
    if (!Usable(commander))
    {
        pl.lastKnown = false;
        return a;
    }
    a.read = true;
    a.x = commander->GetPositionX();
    a.y = commander->GetPositionY();
    a.z = commander->GetPositionZ();
    a.onMap = commander->GetMapId() == planMap;
    a.alive = commander->IsAlive();
    a.ghost = commander->HasPlayerFlag(PLAYER_FLAGS_GHOST);
    bool const moved = PlayerLed::MovedSince(pl, a.x, a.y, a.z);
    a.moving = moved || IsMovingCompat(commander);
    a.partyInCombat = a.alive && commander->IsInCombat();
    for (Player* bot : bots)
    {
        if (!bot->IsAlive())
            continue;
        Pet* const pet = bot->GetPet();
        a.partyInCombat = a.partyInCombat || bot->IsInCombat() || (pet && pet->IsAlive() && pet->IsInWorld() && pet->IsInCombat());
        float const yd = bot->FindMap() == commander->FindMap() ? bot->GetDistance(commander) : 1000.0f;
        if (yd > a.botsMaxYd)
        {
            a.botsMaxYd = yd;
            a.farthestBot = bot->GetName();     // the straggler ladder's summon (PlayerLed::Tick)
        }
    }
    return a;
}

// The creatures in combat with the party (on the player, a bot or a bot's pet), each once, for the step pick's rule (a) (PlayerLed::EngagedStep). Their
// attacker lists only: no grid search.
std::vector<PlayerLed::Engaged> ReadEngaged(Player* commander, std::vector<Player*> const& bots)
{
    std::vector<PlayerLed::Engaged> out;
    std::unordered_set<ObjectGuid> seen;
    auto const add = [&](Unit* victim)
    {
        if (!victim || !victim->IsInWorld())
            return;
        for (Unit* attacker : victim->getAttackers())
        {
            if (!attacker || !attacker->IsAlive() || !attacker->IsCreature() || attacker->FindMap() != victim->FindMap() ||
                sServerFacade.IsFriendlyTo(attacker, victim) || !seen.insert(attacker->GetGUID()).second)
                continue;
            PlayerLed::Engaged e;
            e.entry = attacker->GetEntry();
            e.x = attacker->GetPositionX();
            e.y = attacker->GetPositionY();
            e.z = attacker->GetPositionZ();
            out.push_back(e);
        }
    };
    if (Usable(commander) && commander->IsAlive())
        add(commander);
    for (Player* bot : bots)
    {
        if (!bot->IsAlive())
            continue;
        add(bot);
        add(bot->GetPet());
    }
    return out;
}
}

bool PlayerLedKeyOn()
{
    return sPlayerbotAIConfig.dungeonRunPlayerLed;
}

void PlayerLedConfigure(PlayerLed::State& pl)
{
    pl.pullYd = sPlayerbotAIConfig.dungeonRunPlayerLedPullYd;
    pl.awayMinutes = sPlayerbotAIConfig.dungeonRunPlayerLedAwayMinutes;
}

PlayerLed::Anchor PlayerLedPick(RunRec& rec, Player* tank)
{
    PlayerLedConfigure(rec.pl);
    ai::DungeonPlan const* const plan = rec.run.plan;
    Player* const commander = ObjectAccessor::FindPlayer(rec.commander);
    std::vector<Player*> const bots = RunBots(rec);
    PlayerLed::Anchor anchor = ReadAnchor(commander, bots, plan ? plan->map : 0, rec.pl);
    anchor.team = tank->GetTeam() == ALLIANCE ? ai::kPlanFlagAlliance : ai::kPlanFlagHorde;
    // The pick's path check (PlayerLed::PathShortEnough): the tank's navmesh way to the candidate step's spot, asked for at most PlayerLed::kPathChecksPerTick
    // candidates a tick, never for one turned down lately. No way, or one that stops more than 5 yd short of the spot, is "no path".
    DungeonRun::Run const& run = rec.run;
    PlayerLed::PathCheck const pathOk = [tank, &run](std::size_t step)
    {
        if (!tank->IsAlive() || !tank->IsInWorld())
            return true;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        PlayerLed::StepSpot(run, step, x, y, z);
        PathFinder path(tank);
        PathCalculateCompat(path, x, y, z, false);
        uint32 const type = PathTypeCompat(path);
        G3D::Vector3 const end = PathActualEndCompat(path);
        bool const noPath = (type & PATHFIND_NOPATH) || ((type & PATHFIND_INCOMPLETE) && ai::PlanDistance(end.x, end.y, end.z, x, y, z) > 5.0f);
        float const straightYd = ai::PlanDistance(tank->GetPositionX(), tank->GetPositionY(), tank->GetPositionZ(), x, y, z);
        return PlayerLed::PathShortEnough(noPath, PathLengthCompat(path), straightYd);
    };
    PlayerLed::Pick(rec.run, rec.pl, anchor, ReadEngaged(commander, bots), pathOk);
    return anchor;
}
}
}

#else

namespace TortoiseBots
{
namespace run
{
// Option OFF: the player-led glue is slice-only code (its callers are too); this object is empty.
}
}

#endif
