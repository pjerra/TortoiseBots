// The guild-order window the addon speaks (TBF #155): the `order` verb over
// the TBM addon channel. Every answer is one of the shapes
// TortoiseBotsForeverManager parses (BotOrder.lua:57-75); the live store,
// permissions and read logic stay where the `.tb guildorder` verbs put them.
//
// Search, who, preview and steps replay the existing read handlers through a
// capturing ChatHandler and reshape their stable `.tb` records into TBM lines,
// so the two transports cannot drift. Mutations call the same store functions
// the `.tb` verbs call, with the same permission gates.
#include "host/TbAddonOrders.h"
#include "host/TbAddonOrdersPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/TbItemPrototype.h"
#include "host/TbAddonChannel.h"
#include "host/TbH3.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrderAccess.h"
#include "host/TbHostGuildOrderRead.h"
#include "host/TbHostGuildOrderReassign.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostTown.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "DB2Stores.h"
#include "Guild.h"
#include "ItemTemplate.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Util.h"

#include "runtime/GuildOrderBringPolicy.h"
#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderCraftPolicy.h"
#include "runtime/GuildOrderGiveUpPolicy.h"
#include "runtime/GuildOrderPlayerBuyPolicy.h"
#include "runtime/GuildOrderSourcesPolicy.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace addonorders
{
namespace
{

using GuildOrders::OrderRecord;
using GuildOrders::OrderType;

constexpr char const* kIntent = "order";

// The `.tb` reply grammar goes through ChatHandler::SendSysMessage (TbH3.cpp
// Reply::Send); a console handler with the override captures those lines, so
// the read verbs run unchanged and their permission checks stay authoritative.
class CaptureHandler final : public ChatHandler
{
public:
    CaptureHandler() : ChatHandler(nullptr) { }

    void SendSysMessage(std::string_view text, bool /*escapeCharacters*/) override
    {
        lines.emplace_back(text);
    }

    std::vector<std::string> lines;
};

void SendLines(Player* requester, std::vector<std::string> const& lines)
{
    for (std::string const& line : lines)
        addon::Send(requester, line);
}

std::uint32_t GuildOf(Player* player)
{
    return player ? static_cast<std::uint32_t>(tbhost::guild::GetGuildId(player)) : 0;
}

Player* FindOnlineHostBot(std::string const& name)
{
    std::string normalized(name);
    if (normalized.empty() || !normalizePlayerName(normalized))
        return nullptr;
    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(normalized);
    if (guid.IsEmpty())
        return nullptr;
    Player* bot = ObjectAccessor::FindPlayer(guid);
    if (!bot || !bot->IsInWorld() || !h3::IsHostBot(bot))
        return nullptr;
    return bot;
}

std::string BotName(std::uint32_t guidLow)
{
    std::string name;
    if (sCharacterCache->GetCharacterNameByGuid(ObjectGuid::Create<HighGuid::Player>(guidLow), name))
        return name;
    return "?";
}

std::string ItemName(std::uint32_t itemId)
{
    if (ItemPrototype const* proto = tbcompat::ItemProto(itemId))
        return proto->Name1;
    return "?";
}

std::string OrderItemName(OrderRecord const& order)
{
    if (order.type == OrderType::Enchant)
    {
        if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(order.itemId, DIFFICULTY_NONE))
            if (spell->SpellName)
                return (*spell->SpellName)[LOCALE_enUS];
    }
    return ItemName(order.itemId);
}

std::string ZoneName(std::uint32_t zoneId)
{
    if (!zoneId)
        return "-";
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId);
    if (!area)
        return "-";
    char const* name = area->AreaName[LOCALE_enUS];
    return name && *name ? name : "-";
}

std::string DifficultyFor(OrderRecord const& order)
{
    if (order.type != OrderType::Farm || order.farmChance == 0)
        return "-";
    return GuildOrders::DifficultyToken(GuildOrders::EstimateDifficulty(
        GuildOrders::ExpectedKills(order.amount, static_cast<float>(order.farmChance))));
}

