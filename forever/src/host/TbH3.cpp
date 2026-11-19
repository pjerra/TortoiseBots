// Slice 3, host package H3: helpers shared by the order, state and test-kit commands (see TbH3.h). Core headers only: this file is
// built with and without TORTOISEBOTS_SLICE1.
#include "host/TbH3.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <shared_mutex>

namespace TortoiseBots
{
namespace h3
{
namespace
{

std::map<ObjectGuid, ObjectGuid>& Bindings()
{
    static std::map<ObjectGuid, ObjectGuid> bindings;
    return bindings;
}

bool TokenChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == ':' || c == '-';
}

bool Valid(ObjectGuid bot, ObjectGuid commander)
{
    return !commander.IsEmpty() && BotMgr::Instance().MasterOf(bot) == commander;
}
}

Words Split(std::string_view tail)
{
    Words out;
    size_t i = 0;
    while (i < tail.size())
    {
        while (i < tail.size() && std::isspace(static_cast<unsigned char>(tail[i])))
            ++i;
        size_t const start = i;
        while (i < tail.size() && !std::isspace(static_cast<unsigned char>(tail[i])))
            ++i;
        if (i > start)
            out.w.emplace_back(tail.substr(start, i - start));
    }
    if (!out.w.empty() && out.w.back()[0] == '#')
    {
        std::string const word = out.w.back();
        out.w.pop_back();
        bool good = word.size() >= 2 && word.size() <= 33;
        for (size_t k = 1; good && k < word.size(); ++k)
            good = TokenChar(word[k]);
        if (good)
            out.token = word;
        else
            out.badToken = true;
    }
    return out;
}

std::string Enc(std::string_view bytes)
{
    static char const* const hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size());
    for (char c : bytes)
    {
        unsigned char const u = static_cast<unsigned char>(c);
        if ((std::isalnum(u) && u < 0x80) || c == '.' || c == '_' || c == '-')
            out += c;
        else
        {
            out += '%';
            out += hex[u >> 4];
            out += hex[u & 15];
        }
    }
    return out;
}

Rec& Rec::Put(std::string_view key, std::string value)
{
    // A key appears at most once per line: a repeated key replaces the earlier value.
    for (auto& pair : _kv)
        if (pair.first == key)
        {
            pair.second = std::move(value);
            return *this;
        }
    _kv.emplace_back(std::string(key), std::move(value));
    return *this;
}

Rec& Rec::S(std::string_view key, std::string_view text) { return Put(key, Enc(text)); }
Rec& Rec::N(std::string_view key, int64 value) { return Put(key, std::to_string(value)); }
Rec& Rec::F(std::string_view key, double value, int decimals)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return Put(key, buffer);
}
Rec& Rec::B(std::string_view key, bool value) { return Put(key, value ? "yes" : "no"); }
Rec& Rec::L(std::string_view key, std::vector<std::string> const& items)
{
    // An empty ITEM is never written (addendum A9): an empty list is the empty value.
    std::string joined;
    for (std::string const& item : items)
        if (!item.empty())
            joined += (joined.empty() ? "" : ";") + Enc(item);
    return Put(key, std::move(joined));
}
Rec& Rec::P(std::string_view key, std::vector<std::string> const& parts)
{
    std::string joined;
    for (size_t i = 0; i < parts.size(); ++i)
        joined += (i ? ":" : "") + Enc(parts[i]);
    return Put(key, std::move(joined));
}
Rec& Rec::LP(std::string_view key, std::vector<std::vector<std::string>> const& items)
{
    std::string joined;
    for (auto const& parts : items)
    {
        std::string item;
        for (size_t j = 0; j < parts.size(); ++j)
            item += (j ? ":" : "") + Enc(parts[j]);
        bool empty = true;
        for (std::string const& part : parts)
            empty = empty && part.empty();
        if (empty)
            continue;                                    // an empty ITEM is never written (addendum A9)
        joined += (joined.empty() ? "" : ";") + item;
    }
    return Put(key, std::move(joined));
}
Rec& Rec::Coord(std::string_view key, double x, double y, double z)
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%.2f,%.2f,%.2f", x, y, z);
    return Put(key, buffer);
}

