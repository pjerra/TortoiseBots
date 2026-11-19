#include "TbDashboard.h"
#include "TbBotMgr.h"
#include "Config.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "TransmogMgr.h"
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
    std::string uiMapsFile;
    uint32 serverInfoSinceMs = 0;
    uint32 statsSinceMs = 0;
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

// The bot on the client's own world map: the UiMap the core assigns to its position and where on that map's art it stands (0..100 %).
// ui_map/ui_x/ui_y = the zone map (as the client's map opens on), ui_cont/ui_cx/ui_cy = its continent map (the core's global
// position: the zone point carried up to the continent's art); either may be absent.
std::string UiMapFields(Player* bot)
{
    std::ostringstream ss;
    uint32 uiMapId = 0;
    DBCPosition2D pos{ 0.0f, 0.0f };
    if (!DB2Manager::GetUiMapPosition(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), -1, int32(bot->GetZoneId()), 0, 0,
            UI_MAP_SYSTEM_WORLD, true, &uiMapId, &pos) || int32(uiMapId) <= 0)
        return "";
    ss << ",\"ui_map\":" << uiMapId << ",\"ui_x\":" << pos.X * 100.0f << ",\"ui_y\":" << pos.Y * 100.0f;

    UiMapEntry const* cont = sUiMapStore.LookupEntry(uiMapId);
    while (cont && cont->Type > UI_MAP_TYPE_CONTINENT)
        cont = sUiMapStore.LookupEntry(cont->ParentUiMapID);
    if (cont && cont->Type == UI_MAP_TYPE_CONTINENT && DB2Manager::GetUiMapPosition(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), -1,
            int32(bot->GetZoneId()), 0, 0, UI_MAP_SYSTEM_WORLD, false, nullptr, &pos))
        ss << ",\"ui_cont\":" << cont->ID << ",\"ui_cx\":" << pos.X * 100.0f << ",\"ui_cy\":" << pos.Y * 100.0f;
    return ss.str();
}