std::uint32_t MailItemCount(Player* bot, std::uint32_t itemId)
{
    if (!bot || !itemId)
        return 0;
    std::uint32_t total = 0;
    for (tbhost::town::MailView const& mail : tbhost::town::GetMails(bot))
    {
        if (mail.state == 2)
            continue;
        for (auto const& [itemGuid, templateId] : mail.items)
            if (templateId == itemId)
                ++total;
    }
    return total;
}

std::uint32_t CarryOf(OrderRecord const& order)
{
    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(order.botGuidLow));
    bool const online = bot && bot->IsInWorld();
    bool const terminal = GuildOrders::IsTerminal(order.state);
    std::uint32_t const live = online && !terminal
        ? tbhost::guild_orders::BotBagItemCount(bot, order.itemId) + MailItemCount(bot, order.itemId)
        : 0;
    return GuildOrders::ListedCarry(terminal, online, live, order.reportedCarry);
}

std::string OrderLineFor(OrderRecord const& order)
{
    OrderView view;
    view.bot = BotName(order.botGuidLow);
    view.type = GuildOrders::TypeToken(order.type);
    view.itemId = order.itemId;
    view.item = OrderItemName(order);
    view.have = GuildOrders::ReportedHave(order.delivered, order.reportedHave);
    view.want = order.amount;
    view.state = GuildOrders::StateToken(order.state);
    view.reason = GuildOrders::ReasonToken(order.reason);
    view.zone = ZoneName(order.farmZoneId);
    if (order.type == OrderType::Buy && view.zone == "-")
        view.zone = "Auction House";
    view.difficulty = DifficultyFor(order);
    view.hours = GuildOrders::HoursToken(order.hours);
    view.keep = GuildOrders::KeepToken(order.keep);
    view.parent = order.parentBotGuidLow ? BotName(order.parentBotGuidLow) : "-";
    view.goldCap = order.hasGoldCap ? std::to_string(order.goldCap) : "-";
    view.step = GuildOrders::SupplyStepToken(order.supplyStep);
    view.need = GuildOrders::NeedItemsToken(order.needItems);
    view.carry = CarryOf(order);
    view.bring = GuildOrders::BringItemsToken(order.bringItems);
    return OrderLine(view);
}

AccessView AccessFor(std::uint32_t guildId, Player* requester)
{
    AccessView view;
    tbhost::guild_order_access::AccessReport report;
    if (guildId && tbhost::guild_order_access::BuildAccessReport(guildId, requester, report) && report.enabled)
    {
        view.rank = std::to_string(report.botRank);
        view.rankName = report.rankName.empty() ? "-" : report.rankName;
        view.tabsOk = report.tabsOk;
        view.tabsDenied = report.tabsDenied;
        view.gold = report.bankGold;
        view.canSetRank = report.canSetRank;
    }
    return view;
}

void SendOrderList(Player* requester, std::uint32_t guildId)
{
    std::vector<OrderRecord> const orders = tbhost::guild_orders::ListOrdersForGuild(guildId);
    std::vector<std::string> lines;
    lines.push_back(OrderBegin(static_cast<std::uint32_t>(orders.size())));
    lines.push_back(AccessLine(AccessFor(guildId, requester)));
    for (OrderRecord const& order : orders)
        lines.push_back(OrderLineFor(order));
    lines.push_back(OrderEnd());
    SendLines(requester, lines);
}

bool RelayReplyError(Player* requester, std::vector<addonorders::Record> const& reply)
{
    for (Record const& rec : reply)
    {
        if (rec.kind != "error")
            continue;
        std::string code = Field(rec, "code");
        std::string message = Field(rec, "msg");
        if (code.empty())
            code = "rejected";
        if (message.empty())
            message = code;
        addon::Err(requester, kIntent, code, message);
        return true;
    }
    return false;
}

