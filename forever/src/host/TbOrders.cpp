// Slice 3, host package H3: the orders (`.tb order`, `.tb role`, `.tb master`) and the engine hooks behind them.
// Design: forever/docs/SLICE3-H-NOTES.md; the reply grammar: forever/docs/TB-COMMANDS.md.
//
// An order is validated and QUEUED by the command (which answers at once), and run by BeforeEngineTick, which engine::Tick
// (TbEngineBrain.cpp) calls right before the bot's UpdateAI: the legacy ACTION is called by its key through
// PlayerbotAI::DoSpecificAction (PlayerbotAI.h:466), never PlayerbotAI::HandleCommand (spec D1). Both the command and the tick run on the
// world thread (TbH3.h), so the queue is plain data. The outcome of an order is kept per bot and shown by `.tb state` (ord=<seq>:<result>).
#include "host/TbH3.h"
#include "host/TbHostAuction.h"
#include "host/TbHostProfession.h"
#include "host/TbHostTaxi.h"
#include "host/TbHostTown.h"
#include "host/TbHostTransport.h"
#include "host/TbIgnore.h"   // I13: explicit orders take a listed mob off the commander's ignore list
#include "playerbot/policy/profession/ProfessionPickPolicy.h"
#include "host/TbOnyxiaGround.h"
#include "MiscPackets.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/TbOrdersApi.h"
#include "host/TbOrderQueuePure.h"
#include "host/TbOrderRingPure.h"
#include "host/TbTactics.h"
#include "host/TbTacticsPure.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/cmd/TbMasterBindPure.h"
#include "host/cmd/TbPlace.h"
#include "host/cmd/TbTestTargetPure.h"
#include "host/run/TbRunLiquid.h"
#include "host/run/TbRunFacts.h"
#include "host/run/TbRunOrdersPure.h"
#include "slice/SliceHybridPolicy.h"
#include "slice/SliceSpecPolicy.h"
#include "slice/SliceDbStore.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include "Group.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "Unit.h"

#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotDbStore.h"
#include "playerbot/PullStartPolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/AttackersValue.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/actions/PullActions.h"
#include "playerbot/strategy/values/RtiTargetValue.h"
#include "PlayerbotAIStorage.h"
#include "runtime/BotManager.h"

