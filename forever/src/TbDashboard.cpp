#include "TbDashboard.h"
#include "TbBotMgr.h"
#include "Config.h"
#include "Creature.h"
#include "GitRevision.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "GameTime.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "UpdateTime.h"
#include "World.h"
#include "WorldSession.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <ctime>
#include <fstream>
#include <unordered_map>
#include <memory>
#include <sstream>

namespace
{
// The dashboard's model.ProtocolVersion this sender writes.
constexpr int ProtocolVersion = 12;
// Bots per BOT_BATCH datagram: keeps one datagram well under the 64 KB UDP limit.
constexpr size_t BotsPerBatch = 40;

struct Sender
{
    bool enabled = false;
    uint32 intervalMs = 2000;
    uint32 sinceMs = 0;
    uint64 session = 0;
    uint64 seq = 0;
    boost::asio::io_context io;
    std::unique_ptr<boost::asio::ip::udp::socket> socket;
    boost::asio::ip::udp::endpoint target;
    std::string itemsFile;
    uint32 serverInfoSinceMs = 0;
    bool serverInfoSent = false;
    std::vector<std::string> events;   // BOT_EVENTS rows waiting for the next cycle
    struct Track { float x = 0, y = 0; uint32 map = 0; time_t since = 0; time_t lastStuck = 0; bool alive = true; };
    std::unordered_map<ObjectGuid, Track> track;
};

// A bot whose position has not changed for this long while its master is far (same map) is reported STUCK, at most once per StuckRepeatSecs.
constexpr time_t StuckSecs = 60;
constexpr time_t StuckRepeatSecs = 300;
constexpr float StuckMasterDist = 30.0f;

Sender& Get()
{
    static Sender sender;
    return sender;
}

std::string Escape(std::string const& in)
{
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                    out += char(c);
        }
    }
    return out;
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

char const* StateOf(Player* bot)
{
    if (!bot->IsAlive())
        return "dead";
    if (bot->IsInCombat())
        return "combat";
    if (bot->isMoving())
        return "moving";
    return "idle";
}

bool IsBot(Player* player)
{
    return player && TortoiseBots::BotMgr::Instance().IsBot(player->GetGUID());
}

void QueueEvent(Player* bot, char const* event, std::string const& info1, std::string const& info2)
{
    Sender& s = Get();
    if (!s.enabled || s.events.size() >= 500)
        return;
    std::ostringstream ev;
    ev << "{\"event\":\"" << event << "\",\"info1\":\"" << Escape(info1) << "\",\"info2\":\"" << Escape(info2) << "\",\"bot\":\""
       << Escape(bot->GetName()) << "\",\"guid\":" << bot->GetGUID().GetCounter() << ",\"class\":\"" << ClassName(bot->GetClass())
       << "\",\"level\":" << uint32(bot->GetLevel()) << ",\"map\":" << bot->GetMapId() << ",\"zone\":" << bot->GetZoneId()
       << ",\"money\":" << bot->GetMoney() << "}";
    s.events.push_back(ev.str());
}

void SendAnomaly(Player* bot, char const* type, char const* severity, std::string const& target, std::string const& details);

void Send(std::string const& datagram)
{
    Sender& s = Get();
    boost::system::error_code ec;
    s.socket->send_to(boost::asio::buffer(datagram), s.target, 0, ec);
}

void SendAnomaly(Player* bot, char const* type, char const* severity, std::string const& target, std::string const& details)
{
    std::ostringstream a;
    a << "{\"ts\":" << GameTime::GetGameTime() << ",\"type\":\"" << type << "\",\"severity\":\"" << severity << "\",\"bot\":\""
      << Escape(bot->GetName()) << "\",\"guid\":" << bot->GetGUID().GetCounter() << ",\"class\":\"" << ClassName(bot->GetClass())
      << "\",\"level\":" << uint32(bot->GetLevel()) << ",\"map\":" << bot->GetMapId() << ",\"zone\":" << bot->GetZoneId()
      << ",\"pos\":{\"x\":" << bot->GetPositionX() << ",\"y\":" << bot->GetPositionY() << ",\"z\":" << bot->GetPositionZ()
      << "},\"target\":\"" << Escape(target) << "\",\"strategy\":\"\",\"last_action\":\"\",\"details\":\"" << Escape(details) << "\"}";
    Send(a.str());
}

