// Slice 9, package O9 (object, loot and use tasks): see TbRunTasks.h and forever/docs/SLICE9-DOORS-NOTES.md. Built with TORTOISEBOTS_SLICE1 only.
//
// The decisions are in TbRunTasksPure.h (forever/tools/test_run_tasks.cpp); this file reads the world, queues the walk, starts the act and keeps the
// per-run task table. One task per bot, the run's own bots only. Nothing here changes a bot that the run does not command.
#include "host/run/TbRunTasks.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "host/TbH3.h"
#include "host/TbOrdersApi.h"
#include "host/run/TbRun.h"
#include "host/run/TbRunFacts.h"
#include "host/run/TbRunOrdersPure.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <strings.h>

#include "Creature.h"
#include "GameTime.h"
#include "DB2Stores.h"
#include "DB2Structure.h"
#include "GameObject.h"
#include "GameObjectData.h"
#include "GameObjectPackets.h"
#include "Group.h"
#include "Loot.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "PartyPackets.h"
#include "Player.h"
#include "Spell.h"
#include "WorldSession.h"

#include "TbBotMgr.h"
#include "compat/TbCompat_I2.h"
#include "compat/TbCompat_S9.h"
#include "compat/TbCompat_Unit.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "PlayerbotAIStorage.h"

