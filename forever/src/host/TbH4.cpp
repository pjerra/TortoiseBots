// Slice 4, host package H4: see TbH4.h. Core headers only (no legacy header), so this file builds with and without TORTOISEBOTS_SLICE1.
#include "host/TbH4.h"
#include "host/TbH3.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"

#include "Chat.h"
#include "ObjectMgr.h"
#include "Player.h"

#include <cctype>
#include <string>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "Creature.h"
#include "CreatureData.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Group.h"
#include "InstanceLockMgr.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "StringFormat.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <vector>

namespace TortoiseBots
{
namespace h4
{
using namespace h3;

namespace
{
bool ParseU32(std::string const& word, uint32& out)
{
    if (word.empty() || word.size() > 9)
        return false;
    for (char c : word)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    out = uint32(std::strtoul(word.c_str(), nullptr, 10));
    return true;
}

bool ParseFloat(std::string const& word, float& out)
{
    if (word.empty())
        return false;
    char* end = nullptr;
    double const value = std::strtod(word.c_str(), &end);
    if (!end || *end != '\0')
        return false;
    out = float(value);
    return true;
}

bool IsNumberWord(std::string const& word)
{
    float ignored = 0.0f;
    return ParseFloat(word, ignored);
}

// The area trigger id whose areatrigger_teleport row is `entry` (ObjectMgr keeps only the row, not the id it was found under).
uint32 TriggerIdOf(AreaTriggerTeleport const* entry)
{
    if (!entry)
        return 0;
    for (AreaTriggerEntry const* at : sAreaTriggerStore)
        if (sObjectMgr->GetAreaTrigger(at->ID) == entry)
            return at->ID;
    return 0;
}

char const* ShapeName(AreaTriggerEntry const* at)
{
    switch (at->GetShapeType())
    {
        case AreaTriggerShapeType::Sphere: return "sphere";
        case AreaTriggerShapeType::Box: return "box";
        default: return "other";
    }
}

}

// ---- `.tb grammar selftest`: one example of every record kind and key of this package (fixed values, built with the same encoder calls as the real writers) ----
void SelfTestRecords(Reply& reply)
{
    // The state keys (appended to every bot record; `corpse`/`corpsemap` only for a dead bot), the end-line keys and the threat keys.
    reply.Line(Rec("selftest").S("case", "state-h4").S("bot", "Mdpsa").S("life", "awaiting-res").S("relhold", "rezzer").S("resreq", "Mpriest").Coord("corpse", -3.85, -11.03, -15.83).N("corpsemap", 389)
        .B("outside", false).S("entered", "trigger").S("entry", "").S("block", "").N("catchup", 0).B("practice", false));
    reply.Line(Rec("selftest").S("case", "state-h4-refused").S("bot", "Mlow").S("life", "alive").S("relhold", "").S("resreq", "").B("outside", true).S("entered", "").S("entry", "").S("block", "level")
        .T("blockmsg", "level too low for this instance (needs 10) (TransferAbortReason 8, MapDifficultyXCondition 4172, PlayerCondition 159263)").N("catchup", 0).B("practice", false));
    reply.Line(Rec("selftest").S("case", "state-end-h4").N("alive", 3).N("dead", 1).N("ghost", 1).B("wipe", false).B("whole", false));
    reply.Line(Rec("selftest").S("case", "threat-h4").S("mob", "000000000000000000000000000000AB").Coord("pos", 12.5, -40.25, -19.1).B("flee", true));
    // The records of the commands.
    reply.Line(Rec("trigger").N("id", 2230).N("map", 1).Coord("pos", 1811.0, -4410.5, -18.5).S("shape", "box").F("length", 6.0, 2).F("width", 6.0, 2).F("height", 5.0, 2).F("yaw", 0.0, 2)
        .N("target", 1102230).N("targetmap", 389).Coord("targetpos", 0.80, -8.23, -15.53));
    reply.Line(Rec("enter").S("bot", "Mtank").N("trigger", 2230).N("map", 389).S("status", "started").B("inbox", true).B("direct", false));
    reply.Line(Rec("enter").S("bot", "Mlow").N("trigger", 2230).N("map", 389).S("leader", "Mtank").S("status", "refused").S("code", "level").T("msg", "level too low for this instance (needs 10)"));
    reply.Line(Rec("instance").S("leader", "Mtank").N("map", 389).S("name", "Ragefire Chasm").N("inst", 1).N("diff", 1).N("players", 5).N("gms", 1).N("mobs_alive", 88).N("mobs_total", 91).N("mobs_loaded", 91)
        .LP("boss", {{"2732", "alive"}, {"2734", "dead"}}).N("entrance", 1102230).N("recent", 1).S("owner", "Mtank"));
    reply.Line(Rec("leave").S("bot", "Mtank").N("map", 1).S("status", "started"));
    reply.Line(Rec("reset").N("map", 389).N("inst", 1).S("status", "ok"));
    reply.Line(Rec("tele").S("bot", "Mcmd").S("to", "Mtank").S("status", "started").N("map", 389).N("inst", 1).S("tag", "SETUP"));
    reply.Line(Rec("revive").S("bot", "Mdpsa").S("tag", "PRACTICE").S("status", "revived").B("practice", true));
    reply.Line(Rec("kill").S("bot", "Mdpsb").S("tag", "SETUP").S("status", "killed"));
    reply.Line(Rec("gm").S("bot", "Mcmd").S("tag", "SETUP").S("status", "ok").B("gm", true));
}

// ---- `.tb trigger <id> [#token]` ----
// The AreaTrigger.db2 row (map, centre, shape and size) and its areatrigger_teleport target (the world_safe_locs row the core's handler sends a
// player to, MiscHandler.cpp:748-753). Read only.
bool HandleTrigger(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "trigger"))
        return false;
    uint32 id = 0;
    if (args.w.empty())
        return Fail(handler, args, "trigger", "missing-argument", ".tb trigger <area trigger id>");
    if (!ParseU32(args.w[0], id))
        return Fail(handler, args, "trigger", "bad-id", args.w[0]);
    if (args.w.size() > 1)
        return Fail(handler, args, "trigger", "unexpected-argument", args.w[1]);
    AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(id);
    if (!at)
        return Fail(handler, args, "trigger", "bad-id", "no such area trigger in AreaTrigger.db2");