void SendServerInfo()
{
    Sender& s = Get();
    TortoiseBots::BotMgr& mgr = TortoiseBots::BotMgr::Instance();
    std::ostringstream ss;
    ss << "{\"v\":" << ProtocolVersion << ",\"session\":" << s.session << ",\"seq\":" << s.seq << ",\"ts\":" << GameTime::GetGameTime()
       << ",\"type\":\"SERVER_INFO\",\"module_version\":\"forever-port\",\"core_revision\":\"" << Escape(GitRevision::GetHash())
       << "\",\"core_date\":\"" << Escape(GitRevision::GetDate()) << "\",\"uptime\":" << GameTime::GetUptime()
       << ",\"max_level\":" << sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL)
       << ",\"rates\":{\"xp_kill\":" << sWorld->getRate(RATE_XP_KILL) << ",\"xp_quest\":" << sWorld->getRate(RATE_XP_QUEST)
       << ",\"drop_money\":" << sWorld->getRate(RATE_DROP_MONEY) << ",\"reputation\":" << sWorld->getRate(RATE_REPUTATION_GAIN) << "}"
       << ",\"bots\":{\"enabled\":\"" << (mgr.Enabled() ? 1 : 0) << "\",\"online\":" << mgr.Count() << "}"
       << ",\"diagnostics\":{\"core\":\"ForeverCore\",\"core_branch\":\"" << Escape(GitRevision::GetBranch()) << "\"}}";
    Send(ss.str());
}

// Deaths come from the kill hook; this catches the other ones (fall, drowning, a player) and STUCK.
void TrackBots(std::vector<Player*> const& bots)
{
    Sender& s = Get();
    TortoiseBots::BotMgr& mgr = TortoiseBots::BotMgr::Instance();
    time_t const now = GameTime::GetGameTime();
    std::unordered_map<ObjectGuid, Sender::Track> next;
    for (Player* bot : bots)
    {
        Sender::Track t;
        if (auto itr = s.track.find(bot->GetGUID()); itr != s.track.end())
            t = itr->second;
        else
            t.since = now;
        bool const alive = bot->IsAlive();
        if (t.alive && !alive)
        {
            // OnKilledByCreature already reported this death when a creature did it (it sets alive=false first).
            SendAnomaly(bot, "BOT_DEATH", "WARN", "", "died");
            QueueEvent(bot, "BotDeath", "", "");
        }
        t.alive = alive;
        if (bot->GetMapId() != t.map || std::abs(bot->GetPositionX() - t.x) > 1.0f || std::abs(bot->GetPositionY() - t.y) > 1.0f)
        {
            t.x = bot->GetPositionX(); t.y = bot->GetPositionY(); t.map = bot->GetMapId(); t.since = now;
        }
        else if (alive && now - t.since >= StuckSecs && now - t.lastStuck >= StuckRepeatSecs)
        {
            Player* master = ObjectAccessor::FindConnectedPlayer(mgr.MasterOf(bot->GetGUID()));
            if (master && master->GetMapId() == bot->GetMapId() && master->GetDistance(bot) > StuckMasterDist)
            {
                SendAnomaly(bot, "STUCK", "WARN", master->GetName(), "no movement for " + std::to_string(now - t.since) + " s, master " +
                    std::to_string(int(master->GetDistance(bot))) + " yd away");
                t.lastStuck = now;
            }
        }
        next[bot->GetGUID()] = t;
    }
    s.track.swap(next);
}