// The hand-over rule TB applies before a give: a bound, quest, timed or
// conjured item can never reach the giver, so neither the preview nor the
// give may promise it. The read preview already reports the facts.
bool RefuseHandoff(Player* requester, std::vector<Record> const& reply)
{
    bool seen = false;
    bool handoff = false;
    bool bound = false;
    bool quest = false;
    bool timed = false;
    bool conjured = false;
    for (Record const& rec : reply)
    {
        if (rec.kind != "orderpreview")
            continue;
        seen = true;
        handoff = Field(rec, "handoff") == "yes";
        bound = Field(rec, "bound") == "yes";
        quest = Field(rec, "quest") == "yes";
        timed = Field(rec, "timed") == "yes";
        conjured = Field(rec, "conjured") == "yes";
    }
    if (!seen || handoff)
        return false;
    if (bound || quest || timed)
        addon::Err(requester, kIntent, "bound",
            "That item is bound when picked up (or a quest or timed item), so the bot cannot hand it over.");
    else if (conjured)
        addon::Err(requester, kIntent, "bad-item", "That item is conjured and cannot be handed over.");
    else
        addon::Err(requester, kIntent, "bad-item", "That item cannot be handed over.");
    return true;
}

std::vector<addonorders::Record> RunRead(Player* requester, std::string const& tail, std::string const& sub)
{
    CaptureHandler handler;
    h3::Words const words = h3::Split(tail);
    h3::guild_read::Handle(&handler, words, requester, sub);
    return ParseReply(handler.lines);
}

std::string JoinFrom(std::vector<std::string> const& words, std::size_t from)
{
    std::string out;
    for (std::size_t i = from; i < words.size(); ++i)
    {
        if (!out.empty())
            out += ' ';
        out += words[i];
    }
    return out;
}

// Everything after the first whitespace-separated word, original spacing kept
// for the read verbs (they split it themselves).
std::string TailAfterWord(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    return std::string(text.substr(i));
}

void HandleList(Player* requester)
{
    std::uint32_t const guildId = GuildOf(requester);
    if (!guildId)
    {
        addon::Err(requester, kIntent, "not-guild", "You are not in a guild.");
        return;
    }
    SendOrderList(requester, guildId);
}

void HandleAccess(Player* requester)
{
    std::uint32_t const guildId = GuildOf(requester);
    if (!guildId)
    {
        addon::Err(requester, kIntent, "not-guild", "You are not in a guild.");
        return;
    }
    addon::Send(requester, AccessLine(AccessFor(guildId, requester)));
}

void HandleRank(Player* requester, std::vector<std::string> const& words)
{
    std::uint32_t wanted = 0;
    if (words.size() < 2 || !ParseUint(words[1], wanted))
    {
        addon::Err(requester, kIntent, "invalid", "Usage: order rank <rankIndex>.");
        return;
    }
    std::uint32_t const guildId = GuildOf(requester);
    if (!guildId)
    {
        addon::Err(requester, kIntent, "not-guild", "You are not in a guild.");
        return;
    }
    std::string code;
    std::string detail;
    if (!tbhost::guild_order_access::MayChangeBotRank(guildId, requester, code, detail))
    {
        addon::Err(requester, kIntent, code, detail);
        return;
    }
    std::uint32_t moved = 0;
    if (!tbhost::guild_order_access::SetBotRank(guildId, wanted, &code, &detail, &moved))
    {
        addon::Err(requester, kIntent, code, detail);
        return;
    }
    addon::Ack(requester, kIntent, "rank:" + std::to_string(wanted), 1, "rank");
    addon::Send(requester, AccessLine(AccessFor(guildId, requester)));
}

void HandleReassign(Player* requester, std::vector<std::string> const& words)
{
    if (words.size() < 3)
    {
        addon::Err(requester, kIntent, "invalid", "Usage: order reassign <bot> <newBot>.");
        return;
    }
    Player* bot = FindOnlineHostBot(words[1]);
    if (!bot)
    {
        addon::Err(requester, kIntent, "no-bot", "No such bot.");
        return;
    }
    Player* newBot = FindOnlineHostBot(words[2]);
    if (!newBot)
    {
        addon::Err(requester, kIntent, "no-bot", "No such bot.");
        return;
    }
    std::uint32_t const guildId = GuildOf(bot);
    std::string code;
    std::string detail;
    if (!tbhost::guild_order_access::MayGiveOrders(guildId, requester, code, detail))
    {
        addon::Err(requester, kIntent, code, detail);
        return;
    }
    if (!tbhost::guild_orders::ReassignOrder(bot->GetGUID().GetCounter(),
            newBot->GetGUID().GetCounter(), code, detail))
    {
        addon::Err(requester, kIntent, code.empty() ? "invalid" : code, detail);
        return;
    }
    addon::Ack(requester, kIntent, "bot:" + bot->GetName(), 1, "reassign");
}

