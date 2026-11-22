// BG1 (BATTLEGROUNDS-SPEC.md, rounds BG1 and decisions D4/D5/D6/D11), see host/bg/TbBgQueue.h. World thread only.
//
// The module never touches a queue structure: a bot is queued through WorldSession::JoinBattlegroundQueue, answers an invitation
// through HandleBattleFieldPortOpcode, leaves through Player::LeaveBattleground and is taken out of a queue with the same
// BattlegroundQueue::RemovePlayer + RemoveBattlegroundQueueId pair the core uses. A new instance is only ever started by the core;
// WorldScript::CanCreateBattleground merely answers whether it may start it yet. The decisions are the pure headers under
// ai/playerbot/policy/bg/ (tested by forever/tools/test_bg*_policy.cpp).
#include "host/bg/TbBgQueue.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "host/bg/TbBgMapData.h"
#include "TbBotMgr.h"

#include "ai/playerbot/policy/bg/BgIds.h"
#include "ai/playerbot/policy/bg/BgJoinLeavePolicy.h"
#include "ai/playerbot/policy/bg/BgLimits.h"
#include "ai/playerbot/policy/bg/BgQueuePolicy.h"
#include "runtime/policy/pvp/HumanBgDemandPolicy.h"

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MiscPackets.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldStateMgr.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "host/run/TbRun.h"
#endif

