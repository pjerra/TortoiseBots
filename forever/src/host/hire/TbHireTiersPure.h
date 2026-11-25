// Hire gear tiers and their contracts, the part with no core or engine types: tier names, the tier-set key space
// (the recruiter menu's spec rows), the unlock quest catalog (quests, credit creatures, the bosses that count and the
// per-tier gold due), the counter math the contracts page and the kill credit share, and the tier fee multiplier.
// Unit-tested standalone by forever/tools/test_hire_tiers_pure.cpp.
//
// TortoiseBots reference: runtime/policy/hire/HireGearPolicy.h, runtime/HireUnlockPolicy.h,
// runtime/HireUnlockSync.h, runtime/HireUnlockGroupCredit.h, runtime/HireTierRequestPolicy.h. This catalog is Forever's:
// same tier ladder and the same recruiter spec keys, minus the Turtle-only maps, bosses and the Tower of Karazhan tier
// (forever/docs/HIRE-P4-NOTES.md). The host half (TbHireTiers.cpp) reads the sets and talks to the core.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime/policy/hire/HireGearPolicy.h"

namespace TortoiseBots
{
namespace hire
{

// TB's ladder ends at tier 7, the Tower of Karazhan: a Turtle-only raid with no spawns on Forever (the tier-7 rows are
// its set, and they collapse onto tier-6/5 items here). Tiers 0..6 are what Forever's content supports.
constexpr uint8_t kTierMax = 6;
constexpr uint8_t kTierHireLevel = 60;
constexpr size_t kTierSpecKeyMax = 16;

// Display names for the tier step and the contracts page. nullptr above kTierMax.
inline char const* TierDisplayName(uint8_t tier)
{
    switch (tier)
    {
        case 0: return "Standard";
        case 1: return "Pre-raid";
        case 2: return "Zul'Gurub";
        case 3: return "Molten Core";
        case 4: return "Blackwing Lair";
        case 5: return "Ahn'Qiraj";
        case 6: return "Naxxramas";
        default: return nullptr;
    }
}

inline std::string TierKitName(uint8_t tier)
{
    char const* name = TierDisplayName(tier);
    return name ? std::string(name) + " kit" : std::string();
}

// A static pass runs only at level 60 with tiers on; below 60 a hire keeps its levelled kit whatever the choice.
inline bool TierSetEligible(uint8_t level, bool tiersEnabled, uint8_t tier)
{
    return tiersEnabled && level == kTierHireLevel && tier >= 1 && tier <= kTierMax;
}

// The recruiter menu (runtime/HireSpecPolicy.h) index a talent tab means, per class. The menu order is not the tab
// order (a warrior's first row is Protection, his third tab; a mage's first row is Frost, his third tab). Druid feral
// is one tab for two menu rows: the role picks Bear (tank) or Cat.
inline int TierSpecIndexForTab(uint8_t classId, int tab, uint8_t roleBits)
{
    if (tab < 0 || tab > 2)
        return -1;
    int const warrior[3] = { 1, 2, 0 };
    int const paladin[3] = { 1, 0, 2 };
    int const hunter[3] = { 0, 1, 2 };
    int const rogue[3] = { 0, 1, 2 };
    int const priest[3] = { 1, 0, 2 };
    int const shaman[3] = { 1, 2, 0 };
    int const mage[3] = { 2, 1, 0 };
    int const warlock[3] = { 0, 1, 2 };
    switch (classId)
    {
        case kHireClassWarrior: return warrior[tab];
        case kHireClassPaladin: return paladin[tab];
        case kHireClassHunter: return hunter[tab];
        case kHireClassRogue: return rogue[tab];
        case kHireClassPriest: return priest[tab];
        case kHireClassShaman: return shaman[tab];
        case kHireClassMage: return mage[tab];
        case kHireClassWarlock: return warlock[tab];
        case kHireClassDruid:
            if (tab == 1)
                return (roleBits & kHireRoleTank) ? 0 : 3;
            return tab == 0 ? 2 : 1;
        default: return -1;
    }
}

// Table spec key for a talent tab, nullptr for an unknown class/tab or a row with no set on Forever (shaman tank).
inline char const* TierSpecKeyForTab(uint8_t classId, int tab, uint8_t roleBits)
{
    int const index = TierSpecIndexForTab(classId, tab, roleBits);
    return index < 0 ? nullptr : HireGearSpecKey(classId, index);
}

// One `tortoise_bots_hire_gear` row as TbHireTiers.cpp loads it. race 0 is the generic row; a row for the hire's own
// race wins over it. No enchant: TB's enchant ids are mostly not Forever spells (forever/docs/HIRE-P4-NOTES.md).
struct TierGearRow
{
    uint8_t classId = 0;
    char spec[kTierSpecKeyMax] = {};
    uint8_t race = 0;
    uint8_t tier = 0;
    uint8_t slot = 0;
    uint32_t itemId = 0;
};

// The row that answers (class, spec, race, tier, slot): the race-specific row wins, else the race-0 row. False when
// the slot has no row, which leaves whatever the levelled kit put there.
inline bool LookupTierGearRow(TierGearRow const* rows, size_t count,
    uint8_t classId, char const* spec, uint8_t race, uint8_t tier, uint8_t slot, TierGearRow const*& out)
{
    out = nullptr;
    if (!rows || !spec)
        return false;
    TierGearRow const* generic = nullptr;
    for (size_t i = 0; i < count; ++i)
    {
        TierGearRow const& row = rows[i];
        if (row.classId != classId || row.tier != tier || row.slot != slot || std::strcmp(row.spec, spec) != 0)
            continue;
        if (row.race == race)
        {
            out = &row;
            return true;
        }
        if (row.race == 0)
            generic = &row;
    }
    if (generic)
    {
        out = generic;
        return true;
    }
    return false;
}

inline bool HasTierSet(TierGearRow const* rows, size_t count, uint8_t classId, char const* spec, uint8_t tier)
{
    if (!rows || !spec)
        return false;
    for (size_t i = 0; i < count; ++i)
        if (rows[i].classId == classId && rows[i].tier == tier && std::strcmp(rows[i].spec, spec) == 0)
            return true;
    return false;
}

// ---- Unlock contracts ----
// One quest per tier 1..6 at the six capital recruiters: ids 100050-100055 (TB's 90100-90106 are partly taken on
// Forever, id-ranges.md; 100000-100013 belong to the world-buff contracts). Unlock = the quest is rewarded; the quest is a gold-priced boss contract
// (quest_classic_money negative RewardMoney = required, deducted at turn-in).
constexpr uint32_t kQuestFirst = 100050;
constexpr uint32_t kQuestLast = kQuestFirst + kTierMax - 1;

inline uint32_t QuestForTier(uint8_t tier)
{
    if (tier < 1 || tier > kTierMax)
        return 0;
    return kQuestFirst + tier - 1;
}

inline uint8_t TierForQuest(uint32_t questId)
{
    if (questId < kQuestFirst || questId > kQuestLast)
        return 0;
    return static_cast<uint8_t>(questId - kQuestFirst + 1);
}

// Gold due at turn-in, copper. Index = tier; [0] unused.
constexpr uint32_t kTierDueCopper[kTierMax + 1] = {
    0, 500000, 1000000, 2000000, 3000000, 4000000, 5000000 };

// Hire fee multiplier per tier, index = tier. TB's defaults for tiers 0..6 (its 7th value belongs to the dropped tier).
constexpr float kTierCostMultDefault[kTierMax + 1] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };

// Fee at a tier: the base quote times the tier's multiplier, whole copper. A short or empty array falls back to the
// defaults, so a mis-read config never makes a tier free.
inline uint32_t TierCostCopper(uint32_t baseCopper, uint8_t tier, float const* mults = nullptr, size_t multCount = 0)
{
    if (tier > kTierMax)
        tier = kTierMax;
    float mult = kTierCostMultDefault[tier];
    if (mults && multCount > tier && mults[tier] >= 0.0f)
        mult = mults[tier];
    double cost = static_cast<double>(baseCopper) * mult + 0.5;
    if (cost > 4294967295.0)
        return 4294967295u;
    return static_cast<uint32_t>(cost);
}

// One boss (encounter) that counts toward a counter.
struct UnlockBoss
{
    uint32_t entry;
    uint32_t mapId;
    char const* name;
};

// One quest-log counter: an invisible credit creature (never spawned) named by a quest objective, one per boss.
struct UnlockCounter
{
    uint8_t tier;
    uint8_t slot;             // the quest's StorageIndex (0..3)
    uint32_t creditEntry;     // creature_template entry, 1000080-1000090
    char const* label;        // quest objective text and the contracts page
    UnlockBoss const* bosses;
    uint8_t bossCount;
};

constexpr uint32_t kMapBlackrockDepths = 230;
constexpr uint32_t kMapBlackrockSpire = 229;
constexpr uint32_t kMapScholomance = 289;
constexpr uint32_t kMapStratholme = 329;
constexpr uint32_t kMapDireMaul = 429;
constexpr uint32_t kMapZulGurub = 309;
constexpr uint32_t kMapRuinsOfAhnQiraj = 509;
constexpr uint32_t kMapMoltenCore = 409;
constexpr uint32_t kMapOnyxiasLair = 249;
constexpr uint32_t kMapBlackwingLair = 469;
constexpr uint32_t kMapTempleOfAhnQiraj = 531;
constexpr uint32_t kMapNaxxramas = 533;

// Dungeon lords: TB's list without the Karazhan Crypt (Turtle-only map 800).
constexpr UnlockBoss kBossesDungeons[] = {
    { 9019, kMapBlackrockDepths, "Emperor Dagran Thaurissan" },
    { 1853, kMapScholomance, "Darkmaster Gandling" },
    { 10440, kMapStratholme, "Baron Rivendare" },
    { 10813, kMapStratholme, "Balnazzar" },
    { 10363, kMapBlackrockSpire, "General Drakkisath" },
    { 9568, kMapBlackrockSpire, "Overlord Wyrmthalak" },
    { 11492, kMapDireMaul, "Alzzin the Wildshaper" },
    { 11486, kMapDireMaul, "Prince Tortheldrin" },
    { 11501, kMapDireMaul, "King Gordok" },
};

// Thekal is entry 14509 here: Forever has that template only, and 14509 is the real, lootable death (TB's 14599 is
// the fake first corpse of a Turtle spawn).
constexpr UnlockBoss kBossesZulGurub[] = {
    { 14507, kMapZulGurub, "High Priest Venoxis" },
    { 14517, kMapZulGurub, "High Priestess Jeklik" },
    { 14510, kMapZulGurub, "High Priestess Mar'li" },
    { 14509, kMapZulGurub, "High Priest Thekal" },
    { 14515, kMapZulGurub, "High Priestess Arlokk" },
    { 11380, kMapZulGurub, "Jin'do the Hexxer" },
    { 11382, kMapZulGurub, "Bloodlord Mandokir" },
    { 14834, kMapZulGurub, "Hakkar" },
};

constexpr UnlockBoss kBossesRuinsAq[] = {
    { 15348, kMapRuinsOfAhnQiraj, "Kurinnaxx" },
    { 15341, kMapRuinsOfAhnQiraj, "General Rajaxx" },
    { 15340, kMapRuinsOfAhnQiraj, "Moam" },
    { 15370, kMapRuinsOfAhnQiraj, "Buru the Gorger" },
    { 15369, kMapRuinsOfAhnQiraj, "Ayamiss the Hunter" },
    { 15339, kMapRuinsOfAhnQiraj, "Ossirian the Unscarred" },
};

// Molten Core: TB's list without the four Turtle-only additions (Incindis, Sorcerer-Thane Thaurissan, Basalthar,
// Smoldaris).
constexpr UnlockBoss kBossesMoltenCore[] = {
    { 12118, kMapMoltenCore, "Lucifron" },
    { 11982, kMapMoltenCore, "Magmadar" },
    { 12259, kMapMoltenCore, "Gehennas" },
    { 12057, kMapMoltenCore, "Garr" },
    { 12056, kMapMoltenCore, "Baron Geddon" },
    { 12264, kMapMoltenCore, "Shazzrah" },
    { 12098, kMapMoltenCore, "Sulfuron Harbinger" },
    { 11988, kMapMoltenCore, "Golemagg the Incinerator" },
    { 12018, kMapMoltenCore, "Majordomo Executus" },
    { 11502, kMapMoltenCore, "Ragnaros" },
};

constexpr UnlockBoss kBossesOnyxia[] = {
    { 10184, kMapOnyxiasLair, "Onyxia" },
};

constexpr UnlockBoss kBossesBlackwing[] = {
    { 12435, kMapBlackwingLair, "Razorgore the Untamed" },
    { 13020, kMapBlackwingLair, "Vaelastrasz the Corrupt" },
    { 12017, kMapBlackwingLair, "Broodlord Lashlayer" },
    { 11983, kMapBlackwingLair, "Firemaw" },
    { 14601, kMapBlackwingLair, "Ebonroc" },
    { 11981, kMapBlackwingLair, "Flamegor" },
    { 14020, kMapBlackwingLair, "Chromaggus" },
    { 11583, kMapBlackwingLair, "Nefarian" },
};

constexpr UnlockBoss kBossesTempleAq[] = {
    { 15263, kMapTempleOfAhnQiraj, "The Prophet Skeram" },
    { 15516, kMapTempleOfAhnQiraj, "Battleguard Sartura" },
    { 15510, kMapTempleOfAhnQiraj, "Fankriss the Unyielding" },
    { 15509, kMapTempleOfAhnQiraj, "Princess Huhuran" },
    { 15275, kMapTempleOfAhnQiraj, "Emperor Vek'nilash" },
    { 15276, kMapTempleOfAhnQiraj, "Emperor Vek'lor" },
    { 15727, kMapTempleOfAhnQiraj, "C'Thun" },
};

constexpr UnlockBoss kBossesNaxxramas[] = {
    { 15956, kMapNaxxramas, "Anub'Rekhan" },
    { 15953, kMapNaxxramas, "Grand Widow Faerlina" },
    { 15952, kMapNaxxramas, "Maexxna" },
    { 15954, kMapNaxxramas, "Noth the Plaguebringer" },
    { 15936, kMapNaxxramas, "Heigan the Unclean" },
    { 16011, kMapNaxxramas, "Loatheb" },
    { 16061, kMapNaxxramas, "Instructor Razuvious" },
    { 16060, kMapNaxxramas, "Gothik the Harvester" },
    { 16062, kMapNaxxramas, "The Four Horsemen" },
    { 16028, kMapNaxxramas, "Patchwerk" },
    { 15931, kMapNaxxramas, "Grobbulus" },
    { 15932, kMapNaxxramas, "Gluth" },
    { 15928, kMapNaxxramas, "Thaddius" },
    { 15989, kMapNaxxramas, "Sapphiron" },
    { 15990, kMapNaxxramas, "Kel'Thuzad" },
};

#define TB_FOREVER_HIRE_N(arr) static_cast<uint8_t>(sizeof(arr) / sizeof((arr)[0]))

// Credit creatures keep TB's ids for the counters that survive (1000083, 1000086, 1000088 and 1000091 were the
// Lower Karazhan, Timbermaw Hold, Emerald Sanctum and Tower of Karazhan counters and are not used).
constexpr UnlockCounter kUnlockCounters[] = {
    { 1, 0, 1000080, "Dungeon lords slain", kBossesDungeons, TB_FOREVER_HIRE_N(kBossesDungeons) },
    { 2, 0, 1000081, "Zul'Gurub bosses slain", kBossesZulGurub, TB_FOREVER_HIRE_N(kBossesZulGurub) },
    { 2, 1, 1000082, "Ruins of Ahn'Qiraj bosses slain", kBossesRuinsAq, TB_FOREVER_HIRE_N(kBossesRuinsAq) },
    { 3, 0, 1000084, "Molten Core bosses slain", kBossesMoltenCore, TB_FOREVER_HIRE_N(kBossesMoltenCore) },
    { 3, 1, 1000085, "Onyxia slain", kBossesOnyxia, TB_FOREVER_HIRE_N(kBossesOnyxia) },
    { 4, 0, 1000087, "Blackwing Lair bosses slain", kBossesBlackwing, TB_FOREVER_HIRE_N(kBossesBlackwing) },
    { 5, 0, 1000089, "Temple of Ahn'Qiraj bosses slain", kBossesTempleAq, TB_FOREVER_HIRE_N(kBossesTempleAq) },
    { 6, 0, 1000090, "Naxxramas bosses slain", kBossesNaxxramas, TB_FOREVER_HIRE_N(kBossesNaxxramas) },
};

#undef TB_FOREVER_HIRE_N

// The Four Horsemen are one boss: a counter slot fills when the last of the four dies (TbHireTiers.cpp checks the
// other corpses), and every member maps to one encounter key so the stored kills dedupe.
constexpr uint32_t kFourHorsemen[] = { 16062, 16063, 16064, 16065 };

inline uint32_t EncounterKey(uint32_t creatureEntry)
{
    for (uint32_t member : kFourHorsemen)
        if (member == creatureEntry)
            return kFourHorsemen[0];
    return creatureEntry;
}

inline bool IsGroupedEncounter(uint32_t encounterKey)
{
    return encounterKey == kFourHorsemen[0];
}

// Ragnaros can only be summoned once Majordomo falls; Majordomo never dies himself (defeat = evade), so a Ragnaros
// kill credits him too. Dedup is per character, so the order of the two kills does not matter.
struct UnlockLinkedCredit
{
    uint32_t triggerEntry;
    uint32_t alsoCreditEntry;
};

constexpr UnlockLinkedCredit kLinkedCredits[] = {
    { 11502, 12018 },
};

inline uint32_t LinkedCreditFor(uint32_t creatureEntry)
{
    for (UnlockLinkedCredit const& link : kLinkedCredits)
        if (link.triggerEntry == creatureEntry)
            return link.alsoCreditEntry;
    return 0;
}

// The counter a killed creature feeds, or nullptr. The encounter key decides, so all four Horsemen resolve alike.
inline UnlockCounter const* CounterForKill(uint32_t creatureEntry, UnlockBoss const** bossOut = nullptr)
{
    uint32_t const key = EncounterKey(creatureEntry);
    for (UnlockCounter const& counter : kUnlockCounters)
        for (uint8_t i = 0; i < counter.bossCount; ++i)
            if (counter.bosses[i].entry == key)
            {
                if (bossOut)
                    *bossOut = &counter.bosses[i];
                return &counter;
            }
    return nullptr;
}

// Who gets credit for one boss death: the killer always, plus every non-bot group member in reward distance.
// Bots never count (a bot landing the blow must not starve the party's real players). Deduped.
struct CreditCandidate
{
    uint32_t guidLow;
    bool isBot;
    bool inRewardDistance;
};

inline void CreditTargets(uint32_t killerGuidLow, CreditCandidate const* members, size_t memberCount,
    std::vector<uint32_t>& targets)
{
    targets.clear();
    targets.push_back(killerGuidLow);
    for (size_t i = 0; i < memberCount; ++i)
    {
        if (members[i].guidLow == killerGuidLow || members[i].isBot || !members[i].inRewardDistance)
            continue;
        targets.push_back(members[i].guidLow);
    }
}

// Highest tier a player may hire at: the start tier, or the highest rewarded unlock quest when that is higher.
// rewardedMask bit (tier - 1) = that tier's quest is rewarded; the quests need not be contiguous.
inline uint8_t HighestTierFromMask(uint8_t startTier, uint32_t rewardedMask)
{
    uint8_t best = startTier > kTierMax ? kTierMax : startTier;
    for (uint8_t tier = kTierMax; tier >= 1; --tier)
        if (rewardedMask & (1u << (tier - 1)))
        {
            if (tier > best)
                best = tier;
            break;
        }
    return best;
}

// Quest-log counter value: recorded encounters belonging to this counter, never lower than what the quest log
// already holds (a kill missed by the hook must not undo progress the client has shown).
inline uint8_t CounterTarget(UnlockCounter const& counter, uint32_t const* recorded, size_t recordedCount,
    uint8_t current)
{
    uint8_t target = 0;
    for (uint8_t i = 0; i < counter.bossCount; ++i)
        for (size_t r = 0; r < recordedCount; ++r)
            if (counter.bosses[i].entry == recorded[r])
            {
                ++target;
                break;
            }
    return target < current ? current : target;
}

inline bool CounterHasKill(UnlockCounter const& counter, uint32_t creatureEntry)
{
    uint32_t const key = EncounterKey(creatureEntry);
    for (uint8_t i = 0; i < counter.bossCount; ++i)
        if (counter.bosses[i].entry == key)
            return true;
    return false;
}

// Gossip line budget: the contracts page keeps one counter to one line and opens a sub-page for a longer roster.
constexpr size_t kCounterLineMax = 120;

// "Label: x/N, missing: Boss, Boss". False when the roster does not fit one line.
inline bool CounterLine(UnlockCounter const& counter, uint8_t have, uint32_t const* recorded, size_t recordedCount,
    std::string& line)
{
    line = std::string(counter.label) + ": " + std::to_string(have) + "/" + std::to_string(counter.bossCount);
    bool any = false;
    for (uint8_t i = 0; i < counter.bossCount; ++i)
    {
        bool done = false;
        for (size_t r = 0; r < recordedCount; ++r)
            if (counter.bosses[i].entry == recorded[r])
            {
                done = true;
                break;
            }
        if (done)
            continue;
        line += any ? ", " : ", missing: ";
        line += counter.bosses[i].name;
        any = true;
    }
    return line.size() <= kCounterLineMax;
}

// The contracts branch: tiers on, a capital recruiter (the caller passes its own answer), a real player at 60.
inline bool ContractsVisibleFor(bool tiersEnabled, bool capitalRecruiter, bool isBot, uint32_t level)
{
    return tiersEnabled && capitalRecruiter && !isBot && level == kTierHireLevel;
}

// The six capital recruiters, TB's 95017/95006/95012/95025/95018/95019 renumbered by the plan (1000017 Stormwind,
// 1000006 Ironforge, 1000012 Darnassus, 1000025 Orgrimmar, 1000018 Undercity, 1000019 Thunder Bluff).
constexpr uint32_t kCapitalRecruiters[] = { 1000017, 1000006, 1000012, 1000025, 1000018, 1000019 };

inline bool IsCapitalRecruiter(uint32_t entry)
{
    for (uint32_t capital : kCapitalRecruiters)
        if (capital == entry)
            return true;
    return false;
}

} // namespace hire
} // namespace TortoiseBots