void HandleCancel(Player* requester, std::vector<std::string> const& words)
{
    if (words.size() < 2)
    {
        addon::Err(requester, kIntent, "invalid", "Usage: order <bot> cancel.");
        return;
    }
    Player* bot = FindOnlineHostBot(words[1]);
    if (!bot)
    {
        addon::Err(requester, kIntent, "no-bot", "No such bot.");
        return;
    }
    std::uint32_t const guildId = GuildOf(bot);
    std::string code;
    std::string detail;
    if (!tbhost::guild_order_access::MayGiveOrders(guildId, requester, code, detail))
    {
        addon::Err(requester, kIntent, code, detail);
        return;
    }
    std::uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    if (!tbhost::guild_orders::FindOrder(botGuidLow))
    {
        addon::Err(requester, kIntent, "not-found", "That bot has no order.");
        return;
    }
    if (!tbhost::guild_orders::CancelOrder(botGuidLow))
    {
        addon::Err(requester, kIntent, "not-found", "That bot has no order.");
        return;
    }
    tbhost::guild_orders::SetOrderWorking(botGuidLow, false);
    addon::Ack(requester, kIntent, "bot:" + bot->GetName(), 1, "cancel");
}

void HandleCreate(Player* requester, std::string_view tail)
{
    GiveArgs args;
    if (!ParseGiveArgs(tail, args))
    {
        addon::Err(requester, kIntent, "invalid", args.error);
        return;
    }
    if (args.kind == "buy")
    {
        addon::Err(requester, kIntent, GuildOrders::PlayerBuyRefusalCode(), GuildOrders::PlayerBuyRefusal());
        return;
    }
    if (args.kind == "enchant")
    {
        addon::Err(requester, kIntent, "invalid", "Enchant orders are not available on this server yet.");
        return;
    }
    if (args.kind != "farm" && args.kind != "craft")
    {
        addon::Err(requester, kIntent, "bad-type", args.kind);
        return;
    }
    Player* bot = FindOnlineHostBot(args.bot);
    if (!bot)
    {
        addon::Err(requester, kIntent, "no-bot", "No such bot.");
        return;
    }
    std::uint32_t const guildId = GuildOf(bot);
    if (!guildId)
    {
        addon::Err(requester, kIntent, "not-guild", "The bot is not in a guild.");
        return;
    }
    std::string code;
    std::string detail;
    if (!tbhost::guild_order_access::MayGiveOrders(guildId, requester, code, detail))
    {
        addon::Err(requester, kIntent, code, detail);
        return;
    }
    if (!tbhost::guild_orders::IsOrderStoreAvailable())
    {
        addon::Err(requester, kIntent, "store-unavailable", "The order store is not available.");
        return;
    }

    // The same plan the preview and search show: a craft the bot cannot make,
    // or an item nothing can source, is refused before the row exists.
    std::vector<Record> const preview = RunRead(requester,
        "preview " + bot->GetName() + " " + args.kind + " " + std::to_string(args.itemId) + " " +
            std::to_string(args.amount),
        "preview");
    if (RelayReplyError(requester, preview))
        return;
    std::string verdict;
    for (Record const& rec : preview)
        if (rec.kind == "orderpreview")
            verdict = Field(rec, "verdict");
    if (verdict.empty())
    {
        addon::Err(requester, kIntent, "invalid", "The order could not be checked.");
        return;
    }
    if (verdict != "ok")
    {
        GuildOrders::OrderReason reason = GuildOrders::OrderReason::NoSource;
        GuildOrders::ParseReason(verdict, reason);
        addon::Err(requester, kIntent, verdict,
            args.kind == "craft" ? GuildOrders::CraftOrderRefusal(reason) : "The bot cannot get that item.");
        return;
    }
    if (RefuseHandoff(requester, preview))
        return;

    OrderRecord record;
    record.botGuidLow = bot->GetGUID().GetCounter();
    record.guildId = guildId;
    record.giverGuidLow = requester ? requester->GetGUID().GetCounter() : record.botGuidLow;
    record.rootGiverGuidLow = record.giverGuidLow;
    record.type = args.kind == "craft" ? OrderType::Craft : OrderType::Farm;
    record.itemId = args.itemId;
    record.amount = args.amount;
    record.hours = args.hasHours ? args.hours : GuildOrders::kDefaultOrderHours;
    record.keep = args.keep;
    record.stockTarget = args.keep ? args.amount : 0;
    record.goldCap = args.gold;
    record.hasGoldCap = args.hasGold;
    record.bringItems = args.bring;
    record.state = GuildOrders::OrderState::Working;
    record.reason = GuildOrders::OrderReason::None;
    if (!tbhost::guild_orders::GiveOrder(record))
    {
        addon::Err(requester, kIntent, "store-write-failed", "The order could not be stored.");
        return;
    }
    // `.tb guildorder create` stores the row; `work` is what starts the bot.
    // The addon's give is one action, so the order starts here.
    tbhost::guild_orders::SetOrderWorking(bot->GetGUID().GetCounter(), true);
    addon::Ack(requester, kIntent, "bot:" + bot->GetName(), 1, args.kind);
    // The TB server pushes a live TBM:ORDER; TBF has no live order push yet, so
    // the new row is reported with the snapshot the addon already accepts.
    SendOrderList(requester, guildId);
}