    Reply reply(handler, args, "trigger");
    reply.Begin();
    Rec rec("trigger");
    rec.N("id", id).N("map", at->ContinentID).Coord("pos", at->Pos.X, at->Pos.Y, at->Pos.Z).S("shape", ShapeName(at));
    if (at->GetShapeType() == AreaTriggerShapeType::Sphere)
        rec.F("radius", at->Radius, 2);
    else if (at->GetShapeType() == AreaTriggerShapeType::Box)
        rec.F("length", at->BoxLength, 2).F("width", at->BoxWidth, 2).F("height", at->BoxHeight, 2).F("yaw", at->BoxYaw, 2);
    if (AreaTriggerTeleport const* target = sObjectMgr->GetAreaTrigger(id))
        rec.N("target", target->ID).N("targetmap", target->Loc.GetMapId()).Coord("targetpos", target->Loc.GetPositionX(), target->Loc.GetPositionY(), target->Loc.GetPositionZ());
    reply.Line(rec);
    Rec end("trigger");
    end.N("id", id);
    reply.End(end);
    return true;
}

// ---- `.tb enter <bot|party [leader]> [trigger <id> | map <id>] [direct] [#token]` ----
bool HandleEnter(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "enter"))
        return false;
    auto const isOption = [](std::string const& word) { std::string const w = Lower(word); return w == "trigger" || w == "map" || w == "direct"; };
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party" && !isOption(args.w[1]);
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "enter", who.code, who.detail);

    uint32 triggerId = 0, mapId = 0;
    bool direct = false, haveTrigger = false, haveMap = false;
    while (pos < args.w.size())
    {
        std::string const option = Lower(args.w[pos++]);
        if (option == "direct")
            direct = true;
        else if (option == "trigger" || option == "map")
        {
            uint32 value = 0;
            if (pos >= args.w.size())
                return Fail(handler, args, "enter", "missing-argument", option + " needs a number");
            if (!ParseU32(args.w[pos], value))
                return Fail(handler, args, "enter", "bad-id", args.w[pos]);
            ++pos;
            (option == "trigger" ? haveTrigger : haveMap) = true;
            (option == "trigger" ? triggerId : mapId) = value;
        }
        else
            return Fail(handler, args, "enter", "unexpected-argument", option);
    }
    if (haveTrigger && haveMap)
        return Fail(handler, args, "enter", "bad-argument", "give a trigger or a map, not both");

    // The party's leader, when it is one of the addressed host bots (C5: the tank bot leads; the commander is never the leader that walks in): it enters first
    // and the others call the handler only after it stands inside.
    Player* leaderBot = nullptr;
    if (who.bots.size() > 1 || who.leader)
    {
        Player* candidate = who.leader;
        if (!candidate)
            if (Group* group = who.bots.front()->GetGroup())
                candidate = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
        if (candidate && IsHostBot(candidate) && std::find(who.bots.begin(), who.bots.end(), candidate) != who.bots.end())
            leaderBot = candidate;
    }
    Player* anchor = leaderBot ? leaderBot : who.bots.front();

    if (haveMap)
    {
        AreaTriggerTeleport const* entry = sObjectMgr->GetMapEntranceTrigger(mapId);
        triggerId = TriggerIdOf(entry);
        if (!triggerId)
            return Fail(handler, args, "enter", "no-trigger", "no area trigger leads into that map");
    }
    else if (!haveTrigger)
    {
        // The default: the area trigger the leader (the first bot) stands in and that leads somewhere else.
        for (AreaTriggerEntry const* at : sAreaTriggerStore)
        {
            if (at->ContinentID != anchor->GetMapId() || !anchor->IsInAreaTrigger(at))
                continue;
            AreaTriggerTeleport const* target = sObjectMgr->GetAreaTrigger(at->ID);
            if (target && target->Loc.GetMapId() != anchor->GetMapId())
            {
                triggerId = at->ID;
                break;
            }
        }
        if (!triggerId)
            return Fail(handler, args, "enter", "no-trigger", anchor->GetName() + " stands in no area trigger that leads to another map: name one (`trigger <id>`, `map <id>`)");
    }
    AreaTriggerTeleport const* target = sObjectMgr->GetAreaTrigger(triggerId);
    uint32 const targetMap = target ? target->Loc.GetMapId() : 0;

    Reply reply(handler, args, "enter");
    reply.Begin();
    uint32 started = 0, refused = 0;
    bool leaderRefused = false;
    std::vector<Player*> order = who.bots;
    if (leaderBot)
        std::stable_partition(order.begin(), order.end(), [leaderBot](Player* p) { return p == leaderBot; });
    for (Player* bot : order)
    {
        Rec rec("enter");
        rec.S("bot", bot->GetName()).N("trigger", triggerId).N("map", targetMap);
        if (leaderBot && bot != leaderBot)
            rec.S("leader", leaderBot->GetName());
        BotMgr::EntryStart result;
        if (leaderRefused && bot != leaderBot)
            result = { "leader-refused", "the party leader was refused: nobody enters before it" };
        else
            result = BotMgr::Instance().StartEntry(bot->GetGUID(), triggerId, leaderBot && bot != leaderBot ? leaderBot->GetGUID() : ObjectGuid::Empty, direct);
        if (result.code.empty())
        {
            ++started;
            AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(triggerId);
            rec.S("status", "started").B("inbox", at && bot->IsInAreaTrigger(at)).B("direct", direct);
        }
        else
        {
            ++refused;
            if (bot == leaderBot)
                leaderRefused = true;
            rec.S("status", "refused").S("code", result.code).T("msg", result.text);
        }
        reply.Line(rec);
    }
    Rec end("enter");
    end.N("trigger", triggerId).N("map", targetMap).P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("started", started).N("refused", refused);
    reply.End(end);
    return true;
}