namespace TortoiseBots
{
namespace h3
{
namespace
{
constexpr uint32 OrderMaxAgeMs = 15000;     // an order the engine could not run within this long (no tick: the bot is teleporting, away) expires
constexpr size_t OrderQueueMax = 16;
constexpr uint32 kPullJoinDelaySeconds = 3;       // L2: the held bots' minimum wait after the pull landed (lead decision: 3 s)
constexpr uint32 kPullApproachWindowSeconds = 15; // L2: how long the hold lasts when the pull never lands (AiPlayerbot.PullBackMaxReturnTime's default)

uint32 NowMs()
{
    return uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Order
{
    uint32 seq = 0;
    std::string verb;
    std::string arg;            // follow: member name or "none"; co/nc: the strategy change; role: the role number
    ObjectGuid owner;           // the Event's owner: the commander when in the world, else the bot itself
    ObjectGuid unit;            // attack / pull: the unit
    ObjectGuid who;             // assist: the member whose victim is taken
    float x = 0, y = 0, z = 0;  // come
    uint32 map = 0;
    uint32 instance = 0;        // move-direct: the Map instance the point was made in (Codex review fix)
    uint32 queuedAt = 0;
    bool fromRun = false;       // O6: the run leader's order (QueueOrder): owner is the bot itself, a pull hold anchors on the tank's own spot
    bool disengage = false;     // come: the fall back (stop the attack/cast/pet and hold passive on the way)
    bool placed = false;        // come: the placement's own order (only it keeps the combat-time behaviour)
    uint32 passiveToken = 0;    // come: the row's passive owner a walk back carries; 0 = the fall back's own come
};

struct BotRec
{
    std::deque<Order> queue;
    // TBF-B1 review finding 2: every transition of every order (queued, ok, fail:<code>), read one per snapshot by the provider
    // through the ring's own cursor. The `seq/verb/result` slot below stays the record's newest word for `.tb state`; an order a
    // newer one overwrote still reaches the engine as its own transition, and the ring is erased with the record at logout.
    OrderTransitionRing ring;
    uint32 seq = 0;             // the last order that finished or is queued
    std::string verb = "none";
    std::string result = "none";   // ok | fail:<code> | queued
    place::PassiveBook placePassive;   // F1: the fall back's emergency passive - who owns it, and the last order that set `passive` itself
};

std::map<ObjectGuid, BotRec>& Recs()
{
    static std::map<ObjectGuid, BotRec> recs;
    return recs;
}

// R6 (review item 5): the leaders of the live runs (HoldRunLead): BeforeEngineTick keeps them from following their commander.
std::set<ObjectGuid>& RunLeads()
{
    static std::set<ObjectGuid> leads;
    return leads;
}

std::map<ObjectGuid, uint8>& Roles()
{
    static std::map<ObjectGuid, uint8> roles;
    return roles;
}

// TBF-B1 review finding 7: the two wait clocks of the engine hooks below. They are per-bot state like the order record, so they are
// erased with it at logout instead of growing with every bot guid that ever ticked.
std::map<ObjectGuid, uint32>& LastAcceptMs()
{
    static std::map<ObjectGuid, uint32> last;
    return last;
}

std::map<ObjectGuid, uint32>& LastRoleCheckMs()
{
    static std::map<ObjectGuid, uint32> last;
    return last;
}

uint32& SeqCounter()
{
    static uint32 seq = 0;
    return seq;
}

constexpr uint8 RoleTank = 1, RoleHealer = 2, RoleDps = 4;      // ChatHelper.h BOT_ROLE_*

bool IsVerb(std::string const& word)
{
    static char const* const verbs[] = { "follow", "stay", "come", "attack", "pull", "endpull", "end", "hold", "free", "assist", "co", "nc", "release", "sell", "buy", "repair", "train", "mail", "learn-prof", "gather", "craft", "taxi-learn", "fly", "ride", "ah", "tame",
        "stop", "flee", "guard", "stance", "engage", "rebuff", "interrupt", "aoe", "autocc", "sap", "loot", "corpserun" };
    for (char const* verb : verbs)
        if (word == verb)
            return true;
    return false;
}

bool ActionRegistered(PlayerbotAI* ai, char const* key)
{
    // The context's own lookup; it caches what it finds (and a miss), so asking never leaks and never creates a second object.
    return ai->GetAiObjectContext() && ai->GetAiObjectContext()->GetAction(key) != nullptr;
}

PlayerbotAI* AiOf(Player* bot)
{
    return PlayerbotAIStorage::Instance().GetAI(bot);
}

char const* const MarkWords[8] = { "star", "circle", "diamond", "triangle", "moon", "square", "cross", "skull" };

// One of the eight raid marks by its word, -1 if the word is none of them.
int MarkIndex(std::string const& word)
{
    for (int i = 0; i < 8; ++i)
        if (word == MarkWords[i])
            return i;
    return -1;
}

// The unit an `attack` / `pull` argument names, for this bot: `my` (the sender's selection, read when the command runs), a raid mark of the
// bot's group (`skull`, `mark`, `mark:<word>`), `spawn:<db spawn id>` (a creature of the bot's map) or the 32-hex guid text of `.tb state`.
bool ResolveTarget(Player* bot, Player* sender, std::string const& argument, ObjectGuid& out, std::string& code, std::string& detail)
{
    std::string const arg = Lower(argument);
    ObjectGuid guid;
    if (arg == "my")
    {
        if (!sender)
        {
            code = "needs-player-sender";
            detail = "`my` is the selection of the player who sent the command; from the console name a guid, a mark or spawn:<id>";
            return false;
        }
        guid = sender->GetTarget();
        if (guid.IsEmpty())
        {
            code = "no-selection";
            detail = "the sender has no target selected";
            return false;
        }
    }
    else if (arg == "mark" || arg.rfind("mark:", 0) == 0 || MarkIndex(arg) >= 0)
    {
        std::string const word = arg == "mark" ? "skull" : (arg.rfind("mark:", 0) == 0 ? arg.substr(5) : arg);
        int const index = MarkIndex(word);
        Group* group = bot->GetGroup();
        if (index < 0)
        {
            code = "bad-target";
            detail = "unknown raid mark " + word;
            return false;
        }
        if (!group)
        {
            code = "no-party";
            detail = "the bot is in no party, so it sees no raid mark";
            return false;
        }
        guid = group->GetTargetIcon(uint8(index));
        if (guid.IsEmpty())
        {
            code = "mark-empty";
            detail = "the " + word + " mark is on nothing";
            return false;
        }
    }
    else if (arg.rfind("spawn:", 0) == 0)
    {
        char* end = nullptr;
        unsigned long long const id = std::strtoull(arg.c_str() + 6, &end, 10);
        Creature* creature = (end && *end == '\0' && id) ? bot->GetMap()->GetCreatureBySpawnId(ObjectGuid::LowType(id)) : nullptr;
        if (!creature)
        {
            code = "no-such-spawn";
            detail = "no creature with that spawn id on the bot's map (it is not loaded or not spawned)";
            return false;
        }
        guid = creature->GetGUID();
    }
    else if (!ParseGuidText(arg, guid))
    {
        code = "bad-target";
        detail = "a target is `my`, a raid mark, spawn:<id> or the 32 hex digits of a guid";
        return false;
    }

    Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
    if (!unit)
    {
        code = "target-not-found";
        detail = "no such unit on the bot's map";
        return false;
    }
    if (!unit->IsAlive())
    {
        code = "target-dead";
        detail = "the target is dead";
        return false;
    }
    if (!bot->IsValidAttackTarget(unit))
    {
        code = "not-attackable";
        detail = "the core says this bot may not attack that unit";
        return false;
    }
    out = guid;
    return true;
}

// The eight raid marks of a group as the engine's mark-based attack reads them: set the mark, run the action, put every mark back. The marks
// are the group's own state and the group (a GM's client) sees them change, so the order leaves nothing of itself behind.
bool AttackThroughMark(Player* bot, PlayerbotAI* ai, Player* owner, ObjectGuid unit, char const* action)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    ObjectGuid saved[8];
    for (uint8 i = 0; i < 8; ++i)
        saved[i] = group->GetTargetIcon(i);

    std::string rti = "skull";
    if (ai::Value<std::string>* value = ai->GetAiObjectContext()->GetValue<std::string>("rti"))
        rti = value->Get();
    int index = MarkIndex(rti);
    if (index < 0)
        index = 7;

    group->SetTargetIcon(uint8(index), unit, owner->GetGUID());
    // "rti target" is a cached value: ask again.
    if (ai::Value<Unit*>* value = ai->GetAiObjectContext()->GetValue<Unit*>("rti target"))
        value->Reset();
    bool const ok = ai->DoSpecificAction(action, Event("tb order", unit, owner), true);

    for (int pass = 0; pass < 2; ++pass)
        for (uint8 i = 0; i < 8; ++i)
            if (group->GetTargetIcon(i) != saved[i])
                group->SetTargetIcon(i, saved[i], owner->GetGUID());
    return ok;
}

// O6: the stay position written the way `come` writes it (below): the entry "stay" of the position value. The facts' leadStayKnown reads this entry.
void WriteStay(PlayerbotAI* ai, float x, float y, float z, uint32 map)
{
    ai::PositionMap& positions = ai->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
    ai::PositionEntry stay = positions["stay"];
    stay.Set(x, y, z, map);
    positions["stay"] = stay;
}

// O6: a run's pull hold anchors on the TANK's own spot. ai::PausePartyPullHold anchors on the tank's master (the bound commander), which may stand outside
// the instance; the run's tank is its own anchor. Written for the same members and the same two entries the hold writes (every member that is neither
// a tank nor a healer: PullActions.cpp:106-141), after the hold ran.
void AnchorPullHoldOnTank(Player* tank)
{
    Group* group = tank ? tank->GetGroup() : nullptr;
    if (!group)
        return;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (!member || member == tank || !member->IsInWorld() || member->GetMapId() != tank->GetMapId())
            continue;
        PlayerbotAI* memberAi = AiOf(member);
        if (!memberAi || !memberAi->GetAiObjectContext() || run::IsTankMember(member) || PlayerbotAI::IsHeal(member, true))
            continue;
        ai::PositionMap& positions = memberAi->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
        for (char const* key : { "stay", "pull hold" })
        {
            ai::PositionEntry entry = positions[key];
            entry.Set(tank->GetPositionX(), tank->GetPositionY(), tank->GetPositionZ(), tank->GetMapId());
            positions[key] = entry;
        }
    }
}

// N6: what the legacy pull order does once the request is accepted (commands/Action.cpp:930-960, Party.cpp:1152): the tank may break its stay
// (RelaxTacticalMovement = DropQueuedMovementOrders + EndStayForOrder), its action clock is cleared so the pull does not wait out whatever it was doing, and a
// casting or seated tank is made ready (PullStartPolicy.h ReadyTankForPull: "reach pull" does not move a casting or sitting bot). Without this the run's
// `come` (the stand the policy put the tank on) left the stay strategy on, its "return to stay position" trigger walked the tank back every tick, and a pack
// more than about 30 yd from the stand was never reached (M6 live test: steps 16 and 17, `tgt=` empty, position unchanged for minutes, 175 `pull-release
// reason=expired`). The legacy order also runs one quiet decision tick (ExecuteQuietNextAction); the engines tick every world update here, so the cleared clock is enough.
void RelaxTankForPull(Player* tank, PlayerbotAI* ai, bool fromRun)
{
    ai->DropQueuedMovementOrders();
    ai->EndStayForOrder();
    // R6 (review item 5): EndStayForOrder hands a bot with no movement mode its default, `follow` of its commander; a run never rebinds its leader, so for a run
    // order that follow comes off again (the tank holds where it is if the pull does not land). The pull's own end may add it back: the run-lead hold below (BeforeEngineTick) puts it on stay.
    if (fromRun)
    {
        ai->ChangeStrategy("-follow", BotState::BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("-follow", BotState::BOT_STATE_REACTION);
    }
    ai->ClearActionClock();
    ai::PullReadyFacts facts;
    facts.casting = tank->IsNonMeleeSpellCast(true, false, true);
    facts.seated = tank->GetStandState() != UNIT_STAND_STATE_STAND;
    ai::PullReadySteps const steps = ai::ReadyTankForPull(facts);
    if (steps.interrupt)
        tank->InterruptNonMeleeSpells(false);
    if (steps.stand)
        tank->SetStandState(UNIT_STAND_STATE_STAND);
}

// Role: the forced role of the AI, then a rebuild of the kit from it (the slice's AiFactory reads it), then the movement mode back as it was.
void ApplyKit(PlayerbotAI* ai, uint8 role, bool setRole)
{
    bool const freeMove = ai->HasStrategy("free", BotState::BOT_STATE_NON_COMBAT);
    bool const stay = ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT);
    bool const guard = ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT);
    if (setRole)
        ai->SetForcedRole(role);
    ai->ResetStrategies();
    ai->SetMovementStrategy(freeMove ? "free" : (stay ? "stay" : (guard ? "guard" : "follow")));
}

std::string Run(Player* bot, PlayerbotAI* ai, Order const& o)
{
    Player* owner = ObjectAccessor::FindPlayer(o.owner);
    if (!owner || !owner->IsInWorld())
        owner = bot;
    auto const act = [&](char const* key, Event event) { return ai->DoSpecificAction(key, event, true); };

    if (o.verb == "follow")
    {
        if (!o.arg.empty() && !act("follow target", Event("tb order", o.arg, owner)))
            return "fail:follow-target";
        return act("follow chat shortcut", Event("tb order", std::string(), owner)) ? "ok" : "fail:follow";
    }
    if (o.verb == "stay")
        return act("stay chat shortcut", Event("tb order", std::string(), owner)) ? "ok" : "fail:stay";
    if (o.verb == "come")
    {
        // The stay position is the spot (PullActions.cpp:128-133 writes it the same way); the stay strategy's own trigger
        // ("return to stay position", StayStrategy.cpp:25-30) walks the bot there. An explicit order is a fresh walk (StayWalkOrdered).
        //
        // Only a placement changes the combat-time behaviour (PlanCome): it keeps its target in a fight, and `stay` is kept out of the
        // combat state so reach/LOS micro-movement still works. Every other `come` (a run's walk, a rally, a plain order) resets first,
        // exactly as it always has: a run's pull depends on "return to stay position" answering. A fall back additionally disengages: the
        // swing, the cast and the pet's attack stop, and the bot is held passive on the way, so it leaves instead of re-engaging.
        place::ComePlan const come = place::PlanCome(o.placed, o.disengage, bot->IsInCombat());
        if (come.stopFight)
        {
            bot->AttackStop();
            bot->InterruptNonMeleeSpells(false);
            if (Pet* pet = bot->GetPet())
                pet->AttackStop();
        }
        if (come.reset)
            ai->Reset();
        ai::PositionMap& positions = ai->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
        ai::PositionEntry stay = positions["stay"];
        stay.Set(o.x, o.y, o.z, o.map);
        positions["stay"] = stay;
        ai->SetMovementStrategy("stay");
        if (come.passive)
        {
            // F1: the row owns the passive it adds, so its exit never strips one an owner order set (`hold`, `flee`, `.bot co +passive`).
            // The read of "already passive" happens inside ApplyComePassive, before any `+passive`: a bot that was already passive keeps that
            // passive, and the same row's walk-back `come` keeps the row's token, never its new number. That token also gates the add: a walk
            // back whose row the 10 s cap already ended (the order may live 15 s) moves the bot and adds nothing, instead of turning it passive
            // with no owner left to release it.
            struct Engine
            {
                PlayerbotAI* ai;
                bool PassiveOn() const
                {
                    return ai->HasStrategy("passive", BotState::BOT_STATE_REACTION) ||
                        ai->HasStrategy("passive", BotState::BOT_STATE_NON_COMBAT) ||
                        ai->HasStrategy("passive", BotState::BOT_STATE_COMBAT);
                }
                void AddPassive() const
                {
                    ai->ChangeStrategy("+passive", BotState::BOT_STATE_REACTION);
                    ai->ChangeStrategy("+passive", BotState::BOT_STATE_NON_COMBAT);
                    ai->ChangeStrategy("+passive", BotState::BOT_STATE_COMBAT);
                }
            };
            place::PassiveBook* book = nullptr;
            if (auto rec = Recs().find(bot->GetGUID()); rec != Recs().end())
                book = &rec->second.placePassive;
            Engine engine{ ai };
            place::ApplyComePassive(engine, book, o.seq, o.passiveToken);
        }
        if (come.keepCombatStayOut)
            ai->ChangeStrategy("-stay", BotState::BOT_STATE_COMBAT);
        TortoiseBots::StayWalkOrdered(ai->GetAiObjectContext()->GetValue<ai::LastMovement&>("last movement")->Get().stayWalk);
        return ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) ? "ok" : "fail:strategy-not-registered:stay";
    }
    if (o.verb == "attack" || o.verb == "assist")
    {
        ObjectGuid unit = o.unit;
        if (o.verb == "assist")
        {
            Player* who = ObjectAccessor::FindPlayer(o.who);
            Unit* victim = who ? who->GetVictim() : nullptr;
            if (!victim && who)
                victim = ObjectAccessor::GetUnit(*bot, who->GetTarget());
            if (!victim || !victim->IsAlive() || !bot->IsValidAttackTarget(victim))
                return "fail:nothing-to-assist";
            unit = victim->GetGUID();
        }
        if (!bot->GetGroup())
            return "fail:no-party";
        // I13: an explicit order wins over `.tb ignore`: the mob leaves the commander's list before the attack runs.
        TortoiseBots::i13::NoteExplicitTarget(bot, unit);
        return AttackThroughMark(bot, ai, owner, unit, "attack rti target") ? "ok" : "fail:attack";
    }
    if (o.verb == "pull")
    {
        // D14: the order's own option text (`[seconds][ ranged|body]`, TbRunOrdersPure.h PullOptionText). A `ranged` or `body` request is
        // recorded on the PullStrategy for this pull only (BeginCommand), exactly as the pullback branch does, and rolled back when the
        // request refuses.
        ai::PullOrderOption option;
        ai::ParsePullOrderOption(o.arg, option);
        uint32 const delay = option.hasSeconds ? option.seconds : kPullJoinDelaySeconds;
        bool const command = option.ranged || option.body;
        ai::PullStrategy::CommandState commandBefore;
        bool hasCommandBefore = false;
        if (command)
        {
            bool const hadPullBack = ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT) || ai->HasStrategy("pull back", BotState::BOT_STATE_NON_COMBAT);
            if (ai::PullStrategy* probe = ai::PullStrategy::Get(ai))
            {
                commandBefore = probe->SaveCommand();
                hasCommandBefore = true;
            }
            if (ai::PullStrategy* strategy = ai::PullStrategy::Get(ai))
                strategy->BeginCommand(false, hadPullBack, delay, option.ranged, option.body);
        }
        // I13: an explicit order wins over `.tb ignore`, as for attack above.
        TortoiseBots::i13::NoteExplicitTarget(bot, o.unit);
        if (!act("pull my target", Event("tb order", o.unit, owner)))
        {
            if (command)
                if (ai::PullStrategy* strategy = ai::PullStrategy::Get(ai))
                {
                    if (hasCommandBefore)
                        strategy->RestoreCommand(commandBefore);
                    else
                        strategy->OnPullEnded();
                }
            return "fail:pull";
        }
        // M2 live evidence (batch 3, C5): a `ranged` order whose tank walked in and meleed left no trace of why. The strategy wants the ranged opening for an
        // explicit order (DecidePullOpening) and falls back to the body pull when the weapon cannot fire (GetRangedBlocker: no weapon, untrained, no spell,
        // no ammo, or the mob inside the shot's dead zone). Say which one in the log so a fallback is read, not guessed.
        if (option.ranged)
            if (ai::PullStrategy* strategy = ai::PullStrategy::Get(ai))
                if (Unit* mob = ObjectAccessor::GetUnit(*bot, o.unit))
                {
                    ai::PullRangedBlocker const blocker = strategy->GetRangedBlocker(bot->GetDistance(mob));
                    if (blocker != ai::PullRangedBlocker::None)
                        sLog.outString("TortoiseBots order: %s seq %u (pull ranged) opens as a body pull: %s", bot->GetName().c_str(), o.seq, ai::PullRangedBlockerName(blocker));
                }
        // L2 (package L): `pull my target` places no hold (only the chat `pullback` and the command layer's PausePartyDpsForPull do), so the damage
        // bots and the healer's damage spells opened on a mob the tank had not reached (M package, observation d). The same party hold as the
        // legacy chat pull, lead-keyed: damage warriors stay on the commander's anchor, every member's window opens only after the pull LANDED
        // (PullActions.cpp RestampPullParty) and ends when the whole pack is on the tank (the order's seconds, 3 s by default, and 10 s at the
        // latest), at once when a mob hits a held bot or the tank is under 60 %, and at the latest 15 s after the order if the pull never lands.
        RelaxTankForPull(bot, ai, o.fromRun);
        ai::PausePartyPullHold(bot, delay, true, kPullApproachWindowSeconds);
        if (o.fromRun)
            AnchorPullHoldOnTank(bot);
        return "ok";
    }
    if (o.verb == "endpull")
        return act("end pull", Event("tb order", std::string(), owner)) ? "ok" : "fail:end-pull";
    // H4: the order verb `release` (ChatActionContext "release"): the dead bot releases its spirit now. Inside a dungeon the core revives it at the
    // instance's entrance (D2). The action's own result is the order's result; a bot that is not dead has nothing to release (`fail:release`).
    if (o.verb == "release")
        return act("release", Event("tb order", std::string(), owner)) ? "ok" : "fail:release";
    if (o.verb == "tame")
    {
        std::string arg = o.arg;
        if (!arg.empty())
        {
            std::istringstream iss(arg);
            std::string first;
            iss >> first;
            std::string firstLower = first;
            for (char& c : firstLower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (firstLower != "abandon" && firstLower != "rename" && firstLower != "target" &&
                firstLower != "name" && firstLower != "id" && firstLower != "family" && firstLower != "guid")
            {
                ObjectGuid resolved;
                std::string code, detail;
                if (ResolveTarget(bot, owner, arg, resolved, code, detail))
                    arg = "guid " + std::to_string(resolved.GetCounter());
            }
        }
        return act("tame", Event("tb order", arg, owner)) ? "ok" : "fail:tame";
    }
    if (o.verb == "sell")
        return act("sell", Event("tb order", o.arg.empty() ? std::string("gray") : o.arg, owner)) ? "ok" : "fail:sell";
    if (o.verb == "buy")
        return act("buy", Event("tb order", o.arg.empty() ? std::string("vendor") : o.arg, owner)) ? "ok" : "fail:buy";
    if (o.verb == "repair")
        return act("repair", Event("tb order", std::string(), owner)) ? "ok" : "fail:repair";
    if (o.verb == "train")
        return act("trainer", Event("tb order", o.arg.empty() ? std::string("learn") : o.arg, owner)) ? "ok" : "fail:train";
    if (o.verb == "mail")
    {
        if (!tbhost::town::FindNearestMailbox(bot))
            return "fail:no-mailbox";
        std::string const arg = o.arg.empty() ? std::string("take") : o.arg;
        if (act("mail", Event("tb order", arg, owner)))
            return "ok";
        return arg.rfind("take", 0) == 0 ? "fail:mail-none-taken" : "fail:mail";
    }
    if (o.verb == "learn-prof")
    {
        uint32 const skillId = TortoiseBots::prof::SkillIdFromName(o.arg);
        std::string code, detail;
        return tbhost::prof::LearnProfession(bot, skillId, code, detail) ? "ok" : ("fail:" + code);
    }
    if (o.verb == "gather")
    {
        std::string stateOut, detail;
        return tbhost::prof::StartGatherNearest(bot, 150.0f, o.seq, stateOut, detail) ? stateOut : ("fail:" + stateOut);
    }
    if (o.verb == "craft")
    {
        uint32 spellId = 0;
        uint32 count = 1;
        std::istringstream iss(o.arg);
        iss >> spellId >> count;
        if (count == 0)
            count = 1;
        std::string stateOut, detail;
        return tbhost::prof::StartCraft(bot, spellId, count, o.seq, stateOut, detail) ? stateOut : ("fail:" + stateOut);
    }
    if (o.verb == "taxi-learn")
    {
        Creature* npc = tbhost::taxi::NearestFlightMaster(bot, 30.0f);
        if (!npc)
            return "fail:no-flightmaster";
        return tbhost::taxi::Learn(bot, npc) ? "ok" : "fail:taxi-learn";
    }
    if (o.verb == "fly")
    {
        Creature* npc = tbhost::taxi::NearestFlightMaster(bot, 30.0f);
        if (!npc)
            return "fail:no-flightmaster";
        uint32_t const toNode = tbhost::taxi::ResolveNodeArg(bot, o.arg);
        if (!toNode)
            return "fail:no-node";
        std::string failCode;
        return tbhost::taxi::Fly(bot, npc, toNode, &failCode) ? "ok" : ("fail:" + (failCode.empty() ? std::string("fly") : failCode));
    }
    if (o.verb == "ride")
    {
        uint32_t const entry = static_cast<uint32_t>(std::strtoul(o.arg.c_str(), nullptr, 10));
        if (!entry)
            return "fail:bad-entry";
        std::string stateOut;
        return tbhost::ride::StartRide(bot, entry, &stateOut, o.seq) ? stateOut : ("fail:" + (stateOut.empty() ? std::string("ride") : stateOut));
    }
    if (o.verb == "ah")
    {
        if (!tbhost::ah::IsWorldAuctionEnabled())
            return "fail:auction-disabled";
        auto ahResult = [&](bool ok, char const* fallback) -> std::string
        {
            if (ok)
                return "ok";
            std::string const code = tbhost::ah::TakeLastFailCode(bot);
            return "fail:" + (code.empty() ? std::string(fallback) : code);
        };
        std::string const sub = Lower(o.arg);
        if (sub == "list")
            return ahResult(act("ah", Event("tb order", std::string("list"), owner)), "ah-list");
        if (sub == "sell" || sub.rfind("sell ", 0) == 0)
        {
            std::string const param = (sub == "sell") ? std::string("vendor") : o.arg.substr(5);
            return ahResult(act("ah", Event("tb order", param, owner)), "ah-sell");
        }
        if (sub.rfind("buy ", 0) == 0)
            return ahResult(act("ah bid", Event("tb order", o.arg.substr(4), owner)), "ah-buy");
        if (sub == "cancel" || sub.rfind("cancel ", 0) == 0)
        {
            std::string const param = (sub == "cancel") ? std::string("all") : o.arg.substr(7);
            return ahResult(act("ah cancel", Event("tb order", param, owner)), "ah-cancel");
        }
        return "fail:ah-arg";
    }
    if (o.verb == "hold")
    {
        ai->ChangeStrategy("+passive", BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy("+passive", BotState::BOT_STATE_NON_COMBAT);
        return ai->HasStrategy("passive", BotState::BOT_STATE_COMBAT) ? "ok" : "fail:strategy-not-registered:passive";
    }
    if (o.verb == "free")
    {
        // Drop every order: the factory kit again (it ends a running pull window too), the movement mode follow as engine::Create sets it, and the
        // explicit attack target the attack orders keep.
        ApplyKit(ai, ai->GetForcedRole(), false);
        ai->SetMovementStrategy("follow");
        if (ai::Value<ObjectGuid>* value = ai->GetAiObjectContext()->GetValue<ObjectGuid>("explicit attack target"))
            value->Set(ObjectGuid());
        return "ok";
    }
    if (o.verb == "co" || o.verb == "nc")
    {
        ai->ChangeStrategy(o.arg, o.verb == "co" ? BotState::BOT_STATE_COMBAT : BotState::BOT_STATE_NON_COMBAT);
        if (!sPlayerbotAIConfig.bExplicitDbStoreSave)
            sPlayerbotDbStore.Save(ai);
        return "ok";
    }
    if (o.verb == "role")
    {
        // N4a: the role is committed HERE, when the queued order runs (the kit rebuild below reads the stored role first), never when the command
        // is typed: a refused or expired order must not change a role.
        uint8 const role = uint8(std::atoi(o.arg.c_str()));
        PlayerbotAIStorage::Instance().SetPlayerForcedRole(bot->GetGUID(), role);
        if (role)
            Roles()[bot->GetGUID()] = role;
        else
            Roles().erase(bot->GetGUID());
        sPlayerbotDbStore.InvalidateStrategySnapshots(ai);
        ApplyKit(ai, role, true);
        return "ok";
    }
    if (o.verb == "rekit")
    {
        ApplyKit(ai, ai->GetForcedRole(), false);
        return "ok";
    }
    if (o.verb == "spec")
    {
        int8 const tab = int8(std::atoi(o.arg.c_str()));
        PlayerbotAIStorage::Instance().SetPlayerForcedSpecTab(bot->GetGUID(), tab);
        sPlayerbotDbStore.InvalidateStrategySnapshots(ai);
        ApplyKit(ai, ai->GetForcedRole(), false);
        return "ok";
    }
    // ---- O6: the verbs the run leader needs (run/TbRunOrders.cpp), reached through QueueOrder only ----
    if (o.verb == "move-direct")
    {
        // CommanderMoveDirect for the lead (Commander.cpp:3810): a straight walk (no path) to the point; the AI would re-plan it on the navmesh at its next
        // step (the stay spot is ahead), so it sleeps for the walk, at most 4 s (RemoteCommander::kDirectMaxSleepMs), and not in a fight. The followers get
        // a plain `follow` from the executor. The point's z is the floor the judge read under it.
        // R6 (review item 7, round 2): the straight line is read again where it is walked (the judge read it when the order was made; the bot may have moved since),
        // with the same move rule as the judge (TbRunLiquid.h): wet samples only as the contiguous prefix that holds the bot's own spot, the end dry.
        // Review fix 4: the order was queued up to 15 s ago for a bot on map `o.map`. A bot that is teleporting (GetMap() asserts with no map) or stands on another
        // map by now must not walk this point's coordinates on that map: the order fails, the run hears it as a late failure.
        // Codex review fix: the map id is equal for two copies of a dungeon, so the instance recorded at queueing must match too (PointOnBotMap; 0 = not recorded).
        Map* const map = bot->FindMap();
        if (!map || bot->IsBeingTeleported() || !PointOnBotMap(o.map, o.instance, bot->GetMapId(), bot->GetInstanceId()))
            return "fail:wrong-map";
        if (run::liquid::SegmentPrefixRefusedOnMap(map, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), o.x, o.y, o.z))
            return "fail:liquid";
        ai->DropQueuedMovementOrders();
        ai->Reset();
        ai->SetMovementStrategy("stay");
        WriteStay(ai, o.x, o.y, o.z, o.map);
        TortoiseBots::StayWalkOrdered(ai->GetAiObjectContext()->GetValue<ai::LastMovement&>("last movement")->Get().stayWalk);
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MovePoint(0, o.x, o.y, o.z, false);
        if (!bot->IsInCombat())
        {
            float const dx = o.x - bot->GetPositionX(), dy = o.y - bot->GetPositionY(), dz = o.z - bot->GetPositionZ();
            float const length = std::sqrt(dx * dx + dy * dy + dz * dz);
            float const speed = bot->GetSpeed(MOVE_RUN) > 1.0f ? bot->GetSpeed(MOVE_RUN) : 7.0f;
            ai->SetActionDuration(std::min<uint32>(uint32(length / speed * 1000.0f) + sPlayerbotAIConfig.reactDelay, 4000u));
        }
        return ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) ? "ok" : "fail:strategy-not-registered:stay";
    }
    if (o.verb == "pullback")
    {
        // The chat action `pullback my target` (PullbackMyTargetAction, PullActions.cpp:278) is not in the slice's action context, so its steps are done here
        // with the same public calls: the tank's "pull back" strategy on for this pull, the per-command mode recorded on the PullStrategy (the return leg and
        // the ranged opening read it), then `pull my target` (the same request action), and everything rolled back when the request refuses. The pull
        // anchor is the tank's own spot: it is where the party hold stands and where the tank returns to after the shot (the event's owner is the tank itself
        // for a run order, so the request's "pull" position is the same spot). The stay position is written there, the way `come` writes it, before the
        // request runs (the facts' leadStayKnown, and the walk back after a pick-up).
        WriteStay(ai, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
        ai::PullOrderOption option;
        ai::ParsePullOrderOption(o.arg, option);
        uint32 const delay = option.hasSeconds ? option.seconds : sPlayerbotAIConfig.pullBackDpsJoinDelay;
        uint32 const holdDelay = option.hasSeconds ? option.seconds : kPullJoinDelaySeconds;   // review fix: no seconds, the hold keeps its 3 s
        bool const hadPullBack = ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT) || ai->HasStrategy("pull back", BotState::BOT_STATE_NON_COMBAT);
        ai::PullStrategy::CommandState commandBefore;
        bool hasCommandBefore = false;
        if (ai::PullStrategy* probe = ai::PullStrategy::Get(ai))
        {
            commandBefore = probe->SaveCommand();
            hasCommandBefore = true;
        }
        ai->ChangeStrategy("+pull back", BotState::BOT_STATE_ALL);
        if (ai::PullStrategy* strategy = ai::PullStrategy::Get(ai))
            strategy->BeginCommand(true, hadPullBack, delay, option.ranged, option.body);
        if (!act("pull my target", Event("tb order", o.unit, owner)))
        {
            if (ai::PullStrategy* strategy = ai::PullStrategy::Get(ai))
            {
                if (hasCommandBefore)
                    strategy->RestoreCommand(commandBefore);
                else
                    strategy->OnPullEnded();
            }
            if (!hadPullBack)
                ai->ChangeStrategy("-pull back", BotState::BOT_STATE_ALL);
            return "fail:pullback";
        }
        RelaxTankForPull(bot, ai, o.fromRun);
        ai::PausePartyPullHold(bot, holdDelay, true, kPullApproachWindowSeconds);   // L2: the same lead-keyed hold as `pull`, from the order's seconds
        if (o.fromRun)
            AnchorPullHoldOnTank(bot);
        return "ok";
    }
    if (o.verb == "focus")
    {
        // CommanderFocus (Commander.cpp:3322) for the plain target: the skull on the unit in the bot's group, which STAYS for the fight (the engine's
        // "rti target" reads it every tick; no other order of the run puts it back), the bot off its stay, its "rti" on skull, then the attack on it.
        Group* group = bot->GetGroup();
        if (!group)
            return "fail:no-party";
        // I13: a focus is an explicit order too: it takes the mob off the commander's ignore list before the mark goes on.
        TortoiseBots::i13::NoteExplicitTarget(bot, o.unit);
        group->SetTargetIcon(uint8(MarkIndex("skull")), o.unit, owner->GetGUID());
        ai->DropQueuedMovementOrders();
        ai->EndStayForOrder();
        // The legacy `rti` action only sets the value of that name (RtiAction.cpp:31) and tells the player; the action is not in the slice's context.
        if (ai::Value<std::string>* rti = ai->GetAiObjectContext()->GetValue<std::string>("rti"))
            rti->Set("skull");
        else
            return "fail:focus-rti";
        if (ai::Value<Unit*>* value = ai->GetAiObjectContext()->GetValue<Unit*>("rti target"))
            value->Reset();
        if (act("attack rti target", Event("focus skull", std::string(), owner)))
            return "ok";
        // N6: "attack rti target" refuses when the marked unit is not in the bot's sight yet: the value `rti target` (RtiTargetValue.h CalculateMark) needs the unit
        // within AiPlayerbot.SightDistance AND in line of sight, and a pack is marked before the pull, usually out of both. The order is the MARK, and it is
        // set (group icon + `rti` value above): the tank's and the damage bots' own target picks read it every tick (CalculateFocus) as soon as the unit is
        // seen. So when the value says "not visible yet" the order did what it can do now and the answer is ok; any other refusal (the unit is visible and the
        // attack is still refused: evading, dead, charmed, out of the order range) stays fail:focus.
        // R6 (review item 8): ok ONLY when the marked unit exists, is alive, stands on the bot's map and the value is empty for the reason this comment names
        // (not in sight / out of range yet). An empty value over a dead, despawned or foreign-map unit is a failed order, not a mark that waits.
        ai::Value<Unit*>* seen = ai->GetAiObjectContext()->GetValue<Unit*>("rti target");
        Unit* marked = ObjectAccessor::GetUnit(*bot, o.unit);
        if (seen && !seen->Get() && FocusMarkWaits(marked != nullptr, marked && marked->IsInWorld(), marked && marked->IsAlive(),
                marked && marked->GetMapId() == bot->GetMapId()))
            return "ok";
        return "fail:focus";
    }
    if (o.verb == "rest")
    {
        // OrderRest (commands/BotCommandContext.cpp:124): drink when the mana is short, eat when the health is, "rest to full" so the resume triggers go on
        // to 100 %, and the bot sits out the meal (30 s). Nothing short = nothing to do = ok. The actions refuse in a fight (isUseful): fail:rest.
        bool const needMana = bot->GetPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA);
        bool const needHealth = bot->GetHealth() < bot->GetMaxHealth();
        if (!needMana && !needHealth)
            return "ok";
        if (!bot->IsAlive())
            return "fail:rest-dead";
        ai::Value<bool>* restToFull = ai->GetAiObjectContext()->GetValue<bool>("rest to full");
        if (restToFull)
            restToFull->Set(true);
        bool rested = false;
        if (needMana && act("drink", Event("tb order", std::string(), owner)))
            rested = true;
        if (needHealth && act("food", Event("tb order", std::string(), owner)))
            rested = true;
        if (rested)
        {
            ai->SetActionDuration(30000);
            return "ok";
        }
        if (restToFull)
            restToFull->Set(false);
        // N6: no meal was started. In a fight that is a real refusal (the actions refuse in combat). Out of combat the bot has nothing to eat or drink (no item,
        // or below the bot's own "should drink/eat" floors): it rests by regeneration, which the engine does for a bot that stands out of combat, and the order
        // is "ok" (it ends by the policy's own health and mana floors or its rest cap, not by an action). Before N6 this was fail:rest and the run logged a
        // late failure for every rest of a bot without food.
        bool const fighting = bot->IsInCombat() || ai->IsGroupMemberNearInCombat() || ai->GetAiObjectContext()->GetValue<bool>("has attackers")->Get();
        return fighting ? "fail:rest" : "ok";
    }
    if (tactics::IsTacticalVerb(o.verb))
        return tactics::Run(bot, ai, o.verb, o.arg, o.unit, owner, o.seq);
    return "fail:unknown-verb";
}

