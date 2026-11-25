#include "TbDashboard.h"
#include "TbBotMgr.h"
#include "dash/TbDashEndpoint.h"
#include "dash/TbDashboardStatePure.h"
#include "host/TbEngineBrain.h"
#include "host/TbTalents.h"
#include "host/TbTalentsPure.h"
#include "host/pool/TbPoolRegistry.h"
#include "ai/playerbot/policy/world/PoolCompositionPolicy.h"
#include "CharacterCache.h"
#include "Config.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Item.h"
#include "Map.h"
#include "Spell.h"
#include "MotionMaster.h"
#include "GameTables.h"
#include "ItemEnchantmentMgr.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TransmogMgr.h"
#include "TraitPacketsCommon.h"
#include "TraitMgr.h"
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
#include "runtime/PlayerLagWindow.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <list>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <vector>

namespace TortoiseBots::Dashboard
{
bool WriteJsonFile(std::string const& path, std::string const& body, char const* what, size_t count);
}

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
    std::string traceDir; // where the dungeon run summaries go (the dashboard's -trace-dir)
    uint32 serverInfoSinceMs = 0;
    uint32 statsSinceMs = 0;
    bool serverInfoSent = false;
    TortoiseBots::PlayerLagWindow tickWindow{ 30000 };
    std::vector<std::string> events;   // BOT_EVENTS rows waiting for the next cycle
    struct Track
    {
        float x = 0, y = 0;
        uint32 map = 0;
        time_t since = 0;
        time_t lastStuck = 0;
        bool alive = true;
        // Activity clock (world milliseconds): last observable work, the start of a churn-only run, the last fight and
        // the last XP/level change. Read by the macro-state refinement.
        uint32 lastActivityMs = 0, churnSinceMs = 0, lastCombatMs = 0, lastXpMs = 0;
        uint32 lastXp = 0, lastLevel = 0;
        bool seen = false;
        std::string lastActionName;
        uint8 state = TortoiseBots::dashboard_state::Idle;
        // A hostile combat target that stays out of reach or line of sight; the clock restarts when the target changes.
        ObjectGuid unreachableTarget;
        uint32 unreachableDurationMs = 0;
        bool unreachableReported = false;
        uint32 lastUnreachableReportMs = 0;
    };
    std::unordered_map<ObjectGuid, Track> track;
    // Rolling macro-state histogram: 90 buckets of 2 s, one column per state, so the ratios describe the last 3 min.
    uint64 stateWindow[90][TortoiseBots::dashboard_state::StateCount] = {};
    size_t stateBucketIndex = 0;
    uint32 stateBucketElapsedMs = 0;
    struct BrainInfo { std::string role, combat, nonCombat, engine, state; };
    std::unordered_map<ObjectGuid, BrainInfo> brains;   // refreshed with BOT_STATS, read by every roster row
    struct ActionFailure { uint32 firstMs = 0, lastReportMs = 0, count = 0; bool reported = false; };
    std::unordered_map<std::string, ActionFailure> actionFailures;   // "guid|action" -> the current 2 s window
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

// A client string in the realm's locale; a missing string (null table entry or null text) reads as "".
char const* Loc(LocalizedString const* str)
{
    char const* text = str ? (*str)[sWorld->GetDefaultDbcLocale()] : nullptr;
    return text ? text : "";
}

uint32 ParseItemId(std::string const& s)
{
    if (s.empty())
        return 0;
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (end == s.c_str() || v == 0 || v > 0x7FFFFFFF)
        return 0;
    return uint32(v);
}

// Hooks run on the map threads (MapUpdate.Threads > 1) while the feed cycle runs on the world thread: one lock over
// the module's shared state (events, tracked bots, fights).
std::mutex g_feedLock;

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

bool IsCasting(Player* bot)
{
    return bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) || bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
}

// The instant part of the macro state; the idle clock refines it (dashboard_state::RefineIdle).
uint8 BaseMacroState(Player* bot, TortoiseBots::engine::Telemetry const& engineState)
{
    using namespace TortoiseBots::dashboard_state;
    if (!bot->IsAlive() || engineState.deadState)
        return Dead;
    if (bot->IsInCombat() || engineState.combatState)
        return Combat;
    if (bot->isMoving() || (bot->GetMotionMaster() && (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE ||
            bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE ||
            bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)))
        return Moving;
    if (bot->HasPlayerFlag(PLAYER_FLAGS_RESTING))
        return Resting;
    if (bot->HasUnitFlag(UNIT_FLAG_LOOTING) || IsCasting(bot) || bot->GetStandState() == UNIT_STAND_STATE_SIT)
        return Busy;
    return Idle;
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
    if (Player* master = ObjectAccessor::FindConnectedPlayer(masterGuid); master && master->IsInWorld() && master->FindMap() && master->FindMap() == bot->FindMap())
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
       << (extra.find("\"money\"") == std::string::npos ? ",\"money\":" + std::to_string(bot->GetMoney()) : std::string()) << extra << "}";
    s.events.push_back(ev.str());
}

void SendAnomaly(Player* bot, char const* type, char const* severity, std::string const& target, std::string const& details,
    std::string const& strategy = "", std::string const& lastAction = "");

void Send(std::string const& datagram)
{
    Sender& s = Get();
    boost::system::error_code ec;
    s.socket->send_to(boost::asio::buffer(datagram), s.target, 0, ec);
}

void SendAnomaly(Player* bot, char const* type, char const* severity, std::string const& target, std::string const& details,
    std::string const& strategy, std::string const& lastAction)
{
    std::ostringstream a;
    a << "{\"ts\":" << GameTime::GetGameTime() << ",\"type\":\"" << type << "\",\"severity\":\"" << severity << "\",\"bot\":\""
      << Escape(bot->GetName()) << "\",\"guid\":" << bot->GetGUID().GetCounter() << ",\"class\":\"" << ClassName(bot->GetClass())
      << "\",\"level\":" << uint32(bot->GetLevel()) << ",\"map\":" << bot->GetMapId() << ",\"zone\":" << bot->GetZoneId()
      << ",\"pos\":{\"x\":" << bot->GetPositionX() << ",\"y\":" << bot->GetPositionY() << ",\"z\":" << bot->GetPositionZ()
      << "},\"target\":\"" << Escape(target) << "\",\"strategy\":\"" << Escape(strategy) << "\",\"last_action\":\"" << Escape(lastAction)
      << "\",\"details\":\"" << Escape(details) << "\"}";
    Send(a.str());
}

