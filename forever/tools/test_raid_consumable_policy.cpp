// Standalone test of the raid-consumable policy (ai/playerbot/policy/raid/RaidConsumablePolicy.h, issue #22 round 1):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_raid_consumable_policy.cpp -o /tmp/test_raid_consumable_policy && /tmp/test_raid_consumable_policy
//
// The cases the host and the brain depend on: the mode switch (0, 1, 2 and nothing else), the map gate (raids, a dungeon
// only with its key, never a battleground), a player's bot gets free mode only with the opt-in, a held flask covers both
// elixir slots, a row below the bot's level is not applied, a weapon row is due exactly while the main hand carries no
// temporary enchant, a free cast is chosen from the checked index by the bot's own role and category, and the table's
// not-covered declarations answer per role.
#include "ai/playerbot/policy/raid/RaidConsumablePolicy.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace rc = ai::raidcons;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; auto va = (a); auto vb = (b); if (!(va == vb)) { ++g_fail; std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, (long long)va, (long long)vb); } } while (0)

namespace
{
// The aura and item ids are the pin's (the CSV is forever/data/raid/consumables.csv): the flask of the titans, the
// elixir of giants, the elixir of superior defense and the dumplings' Well Fed, one per category.
constexpr std::uint32_t kTitansAura = 17626;
constexpr std::uint32_t kElixirOfGiantsAura = 11405;
constexpr std::uint32_t kGreatArmorAura = 11348;
constexpr std::uint32_t kFoodAura = 1248422;

rc::RowView Row(rc::Role role, rc::Category category, std::uint32_t item, std::uint32_t spell, std::uint32_t aura,
    std::uint8_t level, rc::WeaponFamily weapon = rc::WeaponFamily::Any)
{
    rc::RowView row;
    row.role = role;
    row.category = category;
    row.item = item;
    row.spell = spell;
    row.aura = aura;
    row.minLevel = level;
    row.weapon = weapon;
    return row;
}

// The core's ItemSubclassWeapon ids the weapon cases use (ItemTemplate.h: sword 7, dagger 15, mace 4, staff 10, bow 2).
constexpr std::uint32_t kSword = 7;
constexpr std::uint32_t kMace = 4;
constexpr std::uint32_t kStaff = 10;
constexpr std::uint32_t kBow = 2;

// A main hand holding a weapon of that subclass, and an empty one.
rc::MainHand Hand(std::uint32_t subClass)
{
    rc::MainHand hand;
    hand.weapon = subClass != 0;
    hand.subClass = subClass;
    return hand;
}

// One tank's dinner as the table seeds it: a flask, a battle elixir, a guardian elixir, food and a weapon stone.
std::vector<rc::RowView> TankRows()
{
    return {
        Row(rc::Role::Tank, rc::Category::Flask, 13510, 17626, kTitansAura, 50),
        Row(rc::Role::Tank, rc::Category::Battle, 9206, 11405, kElixirOfGiantsAura, 48),
        Row(rc::Role::Tank, rc::Category::Guardian, 13445, 11348, kGreatArmorAura, 43),
        Row(rc::Role::Tank, rc::Category::Food, 20452, 1248401, kFoodAura, 45),
        Row(rc::Role::Tank, rc::Category::Weapon, 12404, 16138, 0, 35, rc::WeaponFamily::Edged),
    };
}

std::vector<std::uint8_t> Due(std::vector<std::uint32_t> const& heals, bool weaponEnchant, std::uint32_t level,
    std::vector<rc::RowView> const& rows, rc::Role role = rc::Role::Tank)
{
    return rc::DueCategories(role, heals, weaponEnchant, level, rows.data(), rows.size());
}

bool Has(std::vector<std::uint8_t> const& due, rc::Category category)
{
    for (std::uint8_t id : due)
        if (id == static_cast<std::uint8_t>(category))
            return true;
    return false;
}
} // namespace

