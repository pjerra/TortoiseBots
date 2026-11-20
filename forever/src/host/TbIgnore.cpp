// Slice 13, the `.tb ignore` command: see host/TbIgnore.h. The command parsing and the replies use the core and TbH3.h only; the engine-facing part is behind
// TORTOISEBOTS_SLICE1 exactly as TbOrders.cpp keeps it, and the default build answers `error code=engine-not-built` (the second half of this file).
#include "host/TbIgnore.h"
#include "host/TbH3.h"
#include "TbBotMgr.h"

#include "Chat.h"
#include "Player.h"

#include <ctime>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <map>
#include "ObjectAccessor.h"
#include "Unit.h"

#include "ai/playerbot/policy/combat/IgnoreMobPolicy.h"

namespace TortoiseBots
{
namespace i13
{
using namespace h3;

namespace
{
// One commander's list, with when dead units it could see were last dropped.
struct CommanderState
{
    ai::IgnoreMobList<ObjectGuid> list;
    uint32_t lastPruneSec = 0;
};

std::map<uint32_t, CommanderState>& Store()
{
    static std::map<uint32_t, CommanderState> store;
    return store;
}

uint32_t NowSec() { return uint32_t(std::time(nullptr)); }

// Dead entries are re-checked at most this often (the check is one map lookup per entry).
constexpr uint32_t kPruneEverySec = 5;

// The bot's bound commander as a guid counter (0 = none bound).
uint32_t CommanderOf(Player* bot)
{
    if (!bot)
        return 0;
    ObjectGuid commander = CommanderGuid(bot->GetGUID());
    return commander.IsEmpty() ? 0 : commander.GetCounter();
}

std::string CommanderName(ObjectGuid commander)
{
    if (commander.IsEmpty())
        return {};
    if (Player* player = ObjectAccessor::FindPlayer(commander))
        if (player->IsInWorld())
            return player->GetName();
    return {};
}

// Drop expired entries and dead units `seer` can still see (a unit on another
// map is kept: it cannot be told apart from a despawned one there, and the
// 30-minute expiry is the backstop for those).
void Prune(uint32_t commander, Player* seer, uint32_t nowSec)
{
    auto it = Store().find(commander);
    if (it == Store().end())
        return;
    it->second.list.PruneExpired(nowSec);
    if (seer && seer->IsInWorld() && !seer->IsBeingTeleported())
        for (ai::IgnoreMobEntry<ObjectGuid> const& entry : it->second.list.Live(nowSec))
        {
            Unit* unit = ObjectAccessor::GetUnit(*seer, entry.guid);
            if (unit && !unit->IsAlive())
                it->second.list.Remove(entry.guid);
        }
    it->second.lastPruneSec = nowSec;
}

bool IsVerb(std::string const& word)
{
    return word == "add" || word == "remove" || word == "list" || word == "clear";
}
}

bool IsIgnored(Player* bot, ObjectGuid guid)
{
    if (!bot || guid.IsEmpty())
        return false;
    uint32_t const commander = CommanderOf(bot);
    if (!commander)
        return false;
    auto it = Store().find(commander);
    if (it == Store().end())
        return false;
    uint32_t const now = NowSec();
    if (now - it->second.lastPruneSec >= kPruneEverySec)
        Prune(commander, bot, now);
    return it->second.list.Contains(guid, now);
}

bool NoteExplicitTarget(Player* bot, ObjectGuid guid)
{
    if (!bot || guid.IsEmpty())
        return false;
    uint32_t const commander = CommanderOf(bot);
    if (!commander)
        return false;
    auto it = Store().find(commander);
    if (it == Store().end())
        return false;
    return it->second.list.Remove(guid);
}

// ---- `.tb ignore <bot|party [leader]> add <guid> | remove <guid> | list | clear [#token]` ----
bool HandleIgnore(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "ignore"))
        return false;
    // `party` takes the next word as the leader unless that word is already the verb.
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party" && !IsVerb(Lower(args.w[1]));
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "ignore", who.code, who.detail);
    if (pos >= args.w.size())
        return Fail(handler, args, "ignore", "missing-verb", "add <guid> | remove <guid> | list | clear");
    std::string const verb = Lower(args.w[pos++]);
    if (!IsVerb(verb))
        return Fail(handler, args, "ignore", "unknown-verb", "add <guid> | remove <guid> | list | clear");
    std::string guidWord;
    if (verb == "add" || verb == "remove")
    {
        if (pos >= args.w.size())
            return Fail(handler, args, "ignore", "missing-argument", verb + " <the 32 hex digits of a guid>");
        guidWord = args.w[pos++];
    }
    if (pos < args.w.size())
        return Fail(handler, args, "ignore", "unexpected-argument", args.w[pos]);
    // One commander owns the list: every addressed bot must answer to the same one.
    ObjectGuid commanderGuid;
    for (Player* bot : who.bots)
    {
        ObjectGuid const bound = CommanderGuid(bot->GetGUID());
        if (bound.IsEmpty())
            return Fail(handler, args, "ignore", "no-commander",
                bot->GetName() + " is bound to no commander (`.tb master <bot> <commander>`)");
        if (commanderGuid.IsEmpty())
            commanderGuid = bound;
        else if (!(commanderGuid == bound))
            return Fail(handler, args, "ignore", "ambiguous-party",
                "the bots answer to different commanders; address one commander's bots");
    }
    uint32_t const commander = commanderGuid.GetCounter();
    std::string const commanderName = CommanderName(commanderGuid);
    uint32_t const now = NowSec();
    Prune(commander, who.bots.front(), now);

    Reply reply(handler, args, "ignore");
    reply.Begin();
    uint32_t ok = 0;
    if (verb == "list")
    {
        for (ai::IgnoreMobEntry<ObjectGuid> const& entry : Store()[commander].list.Live(now))
        {
            Rec rec("ignore");
            rec.S("commander", commanderName).S("verb", "list").S("status", "ok")
                .S("guid", GuidText(entry.guid)).S("name", entry.name)
                .N("expires", Store()[commander].list.ExpiresIn(entry.guid, now));
            reply.Line(rec);
            ++ok;
        }
    }
    else if (verb == "clear")
    {
        uint32_t const cleared = Store()[commander].list.Clear();
        Rec rec("ignore");
        rec.S("commander", commanderName).S("verb", "clear").S("status", "ok").N("cleared", cleared);
        reply.Line(rec);
        ok = 1;
    }
    else
    {
        ObjectGuid guid;
        if (!ParseGuidText(guidWord, guid))
            return Fail(handler, args, "ignore", "bad-target", "a unit is the 32 hex digits of a guid");
        if (verb == "add")
        {
            // Any addressed bot's map may hold the unit (a party split across maps).
            Player* seer = nullptr;
            Unit* unit = nullptr;
            for (Player* bot : who.bots)
            {
                Unit* seen = ObjectAccessor::GetUnit(*bot, guid);
                if (seen && seen->IsInWorld())
                {
                    seer = bot;
                    unit = seen;
                    break;
                }
            }
            if (!unit)
                return Fail(handler, args, "ignore", "target-not-found", "no such unit on the bots' maps");
            if (!unit->IsAlive())
                return Fail(handler, args, "ignore", "target-dead", "the target is dead");
            if (unit->IsPlayer())
                return Fail(handler, args, "ignore", "bad-target", "a listed mob is a creature");
            if (!seer->IsValidAttackTarget(unit))
                return Fail(handler, args, "ignore", "not-attackable", "the core says the bots may not attack that unit");
            std::string const name = unit->GetName();
            ai::IgnoreMobList<ObjectGuid>::AddResult const added = Store()[commander].list.Add(guid, name, now);
            if (added == ai::IgnoreMobList<ObjectGuid>::AddResult::Duplicate)
                return Fail(handler, args, "ignore", "duplicate", "the list already names that mob");
            if (added == ai::IgnoreMobList<ObjectGuid>::AddResult::Full)
                return Fail(handler, args, "ignore", "too-many", "the list holds 8 mobs already");
            Rec rec("ignore");
            rec.S("commander", commanderName).S("verb", "add").S("status", "ok")
                .S("guid", GuidText(guid)).S("name", name)
                .N("expires", Store()[commander].list.ExpiresIn(guid, now))
                .T("msg", "Bots will leave " + name + " alone.");
            reply.Line(rec);
            ok = 1;
        }
        else
        {
            std::string name;
            for (ai::IgnoreMobEntry<ObjectGuid> const& entry : Store()[commander].list.Live(now))
                if (entry.guid == guid)
                    name = entry.name;
            if (!Store()[commander].list.Remove(guid))
                return Fail(handler, args, "ignore", "not-found", "the list does not name that mob");
            Rec rec("ignore");
            rec.S("commander", commanderName).S("verb", "remove").S("status", "ok")
                .S("guid", GuidText(guid)).S("name", name)
                .T("msg", "Bots may attack " + (name.empty() ? "it" : name) + " again.");
            reply.Line(rec);
            ok = 1;
        }
    }
    Rec end("ignore");
    end.S("verb", verb).S("target", who.label).S("commander", commanderName).N("ok", ok).N("refused", 0);
    reply.End(end);
    return true;
}

