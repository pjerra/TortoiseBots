#include "playerbot/playerbot.h"
#include "HoldKillBlockersValue.h"
#include <cstdlib>
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "TargetValue.h"
#include "Objects/TemporarySummon.h"

using namespace ai;

namespace
{
// The qualified entry of the value ("hold kill blockers::<entry>" / "sole kill candidates::<entry>").
uint32 QualifierEntry(const std::string& qualifier)
{
    return static_cast<uint32>(std::strtoul(qualifier.c_str(), nullptr, 10));
}

// The entry's living creatures within kHoldKillRangeYd of the bot, by grid search (the core's
// AllCreaturesOfEntryInRange through the world object's entry-in-grid list).
void ScanEntry(Player* bot, uint32 entry, std::list<Creature*>& found, float range = kHoldKillRangeYd)
{
    if (entry && bot)
        bot->GetCreatureListWithEntryInGrid(found, entry, range);
}

// Whether `unit` is one of the bot's group: a member, or a pet or guardian whose owner is one. The bot alone counts
// when it runs without a group.
bool IsGroupUnit(Unit* unit, Player* bot, Group* group)
{
    if (!unit)
        return false;
    Player* const owner = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
    return owner && (owner == bot || (group && group->IsMember(owner->GetObjectGuid())));
}
}

// Living creatures of the qualifier's entry within kHoldKillRangeYd of the bot. Never the bots' "attackers" list: the
// Portal of Madness 15141 a dead Mad Servant leaves is unselectable (unit_flags 33555200) and never attacks, so it would
// never show up there. A blocker counts while it fights, or while it can never fight at all: a loose Voidwalker that has
// not woken yet is not a blocker yet, the portal is.
uint32 HoldKillBlockersValue::Calculate()
{
    std::list<Creature*> found;
    ScanEntry(bot, QualifierEntry(qualifier), found);
    uint32 count = 0;
    for (Creature* creature : found)
    {
        if (!creature->IsAlive())
            continue;
        if (CountsAsBlocker(creature->IsInCombat(), creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE)))
            ++count;
    }
    return count;
}

// The qualifier of the gate value is "<entry>::<radius>": the blocker entry and the row's search radius in yards.
uint32 GateHoldFightingBlockersValue::Calculate()
{
    std::vector<std::string> const parts = Qualified::getMultiQualifiers(qualifier, "::");
    const uint32 entry = parts.empty() ? 0 : static_cast<uint32>(std::strtoul(parts[0].c_str(), nullptr, 10));
    const float radius = parts.size() > 1 ? static_cast<float>(std::strtod(parts[1].c_str(), nullptr))
                                          : kGateHoldRadiusYd;
    std::list<Creature*> found;
    ScanEntry(bot, entry, found, radius > 0.0f ? radius : kGateHoldRadiusYd);
    uint32 count = 0;
    for (Creature* creature : found)
    {
        if (!creature->IsAlive())
            continue;
        if (CountsAsFightingBlocker(creature->IsInCombat(), creature->IsInEvadeMode(),
                creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE)))
            ++count;
    }
    return count;
}

// The one_at_a_time candidates: every living creature of the entry within kHoldKillRangeYd of the bot that fights. A
// crowd-controlled or evading one stays on the list with `eligible` false: only PickSoleKill reads that flag (the raid
// never converges on a sheep or a mob running home), while the area veto still counts it as a held unit standing in the
// pile. A Servant of a later wave, out of combat at the portal, is not on the list, so it never becomes the pick of the
// wave the raid is on. Never the bot's own "attackers" list, which "focus rti targets" collapses to the raid mark.
std::vector<SoleCandidate> SoleKillCandidatesValue::Calculate()
{
    std::vector<SoleCandidate> candidates;
    std::list<Creature*> found;
    ScanEntry(bot, QualifierEntry(qualifier), found);
    for (Creature* creature : found)
    {
        if (!creature->IsAlive() || !creature->IsInCombat())
            continue;
        SoleCandidate candidate;
        candidate.guid = creature->GetObjectGuid().GetRawValue();
        candidate.hpPct = static_cast<int>(creature->GetHealthPercent());
        candidate.inCombat = true;
        candidate.eligible = !creature->IsInEvadeMode() && !FindNonCcTargetStrategy::IsCrowdControlled(ai, creature);
        candidates.push_back(candidate);
    }
    return candidates;
}

