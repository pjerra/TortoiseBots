// The `.bot` entry: PlayerScript::OnCommand claims the word `bot` before the core's lookup and RBAC check (D-1), Dispatch reads the
// caller and answers. Rounds R1-R3 serve `help`, `who` (+ `roster`, `list`), `add`, `logout` (+ `remove`), `follow`, `stay`, `attack`,
// `flee`, `summon` and `stats`; the pure rule (tiers, ownership, login cap, summon cooldown, the encoded-text decode) is
// TbPlayerAuthPure.h and is tested standalone by forever/tools/test_player_auth.cpp.
#include "host/player/TbPlayerCmd.h"
#include "host/player/TbChoiceCmd.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/player/TbUpkeep.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/TbH3.h"
#include "host/TbH4.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/run/TbRunHooks.h"

#include "CharacterCache.h"
#include "BattlenetAccountMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Define.h"
#include "GitRevision.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Util.h"
#include "WorldSession.h"
#include "host/TbAddonInfo.h"
#include "host/TbOrdersApi.h"
#include "host/TbHostLfgFill.h"
#include "host/TbAutogear.h"
#include "host/hire/TbHire.h"
#include "host/hire/TbHireProvision.h"
#include "host/hire/TbHireProvisionPure.h"
#include "runtime/HireLifecycle.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
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

bool OwnsGameAccount(OwnerScope const& scope, std::uint32_t gameAccount)
{
    return scope.Owns(gameAccount);
}

// D-6: one line per answer, the module's name first, then the verb (and the bot, once a verb names one). Never an account id or name.
namespace
{
std::vector<std::string>* s_replySink = nullptr;   // set while an addon verb runs; world thread only, never kept
}

void Say(Player* player, std::string const& line)
{
    if (s_replySink)
    {
        s_replySink->push_back(line);
        return;
    }
    if (!player || !player->GetSession())
        return;
    ChatHandler handler(player->GetSession());
    handler.SendSysMessage(line);
}

void Refuse(Player* player, std::string const& verb, char const* code, std::string const& text)
{
    Say(player, "TortoiseBots: " + verb + ": refused " + code + ": " + text);
}

void SetReplySink(std::vector<std::string>* sink)
{
    s_replySink = sink;
}

bool CaptureReply(std::string const& line)
{
    if (!s_replySink)
        return false;
    s_replySink->push_back(line);
    return true;
}

namespace
{
bool s_commands = true;
[[maybe_unused]] std::uint32_t s_maxBots = 4;   // TortoiseBots.Player.MaxBots: the owned-login cap of round 2's `add`
// AiPlayerbot.OwnedBotSummonCooldown (the legacy key, wired by round 3): the per-bot wait between `.bot summon`s, read the way the legacy
// config reads it (default 300 s, capped at 3600; 0 = no wait). The other OwnedBotSummon* keys stay unread (spec D-8, D-13).
std::uint32_t s_summonCooldown = 300;

// The caller's Battle.net account is the one its session logged in with (the core's own identity of the player); its game
// accounts are checked against the login DB now (see OwnerScope).
OwnerScope ScopeOf(Player const* player)
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
    Say(player, "TortoiseBots: help: .bot stats - your bot fleet in one line");
    Say(player, "TortoiseBots: help: .bot stats <bot> - its level, class, health, mana, money, zone and movement");
    Say(player, "TortoiseBots: help: .bot version - module version and credits");
    Say(player, "TortoiseBots: help: .bot features - the server's player-facing switches");
    Say(player, "TortoiseBots: help: .bot summon <bot> - bring it to your side (its master only; a short per-bot wait)");
    // Every player may add and log out its own characters (D-7), so the owner rows are shown to all: a new owner owns no bot yet.
    Say(player, "TortoiseBots: help: .bot add <name> - log one of your own characters in as a bot (it joins your party)");
    Say(player, "TortoiseBots: help: .bot logout <bot> - log your bot out");
    Say(player, "TortoiseBots: help: .bot maintenance <bot> - refresh your bot's kit and repair (when enabled)");
    Say(player, "TortoiseBots: help: .bot totem <bot|party> <earth|fire|water|air> <totem|auto|none> | set <earth>,<fire>,<water>,<air> | mode <auto|prepull|manual> | status - a shaman's totems, until logout");
    Say(player, "TortoiseBots: help: .bot behavior <bot|party> <loot|aoe|autocc|savemana|boost|threat|potions|mimic|spread|hold|noblink|offheal|offdps> <on|off> | status - behaviour toggles, until logout");
    Say(player, "TortoiseBots: help: .bot blessing <bot|party> <warrior|paladin|hunter|rogue|priest|shaman|mage|warlock|druid> <might|wisdom|kings|salvation|sanctuary|light|auto> | player <name> <blessing|auto> | status - a paladin's blessing orders, until logout");
    Say(player, "TortoiseBots: help: .bot choice <bot|party> <aura|aspect|curse|poison_mh|poison_oh|sting|pet> <value|auto> | status - the ordered buff, poison or demon, until logout");
    Say(player, "TortoiseBots: help: .bot service <bot> [portal <spellId> | conjure <food|water|healthstone>] - what a bot can conjure or portal");
    Say(player, "TortoiseBots: help: .bot hire <class> [race] [male/female] [tank/healer/dps|spec] [tier] [level=<n>] - hire a companion (recruiter unless HireAnywhere)");
    Say(player, "TortoiseBots: help: .bot lfg join <dungeon id> <tank|healer|dps> | status | leave - the dungeon fill for your own character");
    return true;
}

