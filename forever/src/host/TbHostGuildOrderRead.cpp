#include "host/TbHostGuildOrderRead.h"

#include "TbBotMgr.h"
#include "compat/TbItemPrototype.h"
#include "host/TbH3.h"
#include "host/TbHostAuction.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostProfession.h"
#include "host/TbHostTown.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "compat/TbCompat_S5.h"
#include "GameObject.h"
#include "ItemTemplate.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include "playerbot/policy/profession/ProfessionPickPolicy.h"
#include "playerbot/GuildOrderEnchants.h"
#include "runtime/GuildOrderCategoryPolicy.h"
#include "runtime/GuildOrderEnchantPolicy.h"
#include "runtime/GuildOrderReadPolicy.h"
#include "runtime/GuildOrderSearchPolicy.h"
#include "runtime/GuildOrderStepHistoryPolicy.h"
#include "runtime/GuildOrderWhoSearchPolicy.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{
namespace h3
{
namespace guild_read
{

namespace
{
using namespace TortoiseBots::GuildOrders;

constexpr float kPreviewAuctionRangeYd = 300.0f;

std::string LowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

bool ParseUint(std::string const& token, uint32_t& out)
{
    out = 0;
    if (token.empty() || token.size() > 10)
        return false;
    for (char c : token)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    unsigned long const parsed = std::strtoul(token.c_str(), nullptr, 10);
    if (parsed > 0xFFFFFFFFul)
        return false;
    out = static_cast<uint32_t>(parsed);
    return true;
}

std::string JoinWords(Words const& words, std::size_t from)
{
    std::string out;
    for (std::size_t i = from; i < words.w.size(); ++i)
    {
        if (!out.empty())
            out += ' ';
        out += words.w[i];
    }
    return out;
}

uint32_t MailItemCount(Player* bot, uint32_t itemId)
{
    if (!bot || !itemId)
        return 0;
    time_t const now = std::time(nullptr);
    uint32_t total = 0;
    for (tbhost::town::MailView const& mail : tbhost::town::GetMails(bot))
    {
        if (mail.state == 2 || now < mail.deliverTime)
            continue;
        for (auto const& entry : mail.items)
            if (entry.second == itemId)
                ++total;
    }
    return total;
}

uint32_t HeldCount(Player* bot, uint32_t itemId)
{
    if (!bot || !itemId)
        return 0;
    return tbhost::guild_orders::BotBagItemCount(bot, itemId) + MailItemCount(bot, itemId);
}

FarmSkill BotFarmSkill(Player* bot, uint32_t skillId)
{
    FarmSkill skill;
    if (!bot || !skillId)
        return skill;
    skill.has = bot->HasSkill(skillId);
    skill.value = skill.has ? uint32_t(bot->GetSkillValue(skillId)) : 0;
    return skill;
}

// ---- lazy item index: what a guild order can be given for ----

struct RecipeFact
{
    uint32_t spellId = 0;
    uint32_t skillId = 0;
    uint32_t reqSkill = 0;
};

struct ItemRow
{
    uint32_t itemId = 0;
    std::string name;
    std::string lower;
    uint32_t quality = 0;
    uint32_t itemClass = 0;
    bool material = false;
    bool craftable = false;
    uint8_t sources = 0; // SourceHerb | SourceOre
    uint16_t herbReq = 0;
    uint16_t oreReq = 0;
    OrderItemCategory category = OrderItemCategory::Other;
};

struct GatherReq
{
    uint16_t herbReq = 0;
    uint16_t oreReq = 0;
};

struct ItemIndex
{
    std::vector<ItemRow> rows;
    std::unordered_map<uint32_t, std::vector<RecipeFact>> recipesByItem;
};

struct LootItems
{
    std::vector<uint32_t> items;
    std::vector<uint32_t> refs;
};

using LootMap = std::unordered_map<uint32_t, LootItems>;

void LoadLootTable(char const* tableName, LootMap& out)
{
    std::string const sql = std::string("SELECT `Entry`, `ItemType`, `Item` FROM `") + tableName + "`";
    bool schemaMissing = false;
    QueryResult result = WorldQueryCompat(sql.c_str(), &schemaMissing);
    if (schemaMissing || !result)
        return;
    do
    {
        Field* fields = result->Fetch();
        uint32_t const lootId = fields[0].GetUInt32();
        int32_t const itemType = fields[1].GetInt8();
        uint32_t const item = fields[2].GetUInt32();
        if (itemType == 0 && item)
            out[lootId].items.push_back(item);
        else if (itemType == 1 && item)
            out[lootId].refs.push_back(item);
    } while (result->NextRow());
}

void CollectRefItems(LootMap const& refs, uint32_t refId, int depth, std::unordered_set<uint32_t>& visited,
    std::unordered_set<uint32_t>& items)
{
    if (!refId || depth <= 0 || !visited.insert(refId).second)
        return;
    auto const found = refs.find(refId);
    if (found == refs.end())
        return;
    for (uint32_t item : found->second.items)
        if (item)
            items.insert(item);
    for (uint32_t nested : found->second.refs)
        CollectRefItems(refs, nested, depth - 1, visited, items);
}

void CollectLootItems(LootMap const& loot, LootMap const& refs, uint32_t lootId, int depth,
    std::unordered_set<uint32_t>& visited, std::unordered_set<uint32_t>& items)
{
    if (!lootId || depth <= 0 || !visited.insert(lootId).second)
        return;
    auto const found = loot.find(lootId);
    if (found == loot.end())
        return;
    for (uint32_t item : found->second.items)
        if (item)
            items.insert(item);
    for (uint32_t refId : found->second.refs)
        CollectRefItems(refs, refId, depth, visited, items);
}

// Herbs and ore from chest nodes: the two node kinds the farm order's own
// gather search accepts (TbHostProfession.cpp FindBestGatherNodeInSight).
std::unordered_map<uint32_t, GatherReq> BuildGatherIndex()
{
    std::unordered_map<uint32_t, GatherReq> gathers;
    LootMap loot;
    LootMap refs;
    LoadLootTable("gameobject_loot_template", loot);
    LoadLootTable("reference_loot_template", refs);
    if (loot.empty())
        return gathers;

    for (auto const& [entry, tpl] : sObjectMgr->GetGameObjectTemplates())
    {
        (void)entry;
        if (tpl.type != GAMEOBJECT_TYPE_CHEST)
            continue;
        uint32_t const lootId = tpl.GetLootId();
        if (!lootId)
            continue;
        LockEntry const* lock = sLockStore.LookupEntry(tpl.GetLockId());
        if (!lock)
            continue;
        uint32_t skillId = 0;
        uint16_t reqSkill = 0;
        for (uint32_t i = 0; i < MAX_LOCK_CASE; ++i)
        {
            if (lock->Type[i] != LOCK_KEY_SKILL)
                continue;
            skillId = uint32_t(SkillByLockType(LockType(lock->Index[i])));
            reqSkill = uint16_t(std::max<uint32_t>(1, lock->Skill[i]));
            break;
        }
        if (skillId != TortoiseBots::prof::SkillMining && skillId != TortoiseBots::prof::SkillHerbalism)
            continue;
        std::unordered_set<uint32_t> visited;
        std::unordered_set<uint32_t> items;
        CollectLootItems(loot, refs, lootId, 4, visited, items);
        for (uint32_t item : items)
        {
            if (!item)
                continue;
            GatherReq& req = gathers[item];
            uint16_t& slot = (skillId == TortoiseBots::prof::SkillHerbalism) ? req.herbReq : req.oreReq;
            if (slot == 0 || reqSkill < slot)
                slot = reqSkill;
        }
    }
    return gathers;
}

// Recipes of the professions a guild order can craft: the same skill list the
// bot's own craft path walks (TbHostProfession.cpp GetCraftReagentCache).
std::unordered_map<uint32_t, std::vector<RecipeFact>> BuildRecipeIndex()
{
    std::unordered_map<uint32_t, std::vector<RecipeFact>> byItem;
    static uint32_t const kCraftSkills[] = {
        TortoiseBots::prof::SkillBlacksmithing,
        TortoiseBots::prof::SkillLeatherworking,
        TortoiseBots::prof::SkillAlchemy,
        TortoiseBots::prof::SkillTailoring,
        TortoiseBots::prof::SkillEngineering,
        TortoiseBots::prof::SkillEnchanting,
        TortoiseBots::prof::SkillFirstAid,
        TortoiseBots::prof::SkillCooking
    };
    for (uint32_t skillId : kCraftSkills)
    {
        auto const* abilities = sDB2Manager.GetSkillLineAbilitiesBySkill(skillId);
        if (!abilities)
            continue;
        for (SkillLineAbilityEntry const* ability : *abilities)
        {
            if (!ability || !ability->Spell)
                continue;
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(uint32_t(ability->Spell), DIFFICULTY_NONE);
            if (!spell)
                continue;
            uint32_t const reqSkill = ability->MinSkillLineRank > 0 ? uint32_t(ability->MinSkillLineRank) : 0;
            for (SpellEffectInfo const& effect : spell->GetEffects())
            {
                if (effect.Effect != SPELL_EFFECT_CREATE_ITEM || effect.ItemType == 0)
                    continue;
                std::vector<RecipeFact>& recipes = byItem[uint32_t(effect.ItemType)];
                auto const known = std::find_if(recipes.begin(), recipes.end(),
                    [&](RecipeFact const& recipe)
                    {
                        return recipe.spellId == spell->Id && recipe.skillId == skillId;
                    });
                if (known == recipes.end())
                    recipes.push_back({ spell->Id, skillId, reqSkill });
            }
        }
    }
    return byItem;
}

ItemIndex BuildIndex()
{
    ItemIndex index;
    index.recipesByItem = BuildRecipeIndex();
    std::unordered_map<uint32_t, GatherReq> const gathers = BuildGatherIndex();

    for (auto const& entry : sObjectMgr->GetItemTemplateStore())
    {
        uint32_t const itemId = entry.first;
        ItemPrototype const* proto = tbcompat::ItemProto(itemId);
        if (!proto || proto->Name1.empty())
            continue;
        OrderableFacts const facts = CheckOrderable(proto->Class, proto->Bonding, proto->Duration,
            (proto->Flags & kItemFlagConjured) != 0);
        if (!IsOrderableItem(facts) || facts.conjured)
            continue;

        ItemRow row;
        row.itemId = itemId;
        row.name = proto->Name1;
        row.lower = LowerCopy(row.name);
        if (row.lower.rfind("test ", 0) == 0)
            continue;
        row.quality = proto->Quality;
        row.itemClass = proto->Class;
        row.material = proto->Class == ITEM_CLASS_TRADE_GOODS || proto->Class == ITEM_CLASS_REAGENT;
        auto const recipe = index.recipesByItem.find(itemId);
        row.craftable = recipe != index.recipesByItem.end() && !recipe->second.empty();
        auto const gather = gathers.find(itemId);
        if (gather != gathers.end())
        {
            if (gather->second.herbReq)
                row.sources |= SourceHerb;
            if (gather->second.oreReq)
                row.sources |= SourceOre;
            row.herbReq = gather->second.herbReq;
            row.oreReq = gather->second.oreReq;
        }
        if (!row.sources && !row.craftable && !row.material)
            continue;
        row.category = ClassifyOrderCategory(SourcesToken(row.sources),
            IsTradeCloth(uint8_t(row.itemClass), row.lower));
        index.rows.push_back(std::move(row));
    }
    return index;
}

ItemIndex const& Index()
{
    static ItemIndex const index = BuildIndex();
    return index;
}

bool KnownRecipe(Player* bot, uint32_t itemId, RecipeFact& out, uint32_t& outBotSkill)
{
    outBotSkill = 0;
    if (!bot || !itemId)
        return false;
    auto const found = Index().recipesByItem.find(itemId);
    if (found == Index().recipesByItem.end())
        return false;
    RecipeFact const* best = nullptr;
    for (RecipeFact const& recipe : found->second)
    {
        if (!bot->HasSpell(recipe.spellId) || !bot->HasSkill(recipe.skillId))
            continue;
        if (!best || recipe.reqSkill < best->reqSkill)
            best = &recipe;
    }
    if (!best)
        return false;
    out = *best;
    outBotSkill = uint32_t(bot->GetSkillValue(best->skillId));
    return true;
}

struct BotVerdict
{
    OrderReason reason = OrderReason::NoSource;
    uint32_t skillId = 0;
    uint32_t skillValue = 0;
};

BotVerdict VerdictFor(Player* bot, OrderType type, ItemRow const& row)
{
    BotVerdict out;
    switch (type)
    {
        case OrderType::Farm:
        {
            FarmSkill const herb = BotFarmSkill(bot, TortoiseBots::prof::SkillHerbalism);
            FarmSkill const mining = BotFarmSkill(bot, TortoiseBots::prof::SkillMining);
            out.reason = FarmVerdict((row.sources & SourceHerb) != 0, row.herbReq,
                (row.sources & SourceOre) != 0, row.oreReq, herb, mining);
            if (out.reason == OrderReason::None)
            {
                bool const herbServes = (row.sources & SourceHerb) && herb.has && herb.value >= row.herbReq;
                out.skillId = herbServes ? TortoiseBots::prof::SkillHerbalism : TortoiseBots::prof::SkillMining;
                out.skillValue = herbServes ? herb.value : mining.value;
            }
            return out;
        }
        case OrderType::Craft:
        {
            RecipeFact recipe;
            uint32_t botSkill = 0;
            bool const knows = KnownRecipe(bot, row.itemId, recipe, botSkill);
            out.reason = CraftVerdict(row.craftable, knows, botSkill, knows ? recipe.reqSkill : 0);
            out.skillId = knows ? recipe.skillId : 0;
            out.skillValue = botSkill;
            return out;
        }
        case OrderType::Buy:
            out.reason = OrderReason::None;
            return out;
        case OrderType::Enchant:
            break;
    }
    out.reason = OrderReason::NoSource;
    return out;
}

std::string VerdictToken(OrderReason reason)
{
    return reason == OrderReason::None ? std::string("ok") : std::string(ReasonToken(reason));
}

std::string PlanSourcesToken(OrderType type, ItemRow const& row)
{
    switch (type)
    {
        case OrderType::Farm:  return SourcesToken(row.sources);
        case OrderType::Craft: return "craft";
        case OrderType::Buy:   return "ah";
        case OrderType::Enchant: break;
    }
    return "-";
}

std::vector<std::pair<uint32_t, uint32_t>> RecipeReagents(uint32_t spellId)
{
    std::vector<std::pair<uint32_t, uint32_t>> out;
    SpellInfo const* spell = spellId ? sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE) : nullptr;
    if (!spell)
        return out;
    for (uint32_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (spell->Reagent[i] > 0 && spell->ReagentCount[i] > 0)
            out.emplace_back(uint32_t(spell->Reagent[i]), uint32_t(spell->ReagentCount[i]));
    }
    return out;
}

bool ParseTypeToken(std::string const& token, OrderType& out)
{
    std::string const lower = LowerCopy(token);
    if (lower == "farm")  { out = OrderType::Farm;  return true; }
    if (lower == "craft") { out = OrderType::Craft; return true; }
    if (lower == "buy")   { out = OrderType::Buy;   return true; }
    if (lower == "enchant") { out = OrderType::Enchant; return true; }
    return false;
}

bool RefuseType(ChatHandler* handler, Words const& words, std::string const& token)
{
    return Fail(handler, words, "guildorder", "bad-type", token);
}

// ---- enchant catalogue reads (search, who, preview) ----

std::vector<EnchantEntry> const& EnchantCatalogue()
{
    static std::vector<EnchantEntry> const catalogue = []
    {
        ai::BuildGuildOrderEnchantIndex();
        return ai::GuildOrderEnchants();
    }();
    return catalogue;
}

EnchantEntry const* EnchantEntryFor(uint32_t spellId)
{
    for (EnchantEntry const& entry : EnchantCatalogue())
        if (entry.spellId == spellId)
            return &entry;
    return nullptr;
}

uint32_t EnchantSkill(Player* bot)
{
    if (!bot || !bot->HasSkill(TortoiseBots::prof::SkillEnchanting))
        return 0;
    return uint32_t(bot->GetSkillValue(TortoiseBots::prof::SkillEnchanting));
}

// Per-bot reagent rows: item, one cast's need, what the bot holds (bags plus
// mail, like the search shows).
std::vector<std::vector<std::string>> EnchantReagentRows(Player* bot, EnchantEntry const& entry)
{
    std::vector<std::vector<std::string>> rows;
    for (auto const& [itemId, need] : entry.reagents)
        rows.push_back({ std::to_string(itemId), std::to_string(need),
            std::to_string(HeldCount(bot, itemId)) });
    return rows;
}

uint32_t EnchantReagentsHeldInFull(Player* bot, EnchantEntry const& entry)
{
    uint32_t have = 0;
    for (auto const& [itemId, need] : entry.reagents)
        if (HeldCount(bot, itemId) >= need)
            ++have;
    return have;
}

// A player may read the orders of the guild the bot is in; the console and a
// GM without a character may read any. The officer-note right is required to
// change an order, not to look at one.
bool CheckGuildRead(ChatHandler* handler, Words const& words, Player* sender, uint32_t guildId)
{
    if (!sender)
        return true;
    if (!guildId)
        return Fail(handler, words, "guildorder", "not-in-guild", "that bot has no guild order");
    if (sender->GetGuildId() != guildId)
        return Fail(handler, words, "guildorder", "wrong-guild", "the bot is not in your guild");
    return true;
}

// ---- search ----

bool HandleSearch(ChatHandler* handler, Words const& words, Player* sender)
{
    if (words.w.size() < 3)
        return Fail(handler, words, "guildorder", "usage", "search <bot> <farm|craft|buy|enchant> [cat=<kind>] <text>");
    Player* bot = FindOnline(words.w[1]);
    if (!bot || !IsHostBot(bot))
        return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
    uint64_t const guildId = tbhost::guild::GetGuildId(bot);
    if (!guildId)
        return Fail(handler, words, "guildorder", "not-in-guild", bot->GetName());
    if (!CheckGuildRead(handler, words, sender, uint32_t(guildId)))
        return false;

    OrderType type = OrderType::Farm;
    if (!ParseTypeToken(words.w[2], type))
        return RefuseType(handler, words, words.w[2]);

    if (type == OrderType::Enchant)
    {
        std::string const query = JoinWords(words, 3);
        if (query.size() < kSearchMinChars)
            return Fail(handler, words, "guildorder", "usage", "search text needs at least 3 characters");
        std::vector<std::string> const queryWords = SplitQueryWords(LowerCopy(query));
        uint32_t const skill = EnchantSkill(bot);

        struct EnchantHit
        {
            EnchantEntry const* entry = nullptr;
            OrderReason verdict = OrderReason::NoRecipe;
        };
        std::vector<EnchantHit> hits;
        for (EnchantEntry const& entry : EnchantCatalogue())
        {
            if (!EnchantSearchMatches(entry, queryWords))
                continue;
            bool const known = bot->HasSpell(entry.spellId);
            if (!known)
                continue;
            hits.push_back(EnchantHit{ &entry, EnchantSearchVerdict(known, skill, entry.reqSkill) });
        }
        if (hits.size() > kSearchMaxRows)
            hits.resize(kSearchMaxRows);

        Reply reply(handler, words, "guildorder");
        reply.Begin();
        for (EnchantHit const& hit : hits)
        {
            Rec rec("orderitem");
            rec.S("bot", bot->GetName()).N("spell", hit.entry->spellId).S("name", hit.entry->name)
                .S("slot", EnchantSlotToken(hit.entry->slot)).N("reqskill", hit.entry->reqSkill)
                .S("verdict", EnchantVerdictToken(hit.verdict))
                .P("skill", { std::to_string(TortoiseBots::prof::SkillEnchanting), std::to_string(skill) })
                .LP("reagents", EnchantReagentRows(bot, *hit.entry));
            reply.Line(rec);
        }
        Rec end("guildorder-end");
        end.S("bot", bot->GetName()).N("found", uint32_t(hits.size()));
        reply.End(end);
        return true;
    }

    std::string query = JoinWords(words, 3);
    OrderItemCategory category = OrderItemCategory::Other;
    CategoryArgParse const catParse = StripCategoryArg(query, category);
    if (catParse == CategoryArgParse::Bad)
        return Fail(handler, words, "guildorder", "bad-kind", "use herb, ore, leather, fish, cloth, drop or other");
    bool const hasCategory = catParse == CategoryArgParse::Ok;
    if (query.empty())
    {
        if (hasCategory && !IsBrowsableCategory(category))
            return Fail(handler, words, "guildorder", "usage", "that kind is too big to browse; add at least 3 characters");
    }
    else if (query.size() < kSearchMinChars)
        return Fail(handler, words, "guildorder", "usage", "search text needs at least 3 characters");

    std::vector<std::string> const queryWords = SplitQueryWords(LowerCopy(query));
    std::string const needle = NormalizeQueryWords(queryWords);

    struct Hit
    {
        ItemRow const* row = nullptr;
        uint32_t rank = 3;
        BotVerdict verdict;
    };
    std::vector<Hit> hits;
    for (ItemRow const& row : Index().rows)
    {
        if (hasCategory && row.category != category)
            continue;
        OrderItemFacts const facts{ row.material, row.craftable, row.sources };
        if (!OrderSearchShows(type, facts))
            continue;
        if (!queryWords.empty() && !SearchNameMatches(row.lower, queryWords))
            continue;
        Hit hit;
        hit.row = &row;
        hit.rank = queryWords.empty() ? 1u : SearchNameRank(row.lower, needle);
        hit.verdict = VerdictFor(bot, type, row);
        hits.push_back(hit);
    }
    std::sort(hits.begin(), hits.end(), [](Hit const& a, Hit const& b)
    {
        return SearchOrderLess({ a.rank, a.row->name, a.verdict.reason == OrderReason::None },
                               { b.rank, b.row->name, b.verdict.reason == OrderReason::None });
    });
    if (hits.size() > kSearchMaxRows)
        hits.resize(kSearchMaxRows);

    Reply reply(handler, words, "guildorder");
    reply.Begin();
    for (Hit const& hit : hits)
    {
        Rec rec("orderitem");
        rec.S("bot", bot->GetName()).N("item", hit.row->itemId).S("name", hit.row->name)
            .N("quality", hit.row->quality).S("type", TypeToken(type))
            .S("sources", PlanSourcesToken(type, *hit.row))
            .S("verdict", VerdictToken(hit.verdict.reason))
            .S("cat", CategoryToken(hit.row->category));
        if (hit.verdict.skillId)
            rec.P("skill", { std::to_string(hit.verdict.skillId), std::to_string(hit.verdict.skillValue) });
        reply.Line(rec);
    }
    Rec end("guildorder-end");
    end.S("bot", bot->GetName()).N("found", uint32_t(hits.size()));
    reply.End(end);
    return true;
}

// ---- who ----

struct GuildBotView
{
    Player* bot = nullptr;
    std::string name;
    bool busy = false;
};

std::vector<GuildBotView> GuildBots(uint32_t guildId)
{
    std::vector<GuildBotView> out;
    for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld() || bot->GetGuildId() != guildId)
            continue;
        std::optional<OrderRecord> const order = tbhost::guild_orders::FindOrder(guid.GetCounter());
        GuildBotView view;
        view.bot = bot;
        view.name = bot->GetName();
        view.busy = OrderBotBusy(order.has_value(), order ? order->cancelled : false,
            order ? order->state : OrderState::Working);
        out.push_back(std::move(view));
    }
    std::sort(out.begin(), out.end(), [](GuildBotView const& a, GuildBotView const& b)
    {
        return a.name < b.name;
    });
    return out;
}