// The pool's live composition against the census shares the picker uses: counts by class, race, gender and rolled spec tab
// from the registry's ready rows (in memory, no query) and the shares copied into the pure policy headers. Every class, race
// and spec is listed even at count 0 so the dashboard draws a stable table. Empty while the pool has no characters.
std::string BuildPoolCensus()
{
    std::vector<pool::Character> const& characters = pool::Characters();
    if (characters.empty())
        return {};

    std::uint32_t classes[12] = {};
    std::uint32_t races[97] = {};
    std::uint32_t genders[2] = {};
    std::uint32_t specs[12][3] = {};
    std::uint32_t levels[81] = {};
    std::uint32_t levelKnown = 0;
    bool const censusSpec = talents::PoolCensusSpecEnabled();
    for (pool::Character const& character : characters)
    {
        if (character.klass < 12)
            ++classes[character.klass];
        if (character.race < 97)
            ++races[character.race];
        if (character.gender < 2)
            ++genders[character.gender];
        if (character.klass < 12)
        {
            int const tab = talents::PickSpecTabForBot(character.klass, std::uint32_t(character.guid), -1, 0, 0, 0, censusSpec);
            if (tab >= 0 && tab <= 2)
                ++specs[character.klass][tab];
        }
        // The character cache knows every level (it loads the whole table at startup): a hash lookup, no query.
        std::uint8_t const level = character.guid
            ? sCharacterCache->GetCharacterLevelByGuid(ObjectGuid::Create<HighGuid::Player>(character.guid)) : 0;
        if (level >= 1 && level <= 80)
        {
            ++levels[level];
            ++levelKnown;
        }
    }

    std::ostringstream ss;
    ss << "{\"total\":" << characters.size()
       << ",\"census_composition\":" << (pool::CensusComposition() ? 1 : 0)
       << ",\"census_spec\":" << (censusSpec ? 1 : 0)
       << ",\"classes\":[";
    static std::uint8_t const kClasses[] = { world::ClassWarrior, world::ClassPaladin, world::ClassHunter, world::ClassRogue,
        world::ClassPriest, world::ClassShaman, world::ClassMage, world::ClassWarlock, world::ClassDruid };
    for (std::size_t i = 0; i < sizeof(kClasses) / sizeof(kClasses[0]); ++i)
    {
        std::uint8_t const klass = kClasses[i];
        std::uint32_t const census = (world::CensusClassSharePm(0, klass) + world::CensusClassSharePm(1, klass)) / 2;
        ss << (i ? "," : "") << "{\"id\":" << unsigned(klass) << ",\"count\":" << classes[klass] << ",\"census_pm\":" << census << '}';
    }
    ss << "],\"races\":[";
    static std::uint8_t const kRaces[] = { world::RaceHuman, world::RaceNightElf, world::RaceDwarf, world::RaceGnome,
        world::RaceSkyborneAlliance, world::RaceUndead, world::RaceOrc, world::RaceTauren, world::RaceTroll, world::RaceSkyborneHorde };
    for (std::size_t i = 0; i < sizeof(kRaces) / sizeof(kRaces[0]); ++i)
    {
        std::uint8_t const race = kRaces[i];
        std::uint32_t const census = world::CensusRaceSharePm(race) / 2;      // the pool is half Alliance, half Horde
        ss << (i ? "," : "") << "{\"id\":" << unsigned(race) << ",\"faction\":" << (world::IsAllianceRace(race) ? 0 : 1)
           << ",\"count\":" << races[race] << ",\"census_pm\":" << census << '}';
    }
    ss << "],\"genders\":[";
    std::uint32_t censusFemale = 0;
    for (std::uint8_t const klass : kClasses)
        censusFemale += ((world::CensusClassSharePm(0, klass) + world::CensusClassSharePm(1, klass)) / 2) * world::CensusFemaleSharePm(klass) / 1000;
    ss << "{\"id\":0,\"count\":" << genders[0] << ",\"census_pm\":" << (1000 - censusFemale) << "},"
       << "{\"id\":1,\"count\":" << genders[1] << ",\"census_pm\":" << censusFemale << "}";
    ss << "],\"specs\":[";
    bool first = true;
    for (std::uint8_t const klass : kClasses)
    {
        for (int tab = 0; tab < 3; ++tab)
        {
            ss << (first ? "" : ",") << "{\"class\":" << unsigned(klass) << ",\"tab\":" << tab
               << ",\"count\":" << specs[klass][tab] << ",\"census_pm\":" << talents::CensusSpecSharePm(klass, tab) << '}';
            first = false;
        }
    }
    // The pool's own levels are a reference (it starts at 1): min, median, max and how many reached 60, from the cache.
    std::uint32_t levelMin = 0, levelMax = 0, levelMedian = 0, levelSixty = levels[60];
    if (levelKnown)
    {
        for (std::uint32_t level = 1; level <= 80; ++level)
        {
            if (!levels[level])
                continue;
            if (!levelMin)
                levelMin = level;
            levelMax = level;
        }
        std::uint32_t seen = 0;
        for (std::uint32_t level = 1; level <= 80 && !levelMedian; ++level)
            if ((seen += levels[level]) * 2 >= levelKnown)
                levelMedian = level;
    }
    ss << "],\"levels\":{\"known\":" << levelKnown << ",\"min\":" << levelMin << ",\"median\":" << levelMedian
       << ",\"max\":" << levelMax << ",\"sixty\":" << levelSixty << "}}";
    return ss.str();
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
       << ",\"drop_money\":" << sWorld->getRate(RATE_DROP_MONEY) << ",\"rep_gain\":" << sWorld->getRate(RATE_REPUTATION_GAIN)
       << ",\"reputation\":" << sWorld->getRate(RATE_REPUTATION_GAIN)
       << ",\"xp_explore\":" << sWorld->getRate(RATE_XP_EXPLORE) << ",\"xp_bg_kill\":" << sWorld->getRate(RATE_XP_BG_KILL) << ",\"honor\":" << sWorld->getRate(RATE_HONOR)
       << ",\"drop_poor\":" << sWorld->getRate(RATE_DROP_ITEM_POOR) << ",\"drop_uncommon\":" << sWorld->getRate(RATE_DROP_ITEM_UNCOMMON)
       << ",\"drop_item_normal\":" << sWorld->getRate(RATE_DROP_ITEM_NORMAL) << ",\"drop_item_rare\":" << sWorld->getRate(RATE_DROP_ITEM_RARE)
       << ",\"drop_item_epic\":" << sWorld->getRate(RATE_DROP_ITEM_EPIC) << ",\"drop_legendary\":" << sWorld->getRate(RATE_DROP_ITEM_LEGENDARY)
       << ",\"rest_ingame\":" << sWorld->getRate(RATE_REST_INGAME)
       << ",\"skill_discovery\":" << sWorld->getRate(RATE_SKILL_DISCOVERY);
    if (TortoiseBots::engine::ConfigReady())
        ss << ",\"bot_xp_mult\":" << TortoiseBots::engine::BotXpRate();
    ss << "}"
       << ",\"bots\":{\"enabled\":\"" << (mgr.Enabled() ? 1 : 0) << "\",\"online\":" << mgr.Count()
       << ",\"engine_by_default\":\"" << (sConfigMgr->GetBoolDefault("TortoiseBots.Slice.EngineByDefault", false) ? 1 : 0)
       << "\",\"master_grace_s\":" << mgr.MasterGraceSeconds() << ",\"dashboard_interval_ms\":" << s.intervalMs;
    std::string const engineBots = TortoiseBots::engine::ServerBotsJson();
    if (!engineBots.empty())
        ss << ',' << engineBots;
    ss << "},\"diagnostics\":{\"core\":\"ForeverCore\",\"core_branch\":\"" << Escape(GitRevision::GetBranch()) << "\"";
    std::string const engineDiagnostics = TortoiseBots::engine::ServerDiagnosticsJson();
    if (!engineDiagnostics.empty())
        ss << ',' << engineDiagnostics;
    ss << '}';
    std::string const census = BuildPoolCensus();
    if (!census.empty())
        ss << ",\"pool_census\":" << census;
    ss << '}';
    Send(ss.str());
}