void HandlePreview(Player* requester, std::string_view tail)
{
    PreviewArgs args;
    if (!ParsePreviewArgs(tail, args))
    {
        addon::Err(requester, kIntent, "invalid", args.error);
        return;
    }
    if (args.kind == "buy")
    {
        addon::Err(requester, kIntent, GuildOrders::PlayerBuyRefusalCode(), GuildOrders::PlayerBuyRefusal());
        return;
    }
    if (args.kind != "farm" && args.kind != "craft")
    {
        addon::Err(requester, kIntent, "bad-type", args.kind);
        return;
    }
    std::vector<Record> const reply = RunRead(requester,
        "preview " + args.bot + " " + args.kind + " " + std::to_string(args.itemId) + " " +
            std::to_string(args.amount),
        "preview");
    if (RelayReplyError(requester, reply))
        return;
    if (RefuseHandoff(requester, reply))
        return;

    std::string goldCap = "-";
    if (Player* bot = FindOnlineHostBot(args.bot))
        if (auto const order = tbhost::guild_orders::FindOrder(bot->GetGUID().GetCounter()))
            if (order->hasGoldCap)
                goldCap = std::to_string(order->goldCap);

    std::vector<std::string> const lines = PreviewLines(reply, goldCap, args.bring);
    if (lines.empty())
    {
        addon::Err(requester, kIntent, "invalid", "The preview is not available.");
        return;
    }
    SendLines(requester, lines);
}

void HandleSteps(Player* requester, std::vector<std::string> const& words)
{
    if (words.size() < 2)
    {
        addon::Err(requester, kIntent, "invalid", "Usage: order <bot> steps.");
        return;
    }
    std::vector<Record> const reply = RunRead(requester, "steps " + words[1], "steps");
    if (RelayReplyError(requester, reply))
        return;
    std::vector<std::string> const lines = StepLines(reply);
    if (lines.empty())
    {
        addon::Err(requester, kIntent, "no-bot", "No such bot.");
        return;
    }
    SendLines(requester, lines);
}

