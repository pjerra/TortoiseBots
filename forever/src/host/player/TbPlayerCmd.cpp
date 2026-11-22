// The `.bot` entry: PlayerScript::OnCommand claims the word `bot` before the core's lookup and RBAC check (D-1), Dispatch reads the
// caller and answers. Round 1 serves `help` and `who`; the pure rule (tiers, ownership, login cap, cooldown) is TbPlayerAuthPure.h
// and is tested standalone by forever/tools/test_player_auth.cpp.
#include "host/player/TbPlayerCmd.h"
#include "host/player/TbPlayerAuthPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/TbH3.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/run/TbRunHooks.h"

#include "CharacterCache.h"
#include "BattlenetAccountMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Define.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "host/TbOrdersApi.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{
namespace player
{
// Who the caller is as an owner: the Battle.net account of the caller's own session. Whether a game account is one of its own is
// asked of the core's Battlenet::AccountMgr::GetIdByGameAccount (the core's own login-DB read, no module query) for each game
// account the line touches, memoised only inside this one scope (one `.bot` line), never kept across lines: a `.bnetaccount
// link`/`unlink` takes effect for the next line, so a game account moved to another Battle.net account stops being "owned" at
// once (fail closed: bnet 0 or an unlinked account owns nothing).
struct OwnerScope
{
    std::uint32_t bnet = 0;
    bool Owns(std::uint32_t gameAccount) const
    {
        if (!bnet || !gameAccount)
            return false;
        auto it = memo.find(gameAccount);
        if (it == memo.end())
            it = memo.emplace(gameAccount, Battlenet::AccountMgr::GetIdByGameAccount(gameAccount)).first;
        return it->second == bnet;
    }
private:
    mutable std::unordered_map<std::uint32_t, std::uint32_t> memo;   // game account -> its Battle.net account now, this line only
};

BotFacts FactsOf(Player* requester, Player* bot, OwnerScope const& scope);   // fills the pure rule's facts; defined below

namespace
{
bool s_commands = true;
[[maybe_unused]] std::uint32_t s_maxBots = 4;   // TortoiseBots.Player.MaxBots: the owned-login cap of round 2's `add`

// The caller's Battle.net account is the one its session logged in with (the core's own identity of the player); its game
// accounts are checked against the login DB now (see OwnerScope).
OwnerScope ScopeOf(Player* player)
{
    OwnerScope scope;
    WorldSession* session = player ? player->GetSession() : nullptr;
    if (!session)
        return scope;
    scope.bnet = session->GetBattlenetAccountId();
    // The session keeps its login Battle.net id after a live unlink or move of its own game account: that id is the caller's
    // only while the caller's game account is still one of its game accounts now, else the caller owns nothing.
    if (!scope.Owns(session->GetAccountId()))
        return OwnerScope{};
    return scope;
}

std::string Lower(std::string_view word)
{
    std::string out;
    out.reserve(word.size());
    for (char c : word)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string_view FirstWord(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    std::size_t const begin = i;
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    return text.substr(begin, i - begin);
}

std::string_view AfterFirstWord(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    return text.substr(i);
}

// D-6: one line per answer, the module's name first, then the verb (and the bot, once a verb names one). Never an account id or name.
void Say(Player* player, std::string const& line)
{
    if (!player || !player->GetSession())
        return;
    ChatHandler handler(player->GetSession());
    handler.SendSysMessage(line);
}

void Refuse(Player* player, std::string const& verb, char const* code, std::string const& text)
{
    Say(player, "TortoiseBots: " + verb + ": refused " + code + ": " + text);
}

std::string ClassName(uint8 classId)
{
    ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId);
    if (entry && entry->Name[LOCALE_enUS])
        return entry->Name[LOCALE_enUS];
    return "class " + std::to_string(classId);
}

// The movement the bot was last told to keep: a free bot stands to its own business, a bot told to stay stays, every other bot follows
// its master. The order log is the engine's (h3::LastOrder); round 1 has no order verb yet, so this is `follow` and `free` only.
std::string MoveWord(ObjectGuid guid)
{
    if (BotMgr::Instance().IsFreeBot(guid))
        return "free";
    if (h3::LastOrder(guid).verb == "stay")
        return "stay";
    return "follow";
}

// The brain the bot runs now (a spike bot has none).
std::string BrainWord(ObjectGuid guid)
{
    bool away = false, blocked = false, hasBrain = false;
    std::uint32_t awaySeconds = 0;
    std::string blockedWhy;
    if (!BotMgr::Instance().FollowStatus(guid, away, awaySeconds, blocked, blockedWhy, hasBrain))
        return "spike";
    return hasBrain ? "engine" : "spike";
}

// Who sees a bot in `who`: the same four faces D-4 lets order it, without the GM widening (that is `who all`) and without the run
// refusal (a run hides nothing, it only refuses orders).
bool Visible(Caller const& caller, BotFacts const& bot)
{
    if (caller.account != 0 && bot.account != 0 && caller.account == bot.account)
        return true;
    if (bot.master != 0 && bot.master == caller.guid)
        return true;
    if (bot.commandedByCaller)
        return true;
    if (bot.groupLeader != 0 && bot.groupLeader == caller.guid && !bot.leaderIsBot)
        return true;
    return false;
}

bool HandleHelp(Player* player, Caller const& caller)
{
    Say(player, "TortoiseBots: help: .bot help - this list");
    Say(player, "TortoiseBots: help: .bot who [all] - the bots you may order, one line each");
    if (caller.gm)
        Say(player, "TortoiseBots: help: .bot who all - every bot in the world (GameMasters)");
    Say(player, "TortoiseBots: help: .bot follow <bot> [member] - follow a member or your target");
    Say(player, "TortoiseBots: help: .bot stay <bot> - stand where it is");
    Say(player, "TortoiseBots: help: .bot attack <bot> - attack your selected target");
    Say(player, "TortoiseBots: help: .bot flee <bot> - run from the fight");
    // Every player may add and log out its own characters (D-7), so the owner rows are shown to all: a new owner owns no bot yet.
    Say(player, "TortoiseBots: help: .bot add <name> - log one of your own characters in as a bot (it joins your party)");
    Say(player, "TortoiseBots: help: .bot logout <bot> - log your bot out");
    return true;
}

bool HandleWho(Player* player, Caller const& caller, OwnerScope const& scope, std::string_view tail)
{
    std::string const argument = Lower(tail);
    bool all = false;
    if (!argument.empty())
    {
        if (argument != "all")
        {
            Refuse(player, "who", "usage", ".bot who [all]");
            return true;
        }
        all = caller.gm;   // the widening is a GM's; a player's `all` names no more than `who` does
    }

    std::vector<Player*> bots;
    for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld())
            continue;
        if (!all && !Visible(caller, FactsOf(player, bot, scope)))
            continue;
        bots.push_back(bot);
    }
    std::sort(bots.begin(), bots.end(), [](Player* left, Player* right) { return left->GetName() < right->GetName(); });

