// Hire gear tiers and contract unlocks, the pure half (forever/src/host/hire/TbHireTiersPure.h).
// Build: g++ -std=c++20 -I. forever/tools/test_hire_tiers_pure.cpp -o /tmp/t && /tmp/t
// Every case fails against a plausible wrong rule: a tier-7 name that should not exist, the tab order taken for the
// menu order, the druid feral role swap, a race row that does not win, a boss counted twice, a captured 4-Horseman
// member, a counter target that lowers progress, a fee multiplier index off by one.
#include "forever/src/host/hire/TbHireTiersPure.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

namespace hire = TortoiseBots::hire;
using TortoiseBots::HireGearSpecKey;
using TortoiseBots::kHireRoleDps;
using TortoiseBots::kHireRoleTank;

void TierNames()
{
    Check(std::strcmp(hire::TierDisplayName(0), "Standard") == 0, "tier 0 is Standard");
    Check(std::strcmp(hire::TierDisplayName(1), "Pre-raid") == 0, "tier 1 is Pre-raid");
    Check(std::strcmp(hire::TierDisplayName(6), "Naxxramas") == 0, "tier 6 is Naxxramas");
    Check(hire::TierDisplayName(7) == nullptr, "there is no tier 7 on Forever");
    Check(hire::TierKitName(3) == "Molten Core kit", "kit label");
    Check(hire::TierKitName(7).empty(), "no kit label above the max tier");
}

void Eligibility()
{
    Check(hire::TierSetEligible(60, true, 1), "level 60 tier 1 with tiers on");
    Check(!hire::TierSetEligible(59, true, 1), "tier sets need level 60");
    Check(!hire::TierSetEligible(60, false, 1), "tiers off");
    Check(!hire::TierSetEligible(60, true, 0), "tier 0 is the levelled kit");
    Check(!hire::TierSetEligible(60, true, 7), "tier 7 does not exist");
}

void SpecKeys()
{
    // The menu order of runtime/HireSpecPolicy.h, not the talent-tab order.
    Check(std::strcmp(HireGearSpecKey(1, 0), "protection") == 0, "warrior row 0 is Protection");
    Check(std::strcmp(HireGearSpecKey(1, 1), "arms") == 0, "warrior row 1 is Arms");
    Check(std::strcmp(HireGearSpecKey(8, 0), "frost") == 0, "mage row 0 is Frost");
    Check(std::strcmp(HireGearSpecKey(5, 0), "holy") == 0, "priest row 0 is Holy");
    Check(std::strcmp(HireGearSpecKey(11, 3), "cat") == 0, "druid row 3 is Cat");
    Check(std::strcmp(HireGearSpecKey(7, 3), "tank") == 0, "shaman row 3 is the Turtle tank row");

    // Tab -> menu row: a warrior's second tab is Protection, his first is Arms.
    Check(hire::TierSpecIndexForTab(1, 2, 0) == 0, "warrior protection tab -> menu row 0");
    Check(hire::TierSpecIndexForTab(1, 0, 0) == 1, "warrior arms tab -> menu row 1");
    Check(hire::TierSpecIndexForTab(1, 1, 0) == 2, "warrior fury tab -> menu row 2");
    Check(hire::TierSpecIndexForTab(8, 0, 0) == 2, "mage arcane tab -> menu row 2");
    Check(hire::TierSpecIndexForTab(2, 1, 0) == 0, "paladin protection tab -> menu row 0");
    Check(hire::TierSpecIndexForTab(5, 1, 0) == 0, "priest holy tab -> menu row 0");
    Check(hire::TierSpecIndexForTab(7, 2, 0) == 0, "shaman restoration tab -> menu row 0");

    // Druid feral is one tab and two rows: the role picks Bear or Cat.
    Check(std::strcmp(hire::TierSpecKeyForTab(11, 1, kHireRoleTank), "bear") == 0, "feral + tank -> Bear set");
    Check(std::strcmp(hire::TierSpecKeyForTab(11, 1, kHireRoleDps), "cat") == 0, "feral + damage -> Cat set");
    Check(std::strcmp(hire::TierSpecKeyForTab(11, 0, 0), "balance") == 0, "druid balance tab");
    Check(std::strcmp(hire::TierSpecKeyForTab(11, 2, 0), "restoration") == 0, "druid restoration tab");
    // The shaman tank menu row has no tab of its own: a tank shaman falls back to his real tab.
    Check(std::strcmp(hire::TierSpecKeyForTab(7, 1, kHireRoleTank), "enhancement") == 0, "shaman tank tab -> Enhancement set");
    Check(hire::TierSpecKeyForTab(4, 3, 0) == nullptr, "no fourth tab");
    Check(hire::TierSpecKeyForTab(6, 0, 0) == nullptr, "a class that is not hireable");
}

