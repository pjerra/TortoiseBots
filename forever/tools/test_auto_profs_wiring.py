#!/usr/bin/env python3
"""Wiring test for autonomous level-5 profession grants (TbAutoProfs).
Run:
  python3 -m unittest forever/tools/test_auto_profs_wiring.py
  python3 forever/tools/test_auto_profs_wiring.py --mutate   # must FAIL (exit non-zero)
"""
import os
import sys
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MUTATE = False


def read(rel):
    with open(os.path.join(ROOT, rel), "r", encoding="utf-8") as f:
        return f.read()


class TestAutoProfsWiring(unittest.TestCase):
    def test_scripts_hook_login_level_and_config(self):
        scripts = read("forever/src/TbScripts.cpp")
        if MUTATE:
            scripts = scripts.replace("TortoiseBots::autoprofs::Sync", "DisabledAutoProfsSync")
        self.assertIn('#include "host/TbAutoProfs.h"', scripts)
        self.assertIn("TortoiseBots::autoprofs::LoadConfig();", scripts)
        self.assertIn('TortoiseBots::autoprofs::Sync(player, "login");', scripts)
        self.assertIn('TortoiseBots::autoprofs::Sync(player, "level");', scripts)

    def test_host_checks_pool_bot_and_uses_learn_spell(self):
        host = read("forever/src/host/TbAutoProfs.cpp")
        self.assertIn("IsPoolBot", host)
        self.assertIn("LearnSpell", host)
        self.assertIn('"TortoiseBots.AutoProfessions"', host)

    def test_conf_dist_default_on(self):
        conf = read("forever/conf/tortoisebots.conf.dist")
        self.assertIn("TortoiseBots.AutoProfessions = 1", conf)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        MUTATE = True
        sys.argv.remove("--mutate")
    unittest.main()
