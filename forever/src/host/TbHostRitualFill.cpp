// Issue #14 round 1: the ritual fill (see host/TbHostRitualFill.h for what this is for). The decisions are host/TbObjectUsePure.h;
// this file only fills facts from the core, holds the bots and walks them.
//
// Three sources feed one fill:
//   * a real player's `CMSG_GAME_OBJ_USE` (OnPacket) of a type-18 object: the scripted altars never reach the core's own counter,
//     so their users are counted from the clicks plus the bots this fill sent (TbObjectUsePure.h Users);
//   * the groups of the world (once a second): a channeling member whose spell-created object is type 18 is a summoned ritual
//     (Ritual of Summoning); its owner must keep the channel or every use is refused (GameObject.cpp:3169-3170);
//   * the holds: each held bot walks, uses the object once and stands still. Every second it is re-judged (TbObjectUsePure.h
//     JudgeHold) against the object, the channel, its own state and HoldSeconds.
#include "host/TbHostRitualFill.h"

#include "host/TbObjectUsePure.h"

#include "compat/botpch_forever.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "compat/TbCompat_Unit.h"

#include "Config.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "TbBotMgr.h"
#include "host/chat/TbChatIn.h"
#include "host/run/TbRunHooks.h"
#include "host/run/TbRunTasks.h"
#include "runtime/PlayerbotAIStorage.h"
#include "runtime/RemoteCommanderTasks.h"

#include "playerbot/PlayerbotAI.h"

