// the module-owned dungeon-finder queue and its fill, see host/TbHostLfgFill.h.
//
// The pass is TortoiseBots' LFT fill cadence (runtime/LftBotFillService.cpp:594-596,653,946): one timer clamped to
// 5-60 s, and at most AiPlayerbot.RandomBotLftMaxFillsPerInterval bots per pass. A bot is picked by the pure policy,
// grouped with the player through BotMgr::GroupWith (the core's own invite/accept path), given the group's LFG role bit
// (Group::SetLfgRoles, the write LFGMgr::MakeNewGroup makes) and the kit role (h3::SetBotRole, the `.tb role` commit).
// Nothing here reads or writes sLFGMgr: the 1.60 client has no dungeon list, so the queue is the module's.
#include "host/TbHostLfgFill.h"
#include "host/TbH3.h"
#include "host/TbLfgFillPure.h"
#include "host/TbOrdersApi.h"
#include "host/player/TbPlayerCmd.h"

#include "compat/botpch_forever.h"

#include "host/TbEngineBrain.h"
#include "TbBotMgr.h"

#include "DB2Stores.h"
#include "Group.h"
#include "LFG.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include "playerbot/PlayerbotAIConfig.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace lfg
{
namespace
{
// One bot the fill put in a party, and the slot it fills.
struct Claim
{
    ObjectGuid bot;
    std::uint8_t role = 0;
};

// The queue of one player. A player has one entry: joining again is refused (`already-queued`).
struct Entry
{
    ObjectGuid player;
    std::string playerName;
    std::uint32_t dungeon = 0;
    std::string dungeonName;
    std::uint8_t role = 0;
    bool complete = false;
    std::vector<Claim> claims;
    std::set<std::uint8_t> reported;   // roles whose "no candidate" or refusal was already logged, so the pass is not noisy
};

struct State
{
    bool configured = false;
    bool enabled = false;
    std::uint32_t intervalMs = 15000;
    std::uint32_t maxPerPass = 1;
    bool allowBorrow = false;
    std::uint32_t elapsedMs = 0;
    bool logged = false;
    bool loggedOn = false;
    std::map<std::uint64_t, Entry> entries;
};

State& S()
{
    static State state;
    return state;
}

// The legacy `sLog` of botpch_forever.h replaces the core's, so TC_LOG_* does not compile after it; its proxy writes the
// same line to the server log (forever-compat-shim.h, CmangosLogProxy). The text is an argument, never a format.
void LogLine(std::string const& text)
{
    sLog.outString("%s", ("TortoiseBots: LFG fill: " + text).c_str());
}

std::string ClassName(Player* bot)
{
    if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(bot->GetClass()))
        if (entry->Name[LOCALE_enUS])
            return entry->Name[LOCALE_enUS];
    return "class " + std::to_string(unsigned(bot->GetClass()));
}

std::string BotName(ObjectGuid guid)
{
    Player* bot = ObjectAccessor::FindPlayer(guid);
    return bot ? bot->GetName() : guid.ToString();
}

std::vector<std::uint8_t> FilledRoles(Entry const& entry)
{
    std::vector<std::uint8_t> filled;
    filled.reserve(entry.claims.size());
    for (Claim const& claim : entry.claims)
        filled.push_back(claim.role);
    return filled;
}

std::vector<std::string> NeedNames(Entry const& entry)
{
    std::vector<std::string> names;
    for (std::uint8_t role : lfgfill::MissingSlots(entry.role, FilledRoles(entry)))
        names.push_back(lfgfill::RoleName(role));
    return names;
}

// The specs are read once: EnsureConfig remembers its success, so a reload never changes them (TB reads them at
// Initialize for the same reason). The fallbacks are TB's defaults (PlayerbotAIConfig.cpp:999-1002).
void LoadConfig()
{
    if (!engine::ConfigReady())
        return;
    State& s = S();
    s.configured = true;
    s.enabled = sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.randomBotLftEnabled;
    s.intervalMs = std::clamp<std::uint32_t>(sPlayerbotAIConfig.randomBotLftUpdateInterval, 5000, 60000);
    s.maxPerPass = std::clamp<std::uint32_t>(sPlayerbotAIConfig.randomBotLftMaxFillsPerInterval, 0, 5);
    s.allowBorrow = sPlayerbotAIConfig.randomBotLftAllowRoleBorrow;
    if (!s.logged || s.loggedOn != s.enabled)
    {
        s.logged = true;
        s.loggedOn = s.enabled;
        if (s.enabled)
            LogLine(Trinity::StringFormat("enabled interval {} ms max {} per pass borrow {}", s.intervalMs, s.maxPerPass,
                s.allowBorrow ? 1 : 0));
        else
            LogLine("disabled (AiPlayerbot.RandomBotLftEnabled = 0)");
    }
}

// The engine config loads on its first brain; before that the keys are not readable (PlayerbotAIConfig::enabled has no default).
bool EnsureConfigured()
{
    State& s = S();
    if (s.configured)
        return true;
    if (!engine::Built() || !engine::ConfigReady())
        return false;
    LoadConfig();
    return s.configured;
}

lfgfill::CandidateFacts FactsOf(Player* bot, std::uint32_t playerTeam, bool claimed)
{
    lfgfill::CandidateFacts facts;
    ObjectGuid const guid = bot->GetGUID();
    facts.inWorld = bot->IsInWorld();
    facts.free = BotMgr::Instance().IsFreeBot(guid);
    facts.grouped = bot->GetGroup() != nullptr;
    facts.hasMaster = !BotMgr::Instance().MasterOf(guid).IsEmpty();
    bool away = false, blocked = false, hasBrain = false;
    std::uint32_t awaySeconds = 0;
    std::string blockedWhy;
    BotMgr::Instance().FollowStatus(guid, away, awaySeconds, blocked, blockedWhy, hasBrain);
    facts.hasBrain = hasBrain;
    facts.inBattleground = bot->InBattleground();
    facts.inBattlegroundQueue = bot->InBattlegroundQueue();
    facts.teleporting = bot->IsBeingTeleported();
    facts.flying = bot->IsInFlight();
    facts.inCombat = bot->IsInCombat();
    facts.alive = bot->IsAlive();
    facts.level = bot->GetLevel();
    facts.team = std::uint32_t(bot->GetTeam());
    facts.naturalRoles = h3::BotNaturalRoles(bot);
    facts.classId = unsigned(bot->GetClass());
    facts.claimed = claimed;
    return facts;
}

bool ClaimedByAnother(ObjectGuid bot, Entry const& self)
{
    for (auto const& [key, entry] : S().entries)
    {
        if (&entry == &self)
            continue;
        for (Claim const& claim : entry.claims)
            if (claim.bot == bot)
                return true;
    }
    return false;
}

void LogComplete(Entry const& entry)
{
    std::string tank, healer, dps;
    auto const add = [&](std::string const& name, std::uint8_t role)
    {
        if (role == lfgfill::RoleTank)
            tank = name;
        else if (role == lfgfill::RoleHealer)
            healer = name;
        else if (role == lfgfill::RoleDps)
            dps += (dps.empty() ? "" : ",") + name;
    };
    add(entry.playerName, entry.role);
    for (Claim const& claim : entry.claims)
        add(BotName(claim.bot), claim.role);
    LogLine("party complete for " + entry.playerName + ": tank=" + (tank.empty() ? "-" : tank) + ", healer=" +
        (healer.empty() ? "-" : healer) + ", dps=" + (dps.empty() ? "-" : dps));
}

// Lets go of every bot this entry grouped: the kit role goes, the bot leaves the party, and (when nothing else holds it) it
// plays free again. A bot someone else took over meanwhile is left alone.
void ReleaseClaims(Entry& entry, char const* why)
{
    for (Claim const& claim : entry.claims)
    {
        Player* bot = ObjectAccessor::FindPlayer(claim.bot);
        if (!bot || !bot->IsInWorld())
            continue;
        std::string code, detail;
        h3::SetBotRole(bot, 0, code, detail);
        BotMgr::Instance().Ungroup(claim.bot);
        if (!bot->GetGroup() && !BotMgr::Instance().MasterOf(claim.bot).IsEmpty())
            BotMgr::Instance().SetMaster(claim.bot, ObjectGuid::Empty);
        std::string const refusal = BotMgr::Instance().SetFree(claim.bot, true);
        if (refusal.empty())
            LogLine("released " + bot->GetName() + " (" + why + ")");
        else
            LogLine("left " + bot->GetName() + " where it is (" + refusal + ")");
    }
    entry.claims.clear();
    entry.complete = false;
    entry.reported.clear();
}

// A claim whose bot left the party (or the world) is dropped; the slot is filled again on the next pass.
void DropLostClaims(Entry& entry, Player* player)
{
    for (auto itr = entry.claims.begin(); itr != entry.claims.end();)
    {
        Player* bot = ObjectAccessor::FindPlayer(itr->bot);
        if (!bot || !bot->IsInWorld() || bot->GetGroup() != player->GetGroup())
        {
            itr = entry.claims.erase(itr);
            entry.complete = false;
            entry.reported.clear();
            continue;
        }
        ++itr;
    }
}

void Fill(Entry& entry, Player* player, std::uint32_t& budget)
{
    Group* group = player->GetGroup();
    if (group && lfgfill::PartyComplete(group->GetMembersCount()))
    {
        if (!entry.complete)
        {
            entry.complete = true;
            LogComplete(entry);
        }
        return;
    }

    // Bots in guid order, so two bots at the same level gap always resolve the same way (PickCandidate's index tie-break).
    std::vector<ObjectGuid> guids = BotMgr::Instance().BotGuids();
    std::sort(guids.begin(), guids.end(), [](ObjectGuid const& left, ObjectGuid const& right)
    {
        return left.GetCounter() < right.GetCounter();
    });
    std::vector<ObjectGuid> pool;
    std::vector<lfgfill::CandidateFacts> facts;
    std::uint32_t const playerTeam = std::uint32_t(player->GetTeam());
    for (ObjectGuid const& guid : guids)
    {
        Player* bot = BotMgr::Instance().FindBot(guid);
        if (!bot)
            continue;
        pool.push_back(guid);
        facts.push_back(FactsOf(bot, playerTeam, ClaimedByAnother(guid, entry)));
    }

    while (budget)
    {
        std::vector<std::uint8_t> const missing = lfgfill::MissingSlots(entry.role, FilledRoles(entry));
        if (missing.empty())
            break;
        std::uint8_t const need = missing.front();
        lfgfill::Choice const choice = lfgfill::PickCandidate(facts, need, player->GetLevel(), playerTeam, S().allowBorrow);
        if (choice.index < 0)
        {
            if (entry.reported.insert(need).second)
                LogLine("no free " + std::string(lfgfill::RoleName(need)) + " candidate for " + entry.playerName + " (" +
                    std::to_string(pool.size()) + " free bot(s) in the world)");
            break;
        }
        std::size_t const index = std::size_t(choice.index);
        ObjectGuid const botGuid = pool[index];
        Player* bot = ObjectAccessor::FindPlayer(botGuid);
        std::string const error = bot ? BotMgr::Instance().GroupWith(botGuid, entry.player) : "not in the world";
        if (!error.empty())
        {
            if (entry.reported.insert(need).second)
                LogLine(BotName(botGuid) + " cannot join as " + std::string(lfgfill::RoleName(need)) + ": " + error);
            pool.erase(pool.begin() + std::ptrdiff_t(index));
            facts.erase(facts.begin() + std::ptrdiff_t(index));
            continue;
        }
        std::string code, detail;
        if (!h3::SetBotRole(bot, need, code, detail))
        {
            if (entry.reported.insert(need).second)
                LogLine(BotName(botGuid) + " cannot play " + std::string(lfgfill::RoleName(need)) + ": " + code + " (" + detail + ")");
            BotMgr::Instance().Ungroup(botGuid);
            BotMgr::Instance().SetFree(botGuid, true);
            pool.erase(pool.begin() + std::ptrdiff_t(index));
            facts.erase(facts.begin() + std::ptrdiff_t(index));
            continue;
        }
        group = player->GetGroup();
        if (group)
        {
            group->SetLfgRoles(botGuid, lfgfill::CoreRoleBits(need));
            group->SetLfgRoles(entry.player, lfgfill::CoreRoleBits(entry.role) | lfgfill::CoreRoleLeader);
        }
        entry.claims.push_back(Claim{ botGuid, need });
        entry.complete = false;
        entry.reported.clear();
        --budget;
        std::uint32_t const members = group ? group->GetMembersCount() : 2;
        LogLine(bot->GetName() + " joins as " + std::string(lfgfill::RoleName(need)) + " (" + ClassName(bot) + " " +
            std::to_string(bot->GetLevel()) + "), party " + std::to_string(members) + "/5");
        if (group && lfgfill::PartyComplete(members))
        {
            entry.complete = true;
            LogComplete(entry);
            break;
        }
    }
}

void Update(std::uint32_t diff)
{
    if (!EnsureConfigured())
        return;
    State& s = S();
    if (!s.enabled)
    {
        s.elapsedMs = 0;
        return;
    }
    s.elapsedMs += diff;
    if (s.elapsedMs < s.intervalMs)
        return;
    s.elapsedMs = 0;

    std::uint32_t budget = s.maxPerPass;
    for (auto itr = s.entries.begin(); itr != s.entries.end();)
    {
        Entry& entry = itr->second;
        Player* player = ObjectAccessor::FindPlayer(entry.player);
        lfgfill::PlayerFacts facts;
        facts.online = player != nullptr;
        facts.inWorld = player && player->IsInWorld();
        facts.inGroup = player && player->GetGroup() != nullptr;
        if (lfgfill::ShouldRelease(facts))
        {
            ReleaseClaims(entry, "player left");
            itr = s.entries.erase(itr);
            continue;
        }
        DropLostClaims(entry, player);
        if (budget)
            Fill(entry, player, budget);
        ++itr;
    }
}

bool ParseDungeonId(std::string const& text, std::uint32_t& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    unsigned long const id = std::strtoul(text.c_str(), &end, 10);
    if (!end || *end != '\0' || id == 0 || id > 0xFFFFFFul)
        return false;
    out = std::uint32_t(id);
    return true;
}

class LfgFillWorldScript : public WorldScript
{
public:
    LfgFillWorldScript() : WorldScript("tortoisebots_lfg_fill") { }

    void OnConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

    void OnWorldInitialized() override
    {
        if (!engine::Built() || !BotMgr::Instance().Enabled())
            return;
        if (!engine::ConfigReady())
            sLog.outError("%s", "TortoiseBots: LFG fill: the engine configuration is not loaded; the fill is off until it is");
        else
            LoadConfig();
    }

    void OnUpdate(std::uint32_t diff) override
    {
        Update(diff);
    }

    void OnShutdown() override
    {
        S().entries.clear();
    }
};
} // namespace

