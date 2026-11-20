// Commander port, package P1: see TbCmdBinding.h. The decisions (what a card is, how a run shares one) are in TbCmdBindingPure.h and tested standalone;
// this file reads the live objects and writes the replies.
#include "host/cmd/TbCmdBinding.h"

#include "Chat.h"

#include <cctype>
#include <string>
#include <string_view>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "TbBotMgr.h"
#include "host/TbH3.h"
#include "host/run/TbRun.h"

#include "CharacterCache.h"
#include "GameTime.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <unordered_set>

namespace TortoiseBots
{
namespace cmd
{
namespace
{
namespace P = ::TortoiseBots::cmd::pure;
namespace CT = ::TortoiseBots::CommanderTelemetry;

constexpr uint32 kRoleRefreshMs = 10000;     // a bot's role is re-read from its brain this often (DescribeBrain builds lines)

// What the module keeps BESIDE h3's table (decision D1): guids and times only (decision D2), never a group number.
struct Meta
{
    ObjectGuid lead;            // the party leader `.tb master party <leader> <commander>` named, empty = not named
    int64 boundAt = 0;          // unix seconds the card was first seen standing
};

struct State
{
    std::map<ObjectGuid, Meta> metas;               // by commander
    P::OrderStore log;                              // the cards' last orders, by card key; no whole-store commander cap
    struct Role { std::string role; uint32 atMs = 0; };
    std::map<ObjectGuid, Role> roles;
};

State& Get()
{
    static State state;
    return state;
}

uint32 Ctr(ObjectGuid guid)
{
    return uint32(guid.GetCounter());
}

ObjectGuid PlayerGuid(uint32 counter)
{
    return ObjectGuid::Create<HighGuid::Player>(counter);
}

Player* InWorld(ObjectGuid guid)
{
    Player* player = ObjectAccessor::FindPlayer(guid);
    return player && player->IsInWorld() ? player : nullptr;
}

// The Turtle record's group id is the group's session counter (decision D2: GroupIdW2, `compat/TbCompat_W2.h`; never GetDbStoreId, which is reused).
uint32 GroupKey(Group const* group)
{
    return group ? uint32(group->GetGUID().GetCounter()) : 0u;
}

char const* ClassName(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR: return "warrior";
        case CLASS_PALADIN: return "paladin";
        case CLASS_HUNTER: return "hunter";
        case CLASS_ROGUE: return "rogue";
        case CLASS_PRIEST: return "priest";
        case CLASS_SHAMAN: return "shaman";
        case CLASS_MAGE: return "mage";
        case CLASS_WARLOCK: return "warlock";
        case CLASS_DRUID: return "druid";
        default: return "unknown";
    }
}

char const* PowerName(Powers power)
{
    switch (power)
    {
        case POWER_MANA: return "mana";
        case POWER_RAGE: return "rage";
        case POWER_FOCUS: return "focus";
        case POWER_ENERGY: return "energy";
        default: return "";
    }
}

// The roster's display values: rage and runic power are stored in tenths.
uint32 ShownPower(Powers power, int32 value)
{
    int32 const shown = power == POWER_RAGE || power == POWER_RUNIC_POWER ? value / 10 : value;
    return shown < 0 ? 0u : uint32(shown);
}

std::string RoleOf(Player* bot, uint32 nowMs)
{
    State::Role& cached = Get().roles[bot->GetGUID()];
    if (cached.atMs == 0 || nowMs - cached.atMs >= kRoleRefreshMs)
    {
        cached.role = P::RoleOfBrainLines(BotMgr::Instance().DescribeBrain(bot->GetGUID()));
        cached.atMs = nowMs ? nowMs : 1;
    }
    return cached.role;
}

std::string NameOf(ObjectGuid guid)
{
    if (Player* player = ObjectAccessor::FindPlayer(guid))
        return player->GetName();
    std::string name;
    if (sCharacterCache->GetCharacterNameByGuid(guid, name))
        return name;
    return "offline #" + std::to_string(Ctr(guid));
}

P::BotView BotOf(Player* player, Group* group, bool lead, uint32 nowMs)
{
    P::BotView m;
    m.name = player->GetName();
    m.guid = Ctr(player->GetGUID());
    m.className = ClassName(player->GetClass());
    m.role = BotMgr::Instance().IsBot(player->GetGUID()) ? RoleOf(player, nowMs) : std::string();
    Powers const power = player->GetPowerType();
    m.powerType = PowerName(power);
    m.level = player->GetLevel();
    m.hp = uint32(player->GetHealth());
    m.maxHp = uint32(player->GetMaxHealth());
    m.power = ShownPower(power, player->GetPower(power));
    m.maxPower = ShownPower(power, player->GetMaxPower(power));
    m.alive = player->IsAlive();
    m.lead = lead;
    m.map = player->GetMapId();
    m.zone = player->GetZoneId();
    m.x = player->GetPositionX();
    m.y = player->GetPositionY();
    m.z = player->GetPositionZ();
    m.subgroup = group && group->isRaidGroup() ? int(group->GetMemberGroup(player->GetGUID())) : -1;
    return m;
}

// One card with everything the replies and the dashboard need.
struct HostCard
{
    P::Card card;
    P::CardView view;
    run::RunRec const* run = nullptr;
    bool inParty = false, raid = false, playerLed = false;
    uint32 botsBound = 0;       // guids bound, online or not (view.bots holds the ones in the world)
};

// The commander key a bot's orders are logged under: the card it is shown on. A bot bound to a run's tank shows on the card of the tank's own commander
// when it has one (the run keeps its members' old commander for the end), else on the tank's system card.
ObjectGuid CardKeyOf(ObjectGuid commander)
{
    if (commander.IsEmpty())
        return commander;
    run::RunRec const* rec = run::Find(commander);
    if (rec && !run::Ended(*rec))
    {
        ObjectGuid const above = h3::CommanderGuid(commander);
        return above.IsEmpty() ? commander : above;
    }
    return commander;
}

// The card an order for this bot is logged under. A bot bound to a commander shows on that commander's card (a run's fold included); a run's leader the
// run never rebound (a system run) stands on the run's own card, keyed by itself.
ObjectGuid CardKeyOfBot(ObjectGuid bot)
{
    ObjectGuid const commander = h3::CommanderGuid(bot);
    if (!commander.IsEmpty())
        return CardKeyOf(commander);
    run::RunRec const* rec = run::Find(bot);
    return rec && !run::Ended(*rec) ? bot : ObjectGuid::Empty;
}

// The card keys that stand now: every valid binding's card and every run that goes on. The same keys BuildCards returns, so the state kept beside h3's
// table is pruned from the whole collection and never from a capped view.
std::unordered_set<uint64> StandingKeys()
{
    std::unordered_set<uint64> keep;
    for (auto const& binding : h3::CommanderBindings())
    {
        ObjectGuid const key = CardKeyOf(binding.second);
        if (!key.IsEmpty())
            keep.insert(Ctr(key));
    }
    for (run::RunRec* rec : run::All())
        if (!run::Ended(*rec))
            keep.insert(Ctr(CardKeyOf(rec->leader)));
    return keep;
}

// A released binding (or a rebind that leaves a commander no bot) takes its record, its order log and its repeat suppression with it at once, not only at
// the next read.
void PruneToStanding(State& s)
{
    std::unordered_set<uint64> const keep = StandingKeys();
    for (auto it = s.metas.begin(); it != s.metas.end();)
        it = keep.count(Ctr(it->first)) ? std::next(it) : s.metas.erase(it);
    s.log.Retain(keep);
}

std::vector<HostCard> Read(int64 nowUnix, uint32 nowMs)
{
    State& s = Get();
    std::vector<P::Pair> pairs;
    for (auto const& [bot, commander] : h3::CommanderBindings())
        pairs.push_back({ Ctr(bot), Ctr(commander) });

    std::vector<run::RunRec const*> runRecs;
    std::vector<P::RunIn> runs;
    for (run::RunRec* rec : run::All())
    {
        if (run::Ended(*rec))
            continue;
        P::RunIn r;
        r.leader = Ctr(rec->leader);
        r.leaderCommander = Ctr(h3::CommanderGuid(rec->leader));
        r.index = runRecs.size();
        if (Player* lead = InWorld(rec->leader))
            if (Group* group = lead->GetGroup())
                for (Group::MemberSlot const& slot : group->GetMemberSlots())
                    P::AddUnique(r.members, Ctr(slot.guid));
        // While the leader loads or teleports the live group is not readable: the run's own bound members still say who is on its card. BuildCards keeps
        // only the candidates h3's table binds to the leader, so neither list can add a human player or an unbound bot.
        for (auto const& member : rec->bound)
            P::AddUnique(r.members, Ctr(member.first));
        runRecs.push_back(rec);
        runs.push_back(std::move(r));
    }

    // A card seen for the first time gets its boundAt now (T: "the unix second the binding was first seen standing").
    auto const touch = [&](uint32 commander)
    {
        if (!commander)
            return;
        Meta& m = s.metas[PlayerGuid(commander)];
        if (!m.boundAt)
            m.boundAt = nowUnix;
    };
    for (P::Pair const& p : pairs)
        touch(p.commander);
    for (P::RunIn const& r : runs)
        touch(r.leaderCommander ? r.leaderCommander : r.leader);
    PruneToStanding(s);

    std::vector<P::MetaIn> metas;
    for (auto const& [commander, m] : s.metas)
        metas.push_back({ Ctr(commander), Ctr(m.lead), m.boundAt });
    std::map<uint32, uint32> groupLeader;
    for (P::Pair const& p : pairs)
        if (Player* bot = InWorld(PlayerGuid(p.bot)))
            if (Group* group = bot->GetGroup())
                groupLeader[p.bot] = Ctr(group->GetLeaderGUID());

    std::vector<P::Card> const cards = P::BuildCards(pairs, runs, metas, groupLeader);

    for (auto it = s.roles.begin(); it != s.roles.end();)
        it = nowMs - it->second.atMs > 60000 ? s.roles.erase(it) : std::next(it);

    std::vector<HostCard> out;
    for (P::Card const& c : cards)
    {
        HostCard h;
        h.card = c;
        h.run = c.run >= 0 ? runRecs[size_t(c.run)] : nullptr;
        h.botsBound = uint32(c.bots.size());
        P::CardView& v = h.view;
        ObjectGuid const commanderGuid = PlayerGuid(c.commander);
        Player* commander = InWorld(commanderGuid);
        v.commanderGuid = c.commander;
        v.commanderName = NameOf(commanderGuid);
        v.commanderOnline = commander != nullptr;
        if (commander)
        {
            v.commanderPlaced = true;
            v.commanderMap = commander->GetMapId();
            v.commanderX = commander->GetPositionX();
            v.commanderY = commander->GetPositionY();
            v.commanderZ = commander->GetPositionZ();
        }
        Player* lead = InWorld(PlayerGuid(c.lead));
        Group* group = lead ? lead->GetGroup() : nullptr;
        v.groupId = GroupKey(group);
        v.leadName = NameOf(PlayerGuid(c.lead));
        v.boundAt = c.boundAt;
        h.inParty = !c.system && group && group->IsMember(commanderGuid);   // a system card's commander IS the lead: T never marks it in-party
        h.raid = group && group->isRaidGroup();
        h.playerLed = !c.system && !BotMgr::Instance().IsBot(commanderGuid);
        for (uint32 guid : c.bots)
            if (Player* bot = InWorld(PlayerGuid(guid)))
                v.bots.push_back(BotOf(bot, bot->GetGroup(), guid == c.lead, nowMs));
        v.orders = s.log.Recent(c.commander);
        out.push_back(std::move(h));
    }
    return out;
}

void NoteLog(ObjectGuid commander, CT::OrderRecord rec)
{
    if (commander.IsEmpty())
        return;
    Get().log.Note(Ctr(commander), std::move(rec));
}
}

// ---- hooks ----

void NoteMaster(std::vector<Player*> const& bots, Player* leader, Player* commander)
{
    State& s = Get();
    int64 const now = int64(GameTime::GetGameTime());
    if (!commander)
    {
        // A release (or a rebind that left a commander no bot) drops the card's record and order log at once, so a later bind starts a new card.
        PruneToStanding(s);
        return;
    }
    // Only what h3 holds now counts: a bot the host refused is not bound, whatever the command asked.
    bool anyBound = false, leaderBound = false;
    for (Player* bot : bots)
        if (h3::CommanderGuid(bot->GetGUID()) == commander->GetGUID())
        {
            anyBound = true;
            leaderBound = leaderBound || bot == leader;
        }
    if (!anyBound)
        return;
    Meta& m = s.metas[commander->GetGUID()];
    if (!m.boundAt)
        m.boundAt = now;
    if (leader && leaderBound)
        m.lead = leader->GetGUID();
    PruneToStanding(s);     // a commander whose last bot was rebound elsewhere is gone from h3's table now: its log and lead go with it
}

void NoteOrder(std::vector<std::pair<Player*, std::string>> const& results, std::string const& verb, std::string const& arg)
{
    std::map<uint32, uint32> cardOfBot;
    std::vector<P::OrderOutcome> outcomes;
    for (auto const& [bot, refusal] : results)
    {
        ObjectGuid const key = CardKeyOfBot(bot->GetGUID());
        if (key.IsEmpty())
            continue;
        uint32 const botGuid = Ctr(bot->GetGUID());
        cardOfBot[botGuid] = Ctr(key);
        outcomes.push_back({ botGuid, refusal });
    }
    if (outcomes.empty())
        return;
    int64 const now = int64(GameTime::GetGameTime());
    for (auto const& [card, tally] : P::TallyByCard(outcomes, cardOfBot))
    {
        bool const ok = tally.queued > 0;
        std::string text = "queued " + std::to_string(tally.queued) + " of " + std::to_string(tally.queued + tally.refused);
        if (tally.refused)
            text += ", refused " + std::to_string(tally.refused);
        NoteLog(PlayerGuid(card), P::MakeOrder(now, verb, arg, ok, tally.firstRefusal, ok ? text : std::string()));
    }
}

void NoteRunOrder(ObjectGuid leader, std::string const& order, bool accepted, std::string const& code, std::string const& text)
{
    ObjectGuid const above = h3::CommanderGuid(leader);
    std::string verb, detail;
    P::SplitRunOrder(order, verb, detail);
    NoteLog(above.IsEmpty() ? leader : above, P::MakeOrder(int64(GameTime::GetGameTime()), verb, detail, accepted, code, text));
}

void AppendBindings(std::vector<pure::CT::BindingInfo>& out, std::vector<uint32> const& runLeadersShown)
{
    for (HostCard const& h : Read(int64(GameTime::GetGameTime()), GameTime::GetGameTimeMS()))
    {
        if (std::find(runLeadersShown.begin(), runLeadersShown.end(), h.card.lead) != runLeadersShown.end())
            continue;
        // The datagram's cap, not the card collection's: `.tb commander status` shows every card and the state beside the table is pruned from all of them.
        if (out.size() >= CT::kMaxCommandersReported)
            return;
        out.push_back(P::BindingOf(h.view));
    }
}

// ---- `.tb commander status [<commander>] [#token]` ----
// commander-begin #t
// binding cmd= cguid= online= lead= group= bound= bots= system= inparty= raid= playerled= boundat= orders= [map= pos=] #t
// member cmd= bot= guid= class= role= level= hp= maxhp= power= maxpower= ptype= alive= lead= map= zone= pos= [sub=] #t       (one per bound bot in the world, the lead first)
// order cmd= at= verb= detail= ok= result= #t                                                                              (oldest first, at most 8)
// run cmd= lead= plan= state= ... #t                                                                                       (the `.tb run status` record of a card that runs)
// commander-end count= records= bindings= #t
bool HandleCommander(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    using namespace h3;
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "commander"))
        return false;
    if (args.w.empty())
        return Fail(handler, args, "commander", "missing-verb", "status");
    if (Lower(args.w[0]) != "status")
        return Fail(handler, args, "commander", "unknown-verb", args.w[0]);
    if (args.w.size() > 2)
        return Fail(handler, args, "commander", "unexpected-argument", args.w[2]);
    int64 const nowUnix = int64(GameTime::GetGameTime());
    uint32 const nowMs = GameTime::GetGameTimeMS();
    std::vector<HostCard> cards = Read(nowUnix, nowMs);
    if (args.w.size() == 2)
    {
        std::string const wanted = Lower(args.w[1]);
        cards.erase(std::remove_if(cards.begin(), cards.end(), [&](HostCard const& h) { return Lower(h.view.commanderName) != wanted; }), cards.end());
        if (cards.empty())
            return Fail(handler, args, "commander", "no-binding", args.w[1] + " commands no bots");
    }
    Reply reply(handler, args, "commander");
    reply.Begin();
    for (HostCard const& h : cards)
    {
        P::CardView const& v = h.view;
        Rec b("binding");
        b.S("cmd", v.commanderName).N("cguid", v.commanderGuid).B("online", v.commanderOnline).S("lead", v.leadName);
        if (v.groupId)
            b.N("group", v.groupId);
        b.N("bound", h.botsBound).N("bots", v.bots.size()).B("system", h.card.system).B("inparty", h.inParty).B("raid", h.raid).B("playerled", h.playerLed);
        b.N("boundat", v.boundAt).N("orders", v.orders.size());
        if (v.commanderPlaced)
            b.N("map", v.commanderMap).Coord("pos", v.commanderX, v.commanderY, v.commanderZ);
        reply.Line(b);
        for (P::BotView const& m : v.bots)
        {
            Rec r("member");
            r.S("cmd", v.commanderName).S("bot", m.name).N("guid", m.guid).S("class", m.className).S("role", m.role).N("level", m.level);
            r.N("hp", m.hp).N("maxhp", m.maxHp).N("power", m.power).N("maxpower", m.maxPower).S("ptype", m.powerType).B("alive", m.alive).B("lead", m.lead);
            r.N("map", m.map).N("zone", m.zone).Coord("pos", m.x, m.y, m.z);
            if (m.subgroup >= 0)
                r.N("sub", m.subgroup);
            reply.Line(r);
        }
        for (CT::OrderRecord const& o : v.orders)
        {
            Rec r("order");
            r.S("cmd", v.commanderName).N("at", o.at).S("verb", o.verb).T("detail", o.detail).B("ok", o.ok).T("result", o.result);
            reply.Line(r);
        }
        if (h.run)
        {
            Rec r = run::RunRecord(*h.run, nowMs);
            r.S("cmd", v.commanderName);
            reply.Line(r);
        }
    }
    Rec end("commander");
    end.N("bindings", cards.size());
    reply.End(end);
    return true;
}
}
}