void SelfTestRecords(Reply& reply)
{
    reply.Line(Rec("selftest").S("case", "ignore").S("verb", "add").S("commander", "Mcmd"));
    reply.Line(Rec("ignore").S("commander", "Mcmd").S("verb", "add").S("status", "ok")
        .S("guid", "0000000000000000F130002C4A000001").S("name", "Ashenvale Bear").N("expires", 1800));
    reply.Line(Rec("selftest").S("case", "ignore-end").S("verb", "add").S("target", "party:Mtank").N("ok", 1).N("refused", 0));
}
}
}

#else   // not built with the slice: the same refusal the slice-3 stub gives (TbH3Off.cpp)

namespace TortoiseBots
{
namespace i13
{
namespace
{
std::string TokenOf(std::string_view tail)
{
    size_t end = tail.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(tail[end - 1])))
        --end;
    size_t start = end;
    while (start > 0 && !std::isspace(static_cast<unsigned char>(tail[start - 1])))
        --start;
    std::string_view const word = tail.substr(start, end - start);
    if (word.size() < 2 || word.size() > 33 || word[0] != '#')
        return {};
    for (size_t i = 1; i < word.size(); ++i)
    {
        char const c = word[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != ':' && c != '-')
            return {};
    }
    return std::string(word);
}

bool NotBuilt(ChatHandler* handler, Trinity::ChatCommands::Tail args, char const* cmd)
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
    std::string line = std::string("error code=engine-not-built cmd=") + cmd + " msg=" + msg;
    std::string const token = TokenOf(std::string_view(args));
    if (!token.empty())
        line += " " + token;
    handler->SendSysMessage(line);
    handler->SetSentErrorMessage(true);
    return false;
}
}

bool IsIgnored(Player*, ObjectGuid) { return false; }
bool NoteExplicitTarget(Player*, ObjectGuid) { return false; }
bool HandleIgnore(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "ignore"); }
}
}

#endif
