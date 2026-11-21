#!/usr/bin/env python3
"""Wiring test for W7 (professions, ordered half + free-kit gather).
Run:
  python3 -m unittest forever/tools/test_w7_prof_wiring.py
  python3 forever/tools/test_w7_prof_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestW7ProfWiring(unittest.TestCase):
    def test_gather_policy_checks_lock_skill(self):
        policy = read("ai/playerbot/policy/profession/GatherNodePolicy.h")
        if MUTATE:
            policy = policy.replace("f.botSkill < f.reqSkill", "false")
        self.assertIn("f.botSkill < f.reqSkill", policy, "GatherNodePolicy must check lock skill vs bot skill")
        self.assertIn('"skill"', policy)

    def test_host_profession_uses_policies_and_core_handlers(self):
        host = read("forever/src/host/TbHostProfession.cpp")
        self.assertIn("EvaluateGatherNode", host)
        self.assertIn("StepGatherErrand", host)
        self.assertIn("EvaluateCraftForSelf", host)
        self.assertIn("StepCraftErrand", host)
        self.assertIn("HandleCastSpellOpcode", host)
        self.assertIn("SendGameObjectUse(", host)
        self.assertIn("HandleAutostoreLootItemOpcode", host)
        self.assertIn("TeachTrainerSpell", host)
        self.assertNotIn("SetSkill", host)
        self.assertNotIn("WorldDatabase.Query", host)
        self.assertNotIn("WorldDatabase.PQuery", host)

    def test_orders_and_state_wired(self):
        orders = read("forever/src/host/TbOrders.cpp")
        for verb in ('"learn-prof"', '"gather"', '"craft"'):
            self.assertIn(verb, orders, f"missing verb {verb} in TbOrders.cpp")
        if MUTATE:
            orders = orders.replace("PollOrderErrand", "DisabledPoll")
        self.assertIn("PollOrderErrand", orders, "TbOrders must poll multi-tick gather/craft errands")
        mgr = read("forever/src/TbBotMgr.cpp")
        self.assertIn("tbhost::prof::TickErrands", mgr, "TbBotMgr must tick gather/craft errands")
        state = read("forever/src/host/TbPartyState.cpp")
        self.assertIn('"prof"', state)

    def test_free_kit_config_off_by_default(self):
        conf = read("forever/conf/tortoisebots.conf.dist")
        self.assertIn("TortoiseBots.World.Professions = 0", conf)
        mgr = read("forever/src/TbBotMgr.cpp")
        self.assertIn('"TortoiseBots.World.Professions"', mgr)
        self.assertIn("tbhost::prof::TickFreeGather", mgr)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