namespace TortoiseBots
{
namespace run
{
namespace tasks
{
namespace
{
namespace od = orders;

// One running task. Guids and plain data only.
struct Task
{
    ObjectGuid bot;
    ObjectGuid target;
    TaskRec rec;
    uint32 entry = 0;               // the target's entry (for the events and the texts)
    bool goTarget = false;          // a game object (else a corpse)
    uint32 walkSeq = 0;             // the order seq of the newest `come`
    std::string order;              // the order the policy sent
    uint32 startedMs = 0;
};

struct RunTasks
{
    std::map<ObjectGuid, Task> byBot;
    uint32 accumulatedMs = 0;
    uint32 queuedOrders = 0;        // `come` orders queued by tasks since TakeQueuedOrders
    TaskState state;
};

std::map<ObjectGuid, RunTasks>& Table()
{
    static std::map<ObjectGuid, RunTasks> table;
    return table;
}

Totals& TotalsRw()
{
    static Totals totals;
    return totals;
}

PlayerbotAI* AiOf(Player* bot)
{
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    return ai && ai->GetAiObjectContext() ? ai : nullptr;
}

std::string NameOf(Player* player)
{
    return player ? std::string(player->GetName()) : std::string();
}

// A run takes loot under group loot only (spec D4), for its whole life. The run tick pauses on a changed method once a second, but a task's act comes up
// to four times a second: the method is read again right before every act, so no loot or object use starts under rules the policy does not know.
bool LeadGroupLoot(Player* lead, std::string& why)
{
    Group* group = lead ? lead->GetGroup() : nullptr;
    if (!group)
    {
        why = "the lead bot is not in a group";
        return false;
    }
    return RunLootMethodOk(PolicyLootMethod(uint32(group->GetLootMethod())), why);
}

OrderAnswer Refuse(std::string const& code, std::string const& message)
{
    OrderAnswer a;
    a.code = code;
    a.text = od::RefusalText(code, message);
    return a;
}

OrderAnswer Accepted(std::string const& scope, uint32 count)
{
    OrderAnswer a;
    a.accepted = true;
    a.text = od::AckText(scope, count);
    a.bots = count;
    return a;
}

// ---- the lock of a game object, as the core's lock row says it (T: GoLockFacts, Commander.cpp:4303) ----
// A key item the bot carries, or skill it has; a lock with neither row is open. The core's `Use` of a door or a lever does not look at the lock (GameObject.cpp:
// 2724-2728, :2949), a chest's opening spell does: the host judges first and refuses what the core would let through (do less).
RC::LockFacts GoLockFacts(Player* bot, GameObject* go)
{
    RC::LockFacts facts;
    if (!go || !go->GetGOInfo())
        return facts;
    LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
    if (!lock)
        return facts;
    for (uint32 i = 0; i < MAX_LOCK_CASE; ++i)
    {
        if (lock->Type[i] == LOCK_KEY_ITEM && lock->Index[i] > 0)
        {
            if (!facts.hasKey)
            {
                facts.keyItem = uint32(lock->Index[i]);
                facts.hasKey = bot->HasItemCount(facts.keyItem, 1, false);
            }
        }
        else if (lock->Type[i] == LOCK_KEY_SKILL && !facts.skillId)
        {
            uint32 const skillId = uint32(SkillByLockType(LockType(lock->Index[i])));
            if (skillId > 0)
            {
                facts.skillId = skillId;
                facts.reqSkill = std::max<uint32>(1, lock->Skill[i]);
                facts.skillValue = bot->GetSkillValue(skillId);
            }
        }
    }
    return facts;
}

uint32 PackedState(GameObject* go)
{
    return go ? PackState(uint32(go->getLootState()), uint32(go->GetGoState())) : 0u;
}

float Dist2d(float ax, float ay, float bx, float by)
{
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

// ---- the target ----
struct Target
{
    GameObject* go = nullptr;
    Creature* corpse = nullptr;
    WorldObject* object() const { return go ? static_cast<WorldObject*>(go) : static_cast<WorldObject*>(corpse); }
};

Target TargetOf(Player* bot, ObjectGuid guid, bool corpsesToo)
{
    Target t;
    Map* map = bot->FindMap();
    if (!map || guid.IsEmpty())
        return t;
    if (guid.IsGameObject())
    {
        GameObject* go = map->GetGameObject(guid);
        if (go && go->isSpawned())
            t.go = go;
    }
    else if (corpsesToo && guid.IsAnyTypeCreature())
    {
        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        if (creature && !creature->IsAlive())
            t.corpse = creature;
    }
    return t;
}

// By entry: the spawned game objects of the entry (then, for a loot order, the corpses of it) within kEntrySearchYd of the plan's point; the nearest wins
// (PickEntryTarget); none: refused, never another object of the entry. The search is around the bot, so its range covers the point's distance too.
bool FindByEntry(Player* bot, RC::TargetSpec const& spec, bool corpsesToo, ObjectGuid& out)
{
    float px = spec.hasPoint ? spec.point.x : bot->GetPositionX();
    float py = spec.hasPoint ? spec.point.y : bot->GetPositionY();
    float pz = spec.hasPoint ? spec.point.z : bot->GetPositionZ();
    float const range = bot->GetDistance(px, py, pz) + kEntrySearchYd + 1.0f;
    std::vector<Candidate> found;
    std::map<uint64, ObjectGuid> keys;
    std::list<GameObject*> objects;
    bot->GetGameObjectListWithEntryInGrid(objects, spec.entry, range);
    for (GameObject* go : objects)
        if (go && go->isSpawned())
        {
            uint64 const key = GuidKeyCompat(go->GetGUID());
            keys[key] = go->GetGUID();
            found.push_back(Candidate{ key, ai::PlanDistance(go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), px, py, pz) });
        }
    uint64 key = 0;
    if (PickEntryTarget(found, key))
    {
        out = keys[key];
        return true;
    }
    if (!corpsesToo)
        return false;
    found.clear();
    std::list<Creature*> bodies;
    bot->GetCreatureListWithEntryInGrid(bodies, spec.entry, range);
    for (Creature* creature : bodies)
        if (creature && !creature->IsAlive())
        {
            uint64 const k = GuidKeyCompat(creature->GetGUID());
            keys[k] = creature->GetGUID();
            found.push_back(Candidate{ k, ai::PlanDistance(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), px, py, pz) });
        }
    if (!PickEntryTarget(found, key))
        return false;
    out = keys[key];
    return true;
}

// ---- finishing ----
void SetReply(RunTasks& rt, bool ok, std::string const& text)
{
    rt.state.reply = ok ? 1 : 2;
    rt.state.text = text;
}

void Finish(ObjectGuid leader, RunTasks& rt, Task const& task, bool ok, std::string const& code, std::string const& message, uint32 nowMs)
{
    Totals& totals = TotalsRw();
    // The ledger watch is given back whatever the result (a window that never closes must not hold the bot's next corpse).
    RC::LootOrders().Forget(GuidKeyCompat(task.bot));
    std::string const text = ok ? message : code + ": " + message;
    if (rt.state.carrier == task.bot)
        SetReply(rt, ok, text);
    if (ok)
        ++totals.done;
    else
        ++totals.failed;
    if (RunRec* rec = Find(leader))
        NoteTaskEvent(*rec, nowMs, task.rec.lootLike ? "loot" : "open", int32(task.entry), (ok ? "done: " : "failed: ") + text);
}

// ---- the act ----
enum class ActStart : uint8 { Started, Retry, Failed, Done };

struct ActResult
{
    ActStart how = ActStart::Failed;
    std::string code, message;
    std::string payload;            // Done: the success text (no act was needed)
};

ActResult ActFailed(char const* code, std::string const& message, ActStart how = ActStart::Failed)
{
    ActResult r;
    r.how = how;
    r.code = code;
    r.message = message;
    return r;
}

ActResult Started()
{
    ActResult r;
    r.how = ActStart::Started;
    return r;
}

// A door or a lever: the typed GameObjUse packet, what a client's click sends (SpellHandler.cpp:201). The core runs GetGameObjectIfCanInteractWith (the
// interaction range, Player.cpp:1993) and `Use`. Judged first, by the host: the lock row (RC CheckLock), and the state (a door that is not at rest is open
// already, and the core's Use TOGGLES it).
ActResult BeginObjectAct(Task& task, Player* bot, PlayerbotAI* ai, GameObject* go)
{
    LockJudgement const lock = JudgeLock(GoLockFacts(bot, go));
    if (!lock.ok)
        return ActFailed(lock.code.c_str(), lock.text);
    if (AlreadyUsed(uint32(go->GetGoState())))
    {
        ActResult r;
        r.how = ActStart::Done;
        r.payload = "already open (state " + std::to_string(uint32(go->GetGoState())) + "): not used again";
        return r;
    }
    if (!go->IsWithinDistInMap(bot))
        return ActFailed(RC::kCodeUnreachable, "The bot is not within the object's interaction range.", ActStart::Retry);
    if (bot->IsMounted())
        ai->Unmount();
    task.rec.baseline = PackedState(go);
    WorldSession* session = bot->GetSession();
    if (!session)
        return ActFailed(RC::kCodeFailed, "The bot has no session.");
    WorldPackets::GameObject::GameObjUse packet{ WorldPacket(CMSG_GAME_OBJ_USE) };
    packet.Guid = go->GetGUID();
    session->HandleGameObjectUseOpcode(packet);
    return Started();
}

// A chest (loot or open) or a corpse: the engine's own loot machinery, with the ledger watching the window (StoreLootAction reports into it).
ActResult BeginLootAct(Task& task, Player* bot, PlayerbotAI* ai, Player* lead, Target const& target)
{
    ObjectGuid const guid = task.target;
    ai::LootObject lootObject(bot, guid, true);
    if (lootObject.IsEmpty())
    {
        if (target.corpse)
        {
            Loot* loot = target.corpse->GetLootForPlayer(bot);
            if (!loot || loot->isLooted())
                return ActFailed(RC::kCodeFailed, std::string(LootEmptyText()) + " on this corpse.");
            return ActFailed(RC::kCodeFailed, std::string("This corpse has ") + LootRightsText() + " under the group's loot rules (another member's turn, already looted, or not tapped by the group).");
        }
        return ActFailed(RC::kCodeFailed, "The object cannot be looted right now (already open, used up, or on the bot's skip list).");
    }
    ai::LootObjectStack* stack = ai->GetAiObjectContext()->GetValue<ai::LootObjectStack*>("available loot")->Get();
    if (target.go)
    {
        LockJudgement const lock = JudgeLock(GoLockFacts(bot, target.go));
        if (!lock.ok)
            return ActFailed(lock.code.c_str(), lock.text);
        if (!lootObject.IsLootPossible(bot))
            return ActFailed(RC::kCodeFailed, "Nothing in it for this bot (quest-only contents, bags full, or the object is in use).");
    }
    if (bot->IsMounted())
        ai->Unmount();
    RC::LootOrders().Watch(GuidKeyCompat(bot->GetGUID()), GuidKeyCompat(guid), task.rec.itemId);
    if (stack)
        stack->NotePlayerOrder(guid);
    if (target.corpse)
    {
        // The corpse window at once: an explicit order, so the strategy's "is it worth the bags" filter does not apply (the loot rights were checked by
        // the loot object above). The core refuses silently (rights, range): the window is read back from the player's loot view.
        if (LootOpenCompat(ai, bot, guid))
            return Started();
        RC::LootOrders().Forget(GuidKeyCompat(bot->GetGUID()));
        return ActFailed(RC::kCodeFailed, std::string("The core did not open the corpse's loot window for this bot: ") + LootRightsText() +
            " (not its turn, already looted, not tapped by the group, or out of range).", ActStart::Retry);
    }
    // A chest opens through the bot's own opening code (its lock spell, a key it carries); the window comes back through the loot window handler.
    ai->ClearLastCastCheckFail();
    ai->GetAiObjectContext()->GetValue<ai::LootObject>("loot target")->Set(lootObject);
    // The order runs the action itself (the Turtle ExecuteOrderedAction, commands/BotCommandContext.cpp:112): through the engine's DoSpecificAction the action's
    // own isPossible / isUseful gates refuse a chest the bot stands at ("can loot" wants it queued by the strategy), and the order then ended before a cast was tried.
    ai::Action* openLoot = ai->GetAiObjectContext()->GetAction("open loot");
    ai::Event openEvent("cmd loot", "", lead);
    if (openLoot && openLoot->Execute(openEvent))
    {
        // The Opening cast of a locked chest runs inside the act stage: give the stage the cast time it just started, on top of the window wait.
        if (Spell* cast = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            NoteActCast(task.rec, uint32(cast->GetCastedTime()));
        return Started();
    }
    RC::LootOrders().Forget(GuidKeyCompat(bot->GetGUID()));
    uint32 const castFail = uint32(ai->GetLastCastCheckFail());
    char const* reason = nullptr;
    if (castFail == uint32(SPELL_CAST_OK) && (target.go->getLootState() != GO_READY || target.go->HasFlag(GO_FLAG_IN_USE)))
        reason = "in-use";
    if (!reason && castFail == uint32(SPELL_CAST_OK) && !lootObject.IsInLootRange(bot))
        reason = "range";
    std::string const why = reason ? reason : "";
    return ActFailed(RC::CastReasonCode(why), RC::OpenRefusalText(why, castFail), ActStart::Retry);
}

// Before each start the bot stands still: it stopped at its stand already (the stay position is the stand), a bot still on the last yards is stopped and the
// start comes on the next look (RC::ActWaitsForStop).
bool HoldForAct(PlayerbotAI* ai, Player* bot)
{
    ai->DropQueuedMovementOrders();
    if (bot->HasUnitState(UNIT_STATE_LOST_CONTROL))
        return false;
    if (bot->IsNonMeleeSpellCast(false))
        bot->InterruptNonMeleeSpells(false);
    bool const moving = RC::ActWaitsForStop(sServerFacade.isMoving(bot), !bot->movespline->Finalized());
    if (moving)
        ai->StopMoving();
    return moving;
}

// ---- the stand: kApproachBackoff short of the object on the bot's side, on the floor ----
bool StandPoint(Player* bot, WorldObject* object, float& x, float& y, float& z)
{
    RC::Point3 const stand = RC::StandingPoint(bot->GetPositionX(), bot->GetPositionY(), object->GetPositionX(), object->GetPositionY(), object->GetPositionZ());
    x = stand.x;
    y = stand.y;
    z = stand.z;
    Map* map = bot->FindMap();
    if (!map)
        return false;
    // The object's height may be above the floor (a door's centre): the floor under the stand, within a few yards of the object's height and the bot's.
    float const base = std::max(object->GetPositionZ(), bot->GetPositionZ());
    float const ground = GetHeightCompat(map, x, y, base + 1.0f, true, 12.0f);
    z = ground > INVALID_HEIGHT ? ground : bot->GetPositionZ();
    return true;
}

// Queues the walk to the stand. False (with the task failed by the caller) when the host refuses the order.
bool OrderWalk(RunTasks& rt, Task& task, Player* bot, WorldObject* object, std::string& code, std::string& detail)
{
    h3::OrderSpec spec;
    spec.verb = "come";
    spec.fromRun = true;
    spec.map = bot->GetMapId();
    if (!StandPoint(bot, object, spec.x, spec.y, spec.z))
    {
        code = "unreachable";
        detail = "The bot is on no map.";
        return false;
    }
    uint32 seq = 0;
    if (!h3::QueueOrder(bot, spec, code, detail, seq))
        return false;
    task.walkSeq = seq;
    ++rt.queuedOrders;
    return true;
}

// ---- one look at one task ----
// Returns true when the task is over.
bool Look(ObjectGuid leader, RunTasks& rt, Task& task, uint32 diffMs, uint32 nowMs)
{
    Player* bot = ObjectAccessor::FindPlayer(task.bot);
    Player* lead = ObjectAccessor::FindPlayer(leader);
    PlayerbotAI* ai = bot && bot->IsInWorld() ? AiOf(bot) : nullptr;
    Obs o;
    o.botAvailable = ai && bot->FindMap() && bot->IsAlive() && !bot->IsBeingTeleported() && !bot->IsInFlight();
    o.teleporting = bot && bot->IsAlive() && (!bot->IsInWorld() || bot->IsBeingTeleported());
    // A follower that left the commanded party is not ours to order any more.
    if (o.botAvailable && task.bot != leader && h3::CommanderGuid(task.bot) != leader)
    {
        Finish(leader, rt, task, false, RC::kCodeUnavailable, "The bot left the commanded party.", nowMs);
        return true;
    }
    Target target;
    if (o.botAvailable)
    {
        target = TargetOf(bot, task.target, true);
        o.targetPresent = target.object() != nullptr;
        o.inCombat = bot->IsInCombat();
        if (WorldObject* object = target.object())
        {
            o.distance = bot->GetDistance(object);
            o.inRange = target.go ? target.go->IsWithinDistInMap(bot) : o.distance <= kCorpseRangeYd;
        }
        o.moving = sServerFacade.isMoving(bot) || !bot->movespline->Finalized();
        if (task.walkSeq)
        {
            h3::OrderStatus const status = h3::LastOrder(task.bot);
            if (status.seq == task.walkSeq && status.result.compare(0, 5, "fail:") == 0)
            {
                o.walkOrderFailed = true;
                o.walkOrderText = status.result;
            }
        }
        o.objectPresent = target.go != nullptr;
        o.objectState = PackedState(target.go);
        if (task.rec.lootLike)
        {
            RC::LootWatch const* watch = RC::LootOrders().Find(GuidKeyCompat(task.bot));
            o.watchFound = watch != nullptr;
            if (watch)
            {
                o.windowFinished = watch->finished;
                o.windowRefused = watch->refused;
                o.itemsTaken = RC::LootedItemCount(*watch);
                o.looted = RC::FormatLootedItems(*watch);
            }
        }
    }
    Step step = Advance(task.rec, o, diffMs);
    switch (step.what)
    {
        case Do::Wait:
            return false;
        case Do::OrderWalk:
        {
            if (!target.object())
                return false;
            std::string code, detail;
            if (!OrderWalk(rt, task, bot, target.object(), code, detail))
            {
                Finish(leader, rt, task, false, RC::kCodeUnreachable, "The bot could not be sent to the object (" + code + ": " + detail + ").", nowMs);
                return true;
            }
            return false;
        }
        case Do::BeginAct:
        {
            ActResult result;
            std::string methodWhy;
            if (!LeadGroupLoot(lead ? lead : bot, methodWhy))
                result = ActFailed("loot-method", methodWhy + ": no loot or object is used while the group's loot method is not group loot.");
            else if (HoldForAct(ai, bot))
                result = ActFailed(RC::CastReasonCode("moving"), RC::OpenRefusalText("moving", 0), ActStart::Retry);
            else if (task.rec.lootLike)
                result = BeginLootAct(task, bot, ai, lead ? lead : bot, target);
            else if (target.go)
                result = BeginObjectAct(task, bot, ai, target.go);
            else
                result = ActFailed(RC::kCodeInvalidTarget, "The object is gone.");
            if (result.how == ActStart::Done)
            {
                Finish(leader, rt, task, true, "", result.payload, nowMs);
                return true;
            }
            NoteActStart(task.rec, result.how == ActStart::Started, result.how == ActStart::Failed, result.code, result.message);
            return false;
        }
        case Do::Succeed:
            Finish(leader, rt, task, true, "", step.message, nowMs);
            return true;
        case Do::Fail:
            Finish(leader, rt, task, false, step.code, step.message, nowMs);
            return true;
    }
    return false;
}
}

// ======================================================================================================================================================
// the orders
// ======================================================================================================================================================
OrderAnswer Begin(RunRec& rec, Player* lead, std::vector<Player*> const& bots, TaskOrder const& order, uint32 nowMs)
{
    bool const isLoot = order.verb == Verb::Loot;
    Player* bot = nullptr;
    for (Player* candidate : bots)
        if (candidate && strcasecmp(candidate->GetName().c_str(), order.bot.c_str()) == 0)
            bot = candidate;
    if (!bot)
        return Refuse("no-bot", "That bot is not in your commanded group.");
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !bot->IsAlive() || bot->IsInFlight() || bot->IsBeingTeleported())
        return Refuse("unavailable", NameOf(bot) + " is dead, on a taxi, teleporting or has no AI.");
    RunTasks& rt = Table()[rec.leader];
    if (rt.byBot.count(bot->GetGUID()))
        return Refuse(RC::kCodeBusy, NameOf(bot) + " is carrying out an earlier order.");
    if (bot->IsInCombat())
        return Refuse(RC::kCodeInCombat, NameOf(bot) + " is in combat.");
    if (bot != lead && h3::CommanderGuid(bot->GetGUID()) != lead->GetGUID())
        return Refuse("not-bound", NameOf(bot) + " is not bound to the lead bot as its commander (`.tb master`).");

