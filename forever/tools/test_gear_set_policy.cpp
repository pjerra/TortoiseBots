#define TORTOISEBOTS_CORE_FOREVER 1
#include "playerbot/policy/item/GearSetPolicy.h"

#include <cassert>
#include <iostream>
#include <vector>

namespace
{
ai::GearItemCandidate MakeItem(uint32_t id,
                               uint32_t exp,
                               uint8_t quality,
                               uint16_t ilvl,
                               uint8_t reqLvl,
                               uint8_t invType,
                               uint8_t itemClass,
                               uint8_t subClass,
                               uint32_t score,
                               uint32_t randomBonus = 0,
                               uint32_t allowClass = 0xFFFFFFFFu)
{
    ai::GearItemCandidate c;
    c.itemId = id;
    c.requiredExpansion = exp;
    c.randomBonusListTemplateId = randomBonus;
    c.allowableClass = allowClass;
    c.score = score;
    c.itemLevel = ilvl;
    c.requiredLevel = reqLvl;
    c.quality = quality;
    c.inventoryType = invType;
    c.itemClass = itemClass;
    c.subClass = subClass;
    return c;
}
} // namespace

int main()
{
    // 1. Failing case first: pool contains a higher-scoring non-classic item (RequiredExpansion > 0), an id
    //    outside every gear band and an item that must not be picked; a level-60 warrior picks the legal plate.
    {
        std::vector<ai::GearItemCandidate> pool = {
            MakeItem(24000, 1, 3, 70, 60, 1, 4, 4, 9999),  // TBC expansion=1, highest score -> MUST NOT PICK
            MakeItem(28000, 0, 3, 70, 60, 1, 4, 4, 8888),  // 25000-199999 is outside the band -> MUST NOT PICK
            MakeItem(16922, 0, 4, 76, 60, 1, 4, 4, 7777),  // trap item -> MUST NOT PICK
            MakeItem(12640, 0, 4, 61, 56, 1, 4, 4, 6000),  // epic, requires quality=4 -> skipped at default rare
            MakeItem(12587, 0, 3, 63, 58, 1, 4, 4, 500),   // classic rare plate helm -> MUST PICK
            MakeItem(14000, 0, 3, 63, 58, 1, 4, 4, 500, 42) // random suffix != 0 -> MUST NOT PICK
        };
        uint8_t const maxQ = ai::TargetMaxQualityForLevel(60); // Rare (3)
        auto ranked = ai::RankCandidatesForSlot(pool, 1, "arms", 60, 0, maxQ, false, false);
        assert(!ranked.empty());
        assert(ranked[0].itemId == 12587);
    }

    // 2. Two-hand weapon in mainhand forbids off-hand slot 16.
    {
        std::vector<ai::GearItemCandidate> ohPool = {
            MakeItem(12000, 0, 3, 60, 58, 14, 4, 6, 400), // Shield
            MakeItem(12001, 0, 3, 60, 58, 13, 2, 7, 400), // 1H Sword
        };
        auto ranked = ai::RankCandidatesForSlot(ohPool, 1, "arms", 60, 16, 3, true, true);
        assert(ranked.empty());
    }

    // 3. Level-30 warrior cannot wear plate (subclass 4); gets mail (subclass 3) instead.
    {
        std::vector<ai::GearItemCandidate> chestPool = {
            MakeItem(10001, 0, 3, 32, 30, 5, 4, 4, 900), // Plate at 30 -> refused
            MakeItem(10002, 0, 3, 32, 30, 5, 4, 3, 300), // Mail at 30 -> picked
        };
        auto ranked = ai::RankCandidatesForSlot(chestPool, 1, "arms", 30, 4, 3, false, false);
        assert(ranked.size() == 1);
        assert(ranked[0].itemId == 10002);
    }

    // 4. Level bracket: level-30 set never picks reqLevel > 30 or ilvl below bracket floor.
    {
        auto const bracket = ai::LevelBracketForLevel(30);
        std::vector<ai::GearItemCandidate> pool = {
            MakeItem(10010, 0, 2, 33, 31, 5, 4, 3, 900), // reqLevel 31 > 30 -> refused
            MakeItem(10011, 0, 2, 10, 10, 5, 4, 3, 800), // ilvl 10 < floor (18) -> refused
            MakeItem(10012, 0, 2, 28, 25, 5, 4, 3, 200), // valid
        };
        auto ranked = ai::RankCandidatesForSlot(pool, 1, "arms", 30, 4, ai::TargetMaxQualityForLevel(30), false, false);
        assert(ranked.size() == 1);
        assert(ranked[0].itemId == 10012);
        assert(ranked[0].requiredLevel <= bracket.maxRequiredLevel);
        assert(ranked[0].itemLevel >= bracket.minItemLevel);
    }

    // 5. Tie-break by lowest itemId when score, quality, and itemLevel match.
    {
        std::vector<ai::GearItemCandidate> pool = {
            MakeItem(15000, 0, 2, 20, 15, 5, 4, 1, 100),
            MakeItem(12000, 0, 2, 20, 15, 5, 4, 1, 100),
        };
        auto ranked = ai::RankCandidatesForSlot(pool, 8, "frost", 20, 4, 2, false, false);
        assert(ranked.size() == 2);
        assert(ranked[0].itemId == 12000);
    }

    // 6. Quality ramp: uncommon below 30, rare from 30, epic only when explicitly requested.
    {
        assert(ai::TargetMaxQualityForLevel(10) == 2);
        assert(ai::TargetMaxQualityForLevel(29) == 2);
        assert(ai::TargetMaxQualityForLevel(30) == 3);
        assert(ai::TargetMaxQualityForLevel(60) == 3);
        assert(ai::TargetMaxQualityForLevel(60, 4) == 4);
    }

    // 7. Preferred replacements lookup at level 60 (normalized spec names).
    {
        auto prefs = ai::PreferredReplacementsFor(8, "frost", 0);
        assert(!prefs.empty());
        assert(prefs[0] == 22498); // Frostfire Circlet (highest tier)
        auto protPrefs = ai::PreferredReplacementsFor(1, "protection", 0);
        assert(!protPrefs.empty());
    }

    // 8. Mage at level 60 ranks classic cloth/weapon candidates both with "fire" and empty spec.
    {
        std::vector<ai::GearItemCandidate> pool = {
            MakeItem(18727, 0, 3, 59, 54, 1, 4, 1, 248), // Crimson Felt Hat (cloth)
            MakeItem(12587, 0, 3, 63, 58, 1, 4, 4, 500), // Plate helm -> refused for mage
        };
        auto rankedFire = ai::RankCandidatesForSlot(pool, 8, "fire", 60, 0, 3, false, false);
        assert(rankedFire.size() == 1);
        assert(rankedFire[0].itemId == 18727);
        auto rankedEmpty = ai::RankCandidatesForSlot(pool, 8, "", 60, 0, 3, false, false);
        assert(rankedEmpty.size() == 1);
        assert(rankedEmpty[0].itemId == 18727);
    }

    // 9. Gear band: the classic range plus Forever's own item range, nothing between or after.
    {
        assert(ai::IsItemInGearBand(19019));
        assert(ai::IsItemInGearBand(24999));
        assert(!ai::IsItemInGearBand(0));
        assert(!ai::IsItemInGearBand(25000));
        assert(!ai::IsItemInGearBand(199999));
        assert(ai::IsItemInGearBand(200000));
        assert(ai::IsItemInGearBand(291874));
        assert(!ai::IsItemInGearBand(292000));

        std::vector<ai::GearItemCandidate> pool = {
            MakeItem(24999, 0, 3, 62, 57, 1, 4, 4, 100),
            MakeItem(200001, 0, 3, 62, 57, 1, 4, 4, 900), // new Forever plate helm -> MUST PICK
            MakeItem(292001, 0, 3, 62, 57, 1, 4, 4, 9999), // past the band -> MUST NOT PICK
        };
        auto ranked = ai::RankCandidatesForSlot(pool, 1, "arms", 60, 0, 3, false, false);
        assert(ranked.size() == 2);
        assert(ranked[0].itemId == 200001);
    }

    // 10. RequiredLevel 0 uses the effective level from the item level; a real requirement is kept.
    {
        std::vector<ai::GearItemCandidate> pool = {
            MakeItem(13000, 0, 2, 16, 0, 5, 4, 1, 900), // req 0, ilvl 16 -> effective 11 > 10 -> refused
            MakeItem(13001, 0, 1, 9, 0, 5, 4, 1, 800),  // req 0, ilvl 9 -> effective 4 -> allowed
            MakeItem(13002, 0, 2, 10, 20, 5, 4, 1, 700), // stored req 20 > 10 -> refused
        };
        auto ranked = ai::RankCandidatesForSlot(pool, 8, "frost", 10, 4, 2, false, false);
        assert(ranked.size() == 1);
        assert(ranked[0].itemId == 13001);

        std::vector<ai::GearItemCandidate> level20 = {
            MakeItem(13003, 0, 2, 25, 0, 5, 4, 1, 900), // effective 20 <= 20 -> allowed
        };
        auto ranked20 = ai::RankCandidatesForSlot(level20, 8, "frost", 20, 4, 2, false, false);
        assert(ranked20.size() == 1);
    }

    std::cout << "PASS: test_gear_set_policy (10 checks, 0 failed)\n";
    return 0;
}
