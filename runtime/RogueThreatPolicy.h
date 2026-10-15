#pragma once

// Rogue threat drops in 5-man dungeons (AiPlayerbot.RogueDungeonThreatDrops,
// default 1). Pure decision rules with no core types: the triggers in
// ai/playerbot/strategy/rogue/RogueTriggers.{h,cpp} translate game state into these
// plain inputs, so the gating is unit-tested on its own
// (tools/test_rogue_threat_policy.cpp).
//
// The threat levels themselves are the module's existing "medium threat" (relative
// threat on the current target >= 60) and "high threat" (>= 80, with the five-second
// look-ahead guard in HighThreatTrigger) triggers - the same ones the raid rows use -
// so this adds no new threat scan. mod-playerbots (79bd428) acts on its own medium
// threat with Vanish in every PvE rogue strategy; here Feint comes first at medium
// and Vanish stays the last resort at high, and only where a living tank can take
// the mob over.
//
// Vanish never fires when it would end a solo fight or leave the group without a
// tank in the fight: it needs a group, a main tank other than the rogue that is alive
// and in combat, and a mob that is on that tank or on the rogue.
//
// The tank must also actually hold the mob: ThreatValue reads 100 for an in-combat
// mob the tank has no threat on (a loose add), and the high-threat look-ahead never
// holds against a tank threat of 0. Without the tankHoldsMob gate a rogue with a
// loose add would Feint and then Vanish for nothing, burning the five-minute cooldown.

namespace TortoiseBots
{

struct RogueThreatInputs
{
    bool toggleOn = false;            // AiPlayerbot.RogueDungeonThreatDrops
    bool inFiveManDungeon = false;    // dungeon map, not a raid (raids have their own rows)
    bool grouped = false;
    bool otherTankInFight = false;    // main tank exists, is not the rogue, is alive and in combat
    bool tankHoldsMob = false;        // the tank has threat on the current target, or is its victim
    bool mediumThreat = false;        // existing "medium threat" trigger
    bool highThreat = false;          // existing "high threat" trigger
    bool victimIsTankOrRogue = false; // the current target is held by that tank or by the rogue
};

inline bool RogueThreatContextOk(RogueThreatInputs const& in)
{
    return in.toggleOn && in.inFiveManDungeon && in.grouped && in.otherTankInFight && in.tankHoldsMob;
}

// Feint (threat reduction, no cost to the fight) as soon as the threat is medium.
inline bool ShouldRogueDungeonFeint(RogueThreatInputs const& in)
{
    return RogueThreatContextOk(in) && in.mediumThreat;
}

// Vanish only at critical threat, and only where it hands the mob back to the tank.
// The vanish row queues below the Feint row, so a ready Feint is always tried first.
inline bool ShouldRogueDungeonVanish(RogueThreatInputs const& in)
{
    return RogueThreatContextOk(in) && in.highThreat && in.victimIsTankOrRogue;
}

// Every Feint row (dungeon, raid, low-health emergency) goes through the Feint action,
// which asks this. Feint only sheds threat onto someone else, so it needs a living
// target to take the mob: a main tank other than the rogue, or a living party pet.
// With neither it only costs energy and a GCD (issue #915: a Feint 0.07 s after the
// tank died).
inline bool CanRogueFeintShedThreat(bool grouped, bool livingOtherTank, bool livingPartyPet)
{
    return grouped && (livingOtherTank || livingPartyPet);
}

} // namespace TortoiseBots
