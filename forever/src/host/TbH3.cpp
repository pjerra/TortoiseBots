// Slice 3, host package H3: helpers shared by the order, state and test-kit commands (see TbH3.h). Core headers only: this file is
// built with and without TORTOISEBOTS_SLICE1.
#include "host/TbH3.h"
#include "host/TbGuardPure.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/TbH4.h"
#include "host/TbCc.h"
#include "host/TbIgnore.h"            // I13: `.tb ignore` (self-test records here; the handler lives in TbIgnore.cpp)
#include "host/TbHybrid.h"
#include "host/TbTactics.h"
#include "host/TbPet.h"
#include "host/cmd/TbTestTargetPure.h"
#include "host/run/TbRunHooks.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "Log.h"
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

// A comma-separated tuple of plain numbers: the same structure the coordinate form uses (`,` between the parts), for a
// list that is not a position: the six resistances of a piece (`res=`) and of a bot (`resist=`). Every part is a
// decimal number, so no part needs encoding.
Rec& Rec::Csv(std::string_view key, std::vector<int64> const& values)
{
    std::string joined;
    for (int64 value : values)
        joined += (joined.empty() ? "" : ",") + std::to_string(value);
    return Put(key, std::move(joined));
}

namespace
{
uint32 g_lineTooLong = 0;
}

uint32 LineTooLongCount() { return g_lineTooLong; }

// Free text, cut to its stated cap BEFORE encoding (N6): the record carries `trunc=yes` (an `error` line, which has only code/cmd/msg, ends its text with "...").
Rec& Rec::T(std::string_view key, std::string_view text, size_t cap)
{
    if (text.size() <= cap)
        return S(key, text);
    size_t keep = cap;
    while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80)    // not in the middle of a UTF-8 sequence
        --keep;
    std::string cut(text.substr(0, keep));
    if (_kind == "error")
        cut += "...";
    else
        _truncated = true;
    return S(key, cut);
}

void Reply::Send(std::string line)
{
    if (!_args.token.empty())
        line += " " + _args.token;
    if (line.size() > MaxLineBytes)
    {
        // A host bug, never a silent cut (N6): the encoder writes this refusal instead of the over-long line and counts it.
        ++g_lineTooLong;
        line = "error code=line-too-long cmd=" + Enc(_kind) + " msg=a%20reply%20line%20would%20pass%203000%20bytes";
        if (!_args.token.empty())
            line += " " + _args.token;
    }
    _handler->SendSysMessage(line);
}

void Reply::Begin() { Send(_kind + "-begin"); }