void SendCycle()
{
    Sender& s = Get();
    TortoiseBots::BotMgr& mgr = TortoiseBots::BotMgr::Instance();
    std::vector<Player*> bots;
    for (ObjectGuid const& guid : mgr.BotGuids())
        if (Player* bot = mgr.FindBot(guid); bot && bot->IsInWorld())
            bots.push_back(bot);

    TrackBots(bots);

    uint32 counts[7] = {}; // combat moving busy stalled resting dead idle
    for (Player* bot : bots)
    {
        std::string const state = StateOf(bot);
        counts[state == "combat" ? 0 : state == "moving" ? 1 : state == "dead" ? 5 : 6]++;
    }
    auto ratio = [&](uint32 n) { return bots.empty() ? 0.0 : double(n) / double(bots.size()); };

    uint32 const sessions = sWorld->GetActiveSessionCount();
    uint32 const humans = sessions > bots.size() ? uint32(sessions - bots.size()) : 0;
    uint64 const seq = ++s.seq;
    time_t const now = GameTime::GetGameTime();
    size_t const total = bots.empty() ? 0 : (bots.size() + BotsPerBatch - 1) / BotsPerBatch;

    std::ostringstream hb;
    hb << "{\"v\":" << ProtocolVersion << ",\"session\":" << s.session << ",\"seq\":" << seq << ",\"ts\":" << now
       << ",\"type\":\"HEARTBEAT\",\"uptime\":" << GameTime::GetUptime() << ",\"diff\":" << sWorldUpdateTime.GetLastUpdateTime()
       << ",\"humans\":" << humans << ",\"bots\":" << bots.size() << ",\"states\":{\"combat\":" << ratio(counts[0])
       << ",\"moving\":" << ratio(counts[1]) << ",\"busy\":0,\"stalled\":0,\"resting\":0,\"dead\":" << ratio(counts[5])
       << ",\"idle\":" << ratio(counts[6]) << "}}";
    Send(hb.str());
    // After the heartbeat: the dashboard drops a SERVER_INFO of a session its heartbeat has not opened yet.
    if (!s.serverInfoSent || s.serverInfoSinceMs >= 60000)
    {
        SendServerInfo();
        s.serverInfoSent = true;
        s.serverInfoSinceMs = 0;
    }

    for (size_t batch = 0; batch < total; ++batch)
    {
        std::ostringstream ss;
        ss << "{\"v\":" << ProtocolVersion << ",\"session\":" << s.session << ",\"seq\":" << seq << ",\"ts\":" << now
           << ",\"type\":\"BOT_BATCH\",\"batch_index\":" << batch << ",\"total_batches\":" << total << ",\"bots\":[";
        size_t const end = std::min(bots.size(), (batch + 1) * BotsPerBatch);
        for (size_t i = batch * BotsPerBatch; i < end; ++i)
        {
            Player* bot = bots[i];
            Powers const power = bot->GetPowerType();
            std::string target;
            if (Unit* unit = ObjectAccessor::GetUnit(*bot, bot->GetTarget()))
                target = unit->GetName();
            Group* group = bot->GetGroup();
            if (i != batch * BotsPerBatch)
                ss << ',';
            ss << "{\"name\":\"" << Escape(bot->GetName()) << "\",\"guid\":" << bot->GetGUID().GetCounter()
               << ",\"kind\":\"" << (mgr.MasterOf(bot->GetGUID()).IsEmpty() ? "pool" : "hired") << "\""
               << ",\"class\":\"" << ClassName(bot->GetClass()) << "\",\"role\":\"\""
               << ",\"group\":" << (group ? group->GetGUID().GetCounter() : 0)
               << ",\"level\":" << uint32(bot->GetLevel())
               << ",\"hp\":" << bot->GetHealth() << ",\"max_hp\":" << bot->GetMaxHealth()
               << ",\"power\":" << bot->GetPower(power) << ",\"max_power\":" << bot->GetMaxPower(power)
               << ",\"power_type\":\"" << PowerName(power) << "\""
               << ",\"map\":" << bot->GetMapId() << ",\"zone\":" << bot->GetZoneId() << ",\"instance\":" << bot->GetInstanceId()
               << ",\"x\":" << bot->GetPositionX() << ",\"y\":" << bot->GetPositionY() << ",\"z\":" << bot->GetPositionZ()
               << ",\"o\":" << bot->GetOrientation()
               << ",\"target\":\"" << Escape(target) << "\",\"strategy\":\"\",\"state\":\"" << StateOf(bot) << "\""
               << ",\"xp\":" << bot->GetXP() << ",\"next_xp\":" << bot->GetXPForNextLevel()
               << ",\"guild\":\"" << Escape(bot->GetGuildName()) << "\"}";
        }
        ss << "]}";
        Send(ss.str());
    }

    if (!s.events.empty())
    {
        std::ostringstream ev;
        ev << "{\"v\":" << ProtocolVersion << ",\"session\":" << s.session << ",\"seq\":" << seq << ",\"ts\":" << now
           << ",\"type\":\"BOT_EVENTS\",\"events\":[";
        for (size_t i = 0; i < s.events.size(); ++i)
            ev << (i ? "," : "") << s.events[i];
        ev << "]}";
        Send(ev.str());
        s.events.clear();
    }
}
}

