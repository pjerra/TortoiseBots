"""Wiring pins for RegroupReleaseProbe (who starts the fight after a `revive|party` release).

The rules and the row layout are pure and tested in tools/test_regroup_release_probe_policy.cpp
(the ten-tick window, the per-commander book, the key=value lines). This file proves
commands/Commander.cpp is wired to them: the release arms the run's window and writes tick=0, each
run tick writes one row through the policy's Book, a run's end drops its window, and nothing is
collected or written unless AiPlayerbot.RaidBossDiagnostics is 1.

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


def run_tick_body():
    start = COMMANDER.index("void TickDungeonRun(")
    return COMMANDER[start:COMMANDER.index("\n}\n", COMMANDER.index("IssueRunOrder(", start))]


class RegroupReleaseProbeWiring(unittest.TestCase):
    def test_the_release_arms_only_a_live_runs_window(self):
        body = function_body("BeginRegroupReleaseProbe")
        # Only a run keeps the window; a release outside one passes false and leaves no state.
        self.assertIn("RegroupProbeBook().Release(commanderRaw, bots, plan != nullptr)", body)
        self.assertNotIn(".Arm(", body)
        # The release row is tick=0: the boss snapshot before any run tick spends the window.
        self.assertIn("WriteRegroupProbeBosses(map, *plan, 0)", body)
        self.assertIn("WriteRegroupProbeBot(botRaw, 0)", body)
        self.assertLess(body.index("WriteRegroupProbeBosses"), body.index("WriteRegroupProbeBot(botRaw, 0)"))
        # The rows are the release's own, written from its bot list whether a window was armed or not.
        self.assertIn("for (std::uint64_t botRaw : bots)", body)

    def test_an_out_of_run_release_leaves_no_state(self):
        # The book drops the commander's window for a release outside a run (the C++ test walks
        # both cases); a run start drops whatever an earlier release left.
        self.assertIn("void Release(std::uint64_t commander, std::vector<std::uint64_t> const& bots, bool liveRun)",
                      POLICY)
        self.assertIn("else\n            End(commander);", POLICY)
        insertion = COMMANDER.index("DungeonRunSlot& stored = DungeonRuns()[commanderRaw] = slot;")
        before = COMMANDER[COMMANDER.rindex("\n\n", 0, insertion):insertion]
        self.assertIn("EndRegroupReleaseProbe(commanderRaw);", before)

    def test_the_key_gates_the_call_sites(self):
        branch = COMMANDER.index('if (word == "party")')
        block = COMMANDER[branch:COMMANDER.index("SendActionAck(handler, intent, \"party\", revived", branch)]
        # With the key off the release collects nothing and arms nothing.
        self.assertRegex(block, r"if \(sPlayerbotAIConfig\.raidBossDiagnostics\)\s+revivedRaw\.push_back\(")
        self.assertRegex(block, r"if \(sPlayerbotAIConfig\.raidBossDiagnostics\)\s+BeginRegroupReleaseProbe\(")
        # ... and a run tick never enters the probe either.
        self.assertRegex(run_tick_body(),
                         r"if \(sPlayerbotAIConfig\.raidBossDiagnostics\)\s+RegroupReleaseProbeTick\(")
        # The writer itself never bypasses the event log the row goes to.
        row = function_body("WriteRegroupProbeRow")
        self.assertIn('hasLog("bot_events.csv")', row)
        self.assertIn('"RegroupReleaseProbe"', row)
        # No new key: the probe reads the existing RaidBossDiagnostics switch only.
        self.assertNotIn("regroupReleaseProbe", COMMANDER)
        self.assertNotIn("RegroupReleaseProbe =", POLICY)

    def test_one_row_per_run_tick_inside_the_window(self):
        body = function_body("RegroupReleaseProbeTick")
        self.assertIn("book.Find(commanderRaw)", body)
        self.assertIn("state->window.Next()", body)
        # A spent window is dropped, not left armed.
        self.assertIn("book.End(commanderRaw)", body)
        # The state is per run, not one global window another commander's tick could spend.
        self.assertIn("struct Book", POLICY)
        self.assertIn("std::map<std::uint64_t, RunState> runs;", POLICY)
        self.assertNotIn("probe.commander", COMMANDER)
        # kTickRows and kHolderNames stay the policy's (the C++ test walks the bounds).
        self.assertIn("kTickRows = 10", POLICY)
        self.assertIn("kHolderNames = 5", POLICY)

    def test_the_window_dies_with_the_run(self):
        erase = function_body("EraseRunSlot")
        self.assertIn("EndRegroupReleaseProbe(commanderRaw)", erase)
        clear = function_body("EraseAllRunSlots")
        self.assertIn("ClearRegroupReleaseProbes()", clear)

    def test_the_run_tick_calls_it_before_the_ticks_own_order(self):
        body = run_tick_body()
        call = body.index("RegroupReleaseProbeTick(")
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
        self.assertIn("BossSpawnLowGuids(plan.map, step.entry)", body)
        self.assertNotIn("GetMapId(", body)
        for field in ("GetThreatManager().getThreatList()", "boss->IsInCombat()", "boss->GetVictim()",
                      "boss->IsInEvadeMode()", "GetRespawnCoord(", "boss->GetDistance("):
            self.assertIn(field, body)
        # Boss-generic: no boss entry constant anywhere in the probe.
        self.assertNotIn("14834", body)

    def test_the_map_is_read_only_from_a_checked_lead(self):
        checked = "Map* map = lead && lead->IsInWorld() ? lead->FindMap() : nullptr;"
        self.assertEqual(COMMANDER.count(checked), 2)  # the release and the run tick
        for name in ("BeginRegroupReleaseProbe", "RegroupReleaseProbeTick"):
            body = function_body(name)
            self.assertIn(checked, body)
            self.assertNotIn("->GetMap()", body)
        self.assertIn("if (!map)", function_body("WriteRegroupProbeBosses"))

    def test_the_bot_row_reads_what_the_brief_names(self):
        body = function_body("WriteRegroupProbeBot")
        for value in ('"current target"', '"attack target"', '"pull target"'):
            self.assertIn(value, body)
        self.assertIn("bot->GetVictim()", body)
        self.assertIn("bot->GetSelectionGuid()", body)
        self.assertIn("ai->GetUnit(selected)", body)
        self.assertIn("selected.GetRawValue()", body)
        self.assertIn("bot->IsInCombat()", body)
        self.assertIn("GetHostileRefManager(bot).getFirst()", body)
        self.assertIn("kHolderNames", body)
        self.assertIn("holderNames.push_back(owner->GetName())", body)
        self.assertIn("EngineStateName(", body)

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
