#include "host/TbOnyxiaGround.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "compat/botpch_forever.h"
#include "host/TbH3.h"
#include "host/TbOnyxiaGroundPolicy.h"
#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Pet.h"
#include "ThreatManager.h"
#include "PlayerbotAIStorage.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Strategy.h"
#include "playerbot/strategy/actions/AttackAction.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/actions/EnemyAreaSpell.h"
#include "playerbot/policy/combat/AoeOffPolicy.h"
#include "playerbot/ServerFacade.h"
#include <algorithm>
#include <chrono>
#include <map>

namespace TortoiseBots::onyxia
{
namespace
{
uint32 Now() { return uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
Point Pos(WorldObject const* object) { return {object->GetPositionX(),object->GetPositionY(),object->GetPositionZ()}; }
struct GroundState
{
    ObjectGuid boss, owner, group;
    uint32 instance=0, nextMove=0, spline=0;
    DamageGate gate;
    ObjectGuid pet;
    ReactStates petReact=REACT_DEFENSIVE;
    bool petHeld=false;
    std::vector<Point> route, eggs;
    uint32 checkedAt=0;
};
std::map<ObjectGuid,GroundState> states;
GroundState* State(Player* bot)
{
    auto it=states.find(bot->GetGUID());
    return it==states.end()?nullptr:&it->second;
}
Creature* Boss(Player* bot, GroundState const& state)
{
    if (bot->GetMapId()!=249 || bot->GetInstanceId()!=state.instance || !bot->GetGroup() || bot->GetGroup()->GetGUID()!=state.group) return nullptr;
    Creature* boss=ObjectAccessor::GetCreature(*bot,state.boss);
    return boss && boss->GetMap()==bot->GetMap() && boss->GetEntry()==10184?boss:nullptr;
}
Player* Owner(Player* bot, GroundState const& state)
{
    Player* owner=ObjectAccessor::FindPlayer(state.owner);
    return owner && owner->GetMap()==bot->GetMap() && owner->GetGroup()==bot->GetGroup() &&
        h3::IsHostBot(owner) && h3::WantedRole(owner->GetGUID())==1?owner:nullptr;
}
bool Active(Creature* boss)
{
    return boss && boss->IsAlive() && boss->IsInCombat() && !boss->HasAura(17131) && !boss->IsFlying();
}
void StopOwned(Player* bot,GroundState& state)
{
    if (state.spline && bot->movespline->GetId()==state.spline && !bot->movespline->Finalized()) bot->StopMoving();
    state.spline=0;
    state.route.clear();
}
void RestorePet(Player* bot,GroundState& state)
{
    if (Pet* pet=bot->GetPet())
        if (state.petHeld && pet->GetGUID()==state.pet && pet->GetReactState()==REACT_PASSIVE)
            pet->SetReactState(state.petReact);
    state.petHeld=false;
}
bool Held(Player* bot, GroundState const& state)
{
    return bot->GetGUID()!=state.owner && (state.gate.held || h3::WantedRole(bot->GetGUID())==1);
}
bool Settled(PlayerbotAI* ai, Creature* boss, GroundState const& state)
{
    Player* bot=ai->GetBot();
    if (bot->GetGUID()==state.owner)
        return Distance(Pos(bot),{22.8763f,-217.152f,-85.054054f})<=2 && bot->IsWithinMeleeRange(boss);
    if (!Flank(Pos(bot),Pos(boss),boss->GetOrientation()) || !bot->IsWithinLOSInMap(boss)) return false;
    if (ai->IsHeal(bot))
    {
        Player* owner=Owner(bot,state);
        return owner && bot->IsWithinDistInMap(owner,35) && bot->IsWithinLOSInMap(owner);
    }
    if (!ai->IsRanged(bot)) return bot->IsWithinMeleeRange(boss);
    return bot->GetDistance(boss)<=30 && (bot->GetClass()!=CLASS_HUNTER || bot->GetDistance(boss)>=8);
}

class GroundAction : public ai::AttackAction
{
public:
    explicit GroundAction(PlayerbotAI* ai):AttackAction(ai,"onyxia ground") { }
    bool Safe(std::vector<Point> const& points, Creature* boss, GroundState const& state)
    {
        if (!RouteSafe(points,state.eggs,Pos(boss),boss->GetOrientation(),bot->GetGUID()==state.owner)) return false;
        for (size_t i=1;i<points.size();++i)
        {
            Point a=points[i-1], b=points[i];
            unsigned steps=unsigned(std::ceil(Distance(a,b)))+1;
            for (unsigned j=0;j<=steps;++j)
            {
                float t=float(j)/steps;
                Point p{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
                if (!IsMovingAllowed(249,p.x,p.y,p.z) || IsHazardNearPosition(WorldPosition(249,p.x,p.y,p.z))) return false;
            }
        }
        return true;
    }
    bool isUseful() override
    {
        GroundState* state=State(bot);
        Creature* boss=state?Boss(bot,*state):nullptr;
        if (!Active(boss) || !bot->IsAlive() || !Owner(bot,*state) || !Owner(bot,*state)->IsAlive()) return false;
        if (state->spline && !bot->movespline->Finalized() && bot->movespline->GetId()==state->spline) return false;
        if (!Held(bot,*state) && !ai->IsHeal(bot) && bot->GetVictim()!=boss) return true;
        return int32(Now()-state->nextMove)>=0 && !Settled(ai,boss,*state);
    }
    bool Execute(ai::Event&) override
    {
        GroundState* state=State(bot);
        Creature* boss=state?Boss(bot,*state):nullptr;
        if (!Active(boss) || !bot->IsAlive()) return false;
        Player* owner=Owner(bot,*state);
        if (!owner || !owner->IsAlive()) return false;
        bool const tank=bot->GetGUID()==state->owner;
        if (!Held(bot,*state) && !ai->IsHeal(bot) && bot->GetVictim()!=boss)
            return Attack(ai->GetMaster()?ai->GetMaster():bot,boss,100,true);
        if (Settled(ai,boss,*state) || !ai->CanMove() || bot->IsNonMeleeSpellCast(false) || int32(Now()-state->nextMove)<0) return false;
        state->nextMove=Now()+2000;
        std::vector<Point> goals;
        Point const origin=Pos(boss);
        if (tank)
        {
            if (boss->GetVictim()!=bot && !bot->IsWithinMeleeRange(boss))
                goals.push_back({std::clamp(origin.x,-110.f,38.f),std::clamp(origin.y,-232.f,-198.f),origin.z});
            else goals.push_back({22.8763f,-217.152f,-85.054054f});
        }
        else
        {
            float const radius=bot->GetClass()==CLASS_HUNTER?36.f:ai->IsRanged(bot)||ai->IsHeal(bot)?26.f:20.f;
            for (float sign:{-1.f,1.f})
                for (float angle:{1.5707963f,1.35f,1.8f})
                    goals.push_back({origin.x+radius*std::cos(boss->GetOrientation()+sign*angle),origin.y+radius*std::sin(boss->GetOrientation()+sign*angle),origin.z});
            std::sort(goals.begin(),goals.end(),[&](Point a,Point b){return Distance(Pos(bot),a)<Distance(Pos(bot),b);});
        }
        std::list<GameObject*> objects;
        bot->GetGameObjectListWithEntryInGrid(objects,176511,160);
        std::vector<Point> eggs;
        for (GameObject* egg:objects) eggs.push_back(Pos(egg));
        state->eggs=eggs;
        for (Point goal:goals)
        {
            goal.z=bot->GetMap()->GetHeight(bot->GetPhaseShift(),goal.x,goal.y,origin.z+5);
            if (Distance(Pos(bot),goal)<1 || !Floor(goal) || (ai->IsHeal(bot) && (Distance(goal,Pos(owner))>35 || !owner->IsWithinLOS(goal.x,goal.y,goal.z+2)))) continue;
            if (!boss->IsWithinLOS(goal.x,goal.y,goal.z+2)) continue;
            PathGenerator path(bot);
            if (!path.CalculatePath(goal.x,goal.y,goal.z) || path.GetPathType()!=PATHFIND_NORMAL) continue;
            std::vector<Point> points;
            for (auto const& p:path.GetPath()) points.push_back({p.x,p.y,p.z});
            if (points.empty() || Distance(points.back(),goal)>1.5f || !Safe(points,boss,*state)) continue;
            Movement::MoveSplineInit movement(bot);
            movement.MovebyPath(path.GetPath());
            movement.SetWalk(false);
            movement.Launch();
            state->spline=bot->movespline->GetId();
            state->route=points;
            state->checkedAt=Now();
            SetDuration(100);
            return true;
        }
        sLog.outString("Onyxia ground blocked bot=%s inst=%u reason=no-checked-route",bot->GetName().c_str(),state->instance);
        return false;
    }
};

class GroundTrigger : public ai::Trigger
{
public:
    explicit GroundTrigger(PlayerbotAI* ai):Trigger(ai,"onyxia ground",0) { }
    bool IsActive() override { GroundAction action(ai); return action.isUseful(); }
};
class GroundMultiplier : public ai::Multiplier
{
public:
    explicit GroundMultiplier(PlayerbotAI* ai):Multiplier(ai,"onyxia ground") { }
    float GetValue(ai::Action* action) override
    {
        Player* bot=ai->GetBot();
        GroundState* state=State(bot);
        Creature* boss=state?Boss(bot,*state):nullptr;
        if (!action || !boss || !boss->IsAlive() || boss->HasAura(17131) || boss->IsFlying()) return 1;
        std::string const name=action->getName();
        if (name=="onyxia ground" || name=="move away from hazard" || name=="raid bomb runout" || name=="flee" || name=="set facing") return 1;
        // Healing carries the legacy AOE threat mark too; never classify that
        // mark as enemy damage without first exempting healing actions.
        if (dynamic_cast<ai::CastHealingSpellAction*>(action)) return 1;
        bool area=ai::IsAreaSpellAction(name);
        if (auto* cast=dynamic_cast<ai::CastSpellAction*>(action))
        {
            uint32 id=ai->GetAiObjectContext()->GetValue<uint32>("spell id",cast->GetSpellName())->Get();
            area=area || ai::IsEnemyAreaSpell(sServerFacade.LookupSpellInfo(id));
        }
        if (area) return 0; // This increment owns single-target ground combat only.
        Unit* target=action->GetTarget();
        bool const damage=action->getThreatType()!=ai::ActionThreatType::ACTION_THREAT_NONE || dynamic_cast<ai::AttackAction*>(action);
        if (Held(bot,*state) && target==boss && damage) return 0;
        if (Active(boss) && dynamic_cast<ai::MovementAction*>(action))
        {
            if (dynamic_cast<ai::AttackAction*>(action) && target==boss && bot->IsWithinMeleeRange(boss) && !Held(bot,*state)) return 1;
            return 0;
        }
        return 1;
    }
};
class GroundStrategy : public ai::Strategy
{
public:
    explicit GroundStrategy(PlayerbotAI* ai):Strategy(ai) { }
    std::string getName() override { return "onyxia ground"; }
    void InitCombatTriggers(std::list<ai::TriggerNode*>& triggers) override
    {
        triggers.push_back(new ai::TriggerNode("onyxia ground",ai::NextAction::array(0,new ai::NextAction("onyxia ground",ai::ACTION_HIGH+2),nullptr)));
    }
    void InitCombatMultipliers(std::list<ai::Multiplier*>& multipliers) override { multipliers.push_back(new GroundMultiplier(ai)); }
    void InitNonCombatMultipliers(std::list<ai::Multiplier*>& multipliers) override { multipliers.push_back(new GroundMultiplier(ai)); }
};
template<class Base,class Impl> class Context : public ai::NamedObjectContext<Base>
{
public:
    Context() { this->creators["onyxia ground"]=[](PlayerbotAI* ai)->Base* { return new Impl(ai); }; }
};
}
void InstallGround(PlayerbotAI* ai)
{
    ai->GetAiObjectContext()->AddShared(new Context<ai::Strategy,GroundStrategy>());
    ai->GetAiObjectContext()->AddShared(new Context<ai::Action,GroundAction>());
    ai->GetAiObjectContext()->AddShared(new Context<ai::Trigger,GroundTrigger>());
}
ObjectGuid GroundOwner(ObjectGuid guid) { auto i=states.find(guid);return i==states.end()?ObjectGuid::Empty:i->second.owner; }
ObjectGuid GroundBoss(ObjectGuid guid) { auto i=states.find(guid);return i==states.end()?ObjectGuid::Empty:i->second.boss; }
void ForgetGround(ObjectGuid guid)
{
    auto i=states.find(guid);
    if (i==states.end()) return;
    if (Player* bot=ObjectAccessor::FindPlayer(guid)) { StopOwned(bot,i->second);RestorePet(bot,i->second); }
    states.erase(i);
    // Disabling the named owner also releases its followers. No follower may
    // silently keep driving an encounter after the tank accepted a manual order.
    for (ObjectGuid follower:Followers(states,guid)) ForgetGround(follower);
}
std::string ConfigureGround(Player* bot,ObjectGuid boss,Player* tank)
{
    PlayerbotAI* ai=PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai) return "no-engine";
    if (!tank || !h3::IsHostBot(tank) || !tank->IsAlive() || !bot->GetGroup() || tank->GetGroup()!=bot->GetGroup() || tank->GetMap()!=bot->GetMap() || h3::WantedRole(tank->GetGUID())!=1) return "invalid-tank";
    GroundState state{boss,tank->GetGUID(),bot->GetGroup()->GetGUID(),bot->GetInstanceId()};
    if (!Boss(bot,state)) return "wrong-boss";
    for (auto const& pair:states)
        if (pair.second.boss==boss && pair.second.group==state.group && pair.second.instance==state.instance && pair.second.owner!=state.owner)
            return "conflicting-owner";
    if (bot!=tank)
    {
        auto ownerState=states.find(tank->GetGUID());
        if (ownerState==states.end() || ownerState->second.boss!=boss || ownerState->second.owner!=tank->GetGUID())
            return "configure-owner-first";
    }
    ForgetGround(bot->GetGUID());
    states[bot->GetGUID()]=state;
    ai->ChangeStrategy("+onyxia ground",BotState::BOT_STATE_COMBAT);
    ai->ChangeStrategy("+onyxia ground",BotState::BOT_STATE_NON_COMBAT);
    return {};
}
void GroundTick(Player* bot,PlayerbotAI* ai)
{
    GroundState* state=State(bot);
    if (!state) return;
    if (!ai->HasStrategy("onyxia ground",BotState::BOT_STATE_COMBAT)) { ForgetGround(bot->GetGUID());return; }
    Creature* boss=Boss(bot,*state);
    Player* owner=Owner(bot,*state);
    if (!boss || !boss->IsAlive() || !owner) { ForgetGround(bot->GetGUID());return; }
    if (!owner->IsAlive())
    {
        for (auto const& pair:states)
        {
            if (pair.second.boss!=state->boss || pair.second.group!=state->group || pair.second.instance!=state->instance) continue;
            Player* next=ObjectAccessor::FindPlayer(pair.first);
            if (next && next->IsAlive() && next->GetMap()==bot->GetMap() && next->GetGroup()==bot->GetGroup() && h3::WantedRole(pair.first)==1)
            {
                ReassignOwner(states,*state,pair.first);
                owner=next;
                break;
            }
        }
    }
    if (!owner->IsAlive()) StopOwned(bot,*state);
    if (!bot->IsAlive()) { StopOwned(bot,*state);RestorePet(bot,*state);return; }
    if (!Active(boss)) { StopOwned(bot,*state);RestorePet(bot,*state);state->gate.held=true;return; }
    if (state->spline && bot->movespline->GetId()==state->spline && !bot->movespline->Finalized() && Now()-state->checkedAt>=250)
    {
        state->checkedAt=Now();
        std::vector<Point> remaining{Pos(bot)};
        size_t next=size_t(std::max(0,bot->movespline->currentPathIdx()))+1;
        for (;next<state->route.size();++next) remaining.push_back(state->route[next]);
        GroundAction checker(ai);
        if (!ai->CanMove() || !checker.Safe(remaining,boss,*state)) StopOwned(bot,*state);
    }
    float other=0;
    for (ThreatReference const* threat:boss->GetThreatManager().GetSortedThreatList())
        if (threat && threat->GetVictim()!=owner) other=std::max(other,threat->GetThreat());
    state->gate.Update(owner->IsAlive(),boss->GetVictim()==owner,owner->GetHealthPct(),boss->GetThreatManager().GetThreat(owner)-other);
    if (Held(bot,*state))
    {
        if (bot->GetVictim()==boss) bot->AttackStop();
        if (Pet* pet=bot->GetPet())
        {
            if (state->petHeld && state->pet==pet->GetGUID() && pet->GetReactState()!=REACT_PASSIVE)
            { ForgetGround(bot->GetGUID());return; }
            if (!state->petHeld || state->pet!=pet->GetGUID())
            { state->pet=pet->GetGUID();state->petReact=pet->GetReactState();state->petHeld=true; }
            pet->SetReactState(REACT_PASSIVE);
            if (pet->GetVictim()==boss) pet->AttackStop();
        }
    }
    else RestorePet(bot,*state);
}
}
#else
namespace TortoiseBots::onyxia
{
void InstallGround(PlayerbotAI*) { }
std::string ConfigureGround(Player*,ObjectGuid,Player*) { return "engine-not-built"; }
void ForgetGround(ObjectGuid) { }
void GroundTick(Player*,PlayerbotAI*) { }
ObjectGuid GroundOwner(ObjectGuid) { return {}; }
ObjectGuid GroundBoss(ObjectGuid) { return {}; }
}
#endif
