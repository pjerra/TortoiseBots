#pragma once

// Pure ranking rules for the "party member to dispel" value.
// Donor behaviour (mod-playerbots @ 79bd4281,
// src/Ai/Base/Value/PartyMemberValue.cpp PartyMemberValue::Check): a dispel
// target must be in line of sight and within SpellDistance * 2 of the bot.
// Adapted: the reach cap here is 1.5 x spell distance (39 yd at the default
// 26), so a member just outside cast range is still walked to (the reason
// the old cast-range filter was dropped) but nobody crosses a raid room.
// Native addition (no donor): dispellers spread. Each bot claims the member
// it picked in BuffClaimRegistry under DispelClaimKey(type); another bot
// then ranks that member below every unclaimed one, so N decursers pick N
// different members instead of all hitting the first healer. A claim only
// lowers the rank, never rejects: a member nobody else is free for still
// gets dispelled.
// Within one tier the caller's role order (healers, tanks, master, others)
// decides, as before.
// No core includes: callers translate game state into plain inputs.

#include <cstdint>
#include <string>
#include <vector>

namespace ai
{
    // Debuffs a cure must not remove on its own (Turtle 1.18.1 spell ids). Mutating Injection (Grobbulus, Naxx, 28169)
    // drops a poison cloud where its carrier stands when it is removed, by dispel or by expiry (core boss_grobbulus.cpp,
    // spell_mutating_injection). A cleanse while the carrier is still in the raid puts the cloud in the raid; bomb runout
    // moves the carrier clear first (BombRunoutPolicy.h). A dispel_priority row in mode `clear` is the opt-in release: it
    // may remove such an aura once the carrier is clear of the group. Ported from mod-playerbots dispel_whitelist.
    // StoneformTrigger is held back by the same list: Stoneform removes every disease on the bot when it is cast.
    constexpr std::uint32_t kNeverDispelSpellIds[] = { 28169 };

    inline bool IsNeverDispelSpellId(std::uint32_t spellId)
    {
        for (std::uint32_t neverDispelId : kNeverDispelSpellIds)
        {
            if (spellId == neverDispelId)
                return true;
        }
        return false;
    }

    inline bool DispelOverrideContains(const std::vector<std::uint32_t>& set, std::uint32_t spellId)
    {
        for (std::uint32_t listed : set)
        {
            if (listed == spellId)
                return true;
        }
        return false;
    }

    // Per-member correction to the hard never-dispel list, built from the fight's dispel_priority rows: `allow` lists a
    // never-dispel aura a `clear` row releases (the carrier is clear of the group), `deny` lists an aura a cure must not
    // remove even though it is not on the hard list. Null everywhere else, which keeps the plain rules.
    struct DispelOverride
    {
        std::vector<std::uint32_t> allow;
        std::vector<std::uint32_t> deny;
    };

    constexpr int kDispelTargetRejected = -1;
    constexpr float kDispelReachFactor = 1.5f;

    // Farthest member a dispeller will walk toward.
    inline float DispelReachCap(float spellDistance)
    {
        return spellDistance * kDispelReachFactor;
    }

    // Distance gate, checked before the (expensive) line-of-sight test.
    inline bool IsWithinDispelReach(float distance, float spellDistance)
    {
        return distance <= DispelReachCap(spellDistance);
    }

    // 0 = in cast range and unclaimed (take it at once), 1 = in cast range
    // but another bot already picked it, 2 = needs a short walk, unclaimed,
    // 3 = needs a short walk and claimed. Lower wins. Out of sight or past
    // the reach cap is rejected.
    inline int DispelTargetTier(float distance, float spellDistance, bool inLineOfSight, bool claimedByOther)
    {
        if (!inLineOfSight || !IsWithinDispelReach(distance, spellDistance))
            return kDispelTargetRejected;

        int const tier = distance <= spellDistance ? 0 : 2;
        return claimedByOther ? tier + 1 : tier;
    }