// The core keeps rage (and runic power) times ten; the client shows a tenth.
int32 ShownPower(Player* bot, Powers power, int32 value)
{
    return power == POWER_RAGE || power == POWER_RUNIC_POWER ? value / 10 : value;
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

// BOT_STATS: the live character sheet of every bot, the values the core itself computes for this client version (the profile's
// stats; character_stats in the database is only written at logout on a default realm). Field names = the dashboard's CharacterStats.
constexpr size_t StatsPerDatagram = 20;
constexpr uint32 StatsIntervalMs = 10000;

void AppendStats(std::ostringstream& ss, Player* bot)
{
    auto const& ap = bot->m_activePlayerData;
    auto const& ud = bot->m_unitData;
    auto dmg = [](float lo, float hi) { std::ostringstream d; d << uint32(lo) << " - " << uint32(hi); return d.str(); };
    float const speed = ud->ModCastingSpeed;
    ss << "{\"guid\":" << bot->GetGUID().GetCounter() << ",\"source\":\"live\",\"maxhealth\":" << bot->GetMaxHealth();
    // maxpower1..5 = mana, rage, focus, energy, happiness (the dashboard's 1.12 order); ForeverCore has no happiness.
    Powers const order[4] = { POWER_MANA, POWER_RAGE, POWER_FOCUS, POWER_ENERGY };
    for (uint8 i = 0; i < 4; ++i)
        ss << ",\"maxpower" << uint32(i + 1) << "\":" << ShownPower(bot, order[i], bot->GetMaxPower(order[i]));
    ss << ",\"maxpower5\":0";
    ss << ",\"strength\":" << bot->GetStat(STAT_STRENGTH) << ",\"agility\":" << bot->GetStat(STAT_AGILITY) << ",\"stamina\":" << bot->GetStat(STAT_STAMINA)
       << ",\"intellect\":" << bot->GetStat(STAT_INTELLECT) << ",\"spirit\":" << bot->GetStat(STAT_SPIRIT) << ",\"armor\":" << bot->GetArmor()
       << ",\"res_holy\":" << bot->GetResistance(SPELL_SCHOOL_HOLY) << ",\"res_fire\":" << bot->GetResistance(SPELL_SCHOOL_FIRE)
       << ",\"res_nature\":" << bot->GetResistance(SPELL_SCHOOL_NATURE) << ",\"res_frost\":" << bot->GetResistance(SPELL_SCHOOL_FROST)
       << ",\"res_shadow\":" << bot->GetResistance(SPELL_SCHOOL_SHADOW) << ",\"res_arcane\":" << bot->GetResistance(SPELL_SCHOOL_ARCANE)
       << ",\"block_pct\":" << float(ap->BlockPercentage) << ",\"dodge_pct\":" << float(ap->DodgePercentage) << ",\"parry_pct\":" << float(ap->ParryPercentage)
       << ",\"melee_crit_pct\":" << float(ap->CritPercentage) << ",\"ranged_crit_pct\":" << float(ap->RangedCritPercentage)
       << ",\"spell_crit_pct\":" << float(ap->SpellCritPercentage)
       << ",\"attack_power\":" << bot->GetTotalAttackPowerValue(BASE_ATTACK) << ",\"ranged_attack_power\":" << bot->GetTotalAttackPowerValue(RANGED_ATTACK)
       << ",\"melee_damage\":\"" << dmg(ud->MinDamage, ud->MaxDamage) << "\",\"ranged_damage\":\"" << dmg(ud->MinRangedDamage, ud->MaxRangedDamage) << "\""
       << ",\"melee_speed\":" << ud->AttackRoundBaseTime[0] / 1000.0f << ",\"ranged_speed\":" << ud->RangedAttackRoundBaseTime / 1000.0f
       << ",\"cast_speed\":" << (speed > 0.0f ? (1.0f / speed - 1.0f) * 100.0f : 0.0f)
       << ",\"melee_hit\":" << bot->m_modMeleeHitChance << ",\"ranged_hit\":" << bot->m_modRangedHitChance << ",\"spell_hit\":" << bot->m_modSpellHitChance;
    int32 minSchool = 0;
    for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
        minSchool = school == SPELL_SCHOOL_HOLY ? ap->ModDamageDonePos[school] : std::min<int32>(minSchool, ap->ModDamageDonePos[school]);
    // spell_dmg_<school> = what that school has above the base (the page shows base + that); spell_damage = the base (lowest school).
    int32 const base = std::max(0, minSchool);
    auto above = [&](SpellSchools school) { return std::max<int32>(0, ap->ModDamageDonePos[school] - base); };
    ss << ",\"spell_damage\":" << base << ",\"healing_power\":" << std::max<int32>(0, ap->ModHealingDonePos)
       << ",\"spell_dmg_holy\":" << above(SPELL_SCHOOL_HOLY) << ",\"spell_dmg_fire\":" << above(SPELL_SCHOOL_FIRE)
       << ",\"spell_dmg_nature\":" << above(SPELL_SCHOOL_NATURE) << ",\"spell_dmg_frost\":" << above(SPELL_SCHOOL_FROST)
       << ",\"spell_dmg_shadow\":" << above(SPELL_SCHOOL_SHADOW) << ",\"spell_dmg_arcane\":" << above(SPELL_SCHOOL_ARCANE)
       << ",\"mana_regen\":" << uint32(std::max(0.0f, float(ud->PowerRegenFlatModifier[0])) * 5.0f)
       << ",\"mana_regen_casting\":" << uint32(std::max(0.0f, float(ud->PowerRegenInterruptedFlatModifier[0])) * 5.0f)
       // The Classic character sheet's other lines: defense (skill and the level's maximum), shield block value, spell penetration, expertise.
       << ",\"defense\":" << bot->GetSkillValue(SKILL_DEFENSE) << ",\"defense_max\":" << bot->GetMaxSkillValueForLevel()
       << ",\"block_value\":" << int32(ap->ShieldBlock) << ",\"spell_penetration\":" << -int32(ap->ModTargetResistance)
       << ",\"expertise\":" << float(ap->MainhandExpertise) << ",\"ratings\":{";
    bool first = true;
    for (uint32 cr = 0; cr < MAX_COMBAT_RATING; ++cr)
        if (int32 const value = ap->CombatRatings[cr])
        {
            ss << (first ? "" : ",") << "\"" << cr << "\":" << value;
            first = false;
        }
    // What each rating is worth, as the core converts it (percent, or skill points for defense and expertise).
    ss << "},\"rating_bonus\":{";
    first = true;
    for (uint32 cr = 0; cr < MAX_COMBAT_RATING; ++cr)
        if (ap->CombatRatings[cr])
        {
            ss << (first ? "" : ",") << "\"" << cr << "\":" << bot->GetRatingBonusValue(CombatRating(cr));
            first = false;
        }
    ss << "}}";
}

void SendStats(std::vector<Player*> const& bots)
{
    Sender& s = Get();
    for (size_t first = 0; first < bots.size(); first += StatsPerDatagram)
    {
        std::ostringstream ss;
        ss << "{\"v\":" << ProtocolVersion << ",\"session\":" << s.session << ",\"seq\":" << s.seq << ",\"ts\":" << GameTime::GetGameTime()
           << ",\"type\":\"BOT_STATS\",\"stats\":[";
        for (size_t i = first; i < std::min(bots.size(), first + StatsPerDatagram); ++i)
        {
            if (i != first)
                ss << ',';
            AppendStats(ss, bots[i]);
        }
        ss << "]}";
        Send(ss.str());
    }
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
               << ",\"power\":" << ShownPower(bot, power, bot->GetPower(power)) << ",\"max_power\":" << ShownPower(bot, power, bot->GetMaxPower(power))
               << ",\"power_type\":\"" << PowerName(power) << "\""
               << ",\"map\":" << bot->GetMapId() << ",\"zone\":" << bot->GetZoneId() << ",\"instance\":" << bot->GetInstanceId()
               << ",\"x\":" << bot->GetPositionX() << ",\"y\":" << bot->GetPositionY() << ",\"z\":" << bot->GetPositionZ()
               << ",\"o\":" << bot->GetOrientation()
               << ",\"target\":\"" << Escape(target) << "\",\"strategy\":\"\",\"state\":\"" << StateOf(bot) << "\""
               << ",\"xp\":" << bot->GetXP() << ",\"next_xp\":" << bot->GetXPForNextLevel()
               << ",\"guild\":\"" << Escape(bot->GetGuildName()) << "\"" << UiMapFields(bot) << "}";
        }
        ss << "]}";
        Send(ss.str());
    }

    s.statsSinceMs += s.intervalMs;
    if (s.statsSinceMs >= StatsIntervalMs)
    {
        s.statsSinceMs = 0;
        SendStats(bots);
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
    s.uiMapsFile = sConfigMgr->GetStringDefault("TortoiseBots.Dashboard.UiMapsFile", "");
    if (s.uiMapsFile.empty())
        s.uiMapsFile = sConfigMgr->GetStringDefault("LogsDir", ".") + "/tortoisebots-uimaps.json";
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

uint32 IconOf(uint32 itemId)
{
    if (ItemModifiedAppearanceEntry const* modified = TransmogMgr::GetDefaultItemModifiedAppearance(itemId))
        if (ItemAppearanceEntry const* appearance = sItemAppearanceStore.LookupEntry(modified->ItemAppearanceID))
            if (appearance->DefaultIconFileDataID > 0)
                return uint32(appearance->DefaultIconFileDataID);
    // Items that are not worn (food, potions, recipes, quest items) carry their icon on the item record itself.
    if (ItemEntry const* item = sItemStore.LookupEntry(itemId))
        return uint32(std::max(0, item->IconFileDataID));
    return 0;
}

void WriteUiMapsFile()
{
    Sender& s = Get();
    std::string const tmp = s.uiMapsFile + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out)
        return;
    out << "{\"maps\":[";
    size_t n = 0;
    for (UiMapEntry const* map : sUiMapStore)
        out << (n++ ? "," : "") << "{\"id\":" << map->ID << ",\"name\":\"" << Escape(map->Name[sWorld->GetDefaultDbcLocale()]) << "\",\"parent\":"
            << map->ParentUiMapID << ",\"type\":" << int32(map->Type) << ",\"system\":" << int32(map->System) << "}";
    out << "]}";
    out.close();
    std::rename(tmp.c_str(), s.uiMapsFile.c_str());
    TC_LOG_INFO("server.loading", "TortoiseBots: dashboard map file {} ({} maps)", s.uiMapsFile, n);
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
            << ",\"buy\":" << item.GetBuyPrice() << ",\"icon\":" << IconOf(id) << "}";
    }
    out << "]}";
    out.close();
    std::rename(tmp.c_str(), s.itemsFile.c_str());
    TC_LOG_INFO("server.loading", "TortoiseBots: dashboard item file {} ({} items)", s.itemsFile, n);
    WriteUiMapsFile();
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
