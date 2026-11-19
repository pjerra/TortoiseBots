#include "TbDashboard.h"
#include "TbBotMgr.h"
#include "Config.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "Item.h"
#include "MotionMaster.h"
#include "GameTables.h"
#include "ItemEnchantmentMgr.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
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
#include <optional>
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
    struct BrainInfo { std::string role, combat, nonCombat, engine, state; };
    std::unordered_map<ObjectGuid, BrainInfo> brains;   // refreshed with BOT_STATS, read by every roster row
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

char const* MoveName(MovementGeneratorType type)
{
    switch (type)
    {
        case IDLE_MOTION_TYPE: return "idle";
        case RANDOM_MOTION_TYPE: return "random";
        case WAYPOINT_MOTION_TYPE: return "waypoint";
        case CONFUSED_MOTION_TYPE: return "confused";
        case CHASE_MOTION_TYPE: return "chase";
        case HOME_MOTION_TYPE: return "home";
        case FLIGHT_MOTION_TYPE: return "flight";
        case POINT_MOTION_TYPE: return "point";
        case FLEEING_MOTION_TYPE: case TIMED_FLEEING_MOTION_TYPE: return "flee";
        case FOLLOW_MOTION_TYPE: return "follow";
        default: return "other";
    }
}

// The rows a bot driver needs (master and distance, movement and how long it has not moved, life, bags and repairs).
std::string DriverFields(Player* bot, time_t stillSince, time_t now)
{
    std::ostringstream ss;
    ObjectGuid const masterGuid = TortoiseBots::BotMgr::Instance().MasterOf(bot->GetGUID());
    ss << ",\"master\":" << masterGuid.GetCounter();
    if (Player* master = ObjectAccessor::FindConnectedPlayer(masterGuid); master && master->IsInWorld() && master->GetMap() == bot->GetMap())
        ss << ",\"dist_master\":" << bot->GetDistance(master) << ",\"los_master\":" << (bot->IsWithinLOSInMap(master) ? 1 : 0);
    ss << ",\"move\":\"" << MoveName(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()) << "\",\"still_s\":" << (now - stillSince)
       << ",\"ghost\":" << (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? 1 : 0) << ",\"rez_pending\":" << (bot->IsResurrectRequested() ? 1 : 0)
       << ",\"free_slots\":" << bot->GetFreeInventorySlotCount();
    uint32 lowest = 100;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (uint32 const max = item->m_itemData->MaxDurability)
                lowest = std::min<uint32>(lowest, uint32(item->m_itemData->Durability) * 100 / max);
    ss << ",\"durability_min_pct\":" << lowest;
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

void QueueEvent(Player* bot, char const* event, std::string const& info1, std::string const& info2, std::string const& extra = "")
{
    Sender& s = Get();
    if (!s.enabled || s.events.size() >= 500)
        return;
    std::ostringstream ev;
    ev << "{\"event\":\"" << event << "\",\"info1\":\"" << Escape(info1) << "\",\"info2\":\"" << Escape(info2) << "\",\"bot\":\""
       << Escape(bot->GetName()) << "\",\"guid\":" << bot->GetGUID().GetCounter() << ",\"class\":\"" << ClassName(bot->GetClass())
       << "\",\"level\":" << uint32(bot->GetLevel()) << ",\"map\":" << bot->GetMapId() << ",\"zone\":" << bot->GetZoneId()
       << ",\"money\":" << bot->GetMoney() << extra << "}";
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
       << ",\"drop_money\":" << sWorld->getRate(RATE_DROP_MONEY) << ",\"reputation\":" << sWorld->getRate(RATE_REPUTATION_GAIN)
       << ",\"xp_explore\":" << sWorld->getRate(RATE_XP_EXPLORE) << ",\"xp_bg_kill\":" << sWorld->getRate(RATE_XP_BG_KILL) << ",\"honor\":" << sWorld->getRate(RATE_HONOR)
       << ",\"drop_item_normal\":" << sWorld->getRate(RATE_DROP_ITEM_NORMAL) << ",\"drop_item_rare\":" << sWorld->getRate(RATE_DROP_ITEM_RARE)
       << ",\"drop_item_epic\":" << sWorld->getRate(RATE_DROP_ITEM_EPIC) << ",\"rest_ingame\":" << sWorld->getRate(RATE_REST_INGAME)
       << ",\"skill_discovery\":" << sWorld->getRate(RATE_SKILL_DISCOVERY) << "}"
       << ",\"bots\":{\"enabled\":\"" << (mgr.Enabled() ? 1 : 0) << "\",\"online\":" << mgr.Count()
       << ",\"engine_by_default\":\"" << (sConfigMgr->GetBoolDefault("TortoiseBots.Slice.EngineByDefault", false) ? 1 : 0)
       << "\",\"master_grace_s\":" << mgr.MasterGraceSeconds() << ",\"dead_backstop_s\":" << sConfigMgr->GetIntDefault("TortoiseBots.Slice.DeadBackstopSeconds", 180)
       << ",\"dashboard_interval_ms\":" << s.intervalMs << "}"
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
    for (auto itr = s.brains.begin(); itr != s.brains.end();)
        itr = s.track.count(itr->first) ? std::next(itr) : s.brains.erase(itr);
}

// BOT_STATS: the live character sheet of every bot, the values the core itself computes for this client version (the profile's
// stats; character_stats in the database is only written at logout on a default realm). Field names = the dashboard's CharacterStats.
constexpr size_t StatsPerDatagram = 20;
constexpr uint32 StatsIntervalMs = 10000;

// The engine's view of the bot, from the lines `.tb where` prints (BotMgr::DescribeBrain): engine on/off, its state and its
// combat / non-combat strategies; the role is read off the strategies (tank / heal).
Sender::BrainInfo ReadBrain(Player* bot)
{
    Sender::BrainInfo info;
    std::vector<std::string> const lines = TortoiseBots::BotMgr::Instance().DescribeBrain(bot->GetGUID());
    // First line: "brain: engine" when the engine runs, else "brain: spike (why the engine is not running)".
    std::string const first = lines.empty() ? "" : lines.front();
    info.engine = first.compare(0, 13, "brain: engine") == 0 ? "on" : "off";
    if (info.engine == "off" && first.size() > 7)
        info.state = first.substr(7);
    auto after = [](std::string const& line, char const* prefix) -> std::optional<std::string>
    {
        std::string const p = prefix;
        if (line.compare(0, p.size(), p) != 0)
            return std::nullopt;
        return line.substr(p.size());
    };
    for (std::string const& line : lines)
    {
        if (auto v = after(line, "state: "))
            info.state = *v;
        else if (auto v = after(line, "combat strategies: "))
            info.combat = *v;
        else if (auto v = after(line, "non-combat strategies: "))
            info.nonCombat = *v;
    }
    // Whole strategy names only ("heal interrupt" is not the heal strategy).
    auto has = [&](char const* name) { return (", " + info.combat + ",").find(std::string(", ") + name + ",") != std::string::npos; };
    // The engine's role strategies (AiFactory): tanks get "tank assist"/"tank face"/"tank feral", healers "holy"/"discipline"/"restoration".
    info.role = info.engine != "on" ? "" : has("tank assist") || has("tank face") || has("tank feral") ? "tank"
        : has("holy") || has("discipline") || has("restoration") ? "healer" : "dps";
    return info;
}

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
    // The auras on the bot now: spell, remaining ms (-1 permanent), stacks, positive.
    ss << "},\"auras\":[";
    first = true;
    size_t shown = 0;
    for (auto const& [spellId, app] : bot->GetAppliedAuras())
    {
        if (++shown > 40)
            break;
        Aura const* aura = app->GetBase();
        ss << (first ? "" : ",") << "[" << spellId << "," << aura->GetDuration() << "," << uint32(aura->GetStackAmount()) << "," << (app->IsPositive() ? 1 : 0) << "]";
        first = false;
    }
    ss << "]}";
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
            s.brains[bots[i]->GetGUID()] = ReadBrain(bots[i]);
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
            Sender::BrainInfo const& brain = s.brains[bot->GetGUID()];
            if (i != batch * BotsPerBatch)
                ss << ',';
            ss << "{\"name\":\"" << Escape(bot->GetName()) << "\",\"guid\":" << bot->GetGUID().GetCounter()
               << ",\"kind\":\"" << (mgr.MasterOf(bot->GetGUID()).IsEmpty() ? "pool" : "hired") << "\""
               << ",\"class\":\"" << ClassName(bot->GetClass()) << "\",\"role\":\"" << Escape(brain.role) << "\""
               << ",\"group\":" << (group ? group->GetGUID().GetCounter() : 0)
               << ",\"level\":" << uint32(bot->GetLevel())
               << ",\"hp\":" << bot->GetHealth() << ",\"max_hp\":" << bot->GetMaxHealth()
               << ",\"power\":" << ShownPower(bot, power, bot->GetPower(power)) << ",\"max_power\":" << ShownPower(bot, power, bot->GetMaxPower(power))
               << ",\"power_type\":\"" << PowerName(power) << "\""
               << ",\"map\":" << bot->GetMapId() << ",\"zone\":" << bot->GetZoneId() << ",\"instance\":" << bot->GetInstanceId()
               << ",\"x\":" << bot->GetPositionX() << ",\"y\":" << bot->GetPositionY() << ",\"z\":" << bot->GetPositionZ()
               << ",\"o\":" << bot->GetOrientation()
               << ",\"target\":\"" << Escape(target) << "\",\"strategy\":\"" << Escape(brain.combat) << "\",\"strategies_nc\":\"" << Escape(brain.nonCombat)
               << "\",\"engine\":\"" << brain.engine << "\",\"engine_state\":\"" << Escape(brain.state) << "\",\"state\":\"" << StateOf(bot) << "\""
               << ",\"xp\":" << bot->GetXP() << ",\"next_xp\":" << bot->GetXPForNextLevel()
               << ",\"guild\":\"" << Escape(bot->GetGuildName()) << "\"" << UiMapFields(bot)
               << DriverFields(bot, s.track.count(bot->GetGUID()) ? s.track[bot->GetGUID()].since : now, now) << "}";
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

std::string LocName(LocalizedString const& str)
{
    char const* text = str[sWorld->GetDefaultDbcLocale()];
    return Escape(text ? text : "");
}

// What an item tooltip shows: stats (this version keeps them as a share of the item level's budget; the value is the core's
// Item::GetItemStatValue formula for the item as it drops), armour, weapon damage and speed, binding, set, durability,
// required skill and classes, description and the spells it casts (trigger: 0 use, 1 equip, 2 chance on hit).
std::string ItemDetails(ItemTemplate const& item)
{
    std::ostringstream ss;
    BonusData bonus;
    bonus.Initialize(&item);
    uint32 const level = item.GetBaseItemLevel();
    float const points = GetRandomPropertyPoints(level, item.GetQuality(), item.GetInventoryType(), item.GetSubClass());
    GtItemSocketCostPerLevelEntry const* socketCost = sItemSocketCostPerLevelGameTable.GetRow(level);
    ss << ",\"stats\":[";
    bool first = true;
    for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        int32 const type = bonus.ItemStatType[i];
        if (type < 0 || !bonus.StatPercentEditor[i])
            continue;
        float value = type == ITEM_MOD_CORRUPTION || type == ITEM_MOD_CORRUPTION_RESISTANCE ? float(bonus.StatPercentEditor[i])
            : float(bonus.StatPercentEditor[i]) * points * 0.0001f;
        if (socketCost)
            value -= float(bonus.ItemStatSocketCostMultiplier[i] * socketCost->SocketCost);
        int32 const shown = int32(std::lround(value));
        if (!shown)
            continue;
        ss << (first ? "" : ",") << "[" << type << "," << shown << "]";
        first = false;
    }
    ss << "]";
    if (uint32 armor = item.GetArmor(level))
        ss << ",\"armor\":" << armor;
    // ItemTemplate::GetDamage asserts a row for the item level in every weapon damage store; some items have levels past them.
    bool const damageRows = sItemDamageAmmoStore.LookupEntry(level) && sItemDamageOneHandStore.LookupEntry(level) && sItemDamageTwoHandStore.LookupEntry(level)
        && sItemDamageOneHandCasterStore.LookupEntry(level) && sItemDamageTwoHandCasterStore.LookupEntry(level);
    if (item.GetClass() == ITEM_CLASS_WEAPON && damageRows)
    {
        float lo = 0.0f, hi = 0.0f;
        item.GetDamage(level, lo, hi);
        ss << ",\"dmg_min\":" << uint32(lo) << ",\"dmg_max\":" << uint32(hi) << ",\"delay\":" << item.GetDelay();
    }
    ss << ",\"bonding\":" << uint32(item.GetBonding()) << ",\"set\":" << item.GetItemSet() << ",\"durability\":" << item.MaxDurability
       << ",\"req_skill\":" << item.GetRequiredSkill() << ",\"req_skill_rank\":" << item.GetRequiredSkillRank()
       << ",\"classes\":" << item.GetAllowableClass() << ",\"desc\":\"" << LocName(item.ExtendedData->Description) << "\",\"spells\":[";
    first = true;
    for (ItemEffectEntry const* effect : item.Effects)
        if (effect->SpellID > 0)
        {
            ss << (first ? "" : ",") << "[" << effect->SpellID << "," << uint32(effect->TriggerType) << "]";
            first = false;
        }
    ss << "]";
    return ss.str();
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

std::string DataFilePath(char const* key, char const* name)
{
    std::string path = sConfigMgr->GetStringDefault(key, "");
    return path.empty() ? sConfigMgr->GetStringDefault("LogsDir", ".") + "/" + name : path;
}

bool WriteJsonFile(std::string const& path, std::string const& body, char const* what, size_t count)
{
    std::string const tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out)
    {
        TC_LOG_ERROR("server.loading", "TortoiseBots: cannot write the dashboard {} file {}", what, tmp);
        return false;
    }
    out << body;
    out.close();
    std::rename(tmp.c_str(), path.c_str());
    TC_LOG_INFO("server.loading", "TortoiseBots: dashboard {} file {} ({} rows)", what, path, count);
    return true;
}

