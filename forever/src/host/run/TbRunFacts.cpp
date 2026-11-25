// Slice 6, package F6 (facts): see TbRunFacts.h and forever/docs/RUN-S6-F-NOTES.md. Built with TORTOISEBOTS_SLICE1 only.
//
// Every fact is read on the world thread from the core's public API (the file:line of each group is in the notes, section 3). The decisions about
// the numbers are in TbRunFactsPure.h (run by forever/tools/test_run_facts.cpp); this file only asks the world and fills the plain structs.
#include "host/run/TbRunFacts.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "host/run/TbPlanUnits.h"
#include "host/run/TbRun.h"
#include "host/run/TbRunFactsPure.h"
#include "host/run/TbRunLiquid.h"
#include "slice/SliceHybridPolicy.h"
#include "host/run/TbRunTasks.h"

#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <unordered_set>

#include "Corpse.h"
#include "CreatureData.h"
#include "CreatureGroups.h"
#include "DB2Stores.h"
#include "GameTime.h"
#include "Group.h"
#include "Loot.h"
#include "Map.h"
#include "MapManager.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Player.h"
#include "PathGenerator.h"
#include "Unit.h"

#include "TbBotMgr.h"
#include "compat/TbCompat_Unit.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/MoveFailNotePolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/WorldPosition.h"
#include "runtime/RezHoldPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "PlayerbotAIStorage.h"

namespace TortoiseBots
{
namespace run
{
namespace
{
using DungeonRun::Facts;
using DungeonRun::MemberFact;
using DungeonRun::UnitFact;
namespace fx = facts;

// ---- per-run reader state: guid handles and the move probe cache. Never a pointer to a world object. ----
struct RunState
{
    fx::HandleTable<ObjectGuid> handles;
    DungeonRun::MoveProbeCache moveProbe;
};

constexpr size_t kMaxStates = 64;

std::map<ObjectGuid, RunState>& States()
{
    static std::map<ObjectGuid, RunState> states;
    return states;
}

RunState& StateOf(ObjectGuid leader)
{
    auto& states = States();
    if (states.size() >= kMaxStates && !states.count(leader))
        states.erase(states.begin());       // a run that was never forgotten: the oldest key goes (its handles then resolve to "gone")
    return states[leader];
}

FactsTotals& TotalsRw()
{
    static FactsTotals totals;
    return totals;
}

// One read's context: the lead, the map, the clock and the counters. Lives on the stack of ReadFacts.
struct Ctx
{
    Player* lead = nullptr;
    Map* map = nullptr;
    RunRec* rec = nullptr;
    RunState* state = nullptr;
    Player* judge = nullptr;        // whose attack rules say "the group can attack it" (the lead, else a living member on its map)
    uint32 nowMs = 0;
    FactsReport* report = nullptr;
    FactsReport local;              // used when the caller passed none
    FactsReport& R() { return report ? *report : local; }
    std::unordered_set<ObjectGuid> partyGuids;      // the bots and their pets
    std::vector<Player*> bots;                      // living bots in the world on the lead's map (lead first)
    fx::LeadObs lo;                                 // the lead's own facts, written by ReadLead and ReadGuard, applied once