void RowLookup()
{
    hire::TierGearRow rows[4] = {};
    rows[0].classId = 1;
    std::strcpy(rows[0].spec, "arms");
    rows[0].tier = 3;
    rows[0].slot = 0;
    rows[0].itemId = 100;

    rows[1] = rows[0];
    rows[1].race = 3;
    rows[1].itemId = 101;

    rows[2] = rows[0];
    rows[2].slot = 1;
    rows[2].itemId = 0;   // clear the off hand of a two-handed set

    rows[3] = rows[0];
    rows[3].tier = 4;
    rows[3].itemId = 200;

    hire::TierGearRow const* out = nullptr;
    Check(hire::LookupTierGearRow(rows, 4, 1, "arms", 3, 3, 0, out) && out && out->itemId == 101,
        "the race-specific row wins over the generic one");
    Check(hire::LookupTierGearRow(rows, 4, 1, "arms", 5, 3, 0, out) && out && out->itemId == 100,
        "another race falls back to the generic row");
    Check(hire::LookupTierGearRow(rows, 4, 1, "arms", 0, 3, 1, out) && out && out->itemId == 0,
        "a row with item 0 is a clear-slot row, not a miss");
    Check(!hire::LookupTierGearRow(rows, 4, 1, "arms", 0, 5, 0, out), "no row for another tier");
    Check(!hire::LookupTierGearRow(rows, 4, 1, "fury", 0, 3, 0, out), "no row for another spec");
    Check(!hire::LookupTierGearRow(rows, 4, 1, nullptr, 0, 3, 0, out), "no spec key, no row");
    Check(hire::HasTierSet(rows, 4, 1, "arms", 3), "the set exists");
    Check(!hire::HasTierSet(rows, 4, 1, "arms", 2), "no set at that tier");
}

void Quests()
{
    Check(hire::QuestForTier(1) == 100050, "tier 1 quest is 100050");
    Check(hire::QuestForTier(6) == 100055, "tier 6 quest is 100055");
    Check(hire::QuestForTier(0) == 0 && hire::QuestForTier(7) == 0, "no quest outside the tiers");
    for (uint8_t tier = 1; tier <= hire::kTierMax; ++tier)
        Check(hire::TierForQuest(hire::QuestForTier(tier)) == tier, "quest id round trip");
    Check(hire::TierForQuest(90100) == 0, "TB's quest ids are not Forever's");
    Check(hire::kTierDueCopper[1] == 500000 && hire::kTierDueCopper[6] == 5000000, "gold dues 50g..500g");
}

