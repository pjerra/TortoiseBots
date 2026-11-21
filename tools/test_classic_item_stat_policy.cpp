#include "../ai/playerbot/policy/item/ClassicItemStatPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

namespace
{

bool SameName(char const* actual, char const* expected)
{
    if (!actual || !expected)
        return actual == expected;
    return std::strcmp(actual, expected) == 0;
}

uint32_t ScoreClassicStat(uint32_t statType, int32_t value, bool isCaster,
    std::unordered_map<std::string, uint32_t> const& weights)
{
    if (value <= 0)
        return 0;
    char const* name = ai::ClassicItemStatWeightName(statType);
    if (!name)
        return 0;

    // Spell-damage / healing items score 0 for non-caster classes (e.g. warrior).
    std::string key(name);
    if (!isCaster && (key == "splpwr" || key == "splheal" ||
        key == "firsplpwr" || key == "natsplpwr" || key == "frosplpwr" ||
        key == "shasplpwr" || key == "arcsplpwr" ||
        key == "spellhitrtng" || key == "spellcritstrkrtng"))
        return 0;

    auto it = weights.find(key);
    if (it == weights.end())
        return 0;
    return static_cast<uint32_t>(value) * it->second;
}

} // namespace

int main()
{
    std::cout << "Starting TortoiseBots classic item stat policy tests...\n";

    // 1. All mapped ForeverCore Classic 1.60 and rating stat ids.
    CHECK(SameName(ai::ClassicItemStatWeightName(12), "defrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(13), "dodgertng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(14), "parryrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(15), "blockrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(16), "hitrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(17), "hitrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(18), "spellhitrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(19), "critstrkrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(20), "critstrkrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(21), "spellcritstrkrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(31), "hitrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(32), "critstrkrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(37), "exprtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(38), "atkpwr"));
    CHECK(SameName(ai::ClassicItemStatWeightName(39), "atkpwr"));
    CHECK(SameName(ai::ClassicItemStatWeightName(41), "splheal"));
    CHECK(SameName(ai::ClassicItemStatWeightName(42), "splpwr"));
    CHECK(SameName(ai::ClassicItemStatWeightName(43), "manargn"));
    CHECK(SameName(ai::ClassicItemStatWeightName(45), "splpwr"));
    CHECK(SameName(ai::ClassicItemStatWeightName(47), "spellpenrtng"));
    CHECK(SameName(ai::ClassicItemStatWeightName(48), "block"));
    CHECK(SameName(ai::ClassicItemStatWeightName(50), "armor"));
    CHECK(SameName(ai::ClassicItemStatWeightName(85), "firsplpwr"));
    CHECK(SameName(ai::ClassicItemStatWeightName(86), "natsplpwr"));
    CHECK(SameName(ai::ClassicItemStatWeightName(87), "frosplpwr"));
    CHECK(SameName(ai::ClassicItemStatWeightName(88), "shasplpwr"));
    CHECK(SameName(ai::ClassicItemStatWeightName(89), "arcsplpwr"));
    for (uint32_t pen = 119; pen <= 123; ++pen)
        CHECK(SameName(ai::ClassicItemStatWeightName(pen), "spellpenrtng"));
    std::cout << "  [PASS] mapped Classic 1.60 and rating stat ids\n";

    // 2. Unknown, base (handled by weightStatLink), and unweighted ids return nullptr (-> 0).
    CHECK(ai::ClassicItemStatWeightName(0) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(3) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(7) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(83) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(84) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(90) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(118) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(124) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(125) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(139) == nullptr);
    CHECK(ai::ClassicItemStatWeightName(999) == nullptr);
    std::cout << "  [PASS] unknown and base stat ids return nullptr\n";

    // 3. Spell-damage item (stat 85/87/42) scores > 0 for a mage, 0 for a warrior.
    std::unordered_map<std::string, uint32_t> const frostMageWeights = {
        {"frosplpwr", 11}, {"splpwr", 11}, {"int", 3}
    };
    std::unordered_map<std::string, uint32_t> const warriorWeights = {
        {"str", 4}, {"atkpwr", 2}, {"critstrkrtng", 56}
    };
    CHECK(ScoreClassicStat(87, 15, true, frostMageWeights) > 0);
    CHECK(ScoreClassicStat(42, 15, true, frostMageWeights) > 0);
    CHECK(ScoreClassicStat(87, 15, false, warriorWeights) == 0);
    CHECK(ScoreClassicStat(42, 15, false, warriorWeights) == 0);
    std::cout << "  [PASS] spell-damage scores for mage and 0 for warrior\n";

    // 4. Healing (41) vs intellect ordering for a holy priest.
    std::unordered_map<std::string, uint32_t> const holyPriestWeights = {
        {"splheal", 29}, {"int", 25}, {"manargn", 26}
    };
    uint32_t const healScore = ScoreClassicStat(41, 10, true, holyPriestWeights);
    uint32_t const intScore = 10 * holyPriestWeights.at("int");
    CHECK(healScore > intScore);
    std::cout << "  [PASS] healing outscores equal intellect for holy priest\n";

    std::cout << "0 failed\n";
    return 0;
}