    uint64 H(ObjectGuid guid) { return state->handles.Handle(guid, nowMs); }
};

void NotKnownField(Ctx& c, char const* field)
{
    if (c.report)
        c.report->notKnown.push_back(field);
}

float DistanceTo(WorldObject const* object, float x, float y, float z)
{
    return ai::PlanDistance(object->GetPositionX(), object->GetPositionY(), object->GetPositionZ(), x, y, z);
}

bool IsStealthed(Unit const* unit)
{
    return unit->HasAuraType(SPELL_AURA_MOD_STEALTH) || unit->HasAuraType(SPELL_AURA_MOD_INVISIBILITY);
}

bool MemberInCombat(Player* bot)
{
    Pet* const pet = bot->GetPet();
    return bot->IsInCombat() || (pet && pet->IsAlive() && pet->IsInWorld() && pet->IsInCombat());
}

// A player the engine runs: a host bot with a brain (BotMgr), in the world and on a map.
bool InWorldPlayer(Player* player)
{
    return player && player->IsInWorld() && !player->IsBeingTeleported() && player->FindMap();   // GetMap() ASSERTs on a player with no map (Object.h:411)
}

fx::Config ReadConfig()
{
    fx::Config c;
    c.linkedDeath = sPlayerbotAIConfig.dungeonRunLinkedDeath;
    c.fightForward = sPlayerbotAIConfig.dungeonRunFightForward;
    c.lightRest = sPlayerbotAIConfig.dungeonRunLightRest;
    c.walkBackAfterWonFight = sPlayerbotAIConfig.dungeonRunWalkBackAfterWonFight;
    c.retreatAt2Dead = sPlayerbotAIConfig.dungeonRunRetreatAt2Dead;
    c.packClearMarginYd = sPlayerbotAIConfig.dungeonRunPackClearMarginYd;
    c.packClearRepulls = sPlayerbotAIConfig.dungeonRunPackClearRepulls;
    c.nearWipeStop = sPlayerbotAIConfig.dungeonRunNearWipeStop;
    c.nearWipeStopMinutes = sPlayerbotAIConfig.dungeonRunNearWipeStopMinutes;
    c.nearWipeStopParty = sPlayerbotAIConfig.dungeonRunNearWipeStopParty;
    return c;
}

// ---- crowd control (G8; Commander.cpp RunCcHeld / RunCcRemainingMs / RunCcImmune, Polymorph only) ----
// The run's crowd control on this host is the mage's Polymorph (D1: the plan's lists; the mage is the only class whose cc row is in a kit): the other families
// are never READ as held and no bot carries them (MemberFact::cc stays 0), so the policy never picks them (do less).
// A unit holds the run's crowd control when the core says it is polymorphed (transform aura of the polymorph spell family) or carries a rank of Polymorph.
bool RunCcHeld(Unit const* unit)
{
    if (unit->IsPolymorphed())
        return true;
    for (uint32 spell : DungeonRun::kPolymorphSpells)
        if (unit->HasAura(spell))
            return true;
    return false;
}

// The soonest-ending Polymorph aura's remaining time in ms (0: none, or a permanent one): RunCcBreakTick breaks the hold that ends first.
uint32 RunCcRemainingMs(Unit const* unit)
{
    uint32 remain = 0;
    for (uint32 spell : DungeonRun::kPolymorphSpells)
        if (Aura const* aura = unit->GetAura(spell))
        {
            int32 const ms = aura->GetDuration();
            if (ms > 0 && (!remain || uint32(ms) < remain))
                remain = uint32(ms);
        }
    return remain;
}

// CcBit of every run family the unit is immune to: the first rank of Polymorph's mechanic against the unit's live mechanic immunities (the creature's template
// immunities are in it: Unit::GetMechanicImmunityMask).
uint32 RunCcImmune(Unit const* unit)
{
    SpellInfo const* info = sSpellMgr.GetSpellInfo(DungeonRun::kPolymorphSpells[0], DIFFICULTY_NONE);
    if (info && (unit->GetMechanicImmunityMask() & info->GetAllEffectsMechanicMask()))
        return DungeonRun::CcBit(DungeonRun::CcFamily::Polymorph);
    return 0;
}

// What a Polymorph order needs from a mage: it knows a rank (kPolymorphSpells).
bool KnowsPolymorph(Player const* bot)
{
    for (uint32 spell : DungeonRun::kPolymorphSpells)
        if (bot->HasSpell(spell))
            return true;
    return false;
}

// ---- paths: one PathGenerator query on the lead (or another member), the result as plain data ----
// PathGenerator.h:78/86 CalculatePath, :102 GetPathType, :100 GetPathLength; the type bits NOPATH 8, INCOMPLETE 4, SHORTCUT 2, NOT_USING_PATH 0x10
// are the Turtle ones (TbCompat_Unit.h section 6).
fx::PathObs QueryPath(Ctx& c, Player* who, float x, float y, float z)
{
    fx::PathObs out;
    if (!who || !InWorldPlayer(who))
        return out;
    ++c.R().pathQueries;
    PathFinder path(who);
    PathCalculateCompat(path, x, y, z, false);
    uint32 const type = PathTypeCompat(path);
    out.valid = true;
    out.noPath = (type & PATHFIND_NOPATH) != 0;
    out.incomplete = (type & PATHFIND_INCOMPLETE) != 0;
    out.shortcut = (type & PATHFIND_SHORTCUT) != 0;
    out.notUsingPath = (type & PATHFIND_NOT_USING_PATH) != 0;
    G3D::Vector3 const end = PathActualEndCompat(path);
    out.endX = end.x;
    out.endY = end.y;
    out.endZ = end.z;
    Movement::PointsArray const& way = PathPointsCompat(path);
    for (size_t i = 0; i < way.size(); ++i)
    {
        out.points.push_back(DungeonRun::PathPoint{ way[i].x, way[i].y, way[i].z });
        if (i)
            out.lengthYd += ai::PlanDistance(way[i - 1].x, way[i - 1].y, way[i - 1].z, way[i].x, way[i].y, way[i].z);
    }
    return out;
}

// ---- creature -> plain observation ----
fx::CreatureObs ObserveCreature(Ctx& c, Creature* creature)
{
    fx::CreatureObs o;
    o.handle = c.H(creature->GetGUID());
    o.entry = creature->GetEntry();
    o.alive = creature->IsAlive();
    o.x = creature->GetPositionX();
    o.y = creature->GetPositionY();
    o.z = creature->GetPositionZ();
    o.distLead = c.lead->GetDistance(creature);
    if (CreatureData const* data = creature->GetCreatureData())      // the DB row's spawn point, not the (movable) home position
    {
        o.hasSpawn = true;
        o.spawnX = data->spawnPoint.GetPositionX();
        o.spawnY = data->spawnPoint.GetPositionY();
        o.spawnZ = data->spawnPoint.GetPositionZ();
    }
    o.ownWaypoint = creature->GetDefaultMovementType() == WAYPOINT_MOTION_TYPE;
    if (CreatureGroup* group = creature->GetFormation())             // Creature.h:416, CreatureGroups.h GetLeader
        if (Creature* leader = group->GetLeader())
            if (leader != creature)
                o.followsWaypointLeader = leader->IsAlive() && leader->GetDefaultMovementType() == WAYPOINT_MOTION_TYPE;
    o.airborne = creature->CanFly() || creature->IsFlying() || creature->IsHovering();
    o.hostile = !sServerFacade.IsFriendlyTo(creature, c.lead);
    o.stealthed = IsStealthed(creature);
    o.evading = creature->IsInEvadeMode();
    o.attackable = !c.judge || c.judge->IsValidAttackTarget(creature);
    o.selectable = !HasUnitFlagCompat(creature, UNIT_FLAG_UNINTERACTIBLE);
    o.caster = creature->GetMaxPower(POWER_MANA) > 0;
    o.normalRank = CreatureRankCompat(creature) == 0;
    o.level = creature->GetLevel();
    o.creatureType = creature->GetCreatureType();
    o.polymorphed = RunCcHeld(creature);
    o.ccRemainMs = o.polymorphed ? RunCcRemainingMs(creature) : 0;
    o.ccImmune = RunCcImmune(creature);
    o.healthPct = creature->GetHealthPct();
    // A units hold's wave unit: awake and fighting (the tank takes it), and whom it is on (the lead bot keeps what it holds, the healer's attacker
    // comes first). The same three reads the Turtle glue makes for its hold units.
    Unit* const victim = creature->GetVictim();
    o.engaged = o.hostile && creature->IsInCombat() && victim;
    o.victimLead = victim == c.lead;
    o.victimHealer = victim && victim->IsPlayer() && PlayerbotAI::IsHeal(static_cast<Player*>(victim));
    return o;
}

// ---- the party ----
// Slots of the lead's group as PartyEntry rows (the lead first). A slot that is not a host bot is a real player: it goes to Facts::players.
void ReadParty(Ctx& c, Facts& f, bool& playerInGroup)
{
    Player* const lead = c.lead;
    std::vector<fx::PartyEntry> party;
    std::vector<ObjectGuid> order;
    std::vector<std::string> names;
    order.push_back(lead->GetGUID());
    names.push_back(lead->GetName());
    if (Group* group = lead->GetGroup())
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (slot.guid != lead->GetGUID() && order.size() < fx::kMaxMembers)
            {
                order.push_back(slot.guid);
                names.push_back(slot.name);
            }
    ai::DungeonPlan const* plan = c.rec->run.plan;
    for (size_t i = 0; i < order.size(); ++i)
    {
        Player* bot = i == 0 ? lead : ObjectAccessor::FindPlayer(order[i]);
        ++c.R().members;
        bool const host = bot ? h3::IsHostBot(bot) : false;
        if (bot && !host && i != 0)
        {
            // A real player in the group (not a bot): Facts::players. The run waits for him; C6 pauses a run that has one (player-in-group).
            playerInGroup = true;
            DungeonRun::PlayerFact p;
            p.name = names[i];
            p.alive = bot->IsAlive();
            p.ghost = bot->HasPlayerFlag(PLAYER_FLAGS_GHOST);
            p.onMap = InWorldPlayer(bot) && plan && bot->GetMapId() == plan->map;
            p.online = bot->IsInWorld();
            p.inCombat = bot->IsInCombat();
            p.yd = InWorldPlayer(bot) && bot->GetMap() == lead->GetMap() ? bot->GetDistance(lead) : 1000.0f;
            p.healthPct = bot->GetMaxHealth() ? uint32(uint64(bot->GetHealth()) * 100 / bot->GetMaxHealth()) : 100;
            f.players.push_back(p);
            continue;
        }
        fx::PartyEntry e;
        e.name = names[i];
        e.isLead = i == 0;
        e.present = InWorldPlayer(bot);
        if (!e.present)
        {
            ++c.R().membersAbsent;
            party.push_back(e);
            continue;
        }
        e.alive = bot->IsAlive();
        if (!e.alive)
        {
            party.push_back(e);
            continue;
        }
        bool const sameMap = bot->GetMap() == lead->GetMap();
        e.inCombat = MemberInCombat(bot);
        e.healer = PlayerbotAI::IsHeal(bot);
        e.dps = !e.healer && !IsTankMember(bot);
        uint8 const cls = bot->GetClass();
        e.canResurrect = cls == CLASS_PRIEST || cls == CLASS_PALADIN || cls == CLASS_SHAMAN;
        e.healthPct = bot->GetMaxHealth() ? uint32(uint64(bot->GetHealth()) * 100 / bot->GetMaxHealth()) : 100;
        e.usesMana = bot->GetPowerType() == POWER_MANA && bot->GetMaxPower(POWER_MANA) > 0;
        e.manaPct = e.usesMana ? uint32(uint64(bot->GetPower(POWER_MANA)) * 100 / bot->GetMaxPower(POWER_MANA)) : 100;
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            e.eating = ai->HasSeatedMeal(false) || ai->HasSeatedMeal(true);

        MemberFact& m = e.fact;
        m.name = e.name;
        m.yd = bot == lead ? 0.0f : sameMap ? bot->GetDistance(lead) : 1000.0f;
        m.dz = sameMap ? bot->GetPositionZ() - lead->GetPositionZ() : 0.0f;
        m.inCombat = e.inCombat;
        m.x = bot->GetPositionX();
        m.y = bot->GetPositionY();
        m.z = bot->GetPositionZ();
        m.healer = e.healer;
        m.tank = IsTankMember(bot);
        // The pull's healer gate (DungeonRun::HealerOutOfReach): a healer's sight line to the lead bot, read fresh each tick (one LOS query a healer).
        m.healerSight = bot == lead || (e.healer && sameMap && IsWithinLOSInMapCompat(bot, lead));
        m.core = m.tank || m.healer;
        m.level = bot->GetLevel();
        // G8: the run's crowd control is a mage's Polymorph only (see RunCcHeld). No other class carries a family and no Sap is ordered (canSap stays false: a
        // rogue's pre-pull Sap trigger needs a real player master, which a bot-led run does not have).
        m.polymorph = cls == CLASS_MAGE && KnowsPolymorph(bot);
        m.cc = 0;
        m.canSap = false;
        m.offPlanMap = plan && bot->GetMapId() != plan->map;
        m.otherCopy = bot->GetMapId() == lead->GetMapId() && bot->GetInstanceId() != lead->GetInstanceId();
        m.offMapReturnable = false;         // no return teleports on ForeverCore (GM power, forced off): "not returnable" is the do-less answer
        // Progress: a cast in progress or a melee swing in progress counts as "made progress now"; no cast / swing counters exist, so a bot that is
        // only in combat is "none" (0xFFFFFFFF), which makes the wedged rules wait.
        m.actedMs = (bot->IsNonMeleeSpellCast(false) || (bot->GetVictim() && bot->HasUnitState(UNIT_STATE_MELEE_ATTACKING))) ? 0u : 0xFFFFFFFFu;
        for (Unit* attacker : bot->getAttackers())
            if (attacker && attacker->IsAlive() && !sServerFacade.IsFriendlyTo(attacker, lead))
                ++m.attackers;
        if (Pet* pet = bot->GetPet())
            if (pet->IsAlive() && pet->IsInWorld())
                for (Unit* attacker : pet->getAttackers())
                    if (attacker && attacker->IsAlive() && !sServerFacade.IsFriendlyTo(attacker, lead))
                        ++m.attackers;
        c.partyGuids.insert(bot->GetGUID());
        if (Pet* pet = bot->GetPet())
            c.partyGuids.insert(pet->GetGUID());
        if (sameMap)
            c.bots.push_back(bot);
        party.push_back(e);
    }
    // Who judges "the group can attack it": the lead, else the first living bot on its map that is not charmed.
    if (lead->IsAlive() && !lead->IsCharmed())
        c.judge = lead;
    else
        for (Player* bot : c.bots)
            if (bot->IsAlive() && !bot->IsCharmed())
            {
                c.judge = bot;
                break;
            }
    fx::RollUpParty(f, party);
}

// Both objects stand on the SAME Map object (the same instance of the same map): a null map (not placed, teleporting) is never "the same". GetMapId() alone
// equals for two copies of one dungeon, and GetDistance across them compares coordinates of different worlds.
bool SameMapObject(WorldObject const* a, WorldObject const* b)
{
    Map const* const map = a ? a->FindMap() : nullptr;
    return map && b && map == b->FindMap();
}

// ---- the rez hold's roster (N6; commands/Commander.cpp:10474 ReadRezRoster, the same reads) ----
// RezHoldPolicy.h decides from this roster whether the regroup HOLDS for a resurrection (the priest's own AI raises the dead where they lie), waits for the
// resurrector's mana, or sends `revive party`. AiPlayerbot.DungeonRunRezHold (default false on Turtle) is FORCED ON here: M6's first live test showed the
// fixed 15 s wait (kPartyRezWaitMs) releasing the dead before a priest that had to walk or drink first could cast (10 s cast + reaction). Read only while
// someone is dead, like the Turtle glue. "No resurrector" (no priest, paladin, shaman or druid, or none with the spell ready) is the policy's own SendRevive
// verdict: the practice revive stays the fallback. Group slots are read directly (the dead are not in c.bots).
void ReadRezRoster(Ctx& c, Facts& f)
{
    using RezHold::RezClass;
    static uint32 const priest[] = {20770, 10881, 10880, 2010, 2006};
    static uint32 const paladin[] = {20773, 20772, 10324, 10322, 7328};
    static uint32 const shaman[] = {20777, 20776, 20610, 20609, 2008};
    static uint32 const druid[] = {20748, 20747, 20742, 20739, 20484};
    Player* const lead = c.lead;
    Group* const group = lead->GetGroup();
    if (!group)
        return;
    std::vector<Player*> bots;
    bots.push_back(lead);
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (slot.guid != lead->GetGUID() && bots.size() < fx::kMaxMembers)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                if (h3::IsHostBot(bot) && InWorldPlayer(bot))
                    bots.push_back(bot);
    f.rezHold = true;
    std::vector<Player*> memberBots;      // parallel to f.rezMembers
    std::vector<WorldObject*> memberBody; // ... the body of a dead member (its corpse, else the bot itself: not released yet)
    for (Player* bot : bots)
    {
        RezHold::Member m;
        m.name = bot->GetName();
        m.healer = PlayerbotAI::IsHeal(bot);
        m.tank = IsTankMember(bot);
        if (!bot->IsAlive())
        {
            m.dead = true;
            Corpse* corpse = bot->GetCorpse();
            WorldObject* body = corpse ? static_cast<WorldObject*>(corpse) : static_cast<WorldObject*>(bot);
            // Review fix 5: the same Map object (the instance), not only the same map id: a body in another copy of this dungeon is as far as another map.
            m.corpseYd = SameMapObject(body, lead) ? body->GetDistance(lead) : 1000.0f;
            m.placed = true;
            m.bodyX = body->GetPositionX();
            m.bodyY = body->GetPositionY();
            m.bodyZ = body->GetPositionZ();
            if (IsResurrectRequestedCompat(bot))
                f.rezInProgress = true;         // raised: the request waits for the bot to accept it
            f.rezMembers.push_back(m);
            memberBots.push_back(bot);
            memberBody.push_back(body);
            continue;
        }
        m.inCombat = bot->IsInCombat();
        uint32 const* list = nullptr;
        uint8 const cls = bot->GetClass();
        switch (cls)
        {
            case CLASS_PRIEST: list = priest; m.rez = RezClass::Priest; break;
            case CLASS_PALADIN: list = paladin; m.rez = RezClass::Paladin; break;
            case CLASS_SHAMAN: list = shaman; m.rez = RezClass::Shaman; break;
            case CLASS_DRUID: list = druid; m.rez = RezClass::Druid; break;
            default: break;
        }
        m.blocksDruid = (cls == CLASS_PRIEST || cls == CLASS_PALADIN || cls == CLASS_SHAMAN) &&
            SameMapObject(bot, lead) && bot->GetDistance(lead) <= sPlayerbotAIConfig.sightDistance;
        uint32 spellId = 0;
        for (size_t i = 0; list && i < 5 && !spellId; ++i)
            if (bot->HasSpell(list[i]))
                spellId = list[i];
        SpellEntry const* spell = spellId ? GetSpellEntryCompat(spellId) : nullptr;
        bool ready = spell && !bot->HasSpellCooldown(spellId) && !(spell->Category && HasSpellCategoryCooldownCompat(bot, spell->Category));
        for (uint32 k = 0; ready && k < MAX_SPELL_REAGENTS; ++k)
            if (spell->Reagent[k] > 0 && !bot->HasItemCount(uint32(spell->Reagent[k]), spell->ReagentCount[k]))
                ready = false;
        if (!ready)
            m.rez = RezClass::None;
        else
        {
            m.mana = bot->GetPower(POWER_MANA);
            m.rezCost = spell->manaCost ? spell->manaCost : uint32(spell->ManaCostPercentage) * bot->GetCreateMana() / 100;
        }
        if (list)
            if (Spell* casting = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                if (SpellInfo const* info = casting->GetSpellInfo())
                    for (size_t k = 0; k < 5; ++k)
                        if (info->Id == list[k])
                            f.rezInProgress = true;
        m.placed = true;
        m.x = bot->GetPositionX();
        m.y = bot->GetPositionY();
        m.z = bot->GetPositionZ();
        f.rezMembers.push_back(m);
        memberBots.push_back(bot);
        memberBody.push_back(nullptr);
    }
    // Sight, for the hold's note: one ray per living rezzer to the body the run expects raised first (the policy's own pick).
    int const target = RezHold::PickTarget(f.rezMembers);
    WorldObject* const body = target >= 0 ? memberBody[static_cast<size_t>(target)] : nullptr;
    for (size_t i = 0; body && i < f.rezMembers.size(); ++i)
    {
        RezHold::Member& m = f.rezMembers[i];
        if (!m.dead && m.rez != RezClass::None && SameMapObject(memberBots[i], body))
        {
            ++c.R().losQueries;
            m.seesBody = memberBots[i]->IsWithinLOSInMap(body);
        }
    }
}

// ---- engaged units: the units on the attacker sets of the bots and their pets, and their victims (Commander.cpp:8860-8937) ----
void AddEngaged(Ctx& c, Unit* other, std::unordered_set<ObjectGuid>& seen, std::vector<fx::EngagedObs>& out,
    std::unordered_set<ObjectGuid> const& victims)
{
    if (!other || !other->IsAlive() || c.partyGuids.count(other->GetGUID()) || sServerFacade.IsFriendlyTo(other, c.lead) || !seen.insert(other->GetGUID()).second)
        return;
    fx::EngagedObs e;
    e.handle = c.H(other->GetGUID());
    e.entry = other->GetEntry();
    e.x = other->GetPositionX();
    e.y = other->GetPositionY();
    e.z = other->GetPositionZ();
    e.distLead = other->GetMap() == c.lead->GetMap() ? c.lead->GetDistance(other) : 1000.0f;
    e.attackable = !c.judge || c.judge->IsValidAttackTarget(other);
    e.evading = other->IsCreature() && static_cast<Creature*>(other)->IsInEvadeMode();
    e.health = other->GetHealth();
    e.maxHealth = other->GetMaxHealth();
    e.partyHits = victims.count(other->GetGUID()) != 0;
    if (Unit* victim = other->GetVictim())
        e.hitsParty = c.partyGuids.count(victim->GetGUID()) != 0;
    e.caster = other->GetMaxPower(POWER_MANA) > 0;
    e.creatureType = other->GetCreatureType();
    e.normalRank = other->IsCreature() && CreatureRankCompat(static_cast<Creature*>(other)) == 0;
    e.level = other->GetLevel();
    e.polymorphed = RunCcHeld(other);
    e.ccRemainMs = e.polymorphed ? RunCcRemainingMs(other) : 0;
    e.ccImmune = RunCcImmune(other);
    out.push_back(e);
}

void ReadEngaged(Ctx& c, Facts& f)
{
    std::unordered_set<ObjectGuid> victims;
    for (Player* bot : c.bots)
    {
        if (!bot->IsAlive())
            continue;
        if (Unit* v = bot->GetVictim())
            victims.insert(v->GetGUID());
        if (Pet* pet = bot->GetPet())
            if (pet->IsAlive() && pet->IsInWorld())
                if (Unit* v = pet->GetVictim())
                    victims.insert(v->GetGUID());
    }
    std::unordered_set<ObjectGuid> seen;
    std::vector<fx::EngagedObs> obs;
    for (Player* bot : c.bots)
    {
        if (!bot->IsAlive())
            continue;
        for (Unit* attacker : bot->getAttackers())
            AddEngaged(c, attacker, seen, obs, victims);
        AddEngaged(c, bot->GetVictim(), seen, obs, victims);
        // A unit in combat with the bot that neither attacks it nor is its victim: one that only holds it on its threat
        // list. Shadowfang Keep live run 3 (2026-11-23): Arugal 10000 on the courtyard wall (IMMUNE_TO_PC, nobody can
        // fight him) held the tank and the healer in combat at 0 threat; this read never saw him, the fight read as a stale
        // flag with nothing engaged, and "a fight lasted more than 180 s (kept on by nothing hostile engaged)" paused the
        // run twice. Read here, he is an engaged unit the group cannot attack, and UnattackableOnlyFight lets the fight go.
        for (auto const& ref : bot->GetCombatManager().GetPvECombatRefs())
            if (ref.second && !ref.second->IsSuppressedFor(bot))
                AddEngaged(c, ref.second->GetOther(bot), seen, obs, victims);
        if (Pet* pet = bot->GetPet())
            if (pet->IsAlive() && pet->IsInWorld())
            {
                for (Unit* attacker : pet->getAttackers())
                    AddEngaged(c, attacker, seen, obs, victims);
                AddEngaged(c, pet->GetVictim(), seen, obs, victims);
            }
    }
    std::vector<DungeonRun::OwnUnitFact> ownFight;
    bool ownFightRead = false;
    ai::DungeonPlan const* plan = c.rec->run.plan;
    // The fight's own units, one object lookup each (Commander.cpp:8992).
    if (c.map && plan && c.map->GetId() == plan->map)
    {
        for (std::uint64_t handle : c.rec->run.ownFightGuids)
        {
            DungeonRun::OwnUnitFact fact;
            fact.guid = handle;
            ObjectGuid guid;
            if (c.state->handles.Resolve(handle, guid))
            {
                Unit* unit = ObjectAccessor::GetUnit(*c.lead, guid);
                fact.state = !unit ? DungeonRun::OwnUnitState::Gone : !unit->IsAlive() ? DungeonRun::OwnUnitState::Dead : DungeonRun::OwnUnitState::Alive;
            }
            ownFight.push_back(fact);        // an unresolved handle stays Unknown: it never ends a step
        }
        ownFightRead = true;
    }
    size_t cut = 0;
    fx::ReadFightFacts(f, plan, obs, ownFightRead ? &ownFight : nullptr, &cut);
    c.R().listsCut += uint32(cut);
    // The members' own engaged lists (CombatDropTick): in combat only, the units on the member's and its pet's attacker sets.
    for (MemberFact& m : f.members)
    {
        if (!m.inCombat)
            continue;
        Player* bot = nullptr;
        for (Player* b : c.bots)
            if (b->GetName() == m.name)
                bot = b;
        if (!bot)
            continue;
        std::unordered_set<ObjectGuid> mseen;
        std::vector<fx::EngagedObs> mobs;
        std::unordered_set<ObjectGuid> const none;
        for (Unit* attacker : bot->getAttackers())
            AddEngaged(c, attacker, mseen, mobs, none);
        AddEngaged(c, bot->GetVictim(), mseen, mobs, none);
        if (Pet* pet = bot->GetPet())
            if (pet->IsAlive() && pet->IsInWorld())
            {
                for (Unit* attacker : pet->getAttackers())
                    AddEngaged(c, attacker, mseen, mobs, none);
                AddEngaged(c, pet->GetVictim(), mseen, mobs, none);
            }
        for (fx::EngagedObs const& e : mobs)
            if (m.engaged.size() < fx::kMaxListed)
                m.engaged.push_back(fx::EngagedFact(e));
        m.engagedRead = true;
    }
}

// ---- the guard reads (Commander.cpp:9522 ReadReachFacts, :9661 ReadGuardFacts, GuardRead::Lead only: a bot-led party has no raid) ----
void ReadGuard(Ctx& c, Facts& f)
{
    Player* const lead = c.lead;
    PlayerbotAI* leadAi = PlayerbotAIStorage::Instance().GetAI(lead);
    if (!leadAi || !lead->IsInWorld())
    {
        NotKnownField(c, "leadIsTank");
        return;
    }
    if (lead->IsAlive())
    {
        // The pull in flight: the lead's own pull is still reaching its target (the target has not come for it).
        PullStrategy* pull = PullStrategy::Get(leadAi);
        Unit* target = pull && pull->IsCommandActive() && pull->HasTarget() && pull->GetReturnStartTime() == 0 ? pull->GetTarget() : nullptr;
        if (target)
        {
            std::unordered_set<ObjectGuid> seen;
            for (Player* bot : c.bots)
            {
                if (!bot->IsAlive())
                    continue;
                for (Unit* attacker : bot->getAttackers())
                {
                    if (!attacker || !attacker->IsAlive() || !attacker->IsCreature() || sServerFacade.IsFriendlyTo(attacker, lead))
                        continue;
                    Unit* victim = attacker->GetVictim();
                    if (victim && c.partyGuids.count(victim->GetGUID()) && attacker != target &&
                        DungeonRun::ReachAggroCounts(attacker->GetDistance(target)) && seen.insert(attacker->GetGUID()).second)
                        ++c.lo.reachAggro;
                }
            }
            Unit* victim = target->GetVictim();
            c.lo.reaching = !(victim && c.partyGuids.count(victim->GetGUID()));
        }
        c.lo.isTank = IsTankMember(lead);
    }
    c.lo.guardRead = true;
    if (DungeonRun::GuardReadMode(lead->IsAlive(), c.lo.isTank, false, false) != DungeonRun::GuardRead::Lead)
        return;
    c.lo.staying = leadAi->HasStrategy("stay", BotState::BOT_STATE_COMBAT) || leadAi->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT);
    if (c.lo.staying)
    {
        ai::PositionMap& posMap = leadAi->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
        auto const stay = posMap.find("stay");
        if (stay != posMap.end() && stay->second.isSet() && stay->second.mapId == lead->GetMapId())
        {
            c.lo.stayKnown = true;
            c.lo.stayX = stay->second.x;
            c.lo.stayY = stay->second.y;
            c.lo.stayZ = stay->second.z;
        }
    }
    Unit* nearestHit = nullptr;
    float nearestHitYd = 0.0f;
    for (Unit* attacker : lead->getAttackers())
        if (attacker && attacker->IsAlive() && attacker->GetVictim() == lead && attacker->CanReachWithMeleeAutoAttack(lead))
        {
            c.lo.hit = true;
            float const yd = lead->GetDistance(attacker);
            if (attacker->IsCreature() && !sServerFacade.IsFriendlyTo(attacker, lead) && (!nearestHit || yd < nearestHitYd))
            {
                nearestHit = attacker;
                nearestHitYd = yd;
            }
        }
    if (nearestHit)
    {
        UnitFact& u = c.lo.attacker;
        u.guid = c.H(nearestHit->GetGUID());
        u.entry = nearestHit->GetEntry();
        u.distLead = nearestHitYd;
        u.evading = static_cast<Creature*>(nearestHit)->IsInEvadeMode();
        u.engaged = true;
        u.hitting = true;
        u.attackable = lead->IsValidAttackTarget(nearestHit);
        c.lo.hasAttacker = true;
    }
    // Living mobs whose victim is a party bot (or pet) other than the lead within kGuardYd of it.
    std::unordered_set<ObjectGuid> partyVictims, seen;
    for (Player* bot : c.bots)
    {
        if (!bot->IsAlive())
            continue;
        if (Unit* v = bot->GetVictim())
            partyVictims.insert(v->GetGUID());
        if (Pet* pet = bot->GetPet())
            if (pet->IsAlive() && pet->IsInWorld())
                if (Unit* v = pet->GetVictim())
                    partyVictims.insert(v->GetGUID());
    }
    auto addAttackers = [&](Unit* victim, bool healer, bool petVictim, bool tankVictim)
    {
        for (Unit* attacker : victim->getAttackers())
        {
            if (!attacker || !attacker->IsAlive() || attacker->GetVictim() != victim || sServerFacade.IsFriendlyTo(attacker, lead) ||
                !seen.insert(attacker->GetGUID()).second)
                continue;
            if (f.partyAttackers.size() >= fx::kMaxListed)
            {
                ++c.R().listsCut;
                return;
            }
            UnitFact u;
            u.guid = c.H(attacker->GetGUID());
            u.entry = attacker->GetEntry();
            u.distLead = lead->GetDistance(attacker);
            u.x = attacker->GetPositionX();
            u.y = attacker->GetPositionY();
            u.z = attacker->GetPositionZ();
            u.evading = attacker->IsCreature() && static_cast<Creature*>(attacker)->IsInEvadeMode();
            u.creatureType = attacker->GetCreatureType();
            u.normalRank = attacker->IsCreature() && CreatureRankCompat(static_cast<Creature*>(attacker)) == 0;
            u.caster = attacker->GetMaxPower(POWER_MANA) > 0;
            u.level = attacker->GetLevel();
            u.polymorphed = RunCcHeld(attacker);
            u.ccRemainMs = u.polymorphed ? RunCcRemainingMs(attacker) : 0;
            u.ccImmune = RunCcImmune(attacker);
            u.partyHits = partyVictims.count(attacker->GetGUID()) != 0;
            u.engaged = true;
            bool const melee = attacker->CanReachWithMeleeAutoAttack(victim);
            u.hitting = melee || (!IsMovingCompat(attacker) && attacker->IsNonMeleeSpellCast(false));
            u.castingAtRange = u.hitting && !melee;
            u.victimHealer = healer;
            u.victimPet = petVictim;
            u.victimTank = tankVictim;
            u.attackable = lead->IsValidAttackTarget(attacker);
            u.selectable = !HasUnitFlagCompat(attacker, UNIT_FLAG_UNINTERACTIBLE);
            f.partyAttackers.push_back(u);
        }
    };
    for (Player* bot : c.bots)
    {
        if (!bot->IsAlive() || bot == lead || bot->GetDistance(lead) > DungeonRun::kGuardYd)
            continue;
        addAttackers(bot, PlayerbotAI::IsHeal(bot), false, IsTankMember(bot));
        if (Pet* pet = bot->GetPet())
            if (pet->IsAlive() && pet->IsInWorld())
                addAttackers(pet, false, true, false);
    }
    if (Pet* pet = lead->GetPet())
        if (pet->IsAlive() && pet->IsInWorld())
            addAttackers(pet, false, true, false);
    NotKnownField(c, "partyAttackers.campIdle");
}

// ---- the lead's own state ----
void ReadLead(Ctx& c, Facts& f)
{
    Player* const lead = c.lead;
    Map* const map = c.map;
    fx::LeadObs& lo = c.lo;
    lo.alive = lead->IsAlive();
    lo.x = lead->GetPositionX();
    lo.y = lead->GetPositionY();
    lo.z = lead->GetPositionZ();
    lo.name = lead->GetName();
    lo.alliance = lead->GetTeam() == ALLIANCE;
    lo.moving = IsMovingCompat(lead);
    lo.casting = lead->IsNonMeleeSpellCast(true, false, true);
    lo.inWater = lead->IsInWater() || IsUnderwaterCompat(lead);
    bool meal = false;
    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(lead))
    {
        meal = ai->HasSeatedMeal(false) || ai->HasSeatedMeal(true);
        ai::LastMovement& lastMove = ai->GetAiObjectContext()->GetValue<ai::LastMovement&>("last movement")->Get();
        lo.moveFailed = ai::PositionMoveFailing(lastMove.positionMoveFailMs, WorldTimer::getMSTime());
        lo.moveFailReason = ai::MoveFailReasonName(lastMove.moveFailReason);
    }
    else
        NotKnownField(c, "leadMoveFailed");
    NotKnownField(c, "leadStayWalkResting");
    lo.resting = meal || lead->GetStandState() != UNIT_STAND_STATE_STAND;
    // The floor under the lead (Commander.cpp:11796): the map's height read; no navmesh feet probe on ForeverCore (leadFloorZ is the height read).
    fx::LeadGround& g = lo.ground;
    float const leadZ = lead->GetPositionZ();
    g.mapFloorZ = leadZ;
    if (map)
    {
        ++c.R().heightQueries;
        float const height = GetHeightCompat(map, lead->GetPositionX(), lead->GetPositionY(), leadZ + 0.5f);
        g.surfaceFound = height > INVALID_HEIGHT + 1.0f;
        g.surfaceZ = height;
        if (g.surfaceFound)
            g.mapFloorZ = height;
        ai::WorldPosition feet(lead->GetMapId(), lead->GetPositionX(), lead->GetPositionY(), leadZ);
        g.mmapLoaded = feet.isMmapLoaded(lead->GetInstanceId());
    }
    g.onTransport = lead->GetTransport() != nullptr;
    g.inWater = lo.inWater;
    // The group's raid icons (Group.h:365): a mark is a handle.
    if (Group* group = lead->GetGroup())
    {
        for (uint8 i = 0; i < 8; ++i)
        {
            ObjectGuid const icon = group->GetTargetIcon(i);
            lo.icons[i] = icon.IsEmpty() ? 0 : c.H(icon);
        }
        lo.iconsRead = true;
    }
    // Terrain read: any point in or under liquid is refused; a map or terrain that cannot be found refuses the point too (do less).
    uint32 const mapId = lead->GetMapId();
    uint32 const instanceId = lead->GetInstanceId();
    f.pointLiquid = [mapId, instanceId](float x, float y, float z)
    {
        // R6 (review item 2): the core's own bits rule plus the level rule, at the asked z (TbRunLiquid.h). The old test compared the status for equality, which
        // missed a bot on the floor of a lava pool (OCEAN_FLOOR ORed in). No map = refused.
        Map* const m = sMapMgr.FindMap(mapId, instanceId);
        return !m || run::liquid::PointRefusedOnMap(m, x, y, z);
    };
}

// ---- the current step ----
// The matched spawn ids of the plan's step (S5 unit rows against this instance): the boss's own spawn and the boss search read them.
struct StepSpawns
{
    std::vector<uint32> all;        // spawn ids of the members matched or near, any role
    uint32 own = 0;                 // the boss row's spawn (role boss, entry = the step's)
};

StepSpawns SpawnsOfStep(Ctx& c, ai::PlanStep const& step)
{
    StepSpawns out;
    ai::DungeonPlan const* plan = c.rec->run.plan;
    if (!plan || !c.map || c.map->GetId() != plan->map)
        return out;
    std::shared_ptr<PlanCheck const> const held = CheckPlanOnMap(plan->name, c.map, false);       // a shared copy: nothing here keeps a reference into the cache
    if (!held)
        return out;
    PlanCheck const& check = *held;
    for (UnitCheck const& u : check.units)
    {
        if (u.leg != step.leg || u.seq != step.seq || u.result == UnitResult::Missing || !u.spawnId)
            continue;
        out.all.push_back(u.spawnId);
        if (u.role == "boss" && u.entry == step.entry)
            out.own = u.spawnId;
    }
    return out;
}

fx::BossSpawnObs ObserveSpawn(Ctx& c, uint32 spawnId, bool own)
{
    fx::BossSpawnObs o;
    o.isOwn = own;
    Creature* creature = c.map->GetCreatureBySpawnId(spawnId);
    if (creature)
    {
        o.loaded = true;
        o.alive = creature->IsAlive();
        o.entry = creature->GetEntry();
        o.handle = c.H(creature->GetGUID());
        o.x = creature->GetPositionX();
        o.y = creature->GetPositionY();
        o.z = creature->GetPositionZ();
    }
    else
    {
        o.respawnPending = c.map->GetCreatureRespawnTime(spawnId) > GameTime::GetGameTime();
        if (CreatureData const* data = sObjectMgr.GetCreatureData(spawnId))
            o.gridLoaded = c.map->IsGridLoaded(data->spawnPoint.GetPositionX(), data->spawnPoint.GetPositionY());
    }
    return o;
}

// The core's StepWorld: the grid, path and sight calls behind TbRunFactsPure.h's ReadStepFacts. All counters go to the report.
// S9 (F9): the instance's doors: where an area trigger into the plan's map puts a player (the trigger's destination), once per map (Commander.cpp:10800
// InstanceDoors). A living lead bot on one of them far from every point of the plan revived in another part of the instance (DungeonRun::LeadInOtherPart).
// ForeverCore keeps the trigger rows in ObjectMgr::GetAreaTrigger(id) (the destination is its `Loc`), the ids in the DB2 store.
std::vector<DungeonRun::DoorFact> const& InstanceDoors(uint32 mapId)
{
    static std::map<uint32, std::vector<DungeonRun::DoorFact>> cache;
    auto found = cache.find(mapId);
    if (found != cache.end())
        return found->second;
    std::vector<DungeonRun::DoorFact>& doors = cache[mapId];
    for (AreaTriggerEntry const* at : sAreaTriggerStore)
    {
        AreaTriggerTeleport const* tele = at ? sObjectMgr.GetAreaTrigger(at->ID) : nullptr;
        if (!tele || tele->Loc.GetMapId() != mapId)
            continue;
        DungeonRun::DoorFact door;
        door.x = tele->Loc.GetPositionX();
        door.y = tele->Loc.GetPositionY();
        door.z = tele->Loc.GetPositionZ();
        doors.push_back(door);
    }
    return doors;
}

class CoreStepWorld : public fx::StepWorld
{
public:
    CoreStepWorld(Ctx& c, Facts const& f) : _c(c), _f(f) {}

