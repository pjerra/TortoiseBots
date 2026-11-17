#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "OnyxiaBreathPolicy.h"

// Pure decision rules for the optional Onyxia whelp and egg tactics
// (AiPlayerbot.OnyxiaWhelpsAndEggs, on by default).
// Donor behaviour: mod-playerbots @ 79bd428, src/Ai/Raid/Ony/OnyTriggers.cpp
// (RaidOnyxiaWhelpsSpawnTrigger: boss airborne, non-healers go for whelps;
// OnyxiaAvoidEggsTrigger: bot inside the egg cave) and OnyActions.cpp
// (RaidOnyxiaKillWhelpsAction: attack a whelp unless already on one;
// OnyxiaAvoidEggsAction: walk back out of the cave).
// Adapted to Turtle 1.18.1 data (tw_world): Onyxian Whelp 11262, the
// trap game object "Onyxia Egg" 176511 (type 6, radius 10, spell 17647)
// and the whelp spawn points of core boss_onyxia.cpp. No core includes:
// callers translate game state into plain inputs so the rules stay
// testable in tools/test_onyxia_whelp_egg_policy.cpp.

namespace ai
{
    constexpr std::uint32_t kOnyxiaMapId = 249;
    constexpr std::uint32_t kOnyxianWhelpEntry = 11262;
    constexpr std::uint32_t kOnyxiaEggEntry = 176511;

    // The core drives her phase-2 flight through this Hover aura.
    constexpr std::uint32_t kOnyxiaHoverAura = 17131;

    // The boss unit of an engagement scan: the entry, alive, on the bot's map and in combat. The scan holds a victim
    // or attacker of the raid's members, so a dormant spawn or a marked whelp in the same lists is not her.
    inline bool IsOnyxiaBossUnit(std::uint32_t entry, bool alive, bool inMap, bool inCombat)
    {
        return entry == kOnyxiaEntry && alive && inMap && inCombat;
    }

    // Egg trap radius (gameobject_template data2 of 176511) plus a margin
    // so a bot walking past is never inside the trigger circle.
    constexpr float kOnyxiaEggTriggerRadius = 10.0f;
    constexpr float kOnyxiaEggMargin = 2.0f;
    constexpr float kOnyxiaEggDangerRadius = kOnyxiaEggTriggerRadius + kOnyxiaEggMargin;

    // A whelp further than this from the bot is somebody else's problem.
    constexpr float kOnyxiaWhelpEngageRange = 40.0f;

    // The egg fields lie west of the arena (spawn rows of 176511 span
    // x -115.2..-32.0, y -279.3..-151.4). Padded by the danger radius:
    // outside this box a bot cannot be inside any egg's circle, so no
    // game object search is needed there.
    constexpr float kOnyxiaEggZoneMinX = -128.0f;
    constexpr float kOnyxiaEggZoneMaxX = -20.0f;
    constexpr float kOnyxiaEggZoneMinY = -292.0f;
    constexpr float kOnyxiaEggZoneMaxY = -139.0f;

    inline bool IsInOnyxiaEggZone(float x, float y)
    {
        return x >= kOnyxiaEggZoneMinX && x <= kOnyxiaEggZoneMaxX &&
               y >= kOnyxiaEggZoneMinY && y <= kOnyxiaEggZoneMaxY;
    }

    // Distance 2D between two points (egg traps check a circle).
    inline float OnyxiaDist2d(float x1, float y1, float x2, float y2)
    {
        const float dx = x1 - x2;
        const float dy = y1 - y2;
        return std::sqrt(dx * dx + dy * dy);
    }

    inline bool IsInsideOnyxiaEggDanger(float distance2d)
    {
        return distance2d < kOnyxiaEggDangerRadius;
    }

    // Exit points on the arena side of the two egg caves (donor
    // OnyxiaAvoidEggsAction targets, Onyxia's lair map coordinates).
    struct OnyxiaEggExit { float x, y, z; };
    constexpr OnyxiaEggExit kOnyxiaEggExits[2] =
    {
        { -10.0f, -180.0f, -87.0f }, // north cave mouth
        { -16.0f, -250.0f, -88.0f }, // south cave mouth
    };

