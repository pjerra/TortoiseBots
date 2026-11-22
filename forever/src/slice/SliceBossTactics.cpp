#include "compat/botpch_forever.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossStandMgr.h"
#include "playerbot/BossTacticsFiveManPolicy.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/strategy/generic/BossTacticsStrategy.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/generic/DungeonStrategy.h"
#include "playerbot/strategy/triggers/BossTacticsTriggers.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "runtime/CommandedGroupQuery.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MoveSplineInit.h"
#include "PathGenerator.h"
#include "Spell.h"
#include "SpellInfo.h"
#include <set>

namespace ai
{
namespace
{
bool Wanted(Player* bot)
{
    return bot && bot->IsInWorld() && !bot->IsBeingTeleported() && FiveManMap(bot->GetMapId()) &&
        BossTacticsWanted(sPlayerbotAIConfig.dungeonTactics,TortoiseBots::BotCommands::IsInCommandedGroup(bot),
            GetBossTactics().HasMap(bot->GetMapId()) || GetBossStands().HasMap(bot->GetMapId()));
}
bool Ready(PlayerbotAI* ai,Player* bot)
{
    return ai && Wanted(bot) && bot->IsAlive() && ai->HasStrategy("boss tactics",BotState::BOT_STATE_COMBAT);
}
bool Engaged(Player* bot,Unit* unit)
{
    if (!unit || !unit->IsAlive() || unit->GetMap()!=bot->GetMap() || !unit->IsInCombat() ||
        !bot->IsValidAttackTarget(unit) || bot->GetDistance(unit)>80) return false;
    Creature* creature=unit->ToCreature();
    if (!creature || creature->IsInEvadeMode()) return false;
    if (unit->IsInCombatWith(bot)) return true;
    Unit* victim=unit->GetVictim();
    Player* player=victim?victim->GetCharmerOrOwnerPlayerOrPlayerItself():nullptr;
    return player && bot->GetGroup() && player->GetGroup()==bot->GetGroup();
}
uint32 CastId(Unit* unit,CurrentSpellTypes slot)
{
    Spell* spell=unit->GetCurrentSpell(slot);
    return spell?spell->GetSpellInfo()->Id:0;
}
struct ScanCache
{
    unsigned depth=0;
    PlayerbotAI* ai=nullptr;
    BossTacticTable const* table=nullptr;
    BossTacticsFight fight;
};
thread_local ScanCache cache;
void Note(Player* bot,char const* rule,Unit* target)
{
    sLog.outString("FiveMan tactic bot=%s map=%u rule=%s target=%u",bot->GetName().c_str(),bot->GetMapId(),rule,target?target->GetEntry():0);
}
bool Ordered(PlayerbotAI* ai)
{
    return !ai->GetAiObjectContext()->GetValue<ObjectGuid>("explicit attack target")->Get().IsEmpty();
}
class CheckedMove : public MovementAction
{
public:
    explicit CheckedMove(PlayerbotAI* ai):MovementAction(ai,"five-man checked move") { }
    bool Run(float x,float y,float z,Unit* facing=nullptr)
    {
        if (!ai->CanMove() || bot->IsNonMeleeSpellCast(false) || !bot->movespline->Finalized()) return false;
        if (ai->HasStrategy("stay",BotState::BOT_STATE_COMBAT) && !ai->HasPullWindow()) return false;
        if (ai->HasStrategy("passive",BotState::BOT_STATE_COMBAT)) return false;
        float height=bot->GetMap()->GetHeight(bot->GetPhaseShift(),x,y,z+3);
        if (!std::isfinite(height) || std::abs(height-z)>4 || !IsMovingAllowed(bot->GetMapId(),x,y,height)) return false;
        PathGenerator path(bot);
        if (!path.CalculatePath(x,y,height) || path.GetPathType()!=PATHFIND_NORMAL) return false;
        auto const& points=path.GetPath();
        if (points.size()<2 || std::hypot(points.back().x-x,points.back().y-y)>1.5f) return false;
        float total=0;
        for (size_t i=1;i<points.size();++i)
        {
            auto const& a=points[i-1];auto const& b=points[i];
            float length=(b-a).length();
            if (!std::isfinite(length) || (total+=length)>60) return false;
            unsigned count=unsigned(std::ceil(length/0.5f))+1;
            for (unsigned j=0;j<=count;++j)
            {
                auto p=a+(b-a)*(float(j)/count);
                if (!IsMovingAllowed(bot->GetMapId(),p.x,p.y,p.z) ||
                    IsHazardNearPosition(WorldPosition(bot->GetMapId(),p.x,p.y,p.z))) return false;
            }
        }
        Movement::MoveSplineInit move(bot);
        move.MovebyPath(points);move.SetWalk(false);
        if (facing) { bot->SetFacingToObject(facing);move.SetOrientationFixed(true); }
        move.Launch();return true;
    }
};
}
BossTacticsScanScope::BossTacticsScanScope() { if (!cache.depth++) { cache.ai=nullptr;cache.fight={}; } }
BossTacticsScanScope::~BossTacticsScanScope() { if (cache.depth && !--cache.depth) { cache.ai=nullptr;cache.fight={}; } }
BossTacticsFight ScanBossTactics(PlayerbotAI* ai,Player* bot)
{
    BossTacticsFight fight;
    if (!Ready(ai,bot)) return fight;
    auto const& table=GetBossTactics();
    if (cache.depth && cache.ai==ai && cache.table==&table) return cache.fight;
    std::set<ObjectGuid> seen;
    for (char const* key:{"attackers","possible targets no los"})
        for (ObjectGuid guid:ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>(key)->Get())
        {
            Unit* unit=ai->GetUnit(guid);
            if (seen.insert(guid).second && Engaged(bot,unit)) fight.units.push_back(unit);
        }
    std::set<uint32> entries;
    for (Unit* unit:fight.units)
        if (entries.insert(unit->GetEntry()).second)
            if (auto* rows=table.Rows(bot->GetMapId(),unit->GetEntry()))
                for (auto const& row:*rows)
                    if (FiveManKind(row.kind) && row.mode==TacticMode::Default && RowGateOpen(row,false,int(unit->GetHealthPct()),row.botAura && bot->HasAura(row.botAura)))
                        fight.rows.push_back(&row);
    if (cache.depth) { cache.ai=ai;cache.table=&table;cache.fight=fight; }
    return fight;
}
BossInterruptScan FindBossInterrupt(PlayerbotAI* ai,Player*,BossTacticsFight const& fight)
{
    for (Unit* unit:fight.units)
        if (IsInterruptListed(fight.rows,unit->GetEntry(),CastId(unit,CURRENT_GENERIC_SPELL)) ||
            IsInterruptListed(fight.rows,unit->GetEntry(),CastId(unit,CURRENT_CHANNELED_SPELL)))
            for (char const* spell:{"shield bash","pummel","kick","counterspell","earth shock","silence","spell lock"})
                if (ai->IsInterruptableSpellCasting(unit,std::string(spell)) && ai->CanCastSpell(std::string(spell),unit)) return {unit,spell};
    return {};
}
Unit* FindBossKillFirst(PlayerbotAI* ai,Player* bot,BossTacticsFight const& fight)
{
    if (PlayerbotAI::IsTank(bot) || PlayerbotAI::IsHeal(bot) || Ordered(ai)) return nullptr;
    Unit* best=nullptr;unsigned priority=256;
    for (auto* row:fight.rows)
        if (row->kind==BossTacticKind::KillFirst)
            for (Unit* unit:fight.units)
                if (unit->GetEntry()==row->targetEntry && !unit->HasBreakableByDamageCrowdControlAura() &&
                    (row->priority<priority || (row->priority==priority && (!best || unit->GetGUID()<best->GetGUID()))))
                { best=unit;priority=row->priority; }
    return best;
}
Unit* FindBossFrontDanger(PlayerbotAI*,Player* bot,BossTacticsFight const& fight)
{
    if (PlayerbotAI::IsTank(bot)) return nullptr;
    for (auto* row:fight.rows)
        if (row->kind==BossTacticKind::Behind)
            for (Unit* unit:fight.units)
                if (unit->GetEntry()==TacticSubjectEntry(*row) && unit->GetVictim() && unit->GetVictim()!=bot &&
                    InsideBossFront(unit->GetPositionX(),unit->GetPositionY(),unit->GetOrientation(),bot->GetPositionX(),bot->GetPositionY(),row->param)) return unit;
    return nullptr;
}
float CastDangerOf(BossTacticsFight const& fight,Unit* unit)
{
    return unit?ListedCastRadius(fight.rows,unit->GetEntry(),CastId(unit,CURRENT_GENERIC_SPELL),CastId(unit,CURRENT_CHANNELED_SPELL)):0;
}
BossCastDanger FindBossCastDanger(PlayerbotAI*,Player* bot,BossTacticsFight const& fight)
{
    if (PlayerbotAI::IsTank(bot)) return {};
    for (Unit* unit:fight.units)
    {
        float radius=CastDangerOf(fight,unit);
        if (radius>0 && bot->GetExactDist(unit)<radius+2) return {unit,radius};
    }
    return {};
}
BossStandTarget FindBossStand(PlayerbotAI* ai,Player* bot)
{
    if (!Ready(ai,bot)) return {};
    bool tank=PlayerbotAI::IsTank(bot);
    StandRole role=tank?StandRole::Tank:StandRoleOf(false,PlayerbotAI::IsHeal(bot),ai->IsRanged(bot));
    for (Unit* boss:ScanBossTactics(ai,bot).units)
    {
        if (tank && (boss->GetVictim()!=bot || !bot->IsWithinMeleeRange(boss))) continue;
        auto* rows=GetBossStands().Rows(bot->GetMapId(),boss->GetEntry());
        if (!rows) continue;
        for (auto const& row:*rows)
            if (row.role==role && row.slots==1 && row.holdMs &&
                StandRowActive(row,boss->GetPositionX(),boss->GetPositionY(),row.aura && boss->HasAura(row.aura),CastId(boss,CURRENT_GENERIC_SPELL)))
                return {true,false,row.x,row.y,row.z};
    }
    return {};
}
bool BossTacticsInterruptAction::Execute(Event&)
{
    auto pick=FindBossInterrupt(ai,bot,ScanBossTactics(ai,bot));
    if (!pick.caster || !ai->CanCastSpell(std::string(pick.spell),pick.caster) || !ai->CastSpell(std::string(pick.spell),pick.caster)) return false;
    Note(bot,"interrupt",pick.caster);return true;
}
bool BossTacticsKillFirstAction::Execute(Event&)
{
    Unit* pick=FindBossKillFirst(ai,bot,ScanBossTactics(ai,bot));
    if (!pick || !Engaged(bot,pick) || !Attack(ai->GetMaster()?ai->GetMaster():bot,pick,100,true)) return false;
    Note(bot,"kill-first",pick);return true;
}
bool BossTacticsStepBehindAction::isPossible() { return Ready(ai,bot) && ai->CanMove(); }
bool BossTacticsStepBehindAction::Execute(Event&)
{
    Unit* boss=FindBossFrontDanger(ai,bot,ScanBossTactics(ai,bot));
    if (!boss) return false;
    float radius=std::max(3.f,bot->GetExactDist2d(boss));
    for (float side:{1.f,-1.f})
    {
        float angle=boss->GetOrientation()+side*1.9f;
        CheckedMove move(ai);
        if (move.Run(boss->GetPositionX()+radius*std::cos(angle),boss->GetPositionY()+radius*std::sin(angle),bot->GetPositionZ()))
        { Note(bot,"behind",boss);return true; }
    }
    return false;
}
bool BossTacticsAvoidCastAction::isUseful() { return FindBossCastDanger(ai,bot,ScanBossTactics(ai,bot)).caster!=nullptr; }
bool BossTacticsAvoidCastAction::isPossible() { return Ready(ai,bot) && ai->CanMove(); }
bool BossTacticsAvoidCastAction::Execute(Event&)
{
    auto danger=FindBossCastDanger(ai,bot,ScanBossTactics(ai,bot));
    if (!danger.caster) return false;
    float angle=std::atan2(bot->GetPositionY()-danger.caster->GetPositionY(),bot->GetPositionX()-danger.caster->GetPositionX());
    CheckedMove move(ai);
    if (!move.Run(danger.caster->GetPositionX()+(danger.radius+3)*std::cos(angle),danger.caster->GetPositionY()+(danger.radius+3)*std::sin(angle),bot->GetPositionZ())) return false;
    Note(bot,"avoid-cast",danger.caster);return true;
}
bool BossTacticsStandAction::isPossible() { return Ready(ai,bot) && ai->CanMove(); }
bool BossTacticsStandAction::Execute(Event&)
{
    auto seat=FindBossStand(ai,bot);
    if (!seat.valid || bot->GetExactDist(seat.x,seat.y,seat.z)<1.5f) return false;
    Unit* boss=nullptr;
    if (PlayerbotAI::IsTank(bot))
    {
        for (Unit* unit:ScanBossTactics(ai,bot).units)
            if (unit->GetVictim()==bot && GetBossStands().Rows(bot->GetMapId(),unit->GetEntry())) { boss=unit;break; }
        if (!boss || !bot->IsWithinMeleeRange(boss)) return false;
        float length=std::hypot(seat.x-bot->GetPositionX(),seat.y-bot->GetPositionY());
        if (length>3) { seat.x=bot->GetPositionX()+(seat.x-bot->GetPositionX())*3/length;seat.y=bot->GetPositionY()+(seat.y-bot->GetPositionY())*3/length;seat.z=bot->GetPositionZ(); }
    }
    CheckedMove move(ai);
    if (!move.Run(seat.x,seat.y,seat.z,boss)) return false;
    Note(bot,"stand",boss);return true;
}
bool BossTacticsEnterTrigger::IsActive() { return Wanted(bot) && !ai->HasStrategy("boss tactics",BotState::BOT_STATE_COMBAT); }
bool BossTacticsLeaveTrigger::IsActive() { return bot->IsInWorld() && !bot->IsBeingTeleported() && !Wanted(bot); }
bool BossTacticsInterruptTrigger::IsActive() { return FindBossInterrupt(ai,bot,ScanBossTactics(ai,bot)).caster!=nullptr; }
bool BossTacticsKillFirstTrigger::IsActive()
{
    Unit* pick=FindBossKillFirst(ai,bot,ScanBossTactics(ai,bot));
    return pick && (bot->GetVictim()!=pick || ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get()!=pick);
}
bool BossTacticsFrontTrigger::IsActive() { return FindBossFrontDanger(ai,bot,ScanBossTactics(ai,bot))!=nullptr; }
bool BossTacticsAvoidCastTrigger::IsActive() { return FindBossCastDanger(ai,bot,ScanBossTactics(ai,bot)).caster!=nullptr; }
bool BossTacticsStandTrigger::IsActive()
{
    auto seat=FindBossStand(ai,bot);
    return seat.valid && bot->GetExactDist(seat.x,seat.y,seat.z)>=1.5f && bot->movespline->Finalized();
}
void BossTacticsStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    for (auto const& row:std::vector<std::pair<std::string,float>>{{"interrupt",ACTION_INTERRUPT+1},{"kill first",ACTION_HIGH+1},{"avoid cast",ACTION_EMERGENCY+6},{"stand",ACTION_EMERGENCY}})
        triggers.push_back(new TriggerNode("boss tactics "+row.first,NextAction::array(0,new NextAction("boss tactics "+row.first,row.second),nullptr)));
    triggers.push_back(new TriggerNode("boss tactics front",NextAction::array(0,new NextAction("boss tactics step behind",ACTION_EMERGENCY+1),nullptr)));
    triggers.push_back(new TriggerNode("leave boss tactics",NextAction::array(0,new NextAction("disable boss tactics strategy",100),nullptr)));
}
void BossTacticsStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("leave boss tactics",NextAction::array(0,new NextAction("disable boss tactics strategy",100),nullptr)));
}
void BossTacticsStrategy::InitReactionTriggers(std::list<TriggerNode*>&) { }
namespace
{
class FiveManTargetMultiplier : public Multiplier
{
public:
    explicit FiveManTargetMultiplier(PlayerbotAI* ai):Multiplier(ai,"five-man targets") { }
    float GetValue(Action* action) override
    {
        if (!action || dynamic_cast<CastHealingSpellAction*>(action)) return 1;
        Player* bot=ai->GetBot();
        Unit* pick=FindBossKillFirst(ai,bot,ScanBossTactics(ai,bot));
        if (!pick) return 1;
        std::string name=action->getName();
        if (name=="boss tactics kill first" || name=="boss tactics interrupt" || FiveManSafetyAction(name)) return 1;
        if (dynamic_cast<AttackAction*>(action) && name!="melee") return 0;
        Unit* target=action->GetTarget();
        if (target && target!=pick && Engaged(bot,target) && action->getThreatType()!=ActionThreatType::ACTION_THREAT_NONE) return 0;
        return 1;
    }
};
}
void BossTacticsStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers) { multipliers.push_back(new FiveManTargetMultiplier(ai)); }
void BossTacticsStrategy::InitNonCombatMultipliers(std::list<Multiplier*>&) { }
void BossTacticsStrategy::InitReactionMultipliers(std::list<Multiplier*>&) { }
void DungeonStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("enter boss tactics",NextAction::array(0,new NextAction("enable boss tactics strategy",100),nullptr)));
}
void DungeonStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers) { InitCombatTriggers(triggers); }
void DungeonStrategy::InitCombatMultipliers(std::list<Multiplier*>&) { }
}
