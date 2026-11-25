// Raid consumables for bots (issue #22 round 1): see host/TbRaidConsumables.h.
//
// The table is compiled in (forever/src/host/data/TbConsumableData.gen.h, from forever/tools/gen_data_tables.py); building
// the index checks every row against the core's own stores, so a wrong id can never hand a bot the wrong buff: a row whose
// item is missing, whose item does not list that spell, or whose spell leaves another aura is dropped with a log line.
//
// Nothing here creates, buys or mails an item: mode 1 only names an item the bags already hold, and mode 2 only casts the
// row's spell (the core's spell groups decide the stacking, never this file).
#include "host/TbRaidConsumables.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "compat/botpch_forever.h"   // the core headers, then the shim: every core include after this one is a no-op
#endif

#include "Config.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"

#include "TbBotMgr.h"
#include "compat/TbItemPrototype.h"
#include "compat/TbSpellEntry.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotAI.h"
#endif

#include <algorithm>
#include <utility>

namespace TortoiseBots
{
namespace raidcons
{
namespace
{
using ai::raidcons::Category;
using ai::raidcons::Mode;
using ai::raidcons::Role;

// The logging goes straight to the core's logger: in the engine build the compat shim has already replaced the legacy
// `sLog` with its printf proxy, so the TC_LOG_ macros do not compile there (the same reason host/TbResistGear.cpp does it).
template <typename... Args>
void LogAt(char const* logger, LogLevel level, fmt::format_string<Args...> message, Args&&... args)
{
    Log::instance()->OutMessage(logger, level, message, std::forward<Args>(args)...);
}

struct Config
{
    Mode mode = Mode::Off;
    bool dungeons = false;
    bool freeForOwned = false;
};
Config config;

// One row of the table that resolved on this server, with its words already turned into the policy's enums.
struct Row
{
    Role role = Role::Melee;
    Category category = Category::Battle;
    std::uint32_t item = 0;
    std::uint32_t spell = 0;
    std::uint32_t aura = 0;
    std::uint8_t minLevel = 0;
    ai::raidcons::WeaponFamily weapon = ai::raidcons::WeaponFamily::Any;
    char const* note = "";
};

std::vector<Row> rows;
std::vector<ai::raidcons::RowView> indexViews;   // `rows` as the pure rules ask for them, rebuilt when the index is
std::vector<std::uint32_t> weaponEnchants;       // the weapon rows' enchant ids: what counts as the slot being held
bool indexBuilt = false;
bool unbuiltLogged = false;   // the "store is not ready" line once per process (`.tb state` asks six times a bot)

// A classic item id. A Turtle custom consumable (250000+) must be named here before the table may use it, so a typo in
// a large id is dropped at boot instead of naming an unrelated custom item.
constexpr std::uint32_t kClassicItemCeiling = 25000;
bool ListedCustomItem(std::uint32_t /*itemId*/)
{
    return false;   // none yet: the R1 seeds are all classic ids, and later rounds list their custom rows here
}

// Whether the spell leaves `aura` on its caster: the aura is the spell itself (a flask, an elixir, a food spell that
// applies its own buff) or a spell one of its effects triggers (the pin's food rows: "Nutritious Food" triggers the
// 15-minute "Well Fed"). A spell with no aura effect of its own and no such trigger does not qualify.
bool AuraOfSpell(SpellEntry const& spell, std::uint32_t aura)
{
    if (aura == spell.Id)
        for (std::uint32_t effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
            if (spell.EffectApplyAuraName[effect] != 0)
                return true;
    for (std::uint32_t effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
        if (spell.EffectTriggerSpell[effect] == aura)
            return true;
    return false;
}

// A weapon row's effect: the temporary enchantment of the main-hand item (sharpen stones and the oils), and the enchant id
// it leaves there (the spell effect's misc value). The core names two spell effects for it (SharedDefines.h: the classic
// 1.60 one and the older one); 0 means the spell is not such an effect.
std::uint32_t TemporaryEnchantId(SpellEntry const& spell)
{
    for (std::uint32_t effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
        if (spell.Effect[effect] == SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY ||
            spell.Effect[effect] == SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY_2)
            return static_cast<std::uint32_t>(spell.EffectMiscValue[effect]);
    return 0;
}

void EnsureIndex()
{
    if (!indexBuilt)
        BuildIndex();
}

Item* MainHandOf(Player* bot)
{
    return bot ? bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) : nullptr;
}

// The main hand's temporary enchantment when it is one of the table's own weapon rows (a stone or an oil), else 0: a rogue
// poison or a shaman imbue sits in the same slot and is not this feature's.
std::uint32_t TableWeaponEnchant(Player* bot)
{
    Item* const mainHand = MainHandOf(bot);
    if (!mainHand)
        return 0;
    std::uint32_t const enchant = mainHand->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT);
    if (enchant == 0)
        return 0;
    EnsureIndex();
    return ai::raidcons::WeaponEnchantHeld(enchant, weaponEnchants.data(), weaponEnchants.size()) ? enchant : 0;
}

// Whether the main hand's weapon can take the row's enchantment (the table's `weapon` family). False without a weapon there.
bool MainHandFits(Player* bot, ai::raidcons::WeaponFamily family)
{
    Item* const mainHand = MainHandOf(bot);
    if (!mainHand)
        return false;
    ItemTemplate const* const proto = mainHand->GetTemplate();
    if (!proto || proto->GetClass() != ITEM_CLASS_WEAPON)
        return false;
    return ai::raidcons::WeaponFits(family, proto->GetSubClass());
}

// A player's bot — an owned alt, a hire, anything bound to a real player — rather than a pool bot or a server-run free
// bot. The master is the binding (`.bot add`, a hire); a bot with neither a master nor free mode fails closed as owned,
// so a test login never gets something for free that an owner would pay for.
bool PlayerBound(Player* bot)
{
    ObjectGuid const guid = bot->GetGUID();
    BotMgr& mgr = BotMgr::Instance();
    if (mgr.IsPoolBot(guid))
        return false;
    if (mgr.MasterOf(guid) != ObjectGuid::Empty)
        return true;
    return !mgr.IsFreeBot(guid);
}
} // namespace

bool Enabled()
{
    return config.mode != Mode::Off;
}

void LoadConfig()
{
    std::int32_t const mode = sConfigMgr->GetIntDefault("TortoiseBots.RaidConsumables", 0);
    if (!ai::raidcons::ModeFromInt(mode, config.mode))
        LogAt("server.loading", LOG_LEVEL_ERROR, "TortoiseBots.RaidConsumables = {} is not a mode (0 off, 1 items, 2 free); raid consumables stay off", mode);
    config.dungeons = sConfigMgr->GetBoolDefault("TortoiseBots.RaidConsumables.Dungeons", false);
    config.freeForOwned = sConfigMgr->GetBoolDefault("TortoiseBots.RaidConsumables.FreeForOwned", false);
}

void BuildIndex()
{
    rows.clear();
    indexViews.clear();
    weaponEnchants.clear();
    indexBuilt = false;
    // The rows are checked against the item and spell mirrors, which answer nothing until their stores are ready. Leave the
    // index unbuilt then and let the first answer try again (a world that starts with the module off must not cache an
    // empty table for good).
    if (tbcompat::MaxSpellId() == 0)
    {
        if (!unbuiltLogged)
        {
            unbuiltLogged = true;
            LogAt("server.loading", LOG_LEVEL_INFO, "TortoiseBots: raid consumables index left unbuilt (the spell store is not ready); the first lookup builds it");
        }
        return;
    }

    for (std::size_t i = 0; i < tbdata::raidcons::kConsumableCount; ++i)
    {
        tbdata::raidcons::ConsumableRow const& source = tbdata::raidcons::kConsumables[i];
        Role role = Role::Count;
        Category category = Category::Count;
        if (!ai::raidcons::RoleFromWord(source.role, role) || !ai::raidcons::CategoryFromWord(source.category, category))
        {
            LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: raid consumable row {} ({}) dropped: unknown role or category", uint32(source.item), source.note);
            continue;
        }
        if (source.item >= kClassicItemCeiling && !ListedCustomItem(source.item))
        {
            LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: raid consumable row {} ({}) dropped: a custom item id is not listed", uint32(source.item), source.note);
            continue;
        }
        ItemPrototype const* item = tbcompat::ItemProto(source.item);
        if (!item)
        {
            LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: raid consumable row {} ({}) dropped: the item is not in the item store", uint32(source.item), source.note);
            continue;
        }
        bool listsSpell = false;
        for (std::uint32_t slot = 0; slot < MAX_ITEM_PROTO_SPELLS; ++slot)
            if (item->Spells[slot].SpellId == source.spell)
            {
                listsSpell = true;
                break;
            }
        if (!listsSpell)
        {
            LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: raid consumable row {} ({}) dropped: the item's spells do not include {}", uint32(source.item), source.note, uint32(source.spell));
            continue;
        }
        SpellEntry const* spell = tbcompat::SpellProto(source.spell);
        if (!spell)
        {
            LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: raid consumable row {} ({}) dropped: spell {} is not in the spell store", uint32(source.item), source.note, uint32(source.spell));
            continue;
        }
        ai::raidcons::WeaponFamily weapon = ai::raidcons::WeaponFamily::Any;
        std::uint32_t enchant = 0;
        if (source.aura != 0)
        {
            if (!AuraOfSpell(*spell, source.aura))
            {
                LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: raid consumable row {} ({}) dropped: spell {} leaves no aura {}", uint32(source.item), source.note, uint32(source.spell), uint32(source.aura));
                continue;
            }
        }
        else
        {
            enchant = TemporaryEnchantId(*spell);
            if (enchant == 0)
            {
                LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: raid consumable row {} ({}) dropped: spell {} carries no temporary weapon enchantment", uint32(source.item), source.note, uint32(source.spell));
                continue;
            }
        }
        if (category == Category::Weapon)
        {
            if (!ai::raidcons::WeaponFamilyFromWord(source.weapon, weapon))
            {
                LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: raid consumable row {} ({}) dropped: unknown weapon family '{}'", uint32(source.item), source.note, source.weapon);
                continue;
            }
        }
        else if (source.weapon[0] != '\0')
        {
            LogAt("server.loading", LOG_LEVEL_WARN, "TortoiseBots: raid consumable row {} ({}) dropped: a weapon family on a {} row", uint32(source.item), source.note, ai::raidcons::CategoryWord(category));
            continue;
        }
        Row row;
        row.role = role;
        row.category = category;
        row.item = source.item;
        row.spell = source.spell;
        row.aura = source.aura;
        row.minLevel = source.minLevel;
        row.weapon = weapon;
        row.note = source.note;
        rows.push_back(row);
        if (category == Category::Weapon)
            weaponEnchants.push_back(enchant);
    }
    indexViews.reserve(rows.size());
    for (Row const& row : rows)
    {
        ai::raidcons::RowView view;
        view.role = row.role;
        view.category = row.category;
        view.item = row.item;
        view.spell = row.spell;
        view.aura = row.aura;
        view.minLevel = row.minLevel;
        view.weapon = row.weapon;
        indexViews.push_back(view);
    }
    indexBuilt = true;
    LogAt("server.loading", LOG_LEVEL_INFO, "TortoiseBots: {}", BootLine());
    for (std::size_t i = 0; i < tbdata::raidcons::kNotCoveredCount; ++i)
    {
        tbdata::raidcons::NotCoveredRow const& gap = tbdata::raidcons::kNotCovered[i];
        LogAt("server.loading", LOG_LEVEL_INFO, "TortoiseBots: raid consumables: {}/{} not covered: {}", gap.role, gap.category, gap.reason);
    }
}

std::string BootLine()
{
    std::string line = "raid consumables: " + std::to_string(tbdata::raidcons::kConsumableCount) + " rows, " +
        std::to_string(rows.size()) + " kept, " + std::to_string(tbdata::raidcons::kNotCoveredCount) + " uncovered (mode " +
        std::to_string(uint32(config.mode)) + ", dungeons " + (config.dungeons ? "yes" : "no") + ", free for owned " +
        (config.freeForOwned ? "yes" : "no") + ")";
    return line;
}

ai::raidcons::Mode ModeFor(Player* bot)
{
    if (!Enabled() || !bot)
        return Mode::Off;
    if (!BotMgr::Instance().IsBot(bot->GetGUID()))
        return Mode::Off;   // a real player's character is never given one
    bool const pool = BotMgr::Instance().IsPoolBot(bot->GetGUID());
    bool const owned = !pool && PlayerBound(bot);
    return ai::raidcons::EffectiveMode(config.mode, pool, owned, config.freeForOwned);
}

bool MapAllows(Player* bot)
{
    Map* const map = bot ? bot->GetMap() : nullptr;
    if (!map)
        return false;
    return ai::raidcons::MapAllowed(ModeFor(bot), map->IsRaid(), map->IsDungeon(), map->IsBattleground(), config.dungeons);
}

bool RoleOf(Player* bot, ai::raidcons::Role& role)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    if (!bot)
        return false;
    // The same table the brain's own consumable picker uses (UseConsumableAction::GetBotRole: the class, the spec tab, and
    // for the shaman and druid tank tabs the engine's tank view). Karrz's file is read-only, so the mapping lives here.
    std::uint8_t const playerClass = bot->GetClass();
    int const tab = AiFactory::GetPlayerSpecTab(bot);
    switch (playerClass)
    {
        case CLASS_WARRIOR:
            role = tab == 2 ? Role::Tank : Role::Melee;
            return true;
        case CLASS_PALADIN:
            role = tab == 0 ? Role::Healer : tab == 1 ? Role::Tank : Role::Melee;
            return true;
        case CLASS_HUNTER:
            role = Role::Ranged;
            return true;
        case CLASS_ROGUE:
            role = Role::Melee;
            return true;
        case CLASS_PRIEST:
            role = tab == 2 ? Role::Caster : Role::Healer;
            return true;
        case CLASS_SHAMAN:
            if (tab == 1 && PlayerbotAI::IsTank(bot, true))
                role = Role::Tank;
            else if (tab == 0)
                role = Role::Caster;
            else if (tab == 1)
                role = Role::Melee;
            else
                role = Role::Healer;
            return true;
        case CLASS_MAGE:
        case CLASS_WARLOCK:
            role = Role::Caster;
            return true;
        case CLASS_DRUID:
            if (tab == 0)
                role = Role::Caster;
            else if (tab == 1)
                role = PlayerbotAI::IsTank(bot, true) ? Role::Tank : Role::Melee;
            else
                role = Role::Healer;
            return true;
        default:
            role = Role::Melee;
            return true;
    }
#else
    (void)bot;
    (void)role;
    return false;
#endif
}

std::uint32_t BagItemFor(Player* bot, ai::raidcons::Category category)
{
    if (!Enabled() || !bot)
        return 0;
    Role role = Role::Count;
    if (!RoleOf(bot, role))
        return 0;
    EnsureIndex();
    for (Row const& row : rows)
    {
        if (row.role != role || row.category != category || bot->GetLevel() < row.minLevel ||
            bot->GetItemCount(row.item, false) == 0)
            continue;
        // A stone or an oil only goes on the weapon family the table names: a sharpening stone on a mace would be the wrong
        // buff, and the core would let it through.
        if (category == Category::Weapon && !MainHandFits(bot, row.weapon))
            continue;
        return row.item;
    }
    return 0;
}

bool ApplyFree(Player* bot, ai::raidcons::Category category)
{
    if (!Enabled() || !bot)
        return false;
    if (!ai::raidcons::FreeCastStateOk(ModeFor(bot), MapAllows(bot), bot->IsAlive(), bot->IsInCombat()))
        return false;
    Role role = Role::Count;
    if (!RoleOf(bot, role))
        return false;

    // The cast row comes from the checked index: the bot's own role, the category asked for, at or below its level, a
    // category that is due (a held flask keeps a battle elixir away and a held elixir keeps the flask away) and, for a
    // weapon row, a main hand the enchantment goes on. A generated row the startup check dropped is not in the index and can
    // never be cast, and the spell cast here is the indexed row's, never a caller's.
    EnsureIndex();
    std::vector<std::uint32_t> heldAuras;
    for (Row const& row : rows)
        if (row.aura != 0 && bot->HasAura(row.aura))
            heldAuras.push_back(row.aura);
    bool const weaponHeld = TableWeaponEnchant(bot) != 0;
    ai::raidcons::MainHand mainHand;
    if (Item* const equipped = category == Category::Weapon ? MainHandOf(bot) : nullptr)
    {
        ItemTemplate const* const proto = equipped->GetTemplate();
        mainHand.weapon = proto && proto->GetClass() == ITEM_CLASS_WEAPON;
        mainHand.subClass = proto ? proto->GetSubClass() : 0;
    }
    ai::raidcons::RowView const* row = ai::raidcons::DueFreeCastRow(role, category, bot->GetLevel(), heldAuras,
        weaponHeld, mainHand, indexViews.data(), indexViews.size());
    if (!row)
        return false;

    if (category == Category::Weapon)
    {
        Item* const mainHand = MainHandOf(bot);
        if (!mainHand)
            return false;
        SpellCastResult const result = bot->CastSpell(CastSpellTargetArg(mainHand), row->spell, CastSpellExtraArgs(true));
        if (result != SPELL_CAST_OK)
            LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots raid consumables: {}: the core refused the free {} cast of item {} ({})", bot->GetName(), ai::raidcons::CategoryWord(category), uint32(row->item), uint32(result));
        return result == SPELL_CAST_OK;
    }

    SpellCastResult const result = bot->CastSpell(CastSpellTargetArg(static_cast<WorldObject*>(bot)), row->spell, CastSpellExtraArgs(true));
    if (result != SPELL_CAST_OK)
        LogAt("tortoisebots", LOG_LEVEL_WARN, "TortoiseBots raid consumables: {}: the core refused the free {} cast of item {} ({})", bot->GetName(), ai::raidcons::CategoryWord(category), uint32(row->item), uint32(result));
    return result == SPELL_CAST_OK;
}

bool Held(Player* bot, ai::raidcons::Category category, std::uint32_t& auraId)
{
    auraId = 0;
    if (!bot)
        return false;
    if (category == Category::Weapon)
        return TableWeaponEnchant(bot) != 0;
    // The compiled table, not the checked index: a bot's auras are a fact even while the feature is off (the index needs
    // the spell mirror, which a module that starts off leaves down, and `.tb state` still says what the bot carries). The
    // weapon slot is the exception: which enchantments count is the table's own answer, so it needs the checked index.
    for (std::size_t i = 0; i < tbdata::raidcons::kConsumableCount; ++i)
    {
        tbdata::raidcons::ConsumableRow const& source = tbdata::raidcons::kConsumables[i];
        Category rowCategory = Category::Count;
        if (!ai::raidcons::CategoryFromWord(source.category, rowCategory) || rowCategory != category || source.aura == 0)
            continue;
        if (bot->HasAura(source.aura))
        {
            auraId = source.aura;
            return true;
        }
    }
    return false;
}

std::vector<std::string> MissingCategories(Player* bot)
{
    std::vector<std::string> missing;
    if (!Enabled() || !bot)
        return missing;
    if (!MapAllows(bot) || !bot->IsAlive() || bot->IsInCombat())
        return missing;
    Role role = Role::Count;
    if (!RoleOf(bot, role))
        return missing;
    EnsureIndex();

    std::vector<std::uint32_t> heldAuras;
    for (Row const& row : rows)
        if (row.aura != 0 && bot->HasAura(row.aura))
            heldAuras.push_back(row.aura);

    for (std::uint8_t id : ai::raidcons::DueCategories(role, heldAuras, TableWeaponEnchant(bot) != 0, bot->GetLevel(), indexViews.data(), indexViews.size()))
        missing.push_back(ai::raidcons::CategoryWord(static_cast<Category>(id)));
    return missing;
}

std::vector<std::string> NotCoveredCategories(Player* bot)
{
    std::vector<std::string> uncovered;
    if (!Enabled() || !bot)
        return uncovered;
    Role role = Role::Count;
    if (!RoleOf(bot, role))
        return uncovered;

    std::vector<ai::raidcons::NotCovered> declared;
    declared.reserve(tbdata::raidcons::kNotCoveredCount);
    for (std::size_t i = 0; i < tbdata::raidcons::kNotCoveredCount; ++i)
    {
        ai::raidcons::NotCovered gap;
        if (!ai::raidcons::RoleFromWord(tbdata::raidcons::kNotCovered[i].role, gap.role) ||
            !ai::raidcons::CategoryFromWord(tbdata::raidcons::kNotCovered[i].category, gap.category))
            continue;
        declared.push_back(gap);
    }
    for (std::uint8_t id : ai::raidcons::NotCoveredCategories(role, declared.data(), declared.size()))
        uncovered.push_back(ai::raidcons::CategoryWord(static_cast<Category>(id)));
    return uncovered;
}
} // namespace raidcons
} // namespace TortoiseBots