    // The target: a handle of ReadFacts (a boss corpse), or the entry at the plan's point.
    ObjectGuid guid;
    if (order.spec.ByEntry())
    {
        float const px = order.spec.hasPoint ? order.spec.point.x : bot->GetPositionX();
        float const py = order.spec.hasPoint ? order.spec.point.y : bot->GetPositionY();
        float const pz = order.spec.hasPoint ? order.spec.point.z : bot->GetPositionZ();
        if (!RC::EntryPointInReach(bot->GetDistance(px, py, pz), sPlayerbotAIConfig.reactDistance))
            return Refuse(RC::kCodeTooFar, "That point is farther from the bot than AiPlayerbot.ReactDistance. Move the group closer first.");
        if (!FindByEntry(bot, order.spec, isLoot, guid))
            return Refuse(RC::kCodeInvalidTarget, NotFoundText(order.spec.entry));
    }
    else if (!ResolveHandle(rec.leader, order.spec.guid, guid))
        return Refuse(RC::kCodeInvalidTarget, "No corpse or game object with that handle is on the group's map (the handle came from an earlier read and no longer resolves).");
    Target const target = TargetOf(bot, guid, isLoot);
    if (!target.object())
        return Refuse(RC::kCodeInvalidTarget, isLoot ? "No corpse or game object with that GUID is on the bot's map." : "No game object with that GUID is on the bot's map.");
    float const distance = bot->GetDistance(target.object());
    if (!RC::WithinReactDistance(distance, sPlayerbotAIConfig.reactDistance))
        return Refuse(RC::kCodeTooFar, "The object is farther from the bot than AiPlayerbot.ReactDistance. Move the group closer first.");
    if (target.go)
    {
        LockJudgement const lock = JudgeLock(GoLockFacts(bot, target.go));
        if (!lock.ok)
            return Refuse(lock.code, lock.text);
    }