// Queues one order for one bot. Empty string = queued (seq in `seq`), else the refusal code and detail in `code`/`detail`.
bool Enqueue(Player* bot, Order order, std::string& code, std::string& detail, uint32& seq)
{
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
    {
        code = "no-engine";
        detail = "the bot has no running engine (`.tb engine <bot> on`)";
        return false;
    }
    BotRec& rec = Recs()[bot->GetGUID()];
    if (rec.queue.size() >= OrderQueueMax)
    {
        code = "queue-full";
        detail = "the bot's engine is not ticking: " + std::to_string(rec.queue.size()) + " orders wait";
        return false;
    }
    order.seq = ++SeqCounter();
    order.queuedAt = NowMs();
    seq = order.seq;
    rec.seq = order.seq;
    rec.verb = order.verb;
    rec.result = "queued";
    rec.ring.Push(order.seq, order.verb, "queued");
    rec.queue.push_back(std::move(order));
    return true;
}

std::string JoinFrom(std::vector<std::string> const& words, size_t from)
{
    std::string out;
    for (size_t i = from; i < words.size(); ++i)
        out += (out.empty() ? "" : " ") + words[i];
    return out;
}
}

// ---- the engine hooks ----

// H4 (slice 4, spec section 2): who offered the dead bot a resurrection, when that is a member of its party or its commander (a stranger's offer is
// DECLINED: the bot does nothing, the offer lapses by itself; the legacy action would accept any). The core keeps the pending offer on the Player
// (Player::SetResurrectRequestData, Player.cpp:25625); `IsResurrectRequestedBy` is the public question. Reading the Player's own state replaces a capture
// of SMSG_RESURRECT_REQUEST in the session: the same fact, on the world thread, with no packet to parse.
Player* ResurrectOffererOf(Player* bot)
{
    if (!bot || bot->IsAlive() || !bot->IsResurrectRequested())
        return nullptr;
    if (Group* group = bot->GetGroup())
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (slot.guid != bot->GetGUID() && bot->IsResurrectRequestedBy(slot.guid))
                if (Player* member = ObjectAccessor::FindPlayer(slot.guid); member && member->IsInWorld())
                    return member;
    if (Player* commander = CommanderOf(bot); commander && bot->IsResurrectRequestedBy(commander->GetGUID()))
        return commander;
    return nullptr;
}