#else   // not built with the slice: `.tb commander` says so, the hooks do nothing

namespace TortoiseBots
{
namespace cmd
{
bool HandleCommander(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    static char const* const text = "the engine is not built in (this build has no group-fight commands)";
    std::string msg;
    for (char const* c = text; *c; ++c)
    {
        unsigned char const u = static_cast<unsigned char>(*c);
        if (std::isalnum(u) || u == '.' || u == '_' || u == '-')
            msg += *c;
        else
        {
            static char const* const hex = "0123456789ABCDEF";
            msg += '%';
            msg += hex[u >> 4];
            msg += hex[u & 15];
        }
    }
    std::string line = "error code=engine-not-built cmd=commander msg=" + msg;
    // The echo token: the last word when it starts with '#', 2-33 characters of [A-Za-z0-9_.:-] (as TbH3.cpp Split).
    std::string_view const tailText(args);
    size_t end = tailText.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(tailText[end - 1])))
        --end;
    size_t start = end;
    while (start > 0 && !std::isspace(static_cast<unsigned char>(tailText[start - 1])))
        --start;
    std::string_view const word = tailText.substr(start, end - start);
    bool good = word.size() >= 2 && word.size() <= 33 && word[0] == '#';
    for (size_t i = 1; good && i < word.size(); ++i)
        good = std::isalnum(static_cast<unsigned char>(word[i])) || word[i] == '_' || word[i] == '.' || word[i] == ':' || word[i] == '-';
    if (good)
        line += " " + std::string(word);
    handler->SendSysMessage(line);
    handler->SetSentErrorMessage(true);
    return false;
}

void NoteMaster(std::vector<Player*> const&, Player*, Player*) {}
void NoteOrder(std::vector<std::pair<Player*, std::string>> const&, std::string const&, std::string const&) {}
void NoteRunOrder(ObjectGuid, std::string const&, bool, std::string const&, std::string const&) {}
void AppendBindings(std::vector<pure::CT::BindingInfo>&, std::vector<uint32> const&) {}
}
}

#endif
