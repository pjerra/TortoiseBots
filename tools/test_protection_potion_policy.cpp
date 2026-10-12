#include "../ai/playerbot/ProtectionPotionPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BossProtectionPotion;

int main()
{
    std::cout << "Starting TortoiseBots protection potion policy tests...\n";

    // Fire: MC fire bosses, Onyxia, Razorgore, Vaelastrasz.
    for (std::uint32_t entry : { 11982u, 12056u, 11988u, 11502u, 10184u, 12435u, 13020u })
    {
        CHECK(BossProtectionPotion(entry).itemId == 13457);
        CHECK(BossProtectionPotion(entry).buffSpellId == 17543);
    }

    // Shadow: Lucifron, Gehennas, Nefarian.
    for (std::uint32_t entry : { 12118u, 12259u, 11583u })
    {
        CHECK(BossProtectionPotion(entry).itemId == 13459);
        CHECK(BossProtectionPotion(entry).buffSpellId == 17548);
    }

    // Nature: Princess Huhuran, Viscidus.
    for (std::uint32_t entry : { 15509u, 15299u })
    {
        CHECK(BossProtectionPotion(entry).itemId == 13458);
        CHECK(BossProtectionPotion(entry).buffSpellId == 17546);
    }

    // Trash and unlisted bosses get nothing (no potion between pulls).
    CHECK(BossProtectionPotion(0).itemId == 0);
    CHECK(BossProtectionPotion(11673).itemId == 0); // Ancient Core Hound (MC trash)
    CHECK(BossProtectionPotion(12057).itemId == 0); // Garr: the fire is on his Firesworn adds
    CHECK(BossProtectionPotion(14834).itemId == 0); // Hakkar: mixed schools, not listed

    std::cout << "Protection potion policy tests passed.\n";
    return 0;
}
