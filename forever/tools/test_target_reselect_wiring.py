#!/usr/bin/env python3
"""Wiring pins for combat target re-selection (TBF #153, combat half).

A bot in combat with no current target runs `select new target` every tick. That action's
only way to acquire an attacker is the dps/tank assist pick, and both assist strategies
define combat triggers only: asking the non-combat engine never finds them, so the action
cleared the target and returned, and the bot sat targetless next to a live attacker.

Run:
  python3 -m unittest forever/tools/test_target_reselect_wiring.py
  python3 forever/tools/test_target_reselect_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestTargetReselectWiring(unittest.TestCase):
    def test_assist_pick_reads_the_combat_engine(self):
        src = read("ai/playerbot/strategy/actions/ChooseTargetActions.cpp")
        if MUTATE:
            src = src.replace('HasStrategy("dps assist", BotState::BOT_STATE_COMBAT)',
                              'HasStrategy("dps assist", BotState::BOT_STATE_NON_COMBAT)')
            src = src.replace('HasStrategy("tank assist", BotState::BOT_STATE_COMBAT)',
                              'HasStrategy("tank assist", BotState::BOT_STATE_NON_COMBAT)')
        self.assertIn('HasStrategy("dps assist", BotState::BOT_STATE_COMBAT)', src)
        self.assertIn('HasStrategy("tank assist", BotState::BOT_STATE_COMBAT)', src)
        self.assertNotIn('HasStrategy("dps assist", BotState::BOT_STATE_NON_COMBAT)', src)
        self.assertNotIn('HasStrategy("tank assist", BotState::BOT_STATE_NON_COMBAT)', src)

    def test_assist_strategies_are_combat_only(self):
        # The pinned state above is the one the strategies are actually added to.
        dps = read("ai/playerbot/strategy/generic/DpsAssistStrategy.cpp")
        tank = read("ai/playerbot/strategy/generic/TankAssistStrategy.cpp")
        if MUTATE:
            dps = dps.replace("void DpsAssistStrategy::InitCombatTriggers",
                              "void DpsAssistStrategy::InitNonCombatTriggers")
            tank = tank.replace("void TankAssistStrategy::InitCombatTriggers",
                                "void TankAssistStrategy::InitNonCombatTriggers")
        self.assertIn("void DpsAssistStrategy::InitCombatTriggers", dps)
        self.assertIn('"not dps target active"', dps)
        self.assertNotIn("InitNonCombatTriggers", dps)
        self.assertIn("void TankAssistStrategy::InitCombatTriggers", tank)
        self.assertIn('"tank assist"', tank)
        self.assertNotIn("InitNonCombatTriggers", tank)

    def test_the_assist_actions_target_the_pick(self):
        actions = read("ai/playerbot/strategy/actions/ChooseTargetActions.h")
        if MUTATE:
            actions = actions.replace('GetTargetName() override { return "dps target"; }',
                                      'GetTargetName() override { return "self target"; }')
            actions = actions.replace('GetTargetName() override { return "tank target"; }',
                                      'GetTargetName() override { return "self target"; }')
        self.assertIn('GetTargetName() override { return "dps target"; }', actions)
        self.assertIn('GetTargetName() override { return "tank target"; }', actions)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
