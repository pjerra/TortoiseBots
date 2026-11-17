#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/BossTacticsPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include <cstdint>
#include <vector>

namespace ai
{
    // "hold kill blockers::<entry>": how many living units of the blocker entry a hold_kill row waits for (a fighting
    // Mad Voidwalker, a Portal of Madness) stand within kHoldKillRangeYd of the bot. The one grid search of the
    // hold_kill tactic, run at most once a second per bot and blocker entry: every reader (the dps target pickers, the
    // hold multiplier, the trigger and the action) goes through this value, never through a search of its own.
    class HoldKillBlockersValue : public CalculatedValue<uint32>, public Qualified
    {
    public:
        HoldKillBlockersValue(PlayerbotAI* ai) : CalculatedValue<uint32>(ai, "hold kill blockers", 1), Qualified() {}
        uint32 Calculate() override;
    };

    // "fighting blockers::<entry>::<radius>": how many living units of a gate_hold row's blocker entry within `radius`
    // yards of the bot fight (in combat and not evading: CountsAsFightingBlocker). Never the hold_kill rule, which also
    // counts an unselectable unit that never fights: an evaded second boss back in the air is out of the encounter and
    // must not hold the spared one. One grid search per bot, entry and radius a second; every gate reader (the pickers,
    // the veto and the hold action) goes through this value.
    class GateHoldFightingBlockersValue : public CalculatedValue<uint32>, public Qualified
    {
    public:
        GateHoldFightingBlockersValue(PlayerbotAI* ai)
            : CalculatedValue<uint32>(ai, "fighting blockers", 1), Qualified() {}
        uint32 Calculate() override;
    };

    // "sole kill candidates::<entry>": the living units of the entry within kHoldKillRangeYd that fight, the whole
    // fight's units of that entry (the pick rule is BossTacticsPolicy's PickSoleKill: lowest health, lowest guid on a
    // tie, among the `eligible` ones; a crowd-controlled or evading unit stays on the list, ineligible, for the area
    // veto). Never the bot's own "attackers" list, which "focus rti targets" collapses to the raid mark, so every bot
    // of the fight reads the same set. The same grid search as the blocker count, at most once a second per bot and
    // entry.
    class SoleKillCandidatesValue : public CalculatedValue<std::vector<SoleCandidate>>, public Qualified
    {
    public:
        SoleKillCandidatesValue(PlayerbotAI* ai)
            : CalculatedValue<std::vector<SoleCandidate>>(ai, "sole kill candidates", 1), Qualified() {}
        std::vector<SoleCandidate> Calculate() override;
    };

    // "kill first grid units::<entry>": every living creature of the entry within kBossTacticsEngageRange of the bot,
    // with what the kill_first grid rule needs (KillFirstGridUnit). Its reader is FindBossKillFirst: a kill_first unit
    // the bot's attackers list never shows (a healing ward on the boss, a Shade it cannot see) is still found here.
    // Never the bot's own "attackers" list, which "focus rti targets" collapses to the raid mark. One grid search per
    // bot and entry per second.
    class KillFirstGridUnitsValue : public CalculatedValue<std::vector<KillFirstGridUnit>>, public Qualified
    {
    public:
        KillFirstGridUnitsValue(PlayerbotAI* ai)
            : CalculatedValue<std::vector<KillFirstGridUnit>>(ai, "kill first grid units", 1), Qualified() {}
        std::vector<KillFirstGridUnit> Calculate() override;
    };

    // "tank post units::<entry>": every creature of the awaited entry within kTankPostSearchYd of the bot, alive or
    // dead, with the flags the tank_post arrival read needs (TankPostUnit). The same grid search as the blocker count,
    // at most once a second per bot and entry; the pick (PickTankPostUnit) is the policy's.
    class TankPostUnitsValue : public CalculatedValue<std::vector<TankPostUnit>>, public Qualified
    {
    public:
        TankPostUnitsValue(PlayerbotAI* ai)
            : CalculatedValue<std::vector<TankPostUnit>>(ai, "tank post units", 1), Qualified() {}
        std::vector<TankPostUnit> Calculate() override;
    };