    Say(player, "TortoiseBots: who: " + std::to_string(bots.size()) + " bot(s)");
    for (Player* bot : bots)
    {
        ObjectGuid const guid = bot->GetGUID();
        Say(player, "TortoiseBots: who: " + bot->GetName() + " level " + std::to_string(bot->GetLevel()) + " " +
                        ClassName(bot->GetClass()) + " map " + std::to_string(bot->GetMapId()) + " " + MoveWord(guid) + " " +
                        BrainWord(guid));
    }
    return true;
}
}

bool ResolveCharacter(std::string_view name, CharacterCacheEntry const*& entry, std::string& code)
{
    std::string normalized(name);
    normalizePlayerName(normalized);
    if (normalized.empty())
    {
        code = "not-found";
        return false;
    }
    entry = sCharacterCache->GetCharacterCacheByName(normalized);
    if (!entry)
    {
        code = "not-found";
        return false;
    }
    return true;
}

bool ResolveBot(std::string_view name, Player*& bot, std::string& code)
{
    CharacterCacheEntry const* entry = nullptr;
    if (!ResolveCharacter(name, entry, code))
        return false;
    bot = ObjectAccessor::FindConnectedPlayer(entry->Guid);
    if (!bot || !bot->IsInWorld())
    {
        code = "not-found";
        return false;
    }
    if (!BotMgr::Instance().IsBot(bot->GetGUID()))
    {
        code = "not-a-bot";
        return false;
    }
    return true;
}