// F1: an order that just ran, for the fall back's passive record (TbPlacePlan.h). A `come` keeps the row's owner token; `hold`, `flee` and an
// owner's `+passive` take the record over. A plain come/attack/follow leaves the record alone, so the row still releases its own passive on
// every exit path.
void NoteRanOrderPassive(BotRec& rec, Order const& order)
{
    place::NoteRanPassive(rec.placePassive, order.verb, order.arg, order.disengage, order.seq);
}

void BeforeEngineTick(Player* bot, PlayerbotAI* ai)
{
    if (!bot || !ai)
        return;
    ObjectGuid const guid = bot->GetGUID();

    // M3 (D7): re-enforce the per-bot toggle settings every tick, so a ResetStrategies or a `co`/`nc` change of the same
    // strategy does not win over the host table.
    tactics::BeforeTick(bot, ai);

    // H4: a dead bot with a pending resurrection from its own party answers it ("accept resurrect", the legacy action, with the offerer's guid as the event's
    // object: the shape the action reads). At most once per second per bot; the core's accept (ResurrectResponse, Response 0 = accept, MiscHandler.cpp:507-534)
    // is what the engine side turns the action into.
    if (!bot->IsAlive() && bot->IsResurrectRequested() && ActionRegistered(ai, "accept resurrect"))
        if (Player* offerer = ResurrectOffererOf(bot))
        {
            uint32 const nowMs = NowMs();
            uint32& last = LastAcceptMs()[guid];
            if (!last || nowMs - last >= 1000)
            {
                last = nowMs;
                bool const done = ai->DoSpecificAction("accept resurrect", Event("resurrect request", offerer->GetGUID(), bot), true);
                sLog.outString("TortoiseBots: %s answers the resurrection offered by %s: %s", bot->GetName().c_str(), offerer->GetName().c_str(), done ? "accepted" : "the action did not accept");
            }
        }

    // N7 (package N): a resurrection request that is NOT from the bot's party or its commander (a GM or a player outside) is DECLINED, not left pending: the core
    // keeps one request per player and refuses a second caster's while one is stored (Spell::EffectResurrect, SpellEffects.cpp:3833), so a stranger's request
    // would block the party priest and the body would wait. The core's own decline: ResurrectResponse with Response != 0 clears the request whoever sent it
    // (WorldSession::HandleResurrectResponse, MiscHandler.cpp:511-515).
    if (!bot->IsAlive() && bot->IsResurrectRequested() && !ResurrectOffererOf(bot) && bot->GetSession())
    {
        WorldPackets::Misc::ResurrectResponse decline{ WorldPacket(CMSG_RESURRECT_RESPONSE) };
        decline.Response = 1;
        bot->GetSession()->HandleResurrectResponse(decline);
        sLog.outString("TortoiseBots: %s declined a resurrection request from outside its party and commander", bot->GetName().c_str());
    }

    // A brain that was built after `.tb role` (the engine was off, or it was rebuilt) starts with no forced role: the stored role is applied
    // before its first tick, so the kit it runs is the one the role asks for.
    if (auto role = Roles().find(guid); role != Roles().end() && ai->GetForcedRole() != role->second)
        ApplyKit(ai, role->second, true);
    else if (ai->GetForcedRole() == 0 && Roles().find(guid) == Roles().end())
    {
        bool const hasTankKit = ai->ContainsStrategy(STRATEGY_TYPE_TANK);
        if (bot->GetGroup() || hasTankKit)
        {
            uint32 const nowMs = NowMs();
            uint32& last = LastRoleCheckMs()[guid];
            if (!last || nowMs - last >= 1000)
            {
                last = nowMs;
                bool const shouldTank = (AiFactory::GetPlayerRoles(bot) & BOT_ROLE_TANK) != 0;
                if (shouldTank != hasTankKit)
                    ApplyKit(ai, 0, false);
            }
        }
    }

    // R6 (review item 5): a run's leader out of combat with no pull under way never follows its commander (ShouldHoldLead): it holds where it stands.
    if (RunLeads().count(guid))
    {
        bool const following = ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("follow", BotState::BOT_STATE_REACTION);
        ai::PullStrategy* pull = ai::PullStrategy::Get(ai);
        bool const pullActive = pull && (pull->HasPullStarted() || pull->IsCommandActive());
        if (ShouldHoldLead(following, bot->IsInCombat(), pullActive))
        {
            WriteStay(ai, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId());
            ai->SetMovementStrategy("stay");
        }
    }

    // M2: an interrupt order that could not cast at its first look keeps trying at every tick while the target still casts (kInterruptWindowMs).
    {
        uint32 pendingSeq = 0;
        std::string pendingResult;
        if (tactics::PollPendingInterrupt(bot, ai, pendingSeq, pendingResult))
        {
            auto pendingRec = Recs().find(guid);
            if (pendingRec != Recs().end() && pendingRec->second.seq == pendingSeq)
            {
                pendingRec->second.result = pendingResult;
                pendingRec->second.ring.Push(pendingSeq, "interrupt", pendingResult);
            }
            if (pendingResult != "ok" && pendingResult.compare(0, 7, "pending") != 0)
                sLog.outString("TortoiseBots order: %s seq %u (interrupt) -> %s", bot->GetName().c_str(), pendingSeq, pendingResult.c_str());
        }
    }

    auto rec = Recs().find(guid);
    if (rec == Recs().end())
        return;
    if (rec->second.seq != 0)
    {
        std::string errandResult;
        bool const hasErrandUpdate =
            (tbhost::prof::PollOrderErrand(bot, rec->second.seq, errandResult) ||
             tbhost::ride::PollOrderErrand(bot, rec->second.seq, errandResult));
        if (hasErrandUpdate && rec->second.result != errandResult)
        {
            rec->second.result = errandResult;
            rec->second.ring.Push(rec->second.seq, rec->second.verb, errandResult);
            if (errandResult.compare(0, 5, "fail:") == 0)
                sLog.outString("TortoiseBots order: %s seq %u (%s) -> %s", bot->GetName().c_str(), rec->second.seq, rec->second.verb.c_str(), errandResult.c_str());
        }
    }
    if (rec->second.queue.empty())
        return;
    uint32 const now = NowMs();
    while (!rec->second.queue.empty())
    {
        Order order = std::move(rec->second.queue.front());
        rec->second.queue.pop_front();
        if (order.verb != "interrupt")
            tactics::CancelPendingInterrupt(bot);
        if (order.verb != "gather" && order.verb != "craft")
            tbhost::prof::CancelOrderErrand(bot);
        if (order.verb != "ride")
            tbhost::ride::CancelOrderErrand(bot);
        std::string result = now - order.queuedAt > OrderMaxAgeMs ? "fail:expired" : Run(bot, ai, order);
        if (result == "ok" || result.compare(0, 7, "pending") == 0)
            NoteRanOrderPassive(rec->second, order);
        // TBF-B1 review finding 2: this order's own outcome is kept for the provider whatever the record's newest word is (a newer
        // order may have taken `seq` over, and its ok/fail used to be the only one the API ever saw).
        rec->second.ring.Push(order.seq, order.verb, result);
        if (rec->second.seq == order.seq)       // a later order of the same bot keeps the later one's name and seq
            rec->second.result = result;
        if (result != "ok" && result.compare(0, 7, "pending") != 0)
            sLog.outString("TortoiseBots order: %s seq %u (%s) -> %s", bot->GetName().c_str(), order.seq, order.verb.c_str(), result.c_str());
    }
}