    // Index of the exit nearest to the bot.
    inline int NearestOnyxiaEggExit(float x, float y)
    {
        const float d0 = OnyxiaDist2d(x, y, kOnyxiaEggExits[0].x, kOnyxiaEggExits[0].y);
        const float d1 = OnyxiaDist2d(x, y, kOnyxiaEggExits[1].x, kOnyxiaEggExits[1].y);
        return d0 <= d1 ? 0 : 1;
    }

    // Egg retreat decision. The action never fires for a bot that is not
    // inside the danger circle of an unbroken egg.
    inline bool ShouldAvoidOnyxiaEggs(bool featureOn, bool onOnyxiaMap, bool inEggZone, bool eggInDanger)
    {
        return featureOn && onOnyxiaMap && inEggZone && eggInDanger;
    }

    // Whelp duty: the AoE-capable ranged classes. Tanks and healers
    // keep their own duty and melee cannot do much against a flying boss
    // anyway but keeps its generic target choice.
    // classId values are the 1.12 Classes enum (Hunter 3, Mage 8, Warlock 9,
    // Shaman 7, Druid 11).
    enum OnyxiaWhelpClass : std::uint8_t
    {
        kWhelpClassShaman = 7,
        kWhelpClassHunter = 3,
        kWhelpClassMage = 8,
        kWhelpClassWarlock = 9,
        kWhelpClassDruid = 11,
    };

    inline bool IsOnyxiaWhelpHunterClass(std::uint8_t classId)
    {
        switch (classId)
        {
            case kWhelpClassHunter:  // Multi-Shot
            case kWhelpClassMage:    // Arcane Explosion, Blizzard
            case kWhelpClassWarlock: // Rain of Fire, Hellfire
            case kWhelpClassShaman:  // Chain Lightning (ranged specs)
            case kWhelpClassDruid:   // Hurricane (ranged specs)
                return true;
            default:
                return false;
        }
    }

    inline bool IsOnyxiaWhelpHunter(std::uint8_t classId, bool isTank, bool isHealer, bool isRanged)
    {
        return !isTank && !isHealer && isRanged && IsOnyxiaWhelpHunterClass(classId);
    }

    // AiPlayerbot.DungeonRunAddCrew: the whelp duty once the row's switch unit is dead (state from the "add crew"
    // value). Waiting keeps today's duty; Crew takes it; NotCrew leaves it. So the whelp trigger, the kill action and
    // the whelp hold follow the crew, and the melee and tanks that were never hunters join it.
    inline bool OnyxiaWhelpDutyFor(bool todayDuty, std::uint8_t crewState)
    {
        return crewState == 1 ? true : crewState == 2 ? false : todayDuty;
    }

    // AiPlayerbot.OnyxiaWhelpCrewOnGround: the whelp phase. While she hovers it is the airborne phase as before; with
    // the key on and the boss found, it also covers her last 40 % on the ground, so the crew keeps the whelps there too
    // instead of leaving P3 (the phase that kills the raid) to the melee and tanks (walk 2g: 12-16 whelps up in P3, 21
    // at the peak, 28 P3 deaths to them). P1 stays untouched: it runs above 40 %.
    constexpr float kOnyxiaGroundCrewPct = 40.0f;
    inline bool OnyxiaWhelpPhase(bool bossAirborne, bool keyOn, bool bossFound, float bossPct)
    {
        return bossAirborne || (keyOn && bossFound && bossPct <= kOnyxiaGroundCrewPct);
    }

    // The ground half of the phase: the whelp phase is on and she stands. Both the whelp scan and the duty read it,
    // so the shape of the P3 window has one definition.
    inline bool OnyxiaGroundPhase(bool bossAirborne, bool keyOn, bool bossFound, float bossPct)
    {
        return !bossAirborne && OnyxiaWhelpPhase(bossAirborne, keyOn, bossFound, bossPct);
    }

    // The ground duty while the add_crew row waits (its switch unit still lives): today's duty would put every AoE
    // ranged damage dealer of the raid on the whelps and leave none of them on her, so on the ground only the crew's
    // own bots keep today's duty. `crewKeepsDuty` is the row's place for this bot; with no row to apply it is true
    // and today's duty stands. The air is untouched: waiting keeps today's duty there.
    inline bool OnyxiaWaitingGroundDuty(bool todayDuty, bool crewKeepsDuty)
    {
        return todayDuty && crewKeepsDuty;
    }