bool HandleAdd(Player* player, Caller const& caller, OwnerScope const& scope, std::string_view tail)
{
    std::string_view const name = FirstWord(tail);
    if (name.empty())
    {
        Refuse(player, "add", "usage", ".bot add <name>");
        return true;
    }
    CharacterCacheEntry const* entry = nullptr;
    std::string code;
    if (!ResolveCharacter(name, entry, code))
    {
        Refuse(player, "add", code.c_str(), "no such character");
        return true;
    }
    ObjectGuid const guid = entry->Guid;
    if (ObjectAccessor::FindConnectedPlayer(guid))
    {
        Refuse(player, "add", "already-online", "that character is already in the world");
        return true;
    }
    if (Group* group = player->GetGroup(); group && group->GetLeaderGUID() != player->GetGUID())
    {
        Refuse(player, "add", "not-leader", "you are in a group led by someone else");
        return true;
    }
    std::uint32_t const gameAccount = entry->AccountId;
    std::uint32_t const characterBnet = scope.bnet && scope.Owns(gameAccount) ? scope.bnet : 0;
    std::uint32_t const ownedOnline = BotMgr::Instance().OwnedBotsOf(player->GetSession() ? player->GetSession()->GetBattlenetAccountId() : 0);
    if (!MayLoginOwned(caller.account, characterBnet, caller.gm, ownedOnline, s_maxBots, code))
    {
        if (code == "too-many")
            Refuse(player, "add", "too-many", "the owned-bot cap is reached");
        else
            Refuse(player, "add", code.c_str(), "that character is not yours");
        return true;
    }
    std::string const error = BotMgr::Instance().Login(guid, player->GetGUID(), false, false, true);
    if (!error.empty())
    {
        Refuse(player, "add", "login-refused", error);
        return true;
    }
    Say(player, "TortoiseBots: add " + entry->Name + ": ok queued for login; it joins your party");
    return true;
}

bool HandleLogout(Player* player, Caller const& caller, OwnerScope const& scope, std::string_view tail)
{
    std::string_view const name = FirstWord(tail);
    if (name.empty())
    {
        Refuse(player, "logout", "usage", ".bot logout <bot>");
        return true;
    }
    Player* bot = nullptr;
    std::string code;
    if (!ResolveBot(name, bot, code))
    {
        Refuse(player, "logout", code.c_str(), code == "not-found" ? "no such online bot" : "that character is not a bot");
        return true;
    }
    BotFacts const facts = FactsOf(player, bot, scope);
    Verdict const verdict = Decide(caller, facts, Tier::Owner);
    if (verdict != Verdict::Allowed)
    {
        Refuse(player, "logout", CodeOf(verdict), "you may not log that bot out");
        return true;
    }
    if (!BotMgr::Instance().Logout(bot->GetGUID(), "logged out by its owner"))
    {
        Refuse(player, "logout", "not-found", "that bot is not logged in");
        return true;
    }
    Say(player, "TortoiseBots: logout " + bot->GetName() + ": ok logged out");
    return true;
}

bool HandleOrderVerb(Player* player, Caller const& caller, OwnerScope const& scope, std::string_view verb, std::string_view tail)
{
    std::string_view const name = FirstWord(tail);
    if (name.empty())
    {
        Refuse(player, std::string(verb), "usage", ".bot " + std::string(verb) + " <bot>");
        return true;
    }
    Player* bot = nullptr;
    std::string code;
    if (!ResolveBot(name, bot, code))
    {
        Refuse(player, std::string(verb), code.c_str(), code == "not-found" ? "no such online bot" : "that character is not a bot");
        return true;
    }
    BotFacts const facts = FactsOf(player, bot, scope);
    Verdict const verdict = Decide(caller, facts, Tier::Tactical);
    if (verdict != Verdict::Allowed)
    {
        Refuse(player, std::string(verb), CodeOf(verdict), "you may not order that bot");
        return true;
    }
    h3::OrderSpec spec;
    spec.verb = std::string(verb);
    if (verb == "follow")
    {
        spec.arg = std::string(AfterFirstWord(tail));
    }
    else if (verb == "attack")
    {
        Unit* selected = player->GetSelectedUnit();
        if (!selected)
        {
            Refuse(player, "attack", "no-selection", "select a target first");
            return true;
        }
        spec.unit = selected->GetGUID();
    }
    std::string queueCode;
    std::string detail;
    std::uint32_t seq = 0;
    if (!h3::QueueOrder(bot, spec, queueCode, detail, seq))
    {
        Refuse(player, std::string(verb), queueCode.c_str(), detail);
        return true;
    }
    Say(player, "TortoiseBots: " + std::string(verb) + " " + bot->GetName() + ": ok order " + std::to_string(seq));
    return true;
}