bool IsCommandedBot(Player* bot)
{
    return bot && !CommanderGuid(bot->GetGUID()).IsEmpty();
}

OrderStatus LastOrder(ObjectGuid bot)
{
    OrderStatus status;
    if (auto rec = Recs().find(bot); rec != Recs().end())
    {
        status.seq = rec->second.seq;
        status.verb = rec->second.verb;
        status.result = rec->second.result;
    }
    return status;
}

bool PollOrderEvent(ObjectGuid bot, OrderStatus& out)
{
    auto rec = Recs().find(bot);
    if (rec == Recs().end())
        return false;
    OrderTransitionRing::Entry entry;
    if (!rec->second.ring.Take(entry))
        return false;
    out.seq = entry.seq;
    out.verb = entry.verb;
    out.result = entry.result;
    return true;
}

void ForgetBotRecords(ObjectGuid bot)
{
    // TBF-B1 review finding 7: the bot's order record (the ring with it), the accept clock and the role-check clock go at logout:
    // none of them grows with every bot guid that ever ticked, and nothing of the old session reaches the next reader.
    Recs().erase(bot);
    LastAcceptMs().erase(bot);
    LastRoleCheckMs().erase(bot);
}

uint8 WantedRole(ObjectGuid bot)
{
    auto role = Roles().find(bot);
    return role == Roles().end() ? uint8(0) : role->second;
}

bool ReleasePlaceStay(Player* bot, uint32 seq)
{
    if (!bot)
        return false;
    OrderStatus const last = LastOrder(bot->GetGUID());
    if (last.seq != seq || last.verb != "come")
        return false;      // another order owns the bot now: only the placement's own stay is ours to clear
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
        return false;
    ai->Reset();
    ai->SetMovementStrategy("follow");
    return true;
}