// Every spell with a name: what a spell tooltip and the bot's spell book need, from the core's own SpellInfo.
void WriteSpellsFile()
{
    std::ostringstream ss;
    ss << "{\"spells\":[";
    size_t n = 0;
    for (SpellNameEntry const* name : sSpellNameStore)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(name->ID, DIFFICULTY_NONE);
        if (!info)
        {
            // A spell the core does not load (e.g. a talent of the old trees): its name still labels the talent.
            ss << (n++ ? "," : "") << "{\"id\":" << name->ID << ",\"name\":\"" << LocName(name->Name) << "\"}";
            continue;
        }
        ss << (n++ ? "," : "") << "{\"id\":" << info->Id << ",\"name\":\"" << LocName(name->Name) << "\",\"icon\":" << info->IconFileDataId
           << ",\"school\":" << info->SchoolMask << ",\"cast_ms\":" << info->CalcCastTime() << ",\"range\":" << info->GetMaxRange()
           << ",\"duration_ms\":" << info->GetDuration() << ",\"cooldown_ms\":" << info->RecoveryTime << ",\"level\":" << info->SpellLevel
           << ",\"base_level\":" << info->BaseLevel;
        for (SpellPowerEntry const* power : info->PowerCosts)
            if (power && (power->ManaCost || power->PowerCostPct > 0.0f))
            {
                ss << ",\"power_type\":" << int32(power->PowerType) << ",\"cost\":" << power->ManaCost << ",\"cost_pct\":" << power->PowerCostPct;
                break;
            }
        ss << "}";
    }
    ss << "]}";
    WriteJsonFile(DataFilePath("TortoiseBots.Dashboard.SpellsFile", "tortoisebots-spells.json"), ss.str(), "spell", n);
}