namespace tbhost
{
namespace ritualfill
{
namespace
{
// The fill's own clock (the engine reads the same holds every AI tick).
constexpr std::uint32_t kTickMs = 1000;

std::uint64_t NowMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool Enabled()
{
    return sConfigMgr->GetBoolDefault("TortoiseBots.RitualFill.Enable", true, true);
}

float FillRangeYd()
{
    // Whole yards, the same integer `.tb feature` shows (TbAddonInfo.cpp kFeatures).
    std::int32_t const yards = std::clamp<std::int32_t>(sConfigMgr->GetIntDefault("TortoiseBots.RitualFill.Range",
        std::int32_t(TortoiseBots::objuse::kDefaultRangeYd), true), 1, 100);
    return float(yards);
}

std::uint64_t HoldMs()
{
    std::int32_t seconds = std::clamp<std::int32_t>(sConfigMgr->GetIntDefault("TortoiseBots.RitualFill.HoldSeconds", 120, true), 1, 600);
    return std::uint64_t(seconds) * 1000ull;
}

// A real player's use of one object, copied off the packet thread. Nothing here is resolved yet.
struct Click
{
    ObjectGuid player;
    ObjectGuid go;
    std::uint64_t ms = 0;
};

// One object the fill works on. A click on the same object refreshes it; an owned one is found again by the channel scan.
struct Target
{
    ObjectGuid go;
    std::uint32_t mapId = 0;
    ObjectGuid initiator;            // the real player whose group fills it
    ObjectGuid owner;                // the summoning caster, for an owned ritual
    bool owned = false;
    std::uint32_t casters = 0;
    std::vector<ObjectGuid> clickers;   // the real players who clicked it
    std::set<ObjectGuid> used;       // bots that already used this object: once per object per bot
    std::uint64_t lastSeenMs = 0;    // last click, hold step or channel scan: the target's memory window
};

// One bot held at one object.
struct Hold
{
    ObjectGuid bot;
    ObjectGuid go;
    ObjectGuid initiator;
    ObjectGuid owner;
    bool owned = false;
    bool named = false;              // round 2's `use <object>`; nothing sets it in round 1
    std::uint32_t casters = 0;
    std::uint32_t mapId = 0;
    std::uint64_t bornMs = 0;
    std::uint32_t maxUsers = 0;      // the highest use count seen since the assignment
    bool used = false;
};

std::mutex& ClickLock()
{
    static std::mutex lock;
    return lock;
}

TortoiseBots::objuse::ClickQueue<Click>& Clicks()
{
    static TortoiseBots::objuse::ClickQueue<Click> clicks;
    return clicks;
}

std::map<ObjectGuid, Target>& Targets()
{
    static std::map<ObjectGuid, Target> targets;
    return targets;
}

std::map<ObjectGuid, Hold>& Holds()
{
    static std::map<ObjectGuid, Hold> holds;
    return holds;
}

std::uint32_t& ElapsedMs()
{
    static std::uint32_t elapsed = 0;
    return elapsed;
}

Player* FindPlayer(ObjectGuid guid)
{
    if (guid.IsEmpty())
        return nullptr;
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    return player && player->IsInWorld() ? player : nullptr;
}

GameObject* FindObject(Player* by, std::uint32_t mapId, ObjectGuid guid)
{
    if (!by || by->GetMapId() != mapId || !by->FindMap() || guid.IsEmpty())
        return nullptr;
    return by->GetMap()->GetGameObject(guid);
}

// The caster's channel runs and the object it created is this one (`Unit::GetGameObject`, Unit.h:1677).
bool OwnerChannels(Player* owner, GameObject* go)
{
    if (!owner || !go || !owner->IsInWorld())
        return false;
    Spell* channel = owner->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!channel || !channel->GetSpellInfo())
        return false;
    GameObject* summon = owner->GetGameObject(channel->GetSpellInfo()->Id);
    return summon && summon->GetGUID() == go->GetGUID();
}

bool HeldByCommander(Player* bot)
{
    return TortoiseBots::RemoteCommander::RitualHolds().Holds(GuidKeyCompat(bot->GetGUID()));
}

// Releases the engine hold and stands the bot down. The bot's own AI takes it from its next tick.
void ReleaseHold(std::map<ObjectGuid, Hold>::iterator it, char const* code)
{
    Player* bot = FindPlayer(it->second.bot);
    TortoiseBots::RemoteCommander::RitualHolds().Release(GuidKeyCompat(it->second.bot));
    if (bot)
    {
        if (bot->isMoving())
            bot->StopMoving();
        if (bot->GetMotionMaster() && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        {
            bot->GetMotionMaster()->Clear();
            bot->GetMotionMaster()->MoveIdle();
        }
        sLog.outDetail("ritual-fill: bot=%s object=%s release=%s used=%d", bot->GetName().c_str(),
            it->second.go.ToString().c_str(), code, it->second.used ? 1 : 0);
    }
    Holds().erase(it);
}

void ReleaseAll(char const* code)
{
    while (!Holds().empty())
        ReleaseHold(Holds().begin(), code);
    Targets().clear();
}

// World thread: the clicks the hook queued since the last tick become (or refresh) a fill target. A bot's own use, a dead object
// and anything that is not a type-18 ritual are dropped here.
void DrainClicks()
{
    std::vector<Click> clicks;
    {
        std::lock_guard<std::mutex> guard(ClickLock());
        clicks = Clicks().Drain();
    }
    for (Click const& click : clicks)
    {
        Player* player = FindPlayer(click.player);
        if (!player || TortoiseBots::BotMgr::Instance().IsBot(player->GetGUID()))
            continue;
        GameObject* go = FindObject(player, player->GetMapId(), click.go);
        if (!go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_RITUAL || !go->GetGOInfo())
            continue;
        std::uint32_t const casters = go->GetGOInfo()->ritual.casters;
        if (casters < 2)
            continue;

        Target& target = Targets()[go->GetGUID()];
        target.go = go->GetGUID();
        target.mapId = player->GetMapId();
        target.lastSeenMs = click.ms;
        target.casters = std::max(target.casters, casters);
        if (!go->GetOwnerGUID().IsEmpty())
        {
            target.owned = true;
            target.owner = go->GetOwnerGUID();
        }
        if (target.initiator.IsEmpty())
            target.initiator = player->GetGUID();
        bool refreshed = false;
        for (ObjectGuid const& clicker : target.clickers)
        {
            if (clicker == player->GetGUID())
            {
                refreshed = true;
                break;
            }
        }
        if (!refreshed)
            target.clickers.push_back(player->GetGUID());
    }
}

// World thread: every group that holds a bot, once a second, for a channeling member whose spell-created object is a type-18
// ritual. That is the summoned ritual (Ritual of Summoning), whether its caster is a player or a bot.
void ScanOwnedRituals()
{
    for (auto const& entry : sGroupMgr->GetGroupStore())
    {
        Group* group = entry.second;
        if (!group)
            continue;
        bool hasBot = false;
        Player* caster = nullptr;
        GameObject* ritual = nullptr;
        for (GroupReference const& reference : group->GetMembers())
        {
            Player* member = reference.GetSource();
            if (!member || !member->IsInWorld())
                continue;
            if (!hasBot && TortoiseBots::BotMgr::Instance().IsBot(member->GetGUID()))
                hasBot = true;
            if (caster)
                continue;
            Spell* channel = member->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (!channel || !channel->GetSpellInfo())
                continue;
            GameObject* go = member->GetGameObject(channel->GetSpellInfo()->Id);
            if (!go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_RITUAL || !go->GetGOInfo())
                continue;
            if (go->GetOwnerGUID() != member->GetGUID())
                continue;
            caster = member;
            ritual = go;
        }
        if (!hasBot || !caster || !ritual)
            continue;

        Target& target = Targets()[ritual->GetGUID()];
        target.go = ritual->GetGUID();
        target.mapId = caster->GetMapId();
        target.lastSeenMs = NowMs();
        target.owned = true;
        target.owner = caster->GetGUID();
        target.initiator = caster->GetGUID();
        target.casters = std::max(target.casters, ritual->GetGOInfo()->ritual.casters);
    }
}

struct PickList
{
    std::vector<TortoiseBots::objuse::Candidate> candidates;
    std::map<std::uint64_t, Player*> players;
};

// The initiator's group bots, as the picker reads them.
PickList BuildPickList(Player* initiator, GameObject* go, Target const& target)
{
    PickList list;
    Group* group = initiator ? initiator->GetGroup() : nullptr;
    if (!group)
        return list;
    for (GroupReference const& reference : group->GetMembers())
    {
        Player* member = reference.GetSource();
        if (!member || !member->IsInWorld() || !TortoiseBots::BotMgr::Instance().IsBot(member->GetGUID()))
            continue;
        if (member->GetGUID() == initiator->GetGUID() || (!target.owner.IsEmpty() && member->GetGUID() == target.owner))
            continue;
        if (Holds().count(member->GetGUID()))
            continue;

        TortoiseBots::objuse::Candidate candidate;
        candidate.guid = GuidKeyCompat(member->GetGUID());
        candidate.alive = member->IsAlive();
        candidate.inCombat = member->IsInCombat();
        candidate.used = target.used.count(member->GetGUID()) != 0;
        candidate.held = HeldByCommander(member);
        candidate.inRun = TortoiseBots::run::BotInRunningRun(member);
        candidate.inBattleground = member->InBattleground() || member->InArena();
        candidate.authorized = TortoiseBots::chatin::StillAuthorized(member, initiator);
        candidate.isTank = PlayerbotAI::IsTank(member);
        candidate.isHealer = PlayerbotAI::IsHeal(member);
        candidate.distance = member->GetDistance(go);
        list.players[candidate.guid] = member;
        list.candidates.push_back(candidate);
    }
    return list;
}

void AssignHold(Player* bot, GameObject* go, Player* initiator, Target const& target, std::uint64_t now)
{
    Hold hold;
    hold.bot = bot->GetGUID();
    hold.go = go->GetGUID();
    hold.initiator = initiator->GetGUID();
    hold.owner = target.owner;
    hold.owned = target.owned;
    hold.casters = target.casters;
    hold.mapId = bot->GetMapId();
    hold.bornMs = now;
    Holds()[bot->GetGUID()] = hold;

    // Hold at once: the engine must not move, cast or drink the bot off the object while it walks there.
    TortoiseBots::RemoteCommander::RitualHolds().Hold(GuidKeyCompat(bot->GetGUID()));
    if (bot->IsMounted())
        bot->Dismount();

    std::string const name = go->GetGOInfo()->name.empty() ? std::to_string(go->GetEntry()) : go->GetGOInfo()->name;
    if (initiator->GetSession())
        bot->Whisper("using " + name, LANG_UNIVERSAL, initiator);
    sLog.outDetail("ritual-fill: bot=%s held for object=%s (%s) by %s", bot->GetName().c_str(), name.c_str(),
        go->GetGUID().ToString().c_str(), initiator->GetName().c_str());
}

// World thread: one object, one second. Counts its users, picks what is missing and holds it.
void FillTarget(Target const& target, std::uint64_t now)
{
    Player* initiator = FindPlayer(target.initiator);
    if (!initiator)
        return;
    GameObject* go = FindObject(initiator, target.mapId, target.go);
    if (!go || !go->isSpawned())
        return;

    bool ownerChanneling = true;
    if (target.owned)
    {
        Player* owner = FindPlayer(target.owner);
        ownerChanneling = OwnerChannels(owner, go);
    }

    // The real players who clicked it and are still within the scripted altar's own distance.
    std::uint32_t recorded = 0;
    for (ObjectGuid const& clickerGuid : target.clickers)
    {
        Player* clicker = FindPlayer(clickerGuid);
        if (clicker && clicker->GetDistance(go) <= TortoiseBots::objuse::kScriptedUserYd)
            ++recorded;
    }

    std::uint32_t usedBots = 0, walking = 0;
    for (auto const& entry : Holds())
    {
        Hold const& hold = entry.second;
        if (hold.go != target.go)
            continue;
        if (hold.used)
            ++usedBots;
        else
            ++walking;
    }

    TortoiseBots::objuse::FillFacts facts;
    facts.ownedRitual = target.owned;
    facts.ownerChanneling = ownerChanneling;
    facts.casters = target.casters;
    facts.uniqueUses = go->GetUniqueUseCount();
    facts.recordedUsers = recorded;
    facts.heldBotsUsed = usedBots;
    facts.walking = walking;
    std::uint32_t const missing = TortoiseBots::objuse::MissingUsers(facts);
    if (!missing)
        return;

    PickList list = BuildPickList(initiator, go, target);
    if (list.candidates.empty())
        return;
    std::vector<std::uint64_t> picked = TortoiseBots::objuse::PickFillers(list.candidates, missing, FillRangeYd());
    for (std::uint64_t key : picked)
    {
        auto found = list.players.find(key);
        if (found != list.players.end() && found->second)
            AssignHold(found->second, go, initiator, target, now);
    }
}

// World thread: a target with no bot held is kept while its object still fills (so its `used` record outlives a release and a
// bot is not held twice for one object), for at most kTargetMemoryMs after it was last seen; otherwise it is dropped.
void PruneTargets(std::uint64_t now)
{
    for (auto it = Targets().begin(); it != Targets().end();)
    {
        Target& target = it->second;
        bool held = false;
        for (auto const& entry : Holds())
            if (entry.second.go == target.go)
            {
                held = true;
                break;
            }
        if (held)
        {
            target.lastSeenMs = now;
            ++it;
            continue;
        }
        Player* initiator = FindPlayer(target.initiator);
        GameObject* go = initiator ? FindObject(initiator, target.mapId, target.go) : nullptr;
        // A click can be newer than this tick's clock, so the age never goes below zero.
        std::uint64_t const seenAgoMs = now > target.lastSeenMs ? now - target.lastSeenMs : 0;
        if (TortoiseBots::objuse::RemembersTarget(go && go->isSpawned(), go && go->GetGoState() == GO_STATE_READY,
            seenAgoMs, TortoiseBots::objuse::kTargetMemoryMs))
        {
            ++it;
            continue;
        }
        it = Targets().erase(it);
    }
}

// World thread: re-judge every held bot and take its next step (walk, use once, stand still).
void StepHolds()
{
    std::uint64_t const now = NowMs();
    for (auto it = Holds().begin(); it != Holds().end();)
    {
        Hold& hold = it->second;
        Player* bot = FindPlayer(hold.bot);
        if (!bot)
        {
            ReleaseHold(it++, "gone");
            continue;
        }
        Player* initiator = FindPlayer(hold.initiator);
        if (!initiator || !TortoiseBots::chatin::StillAuthorized(bot, initiator))
        {
            ReleaseHold(it++, "not-authorized");
            continue;
        }

        GameObject* go = FindObject(bot, hold.mapId, hold.go);
        bool ownerChanneling = true;
        if (hold.owned)
            ownerChanneling = OwnerChannels(FindPlayer(hold.owner), go);
        std::uint32_t const users = go ? go->GetUniqueUseCount() : 0;
        if (users > hold.maxUsers)
            hold.maxUsers = users;

        TortoiseBots::objuse::HoldFacts facts;
        facts.named = hold.named;
        facts.objectPresent = go && go->isSpawned();
        facts.objectReady = go && go->GetGoState() == GO_STATE_READY;
        facts.ownedRitual = hold.owned;
        facts.ownerChanneling = ownerChanneling;
        facts.ritualComplete = hold.casters > 1 && users >= hold.casters;
        facts.ritualReset = hold.casters > 1 && hold.maxUsers >= hold.casters && users < hold.casters;
        facts.inCombat = bot->IsInCombat();
        facts.dead = !bot->IsAlive();
        facts.offMap = bot->IsBeingTeleported() || bot->GetMapId() != hold.mapId;
        facts.holdElapsed = now - hold.bornMs >= HoldMs();

        TortoiseBots::objuse::HoldVerdict verdict = TortoiseBots::objuse::JudgeHold(facts);
        if (verdict.release)
        {
            if (hold.used)
                if (auto target = Targets().find(hold.go); target != Targets().end())
                    target->second.used.insert(hold.bot);   // nobody re-sends a bot that already used it
            ReleaseHold(it++, verdict.code);
            continue;
        }

        if (hold.used)
        {
            if (bot->isMoving())
                bot->StopMoving();
        }
        else if (facts.objectPresent && bot->GetGameObjectIfCanInteractWith(hold.go, GAMEOBJECT_TYPE_RITUAL))
        {
            bot->StopMoving();
            if (TortoiseBots::run::tasks::SendGameObjectUse(bot, go))
            {
                hold.used = true;
                if (auto target = Targets().find(hold.go); target != Targets().end())
                    target->second.used.insert(hold.bot);
            }
        }
        else if (facts.objectPresent && !bot->IsBeingTeleported() && !bot->isMoving())
        {
            bot->GetMotionMaster()->MovePoint(0, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
        }
        ++it;
    }
}

void TickOnce()
{
    std::uint64_t const now = NowMs();
    DrainClicks();
    ScanOwnedRituals();
    for (auto const& entry : Targets())
        FillTarget(entry.second, now);
    StepHolds();
    PruneTargets(now);
}
}

void OnPacket(WorldSession* session, WorldPacket& packet)
{
    if (!session || packet.GetOpcode() != CMSG_GAME_OBJ_USE)
        return;
    if (!Enabled())
        return;                                  // the fill is off: record no packet
    Player* player = session->GetPlayer();
    if (!player)
        return;
    ObjectGuid botGuid;
    if (PlayerbotAIStorage::Instance().GetBotGuid(player, botGuid))
        return;                                  // a bot's own use: it is never a fill trigger
    WorldPacket copy(packet);                    // the hook runs before the handler reads it
    ObjectGuid go;
    copy >> go;
    if (go.IsEmpty())
        return;
    std::lock_guard<std::mutex> guard(ClickLock());
    Clicks().Record({ player->GetGUID(), go, NowMs() });
}

void Tick(std::uint32_t diffMs)
{
    ElapsedMs() += diffMs;
    if (ElapsedMs() < kTickMs)
        return;
    ElapsedMs() = 0;
    bool const enabled = Enabled();
    {
        std::lock_guard<std::mutex> guard(ClickLock());
        Clicks().SetEnabled(enabled);            // off: queued clicks are dropped, never replayed
    }
    if (!enabled)
    {
        ReleaseAll("disabled");
        return;
    }
    TickOnce();
}

void NoteLogout(Player* player)
{
    if (!player)
        return;
    if (auto hold = Holds().find(player->GetGUID()); hold != Holds().end())
        ReleaseHold(hold, "logout");
    for (auto it = Targets().begin(); it != Targets().end();)
        if (it->second.initiator == player->GetGUID())
            it = Targets().erase(it);
        else
            ++it;
}
}
}
