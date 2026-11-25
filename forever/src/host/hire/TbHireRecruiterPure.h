// The recruiter wizard's decisions, free of core and engine types: how each page packs its choice into the
// 32-bit gossip sender/action pair, which packed values are valid, which classes a recruiter of each side
// offers, when the gear-tier step shows, and which npc_text id a recruiter's greeting lives at.
// Unit-tested standalone: forever/tools/test_hire_recruiter_wizard_check.cpp.
//
// The sender map is the contract header's (forever/src/host/hire/TbHire.h): 501-504 the wizard steps,
// 505-509 world buffs, 510-512 contracts, 520+tier / 530+tier / 540 the gear-tier step and its confirm.
// This page owns 500 (the root) only, which no other page uses.
#pragma once

#include <cstdint>

namespace TortoiseBots::hire::wizard
{
// Gossip sender of the recruiter's own front page ("Hire bots" / "World buffs" / "Mercenary contracts").
constexpr uint32_t kSenderRoot = 500;
constexpr uint32_t kRootHire = 1;
constexpr uint32_t kRootWorldBuffs = 2;
constexpr uint32_t kRootContracts = 3;

// Highest class id with a hire spec table (CLASS_DRUID) and the last playable race of this core
// (RACE_SKYBORNE_HORDE). Races beyond Classic's eight exist here; the menus stay data-driven
// (sObjectMgr->GetPlayerInfo) and only the packing needs a bound.
constexpr uint8_t kMaxClass = 11;
constexpr uint8_t kMaxRace = 96;
constexpr uint8_t kMaxSpecIndex = 3;   // shaman and druid have four rows
constexpr uint8_t kMaxTier = 9;        // protocol bound: 520+t and 530+t must stay below the 540 back sender (content is 0-6)

// The one reserved action byte: "< Back". Every earlier step packs a real choice into each byte, so a
// packed byte can never equal it: classes are 1-11, races 1-96, genders 0/1, specs 0-3.
constexpr uint32_t kActionBack = 255;

// Recruiter entries and their greeting texts (forever/sql/world/2026_11_25_01_tortoisebots_hire_recruiters.sql).
// npc_text id = entry; entry+100 is the variant without the PvE-only line (AiPlayerbot.HireAllowPvP on).
constexpr uint32_t kRecruiterEntryFirst = 1000000;
constexpr uint32_t kRecruiterEntryLast = 1000099;

inline bool IsRecruiterEntry(uint32_t entry)
{
    return entry >= kRecruiterEntryFirst && entry <= kRecruiterEntryLast;
}

inline uint32_t GossipTextId(uint32_t entry, bool allowPvp)
{
    return allowPvp ? entry + 100 : entry;
}

struct Choice
{
    uint8_t classId = 0;
    uint8_t race = 0;
    uint8_t gender = 0;
    uint8_t specIndex = 0;
};

inline bool ValidClass(uint8_t classId) { return classId >= 1 && classId <= kMaxClass; }
inline bool ValidRace(uint8_t race) { return race >= 1 && race <= kMaxRace; }
inline bool ValidGender(uint8_t gender) { return gender <= 1; }
inline bool ValidSpec(uint8_t specIndex) { return specIndex <= kMaxSpecIndex; }

// Class page: action = classId.
inline uint32_t PackClass(uint8_t classId) { return classId; }

inline bool DecodeClass(uint32_t action, uint8_t& classId)
{
    if (action > 0xFF)
        return false;
    uint8_t const value = uint8_t(action);
    if (!ValidClass(value))
        return false;
    classId = value;
    return true;
}

// Race page: action = classId << 8 | race (or classId << 8 | kActionBack for "< Back").
inline uint32_t PackClassRace(uint8_t classId, uint8_t race)
{
    return (uint32_t(classId) << 8) | race;
}

inline bool DecodeClassRace(uint32_t action, uint8_t& classId, uint8_t& race)
{
    if (action > 0xFFFF)
        return false;
    uint8_t const cls = uint8_t((action >> 8) & 0xFF);
    uint8_t const value = uint8_t(action & 0xFF);
    if (!ValidClass(cls) || !ValidRace(value))
        return false;
    classId = cls;
    race = value;
    return true;
}

// Gender page: action = classId << 16 | race << 8 | gender.
inline uint32_t PackClassRaceGender(uint8_t classId, uint8_t race, uint8_t gender)
{
    return (uint32_t(classId) << 16) | (uint32_t(race) << 8) | gender;
}

inline bool DecodeClassRaceGender(uint32_t action, uint8_t& classId, uint8_t& race, uint8_t& gender)
{
    if (action > 0xFFFFFF)
        return false;
    uint8_t const cls = uint8_t((action >> 16) & 0xFF);
    uint8_t const value = uint8_t((action >> 8) & 0xFF);
    uint8_t const sex = uint8_t(action & 0xFF);
    if (!ValidClass(cls) || !ValidRace(value) || !ValidGender(sex))
        return false;
    classId = cls;
    race = value;
    gender = sex;
    return true;
}

// Spec page and every later step: the full selection in four bytes.
inline uint32_t PackChoice(Choice const& choice)
{
    return (uint32_t(choice.classId) << 24) | (uint32_t(choice.race) << 16) |
        (uint32_t(choice.gender) << 8) | choice.specIndex;
}

inline bool DecodeChoice(uint32_t action, Choice& choice)
{
    Choice const value = {
        uint8_t((action >> 24) & 0xFF),
        uint8_t((action >> 16) & 0xFF),
        uint8_t((action >> 8) & 0xFF),
        uint8_t(action & 0xFF),
    };
    if (!ValidClass(value.classId) || !ValidRace(value.race) ||
        !ValidGender(value.gender) || !ValidSpec(value.specIndex))
        return false;
    choice = value;
    return true;
}

// "< Back" on the race, gender and spec pages: the choice bytes are zeroed and the last byte is 255.
inline bool IsBack(uint32_t action)
{
    return (action & 0xFF) == kActionBack;
}

inline uint32_t BackFrom(uint8_t classId, uint8_t race = 0, uint8_t gender = 0)
{
    return PackChoice({ classId, race, gender, uint8_t(kActionBack) });
}

// Which side's classes and races a recruiter offers: the player's own, or both at a neutral inn when
// AiPlayerbot.HireCrossFactionNeutralInns is on and the recruiter's faction belongs to neither side.
enum class OfferSide : uint8_t
{
    Both,
    Alliance,
    Horde,
};

inline bool ClassOffered(uint8_t classId, OfferSide side)
{
    if (!ValidClass(classId))
        return false;
    switch (side)
    {
        case OfferSide::Alliance: return classId != 7;   // CLASS_SHAMAN
        case OfferSide::Horde: return classId != 2;      // CLASS_PALADIN
        default: return true;
    }
}

inline constexpr uint8_t kHireClasses[] = { 1, 2, 3, 4, 5, 7, 8, 9, 11 };
constexpr uint32_t kHireClassCount = sizeof(kHireClasses) / sizeof(kHireClasses[0]);

// The "Choose equipment" step: only at level 60 (the tier sets are level-60 kits) and only when the player
// has unlocked a set above the levelled kit. Below that the wizard goes straight from spec to confirm.
inline bool TierStepVisible(bool tiersEnabled, uint32_t hireLevel, uint8_t highestTier)
{
    return tiersEnabled && hireLevel == 60 && highestTier >= 1 && highestTier <= kMaxTier;
}

inline uint32_t TierStepSender(uint8_t tier) { return 520 + tier; }
inline uint32_t TierConfirmSender(uint8_t tier) { return 530 + tier; }
constexpr uint32_t kSenderTierBack = 540;

inline bool DecodeTierStep(uint32_t sender, uint8_t& tier)
{
    if (sender < 520 || sender > 520 + kMaxTier)
        return false;
    tier = uint8_t(sender - 520);
    return true;
}

inline bool DecodeTierConfirm(uint32_t sender, uint8_t& tier)
{
    if (sender < 530 || sender > 530 + kMaxTier)
        return false;
    tier = uint8_t(sender - 530);
    return true;
}
}