    // The whelp duty on the ground: the melee keep it (an airborne-only duty left the whole crew idle there), the tanks
    // follow their boss-tactic posts or the generic pick, and the other ranged stay on her (the air duty's job was to
    // keep the ranged on her because the melee could not reach a hovering boss).
    inline bool OnyxiaGroundDuty(bool duty, bool isTank, bool bossAirborne) { return duty && (bossAirborne || !isTank); }

    // The early add crew's boss duty aims a ranged damage dealer at her only while she is not its target already. An
    // attack on the current victim reports success and ends the engine tick before the rotation runs, so re-aiming it
    // every tick left the bot casting nothing (walk 2o: 48 % of the ranged-on-Onyxia frames were that one action, and
    // 2m's return only fired off her). An explicit order and a gate_hold row outrank it, and a dead, unreachable or
    // other-map boss fires nothing.
    inline bool OnyxiaBossDutyFires(bool bossDuty, bool explicitOrder, bool bossValid, bool gateHeld, bool onBoss)
    {
        return bossDuty && !explicitOrder && bossValid && !gateHeld && !onBoss;
    }

    // The early add crew's assist hold: on the boss duty the assist re-pick would aim the bot at a whelp every tick and
    // pull it off her, so it is held while she is the bot's target already and no explicit order stands. A gate_hold
    // row holding her off outranks the duty (the boss duty trigger's order), so the hold stands down there and the
    // assist may move the bot to the row's blocker. The whelp key is not a gate: the hold belongs to the add crew.
    inline bool OnyxiaBossDutyHoldsAssist(bool bossDuty, bool gateHeld, bool onBoss, bool explicitOrder)
    {
        return bossDuty && !gateHeld && onBoss && !explicitOrder;
    }

    // AiPlayerbot.OnyxiaWhelpPickTankFirst: the whelp a bot kills first while she stands on the ground. A whelp eating
    // the raid's tank ranks first (slot 1 already holds them in P3 and dies; the ground crew is what can pull them
    // off), one eating a healer second, the rest last; the distance breaks a tie inside one rank only.
    inline int OnyxiaWhelpVictimRank(bool victimTank, bool victimHealer)
    {
        return victimTank ? 0 : victimHealer ? 1 : 2;
    }

    // The better ground pick: the tank rank first, then a healer's, then the rest. Among the whelps eating a tank the
    // lowest health comes first (it dies soonest), then the fewest crew hitters (a whelp already crowded adds nothing
    // new), and the distance only breaks what is left. The switch's crowded clause is about the tank rank, so the pick
    // and that clause rank the same whelp; the other ranks keep the distance as their tie-break.
    inline bool OnyxiaWhelpPickBetter(int rankA, float healthPctA, int hittersA, float distA,
                                      int rankB, float healthPctB, int hittersB, float distB)
    {
        if (rankA != rankB)
            return rankA < rankB;
        if (rankA == 0)
        {
            if (healthPctA < healthPctB)
                return true;
            if (healthPctB < healthPctA)
                return false;
            if (hittersA != hittersB)
                return hittersA < hittersB;
        }
        return distA < distB;
    }

    // The switch's hysteresis (AiPlayerbot.OnyxiaWhelpPickTankFirst): at most one switch per this interval, so a bot
    // whose harmless whelp dies and a better one appears does not dance between two picks, and the crowded-pick count
    // the last rule reads.
    constexpr std::uint32_t kOnyxiaWhelpSwitchMs = 4000;
    constexpr int kOnyxiaWhelpSwitchHitters = 2;

    // Whether a bot already on a whelp switches to the scan's pick on the ground (AiPlayerbot.OnyxiaWhelpPickTankFirst).
    // The bot leaves only a whelp that eats neither a tank nor a healer (ranks 0 and 1 of OnyxiaWhelpVictimRank), so a
    // whelp on a tank is never abandoned, and only for one eating a tank (rank 0) or a healer (rank 1): the pick the
    // scan ranked first, distance breaking a tie inside a rank. Hysteresis: one switch per kOnyxiaWhelpSwitchMs, and a
    // whelp two crew bots already hit stays alone unless it is the tank's lowest one (that one is finished first).
    // `msSinceLastSwitch` is capped by the caller for a bot that never switched; a negative candidate rank (the scan
    // did not rank: airborne, key off) switches nothing.
    inline bool OnyxiaWhelpSwitch(int currentRank, int candidateRank, std::uint32_t msSinceLastSwitch,
                                  int hittersOnCandidate, bool candidateLowestHp)
    {
        if (currentRank < 2)  // never away from a whelp eating a tank or a healer
            return false;
        if (candidateRank < 0 || candidateRank > 1)  // only a whelp on a tank, then a healer, is worth a switch
            return false;
        if (msSinceLastSwitch < kOnyxiaWhelpSwitchMs)
            return false;
        if (hittersOnCandidate >= kOnyxiaWhelpSwitchHitters && !(candidateRank == 0 && candidateLowestHp))
            return false;
        return true;
    }