// ---- `.tb instance <leader> info|reset|leave` ----
namespace
{
// The instance the command is about: the leader's own map when it is an instance; else, with `map`, the one the leader's party (or the leader) was last in
// for that map; else the instance a member of the party stands in.
InstanceMap* FindInstance(Player* leader, uint32 mapId, std::string& why)
{
    Group* group = leader->GetGroup();
    if (mapId)
    {
        uint32 const instanceId = group ? group->GetRecentInstanceId(mapId) : leader->GetRecentInstanceId(mapId);
        if (!instanceId)
        {
            why = "the party has no recent instance of that map";
            return nullptr;
        }
        Map* map = sMapMgr->FindMap(mapId, instanceId);
        if (!map || !map->ToInstanceMap())
        {
            why = "the party's recent instance of that map is not loaded any more";
            return nullptr;
        }
        return map->ToInstanceMap();
    }
    if (Map* map = leader->GetMap(); map && map->ToInstanceMap())
        return map->ToInstanceMap();
    if (group)
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (Player* member = ObjectAccessor::FindPlayer(slot.guid); member && member->IsInWorld() && member->GetMap() && member->GetMap()->ToInstanceMap())
                return member->GetMap()->ToInstanceMap();
    why = "the leader and its party are outside every instance: add `map <id>`";
    return nullptr;
}

struct SpawnCounts
{
    uint32 total = 0, alive = 0, loaded = 0;
    std::vector<std::vector<std::string>> bosses;   // {dungeon encounter id (the boss index when the script names none), alive|dead}
};

// Creatures of the instance: every creature spawn of the map (and difficulty) in the world database is "total"; a spawn is alive when its loaded creature
// lives, or, when it is not loaded, when the map holds no future respawn time for it (a killed creature keeps one); `loaded` = creatures the map holds now.
SpawnCounts CountSpawns(InstanceMap* map)
{
    SpawnCounts counts;
    time_t const now = GameTime::GetGameTime();
    Difficulty const difficulty = map->GetDifficultyID();
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        if (data.mapId != map->GetId())
            continue;
        if (!data.spawnDifficulties.empty() && std::find(data.spawnDifficulties.begin(), data.spawnDifficulties.end(), difficulty) == data.spawnDifficulties.end())
            continue;
        ++counts.total;
        bool alive = true;
        auto const range = map->GetCreatureBySpawnIdStore().equal_range(spawnId);
        if (range.first != range.second)
        {
            alive = false;
            for (auto it = range.first; it != range.second; ++it)
            {
                ++counts.loaded;
                alive = alive || it->second->IsAlive();
            }
        }
        else if (map->GetCreatureRespawnTime(spawnId) > now)
            alive = false;
        if (alive)
            ++counts.alive;
    }
    return counts;
}
}