namespace TortoiseBots
{
namespace bg
{
namespace
{

ObjectGuid PlayerGuidOf(std::uint64_t guid)
{
    return ObjectGuid::Create<HighGuid::Player>(std::uint32_t(guid));
}

std::uint32_t NowMs()
{
    return GameTime::GetGameTimeMS();
}

// A bot a live dungeon run drives: a queued free bot a run took leaves the queue (D5). Runs exist only in the engine build.
bool InRun(ObjectGuid guid)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    for (run::RunRec* rec : run::All())
    {
        if (!rec || rec->stopped || rec->run.state == DungeonRun::State::Done)
            continue;
        if (rec->leader == guid || rec->bound.count(guid) != 0)
            return true;
    }
#else
    (void)guid;
#endif
    return false;
}

// One bot the module is following through a queue or a match.
struct Track
{
    std::string name;
    std::uint32_t listId = 0;
    std::uint32_t bracket = 0;          // the core's bracket id the bot queued in
    bool weQueued = false;              // the module queued it (only those are cleaned out of a queue)
    bool ours = false;                  // a bot the module holds for a battleground (D9 caps): a free bot it queued or accepted
    ObjectGuid follower;                // a bound bot's human: its master, or the group leader that queued its group
    ObjectGuid masterAtQueue;           // the master when the group queued it: a later unbind ends the bot's queue entry
    std::vector<ObjectGuid> serves;     // the humans whose queue the module queued this bot for (D9: its reservation)
    bool invited = false;
    std::uint32_t inviteDueMs = 0;
    std::uint32_t inviteDeadlineMs = 0; // the core's RemoveInviteTime for this invitation
    std::uint32_t invitedAtMs = 0;
    std::uint32_t queuedAtMs = 0;
    bool inside = false;
    std::uint32_t instance = 0;
    std::uint32_t insideSinceMs = 0;
    std::uint32_t leaveDueMs = 0;       // STATUS_WAIT_LEAVE: when the 10-30 s roll is up
    bool leaving = false;               // a LeaveBattleground was sent for this stay
    bool havePos = false;
    float x = 0, y = 0, z = 0;
    std::uint32_t lastMoveMs = 0;
    bool released = false;              // the release (CMSG_REPOP_REQUEST) was sent for this death
    std::uint32_t healerRetryMs = 0;    // the next walk to a spirit healer out of queue range
    std::uint32_t orphanSinceMs = 0;    // first look with no human in its bucket; 0 = a human is (or was just) waiting
    std::uint32_t masterAbsentSinceMs = 0;  // a bound bot inside: first look with its master not in the instance
};

struct BgEvent
{
    std::uint32_t seq = 0;
    std::uint32_t atMs = 0;
    std::string kind;
    std::string bot;
    std::string map;
    std::string team;
    std::uint32_t instance = 0;
    std::int32_t flag = -1;
    std::int32_t node = -1;
    std::string why;
};

struct Deficit
{
    std::uint32_t sinceMs = 0;
    bool told = false;
};

struct State
{
    bool configured = false;
    bool enabled = false;
    bool mapOn[2] = { true, true };      // index 0 = WSG, 1 = AB (BgMapKind - 1)
    bool fillHumanQueues = true;
    bool autonomous = false;
    std::uint32_t minPerTeam = 5;
    std::uint32_t maxInstances = 2;
    std::uint32_t maxBots = 50;
    std::uint32_t maxMinutes = 45;
    std::uint32_t accMs = 0;
    std::uint32_t fillAccMs = 0;
    std::unordered_map<std::uint64_t, Track> bots;
    std::deque<BgEvent> events;
    std::uint32_t nextSeq = 1;
    std::map<std::uint64_t, Deficit> deficits;
};

State& S()
{
    static State state;
    return state;
}

std::uint64_t DeficitKey(std::uint32_t listId, std::uint32_t bracket, std::uint32_t side)
{
    return (std::uint64_t(listId) << 40) | (std::uint64_t(bracket) << 8) | side;
}

char const* TeamWord(Team team)
{
    return team == ALLIANCE ? "alliance" : "horde";
}

char const* StatusWord(std::int32_t status)
{
    switch (status)
    {
        case 1: return "wait-queue";
        case 2: return "wait-join";
        case 3: return "in-progress";
        case 4: return "wait-leave";
        default: return "none";
    }
}

std::string LevelRange(std::uint32_t mapId, std::uint32_t level)
{
    PVPDifficultyEntry const* entry = DB2Manager::GetBattlegroundBracketByLevel(mapId, level);
    if (!entry)
        return "-";
    return std::to_string(entry->MinLevel) + "-" + std::to_string(entry->MaxLevel);
}

void LogEventImpl(char const* kind, Track const& track, char const* why, char const* team = "", std::uint32_t instance = 0,
                  std::int32_t flag = -1, std::int32_t node = -1)
{
    State& s = S();
    BgEvent event;
    event.seq = s.nextSeq++;
    event.atMs = NowMs();
    event.kind = kind;
    event.bot = track.name;
    event.map = BgMapName(track.listId);
    event.team = team;
    event.instance = instance ? instance : track.instance;
    event.flag = flag;
    event.node = node;
    event.why = why ? why : "";
    s.events.push_back(std::move(event));
    while (s.events.size() > 1024)
        s.events.pop_front();
}

void LogEvent(char const* kind, Track const& track, char const* why, char const* team = "", std::uint32_t instance = 0,
              std::int32_t flag = -1, std::int32_t node = -1)
{
    LogEventImpl(kind, track, why, team, instance, flag, node);
}

std::string EventLine(BgEvent const& event)
{
    std::string const flag = event.flag >= 0 ? std::to_string(event.flag) : "-";
    std::string const node = event.node >= 0 ? std::to_string(event.node) : "-";
    return Trinity::StringFormat("bg event seq={} t={} kind={} bot={} map={} team={} instance={} flag={} node={} why={}",
        event.seq, event.atMs / 1000, event.kind, event.bot, event.map, event.team, event.instance, flag, node, event.why);
}

std::uint32_t TeamIndex(Team team)
{
    return team == ALLIANCE ? 0 : 1;
}

std::uint32_t SideTeam(std::uint32_t side)
{
    return side == 0 ? TEAM_ALLIANCE : TEAM_HORDE;
}

BattlegroundQueueTypeId QueueIdOf(std::uint32_t listId)
{
    return BattlegroundMgr::BGQueueTypeId(uint16(listId), BattlegroundQueueIdType::Battleground, false, 0);
}

// The humans a module-queued bot waits for are all in a match of this list already: inside it, or invited to it in the core's
// queue. The entry is then a leftover the core fills that match from, not a reservation for the next one.
bool DemandInInstance(std::vector<ObjectGuid> const& humans, std::uint32_t listId)
{
    if (humans.empty())
        return false;
    BattlegroundQueueTypeId const queueId = QueueIdOf(listId);
    for (ObjectGuid const human : humans)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(human);
        if (!player)
            return false;
        Battleground* battleground = player->GetBattleground();
        if (battleground && battleground->isBattleground() && std::uint32_t(battleground->GetTypeID()) == listId)
            continue;
        if (player->GetBattlegroundQueueIndex(queueId) >= PLAYER_MAX_BATTLEGROUND_QUEUES)
            return false;
        GroupQueueInfo info{};
        if (!sBattlegroundMgr->GetBattlegroundQueue(queueId).GetPlayerGroupInfoData(human, &info) ||
            !info.IsInvitedToBGInstanceGUID)
            return false;
    }
    return true;
}

// D9: the caps count every bot the module holds (queued, invited, inside), each match once. Reading only `InBattleground()` let
// one 5 s pass queue a match's worth of bots while both caps still read 0 used; counting an invited bot's bucket next to its
// instance made one match two.
std::vector<BgTrackFact> TrackFacts()
{
    std::vector<BgTrackFact> facts;
    facts.reserve(S().bots.size());
    for (auto const& [guid, track] : S().bots)
    {
        if (!track.ours)
            continue;                                   // a bound bot's group queue entry is its human's, not the module's
        BgTrackFact fact;
        fact.listId = track.listId;
        fact.bracket = track.bracket;
        fact.instance = track.instance;
        if (track.inside)
            fact.state = BgTrackState::Inside;
        else if (track.invited)
            fact.state = BgTrackState::Invited;
        else
        {
            fact.state = BgTrackState::Queued;
            fact.demandPlaced = DemandInInstance(track.serves, track.listId);
        }
        facts.push_back(fact);
    }
    // A bot inside a battleground the module never queued (normal play has none) still counts.
    for (ObjectGuid const guid : BotMgr::Instance().BotGuids())
    {
        if (S().bots.count(guid.GetCounter()))
            continue;
        Player* player = BotMgr::Instance().FindBot(guid);
        if (!player || !player->InBattleground())
            continue;
        Battleground* battleground = player->GetBattleground();
        if (!battleground || !battleground->isBattleground())
            continue;
        BgTrackFact fact;
        fact.listId = std::uint32_t(battleground->GetTypeID());
        fact.bracket = std::uint32_t(battleground->GetBracketId());
        fact.instance = battleground->GetInstanceID();
        fact.state = BgTrackState::Inside;
        facts.push_back(fact);
    }
    return facts;
}

BgQueueCaps CapsOfCurrentWorld()
{
    return BgCapsOfTracks(TrackFacts(), S().maxInstances, S().maxBots);
}

void FillFacts(Player* player, BgMapInfo const* map, PVPDifficultyEntry const* bracket, BgQueueCaps const& caps,
               BgQueueFacts& facts)
{
    facts.pool = BotMgr::Instance().IsPoolBot(player->GetGUID());
    facts.free = BotMgr::Instance().IsFreeBot(player->GetGUID());
    facts.alive = player->IsAlive();
    facts.inBattleground = player->InBattleground();
    facts.inQueue = player->InBattlegroundQueue();
    facts.inLfg = player->isUsingLfg();
    facts.inRun = InRun(player->GetGUID());
    facts.deserter = player->IsDeserter();
    facts.inCombat = player->IsInCombat();
    facts.caps = caps;
    if (map && bracket && map->mapId)
    {
        PVPDifficultyEntry const* own = DB2Manager::GetBattlegroundBracketByLevel(map->mapId, player->GetLevel());
        facts.inBracket = own && own->GetBracketId() == bracket->GetBracketId();
    }
    else
        facts.inBracket = false;
}

// The bracket entry of a queued bot, read again from its level (the core does the same when it accepts an invite).
PVPDifficultyEntry const* BracketOf(BgMapInfo const* map, Player* player)
{
    if (!map || !map->mapId)
        return nullptr;
    return DB2Manager::GetBattlegroundBracketByLevel(map->mapId, player->GetLevel());
}

bool SendPort(Player* player, std::uint32_t listId, bool accept)
{
    WorldSession* session = player->GetSession();
    if (!session)
        return false;
    BattlegroundQueueTypeId const queueId =
        BattlegroundMgr::BGQueueTypeId(uint16(listId), BattlegroundQueueIdType::Battleground, false, 0);
    std::uint32_t const slot = player->GetBattlegroundQueueIndex(queueId);
    if (slot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
        return false;
    WorldPackets::Battleground::BattlefieldPort packet{ WorldPacket(CMSG_BATTLEFIELD_PORT) };
    packet.Ticket.RequesterGuid = player->GetGUID();
    packet.Ticket.Id = slot;
    packet.Ticket.Type = WorldPackets::LFG::RideType::Battlegrounds;
    packet.Ticket.Time = player->GetBattlegroundQueueJoinTime(queueId);
    packet.AcceptedInvite = accept;
    session->HandleBattleFieldPortOpcode(packet);
    return true;
}

// D5: the core's own pair, as BattleGroundHandler.cpp:575-582 does it. The queue is re-evaluated at once: the removed group must
// not stay matched into a selection pool.
void RemoveFromQueue(Player* player, Track& track)
{
    for (std::uint8_t i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
    {
        BattlegroundQueueTypeId const queueId = player->GetBattlegroundQueueTypeId(i);
        if (queueId == BATTLEGROUND_QUEUE_NONE || queueId.BattlemasterListId != track.listId)
            continue;
        sBattlegroundMgr->GetBattlegroundQueue(queueId).RemovePlayer(player->GetGUID(), true);
        player->RemoveBattlegroundQueueId(queueId);
    }
    track.leaving = true;
    sBattlegroundMgr->ScheduleQueueUpdate(0,
        BattlegroundMgr::BGQueueTypeId(uint16(track.listId), BattlegroundQueueIdType::Battleground, false, 0),
        BattlegroundBracketId(track.bracket));
}

// D6: release at once, then queue at the nearest area spirit healer (the core sets Waiting to Resurrect and revives in waves).
void SpiritHealer(Player* player, Track& track, std::uint32_t now)
{
    std::list<Creature*> creatures;
    FindCreatureOptions options;
    options.IsAlive = FindCreatureAliveState::Alive;
    player->GetCreatureListWithOptionsInGrid(creatures, kSpiritHealerSearchYd, options);
    Creature* nearest = nullptr;
    float nearestYd = 0;
    for (Creature* creature : creatures)
    {
        if (!creature || !creature->IsAreaSpiritHealer())
            continue;
        float const yd = player->GetDistance(creature);
        if (!nearest || yd < nearestYd)
        {
            nearest = creature;
            nearestYd = yd;
        }
    }
    if (!nearest)
        return;
    if (nearestYd > kSpiritHealerQueueYd)
    {
        if (now < track.healerRetryMs)
            return;
        track.healerRetryMs = now + kSpiritHealerWalkRetryMs;
        player->GetMotionMaster()->MovePoint(0, nearest->GetPosition(), true);
        return;
    }
    // The core owns "waiting at a healer": SetAreaSpiritHealer applies SPELL_WAITING_FOR_RESURRECT and ResurrectPlayer clears it,
    // so the GUID says whether the request took. An out-of-range or despawned healer leaves it empty and is retried.
    if (!player->GetSpiritHealerGUID().IsEmpty())
        return;
    if (WorldSession* session = player->GetSession())
    {
        WorldPackets::Battleground::AreaSpiritHealerQueue packet{ WorldPacket(CMSG_AREA_SPIRIT_HEALER_QUEUE) };
        packet.HealerGuid = nearest->GetGUID();
        session->HandleAreaSpiritHealerQueueOpcode(packet);
        if (!player->GetSpiritHealerGUID().IsEmpty())
            TC_LOG_INFO("tortoisebots", "TortoiseBots bg: {} queues at the area spirit healer {:.0f} yd away", player->GetName(),
                nearestYd);
        else
            track.healerRetryMs = now + kSpiritHealerWalkRetryMs;
    }
}

// The human a bound bot follows: its bound master, or the group leader that queued its group while the engine has not bound it.
ObjectGuid HumanToFollow(ObjectGuid botGuid, Track const& track)
{
    ObjectGuid const master = BotMgr::Instance().MasterOf(botGuid);
    return master.IsEmpty() ? track.follower : master;
}

// The human has ENTERED the instance: the core adds a player to Battleground::GetPlayers() at the world-port ack, after
// SetBattlegroundId. An invitation (or a player still porting) is not enough (D4.1: the bot only enters with its human).
bool MasterEnteredInstance(ObjectGuid botGuid, Track const& track)
{
    ObjectGuid const human = HumanToFollow(botGuid, track);
    if (human.IsEmpty())
        return false;
    Player* master = ObjectAccessor::FindConnectedPlayer(human);
    if (!master || master->GetBattlegroundId() != track.instance)
        return false;
    Battleground* battleground = master->GetBattleground();
    return battleground && battleground->GetPlayers().count(human) != 0;
}

// A human's own battleground as (list, instance): what BgHumanInTrackMatch compares with the bot's track. (0, 0) when he is in
// none, so an arena or a missing battleground holds no queue.
std::pair<std::uint32_t, std::uint32_t> HumanBattleground(Player* human)
{
    Battleground* battleground = human ? human->GetBattleground() : nullptr;
    if (!battleground || !battleground->isBattleground())
        return { 0, 0 };
    return { std::uint32_t(battleground->GetTypeID()), battleground->GetInstanceID() };
}

// D5: a bound bot's queue entry stays only while its human is connected and still holds the same queue, or has entered THE match
// the bot queued for. A human who leaves the queue, cancels, logs out or ungroups leaves nothing for the core to take the bot
// out with; a human who went into another battleground left this queue too.
bool FollowerHoldsQueue(ObjectGuid botGuid, Track const& track)
{
    ObjectGuid const human = HumanToFollow(botGuid, track);
    Player* master = human.IsEmpty() ? nullptr : ObjectAccessor::FindConnectedPlayer(human);
    bool inQueue = false;
    if (master && !master->InBattleground() && master->InBattlegroundQueue())
    {
        BattlegroundQueueTypeId const queueId = QueueIdOf(track.listId);
        if (master->GetBattlegroundQueueIndex(queueId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
        {
            BgMapInfo const* map = BgMapByList(track.listId);
            PVPDifficultyEntry const* own = map && map->mapId
                ? DB2Manager::GetBattlegroundBracketByLevel(map->mapId, master->GetLevel()) : nullptr;
            inQueue = own && std::uint32_t(own->GetBracketId()) == track.bracket;
        }
    }
    auto const [humanList, humanInstance] = HumanBattleground(master);
    bool const inTrackMatch = BgHumanInTrackMatch(humanList, humanInstance, track.listId, track.instance);
    return BgBoundQueueHeld(!human.IsEmpty(), master != nullptr, inQueue, inTrackMatch);
}

// Does the instance already hold a human, inside it or already porting into it? A free bot declines an invite into an instance
// whose only players would be bots. The human who accepted can be in the second between his accept and his world-port ack, when
// Battleground::GetPlayers() does not list him yet but his GetBattlegroundId() already names the instance.
bool HumanInInstance(std::uint32_t listId, std::uint32_t instance)
{
    if (!instance)
        return false;
    for (auto const& [accountId, session] : sWorld->GetAllSessions())
    {
        if (!session || session->IsBot())
            continue;
        Player* player = session->GetPlayer();
        if (!player || !player->InBattleground())
            continue;
        Battleground* battleground = player->GetBattleground();
        if (battleground && battleground->GetInstanceID() == instance && std::uint32_t(battleground->GetTypeID()) == listId)
            return true;
    }
    return false;
}

// The refusals that still matter at the moment an invitation comes due. The invitation only enters an instance that already
// exists, so only Bg.MaxBots still applies here, never the instance cap. A bound bot's human is the invitation rule's own
// business (BgHumanEntryInviteAnswer); a run or a match it is already in declines either way.
bool AcceptUsable(Player* player, Track& track)
{
    BgMapInfo const* map = BgMapByList(track.listId);
    PVPDifficultyEntry const* bracket = BracketOf(map, player);
    BgQueueCaps const caps = CapsOfCurrentWorld();
    BgQueueFacts facts;
    FillFacts(player, map, bracket, caps, facts);
    facts.inQueue = false;                       // it is queued: that is how the invitation found it
    if (BgAcceptCapsOver(caps))
        return false;
    if (BotMgr::Instance().IsFreeBot(player->GetGUID()))
        return BgAcceptEligible(facts);
    return !facts.inRun && !facts.inBattleground;
}

void TickInside(Player* player, Track& track, std::uint32_t now)
{
    Battleground* battleground = player->GetBattleground();
    if (!battleground || !battleground->isBattleground())
        return;                                      // an arena is not this round's business
    std::int32_t const status = std::int32_t(battleground->GetStatus());
    std::string const name = player->GetName();

    if (!track.inside)
    {
        track.inside = true;
        track.instance = battleground->GetInstanceID();
        track.listId = battleground->GetTypeID();
        track.insideSinceMs = now;
        track.invited = false;
        track.inviteDueMs = 0;
        track.inviteDeadlineMs = 0;
        track.invitedAtMs = 0;
        track.leaveDueMs = 0;
        track.leaving = false;
        track.released = false;
        track.masterAbsentSinceMs = 0;
        track.havePos = false;
        track.name = name;
        LogEventImpl("join", track, "", TeamWord(player->GetBGTeam()), track.instance);
    }

    // D6: a dead bot in a battleground does not corpse-run (W1 is off here): it releases at once and waits at the spirit healer.
    if (!player->IsAlive())
    {
        if (!player->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        {
            if (!track.released)
            {
                if (WorldSession* session = player->GetSession())
                {
                    WorldPackets::Misc::RepopRequest packet{ WorldPacket(CMSG_REPOP_REQUEST) };
                    session->HandleRepopRequest(packet);
                    // The handler returns silently while the bot is being teleported or a prevent-resurrection aura is up; only
                    // the ghost flag says the release took, and anything else is retried next second.
                    track.released = player->HasPlayerFlag(PLAYER_FLAGS_GHOST);
                }
            }
        }
        else
            SpiritHealer(player, track, now);
    }
    else
        track.released = false;

    if (!track.havePos)
    {
        track.x = player->GetPositionX();
        track.y = player->GetPositionY();
        track.z = player->GetPositionZ();
        track.havePos = true;
        track.lastMoveMs = now;
    }
    else
    {
        float const dx = player->GetPositionX() - track.x;
        float const dy = player->GetPositionY() - track.y;
        float const dz = player->GetPositionZ() - track.z;
        if (dx * dx + dy * dy + dz * dz > kMoveSampleYd * kMoveSampleYd)
        {
            track.x = player->GetPositionX();
            track.y = player->GetPositionY();
            track.z = player->GetPositionZ();
            track.lastMoveMs = now;
        }
    }

    if (status == STATUS_WAIT_LEAVE && !track.leaveDueMs)
        track.leaveDueMs = now + BgMatchOverLeaveDelayMs(rand32());
    bool const matchOverDone = track.leaveDueMs && now >= track.leaveDueMs;
    bool const free = BotMgr::Instance().IsFreeBot(player->GetGUID());
    if (!free)
    {
        // A bound bot's own leave comes from the core when its human leaves; when the human never entered (a lapsed invite, a
        // logout, a cancel after the bot accepted) nothing would ever take it out of a match it sits in alone.
        bool const masterHere = MasterEnteredInstance(player->GetGUID(), track);
        if (masterHere)
            track.masterAbsentSinceMs = 0;
        else if (!track.masterAbsentSinceMs)
            track.masterAbsentSinceMs = now ? now : 1;
        std::uint32_t const away = track.masterAbsentSinceMs ? now - track.masterAbsentSinceMs : 0;
        if (!track.leaving && BgMasterLeave(false, masterHere, away))
        {
            player->LeaveBattleground(true, /*withoutDeserterDebuff*/ true);
            LogEvent("leave", track, "master-gone", TeamWord(player->GetBGTeam()));
            track.leaving = true;
        }
        return;
    }
    BgLeaveAction const action = BgLeaveDecision(free, status, matchOverDone, now - track.insideSinceMs,
        S().maxMinutes * 60000u);
    if (!track.leaving && action != BgLeaveAction::Stay)
    {
        if (action == BgLeaveAction::LeaveMatchOver)
        {
            player->LeaveBattleground();                 // the core casts no Deserter in STATUS_WAIT_LEAVE
            LogEvent("leave", track, "match-over", TeamWord(player->GetBGTeam()));
        }
        else
        {
            player->LeaveBattleground(true, /*withoutDeserterDebuff*/ true);
            LogEvent("leave", track, "max-minutes", TeamWord(player->GetBGTeam()));
        }
        track.leaving = true;
    }
}

void TickQueued(Player* player, Track& track, std::uint32_t now, bool humanWaiting)
{
    ObjectGuid const botGuid = player->GetGUID();
    bool const free = BotMgr::Instance().IsFreeBot(botGuid);
    BgMapInfo const* map = BgMapByList(track.listId);
    PVPDifficultyEntry const* bracket = BracketOf(map, player);
    BattlegroundQueueTypeId const queueId = QueueIdOf(track.listId);

    // D5: a bot its human's group queued follows that human, also out of the queue: the core's own leave runs only on the
    // human's CMSG_BATTLEFIELD_LEAVE inside a match, so a cancel, a logout or an ungroup would otherwise leave the bot queued
    // with no human and no end. An unbound bot (its master changed or went away) has been let go and leaves too.
    if (!track.weQueued)
    {
        ObjectGuid const masterNow = BotMgr::Instance().MasterOf(botGuid);
        bool const unbound = !track.masterAtQueue.IsEmpty() && masterNow != track.masterAtQueue;
        bool const followsHuman = unbound || !free;
        if ((unbound || (followsHuman && !FollowerHoldsQueue(botGuid, track))))
        {
            RemoveFromQueue(player, track);
            LogEvent("leave", track, "master-gone", TeamWord(player->GetTeam()));
            TC_LOG_INFO("tortoisebots", "TortoiseBots bg: {} leaves the {} queue (its human is gone)", player->GetName(),
                BgMapName(track.listId));
            return;
        }
    }

    if (track.invited)
    {
        if (!player->InBattlegroundQueueForBattlegroundQueueType(queueId))
            return;                                  // the core removed the entry: the next pass erases the track
        bool const usable = AcceptUsable(player, track);
        bool const acceptDue = now >= track.inviteDueMs;
        bool const deadlinePassed = BgInviteDeadlinePassed(now, track.inviteDeadlineMs);
        bool const humanInside = free ? HumanInInstance(track.listId, track.instance)
                                      : MasterEnteredInstance(botGuid, track);
        BgInviteReply const answer = BgHumanEntryInviteAnswer(usable, humanInside, acceptDue, deadlinePassed);
        if (answer == BgInviteReply::Wait)
            return;
        bool const accept = answer == BgInviteReply::Accept;
        track.invited = false;
        track.inviteDueMs = 0;
        track.inviteDeadlineMs = 0;
        track.invitedAtMs = 0;
        if (!SendPort(player, track.listId, accept))
        {
            track.leaving = true;                    // the queue slot is gone: nothing to answer
            LogEvent("leave", track, "no-slot");
            return;
        }
        if (!accept)
        {
            track.leaving = true;                    // the decline takes the queue entry with it
            LogEvent("leave", track, "declined");
            TC_LOG_INFO("tortoisebots", "TortoiseBots bg: {} declined its invitation ({})", player->GetName(),
                free ? "no human in the instance" : "its human did not enter");
        }
        else
            TC_LOG_INFO("tortoisebots", "TortoiseBots bg: {} accepted its invitation (map {})", player->GetName(),
                BgMapName(track.listId));
        return;
    }

    if (!track.weQueued)
        return;                                     // a human's group put it here and the human still holds it
    BgQueueCaps const caps{};
    BgQueueFacts facts;
    FillFacts(player, map, bracket, caps, facts);
    if (BgMustLeaveQueue(facts))
    {
        char const* why = !facts.free ? "bound" : (facts.inRun ? "run" : "left-pool");
        RemoveFromQueue(player, track);
        LogEvent("leave", track, why, TeamWord(player->GetTeam()));
        TC_LOG_INFO("tortoisebots", "TortoiseBots bg: {} leaves the {} queue ({})", player->GetName(),
            BgMapName(track.listId), why);
        return;
    }
    // A module bot waits only while its bucket still has a human, and not forever either way: a queue entry with no human left
    // could otherwise be matched into a bot-only instance, and a human's own cancel would leave the pool holding its session.
    if (humanWaiting)
        track.orphanSinceMs = 0;
    else if (!track.orphanSinceMs)
        track.orphanSinceMs = now ? now : 1;
    std::uint32_t const orphanMs = track.orphanSinceMs ? now - track.orphanSinceMs : 0;
    std::uint32_t const queuedMs = track.queuedAtMs ? now - track.queuedAtMs : 0;
    BgQueueDrop const drop = BgQueuedDrop(humanWaiting, orphanMs, queuedMs);
    if (drop == BgQueueDrop::None)
        return;
    RemoveFromQueue(player, track);
    LogEvent("leave", track, BgQueueDropWhy(drop), TeamWord(player->GetTeam()));
    TC_LOG_INFO("tortoisebots", "TortoiseBots bg: {} leaves the {} queue ({})", player->GetName(),
        BgMapName(track.listId), BgQueueDropWhy(drop));
}

// Every (list, bracket) bucket a non-bot session is queued (or invited) in: one scan per second for the whole tracked queue.
struct BucketKey
{
    std::uint32_t listId = 0;
    std::uint32_t bracket = 0;

    bool operator<(BucketKey const& other) const
    {
        return listId != other.listId ? listId < other.listId : bracket < other.bracket;
    }
};

std::set<BucketKey> HumanBuckets()
{
    std::set<BucketKey> buckets;
    for (auto const& [accountId, session] : sWorld->GetAllSessions())
    {
        if (!session || session->IsBot())
            continue;
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld() || player->InBattleground() || !player->InBattlegroundQueue())
            continue;
        for (std::uint8_t i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattlegroundQueueTypeId const queueId = player->GetBattlegroundQueueTypeId(i);
            if (queueId == BATTLEGROUND_QUEUE_NONE)
                continue;
            BgMapInfo const* map = BgMapByList(queueId.BattlemasterListId);
            if (!map || !map->mapId)
                continue;
            PVPDifficultyEntry const* own = DB2Manager::GetBattlegroundBracketByLevel(map->mapId, player->GetLevel());
            if (!own)
                continue;
            buckets.insert(BucketKey{ map->listId, std::uint32_t(own->GetBracketId()) });
        }
    }
    return buckets;
}

// A bound bot its human's group queued is normally tracked by OnBattlegroundQueueJoin; this picks up the ones that joined while
// the module was not looking (a reload, a group join before the module was enabled), so no bound queue entry can outlive its
// human untracked.
void TrackQueuedBots()
{
    State& s = S();
    std::uint32_t const now = NowMs();
    for (ObjectGuid const guid : BotMgr::Instance().BotGuids())
    {
        auto const found = s.bots.find(guid.GetCounter());
        if (found != s.bots.end() && (found->second.inside || found->second.invited || found->second.weQueued))
            continue;
        Player* player = BotMgr::Instance().FindBot(guid);
        if (!player || !player->InBattlegroundQueue())
            continue;
        ObjectGuid const master = BotMgr::Instance().MasterOf(guid);
        if (master.IsEmpty())
            continue;                                   // not bound: only a human's group entry is followed here
        for (std::uint8_t i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattlegroundQueueTypeId const queueId = player->GetBattlegroundQueueTypeId(i);
            if (queueId == BATTLEGROUND_QUEUE_NONE)
                continue;
            BgMapInfo const* map = BgMapByList(queueId.BattlemasterListId);
            if (!map || !map->enabled || !map->mapId)
                continue;
            PVPDifficultyEntry const* own = DB2Manager::GetBattlegroundBracketByLevel(map->mapId, player->GetLevel());
            if (!own)
                continue;
            Track& track = s.bots[guid.GetCounter()];
            track.name = player->GetName();
            track.listId = map->listId;
            track.bracket = std::uint32_t(own->GetBracketId());
            track.weQueued = false;
            track.ours = false;
            track.instance = 0;
            track.follower = master;
            track.masterAtQueue = master;
            track.queuedAtMs = now;
            track.invited = false;
            track.inviteDeadlineMs = 0;
            track.invitedAtMs = 0;
            track.inside = false;
            track.leaving = false;
            track.orphanSinceMs = 0;
            break;
        }
    }
}

void TickBots(std::uint32_t now)
{
    State& s = S();
    TrackQueuedBots();
    std::set<BucketKey> humanBuckets;            // computed once, only when a bot is actually waiting in a queue
    bool haveBuckets = false;
    for (auto itr = s.bots.begin(); itr != s.bots.end();)
    {
        ObjectGuid const guid = PlayerGuidOf(itr->first);
        Track& track = itr->second;
        Player* player = BotMgr::Instance().FindBot(guid);
        if (!player)
        {
            // A logged-in bot mid-teleport has no Player in the world for a tick; its track must survive it. Only a bot that is
            // no longer a bot at all has logged out.
            if (BotMgr::Instance().IsBot(guid))
            {
                ++itr;
                continue;
            }
            if (!track.leaving)
                LogEvent("leave", track, "logout");
            itr = s.bots.erase(itr);
            continue;
        }
        if (player->InBattleground())
        {
            TickInside(player, track, now);
            ++itr;
            continue;
        }
        if (player->InBattlegroundQueue() &&
            player->InBattlegroundQueueForBattlegroundQueueType(QueueIdOf(track.listId)))
        {
            if (!haveBuckets)
            {
                humanBuckets = HumanBuckets();
                haveBuckets = true;
            }
            TickQueued(player, track, now, humanBuckets.count(BucketKey{ track.listId, track.bracket }) != 0);
            ++itr;
            continue;
        }
        if (!track.leaving)
            LogEvent("leave", track, track.inside ? "out" : "queue-end");
        itr = s.bots.erase(itr);
    }
}

struct Demand
{
    std::uint32_t listId = 0;
    std::uint32_t bracket = 0;
    std::vector<ObjectGuid> humans;
};

// What one (list, bracket) bucket holds right now: the core's uninvited queue, the invitations still in flight (humans and the
// module's bots), the target and the deficit window. The fill, the orphan rule and the creation veto all read this one view, so
// "waiting" means the same thing in all three.
BgBucketFacts BucketFacts(std::uint32_t listId, std::uint32_t bracket, std::uint32_t now)
{
    BgBucketFacts facts;
    BgMapInfo const* map = BgMapByList(listId);
    BattlegroundTemplate const* templ = sBattlegroundMgr->GetBattlegroundTemplateByTypeId(BattlegroundTypeId(listId));
    if (!map || !map->mapId || !templ)
        return facts;
    std::uint32_t const maxPerTeam = templ->GetMaxPlayersPerTeam();
    std::uint32_t const minPerTeam = std::min(S().minPerTeam, maxPerTeam);
    facts.target = HumanBgSideTarget(minPerTeam, maxPerTeam);
    facts.autonomous = S().autonomous;
    BattlegroundQueueTypeId const queueId =
        BattlegroundMgr::BGQueueTypeId(uint16(listId), BattlegroundQueueIdType::Battleground, false, 0);
    BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(queueId);
    for (std::uint32_t side = 0; side < 2; ++side)
        facts.waiting[side] = queue.GetQueuedPlayersCount(TeamId(SideTeam(side)), BattlegroundBracketId(bracket));

    // GetQueuedPlayersCount skips invited groups, so the invitations in flight are added by hand: a second batch queued while a
    // human's invite is pending would push the sides past the target.
    for (auto const& [accountId, session] : sWorld->GetAllSessions())
    {
        if (!session || session->IsBot())
            continue;
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld() || player->InBattleground())
            continue;
        if (player->GetBattlegroundQueueIndex(queueId) >= PLAYER_MAX_BATTLEGROUND_QUEUES)
            continue;
        PVPDifficultyEntry const* own = DB2Manager::GetBattlegroundBracketByLevel(map->mapId, player->GetLevel());
        if (!own || std::uint32_t(own->GetBracketId()) != bracket)
            continue;
        facts.humanWaiting = true;
        GroupQueueInfo info{};
        if (queue.GetPlayerGroupInfoData(player->GetGUID(), &info) && info.IsInvitedToBGInstanceGUID)
            ++facts.invited[TeamIndex(info.Team)];
    }
    for (auto const& [guid, track] : S().bots)
    {
        if (track.inside || track.listId != listId || track.bracket != bracket)
            continue;
        facts.moduleBots = true;
        if (!track.invited)
            continue;
        Player* player = BotMgr::Instance().FindBot(PlayerGuidOf(guid));
        if (!player)
            continue;
        ++facts.invited[TeamIndex(player->GetTeam())];
    }

    for (std::uint32_t side = 0; side < 2; ++side)
    {
        auto itr = S().deficits.find(DeficitKey(listId, bracket, side));
        if (itr != S().deficits.end() && itr->second.sinceMs && now - itr->second.sinceMs >= kBgDeficitMs)
            facts.deficitDone = true;
    }
    return facts;
}

// True when the core may not create an instance for this list and bracket yet (WorldScript::CanCreateBattleground). A 1v1 made
// from a batch of bots is exactly what this stops: the queue starts once both sides reach the target, or once the pool has proven
// it cannot fill them for the deficit window.
bool CanCreate(std::uint32_t listId, std::uint8_t bracketId)
{
    State& s = S();
    if (!s.configured)
        LoadConfig();
    // A cap of 0 turns the fill off on purpose: holding humans for bots that may never be queued is not this hook's business.
    if (!s.enabled || !s.fillHumanQueues || !s.maxBots || !s.maxInstances)
        return true;
    BgMapInfo const* map = BgMapByList(listId);
    // A map this round does not manage (config Bg.Maps, arena, random) is never held back: the module has no bots for it.
    if (!map || !map->enabled || map->kind == BgMapKind::None || !s.mapOn[int(map->kind) - 1])
        return true;
    bool const veto = BgVetoCreate(BucketFacts(listId, bracketId, NowMs()));
    if (veto)
        TC_LOG_DEBUG("tortoisebots", "TortoiseBots bg: holding {} bracket {} back until the queue reaches its target", BgMapName(listId),
            uint32(bracketId));
    return !veto;
}

// The core re-evaluates a queue on a join, a decline, a leave and an invite timeout, never on an accept: without this the bots
// still waiting after the human entered would never be pulled into his instance. Every bucket with a human demand or with a
// module-queued, uninvited bot is re-evaluated each pass.
void ScheduleQueues(std::vector<Demand> const& demands)
{
    std::set<BucketKey> buckets;
    for (Demand const& demand : demands)
        buckets.insert(BucketKey{ demand.listId, demand.bracket });
    for (auto const& [guid, track] : S().bots)
        if (!track.inside && !track.invited)
            buckets.insert(BucketKey{ track.listId, track.bracket });
    for (BucketKey const& bucket : buckets)
        sBattlegroundMgr->ScheduleQueueUpdate(0,
            BattlegroundMgr::BGQueueTypeId(uint16(bucket.listId), BattlegroundQueueIdType::Battleground, false, 0),
            BattlegroundBracketId(bucket.bracket));
}

// Bots a side can still be filled from in this bracket, under the pass's caps (a dry run of the candidate checks).
std::uint32_t AvailableBots(std::vector<ObjectGuid> const& guids, BgMapInfo const* map, PVPDifficultyEntry const* demandBracket,
                            BgQueueCaps const& caps, std::uint32_t side)
{
    std::uint32_t available = 0;
    for (ObjectGuid const guid : guids)
    {
        Player* player = BotMgr::Instance().FindBot(guid);
        if (!player || TeamIndex(player->GetTeam()) != side)
            continue;
        BgQueueFacts facts;
        FillFacts(player, map, demandBracket, caps, facts);
        if (BgQueueRefusalOf(facts) == BgRefusal::None)
            ++available;
    }
    return available;
}

void FillHumanQueues(std::uint32_t now)
{
    State& s = S();
    if (!s.fillHumanQueues)
        return;
    std::vector<Demand> demands;
    for (auto const& [accountId, session] : sWorld->GetAllSessions())
    {
        if (!session || session->IsBot())
            continue;
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld() || player->InBattleground() || !player->InBattlegroundQueue())
            continue;
        for (std::uint8_t i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattlegroundQueueTypeId const queueId = player->GetBattlegroundQueueTypeId(i);
            if (queueId == BATTLEGROUND_QUEUE_NONE)
                continue;
            BgMapInfo const* map = BgMapByList(queueId.BattlemasterListId);
            if (!map || !map->enabled || int(map->kind) - 1 < 0 || !s.mapOn[int(map->kind) - 1])
                continue;
            BattlegroundTemplate const* templ =
                sBattlegroundMgr->GetBattlegroundTemplateByTypeId(BattlegroundTypeId(map->listId));
            if (!templ || templ->MapIDs.empty())
                continue;
            PVPDifficultyEntry const* bracket = DB2Manager::GetBattlegroundBracketByLevel(templ->MapIDs.front(), player->GetLevel());
            if (!bracket)
                continue;
            auto found = std::find_if(demands.begin(), demands.end(), [&](Demand const& demand)
            {
                return demand.listId == map->listId && demand.bracket == std::uint32_t(bracket->GetBracketId());
            });
            if (found == demands.end())
            {
                Demand demand;
                demand.listId = map->listId;
                demand.bracket = std::uint32_t(bracket->GetBracketId());
                demands.push_back(std::move(demand));
                found = demands.end() - 1;
            }
            if (std::find(found->humans.begin(), found->humans.end(), player->GetGUID()) == found->humans.end())
                found->humans.push_back(player->GetGUID());
        }
    }
    // A bucket no human waits in any more keeps no deficit: the next human there starts the window again and gets his line.
    for (auto itr = s.deficits.begin(); itr != s.deficits.end();)
    {
        std::uint32_t const listId = std::uint32_t(itr->first >> 40);
        std::uint32_t const bracket = std::uint32_t((itr->first >> 8) & 0xffffffffu);
        bool const demanded = std::any_of(demands.begin(), demands.end(), [&](Demand const& demand)
        {
            return demand.listId == listId && demand.bracket == bracket;
        });
        if (!demanded)
            itr = s.deficits.erase(itr);
        else
            ++itr;
    }
    if (demands.empty())
    {
        ScheduleQueues(demands);
        return;
    }

    std::vector<ObjectGuid> guids = BotMgr::Instance().BotGuids();
    std::sort(guids.begin(), guids.end(), [](ObjectGuid const& a, ObjectGuid const& b)
    {
        return a.GetCounter() < b.GetCounter();
    });
    // D9: one view of the bots the module holds, each match once, and a bot queued below joins it at once: its bucket then holds
    // a reservation of its own (ownBucketCounted), so the instance cap never stops a human's own queue half-filled. A match
    // already running in the same bracket is a different count: the queue this fill serves may create the next one. A bucket whose
    // humans already went into their instance keeps its leftover entries uncounted: they wait for that match, not a new one.
    std::vector<BgTrackFact> tracks = TrackFacts();
    auto capsFor = [&](std::uint32_t listId, std::uint32_t bracket)
    {
        return BgCapsOfTracks(tracks, s.maxInstances, s.maxBots, listId, bracket);
    };

    for (Demand const& demand : demands)
    {
        BgMapInfo const* map = BgMapByList(demand.listId);
        // The candidate is checked against the bucket the human waits in, never its own level bracket: a bot of another level
        // would otherwise be queued into its own bracket while the human's queue stayed empty.
        PVPDifficultyEntry const* demandBracket =
            map && map->mapId ? DB2Manager::GetBattlegroundBracketById(map->mapId, BattlegroundBracketId(demand.bracket)) : nullptr;
        if (!map || !demandBracket)
            continue;
        BgBucketFacts const bucket = BucketFacts(demand.listId, demand.bracket, now);
        std::uint32_t const target = bucket.target;
        std::uint32_t need[2];
        for (std::uint32_t side = 0; side < 2; ++side)
            need[side] = BgSideNeed(bucket, side);

        // Do not queue a half match: if a side that still needs bots has none of the opposite faction to meet, the queue cannot
        // form at the target and only holds pool bots. The deficit line below still fires.
        BgQueueCaps const bucketCaps = capsFor(demand.listId, demand.bracket);
        std::uint32_t available[2] = { 0, 0 };
        bool pairMissing = false;
        for (std::uint32_t side = 0; side < 2; ++side)
            available[side] = AvailableBots(guids, map, demandBracket, bucketCaps, side);
        for (std::uint32_t side = 0; side < 2; ++side)
            if (need[side] && need[1 - side] && available[1 - side] == 0)
                pairMissing = true;

        BattlegroundQueueTypeId const queueId =
            BattlegroundMgr::BGQueueTypeId(uint16(demand.listId), BattlegroundQueueIdType::Battleground, false, 0);
        for (std::uint32_t side = 0; side < 2; ++side)
        {
            std::uint64_t const key = DeficitKey(demand.listId, demand.bracket, side);
            std::uint32_t queued = 0;
            if (need[side] && !pairMissing)
            {
                for (ObjectGuid const guid : guids)
                {
                    if (!HumanBgSideWantsBot(need[side] - queued, queued))
                        break;
                    Player* player = BotMgr::Instance().FindBot(guid);
                    if (!player || TeamIndex(player->GetTeam()) != side)
                        continue;
                    BgQueueCaps const caps = capsFor(demand.listId, demand.bracket);
                    BgQueueFacts facts;
                    FillFacts(player, map, demandBracket, caps, facts);
                    if (BgQueueRefusalOf(facts) != BgRefusal::None)
                        continue;
                    WorldSession* session = player->GetSession();
                    if (!session)
                        continue;
                    std::string const name = player->GetName();
                    session->JoinBattlegroundQueue(queueId);
                    if (!player->InBattlegroundQueueForBattlegroundQueueType(queueId))
                    {
                        TC_LOG_INFO("tortoisebots", "TortoiseBots bg: pool bot {} was refused the {} queue: {}", name,
                            BgMapName(demand.listId), BgRefusalName(BgQueueRefusalOf(facts)));
                        continue;
                    }
                    Track& track = s.bots[guid.GetCounter()];
                    track.name = name;
                    track.listId = demand.listId;
                    track.bracket = demand.bracket;
                    track.weQueued = true;
                    track.ours = true;
                    track.follower = ObjectGuid::Empty;
                    track.masterAtQueue = ObjectGuid::Empty;
                    track.serves = demand.humans;
                    track.queuedAtMs = now;
                    track.invited = false;
                    track.inviteDeadlineMs = 0;
                    track.invitedAtMs = 0;
                    track.instance = 0;
                    track.inside = false;
                    track.leaving = false;
                    track.orphanSinceMs = 0;
                    LogEventImpl("queue", track, "", TeamWord(player->GetTeam()));
                    ++queued;
                    BgTrackFact fact;
                    fact.listId = demand.listId;
                    fact.bracket = demand.bracket;
                    fact.state = BgTrackState::Queued;
                    tracks.push_back(fact);
                }
            }

            std::uint32_t const remaining = need[side] > queued ? need[side] - queued : 0;
            if (!remaining)
            {
                s.deficits.erase(key);
                continue;
            }
            Deficit& deficit = s.deficits[key];
            if (!deficit.sinceMs)
                deficit.sinceMs = now;
            if (deficit.told || now - deficit.sinceMs < kBgDeficitMs)
                continue;
            deficit.told = true;
            std::string const sideWord = side == 0 ? "Alliance" : "Horde";
            // One line to every human waiting in this queue and bracket, naming the side that is short and what it holds.
            std::uint32_t const have = std::min(target, BgSideHeld(bucket, side) + queued);
            for (ObjectGuid const guid : demand.humans)
            {
                Player* human = ObjectAccessor::FindConnectedPlayer(guid);
                if (!human || !human->GetSession() || human->GetSession()->IsBot())
                    continue;
                PVPDifficultyEntry const* own = DB2Manager::GetBattlegroundBracketByLevel(map->mapId, human->GetLevel());
                if (!own || std::uint32_t(own->GetBracketId()) != demand.bracket)
                    continue;
                ChatHandler(human->GetSession()).SendSysMessage(Trinity::StringFormat(
                    "TortoiseBots: not enough {} bots for levels {}-{}: {} of {}", sideWord, own->MinLevel, own->MaxLevel,
                    have, target));
            }
        }
    }

    ScheduleQueues(demands);
}

// `.tb bg status` helpers.
struct InstanceView
{
    std::uint32_t mapId = 0;
    std::uint32_t listId = 0;
    std::uint32_t instance = 0;
    std::uint32_t alliance = 0;
    std::uint32_t horde = 0;
    std::uint32_t humansAlliance = 0;
    std::uint32_t humansHorde = 0;
    std::uint32_t scoreAlliance = 0;
    std::uint32_t scoreHorde = 0;
    std::int32_t status = 0;
};

} // namespace

void LoadConfig()
{
    State& s = S();
    s.configured = true;
    s.enabled = sConfigMgr->GetIntDefault("TortoiseBots.Bg.Enable", 0) != 0;
    s.fillHumanQueues = sConfigMgr->GetIntDefault("TortoiseBots.Bg.FillHumanQueues", 1) != 0;
    s.autonomous = sConfigMgr->GetIntDefault("TortoiseBots.Bg.Autonomous", 0) != 0;
    s.minPerTeam = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Bg.MinPlayersPerTeam", 5), 0, 40));
    s.maxInstances = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Bg.MaxInstancesWithBots", 2), 0, 50));
    s.maxBots = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Bg.MaxBots", 50), 0, 2000));
    s.maxMinutes = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Bg.MaxMinutes", 45), 1, 240));

    bool maps[2] = { false, false };
    std::string const spec = sConfigMgr->GetStringDefault("TortoiseBots.Bg.Maps", "wsg,ab");
    std::string token;
    auto apply = [&](std::string const& word)
    {
        if (word == "wsg")
            maps[0] = true;
        else if (word == "ab")
            maps[1] = true;
        else if (!word.empty())
            TC_LOG_WARN("tortoisebots", "TortoiseBots bg: unknown map '{}' in TortoiseBots.Bg.Maps (wsg, ab)", word);
    };
    for (char const c : spec)
    {
        if (c == ',' || c == ' ' || c == '\t')
        {
            apply(token);
            token.clear();
        }
        else
            token.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    }
    apply(token);
    s.mapOn[0] = maps[0];
    s.mapOn[1] = maps[1];
}

bool Enabled()
{
    State& s = S();
    if (!s.configured)
        LoadConfig();
    return s.enabled;
}

bool Update(std::uint32_t diff)
{
    State& s = S();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled)
        return false;
    s.accMs += diff;
    if (s.accMs < 1000)
        return false;
    s.accMs = 0;
    std::uint32_t const now = NowMs();
    TickBots(now);
    s.fillAccMs += 1000;
    if (s.fillAccMs >= kHumanBgDemandIntervalMs)
    {
        s.fillAccMs = 0;
        FillHumanQueues(now);
    }
    return true;
}

namespace
{

class BgPlayerScript final : public PlayerScript
{
public:
    BgPlayerScript() : PlayerScript("tortoisebots_bg") { }