bool IsQueued(Player* player)
{
    return player && S().entries.count(player->GetGUID().GetCounter()) != 0;
}

JoinResult Join(Player* player, std::uint32_t dungeonId, std::uint8_t role)
{
    JoinResult result;
    if (!EnsureConfigured())
    {
        result.code = "not-ready";
        result.detail = "the engine configuration is not loaded yet; the fill cannot start";
        return result;
    }
    if (!S().enabled)
    {
        result.code = "disabled";
        result.detail = "the fill is off (AiPlayerbot.RandomBotLftEnabled = 0)";
        return result;
    }
    if (!player || !player->IsInWorld() || !player->GetSession())
    {
        result.code = "not-in-world";
        result.detail = "the player must be in the world";
        return result;
    }
    if (role != lfgfill::RoleTank && role != lfgfill::RoleHealer && role != lfgfill::RoleDps)
    {
        result.code = "bad-role";
        result.detail = "the role is tank, healer (heal) or dps (damage)";
        return result;
    }
    LFGDungeonsEntry const* dungeon = sLFGDungeonsStore.LookupEntry(dungeonId);
    if (!dungeon)
    {
        result.code = "no-such-dungeon";
        result.detail = "no dungeon with that id in the client's dungeon list";
        return result;
    }
    if (IsQueued(player))
    {
        result.code = "already-queued";
        result.detail = "that player is already waiting (leave first)";
        return result;
    }
    if (Group* group = player->GetGroup())
    {
        if (group->isRaidGroup())
        {
            result.code = "raid";
            result.detail = "raids are not filled: a party holds five";
            return result;
        }
        if (!group->IsLeader(player->GetGUID()))
        {
            result.code = "not-leader";
            result.detail = "the player must lead the party the bots join";
            return result;
        }
        if (group->GetMembersCount() >= MAX_GROUP_SIZE)
        {
            result.code = "party-full";
            result.detail = "the party already holds five";
            return result;
        }
    }

    Entry entry;
    entry.player = player->GetGUID();
    entry.playerName = player->GetName();
    entry.dungeon = dungeonId;
    entry.dungeonName = dungeon->Name[LOCALE_enUS] ? dungeon->Name[LOCALE_enUS] : std::to_string(dungeonId);
    entry.role = role;
    result.dungeonName = entry.dungeonName;
    result.need = NeedNames(entry);
    S().entries.emplace(entry.player.GetCounter(), entry);

    std::string need;
    for (std::string const& name : result.need)
        need += (need.empty() ? "" : ", ") + name;
    LogLine(entry.playerName + " queued " + lfgfill::RoleName(role) + " for dungeon " + std::to_string(dungeonId) + " (" +
        entry.dungeonName + "); need " + need);
    result.ok = true;
    return result;
}