// Deaths come from the kill hook; this catches the other ones (fall, drowning, a player) and STUCK.
void TrackBots(std::vector<Player*> const& bots, std::unordered_map<ObjectGuid, TortoiseBots::engine::Telemetry> const& telemetry)
{
    using namespace TortoiseBots::dashboard_state;
    Sender& s = Get();
    TortoiseBots::BotMgr& mgr = TortoiseBots::BotMgr::Instance();
    time_t const now = GameTime::GetGameTime();
    uint32 const nowMs = GameTime::GetGameTimeMS();
    std::unordered_map<ObjectGuid, Sender::Track> next;
    for (Player* bot : bots)
    {
        Sender::Track t;
        if (auto itr = s.track.find(bot->GetGUID()); itr != s.track.end())
            t = itr->second;
        else
            t.since = now;
        auto const engineItr = telemetry.find(bot->GetGUID());
        static TortoiseBots::engine::Telemetry const noEngine;
        TortoiseBots::engine::Telemetry const& engineState = engineItr != telemetry.end() ? engineItr->second : noEngine;
        bool const alive = bot->IsAlive();
        if (t.alive && !alive)
        {
            // OnKilledByCreature already reported this death when a creature did it (it sets alive=false first).
            SendAnomaly(bot, "BOT_DEATH", "WARN", "", "died");
            QueueEvent(bot, "BotDeath", "", "");
        }
        t.alive = alive;

        float const dx = bot->GetPositionX() - t.x;
        float const dy = bot->GetPositionY() - t.y;
        bool const moved = dx * dx + dy * dy >= 0.25f;
        bool const mapChanged = bot->GetMapId() != t.map;
        if (mapChanged || moved)
            t.since = now;
        t.x = bot->GetPositionX(); t.y = bot->GetPositionY(); t.map = bot->GetMapId();
        if (!mapChanged && !moved && alive && now - t.since >= StuckSecs && now - t.lastStuck >= StuckRepeatSecs)
        {
            Player* master = ObjectAccessor::FindConnectedPlayer(mgr.MasterOf(bot->GetGUID()));
            if (master && master->GetMapId() == bot->GetMapId() && master->GetDistance(bot) > StuckMasterDist)
            {
                SendAnomaly(bot, "STUCK", "WARN", master->GetName(), "no movement for " + std::to_string(now - t.since) + " s, master " +
                    std::to_string(int(master->GetDistance(bot))) + " yd away");
                t.lastStuck = now;
            }
        }

        bool const fresh = !t.seen;
        bool const actionChanged = !engineState.lastAction.empty() && engineState.lastAction != t.lastActionName;
        bool const progress = moved || bot->HasUnitFlag(UNIT_FLAG_LOOTING) || IsCasting(bot) || bot->GetStandState() == UNIT_STAND_STATE_SIT;
        bool const churn = actionChanged || !engineState.travelPurpose.empty();
        if (!engineState.lastAction.empty())
            t.lastActionName = engineState.lastAction;
        if (progress)
            t.churnSinceMs = 0;
        else if (churn && t.churnSinceMs == 0)
            t.churnSinceMs = nowMs;
        if (progress || churn)
            t.lastActivityMs = nowMs;
        // A freshly seen bot has no past: its idle clock starts expired instead of granting a 45 s grace window.
        if (fresh && t.lastActivityMs == 0)
            t.lastActivityMs = nowMs - IdleAfterMs;

        uint8 const base = BaseMacroState(bot, engineState);
        if (base == Combat)
            t.lastCombatMs = nowMs;
        uint32 const curXp = bot->GetXP();
        uint32 const curLevel = bot->GetLevel();
        if (t.seen && (curXp != t.lastXp || curLevel != t.lastLevel))
            t.lastXpMs = nowMs;
        t.lastXp = curXp;
        t.lastLevel = curLevel;
        if (base == Idle && (t.lastActivityMs == 0 || nowMs - t.lastActivityMs >= IdleAfterMs))
            t.churnSinceMs = 0;
        t.state = RefineIdle(base, nowMs, t.lastActivityMs, t.churnSinceMs, t.lastCombatMs, t.lastXpMs);

        // A hostile combat target that stays more than 45 yd away or out of line of sight for 10 s is unreachable. The
        // engine's own "current target" is judged, not the selection, which outlives the fight.
        if (engineState.currentTarget != t.unreachableTarget)
        {
            t.unreachableTarget = engineState.currentTarget;
            t.unreachableDurationMs = 0;
            t.unreachableReported = false;
        }
        if (t.state == Combat && engineState.combatState && !engineState.currentTarget.IsEmpty() && engineState.targetHostile)
        {
            bool const unreachable = engineState.targetDistance > 45.0f || !engineState.targetInLos;
            if (unreachable)
            {
                t.unreachableDurationMs += s.intervalMs;
                bool const due = !t.unreachableReported || (nowMs - t.lastUnreachableReportMs) >= 30000;
                if (t.unreachableDurationMs >= 10000 && due)
                {
                    t.unreachableReported = true;
                    t.lastUnreachableReportMs = nowMs;
                    std::ostringstream details;
                    details << "Combat target '" << engineState.currentTargetName << "' unreachable / out of LoS for "
                            << (t.unreachableDurationMs / 1000.0f) << "s (dist=" << std::fixed << std::setprecision(1)
                            << engineState.targetDistance << ", inLos=" << (engineState.targetInLos ? "true" : "false") << ")";
                    std::string activeStrategy;
                    TortoiseBots::engine::Telemetry detail;
                    if (TortoiseBots::engine::ReadTelemetry(bot, detail, true))
                        activeStrategy = detail.combatStrategies;
                    SendAnomaly(bot, "UNREACHABLE_TARGET", "WARN", engineState.currentTargetName, details.str(),
                        activeStrategy, engineState.lastAction);
                }
            }
            else
            {
                t.unreachableDurationMs = 0;
                t.unreachableReported = false;
            }
        }
        else
        {
            t.unreachableDurationMs = 0;
            t.unreachableReported = false;
        }
        t.seen = true;
        next[bot->GetGUID()] = t;
    }
    s.track.swap(next);
    for (auto itr = s.brains.begin(); itr != s.brains.end();)
        itr = s.track.count(itr->first) ? std::next(itr) : s.brains.erase(itr);
}

void AddStateTime(uint8 state, uint32 diff)
{
    using namespace TortoiseBots::dashboard_state;
    if (state >= StateCount || diff == 0)
        return;
    Sender& s = Get();
    // Fill whole 2 s buckets: a large snapshot (many bots) may span several, and only the newest 90 survive.
    while (diff > 0)
    {
        uint32 const room = (s.stateBucketElapsedMs < 2000 ? 2000 - s.stateBucketElapsedMs : 1);
        uint32 const add = std::min(room, diff);
        s.stateWindow[s.stateBucketIndex][state] += add;
        s.stateBucketElapsedMs += add;
        diff -= add;
        if (s.stateBucketElapsedMs >= 2000)
        {
            s.stateBucketElapsedMs -= 2000;
            s.stateBucketIndex = (s.stateBucketIndex + 1) % 90;
            for (size_t i = 0; i < StateCount; ++i)
                s.stateWindow[s.stateBucketIndex][i] = 0;
        }
    }
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

// DUNGEON_MOBS: every dungeon or raid copy with bots in it, its members (and their targets) and the creatures near
// them (health, place, victim, reaction to the bots, level, rank, the spell being cast). Ids are "p<n>" for players
// and "c<n>" for creatures so a mob's victim names a member.
constexpr float DungeonMobRange = 80.0f;
constexpr size_t DungeonMobCap = 60;

std::string UnitId(Unit const* unit)
{
    return (unit->GetTypeId() == TYPEID_PLAYER ? "p" : "c") + std::to_string(unit->GetGUID().GetCounter());
}

void SendDungeonMobs(std::vector<Player*> const& bots, uint64 seq, time_t now)
{
    Sender& s = Get();
    std::map<std::pair<uint32, uint32>, std::vector<Player*>> runs;
    for (Player* bot : bots)
        if (Map* map = bot->FindMap(); map && map->IsDungeon())
            runs[{ bot->GetMapId(), bot->GetInstanceId() }].push_back(bot);

    std::ostringstream ss;
    ss << "{\"v\":" << ProtocolVersion << ",\"session\":" << s.session << ",\"seq\":" << seq << ",\"ts\":" << now
       << ",\"type\":\"DUNGEON_MOBS\",\"part\":0,\"parts\":1,\"runs\":[";
    bool firstRun = true;
    for (auto const& [key, members] : runs)
    {
        std::map<ObjectGuid, Creature*> mobs;
        for (Player* member : members)
        {
            FindCreatureOptions options;
            options.IsAlive = FindCreatureAliveState::Alive;
            std::list<Creature*> near;
            member->GetCreatureListWithOptionsInGrid(near, DungeonMobRange, options);
            for (Creature* creature : near)
                mobs.emplace(creature->GetGUID(), creature);
        }
        Player* viewer = members.front();
        ss << (firstRun ? "" : ",") << "{\"map\":" << key.first << ",\"instance\":" << key.second << ",\"raid\":" << (viewer->FindMap() && viewer->FindMap()->IsRaid() ? "true" : "false")
           << ",\"members\":[";
        firstRun = false;
        for (size_t i = 0; i < members.size(); ++i)
        {
            Unit const* target = ObjectAccessor::GetUnit(*members[i], members[i]->GetTarget());
            ss << (i ? "," : "") << "{\"guid\":" << members[i]->GetGUID().GetCounter() << ",\"id\":\"" << UnitId(members[i]) << "\""
               << (target ? ",\"target\":\"" + UnitId(target) + "\"" : std::string()) << "}";
        }
        ss << "],\"mobs_map\":" << key.first << ",\"mobs_total\":" << mobs.size() << ",\"mobs\":[";
        size_t shown = 0;
        for (auto const& [guid, mob] : mobs)
        {
            if (shown >= DungeonMobCap)
                break;
            char const* reaction = viewer->IsHostileTo(mob) ? "h" : viewer->IsFriendlyTo(mob) ? "f" : "n";
            CreatureClassifications const cls = mob->GetCreatureClassification();
            char const* rank = mob->IsDungeonBoss() ? "b" : (cls == CreatureClassifications::Elite || cls == CreatureClassifications::RareElite) ? "e"
                : cls == CreatureClassifications::Rare ? "r" : "";
            std::string cast;
            if (Spell const* spell = mob->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                cast = Loc(spell->GetSpellInfo()->SpellName);
            Unit const* victim = mob->GetVictim();
            ss << (shown ? "," : "") << "{\"id\":\"" << UnitId(mob) << "\",\"entry\":" << mob->GetEntry() << ",\"name\":\"" << Escape(mob->GetName())
               << "\",\"hp\":" << uint32(mob->GetHealthPct()) << ",\"x\":" << mob->GetPositionX() << ",\"y\":" << mob->GetPositionY()
               << (victim ? ",\"victim\":\"" + UnitId(victim) + "\"" : std::string()) << ",\"reaction\":\"" << reaction << "\",\"lvl\":" << uint32(mob->GetLevel())
               << (*rank ? std::string(",\"rank\":\"") + rank + "\"" : std::string()) << (cast.empty() ? std::string() : ",\"cast\":\"" + Escape(cast) + "\"") << "}";
            ++shown;
        }
        ss << "]}";
    }
    ss << "]}";
    Send(ss.str());
}

// RAIDS: every raid group with a bot in it: leader, where the leader is, the members with their subgroup and whether they are online.
void SendRaids(std::vector<Player*> const& bots, uint64 seq, time_t now)
{
    Sender& s = Get();
    std::map<ObjectGuid, Group*> groups;
    for (Player* bot : bots)
        if (Group* group = bot->GetGroup(); group && group->isRaidGroup())
            groups.emplace(group->GetGUID(), group);

    std::ostringstream ss;
    ss << "{\"v\":" << ProtocolVersion << ",\"session\":" << s.session << ",\"seq\":" << seq << ",\"ts\":" << now << ",\"type\":\"RAIDS\",\"raids\":[";
    bool first = true;
    for (auto const& [guid, group] : groups)
    {
        Player* leader = ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID());
        ss << (first ? "" : ",") << "{\"group\":" << guid.GetCounter() << ",\"key\":\"\",\"map\":" << (leader ? leader->GetMapId() : 0)
           << ",\"instance\":" << (leader ? leader->GetInstanceId() : 0) << ",\"rg\":true,\"leader\":\"" << Escape(group->GetLeaderName())
           << "\",\"leader_guid\":" << group->GetLeaderGUID().GetCounter() << ",\"members\":[";
        first = false;
        bool firstMember = true;
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            bool const online = ObjectAccessor::FindConnectedPlayer(slot.guid) != nullptr;
            ss << (firstMember ? "" : ",") << "{\"g\":" << slot.guid.GetCounter() << ",\"s\":" << uint32(slot.group) << ",\"n\":\"" << Escape(slot.name) << "\""
               << (online ? "" : ",\"off\":true") << "}";
            firstMember = false;
        }
        ss << "]}";
    }
    ss << "]}";
    Send(ss.str());
}