void Counters()
{
    Check(sizeof(hire::kUnlockCounters) / sizeof(hire::kUnlockCounters[0]) == 8, "eight counters survive");
    uint8_t perTier[hire::kTierMax + 1] = {};
    uint8_t bossesPerTier[hire::kTierMax + 1] = {};
    uint32_t creditEntries[8] = {};
    size_t creditCount = 0;
    std::vector<uint32_t> allBosses;
    for (hire::UnlockCounter const& counter : hire::kUnlockCounters)
    {
        Check(counter.tier >= 1 && counter.tier <= hire::kTierMax, "counter tier in range");
        Check(counter.slot < 4, "a quest has at most four counter slots");
        Check(counter.bossCount > 0, "a counter names bosses");
        ++perTier[counter.tier];
        bossesPerTier[counter.tier] = uint8_t(bossesPerTier[counter.tier] + counter.bossCount);
        Check(creditCount < 8, "counter count fits the credit list");
        creditEntries[creditCount++] = counter.creditEntry;
        for (uint8_t i = 0; i < counter.bossCount; ++i)
        {
            allBosses.push_back(counter.bosses[i].entry);
            // The encounter key must be the one the kill hook computes, or the contracts page and the credit disagree.
            Check(hire::EncounterKey(counter.bosses[i].entry) == counter.bosses[i].entry, "catalog entries are encounter keys");
        }
    }
    Check(perTier[1] == 1 && perTier[2] == 2 && perTier[3] == 2 && perTier[4] == 1 && perTier[5] == 1 && perTier[6] == 1,
        "counters per tier");
    Check(bossesPerTier[1] == 9 && bossesPerTier[2] == 14 && bossesPerTier[3] == 11 && bossesPerTier[4] == 8
        && bossesPerTier[5] == 7 && bossesPerTier[6] == 15, "bosses per tier");
    Check(allBosses.size() == 64, "64 boss entries in the catalog");
    for (size_t i = 0; i < allBosses.size(); ++i)
        for (size_t j = i + 1; j < allBosses.size(); ++j)
            Check(allBosses[i] != allBosses[j], "a boss is listed once");
    // The credit entries are TB's ids for the counters that survive, gaps included.
    uint32_t const expected[8] = { 1000080, 1000081, 1000082, 1000084, 1000085, 1000087, 1000089, 1000090 };
    for (size_t i = 0; i < 8; ++i)
        Check(creditEntries[i] == expected[i], "credit entry ids");
}

void KillRules()
{
    Check(hire::EncounterKey(16063) == 16062 && hire::EncounterKey(16065) == 16062, "every Horseman maps to one key");
    Check(hire::IsGroupedEncounter(16062) && !hire::IsGroupedEncounter(16061), "16062 is the grouped encounter");
    Check(hire::EncounterKey(14509) == 14509, "Thekal is 14509 on Forever, with no phase-two alias");
    hire::UnlockBoss const* boss = nullptr;
    Check(hire::CounterForKill(16065, &boss) != nullptr && boss && boss->entry == 16062, "the last Horseman feeds Naxx");
    Check(hire::CounterForKill(14509, &boss) != nullptr && boss && std::strcmp(boss->name, "High Priest Thekal") == 0,
        "Thekal feeds Zul'Gurub");
    Check(hire::CounterForKill(91928) == nullptr, "Turtle-only bosses are not in the catalog");
    Check(hire::CounterForKill(52145) == nullptr, "the Turtle MC bosses are not in the catalog");
    // Linked credit: a Ragnaros kill also counts Majordomo once.
    Check(hire::LinkedCreditFor(11502) == 12018, "Ragnaros credits Majordomo");
    Check(hire::LinkedCreditFor(12018) == 0, "no chain back");
    Check(hire::CounterForKill(12018, &boss) != nullptr && boss->entry == 12018, "Majordomo is in the MC counter");
}

void CounterMath()
{
    hire::UnlockCounter const& naxx = hire::kUnlockCounters[7];
    Check(naxx.tier == 6, "last counter is the Naxxramas one");
    uint32_t recorded[3] = { 15956, 16062, 15990 };
    Check(hire::CounterTarget(naxx, recorded, 3, 0) == 3, "three recorded bosses");
    Check(hire::CounterTarget(naxx, recorded, 3, 5) == 5, "progress the quest log already shows is never lowered");
    Check(hire::CounterTarget(naxx, nullptr, 0, 0) == 0, "no records, no progress");
    uint32_t foreign[1] = { 9019 };
    Check(hire::CounterTarget(naxx, foreign, 1, 0) == 0, "another counter's boss does not count");

    std::string line;
    std::vector<uint32_t> every;
    for (uint8_t i = 0; i < naxx.bossCount; ++i)
        every.push_back(naxx.bosses[i].entry);
    Check(hire::CounterLine(naxx, 15, every.data(), every.size(), line) && line.find("15/15") != std::string::npos,
        "a complete counter renders with no missing roster");
    // A full missing roster cannot fit one gossip line: the page must open a sub-page instead of truncating.
    Check(!hire::CounterLine(naxx, 0, nullptr, 0, line), "a 15-boss roster overflows one line");
    hire::UnlockCounter const& onyxia = hire::kUnlockCounters[4];
    Check(onyxia.bossCount == 1, "Onyxia is one boss");
    Check(hire::CounterLine(onyxia, 0, nullptr, 0, line) && line.find("Onyxia slain: 0/1") == 0
        && line.find("missing: Onyxia") != std::string::npos, "the Onyxia counter fits one line");
}