// The talent grid of every class (tab, tier, column, the spell of each rank, prerequisites).
void WriteTalentsFile()
{
    std::ostringstream ss;
    ss << "{\"talents\":[";
    size_t n = 0;
    for (TalentEntry const* t : sTalentStore)
    {
        ss << (n++ ? "," : "") << "{\"id\":" << t->ID << ",\"class\":" << int32(t->ClassID) << ",\"tab\":" << t->TabID << ",\"tier\":" << uint32(t->TierID)
           << ",\"col\":" << uint32(t->ColumnIndex) << ",\"spell\":" << t->SpellID << ",\"ranks\":[";
        bool first = true;
        for (uint32 rank : t->SpellRank)
            if (rank)
            {
                ss << (first ? "" : ",") << rank;
                first = false;
            }
        ss << "],\"prereq\":[";
        first = true;
        for (size_t i = 0; i < t->PrereqTalent.size(); ++i)
            if (t->PrereqTalent[i])
            {
                ss << (first ? "" : ",") << "[" << t->PrereqTalent[i] << "," << uint32(t->PrereqRank[i]) << "]";
                first = false;
            }
        ss << "]}";
    }
    ss << "]}";
    WriteJsonFile(DataFilePath("TortoiseBots.Dashboard.TalentsFile", "tortoisebots-talents.json"), ss.str(), "talent", n);
}

