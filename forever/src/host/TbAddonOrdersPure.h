// The addon order window without the core (TBF #155): the TBM shapes of the
// guild-order replies the TortoiseBotsForeverManager addon parses, and the
// reader for the `.tb` records the host read verbs emit. No core header and no
// legacy type; tested standalone by forever/tools/test_addon_orders.cpp.
// The live half that owns the store and the requester is host/TbAddonOrders.cpp.
//
// Wire sources (TortoiseBotsForeverManager, read-only here):
//   BotOrder.lua:57-75   the ORDER, ACCESS, ITEM, WHO, PREVIEW, STEP shapes
//   BotOrder.lua:1783    ORDER field order and counts
//   BotOrder.lua:1406    WHO bots token (name:skill:have:need:busy)
//   BotOrder.lua:1544    PREVIEW reagents token (11 colon fields)
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "host/TbAddonPure.h"

namespace TortoiseBots
{
namespace addonorders
{

// One `kind key=value ...` line of the `.tb` reply grammar (TbH3.h): values are
// percent-decoded here, a `<kind>+` continuation line appends to the previous
// record of `kind`.
struct Record
{
    std::string kind;
    std::vector<std::pair<std::string, std::string>> pairs;
};

// Percent-decode one `.tb` value: `%XX` (upper-case hex) back to its byte, an
// invalid escape left as written.
inline std::string PercentDecode(std::string_view text)
{
    auto hex = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size())
        {
            int const hi = hex(text[i + 1]);
            int const lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

inline std::vector<std::string> SplitWords(std::string_view text)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        std::size_t const start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i > start)
            out.emplace_back(text.substr(start, i - start));
    }
    return out;
}

// The record grammar one captured `.tb` reply is made of.
inline std::vector<Record> ParseReply(std::vector<std::string> const& lines)
{
    std::vector<Record> out;
    for (std::string const& line : lines)
    {
        std::vector<std::string> const words = SplitWords(line);
        if (words.empty())
            continue;
        std::string kind = words[0];
        bool const continuation = !kind.empty() && kind.back() == '+';
        if (continuation)
            kind.pop_back();
        Record rec;
        rec.kind = kind;
        for (std::size_t i = 1; i < words.size(); ++i)
        {
            std::size_t const eq = words[i].find('=');
            if (eq == std::string::npos)
                continue;
            rec.pairs.emplace_back(words[i].substr(0, eq), PercentDecode(words[i].substr(eq + 1)));
        }
        if (continuation && !out.empty() && out.back().kind == kind)
        {
            for (auto& pair : rec.pairs)
                out.back().pairs.push_back(std::move(pair));
        }
        else if (!continuation)
        {
            out.push_back(std::move(rec));
        }
    }
    return out;
}

inline std::string Field(Record const& rec, std::string_view key)
{
    for (auto const& [k, v] : rec.pairs)
        if (k == key)
            return v;
    return std::string();
}

inline bool HasField(Record const& rec, std::string_view key)
{
    for (auto const& [k, v] : rec.pairs)
        if (k == key)
            return true;
    return false;
}