int main()
{
    // OffDoesNothing: no map allows it and no bot gets a mode, whatever its kind.
    {
        CHECK(!rc::MapAllowed(rc::Mode::Off, true, true, false, true));
        CHECK_EQ(static_cast<int>(rc::EffectiveMode(rc::Mode::Off, true, true, true)), 0);
        CHECK_EQ(static_cast<int>(rc::EffectiveMode(rc::Mode::Off, false, false, false)), 0);
    }

    // BattlegroundNever: not even with the dungeon key on and a raid flag set by a custom map.
    {
        CHECK(!rc::MapAllowed(rc::Mode::Items, false, false, true, true));
        CHECK(!rc::MapAllowed(rc::Mode::Free, true, false, true, true));
        CHECK(!rc::MapAllowed(rc::Mode::Items, false, true, true, true));
    }

    // DungeonNeedsKey: dungeons only with the key, raids always (a plain world map never).
    {
        CHECK(!rc::MapAllowed(rc::Mode::Items, false, true, false, false));
        CHECK(rc::MapAllowed(rc::Mode::Items, false, true, false, true));
        CHECK(rc::MapAllowed(rc::Mode::Items, true, false, false, false));
        CHECK(rc::MapAllowed(rc::Mode::Free, true, true, false, true));
        CHECK(!rc::MapAllowed(rc::Mode::Free, false, false, false, true));
    }

    // OwnedFreeNeedsOptIn: a player's bot in free mode falls back to items unless FreeForOwned is on; a pool bot and a
    // server-run bot do not.
    {
        CHECK_EQ(static_cast<int>(rc::EffectiveMode(rc::Mode::Free, true, false, false)), 2);
        CHECK_EQ(static_cast<int>(rc::EffectiveMode(rc::Mode::Free, false, false, false)), 2);
        CHECK_EQ(static_cast<int>(rc::EffectiveMode(rc::Mode::Free, false, true, false)), 1);
        CHECK_EQ(static_cast<int>(rc::EffectiveMode(rc::Mode::Free, false, true, true)), 2);
        CHECK_EQ(static_cast<int>(rc::EffectiveMode(rc::Mode::Items, false, true, true)), 1);
        CHECK_EQ(static_cast<int>(rc::EffectiveMode(rc::Mode::Items, true, false, false)), 1);
    }

    // FlaskCoversBothElixirs: a held flask covers both elixir slots (and itself); the food and the weapon stay due. A held
    // elixir instead fills the
    // slot a flask would replace, so the flask is not due while a battle or guardian elixir is on the bot.
    {
        std::vector<rc::RowView> const rows = TankRows();
        std::vector<std::uint8_t> const flask = Due({ kTitansAura }, false, 60, rows);
        CHECK(!Has(flask, rc::Category::Flask));
        CHECK(!Has(flask, rc::Category::Battle));
        CHECK(!Has(flask, rc::Category::Guardian));
        CHECK(Has(flask, rc::Category::Food));
        CHECK(Has(flask, rc::Category::Weapon));

        std::vector<std::uint8_t> const battle = Due({ kElixirOfGiantsAura }, false, 60, rows);
        CHECK(!Has(battle, rc::Category::Battle));
        CHECK(!Has(battle, rc::Category::Flask));
        CHECK(Has(battle, rc::Category::Guardian));

        std::vector<std::uint8_t> const guardian = Due({ kGreatArmorAura }, false, 60, rows);
        CHECK(!Has(guardian, rc::Category::Guardian));
        CHECK(!Has(guardian, rc::Category::Flask));
        CHECK(Has(guardian, rc::Category::Battle));
    }

    // MinLevelRespected: a row above the bot's level is not due. At 42 the flask (50), the battle elixir (48) and the food (45) wait, the
    // guardian elixir (43) waits one more level, and the stone (35) is due.
    {
        std::vector<rc::RowView> const rows = TankRows();
        std::vector<std::uint8_t> const level42 = Due({}, false, 42, rows);
        CHECK(!Has(level42, rc::Category::Flask));
        CHECK(!Has(level42, rc::Category::Battle));
        CHECK(!Has(level42, rc::Category::Guardian));
        CHECK(!Has(level42, rc::Category::Food));
        CHECK(Has(level42, rc::Category::Weapon));

        std::vector<std::uint8_t> const level43 = Due({}, false, 43, rows);
        CHECK(Has(level43, rc::Category::Guardian));
        CHECK(!Has(level43, rc::Category::Food));

        // A prompt for every due category, in the table's own order (flask, battle, guardian, food, weapon, scroll).
        std::vector<std::uint8_t> const empty = Due({}, false, 60, rows);
        CHECK_EQ(empty.size(), std::size_t(5));
        CHECK_EQ(static_cast<int>(empty[0]), static_cast<int>(rc::Category::Flask));
        CHECK_EQ(static_cast<int>(empty[1]), static_cast<int>(rc::Category::Battle));
        CHECK_EQ(static_cast<int>(empty[2]), static_cast<int>(rc::Category::Guardian));
        CHECK_EQ(static_cast<int>(empty[3]), static_cast<int>(rc::Category::Food));
        CHECK_EQ(static_cast<int>(empty[4]), static_cast<int>(rc::Category::Weapon));
    }

    // WeaponDueWithoutEnchant: the weapon slot is held by the main hand's temporary enchantment alone (its rows carry no aura).
    {
        std::vector<rc::RowView> const rows = TankRows();
        std::vector<std::uint8_t> const without = Due({}, false, 60, rows);
        CHECK(Has(without, rc::Category::Weapon));
        std::vector<std::uint8_t> const with = Due({}, true, 60, rows);
        CHECK(!Has(with, rc::Category::Weapon));
        CHECK(Has(with, rc::Category::Flask));
    }

    // Another role's rows never answer for this one, and a role with no row has nothing due.
    {
        std::vector<rc::RowView> const rows = TankRows();
        std::vector<std::uint8_t> const healer = Due({}, false, 60, rows, rc::Role::Healer);
        CHECK_EQ(healer.size(), std::size_t(0));
        std::vector<std::uint8_t> const melee = Due({}, false, 60, rows, rc::Role::Melee);
        CHECK_EQ(melee.size(), std::size_t(0));
    }

    // The words the CSV and the read-outs share round-trip.
    {
        CHECK(std::string_view(rc::CategoryWord(rc::Category::Flask)) == "flask");
        CHECK(std::string_view(rc::CategoryWord(rc::Category::Weapon)) == "weapon");
        CHECK(std::string_view(rc::RoleWord(rc::Role::Ranged)) == "ranged");
        rc::Category category = rc::Category::Count;
        CHECK(rc::CategoryFromWord("guardian", category));
        CHECK_EQ(static_cast<int>(category), static_cast<int>(rc::Category::Guardian));
        CHECK(!rc::CategoryFromWord("potions", category));
        rc::Role role = rc::Role::Count;
        CHECK(rc::RoleFromWord("caster", role));
        CHECK_EQ(static_cast<int>(role), static_cast<int>(rc::Role::Caster));
        CHECK(!rc::RoleFromWord("healers", role));
    }

    // ModeFromInt: exactly 0, 1 and 2 are the documented values. Anything else is a typo and must not enable a mode: it
    // falls back to off (the host logs the configured value once), so `3` never turns on the item-free casts.
    {
        rc::Mode mode = rc::Mode::Free;
        CHECK(rc::ModeFromInt(0, mode));
        CHECK_EQ(static_cast<int>(mode), 0);
        CHECK(rc::ModeFromInt(1, mode));
        CHECK_EQ(static_cast<int>(mode), 1);
        CHECK(rc::ModeFromInt(2, mode));
        CHECK_EQ(static_cast<int>(mode), 2);
        CHECK(!rc::ModeFromInt(3, mode));
        CHECK_EQ(static_cast<int>(mode), 0);
        CHECK(!rc::ModeFromInt(7, mode));
        CHECK_EQ(static_cast<int>(mode), 0);
        CHECK(!rc::ModeFromInt(-1, mode));
        CHECK_EQ(static_cast<int>(mode), 0);
    }

    // FreeCastUsesTheIndex: a free cast uses one row of the checked index, the bot's own role and the category asked for, at
    // or below its level, and only while the category is due. A category the index has no row for answers nothing, so a row
    // the startup check dropped can never be cast.
    {
        std::vector<rc::RowView> const index = TankRows();
        std::vector<std::uint32_t> const none;
        rc::RowView const* row = rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Flask, 60, none, false,
            Hand(kSword), index.data(), index.size());
        CHECK(row != nullptr);
        CHECK_EQ(row ? row->item : 0, 13510u);
        CHECK_EQ(row ? row->spell : 0, 17626u);
        CHECK_EQ(row ? row->aura : 0, kTitansAura);
        // The bot's role decides: a healer never gets the tank's flask even when a flask is asked for.
        CHECK(rc::DueFreeCastRow(rc::Role::Healer, rc::Category::Flask, 60, none, false,
            Hand(kSword), index.data(), index.size()) == nullptr);
        // Level: the flask needs 50, the stone 35.
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Flask, 49, none, false,
            Hand(kSword), index.data(), index.size()) == nullptr);
        rc::RowView const* stone = rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Weapon, 35, none, false,
            Hand(kSword), index.data(), index.size());
        CHECK(stone != nullptr);
        CHECK_EQ(stone ? stone->item : 0, 12404u);
        // The weapon row is cast on the main hand, so it carries no aura.
        CHECK_EQ(stone ? stone->aura : 1, 0u);
        // An empty index (the table did not resolve on this server) answers nothing.
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Flask, 60, none, false,
            Hand(kSword), nullptr, 0) == nullptr);
    }

    // FreeCastNeverWipesHeldFlaskOrElixir: the category asked for must be due. A held flask keeps the battle and guardian
    // elixirs away (they would only be wiped by the flask's slots); a held elixir keeps the flask away.
    {
        std::vector<rc::RowView> const index = TankRows();
        std::vector<std::uint32_t> const none;
        std::vector<std::uint32_t> const flask = { kTitansAura };
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Battle, 60, flask, false,
            Hand(kSword), index.data(), index.size()) == nullptr);
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Guardian, 60, flask, false,
            Hand(kSword), index.data(), index.size()) == nullptr);
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Food, 60, flask, false,
            Hand(kSword), index.data(), index.size()) != nullptr);
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Weapon, 60, flask, false,
            Hand(kSword), index.data(), index.size()) != nullptr);

        std::vector<std::uint32_t> const battle = { kElixirOfGiantsAura };
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Flask, 60, battle, false,
            Hand(kSword), index.data(), index.size()) == nullptr);
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Battle, 60, battle, false,
            Hand(kSword), index.data(), index.size()) == nullptr);
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Guardian, 60, battle, false,
            Hand(kSword), index.data(), index.size()) != nullptr);

        std::vector<std::uint32_t> const guardian = { kGreatArmorAura };
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Flask, 60, guardian, false,
            Hand(kSword), index.data(), index.size()) == nullptr);
        // A category with no row at all is never due, whatever the caller asks for.
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Scroll, 60, none, false,
            Hand(kSword), index.data(), index.size()) == nullptr);
    }

    // FreeCastStaysOffInCombat: the state gate is free mode, an allowed map, a living bot and no combat. The host hands its
    // own facts; a guard dropped here fails this case.
    {
        CHECK(rc::FreeCastStateOk(rc::Mode::Free, true, true, false));
        CHECK(!rc::FreeCastStateOk(rc::Mode::Free, true, true, true));
        CHECK(!rc::FreeCastStateOk(rc::Mode::Free, true, false, false));
        CHECK(!rc::FreeCastStateOk(rc::Mode::Free, false, true, false));
        CHECK(!rc::FreeCastStateOk(rc::Mode::Items, true, true, false));
        CHECK(!rc::FreeCastStateOk(rc::Mode::Off, true, true, false));
    }

    // WeaponFamilyFits: a stone goes on a blade, a weightstone on a blunt weapon, an oil on any weapon; a bow takes none
    // (the table declares the ranged weapon slot uncovered instead).
    {
        CHECK(rc::WeaponFits(rc::WeaponFamily::Edged, kSword));
        CHECK(rc::WeaponFits(rc::WeaponFamily::Edged, 15));   // dagger
        CHECK(rc::WeaponFits(rc::WeaponFamily::Edged, 0));    // one-handed axe
        CHECK(rc::WeaponFits(rc::WeaponFamily::Edged, 6));    // polearm
        CHECK(!rc::WeaponFits(rc::WeaponFamily::Edged, kMace));
        CHECK(!rc::WeaponFits(rc::WeaponFamily::Edged, kStaff));
        CHECK(!rc::WeaponFits(rc::WeaponFamily::Edged, kBow));
        CHECK(rc::WeaponFits(rc::WeaponFamily::Blunt, kMace));
        CHECK(rc::WeaponFits(rc::WeaponFamily::Blunt, kStaff));
        CHECK(rc::WeaponFits(rc::WeaponFamily::Blunt, 13));   // fist weapon
        CHECK(!rc::WeaponFits(rc::WeaponFamily::Blunt, kSword));
        CHECK(rc::WeaponFits(rc::WeaponFamily::Any, kBow));
        CHECK(rc::WeaponFits(rc::WeaponFamily::Any, 19));     // wand
        CHECK(!rc::WeaponFits(rc::WeaponFamily::Any, 32));    // not a weapon subclass
        // The row only goes on the family the table names, and only with a weapon in the main hand.
        std::vector<rc::RowView> const index = TankRows();
        std::vector<std::uint32_t> const none;
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Weapon, 60, none, false,
            Hand(kMace), index.data(), index.size()) == nullptr);
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Weapon, 60, none, false,
            Hand(0), index.data(), index.size()) == nullptr);
        rc::RowView const* stone = rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Weapon, 60, none, false,
            Hand(kSword), index.data(), index.size());
        CHECK(stone != nullptr);
        CHECK_EQ(stone ? static_cast<int>(stone->weapon) : -1, static_cast<int>(rc::WeaponFamily::Edged));
        // A weapon row already on the main hand (the table's own enchant) is not cast again.
        CHECK(rc::DueFreeCastRow(rc::Role::Tank, rc::Category::Weapon, 60, none, true,
            Hand(kSword), index.data(), index.size()) == nullptr);
        rc::WeaponFamily family = rc::WeaponFamily::Count;
        CHECK(rc::WeaponFamilyFromWord("edged", family));
        CHECK_EQ(static_cast<int>(family), static_cast<int>(rc::WeaponFamily::Edged));
        CHECK(rc::WeaponFamilyFromWord("any", family));
        CHECK_EQ(static_cast<int>(family), static_cast<int>(rc::WeaponFamily::Any));
        CHECK(!rc::WeaponFamilyFromWord("sharp", family));
        CHECK(std::string_view(rc::WeaponFamilyWord(rc::WeaponFamily::Blunt)) == "blunt");
    }

    // HeldWeaponEnchant: only the table's own enchant ids count. A rogue poison or a shaman imbue is a temporary enchantment
    // too, but not the sharpening stone or the oil.
    {
        std::uint32_t const table[] = { 2647u, 2648u };
        CHECK(rc::WeaponEnchantHeld(2647u, table, 2));
        CHECK(rc::WeaponEnchantHeld(2648u, table, 2));
        CHECK(!rc::WeaponEnchantHeld(2649u, table, 2));   // a poison or an imbue
        CHECK(!rc::WeaponEnchantHeld(0u, table, 2));
        CHECK(!rc::WeaponEnchantHeld(2647u, nullptr, 0));
        CHECK(!rc::WeaponEnchantHeld(2647u, table, 0));
        // A row whose spell leaves no enchant id never matches.
        std::uint32_t const zero[] = { 0u };
        CHECK(!rc::WeaponEnchantHeld(2647u, zero, 1));
    }

    // NotCoveredCategories: the declarations of the bot's role, in the table's order, whatever order the file lists them
    // in; another role's declarations never answer.
    {
        rc::NotCovered const declared[] = {
            { rc::Role::Tank, rc::Category::Weapon },
            { rc::Role::Ranged, rc::Category::Food },
            { rc::Role::Tank, rc::Category::Food },
        };
        std::vector<std::uint8_t> const tank = rc::NotCoveredCategories(rc::Role::Tank, declared, 3);
        CHECK_EQ(tank.size(), std::size_t(2));
        CHECK_EQ(static_cast<int>(tank[0]), static_cast<int>(rc::Category::Food));
        CHECK_EQ(static_cast<int>(tank[1]), static_cast<int>(rc::Category::Weapon));
        std::vector<std::uint8_t> const healer = rc::NotCoveredCategories(rc::Role::Healer, declared, 3);
        CHECK_EQ(healer.size(), std::size_t(0));
        CHECK_EQ(rc::NotCoveredCategories(rc::Role::Tank, nullptr, 0).size(), std::size_t(0));
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