// Names the page shows by id: skills (professions, weapons), areas (zones), factions (reputation).
void WriteNamesFile()
{
    std::ostringstream ss;
    size_t n = 0;
    ss << "{\"skills\":{";
    bool first = true;
    for (SkillLineEntry const* skill : sSkillLineStore)
    {
        ss << (first ? "" : ",") << "\"" << skill->ID << "\":{\"name\":\"" << LocName(skill->DisplayName) << "\",\"category\":" << int32(skill->CategoryID)
           << ",\"icon\":" << skill->SpellIconFileID << "}";
        first = false;
        ++n;
    }
    ss << "},\"areas\":{";
    first = true;
    for (AreaTableEntry const* area : sAreaTableStore)
    {
        ss << (first ? "" : ",") << "\"" << area->ID << "\":{\"name\":\"" << LocName(area->AreaName) << "\",\"map\":" << area->ContinentID
           << ",\"parent\":" << area->ParentAreaID << "}";
        first = false;
        ++n;
    }
    ss << "},\"factions\":{";
    first = true;
    for (FactionEntry const* faction : sFactionStore)
    {
        ss << (first ? "" : ",") << "\"" << faction->ID << "\":{\"name\":\"" << LocName(faction->Name) << "\",\"parent\":" << faction->ParentFactionID << "}";
        first = false;
        ++n;
    }
    ss << "},\"item_sets\":{";
    first = true;
    std::unordered_map<uint32, std::vector<std::pair<uint32, uint32>>> setBonuses;
    for (ItemSetSpellEntry const* bonus : sItemSetSpellStore)
        setBonuses[bonus->ItemSetID].emplace_back(bonus->Threshold, bonus->SpellID);
    for (ItemSetEntry const* set : sItemSetStore)
    {
        ss << (first ? "" : ",") << "\"" << set->ID << "\":{\"name\":\"" << LocName(set->Name) << "\",\"items\":[";
        bool firstItem = true;
        for (uint32 item : set->ItemID)
            if (item)
            {
                ss << (firstItem ? "" : ",") << item;
                firstItem = false;
            }
        ss << "],\"bonuses\":[";
        bool firstBonus = true;
        for (auto const& [pieces, spell] : setBonuses[set->ID])
        {
            ss << (firstBonus ? "" : ",") << "[" << pieces << "," << spell << "]";
            firstBonus = false;
        }
        ss << "]}";
        first = false;
        ++n;
    }
    ss << "},\"enchants\":{";
    first = true;
    for (SpellItemEnchantmentEntry const* enchant : sSpellItemEnchantmentStore)
    {
        ss << (first ? "" : ",") << "\"" << enchant->ID << "\":\"" << LocName(enchant->Name) << "\"";
        first = false;
        ++n;
    }
    ss << "}}";
    WriteJsonFile(DataFilePath("TortoiseBots.Dashboard.NamesFile", "tortoisebots-names.json"), ss.str(), "names", n);
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
            << ",\"buy\":" << item.GetBuyPrice() << ",\"icon\":" << IconOf(id) << ItemDetails(item) << "}";
    }
    out << "]}";
    out.close();
    std::rename(tmp.c_str(), s.itemsFile.c_str());
    TC_LOG_INFO("server.loading", "TortoiseBots: dashboard item file {} ({} items)", s.itemsFile, n);
    WriteUiMapsFile();
    WriteSpellsFile();
    WriteTalentsFile();
    WriteNamesFile();
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

// A looted item stored in the bot's bags: the loot feed and the loot counters (item, quality, vendor value of the stack).
void OnLootItem(Player* player, Item* item)
{
    if (!IsBot(player) || !item)
        return;
    ItemTemplate const* proto = item->GetTemplate();
    std::ostringstream extra;
    extra << ",\"item_id\":" << item->GetEntry() << ",\"quality\":" << proto->GetQuality() << ",\"sell\":" << proto->GetSellPrice() * item->GetCount()
          << ",\"buy\":" << proto->GetBuyPrice();
    QueueEvent(player, "StoreLootAction", proto->GetDefaultLocaleName(), std::to_string(item->GetEntry()), extra.str());
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
