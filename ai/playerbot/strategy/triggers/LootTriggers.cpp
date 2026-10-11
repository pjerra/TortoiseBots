
#include "playerbot/playerbot.h"
#include "LootTriggers.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
using namespace ai;

namespace
{
    // Any live hostile in sight range (walls ignored), the mod-playerbots "all targets" test.
    // Reads the "possible targets" list that AttackersValue::AddTargetsOf fills for this bot
    // with the same qualifier (sight range, unvalidated), so it is normally the cached list;
    // at worst it refreshes once a second, and only while loot waits for a target.
    // Neutral mobs (they do not aggro) and unselectable trigger NPCs do not count.
    bool HasHostileInSight(PlayerbotAI* ai, Player* bot)
    {
        const std::string qualifier = Qualified::MultiQualify(
            { std::to_string((int32)sPlayerbotAIConfig.sightDistance), std::to_string(true) }, ":");
        const std::list<ObjectGuid> units =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible targets", qualifier)->Get();
        for (const ObjectGuid& guid : units)
        {
            Unit* unit = ai->GetUnit(guid);
            if (!unit || !unit->IsAlive() || !bot->IsHostileTo(unit))
                continue;
            if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NOT_ATTACKABLE_1))
                continue;
            return true;
        }
        return false;
    }
}

bool LootAvailableTrigger::IsActive()
{
    // The loot chain only exists in the non-combat engine, and that engine's state comes from the
    // "combat start"/"combat end" reaction (has attackers) - not from UNIT_FLAG_IN_COMBAT, which
    // lingers for a moment after every kill. Testing the flag here suppressed "loot" (6.0) for
    // exactly the window in which the bot must loot before picking its next target, so
    // "attack anything" (5.0, GrindingStrategy) ordered the next pull instead: live measurement
    // showed that a kill followed by an attack order within 2s produced loot on 7% of corpses,
    // against 43% when the bot stayed quiet for 5-10s. The corpse then expired unopened.
    // "combat" also counted any group member in combat within reactDistance (150y), so a bot
    // party stopped looting while anyone nearby fought.
    //
    // bot->GetAttackers() replaces it with the core's own "attacking me right now" set, which has
    // neither of those two defects: it is filled in Unit::Attack (the moment an add aggros, before
    // its first hit) and drained in CombatStop/AttackStop (the moment it dies or evades). That
    // closes the engine's blind spot - "has attackers" is a 2s-cached list, so a corpse could make
    // the bot kneel for lootDelay next to an add that already had it as its victim - while a kill
    // still loots at once (the dead mob is gone from the set) and a corpse held up by a real fight
    // waits in the stack for LOOT_OBJECT_TTL_SECONDS instead of being lost.
    //
    // A mounted bot selects loot too (mod-playerbots LootTriggers.cpp has no mount
    // gate); "open loot" dismounts it at the corpse or node. The old "mounted" veto
    // deadlocked travel: "move to travel target" stands down while the stack holds
    // possible loot within lootDistance, so a bot that rode past a herb/ore node or a
    // chest stood mounted next to it, and "add gathering loot" kept refreshing the
    // node so it never aged out. Live pool 2026-10-11: 11 of 108 standstills, 3-12 min
    // each, ended only by a buff cast that happened to dismount or a long-stuck rescue.
    // Taxi flight keeps the veto: the bot cannot leave the flight path.
    //
    // Only for masterless bots: an owned/hired bot following a mounted player keeps the
    // old mounted veto, or it would dismount at every node and chest near the road,
    // get pulled away by "follow" and have to remount again and again.
    if (bot->IsTaxiFlying() || !bot->GetAttackers().empty())
        return false;

    if (ai->HasRealPlayerMaster() && AI_VALUE2(bool, "mounted", "self target"))
        return false;

    if (!AI_VALUE(bool, "has available loot"))
        return false;

    // In a dungeon or raid, pick a corpse only when no hostile is in sight, or when one lies
    // within reach (mod-playerbots LootAvailableTrigger). Otherwise a bot walked to a corpse
    // that fell beside an unpulled pack or a patrol path and pulled it. The open world keeps
    // the ungated rule below: there a sight-range gate would stop looting in busy zones.
    if (bot->GetMap()->IsDungeon() && HasHostileInSight(ai, bot) &&
        AI_VALUE(LootObjectStack*, "available loot")->GetLoot(INTERACTION_DISTANCE - 2.0f).IsEmpty())
        return false;

    // Loot every corpse this bot (or its group) killed and may loot before
    // pulling the next mob, like a player. Fire the selection action ("loot")
    // when no corpse is picked yet, or the picked one can no longer be looted
    // or reached (looted by someone else, despawned, expired, or abandoned
    // after a failed approach): "loot" then picks the nearest corpse still on
    // the stack, so the chain repairs itself instead of stalling.
    //
    // The old "already within INTERACTION_DISTANCE, or no hostile targets
    // around" gate starved the chain in mob-dense grind zones: "attack
    // anything" (GrindingStrategy, 5.0) grabbed the next mob and the bot walked
    // off, and a selected corpse that went stale was never replaced, so the
    // "far from current loot" -> "move to loot" path (7.0) could not run.
    // While a valid target is selected, those two actions walk to and open it,
    // so re-selecting the target every tick is neither needed nor desirable.
    LootObject lootTarget = AI_VALUE(LootObject, "loot target");
    return lootTarget.IsEmpty() || !lootTarget.IsLootPossible(bot);
}

bool FarFromCurrentLootTrigger::IsActive()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");

    if (!loot.IsLootPossible(bot))
        return false;

    // Abandon loot the bot cannot safely reach without breaking its follow leash.
    // "move to loot" runs at priority 7 but "out of free move range" fires "follow" at
    // ACTION_HIGH (20). A corpse is only reachable without oscillation if the master is
    // within followDistance + GetMaxLootDistance of it — otherwise the bot must leave the
    // leash to reach the corpse and follow immediately wins, causing a yo-yo. Same predicate
    // the loot stack uses to decide whether the corpse counts as available loot at all.
    if (!AI_VALUE(LootObjectStack*, "available loot")->IsWithinMasterLootRange(loot))
        return false;

    // Must agree with OpenLootAction::DoLoot's loot-range rule, or the bot deadlocks: while the
    // 2D "distance" value here read "close enough", "open loot" (8.0) outranked "move to loot"
    // (7.0) and failed the server's 3D range check every tick, so on sloped ground the bot sat
    // a few yards off a corpse it could not loot and never approached it.
    return !loot.IsInLootRange(bot);
}

bool CanLootTrigger::IsActive()
{
    return AI_VALUE(bool, "can loot");
}
