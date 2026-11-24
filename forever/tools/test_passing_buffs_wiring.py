"""Wiring pins for the passing-buffs sweep on ForeverCore (#158, package P2).

The service is shared with the Turtle build: its three host-specific reads go through the both-core helpers, so it can be
compiled by the slice (it is on forever/SLICE-W4.txt) and driven from the world tick. The pins read the sources and prove
the old core spellings are gone, the tick call is in the slice-only block, and the config key is shipped.

Run: python3 -m unittest forever/tools/test_passing_buffs_wiring.py
"""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SERVICE = (ROOT / "runtime/PassingBuffsService.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
SLICE = (ROOT / "forever/SLICE-W4.txt").read_text()
DIST = (ROOT / "forever/conf/aiplayerbot.conf.dist").read_text()
KEYS_DOC = (ROOT / "docs/server/config-keys.md").read_text()


class PassingBuffsWiring(unittest.TestCase):
    def test_the_service_is_on_the_slice_list(self):
        self.assertIn("runtime/PassingBuffsService.cpp\n", SLICE)

    def test_the_host_specific_reads_are_the_both_core_helpers(self):
        for spelling in ("MaNGOS::AnyFriendlyUnitInObjectRangeCheck", "VISIBILITY_OFF", "BotManager::Instance().IsRandomBot"):
            self.assertNotIn(spelling, SERVICE)
        self.assertIn("VisitFriendlyUnitsCompat(player, nearby, sPlayerbotAIConfig.sightDistance);", SERVICE)
        self.assertIn("PlayerHiddenGmCompat(player)", SERVICE)
        self.assertIn("sRandomBotFacade.IsFreeBot(bot)", SERVICE)
        self.assertIn('#include "../ai/playerbot/RandomBotFacade.h"', SERVICE)

    def test_the_world_tick_calls_the_sweep_in_the_slice_block(self):
        call = "TortoiseBots::PassingBuffsService::Instance().Update(diff);"
        at = SCRIPTS.index(call)
        guard = SCRIPTS.rfind("#ifdef TORTOISEBOTS_SLICE1", 0, at)
        self.assertGreaterEqual(guard, 0)
        self.assertNotIn("#endif", SCRIPTS[guard:at])                 # the call is inside the slice-only block
        self.assertLess(SCRIPTS.index("BotMgr::Instance().Update(diff);"), at)
        self.assertIn('#include "runtime/PassingBuffsService.h"', SCRIPTS)

    def test_the_config_key_is_shipped_and_read(self):
        self.assertIn("AiPlayerbot.PassingBuffs = 1\n", DIST)        # TB parity: on by default
        self.assertNotIn("`AiPlayerbot.PassingBuffs` | `1` | bool | no |", KEYS_DOC)
        self.assertIn("`AiPlayerbot.PassingBuffs` | `1` | bool | yes | `runtime/PassingBuffsService.cpp`", KEYS_DOC)


if __name__ == "__main__":
    unittest.main()