// COMBAT_FIGHT: one fight of the bots in a dungeon or raid copy, from the first bot entering combat until none is in combat:
// per bot damage done and taken, healing and overhealing, casts per spell and deaths with their last hits. The dashboard
// files it under the copy's run ("dgn:<map>:<instance>") and judges it with its own rules.
struct FightHitRec { std::string attacker; uint32 amount; uint32 tMs; };
struct FightBotAcc
{
    uint64 dmg = 0, heal = 0, over = 0, taken = 0;
    uint32 casts = 0, healCasts = 0, deaths = 0;
    std::map<std::string, uint32> spells;
    std::vector<FightHitRec> lastHits; // newest last, at most 5
};
struct FightDeathRec { std::string name, killer; uint32 tMs; float x, y, z; std::vector<FightHitRec> hits; };
struct FightAcc
{
    uint32 startMs = 0;
    time_t start = 0;
    uint32 seq = 0;
    std::map<ObjectGuid, FightBotAcc> bots;
    std::map<uint32, std::pair<std::string, std::set<ObjectGuid>>> engaged; // entry -> name, units
    std::vector<FightDeathRec> deaths;
    uint32 boss = 0;
    std::string bossName;
};
std::map<std::pair<uint32, uint32>, FightAcc> g_fights;
uint32 g_fightCounter = 0;

// A dungeon run summary for the Dungeon runs tab (<TraceDir>/<map>-<instance>-<start>.summary.json, the trace summary
// format the dashboard reads): from the first bot entering a dungeon or raid copy until no bot is in it, the copy's bosses
// (the core's encounter list) with the second each died, the wipes and deaths with where they happened, the party and the outcome.
struct RunSpot { uint32 t; float x, y; std::string role; };
struct RunMember { std::string cls, role; uint32 level; };
struct RunRec
{
    time_t started = 0;
    std::vector<std::string> bosses;           // the copy's encounters, in order
    std::map<std::string, uint32> killedAt;    // boss name -> seconds from the start
    std::vector<RunSpot> wipes, deaths;
    std::map<ObjectGuid, RunMember> party;
};
std::map<std::pair<uint32, uint32>, RunRec> g_runs;

RunRec* RunOf(Unit const* unit)
{
    if (!unit || !unit->FindMap() || !unit->FindMap()->IsDungeon())
        return nullptr;
    auto itr = g_runs.find({ unit->GetMapId(), unit->GetInstanceId() });
    return itr == g_runs.end() ? nullptr : &itr->second;
}