void Reply::Line(Rec const& rec)
{
    // The token and the separating space are part of the 3,000 bytes.
    size_t const room = MaxLineBytes - (_args.token.empty() ? 0 : _args.token.size() + 1);
    ++_records;
    std::vector<std::pair<std::string, std::string>> pairs = rec.Pairs();
    if (rec.Truncated())
        pairs.emplace_back("trunc", "yes");
    auto const tooLong = [&]()
    {
        ++g_lineTooLong;
        std::string line = "error code=line-too-long cmd=" + Enc(_kind) + " msg=a%20value%20of%20the%20record%20cannot%20be%20split%20below%203000%20bytes";
        if (!_args.token.empty())
            line += " " + _args.token;
        _handler->SendSysMessage(line);
        ++_lines;
    };
    if (pairs.empty())
    {
        Send(rec.Kind());
        ++_lines;
        return;
    }
    // The identity key (the first pair) opens the base line and every continuation line (`<kind>+ bot=<name> ...`).
    std::string const head = " " + pairs.front().first + "=" + pairs.front().second;
    if (rec.Kind().size() + 1 + head.size() > room / 2)
    {
        tooLong();
        return;
    }
    std::string line = rec.Kind() + head;
    bool lineHasMore = false;                // something besides the identity key is on the current line
    auto const flush = [&]()
    {
        Send(line);
        ++_lines;
        line = rec.Kind() + "+" + head;
        lineHasMore = false;
    };
    for (size_t i = 1; i < pairs.size(); ++i)
    {
        std::string const& key = pairs[i].first;
        std::string value = pairs[i].second;
        // A repeated key on a continuation line APPENDS its items to the key's list (list keys only): lists are split at `;`.
        while (true)
        {
            std::string const word = " " + key + "=" + value;
            if (line.size() + word.size() <= room)
            {
                line += word;
                lineHasMore = true;
                break;
            }
            // Does it fit on a fresh continuation line, whole? Then break the line first.
            if (lineHasMore && rec.Kind().size() + 1 + head.size() + word.size() <= room)
            {
                flush();
                continue;
            }
            // A list: put as many whole items as fit on this line, the rest goes on.
            size_t fitEnd = std::string::npos, pos = 0;
            size_t const budget = room > line.size() + 1 + key.size() + 1 ? room - line.size() - 1 - key.size() - 1 : 0;
            while (true)
            {
                size_t const semi = value.find(';', pos);
                size_t const end = semi == std::string::npos ? value.size() : semi;
                if (end > budget)
                    break;
                fitEnd = end;
                if (semi == std::string::npos)
                    break;
                pos = semi + 1;
            }
            if (value.find(';') == std::string::npos || fitEnd == std::string::npos || fitEnd == 0)
            {
                if (lineHasMore)
                {
                    flush();
                    continue;
                }
                tooLong();                  // one item (or a plain value) wider than a whole line: a host bug
                return;
            }
            line += " " + key + "=" + value.substr(0, fitEnd);
            lineHasMore = true;
            value = value.substr(fitEnd + 1);
            flush();
            if (value.empty())
                break;
        }
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
    if (extra.Truncated())
        line += " trunc=yes";
    Send(line);                              // Send checks the cap: an over-long end line becomes `error code=line-too-long`
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
    Rec rec("error");
    rec.S("code", code).S("cmd", cmd);
    if (!detail.empty())
        rec.T("msg", detail);
    Reply(handler, args, "error").Single(rec);
    handler->SetSentErrorMessage(true);
    return false;
}

bool HandleGrammar(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "grammar"))
        return false;
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
    // N6: free text over its cap is cut BEFORE encoding and the record says so; a list longer than a line is split at items; a value that cannot be
    // split below the cap is refused with `error code=line-too-long` (counted), never cut.
    reply.Line(Rec("selftest").S("bot", "Mtank").S("case", "freetext").T("msg", std::string(450, 'x') + " tail", 400));
    reply.Line(Rec("selftest").S("bot", "Mtank").S("case", "unsplittable").S("value", std::string(3100, 'y')));
    h4::SelfTestRecords(reply);               // H4: one example of every record kind and key slice 4 adds
    run::SelfTestRecords(reply);              // H5: one plan, leg, unit, run and event record
    h8::SelfTestRecords(reply);               // H8: the cc and strategy records and the state keys slice 8 adds
    h10::SelfTestRecords(reply);              // H10: the state keys slice 10 adds (form, totems, imbue, seal, paura)
    p11::SelfTestRecords(reply);              // P11: the pet state keys and the `pet` record
    tactics::SelfTestRecords(reply);          // the tactical order and cmd records (interrupt's action=) and the stance=/rebuff= keys
    i13::SelfTestRecords(reply);              // I13: the `ignore` records of `.tb ignore`
    Rec end("selftest");
    end.N("line_too_long", LineTooLongCount()).S("note", "fixed records").B("ready", false).LP("why", {{"resting", "Mpriest"}, {"far", "Mdpsb"}});     // why= is present exactly when ready=no
    reply.End(end);
    // The refusal, the only one-line reply: exactly code=, cmd= and an optional msg=.
    Reply error(handler, args, "error");
    error.Single(Rec("error").S("code", "selftest").S("cmd", "grammar").S("msg", "a b=c;d"));
    return true;
}