// `.bot hire <class> [race] [gender] [role|spec] [tier] [level=<n>]`: the fast path. Without
// AiPlayerbot.HireAnywhere the provisioner refuses it with the recruiter hint.
char const* HireStatusWord(hire::Status status)
{
    switch (status)
    {
        case hire::Status::Ok: return "ok";
        case hire::Status::Disabled: return "disabled";
        case hire::Status::NoPermission: return "no-permission";
        case hire::Status::RecruiterRequired: return "recruiter-required";
        case hire::Status::InvalidChoice: return "bad-choice";
        case hire::Status::GroupFull: return "group-full";
        case hire::Status::CapReached: return "cap";
        case hire::Status::Poor: return "poor";
        case hire::Status::TierLocked: return "tier-locked";
        default: return "failed";
    }
}

bool HandleHire(Player* player, std::string_view tail)
{
    hire::HireCommandParse const parse = hire::ParseHireCommand(tail, player->GetLevel(), hire::ShamanTankEnabled());
    if (!parse.ok)
    {
        Refuse(player, "hire", parse.errorCode.c_str(), parse.message);
        return true;
    }
    hire::Selection selection;
    selection.classId = parse.classId;
    selection.race = parse.race;
    selection.gender = parse.gender;
    selection.role = parse.role;
    selection.specIndex = parse.specIndex;
    selection.gearTier = parse.gearTier;
    selection.level = parse.requestedLevel > 255 ? 255 : static_cast<uint8>(parse.requestedLevel);
    if (!selection.race)
    {
        // The hirer's faction's races that can be the class, as the core's own player info says.
        std::vector<uint8> pool;
        for (uint32 race = 1; race <= 11; ++race)
            if (Player::TeamForRace(static_cast<uint8>(race)) == player->GetTeam() &&
                sObjectMgr->GetPlayerInfo(race, selection.classId))
                pool.push_back(static_cast<uint8>(race));
        if (pool.empty())
        {
            Refuse(player, "hire", "bad-choice", "No race of your faction can be that class.");
            return true;
        }
        selection.race = pool[urand(0, static_cast<uint32>(pool.size() - 1))];
    }
    if (!parse.genderGiven)
        selection.gender = static_cast<uint8>(urand(0, 1));

    hire::Outcome const outcome = hire::Hire(player, selection, nullptr);
    if (outcome.status == hire::Status::Ok)
        Say(player, "TortoiseBots: hire " + outcome.botName + ": ok tier " + std::to_string(selection.gearTier) +
            ", fee " + std::to_string(outcome.cost) + " copper");
    else
        Refuse(player, "hire", HireStatusWord(outcome.status), outcome.message);
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
    // A hire is not a bot that may be parked: logging it out ends the hire and deletes its character,
    // and no fee is returned (the fee is for the hire that ran).
    if (HireLifecycle::Instance().IsHired(bot->GetGUID()))
    {
        HireLifecycle::Instance().DismissNow(bot->GetGUID(), "dismissed by its owner");
        Say(player, "TortoiseBots: logout " + bot->GetName() + ": ok dismissed the hire (its character is deleted, no fee is returned)");
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

// Piece E upkeep (ticket #137): TB's `.bot maintenance` for owned bots. The
// owner tier is the pure rule (TierOf("maintenance") is Owner: the caller
// owns the bot or is a GameMaster); pool bots, the disabled flag and dead
// bots are the pure upkeep checklist. The work itself is upkeep::Run.
bool HandleMaintenance(Player* player, Caller const& caller, OwnerScope const& scope, std::string_view tail)
{
    std::string_view const name = FirstWord(tail);
    if (name.empty())
    {
        Refuse(player, "maintenance", "usage", ".bot maintenance <bot>");
        return true;
    }
    Player* bot = nullptr;
    std::string code;
    if (!ResolveBot(name, bot, code))
    {
        Refuse(player, "maintenance", code.c_str(), code == "not-found" ? "no such online bot" : "that character is not a bot");
        return true;
    }
    BotFacts const facts = FactsOf(player, bot, scope);
    Verdict const verdict = Decide(caller, facts, TierOf("maintenance"));
    if (verdict != Verdict::Allowed)
    {
        Refuse(player, "maintenance", CodeOf(verdict), "you may not maintain that bot");
        return true;
    }
    upkeep::Input in;
    in.enabled = upkeep::IsEnabled();
    in.poolBot = BotMgr::Instance().IsPoolBot(bot->GetGUID());
    in.alive = bot->IsAlive();
    upkeep::Refusal const refusal = upkeep::Decide(in);
    if (refusal != upkeep::Refusal::None)
    {
        char const* text = "";
        if (refusal == upkeep::Refusal::Pool)
            text = "pool bots keep progressing organically";
        else if (refusal == upkeep::Refusal::Disabled)
            text = "owned-bot maintenance is disabled (AiPlayerbot.OwnedBotMaintenanceEnabled = 0)";
        else
            text = "that bot must be alive";
        Refuse(player, "maintenance", upkeep::CodeOf(refusal), text);
        return true;
    }
    upkeep::Summary summary;
    if (!upkeep::Run(bot, summary))
    {
        Refuse(player, "maintenance", "failed", "maintenance did not run");
        return true;
    }
    Say(player, "TortoiseBots: maintenance " + bot->GetName() + ": ok " + upkeep::Describe(summary));
    return true;
}

namespace
{
// D-8: when each bot was last summoned, per bot guid, in this process only (world thread only: a chat opcode is PROCESS_THREADUNSAFE).
// Nothing about an account or a session is kept here, and a restart forgets the waits, which is right for a courtesy cooldown.
std::unordered_map<std::uint64_t, std::uint64_t>& SummonStamps()
{
    static std::unordered_map<std::uint64_t, std::uint64_t> stamps;
    return stamps;
}

// The stamp of one bot, 0 = never summoned (no entry is created by a read).
std::uint64_t SummonStampOf(std::uint64_t guid)
{
    auto const it = SummonStamps().find(guid);
    return it == SummonStamps().end() ? 0 : it->second;
}

// D-8: the shared teleport's own refusal, read back out of its reply record (status=refused, code=, msg=; grammar v2 encodes every
// value, hence the decode). Its wording is the only copy: the player line repeats it instead of restating the rules.
void TeleportRefusal(h3::Rec const& rec, std::string& code, std::string& text)
{
    for (auto const& pair : rec.Pairs())
    {
        if (pair.first == "code")
            code = EncDecode(pair.second);
        else if (pair.first == "msg")
            text = EncDecode(pair.second);
    }
    if (code.empty())
        code = "teleport-refused";
    if (text.empty())
        text = "the teleport did not start";
}
}

// D-8: an owner (or a GM) brings one bot to its own side through the one teleport path `.tb tele` uses; the per-bot cooldown is
// stamped only when that teleport started. All map, instance, combat and death rules are TeleportBotNextTo's (see TbH4.cpp).
bool HandleSummon(Player* player, Caller const& caller, OwnerScope const& scope, std::string_view tail)
{
    std::string_view const name = FirstWord(tail);
    if (name.empty())
    {
        Refuse(player, "summon", "usage", ".bot summon <bot>");
        return true;
    }
    Player* bot = nullptr;
    std::string code;
    if (!ResolveBot(name, bot, code))
    {
        Refuse(player, "summon", code.c_str(), code == "not-found" ? "no such online bot" : "that character is not a bot");
        return true;
    }
    BotFacts const facts = FactsOf(player, bot, scope);
    Verdict const verdict = Decide(caller, facts, Tier::Owner);   // D-4: owner or GM, and a running dungeon run refuses everyone
    if (verdict != Verdict::Allowed)
    {
        Refuse(player, "summon", CodeOf(verdict), "you may not summon that bot");
        return true;
    }
    // The bot would walk back to its own master, so only that master (or a GM) may bring it over.
    if (!caller.gm && (facts.master == 0 || facts.master != caller.guid))
    {
        Refuse(player, "summon", "not-master", "that bot follows another master; only its master may summon it");
        return true;
    }
    // The destination must hold still: the shared teleport refuses its own cases (combat, dead, instances), not these two.
    if (player->IsBeingTeleported())
    {
        Refuse(player, "summon", "teleporting", "you are being teleported; summon when you land");
        return true;
    }
    if (player->IsInFlight())
    {
        Refuse(player, "summon", "in-flight", "you ride a taxi; summon when you land");
        return true;
    }
    std::uint32_t left = 0;
    // A 64-bit monotonic clock: the core's getMSTime() is 32 bits and wraps after 49.7 days, and a stamp taken just before the wrap
    // would read as "just stamped" until a restart. steady_clock never wraps and never goes back; a restart forgets the stamps anyway.
    std::uint64_t const nowMs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    if (SummonCooldownLeft(nowMs, SummonStampOf(bot->GetGUID().GetCounter()), s_summonCooldown, caller.gm, left))
    {
        Refuse(player, "summon", "on-cooldown", "that bot was summoned moments ago; try again in " + std::to_string(left) + " s");
        return true;
    }
    h3::Rec rec("summon");
    bool const started = h4::TeleportBotNextTo(bot, player, false, rec);   // no revive for players (D-8)
    if (!started)
    {
        std::string refusal, text;
        TeleportRefusal(rec, refusal, text);
        Refuse(player, "summon", refusal.c_str(), text);
        return true;
    }
    SummonStamps()[bot->GetGUID().GetCounter()] = nowMs;   // D-8: stamped only by a teleport that started
    Say(player, "TortoiseBots: summon " + bot->GetName() + ": ok started; it arrives next to you");
    return true;
}

// TB's `.bot version` / `.bot about` / `.bot credits` (commands/Info.cpp HandleVersion): the build id, the
// addon version line and the author notice NOTICE.md requires.
bool HandleVersion(Player* player)
{
    Say(player, "TortoiseBots: version: forever-port (core " + std::string(GitRevision::GetFullVersion()) + ")");
    Say(player, "TBM:VERSION|forever-port");
    Say(player, "TortoiseBots: version: by Sagiroth - https://github.com/Sagiroth/TortoiseBots (AGPL-3.0 or GPL-2.0+, source available)");
    return true;
}

// TB's `.bot features` (commands/Info.cpp HandleFeatures): the same switches the addon's Info window reads.
bool HandleFeatures(Player* player)
{
    for (std::string const& line : addoninfo::FeaturesChatLines())
        Say(player, line);
    return true;
}

// D-9: one line built from the bot's own objects on the world thread (no engine tick, no `stats` whisper onto another channel):
// level, class, health %, mana %, money in g/s/c, the zone id and name, alive/dead, and the movement it was last told to keep.
bool HandleStats(Player* player, Caller const& caller, OwnerScope const& scope, std::string_view tail)
{
    std::string_view const name = FirstWord(tail);
    if (name.empty())
    {
        // TB's `.bot stats`: one line about the fleet the caller may see. TBF has no random bots, so
        // the owned and engine counts replace TB's random and AI counts.
        std::uint32_t online = 0, owned = 0, engines = 0;
        for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
        {
            Player* fleetBot = ObjectAccessor::FindPlayer(guid);
            if (!fleetBot || !fleetBot->IsInWorld())
                continue;
            BotFacts const facts = FactsOf(player, fleetBot, scope);
            if (!caller.gm && !Visible(caller, facts))
                continue;
            ++online;
            owned += facts.account != 0 ? 1 : 0;
            engines += BrainWord(guid) == "engine" ? 1 : 0;
        }
        Say(player, "TortoiseBots: stats: " + std::to_string(online) + " bot(s) online, " + std::to_string(owned) +
                        " owned by you, " + std::to_string(engines) + " with an engine");
        return true;
    }
    Player* bot = nullptr;
    std::string code;
    if (!ResolveBot(name, bot, code))
    {
        Refuse(player, "stats", code.c_str(), code == "not-found" ? "no such online bot" : "that character is not a bot");
        return true;
    }
    BotFacts const facts = FactsOf(player, bot, scope);
    Verdict const verdict = Decide(caller, facts, Tier::Tactical);   // D-4: the tactical faces read a bot's line
    if (verdict != Verdict::Allowed)
    {
        Refuse(player, "stats", CodeOf(verdict), "you may not read that bot");
        return true;
    }
    std::uint64_t const money = bot->GetMoney();
    std::uint32_t const zoneId = bot->GetZoneId();
    std::string zone = "zone " + std::to_string(zoneId);
    if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId))
    {
        char const* areaName = area->AreaName[LOCALE_enUS];
        if (areaName && *areaName)
            zone += " " + std::string(areaName);
        else if (area->ZoneName && *area->ZoneName)
            zone += " " + std::string(area->ZoneName);
    }
    std::string const hp = std::to_string(std::lround(bot->GetHealthPct()));
    std::string const mana = std::to_string(std::lround(bot->GetPowerPct(POWER_MANA)));
    std::string const coins = std::to_string(money / 10000) + "g " + std::to_string((money % 10000) / 100) + "s " +
                              std::to_string(money % 100) + "c";
    Say(player, "TortoiseBots: stats " + bot->GetName() + ": ok level " + std::to_string(bot->GetLevel()) + " " +
                    ClassName(bot->GetClass()) + ", " + hp + "% hp" + ", " + mana + "% mana" + ", " + coins + ", " + zone +
                    ", " + (bot->IsAlive() ? "alive" : "dead") + ", " + MoveWord(bot->GetGUID()));
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
    // The legacy key, with the legacy read's own default and cap (PlayerbotAIConfig.cpp:1156-1158): one key, one meaning.
    s_summonCooldown = static_cast<std::uint32_t>(std::clamp<int32>(sConfigMgr->GetIntDefault("AiPlayerbot.OwnedBotSummonCooldown", 300), 0, 3600));
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
    // Public build metadata, TB's AlwaysVisible rows: not part of the order surface, so the player-commands
    // switch does not hide them.
    if (verb == "version" || verb == "v" || verb == "about" || verb == "credits")
        return HandleVersion(player);
    if (verb == "features")
        return HandleFeatures(player);
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
    if (verb == "maintenance")
        return HandleMaintenance(player, caller, scope, AfterFirstWord(rest));
    if (verb == "totem")
        return choice::HandleTotem(player, caller, scope, AfterFirstWord(rest));
    if (verb == "behavior" || verb == "behaviour")
        return choice::HandleBehavior(player, caller, scope, AfterFirstWord(rest));
    if (verb == "blessing")
        return choice::HandleBlessing(player, caller, scope, AfterFirstWord(rest));
    if (verb == "choice")
        return choice::HandleChoice(player, caller, scope, AfterFirstWord(rest));
    if (verb == "service")
        return choice::HandleService(player, caller, scope, AfterFirstWord(rest));
    if (verb == "autogear")
        return autogear::HandleBotAutogear(player, caller.gm, scope, AfterFirstWord(rest));
    if (verb == "summon")
        return HandleSummon(player, caller, scope, AfterFirstWord(rest));
    if (verb == "stats")
        return HandleStats(player, caller, scope, AfterFirstWord(rest));
    if (verb == "hire")
        return HandleHire(player, AfterFirstWord(rest));
    if (verb == "lfg")
        return lfg::HandlePlayerLfg(player, AfterFirstWord(rest));

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

// R5 fix: the same ownership read as `.bot` (ScopeOf: the sender's Battle.net account, checked against the login DB now, nothing kept
// between messages) applied to the bot's game account; the verdict is the pure rule.
bool CallerOwnsBotForChat(Player const& sender, Player const& bot)
{
    OwnerScope const scope = ScopeOf(&sender);
    std::uint32_t const gameAccount = sCharacterCache->GetCharacterAccountIdByGuid(bot.GetGUID());
    std::uint32_t const botOwnerBnet = scope.Owns(gameAccount) ? scope.bnet : 0;
    return ChatOwnerAllowed(false, scope.bnet, botOwnerBnet);
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
bool CallerOwnsBotForChat(Player const&, Player const&) { return false; }
void SetReplySink(std::vector<std::string>*) { }
bool CaptureReply(std::string const&) { return false; }
}
}

#endif