// Fills the pure rule's facts from the live objects: the bot table for the guid and the master, the character cache's game
// account checked against the caller's Battle.net account, the commander binding, the bot's party and the run gate.
// `account` is the bot's Battle.net account when it is the caller's, else 0 ("not the caller's": never an owner match).
BotFacts FactsOf(Player* requester, Player* bot, OwnerScope const& scope)
{
    BotFacts facts;
    if (!bot)
        return facts;
    ObjectGuid const guid = bot->GetGUID();
    facts.isBot = BotMgr::Instance().IsBot(guid);
    std::uint32_t const gameAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (scope.Owns(gameAccount))
        facts.account = scope.bnet;
    facts.master = BotMgr::Instance().MasterOf(guid).GetCounter();
    facts.commandedByCaller = requester && cmd::IsCommandedBy(requester, bot);
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid const leader = group->GetLeaderGUID();
        facts.groupLeader = leader.GetCounter();
        facts.leaderIsBot = BotMgr::Instance().IsBot(leader);
    }
    facts.inRun = run::BotInRunningRun(bot);
    return facts;
}

void LoadConfig()
{
    s_commands = sConfigMgr->GetBoolDefault("TortoiseBots.Player.Commands", true);
    s_maxBots = static_cast<std::uint32_t>(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Player.MaxBots", 4), 0, 4));
}

bool Dispatch(Player* player, std::string_view rest)
{
    if (!player)
        return false;
    // The module off, or built without the engine: the core answers the command as it does for any other.
    if (!BotMgr::Instance().Enabled() || !engine::Built())
        return false;

    Caller caller;
    caller.gm = player->GetSession() && player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    caller.account = player->GetSession() ? player->GetSession()->GetBattlenetAccountId() : 0;
    caller.guid = player->GetGUID().GetCounter();

    std::string const verb = Lower(FirstWord(rest));
    if (!s_commands && !caller.gm)
    {
        Refuse(player, verb.empty() ? "bot" : verb, "players-off", "player commands are off (TortoiseBots.Player.Commands = 0)");
        return true;
    }

    OwnerScope const scope = ScopeOf(player);
    caller.account = scope.bnet;   // 0 when the session's Battle.net id no longer holds the caller's game account

    if (verb.empty() || verb == "help")
        return HandleHelp(player, caller);
    if (verb == "who" || verb == "roster" || verb == "list")
        return HandleWho(player, caller, scope, AfterFirstWord(rest));
    if (verb == "add")
        return HandleAdd(player, caller, scope, AfterFirstWord(rest));
    if (verb == "logout" || verb == "remove")
        return HandleLogout(player, caller, scope, AfterFirstWord(rest));
    if (verb == "follow" || verb == "stay" || verb == "attack" || verb == "flee")
        return HandleOrderVerb(player, caller, scope, verb, AfterFirstWord(rest));

    Refuse(player, verb, "unknown-verb", "no such command; try .bot help");
    return true;
}

namespace
{
// D-1: the hook the core calls with the text after the leading '.' or '!', before its lookup and before any RBAC check.
class tortoisebots_player_cmd : public PlayerScript
{
public:
    tortoisebots_player_cmd() : PlayerScript("tortoisebots_player_cmd") { }

    bool OnCommand(Player* player, std::string_view command) override
    {
        std::string_view rest;
        if (!ClaimsCommand(command, rest))
            return false;   // not ours: the core answers the command
        return Dispatch(player, rest);
    }
};
}

void AddScripts()
{
    new tortoisebots_player_cmd();
}
}
}

#else   // no engine: the module answers `.tb` only and the core keeps `.bot` to itself

namespace TortoiseBots
{
namespace player
{
void LoadConfig() { }
void AddScripts() { }
bool Dispatch(Player*, std::string_view) { return false; }
}
}

#endif
