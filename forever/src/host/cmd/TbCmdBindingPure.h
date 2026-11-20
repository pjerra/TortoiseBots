// Commander port, package P1 (the binding card): the PURE half. FOREVER-COMMANDER-PORT-SPEC.md sections 3, 4, 5 and 10 (D1, D2).
//
// Standard library and the Turtle telemetry header only (runtime/CommanderTelemetry.h): no core header, no database, no legacy type. Tested standalone by
// forever/tools/cmd_binding_check.cpp (g++ -std=c++17 -I. forever/tools/cmd_binding_check.cpp).
//
// What it decides: how h3's table (bot -> commander, forever/src/host/TbH3.h) and the run table (host/run/TbRun.h) become the COMMANDER CARDS the dashboard and
// `.tb commander status` show. h3's table is the ONLY binding owner (decision D1); the cards are read from it. A card is keyed by its COMMANDER, never by a
// group (the failing case of spec section 7: a run's system binding must not show as a record AND as a run). Everything is a character guid counter
// (decision D2): no group number is stored, the live group is found again through the lead by the host.
#pragma once

#include "runtime/CommanderTelemetry.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace pure
{
namespace CT = ::TortoiseBots::CommanderTelemetry;

// One entry of h3's table that is still valid (h3::CommanderBindings drops a binding BotMgr no longer backs: a re-login lapses it by itself).
struct Pair
{
    uint32_t bot = 0;
    uint32_t commander = 0;
};

// One run that goes on (not ended). `leaderCommander` is h3's commander of the run's leader, 0 when the leader has none: the run never rebinds its own
// leader, so a leader that was commanded before the run still is, and that commander owns the card. `members` are candidates for the card's bots: the
// leader's live party and the run's own bound members. BuildCards keeps the leader plus the candidates h3's table binds to it (RunMembers), so a human
// player or an unbound party member is never counted as a bound bot.
struct RunIn
{
    uint32_t leader = 0;
    uint32_t leaderCommander = 0;
    std::vector<uint32_t> members;
    size_t index = 0;               // what the caller wants back in Card::run (its own index of the run)
};

// What `.tb master` told about a binding when it was made, and when a card was first seen.
struct MetaIn
{
    uint32_t commander = 0;
    uint32_t lead = 0;              // the party leader named by `.tb master party <leader> <commander>`, 0 = not named
    int64_t boundAt = 0;
};

struct Card
{
    uint32_t commander = 0;
    uint32_t lead = 0;              // always one of `bots` (the first)
    bool system = false;            // the run's own binding: the tank leads and no `.tb master` commander stands behind it
    int run = -1;                   // RunIn::index of the run this card shows, -1 = none
    std::vector<uint32_t> bots;     // the lead first
    int64_t boundAt = 0;
};

inline bool Has(std::vector<uint32_t> const& list, uint32_t value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

inline void AddUnique(std::vector<uint32_t>& list, uint32_t value)
{
    if (value && !Has(list, value))
        list.push_back(value);
}

inline void LeadFirst(std::vector<uint32_t>& bots, uint32_t lead)
{
    auto it = std::find(bots.begin(), bots.end(), lead);
    if (it != bots.end() && it != bots.begin())
        std::rotate(bots.begin(), it, it + 1);
}

// The bots of a run's card: its leader, then the candidates h3's table binds to that leader. A member of the leader's party that is a real player or a
// host bot the run did not bind has no such pair and is not on the card.
inline std::vector<uint32_t> RunMembers(uint32_t leader, std::vector<uint32_t> const& candidates, std::vector<Pair> const& pairs)
{
    std::vector<uint32_t> out;
    if (!leader)
        return out;
    out.push_back(leader);
    for (uint32_t candidate : candidates)
    {
        if (!candidate || candidate == leader)
            continue;
        for (Pair const& p : pairs)
            if (p.bot == candidate && p.commander == leader)
            {
                AddUnique(out, candidate);
                break;
            }
    }
    return out;
}

// The cards, by commander guid. `groupLeader` maps a bot to the leader of the group it stands in (0 or absent: none), only to name the lead of a card
// that `.tb master` made without naming one. Rules:
//  * one card per commander: the bots bound to it, wherever they are (a commander never gets two cards);
//  * one card per run: the run's leader is the key (system, when nobody commands the leader) or its own commander is (the run's bound members are folded
//    into that commander's card). The pairs the run itself made (members -> tank) are dropped, so a run shows ONCE;
//  * every card is returned: the telemetry cap (CT::kMaxCommandersReported) belongs to the dashboard serializer, never to this collection (state is pruned
//    from the whole list).
inline std::vector<Card> BuildCards(std::vector<Pair> const& pairs, std::vector<RunIn> const& runs, std::vector<MetaIn> const& metas,
                                    std::map<uint32_t, uint32_t> const& groupLeader)
{
    std::map<uint32_t, std::vector<uint32_t>> byCommander;
    for (Pair const& p : pairs)
        if (p.bot && p.commander && p.bot != p.commander)
            AddUnique(byCommander[p.commander], p.bot);

    std::map<uint32_t, Card> cards;
    for (RunIn const& r : runs)
    {
        if (!r.leader)
            continue;
        uint32_t const key = r.leaderCommander ? r.leaderCommander : r.leader;
        byCommander.erase(r.leader);                 // the members the run bound to its tank: the run's card shows them
        auto it = cards.find(key);
        bool const fresh = it == cards.end();
        Card& c = fresh ? cards[key] : it->second;
        if (fresh)
        {
            c.commander = key;
            c.lead = r.leader;
            c.system = r.leaderCommander == 0;
            c.run = int(r.index);
            AddUnique(c.bots, r.leader);
        }
        for (uint32_t m : RunMembers(r.leader, r.members, pairs))
            AddUnique(c.bots, m);
        if (r.leaderCommander)
        {
            auto const own = byCommander.find(key); // the bots the same commander has outside the run's party
            if (own != byCommander.end())
                for (uint32_t b : own->second)
                    AddUnique(c.bots, b);
            byCommander.erase(key);
        }
    }
    for (auto const& [commander, bots] : byCommander)
    {
        Card c;
        c.commander = commander;
        c.bots = bots;
        c.lead = c.bots.front();
        bool namedLead = false;
        for (MetaIn const& m : metas)
            if (m.commander == commander && m.lead && Has(c.bots, m.lead))
            {
                c.lead = m.lead;
                namedLead = true;
            }
        if (!namedLead)
        {
            auto const g = groupLeader.find(c.bots.front());
            if (g != groupLeader.end() && Has(c.bots, g->second))
                c.lead = g->second;
        }
        LeadFirst(c.bots, c.lead);
        cards[commander] = std::move(c);
    }
    std::vector<Card> out;
    for (auto& [commander, c] : cards)
    {
        for (MetaIn const& m : metas)
            if (m.commander == commander)
                c.boundAt = m.boundAt;
        LeadFirst(c.bots, c.lead);
        out.push_back(std::move(c));
    }
    return out;
}

// ---- the order log (the module's OrderStore: per card, no whole-store commander cap) ----

// One addressed bot's outcome of one order: refusal empty = the bot queued it.
struct OrderOutcome
{
    uint32_t bot = 0;
    std::string refusal;    // the first refusal code for this bot, empty when it queued the order
};

struct OrderTally
{
    uint32_t queued = 0;
    uint32_t refused = 0;
    std::string firstRefusal;
};

// One command's outcomes grouped by the card each bot is shown on (`cardOfBot`: bot guid -> card key). Every card gets its OWN counts and first refusal:
// a card never claims work that queued or was refused for a bot of another card. Bots without a card are ignored.
inline std::map<uint32_t, OrderTally> TallyByCard(std::vector<OrderOutcome> const& outcomes, std::map<uint32_t, uint32_t> const& cardOfBot)
{
    std::map<uint32_t, OrderTally> out;
    for (OrderOutcome const& o : outcomes)
    {
        auto const card = cardOfBot.find(o.bot);
        if (!o.bot || card == cardOfBot.end() || !card->second)
            continue;
        OrderTally& tally = out[card->second];
        if (o.refusal.empty())
            ++tally.queued;
        else
        {
            ++tally.refused;
            if (tally.firstRefusal.empty())
                tally.firstRefusal = o.refusal;
        }
    }
    return out;
}

// One order and the module's answer, in the Turtle record: ok = accepted, result = the scope (accepted) or "<code>: <message>" (refused).
inline CT::OrderRecord MakeOrder(int64_t at, std::string verb, std::string detail, bool ok, std::string const& code, std::string const& text)
{
    CT::OrderRecord rec;
    rec.at = at;
    rec.verb = std::move(verb);
    rec.detail = std::move(detail);
    rec.ok = ok;
    if (ok)
        rec.result = text;
    else
        rec.result = code.empty() ? text : (text.empty() ? code : code + ": " + text);
    return rec;
}

// The module's per-card order history: the last kMaxOrdersPerCommander orders of every standing card, an exact repeat of a card's own last order dropped.
// There is no whole-store commander cap: noting a newer card never evicts another card's history (the Turtle OrderLog's twenty-commander ring does). The
// history and the repeat signature are pruned together by Retain.
class OrderStore
{
public:
    void Note(uint64_t card, CT::OrderRecord rec)
    {
        if (!card)
            return;
        rec.detail = CT::Clip(rec.detail, CT::kMaxOrderDetail);
        rec.result = CT::Clip(rec.result, CT::kMaxOrderResult);
        std::string const sig = rec.verb + "|" + rec.detail + "|" + (rec.ok ? "1" : "0") + "|" + rec.result;
        std::string& last = m_last[card];
        if (last == sig)
            return;
        last = sig;
        std::deque<CT::OrderRecord>& orders = m_orders[card];
        orders.push_back(std::move(rec));
        while (orders.size() > CT::kMaxOrdersPerCommander)
            orders.pop_front();
    }

    // Oldest first.
    std::vector<CT::OrderRecord> Recent(uint64_t card) const
    {
        auto it = m_orders.find(card);
        if (it == m_orders.end())
            return {};
        return std::vector<CT::OrderRecord>(it->second.begin(), it->second.end());
    }

    // Forget every card that is not in keep (a released or rebound-away card): its history and its repeat signature go at once.
    void Retain(std::unordered_set<uint64_t> const& keep)
    {
        for (auto it = m_orders.begin(); it != m_orders.end();)
            it = keep.count(it->first) ? std::next(it) : m_orders.erase(it);
        for (auto it = m_last.begin(); it != m_last.end();)
            it = keep.count(it->first) ? std::next(it) : m_last.erase(it);
    }

    size_t Cards() const { return m_orders.size(); }

private:
    std::map<uint64_t, std::deque<CT::OrderRecord>> m_orders;
    std::map<uint64_t, std::string> m_last;
};

// The verb of a run order string: its first word ("pull 12 body" -> "pull", "revive party" -> "revive"), the detail the rest.
inline void SplitRunOrder(std::string const& order, std::string& verb, std::string& detail)
{
    size_t const space = order.find(' ');
    verb = order.substr(0, space);
    detail = space == std::string::npos ? std::string() : order.substr(space + 1);
}

// ---- the role of a host bot from the lines `.tb where` prints (BotMgr::DescribeBrain) ----
// The engine's role strategies (AiFactory): tanks get "tank assist" / "tank face" / "tank feral", healers "holy" / "discipline" / "restoration". Empty while
// the engine is off. Whole strategy names only.
inline std::string RoleOfBrainLines(std::vector<std::string> const& lines)
{
    std::string const prefix = "combat strategies: ";
    if (lines.empty() || lines.front().compare(0, 13, "brain: engine") != 0)
        return "";
    std::string combat;
    for (std::string const& line : lines)
        if (line.compare(0, prefix.size(), prefix) == 0)
            combat = line.substr(prefix.size());
    auto const has = [&](char const* name) { return (", " + combat + ",").find(std::string(", ") + name + ",") != std::string::npos; };
    if (has("tank assist") || has("tank face") || has("tank feral"))
        return "tank";
    if (has("holy") || has("discipline") || has("restoration"))
        return "healer";
    return "dps";
}

// ---- one card as the dashboard's binding ----
struct BotView
{
    std::string name, className, role, powerType;
    uint32_t guid = 0, level = 0, hp = 0, maxHp = 0, power = 0, maxPower = 0;
    bool alive = true, lead = false;
    uint32_t map = 0, zone = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int subgroup = -1;
};

struct CardView
{
    std::string commanderName;      // "offline #<guid>" when the character cannot be named
    uint32_t commanderGuid = 0;
    bool commanderOnline = false;
    bool commanderPlaced = false;   // online and in the world: map and x/y are valid
    uint32_t commanderMap = 0;
    float commanderX = 0.0f, commanderY = 0.0f, commanderZ = 0.0f;   // z is for `.tb commander status` only (the datagram has x and y)
    uint32_t groupId = 0;           // GroupIdW2: the session counter, 0 = the lead is in no group
    std::string leadName;
    int64_t boundAt = 0;
    std::vector<BotView> bots;      // the lead first
    std::vector<CT::OrderRecord> orders;   // oldest first
};

// The Turtle binding record, filled the way the Turtle collector fills it (commands/Commander.cpp CollectCommanderTelemetry): mobs, objects, bosses and the
// ready/cast extras are NOT reported (the card greys those layers out until package P4), a bot row carries no `extra`.
inline CT::BindingInfo BindingOf(CardView const& v)
{
    CT::BindingInfo b;
    b.commander = v.commanderName;
    b.commanderGuid = v.commanderGuid;
    b.commanderOnline = v.commanderOnline;
    b.groupId = v.groupId;
    b.lead = v.leadName;
    b.orders = v.orders;
    b.commanderPlaced = v.commanderPlaced;
    b.commanderMap = v.commanderMap;
    b.commanderX = v.commanderX;
    b.commanderY = v.commanderY;
    b.boundAt = v.boundAt;
    for (BotView const& m : v.bots)
    {
        CT::BotInfo bot;
        bot.name = m.name;
        bot.guid = m.guid;
        bot.className = m.className;
        bot.role = m.role;
        bot.level = m.level;
        bot.hp = m.hp;
        bot.maxHp = m.maxHp;
        bot.power = m.power;
        bot.maxPower = m.maxPower;
        bot.powerType = m.powerType;
        bot.alive = m.alive;
        bot.lead = m.lead;
        bot.mapId = m.map;
        bot.zoneId = m.zone;
        bot.x = m.x;
        bot.y = m.y;
        bot.z = m.z;
        bot.subgroup = m.subgroup;
        b.bots.push_back(std::move(bot));
    }
    return b;
}

} // namespace pure
} // namespace cmd
} // namespace TortoiseBots