    void OnBattlegroundQueueJoin(Player* player, uint32 bgTypeId, uint8 bracketId) override
    {
        if (!Enabled() || !player)
            return;
        // A human just joined: the fill runs on the next tick instead of waiting out the 5 s check. A bot's own join (a bot
        // group leader) is not a human demand.
        if (BotMgr::Instance().IsBot(player->GetGUID()))
            return;
        // Bg.Enable stays inert for a map the round does not manage (AV, random): the fill has nothing to queue there, and the
        // tracking below would only take the bots out of a queue whose invitations are never answered.
        BgMapInfo const* map = BgMapByList(bgTypeId);
        if (!map || !map->enabled)
            return;
        State& s = S();
        s.fillAccMs = kHumanBgDemandIntervalMs;
        // The core's group join queues every member of the human's party, bots included, with no hook per member: track them here
        // so a bot whose human leaves the queue, logs out or lets it go leaves with him (D5), even if no invitation ever comes.
        Group* group = player->GetGroup();
        if (!group)
            return;
        std::uint32_t const now = NowMs();
        for (GroupReference const& reference : group->GetMembers())
        {
            Player* member = reference.GetSource();
            if (!member || member == player || !BotMgr::Instance().IsBot(member->GetGUID()))
                continue;
            Track& track = s.bots[member->GetGUID().GetCounter()];
            if (track.inside)
                continue;
            track.name = member->GetName();
            track.listId = bgTypeId;
            track.bracket = bracketId;
            track.weQueued = false;
            track.ours = false;
            track.instance = 0;
            track.follower = player->GetGUID();
            track.masterAtQueue = BotMgr::Instance().MasterOf(member->GetGUID());
            track.queuedAtMs = now;
            track.invited = false;
            track.inviteDeadlineMs = 0;
            track.invitedAtMs = 0;
            track.leaving = false;
            track.orphanSinceMs = 0;
        }
    }

