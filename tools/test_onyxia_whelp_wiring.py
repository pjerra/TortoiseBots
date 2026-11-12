"""Wiring pins for the Onyxia ground whelp crew (walk 2h, package W).

The decision is pure (ai/playerbot/policy/raid/OnyxiaWhelpEggPolicy.h: OnyxiaWhelpPhase, OnyxiaGroundDuty) and tested in
tools/test_onyxia_whelp_egg_policy.cpp. These pins prove the live code feeds it: the scan sets the whelp phase from the
boss's health and the two keys it needs; the whelp hunt, the kill action, the whelp hold and the return to the boss all
read the phase instead of the raw airborne flag; the hunt, the kill and the hold ask OnyxiaGroundDuty (so a grounded tank
keeps its post); and the key is read in LoadDungeonRunKeys, templated, documented without "recommended on", and shipped
with its changelog line.

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


def body(text, start, end="\n}\n"):
    i = text.index(start)
    return text[i:text.index(end, i + len(start))]


class OnyxiaWhelpWiring(unittest.TestCase):
    def test_scan_sets_the_whelp_phase_from_the_boss_health(self):
        scan = body(ACTIONS, "OnyxiaWhelpScan ai::ScanOnyxiaWhelps(PlayerbotAI* ai, Player* bot)")
        self.assertIn('if (unit->GetEntry() == kOnyxiaEntry)', scan)
        self.assertIn("scan.bossAirborne = unit->HasAura(17131);", scan)
        self.assertIn("bossFound = true;", scan)
        self.assertIn("bossPct = unit->GetHealthPercent();", scan)
        # The ground crew changes the add_crew's work: both keys have to be on, not just its own.
        self.assertIn("sPlayerbotAIConfig.onyxiaWhelpCrewOnGround && sPlayerbotAIConfig.onyxiaWhelpsAndEggs &&", scan)
        self.assertIn("sPlayerbotAIConfig.dungeonRunAddCrew;", scan)
        self.assertIn("scan.whelpPhase = OnyxiaWhelpPhase(scan.bossAirborne, crewOnGround, bossFound, bossPct);", scan)
        self.assertIn("bool whelpPhase = false;", ACTIONS_H)

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
