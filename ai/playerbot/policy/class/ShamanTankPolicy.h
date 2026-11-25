#pragma once

#include <cstdint>

// Pure policy for the tank shaman combat loop (issue #1125).
// Every function takes plain scalars so tools/test_shaman_tank_policy.cpp
// pins the exact logic without a server: triggers compose these gates with
// HasSpell / aura / attacker-count reads only.

namespace ai
{
    // Pull mana gate: a shaman tank does not pull below 80% own mana
    // (owner decision). The caller (ShouldPullTrigger) already checked the
    // healer loop; this is the tank's own mana only. Non-mana bots (never a
    // shaman) are exempt so the gate cannot strand them.
    inline bool TankShamanPullManaReady(bool hasManaPool, std::uint8_t manaPct)
    {
        if (!hasManaPool)
            return true;
        return manaPct >= 80;
    }

    // Water Shield swap: Lightning Shield for the pull, Water Shield once
    // mana falls to about two thirds (community shaman tank guide). Both
    // rows stay queued like the elemental pair; the trigger picks the phase.
    inline bool TankShamanWantsWaterShield(bool knowsWaterShield, std::uint8_t manaPct)
    {
        return knowsWaterShield && manaPct <= 66;
    }

    // Hard-cast gate: a tank taking hits cannot hard-cast without pushback
    // and interrupts (Chain Lightning, Lightning Bolt). Hard casts are only
    // permitted while nothing is hitting the tank (pull / range before contact,
    // or while free of attackers).
    inline bool TankHardCastAllowed(std::uint8_t attackerCount)
    {
        return attackerCount == 0;
    }

    // Chain Lightning pack gate: 2+ mobs standing ahead, provided nothing is
    // hitting the tank. Once mobs reach the tank (attackerCount > 0), hard
    // casts are suppressed; the tank uses instant spells and strikes instead
    // (Earth Shock, Stoneclaw, Fire Nova, weapon strikes).
    inline bool TankChainLightningShouldFire(std::uint8_t attackerCount, std::uint8_t packSize)
    {
        if (!TankHardCastAllowed(attackerCount))
            return false;
        return packSize >= 2;
    }

    // Chain Lightning pull branch: fire CL on any pull of 2+ mobs, single
    // Lightning Bolt otherwise. Count is possible attack targets at pull
    // time (pack standing ahead), not mobs already on the bot.
    inline bool TankShamanPullWantsChainLightning(std::uint8_t possibleTargets)
    {
        return possibleTargets >= 2;
    }


    // Earth Shock fight gate: any elite (creature
    // rank above normal — elite, rare-elite, world boss and rare, anywhere)
    // or any raid-instance fight burns Earth Shock on cooldown (~40% of
    // threat); normal 5-man/open-world trash spends it only when threat is
    // needed (someone catching up or a mob loose).
    inline bool TankShamanEarthShockFreeFire(bool bossOrElite, bool inInstance, bool inRaid)
    {
        // inInstance stays in the signature (dungeon trash vs open-world
        // trash both stay economical), but only the raid leg matters now
        // that every elite counts anywhere.
        (void)inInstance;
        return bossOrElite || inRaid;
    }

    // Elite-anywhere rank read: creature ranks above normal count, wherever
    // the fight happens (open-world elites included). Mirrors the
    // GrindTargetValue `rank > CREATURE_ELITE_NORMAL` idiom.
    inline bool TankShamanTargetIsElite(int creatureRank, int normalRank)
    {
        return creatureRank > normalRank;
    }

    // Threat-lead decision for economical trash mode: shock when another
    // group member is catching up on the target (their threat at or above
    // the gated fraction of ours) or when a pack mob is loose (attacking a
    // non-tank while we hold the current target). Fractions are 0-100
    // percents; the strategy passes 70 (catch-up means 70%+ of our threat).
    inline bool TankShamanThreatNeedsShock(float ownThreat, float memberThreat, std::uint8_t catchUpPct, bool mobLoose)
    {
        if (mobLoose)
            return true;
        if (ownThreat <= 0.0f)
            return memberThreat > 0.0f;
        return memberThreat >= ownThreat * catchUpPct / 100.0f;
    }

    // Tank earth pick: exactly one earth totem per fight, so
    // the slot never flaps. Fights (elite/boss/raid) hold Strength of
    // Earth; trash holds Stoneclaw (threat-needed or wide pack). Stoneskin
    // is never the tank pick on a fight — it only survives as the low-level
    // node fallback before Strength of Earth is trained.
    //
    // A standing totem the tank's own automatic rows may replace when the
    // pick changes: Strength of Earth (the fight pick, wrong pick on trash)
    // or Earthbind (the runner snare, which must not lock a pack out of its
    // threat totem). Anything else standing — Tremor, Stoneskin, another
    // shaman's totem — keeps the slot.
    inline bool TankEarthPickReplaceable(bool standingStrengthOfEarth, bool standingEarthbind)
    {
        return standingStrengthOfEarth || standingEarthbind;
    }

