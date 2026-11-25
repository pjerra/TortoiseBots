// Raid consumables for bots (issue #22 round 1): WHICH consumable a bot at raiding level keeps up, when the feature may run
// at all and who pays — the pure rules, with no core include (fully testable on the toolchain alone).
//
// The host (forever/src/host/TbRaidConsumables.cpp) reads forever/data/raid/consumables.csv through the generated
// TbConsumableData.gen.h, turns its rows into RowView and asks these functions: the mode (ModeFromInt), the map gate, the
// mode for one bot, the categories due (no aura of the category on the bot), the state gate of a free cast
// (FreeCastStateOk), the checked row a free cast may use (DueFreeCastRow) and the slots the table declares uncovered per
// role (NotCoveredCategories). What the brain then does with a due category — cast the item in the bags (mode 1) or an
// indexed row's spell (mode 2) — is round 2.
//
// Roles are the five `BotConsumableRole`s of the bot brain (ai/playerbot/strategy/actions/UseConsumableAction.h:18-25),
// minus the core: melee, ranged, caster, healer, tank, in the CSV's words. Categories are the exclusivity groups of the
// core's spell groups, never bypassed: a flask fills both elixir slots, so a held flask covers battle and guardian
// (FlaskCoversBothElixirs); and an elixir in either slot fills the slot a flask would wipe, so no flask is due while one
// is held. A free cast only ever takes a category that is due (DueFreeCastRow), so the two can never be applied over each
// other.
//
// The weapon rows carry the main-hand weapon family their enchantment goes on (`edged`, `blunt`, `any`): a sharpening stone
// on a blade, a weightstone on a blunt weapon, an oil on any weapon. The row only fits a main hand of that family
// (WeaponFits), and a held stone is one of the table's own temporary-enchant ids (WeaponEnchantHeld): a rogue poison or a
// shaman imbue is a temporary enchantment too, but not the stone the table keeps up.
//
// Nothing here buys, mails or creates anything: mode 1 only ever names an item already in the bot's bags, and mode 2 only
// names a checked row's spell for a triggered cast (the host does the cast; the core's stacking rules decide).
//
// Tests: forever/tools/test_raid_consumable_policy.cpp (OffDoesNothing, BattlegroundNever, DungeonNeedsKey,
// OwnedFreeNeedsOptIn, FlaskCoversBothElixirs, MinLevelRespected, WeaponDueWithoutEnchant, InvalidModeStaysOff,
// FreeCastUsesTheIndex, FreeCastStaysOffInCombat, FreeCastNeverWipesHeldFlaskOrElixir, WeaponFamilyFits, HeldWeaponEnchant,
// NotCoveredPerRole).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ai
{
namespace raidcons
{

// TortoiseBots.RaidConsumables: 0 off, 1 items (only what the bags already hold), 2 free (a triggered cast of the row's
// spell, no item). The values are the config key's own numbers.
enum class Mode : std::uint8_t
{
    Off = 0,
    Items = 1,
    Free = 2,
};

// The CSV's `role` column; a bot's role comes from its class and spec (the host maps it the way the brain's
// UseConsumableAction::GetBotRole does).
enum class Role : std::uint8_t
{
    Tank = 0,
    Melee = 1,
    Ranged = 2,
    Caster = 3,
    Healer = 4,
    Count = 5,
};

// The CSV's `category` column: the core's exclusivity groups. A category is satisfied by any of its rows' auras; `weapon`
// by the main hand's temporary enchantment (its rows carry no aura: `aura` is 0).
enum class Category : std::uint8_t
{
    Flask = 0,
    Battle = 1,
    Guardian = 2,
    Food = 3,
    Weapon = 4,
    Scroll = 5,
    Count = 6,
};

inline constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::Count);
inline constexpr std::size_t kCategoryCount = static_cast<std::size_t>(Category::Count);

// The CSV's `weapon` column: the main-hand weapon family a weapon row's enchantment goes on. Sharpening stones are for
// blades, weightstones for blunt weapons, oils for any weapon. The other categories leave the column empty (Any here,
// unused).
enum class WeaponFamily : std::uint8_t
{
    Edged = 0,
    Blunt = 1,
    Any = 2,
    Count = 3,
};

inline constexpr std::size_t kWeaponFamilyCount = static_cast<std::size_t>(WeaponFamily::Count);

inline char const* WeaponFamilyWord(WeaponFamily family)
{
    switch (family)
    {
        case WeaponFamily::Edged: return "edged";
        case WeaponFamily::Blunt: return "blunt";
        case WeaponFamily::Any:   return "any";
        case WeaponFamily::Count: break;
    }
    return "";
}

inline bool WeaponFamilyFromWord(std::string_view word, WeaponFamily& out)
{
    for (std::size_t i = 0; i < kWeaponFamilyCount; ++i)
        if (word == WeaponFamilyWord(static_cast<WeaponFamily>(i)))
        {
            out = static_cast<WeaponFamily>(i);
            return true;
        }
    return false;
}

// The core's ItemSubclassWeapon ids (ItemTemplate.h: ItemSubclassWeapon), spelled as numbers because this header includes no
// core file. A fist weapon counts as blunt: the classic weightstones cover it, the shaping stones do not.
inline constexpr std::uint32_t kEdgedWeaponMask =
    (1u << 0) | (1u << 1) |   // axe, two-handed axe
    (1u << 6) | (1u << 7) | (1u << 8) |   // polearm, sword, two-handed sword
    (1u << 15) | (1u << 17);  // dagger, spear
inline constexpr std::uint32_t kBluntWeaponMask =
    (1u << 4) | (1u << 5) |   // mace, two-handed mace
    (1u << 10) | (1u << 13);  // staff, fist weapon

// Whether a main-hand weapon of that core subclass can take the row's enchantment.
inline bool WeaponFits(WeaponFamily family, std::uint32_t subClass)
{
    if (family == WeaponFamily::Any)
        return subClass < 32;
    if (subClass >= 32)
        return false;
    std::uint32_t const bit = 1u << subClass;
    return family == WeaponFamily::Edged ? (kEdgedWeaponMask & bit) != 0 : (kBluntWeaponMask & bit) != 0;
}

// The main hand as the weapon rows need it: whether it carries a weapon, and the core's ItemSubclassWeapon id of it.
struct MainHand
{
    bool weapon = false;
    std::uint32_t subClass = 0;
};

// Whether the main hand's temporary enchantment is one of the table's own weapon rows' (their spells' enchant ids, gathered
// by the host from the checked index). A rogue poison or a shaman imbue is a temporary enchantment too and never counts.
inline bool WeaponEnchantHeld(std::uint32_t mainHandEnchant, std::uint32_t const* tableEnchants, std::size_t count)
{
    if (mainHandEnchant == 0)
        return false;
    for (std::size_t i = 0; tableEnchants && i < count; ++i)
        if (tableEnchants[i] != 0 && tableEnchants[i] == mainHandEnchant)
            return true;
    return false;
}

// One row of the table, as the host hands it to the rules: a classic item and its on-use spell, the aura that spell puts on
// the bot (0 for a weapon row, whose effect is the temporary enchantment), the level the item needs and, for a weapon row,
// the main-hand family it goes on.
struct RowView
{
    Role role = Role::Melee;
    Category category = Category::Battle;
    std::uint32_t item = 0;
    std::uint32_t spell = 0;
    std::uint32_t aura = 0;
    std::uint8_t minLevel = 0;
    WeaponFamily weapon = WeaponFamily::Any;
};

// The words the CSV, the generated table and the read-outs share (lower-case, the CSV spelling).
inline char const* CategoryWord(Category category)
{
    switch (category)
    {
        case Category::Flask:    return "flask";
        case Category::Battle:   return "battle";
        case Category::Guardian: return "guardian";
        case Category::Food:     return "food";
        case Category::Weapon:   return "weapon";
        case Category::Scroll:   return "scroll";
        case Category::Count:    break;
    }
    return "";
}

inline char const* RoleWord(Role role)
{
    switch (role)
    {
        case Role::Tank:   return "tank";
        case Role::Melee:  return "melee";
        case Role::Ranged: return "ranged";
        case Role::Caster: return "caster";
        case Role::Healer: return "healer";
        case Role::Count:  break;
    }
    return "";
}

inline bool CategoryFromWord(std::string_view word, Category& out)
{
    for (std::size_t i = 0; i < kCategoryCount; ++i)
        if (word == CategoryWord(static_cast<Category>(i)))
        {
            out = static_cast<Category>(i);
            return true;
        }
    return false;
}

inline bool RoleFromWord(std::string_view word, Role& out)
{
    for (std::size_t i = 0; i < kRoleCount; ++i)
        if (word == RoleWord(static_cast<Role>(i)))
        {
            out = static_cast<Role>(i);
            return true;
        }
    return false;
}

// Where the feature may run: a raid map always, a dungeon only with TortoiseBots.RaidConsumables.Dungeons on, a
// battleground never (a bot must not spend a flask on a flag), and a plain world map never. `mode` Off refuses everything.
inline bool MapAllowed(Mode mode, bool raid, bool dungeon, bool bg, bool dungeonsKey)
{
    if (mode == Mode::Off)
        return false;
    if (bg)
        return false;
    if (raid)
        return true;
    return dungeon && dungeonsKey;
}

// Who pays. A pool bot and a server-run free bot get the configured mode as it stands (mode 2 costs the owner nothing).
// A player's bot — an owned alt, a hire, anything with a master — gets mode 2 only with FreeForOwned on, else mode 1, so
// a server that leaves free mode on globally still makes a player's bags pay. `owned` means "bound to a real player".
inline Mode EffectiveMode(Mode mode, bool pool, bool owned, bool freeForOwned)
{
    if (mode == Mode::Off)
        return Mode::Off;
    if (pool || !owned)
        return mode;
    if (mode == Mode::Free && !freeForOwned)
        return Mode::Items;
    return mode;
}

// TortoiseBots.RaidConsumables as configured: 0, 1 and 2 are the only values. Anything else is a typo, and a typo must
// never enable the item-free casts: false with `mode` Off, so the host can log the value it refused.
inline bool ModeFromInt(std::int32_t value, Mode& mode)
{
    switch (value)
    {
        case 0: mode = Mode::Off;   return true;
        case 1: mode = Mode::Items; return true;
        case 2: mode = Mode::Free;  return true;
        default: break;
    }
    mode = Mode::Off;
    return false;
}

// A role/category pair the table deliberately has no consumable for (the CSV's not-covered section): the read-out names
// the slot instead of leaving it silently ready.
struct NotCovered
{
    Role role = Role::Tank;
    Category category = Category::Food;
};

// The categories due on one bot, in the table's own order (flask, battle, guardian, food, weapon, scroll), as Category
// values in a uint8 vector. A category is due when at least one of the bot's role's rows is at or below its level and no
// aura of that category is on the bot; `weapon` is due while `tableWeaponEnchant` is false (the main hand carries none of the
// table's own stone and oil enchantments: WeaponEnchantHeld). A held flask covers battle and guardian; a held battle or
// guardian elixir keeps the flask away (it would wipe the elixir for nothing). `heldAuras` is every aura of the table the
// bot carries (any role: a flask is a flask), so a caller scans its rows once.
inline std::vector<std::uint8_t> DueCategories(Role role, std::vector<std::uint32_t> const& heldAuras,
    bool tableWeaponEnchant, std::uint32_t level, RowView const* rows, std::size_t count)
{
    bool held[kCategoryCount] = {};
    bool eligible[kCategoryCount] = {};

    for (std::size_t i = 0; rows && i < count; ++i)
    {
        RowView const& row = rows[i];
        if (row.aura != 0)
            for (std::uint32_t aura : heldAuras)
                if (aura == row.aura)
                {
                    held[static_cast<std::size_t>(row.category)] = true;
                    break;
                }
        if (row.role == role && level >= row.minLevel)
            eligible[static_cast<std::size_t>(row.category)] = true;
    }
    if (tableWeaponEnchant)
        held[static_cast<std::size_t>(Category::Weapon)] = true;

    bool const flaskHeld = held[static_cast<std::size_t>(Category::Flask)];
    bool const elixirHeld = held[static_cast<std::size_t>(Category::Battle)] || held[static_cast<std::size_t>(Category::Guardian)];

    std::vector<std::uint8_t> due;
    for (std::size_t i = 0; i < kCategoryCount; ++i)
    {
        Category const category = static_cast<Category>(i);
        if (!eligible[i] || held[i])
            continue;
        if (flaskHeld && (category == Category::Battle || category == Category::Guardian))
            continue;
        if (elixirHeld && category == Category::Flask)
            continue;
        due.push_back(static_cast<std::uint8_t>(i));
    }
    return due;
}

// The categories the table declares uncovered for `role`, in the table's own order (flask, battle, guardian, food, weapon,
// scroll), as Category values in a uint8 vector. A declared slot is never due and never called ready: the read-out names it.
inline std::vector<std::uint8_t> NotCoveredCategories(Role role, NotCovered const* declared, std::size_t count)
{
    std::vector<std::uint8_t> uncovered;
    for (std::size_t i = 0; i < kCategoryCount; ++i)
        for (std::size_t j = 0; declared && j < count; ++j)
            if (declared[j].role == role && declared[j].category == static_cast<Category>(i))
            {
                uncovered.push_back(static_cast<std::uint8_t>(i));
                break;
            }
    return uncovered;
}

// The state gate of a free cast: free mode, a map the gate allows, a living bot out of combat. Pure so a caller removing
// the combat guard fails a test instead of casting a flask mid-fight.
inline bool FreeCastStateOk(Mode mode, bool mapAllowed, bool alive, bool inCombat)
{
    return mode == Mode::Free && mapAllowed && alive && !inCombat;
}

// The row a free cast (mode 2) may use: the first row of the bot's own role and the category asked for, at or below its
// level, taken from the checked index only — never the generated table, whose dropped rows are not in it. A category that is
// not due is refused (DueCategories): a held flask keeps a battle elixir away and a held elixir keeps the flask away, so a
// free cast can never wipe the other slot. A weapon row must also go on the main hand's weapon family (WeaponFits); with no
// weapon there, or none of the family, there is no row. nullptr when the index has no such row.
inline RowView const* DueFreeCastRow(Role role, Category category, std::uint32_t level,
    std::vector<std::uint32_t> const& heldAuras, bool tableWeaponEnchant, MainHand const& mainHand,
    RowView const* rows, std::size_t count)
{
    bool due = false;
    for (std::uint8_t id : DueCategories(role, heldAuras, tableWeaponEnchant, level, rows, count))
        if (id == static_cast<std::uint8_t>(category))
        {
            due = true;
            break;
        }
    if (!due)
        return nullptr;
    for (std::size_t i = 0; rows && i < count; ++i)
    {
        RowView const& row = rows[i];
        if (row.role != role || row.category != category || level < row.minLevel)
            continue;
        if (category == Category::Weapon && (!mainHand.weapon || !WeaponFits(row.weapon, mainHand.subClass)))
            continue;
        return &row;
    }
    return nullptr;
}

} // namespace raidcons
} // namespace ai