void Unlocks()
{
    Check(hire::HighestTierFromMask(0, 0) == 0, "nothing rewarded, start tier 0");
    Check(hire::HighestTierFromMask(0, 1u << 3) == 4, "tier 4 rewarded");
    Check(hire::HighestTierFromMask(3, 0) == 3, "the start tier stands without unlocks");
    Check(hire::HighestTierFromMask(3, 1u << 1) == 3, "a lower unlock does not lower the start tier");
    Check(hire::HighestTierFromMask(0, (1u << 0) | (1u << 5)) == 6, "unlocks need not be contiguous");
    Check(hire::HighestTierFromMask(0, 1u << 6) == 0, "bits above the max tier are ignored");
    Check(hire::HighestTierFromMask(9, 0) == 6, "a start tier above the max clamps");
}

void CreditRules()
{
    hire::CreditCandidate members[4] = {
        { 7, false, true },    // another real player in range
        { 8, true, true },     // a bot: never
        { 9, false, false },   // a real player out of reward distance
        { 5, false, true },    // the killer's low guid, repeated: deduped
    };
    std::vector<uint32_t> targets;
    hire::CreditTargets(5, members, 4, targets);
    Check(targets.size() == 2 && targets[0] == 5 && targets[1] == 7, "killer plus the real member in range, deduped");
}

void Fees()
{
    Check(std::fabs(hire::TierCostCopper(10000, 0) - 10000.0f) < 1.0f, "tier 0 is the base quote");
    Check(hire::TierCostCopper(10000, 1) == 15000, "tier 1 is 1.5x");
    Check(hire::TierCostCopper(10000, 6) == 60000, "tier 6 is 6x");
    Check(hire::TierCostCopper(10000, 9) == 60000, "a tier above the max clamps to the last multiplier");
    float mults[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 9.0f };
    Check(hire::TierCostCopper(5000, 4, mults, 8) == 5000, "a configured multiplier is used");
    Check(hire::TierCostCopper(5000, 4, nullptr, 0) == 20000, "no array falls back to the defaults");
    float shortMults[2] = { 1.0f, 1.0f };
    Check(hire::TierCostCopper(5000, 4, shortMults, 2) == 20000, "a short array falls back to the defaults");
}

void Visibility()
{
    Check(hire::ContractsVisibleFor(true, true, false, 60), "capital, real player, level 60, tiers on");
    Check(!hire::ContractsVisibleFor(false, true, false, 60), "tiers off");
    Check(!hire::ContractsVisibleFor(true, false, false, 60), "not a capital recruiter");
    Check(!hire::ContractsVisibleFor(true, true, true, 60), "a bot never sees the contracts");
    Check(!hire::ContractsVisibleFor(true, true, false, 59), "level 60 only");
    Check(hire::IsCapitalRecruiter(1000017) && hire::IsCapitalRecruiter(1000025), "Stormwind and Orgrimmar are capitals");
    Check(!hire::IsCapitalRecruiter(1000000), "a non-capital recruiter");
}

} // namespace

int main()
{
    TierNames();
    Eligibility();
    SpecKeys();
    RowLookup();
    Quests();
    Counters();
    KillRules();
    CounterMath();
    Unlocks();
    CreditRules();
    Fees();
    Visibility();
    if (failures)
    {
        std::cout << failures << " checks failed\n";
        return 1;
    }
    std::cout << "PASS: hire tiers pure policy\n";
    return 0;
}