std::string IsoTime(time_t t)
{
    char buf[32];
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

void WriteRunSummary(std::pair<uint32, uint32> const& key, RunRec const& run, time_t now)
{
    std::string const dir = Get().traceDir;
    if (dir.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    MapEntry const* mapEntry = sMapStore.LookupEntry(key.first);
    std::string const id = std::to_string(key.first) + "-" + std::to_string(key.second) + "-" + std::to_string(run.started);
    size_t killed = 0;
    for (std::string const& boss : run.bosses)
        killed += run.killedAt.count(boss);
    char const* outcome = !run.bosses.empty() && killed == run.bosses.size() ? "cleared" : killed || !run.killedAt.empty() ? "partial"
        : !run.wipes.empty() ? "wiped_out" : "abandoned";
    std::ostringstream ss;
    ss << "{\"v\":1,\"schema\":\"bt-trace\",\"run\":\"" << id << "\",\"src\":\"server\",\"status\":\"complete\",\"end_reason\":\"left\",\"dungeon\":{\"map\":"
       << key.first << ",\"name\":\"" << (mapEntry ? Escape(Loc(&mapEntry->MapName)) : std::string()) << "\"},\"mode\":\"engine\",\"started\":\"" << IsoTime(run.started)
       << "\",\"ended\":\"" << IsoTime(now) << "\",\"outcome\":\"" << outcome << "\",\"bosses\":[";
    std::vector<std::string> names = run.bosses;
    for (auto const& [name, t] : run.killedAt)
        if (std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
    uint32 lastKill = 0;
    for (size_t i = 0; i < names.size(); ++i)
    {
        auto k = run.killedAt.find(names[i]);
        ss << (i ? "," : "") << "{\"name\":\"" << Escape(names[i]) << "\",\"killed_at_s\":" << (k != run.killedAt.end() ? std::to_string(k->second) : "null") << "}";
        if (k != run.killedAt.end())
            lastKill = std::max(lastKill, k->second);
    }
    auto spots = [&](std::vector<RunSpot> const& list)
    {
        for (size_t i = 0; i < list.size(); ++i)
            ss << (i ? "," : "") << "{\"t_s\":" << list[i].t << ",\"x\":" << list[i].x << ",\"y\":" << list[i].y << ",\"role\":\"" << list[i].role << "\"}";
    };
    ss << "],\"wipes\":[";
    spots(run.wipes);
    ss << "],\"deaths\":[";
    spots(run.deaths);
    ss << "],\"party\":[";
    bool first = true;
    for (auto const& [guid, m] : run.party)
    {
        ss << (first ? "" : ",") << "{\"class\":\"" << m.cls << "\",\"role\":\"" << (m.role == "healer" ? "heal" : m.role) << "\",\"level\":" << m.level << "}";
        first = false;
    }
    ss << "],\"driver\":{\"model\":\"bot engine\"}";
    if (std::string(outcome) == "cleared")
        ss << ",\"clear_s\":" << lastKill;
    ss << "}";
    TortoiseBots::Dashboard::WriteJsonFile(dir + "/" + id + ".summary.json", ss.str(), "run summary", 1);
}

// Opens a run when bots enter a copy, keeps its party current and files it once no bot is in the copy.
void UpdateRuns(std::vector<Player*> const& bots, time_t now)
{
    Sender& s = Get();
    std::set<std::pair<uint32, uint32>> present;
    for (Player* bot : bots)
    {
        Map* map = bot->FindMap();
        if (!map || !map->IsDungeon())
            continue;
        std::pair<uint32, uint32> const key{ bot->GetMapId(), bot->GetInstanceId() };
        present.insert(key);
        auto [itr, created] = g_runs.try_emplace(key);
        RunRec& run = itr->second;
        if (created)
        {
            run.started = now;
            std::set<int32> orders;
            std::vector<std::pair<int32, std::string>> enc;
            for (DungeonEncounterEntry const* e : sDungeonEncounterStore)
                if (e->MapID == key.first && (e->DifficultyID == 0 || e->DifficultyID == int16(map->GetDifficultyID())))
                {
                    std::string const name = Loc(&e->Name);
                    if (!name.empty() && std::none_of(enc.begin(), enc.end(), [&](auto const& p) { return p.second == name; }))
                        enc.emplace_back(e->OrderIndex, name);
                }
            std::sort(enc.begin(), enc.end());
            for (auto const& [order, name] : enc)
                run.bosses.push_back(name);
        }
        auto brain = s.brains.find(bot->GetGUID());
        run.party[bot->GetGUID()] = { ClassName(bot->GetClass()), brain != s.brains.end() && !brain->second.role.empty() ? brain->second.role : "dps", uint32(bot->GetLevel()) };
    }
    for (auto itr = g_runs.begin(); itr != g_runs.end();)
    {
        if (present.count(itr->first))
        {
            ++itr;
            continue;
        }
        WriteRunSummary(itr->first, itr->second, now);
        itr = g_runs.erase(itr);
    }
}

FightAcc* FightOf(Unit const* unit)
{
    if (!unit || !unit->FindMap() || !unit->FindMap()->IsDungeon())
        return nullptr;
    auto itr = g_fights.find({ unit->GetMapId(), unit->GetInstanceId() });
    return itr == g_fights.end() ? nullptr : &itr->second;
}

uint32 FightMs(FightAcc const& fight) { return GameTime::GetGameTimeMS() - fight.startMs; }

void NoteEnemy(FightAcc& fight, Unit const* unit)
{
    Creature const* creature = unit ? unit->ToCreature() : nullptr;
    if (!creature)
        return;
    auto& entry = fight.engaged[creature->GetEntry()];
    entry.first = creature->GetName();
    entry.second.insert(creature->GetGUID());
    if (creature->IsDungeonBoss() && !fight.boss)
    {
        fight.boss = creature->GetEntry();
        fight.bossName = creature->GetName();
    }
}

void FileFight(std::pair<uint32, uint32> const& key, FightAcc& fight, std::vector<Player*> const& bots, uint64 seq, time_t now)
{
    Sender& s = Get();
    bool anyAlive = false;
    for (Player* bot : bots)
        if (bot->GetMapId() == key.first && bot->GetInstanceId() == key.second && bot->IsAlive())
            anyAlive = true;
    if (!anyAlive)
        if (auto run = g_runs.find(key); run != g_runs.end() && !fight.deaths.empty())
            run->second.wipes.push_back({ uint32(now - run->second.started), fight.deaths.back().x, fight.deaths.back().y, "" });
    std::string const commander = "dgn:" + std::to_string(key.first) + ":" + std::to_string(key.second);
    std::ostringstream ss;
    ss << "{\"v\":" << ProtocolVersion << ",\"session\":" << s.session << ",\"seq\":" << seq << ",\"ts\":" << now
       << ",\"type\":\"COMBAT_FIGHT\",\"fight\":{\"id\":\"f" << fight.seq << "\",\"commander\":\"" << commander
       << "\",\"commander_guid\":0,\"group\":0,\"map\":" << key.first << ",\"plan\":\"\",\"leg\":\"\",\"step\":0,\"kind\":\""
       << (fight.boss ? "boss" : "trash") << "\",\"start\":" << fight.start << ",\"dur_ms\":" << FightMs(fight) << ",\"outcome\":\""
       << (anyAlive ? "win" : "wipe") << "\",\"boss\":" << fight.boss << ",\"boss_name\":\"" << Escape(fight.bossName)
       << "\",\"boss_phase\":0,\"boss_phase_entry\":0,\"planned\":[],\"engaged\":[";
    bool first = true;
    for (auto const& [entry, info] : fight.engaged)
    {
        ss << (first ? "" : ",") << "{\"e\":" << entry << ",\"n\":\"" << Escape(info.first) << "\",\"c\":" << info.second.size() << "}";
        first = false;
    }
    ss << "],\"bots\":[";
    first = true;
    for (auto const& [guid, acc] : fight.bots)
    {
        Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
        if (!bot)
            continue;
        Group* group = bot->GetGroup();
        auto brain = s.brains.find(guid);
        ss << (first ? "" : ",") << "{\"name\":\"" << Escape(bot->GetName()) << "\",\"guid\":" << guid.GetCounter() << ",\"class\":\""
           << ClassName(bot->GetClass()) << "\",\"role\":\"" << (brain != s.brains.end() && !brain->second.role.empty() ? brain->second.role : "dps")
           << "\",\"sub\":" << (group && group->isRaidGroup() ? int32(group->GetMemberGroup(guid)) : -1) << ",\"dmg\":" << acc.dmg << ",\"heal\":"
           << acc.heal << ",\"over\":" << acc.over << ",\"taken\":" << acc.taken << ",\"casts\":" << acc.casts << ",\"heal_casts\":" << acc.healCasts
           << ",\"deaths\":" << acc.deaths << ",\"spells\":[";
        bool firstSpell = true;
        for (auto const& [name, count] : acc.spells)
        {
            ss << (firstSpell ? "" : ",") << "{\"n\":\"" << Escape(name) << "\",\"c\":" << count << "}";
            firstSpell = false;
        }
        ss << "],\"tail_spells\":[]}";
        first = false;
    }
    ss << "],\"debuffs\":{\"sampled_s\":0,\"up_s\":{}},\"deaths\":[";
    first = true;
    for (FightDeathRec const& death : fight.deaths)
    {
        ss << (first ? "" : ",") << "{\"name\":\"" << Escape(death.name) << "\",\"t_ms\":" << death.tMs << ",\"x\":" << death.x << ",\"y\":" << death.y
           << ",\"z\":" << death.z << ",\"killer\":\"" << Escape(death.killer) << "\",\"hits\":[";
        for (size_t i = 0; i < death.hits.size(); ++i)
            ss << (i ? "," : "") << "{\"a\":\"" << Escape(death.hits[i].attacker) << "\",\"d\":" << death.hits[i].amount << ",\"t_ms\":" << death.hits[i].tMs << "}";
        ss << "]}";
        first = false;
    }
    ss << "]}}";
    Send(ss.str());
}

// Starts a fight in a copy when one of its bots enters combat; files it once none of them is in combat any more.
void UpdateFights(std::vector<Player*> const& bots, uint64 seq, time_t now)
{
    std::map<std::pair<uint32, uint32>, bool> inCombat;
    for (Player* bot : bots)
        if (Map* map = bot->FindMap(); map && map->IsDungeon())
            inCombat[{ bot->GetMapId(), bot->GetInstanceId() }] |= bot->IsInCombat();
    for (auto const& [key, combat] : inCombat)
        if (combat && !g_fights.count(key))
        {
            FightAcc& fight = g_fights[key];
            fight.startMs = GameTime::GetGameTimeMS();
            fight.start = now;
            fight.seq = ++g_fightCounter;
        }
    for (auto itr = g_fights.begin(); itr != g_fights.end();)
    {
        auto combat = inCombat.find(itr->first);
        if (combat != inCombat.end() && combat->second)
        {
            ++itr;
            continue;
        }
        if (!itr->second.bots.empty())
            FileFight(itr->first, itr->second, bots, seq, now);
        itr = g_fights.erase(itr);
    }
}

void SendCycle()
{
    Sender& s = Get();
    TortoiseBots::BotMgr& mgr = TortoiseBots::BotMgr::Instance();
    std::vector<Player*> bots;
    for (ObjectGuid const& guid : mgr.BotGuids())
        if (Player* bot = mgr.FindBot(guid); bot && bot->IsInWorld() && bot->FindMap() && !bot->IsBeingTeleported())
            bots.push_back(bot);

    // Real players ride the same roster: the sessions with a socket, never the headless bot sessions.
    std::vector<Player*> players;
    for (auto const& sessionEntry : sWorld->GetAllSessions())
    {
        WorldSession* session = sessionEntry.second;
        if (!session || session->IsBot())
            continue;
        if (Player* player = session->GetPlayer(); player && player->IsInWorld())
            players.push_back(player);
    }

    std::unordered_map<ObjectGuid, TortoiseBots::engine::Telemetry> telemetry;
    telemetry.reserve(bots.size());
    for (Player* bot : bots)
        TortoiseBots::engine::ReadTelemetry(bot, telemetry[bot->GetGUID()]);

    TrackBots(bots, telemetry);

    for (Player* bot : bots)
    {
        auto const itr = s.track.find(bot->GetGUID());
        if (itr != s.track.end())
            AddStateTime(itr->second.state, s.intervalMs);
    }

    uint64 totals[TortoiseBots::dashboard_state::StateCount] = {};
    for (size_t bucket = 0; bucket < 90; ++bucket)
        for (size_t state = 0; state < TortoiseBots::dashboard_state::StateCount; ++state)
            totals[state] += s.stateWindow[bucket][state];
    uint64 grandTotal = 0;
    for (size_t state = 0; state < TortoiseBots::dashboard_state::StateCount; ++state)
        grandTotal += totals[state];
    auto ratio = [&](size_t state) { return grandTotal ? double(totals[state]) / double(grandTotal) : 0.0; };

    uint32 const humans = uint32(players.size());
    uint64 const seq = ++s.seq;
    time_t const now = GameTime::GetGameTime();
    size_t const rosterSize = bots.size() + players.size();
    size_t const total = rosterSize == 0 ? 0 : (rosterSize + BotsPerBatch - 1) / BotsPerBatch;

    std::ostringstream hb;
    TortoiseBots::PlayerLagWindow::Stats const tick = s.tickWindow.Compute();
    hb << "{\"v\":" << ProtocolVersion << ",\"session\":" << s.session << ",\"seq\":" << seq << ",\"ts\":" << now
       << ",\"type\":\"HEARTBEAT\",\"uptime\":" << GameTime::GetUptime() << ",\"diff\":" << sWorldUpdateTime.GetLastUpdateTime()
       << ",\"diff_avg\":" << tick.avgMs << ",\"diff_worst\":" << tick.worstMs
       << ",\"lag_p50\":" << tick.p50Ms << ",\"lag_p95\":" << tick.p95Ms
       << ",\"window_secs\":180"
       << ",\"humans\":" << humans << ",\"bots\":" << bots.size() << ",\"states\":{\"combat\":" << ratio(TortoiseBots::dashboard_state::Combat)
       << ",\"moving\":" << ratio(TortoiseBots::dashboard_state::Moving) << ",\"busy\":" << ratio(TortoiseBots::dashboard_state::Busy)
       << ",\"stalled\":" << ratio(TortoiseBots::dashboard_state::Stalled) << ",\"resting\":" << ratio(TortoiseBots::dashboard_state::Resting)
       << ",\"dead\":" << ratio(TortoiseBots::dashboard_state::Dead) << ",\"idle\":" << ratio(TortoiseBots::dashboard_state::Idle) << "}}";
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
        size_t const end = std::min(rosterSize, (batch + 1) * BotsPerBatch);
        for (size_t i = batch * BotsPerBatch; i < end; ++i)
        {
            if (i != batch * BotsPerBatch)
                ss << ',';
            if (i >= bots.size())
            {
                // A real player: the same list/map fields, no bot-only detail.
                Player* player = players[i - bots.size()];
                Powers const power = player->GetPowerType();
                std::string target;
                if (Unit* unit = ObjectAccessor::GetUnit(*player, player->GetTarget()))
                    target = unit->GetName();
                Group* group = player->GetGroup();
                TortoiseBots::engine::Telemetry const noEngine;
                ss << "{\"name\":\"" << Escape(player->GetName()) << "\",\"guid\":" << player->GetGUID().GetCounter()
                   << ",\"player\":true,\"kind\":\"player\",\"class\":\"" << ClassName(player->GetClass()) << "\",\"role\":\"\""
                   << ",\"group\":" << (group ? group->GetGUID().GetCounter() : 0)
                   << ",\"subgroup\":" << (group ? uint32(group->GetMemberGroup(player->GetGUID())) : 0)
                   << (player->FindMap() && player->FindMap()->IsRaid() ? ",\"raid\":true" : "")
                   << ",\"level\":" << uint32(player->GetLevel())
                   << ",\"hp\":" << player->GetHealth() << ",\"max_hp\":" << player->GetMaxHealth()
                   << ",\"power\":" << ShownPower(player, power, player->GetPower(power))
                   << ",\"max_power\":" << ShownPower(player, power, player->GetMaxPower(power))
                   << ",\"power_type\":\"" << PowerName(power) << "\""
                   << ",\"map\":" << player->GetMapId() << ",\"zone\":" << player->GetZoneId() << ",\"instance\":" << player->GetInstanceId()
                   << ",\"x\":" << player->GetPositionX() << ",\"y\":" << player->GetPositionY() << ",\"z\":" << player->GetPositionZ()
                   << ",\"o\":" << player->GetOrientation()
                   << ",\"target\":\"" << Escape(target) << "\",\"strategy\":\"\",\"state\":\""
                   << TortoiseBots::dashboard_state::Name(BaseMacroState(player, noEngine)) << "\""
                   << ",\"xp\":" << player->GetXP() << ",\"next_xp\":" << player->GetXPForNextLevel()
                   << ",\"guild\":\"" << Escape(player->GetGuildName()) << "\"" << UiMapFields(player) << "}";
                continue;
            }

            Player* bot = bots[i];
            Powers const power = bot->GetPowerType();
            std::string target;
            if (Unit* unit = ObjectAccessor::GetUnit(*bot, bot->GetTarget()))
                target = unit->GetName();
            Group* group = bot->GetGroup();
            Sender::BrainInfo const& brain = s.brains[bot->GetGUID()];
            TortoiseBots::engine::Telemetry const& engineState = telemetry[bot->GetGUID()];
            uint8 const botState = s.track.count(bot->GetGUID()) ? s.track[bot->GetGUID()].state : uint8(TortoiseBots::dashboard_state::Idle);
            ss << "{\"name\":\"" << Escape(bot->GetName()) << "\",\"guid\":" << bot->GetGUID().GetCounter()
               << ",\"player\":false"
               << ",\"kind\":\"" << (mgr.MasterOf(bot->GetGUID()).IsEmpty() ? "pool" : "hired") << "\""
               << ",\"class\":\"" << ClassName(bot->GetClass()) << "\",\"role\":\"" << Escape(brain.role) << "\""
               << ",\"group\":" << (group ? group->GetGUID().GetCounter() : 0)
               << ",\"subgroup\":" << (group ? uint32(group->GetMemberGroup(bot->GetGUID())) : 0)
               << (bot->FindMap() && bot->FindMap()->IsRaid() ? ",\"raid\":true" : "")
               << ",\"level\":" << uint32(bot->GetLevel())
               << ",\"hp\":" << bot->GetHealth() << ",\"max_hp\":" << bot->GetMaxHealth()
               << ",\"power\":" << ShownPower(bot, power, bot->GetPower(power)) << ",\"max_power\":" << ShownPower(bot, power, bot->GetMaxPower(power))
               << ",\"power_type\":\"" << PowerName(power) << "\""
               << ",\"map\":" << bot->GetMapId() << ",\"zone\":" << bot->GetZoneId() << ",\"instance\":" << bot->GetInstanceId()
               << ",\"x\":" << bot->GetPositionX() << ",\"y\":" << bot->GetPositionY() << ",\"z\":" << bot->GetPositionZ()
               << ",\"o\":" << bot->GetOrientation()
               << ",\"target\":\"" << Escape(target) << "\",\"strategy\":\"" << Escape(brain.combat) << "\",\"strategies_nc\":\"" << Escape(brain.nonCombat)
               << "\",\"engine\":\"" << brain.engine << "\",\"engine_state\":\"" << Escape(brain.state) << "\",\"state\":\""
               << TortoiseBots::dashboard_state::Name(botState) << "\""
               << ",\"last_action\":\"" << Escape(engineState.lastAction) << "\",\"last_trigger\":\"" << Escape(engineState.lastTrigger) << "\""
               << ",\"travel_purpose\":\"" << Escape(engineState.travelPurpose) << "\",\"travel_to\":\"" << Escape(engineState.travelTo)
               << "\",\"travel_status\":\"" << Escape(engineState.travelStatus) << "\",\"travel_dist\":" << engineState.travelDist
               << ",\"xp\":" << bot->GetXP() << ",\"next_xp\":" << bot->GetXPForNextLevel()
               << ",\"guild\":\"" << Escape(bot->GetGuildName()) << "\"" << UiMapFields(bot)
               << DriverFields(bot, s.track.count(bot->GetGUID()) ? s.track[bot->GetGUID()].since : now, now) << "}";
        }
        ss << "]}";
        Send(ss.str());
    }

    SendDungeonMobs(bots, seq, now);
    SendRaids(bots, seq, now);
    UpdateFights(bots, seq, now);
    UpdateRuns(bots, now);

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
    s.traceDir = sConfigMgr->GetStringDefault("TortoiseBots.Dashboard.TraceDir", "");
    if (s.traceDir.empty())
        s.traceDir = sConfigMgr->GetStringDefault("LogsDir", ".") + "/runs";
    s.socket.reset();
    if (!s.enabled)
        return;

    std::string resolveError;
    if (!dash::ResolveEndpoint(host, uint16(port), s.target, resolveError))
    {
        TC_LOG_ERROR("server.loading", "TortoiseBots: cannot resolve dashboard host '{}' ({}); the dashboard gets no data", host, resolveError);
        s.enabled = false;
        return;
    }
    boost::system::error_code ec;
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
    std::lock_guard lock(g_feedLock);
    Sender& s = Get();
    if (!s.enabled || !s.socket || !BotMgr::Instance().Enabled())
        return;
    s.serverInfoSinceMs += diff;
    s.tickWindow.Add(diff);
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

// The ItemDisplayInfo id the dashboard's 3D viewer feeds ZamModelViewer.
uint32 DisplayOf(uint32 itemId)
{
    if (ItemModifiedAppearanceEntry const* modified = TransmogMgr::GetDefaultItemModifiedAppearance(itemId))
        if (ItemAppearanceEntry const* appearance = sItemAppearanceStore.LookupEntry(modified->ItemAppearanceID))
            return uint32(appearance->ItemDisplayInfoID);
    return 0;
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
// The class talent tabs as the Forever client draws them, from the trait data (Talent.db2 is the old 1.12 table the
// client no longer uses): a class's three tabs sit side by side in one trait tree, rows 600 apart from the top row,
// talents 600 apart in a row and a wide gap (1500 or more) between tabs. Nodes far below the seventh row or alone are
// hidden ones the data keeps. Each talent: trait node and node entry (what character_trait_entry stores), spell, name,
// icon, max rank, row and column.
void WriteTalentsFile()
{
    std::ostringstream ss;
    ss << "{\"classes\":[";
    size_t n = 0, classes = 0;
    for (uint8 cls = 1; cls < MAX_CLASSES; ++cls)
    {
        ChrSpecializationEntry const* spec = sDB2Manager.GetChrSpecializationByIndex(cls, 0);
        if (!spec)
            continue;
        WorldPackets::Traits::TraitConfig config;
        config.Type = TraitConfigType::Combat;
        config.ChrSpecializationID = spec->ID;
        struct Placed { int32 node, entry, max, def, x, row; };
        std::vector<Placed> placed;
        std::vector<TraitMgr::NodeEntryInfo> const entries = TraitMgr::GetNodeEntriesForConfig(config);
        std::vector<int32> ys;
        for (TraitMgr::NodeEntryInfo const& e : entries)
            if (TraitNodeEntry const* node = sTraitNodeStore.LookupEntry(e.TraitNodeID))
                ys.push_back(node->PosY);
        std::sort(ys.begin(), ys.end());
        int32 topY = ys.empty() ? 0 : ys.front();
        for (int32 y : ys)
            if (std::count_if(ys.begin(), ys.end(), [y](int32 o) { return std::abs(o - y) <= 100; }) >= 3)
            {
                topY = y;
                break;
            }
        std::set<int32> seen;
        for (TraitMgr::NodeEntryInfo const& e : entries)
        {
            TraitNodeEntry const* node = sTraitNodeStore.LookupEntry(e.TraitNodeID);
            if (!node || !seen.insert(e.TraitNodeID).second)
                continue;
            int32 const row = int32(std::lround(double(node->PosY - topY) / 600.0));
            if (row < 0 || row > 6)
                continue;
            placed.push_back({ e.TraitNodeID, e.TraitNodeEntryID, e.MaxRanks, e.TraitDefinitionID, node->PosX, row });
        }
        std::sort(placed.begin(), placed.end(), [](Placed const& a, Placed const& b) { return a.x < b.x; });
        std::vector<std::vector<Placed>> tabs(1);
        for (size_t i = 0; i < placed.size(); ++i)
        {
            if (i && placed[i].x - placed[i - 1].x >= 1500)
                tabs.emplace_back();
            tabs.back().push_back(placed[i]);
        }
        tabs.erase(std::remove_if(tabs.begin(), tabs.end(), [](std::vector<Placed> const& t) { return t.size() < 8; }), tabs.end());
        if (tabs.empty())
            continue;
        ss << (classes++ ? "," : "") << "{\"class\":" << uint32(cls) << ",\"tabs\":[";
        for (size_t t = 0; t < tabs.size(); ++t)
        {
            int32 minX = tabs[t].front().x;
            std::sort(tabs[t].begin(), tabs[t].end(), [](Placed const& a, Placed const& b) { return a.row != b.row ? a.row < b.row : a.x < b.x; });
            ss << (t ? "," : "") << "[";
            for (size_t i = 0; i < tabs[t].size(); ++i)
            {
                Placed const& p = tabs[t][i];
                TraitDefinitionEntry const* def = sTraitDefinitionStore.LookupEntry(p.def);
                uint32 const spellId = def ? uint32(def->SpellID) : 0;
                std::string name = def ? LocName(def->OverrideName) : "";
                SpellInfo const* info = spellId ? sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE) : nullptr;
                if (name.empty() && info)
                    name = Escape(Loc(info->SpellName));
                int32 const icon = def && def->OverrideIcon ? def->OverrideIcon : info ? int32(info->IconFileDataId) : 0;
                ss << (i ? "," : "") << "{\"node\":" << p.node << ",\"entry\":" << p.entry << ",\"spell\":" << spellId << ",\"name\":\"" << name
                   << "\",\"icon\":" << icon << ",\"max\":" << p.max << ",\"row\":" << p.row << ",\"col\":" << int32(std::lround(double(p.x - minX) / 600.0)) << "}";
                ++n;
            }
            ss << "]";
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
        out << (n++ ? "," : "") << "{\"id\":" << map->ID << ",\"name\":\"" << Escape(Loc(&map->Name)) << "\",\"parent\":"
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
            << ",\"buy\":" << item.GetBuyPrice() << ",\"icon\":" << IconOf(id) << ",\"display_id\":" << DisplayOf(id) << ItemDetails(item) << "}";
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
    std::lock_guard lock(g_feedLock);
    if (IsBot(killer) && killed)
    {
        QueueEvent(killer, "Kill", killed->GetName(), std::to_string(killed->GetEntry()));
        if (RunRec* run = RunOf(killed))
            if (killed->IsDungeonBoss() || std::find(run->bosses.begin(), run->bosses.end(), killed->GetName()) != run->bosses.end())
                run->killedAt.try_emplace(killed->GetName(), uint32(time(nullptr) - run->started));
    }
}

void OnKilledByCreature(Creature* killer, Player* killed)
{
    std::lock_guard lock(g_feedLock);
    if (!IsBot(killed) || !Get().enabled)
        return;
    std::string const name = killer ? killer->GetName() : "";
    SendAnomaly(killed, "BOT_DEATH", "WARN", name, killer ? "killed by " + name + " (level " + std::to_string(killer->GetLevel()) + ")" : "died");
    QueueEvent(killed, "BotDeath", name, "");
    if (RunRec* run = RunOf(killed))
    {
        auto brain = Get().brains.find(killed->GetGUID());
        run->deaths.push_back({ uint32(time(nullptr) - run->started), killed->GetPositionX(), killed->GetPositionY(),
            brain != Get().brains.end() ? brain->second.role : "" });
    }
    if (FightAcc* fight = FightOf(killed))
    {
        FightBotAcc& acc = fight->bots[killed->GetGUID()];
        ++acc.deaths;
        fight->deaths.push_back({ killed->GetName(), name, FightMs(*fight), killed->GetPositionX(), killed->GetPositionY(), killed->GetPositionZ(), acc.lastHits });
    }
    if (auto itr = Get().track.find(killed->GetGUID()); itr != Get().track.end())
        itr->second.alive = false;
}

void OnDamage(Unit* attacker, Unit* victim, uint32 damage, SpellInfo const* spell)
{
    std::lock_guard lock(g_feedLock);
    if (!damage || !Get().enabled)
        return;
    Player* atk = attacker ? attacker->ToPlayer() : nullptr;
    Player* vic = victim ? victim->ToPlayer() : nullptr;
    if (atk && IsBot(atk))
        if (FightAcc* fight = FightOf(atk))
        {
            fight->bots[atk->GetGUID()].dmg += damage;
            NoteEnemy(*fight, victim);
        }
    if (vic && IsBot(vic))
        if (FightAcc* fight = FightOf(vic))
        {
            FightBotAcc& acc = fight->bots[vic->GetGUID()];
            acc.taken += damage;
            std::string name = attacker ? attacker->GetName() : "";
            if (spell)
                name += std::string(" (") + Loc(spell->SpellName) + ")";
            acc.lastHits.push_back({ name, damage, FightMs(*fight) });
            if (acc.lastHits.size() > 5)
                acc.lastHits.erase(acc.lastHits.begin());
            NoteEnemy(*fight, attacker);
        }
}

void OnHealOutcome(Unit* healer, uint32 raw, uint32 effective)
{
    std::lock_guard lock(g_feedLock);
    Player* bot = healer ? healer->ToPlayer() : nullptr;
    if (!bot || !IsBot(bot) || !Get().enabled)
        return;
    if (FightAcc* fight = FightOf(bot))
    {
        FightBotAcc& acc = fight->bots[bot->GetGUID()];
        acc.heal += effective;
        acc.over += raw > effective ? raw - effective : 0;
    }
}

void OnSpellCast(Player* player, SpellInfo const* spell)
{
    std::lock_guard lock(g_feedLock);
    if (!spell || !IsBot(player) || !Get().enabled)
        return;
    if (FightAcc* fight = FightOf(player))
    {
        FightBotAcc& acc = fight->bots[player->GetGUID()];
        ++acc.casts;
        if (spell->IsPositive() && spell->HasEffect(SPELL_EFFECT_HEAL))
            ++acc.healCasts;
        ++acc.spells[Loc(spell->SpellName)];
    }
}

// A money change (corpse money, vendor, repairs, trainers): the event carries the new balance, from which the
// dashboard counts money earned and spent. The hook runs before the change, so the balance is computed here.
void OnMoneyChanged(Player* player, int64 amount)
{
    std::lock_guard lock(g_feedLock);
    if (!IsBot(player) || !amount)
        return;
    int64 const after = std::max<int64>(0, int64(player->GetMoney()) + amount);
    QueueEvent(player, "MoneyChanged", std::to_string(amount), "", ",\"money\":" + std::to_string(after));
}

// A looted item stored in the bot's bags: the loot feed and the loot counters (item, quality, vendor value of the stack).
void OnLootItem(Player* player, Item* item)
{
    std::lock_guard lock(g_feedLock);
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
    std::lock_guard lock(g_feedLock);
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

// The engine's bot_events rows, narrowed to what the dashboard's activity rollup reads. Quests, kills, deaths and loot
// already arrive through the core hooks above; the rest (vendor, trainer, repair, auction, mail, revives, PvP) only exist here.
void OnBotActivity(std::string const& event, std::string const& info1, std::string const& info2, Player* bot, uint32 /*targetLevel*/)
{
    std::lock_guard lock(g_feedLock);
    if (!IsBot(bot) || !Get().enabled)
        return;
    static std::set<std::string> const whitelist = {
        // quests
        "QuestRewarded", "AcceptQuestAction", "AcceptQuestShareAction", "TalkToQuestGiverAction", "QuestUpdateCompleteAction",
        "QuestCompleted", "QuestDropped",
        // loot & money
        "StoreLootAction", "GatherLoot", "LootMoney", "RollWon",
        // vendor / trainer / repair / auction
        "SellAction", "BuyAction", "RepairAllAction", "TrainerAction", "NearbyService", "AhAction", "AhBidAction",
        // guild orders: auction buys and relists for the guild, mail taken
        "GuildAhBuy", "GuildAhRelist", "GuildAhRelistFailed", "MailAction",
        // deaths, revives (ghost time) and give-ups
        "BotDeath", "ReviveFromCorpseAction", "ReviveFromSpiritHealerAction", "RepopAction", "LongStuckFallback", "ReachGiveUp",
        // explicit kill signal from XpGainAction
        "Kill",
        // open-world PvP seek: role grants, engagements, kills, deaths
        "WorldPvpSeekGrant", "WorldPvpEngage", "WorldPvpKill", "WorldPvpDeath",
    };
    // Sent by the core hooks already; forwarding them too would count every kill, death, loot and quest twice.
    static std::set<std::string> const covered = {
        "Kill", "BotDeath", "StoreLootAction", "AcceptQuestAction", "QuestCompleted", "QuestRewarded", "QuestDropped",
        "QuestUpdateCompleteAction",
    };
    if (!whitelist.count(event) || covered.count(event))
        return;

    static std::set<std::string> const itemEvents = {
        "StoreLootAction", "GatherLoot", "SellAction", "BuyAction", "AhAction", "AhBidAction", "RollWon",
    };
    std::string extra;
    if (itemEvents.count(event))
        if (uint32 const itemId = ParseItemId(info2))
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId))
            {
                std::ostringstream e;
                e << ",\"item_id\":" << itemId << ",\"quality\":" << proto->GetQuality() << ",\"sell\":" << proto->GetSellPrice()
                  << ",\"buy\":" << proto->GetBuyPrice();
                extra = e.str();
            }
    QueueEvent(bot, event.c_str(), info1, info2, extra);
}

void OnActionFailed(Player* bot, std::string const& actionName, std::string const& targetName, std::string const& strategy)
{
    std::lock_guard lock(g_feedLock);
    Sender& s = Get();
    if (!s.enabled || !IsBot(bot) || actionName.empty())
        return;
    uint32 const nowMs = GameTime::GetGameTimeMS();
    std::string const key = std::to_string(bot->GetGUID().GetCounter()) + "|" + actionName;
    Sender::ActionFailure& rec = s.actionFailures[key];
    if (rec.firstMs == 0 || nowMs - rec.firstMs > 2000)
    {
        rec.firstMs = nowMs;
        rec.count = 1;
        rec.reported = false;
    }
    else
        ++rec.count;
    // One event per bot/action at most every 5 min; the failures keep coming every tick while the loop lasts.
    if (rec.count >= 5 && !rec.reported && (rec.lastReportMs == 0 || nowMs - rec.lastReportMs > 300000))
    {
        rec.reported = true;
        rec.lastReportMs = nowMs;
        TortoiseBots::engine::Telemetry telemetry;
        std::string activeStrategy = strategy;
        if (activeStrategy.empty() && TortoiseBots::engine::ReadTelemetry(bot, telemetry, true))
            activeStrategy = telemetry.combatStrategies;
        SendAnomaly(bot, "ACTION_LOOP", "WARN", targetName, "Action '" + actionName + "' failed >= 5 times within 2 seconds",
            activeStrategy, actionName);
    }
    if (s.actionFailures.size() > 4096)
        s.actionFailures.clear();
}
}