bool ReleasePlacePassive(Player* bot, uint32 seq)
{
    if (!bot)
        return false;
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
        return false;
    auto rec = Recs().find(bot->GetGUID());
    if (rec == Recs().end() || !place::TakeOwnerPassive(rec->second.placePassive, seq))
        return true;      // not a fall back's passive (an owner's, a `hold`'s, a `flee`'s, another row's, or a newer order's): never the row's to drop
    ai->ChangeStrategy("-passive", BotState::BOT_STATE_REACTION);
    ai->ChangeStrategy("-passive", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-passive", BotState::BOT_STATE_COMBAT);
    return true;
}

bool ReleasePlaceDisengage(Player* bot, uint32 seq)
{
    if (!bot)
        return false;
    OrderStatus const last = LastOrder(bot->GetGUID());
    if (last.seq != seq || last.verb != "come")
        return false;      // the fall-back order is not the bot's own any more
    if (!ReleasePlacePassive(bot, seq))
        return false;
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
        return false;
    // The bot fights what follows it now; the non-combat stay still brings it back to the spot after the fight.
    ai->ChangeStrategy("-stay", BotState::BOT_STATE_COMBAT);
    return true;
}

bool ReleasePlaceHold(Player* bot, float x, float y, float z)
{
    PlayerbotAI* ai = AiOf(bot);
    if (!ai || !ai->GetAiObjectContext())
        return false;
    ai::PositionMap& positions = ai->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
    ai::PositionEntry const& stay = positions["stay"];
    if (!stay.valueSet)
        return false;
    float const dx = stay.x - x, dy = stay.y - y, dz = stay.z - z;
    if (dx * dx + dy * dy > 1.0f || std::fabs(dz) > 2.0f)
        return false;      // a movement order wrote its own stay: leave it alone
    ai->ChangeStrategy("-stay", BotState::BOT_STATE_REACTION);
    ai->ChangeStrategy("-stay", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-stay", BotState::BOT_STATE_COMBAT);
    ai->ChangeStrategy("-passive", BotState::BOT_STATE_REACTION);
    ai->ChangeStrategy("-passive", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-passive", BotState::BOT_STATE_COMBAT);
    return true;
}

// ---- O6: the queue as a call (TbOrdersApi.h) ----

namespace
{
// The refusals PullRequestAction::Execute would make later (PullActions.cpp:200-224), asked now (commands/Party.cpp:528 CheckPullTarget, same codes and
// texts): a refused pull must change no bot's state, and the run leader's pause rules read the code.
bool CheckPullUnit(Player* bot, ObjectGuid guid, std::string& code, std::string& detail)
{
    Unit* target = ObjectAccessor::GetUnit(*bot, guid);
    if (!target || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
    {
        code = "invalid-target";
        detail = "The target is not on the tank's map any more.";
        return false;
    }
    if (!target->IsAlive())
    {
        code = "dead-target";
        detail = "The target is dead.";
        return false;
    }
    if (sServerFacade.IsFriendlyTo(target, bot))
    {
        code = "friendly";
        detail = "The target is friendly to the tank and cannot be pulled.";
        return false;
    }
    float const maxPullDistance = sPlayerbotAIConfig.reactDistance * 3;
    if (target->GetDistance(bot) > maxPullDistance)
    {
        code = "too-far";
        detail = "The target is too far from the tank to pull (more than " + std::to_string(uint32(maxPullDistance)) + " yd).";
        return false;
    }
    if (!ai::AttackersValue::IsValid(target, bot, nullptr, false))
    {
        code = "invalid-target";
        detail = "The tank cannot pull the target: it is not attackable, evading, out of the tank's sight, or a player.";
        return false;
    }
    return true;
}
}

bool PreflightOrder(Player* bot, OrderSpec const& spec, std::string& code, std::string& detail)
{
    PlayerbotAI* ai = bot ? AiOf(bot) : nullptr;
    if (!ai || !ai->GetAiObjectContext())
    {
        code = "no-engine";
        detail = "the bot has no running engine (`.tb engine <bot> on`)";
        return false;
    }
    if (auto rec = Recs().find(bot->GetGUID()); rec != Recs().end() && rec->second.queue.size() >= OrderQueueMax)
    {
        code = "queue-full";
        detail = "the bot's engine is not ticking: " + std::to_string(rec->second.queue.size()) + " orders wait";
        return false;
    }
    auto const need = [&](char const* key)
    {
        if (ActionRegistered(ai, key))
            return true;
        code = "action-not-registered";
        detail = std::string("the legacy action `") + key + "` is not registered in this build";
        return false;
    };
    std::string const& verb = spec.verb;
    if (verb == "follow")
        return need("follow chat shortcut");
    if (verb == "stay")
        return need("stay chat shortcut");
    if (verb == "endpull")
        return need("end pull");
    if (verb == "come" || verb == "move-direct" || verb == "hold")
        return true;
    if (verb == "release")
    {
        if (!need("release"))
            return false;
        if (!BotMgr::Instance().ReleaseHasEntrance(bot))
        {
            code = "no-entrance";
            detail = "the instance has no entrance point (no script or no trigger target): a release would revive the bot outside, so it is held";
            return false;
        }
        return true;
    }
    if (verb == "sell")
        return need("sell");
    if (verb == "buy")
        return need("buy");
    if (verb == "repair")
        return need("repair");
    if (verb == "train")
        return need("trainer");
    if (verb == "mail")
        return need("mail");
    if (verb == "learn-prof" || verb == "gather" || verb == "craft")
        return true;
    if (verb == "ah")
        return need("ah") && need("ah bid") && need("ah cancel");
    if (verb == "rest")
    {
        bool const needMana = bot->GetPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA);
        bool const needHealth = bot->GetHealth() < bot->GetMaxHealth();
        if (!bot->IsAlive())
        {
            code = "not-alive";
            detail = "the bot is dead";
            return false;
        }
        return (!needMana || need("drink")) && (!needHealth || need("food"));
    }
    if (verb == "attack" || verb == "focus" || verb == "pull" || verb == "pullback")
    {
        char const* key = (verb == "attack" || verb == "focus") ? "attack rti target" : "pull my target";
        if (!need(key))
            return false;
        if (verb == "focus" && !ai->GetAiObjectContext()->GetValue<std::string>("rti"))
        {
            code = "value-not-registered";
            detail = "the value `rti` is not registered in this build";
            return false;
        }
        if (!bot->GetGroup())
        {
            code = "no-party";
            detail = "the order uses the raid mark of the bot's party; the bot is in no party";
            return false;
        }
        if (verb == "pull" || verb == "pullback")
        {
            if (!ai->HasStrategy("pull", BotState::BOT_STATE_COMBAT) || !run::IsTankMember(bot))
            {
                code = "no-pull-strategy";
                detail = "the bot has no `pull` combat strategy or is not a tank (give it the tank role)";
                return false;
            }
            return CheckPullUnit(bot, spec.unit, code, detail);
        }
        Unit* unit = ObjectAccessor::GetUnit(*bot, spec.unit);
        if (!unit)
        {
            code = "target-not-found";
            detail = "no such unit on the bot's map";
            return false;
        }
        if (!unit->IsAlive())
        {
            code = "target-dead";
            detail = "the target is dead";
            return false;
        }
        if (!bot->IsValidAttackTarget(unit))
        {
            code = "not-attackable";
            detail = "the core says this bot may not attack that unit";
            return false;
        }
        return true;
    }
    if (tactics::IsTacticalVerb(spec.verb))
        return tactics::Preflight(bot, spec, code, detail);
    code = "unknown-verb";
    detail = verb;
    return false;
}

bool QueueOrder(Player* bot, OrderSpec const& spec, std::string& code, std::string& detail, uint32& seq)
{
    if (!PreflightOrder(bot, spec, code, detail))
        return false;
    Order order;
    order.verb = spec.verb;
    order.arg = spec.arg;
    order.unit = spec.unit;
    order.x = spec.x;
    order.y = spec.y;
    order.z = spec.z;
    order.map = spec.map;
    order.instance = spec.instance;
    order.fromRun = spec.fromRun;
    order.disengage = spec.disengage;
    order.placed = spec.placed;
    order.passiveToken = spec.passiveToken;
    Player* commander = CommanderOf(bot);
    order.owner = (commander && !spec.fromRun) ? commander->GetGUID() : bot->GetGUID();
    return Enqueue(bot, std::move(order), code, detail, seq);
}

uint8 BotNaturalRoles(Player* bot)
{
    return bot ? uint8(AiFactory::GetPlayerRoles(bot)) : uint8(0);
}

bool SetBotRole(Player* bot, uint8 role, std::string& code, std::string& detail)
{
    PlayerbotAI* ai = bot ? AiOf(bot) : nullptr;
    if (!ai || !ai->GetAiObjectContext())
    {
        code = "no-engine";
        detail = "the bot has no running engine (`.tb engine <bot> on`)";
        return false;
    }
    TortoiseBots::slice::hybrid::TankOrderStep const plan = TortoiseBots::slice::hybrid::PlanTankOrder(
        unsigned(bot->GetClass()), role == RoleTank,
        PlayerbotAIStorage::Instance().GetPlayerForcedRole(bot->GetGUID()) == RoleTank,
        ai->GetForcedRole() == RoleTank);
    if (plan.refuse)
    {
        if (plan.clearStored)
            PlayerbotAIStorage::Instance().SetPlayerForcedRole(bot->GetGUID(), 0);
        if (auto stored = Roles().find(bot->GetGUID()); stored != Roles().end() && stored->second == RoleTank)
            Roles().erase(stored);
        if (plan.clearAi)
            ApplyKit(ai, 0, true);
        else if (plan.clearStored)
            ApplyKit(ai, ai->GetForcedRole(), false);
        code = "no-tank-kit";
        detail = bot->GetName() + " has no tank kit in this slice: the tank role needs a warrior, a druid (bear) or a paladin (protection, recorded only)";
        return false;
    }
    PlayerbotAIStorage::Instance().SetPlayerForcedRole(bot->GetGUID(), role);
    if (role)
        Roles()[bot->GetGUID()] = role;
    else
        Roles().erase(bot->GetGUID());
    sPlayerbotDbStore.InvalidateStrategySnapshots(ai);
    ApplyKit(ai, role, true);
    // A bot taken out of free play keeps the free movement strategy through ApplyKit; a fill bot follows the player it was grouped with.
    ai->SetMovementStrategy("follow");
    code.clear();
    detail.clear();
    return true;
}

// R6 (review item 1): see TbOrdersApi.h. The queue part is the pure CancelFromRun (TbOrderQueuePure.h, tools/test_run_cancel.cpp).
uint32 CancelRunOrders(ObjectGuid bot)
{
    auto rec = Recs().find(bot);
    if (rec == Recs().end())
        return 0;
    uint32 lastCancelled = 0;
    size_t const removed = CancelFromRun(rec->second.queue, [&lastCancelled](Order const& order) { lastCancelled = std::max(lastCancelled, order.seq); });
    // The bot's last order was one of them: its result must not read "queued" (nothing is going to run it), and the run's late-failure read hears the truth.
    if (removed && rec->second.seq == lastCancelled)
    {
        rec->second.result = "fail:cancelled";
        rec->second.ring.Push(rec->second.seq, rec->second.verb, "fail:cancelled");   // finding 2: the ticket hears the truth
    }
    return uint32(removed);
}

void HoldRunLead(ObjectGuid bot, bool on)
{
    if (on)
        RunLeads().insert(bot);
    else
        RunLeads().erase(bot);
}

void EndRunPull(Player* bot)
{
    PlayerbotAI* ai = bot ? AiOf(bot) : nullptr;
    if (!ai || !ai->GetAiObjectContext())
        return;
    // The tank: the pull strategy's own end (the "end pull" action restores the per-command pullback mode and the pet stance, then clears the pull).
    if (ai::PullStrategy* strategy = ai::PullStrategy::Get(ai); strategy && (strategy->HasPullStarted() || strategy->IsCommandActive()))
        ai->DoSpecificAction("end pull", Event("tb order", std::string(), bot), true);
    // Every bot: its own join window / party hold (the anchor copy and the stay it owns go with it).
    ai->EndPullWindow("run-end");
}

// ---- `.tb order <bot|party [leader]> <verb> [arg] [#token]` ----

bool HandleOrder(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "order"))
        return false;
    Player* sender = SenderPlayer(handler);

    // A later command from the commander ends a pending `place ... cursor` request.
    if (sender)
        place::CancelPending(sender->GetGUID(), handler);

    // `party` takes the next word as the leader unless that word is already the verb.
    bool const leaderFollows = args.w.size() >= 2 && Lower(args.w[0]) == "party" && !IsVerb(Lower(args.w[1]));
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, sender, leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "order", who.code, who.detail);
    if (pos >= args.w.size())
        return Fail(handler, args, "order", "missing-verb", "follow, stay, come, attack, pull, interrupt, endpull, hold, free, assist, co, nc, release, stop, flee, guard, stance, engage, rebuff, aoe, autocc, sap, loot, corpserun");
    std::string verb = Lower(args.w[pos++]);
    if (verb == "end")
    {
        if (pos < args.w.size() && Lower(args.w[pos]) == "pull")
            ++pos;
        verb = "endpull";
    }
    // M3: the two-word toggle and corpse-run forms (`auto cc`, `corpse run`) normalise to the one-word verbs.
    if (verb == "auto" && pos < args.w.size() && Lower(args.w[pos]) == "cc")
    {
        ++pos;
        verb = "autocc";
    }
    else if (verb == "corpse" && pos < args.w.size() && Lower(args.w[pos]) == "run")
    {
        ++pos;
        verb = "corpserun";
    }
    if (!IsVerb(verb))
        return Fail(handler, args, "order", "unknown-verb", verb);
    std::string const rest = JoinFrom(args.w, pos);

    // The tactical verbs have their own words (a stance name, the engage seconds) and their own refusal codes.
    bool const tactical = tactics::IsTacticalVerb(verb);
    std::string tacticArg = rest;
    if (tactical)
    {
        if (!tactics::Enabled())
            return Fail(handler, args, "order", "disabled", "TortoiseBots.Orders.Tactical is 0");
        if (verb == "stance")
        {
            if (rest.empty())
                return Fail(handler, args, "order", "missing-argument", "stance <near|tank|turnback|turn|behind>");
            if (!tactics::NormaliseStance(rest, tacticArg))
                return Fail(handler, args, "order", "bad-argument", "stance <near|tank|turnback|turn|behind>");
        }
        else if (verb == "engage" && !rest.empty())
        {
            unsigned seconds = 0;
            if (!tactics::ParseEngageSeconds(rest, seconds))
                return Fail(handler, args, "order", "bad-argument", "engage [seconds 0-60]");
        }
        else if (verb == "aoe" || verb == "autocc" || verb == "sap" || verb == "loot")
        {
            bool ok = false;
            tactics::ToggleOp const op = tactics::ParseToggle(rest, ok);
            if (!ok)
                return Fail(handler, args, "order", "bad-argument", verb + " [on|off|status]");
            tacticArg = rest;
            // A `status` is a read, not an order: answer the current state per bot without queuing (D1).
            if (op == tactics::ToggleOp::Status)
            {
                Reply reply(handler, args, "order");
                reply.Begin();
                uint32 reads = 0, refused = 0;
                for (Player* bot : who.bots)
                {
                    std::string code, detail;
                    if (tactics::ToggleStatusFault(bot, verb, code, detail))
                    {
                        Rec rec("order");
                        rec.S("bot", bot->GetName()).S("verb", verb).S("status", "refused").S("code", code).T("msg", detail);
                        reply.Line(rec);
                        ++refused;
                        continue;
                    }
                    bool on = false;
                    tactics::ReadToggle(bot, verb, on);
                    Rec rec("order");
                    rec.S("bot", bot->GetName()).S("verb", verb).S("status", "read").S("state", on ? "on" : "off");
                    reply.Line(rec);
                    ++reads;
                }
                Rec end("order");
                end.S("verb", verb).N("bots", reads + refused).N("read", reads).N("queued", 0).N("refused", refused);
                reply.End(end);
                return true;
            }
        }
    }

    if (verb == "interrupt")
    {
        if (rest.empty())
            return Fail(handler, args, "order", "missing-argument", "interrupt <my | mark | skull | spawn:<id> | 32-hex guid>");
        Player* probeBot = who.bots.empty() ? nullptr : who.bots.front();
        if (!probeBot)
            return Fail(handler, args, "order", "not-alive", "no bot of the order is in the world");
        ObjectGuid unit;
        std::string code, detail;
        if (!ResolveTarget(probeBot, sender, rest, unit, code, detail))
        {
            if (code == "target-not-found")
                code = "no-target";
            else if (code == "target-dead")
                code = "dead-target";
            return Fail(handler, args, "order", code, detail);
        }
        Unit* target = ObjectAccessor::GetUnit(*probeBot, unit);
        if (!target || !target->IsInWorld())
            return Fail(handler, args, "order", "no-target", "no such unit on the bot's map");
        if (!target->IsNonMeleeSpellCast(true))
            return Fail(handler, args, "order", "not-casting", "the target is not casting an interruptible spell");
        // D6: the party order decides which bot, and only that bot receives the order.
        std::string action, reasons;
        Player* picked = tactics::PickInterruptBot(who.bots, target, action, reasons);
        if (!picked)
            return Fail(handler, args, "order", "no-interrupt", "no bot has a ready interrupt for this target" +
                (reasons.empty() ? std::string() : ": " + reasons));
        h3::OrderSpec spec;
        spec.verb = "interrupt";
        spec.arg = action;
        spec.unit = unit;
        uint32 seq = 0;
        if (!QueueOrder(picked, spec, code, detail, seq))
            return Fail(handler, args, "order", code.empty() ? "failed" : code, detail);
        Reply reply(handler, args, "order");
        reply.Begin();
        Rec rec("order");
        rec.S("bot", picked->GetName()).S("verb", "interrupt").N("seq", seq).S("status", "queued");
        reply.Line(rec);
        Rec end("order");
        end.S("verb", "interrupt").P("target", LabelParts(who.label)).N("bots", 1).N("queued", 1).N("refused", 0);
        reply.End(end);
        cmd::NoteOrder({ { picked, std::string() } }, verb, rest);
        return true;
    }

    // M2: `pull <target> [seconds] [ranged|body]`. The option is the same text the queued order parses on the bot's tick.
    std::string pullTarget = rest, pullOption;
    if (verb == "pull")
    {
        size_t const space = rest.find(' ');
        std::string optionText;
        if (space != std::string::npos)
        {
            pullTarget = rest.substr(0, space);
            optionText = rest.substr(space + 1);
        }
        ai::PullOrderOption option;
        if (!ai::ParsePullOrderOption(optionText, option))
            return Fail(handler, args, "order", "bad-argument", "pull <target> [seconds 0-60] [ranged|body]");
        pullOption = run::orders::PullOptionText(option);
    }

    if ((verb == "attack" || verb == "pull" || verb == "assist" || verb == "co" || verb == "nc" || verb == "learn-prof" || verb == "craft" || verb == "fly" || verb == "ride") && rest.empty())
        return Fail(handler, args, "order", "missing-argument", verb == "assist" ? "assist <member name>" :
            (verb == "co" || verb == "nc") ? "co|nc <+strategy,-strategy>" :
            verb == "learn-prof" ? "learn-prof <skill>" :
            verb == "craft" ? "craft <spell> [n]" :
            verb == "fly" ? "fly <node id|name>" :
            verb == "ride" ? "ride <transport entry>" : "attack|pull <my | mark | skull | spawn:<id> | 32-hex guid>");
    if ((verb == "stay" || verb == "come" || verb == "hold" || verb == "free" || verb == "endpull" || verb == "release" || verb == "repair" || verb == "gather" || verb == "taxi-learn" ||
            verb == "stop" || verb == "flee" || verb == "guard" || verb == "rebuff" || verb == "corpserun") && !rest.empty())
        return Fail(handler, args, "order", "unexpected-argument", rest);
    if (verb == "learn-prof" && TortoiseBots::prof::SkillIdFromName(rest) == 0)
        return Fail(handler, args, "order", "bad-argument", "unknown or unsupported profession (fishing is out)");
    if (verb == "craft")
    {
        uint32 spellId = 0;
        std::istringstream iss(rest);
        if (!(iss >> spellId) || spellId == 0)
            return Fail(handler, args, "order", "bad-argument", "craft <spellId> [n]");
    }
    if (verb == "ride")
    {
        uint32_t const entry = static_cast<uint32_t>(std::strtoul(rest.c_str(), nullptr, 10));
        if (!entry)
            return Fail(handler, args, "order", "bad-argument", "ride <transport entry>");
    }
    if (verb == "co" || verb == "nc")
        for (char c : rest)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '+' || c == '-' || c == ',' || c == '_'))
                return Fail(handler, args, "order", "bad-argument", "strategy changes use letters, digits, space, + - , _");

    uint32 queued = 0, refused = 0;
    std::vector<std::pair<Player*, std::string>> outcomes;   // per bot, for the card it stands on: its refusal code, empty when queued
    // `order-begin`, one line per bot, `order-end` (grammar version 2: only `error` is a one-line reply).
    Reply reply(handler, args, "order");
    reply.Begin();
    auto const emit = [&](Rec const& rec) { reply.Line(rec); };
    for (Player* bot : who.bots)
    {
        auto const refuse = [&](std::string const& code, std::string const& detail, char const* name = nullptr)
        {
            ++refused;
            outcomes.push_back({ bot, code });
            Rec rec("order");
            rec.S("bot", bot->GetName()).S("verb", verb).S("status", "refused").S("code", code);
            if (name)
                rec.S("name", name);
            rec.T("msg", detail);
            emit(rec);
        };

        PlayerbotAI* ai = AiOf(bot);
        if (!ai || !ai->GetAiObjectContext())
        {
            refuse("no-engine", "the bot has no running engine (`.tb engine <bot> on`)");
            continue;
        }

        Player* commander = CommanderOf(bot);
        Order order;
        order.verb = verb;
        order.owner = commander ? commander->GetGUID() : bot->GetGUID();
        order.arg = rest;
        char const* needAction[3] = { nullptr, nullptr, nullptr };
        std::string code, detail;

        if (tactical)
        {
            order.arg = tacticArg;
            h3::OrderSpec spec;
            spec.verb = verb;
            spec.arg = tacticArg;
            if (!tactics::Preflight(bot, spec, code, detail))
            {
                refuse(code, detail);
                continue;
            }
        }
        else if (verb == "follow")
        {
            needAction[0] = "follow chat shortcut";
            if (!rest.empty())
            {
                order.arg = Lower(rest);
                needAction[1] = "follow target";
                if (order.arg != "none")
                {
                    Player* member = FindOnline(rest);
                    Group* group = bot->GetGroup();
                    if (!member || !group || !group->IsMember(member->GetGUID()) || member == bot)
                    {
                        refuse("not-in-group", rest + " is not another member of the bot's party");
                        continue;
                    }
                }
            }
        }
        else if (verb == "stay")
            needAction[0] = "stay chat shortcut";
        else if (verb == "tame")
            needAction[0] = "tame";
        else if (verb == "come")
        {
            // The spot: where the sender stands (a GM's character), or the bound commander from the console.
            Player* spot = sender ? sender : commander;
            if (!spot)
            {
                refuse("no-commander", "no sender and no bound commander to come to (`.tb master`)");
                continue;
            }
            if (spot->GetMapId() != bot->GetMapId() || spot->GetInstanceId() != bot->GetInstanceId())
            {
                refuse("other-map", "the bot is not on the map of " + spot->GetName());
                continue;
            }
            if (bot->GetDistance(spot) > sPlayerbotAIConfig.reactDistance)
            {
                refuse("too-far", "the spot is further than the engine's react distance (" + std::to_string(int(sPlayerbotAIConfig.reactDistance)) +
                    " yd): the stay position would be dropped");
                continue;
            }
            order.x = spot->GetPositionX();
            order.y = spot->GetPositionY();
            order.z = spot->GetPositionZ();
            order.map = spot->GetMapId();
        }
        else if (verb == "attack" || verb == "pull")
        {
            needAction[0] = verb == "attack" ? "attack rti target" : "pull my target";
            if (!ResolveTarget(bot, sender, verb == "pull" ? pullTarget : rest, order.unit, code, detail))
            {
                refuse(code, detail);
                continue;
            }
            if (verb == "pull")
                order.arg = pullOption;
            if (verb == "attack" && !bot->GetGroup())
            {
                refuse("no-party", "attack uses the raid mark of the bot's party; the bot is in no party");
                continue;
            }
            if (verb == "pull" && !ai->HasStrategy("pull", BotState::BOT_STATE_COMBAT))
            {
                refuse("no-pull-strategy", "the bot has no `pull` combat strategy (give it the tank role)");
                continue;
            }
        }
        else if (verb == "assist")
        {
            needAction[0] = "attack rti target";
            Player* member = FindOnline(rest);
            Group* group = bot->GetGroup();
            if (!member || !group || !group->IsMember(member->GetGUID()) || member == bot)
            {
                refuse("not-in-group", rest + " is not another member of the bot's party");
                continue;
            }
            Unit* victim = member->GetVictim();
            if (!victim)
                victim = ObjectAccessor::GetUnit(*bot, member->GetTarget());
            if (!victim || !victim->IsAlive() || !bot->IsValidAttackTarget(victim))
            {
                refuse("nothing-to-assist", member->GetName() + " has no attackable victim or target now");
                continue;
            }
            order.who = member->GetGUID();
        }
        else if (verb == "endpull")
            needAction[0] = "end pull";
        else if (verb == "release")
        {
            needAction[0] = "release";
            // D2: no entrance on the instance = the core would revive the bot at a graveyard OUTSIDE (Player::RepopAtGraveyard, Player.cpp:5258-5263): refused.
            if (!BotMgr::Instance().ReleaseHasEntrance(bot))
            {
                refuse("no-entrance", "the instance has no entrance point (no script or no trigger target): a release would revive the bot outside, so it is held");
                continue;
            }
        }
        else if (verb == "sell")
            needAction[0] = "sell";
        else if (verb == "buy")
            needAction[0] = "buy";
        else if (verb == "repair")
            needAction[0] = "repair";
        else if (verb == "train")
            needAction[0] = "trainer";
        else if (verb == "mail")
        {
            needAction[0] = "mail";
            if (!tbhost::town::FindNearestMailbox(bot))
            {
                refuse("no-mailbox", "no interactable mailbox within reach");
                continue;
            }
        }
        else if (verb == "ah")
        {
            if (!tbhost::ah::IsWorldAuctionEnabled())
            {
                refuse("auction-disabled", "world auction is disabled (TortoiseBots.World.Auction=0)");
                continue;
            }
            std::string const sub = Lower(order.arg);
            if (sub != "list" && sub != "sell" && sub.rfind("sell ", 0) != 0 &&
                sub.rfind("buy ", 0) != 0 && sub != "cancel" && sub.rfind("cancel ", 0) != 0)
            {
                refuse("bad-ah-subverb", "expected: ah sell [item] | ah buy <item> [n] | ah list | ah cancel <id>");
                continue;
            }
            needAction[0] = "ah";
            needAction[1] = "ah bid";
            needAction[2] = "ah cancel";
        }

        bool missing = false;
        for (char const* key : needAction)
            if (key && !ActionRegistered(ai, key))
            {
                refuse("action-not-registered", std::string("the legacy action `") + key + "` is not registered in this build", key);
                missing = true;
                break;
            }
        if (missing)
            continue;

        bool const ownPull = verb == "pull" && onyxia::GroundBoss(bot->GetGUID()) == order.unit &&
            onyxia::GroundOwner(bot->GetGUID()) == bot->GetGUID();
        uint32 seq = 0;
        if (!Enqueue(bot, std::move(order), code, detail, seq))
        {
            refuse(code, detail);
            continue;
        }
        if (!ownPull)
            onyxia::ForgetGround(bot->GetGUID());
        ++queued;
        outcomes.push_back({ bot, std::string() });
        Rec rec("order");
        rec.S("bot", bot->GetName()).S("verb", verb).N("seq", seq).S("status", "queued");
        emit(rec);
    }
    cmd::NoteOrder(outcomes, verb, rest);   // P1: each card's own order log
    Rec end("order");
    end.S("verb", verb).P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("queued", queued).N("refused", refused);
    reply.End(end);
    return true;
}