    // The whelp hunt for a bot already on a whelp (the trigger's second path) and the kill action's switch read this
    // one rule: with AiPlayerbot.OnyxiaWhelpPickTankFirst on, the whelp phase running and no explicit order, the hunt
    // fires (so the action runs at all) exactly when OnyxiaWhelpSwitch lets the bot leave. The trigger's own
    // already-on-a-whelp gate kept the action from ever running for such a bot, so the switch was unreachable; with
    // the key off, the phase off or a whelp that cannot be left this answers false and the trigger is today's.
    inline bool ShouldSwitchOnyxiaWhelp(bool whelpPhase, bool keyOn, bool explicitOrder, int currentRank,
                                        int candidateRank, std::uint32_t msSinceLastSwitch, int hittersOnCandidate,
                                        bool candidateLowestHp)
    {
        return whelpPhase && keyOn && !explicitOrder &&
            OnyxiaWhelpSwitch(currentRank, candidateRank, msSinceLastSwitch, hittersOnCandidate,
                candidateLowestHp);
    }

    // A ranged damage dealer off the crew on a whelp goes back to her (not on an explicit order) while the whelp phase
    // runs: the crew has the whelps, so a bot past it that stays would leave her to the melee (who cannot reach her in
    // the air; on the ground the crew's melee is on the whelps too).
    inline bool ShouldReturnToOnyxia(bool keyOn, std::uint8_t crewState, bool rangedDps, bool whelpPhase,
                                     bool onWhelp, bool explicitOrder, bool bossValid)
    {
        return keyOn && crewState == 2 && rangedDps && whelpPhase && onWhelp && !explicitOrder && bossValid;
    }

    struct OnyxiaPoint { float x, y; };

    // True when (x, y) lies inside the trap circle (10yd, no margin) of any
    // listed egg. The egg list is the bot's own cached egg search, never the
    // zone box. The whelp spawn points sit 11.5 and 11.9 yd from the nearest
    // egg (tw_world rows of 176511), so a margin here would hide them.
    inline bool IsNearAnyOnyxiaEgg(float x, float y, OnyxiaPoint const* eggs, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (OnyxiaDist2d(x, y, eggs[i].x, eggs[i].y) < kOnyxiaEggTriggerRadius)
                return true;
        }
        return false;
    }

    // Whether a whelp is a legal pick: alive, in range, and not standing
    // within the trap circle of an unbroken egg (chasing it would walk the
    // bot over the eggs). Whelps at the cave mouths, in the arena and at the
    // phase landing spots are 11+ yd from any egg and stay pickable.
    inline bool IsOnyxiaWhelpPickable(bool alive, float distance, bool whelpNearEgg)
    {
        return alive && distance <= kOnyxiaWhelpEngageRange && !whelpNearEgg;
    }

    // Whelp hunt decision: the whelp phase (airborne, or the ground crew's last 40 % with the key on), feature on, the
    // bot is on whelp duty, no whelp is its target yet, and a whelp is available. An explicit order from the master
    // always wins.
    inline bool ShouldHuntOnyxiaWhelps(bool featureOn, bool onOnyxiaMap, bool whelpPhase, bool assigned,
                                       bool hasExplicitOrder, bool alreadyOnWhelp, bool whelpAvailable)
    {
        return featureOn && onOnyxiaMap && whelpPhase && assigned && !hasExplicitOrder &&
               !alreadyOnWhelp && whelpAvailable;
    }

    // Keep the whelp target against the generic assist re-pick: true when
    // the bot is on whelp duty during the airborne phase and its target is
    // a live whelp.
    inline bool ShouldHoldOnyxiaWhelpTarget(bool featureOn, bool onOnyxiaMap, bool bossAirborne, bool assigned,
                                            bool hasExplicitOrder, bool targetIsLiveWhelp)
    {
        return featureOn && onOnyxiaMap && bossAirborne && assigned && !hasExplicitOrder && targetIsLiveWhelp;
    }
}