bool HandleWho(ChatHandler* handler, Words const& words, Player* sender)
{
    if (words.w.size() < 2)
        return Fail(handler, words, "guildorder", "usage", "who [<bot|guild_id>] <craft|farm|enchant> <text>");

    std::size_t pos = 1;
    uint32_t guildId = 0;
    OrderType type = OrderType::Craft;
    if (ParseTypeToken(words.w[pos], type))
    {
        if (!sender || !sender->GetGuildId())
            return Fail(handler, words, "guildorder", "no-guild", "name a guild bot or guild id");
        guildId = uint32_t(tbhost::guild::GetGuildId(sender));
    }
    else
    {
        if (Player* bot = FindOnline(words.w[pos]))
            guildId = uint32_t(tbhost::guild::GetGuildId(bot));
        else
            ParseUint(words.w[pos], guildId);
        if (!guildId)
            return Fail(handler, words, "guildorder", "no-guild", "name a guild bot or guild id");
        ++pos;
        if (words.w.size() <= pos)
            return Fail(handler, words, "guildorder", "usage", "who [<bot|guild_id>] <craft|farm|enchant> <text>");
        if (!ParseTypeToken(words.w[pos], type))
            return RefuseType(handler, words, words.w[pos]);
    }
    if (!CheckGuildRead(handler, words, sender, guildId))
        return false;

    ++pos;
    std::string const query = JoinWords(words, pos);
    if (query.size() < kSearchMinChars)
        return Fail(handler, words, "guildorder", "usage", "search text needs at least 3 characters");
    std::vector<std::string> const queryWords = SplitQueryWords(LowerCopy(query));
    std::string const needle = NormalizeQueryWords(queryWords);

    std::vector<GuildBotView> const bots = GuildBots(guildId);
    if (bots.empty())
        return Fail(handler, words, "guildorder", "no-bots", "no online guild bot");

    if (type == OrderType::Enchant)
    {
        struct EnchantHit
        {
            EnchantEntry const* entry = nullptr;
            std::vector<WhoBot> bots;
        };
        std::vector<EnchantHit> hits;
        for (EnchantEntry const& entry : EnchantCatalogue())
        {
            if (!EnchantSearchMatches(entry, queryWords))
                continue;
            EnchantHit hit;
            hit.entry = &entry;
            for (GuildBotView const& view : bots)
            {
                uint32_t const skill = EnchantSkill(view.bot);
                if (!view.bot->HasSpell(entry.spellId) ||
                    (entry.reqSkill > 0 && skill < entry.reqSkill))
                    continue;
                WhoBot who;
                who.name = view.name;
                who.skill = skill;
                who.have = EnchantReagentsHeldInFull(view.bot, entry);
                who.need = uint32_t(entry.reagents.size());
                who.busy = view.busy;
                hit.bots.push_back(std::move(who));
            }
            if (!hit.bots.empty())
                hits.push_back(std::move(hit));
        }
        if (hits.size() > kWhoMaxRows)
            hits.resize(kWhoMaxRows);

        Reply reply(handler, words, "guildorder");
        reply.Begin();
        for (EnchantHit& hit : hits)
        {
            SortWhoBots(hit.bots);
            std::vector<std::vector<std::string>> parts;
            parts.reserve(hit.bots.size());
            for (WhoBot const& who : hit.bots)
                parts.push_back({ who.name, std::to_string(who.skill), std::to_string(who.have),
                    std::to_string(who.need), who.busy ? "yes" : "no" });
            Rec rec("orderwho");
            rec.N("guild", guildId).N("spell", hit.entry->spellId).S("name", hit.entry->name)
                .S("type", "enchant").S("slot", EnchantSlotToken(hit.entry->slot))
                .N("reqskill", hit.entry->reqSkill).LP("bots", parts);
            reply.Line(rec);
        }
        Rec end("guildorder-end");
        end.N("guild", guildId).N("found", uint32_t(hits.size()));
        reply.End(end);
        return true;
    }

    struct Hit
    {
        ItemRow const* row = nullptr;
        uint32_t rank = 3;
        std::vector<WhoBot> bots;
    };
    std::vector<Hit> hits;
    for (ItemRow const& row : Index().rows)
    {
        if (type == OrderType::Farm)
        {
            if (!row.sources)
                continue;
        }
        else if (!row.craftable)
            continue;
        if (!SearchNameMatches(row.lower, queryWords))
            continue;

        Hit hit;
        hit.row = &row;
        hit.rank = SearchNameRank(row.lower, needle);
        for (GuildBotView const& view : bots)
        {
            if (type == OrderType::Farm)
            {
                FarmSkill const herb = BotFarmSkill(view.bot, TortoiseBots::prof::SkillHerbalism);
                FarmSkill const mining = BotFarmSkill(view.bot, TortoiseBots::prof::SkillMining);
                bool const serves = FarmVerdict((row.sources & SourceHerb) != 0, row.herbReq,
                    (row.sources & SourceOre) != 0, row.oreReq, herb, mining) == OrderReason::None;
                if (!serves)
                    continue;
                bool const byHerb = (row.sources & SourceHerb) && herb.has && herb.value >= row.herbReq;
                WhoBot who;
                who.name = view.name;
                who.skill = byHerb ? herb.value : mining.value;
                who.busy = view.busy;
                hit.bots.push_back(std::move(who));
                continue;
            }
            RecipeFact recipe;
            uint32_t botSkill = 0;
            if (!KnownRecipe(view.bot, row.itemId, recipe, botSkill))
                continue;
            std::vector<WhoReagent> reagents;
            for (auto const& reagent : RecipeReagents(recipe.spellId))
                reagents.push_back({ reagent.second, HeldCount(view.bot, reagent.first) });
            WhoBot who;
            who.name = view.name;
            who.skill = botSkill;
            CountWhoReagents(reagents, who.have, who.need);
            who.busy = view.busy;
            hit.bots.push_back(std::move(who));
        }
        if (!hit.bots.empty())
            hits.push_back(std::move(hit));
    }
    std::sort(hits.begin(), hits.end(), [](Hit const& a, Hit const& b)
    {
        return SearchOrderLess({ a.rank, a.row->name, true }, { b.rank, b.row->name, true });
    });
    if (hits.size() > kWhoMaxRows)
        hits.resize(kWhoMaxRows);

    Reply reply(handler, words, "guildorder");
    reply.Begin();
    for (Hit& hit : hits)
    {
        SortWhoBots(hit.bots);
        std::vector<std::vector<std::string>> parts;
        parts.reserve(hit.bots.size());
        for (WhoBot const& who : hit.bots)
        {
            parts.push_back({ who.name, std::to_string(who.skill), std::to_string(who.have),
                std::to_string(who.need), who.busy ? "yes" : "no" });
        }
        Rec rec("orderwho");
        rec.N("guild", guildId).N("item", hit.row->itemId).S("name", hit.row->name)
            .S("type", TypeToken(type)).N("quality", hit.row->quality)
            .S("cat", CategoryToken(hit.row->category))
            .LP("bots", parts);
        reply.Line(rec);
    }
    Rec end("guildorder-end");
    end.N("guild", guildId).N("found", uint32_t(hits.size()));
    reply.End(end);
    return true;
}