// ---- `.tb role <bot|party [leader]> tank|healer|dps|none [#token]` ----

bool HandleRole(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "role"))
        return false;
    bool const leaderFollows = args.w.size() >= 3 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who;
    if (!args.w.empty() && target::IsRoleWord(Lower(args.w[0])))
    {
        // TB-1: no name means the sender's own character (the first word is already the role).
        std::string code, detail;
        Player* self = ResolveTestTarget(handler, std::string_view(), code, detail);
        if (!self)
            return Fail(handler, args, "role", code, detail);
        who.bots.push_back(self);
        who.label = self->GetName();
    }
    else
    {
        who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows, true);
        if (!who.ok())
            return Fail(handler, args, "role", who.code, who.detail);
    }
    if (pos >= args.w.size())
        return Fail(handler, args, "role", "missing-role", "tank, healer (heal), dps or none");
    std::string const word = Lower(args.w[pos]);
    uint8 role;
    if (word == "tank")
        role = RoleTank;
    else if (word == "healer" || word == "heal")
        role = RoleHealer;
    else if (word == "dps")
        role = RoleDps;
    else if (word == "none" || word == "clear")
        role = 0;
    else
        return Fail(handler, args, "role", "bad-role", word);

    char const* const roleName = role == RoleTank ? "tank" : role == RoleHealer ? "healer" : role == RoleDps ? "dps" : "none";
    uint32 queued = 0;
    Reply reply(handler, args, "role");
    reply.Begin();
    auto const emit = [&](Rec const& rec) { reply.Line(rec); };
    for (Player* bot : who.bots)
    {
        Rec rec("role");
        rec.S("bot", bot->GetName()).S("role", roleName);
        PlayerbotAI* ai = AiOf(bot);
        // Slice 10: a tank order is honoured only by a class this slice gives a tank kit (SliceHybridPolicy.h). Without this a shaman
        // (restoration only, D2) would store the tank bit, PlayerbotAI::IsTank would answer true and the run would admit it as its tank.
        // The refusal also clears a tank bit already on the bot (the two reads IsTank makes, the kit built from them and this tick's role
        // map): an order from an older build, `.bot role` (its shaman tank kit is not in this slice) or the dungeon finder can leave one.
        TortoiseBots::slice::hybrid::TankOrderStep const plan = TortoiseBots::slice::hybrid::PlanTankOrder(
            unsigned(bot->GetClass()), role == RoleTank,
            PlayerbotAIStorage::Instance().GetPlayerForcedRole(bot->GetGUID()) == RoleTank,
            ai && ai->GetForcedRole() == RoleTank);
        if (plan.refuse)
        {
            bool cleared = false;
            if (plan.clearStored)
            {
                PlayerbotAIStorage::Instance().SetPlayerForcedRole(bot->GetGUID(), 0);
                cleared = true;
            }
            auto stored = Roles().find(bot->GetGUID());
            if (stored != Roles().end() && stored->second == RoleTank)
            {
                Roles().erase(stored);
                cleared = true;
            }
            if (ai && plan.clearAi)
            {
                ApplyKit(ai, 0, true);
                cleared = true;
            }
            else if (ai && cleared)
                ApplyKit(ai, ai->GetForcedRole(), false);   // the kit was built from the stored bit just cleared
            rec.S("status", "refused").S("code", "no-tank-kit");
            if (cleared)
                rec.S("cleared", "yes");
            rec.T("msg", bot->GetName() + " has no tank kit in this slice: the tank role needs a warrior, a druid (bear) or a paladin (protection, recorded only)");
            emit(rec);
            continue;
        }
        if (!ai)
        {
            // No engine yet: the role is stored for the brain that is built later (BeforeEngineTick compares it with the AI's), and that is true now.
            PlayerbotAIStorage::Instance().SetPlayerForcedRole(bot->GetGUID(), role);
            if (role)
                Roles()[bot->GetGUID()] = role;
            else
                Roles().erase(bot->GetGUID());
            rec.S("status", "stored");
            emit(rec);
            continue;
        }
        // An engine runs: the order is queued FIRST and nothing is stored when that is refused; the role is committed when the order runs (Run).
        Order order;
        order.verb = "role";
        order.arg = std::to_string(unsigned(role));
        Player* commander = CommanderOf(bot);
        order.owner = commander ? commander->GetGUID() : bot->GetGUID();
        std::string code, detail;
        uint32 seq = 0;
        if (!Enqueue(bot, std::move(order), code, detail, seq))
        {
            rec.S("status", "refused").S("code", code).T("msg", detail);
            emit(rec);
            continue;
        }
        ++queued;
        rec.N("seq", seq).S("status", "queued");
        emit(rec);
    }
    Rec end("role");
    end.S("role", roleName).P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("queued", queued);
    reply.End(end);
    return true;
}