// The kill_first grid: every living creature of the entry within kBossTacticsEngageRange of the bot, with its
// engagement with our group and the unit that owns it (owner, else creator, else the temporary summon's summoner).
// The caller decides who may be picked (KillFirstGridEligible): the owner has to be a unit of the fight, which only
// the fight knows.
std::vector<KillFirstGridUnit> KillFirstGridUnitsValue::Calculate()
{
    std::vector<KillFirstGridUnit> units;
    std::list<Creature*> found;
    ScanEntry(bot, QualifierEntry(qualifier), found, kBossTacticsEngageRange);
    Group* const group = bot->GetGroup();
    for (Creature* creature : found)
    {
        if (!creature->IsAlive())
            continue;
        KillFirstGridUnit unit;
        unit.guid = creature->GetObjectGuid().GetRawValue();
        unit.entry = creature->GetEntry();
        unit.distance = bot->GetDistance(creature);
        unit.alive = true;
        unit.evading = creature->IsInEvadeMode();
        unit.victimOurs = IsGroupUnit(creature->GetVictim(), bot, group);
        Player* const victim = dynamic_cast<Player*>(creature->GetVictim());
        unit.victimHealer = unit.victimOurs && victim && PlayerbotAI::IsHeal(victim);
        for (Unit* attacker : creature->GetAttackers())
        {
            if (!IsGroupUnit(attacker, bot, group))
                continue;
            unit.weHoldIt = true;
            if (attacker != creature->GetVictim() &&
                attacker->GetCharmerOrOwnerPlayerOrPlayerItself() != bot)
                unit.memberAttacks = true;
        }
        // A member that pulled and stopped swinging is off the attackers list but still on the threat list: a hit in
        // combat with an NPC or a stranger is neither, and stays out.
        auto const holds = [creature](Unit* who)
        {
            return who && sServerFacade.GetThreatManager(creature).getThreat(who) > 0.0f;
        };
        unit.weHoldIt = holds(bot) || holds(bot->GetPet());
        if (!unit.weHoldIt && group)
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (member && (holds(member) || holds(member->GetPet())))
                {
                    unit.weHoldIt = true;
                    break;
                }
            }
        }
        ObjectGuid summoner = creature->GetOwnerGuid();
        if (summoner.IsEmpty())
            summoner = creature->GetCreatorGuid();
        if (summoner.IsEmpty())
        {
            if (TemporarySummon* summon = dynamic_cast<TemporarySummon*>(creature))
                summoner = summon->GetSummonerGuid();
        }
        unit.owner = summoner.GetRawValue();
        units.push_back(unit);
    }
    return units;
}

// The awaited units of a tank_post row: every creature of the entry within kTankPostSearchYd of the bot, alive or
// dead (a dead one still ends the post), with the flags the arrival read needs. The same grid search as the blocker
// count; the pick is PickTankPostUnit's.
std::vector<TankPostUnit> TankPostUnitsValue::Calculate()
{
    std::vector<TankPostUnit> units;
    std::list<Creature*> found;
    ScanEntry(bot, QualifierEntry(qualifier), found, kTankPostSearchYd);
    for (Creature* creature : found)
    {
        if (!creature)
            continue;
        TankPostUnit unit;
        unit.guid = creature->GetObjectGuid().GetRawValue();
        unit.x = creature->GetPositionX();
        unit.y = creature->GetPositionY();
        unit.z = creature->GetPositionZ();
        unit.alive = creature->IsAlive();
        unit.selectable = !creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        unit.evading = creature->IsInEvadeMode();
        units.push_back(unit);
    }
    return units;
}

