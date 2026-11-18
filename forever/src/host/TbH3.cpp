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
constexpr size_t MaxLine = 3900;     // a system chat message holds up to 4095 bytes (ChatPackets.cpp:214, 12 bits)

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

std::string Esc(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value)
    {
        unsigned char const u = static_cast<unsigned char>(c);
        if (std::isspace(u))
            out += '_';
        else if (c == '=')
            out += ':';
        else if (c == '|')
            out += '/';
        else if (c == '#')
            out += '+';
        else if (u < 0x20 || u == 0x7F)
            continue;
        else
            out += c;
    }
    return out.empty() ? std::string("none") : out;
}

void Say(ChatHandler* handler, std::string line, Words const& args)
{
    if (!args.token.empty())
        line += " " + args.token;
    if (line.size() > MaxLine)
        line.resize(MaxLine);
    handler->SendSysMessage(line);
}

bool Fail(ChatHandler* handler, Words const& args, std::string_view cmd, std::string_view code, std::string_view detail)
{
    std::string line = "error=" + Esc(code) + " cmd=" + Esc(cmd);
    if (!detail.empty())
        line += " msg=" + Esc(detail);
    if (args.badToken)
        line += " token=rejected";
    Say(handler, std::move(line), args);
    handler->SetSentErrorMessage(true);
    return false;
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
        return "none";
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
            result.label = "party:commander=" + sender->GetName();
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
                result.label = "party:commander=" + commander->GetName();
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
