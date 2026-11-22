#!/usr/bin/env python3
"""Wiring pins for the `choice` addon verb (TbAddonChoice.{h,cpp} and TbAddonChoicePure.h).

Run:
  python3 -m unittest forever/tools/test_addon_choice_wiring.py
  python3 forever/tools/test_addon_choice_wiring.py --mutate
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PURE = (ROOT / "forever/src/host/TbAddonChoicePure.h").read_text()
H = (ROOT / "forever/src/host/TbAddonChoice.h").read_text()
CPP = (ROOT / "forever/src/host/TbAddonChoice.cpp").read_text()
WARLOCK_STRAT = (ROOT / "ai/playerbot/strategy/warlock/WarlockStrategy.cpp").read_text()
PLAYER_CMD = (ROOT / "forever/src/host/player/TbPlayerCmd.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()


def check_registers_choice(scripts_src):
    if "TortoiseBots::choice::RegisterAddonVerbsChoice();" not in scripts_src:
        raise AssertionError("TbScripts must call RegisterAddonVerbsChoice after addon::AddScripts")
    channel = scripts_src.index("TortoiseBots::addon::AddScripts();")
    choice = scripts_src.index("TortoiseBots::choice::RegisterAddonVerbsChoice();")
    if not channel < choice:
        raise AssertionError("RegisterAddonVerbsChoice must run after addon::AddScripts")


class AddonChoiceWiring(unittest.TestCase):
    def test_scripts_wiring(self):
        self.assertIn('#include "host/TbAddonChoice.h"', SCRIPTS)
        check_registers_choice(SCRIPTS)

    def test_header_and_registration(self):
        self.assertIn("RegisterAddonVerbsChoice", H)
        self.assertIn('addon::RegisterVerb("choice"', CPP)
        self.assertIn('addon::AdvertiseCap("choice")', CPP)

    def test_store_and_auth_pins(self):
        self.assertIn('"manual saved string"', CPP)
        self.assertIn("ai::ChoiceOrderKey", CPP)
        self.assertIn("sPlayerbotDbStore.Save(ai)", CPP)
        self.assertIn("player::Decide", CPP)
        self.assertIn("player::Tier::Owner", CPP)
        self.assertIn("host/player/TbPlayerAuthPure.h", CPP)
        for field in ("commandedByCaller", "groupLeader", "leaderIsBot", "inRun", "IsCommandedBy",
                      "BotInRunningRun", "GetIdByGameAccount", "GetCharacterAccountIdByGuid", "MasterOf"):
            self.assertIn(field, CPP, f"facts glue lost {field}")

    def test_choice_pet_row_owned_by_pet_auto(self):
        # The stored `choice pet` order summons only through a strategy that
        # carries the "choice pet" trigger row. The spec-pet strategies own
        # one, but they are never added once "pet auto" evicts its "pet"
        # sibling, so "pet auto" must carry the same row above its ladder.
        body = WARLOCK_STRAT.split("void WarlockAutoPetStrategy::InitNonCombatTriggers")[1]
        body = body.split("void WarlockManualPetStrategy::InitNonCombatTriggers")[0]
        self.assertIn('"choice pet"', body)
        self.assertLess(body.index('"choice pet"'), body.index('"auto pet imp"'))

    def test_no_edits_to_player_cmd(self):
        for token in ("HandleChoiceVerb", "RegisterAddonVerbsChoice", "TbAddonChoice"):
            self.assertNotIn(token, PLAYER_CMD)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        mutated = SCRIPTS.replace("TortoiseBots::choice::RegisterAddonVerbsChoice();", "")
        try:
            check_registers_choice(mutated)
        except AssertionError:
            print("MUTATION CAUGHT: scripts-registration")
        else:
            print("ERROR: scripts-registration mutation survived", file=sys.stderr)
            sys.exit(1)
    else:
        unittest.main(argv=[sys.argv[0]])
