// The add-on hint live half (TBF #46): the login note, the TBM seen set, the
// "once" row in the characters database and the world tick that sends the
// line. The rule without the core is host/TbHostAddonHintPure.h.
#include "host/TbHostAddonHint.h"
#include "host/TbHostAddonHintPure.h"

#include "TbBotMgr.h"
#include "compat/TbCompat_CharStore.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Timer.h"
#include "WorldSession.h"

#include <ctime>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{
namespace addonhint
{
namespace
{
char const* const kTable = "tortoise_bots_player_flag";
char const* const kFlag = "addon-hint";
char const* const kKeyEnabled = "TortoiseBots.Player.AddonHint";
char const* const kKeyDelay = "TortoiseBots.Player.AddonHintDelay";
char const* const kKeyUrl = "TortoiseBots.Player.AddonHintUrl";
char const* const kDefaultUrl = "https://github.com/TortoiseBots/TortoiseBotsForeverManager/releases";
int32 const kDefaultDelaySeconds = 45;

// Same DDL as forever/sql/characters/2026_11_24_10_tb_player_flag.sql: run
// once at startup so a server that never applied the migration still works
// when its database user may create tables. tools/test_addon_hint_wiring.py
// pins the equality.
char const* const kCreate =
    "CREATE TABLE IF NOT EXISTS `tortoise_bots_player_flag` ("
    "`guid` bigint unsigned NOT NULL,"
    "`flag` varchar(32) NOT NULL,"
    "`set_at` int unsigned NOT NULL DEFAULT '0',"
    "PRIMARY KEY (`guid`, `flag`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

struct State
{
    std::mutex seenMutex;                  // guards `seen` (the addon hook thread writes it)
    std::unordered_set<uint64_t> seen;     // TBM messages this server run
    std::unordered_set<uint64_t> flagged;  // the store's addon-hint rows (world thread)
    std::unordered_map<uint64_t, uint32_t> pending;   // guid -> login ms (world thread)
    bool available = false;
    bool warned = false;
    bool createTried = false;
};

State& Store()
{
    static State s;
    return s;
}

bool g_enabled = false;
uint32_t g_delayMs = static_cast<uint32_t>(kDefaultDelaySeconds) * 1000;
std::string g_url = kDefaultUrl;

// ForeverCore aborts the world on a statement naming a missing table or column
// (MySQLConnection.cpp), so the table is probed before any statement names it.
void EnsureSchema(State& s, bool create)
{
    if (create && !s.createTried)
    {
        s.createTried = true;
        CharacterExecuteGuardedCompat(kCreate);
    }
    std::set<std::string> const columns = { "guid", "flag", "set_at" };
    s.available = CharacterSchemaHasCompat(kTable, columns);
    if (!s.available && !s.warned)
    {
        s.warned = true;
        TC_LOG_WARN("tortoisebots", "TortoiseBots: `{}` is missing or lacks columns; the add-on hint is kept for this server run only. Apply forever/sql/characters/2026_11_24_10_tb_player_flag.sql.", kTable);
    }
}

// "Told, or seen with the add-on": the set remembers it for the run whether
// or not the table answered, and the rows for one world tick go out in one
// statement (FlushFlags) instead of one round trip per character.
void FlagCharacter(State& s, std::vector<uint64_t>& batch, uint64_t key)
{
    if (!key || !s.flagged.insert(key).second)
        return;
    if (s.available)
        batch.push_back(key);
}

void FlushFlags(std::vector<uint64_t> const& batch)
{
    if (batch.empty())
        return;
    std::string sql = "INSERT IGNORE INTO `tortoise_bots_player_flag` (`guid`, `flag`, `set_at`) VALUES ";
    std::string const setAt = std::to_string(static_cast<uint32_t>(std::time(nullptr)));
    bool first = true;
    for (uint64_t const key : batch)
    {
        if (!first)
            sql += ", ";
        sql += "(" + std::to_string(key) + ", '" + kFlag + "', " + setAt + ")";
        first = false;
    }
    CharacterExecuteGuardedCompat(sql);
}

Player* ConnectedPlayer(uint64_t key)
{
    ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(key);
    Player* const player = ObjectAccessor::FindConnectedPlayer(guid);
    return player && player->GetSession() ? player : nullptr;
}

// Above the normal player level: moderators and everyone above them (this
// core documents `> SEC_PLAYER` as the elevated-account test and uses it for
// bot command authorization). No unasked line goes to a staff session.
bool IsStaff(Player* player)
{
    WorldSession* const session = player ? player->GetSession() : nullptr;
    return session && session->GetSecurity() > SEC_PLAYER;
}

bool StoredFlag(State& s, uint64_t key)
{
    if (!s.available)
        return s.flagged.count(key) != 0;
    QueryResult const result = CharacterQueryGuardedCompat(
        "SELECT `guid` FROM `tortoise_bots_player_flag` WHERE `guid` = " + std::to_string(key)
        + " AND `flag` = '" + kFlag + "'");
    return static_cast<bool>(result);
}

void Refuse(ChatHandler* handler, std::string const& text)
{
    handler->SendSysMessage("TortoiseBots: addonhint: " + text);
    handler->SetSentErrorMessage(true);
}
} // namespace

void LoadConfig()
{
    g_enabled = sConfigMgr->GetBoolDefault(kKeyEnabled, false);
    g_delayMs = pure::ClampDelaySeconds(sConfigMgr->GetIntDefault(kKeyDelay, kDefaultDelaySeconds)) * 1000;
    g_url = sConfigMgr->GetStringDefault(kKeyUrl, kDefaultUrl);
}

void LoadAtStartup()
{
    if (!g_enabled)
    {
        TC_LOG_INFO("tortoisebots", "TortoiseBots: add-on hint is off ({} = 0)", kKeyEnabled);
        return;
    }
    if (!BotMgr::Instance().Enabled())
        return;
    State& s = Store();
    EnsureSchema(s, true);
    if (!s.available)
        return;
    QueryResult const result = CharacterQueryGuardedCompat(
        "SELECT `guid` FROM `tortoise_bots_player_flag` WHERE `flag` = '" + std::string(kFlag) + "'");
    if (result)
    {
        do
        {
            uint64 const guid = result->Fetch()[0].GetUInt64();
            if (guid)
                s.flagged.insert(guid);
        } while (result->NextRow());
    }
    TC_LOG_INFO("tortoisebots", "TortoiseBots: loaded {} add-on hint rows", uint32(s.flagged.size()));
}

void NoteLogin(Player* player)
{
    if (!player || !player->GetSession())
        return;
    if (!g_enabled || !BotMgr::Instance().Enabled())
        return;
    ObjectGuid const guid = player->GetGUID();
    if (BotMgr::Instance().IsBot(guid) || player->GetSession()->IsBot())
        return;
    uint64_t const key = guid.GetCounter();
    if (!key)
        return;
    State& s = Store();
    if (s.flagged.count(key))
        return;
    s.pending[key] = getMSTime();
}

void NoteAddonSeen(uint64_t lo, uint64_t hi)
{
    if (!g_enabled)
        return;
    ObjectGuid guid;
    guid.SetRawValue(hi, lo);
    uint64_t const key = guid.GetCounter();
    if (!key)
        return;
    State& s = Store();
    std::lock_guard<std::mutex> lock(s.seenMutex);
    s.seen.insert(key);
}

void Tick(uint32_t nowMs)
{
    if (!g_enabled || !BotMgr::Instance().Enabled())
        return;
    State& s = Store();
    std::vector<uint64_t> seen;
    {
        std::lock_guard<std::mutex> lock(s.seenMutex);
        if (s.seen.empty() && s.pending.empty())
            return;
        seen.assign(s.seen.begin(), s.seen.end());
        s.seen.clear();
    }

    // A TBM message is the add-on: flag the character even when no hint was
    // pending (a client that loses the add-on later is not nagged). The entry
    // is settled with it. The rows go to the store once, after the loop.
    std::vector<uint64_t> batch;
    for (uint64_t const key : seen)
    {
        FlagCharacter(s, batch, key);
        s.pending.erase(key);
    }

    uint32_t sent = 0;
    for (auto it = s.pending.begin(); it != s.pending.end();)
    {
        uint64_t const key = it->first;
        Player* const player = ConnectedPlayer(key);
        if (!player)
        {
            it = s.pending.erase(it);
            continue;
        }
        bool settled = false;
        bool flag = false;
        {
            // The observation is taken and the line sent under the one lock
            // NoteAddonSeen takes: a TBM message that arrives before the
            // decision always wins, and one that arrives after it comes from a
            // player already told. NoteAddonSeen waits on nothing else while
            // holding the mutex, so one SendSysMessage inside it cannot
            // deadlock.
            std::lock_guard<std::mutex> lock(s.seenMutex);
            bool const seenNow = s.seen.erase(key) != 0;
            pure::HintStep const step = pure::Decide(s.flagged.count(key) != 0, seenNow, it->second, nowMs, g_delayMs,
                IsStaff(player));
            if (step == pure::HintStep::DropStaff)
            {
                settled = true;
            }
            else if (step == pure::HintStep::DropSeen)
            {
                settled = true;
                flag = true;
            }
            else if (step == pure::HintStep::Send && sent < pure::kMaxHintsPerTick)
            {
                std::string const text = pure::HintText(g_url);
                ChatHandler(player->GetSession()).SendSysMessage(text);
                settled = true;
                flag = true;
                ++sent;
            }
        }
        if (!settled)
        {
            ++it;
            continue;
        }
        if (flag)
            FlagCharacter(s, batch, key);
        it = s.pending.erase(it);
    }

    FlushFlags(batch);
}

void Forget(ObjectGuid guid)
{
    uint64_t const key = guid.GetCounter();
    if (!key)
        return;
    State& s = Store();
    s.pending.erase(key);
    std::lock_guard<std::mutex> lock(s.seenMutex);
    s.seen.erase(key);
}

bool HandleCommand(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    if (!handler)
        return false;
    if (!g_enabled)
    {
        Refuse(handler, "off (TortoiseBots.Player.AddonHint = 0).");
        return false;
    }
    std::istringstream words{ std::string(args) };
    std::string action;
    std::string name;
    words >> action >> name;
    if ((action != "status" && action != "reset") || name.empty())
    {
        Refuse(handler, "usage: .tb addonhint status|reset <character>.");
        return false;
    }
    if (!normalizePlayerName(name))
    {
        Refuse(handler, "usage: .tb addonhint status|reset <character>.");
        return false;
    }
    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(name);
    if (guid.IsEmpty())
    {
        Refuse(handler, "not-found " + name + ".");
        return false;
    }
    std::string canonical = name;
    sCharacterCache->GetCharacterNameByGuid(guid, canonical);
    uint64_t const key = guid.GetCounter();
    State& s = Store();

    if (action == "reset")
    {
        if (s.available)
        {
            CharacterExecuteGuardedCompat(
                "DELETE FROM `tortoise_bots_player_flag` WHERE `guid` = " + std::to_string(key)
                + " AND `flag` = '" + kFlag + "'");
        }
        s.flagged.erase(key);
        s.pending.erase(key);
        {
            std::lock_guard<std::mutex> lock(s.seenMutex);
            s.seen.erase(key);
        }
        handler->PSendSysMessage("TortoiseBots: addonhint %s: reset.", canonical.c_str());
        return true;
    }

    bool const told = StoredFlag(s, key);
    bool const pending = s.pending.count(key) != 0;
    handler->PSendSysMessage("TortoiseBots: addonhint %s: %s", canonical.c_str(),
        told ? "told" : (pending ? "not-told pending" : "not-told -"));
    return true;
}
} // namespace addonhint
} // namespace TortoiseBots
