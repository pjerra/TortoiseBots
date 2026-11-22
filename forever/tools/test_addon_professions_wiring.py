#!/usr/bin/env python3
"""Wiring pins for the `professions` addon verb (TbAddonProfessions.{h,cpp} and TbAddonProfessionsPure.h).

Run:
  python3 -m unittest forever/tools/test_addon_professions_wiring.py
  python3 forever/tools/test_addon_professions_wiring.py --mutate
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PURE = (ROOT / "forever/src/host/TbAddonProfessionsPure.h").read_text()
H = (ROOT / "forever/src/host/TbAddonProfessions.h").read_text()
CPP = (ROOT / "forever/src/host/TbAddonProfessions.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
PLAYER_CMD = (ROOT / "forever/src/host/player/TbPlayerCmd.cpp").read_text()


def check_registers_professions(scripts_src):
    if "TortoiseBots::professions::RegisterAddonVerbsProfessions();" not in scripts_src:
        raise AssertionError("TbScripts must call RegisterAddonVerbsProfessions after addon::AddScripts")
    channel = scripts_src.index("TortoiseBots::addon::AddScripts();")
    professions = scripts_src.index("TortoiseBots::professions::RegisterAddonVerbsProfessions();")
    if not channel < professions:
        raise AssertionError("RegisterAddonVerbsProfessions must run after addon::AddScripts")


class AddonProfessionsWiring(unittest.TestCase):
    def test_scripts_wiring(self):
        self.assertIn('#include "host/TbAddonProfessions.h"', SCRIPTS)
        check_registers_professions(SCRIPTS)

    def test_header_and_registration(self):
        self.assertIn("RegisterAddonVerbsProfessions", H)
        self.assertIn('addon::RegisterVerb("professions"', CPP)
        self.assertIn('addon::AdvertiseCap("professions")', CPP)

    def test_auth_and_snapshot_pins(self):
        self.assertIn("player::Decide", CPP)
        self.assertIn("player::Tier::Tactical", CPP)
        self.assertIn("host/player/TbPlayerAuthPure.h", CPP)
        self.assertIn("ProfessionSnapshotPolicy.h", PURE)
        self.assertIn("sSkillLineAbilityStore", CPP)
        self.assertIn("sSkillLineStore", CPP)
        self.assertIn("GetClassicProfessionSkill", CPP)
        for field in ("commandedByCaller", "groupLeader", "leaderIsBot", "inRun", "IsCommandedBy",
                      "BotInRunningRun", "GetIdByGameAccount", "GetCharacterAccountIdByGuid", "MasterOf"):
            self.assertIn(field, CPP, f"facts glue lost {field}")

    def test_no_edits_to_player_cmd(self):
        for token in ("HandleProfessionsVerb", "RegisterAddonVerbsProfessions", "TbAddonProfessions"):
            self.assertNotIn(token, PLAYER_CMD)


if __name__ == "__main__":
    if "--mutate" in sys.argv:
        mutated = SCRIPTS.replace("TortoiseBots::professions::RegisterAddonVerbsProfessions();", "")
        try:
            check_registers_professions(mutated)
        except AssertionError:
            print("MUTATION CAUGHT: scripts-registration")
        else:
            print("ERROR: scripts-registration mutation survived", file=sys.stderr)
            sys.exit(1)
    else:
        unittest.main(argv=[sys.argv[0]])
