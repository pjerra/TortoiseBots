"""Wiring pins for RegroupReleaseProbe (who starts the fight after a `revive|party` release).

The rules and the row layout are pure and tested in tools/test_regroup_release_probe_policy.cpp
(the ten-tick window, the per-bot living-tick budget, the key=value lines). This file proves
commands/Commander.cpp is wired to them: the release arms the window and writes tick=0, each run
tick writes one row through the policy's Window, and nothing is written unless
AiPlayerbot.RaidBossDiagnostics is 1.

Run: python3 -m unittest tools/test_regroup_release_probe_wiring.py
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
COMMANDER = (ROOT / "commands/Commander.cpp").read_text()
POLICY = (ROOT / "runtime/RegroupReleaseProbePolicy.h").read_text()


def function_body(name):
    start = COMMANDER.index(name + "(")
    # Back up to the start of the declaration line, then to its closing brace.
    start = COMMANDER.rindex("\n", 0, start) + 1
    end = COMMANDER.index("\n}\n", start)
    return COMMANDER[start:end]


class RegroupReleaseProbeWiring(unittest.TestCase):
    def test_the_release_arms_the_window(self):
        body = function_body("BeginRegroupReleaseProbe")
        self.assertIn("probe.window.Arm()", body)
        self.assertIn("probe.commander = commander->GetObjectGuid().GetRawValue()", body)
        # The release row is tick=0: the boss snapshot before any run tick spends the window.
        self.assertIn("WriteRegroupProbeBosses(lead, *plan, 0)", body)
        self.assertIn("WriteRegroupProbeBot(budget, 0)", body)

    def test_the_key_gates_every_write(self):
        release = function_body("BeginRegroupReleaseProbe")
        tick = function_body("RegroupReleaseProbeTick")
        row = function_body("WriteRegroupProbeRow")
        self.assertIn("!sPlayerbotAIConfig.raidBossDiagnostics", release)
        self.assertIn("!sPlayerbotAIConfig.raidBossDiagnostics", tick)
        # The writer itself never bypasses the event log the row goes to.
        self.assertIn('hasLog("bot_events.csv")', row)
        self.assertIn('"RegroupReleaseProbe"', row)
        # No new key: the probe reads the existing RaidBossDiagnostics switch only.
        self.assertNotIn("regroupReleaseProbe", COMMANDER)
        self.assertNotIn("RegroupReleaseProbe =", POLICY)

    def test_one_row_per_run_tick_inside_the_window(self):
        body = function_body("RegroupReleaseProbeTick")
        self.assertIn("probe.window.Spent()", body)
        self.assertIn("probe.commander != commanderRaw", body)
        self.assertIn("probe.window.Next()", body)
        # kTickRows and kBotLivingTicks stay the policy's (the C++ test walks the bounds).
        self.assertIn("kTickRows = 10", POLICY)
        self.assertIn("kBotLivingTicks = 3", POLICY)

    def test_the_run_tick_calls_it_before_the_ticks_own_order(self):
        start = COMMANDER.index("void TickDungeonRun(")
        body = COMMANDER[start:COMMANDER.index("\n}\n", COMMANDER.index("IssueRunOrder(", start))]
        call = body.index("RegroupReleaseProbeTick(commanderRaw, lead, slot.run.plan)")
        order = body.index("IssueRunOrder(")
        self.assertLess(call, order)

    def test_the_revive_party_branch_arms_it(self):
        branch = COMMANDER.index('if (word == "party")')
        block = COMMANDER[branch:COMMANDER.index("SendActionAck(handler, intent, \"party\", revived", branch)]
        # Only the bots this release put back are the probe's (release or corpse run succeeded).
        self.assertIn("revivedRaw.push_back(member->GetObjectGuid().GetRawValue())", block)
        self.assertIn("BeginRegroupReleaseProbe(order.commander, order.lead, revivedRaw)", block)
        # The single-bot `cmd revive <bot>` is not the regroup path and stays out.
        single = function_body("CommanderRevive")
        self.assertNotIn("BeginRegroupReleaseProbe", single)

    def test_the_boss_row_reads_what_the_brief_names(self):
        body = function_body("WriteRegroupProbeBosses")
        self.assertIn("ai::PlanStepKind::Boss", body)
        self.assertIn("BossSpawnLowGuids(", body)
        for field in ("GetThreatManager().getThreatList()", "boss->IsInCombat()", "boss->GetVictim()",
                      "boss->IsInEvadeMode()", "GetRespawnCoord(", "boss->GetDistance("):
            self.assertIn(field, body)
        # Boss-generic: no boss entry constant anywhere in the probe.
        self.assertNotIn("14834", body)

    def test_the_bot_row_reads_what_the_brief_names(self):
        body = function_body("WriteRegroupProbeBot")
        for value in ('"current target"', '"attack target"', '"pull target"'):
            self.assertIn(value, body)
        self.assertIn("bot->IsInCombat()", body)
        self.assertIn("EngineStateName(", body)
        self.assertIn("GetHostileRefManager(bot).getFirst()", body)
        self.assertIn("budget.Due(alive)", body)

    def test_the_probe_only_reads(self):
        for name in ("BeginRegroupReleaseProbe", "RegroupReleaseProbeTick", "WriteRegroupProbeBosses",
                     "WriteRegroupProbeBot"):
            body = function_body(name)
            for writer in ("Attack(", "AttackStart(", "SetInCombatWith(", "AddThreat(", "GetValue<Unit*>(\"current target\")->Set",
                           "RepopAtGraveyard(", "TeleportTo("):
                self.assertNotIn(writer, body)
            self.assertNotRegex(body, re.compile(r"->GetValue<[^>]+>\([^)]*\)->Set\("))


if __name__ == "__main__":
    unittest.main()