void HandleSearch(Player* requester, std::string const& tailAfterSearch)
{
    std::vector<std::string> const words = SplitWords(tailAfterSearch);
    if (words.empty())
    {
        addon::Err(requester, kIntent, "invalid", "Usage: order search <bot> <farm|craft> <text>.");
        return;
    }
    std::string const first = addon::pure::Lower(words[0]);
    if (first == "craft" || first == "enchant")
    {
        // Guild-wide pick-a-bot search (no bot named).
        if (first == "enchant")
        {
            SendLines(requester, { WhoBegin("enchant", 0), WhoEnd() });
            return;
        }
        std::vector<Record> const reply = RunRead(requester, "who " + tailAfterSearch, "who");
        if (RelayReplyError(requester, reply))
            return;
        SendLines(requester, WhoLines(reply));
        return;
    }

    if (words.size() < 2)
    {
        addon::Err(requester, kIntent, "invalid", "Usage: order search <bot> <farm|craft> <text>.");
        return;
    }
    std::string const kind = addon::pure::Lower(words[1]);
    if (kind == "buy")
    {
        addon::Err(requester, kIntent, GuildOrders::PlayerBuyRefusalCode(), GuildOrders::PlayerBuyRefusal());
        return;
    }
    if (kind == "enchant")
    {
        // No enchant orders exist yet; an empty pair is a real answer.
        Player* bot = FindOnlineHostBot(words[0]);
        if (!bot)
        {
            addon::Err(requester, kIntent, "no-bot", "No such bot.");
            return;
        }
        std::uint32_t const guildId = GuildOf(bot);
        if (!guildId || GuildOf(requester) != guildId)
        {
            addon::Err(requester, kIntent, "wrong-guild", "The bot is not in your guild.");
            return;
        }
        SendLines(requester, { EnchantsBegin(0), EnchantsEnd() });
        return;
    }
    if (kind != "farm" && kind != "craft")
    {
        addon::Err(requester, kIntent, "bad-type", kind);
        return;
    }
    std::vector<Record> const reply = RunRead(requester, "search " + tailAfterSearch, "search");
    if (RelayReplyError(requester, reply))
        return;
    SendLines(requester, ItemLines(reply));
}

void HandleOrderVerb(Player* requester, std::string_view args)
{
    std::vector<std::string> const words = SplitWords(args);
    if (words.empty())
    {
        addon::Err(requester, kIntent, "invalid",
            "Usage: order list|access|rank|reassign|search <args>|<bot> <farm|craft|preview|cancel|steps>.");
        return;
    }
    std::string const first = addon::pure::Lower(words[0]);
    if (first == "list")
    {
        HandleList(requester);
        return;
    }
    if (first == "access")
    {
        HandleAccess(requester);
        return;
    }
    if (first == "rank")
    {
        HandleRank(requester, words);
        return;
    }
    if (first == "reassign")
    {
        HandleReassign(requester, words);
        return;
    }
    if (first == "search")
    {
        HandleSearch(requester, TailAfterWord(args));
        return;
    }
    if (words.size() < 2)
    {
        addon::Err(requester, kIntent, "invalid", "Usage: order <bot> <farm|craft|preview|cancel|steps>.");
        return;
    }
    std::string const verb = addon::pure::Lower(words[1]);
    if (verb == "cancel")
    {
        HandleCancel(requester, words);
        return;
    }
    if (verb == "steps")
    {
        HandleSteps(requester, words);
        return;
    }
    if (verb == "meet")
    {
        addon::Err(requester, kIntent, "noorder", "Enchant orders are not available on this server yet.");
        return;
    }
    if (verb == "preview")
    {
        HandlePreview(requester, TailAfterWord(args));
        return;
    }
    if (verb == "enchant")
    {
        addon::Err(requester, kIntent, "invalid", "Enchant orders are not available on this server yet.");
        return;
    }
    if (verb == "farm" || verb == "craft" || verb == "buy")
    {
        HandleCreate(requester, TailAfterWord(args));
        return;
    }
    addon::Err(requester, kIntent, "invalid", "Unknown order verb.");
}

} // namespace

void RegisterAddonVerbsOrders()
{
    addon::RegisterVerb("order", &HandleOrderVerb);
    addon::AdvertiseCap("guild-orders");
    addon::AdvertiseCap(GuildOrders::PlayerBuyCapToken());
    addon::AdvertiseCap("guild-order-steps");
}

} // namespace addonorders
} // namespace TortoiseBots

#else // no engine: the order store and the `.tb guildorder` verbs do not exist

namespace TortoiseBots
{
namespace addonorders
{
void RegisterAddonVerbsOrders() { }
} // namespace addonorders
} // namespace TortoiseBots

#endif