    fx::PathObs PathFromLead(float x, float y, float z) override { return QueryPath(_c, _c.lead, x, y, z); }

    bool LeadSees(float x, float y, float z) override
    {
        ++_c.R().losQueries;
        return _c.lead->IsWithinLOS(x, y, z);
    }

    std::vector<fx::CreatureObs> CreaturesOfEntry(std::uint32_t entry, float range) override
    {
        std::vector<fx::CreatureObs> out;
        std::list<Creature*> found;
        ++_c.R().gridScans;
        _c.lead->GetCreatureListWithEntryInGrid(found, entry, range);       // Object.h:500, the same call as the Turtle glue
        for (Creature* creature : found)
        {
            if (out.size() >= 4 * fx::kMaxListed)
            {
                ++_c.R().listsCut;
                break;
            }
            ++_c.R().creaturesSeen;
            out.push_back(ObserveCreature(_c, creature));
        }
        return out;
    }

    bool OwnBoss(ai::PlanStep const& step, fx::OwnBossObs& out) override
    {
        StepSpawns const spawns = SpawnsOfStep(_c, step);
        if (!spawns.own)
            return false;
        fx::BossSpawnObs const o = ObserveSpawn(_c, spawns.own, true);
        out.known = true;
        out.loaded = o.loaded;
        out.alive = o.alive;
        out.respawnPending = o.respawnPending;
        out.entry = o.entry;
        out.handle = o.handle;
        out.x = o.x;
        out.y = o.y;
        out.z = o.z;
        if (CreatureData const* data = sObjectMgr.GetCreatureData(spawns.own))
        {
            out.rowX = data->spawnPoint.GetPositionX();
            out.rowY = data->spawnPoint.GetPositionY();
        }
        return true;
    }

