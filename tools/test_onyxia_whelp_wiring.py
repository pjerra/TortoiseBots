"""Wiring pins for the Onyxia ground whelp crew (walk 2h, package W).

The decision is pure (ai/playerbot/policy/raid/OnyxiaWhelpEggPolicy.h: OnyxiaWhelpPhase, OnyxiaGroundPhase,
OnyxiaWaitingGroundDuty, OnyxiaGroundDuty) and tested in tools/test_onyxia_whelp_egg_policy.cpp. These pins prove the
live code feeds it: the scan reads the engaged boss from the raid roster ("onyxia boss", never the attacker list that
"focus rti targets" collapses to the marked unit) and sets the whelp phase from its health and the two keys it needs;
the whelp hunt, the kill action, the whelp hold and the return to the boss all read the phase instead of the raw
airborne flag, and the return's trigger and action take her from that same value, never the attacker list; the hunt,
the kill and the hold ask OnyxiaGroundDuty (so a grounded tank keeps its post); a waiting add_crew row keeps only the
crew on the ground whelps; and the key is read in LoadDungeonRunKeys, templated,
documented without "recommended on", and shipped with its changelog line.

Run: python3 -m unittest tools/test_onyxia_whelp_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY = (ROOT / "ai/playerbot/policy/raid/OnyxiaWhelpEggPolicy.h").read_text()
ACTIONS = (ROOT / "ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp").read_text()
ACTIONS_H = (ROOT / "ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.h").read_text()
TRIGGERS = (ROOT / "ai/playerbot/strategy/triggers/OnyxiasLairDungeonTriggers.cpp").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
CONFIG_H = (ROOT / "ai/playerbot/PlayerbotAIConfig.h").read_text()
TEMPLATE = (ROOT / "ai/playerbot/aiplayerbot.conf.dist.in").read_text()
DOCS = (ROOT / "docs/server/settings/dungeons-raids.md").read_text()
BOSS_DOCS = (ROOT / "docs/play/boss-tactics.md").read_text()
CHANGELOG = (ROOT / "CHANGELOG.md").read_text()
VALUE_CONTEXT = (ROOT / "ai/playerbot/strategy/values/ValueContext.h").read_text()


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class OnyxiaWhelpWiring(unittest.TestCase):
    def test_scan_reads_the_engaged_boss_beside_the_whelp_list(self):
        scan = body(ACTIONS, "OnyxiaWhelpScan ai::ScanOnyxiaWhelps(PlayerbotAI* ai, Player* bot)")
        # The boss comes from the engagement value, never the attacker list: "focus rti targets" collapses that list
        # to the marked unit, so a whelp mark used to hide her. The list is still scanned for whelps only.
        self.assertIn('Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));', scan)
        self.assertNotIn("unit->GetEntry() == kOnyxiaEntry", scan)
        self.assertIn("if (unit->GetEntry() != kOnyxianWhelpEntry)", scan)
        self.assertIn("IsOnyxiaBossUnit(boss->GetEntry(), sServerFacade.IsAlive(boss), boss->IsInMap(bot), "
                      "boss->IsInCombat())", scan)
        self.assertIn("scan.bossAirborne = boss->HasAura(kOnyxiaHoverAura);", scan)
        self.assertIn("bossPct = boss->GetHealthPercent();", scan)
        self.assertIn("bossFound = true;", scan)
        # The ground crew changes the add_crew's work: both keys have to be on, not just its own.
        self.assertIn("sPlayerbotAIConfig.onyxiaWhelpCrewOnGround && sPlayerbotAIConfig.onyxiaWhelpsAndEggs &&", scan)
        self.assertIn("sPlayerbotAIConfig.dungeonRunAddCrew;", scan)
        self.assertIn("scan.whelpPhase = OnyxiaWhelpPhase(scan.bossAirborne, crewOnGround, bossFound, bossPct);", scan)
        self.assertIn("bool whelpPhase = false;", ACTIONS_H)

    def test_boss_value_walks_the_raid_roster(self):
        value = body(ACTIONS, "ObjectGuid OnyxiaBossValue::Calculate()")
        # Every live member's victim and attackers, not the bot's own attacker list; a bot without a group scans itself.
        self.assertIn("Group* const group = bot->GetGroup();", value)
        self.assertIn("for (Player* member : LiveGroupMembers(group))", value)
        self.assertIn("member->GetInstanceId() != bot->GetInstanceId()", value)
        self.assertIn("IsEngagedOnyxia(member->GetVictim(), bot)", value)
        self.assertIn("for (Unit* attacker : member->GetAttackers())", value)
        self.assertIn("IsEngagedOnyxia(bot->GetVictim(), bot)", value)
        self.assertIn("for (Unit* attacker : bot->GetAttackers())", value)
        self.assertIn("return unit && IsOnyxiaBossUnit(unit->GetEntry(), sServerFacade.IsAlive(unit), "
                      "unit->IsInMap(bot),", ACTIONS)
        self.assertIn('creators["onyxia boss"] = [](PlayerbotAI* ai) { return new OnyxiaBossValue(ai); };',
                      VALUE_CONTEXT)

    def test_waiting_ground_duty_keeps_only_the_crew(self):
        duty = body(ACTIONS, "bool ai::IsOnyxiaWhelpDuty(PlayerbotAI* ai, Player* bot)")
        self.assertIn("if (crewState != static_cast<uint8>(AddCrewState::Waiting))", duty)
        self.assertIn("OnyxiaGroundPhase(airborne, crewOnGround, found, found ? boss->GetHealthPercent() : 0.0f)",
                      duty)
        self.assertIn('OnyxiaWaitingGroundDuty(today,\n        AI_VALUE2(bool, "add crew keeps duty", '
                      "static_cast<int32>(kOnyxiaEntry)));", duty)
        self.assertIn("inline bool OnyxiaGroundPhase(bool bossAirborne, bool keyOn, bool bossFound, float bossPct)",
                      POLICY)
        self.assertIn("inline bool OnyxiaWaitingGroundDuty(bool todayDuty, bool crewKeepsDuty)", POLICY)
        self.assertIn("constexpr std::uint32_t kOnyxiaHoverAura = 17131;", POLICY)
        self.assertIn("return target->HasAura(kOnyxiaHoverAura) || target->IsFlying();", TRIGGERS)

    def test_hunt_trigger_reads_the_phase_and_the_ground_duty(self):
        trigger = body(TRIGGERS, "bool OnyxiaWhelpsTrigger::IsActive()")
        self.assertIn("const bool duty = IsOnyxiaWhelpDuty(ai, bot);", trigger)
        self.assertIn("OnyxiaGroundDuty(duty, PlayerbotAI::IsTank(bot), scan.bossAirborne)", trigger)
        self.assertIn("ShouldHuntOnyxiaWhelps(true, true, scan.whelpPhase, true, explicitOrder, onWhelp, "
                      "scan.whelp != nullptr)", trigger)
        self.assertNotIn("ShouldHuntOnyxiaWhelps(true, true, scan.bossAirborne", trigger)

    def test_whelp_trigger_fires_for_the_switch(self):
        # Walk 2o round 5: the already-on-a-whelp gate kept OnyxiaKillWhelpsAction from ever running for a bot on a
        # whelp, so the ground switch was unreachable. The trigger now has a second path, read by the same rule the
        # action's switch reads; with the key off that rule answers false, so the trigger is today's exactly.
        trigger = body(TRIGGERS, "bool OnyxiaWhelpsTrigger::IsActive()")
        self.assertIn("if (ShouldHuntOnyxiaWhelps(true, true, scan.whelpPhase, true, explicitOrder, onWhelp, "
                      "scan.whelp != nullptr))\n        return true;", trigger)
        self.assertIn("return OnyxiaWhelpSwitchWanted(ai, bot, current, scan);", trigger)
        self.assertLess(trigger.index("ShouldHuntOnyxiaWhelps("),
                        trigger.index("OnyxiaWhelpSwitchWanted("))
        # The rule the trigger and the action share: the phase, the key and the order gate the switch, then
        # OnyxiaWhelpSwitch's verdict (a harmless whelp and a whelp on a tank fires, the tank's own whelp does not).
        self.assertIn("inline bool ShouldSwitchOnyxiaWhelp(bool whelpPhase, bool keyOn, bool explicitOrder, "
                      "int currentRank,", POLICY)
        self.assertIn("return whelpPhase && keyOn && !explicitOrder &&\n            OnyxiaWhelpSwitch(", POLICY)
        adapter = body(ACTIONS, "bool ai::OnyxiaWhelpSwitchWanted(")
        self.assertIn("if (!scan.whelpPhase || !sPlayerbotAIConfig.onyxiaWhelpPickTankFirst || !scan.whelp)", adapter)
        self.assertIn("current->GetEntry() != kOnyxianWhelpEntry", adapter)
        self.assertIn('AI_VALUE(uint32, "onyxia whelp switch")', adapter)
        self.assertIn("kOnyxiaWhelpSwitchMs : WorldTimer::getMSTimeDiff(lastMs, nowMs)", adapter)
        self.assertIn("ShouldSwitchOnyxiaWhelp(scan.whelpPhase, sPlayerbotAIConfig.onyxiaWhelpPickTankFirst, "
                      "explicitOrder,", adapter)
        self.assertIn("OnyxiaWhelpRankOf(current), scan.whelpRank, sinceMs, OnyxiaWhelpHitters(bot, scan.whelp),",
                      adapter)
        self.assertIn("bool OnyxiaWhelpSwitchWanted(PlayerbotAI* ai, Player* bot, Unit* current, "
                      "const OnyxiaWhelpScan& scan);", ACTIONS_H)

    def test_kill_action_reads_the_phase_and_the_ground_duty(self):
        action = body(ACTIONS, "bool OnyxiaKillWhelpsAction::Execute(Event& event)")
        self.assertIn("const bool duty = IsOnyxiaWhelpDuty(ai, bot);", action)
        self.assertIn("OnyxiaGroundDuty(duty, PlayerbotAI::IsTank(bot), scan.bossAirborne)", action)
        self.assertIn("!scan.whelpPhase || !scan.whelp", action)
        self.assertNotIn("!scan.bossAirborne", action)

    def test_hold_multiplier_reads_the_phase_and_the_ground_duty(self):
        hold = body(ACTIONS, "float OnyxiaWhelpHoldMultiplier::GetValue(Action* action)")
        self.assertIn("const bool duty = IsOnyxiaWhelpDuty(ai, bot);", hold)
        self.assertIn("OnyxiaGroundDuty(duty, PlayerbotAI::IsTank(bot), scan.bossAirborne)", hold)
        self.assertIn("return scan.whelpPhase ? 0.0f : 1.0f;", hold)

    def test_other_ranged_return_runs_in_the_phase(self):
        trigger = body(TRIGGERS, "bool OnyxiaBossDutyTrigger::IsActive()")
        self.assertIn("scan.whelpPhase, onWhelp, explicitOrder,", trigger)
        self.assertNotIn("scan.bossAirborne, onWhelp", trigger)

    def test_other_ranged_return_finds_her_beside_the_marked_whelp(self):
        # Under "focus rti targets" the attacker list holds only the raid mark, and a whelp mark used to hide her from
        # the return: the trigger and the action it fires read the engagement value the scan reads, never that list.
        trigger = body(TRIGGERS, "bool OnyxiaBossDutyTrigger::IsActive()")
        self.assertIn('Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));', trigger)
        self.assertNotIn('"attackers"', trigger)
        action = body(ACTIONS, "bool OnyxiaAttackBossAction::Execute(Event& event)")
        self.assertIn('Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));', action)
        self.assertNotIn('"attackers"', action)

    def test_no_site_passes_the_raw_airborne_flag_as_the_phase(self):
        for text in (TRIGGERS, ACTIONS):
            for line in text.splitlines():
                if ", scan.bossAirborne" not in line:
                    continue
                # The raw flag only feeds the ground-duty gate; every phase argument is scan.whelpPhase.
                self.assertIn("OnyxiaGroundDuty(", line)
        # The scan sets the phase once, from the raw flag; no site passes the raw flag as a phase.
        self.assertEqual(ACTIONS.count("scan.whelpPhase = OnyxiaWhelpPhase("), 1)
        self.assertIn("inline bool OnyxiaWhelpPhase(bool bossAirborne, bool keyOn, bool bossFound, float bossPct)",
                      POLICY)
        self.assertIn("inline bool OnyxiaGroundDuty(bool duty, bool isTank, bool bossAirborne)", POLICY)
        self.assertIn("constexpr float kOnyxiaGroundCrewPct = 40.0f;", POLICY)

    def test_early_crew_takes_the_whelps_and_the_ranged_stay_on_her(self):
        # Walk 2n (AiPlayerbot.DungeonRunAddCrewEarly): the row's `early` mode decides the duty before the crew's usual
        # phase. Whelps: the crew's AoE ranged take the whelps while Cindarion lives and she flies; Boss: the ranged
        # past the crew are not on whelp duty, and the boss duty trigger aims them at her whatever their target.
        # Switch (walk 2o round 1): the rest of them stay on him, never on today's whelp duty.
        duty = body(ACTIONS, "bool ai::IsOnyxiaWhelpDuty(PlayerbotAI* ai, Player* bot)")
        early_at = duty.index('AI_VALUE2(uint8, "add crew early", static_cast<int32>(kOnyxiaEntry))')
        self.assertGreater(early_at, duty.index('AI_VALUE2(uint8, "add crew", static_cast<int32>(kOnyxiaEntry))'),
                           "the early duty is the row's phase before the crew's usual one")
        early = duty[early_at:]
        self.assertIn("if (early == static_cast<uint8>(AddCrewEarly::Whelps))\n        return true;", early)
        self.assertIn("if (early == static_cast<uint8>(AddCrewEarly::Boss) || "
                      "early == static_cast<uint8>(AddCrewEarly::Switch))\n        return false;", early)
        # The trigger reads the same value: a boss-duty bot goes back to her, whatever its current target, unless a
        # gate_hold row holds her off (then the kill_first pick sends it back to that row's blocker).
        trigger = body(TRIGGERS, "bool OnyxiaBossDutyTrigger::IsActive()")
        self.assertIn('const bool bossDuty = AI_VALUE2(uint8, "add crew early", '
                      "static_cast<int32>(kOnyxiaEntry)) ==", trigger)
        self.assertIn("static_cast<uint8>(AddCrewEarly::Boss);", trigger)
        self.assertIn("ActiveBossRows gateRows;", trigger)
        self.assertIn("GetBossTactics().Rows(bot->GetMapId(), kOnyxiaEntry)", trigger)
        self.assertIn("row.kind == BossTacticKind::GateHold", trigger)
        self.assertIn("GateHoldsUnit(ai, gateRows, boss)", trigger)
        # Walk 2o: the rule is OnyxiaBossDutyFires and it takes her as the current target, so a bot already on her
        # keeps its rotation (re-aiming it ended the tick).
        self.assertIn("OnyxiaBossDutyFires(bossDuty, explicitOrder, bossValid, gateHeld, current && current == boss)",
                      trigger)
        # The early phase does not need a whelp: it fires on her own validity, before the return's whelp clause.
        self.assertLess(trigger.index("AddCrewEarly::Boss"), trigger.index("return ShouldReturnToOnyxia("))
        self.assertNotIn("onWhelp", trigger[trigger.index("AddCrewEarly::Boss"):trigger.index(
            "return ShouldReturnToOnyxia(")])

    def test_early_boss_duty_holds_the_assist(self):
        # Walk 2o: the assist re-pick is held while she is the bot's target on the early boss duty, so the ranged past
        # the crew stop dropping her for a whelp. The AoE actions are untouched. Round 1: it runs before the whelp
        # key's return (its gate is the add-crew key and the map), and a gate_hold row holding her off lifts it so the
        # assist may move the bot to that row's blocker.
        hold = body(ACTIONS, "float OnyxiaWhelpHoldMultiplier::GetValue(Action* action)")
        self.assertLess(hold.index("sPlayerbotAIConfig.dungeonRunAddCrewEarly"),
                        hold.index("if (!action || !sPlayerbotAIConfig.onyxiaWhelpsAndEggs)"),
                        "the boss duty hold does not depend on AiPlayerbot.OnyxiaWhelpsAndEggs")
        branch = hold[hold.index("if (action && bot->GetMapId() == kOnyxiaMapId")
                      :hold.index("if (!action || !sPlayerbotAIConfig.onyxiaWhelpsAndEggs)")]
        self.assertIn("sPlayerbotAIConfig.dungeonRunAddCrewEarly", branch)
        self.assertIn('AI_VALUE2(uint8, "add crew early", static_cast<int32>(kOnyxiaEntry))', branch)
        self.assertIn("static_cast<uint8>(AddCrewEarly::Boss)", branch)
        self.assertIn('Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));', branch)
        self.assertIn("ActiveBossRows gateRows;", branch)
        self.assertIn("row.kind == BossTacticKind::GateHold", branch)
        self.assertIn("OnyxiaBossDutyHoldsAssist(bossDuty, GateHoldsUnit(ai, gateRows, boss),", branch)
        self.assertIn('AI_VALUE(Unit*, "current target") == boss', branch)
        self.assertIn('!AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty())', branch)
        self.assertIn("return 0.0f;", branch)
        self.assertNotIn("DpsAoeAction", branch)
        self.assertIn("inline bool OnyxiaBossDutyHoldsAssist(bool bossDuty, bool gateHeld, bool onBoss, "
                      "bool explicitOrder)", POLICY)
        self.assertIn("return bossDuty && !gateHeld && onBoss && !explicitOrder;", POLICY)

    def test_ground_whelp_pick_reads_the_victim(self):
        # Walk 2o (AiPlayerbot.OnyxiaWhelpPickTankFirst): on the ground the scan ranks a pickable whelp by its victim
        # and keeps the better one; the air keeps the nearest pick.
        scan = body(ACTIONS, "OnyxiaWhelpScan ai::ScanOnyxiaWhelps(PlayerbotAI* ai, Player* bot)")
        self.assertIn("const bool pickTankFirst = sPlayerbotAIConfig.onyxiaWhelpPickTankFirst && bossFound && "
                      "!scan.bossAirborne;", scan)
        self.assertIn("const int rank = OnyxiaWhelpRankOf(unit);", scan)
        # Walk 2o round 5: among the whelps on a tank the lowest health comes first, then the fewest crew hitters,
        # then the distance; the other ranks keep the distance. The crowd read is paid only by the tank rank.
        self.assertIn("const int hitters = rank == 0 ? OnyxiaWhelpHitters(bot, unit) : 0;", scan)
        self.assertIn("OnyxiaWhelpPickBetter(rank, healthPct, hitters, dist, bestRank, bestPct, bestHitters,\n"
                      "                bestDist)", scan)
        self.assertIn("bestHitters = hitters;", scan)
        self.assertIn("pickedRank = rank;", scan)
        self.assertIn("scan.whelpRank = pickedRank;", scan)
        # The tank rank's pick is its lowest health (then its fewest hitters): the one crowded pick a switch may take.
        self.assertIn("scan.whelpLowestHp = scan.whelp && pickedRank == 0;", scan)
        pick = body(POLICY, "inline bool OnyxiaWhelpPickBetter(")
        self.assertIn("if (rankA != rankB)\n            return rankA < rankB;", pick)
        self.assertIn("if (rankA == 0)", pick)
        self.assertIn("if (healthPctA < healthPctB)", pick)
        self.assertIn("if (hittersA != hittersB)", pick)
        self.assertIn("return distA < distB;", pick)
        # The range and egg filters stand in front of both picks.
        self.assertLess(scan.index("IsOnyxiaWhelpPickable(true, dist, nearEgg)"), scan.index("pickTankFirst)"))
        # The rank read is one helper so the pick and the switch's current-whelp read cannot disagree.
        rank_of = body(ACTIONS, "int ai::OnyxiaWhelpRankOf(Unit* whelp)")
        self.assertIn("Unit* const victim = whelp ? whelp->GetVictim() : nullptr;", rank_of)
        self.assertIn("victim->GetTypeId() == TYPEID_PLAYER", rank_of)
        self.assertIn("OnyxiaWhelpVictimRank(victimPlayer && PlayerbotAI::IsTank(victimPlayer),", rank_of)
        self.assertIn("victimPlayer && PlayerbotAI::IsHeal(victimPlayer))", rank_of)
        self.assertIn("inline int OnyxiaWhelpVictimRank(bool victimTank, bool victimHealer)", POLICY)
        self.assertIn("return victimTank ? 0 : victimHealer ? 1 : 2;", POLICY)
        self.assertIn("inline bool OnyxiaWhelpPickBetter(int rankA, float healthPctA, int hittersA, float distA,",
                      POLICY)
        # Walk 2o round 4: the kill action no longer keeps any whelp. Behind the same key, on the ground, a whelp
        # eating neither a tank nor a healer gives way to the scan's ranked pick; the key off or airborne path is the
        # old early-out (scan.whelpRank is -1 there), so the module behaves exactly as before.
        action = body(ACTIONS, "bool OnyxiaKillWhelpsAction::Execute(Event& event)")
        self.assertIn("if (current && sServerFacade.IsAlive(current) && current->GetEntry() == "
                      "kOnyxianWhelpEntry)\n    {", action)
        self.assertIn("if (!OnyxiaWhelpSwitchWanted(ai, bot, current, scan))\n            return false;", action)
        self.assertIn("const bool switched = Attack(bot, scan.whelp);", action)
        self.assertIn('SET_AI_VALUE(uint32, "onyxia whelp switch", WorldTimer::getMSTime());', action)
        self.assertIn("return switched;", action)
        self.assertIn("bool OnyxiaWhelpSwitchWanted(", ACTIONS_H)
        self.assertIn("int OnyxiaWhelpRankOf(Unit* whelp);", ACTIONS_H)
        self.assertIn("int OnyxiaWhelpHitters(Player* bot, Unit* whelp);", ACTIONS_H)
        # The switch rule and its hysteresis constants, as the truth table in tools/test_onyxia_whelp_egg_policy.cpp
        # reads them.
        self.assertIn("inline bool OnyxiaWhelpSwitch(int currentRank, int candidateRank, "
                      "std::uint32_t msSinceLastSwitch,", POLICY)
        self.assertIn("if (currentRank < 2)  // never away from a whelp eating a tank or a healer\n            "
                      "return false;", POLICY)
        self.assertIn("if (candidateRank < 0 || candidateRank > 1)", POLICY)
        self.assertIn("if (msSinceLastSwitch < kOnyxiaWhelpSwitchMs)\n            return false;", POLICY)
        self.assertIn("if (hittersOnCandidate >= kOnyxiaWhelpSwitchHitters && !(candidateRank == 0 && "
                      "candidateLowestHp))", POLICY)
        self.assertIn("constexpr std::uint32_t kOnyxiaWhelpSwitchMs = 4000;", POLICY)
        self.assertIn("constexpr int kOnyxiaWhelpSwitchHitters = 2;", POLICY)
        # The crowd read: the group's crew bots on the candidate (no tanks, no healers), deduped by owner.
        hitters = body(ACTIONS, "int ai::OnyxiaWhelpHitters(Player* bot, Unit* whelp)")
        self.assertIn("for (Unit* attacker : whelp->GetAttackers())", hitters)
        self.assertIn("group->IsMember(player->GetObjectGuid())", hitters)
        self.assertIn("counted.insert(player->GetObjectGuid().GetRawValue()).second", hitters)
        self.assertIn("PlayerbotAI::IsTank(player) || PlayerbotAI::IsHeal(player)", hitters)
        # The 4 s latch is the bot's own manual value.
        self.assertIn("class OnyxiaWhelpSwitchValue : public ManualSetValue<uint32>", ACTIONS_H)
        self.assertIn('ManualSetValue<uint32>(ai, 0, "onyxia whelp switch")', ACTIONS_H)
        self.assertIn('creators["onyxia whelp switch"] = [](PlayerbotAI* ai) { return new OnyxiaWhelpSwitchValue(ai); '
                      '};', VALUE_CONTEXT)

    def test_key_is_read_templated_and_documented(self):
        loader = body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()")
        self.assertIn('onyxiaWhelpCrewOnGround = config.GetBoolDefault("AiPlayerbot.OnyxiaWhelpCrewOnGround", false);',
                      loader)
        self.assertIn("bool onyxiaWhelpCrewOnGround = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.OnyxiaWhelpCrewOnGround = 0", TEMPLATE)
        self.assertIn("AiPlayerbot.OnyxiaWhelpCrewOnGround", DOCS)
        self.assertIn("AiPlayerbot.OnyxiaWhelpCrewOnGround", BOSS_DOCS)
        self.assertIn("AiPlayerbot.OnyxiaWhelpCrewOnGround", CHANGELOG)
        # Walk 2n's key: read in the same loader, default 0, recommended on, documented and in the changelog.
        self.assertIn('config.GetIntDefault("AiPlayerbot.DungeonRunAddCrewEarly", 0)', loader)
        self.assertIn("uint32 dungeonRunAddCrewEarly = 0;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.DungeonRunAddCrewEarly = 0$")
        self.assertIn("AiPlayerbot.DungeonRunAddCrewEarly", DOCS)
        self.assertIn("DungeonRunAddCrewEarly", BOSS_DOCS)
        self.assertIn("DungeonRunAddCrewEarly", CHANGELOG)
        # New keys ship default 0 and are documented without "recommended on".
        block_start = TEMPLATE.index("# Onyxia whelp crew on the ground.")
        block = TEMPLATE[block_start:TEMPLATE.index("AiPlayerbot.OnyxiaWhelpCrewOnGround = 0", block_start)]
        self.assertNotIn("recommended on", block)
        section = DOCS[DOCS.index("### `AiPlayerbot.OnyxiaWhelpCrewOnGround`"):]
        section = section[:section.index("### `", 4)]
        self.assertNotIn("recommended on", section)
        self.assertIn("**Default:** `0`", section)
        # Walk 2o's ground pick: read in the same loader, default 0, recommended on, documented and in the changelog.
        self.assertIn('onyxiaWhelpPickTankFirst = config.GetBoolDefault("AiPlayerbot.OnyxiaWhelpPickTankFirst", '
                      "false);", loader)
        self.assertIn("bool onyxiaWhelpPickTankFirst = false;", CONFIG_H)
        self.assertRegex(TEMPLATE, r"(?m)^AiPlayerbot\.OnyxiaWhelpPickTankFirst = 0$")
        block_start = TEMPLATE.index("# Onyxia whelp pick on the ground")
        block = TEMPLATE[block_start:TEMPLATE.index("AiPlayerbot.OnyxiaWhelpPickTankFirst = 0", block_start)]
        self.assertIn("recommended on", block)
        self.assertIn("### `AiPlayerbot.OnyxiaWhelpPickTankFirst`", DOCS)
        section = DOCS[DOCS.index("### `AiPlayerbot.OnyxiaWhelpPickTankFirst`"):]
        section = section[:section.index("### `", 4)]
        self.assertIn("recommended on", section)
        self.assertIn("**Default:** `0`", section)
        self.assertIn("AiPlayerbot.OnyxiaWhelpPickTankFirst", BOSS_DOCS)
        self.assertIn("OnyxiaWhelpPickTankFirst", CHANGELOG)


if __name__ == "__main__":
    unittest.main()