// ---- preview ----

ItemRow const* IndexedItem(uint32_t itemId)
{
    for (ItemRow const& row : Index().rows)
        if (row.itemId == itemId)
            return &row;
    return nullptr;
}

bool HandlePreview(ChatHandler* handler, Words const& words, Player* sender)
{
    if (words.w.size() < 4)
        return Fail(handler, words, "guildorder", "usage",
            "preview <bot> <farm|craft|buy> <item> <amount> | preview <bot> enchant <spell link or id>");
    Player* bot = FindOnline(words.w[1]);
    if (!bot || !IsHostBot(bot))
        return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
    uint64_t const guildId = tbhost::guild::GetGuildId(bot);
    if (!guildId)
        return Fail(handler, words, "guildorder", "not-in-guild", bot->GetName());
    if (!CheckGuildRead(handler, words, sender, uint32_t(guildId)))
        return false;

    OrderType type = OrderType::Farm;
    if (!ParseTypeToken(words.w[2], type))
        return RefuseType(handler, words, words.w[2]);
    if (!PreviewTypeSupported(type))
        return Fail(handler, words, "guildorder", "bad-type", words.w[2]);

    if (type == OrderType::Enchant)
    {
        if (words.w.size() != 4)
            return Fail(handler, words, "guildorder", "unexpected-argument", words.w[4]);
        uint32_t spellId = 0;
        if (!ParseEnchantSpellToken(words.w[3], spellId))
            return Fail(handler, words, "guildorder", "bad-spell", words.w[3]);
        EnchantEntry const* entry = EnchantEntryFor(spellId);
        if (!entry)
            return Fail(handler, words, "guildorder", "bad-spell", words.w[3]);

        uint32_t const skill = EnchantSkill(bot);
        bool const known = bot->HasSpell(spellId);
        OrderReason const verdict = EnchantSearchVerdict(known, skill, entry->reqSkill);
        std::optional<OrderRecord> const existing = tbhost::guild_orders::FindOrder(bot->GetGUID().GetCounter());
        bool const replaces = existing.has_value() && !existing->cancelled;
        bool const busy = existing.has_value() && !existing->cancelled && existing->type == OrderType::Enchant &&
            !IsFinished(existing->state) &&
            (!sender || existing->giverGuidLow != sender->GetGUID().GetCounter());

        Reply reply(handler, words, "guildorder");
        reply.Begin();
        Rec rec("orderpreview");
        rec.S("bot", bot->GetName()).S("type", "enchant").N("spell", spellId)
            .S("name", entry->name).S("slot", EnchantSlotToken(entry->slot))
            .N("reqskill", entry->reqSkill).N("want", 1)
            .S("verdict", EnchantVerdictToken(verdict))
            .P("skill", { std::to_string(TortoiseBots::prof::SkillEnchanting), std::to_string(skill) })
            .LP("reagents", EnchantReagentRows(bot, *entry))
            .B("busy", busy).B("replaces", replaces);
        reply.Line(rec);
        Rec end("guildorder-end");
        end.S("bot", bot->GetName()).N("spell", spellId);
        reply.End(end);
        return true;
    }

    if (words.w.size() < 5)
        return Fail(handler, words, "guildorder", "usage", "preview <bot> <farm|craft|buy> <item> <amount>");

    uint32_t itemId = 0;
    if (!ParseUint(words.w[3], itemId) || !tbcompat::ItemProto(itemId))
        return Fail(handler, words, "guildorder", "bad-item", words.w[3]);
    uint32_t amount = 0;
    if (!ParseUint(words.w[4], amount) || !amount)
        return Fail(handler, words, "guildorder", "bad-amount", words.w[4]);

    ItemPrototype const* proto = tbcompat::ItemProto(itemId);
    OrderableFacts orderable = CheckOrderable(proto->Class, proto->Bonding, proto->Duration,
        (proto->Flags & kItemFlagConjured) != 0);
    orderable.named = !proto->Name1.empty();
    uint32_t const bags = tbhost::guild_orders::BotBagItemCount(bot, itemId);
    uint32_t const mail = MailItemCount(bot, itemId);
    uint32_t const held = bags + mail;
    uint32_t const shortfall = held >= amount ? 0 : amount - held;
    uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    uint32_t const giverGuidLow = sender ? sender->GetGUID().GetCounter() : botGuidLow;

    ItemRow const* indexed = IndexedItem(itemId);
    BotVerdict verdict;
    if (indexed)
        verdict = VerdictFor(bot, type, *indexed);
    else if (type == OrderType::Craft)
        verdict.reason = OrderReason::NoRecipe;
    else if (type == OrderType::Farm)
        verdict.reason = OrderReason::NoSource;
    else
        verdict.reason = OrderReason::None;

    std::optional<OrderRecord> const existing = tbhost::guild_orders::FindOrder(botGuidLow);
    bool const replaces = existing.has_value() && !existing->cancelled;

    Reply reply(handler, words, "guildorder");
    reply.Begin();
    Rec rec("orderpreview");
    rec.S("bot", bot->GetName()).S("type", TypeToken(type)).N("item", itemId).S("name", proto->Name1)
        .N("quality", proto->Quality).N("want", amount).N("bags", bags).N("mail", mail)
        .N("have", held).N("short", shortfall)
        .S("verdict", VerdictToken(verdict.reason))
        .B("bank", tbhost::guild_orders::Delivery().CanDeliverToBank(botGuidLow, guildId))
        .B("mailto", tbhost::guild_orders::Delivery().CanDeliverByMail(botGuidLow, giverGuidLow))
        .B("bound", orderable.bound).B("quest", orderable.quest).B("timed", orderable.timed)
        .B("conjured", orderable.conjured)
        .B("handoff", IsOrderableItem(orderable) && !orderable.conjured)
        .B("replaces", replaces);

    if (type == OrderType::Farm)
    {
        rec.S("sources", indexed ? SourcesToken(indexed->sources) : "-");
        if (verdict.skillId)
            rec.P("skill", { std::to_string(verdict.skillId), std::to_string(verdict.skillValue) });
    }
    else if (type == OrderType::Craft)
    {
        RecipeFact recipe;
        uint32_t botSkill = 0;
        if (KnownRecipe(bot, itemId, recipe, botSkill))
        {
            rec.N("recipe", recipe.spellId)
                .P("skill", { std::to_string(recipe.skillId), std::to_string(botSkill), std::to_string(recipe.reqSkill) });
            std::vector<std::vector<std::string>> reagents;
            for (auto const& reagent : RecipeReagents(recipe.spellId))
            {
                reagents.push_back({ std::to_string(reagent.first), std::to_string(reagent.second),
                    std::to_string(HeldCount(bot, reagent.first)) });
            }
            if (!reagents.empty())
                rec.LP("reagents", reagents);
        }
    }
    else if (type == OrderType::Buy)
    {
        rec.N("money", int64_t(bot->GetMoney()));
        if (Creature* auctioneer = tbhost::ah::FindNearestFactionAuctioneer(bot, kPreviewAuctionRangeYd))
        {
            auto const listings = tbhost::ah::QueryMarketByItemId(bot, auctioneer, itemId);
            uint64_t cheapest = 0;
            uint32_t count = 0;
            for (tbhost::ah::AhListingView const& listing : listings)
            {
                if (listing.isOwn || listing.unitPrice == 0)
                    continue;
                ++count;
                if (!cheapest || listing.unitPrice < cheapest)
                    cheapest = listing.unitPrice;
            }
            rec.N("ah", count);
            if (cheapest)
                rec.N("price", int64_t(cheapest));
        }
    }
    reply.Line(rec);
    Rec end("guildorder-end");
    end.S("bot", bot->GetName()).N("item", itemId);
    reply.End(end);
    return true;
}