    bool BossSpawns(ai::PlanStep const& step, std::vector<fx::BossSpawnObs>& out) override
    {
        StepSpawns const spawns = SpawnsOfStep(_c, step);
        for (uint32 spawnId : spawns.all)
            out.push_back(ObserveSpawn(_c, spawnId, spawnId == spawns.own));
        return !out.empty();
    }

    void HopCandidates(float range, float, float, float, std::vector<fx::HopObs>& out) override
    {
        Player* const lead = _c.lead;
        std::list<Unit*> hostiles;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(lead, lead, range);
        Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(lead, hostiles, check);
        ++_c.R().gridScans;
        Cell::VisitAllObjects(lead, searcher, range);       // Cell.h / GridNotifiers.h:1190, the same search as the Turtle glue
        size_t paths = 0;
        for (Unit* unit : hostiles)
        {
            if (!unit->IsCreature() || !unit->GetOwnerGUID().IsEmpty() || !lead->IsHostileTo(unit) || !unit->IsAlive() || unit->IsInCombat())
                continue;
            if (out.size() >= fx::kMaxHopUnits)
            {
                ++_c.R().listsCut;
                break;
            }
            fx::HopObs h;
            h.handle = _c.H(unit->GetGUID());
            h.entry = unit->GetEntry();
            h.x = unit->GetPositionX();
            h.y = unit->GetPositionY();
            h.z = unit->GetPositionZ();
            h.distLead = lead->GetDistance(unit);
            h.stealthed = IsStealthed(unit);
            h.evading = static_cast<Creature*>(unit)->IsInEvadeMode();
            h.attackable = !_c.judge || _c.judge->IsValidAttackTarget(unit);
            if (!h.stealthed && !h.evading && h.attackable)
            {
                ++_c.R().losQueries;
                h.los = IsWithinLOSInMapCompat(lead, unit);
                if (h.los && paths < fx::kMaxHopPaths)
                {
                    ++paths;
                    fx::PathObs const p = QueryPath(_c, lead, h.x, h.y, h.z);
                    h.pathRead = p.valid && p.points.size() >= 2;
                    h.noPath = p.noPath;
                    h.incomplete = p.incomplete;
                    h.shortcut = p.shortcut;
                    h.notUsingPath = p.notUsingPath;
                    h.endX = p.endX;
                    h.endY = p.endY;
                    h.endZ = p.endZ;
                    h.pathYd = p.lengthYd;
                    h.straightYd = ai::PlanDistance(lead->GetPositionX(), lead->GetPositionY(), lead->GetPositionZ(), h.x, h.y, h.z);
                }
            }
            out.push_back(h);
        }
    }