    // Tank Stoneclaw: grouped threat-gated drop (opposite of the solo-only
    // panic rule in ShamanStoneclawPolicy.h). Manual earth orders keep
    // precedence; any explicit non-stoneclaw earth order wins. Fights
    // (boss/elite/raid) hold Strength of Earth instead of Stoneclaw; trash
    // fires on arrival while the earth slot is empty and threat is needed
    // (another member catching up or a loose mob), or whenever the pack is
    // wide enough that the totem earns its GCD (3+ attackers). A replaceable
    // standing pick (Strength of Earth from the fight just fought, a runner
    // Earthbind) does not keep the pack out of its own pick.
    inline bool TankStoneclawShouldDrop(bool inGroup, bool manualStoneclaw, bool otherManualEarthOrder,
        bool hasEarthTotem, bool threatNeeded, std::uint8_t attackerCount, bool fightTarget = false,
        bool standingEarthPickReplaceable = false)
    {
        if (otherManualEarthOrder)
            return false;
        if (manualStoneclaw)
            return !hasEarthTotem;
        if (fightTarget)
            return false;
        if (!inGroup)
            return false;
        if (hasEarthTotem && !standingEarthPickReplaceable)
            return false;
        if (threatNeeded)
            return true;
        return attackerCount >= 3;
    }

    // Tank Strength of Earth, the fight pick: boss/elite/raid fights hold it
    // while the earth slot is empty, and a standing Stoneclaw — the pick of
    // the trash pack just fought — never locks the fight out of it.
    inline bool TankStrengthOfEarthShouldDrop(bool fightTarget, bool hasEarthTotem, bool standingStoneclaw)
    {
        if (!fightTarget)
            return false;
        if (!hasEarthTotem)
            return true;
        return standingStoneclaw;
    }

    // Fire Nova drop gate for the tank's pack pick. One fire totem at a
    // time: the Nova totem owns the slot for its whole countdown (5 s in
    // 1.12) and detonates only if nothing replaces it, so the row stands
    // down while one stands. The drop itself carries a 15 s category
    // cooldown (spell_template 11315: category 35, categoryRecoveryTime
    // 15000), which the caller reads off the spell - that is the "Nova is
    // not available this tick" signal the Magma fallback keys on.
    inline bool TankFireNovaShouldDrop(bool knowsFireNova, bool novaReady, bool novaTotemStanding)
    {
        if (!knowsFireNova || !novaReady)
            return false;
        return !novaTotemStanding;
    }

    // Forever's Fire Nova bursts around the standing fire totem instead of
    // dropping one: it needs the totem up and its own 10 s category ready,
    // and it leaves the totem standing for the next burst.
    inline bool TankFireNovaPulseShouldFire(bool knowsFireNova, bool novaReady, bool fireTotemStanding)
    {
        if (!knowsFireNova || !novaReady)
            return false;
        return fireTotemStanding;
    }

    // Forever's Magma is the fire totem Fire Nova pulses from, so a real
    // pack earns it even while the burst is ready; there is no Nova totem
    // in the slot for it to replace.
    inline bool TankMagmaPulseShouldDrop(std::uint8_t attackerCount, bool manaForMagma)
    {
        if (!manaForMagma)
            return false;
        return attackerCount >= 3;
    }

    // Fire slot order (one fire totem at a time): Fire Nova is the pack pick
    // and owns the slot whenever it is available; Magma is only the fallback
    // while Fire Nova is on cooldown and no Nova totem stands; Searing is the
    // single-target filler and never replaces a standing Nova or Magma.
    //
    //   Nova ready / standing -> Nova (its row sits above Magma and Searing)
    //   Nova on cooldown      -> Magma into a real pack (3+), mana allowing
    //   no pack at all        -> Searing
    //
    // The old "4+ attackers regardless of Nova" arm let Magma replace the
    // Nova totem one GCD after the drop, before it could detonate.
    inline bool TankMagmaShouldFollow(bool novaReady, bool novaTotemStanding, std::uint8_t attackerCount, bool manaForMagma)
    {
        if (novaReady || novaTotemStanding)
            return false;
        if (!manaForMagma)
            return false;
        return attackerCount >= 3;
    }

    // Stable per-fight boss/elite decision for the tank's earth pick and its
    // Earth Shock gate. Reading the current target alone flipped the answer
    // every time the tank switched between an elite and its normal-rank
    // adds, and the earth pick followed the flip (Stoneclaw took the slot
    // from Strength of Earth and the fight row took it back on the next
    // switch). Any engaged elite keeps the fight a fight for its duration.
    inline bool TankFightTargetIsBossOrElite(bool currentTargetElite, bool anyAttackerElite)
    {
        return currentTargetElite || anyAttackerElite;
    }
}