void Reply::Send(std::string line)
{
    if (!_args.token.empty())
        line += " " + _args.token;
    _handler->SendSysMessage(line);
}

void Reply::Begin() { Send(_kind + "-begin"); }

void Reply::Line(Rec const& rec)
{
    // The token and the separating space are part of the 3,000 bytes.
    size_t const room = MaxLineBytes - (_args.token.empty() ? 0 : _args.token.size() + 1);
    ++_records;
    auto const& pairs = rec.Pairs();
    std::string const head = pairs.empty() ? std::string() : " " + pairs.front().first + "=" + pairs.front().second;
    std::string line = rec.Kind();
    bool first = true;
    for (auto const& [key, value] : pairs)
    {
        std::string word = " " + key + "=" + value;
        if (word.size() + rec.Kind().size() + 1 > room)       // one value alone is too long: cut it on a %-escape boundary
        {
            size_t keep = room > rec.Kind().size() + 2 + key.size() + 1 + head.size() + 1 ? room - rec.Kind().size() - 2 - key.size() - 1 - head.size() - 1 : 0;
            if (keep > value.size())
                keep = value.size();
            while (keep > 0 && (value[keep - 1] == '%' || (keep >= 2 && value[keep - 2] == '%')))
                --keep;
            // A value longer than a whole line (a long strategy list) is cut at an item boundary (`;`), else on a %XX boundary.
            size_t cut = value.rfind(';', keep ? keep - 1 : 0);
            if (cut != std::string::npos && cut > 0 && cut < keep)
                keep = cut;
            word = " " + key + "=" + value.substr(0, keep);
        }
        if (!first && line.size() + word.size() > room)
        {
            Send(line);
            ++_lines;
            line = rec.Kind() + "+" + head;           // the continuation repeats the first key (the record's identity)
        }
        line += word;
        first = false;
    }
    Send(line);
    ++_lines;
}

void Reply::Single(Rec const& rec)
{
    uint32 const before = _lines, beforeRecords = _records;
    Line(rec);
    _lines = before;
    _records = beforeRecords;
}

void Reply::End(Rec extra)
{
    std::string line = _kind + "-end count=" + std::to_string(_lines) + " records=" + std::to_string(_records);
    for (auto const& [key, value] : extra.Pairs())
        if (key != "count" && key != "records")
            line += " " + key + "=" + value;
    Send(line);
}

std::vector<std::string> LabelParts(std::string const& label)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (true)
    {
        size_t const colon = label.find(':', start);
        parts.push_back(label.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
        if (colon == std::string::npos)
            break;
        start = colon + 1;
    }
    return parts;
}

bool Fail(ChatHandler* handler, Words const& args, std::string_view cmd, std::string_view code, std::string_view detail)
{
    // `error` has exactly `code=`, `cmd=`, an optional `msg=` and the token (addendum A11); a refused `#` word is said in the message.
    std::string text(detail.substr(0, 400));
    if (args.badToken)
        text += (text.empty() ? "" : " ") + std::string("(the # word was refused: a token is # and 1 to 32 of A-Za-z0-9_.:-)");
    Rec rec("error");
    rec.S("code", code).S("cmd", cmd);
    if (!text.empty())
        rec.S("msg", std::string_view(text).substr(0, 400));
    Reply(handler, args, "error").Single(rec);
    handler->SetSentErrorMessage(true);
    return false;
}