    bool GroundUnder(fx::CreatureObs const& o, float stepZ, float& groundZ) override
    {
        ++_c.R().heightQueries;
        groundZ = GetHeightCompat(_c.map, o.x, o.y, o.z, true, std::fabs(o.z - stepZ) + DungeonRun::kPackResidueZYd + 2.0f);
        return groundZ > INVALID_HEIGHT;
    }

    void HostilesNear(float range, std::vector<fx::ForwardObs>& out) override
    {
        Player* const lead = _c.lead;
        std::list<Unit*> around;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(lead, lead, range);
        Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(lead, around, check);
        ++_c.R().gridScans;
        Cell::VisitAllObjects(lead, searcher, range);
        for (Unit* unit : around)
        {
            if (!unit->IsCreature() || !unit->IsAlive() || !lead->IsHostileTo(unit) || !unit->GetOwnerGUID().IsEmpty())
                continue;
            if (out.size() >= 4 * fx::kMaxListed)
            {
                ++_c.R().listsCut;
                break;
            }
            fx::ForwardObs o;
            o.entry = unit->GetEntry();
            o.x = unit->GetPositionX();
            o.y = unit->GetPositionY();
            o.z = unit->GetPositionZ();
            o.aggroReachYd = static_cast<Creature*>(unit)->GetAttackDistance(lead);
            out.push_back(o);
        }
    }