    Task task;
    task.bot = bot->GetGUID();
    task.target = guid;
    task.entry = target.object()->GetEntry();
    task.goTarget = target.go != nullptr;
    bool const chest = target.go && target.go->GetGoType() == GAMEOBJECT_TYPE_CHEST;
    bool const lootLike = isLoot || chest;
    task.rec = MakeRec(isLoot ? Kind::Loot : Kind::Open, lootLike, target.corpse != nullptr, isLoot ? order.itemId : 0, distance,
        target.go && target.go->GetGoType() == GAMEOBJECT_TYPE_GOOBER && !lootLike);
    task.startedMs = nowMs;
    rt.byBot[task.bot] = task;
    rt.state.carrier = task.bot;
    rt.state.reply = 0;
    rt.state.text.clear();
    ++TotalsRw().began;
    return Accepted("bot:" + NameOf(bot), 1);
}

OrderAnswer SetLootMethod(RunRec& /*rec*/, Player* lead, std::vector<Player*> const& bots, TaskOrder const& order)
{
    MethodVerdict const verdict = JudgeLootMethod(order);
    if (!verdict.ok)
        return Refuse(verdict.code, verdict.text);
    Group* group = lead ? lead->GetGroup() : nullptr;
    if (!group || !lead->GetSession() || !group->IsLeader(lead->GetGUID()))
        return Refuse("lead-unavailable", "The lead bot does not lead the group.");
    int threshold = order.hasThreshold ? order.threshold : int(group->GetLootThreshold());
    threshold = std::max(2, std::min(5, threshold));
    WorldPackets::Party::SetLootMethod packet{ WorldPacket(CMSG_SET_LOOT_METHOD) };
    packet.LootMethod = uint8(order.method);
    packet.LootMasterGUID = ObjectGuid::Empty;
    packet.LootThreshold = uint32(threshold);
    lead->GetSession()->HandleSetLootMethodOpcode(packet);
    // The answer is read back from the group: a change the core refused is not reported as made.
    if (int(group->GetLootMethod()) != order.method)
        return Refuse(RC::kCodeFailed, "The group's loot method did not change.");
    return Accepted("party", uint32(bots.size()));
}

// ======================================================================================================================================================
// the clock
// ======================================================================================================================================================
void Update(uint32 diffMs)
{
    auto& table = Table();
    if (table.empty())
        return;
    uint32 const nowMs = GameTime::GetGameTimeMS();
    for (auto it = table.begin(); it != table.end();)
    {
        RunTasks& rt = it->second;
        if (rt.byBot.empty())
        {
            ++it;
            continue;
        }
        rt.accumulatedMs += diffMs;
        if (rt.accumulatedMs < RC::kTaskTickMs)
        {
            ++it;
            continue;
        }
        uint32 const dt = rt.accumulatedMs;
        rt.accumulatedMs = 0;
        std::vector<ObjectGuid> bots;
        for (auto const& entry : rt.byBot)
            bots.push_back(entry.first);
        for (ObjectGuid guid : bots)
        {
            auto task = rt.byBot.find(guid);
            if (task == rt.byBot.end())
                continue;
            if (Look(it->first, rt, task->second, dt, nowMs))
                rt.byBot.erase(task);
        }
        ++it;
    }
}

bool TakeQueuedOrders(ObjectGuid leader)
{
    auto found = Table().find(leader);
    if (found == Table().end() || !found->second.queuedOrders)
        return false;
    found->second.queuedOrders = 0;
    return true;
}

void Cancel(ObjectGuid leader)
{
    auto found = Table().find(leader);
    if (found == Table().end())
        return;
    for (auto const& entry : found->second.byBot)
    {
        RC::LootOrders().Forget(GuidKeyCompat(entry.first));
        ++TotalsRw().cancelled;
    }
    found->second.byBot.clear();
    found->second.state = TaskState();
}

void Forget(ObjectGuid leader)
{
    Cancel(leader);
    Table().erase(leader);
}

TaskState StateOf(ObjectGuid leader)
{
    auto found = Table().find(leader);
    if (found == Table().end())
        return TaskState();
    TaskState state = found->second.state;
    state.busy = !state.carrier.IsEmpty() && found->second.byBot.count(state.carrier) != 0;
    return state;
}

bool Busy(ObjectGuid leader, ObjectGuid bot)
{
    auto found = Table().find(leader);
    return found != Table().end() && found->second.byBot.count(bot) != 0;
}

Totals const& TotalsSinceBoot()
{
    return TotalsRw();
}
}
}
}

#else

namespace TortoiseBots
{
namespace run
{
namespace tasks
{
// Option OFF: the tasks are slice-only code (their callers are too); this object is empty.
}
}
}

#endif
