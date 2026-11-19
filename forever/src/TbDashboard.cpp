#include "TbDashboard.h"
#include "TbBotMgr.h"
#include "Config.h"
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
};

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

void Send(std::string const& datagram)
{
    Sender& s = Get();
    boost::system::error_code ec;
    s.socket->send_to(boost::asio::buffer(datagram), s.target, 0, ec);
}

void SendCycle()
{
    Sender& s = Get();
    TortoiseBots::BotMgr& mgr = TortoiseBots::BotMgr::Instance();
    std::vector<Player*> bots;
    for (ObjectGuid const& guid : mgr.BotGuids())
        if (Player* bot = mgr.FindBot(guid); bot && bot->IsInWorld())
            bots.push_back(bot);

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
               << ",\"target\":\"" << Escape(target) << "\",\"strategy\":\"\",\"state\":\"" << StateOf(bot) << "\"}";
        }
        ss << "]}";
        Send(ss.str());
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
    s.sinceMs += diff;
    if (s.sinceMs < s.intervalMs)
        return;
    s.sinceMs = 0;
    SendCycle();
}
}