    // ---- S9 (F9): objects, waits and task bags ----

    // wait_object: the spawned game object of the step's entry nearest to the step's point, within tasks::kEntrySearchYd (the same search as the open order:
    // never another object of the entry). `readable` says whether a "not found" proves it gone (Facts::objectReadable).
    fx::ObjectObs ObjectAt(ai::PlanStep const& step) override
    {
        fx::ObjectObs o;
        Player* const lead = _c.lead;
        ai::DungeonPlan const* plan = _c.rec->run.plan;
        float const toPoint = ai::PlanDistance(lead->GetPositionX(), lead->GetPositionY(), lead->GetPositionZ(), step.x, step.y, step.z);
        bool const reachable = RemoteCommander::EntryPointInReach(toPoint, sPlayerbotAIConfig.reactDistance);
        o.readable = lead->IsAlive() && !lead->IsBeingTeleported() && plan && lead->GetMapId() == plan->map && reachable && _c.map &&
            _c.map->IsGridLoaded(step.x, step.y);
        // A lead dead at a graveyard is on another map: the grid search would look for the plan's object around the graveyard's coordinates, not the
        // instance (and could find a spawn of the same entry there). "Not found" stays unreadable, as a dead lead's read is.
        if (!reachable || !plan || lead->GetMapId() != plan->map)
            return o;
        std::list<GameObject*> found;
        ++_c.R().gridScans;
        lead->GetGameObjectListWithEntryInGrid(found, step.entry, toPoint + tasks::kEntrySearchYd + 1.0f);
        GameObject* best = nullptr;
        float bestYd = 0.0f;
        for (GameObject* go : found)
        {
            if (!go || !go->isSpawned())
                continue;
            float const d = DistanceTo(go, step.x, step.y, step.z);
            if (d > tasks::kEntrySearchYd)
                continue;
            if (!best || d < bestYd)
            {
                best = go;
                bestYd = d;
            }
        }
        if (best)
        {
            o.found = true;
            o.coreState = uint32(best->GetGoState());
        }
        return o;
    }