LeaveResult Leave(Player* player)
{
    LeaveResult result;
    if (!player)
        return result;
    auto itr = S().entries.find(player->GetGUID().GetCounter());
    if (itr == S().entries.end())
        return result;
    result.queued = true;
    for (Claim const& claim : itr->second.claims)
        result.released.push_back(BotName(claim.bot));
    ReleaseClaims(itr->second, "player left");
    S().entries.erase(itr);
    return result;
}

std::vector<StatusRow> Rows()
{
    std::vector<StatusRow> rows;
    for (auto const& [key, entry] : S().entries)
    {
        StatusRow row;
        row.player = entry.playerName;
        row.dungeon = entry.dungeon;
        row.dungeonName = entry.dungeonName;
        row.role = entry.role;
        row.complete = entry.complete;
        row.need = NeedNames(entry);
        for (Claim const& claim : entry.claims)
            row.filled.push_back(std::string(lfgfill::RoleName(claim.role)) + ":" + BotName(claim.bot));
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), [](StatusRow const& left, StatusRow const& right)
    {
        return left.player < right.player;
    });
    return rows;
}

bool HandleLfg(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    h3::Words const args = h3::Split(tail);
    if (!h3::RequireBuilt(handler, args, "lfg"))
        return false;
    if (args.w.empty())
        return h3::Fail(handler, args, "lfg", "usage", ".tb lfg join <dungeon id> <tank|healer|dps> | .tb lfg status [<player>] | .tb lfg leave");

    std::string const verb = h3::Lower(args.w[0]);
    if (verb == "join")
    {
        Player* sender = h3::SenderPlayer(handler);
        if (!sender)
            return h3::Fail(handler, args, "lfg", "needs-player", "join in game: the group leader must be your own character");
        if (args.w.size() < 3)
            return h3::Fail(handler, args, "lfg", "usage", ".tb lfg join <dungeon id> <tank|healer|dps>");
        std::uint32_t dungeonId = 0;
        if (!ParseDungeonId(args.w[1], dungeonId))
            return h3::Fail(handler, args, "lfg", "bad-dungeon", args.w[1] + " is not a dungeon id (for example 39)");
        std::uint8_t const role = lfgfill::RoleOf(h3::Lower(args.w[2]));
        if (!role)
            return h3::Fail(handler, args, "lfg", "bad-role", "the role is tank, healer (heal) or dps (damage)");
        JoinResult const joined = Join(sender, dungeonId, role);
        if (!joined.ok)
            return h3::Fail(handler, args, "lfg", joined.code, joined.detail);
        h3::Reply reply(handler, args, "lfg");
        reply.Begin();
        h3::Rec rec("lfg");
        rec.S("player", sender->GetName()).N("dungeon", dungeonId).S("dungeon_name", joined.dungeonName)
            .S("role", lfgfill::RoleName(role)).S("state", "waiting").L("need", joined.need);
        reply.Line(rec);
        h3::Rec end("lfg");
        end.S("state", "queued").N("entries", 1);
        reply.End(end);
        return true;
    }
    if (verb == "status")
    {
        std::string const target = args.w.size() >= 2 ? args.w[1] : std::string();
        h3::Reply reply(handler, args, "lfg");
        reply.Begin();
        std::uint32_t shown = 0;
        for (StatusRow const& row : Rows())
        {
            if (!target.empty() && h3::Lower(row.player) != h3::Lower(target))
                continue;
            h3::Rec rec("lfg");
            rec.S("player", row.player).N("dungeon", row.dungeon).S("dungeon_name", row.dungeonName)
                .S("role", lfgfill::RoleName(row.role)).S("state", row.complete ? "complete" : "waiting")
                .L("need", row.need);
            std::vector<std::vector<std::string>> filled;
            for (std::string const& pair : row.filled)
            {
                std::size_t const colon = pair.find(':');
                filled.push_back({ pair.substr(0, colon), pair.substr(colon + 1) });
            }
            rec.LP("filled", filled);
            reply.Line(rec);
            ++shown;
        }
        h3::Rec end("lfg");
        end.S("state", "ok").N("entries", shown);
        reply.End(end);
        return true;
    }
    if (verb == "leave")
    {
        Player* sender = h3::SenderPlayer(handler);
        if (!sender)
            return h3::Fail(handler, args, "lfg", "needs-player", "leave in game: the queue entry belongs to your own character");
        LeaveResult const left = Leave(sender);
        if (!left.queued)
            return h3::Fail(handler, args, "lfg", "not-queued", "that player is not waiting");
        h3::Reply reply(handler, args, "lfg");
        reply.Begin();
        h3::Rec rec("lfg");
        rec.S("player", sender->GetName()).S("state", "left").L("released", left.released);
        reply.Line(rec);
        h3::Rec end("lfg");
        end.S("state", "ok").N("released", left.released.size());
        reply.End(end);
        return true;
    }
    return h3::Fail(handler, args, "lfg", "unknown-verb", "join, status or leave");
}