// AiPlayerbot.DungeonRunAddCrew: how this bot's raid has seen the row's switch unit in the boss's encounter. The
// tank-post grid search covers 200 yd and returns nothing both before the unit first appears and after its corpse is
// gone, so an empty list is no death by itself: the value remembers a sighting, a living unit marks one, and only a
// unit seen alive and then gone reads dead. The sighting lasts the encounter, not the bot's own combat: a Feign Death,
// a death and a battle rez, or any other combat drop keeps it, so a bot that saw Cindarion die still has the crew's
// duty when it comes back. It starts over only with the boss (SwitchSightingResets: out of combat at full health, or a
// new pull), a map or instance change, or the row's switch entry changing.
uint8 AddCrewSwitchValue::Calculate()
{
    const uint32 entry = QualifierEntry(qualifier);
    if (!sPlayerbotAIConfig.dungeonRunAddCrew || bot->GetMapId() != mapId_ || bot->GetInstanceId() != instanceId_ ||
        entry != entry_)
    {
        seen_ = static_cast<uint8>(SwitchUnitState::NeverSeen);
        bossInCombat_ = false;
        mapId_ = bot->GetMapId();
        instanceId_ = bot->GetInstanceId();
        entry_ = entry;
        return seen_;
    }
    // The encounter is the boss's, found through the row that gave this switch unit to the bot: its own combat state
    // says nothing about the sighting's, since the bot leaves combat long before the boss's encounter does.
    uint32 bossEntry = 0;
    bool bossKnown = false;
    bool bossAlive = false;
    bool bossInCombat = false;
    bool bossFullHealth = false;
    if (GetBossTactics().AddCrewBossOf(bot->GetMapId(), entry, bossEntry))
    {
        std::list<Creature*> found;
        ScanEntry(bot, bossEntry, found, kTankPostSearchYd);
        Creature* boss = nullptr;
        for (Creature* creature : found)
        {
            if (!creature)
                continue;
            bossKnown = true;
            if (!boss || creature->IsAlive())
                boss = creature;
        }
        if (boss)
        {
            bossAlive = boss->IsAlive();
            bossInCombat = boss->IsInCombat();
            bossFullHealth = bossAlive && boss->GetHealth() >= boss->GetMaxHealth();
        }
    }
    const bool reset = SwitchSightingResets(bossKnown, bossAlive, bossInCombat, bossFullHealth, bossInCombat_);
    if (bossKnown)
        bossInCombat_ = bossInCombat;
    if (reset)
    {
        seen_ = static_cast<uint8>(SwitchUnitState::NeverSeen);
        return seen_;
    }
    bool alive = false;
    for (const TankPostUnit& unit :
        AI_VALUE2(std::vector<TankPostUnit>, "tank post units", static_cast<int32>(entry)))
    {
        if (unit.alive)
        {
            alive = true;
            break;
        }
    }
    seen_ = static_cast<uint8>(SwitchUnitStateNext(static_cast<SwitchUnitState>(seen_), alive));
    return seen_;
}

// AiPlayerbot.DungeonRunAddCrew: the raid's AoE ranged damage dealers through the group's live members and their own
// AIs, and this bot's place among them (AddCrewRank). The switch unit's life is not read here: the crew's place
// stands while it lives, so the Onyxia ground phase can apply it before the row's switch dies.
std::int32_t AddCrewRankValue::Calculate()
{
    if (!sPlayerbotAIConfig.dungeonRunAddCrew)
        return -1;
    std::vector<AddCrewCandidate> candidates;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member || !sServerFacade.IsAlive(member) || member->GetMapId() != bot->GetMapId() ||
            member->GetInstanceId() != bot->GetInstanceId())
            continue;
        if (PlayerbotAI::IsTank(member) || PlayerbotAI::IsHeal(member))
            continue;
        PlayerbotAI* const memberAi = GET_PLAYERBOT_AI(member);
        if (!memberAi || !memberAi->IsRanged(member))
            continue;
        int const order = AddCrewClassOrder(member->GetClass());
        if (order < 0)
            continue;
        candidates.push_back(AddCrewCandidate{member->GetObjectGuid().GetRawValue(), order});
    }
    return AddCrewRank(candidates, bot->GetObjectGuid().GetRawValue());
}

// The crew's place: the row's own count, or this bot's rank among the raid's AoE ranged (the shared value).
bool AddCrewKeepsDutyValue::Calculate()
{
    if (!sPlayerbotAIConfig.dungeonRunAddCrew)
        return true;
    uint32 switchEntry = 0;
    uint8 crewSize = 0;
    if (!GetBossTactics().AddCrewOf(bot->GetMapId(), QualifierEntry(qualifier), switchEntry, crewSize))
        return true;
    return AddCrewMember(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot),
        AI_VALUE(std::int32_t, "add crew rank"), crewSize);
}