// ---- steps ----

struct StepHistoryState
{
    std::mutex mutex;
    std::unordered_map<uint32_t, OrderStepHistory> byBot;
    std::unordered_map<uint32_t, uint32_t> orderItemByBot;
};

StepHistoryState& StepHistory()
{
    static StepHistoryState state;
    return state;
}

uint32_t ResolveBotGuidLow(std::string const& name, std::string& outName)
{
    std::string normalized(name);
    if (!normalizePlayerName(normalized))
        return 0;
    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(normalized);
    if (guid.IsEmpty())
        return 0;
    std::string stored;
    outName = sCharacterCache->GetCharacterNameByGuid(guid, stored) ? stored : normalized;
    return guid.GetCounter();
}

bool HandleSteps(ChatHandler* handler, Words const& words, Player* sender)
{
    if (words.w.size() < 2)
        return Fail(handler, words, "guildorder", "usage", "steps <bot>");

    Player* bot = FindOnline(words.w[1]);
    uint32_t botGuidLow = 0;
    std::string botName;
    uint32_t guildId = 0;
    if (bot && IsHostBot(bot))
    {
        botGuidLow = bot->GetGUID().GetCounter();
        botName = bot->GetName();
        guildId = uint32_t(tbhost::guild::GetGuildId(bot));
    }
    else
    {
        botGuidLow = ResolveBotGuidLow(words.w[1], botName);
        if (!botGuidLow || !BotMgr::Instance().IsBot(ObjectGuid::Create<HighGuid::Player>(botGuidLow)))
            return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
        ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(botGuidLow);
        guildId = uint32_t(sCharacterCache->GetCharacterGuildIdByGuid(guid));
        if (!guildId)
        {
            std::optional<OrderRecord> const order = tbhost::guild_orders::FindOrder(botGuidLow);
            guildId = order ? order->guildId : 0;
        }
    }
    if (!CheckGuildRead(handler, words, sender, guildId))
        return false;

    std::vector<OrderStepView> const steps = OrderHistory(botGuidLow);
    uint32_t const dropped = DroppedOrderSteps(botGuidLow);

    Reply reply(handler, words, "guildorder");
    reply.Begin();
    for (OrderStepView const& step : steps)
    {
        Rec rec("orderstep");
        rec.S("bot", botName).N("age", step.ageSec)
            .S("state", StateToken(step.state))
            .S("reason", ReasonToken(step.reason))
            .S("step", SupplyStepToken(step.step));
        if (step.orderItemId)
            rec.N("order", step.orderItemId);
        if (step.legItemId)
            rec.N("leg", step.legItemId);
        reply.Line(rec);
    }
    Rec end("guildorder-end");
    end.S("bot", botName).N("steps", uint32_t(steps.size())).N("dropped", dropped);
    reply.End(end);
    return true;
}

} // namespace