bool HandleInstance(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "instance"))
        return false;
    if (args.w.empty())
        return Fail(handler, args, "instance", "missing-leader", ".tb instance <leader> info|reset|leave");
    Player* leader = FindOnline(args.w[0]);
    if (!leader)
        return Fail(handler, args, "instance", "no-such-player", args.w[0] + " is not in the world");
    if (args.w.size() < 2)
        return Fail(handler, args, "instance", "missing-verb", "info, reset or leave");
    std::string const verb = Lower(args.w[1]);
    if (verb != "info" && verb != "reset" && verb != "leave")
        return Fail(handler, args, "instance", "unknown-verb", verb);
    // N4 (package N): the same rule as every command that names a leader (C3): a host bot, or the sender's own character. `info` no longer shows a stranger's
    // instance, `leave` no longer moves a stranger's bots.
    if (!MayLeadBots(handler, leader->GetGUID()))
        return Fail(handler, args, "instance", "not-allowed", args.w[0] + " is neither a host bot nor your own character");
    if (verb == "reset" || verb == "leave")
    {
        std::string code, detail;
        if (!CheckPartyLeadAccess(handler, leader, code, detail))
            return Fail(handler, args, "instance", code, detail);
    }
    uint32 mapId = 0;
    if (args.w.size() > 2)
    {
        if (verb != "info" || Lower(args.w[2]) != "map")
            return Fail(handler, args, "instance", "unexpected-argument", args.w[2]);
        if (args.w.size() < 4)
            return Fail(handler, args, "instance", "missing-argument", "map <map id>");
        if (!ParseU32(args.w[3], mapId))
            return Fail(handler, args, "instance", "bad-id", args.w[3]);
        if (args.w.size() > 4)
            return Fail(handler, args, "instance", "unexpected-argument", args.w[4]);
    }

    if (verb == "info")
    {
        std::string why;
        InstanceMap* map = FindInstance(leader, mapId, why);
        if (!map)
            return Fail(handler, args, "instance", "target-not-found", why);
        SpawnCounts const counts = CountSpawns(map);
        uint32 gms = 0;
        for (MapReference const& ref : map->GetPlayers())
            if (Player* player = ref.GetSource(); player && player->IsGameMaster())
                ++gms;
        Group* group = leader->GetGroup();
        InstanceScript const* script = map->GetInstanceScript();
        // M4 live finding: the bosses are NOT found through the creature template: the core strips CREATURE_FLAG_EXTRA_DUNGEON_BOSS from the database
        // value (CREATURE_FLAG_EXTRA_DB_ALLOWED, CreatureData.h:382), so the old spawn scan listed none. The instance script's own boss table is the truth:
        // one entry per boss, named by its DungeonEncounter id (the boss index when the script names none), dead = the boss state is DONE.
        SpawnCounts counts2 = counts;
        counts2.bosses.clear();
        for (uint32 boss = 0; script && boss < script->GetEncounterCount(); ++boss)
        {
            DungeonEncounterEntry const* encounter = script->GetBossDungeonEncounter(boss);
            counts2.bosses.push_back({ std::to_string(encounter ? encounter->ID : boss), script->GetBossState(boss) == DONE ? "dead" : "alive" });
        }
        Reply reply(handler, args, "instance");
        reply.Begin();
        Rec rec("instance");
        rec.S("leader", leader->GetName()).N("map", map->GetId()).S("name", map->GetMapName()).N("inst", map->GetInstanceId()).N("diff", uint32(map->GetDifficultyID()));
        rec.N("players", map->GetPlayersCountExceptGMs()).N("gms", gms);
        rec.N("mobs_alive", counts.alive).N("mobs_total", counts.total).N("mobs_loaded", counts.loaded).LP("boss", counts2.bosses);
        rec.N("entrance", script ? script->GetEntranceLocation() : 0);
        rec.N("recent", group ? group->GetRecentInstanceId(map->GetId()) : leader->GetRecentInstanceId(map->GetId()));
        rec.S("owner", map->GetOwningGroup() ? std::string(map->GetOwningGroup()->GetLeaderName()) : std::string());
        reply.Line(rec);
        Rec end("instance");
        end.S("verb", "info").N("map", map->GetId()).N("inst", map->GetInstanceId());
        reply.End(end);
        return true;
    }

    if (verb == "leave")
    {
        std::string why;
        InstanceMap* map = FindInstance(leader, 0, why);
        if (!map)
            return Fail(handler, args, "instance", "target-not-found", why);
        AreaTriggerTeleport const* exitTrigger = sObjectMgr->GetGoBackTrigger(map->GetId());
        if (!exitTrigger)
            return Fail(handler, args, "instance", "no-trigger", "no area trigger leads out of that map (GetGoBackTrigger found none)");
        // Every host bot on that instance (the party and the commander), each through the exit trigger's own target.
        // N4: only the leader's own people: the leader, the members of its party, the commander of any of them and the bots the leader commands. A host bot of
        // another party or another commander that happens to stand on the same map stays where it is.
        std::set<ObjectGuid> mine;
        mine.insert(leader->GetGUID());
        if (Group* leaderGroup = leader->GetGroup())
            for (Group::MemberSlot const& slot : leaderGroup->GetMemberSlots())
                mine.insert(slot.guid);
        std::vector<Player*> inside;
        for (MapReference const& ref : map->GetPlayers())
            if (Player* player = ref.GetSource(); player && IsHostBot(player))
            {
                ObjectGuid const commander = CommanderGuid(player->GetGUID());
                if (mine.count(player->GetGUID()) || (!commander.IsEmpty() && (commander == leader->GetGUID() || mine.count(commander))))
                    inside.push_back(player);
            }
        // the commander of a party member (outside the party) is one of ours as well
        for (Player* member : std::vector<Player*>(inside))
            if (Player* commander = CommanderOf(member); commander && commander->GetMap() == map && IsHostBot(commander) && std::find(inside.begin(), inside.end(), commander) == inside.end())
                inside.push_back(commander);
        std::sort(inside.begin(), inside.end(), [](Player const* a, Player const* b) { return a->GetName() < b->GetName(); });
        Reply reply(handler, args, "instance");
        reply.Begin();
        uint32 started = 0, refused = 0;
        for (Player* bot : inside)
        {
            Rec rec("leave");
            rec.S("bot", bot->GetName()).N("map", exitTrigger->Loc.GetMapId());
            if (!bot->IsAlive())
            {
                // M4 live finding: a dead bot that was moved out by `leave` was released OUTSIDE by its engine and became a ghost at Razor Hill. A body stays in
                // the dungeon until it is raised (the priest, `.tb revive`) or released inside (the entrance rule): the leave does less, and says why.
                ++refused;
                rec.S("status", "refused").S("code", "dead").T("msg", "the bot is dead: its body stays in the dungeon until it is raised or released (`.tb revive` first)");
            }
            else if (bot->IsBeingTeleported())
            {
                ++refused;
                rec.S("status", "refused").S("code", "teleporting").T("msg", "the bot is being teleported");
            }
            else if (!bot->TeleportTo(exitTrigger->Loc))
            {
                ++refused;
                rec.S("status", "refused").S("code", "teleport-refused").T("msg", "Player::TeleportTo refused the teleport");
            }
            else
            {
                ++started;
                BotMgr::Instance().NoteTeleportStarted(bot->GetGUID(), "order", exitTrigger->Loc.GetMapId());
                rec.S("status", "started");
            }
            reply.Line(rec);
        }
        Rec end("instance");
        end.S("verb", "leave").N("map", map->GetId()).N("inst", map->GetInstanceId()).N("bots", inside.size()).N("started", started).N("refused", refused);
        reply.End(end);
        return true;
    }

    // ---- reset: the core's rules (HandleResetInstancesOpcode MiscHandler.cpp:985-1003, Group::ResetInstances Group.cpp:1379, InstanceMap::Reset Map.cpp:3355) ----
    if (!IsHostBot(leader))
        return Fail(handler, args, "instance", "not-a-bot", args.w[0] + " is not a logged-in host bot");
    if (Map* here = leader->GetMap(); here && here->Instanceable())      // the opcode returns silently for a leader inside an instance
        return Fail(handler, args, "instance", "leader-inside", "the core ignores a reset sent from inside an instance: the leader must be outside");
    Group* group = leader->GetGroup();
    if (!group)
        return Fail(handler, args, "instance", "no-party", args.w[0] + " is not in a party");
    if (!group->IsLeader(leader->GetGUID()))
        return Fail(handler, args, "instance", "not-leader", args.w[0] + " is not the leader of its party: the core resets only for the leader");
    if (group->isLFGGroup())
        return Fail(handler, args, "instance", "lfg-group", "the core does not reset an LFG party's instances by hand");
    std::vector<InstanceMap*> owned;
    sMapMgr->DoForAllMaps([&](Map* map)
    {
        if (InstanceMap* instance = map->ToInstanceMap(); instance && instance->GetOwningGroup() == group)
            owned.push_back(instance);
    });
    if (owned.empty())
        return Fail(handler, args, "instance", "target-not-found", "the party owns no instance");
    for (InstanceMap* instance : owned)
    {
        if (instance->HavePlayers())      // a bot, a body, a GM: InstanceMap::Reset answers NotEmpty and notifies the players (Map.cpp:3372-3382)
            return Fail(handler, args, "instance", "not-empty", Trinity::StringFormat("someone is still on map {} instance {} (a bot, a body or a GM): the core would not reset it", instance->GetId(), instance->GetInstanceId()));
        if (InstanceLock const* lock = instance->GetInstanceLock(); lock && !lock->IsNew())
            return Fail(handler, args, "instance", "locked", Trinity::StringFormat("map {} instance {} has a lock (a boss was killed): the core cannot reset it", instance->GetId(), instance->GetInstanceId()));
    }

    // Everything the core would refuse is refused above; now the core's own call, and the outcome is READ afterwards: a successful reset makes the group
    // forget the instance (Group.cpp:1388), so the next entry opens a new one.
    std::vector<std::pair<uint32, uint32>> ids;
    for (InstanceMap* instance : owned)
        ids.emplace_back(instance->GetId(), instance->GetInstanceId());
    group->ResetInstances(InstanceResetMethod::Manual, leader);
    Reply reply(handler, args, "instance");
    reply.Begin();
    uint32 ok = 0, failed = 0;
    for (auto const& [mapId2, instanceId] : ids)
    {
        bool const forgotten = group->GetRecentInstanceId(mapId2) != instanceId;
        Rec rec("reset");
        rec.N("map", mapId2).N("inst", instanceId);
        if (forgotten)
        {
            ++ok;
            rec.S("status", "ok");
        }
        else
        {
            ++failed;
            rec.S("status", "failed").S("code", "other").T("msg", "the group still names the instance after the reset call");
        }
        reply.Line(rec);
    }
    Rec end("instance");
    end.S("verb", "reset").S("leader", leader->GetName()).N("instances", ids.size()).N("ok", ok).N("failed", failed);
    reply.End(end);
    return true;
}