    // "add crew switch::<entry>": the switch unit's life as this bot's raid has seen it in the boss's encounter
    // (SwitchUnitState: 0 NeverSeen, 1 Alive, 2 Dead). The read is the tank-post grid search within 200 yd; a unit found
    // alive marks it seen alive, and once seen alive an empty search is its death, while a unit never seen stays
    // NeverSeen. One sighting serves both crew values. The sighting lasts the encounter: the boss (found through the
    // add_crew row that names this switch unit) out of combat at full health or a new pull starts it over, while the
    // bot's own combat drop (Feign Death, death and battle rez) leaves it standing. A map or instance change starts it
    // over too, so one fight's sighting never opens the next fight's duty. The boss's encounter state is read once for
    // the instance and at most once a second, shared by every bot of the raid, never one 200 yd scan per bot.
    class AddCrewSwitchValue : public CalculatedValue<std::uint8_t>, public Qualified
    {
    public:
        AddCrewSwitchValue(PlayerbotAI* ai) : CalculatedValue<uint8>(ai, "add crew switch", 1), Qualified() {}
        uint8 Calculate() override;

    private:
        std::uint8_t seen_ = static_cast<std::uint8_t>(SwitchUnitState::NeverSeen);
        std::uint32_t mapId_ = 0;
        std::uint32_t instanceId_ = 0;
        std::uint32_t entry_ = 0;
        bool bossInCombat_ = false;
    };

    // "add crew::<boss>": AiPlayerbot.DungeonRunAddCrew's state for this bot, once the map's add_crew row for the
    // qualifier boss exists and its switch unit (`target_entry`) has been seen dead: Waiting (the key or the row is
    // off, or the switch unit still lives or was never seen: today's add duty stands), Crew (melee, tanks, and the
    // first `priority` AoE ranged bots of the raid) or NotCrew (a ranged damage dealer past the crew: it stays on the
    // boss). The switch unit's life is the "add crew switch" value's sighting, from the same grid search.
    // Written for the uint8 enum (AddCrewState): 0 Waiting, 1 Crew, 2 NotCrew.
    class AddCrewValue : public CalculatedValue<std::uint8_t>, public Qualified
    {
    public:
        AddCrewValue(PlayerbotAI* ai) : CalculatedValue<uint8>(ai, "add crew", 2), Qualified() {}
        uint8 Calculate() override;
    };

    // "add crew keeps duty::<boss>": whether this bot keeps today's add duty under the map's add_crew row, whatever
    // the switch unit's life: true for a bot the row's crew includes (melee and tanks, the first `priority` AoE ranged
    // bots of the raid) and for a map with no row to apply (or the key off); false for the ranged damage dealers past
    // the crew. The Onyxia ground phase reads it while the row still waits (add crew 0), where the crew's own ranged
    // are the only AoE ranged that keep hunting the whelps. The raid's AoE ranged bots are read through the group's
    // live members and their own AIs.
    class AddCrewKeepsDutyValue : public BoolCalculatedValue, public Qualified
    {
    public:
        AddCrewKeepsDutyValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, "add crew keeps duty", 2), Qualified() {}
        bool Calculate() override;
    };

    // "add crew rank": this bot's place among the raid's living AoE ranged damage dealers (AddCrewRank: the crew class
    // order, then the guid), or -1 when it is not one of them (a tank, a healer, a melee, a ranged class outside the
    // order). Read from the group's live members and their own AIs, on this map and instance. The two crew values read
    // it, so one raid walk serves all of them.
    class AddCrewRankValue : public CalculatedValue<std::int32_t>
    {
    public:
        AddCrewRankValue(PlayerbotAI* ai) : CalculatedValue<std::int32_t>(ai, "add crew rank", 2) {}
        std::int32_t Calculate() override;
    };

    // "add crew early::<boss>": AiPlayerbot.DungeonRunAddCrewEarly's duty for this bot under the map's add_crew row of
    // the qualifier boss (AddCrewEarlyDuty: 0 None, 1 Whelps, 2 Boss, 3 Switch). Whelps and Boss only while the row is
    // in mode `early` and the boss (as the raid fights it) flies; the crew's AoE ranged take the boss's adds, the
    // ranged past the crew the split key covers stay on the boss while the switch unit lives, and the rest of them stay
    // on the switch unit (Switch) until it dies, when every one of them takes the boss. None everywhere else: with the
    // key or the row off, with the boss on the ground, or with the switch unit never seen (the sighting is the
    // "add crew switch" value's).
    class AddCrewEarlyValue : public CalculatedValue<std::uint8_t>, public Qualified
    {
    public:
        AddCrewEarlyValue(PlayerbotAI* ai) : CalculatedValue<uint8>(ai, "add crew early", 2), Qualified() {}
        uint8 Calculate() override;
    };

    // The creature of `entry` the raid fights, or nullptr: the first live victim or attacker of a live group member on
    // the bot's map and instance (the bot's own lists when it has no group). An add_crew row names the boss's entry,
    // never the unit, so this is how a reader without the fight scan finds the boss whose row it holds.
    Unit* EngagedBossOfEntry(Player* bot, std::uint32_t entry);
}
