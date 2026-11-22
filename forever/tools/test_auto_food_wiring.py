#!/usr/bin/env python3
"""Wiring pins for TortoiseBots.AutoFood (login, level-up, periodic top-up and conf key).

Run:
  python3 forever/tools/test_auto_food_wiring.py
  python3 forever/tools/test_auto_food_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestAutoFoodWiring(unittest.TestCase):
    def test_scripts_wire_login_level_and_config(self):
        scripts = read("forever/src/TbScripts.cpp")
        if MUTATE:
            scripts = scripts.replace('TortoiseBots::autofood::Sync(player, "login");', "")
        self.assertIn("TortoiseBots::autofood::LoadConfig();", scripts)
        self.assertIn('TortoiseBots::autofood::Sync(player, "login");', scripts)
        self.assertIn('TortoiseBots::autofood::Sync(player, "level");', scripts)

    def test_botmgr_wires_periodic_topup(self):
        mgr = read("forever/src/TbBotMgr.cpp")
        if MUTATE:
            mgr = mgr.replace("autofood::TickTopUp(me);", "")
        self.assertIn("autofood::TickTopUp(me);", mgr)

    def test_conf_key_and_fallback_match(self):
        conf = read("forever/conf/tortoisebots.conf.dist")
        host = read("forever/src/host/TbAutoFood.cpp")
        if MUTATE:
            conf = conf.replace("TortoiseBots.AutoFood = 1", "TortoiseBots.AutoFood = 0")
        self.assertIn("TortoiseBots.AutoFood = 1", conf)
        self.assertIn('GetBoolDefault("TortoiseBots.AutoFood", true)', host)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