    // Priority-bucketed tier:
    //   0..3  = carrier of a spreading debuff (player or pet)
    //   4..7  = non-spreading player
    //   8..11 = non-spreading pet
    // Within each bucket, reach and claim decide (0..3).
    inline int DispelTargetTier(float distance, float spellDistance, bool inLineOfSight, bool claimedByOther,
                                bool isSpreading, bool isPet)
    {
        int const baseTier = DispelTargetTier(distance, spellDistance, inLineOfSight, claimedByOther);
        if (baseTier == kDispelTargetRejected)
            return kDispelTargetRejected;

        int const bucket = isSpreading ? 0 : (isPet ? 2 : 1);
        return bucket * 4 + baseTier;
    }

    struct DispelSpellEffectInput
    {
        std::uint32_t effect = 0;
        std::uint32_t auraName = 0;
        std::uint32_t triggerSpellId = 0;
        std::uint32_t implicitTargetA = 0;
        std::uint32_t implicitTargetB = 0;
        std::uint32_t radiusIndex = 0;
    };

    struct DispelSpellInput
    {
        std::uint32_t spellId = 0;
        std::uint32_t dispelType = 0;
        bool isPositive = false;
        std::vector<DispelSpellEffectInput> effects;
    };

    inline bool IsAoEOrNearCasterTarget(std::uint32_t target)
    {
        switch (target)
        {
            case 3:   // TARGET_UNIT_FRIEND_NEAR_CASTER
            case 4:   // TARGET_UNIT_NEAR_CASTER
            case 7:   // TARGET_ENUM_UNITS_SCRIPT_AOE_AT_SRC_LOC
            case 8:   // TARGET_ENUM_UNITS_SCRIPT_AOE_AT_DEST_LOC
            case 15:  // TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC
            case 16:  // TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC
            case 20:  // TARGET_ENUM_UNITS_PARTY_WITHIN_CASTER_RANGE
            case 24:  // TARGET_ENUM_UNITS_ENEMY_IN_CONE_24
            case 28:  // TARGET_ENUM_UNITS_ENEMY_AOE_AT_DYNOBJ_LOC
            case 29:  // TARGET_ENUM_UNITS_FRIEND_AOE_AT_DYNOBJ_LOC
            case 30:  // TARGET_ENUM_UNITS_FRIEND_AOE_AT_SRC_LOC
            case 31:  // TARGET_ENUM_UNITS_FRIEND_AOE_AT_DEST_LOC
            case 33:  // TARGET_ENUM_UNITS_PARTY_AOE_AT_SRC_LOC
            case 34:  // TARGET_ENUM_UNITS_PARTY_AOE_AT_DEST_LOC
            case 36:  // TARGET_ENUM_UNITS_ENEMY_WITHIN_CASTER_RANGE
            case 54:  // TARGET_ENUM_UNITS_ENEMY_IN_CONE_54
            case 56:  // TARGET_ENUM_UNITS_RAID_WITHIN_CASTER_RANGE
                return true;
            default:
                return false;
        }
    }

    inline bool IsAreaAuraOrApplyAuraEffect(std::uint32_t effect)
    {
        switch (effect)
        {
            case 6:    // SPELL_EFFECT_APPLY_AURA
            case 35:   // SPELL_EFFECT_APPLY_AREA_AURA_PARTY
            case 119:  // SPELL_EFFECT_APPLY_AREA_AURA_PET
            case 128:  // SPELL_EFFECT_APPLY_AREA_AURA_FRIEND
            case 129:  // SPELL_EFFECT_APPLY_AREA_AURA_ENEMY
            case 132:  // SPELL_EFFECT_APPLY_AREA_AURA_RAID
            case 133:  // SPELL_EFFECT_APPLY_AREA_AURA_OWNER
                return true;
            default:
                return false;
        }
    }

    inline bool IsTriggerAuraName(std::uint32_t auraName)
    {
        return auraName == 23   // SPELL_AURA_PERIODIC_TRIGGER_SPELL
            || auraName == 42   // SPELL_AURA_PROC_TRIGGER_SPELL
            || auraName == 218; // SPELL_AURA_PERIODIC_TRIGGER_SPELL2
    }

    inline bool HasAoEOrNearCasterTarget(const DispelSpellEffectInput& eff)
    {
        return IsAoEOrNearCasterTarget(eff.implicitTargetA) || IsAoEOrNearCasterTarget(eff.implicitTargetB);
    }