// ---- `.tb tele` ----
namespace
{
// One teleport of `source` next to `dest` (the destination's map; into an instance: the destination's instance). Fills the record; true = the core started the teleport.
bool TeleportNextTo(Player* source, Player* dest, bool revive, Rec& rec)
{
    auto const refuse = [&](char const* code, std::string const& text)
    {
        rec.S("status", "refused").S("code", code).T("msg", text);
        return false;
    };
    if (source == dest)
        return refuse("bad-target", "a character cannot be teleported to itself");
    if (source->IsBeingTeleported() || dest->IsBeingTeleported())
        return refuse("teleporting", "one of the two is being teleported");
    if (source->IsInCombat() || dest->IsInCombat())
        return refuse("in-combat", "one of the two is in combat");
    if (!dest->IsAlive())
        return refuse("dead", dest->GetName() + " is dead");
    if (!source->IsAlive() && !revive)
        return refuse("dead", source->GetName() + " is dead (add `revive` to raise it at the teleport)");
    Map* destMap = dest->GetMap();
    uint32 instanceId = dest->GetInstanceId();
    if (destMap && destMap->Instanceable())
    {
        if (source->GetMapId() == dest->GetMapId() && source->GetInstanceId() != instanceId)
            return refuse("already-on-map", source->GetName() + " stands in another instance of that map: it must leave it first (the core aborts an entry check for a player on that map)");
        // The core's group bind rule: a GM may join any instance (InstanceMap::CannotEnter, Map.cpp:3145-3146); anyone else only the instance its own group owns.
        if (!source->IsGameMaster() && (!source->GetGroup() || source->GetGroup() != dest->GetGroup()))
            return refuse("not-in-group", source->GetName() + " is neither a GM nor in the party of " + dest->GetName() + ": the core would refuse it the instance");
        // N8 (package N): being in the same party is not enough: the instance must be the one that party OWNS (Map.cpp:3145-3146, the owning-group test), or the core
        // refuses at the worldport ack and sends the bot HOME (which does more than asked). Read with the public InstanceMap::GetOwningGroup.
        if (!source->IsGameMaster())
        {
            InstanceMap* const destInstance = destMap->ToInstanceMap();
            if (!destInstance || destInstance->GetOwningGroup() != source->GetGroup())
                return refuse("not-in-group", "the instance " + dest->GetName() + " stands in is not owned by " + source->GetName() + "'s party: the core would refuse it and send it home");
        }
        if (source->GetMapId() != dest->GetMapId())
            if (BotMgr::EntryCheck const check = BotMgr::CheckEntry(source, dest->GetMapId()); !check.code.empty())
                return refuse(check.code.c_str(), check.text);
    }
    TeleportToOptions const options = revive && !source->IsAlive() ? TELE_REVIVE_AT_TELEPORT : TELE_TO_NONE;
    bool const started = destMap && destMap->Instanceable() ? source->TeleportTo(dest->GetWorldLocation(), options, instanceId) : source->TeleportTo(dest->GetWorldLocation(), options);
    if (!started)
        return refuse("teleport-refused", "Player::TeleportTo refused the teleport (see the server log)");
    if (IsHostBot(source))
    {
        BotMgr::Instance().NoteTeleportStarted(source->GetGUID(), "setup", dest->GetMapId());
        if (revive)
            BotMgr::Instance().MarkPractice(source->GetGUID());
    }
    rec.S("status", "started").N("map", dest->GetMapId()).N("inst", instanceId).S("tag", revive ? "PRACTICE" : "SETUP");
    return true;
}
}