bool HandleGrammar(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (args.w.empty() || Lower(args.w[0]) != "selftest")
        return Fail(handler, args, "grammar", "missing-argument", ".tb grammar selftest");
    Reply reply(handler, args, "grammar");
    reply.Begin();
    // Every special byte in one value: space = : ; # % _ , + | and a non-ASCII byte pair; an empty value.
    reply.Line(Rec("selftest").S("case", "specials").S("value", "a b=c:d;e#f%g_h,i+j|k\xC3\xA9").S("empty", ""));
    // Lists: an empty ITEM is dropped (never `;;` or a trailing `;`), an empty list is the empty value.
    reply.Line(Rec("selftest").S("case", "list").L("items", {"x y", "a;b", "", "50%"}).LP("pairs", {{"Mtank", "343"}, {"a:b", "1"}, {"", ""}}).L("none", {}));
    // A position is ONE key with at most two fraction digits, next to map=; an unknown number is an omitted key (no `unknown` here).
    reply.Line(Rec("selftest").S("case", "coords").Coord("pos", -601.256, -4250.1, 38.944).N("map", 389).B("flag", true).N("number", -17).F("fraction", 12.345, 2));
    reply.Line(Rec("selftest").S("case", "once").S("key", "first").S("key", "second"));          // a key appears at most once: the second value replaces
    std::vector<std::string> many;
    for (int i = 0; i < 320; ++i)
        many.push_back("strategy number " + std::to_string(i));
    reply.Line(Rec("selftest").S("bot", "Mtank").S("case", "overlong").L("list", many).S("tail", "x"));   // over 3,000 bytes: `selftest+ bot=Mtank ...` continuation lines
    Rec end("selftest");
    end.S("note", "fixed records").B("ready", false).LP("why", {{"resting", "Mpriest"}, {"far", "Mdpsb"}});     // why= is present exactly when ready=no
    reply.End(end);
    // The refusal, the only one-line reply: exactly code=, cmd= and an optional msg=.
    Reply error(handler, args, "error");
    error.Single(Rec("error").S("code", "selftest").S("cmd", "grammar").S("msg", "a b=c;d"));
    return true;
}

bool RequireBuilt(ChatHandler* handler, Words const& args, std::string_view cmd)
{
    if (engine::Built())
        return true;
    return Fail(handler, args, cmd, "engine-not-built", "the engine is not built in (this build has no group-fight commands)");
}

std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string GuidText(ObjectGuid guid)
{
    if (guid.IsEmpty())
        return {};
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%016llX%016llX", static_cast<unsigned long long>(guid.GetRawValue(1)),
        static_cast<unsigned long long>(guid.GetRawValue(0)));
    return buffer;
}

bool ParseGuidText(std::string_view text, ObjectGuid& out)
{
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text.remove_prefix(2);
    if (text.size() != 32)
        return false;
    for (char c : text)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    std::string const high(text.substr(0, 16));
    std::string const low(text.substr(16, 16));
    out.SetRawValue(std::strtoull(high.c_str(), nullptr, 16), std::strtoull(low.c_str(), nullptr, 16));
    return !out.IsEmpty();
}

Player* SenderPlayer(ChatHandler* handler)
{
    return handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
}

Player* FindOnline(std::string_view name)
{
    std::string normalized(name);
    if (!normalizePlayerName(normalized))
        return nullptr;
    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(normalized);
    if (guid.IsEmpty())
        return nullptr;
    Player* player = ObjectAccessor::FindPlayer(guid);
    return player && player->IsInWorld() ? player : nullptr;
}

bool IsHostBot(Player const* player)
{
    return player && BotMgr::Instance().IsBot(player->GetGUID());
}

Player* CommanderOf(Player* bot)
{
    if (!bot)
        return nullptr;
    ObjectGuid const master = BotMgr::Instance().MasterOf(bot->GetGUID());
    if (master.IsEmpty())
        return nullptr;
    Player* player = ObjectAccessor::FindPlayer(master);
    return player && player->IsInWorld() ? player : nullptr;
}

std::vector<Player*> AllHostBots()
{
    std::vector<Player*> bots;
    std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        if (player && player->IsInWorld() && IsHostBot(player))
            bots.push_back(player);
    std::sort(bots.begin(), bots.end(), [](Player const* a, Player const* b) { return a->GetName() < b->GetName(); });
    return bots;
}

void SetCommander(ObjectGuid bot, ObjectGuid commander)
{
    if (commander.IsEmpty())
        Bindings().erase(bot);
    else
        Bindings()[bot] = commander;
}

ObjectGuid CommanderGuid(ObjectGuid bot)
{
    auto itr = Bindings().find(bot);
    return itr != Bindings().end() && Valid(bot, itr->second) ? itr->second : ObjectGuid::Empty;
}