    // True when a dispellable negative aura periodically or on-proc triggers a spell hitting nearby units
    // around the carrier. Self-replicating auras require an AoE/near-caster target or radius on the effect
    // itself so single-target self-triggers (e.g. Curse of Idiocy) are not treated as contagious.
    template <typename LookupTriggeredFn>
    inline bool IsSpreadingAuraSpell(const DispelSpellInput& spell, LookupTriggeredFn&& lookupTriggered)
    {
        if (spell.dispelType == 0 || spell.isPositive || IsNeverDispelSpellId(spell.spellId))
            return false;

        for (const DispelSpellEffectInput& eff : spell.effects)
        {
            if (!IsAreaAuraOrApplyAuraEffect(eff.effect) || !IsTriggerAuraName(eff.auraName))
                continue;
            if (eff.triggerSpellId == 0)
                continue;

            bool const selfHasAoE = HasAoEOrNearCasterTarget(eff) || eff.radiusIndex > 0;
            if (eff.triggerSpellId == spell.spellId)
            {
                if (selfHasAoE)
                    return true;
                continue;
            }

            if (selfHasAoE)
                return true;

            DispelSpellInput trig;
            if (lookupTriggered(eff.triggerSpellId, trig))
            {
                for (const DispelSpellEffectInput& trigEff : trig.effects)
                {
                    if (HasAoEOrNearCasterTarget(trigEff))
                        return true;
                }
            }
        }
        return false;
    }

    // BuffClaimRegistry key: one per dispel type, so a priest's Dispel Magic
    // and a mage's Remove Curse on the same member never block each other,
    // while two decursers (mage, druid) share one key.
    inline std::string DispelClaimKey(std::string const& dispelTypeQualifier)
    {
        return "dispel:" + dispelTypeQualifier;
    }

    // --- Dispel mana-worth & danger rules (no core includes) -------------------------------------------------------
    constexpr std::uint32_t kDispelMechanicCharm       = 1;
    constexpr std::uint32_t kDispelMechanicDisoriented = 2;
    constexpr std::uint32_t kDispelMechanicFear        = 5;
    constexpr std::uint32_t kDispelMechanicRoot        = 7;
    constexpr std::uint32_t kDispelMechanicSilence     = 9;
    constexpr std::uint32_t kDispelMechanicSleep       = 10;
    constexpr std::uint32_t kDispelMechanicStun        = 12;
    constexpr std::uint32_t kDispelMechanicFreeze      = 13;
    constexpr std::uint32_t kDispelMechanicKnockout    = 14;
    constexpr std::uint32_t kDispelMechanicPolymorph   = 17;
    constexpr std::uint32_t kDispelMechanicBanish      = 18;
    constexpr std::uint32_t kDispelMechanicShackle     = 20;
    constexpr std::uint32_t kDispelMechanicTurn        = 23;
    constexpr std::uint32_t kDispelMechanicHorror      = 24;

    constexpr std::uint32_t kDispelAuraPeriodicDamage        = 3;
    constexpr std::uint32_t kDispelAuraModConfuse            = 5;
    constexpr std::uint32_t kDispelAuraModCharm              = 6;
    constexpr std::uint32_t kDispelAuraModFear               = 7;
    constexpr std::uint32_t kDispelAuraModStun               = 12;
    constexpr std::uint32_t kDispelAuraModPacify             = 25;
    constexpr std::uint32_t kDispelAuraModRoot               = 26;
    constexpr std::uint32_t kDispelAuraModSilence            = 27;
    constexpr std::uint32_t kDispelAuraPeriodicLeech         = 53;
    constexpr std::uint32_t kDispelAuraModPacifySilence      = 60;
    constexpr std::uint32_t kDispelAuraPeriodicDamagePercent = 89;

    constexpr std::int32_t kBigDotTickDamage = 150;

    struct DispelDebuffEffectInput
    {
        std::uint32_t auraName = 0;
        std::int32_t tickDamage = 0;
    };