bool HandleTele(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "tele"))
        return false;
    if (args.w.empty())
        return Fail(handler, args, "tele", "missing-target", ".tb tele <character> to <character>  or  .tb tele <bot|party [leader]> <map> <x> <y> <z> [o]");
    Player* sender = SenderPlayer(handler);

    // Form 1: `.tb tele <character> to <character> [revive]` (Q4).
    if (args.w.size() >= 3 && Lower(args.w[1]) == "to")
    {
        bool revive = false;
        if (args.w.size() > 3)
        {
            if (Lower(args.w[3]) != "revive" || args.w.size() > 4)
                return Fail(handler, args, "tele", "unexpected-argument", args.w[3]);
            revive = true;
        }
        Player* source = FindOnline(args.w[0]);
        if (!source)
            return Fail(handler, args, "tele", "no-such-player", args.w[0] + " is not in the world");
        Player* dest = FindOnline(args.w[2]);
        if (!dest)
            return Fail(handler, args, "tele", "no-such-player", args.w[2] + " is not in the world");
        // C3: a character named in a command must be a host bot or the sender's own character.
        if (!MayLeadBots(handler, source->GetGUID()))
            return Fail(handler, args, "tele", "not-allowed", args.w[0] + " is neither a host bot nor your own character");
        if (IsHostBot(source))
        {
            std::string code, detail;
            if (!CheckBotAccess(handler, source, code, detail))
                return Fail(handler, args, "tele", code, detail);
        }
        if (!MayLeadBots(handler, dest->GetGUID()))
            return Fail(handler, args, "tele", "not-allowed", args.w[2] + " is neither a host bot nor your own character");
        Reply reply(handler, args, "tele");
        reply.Begin();
        Rec rec("tele");
        rec.S("bot", source->GetName()).S("to", dest->GetName());
        bool const started = TeleportNextTo(source, dest, revive, rec);
        reply.Line(rec);
        Rec end("tele");
        end.S("target", source->GetName()).N("bots", 1).N("started", started ? 1 : 0).N("refused", started ? 0 : 1);
        reply.End(end);
        return true;
    }

    // Form 2: `.tb tele <bot|party [leader]> <map> <x> <y> <z> [o] [revive]` (test setup).
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party" && !IsNumberWord(args.w[1]);
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, sender, leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "tele", who.code, who.detail);
    uint32 mapId = 0;
    float x = 0, y = 0, z = 0, o = 0;
    if (args.w.size() < pos + 4)
        return Fail(handler, args, "tele", "missing-argument", "<map> <x> <y> <z> [o]");
    if (!ParseU32(args.w[pos], mapId))
        return Fail(handler, args, "tele", "bad-id", args.w[pos]);
    if (!ParseFloat(args.w[pos + 1], x) || !ParseFloat(args.w[pos + 2], y) || !ParseFloat(args.w[pos + 3], z))
        return Fail(handler, args, "tele", "bad-argument", "x, y and z are numbers");
    pos += 4;
    if (pos < args.w.size() && IsNumberWord(args.w[pos]))
        ParseFloat(args.w[pos++], o);
    bool revive = false;
    if (pos < args.w.size())
    {
        if (Lower(args.w[pos]) != "revive" || pos + 1 < args.w.size())
            return Fail(handler, args, "tele", "unexpected-argument", args.w[pos]);
        revive = true;
    }
    if (!sMapStore.LookupEntry(mapId))
        return Fail(handler, args, "tele", "bad-id", "no such map");

    Reply reply(handler, args, "tele");
    reply.Begin();
    uint32 started = 0, refused = 0;
    for (Player* bot : who.bots)
    {
        Rec rec("tele");
        rec.S("bot", bot->GetName());
        std::string code, text;
        if (bot->IsBeingTeleported())
            code = "teleporting", text = "the bot is being teleported";
        else if (bot->IsInCombat())
            code = "in-combat", text = "the bot is in combat";
        else if (!bot->IsAlive() && !revive)
            code = "dead", text = "the bot is dead (add `revive`)";
        else if (bot->GetMapId() != mapId)
        {
            // The core's entry question (the one guarded function); a same-map teleport asks nothing.
            if (BotMgr::EntryCheck const check = BotMgr::CheckEntry(bot, mapId); !check.code.empty())
                code = check.code, text = check.text;
        }
        if (!code.empty())
        {
            ++refused;
            rec.S("status", "refused").S("code", code).T("msg", text);
        }
        else if (!bot->TeleportTo(WorldLocation(mapId, x, y, z, o), revive && !bot->IsAlive() ? TELE_REVIVE_AT_TELEPORT : TELE_TO_NONE))
        {
            ++refused;
            rec.S("status", "refused").S("code", "teleport-refused").T("msg", "Player::TeleportTo refused the teleport (see the server log)");
        }
        else
        {
            ++started;
            BotMgr::Instance().NoteTeleportStarted(bot->GetGUID(), "setup", mapId);
            if (revive)
                BotMgr::Instance().MarkPractice(bot->GetGUID());
            rec.S("status", "started").N("map", mapId).Coord("pos", x, y, z).S("tag", revive ? "PRACTICE" : "SETUP");
        }
        reply.Line(rec);
    }
    Rec end("tele");
    end.P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("started", started).N("refused", refused);
    reply.End(end);
    return true;
}