bool RequireBuilt(ChatHandler* handler, Words const& args, std::string_view cmd)
{
    if (!engine::Built())
        return Fail(handler, args, cmd, "engine-not-built", "the engine is not built in (this build has no group-fight commands)");
    // N4c: EVERY handler starts here, so a malformed `#token` word is refused before anything is done (the bad text is not echoed).
    if (args.badToken)
        return Fail(handler, args, cmd, "bad-token", "a token is # and 1 to 32 of A-Za-z0-9_.:-; nothing was done");
    return true;
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

namespace
{
target::Guid Identity(ObjectGuid guid)
{
    return { guid.GetRawValue(0), guid.GetRawValue(1) };
}
}

bool MayLeadBots(ChatHandler* handler, ObjectGuid character)
{
    Player* sender = SenderPlayer(handler);
    return target::MayTarget(Identity(character), sender ? Identity(sender->GetGUID()) : target::Guid(),
                             BotMgr::Instance().IsBot(character));
}

bool CheckBotAccess(Player const* sender, ObjectGuid botGuid, std::string& code, std::string& detail)
{
    if (!sender || botGuid.IsEmpty())
        return true;

    guard::Subject s;
    s.caller = Identity(sender->GetGUID());

    // A bot in a `.tb run` is temporarily bound to the run's tank bot; resolve one hop to the tank's commander so the owner
    // can still intervene (`outside-order` pause) while another player is refused.
    ObjectGuid binding = CommanderGuid(botGuid);
    if (!binding.IsEmpty() && BotMgr::Instance().IsBot(binding))
    {
        ObjectGuid const leadCommander = CommanderGuid(binding);
        if (!leadCommander.IsEmpty())
            binding = leadCommander;
    }
    s.binding = Identity(binding);

    ObjectGuid const master = BotMgr::Instance().MasterOf(botGuid);
    s.master = Identity(master);
    s.masterIsBot = !master.IsEmpty() && BotMgr::Instance().IsBot(master);

    Player const* bot = ObjectAccessor::FindPlayer(botGuid);
    if (bot)
    {
        if (Group const* group = bot->GetGroup())
        {
            ObjectGuid const leaderGuid = group->GetLeaderGUID();
            s.partyLeader = Identity(leaderGuid);
            s.partyLeaderIsBot = !leaderGuid.IsEmpty() && BotMgr::Instance().IsBot(leaderGuid);
        }
    }

    guard::Verdict const verdict = guard::CheckControl(s);
    if (verdict == guard::Verdict::Allowed)
        return true;

    code = guard::CodeOf(verdict);
    std::string const botName = bot ? std::string(bot->GetName()) : GuidText(botGuid);
    detail = verdict == guard::Verdict::ForeignBot
        ? botName + " is owned by another commander"
        : botName + " is in a party led by another player";
    return false;
}

bool CheckBotAccess(ChatHandler* handler, ObjectGuid botGuid, std::string& code, std::string& detail)
{
    return CheckBotAccess(SenderPlayer(handler), botGuid, code, detail);
}

bool CheckBotAccess(ChatHandler* handler, Player const* bot, std::string& code, std::string& detail)
{
    return !bot || CheckBotAccess(SenderPlayer(handler), bot->GetGUID(), code, detail);
}

bool CheckPartyLeadAccess(Player const* sender, Player const* leader, std::string& code, std::string& detail)
{
    if (!sender || !leader)
        return true;
    if (IsHostBot(leader))
        return CheckBotAccess(sender, leader->GetGUID(), code, detail);
    if (leader->GetGUID() != sender->GetGUID())
    {
        code = "foreign-lead";
        detail = std::string(leader->GetName()) + "'s party is led by another player";
        return false;
    }
    return true;
}

bool CheckPartyLeadAccess(ChatHandler* handler, Player const* leader, std::string& code, std::string& detail)
{
    return CheckPartyLeadAccess(SenderPlayer(handler), leader, code, detail);
}

// proof-1 A: the `.tb run` observer exemption. The commander chain is followed with CommanderGuid up to 4 hops from the bot; it ends at a real player when
// a hop is a non-empty guid that is not a host bot (then the party is that player's and even a GM is refused). An empty guid, or four bot hops, ends the
// chain at the bot side. The guid form serves a run whose leader is not in the world (`rec->leader`); CheckBotAccess allows an empty guid.
bool CheckRunObserverAccess(ChatHandler* handler, ObjectGuid botGuid, std::string& code, std::string& detail)
{
    if (CheckBotAccess(handler, botGuid, code, detail))
        return true;

    bool chainEndsAtPlayer = false;
    {
        ObjectGuid guid = botGuid;
        for (int hop = 0; hop < 4; ++hop)
        {
            ObjectGuid const next = CommanderGuid(guid);
            if (next.IsEmpty())
                break;
            if (!BotMgr::Instance().IsBot(next))
            {
                chainEndsAtPlayer = true;
                break;
            }
            guid = next;
        }
    }
    WorldSession* session = handler->GetSession();
    bool const senderIsGm = session && session->GetSecurity() >= SEC_GAMEMASTER;
    bool const optionOn = sConfigMgr->GetBoolDefault("TortoiseBots.Run.GmObserver", true);
    if (!guard::ObserverMayControlRun(optionOn, senderIsGm, chainEndsAtPlayer))
        return false;

    Player* sender = SenderPlayer(handler);
    std::string const senderName = sender ? std::string(sender->GetName()) : std::string("the console");
    Player const* bot = ObjectAccessor::FindPlayer(botGuid);
    std::string const botName = bot ? std::string(bot->GetName()) : GuidText(botGuid);
    TC_LOG_INFO("tortoisebots", "TortoiseBots run: GM observer {} allowed on {} (bot commander)", senderName, botName);
    code.clear();
    detail.clear();
    return true;
}

bool CheckRunObserverAccess(ChatHandler* handler, Player const* bot, std::string& code, std::string& detail)
{
    return !bot || CheckRunObserverAccess(handler, bot->GetGUID(), code, detail);
}

Player* ResolveTestTarget(ChatHandler* handler, std::string_view name, std::string& code, std::string& detail)
{
    Player* sender = SenderPlayer(handler);
    if (name.empty())
    {
        if (sender)
            return sender;
        code = "no-character";
        detail = "the console has no character: name a host bot";
        return nullptr;
    }
    Player* player = FindOnline(name);
    if (!player)
    {
        code = "no-such-player";
        detail = std::string(name) + " is not in the world";
        return nullptr;
    }
    if (!MayLeadBots(handler, player->GetGUID()))
    {
        code = "not-a-bot";
        detail = std::string(name) + " is neither a host bot nor your own character";
        return nullptr;
    }
    if (IsHostBot(player) && !CheckBotAccess(handler, player, code, detail))
        return nullptr;
    return player;
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

ObjectGuid StoredCommander(ObjectGuid bot)
{
    auto itr = Bindings().find(bot);
    return itr == Bindings().end() ? ObjectGuid::Empty : itr->second;
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

Address ResolveAddress(Words const& args, size_t& pos, Player* sender, bool leaderFollows, bool allowSender, bool requireControl)
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
        else if (!IsHostBot(bot) && !(allowSender && sender && bot->GetGUID() == sender->GetGUID()))
        {
            result.code = "not-a-bot";
            result.detail = allowSender ? name + " is neither a host bot nor your own character" : name + " is not a logged-in host bot";
        }
        else if (requireControl && IsHostBot(bot) && !CheckBotAccess(sender, bot->GetGUID(), result.code, result.detail))
        {
            return result;
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
        if (requireControl && !CheckPartyLeadAccess(sender, leader, result.code, result.detail))
            return result;
        result.leader = leader;
        result.label = "party:" + leader->GetName();
        result.bots = BotsOfGroup(group);
        if (result.bots.empty())
        {
            result.code = "no-bots";
            result.detail = "the party of " + name + " holds no host bot";
            return result;
        }
        if (requireControl)
        {
            for (Player* bot : result.bots)
            {
                if (!CheckBotAccess(sender, bot->GetGUID(), result.code, result.detail))
                {
                    result.bots.clear();
                    return result;
                }
            }
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
            if (requireControl)
            {
                for (Player* bot : result.bots)
                {
                    if (!CheckBotAccess(sender, bot->GetGUID(), result.code, result.detail))
                    {
                        result.bots.clear();
                        return result;
                    }
                }
            }
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
            if (requireControl)
            {
                for (Player* bot : result.bots)
                {
                    if (!CheckBotAccess(sender, bot->GetGUID(), result.code, result.detail))
                    {
                        result.bots.clear();
                        return result;
                    }
                }
            }
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
        {
            if (requireControl)
            {
                if (result.leader && !CheckPartyLeadAccess(sender, result.leader, result.code, result.detail))
                {
                    result.bots.clear();
                    return result;
                }
                for (Player* bot : result.bots)
                {
                    if (!CheckBotAccess(sender, bot->GetGUID(), result.code, result.detail))
                    {
                        result.bots.clear();
                        return result;
                    }
                }
            }
            return result;
        }
    }
    result.code = groups.empty() && bindings.empty() ? "no-bots" : "ambiguous-party";
    result.detail = "name the party leader: `party <leader>`";
    return result;
}
}
}
