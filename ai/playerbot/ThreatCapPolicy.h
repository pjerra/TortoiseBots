#pragma once

// The threat reference a damage dealer's threat percentage is measured against (ThreatValue::GetTankThreat): the
// highest tank's threat. On a boss whose tactic data asks for a successor (ai_boss_tactic kind `threat_successor`,
// Hakkar) the second tank has to stay second so the boss turns to it on a threat reset: there the reference is the
// highest threat among the assigned off-tanks on the target, as soon as that is above a tiny absolute floor while an
// off-tank holds the boss (while none does, the old half share of the main tank's threat). The
// trigger is data, never the creature's taunt immunity: most raid bosses in this world data are taunt-immune and must
// keep the highest tank, or a zero reference would read as "the tank has no threat" and veto every damage action.
// Tested in tools/test_threat_cap_policy.cpp; the caller resolves the raid's tank order into plain numbers, so this
// header stays free of core includes.

#include <algorithm>
#include <cstddef>
#include <vector>

namespace ai
{
    // One tank on the target as the caller resolved it from the raid's tank order (GetRaidTankRoles): index 0 is the
    // main tank, every later one an assigned off-tank.
    struct ThreatCapTank
    {
        float threat = 0.0f;
        bool offTank = false;
        bool onBoss = false; // the boss is this tank's victim (it holds the boss right now)
    };

    // The off-tank floor, as an absolute amount rather than a share of the highest tank's threat: the reference is
    // whichever of a 1 % share or this fixed amount is larger. A share-only floor (the 50 % this policy once used) sat
    // the cap at the main tank whenever the off-tank dealt a fraction of the main tank's damage, and Hakkar's Cause
    // Insanity then deleted exactly that tank's threat and handed him to the top damage dealer (walk 41: 43 % of the
    // boss's post-charm victims were not tanks). The floor only keeps a barely-engaged or reset off-tank (dead, or its
    // threat just deleted) from capping the damage dealers at almost nothing.
    constexpr float kThreatCapOffTankMinShare = 0.01f;
    constexpr float kThreatCapOffTankMinThreat = 100.0f;

    // The floor while no assigned off-tank has the boss as its victim (Hakkar's off-tanks are away on Sons): their
    // threat is only what an earlier hold left, and a floor that low would freeze the damage dealers while the main
    // tank holds the boss, so the old share of the highest tank's threat applies.
    constexpr float kThreatCapOffTankAwayShare = 0.5f;

    // The reference and which kind of tank it is: `offTank` is the `threat_successor` cap standing on an assigned
    // off-tank, false when the highest tank is the reference.
    struct ThreatCapPick
    {
        float threat = -1.0f;
        bool offTank = false;
    };

    // -1 when no tank is on the target (the caller's own "no reference" answer, kept from the previous max scan).
    // Without the successor cap: the highest tank, as always. With it: the highest threat among the off-tanks that are
    // on the target (threat > 0) and stand above the absolute floor (kThreatCapOffTankMinThreat, or 1 % of the highest
    // tank when that is larger), and the highest tank when no off-tank does: a zero reference is never returned while
    // a tank stands, since the caller reads that as "the tank has no threat" and stops all damage.
    inline ThreatCapPick ThreatCapPickReference(const std::vector<ThreatCapTank>& tanks, bool successorCap)
    {
        ThreatCapPick pick;
        if (tanks.empty())
            return pick;
        float highest = 0.0f;
        for (const ThreatCapTank& tank : tanks)
            highest = std::max(highest, tank.threat);
        pick.threat = highest;
        if (!successorCap)
            return pick;
        bool haveOffTank = false;
        bool offTankOnBoss = false;
        float offTank = 0.0f;
        for (const ThreatCapTank& tank : tanks)
        {
            if (tank.offTank && tank.onBoss)
                offTankOnBoss = true;
            if (!tank.offTank || tank.threat <= 0.0f)
                continue;
            offTank = haveOffTank ? std::max(offTank, tank.threat) : tank.threat;
            haveOffTank = true;
        }
        // An off-tank on the boss is the successor and the absolute floor applies; with every off-tank away the main
        // tank's reference stands unless one still stands above the old half share.
        const float floor = offTankOnBoss ? std::max(highest * kThreatCapOffTankMinShare, kThreatCapOffTankMinThreat)
                                          : highest * kThreatCapOffTankAwayShare;
        if (!haveOffTank || offTank <= floor)
            return pick;
        pick.threat = offTank;
        pick.offTank = true;
        return pick;
    }

    inline float ThreatCapReference(const std::vector<ThreatCapTank>& tanks, bool successorCap)
    {
        return ThreatCapPickReference(tanks, successorCap).threat;
    }