// ---- `.tb revive <bot|party [leader]>` (PRACTICE) ----
// The driver's shortcut: the dead bot is raised in place (Player::ResurrectPlayer(1.0f), its corpse bones removed) with full health; every reply line and a log
// line carry PRACTICE, and the bot's next state records say `practice=yes` until the next combat ends. A bot that is not dead is refused (nothing to raise).
bool HandleRevive(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "revive"))
        return false;
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "revive", who.code, who.detail);
    if (pos < args.w.size())
        return Fail(handler, args, "revive", "unexpected-argument", args.w[pos]);
    Reply reply(handler, args, "revive");
    reply.Begin();
    uint32 revived = 0, refused = 0;
    for (Player* bot : who.bots)
    {
        Rec rec("revive");
        rec.S("bot", bot->GetName()).S("tag", "PRACTICE");
        if (bot->IsAlive())
        {
            ++refused;
            rec.S("status", "refused").S("code", "not-dead").T("msg", "the bot is alive");
        }
        else
        {
            bot->ResurrectPlayer(1.0f);
            bot->SpawnCorpseBones();
            if (bot->IsAlive())
            {
                ++revived;
                BotMgr::Instance().MarkPractice(bot->GetGUID());
                rec.S("status", "revived").B("practice", true);
                TC_LOG_INFO("tortoisebots", "PRACTICE: `.tb revive` raised {} in place", bot->GetName());
            }
            else
            {
                ++refused;
                rec.S("status", "refused").S("code", "other").T("msg", "Player::ResurrectPlayer left the bot dead");
            }
        }
        reply.Line(rec);
    }
    Rec end("revive");
    end.S("tag", "PRACTICE").P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("revived", revived).N("refused", refused);
    reply.End(end);
    return true;
}

// ---- `.tb kill <bot|party [leader]>` (test setup, as `.die`) ----
bool HandleKill(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "kill"))
        return false;
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "kill", who.code, who.detail);
    if (pos < args.w.size())
        return Fail(handler, args, "kill", "unexpected-argument", args.w[pos]);
    Reply reply(handler, args, "kill");
    reply.Begin();
    uint32 killed = 0, refused = 0;
    for (Player* bot : who.bots)
    {
        Rec rec("kill");
        rec.S("bot", bot->GetName()).S("tag", "SETUP");
        if (!bot->IsAlive())
        {
            ++refused;
            rec.S("status", "refused").S("code", "dead").T("msg", "the bot is dead already");
        }
        else
        {
            bot->KillSelf();
            if (!bot->IsAlive())
            {
                ++killed;
                rec.S("status", "killed");
            }
            else
            {
                ++refused;
                rec.S("status", "refused").S("code", "other").T("msg", "the bot is still alive after Unit::KillSelf");
            }
        }
        reply.Line(rec);
    }
    Rec end("kill");
    end.P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("killed", killed).N("refused", refused);
    reply.End(end);
    return true;
}