bool IsReadVerb(std::string const& sub)
{
    return sub == "search" || sub == "who" || sub == "preview" || sub == "steps";
}

bool Handle(ChatHandler* handler, Words const& words, Player* sender, std::string const& sub)
{
    if (sub == "search")
        return HandleSearch(handler, words, sender);
    if (sub == "who")
        return HandleWho(handler, words, sender);
    if (sub == "preview")
        return HandlePreview(handler, words, sender);
    if (sub == "steps")
        return HandleSteps(handler, words, sender);
    return Fail(handler, words, "guildorder", "usage", "use: search|who|preview|steps");
}

// ---- step history ----

void NoteOrderHistory(OrderRecord const& record, bool fresh)
{
    if (!record.botGuidLow)
        return;
    OrderStepEntry entry;
    entry.atSec = uint64_t(std::time(nullptr));
    entry.state = record.state;
    entry.reason = record.reason;
    entry.step = record.supplyStep;
    entry.itemId = record.supplyItemId;

    StepHistoryState& state = StepHistory();
    std::lock_guard<std::mutex> lock(state.mutex);
    OrderStepHistory& history = state.byBot[record.botGuidLow];
    if (fresh)
        history = OrderStepHistory();
    state.orderItemByBot[record.botGuidLow] = record.itemId;
    NoteOrderStep(history, entry);
}