    // WC: the instance's own data by id (a hold step done by data: the Disciple of Naralex's state, Mutanus's). A map without an instance
    // script answers "not known" (eventData -1: the policy pauses and says the map has none).
    bool InstanceData(std::uint32_t id, long long& out) override
    {
        InstanceMap* instance = _c.map ? _c.map->ToInstanceMap() : nullptr;
        InstanceScript* script = instance ? instance->GetInstanceScript() : nullptr;
        if (!script)
            return false;
        out = static_cast<long long>(script->GetData(id));
        return true;
    }

    // wait_clear: the living hostile creatures within `range` of the lead (HostileCreaturesAround), engaged = in a fight with a party bot or its pet.
    void ClearHostiles(float range, std::vector<fx::ClearObs>& out) override
    {
        Player* const lead = _c.lead;
        std::list<Unit*> around;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(lead, lead, range);
        Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(lead, around, check);
        ++_c.R().gridScans;
        Cell::VisitAllObjects(lead, searcher, range);
        for (Unit* unit : around)
        {
            if (!unit->IsCreature() || !unit->IsAlive() || !unit->GetOwnerGUID().IsEmpty() || !lead->IsHostileTo(unit))
                continue;
            if (out.size() >= 4 * fx::kMaxListed)
            {
                ++_c.R().listsCut;
                break;
            }
            fx::ClearObs o;
            o.x = unit->GetPositionX();
            o.y = unit->GetPositionY();
            o.z = unit->GetPositionZ();
            if (unit->IsInCombat())
            {
                if (Unit* victim = unit->GetVictim())
                    o.engaged = _c.partyGuids.count(victim->GetGUID()) != 0;
                if (!o.engaged)
                    for (Unit* attacker : unit->getAttackers())
                        if (attacker && _c.partyGuids.count(attacker->GetGUID()))
                        {
                            o.engaged = true;
                            break;
                        }
            }
            out.push_back(o);
        }
    }