// ---- `.tb master <bot|party [leader]> <commander|none> [#token]` ----

bool HandleMaster(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "master"))
        return false;
    // `party <leader> <commander>` has three words, `party <commander>` two (the one bot party, or the bots already bound to a commander).
    bool const leaderFollows = args.w.size() >= 3 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "master", who.code, who.detail);
    if (pos >= args.w.size())
        return Fail(handler, args, "master", "missing-commander", "name the commander, or `none` to release the bots");
    std::string const name = args.w[pos];
    bool const release = Lower(name) == "none";
    Player* commander = release ? nullptr : FindOnline(name);
    if (!release && !commander)
        return Fail(handler, args, "master", "no-such-player", name + " is not in the world");
    std::map<ObjectGuid, cmd::master_bind::Verdict> verdicts;   // per bot, when the plain rule refuses the commander (BG2 row 5)
    if (!release && !MayLeadBots(handler, commander->GetGUID()))   // N5
    {
        // BG2 live row 5 (host/cmd/TbMasterBindPure.h): where the plain rule refuses the commander, one battleground instance and one
        // team still let an operator bind a pool bot to the human he watches. Every other refusal stays, and a command whose every
        // bot is refused refuses the way it always did (nothing is changed).
        bool bindable = false;
        for (Player* bot : who.bots)
        {
            cmd::master_bind::Facts facts;
            facts.botInstance = bot->GetBattlegroundId();
            facts.commanderInstance = commander->GetBattlegroundId();
            facts.sameTeam = bot->GetBGTeam() == commander->GetBGTeam();
            verdicts[bot->GetGUID()] = cmd::master_bind::Judge(facts);
            bindable = bindable || verdicts[bot->GetGUID()] == cmd::master_bind::Verdict::Allowed;
        }
        if (!bindable)
        {
            auto const itr = verdicts.find(who.bots.front()->GetGUID());
            cmd::master_bind::Verdict const verdict = itr == verdicts.end() ? cmd::master_bind::Verdict::NotAllowed : itr->second;
            return Fail(handler, args, "master", cmd::master_bind::CodeOf(verdict), cmd::master_bind::DetailOf(verdict, name));
        }
    }
    if (!release && IsHostBot(commander))
    {
        std::string code, detail;
        if (!CheckBotAccess(handler, commander, code, detail))
            return Fail(handler, args, "master", code, detail);
    }
    for (Player* bot : who.bots)
        if (bot == commander)
            return Fail(handler, args, "master", "self-master", "a bot cannot command itself");

    uint32 done = 0, queued = 0, refused = 0;
    std::vector<Player*> boundBots;   // what really bound or released: h3's table, after the host's answer
    // BG2 live row 5 (review F2): the KIND is per bot. A bot bound while it and the commander stand in one battleground instance on one team is a bg
    // member (never stored; it ends only with that match, host/cmd/TbCmdBindingLifePure.h BgBook/CheckBg); any other bot is normal. Each kind is
    // read from the core's own instance and team facts for that bot and handed to the binding check beside it; the instance is the commander's.
    std::vector<cmd::life::Kind> boundKinds;   // one per boundBots entry
    uint32 const commanderInstance = (!release && commander) ? commander->GetBattlegroundId() : 0;
    Reply reply(handler, args, "master");
    reply.Begin();
    auto const emit = [&](Rec const& rec) { reply.Line(rec); };
    for (Player* bot : who.bots)
    {
        ObjectGuid const guid = bot->GetGUID();
        Rec rec("master");
        rec.S("bot", bot->GetName());
        cmd::life::Kind kind = cmd::life::Kind::Normal;
        if (release)
        {
            // N4b: the host's answer is checked; on a failure nothing changes (the binding table is written only after the host accepted it).
            if (!TortoiseBots::BotManager::Instance().ClearBotMaster(guid))
            {
                ++refused;
                rec.S("status", "refused").S("code", "release-failed").S("msg", "the host could not release the master of this bot (it is not a logged-in host bot)");
                emit(rec);
                continue;
            }
            SetCommander(guid, ObjectGuid::Empty);
            rec.S("commander", "");
        }
        else
        {
            if (!verdicts.empty())
            {
                // BG2 row 5: the commander is another real player; only a bot inside the same match instance and on the same team binds.
                auto const itr = verdicts.find(guid);
                cmd::master_bind::Verdict const verdict = itr == verdicts.end() ? cmd::master_bind::Verdict::NotAllowed : itr->second;
                if (verdict != cmd::master_bind::Verdict::Allowed)
                {
                    ++refused;
                    rec.S("status", "refused").S("code", cmd::master_bind::CodeOf(verdict)).S("msg", cmd::master_bind::DetailOf(verdict, name));
                    emit(rec);
                    continue;
                }
            }
            // What the engine's own hire flow does with a master (BotManager::BindBotMaster: the host's master guid and the engine's pointer), and the
            // binding `IsInCommandedGroup` answers from. The party is not touched: the commander stays outside it (spec D2).
            if (!TortoiseBots::BotManager::Instance().BindBotMaster(guid, commander->GetGUID()))
            {
                ++refused;
                rec.S("status", "refused").S("code", "bind-failed").S("msg", "the host refused to bind this bot to that commander (it is not a logged-in host bot)");
                emit(rec);
                continue;
            }
            SetCommander(guid, commander->GetGUID());
            // The kind THIS bot gets, decided where the binding check will read it: both in one match instance and on one team.
            cmd::master_bind::Facts bgFacts;
            bgFacts.botInstance = bot->GetBattlegroundId();
            bgFacts.commanderInstance = commanderInstance;
            bgFacts.sameTeam = bot->GetBGTeam() == commander->GetBGTeam();
            kind = cmd::master_bind::IsBgBind(bgFacts) ? cmd::life::Kind::Bg : cmd::life::Kind::Normal;
            Group* group = bot->GetGroup();
            rec.S("commander", commander->GetName()).B("commander_in_party", group && group->IsMember(commander->GetGUID()));
        }
        ++done;
        boundBots.push_back(bot);
        boundKinds.push_back(kind);
        // The kit depends on the binding (the commanded threat back-off, AiFactory.cpp:633-642): a running engine rebuilds it at its next tick.
        if (PlayerbotAI* ai = AiOf(bot))
        {
            Order order;
            order.verb = "rekit";
            order.owner = guid;
            std::string code, detail;
            uint32 seq = 0;
            if (Enqueue(bot, std::move(order), code, detail, seq))
            {
                ++queued;
                rec.N("seq", seq).S("status", "queued");
            }
            else
                rec.S("status", "bound").S("rekit", "refused").S("code", code);
        }
        else
            rec.S("status", "bound");
        emit(rec);
    }
    cmd::NoteMaster(boundBots, boundKinds, who.leader, commander, commanderInstance);   // P1: the commander record (lead, boundAt) beside h3's table — only what really bound (or was released), each with its own kind
    Rec end("master");
    end.P("target", LabelParts(who.label)).S("commander", release ? std::string() : std::string(commander->GetName())).N("bots", who.bots.size()).N("bound", done).N("queued", queued).N("refused", refused);
    reply.End(end);
    return true;
}

// ---- `.tb spec <bot|party [leader]> <tab|spec|none> [#token]` ----

bool HandleSpec(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "spec"))
        return false;
    bool const leaderFollows = args.w.size() >= 3 && Lower(args.w[0]) == "party";
    size_t pos = 0;
    Address who = ResolveAddress(args, pos, SenderPlayer(handler), leaderFollows);
    if (!who.ok())
        return Fail(handler, args, "spec", who.code, who.detail);
    if (pos >= args.w.size())
        return Fail(handler, args, "spec", "missing-argument", "specify tab (0, 1, 2), spec name, or none");

    std::string word = Lower(args.w[pos]);
    for (size_t i = pos + 1; i < args.w.size(); ++i)
        word += "_" + Lower(args.w[i]);

    for (Player* bot : who.bots)
    {
        int checkTab = -1;
        if (!TortoiseBots::slice::spec::ParseSpecArg(unsigned(bot->GetClass()), word, checkTab))
            return Fail(handler, args, "spec", "bad-argument", args.w[pos]);
    }

    uint32 queued = 0;
    Reply reply(handler, args, "spec");
    reply.Begin();
    for (Player* bot : who.bots)
    {
        int tab = -1;
        TortoiseBots::slice::spec::ParseSpecArg(unsigned(bot->GetClass()), word, tab);
        Rec rec("spec");
        rec.S("bot", bot->GetName());
        if (tab < 0)
            rec.S("tab", "auto").S("spec", "auto");
        else
            rec.N("tab", tab).S("spec", TortoiseBots::slice::spec::SpecTabName(unsigned(bot->GetClass()), tab));

        PlayerbotAI* ai = AiOf(bot);
        if (!ai)
        {
            PlayerbotAIStorage::Instance().SetPlayerForcedSpecTab(bot->GetGUID(), int8(tab));
            ai::slice::aistore::InvalidateStrategySnapshots(bot->GetGUID().GetCounter());
            rec.S("status", "stored");
            reply.Line(rec);
            continue;
        }
        Order order;
        order.verb = "spec";
        order.arg = std::to_string(tab);
        Player* commander = CommanderOf(bot);
        order.owner = commander ? commander->GetGUID() : bot->GetGUID();
        std::string code, detail;
        uint32 seq = 0;
        if (!Enqueue(bot, std::move(order), code, detail, seq))
        {
            rec.S("status", "refused").S("code", code).T("msg", detail);
            reply.Line(rec);
            continue;
        }
        ++queued;
        rec.N("seq", seq).S("status", "queued");
        reply.Line(rec);
    }
    Rec end("spec");
    end.P("target", LabelParts(who.label)).N("bots", who.bots.size()).N("queued", queued);
    reply.End(end);
    return true;
}
}
}

#else   // not built with the slice: every command says so

namespace TortoiseBots
{
namespace h3
{
bool HandleOrder(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "order");
}

bool HandleRole(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "role");
}

bool HandleSpec(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "spec");
}

bool HandleMaster(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    return RequireBuilt(handler, args, "master");
}
}
}

#endif