inline bool ParseUint(std::string const& text, std::uint32_t& out)
{
    out = 0;
    if (text.empty() || text.size() > 10)
        return false;
    std::uint64_t value = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + std::uint64_t(c - '0');
        if (value > 0xFFFFFFFFull)
            return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

inline std::uint32_t ParseUintOr(std::string const& text, std::uint32_t fallback = 0)
{
    std::uint32_t value = 0;
    return ParseUint(text, value) ? value : fallback;
}

// ---- the TBM shapes ----

// TBM:ORDER| (TB FormatOrderLine, runtime/GuildOrderStore.cpp:4233-4260).
struct OrderView
{
    std::string bot;
    std::string type;
    std::uint32_t itemId = 0;
    std::string item;
    std::uint32_t have = 0;
    std::uint32_t want = 0;
    std::string state;
    std::string reason;
    std::string zone;
    std::string difficulty;
    std::string hours;
    std::string keep;
    std::string parent;
    std::string goldCap;
    std::string step;
    std::string need;
    std::uint32_t carry = 0;
    std::string bring;
};

inline std::string OrderLine(OrderView const& v)
{
    auto const& safe = addon::pure::Safe;
    return "TBM:ORDER|" + safe(v.bot) + "|" + safe(v.type) + "|" + std::to_string(v.itemId) + "|" +
        safe(v.item) + "|" + std::to_string(v.have) + "|" + std::to_string(v.want) + "|" +
        safe(v.state) + "|" + safe(v.reason) + "|" + safe(v.zone) + "|" + safe(v.difficulty) + "|" +
        safe(v.hours) + "|" + safe(v.keep) + "|" + safe(v.parent) + "|" + safe(v.goldCap) + "|" +
        safe(v.step) + "|" + safe(v.need) + "|" + std::to_string(v.carry) + "|" + safe(v.bring);
}

// TBM:ORDER_BEGIN|<n> ... TBM:ORDER_END, with the ACCESS line inside the pair.
inline std::string OrderBegin(std::uint32_t count) { return "TBM:ORDER_BEGIN|" + std::to_string(count); }
inline std::string OrderEnd() { return "TBM:ORDER_END"; }

// TBM:ORDER_ACCESS|rank|rankName|tabsOk|tabsDenied|gold|canSetRank.
struct AccessView
{
    std::string rank = "-";
    std::string rankName = "-";
    std::string tabsOk = "-";
    std::string tabsDenied = "-";
    bool gold = false;
    bool canSetRank = false;
};

inline std::string AccessLine(AccessView const& v)
{
    auto const& safe = addon::pure::Safe;
    return "TBM:ORDER_ACCESS|" + safe(v.rank) + "|" + safe(v.rankName) + "|" + safe(v.tabsOk) + "|" +
        safe(v.tabsDenied) + "|" + (v.gold ? "1" : "0") + "|" + (v.canSetRank ? "1" : "0");
}

// TBM:ORDER_ITEM|itemId|item|quality|sources|minLevel|skill|verdict|zone|chance|difficulty|category.
struct ItemView
{
    std::uint32_t itemId = 0;
    std::string item;
    std::uint32_t quality = 0;
    std::string sources = "-";
    std::uint32_t minLevel = 0;
    std::string skill = "-";
    std::string verdict = "ok";
    std::string zone = "-";
    std::uint32_t chance = 0;
    std::string difficulty = "-";
    std::string category = "-";
};

inline std::string ItemLine(ItemView const& v)
{
    auto const& safe = addon::pure::Safe;
    return "TBM:ORDER_ITEM|" + std::to_string(v.itemId) + "|" + safe(v.item) + "|" +
        std::to_string(v.quality) + "|" + safe(v.sources) + "|" + std::to_string(v.minLevel) + "|" +
        safe(v.skill) + "|" + safe(v.verdict) + "|" + safe(v.zone) + "|" + std::to_string(v.chance) + "|" +
        safe(v.difficulty) + "|" + safe(v.category);
}

inline std::string ItemsBegin(std::uint32_t count) { return "TBM:ORDER_ITEMS_BEGIN|" + std::to_string(count); }
inline std::string ItemsEnd() { return "TBM:ORDER_ITEMS_END"; }
inline std::string EnchantsBegin(std::uint32_t count) { return "TBM:ORDER_ENCHANTS_BEGIN|" + std::to_string(count); }
inline std::string EnchantsEnd() { return "TBM:ORDER_ENCHANTS_END"; }

// TBM:ORDER_WHO|kind|id|name|quality|slot|reqSkill|bots.
struct WhoBotView
{
    std::string bot;
    std::uint32_t skill = 0;
    std::uint32_t have = 0;
    std::uint32_t need = 0;
    bool busy = false;
};

inline std::string WhoBotsToken(std::vector<WhoBotView> const& bots)
{
    if (bots.empty())
        return "-";
    auto const& safe = addon::pure::Safe;
    std::string out;
    for (WhoBotView const& bot : bots)
    {
        if (!out.empty())
            out += ';';
        out += safe(bot.bot) + ":" + std::to_string(bot.skill) + ":" + std::to_string(bot.have) + ":" +
            std::to_string(bot.need) + ":" + (bot.busy ? "1" : "0");
    }
    return out;
}

struct WhoView
{
    std::string kind = "craft";
    std::uint32_t itemId = 0;
    std::string item;
    std::string quality = "-";
    std::string slot = "-";
    std::uint32_t reqSkill = 0;
    std::vector<WhoBotView> bots;
};

inline std::string WhoLine(WhoView const& v)
{
    auto const& safe = addon::pure::Safe;
    return "TBM:ORDER_WHO|" + safe(v.kind) + "|" + std::to_string(v.itemId) + "|" + safe(v.item) + "|" +
        safe(v.quality) + "|" + safe(v.slot) + "|" + std::to_string(v.reqSkill) + "|" +
        WhoBotsToken(v.bots);
}

inline std::string WhoBegin(std::string const& kind, std::uint32_t count)
{
    return "TBM:ORDER_WHO_BEGIN|" + addon::pure::Safe(kind) + "|" + std::to_string(count);
}
inline std::string WhoEnd() { return "TBM:ORDER_WHO_END"; }

// TBM:ORDER_PREVIEW|bot|type|itemId|item|want|missing|difficulty|zone|reagents|minutes|goldCap|bring.
struct PreviewReagentView
{
    std::uint32_t itemId = 0;
    std::uint32_t need = 0;
    std::uint32_t bags = 0;
    std::uint32_t mail = 0;
    std::uint32_t bank = 0;
    std::uint32_t farm = 0;
    std::string zone = "-";
    std::uint32_t ah = 0;
    std::uint32_t vendor = 0;
    std::uint32_t sub = 0;
    std::uint32_t player = 0;
};

inline std::string PreviewReagentsToken(std::vector<PreviewReagentView> const& reagents)
{
    if (reagents.empty())
        return "-";
    auto const& safe = addon::pure::Safe;
    std::string out;
    for (PreviewReagentView const& r : reagents)
    {
        if (!out.empty())
            out += ';';
        out += std::to_string(r.itemId) + ":" + std::to_string(r.need) + ":" + std::to_string(r.bags) + ":" +
            std::to_string(r.mail) + ":" + std::to_string(r.bank) + ":" + std::to_string(r.farm) + ":" +
            safe(r.zone) + ":" + std::to_string(r.ah) + ":" + std::to_string(r.vendor) + ":" +
            std::to_string(r.sub) + ":" + std::to_string(r.player);
    }
    return out;
}

struct PreviewView
{
    std::string bot;
    std::string kind = "craft";
    std::uint32_t itemId = 0;
    std::string item;
    std::uint32_t want = 0;
    std::uint32_t missing = 0;
    std::string difficulty = "-";
    std::string zone = "-";
    std::vector<PreviewReagentView> reagents;
    std::uint32_t minutes = 0;
    std::string goldCap = "-";
    std::vector<std::uint32_t> bring;
};

inline std::string PreviewLine(PreviewView const& v)
{
    auto const& safe = addon::pure::Safe;
    std::string bring = "-";
    if (!v.bring.empty())
    {
        bring.clear();
        for (std::uint32_t id : v.bring)
        {
            if (!bring.empty())
                bring += ',';
            bring += std::to_string(id);
        }
    }
    return "TBM:ORDER_PREVIEW|" + safe(v.bot) + "|" + safe(v.kind) + "|" + std::to_string(v.itemId) + "|" +
        safe(v.item) + "|" + std::to_string(v.want) + "|" + std::to_string(v.missing) + "|" +
        safe(v.difficulty) + "|" + safe(v.zone) + "|" + PreviewReagentsToken(v.reagents) + "|" +
        std::to_string(v.minutes) + "|" + safe(v.goldCap) + "|" + bring;
}

// ---- `.tb` record -> TBM translation ----

// The captured `orderitem` rows of a `.tb guildorder search` answer.
inline std::vector<std::string> ItemLines(std::vector<Record> const& reply)
{
    std::vector<ItemView> rows;
    for (Record const& rec : reply)
    {
        if (rec.kind != "orderitem")
            continue;
        ItemView row;
        row.itemId = ParseUintOr(Field(rec, "item"));
        row.item = Field(rec, "name");
        row.quality = ParseUintOr(Field(rec, "quality"));
        row.sources = Field(rec, "sources");
        row.verdict = Field(rec, "verdict");
        row.category = Field(rec, "cat");
        if (row.itemId == 0 || row.item.empty() || row.verdict.empty())
            continue;
        if (HasField(rec, "skill"))
            row.skill = Field(rec, "skill");
        rows.push_back(std::move(row));
    }
    std::vector<std::string> out;
    out.push_back(ItemsBegin(static_cast<std::uint32_t>(rows.size())));
    for (ItemView const& row : rows)
        out.push_back(ItemLine(row));
    out.push_back(ItemsEnd());
    return out;
}

// The captured `orderwho` rows of a `.tb guildorder who` answer.
inline std::vector<std::string> WhoLines(std::vector<Record> const& reply)
{
    std::vector<WhoView> rows;
    for (Record const& rec : reply)
    {
        if (rec.kind != "orderwho")
            continue;
        WhoView row;
        row.kind = Field(rec, "type");
        if (row.kind != "craft" && row.kind != "enchant")
            row.kind = "craft";
        row.itemId = ParseUintOr(Field(rec, "item"));
        row.item = Field(rec, "name");
        row.quality = Field(rec, "quality");
        if (row.itemId == 0 || row.item.empty())
            continue;
        std::string const bots = Field(rec, "bots");
        std::size_t start = 0;
        while (start < bots.size())
        {
            std::size_t const semi = bots.find(';', start);
            std::string const token = bots.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
            std::vector<std::string> parts;
            std::size_t p = 0;
            while (p <= token.size())
            {
                std::size_t const colon = token.find(':', p);
                parts.push_back(token.substr(p, colon == std::string::npos ? std::string::npos : colon - p));
                if (colon == std::string::npos)
                    break;
                p = colon + 1;
            }
            if (parts.size() == 5)
            {
                WhoBotView bot;
                bot.bot = parts[0];
                bot.skill = ParseUintOr(parts[1]);
                bot.have = ParseUintOr(parts[2]);
                bot.need = ParseUintOr(parts[3]);
                bot.busy = parts[4] == "yes" || parts[4] == "1";
                if (!bot.bot.empty())
                    row.bots.push_back(std::move(bot));
            }
            if (semi == std::string::npos)
                break;
            start = semi + 1;
        }
        rows.push_back(std::move(row));
    }
    std::vector<std::string> out;
    std::string kind = rows.empty() ? "craft" : rows.front().kind;
    out.push_back(WhoBegin(kind, static_cast<std::uint32_t>(rows.size())));
    for (WhoView const& row : rows)
        out.push_back(WhoLine(row));
    out.push_back(WhoEnd());
    return out;
}

// The captured `orderpreview` record of a `.tb guildorder preview` answer, or
// no lines when the answer was an error (the caller sends the Err instead).
inline std::vector<std::string> PreviewLines(std::vector<Record> const& reply, std::string const& goldCap,
    std::vector<std::uint32_t> const& bring)
{
    for (Record const& rec : reply)
    {
        if (rec.kind != "orderpreview")
            continue;
        PreviewView row;
        row.bot = Field(rec, "bot");
        row.kind = Field(rec, "type");
        row.itemId = ParseUintOr(Field(rec, "item"));
        row.item = Field(rec, "name");
        row.want = ParseUintOr(Field(rec, "want"));
        row.missing = ParseUintOr(Field(rec, "short"));
        row.goldCap = goldCap;
        row.bring = bring;
        if (row.bot.empty() || row.itemId == 0 || row.item.empty() || row.want == 0)
            return {};
        std::string const reagents = Field(rec, "reagents");
        std::size_t start = 0;
        while (start < reagents.size())
        {
            std::size_t const semi = reagents.find(';', start);
            std::string const token = reagents.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
            std::vector<std::string> parts;
            std::size_t p = 0;
            while (p <= token.size())
            {
                std::size_t const colon = token.find(':', p);
                parts.push_back(token.substr(p, colon == std::string::npos ? std::string::npos : colon - p));
                if (colon == std::string::npos)
                    break;
                p = colon + 1;
            }
            if (parts.size() >= 3)
            {
                PreviewReagentView reagent;
                reagent.itemId = ParseUintOr(parts[0]);
                reagent.need = ParseUintOr(parts[1]);
                reagent.bags = ParseUintOr(parts[2]);
                if (reagent.itemId && reagent.need)
                {
                    if (reagent.bags < reagent.need)
                    {
                        std::uint32_t const shortfall = reagent.need - reagent.bags;
                        // A ticked reagent is the player's to bring (the give
                        // keeps it out of every sourcing lane); the rest is
                        // what the order itself will source.
                        if (std::find(bring.begin(), bring.end(), reagent.itemId) != bring.end())
                            reagent.player = shortfall;
                        else
                            reagent.farm = shortfall;
                    }
                    row.reagents.push_back(std::move(reagent));
                }
            }
            if (semi == std::string::npos)
                break;
            start = semi + 1;
        }
        return { PreviewLine(row) };
    }
    return {};
}

// The captured `orderstep` rows of a `.tb guildorder steps` answer.
inline std::vector<std::string> StepLines(std::vector<Record> const& reply)
{
    struct Step
    {
        std::uint32_t age = 0;
        std::string state;
        std::string reason;
        std::string step;
        std::uint32_t itemId = 0;
    };
    std::string bot;
    std::uint32_t dropped = 0;
    std::vector<Step> steps;
    for (Record const& rec : reply)
    {
        if (rec.kind == "orderstep")
        {
            Step step;
            if (bot.empty())
                bot = Field(rec, "bot");
            step.age = ParseUintOr(Field(rec, "age"));
            step.state = Field(rec, "state");
            step.reason = Field(rec, "reason");
            step.step = Field(rec, "step");
            if (HasField(rec, "leg"))
                step.itemId = ParseUintOr(Field(rec, "leg"));
            if (step.state.empty())
                continue;
            steps.push_back(std::move(step));
        }
        else if (rec.kind == "guildorder-end")
        {
            if (bot.empty())
                bot = Field(rec, "bot");
            dropped = ParseUintOr(Field(rec, "dropped"));
        }
    }
    if (bot.empty())
        return {};
    auto const& safe = addon::pure::Safe;
    std::vector<std::string> out;
    out.push_back("TBM:ORDER_STEPS_BEGIN|" + safe(bot) + "|" + std::to_string(steps.size()) + "|" +
        std::to_string(dropped));
    for (Step const& step : steps)
        out.push_back("TBM:ORDER_STEP|" + safe(bot) + "|" + std::to_string(step.age) + "|" +
            safe(step.state) + "|" + safe(step.reason) + "|" + safe(step.step) + "|" +
            std::to_string(step.itemId));
    out.push_back("TBM:ORDER_STEPS_END|" + safe(bot));
    return out;
}

// ---- command argument helpers ----

// `order <bot> <farm|craft> <itemId> <n> [hours|inf] [keep] [gold=<copper>] [bring=<id>,<id>]`
struct GiveArgs
{
    std::string bot;
    std::string kind;
    std::uint32_t itemId = 0;
    std::uint32_t amount = 0;
    std::uint32_t hours = 0;   // 0 = `inf`
    bool hasHours = false;
    bool keep = false;
    bool hasGold = false;
    std::uint32_t gold = 0;
    std::vector<std::uint32_t> bring;
    std::string error;         // a plain refusal line; empty on success
};

inline bool ParseBringList(std::string const& text, std::vector<std::uint32_t>& out)
{
    out.clear();
    if (text.empty())
        return false;
    std::size_t start = 0;
    while (start <= text.size())
    {
        std::size_t const comma = text.find(',', start);
        std::string const token = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        std::uint32_t const id = ParseUintOr(token);
        if (id == 0)
            return false;
        out.push_back(id);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return !out.empty();
}

inline bool ParseGiveArgs(std::string_view tail, GiveArgs& out)
{
    out = GiveArgs();
    std::vector<std::string> const words = SplitWords(tail);
    if (words.size() < 4)
    {
        out.error = "Usage: order <bot> <farm|craft> <item> <amount> [hours|inf] [keep] [gold=..] [bring=..].";
        return false;
    }
    out.bot = words[0];
    out.kind = addon::pure::Lower(words[1]);
    if (!ParseUint(words[2], out.itemId) || out.itemId == 0)
    {
        out.error = "Item id must be a positive number.";
        return false;
    }
    if (!ParseUint(words[3], out.amount) || out.amount == 0 || out.amount > 200)
    {
        out.error = "Amount must be 1-200.";
        return false;
    }
    bool seenHours = false;
    bool seenGold = false;
    bool seenBring = false;
    for (std::size_t i = 4; i < words.size(); ++i)
    {
        std::string const lower = addon::pure::Lower(words[i]);
        if (lower == "keep")
        {
            if (out.keep)
            {
                out.error = "keep is given once.";
                return false;
            }
            out.keep = true;
        }
        else if (lower.rfind("gold=", 0) == 0)
        {
            if (seenGold)
            {
                out.error = "gold= is given once.";
                return false;
            }
            seenGold = true;
            out.hasGold = true;
            if (!ParseUint(words[i].substr(5), out.gold))
            {
                out.error = "Guild gold cap must be copper from 0 to 4294967295.";
                return false;
            }
        }
        else if (lower.rfind("bring=", 0) == 0)
        {
            if (seenBring)
            {
                out.error = "bring= is given once.";
                return false;
            }
            seenBring = true;
            if (!ParseBringList(words[i].substr(6), out.bring))
            {
                out.error = "Bring list must be item ids joined by commas.";
                return false;
            }
        }
        else if (lower == "inf")
        {
            if (seenHours)
            {
                out.error = "Hours are given once.";
                return false;
            }
            seenHours = true;
            out.hasHours = true;
            out.hours = 0;
        }
        else
        {
            std::uint32_t parsed = 0;
            bool digits = !lower.empty();
            for (char c : lower)
                if (c < '0' || c > '9')
                    digits = false;
            if (digits)
                parsed = ParseUintOr(lower);
            if (!digits || parsed < 1 || parsed > 24)
            {
                out.error = "Hours must be 1-24 or inf.";
                return false;
            }
            if (seenHours)
            {
                out.error = "Hours are given once.";
                return false;
            }
            seenHours = true;
            out.hasHours = true;
            out.hours = parsed;
        }
    }
    if (!out.bring.empty() && out.kind != "craft")
    {
        out.error = "bring= needs a craft order.";
        return false;
    }
    return true;
}

// `order <bot> preview <farm|craft> <itemId> <n> [bring=<id>,<id>]`
struct PreviewArgs
{
    std::string bot;
    std::string kind;
    std::uint32_t itemId = 0;
    std::uint32_t amount = 0;
    std::vector<std::uint32_t> bring;
    std::string error;
};

inline bool ParsePreviewArgs(std::string_view tail, PreviewArgs& out)
{
    out = PreviewArgs();
    std::vector<std::string> const words = SplitWords(tail);
    if (words.size() < 5)
    {
        out.error = "Usage: order <bot> preview <farm|craft> <item> <amount> [bring=..].";
        return false;
    }
    out.bot = words[0];
    if (addon::pure::Lower(words[1]) != "preview")
    {
        out.error = "Usage: order <bot> preview <farm|craft> <item> <amount> [bring=..].";
        return false;
    }
    out.kind = addon::pure::Lower(words[2]);
    if (!ParseUint(words[3], out.itemId) || out.itemId == 0)
    {
        out.error = "Item id must be a positive number.";
        return false;
    }
    if (!ParseUint(words[4], out.amount) || out.amount == 0 || out.amount > 200)
    {
        out.error = "Amount must be 1-200.";
        return false;
    }
    for (std::size_t i = 5; i < words.size(); ++i)
    {
        std::string const lower = addon::pure::Lower(words[i]);
        if (lower.rfind("bring=", 0) == 0)
        {
            if (!out.bring.empty())
            {
                out.error = "bring= is given once.";
                return false;
            }
            if (!ParseBringList(words[i].substr(6), out.bring))
            {
                out.error = "Bring list must be item ids joined by commas.";
                return false;
            }
        }
        else
        {
            out.error = "Unknown preview option.";
            return false;
        }
    }
    return true;
}

} // namespace addonorders
} // namespace TortoiseBots