bool HandlePlayerLfg(Player* who, std::string_view tail)
{
    if (!who)
        return false;
    auto const say = [who](std::string const& line) { TortoiseBots::player::Say(who, line); };
    auto const refuse = [who](char const* code, std::string const& detail) { TortoiseBots::player::Refuse(who, "lfg", code, detail); };
    auto const firstWord = [](std::string_view text, std::string_view& rest) -> std::string_view
    {
        std::size_t i = 0;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        std::size_t const begin = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        std::string_view const word = text.substr(begin, i - begin);
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        rest = text.substr(i);
        return word;
    };
    auto const needText = [](std::vector<std::string> const& names)
    {
        std::string out;
        for (std::string const& name : names)
            out += (out.empty() ? "" : ", ") + name;
        return out.empty() ? std::string("none") : out;
    };

    std::string_view rest;
    std::string const verb = h3::Lower(firstWord(tail, rest));
    if (verb.empty() || verb == "status")
    {
        bool found = false;
        StatusRow row;
        for (StatusRow const& candidate : Rows())
        {
            if (h3::Lower(candidate.player) != h3::Lower(who->GetName()))
                continue;
            row = candidate;
            found = true;
            break;
        }
        if (!found)
        {
            say("TortoiseBots: lfg: not queued (join <dungeon id> <tank|healer|dps>)");
            return true;
        }
        std::string filled;
        for (std::string const& pair : row.filled)
            filled += (filled.empty() ? "" : ", ") + pair;
        say("TortoiseBots: lfg: " + std::string(row.complete ? "complete" : "waiting") + " for " + row.dungeonName + " (" +
            std::to_string(row.dungeon) + ") as " + lfgfill::RoleName(row.role) + "; need " + needText(row.need) +
            "; filled " + (filled.empty() ? std::string("none") : filled));
        return true;
    }
    if (verb == "join")
    {
        std::string_view const dungeonText = firstWord(rest, rest);
        std::string_view const roleText = firstWord(rest, rest);
        if (dungeonText.empty() || roleText.empty())
        {
            refuse("usage", ".bot lfg join <dungeon id> <tank|healer|dps>");
            return true;
        }
        std::uint32_t dungeonId = 0;
        if (!ParseDungeonId(std::string(dungeonText), dungeonId))
        {
            refuse("bad-dungeon", std::string(dungeonText) + " is not a dungeon id (for example 39)");
            return true;
        }
        std::uint8_t const role = lfgfill::RoleOf(h3::Lower(roleText));
        if (!role)
        {
            refuse("bad-role", "the role is tank, healer (heal) or dps (damage)");
            return true;
        }
        JoinResult const joined = Join(who, dungeonId, role);
        if (!joined.ok)
        {
            refuse(joined.code.c_str(), joined.detail);
            return true;
        }
        say("TortoiseBots: lfg: queued " + std::string(lfgfill::RoleName(role)) + " for " + joined.dungeonName + " (" +
            std::to_string(dungeonId) + "); need " + needText(joined.need));
        return true;
    }
    if (verb == "leave")
    {
        LeaveResult const left = Leave(who);
        if (!left.queued)
        {
            say("TortoiseBots: lfg: not queued");
            return true;
        }
        say("TortoiseBots: lfg: left; released " + std::to_string(left.released.size()) + " bot(s)");
        return true;
    }
    refuse("unknown-verb", "no such command; try .bot lfg join|status|leave");
    return true;
}

void RegisterScripts()
{
    new LfgFillWorldScript();
}
} // namespace lfg
} // namespace TortoiseBots