    void OnBattlegroundInvite(Player* player, uint32 instanceId, uint32 bgTypeId) override
    {
        if (!Enabled() || !player || !BotMgr::Instance().IsBot(player->GetGUID()))
            return;
        BgMapInfo const* map = BgMapByList(bgTypeId);
        if (!map || !map->enabled)
            return;
        State& s = S();
        ObjectGuid const guid = player->GetGUID();
        Track& track = s.bots[guid.GetCounter()];
        if (track.invited)
            return;
        std::uint32_t const now = NowMs();
        track.name = player->GetName();
        track.listId = bgTypeId;
        PVPDifficultyEntry const* bracket = BracketOf(map, player);
        track.bracket = bracket ? std::uint32_t(bracket->GetBracketId()) : 0;
        track.invited = true;
        track.inviteDueMs = now + BgAcceptDelayMs(rand32());
        track.invitedAtMs = now;
        track.inviteDeadlineMs = now + kInviteWindowMs;
        GroupQueueInfo info{};
        if (sBattlegroundMgr->GetBattlegroundQueue(QueueIdOf(bgTypeId)).GetPlayerGroupInfoData(guid, &info) && info.RemoveInviteTime)
            track.inviteDeadlineMs = info.RemoveInviteTime;
        track.inside = false;
        track.leaving = false;
        track.instance = instanceId;
        track.ours = BotMgr::Instance().IsFreeBot(guid);
        // A bound bot invited without the group-join track (the module was reloaded, or its leader was a bot) still follows its
        // master; a bot with no human is not this rule's.
        if (track.follower.IsEmpty())
        {
            track.follower = BotMgr::Instance().MasterOf(guid);
            track.masterAtQueue = track.follower;
        }
    }