    // loot / open / use_item: every party bot's bags (the step's inputs it holds, and why it cannot take a task), the task module's carrier, busy bots and
    // answer, and for a loot step after a refusal the remembered corpse. False: the lead's group is gone (nothing to read).
    bool TaskBags(ai::PlanStep const& step, fx::TaskView& out) override
    {
        Player* const lead = _c.lead;
        Group* group = lead->GetGroup();
        if (!group)
            return false;
        out.inputs.push_back(step.kind == ai::PlanStepKind::Open || step.kind == ai::PlanStepKind::UseOn ? step.item : step.entry);
        if (step.param)
            out.inputs.push_back(step.param);
        std::vector<ObjectGuid> members{ lead->GetGUID() };
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            if (slot.guid != lead->GetGUID() && members.size() < fx::kMaxMembers)
                members.push_back(slot.guid);
        for (ObjectGuid guid : members)
        {
            Player* bot = guid == lead->GetGUID() ? lead : ObjectAccessor::FindPlayer(guid);
            fx::BagObs b;
            b.fact.name = bot ? std::string(bot->GetName()) : std::string();
            b.fact.guid = GuidKeyCompat(guid);
            if (!bot)
                continue;                   // a member not in the world carries nothing the run can use; it is not listed
            if (!bot->IsAlive())
                b.fact.away = "dead";
            else if (!bot->IsInWorld() || bot->FindMap() != _c.map)
                b.fact.away = "not in the instance";
            for (uint32 item : out.inputs)
                if (item && bot->HasItemCount(item))
                    b.fact.held.push_back(item);
            b.hasStepItem = step.item && bot->HasItemCount(step.item);
            b.hasEntryItem = step.kind == ai::PlanStepKind::UseItem && bot->HasItemCount(step.entry);
            if (tasks::Busy(_c.rec->leader, guid))
                out.busyKeys.push_back(b.fact.guid);
            out.bags.push_back(b);
        }
        tasks::TaskState const state = tasks::StateOf(_c.rec->leader);
        out.taskCarrierKey = state.carrier.IsEmpty() ? 0 : GuidKeyCompat(state.carrier);
        out.reply = state.reply;
        out.replyText = state.text;
        // The remembered corpse of a loot step, read only after a refusal (Commander.cpp:12815-12834): whose round-robin turn it is, and whether the item
        // is still on it.
        DungeonRun::Run const& run = _c.rec->run;
        if (step.kind == ai::PlanStepKind::Loot && step.item && !run.lootRefusal.empty())
        {
            auto const corpse = run.corpses.find(step.entry);
            ObjectGuid guid;
            if (corpse != run.corpses.end() && corpse->second.guid && ResolveHandle(_c.rec->leader, corpse->second.guid, guid) && _c.map)
                if (Creature* body = _c.map->GetCreature(guid))
                    if (Loot* loot = body->GetLootForPlayer(lead))
                    {
                        out.corpse.checked = true;
                        for (fx::BagObs const& b : out.bags)
                            for (ObjectGuid member : members)
                                if (GuidKeyCompat(member) == b.fact.guid && member == loot->roundRobinPlayer)
                                    out.corpse.turn = b.fact.name;
                        for (LootItem const& item : loot->items)
                            out.corpse.hasItem = out.corpse.hasItem || (item.itemid == step.item && !item.is_looted);
                    }
        }
        return true;
    }

    // A task whose bot is gone: the run's order is retired by the policy's own rule, and the task module drops its task and answer.
    void RetireTask() override
    {
        DungeonRun::RetireOrder(_c.rec->run);
        tasks::Cancel(_c.rec->leader);
    }

    void NotKnown(char const* field) override { NotKnownField(_c, field); }

private:
    Ctx& _c;
    Facts const& _f;
};

void ReadStep(Ctx& c, Facts& f)
{
    DungeonRun::Run const& run = c.rec->run;
    if (DungeonRun::FactStep(run))
        c.R().stepKind = ai::PlanStepKindName(DungeonRun::FactStep(run)->kind);
    fx::LeadView lead;
    lead.x = c.lead->GetPositionX();
    lead.y = c.lead->GetPositionY();
    lead.z = c.lead->GetPositionZ();
    lead.alive = c.lead->IsAlive();
    lead.mapId = c.lead->GetMapId();
    lead.name = c.lead->GetName();
    CoreStepWorld world(c, f);
    fx::ReadStepFacts(f, run, lead, c.state->moveProbe, world);
}
}   // namespace

// One definition for every run read of a member's tank-ness (TbRunFacts.h): `PlayerbotAI::IsTank` reads the forced role bit before the kit, so a
// bit left on a class whose kit cannot tank is not a tank (SliceHybridPolicy.h).
bool IsTankMember(Player* bot)
{
    return bot && TortoiseBots::slice::hybrid::CountsAsTank(unsigned(bot->GetClass()), PlayerbotAI::IsTank(bot));
}

bool ReadFacts(RunRec& rec, uint32 nowMs, DungeonRun::Facts& f, FactsReport* report)
{
    auto const started = std::chrono::steady_clock::now();
    f = Facts();
    Ctx c;
    c.rec = &rec;
    c.nowMs = nowMs;
    c.report = report;
    if (report)
        *report = FactsReport();
    FactsTotals& totals = TotalsRw();
    ++totals.reads;
    auto finish = [&](bool readable)
    {
        FactsReport& r = c.R();
        r.leadReadable = readable;
        auto const us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
        r.costUs = uint32(us);
        r.handlesIssued = c.state ? uint32(c.state->handles.Size()) : 0;
        totals.totalUs += r.costUs;
        totals.maxUs = std::max(totals.maxUs, r.costUs);
        totals.pathQueries += r.pathQueries;
        totals.gridScans += r.gridScans;
        if (!readable)
            ++totals.unreadable;
        return readable;
    };
    c.state = &StateOf(rec.leader);
    fx::ApplyConfig(f, ReadConfig());
    c.lead = ObjectAccessor::FindPlayer(rec.leader);
    if (!InWorldPlayer(c.lead) || !rec.run.plan)
    {
        f.leadAlive = false;
        if (c.lead)
            ++c.R().membersAbsent;
        return finish(false);
    }
    c.map = c.lead->GetMap();
    bool playerInGroup = false;
    ReadParty(c, f, playerInGroup);
    if (f.dead > 0)
        ReadRezRoster(c, f);
    ReadLead(c, f);
    if (f.anyInCombat)
    {
        ReadEngaged(c, f);
        ReadGuard(c, f);
    }
    fx::ReadLeadFacts(f, c.lo);
    // S9 (F9): the group's loot method (the five 1.12 methods keep their numbers; the core's personal loot reads as -1) and whether the lead leads the group
    // (Commander.cpp:11756), and the instance's doors (a lead revived in another part of the instance, Commander.cpp:11766).
    if (Group* leadGroup = c.lead->GetGroup())
    {
        f.lootMethod = tasks::PolicyLootMethod(uint32(leadGroup->GetLootMethod()));
        f.leadLeadsGroup = leadGroup->IsLeader(c.lead->GetGUID());
    }
    if (f.leadAlive)
        f.doors = InstanceDoors(rec.run.plan->map);
    ReadStep(c, f);
    NotKnownField(c, "leadShotYd");
    NotKnownField(c, "members.actedMs(exact)");
    return finish(true);
}

uint64 HandleFor(ObjectGuid leader, ObjectGuid guid, uint32 nowMs)
{
    return StateOf(leader).handles.Handle(guid, nowMs);
}

bool ResolveHandle(ObjectGuid leader, uint64 handle, ObjectGuid& out)
{
    auto const it = States().find(leader);
    return it != States().end() && it->second.handles.Resolve(handle, out);
}

void ForgetRun(ObjectGuid leader)
{
    States().erase(leader);
}

int StepMatchedMembers(RunRec const& rec, size_t stepIndex)
{
    ai::DungeonPlan const* plan = rec.run.plan;
    if (!plan || stepIndex >= plan->steps.size())
        return -1;
    Player* lead = ObjectAccessor::FindPlayer(rec.leader);
    if (!InWorldPlayer(lead) || lead->GetMapId() != plan->map)
        return -1;
    ai::PlanStep const& step = plan->steps[stepIndex];
    std::vector<StepUnit> const& rows = UnitsOfPlan(plan->name);
    int total = 0;
    for (StepUnit const& row : rows)
        if (row.leg == step.leg && row.seq == step.seq)
            ++total;
    if (!total)
        return -1;
    std::shared_ptr<PlanCheck const> const held = CheckPlanOnMap(plan->name, lead->GetMap(), false);
    if (!held)
        return -1;
    PlanCheck const& check = *held;
    int matched = 0;
    for (UnitCheck const& u : check.units)
        if (u.leg == step.leg && u.seq == step.seq && u.result != UnitResult::Missing)
            ++matched;
    return matched;
}

FactsTotals const& Totals()
{
    return TotalsRw();
}
}
}

#endif