    // The core's pull lines (ThreatManager::getHostileTarget): a mob turns on an attacker whose threat passes 110 % of
    // its current victim's while it can reach that attacker with a melee swing, 130 % otherwise.
    constexpr float kThreatCapCastMeleePullLine = 1.1f;
    constexpr float kThreatCapCastRangedPullLine = 1.3f;

    inline float ThreatCapCastPullLine(bool meleeReach)
    {
        return meleeReach ? kThreatCapCastMeleePullLine : kThreatCapCastRangedPullLine;
    }

    // AiPlayerbot.ThreatCapCountsCastThreat: whether a single-target damage cast is held because its own expected
    // threat would carry the bot past the mob's victim. The high-threat veto (HighThreatTrigger, 80 %) reads only the
    // threat the bot has at cast start, so a Fireball started at 79 % landed about 3 s later as one 400-690 threat hit
    // and jumped the mage past the core's 130 % ranged pull line (non-fire mages: 48 of 87 aggro onsets came within 5 s
    // of a Fireball hit). Held when EITHER
    //  (a) the normal hit reaches the reference: botThreat + castThreat >= tankThreat (100 %, the margin for what the
    //      estimate leaves out), or
    //  (b) the bot can crit with the spell and a crit reaches the core's real pull line: botThreat + castThreat *
    //      critMult >= pullLine * tankThreat (critMult 1.5 for spells, 2 for melee/ranged attack spells, plus crit
    //      talents; pullLine 1.1 in the mob's melee reach, else 1.3).
    // False without the key, without tank threat on the target (the old veto keeps that case) and without an estimate.
    inline bool ThreatCapCastOvershoots(bool key, float botThreat, float castThreat, float tankThreat, float critMult,
                                        float pullLine, bool canCrit)
    {
        if (!key || tankThreat <= 0.0f || castThreat <= 0.0f)
            return false;
        if (botThreat + castThreat >= tankThreat)
            return true;
        return canCrit && botThreat + castThreat * critMult >= pullLine * tankThreat;
    }

    // The average of one spell effect's points, the same arithmetic as the core's WorldObject::CalculateSpellDamage
    // without its dice roll: the base plus the per-level growth, plus the middle of the roll range (irand between the
    // base dice and the die sides, both grown per level). levelDelta is the caster's level clamped to the spell's
    // base/max level, minus the spell level, as the core computes it. Spell mods and the level-damage attribute are
    // the caller's.
    inline float ThreatCapAverageEffectPoints(int basePoints, int baseDice, int dieSides, float pointsPerLevel,
                                              float dicePerLevel, int levelDelta)
    {
        int points = basePoints + int(levelDelta * pointsPerLevel);
        int const randomPoints = int(dieSides + levelDelta * dicePerLevel);
        if (randomPoints == 0 || randomPoints == 1)
            return float(points + baseDice);
        return float(points) + (float(baseDice) + float(randomPoints)) / 2.0f;
    }

    // The threat a cast adds when it lands, the core's arithmetic on plain numbers: the damage times the spell threat
    // table's multiplier (Unit::DealDamage: damage * GetSpellThreatMultiplier; Mind Blast, Searing Pain, Earth Shock
    // and Holy Shock are x2 in tw_world_spell_threat), plus the table's flat threat and AP bonus (Spell::
    // HandleThreatSpells: threat + ap_bonus * attack power), each through the caster's SPELLMOD_THREAT percent and
    // total threat modifier (ThreatCalcHelper::CalcThreat). Never below 0.
    inline float ThreatCapCastThreat(float avgDamage, float threatMultiplier, float flatThreat, float apBonus,
                                     float attackPower, float threatModPct, float totalThreatMod)
    {
        float threat = avgDamage > 0.0f ? avgDamage * threatMultiplier : 0.0f;
        threat += flatThreat + apBonus * attackPower;
        threat *= (1.0f + threatModPct / 100.0f) * totalThreatMod;
        return threat > 0.0f ? threat : 0.0f;
    }

    // A channel's whole damage: one tick's damage times its ticks (duration / amplitude, as the aura ticks). A spell that
    // is not channeled gives 0: a DoT's threat comes a tick at a time, and every later cast is checked again.
    inline float ThreatCapChannelDamage(bool channeled, float perTick, int durationMs, int amplitudeMs)
    {
        if (!channeled || perTick <= 0.0f || durationMs <= 0 || amplitudeMs <= 0)
            return 0.0f;
        return perTick * float(durationMs / amplitudeMs);
    }

    // The threat the cast is measured against: the mob's victim's threat, capped by the top tank's (ThreatValue::
    // GetTankThreat). The core turns the mob on whoever passes its current victim, so an off-tank above the victim is
    // no cover. A victim without threat on the list leaves the top tank's.
    inline float ThreatCapCastReference(float victimThreat, float tankThreat)
    {
        if (tankThreat <= 0.0f || victimThreat <= 0.0f)
            return tankThreat;
        return victimThreat < tankThreat ? victimThreat : tankThreat;
    }
}