    void OnBattlegroundPortFailed(Player* player, uint8 reason, uint32 /*instanceId*/) override
    {
        if (!Enabled() || !player || !BotMgr::Instance().IsBot(player->GetGUID()))
            return;
        State& s = S();
        auto itr = s.bots.find(player->GetGUID().GetCounter());
        if (itr == s.bots.end() || !itr->second.invited)
            return;
        itr->second.invited = false;
        itr->second.inviteDeadlineMs = 0;
        itr->second.invitedAtMs = 0;
        LogEvent("leave", itr->second, Trinity::StringFormat("port-failed:{}", uint32(reason)).c_str());
    }
};

// The core's own creation hook (WorldScript::CanCreateBattleground, called from BattlegroundQueue.cpp after the free-slot fill):
// the one place to hold a match back until the fill has queued the target and to refuse a bot-only instance.
class BgWorldScript final : public WorldScript
{
public:
    BgWorldScript() : WorldScript("tortoisebots_bg_veto") { }

    bool CanCreateBattleground(uint32 battlemasterListId, uint8 bracketId) override
    {
        return CanCreate(battlemasterListId, bracketId);
    }
};

} // namespace

void RegisterScripts()
{
    new BgPlayerScript();
    new BgWorldScript();
}

std::vector<std::string> Status()
{
    State& s = S();
    if (!s.configured)
        LoadConfig();
    std::vector<std::string> lines;
    std::uint32_t queued = 0, inside = 0;
    for (auto const& [guid, track] : s.bots)
    {
        if (track.inside)
            ++inside;
        else
            ++queued;
    }
    std::string maps;
    for (int i = 0; i < 2; ++i)
    {
        if (!s.mapOn[i])
            continue;
        maps += (maps.empty() ? "" : ",") + std::string(i == 0 ? "wsg" : "ab");
    }
    lines.push_back(Trinity::StringFormat(
        "bg: enabled={} maps={} fill={} autonomous={} min_per_team={} max_instances={} max_bots={} max_minutes={} queued={} inside={} events={}",
        s.enabled ? 1 : 0, maps.empty() ? "-" : maps, s.fillHumanQueues ? 1 : 0, s.autonomous ? 1 : 0, s.minPerTeam,
        s.maxInstances, s.maxBots, s.maxMinutes, queued, inside, s.events.size()));

    if (!BgMapDataLoaded())
        lines.push_back("bg maps: not loaded yet (the world is still starting)");
    else
    {
        std::string mapLine = "bg maps:";
        for (std::uint32_t listId : { kWsgListId, kAbListId })
        {
            BgMapInfo const* map = BgMapByList(listId);
            if (!map)
                continue;
            if (map->kind == BgMapKind::Wsg)
                mapLine += Trinity::StringFormat(" wsg map=489 list=2 enabled={} stands=2", map->enabled ? 1 : 0);
            else
                mapLine += Trinity::StringFormat(" ab map=529 list=3 enabled={} nodes={}", map->enabled ? 1 : 0,
                    map->nodesLoaded);
        }
        lines.push_back(std::move(mapLine));
    }

    // Instances holding bots, one view each, with the per-side humans and bots and the world-state score.
    std::map<std::pair<std::uint32_t, std::uint32_t>, InstanceView> views;
    for (auto const& [guid, track] : s.bots)
    {
        if (!track.inside)
            continue;
        Player* player = BotMgr::Instance().FindBot(PlayerGuidOf(guid));
        Battleground* battleground = player ? player->GetBattleground() : nullptr;
        if (!player || !battleground)
            continue;
        auto const key = std::make_pair(battleground->GetMapId(), battleground->GetInstanceID());
        auto found = views.find(key);
        if (found == views.end())
        {
            InstanceView view;
            view.mapId = battleground->GetMapId();
            view.listId = battleground->GetTypeID();
            view.instance = battleground->GetInstanceID();
            view.status = std::int32_t(battleground->GetStatus());
            Map* map = player->GetMap();
            BgMapInfo const* info = BgMapByMapId(view.mapId);
            if (info && info->kind == BgMapKind::Wsg)
            {
                int32 const alliance = map ? WorldStateMgr::GetValue(kWsgCapturesAlliance, map) : 0;
                int32 const horde = map ? WorldStateMgr::GetValue(kWsgCapturesHorde, map) : 0;
                view.scoreAlliance = std::uint32_t(alliance > 0 ? alliance : 0);
                view.scoreHorde = std::uint32_t(horde > 0 ? horde : 0);
            }
            else if (info && info->kind == BgMapKind::Ab)
            {
                int32 const alliance = map ? WorldStateMgr::GetValue(kAbResourcesAlliance, map) : 0;
                int32 const horde = map ? WorldStateMgr::GetValue(kAbResourcesHorde, map) : 0;
                view.scoreAlliance = std::uint32_t(alliance > 0 ? alliance : 0);
                view.scoreHorde = std::uint32_t(horde > 0 ? horde : 0);
            }
            found = views.emplace(key, view).first;
        }
    }
    for (auto& [key, view] : views)
    {
        Player* sample = nullptr;
        for (auto const& [guid, track] : s.bots)
        {
            if (!track.inside || track.instance != view.instance)
                continue;
            Player* player = BotMgr::Instance().FindBot(PlayerGuidOf(guid));
            if (player && player->GetBattleground() && player->GetBattleground()->GetMapId() == view.mapId)
            {
                sample = player;
                break;
            }
        }
        Battleground* battleground = sample ? sample->GetBattleground() : nullptr;
        if (battleground)
        {
            for (auto const& [guid, data] : battleground->GetPlayers())
            {
                bool const bot = BotMgr::Instance().IsBot(guid);
                bool const alliance = data.Team == ALLIANCE;
                if (alliance)
                    (bot ? view.alliance : view.humansAlliance) += 1;
                else
                    (bot ? view.horde : view.humansHorde) += 1;
            }
        }
        lines.push_back(Trinity::StringFormat(
            "bg instance={} map={} status={} score={}-{} alliance={}h+{}b horde={}h+{}b", view.instance,
            BgMapName(view.listId), StatusWord(view.status), std::to_string(view.scoreAlliance), std::to_string(view.scoreHorde),
            view.humansAlliance, view.alliance, view.humansHorde, view.horde));
    }

    for (auto const& [guid, track] : s.bots)
    {
        Player* player = BotMgr::Instance().FindBot(PlayerGuidOf(guid));
        if (!player)
            continue;
        if (track.inside)
        {
            std::uint32_t const now = NowMs();
            std::string const age = track.lastMoveMs ? std::to_string((now - track.lastMoveMs) / 1000) + "s" : "-";
            lines.push_back(Trinity::StringFormat(
                "bg bot={} role=- objective=- map={} instance={} team={} alive={} move_age={}", player->GetName(),
                BgMapName(track.listId), track.instance, TeamWord(player->GetBGTeam()), player->IsAlive() ? 1 : 0, age));
        }
        else
        {
            std::uint32_t const now = NowMs();
            std::string const since = track.queuedAtMs ? std::to_string((now - track.queuedAtMs) / 1000) + "s" : "-";
            lines.push_back(Trinity::StringFormat(
                "bg queued bot={} map={} bracket={} since={} invited={}", player->GetName(), BgMapName(track.listId),
                LevelRange(BgMapByList(track.listId) ? BgMapByList(track.listId)->mapId : 0, player->GetLevel()),
                track.weQueued ? since : std::string("-"), track.invited ? 1 : 0));
        }
    }
    return lines;
}

EventPage Events(std::uint32_t since)
{
    State& s = S();
    EventPage page;
    if (s.events.empty())
        return page;
    page.oldest = s.events.front().seq;
    page.last = s.events.back().seq;
    for (BgEvent const& event : s.events)
        if (event.seq > since)
            page.lines.push_back(EventLine(event));
    return page;
}

} // namespace bg
} // namespace TortoiseBots