std::vector<std::pair<ObjectGuid, ObjectGuid>> CommanderBindings()
{
    std::vector<std::pair<ObjectGuid, ObjectGuid>> out;
    for (auto const& [bot, commander] : Bindings())
        if (Valid(bot, commander))
            out.emplace_back(bot, commander);
    return out;
}

namespace
{
// The host bots of a group, in the group's own order.
std::vector<Player*> BotsOfGroup(Group* group)
{
    std::vector<Player*> bots;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (member && member->IsInWorld() && IsHostBot(member))
            bots.push_back(member);
    }
    return bots;
}
}

Address ResolveAddress(Words const& args, size_t& pos, Player* sender, bool leaderFollows)
{
    Address result;
    if (pos >= args.w.size())
    {
        result.code = "missing-target";
        result.detail = "name a bot, or `party [leader]`";
        return result;
    }

    if (Lower(args.w[pos]) != "party")
    {
        std::string const& name = args.w[pos++];
        Player* bot = FindOnline(name);
        if (!bot)
        {
            result.code = "no-such-player";
            result.detail = name + " is not in the world";
        }
        else if (!IsHostBot(bot))
        {
            result.code = "not-a-bot";
            result.detail = name + " is not a logged-in host bot";
        }
        else
        {
            result.bots.push_back(bot);
            result.label = bot->GetName();
        }
        return result;
    }

    ++pos;
    if (leaderFollows && pos < args.w.size())
    {
        std::string const& name = args.w[pos++];
        Player* leader = FindOnline(name);
        if (!leader)
        {
            result.code = "no-such-player";
            result.detail = name + " is not in the world";
            return result;
        }
        Group* group = leader->GetGroup();
        if (!group)
        {
            result.code = "no-party";
            result.detail = name + " is not in a party";
            return result;
        }
        result.leader = leader;
        result.label = "party:" + leader->GetName();
        result.bots = BotsOfGroup(group);
        if (result.bots.empty())
        {
            result.code = "no-bots";
            result.detail = "the party of " + name + " holds no host bot";
        }
        return result;
    }

    // `party` with no leader: the bots bound to the sender (a player), else the bots of the one commander there is, else the one bot-led
    // party there is. Anything else needs the leader's name.
    auto const bindings = CommanderBindings();
    if (sender)
    {
        for (auto const& [botGuid, commanderGuid] : bindings)
            if (commanderGuid == sender->GetGUID())
                if (Player* bot = ObjectAccessor::FindPlayer(botGuid); bot && bot->IsInWorld())
                    result.bots.push_back(bot);
        if (!result.bots.empty())
        {
            result.label = "party:commander:" + sender->GetName();
            return result;
        }
    }
    std::set<ObjectGuid> commanders;
    for (auto const& [botGuid, commanderGuid] : bindings)
        commanders.insert(commanderGuid);
    if (commanders.size() == 1)
    {
        for (auto const& [botGuid, commanderGuid] : bindings)
            if (Player* bot = ObjectAccessor::FindPlayer(botGuid); bot && bot->IsInWorld())
                result.bots.push_back(bot);
        if (!result.bots.empty())
        {
            if (Player* commander = ObjectAccessor::FindPlayer(*commanders.begin()))
                result.label = "party:commander:" + commander->GetName();
            else
                result.label = "party";
            return result;
        }
    }
    std::vector<Group*> groups;
    for (Player* bot : AllHostBots())
        if (Group* group = bot->GetGroup(); group && std::find(groups.begin(), groups.end(), group) == groups.end())
            groups.push_back(group);
    if (groups.size() == 1)
    {
        result.bots = BotsOfGroup(groups.front());
        result.label = "party";
        if (Player* leader = ObjectAccessor::FindPlayer(groups.front()->GetLeaderGUID()))
        {
            result.leader = leader;
            result.label = "party:" + leader->GetName();
        }
        if (!result.bots.empty())
            return result;
    }
    result.code = groups.empty() && bindings.empty() ? "no-bots" : "ambiguous-party";
    result.detail = "name the party leader: `party <leader>`";
    return result;
}
}
}
