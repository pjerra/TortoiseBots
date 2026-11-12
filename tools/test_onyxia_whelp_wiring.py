"""Wiring pins for the Onyxia ground whelp crew (walk 2h, package W).

The decision is pure (ai/playerbot/policy/raid/OnyxiaWhelpEggPolicy.h: OnyxiaWhelpPhase, OnyxiaGroundPhase,
OnyxiaWaitingGroundDuty, OnyxiaGroundDuty) and tested in tools/test_onyxia_whelp_egg_policy.cpp. These pins prove the
live code feeds it: the scan reads the engaged boss from the raid roster ("onyxia boss", never the attacker list that
"focus rti targets" collapses to the marked unit) and sets the whelp phase from its health and the two keys it needs;
the whelp hunt, the kill action, the whelp hold and the return to the boss all read the phase instead of the raw
airborne flag; the hunt, the kill and the hold ask OnyxiaGroundDuty (so a grounded tank keeps its post); a waiting
add_crew row keeps only the crew on the ground whelps; and the key is read in LoadDungeonRunKeys, templated,
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

    def test_key_is_read_templated_and_documented(self):
        loader = body(CONFIG_CPP, "void PlayerbotAIConfig::LoadDungeonRunKeys()")
        self.assertIn('onyxiaWhelpCrewOnGround = config.GetBoolDefault("AiPlayerbot.OnyxiaWhelpCrewOnGround", false);',
                      loader)
        self.assertIn("bool onyxiaWhelpCrewOnGround = false;", CONFIG_H)
        self.assertIn("AiPlayerbot.OnyxiaWhelpCrewOnGround = 0", TEMPLATE)
        self.assertIn("AiPlayerbot.OnyxiaWhelpCrewOnGround", DOCS)
        self.assertIn("AiPlayerbot.OnyxiaWhelpCrewOnGround", BOSS_DOCS)
        self.assertIn("AiPlayerbot.OnyxiaWhelpCrewOnGround", CHANGELOG)
        # New keys ship default 0 and are documented without "recommended on".
        block_start = TEMPLATE.index("# Onyxia whelp crew on the ground.")
        block = TEMPLATE[block_start:TEMPLATE.index("AiPlayerbot.OnyxiaWhelpCrewOnGround = 0", block_start)]
        self.assertNotIn("recommended on", block)
        section = DOCS[DOCS.index("### `AiPlayerbot.OnyxiaWhelpCrewOnGround`"):]
        section = section[:section.index("### `AiPlayerbot.RaidEntryByCommander`")]
        self.assertNotIn("recommended on", section)
        self.assertIn("**Default:** `0`", section)


if __name__ == "__main__":
    unittest.main()