std::vector<OrderStepView> OrderHistory(uint32_t botGuidLow)
{
    std::vector<OrderStepView> out;
    if (!botGuidLow)
        return out;
    StepHistoryState& state = StepHistory();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto const found = state.byBot.find(botGuidLow);
    if (found == state.byBot.end())
        return out;
    uint64_t const now = uint64_t(std::time(nullptr));
    auto const orderItem = state.orderItemByBot.find(botGuidLow);
    uint32_t const orderItemId = orderItem == state.orderItemByBot.end() ? 0 : orderItem->second;
    for (OrderStepEntry const& entry : found->second.entries)
    {
        OrderStepView view;
        view.ageSec = (now > entry.atSec) ? uint32_t(now - entry.atSec) : 0u;
        view.state = entry.state;
        view.reason = entry.reason;
        view.step = entry.step;
        view.legItemId = entry.itemId;
        view.orderItemId = orderItemId;
        out.push_back(view);
    }
    return out;
}

uint32_t DroppedOrderSteps(uint32_t botGuidLow)
{
    if (!botGuidLow)
        return 0;
    StepHistoryState& state = StepHistory();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto const found = state.byBot.find(botGuidLow);
    return found == state.byBot.end() ? 0u : found->second.dropped;
}

void ForgetOrderHistory(uint32_t botGuidLow)
{
    if (!botGuidLow)
        return;
    StepHistoryState& state = StepHistory();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.byBot.erase(botGuidLow);
    state.orderItemByBot.erase(botGuidLow);
}

void ForgetAllOrderHistory()
{
    StepHistoryState& state = StepHistory();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.byBot.clear();
    state.orderItemByBot.clear();
}

} // namespace guild_read
} // namespace h3
} // namespace TortoiseBots