    struct DispelDebuffInput
    {
        std::uint32_t mechanic = 0;
        std::uint32_t allMechanicMask = 0;  // bits are (mechanic - 1), matching SpellEntry::GetAllSpellMechanicMask()
        std::vector<DispelDebuffEffectInput> effects;
    };

    inline bool IsDangerousDispelMechanic(std::uint32_t mechanic)
    {
        switch (mechanic)
        {
            case kDispelMechanicCharm:
            case kDispelMechanicDisoriented:
            case kDispelMechanicFear:
            case kDispelMechanicRoot:
            case kDispelMechanicSilence:
            case kDispelMechanicSleep:
            case kDispelMechanicStun:
            case kDispelMechanicFreeze:
            case kDispelMechanicKnockout:
            case kDispelMechanicPolymorph:
            case kDispelMechanicBanish:
            case kDispelMechanicShackle:
            case kDispelMechanicTurn:
            case kDispelMechanicHorror:
                return true;
            default:
                return false;
        }
    }

    inline bool IsDangerousDispelDebuff(DispelDebuffInput const& debuff)
    {
        if (IsDangerousDispelMechanic(debuff.mechanic))
            return true;

        if (debuff.allMechanicMask != 0)
        {
            constexpr std::uint32_t kDangerousMask =
                (1u << (kDispelMechanicCharm - 1)) |
                (1u << (kDispelMechanicDisoriented - 1)) |
                (1u << (kDispelMechanicFear - 1)) |
                (1u << (kDispelMechanicRoot - 1)) |
                (1u << (kDispelMechanicSilence - 1)) |
                (1u << (kDispelMechanicSleep - 1)) |
                (1u << (kDispelMechanicStun - 1)) |
                (1u << (kDispelMechanicFreeze - 1)) |
                (1u << (kDispelMechanicKnockout - 1)) |
                (1u << (kDispelMechanicPolymorph - 1)) |
                (1u << (kDispelMechanicBanish - 1)) |
                (1u << (kDispelMechanicShackle - 1)) |
                (1u << (kDispelMechanicTurn - 1)) |
                (1u << (kDispelMechanicHorror - 1));
            if ((debuff.allMechanicMask & kDangerousMask) != 0)
                return true;
        }

        for (DispelDebuffEffectInput const& eff : debuff.effects)
        {
            switch (eff.auraName)
            {
                case kDispelAuraModConfuse:
                case kDispelAuraModCharm:
                case kDispelAuraModFear:
                case kDispelAuraModStun:
                case kDispelAuraModPacify:
                case kDispelAuraModRoot:
                case kDispelAuraModSilence:
                case kDispelAuraModPacifySilence:
                case kDispelAuraPeriodicDamagePercent:
                    return true;
                case kDispelAuraPeriodicDamage:
                case kDispelAuraPeriodicLeech:
                    if (eff.tickDamage >= kBigDotTickDamage)
                        return true;
                    break;
                default:
                    break;
            }
        }
        return false;
    }

    struct DispelWorthInput
    {
        bool isFriendlyTarget = true;
        bool isDangerous = false;
        bool isSpreading = false;
        std::int32_t remainingDurationMs = 0;  // 0 = permanent/unknown
        std::uint32_t minDurationMs = 2000;
        bool casterHasMana = true;
        std::uint8_t casterManaPct = 100;
        std::uint8_t minManaPct = 25;
    };

    // True when a dispellable aura is worth spending a dispel cast on.
    // Dangerous friendly debuffs (CC, big DoTs, spreading auras) bypass the
    // low-mana and short-duration gates so Polymorph/Fear/deadly DoTs still
    // get cleansed.
    inline bool IsDispelWorthMana(DispelWorthInput const& in)
    {
        if (in.isFriendlyTarget && (in.isDangerous || in.isSpreading))
            return true;
        if (in.minDurationMs > 0 && in.remainingDurationMs > 0 &&
            static_cast<std::uint32_t>(in.remainingDurationMs) < in.minDurationMs)
            return false;
        if (in.minManaPct > 0 && in.casterHasMana && in.casterManaPct < in.minManaPct)
            return false;
        return true;
    }
} // namespace ai