// AiPlayerbot.DungeonRunAddCrew: this bot's place once the row's switch unit is dead. The unit's life is the "add crew
// switch" value's sighting over the tank-post grid search: dead only after this bot saw it alive, so the take-off
// before the unit appears leaves the row waiting.
uint8 AddCrewValue::Calculate()
{
    if (!sPlayerbotAIConfig.dungeonRunAddCrew)
        return static_cast<uint8>(AddCrewState::Waiting);
    uint32 switchEntry = 0;
    uint8 crewSize = 0;
    if (!GetBossTactics().AddCrewOf(bot->GetMapId(), QualifierEntry(qualifier), switchEntry, crewSize))
        return static_cast<uint8>(AddCrewState::Waiting);
    const uint8 switchState = AI_VALUE2(uint8, "add crew switch", static_cast<int32>(switchEntry));
    bool const member = AI_VALUE2(bool, "add crew keeps duty", qualifier);
    return static_cast<uint8>(AddCrewStateFor(true, true, static_cast<SwitchUnitState>(switchState), member));
}

namespace ai
{
// The creature of `entry` the raid fights: a victim or an attacker of a live group member on this map and instance
// (the bot's own lists when it has no group). An add_crew row names the boss's entry, never the unit, so this is how a
// reader without the fight scan finds the boss it holds a row of. "focus rti targets" collapses the bot's own attacker
// list to the raid mark, so the group's lists are read as well.
Unit* EngagedBossOfEntry(Player* bot, uint32 entry)
{
    if (!bot || entry == 0)
        return nullptr;
    Group* const group = bot->GetGroup();
    if (!group)
    {
        if (Unit* const victim = bot->GetVictim())
            if (victim->GetEntry() == entry && sServerFacade.IsAlive(victim) && victim->IsInCombat())
                return victim;
        for (Unit* attacker : bot->GetAttackers())
            if (attacker && attacker->GetEntry() == entry && sServerFacade.IsAlive(attacker) && attacker->IsInCombat())
                return attacker;
        return nullptr;
    }
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || !member->IsInWorld() || member->GetMapId() != bot->GetMapId() ||
            member->GetInstanceId() != bot->GetInstanceId() || !sServerFacade.IsAlive(member))
            continue;
        if (Unit* const victim = member->GetVictim())
            if (victim->GetEntry() == entry && sServerFacade.IsAlive(victim) && victim->IsInCombat())
                return victim;
        for (Unit* attacker : member->GetAttackers())
            if (attacker && attacker->GetEntry() == entry && sServerFacade.IsAlive(attacker) && attacker->IsInCombat())
                return attacker;
    }
    return nullptr;
}
}

// AiPlayerbot.DungeonRunAddCrewEarly: the `early` mode's duty. The row, its mode, the switch unit's sighting (the
// "add crew switch" value: a unit this bot has not met yet reads NeverSeen and leaves today's duty standing), the
// boss's flight (the core's flight flag on the engaged boss, the same one the hover reach reads) and the split key
// (how many ranks past the crew leave the switch unit while it lives, AiPlayerbot.DungeonRunAddCrewBossWhileSwitch)
// come first; with them the crew's AoE ranged take the adds, the ranged past the crew the key covers stay on the boss,
// and the rest of them stay on the switch unit (AddCrewEarly::Switch) until it dies. The tanks, the healers and the
// melee keep their own duty there too.
uint8 AddCrewEarlyValue::Calculate()
{
    if (!sPlayerbotAIConfig.dungeonRunAddCrewEarly || !sPlayerbotAIConfig.dungeonRunAddCrew)
        return static_cast<uint8>(AddCrewEarly::None);
    const uint32 bossEntry = QualifierEntry(qualifier);
    uint32 switchEntry = 0;
    uint8 crewSize = 0;
    if (!GetBossTactics().AddCrewOf(bot->GetMapId(), bossEntry, switchEntry, crewSize) ||
        !GetBossTactics().AddCrewEarlyOf(bot->GetMapId(), bossEntry))
        return static_cast<uint8>(AddCrewEarly::None);
    const uint8 switchState = AI_VALUE2(uint8, "add crew switch", static_cast<int32>(switchEntry));
    Unit* const boss = EngagedBossOfEntry(bot, bossEntry);
    bool const airborne = boss && boss->IsFlying();
    return static_cast<uint8>(AddCrewEarlyDuty(true, true, static_cast<SwitchUnitState>(switchState), airborne,
        PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot), AI_VALUE(std::int32_t, "add crew rank"),
        crewSize, sPlayerbotAIConfig.dungeonRunAddCrewBossWhileSwitch));
}