namespace TortoiseBots::Dashboard
{
void LoadConfig()
{
    Sender& s = Get();
    s.enabled = sConfigMgr->GetBoolDefault("TortoiseBots.Dashboard.Enable", true);
    s.intervalMs = uint32(std::max(500, sConfigMgr->GetIntDefault("TortoiseBots.Dashboard.IntervalMs", 2000)));
    std::string const host = sConfigMgr->GetStringDefault("TortoiseBots.Dashboard.Host", "127.0.0.1");
    int32 const port = sConfigMgr->GetIntDefault("TortoiseBots.Dashboard.Port", 9195);
    s.itemsFile = sConfigMgr->GetStringDefault("TortoiseBots.Dashboard.ItemsFile", "");
    if (s.itemsFile.empty())
        s.itemsFile = sConfigMgr->GetStringDefault("LogsDir", ".") + "/tortoisebots-items.json";
    s.socket.reset();
    if (!s.enabled)
        return;

    boost::system::error_code ec;
    boost::asio::ip::address const address = boost::asio::ip::make_address(host, ec);
    if (ec)
    {
        TC_LOG_ERROR("server.loading", "TortoiseBots: dashboard host '{}' is not an IP address; the dashboard gets no data", host);
        s.enabled = false;
        return;
    }
    s.target = boost::asio::ip::udp::endpoint(address, uint16(port));
    s.socket = std::make_unique<boost::asio::ip::udp::socket>(s.io);
    s.socket->open(s.target.protocol(), ec);
    if (!ec)
        s.socket->non_blocking(true, ec);
    if (ec)
    {
        TC_LOG_ERROR("server.loading", "TortoiseBots: dashboard socket failed ({}); the dashboard gets no data", ec.message());
        s.socket.reset();
        s.enabled = false;
        return;
    }
    if (!s.session)
        s.session = uint64(time(nullptr));
    TC_LOG_INFO("server.loading", "TortoiseBots: dashboard telemetry to {}:{} every {} ms", host, port, s.intervalMs);
}

void Update(uint32 diff)
{
    Sender& s = Get();
    if (!s.enabled || !s.socket || !BotMgr::Instance().Enabled())
        return;
    s.serverInfoSinceMs += diff;
    s.sinceMs += diff;
    if (s.sinceMs < s.intervalMs)
        return;
    s.sinceMs = 0;
    SendCycle();
}

void WriteItemsFile()
{
    Sender& s = Get();
    if (!s.enabled || s.itemsFile.empty())
        return;
    std::string const tmp = s.itemsFile + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out)
    {
        TC_LOG_ERROR("server.loading", "TortoiseBots: cannot write the dashboard item file {}", tmp);
        return;
    }
    out << "{\"items\":[";
    size_t n = 0;
    for (auto const& [id, item] : sObjectMgr->GetItemTemplateStore())
    {
        out << (n++ ? "," : "") << "{\"id\":" << id << ",\"name\":\"" << Escape(item.GetDefaultLocaleName()) << "\",\"quality\":" << item.GetQuality()
            << ",\"ilvl\":" << item.GetBaseItemLevel() << ",\"req_level\":" << item.GetBaseRequiredLevel() << ",\"class\":" << item.GetClass()
            << ",\"subclass\":" << item.GetSubClass() << ",\"inv_type\":" << uint32(item.GetInventoryType()) << ",\"sell\":" << item.GetSellPrice()
            << ",\"buy\":" << item.GetBuyPrice() << "}";
    }
    out << "]}";
    out.close();
    std::rename(tmp.c_str(), s.itemsFile.c_str());
    TC_LOG_INFO("server.loading", "TortoiseBots: dashboard item file {} ({} items)", s.itemsFile, n);
}

void OnKill(Player* killer, Creature* killed)
{
    if (IsBot(killer) && killed)
        QueueEvent(killer, "Kill", killed->GetName(), std::to_string(killed->GetEntry()));
}

void OnKilledByCreature(Creature* killer, Player* killed)
{
    if (!IsBot(killed) || !Get().enabled)
        return;
    std::string const name = killer ? killer->GetName() : "";
    SendAnomaly(killed, "BOT_DEATH", "WARN", name, killer ? "killed by " + name + " (level " + std::to_string(killer->GetLevel()) + ")" : "died");
    QueueEvent(killed, "BotDeath", name, "");
    if (auto itr = Get().track.find(killed->GetGUID()); itr != Get().track.end())
        itr->second.alive = false;
}

void OnQuestStatusChange(Player* player, uint32 questId)
{
    if (!IsBot(player))
        return;
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    std::string const title = quest ? quest->GetLogTitle() : "";
    switch (player->GetQuestStatus(questId))
    {
        case QUEST_STATUS_INCOMPLETE: QueueEvent(player, "AcceptQuestAction", title, std::to_string(questId)); break;
        case QUEST_STATUS_COMPLETE: QueueEvent(player, "QuestCompleted", title, std::to_string(questId)); break;
        case QUEST_STATUS_REWARDED: QueueEvent(player, "QuestRewarded", title, std::to_string(questId)); break;
        case QUEST_STATUS_NONE: QueueEvent(player, "QuestDropped", title, std::to_string(questId)); break;
        default: break;
    }
}
}