// ---- `.tb gm <character> on|off` ----
// Player::SetGameMaster (Player.h:1336): a GM passes InstanceMap::CannotEnter before the cap and owning-group tests (Map.cpp:3145-3146), takes no seat in an instance
// and is valid in every instance (Player::CheckInstanceValidity, Player.cpp:21617). The commander of a party is a GM that stands outside the party.
bool HandleGm(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "gm"))
        return false;
    if (args.w.empty())
        return Fail(handler, args, "gm", "missing-target", ".tb gm <character> on|off");
    Player* player = FindOnline(args.w[0]);
    if (!player)
        return Fail(handler, args, "gm", "no-such-player", args.w[0] + " is not in the world");
    if (!MayLeadBots(handler, player->GetGUID()))
        return Fail(handler, args, "gm", "not-allowed", args.w[0] + " is neither a host bot nor your own character");
    if (IsHostBot(player))
    {
        std::string code, detail;
        if (!CheckBotAccess(handler, player, code, detail))
            return Fail(handler, args, "gm", code, detail);
    }
    if (args.w.size() < 2)
        return Fail(handler, args, "gm", "missing-argument", "on or off");
    std::string const word = Lower(args.w[1]);
    if (word != "on" && word != "off")
        return Fail(handler, args, "gm", "bad-argument", "on or off");
    if (args.w.size() > 2)
        return Fail(handler, args, "gm", "unexpected-argument", args.w[2]);
    bool const on = word == "on";
    player->SetGameMaster(on);
    Reply reply(handler, args, "gm");
    reply.Begin();
    Rec rec("gm");
    rec.S("bot", player->GetName()).S("tag", "SETUP");
    if (player->IsGameMaster() == on)
        rec.S("status", "ok").B("gm", on);
    else
        rec.S("status", "refused").S("code", "other").T("msg", "Player::SetGameMaster did not change the flag").B("gm", player->IsGameMaster());
    reply.Line(rec);
    Rec end("gm");
    end.S("target", player->GetName()).B("gm", player->IsGameMaster());
    reply.End(end);
    if (player->IsGameMaster() != on)
        handler->SetSentErrorMessage(true);
    return player->IsGameMaster() == on;
}
}
}

#else   // not built with the slice: the same refusal the slice-3 stub gives (TbH3Off.cpp)

namespace TortoiseBots
{
namespace h4
{
namespace
{
std::string TokenOf(std::string_view tail)
{
    size_t end = tail.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(tail[end - 1])))
        --end;
    size_t start = end;
    while (start > 0 && !std::isspace(static_cast<unsigned char>(tail[start - 1])))
        --start;
    std::string_view const word = tail.substr(start, end - start);
    if (word.size() < 2 || word.size() > 33 || word[0] != '#')
        return {};
    for (size_t i = 1; i < word.size(); ++i)
    {
        char const c = word[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != ':' && c != '-')
            return {};
    }
    return std::string(word);
}

bool NotBuilt(ChatHandler* handler, Trinity::ChatCommands::Tail args, char const* cmd)
{
    static char const* const text = "the engine is not built in (this build has no group-fight commands)";
    std::string msg;
    for (char const* c = text; *c; ++c)
    {
        unsigned char const u = static_cast<unsigned char>(*c);
        if (std::isalnum(u) || u == '.' || u == '_' || u == '-')
            msg += *c;
        else
        {
            static char const* const hex = "0123456789ABCDEF";
            msg += '%';
            msg += hex[u >> 4];
            msg += hex[u & 15];
        }
    }
    std::string line = std::string("error code=engine-not-built cmd=") + cmd + " msg=" + msg;
    std::string const token = TokenOf(std::string_view(args));
    if (!token.empty())
        line += " " + token;
    handler->SendSysMessage(line);
    handler->SetSentErrorMessage(true);
    return false;
}
}

bool HandleTrigger(ChatHandler* handler, Trinity::ChatCommands::Tail args)  { return NotBuilt(handler, args, "trigger"); }
bool HandleEnter(ChatHandler* handler, Trinity::ChatCommands::Tail args)    { return NotBuilt(handler, args, "enter"); }
bool HandleInstance(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return NotBuilt(handler, args, "instance"); }
bool HandleTele(ChatHandler* handler, Trinity::ChatCommands::Tail args)     { return NotBuilt(handler, args, "tele"); }
bool HandleRevive(ChatHandler* handler, Trinity::ChatCommands::Tail args)   { return NotBuilt(handler, args, "revive"); }
bool HandleKill(ChatHandler* handler, Trinity::ChatCommands::Tail args)     { return NotBuilt(handler, args, "kill"); }
bool HandleGm(ChatHandler* handler, Trinity::ChatCommands::Tail args)       { return NotBuilt(handler, args, "gm"); }
}
}

#endif
